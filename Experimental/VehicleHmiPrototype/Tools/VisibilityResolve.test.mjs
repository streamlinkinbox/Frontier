//============================================================================================================================================
//                                                          VISIBILITYRESOLVE.TEST.MJS
//============================================================================================================================================
// 🧩 Phase 3 gates: visibility-id packing, visbuffer coverage, glass depth-peel layers (glass never enters
//    the opaque buffer), deferred resolve from UV/family, and two-layer refraction changing gathered colour.

import { test } from "node:test";
import assert from "node:assert/strict";
import { BuildGeometryHierarchy } from "../Source/GeometryHierarchy.js";
import { ExecuteHierarchyRuntimeFrame } from "../Source/GeometryHierarchyRuntime.js";
import { PackVisibilityId, UnpackVisibilityId, RasterizeVisibility, ResolveVisibility } from "../Source/VisibilityResolve.js";
import { ConstructSphereFacets, ConstructUvSeamCreasedMeshFacets } from "./GeometryHierarchyCorpus.mjs";

const Camera = { Eye: [2.0, -6.8, 2.2], Target: [0, 0, 0], Up: [0, 0, 1], FovY: Math.PI / 3, ScreenWidth: 192, ScreenHeight: 192, Near: 0.1, Far: 200 };

function VisibleItems(Hierarchy, Threshold = 2)
{
    const Frame = ExecuteHierarchyRuntimeFrame([{ Hierarchy, LocalToWorld: null, Occupant: 0 }], Camera, { PixelErrorThreshold: Threshold });
    return Frame.VisibleClusters.map((Item) => ({ Hierarchy, Node: Item.Node, LocalToWorld: null }));
}

test("visibility ids pack and unpack losslessly", () =>
{
    for (const [Slot, Tri] of [[0, 0], [5, 127], [1234, 42], [65535, 255]])
    {
        const Decoded = UnpackVisibilityId(PackVisibilityId(Slot, Tri));
        assert.equal(Decoded.RecordSlot, Slot);
        assert.equal(Decoded.LocalTriangle, Tri);
    }
    assert.equal(UnpackVisibilityId(0), null);
});

test("opaque sphere fills the visbuffer centre and every id decodes to a drawn triangle", () =>
{
    const Hierarchy = BuildGeometryHierarchy(ConstructSphereFacets(3));
    const Items = VisibleItems(Hierarchy);
    const Frame = ExecuteHierarchyRuntimeFrame([{ Hierarchy, LocalToWorld: null, Occupant: 0 }], Camera, { PixelErrorThreshold: 2 });
    const Raster = RasterizeVisibility(Items, 192, 192, Frame.ViewProjection);
    const Centre = 96 * 192 + 96;
    assert.ok(Raster.OpaqueVis[Centre] > 0, "screen centre must hit the sphere");
    let Covered = 0;
    for (let Pixel = 0; Pixel < 192 * 192; ++Pixel)
    {
        if (!Raster.OpaqueVis[Pixel]) continue;
        ++Covered;
        const Decoded = UnpackVisibilityId(Raster.OpaqueVis[Pixel]);
        const Item = Items[Decoded.RecordSlot];
        assert.ok(Item, "decoded record slot must exist");
        assert.ok(Decoded.LocalTriangle < Item.Node.TriangleCount, "decoded triangle must belong to the cluster");
    }
    assert.ok(Covered > 1000, "sphere should cover a good part of the frame, got " + Covered);
    assert.equal(Raster.GlassVis[0].some((Id) => Id > 0), false, "opaque sphere produces no glass pixels");
});

test("glass band depth-peels into two layers and never enters the opaque visbuffer", () =>
{
    const Glass = 9;
    const Hierarchy = BuildGeometryHierarchy(ConstructSphereFacets(3, Glass), { TransmissiveFamilies: [Glass] });
    const Items = VisibleItems(Hierarchy);
    const Frame = ExecuteHierarchyRuntimeFrame([{ Hierarchy, LocalToWorld: null, Occupant: 0 }], Camera, { PixelErrorThreshold: 2 });
    const Raster = RasterizeVisibility(Items, 192, 192, Frame.ViewProjection);
    let Front = 0, Back = 0;
    for (let Pixel = 0; Pixel < 192 * 192; ++Pixel)
    {
        if (Raster.GlassVis[0][Pixel]) ++Front;
        if (Raster.GlassVis[1][Pixel]) ++Back;
        if (Raster.GlassVis[0][Pixel])
        {
            const Decoded = UnpackVisibilityId(Raster.GlassVis[0][Pixel]);
            assert.notEqual(Items[Decoded.RecordSlot].Node.ClassMask & 2, 0, "glass buffer holds only transmissive clusters");
        }
    }
    assert.ok(Front > 100, "glass band front layer visible, got " + Front);
    assert.ok(Back > 10, "looking through the band must peel a back layer, got " + Back);
});

test("deferred resolve shades from family and UV, and refraction bends the gathered colour", () =>
{
    const Glass = 9;
    const Hierarchy = BuildGeometryHierarchy(ConstructSphereFacets(3, Glass), { TransmissiveFamilies: [Glass] });
    const Items = VisibleItems(Hierarchy);
    const Frame = ExecuteHierarchyRuntimeFrame([{ Hierarchy, LocalToWorld: null, Occupant: 0 }], Camera, { PixelErrorThreshold: 2 });
    const Raster = RasterizeVisibility(Items, 192, 192, Frame.ViewProjection);
    const Resolved = ResolveVisibility(Raster, { ...Camera, ViewProjection: Frame.ViewProjection });
    assert.ok(Resolved.GlassPixels > 100);
    assert.ok(Resolved.TwoLayerPixels > 10, "two-layer refraction engaged, got " + Resolved.TwoLayerPixels);
    // Glass pixels must differ from the straight-through opaque colour (refraction offset + tint + Fresnel).
    let Differing = 0, GlassChecked = 0;
    for (let Pixel = 0; Pixel < 192 * 192; ++Pixel)
    {
        if (!Raster.GlassVis[0][Pixel]) continue;
        ++GlassChecked;
        const Delta = Math.abs(Resolved.FinalColor[Pixel * 3] - Resolved.OpaqueColor[Pixel * 3]) +
                      Math.abs(Resolved.FinalColor[Pixel * 3 + 1] - Resolved.OpaqueColor[Pixel * 3 + 1]) +
                      Math.abs(Resolved.FinalColor[Pixel * 3 + 2] - Resolved.OpaqueColor[Pixel * 3 + 2]);
        if (Delta > 0.02) ++Differing;
    }
    assert.ok(Differing > GlassChecked * 0.5, `most glass pixels should refract/tint (${Differing}/${GlassChecked})`);
    for (const Value of Resolved.FinalColor) assert.ok(Number.isFinite(Value) && Value >= 0, "resolved colours stay finite");
});

test("vessel resolve is driven by interpolated UVs (both checker parities appear)", () =>
{
    const Hierarchy = BuildGeometryHierarchy(ConstructUvSeamCreasedMeshFacets(32, 14, 4));
    const Items = VisibleItems(Hierarchy, 4);
    const Frame = ExecuteHierarchyRuntimeFrame([{ Hierarchy, LocalToWorld: null, Occupant: 0 }], Camera, { PixelErrorThreshold: 4 });
    const Raster = RasterizeVisibility(Items, 192, 192, Frame.ViewProjection);
    const Resolved = ResolveVisibility(Raster, { ...Camera, ViewProjection: Frame.ViewProjection });
    const Luminances = new Set();
    for (let Pixel = 0; Pixel < 192 * 192; ++Pixel)
        if (Raster.OpaqueVis[Pixel]) Luminances.add(Math.round(Resolved.OpaqueColor[Pixel * 3] * 20));
    assert.ok(Luminances.size >= 4, "UV checker + lighting should produce varied resolved shades, got " + Luminances.size);
});

test("visibility raster and resolve are deterministic", () =>
{
    const Hierarchy = BuildGeometryHierarchy(ConstructSphereFacets(2));
    const Items = VisibleItems(Hierarchy);
    const Frame = ExecuteHierarchyRuntimeFrame([{ Hierarchy, LocalToWorld: null, Occupant: 0 }], Camera, { PixelErrorThreshold: 2 });
    const A = ResolveVisibility(RasterizeVisibility(Items, 128, 128, Frame.ViewProjection), { ...Camera, ViewProjection: Frame.ViewProjection });
    const B = ResolveVisibility(RasterizeVisibility(Items, 128, 128, Frame.ViewProjection), { ...Camera, ViewProjection: Frame.ViewProjection });
    assert.deepEqual([...A.FinalColor], [...B.FinalColor]);
});
