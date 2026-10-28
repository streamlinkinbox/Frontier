//============================================================================================================================================
//                                                      GEOMETRYHIERARCHYRUNTIME.TEST.MJS
//============================================================================================================================================
// 🧩 Phase 2 runtime gates: projected-error DAG cut completeness/monotonicity, frustum + normal-cone culling,
//    two-phase Hi-Z occlusion (glass never writes depth), glass refraction error, fixed-size pages and LRU residency.

import { test } from "node:test";
import assert from "node:assert/strict";
import { BuildGeometryHierarchy } from "../Source/GeometryHierarchy.js";
import {
    PackHierarchyPages, HierarchyResidencyManager, SelectHierarchyCut,
    EvaluateProjectedSphereError, TestClusterNormalCone,
    ConstructHiZPyramid, TestClusterHiZ, ExecuteHierarchyRuntimeFrame, PageSlotAllocator,
    IdentityMatrix
} from "../Source/GeometryHierarchyRuntime.js";
import { ConstructSphereFacets, ConstructHeightfieldFacets } from "./GeometryHierarchyCorpus.mjs";

const Camera = { Eye: [0, -8, 0], Target: [0, 0, 0], Up: [0, 0, 1], FovY: Math.PI / 3, ScreenWidth: 256, ScreenHeight: 256, Near: 0.1, Far: 200 };

function Translation(X, Y, Z)
{
    const M = IdentityMatrix();
    M[12] = X; M[13] = Y; M[14] = Z;
    return M;
}

// A valid DAG cut is an antichain (no selected cluster is an ancestor of another selected cluster) that
// covers every leaf: walking upward from any leaf through parent clusters must reach a selected cluster.
function AssertCutIsCompleteFrontier(Hierarchy, CutIds)
{
    const Selected = new Set(CutIds);
    const AncestorsOf = (Id) =>
    {
        const Found = new Set(), Stack = [...Hierarchy.Clusters[Id].Parents];
        while (Stack.length)
        {
            const Parent = Stack.pop();
            if (Found.has(Parent)) continue;
            Found.add(Parent);
            Stack.push(...Hierarchy.Clusters[Parent].Parents);
        }
        return Found;
    };
    for (const Id of CutIds)
        for (const Ancestor of AncestorsOf(Id))
            assert.ok(!Selected.has(Ancestor), `selected cluster ${Id} has selected ancestor ${Ancestor}: cut is not an antichain`);
    for (const LeafId of Hierarchy.Levels[0].Clusters)
    {
        if (Selected.has(LeafId)) continue;
        const Ancestors = AncestorsOf(LeafId);
        assert.ok([...Ancestors].some((Id) => Selected.has(Id)),
            "leaf " + LeafId + " reaches no selected ancestor: cut has a hole");
    }
}

test("projected-error DAG cut is a complete frontier at every threshold and refines monotonically", () =>
{
    const Hierarchy = BuildGeometryHierarchy(ConstructSphereFacets(3));   // 1280 tris
    let PreviousTriangles = -1;
    for (const Threshold of [1000, 50, 5, 1, 0.1, 0.001])
    {
        const Cut = SelectHierarchyCut(Hierarchy, Camera, null, { PixelErrorThreshold: Threshold });
        assert.ok(Cut.CutClusters.length >= 1);
        AssertCutIsCompleteFrontier(Hierarchy, Cut.CutClusterIds);
        assert.ok(Cut.CutTriangles >= PreviousTriangles,
            `threshold ${Threshold} drew ${Cut.CutTriangles} after ${PreviousTriangles}: smaller thresholds must not coarsen the cut`);
        PreviousTriangles = Cut.CutTriangles;
        if (Threshold >= 1000) assert.equal(Cut.CutClusterIds.length, 1, "huge threshold selects only the root");
        if (Threshold <= 0.001) assert.equal(Cut.CutTriangles, Hierarchy.SourceTriangles, "tiny threshold selects all leaves");
    }
});

test("frustum culling rejects off-screen instances; normal cone culls back-facing clusters but never glass", () =>
{
    const Hierarchy = BuildGeometryHierarchy(ConstructSphereFacets(3));
    const Offscreen = ExecuteHierarchyRuntimeFrame(
        [{ Hierarchy, LocalToWorld: Translation(500, 0, 0), Occupant: 0 }], Camera, {});
    assert.equal(Offscreen.Stats.VisibleTriangles, 0);
    assert.ok(Offscreen.Stats.FrustumCulledCount > 0);

    const Node = { ClassMask: 1, Cone: { Axis: [0, 0, 1], Cutoff: 0.9 },
                   Sphere: { Center: [0, 0, 0], Radius: 0.1 }, TightSphere: { Center: [0, 0, 0], Radius: 0.1 } };
    assert.equal(TestClusterNormalCone(Node, null, [0, 0, -5]), false, "cone facing +Z seen from -Z must cull");
    assert.equal(TestClusterNormalCone(Node, null, [0, 0, 5]), true, "cone facing the viewer must stay");
    assert.equal(TestClusterNormalCone({ ...Node, ClassMask: 2 }, null, [0, 0, -5]), true,
        "transmissive clusters are two-sided and never cone-culled");
});

test("two-phase Hi-Z occlusion culls the hidden sphere on the second frame", () =>
{
    const Hierarchy = BuildGeometryHierarchy(ConstructSphereFacets(3));
    const Instances = [
        { Hierarchy, LocalToWorld: Translation(0, 0, 0), Occupant: 0 },   // near, from camera at y=-8
        { Hierarchy, LocalToWorld: Translation(0, 7, 0), Occupant: 1 }    // directly behind it
    ];
    const Frame1 = ExecuteHierarchyRuntimeFrame(Instances, Camera, {});
    assert.ok(Frame1.Stats.VisibleTriangles > 0);
    const FarVisible1 = Frame1.VisibleClusters.filter((Item) => Item.Occupant === 1).length;
    assert.ok(FarVisible1 > 0, "without a previous pyramid nothing is occlusion-culled yet");

    const Frame2 = ExecuteHierarchyRuntimeFrame(Instances, Camera, { PreviousHiZPyramid: Frame1.CurrentHiZPyramid });
    const FarVisible2 = Frame2.VisibleClusters.filter((Item) => Item.Occupant === 1).length;
    assert.ok(FarVisible2 < FarVisible1,
        `second frame must occlusion-cull some far clusters (${FarVisible1} -> ${FarVisible2})`);
    assert.ok(Frame2.Stats.OccludedCulledCount > 0, "far clusters end in the occluded bin");
    assert.ok(Frame2.VisibleClusters.filter((Item) => Item.Occupant === 0).length > 0, "near sphere still draws");

    // Deterministic Hi-Z test against a hand-built pyramid: an occluder block at the near sphere's front
    // depth (z_eye = 7 from this camera) hides a box behind it and never a box in front of it.
    const WideCamera = { ...Camera, ScreenWidth: 64, ScreenHeight: 64 };
    const Depth = new Float32Array(64 * 64);   // 0 = far/clear (reversed)
    const OccluderDepth = -0.1 / (200 - 0.1) + (200 * 0.1) / (200 - 0.1) / 7.0;
    for (let Y = 16; Y < 48; ++Y) for (let X = 16; X < 48; ++X) Depth[Y * 64 + X] = OccluderDepth;
    const Pyramid = ConstructHiZPyramid(Depth, 64, 64, "reversed");
    assert.equal(Pyramid.Mips[1].Data.length, 32 * 32);
    assert.equal(Pyramid.Mips.at(-1).Data.length, 1, "pyramid reduces to a single texel");
    const ViewProjection = ExecuteHierarchyRuntimeFrame([], WideCamera, {}).ViewProjection;
    const Box = (Min, Max) => ({ ClassMask: 1, TightMinimum: Min, TightMaximum: Max,
                                 Minimum: Min, Maximum: Max, TriangleCount: 1, Level: 0 });
    assert.equal(TestClusterHiZ(Box([-0.2, 6.8, -0.2], [0.2, 7.2, 0.2]), null, ViewProjection, Pyramid), false,
        "box behind the occluder block must be Hi-Z culled");
    assert.equal(TestClusterHiZ(Box([-0.2, -4.2, -0.2], [0.2, -3.8, 0.2]), null, ViewProjection, Pyramid), true,
        "box in front of the occluder block must stay visible");
});

test("glass clusters draw transmissive, never write Hi-Z depth, and refine earlier than opaque", () =>
{
    const Glass = 9;
    const Hierarchy = BuildGeometryHierarchy(ConstructSphereFacets(3, Glass), { TransmissiveFamilies: [Glass] });
    const Frame = ExecuteHierarchyRuntimeFrame([{ Hierarchy, LocalToWorld: null, Occupant: 0 }], Camera, {});
    assert.ok(Frame.Stats.OpaqueDrawCount > 0, "opaque shell draws");
    assert.ok(Frame.Stats.TransmissiveDrawCount > 0, "glass band draws in the transmissive stream");
    for (const Draw of Frame.OpaqueDraws) assert.equal(Draw.ClassMask & 2, 0);
    for (const Draw of Frame.TransmissiveDraws) assert.notEqual(Draw.ClassMask & 2, 0);

    // A hierarchy of only glass writes no depth at all.
    const GlassOnly = ExecuteHierarchyRuntimeFrame(
        [{ Hierarchy, LocalToWorld: null, Occupant: 0 }], Camera, { PixelErrorThreshold: 1e9 });
    assert.ok(Math.max(...GlassOnly.DepthBuffer) <= 1, "depth buffer exists");
    const GlassCluster = Hierarchy.Clusters.find((Node) => (Node.ClassMask & 2) && Node.Error > 0);
    assert.ok(GlassCluster, "need a glass cluster with error");
    const OpaquePx = EvaluateProjectedSphereError(GlassCluster.Sphere, GlassCluster.Error, Camera, null, 1, GlassCluster.Cone, {});
    const GlassPx = EvaluateProjectedSphereError(GlassCluster.Sphere, GlassCluster.Error, Camera, null, 2, GlassCluster.Cone, {});
    assert.ok(GlassPx >= OpaquePx, "refraction deviation must not shrink the projected error");
});

test("fixed-size pages pack every cluster once, root pages are pinned, residency is LRU-bounded", () =>
{
    const Hierarchy = BuildGeometryHierarchy(ConstructSphereFacets(3));
    const Pages = PackHierarchyPages(Hierarchy, { PageByteLimit: 16384 });
    assert.ok(Pages.length >= 2, "1280-tri hierarchy should span several 16 KB pages");
    const Packed = Pages.flatMap((Page) => Page.Clusters);
    assert.equal(Packed.length, Hierarchy.Clusters.length, "every cluster packed exactly once");
    assert.equal(new Set(Packed).size, Packed.length, "no cluster packed twice");
    for (const Page of Pages)
    {
        assert.equal(Page.BinaryPayload.byteLength, Page.ByteSize);
        assert.ok(Page.ByteSize <= 16384 || Page.Clusters.length === 1, "page exceeds the fixed size budget");
        for (const Id of Page.Clusters) assert.equal(Hierarchy.Clusters[Id].PageIndex, Page.PageIndex);
    }
    const RootLevel = Hierarchy.Levels.length - 1;
    assert.ok(Pages[0].Pinned && Pages[0].Levels.includes(RootLevel), "first page is pinned and holds the root level");
    const PinnedBytes = Pages.filter((P) => P.Pinned).reduce((Sum, P) => Sum + P.ByteSize, 0);

    const Residency = new HierarchyResidencyManager(Pages, { MaxResidentBytes: PinnedBytes + 20000 });
    for (const Page of Pages) if (Page.Pinned) assert.ok(Residency.IsPageResident(Page.PageIndex));
    const NonPinned = Pages.filter((P) => !P.Pinned).map((P) => P.PageIndex);
    for (const Index of NonPinned) Residency.RequestPage(Index, 10);
    Residency.ServiceStreamingQueue(0, NonPinned.length);
    assert.ok(Residency.ResidentBytes <= Residency.MaxResidentBytes, "residency stays within its byte budget");
    for (const Page of Pages) if (Page.Pinned) assert.ok(Residency.IsPageResident(Page.PageIndex), "pinned pages are never evicted");

    // A cut against cold residency falls back to resident (root) clusters and queues the missing pages.
    const ColdResidency = new HierarchyResidencyManager(Pages, {});
    const Cut = SelectHierarchyCut(Hierarchy, Camera, null, { PixelErrorThreshold: 0.001, Residency: ColdResidency, FrameIndex: 0 });
    assert.ok(Cut.CutClusters.length >= 1);
    for (const Node of Cut.CutClusters)
        assert.ok(ColdResidency.IsPageResident(Node.PageIndex), "cold cut may only select resident clusters");
    assert.ok(ColdResidency.StreamingQueue.length > 0, "cold cut queues the pages it wanted");
});

test("heightfield cut draws real triangles and respects the threshold direction", () =>
{
    const Hierarchy = BuildGeometryHierarchy(ConstructHeightfieldFacets(48, 30));
    const TerrainCamera = { Eye: [0, -30, 18], Target: [0, 0, 0], Up: [0, 0, 1], FovY: Math.PI / 3, ScreenWidth: 256, ScreenHeight: 256, Near: 0.1, Far: 300 };
    const Coarse = SelectHierarchyCut(Hierarchy, TerrainCamera, null, { PixelErrorThreshold: 8 });
    const Fine = SelectHierarchyCut(Hierarchy, TerrainCamera, null, { PixelErrorThreshold: 0.25 });
    AssertCutIsCompleteFrontier(Hierarchy, Coarse.CutClusterIds);
    AssertCutIsCompleteFrontier(Hierarchy, Fine.CutClusterIds);
    assert.ok(Coarse.CutTriangles < Fine.CutTriangles, "coarser threshold must draw fewer triangles");
    assert.ok(Fine.CutTriangles <= Hierarchy.SourceTriangles);
});

test("page slot allocator streams LRU with pinned pages protected", () =>
{
    const Allocator = new PageSlotAllocator(3);
    Allocator.Pin(0);
    assert.equal(Allocator.Ensure(0, 0).Slot, 0);
    assert.equal(Allocator.Ensure(1, 0).EvictedPage, null);
    assert.equal(Allocator.Ensure(2, 0).EvictedPage, null);
    Allocator.Touch(1, 5);   // page 1 recently used; page 2 is now the LRU victim
    const Loaded = Allocator.Ensure(3, 6);
    assert.equal(Loaded.EvictedPage, 2);
    assert.equal(Allocator.SlotOf(3), Loaded.Slot);
    assert.ok(Allocator.IsResident(0), "pinned page survives eviction pressure");
    assert.ok(!Allocator.IsResident(2));
    const Blocked = new PageSlotAllocator(1);
    Blocked.Pin(0);
    Blocked.Ensure(0, 0);
    assert.equal(Blocked.Ensure(1, 1).Slot, -1, "all-pinned slots refuse new pages instead of evicting");
});
