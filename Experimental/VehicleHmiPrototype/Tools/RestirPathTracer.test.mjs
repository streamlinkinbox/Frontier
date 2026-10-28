//============================================================================================================================================
//                                                        RESTIRPATHTRACER.TEST.MJS
//============================================================================================================================================
// 🧩 Verification suite for the RTX Full ReSTIR GI Path Tracer:
//    1. Default renderer configuration (RayTracing: true, 3 bounces, 2 initial RIS candidates, RT shadows, Russian Roulette, NEE+MIS)
//    2. Two-Level BVH Acceleration Structure (TLAS over active occupants + Binned-SAH BLAS per FacetSet)
//    3. Exact Möller–Trumbore ray-triangle intersection, smooth normal interpolation, glass pass-through, and shredded tyre rim exposure
//    4. Dynamic Voronoi fracture fragment BLAS registration and TLAS update
//    5. Full WGSL shader pipeline verification

import { describe, it } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import {
    RayTracingSpace,
    ConstructFacetBlas,
    PackOctahedralCpu,
    UnpackOctahedralCpu,
    TlasNodeCapacity
} from "../Source/RayTracingSpace.js";
import { SceneStructure } from "../Source/FacetStructure.js";
import { CourseMaterial } from "../Source/MaterialSpecification.js";
import { SpawnPoint } from "../Source/CourseStructure.js";
import { ConstructSolid, FractureSolid, ResolveFragments } from "../Source/FractureStructure.js";
import { ConstructRigidTransform } from "../Source/TransformProjection.js";

const RootDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

// Install a file:// fetch shim for Node so SceneStructure.ConstructScene("Archive") loads the binary vehicle archives.
const OriginalFetch = globalThis.fetch;
globalThis.fetch = async (Resource, Init) =>
{
    const Url = String(Resource);
    if (!/^https?:\/\//i.test(Url))
    {
        const Resolved = path.resolve(RootDir, Url.replace(/^\//, ""));
        const Buffer   = await fs.promises.readFile(Resolved);
        return new Response(Buffer, { status: 200 });
    }
    return OriginalFetch(Resource, Init);
};

describe("RTX Full ReSTIR GI Path Tracer", () =>
{
    it("sets RTX Raytracing as the default renderer mode in IlluminationHost and InspectorPages", () =>
    {
        const HostSource = fs.readFileSync(path.join(RootDir, "Source/IlluminationHost.js"), "utf8");
        assert.match(HostSource, /const RendererParam = \(Query\.get\("renderer"\) \|\| "rtx"\)\.toLowerCase\(\);/);
        assert.match(HostSource, /RayTracing:\s*RendererParam !== "gtx"/);
        assert.match(HostSource, /RayTracedShadows:\s*true/);
        assert.match(HostSource, /RayTracedReflections:\s*true/);
        assert.match(HostSource, /ReflectionBounces:\s*Math\.max\(1,\s*Math\.min\(4,\s*Number\(Query\.get\("reflbounces"\)\s*\|\|\s*2\)\)\)/);
        assert.match(HostSource, /ReflectionScale:\s*1\.0/);
        assert.match(HostSource, /ReflectionRoughness:\s*0\.80/);
        assert.match(HostSource, /RussianRoulette:\s*true/);
        assert.match(HostSource, /FireflyFilter:\s*true/);
        assert.match(HostSource, /MultiBounceNEE:\s*true/);

        const InspectorSource = fs.readFileSync(path.join(RootDir, "Source/InspectorPages.js"), "utf8");
        assert.match(InspectorSource, /RTX · ReSTIR Path Tracer/);
        assert.match(InspectorSource, /GI path bounces/);
        assert.match(InspectorSource, /Reflection bounces/);
        assert.match(InspectorSource, /Ray-traced reflections/);
        assert.match(InspectorSource, /Initial RIS path candidates/);
    });

    it("encodes and decodes octahedral unit normals accurately across all octants", () =>
    {
        const Directions = [
            [0, 0, 1], [0, 0, -1], [1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0],
            [0.57735, 0.57735, 0.57735], [-0.57735, 0.57735, -0.57735], [0.26726, -0.53452, -0.80178]
        ];
        for (const Dir of Directions)
        {
            const Packed   = PackOctahedralCpu(Dir);
            const Unpacked = UnpackOctahedralCpu(Packed);
            const Dot      = Dir[0] * Unpacked[0] + Dir[1] * Unpacked[1] + Dir[2] * Unpacked[2];
            assert.ok(Dot > 0.9995, `Expected dot > 0.9995 for ${JSON.stringify(Dir)}, got ${Dot}`);
        }
    });

    it("builds Two-Level BVH (TLAS + Binned-SAH BLAS) over all 56 FacetSets and intersects vehicle, cabin, ground, and poles", async () =>
    {
        const Scene = new SceneStructure();
        await Scene.ConstructScene("Archive", 64);

        // Position vehicle at spawn
        const VehicleTransform = ConstructRigidTransform([SpawnPoint.Position[0], SpawnPoint.Position[1], 0.63], [0, 0, 0, 1]);
        Scene.AssignOccupantTransform(Scene.VehicleOccupants.Body, VehicleTransform);

        const RtSpace = new RayTracingSpace(null);
        RtSpace.ConstructResources(Scene, null, null);

        assert.ok(RtSpace.TotalTriangles > 20000, `Expected > 20,000 triangles in BLAS, got ${RtSpace.TotalTriangles}`);
        assert.ok(RtSpace.ActiveInstances >= 340, `Expected >= 340 active TLAS instances, got ${RtSpace.ActiveInstances}`);
        assert.equal(RtSpace.UsedTlasNodes, 2 * RtSpace.ActiveInstances - 1);

        // 1. Cast ray straight down onto a BodyCoat facet of the ControlVehicle body
        const BodyFacets = Scene.FacetSets[Scene.Occupants[Scene.VehicleOccupants.Body].FacetSet].Facets;
        const CoatFacet  = BodyFacets.find(
            (F) => F.Family === CourseMaterial.BodyCoat && F.Corners[0][2] > 0.6
        );
        const CoatCentroid = [
            (CoatFacet.Corners[0][0] + CoatFacet.Corners[1][0] + CoatFacet.Corners[2][0]) / 3,
            (CoatFacet.Corners[0][1] + CoatFacet.Corners[1][1] + CoatFacet.Corners[2][1]) / 3,
            (CoatFacet.Corners[0][2] + CoatFacet.Corners[1][2] + CoatFacet.Corners[2][2]) / 3
        ];
        const RoofHit = RtSpace.TraceRayCpu(
            Scene,
            [SpawnPoint.Position[0] + CoatCentroid[0], SpawnPoint.Position[1] + CoatCentroid[1], 3.0],
            [0, 0, -1],
            0.01,
            20.0
        );
        assert.equal(RoofHit.Hit, true, "Expected ray to hit ControlVehicle body");
        assert.equal(RoofHit.Occupant, Scene.VehicleOccupants.Body, "Expected hit occupant to be ControlVehicle body");
        assert.ok(
            [CourseMaterial.BodyCoat, CourseMaterial.BodyTrim, CourseMaterial.VehiclePlastic].includes(RoofHit.Family),
            `Expected hit material family on vehicle top to be exterior body material, got ${RoofHit.Family}`
        );
        assert.ok(RoofHit.Normal[2] > 0.5, `Expected upward body normal, got ${JSON.stringify(RoofHit.Normal)}`);

        // 2. Cast ray down onto the Proving Ground road surface away from the vehicle
        const RoadHit = RtSpace.TraceRayCpu(
            Scene,
            [SpawnPoint.Position[0] + 8.0, SpawnPoint.Position[1], 5.0],
            [0, 0, -1],
            0.01,
            20.0
        );
        assert.equal(RoadHit.Hit, true, "Expected ray to hit Proving Ground road");
        assert.equal(RoadHit.Occupant, Scene.GroundOccupant, "Expected hit occupant to be GroundOccupant");
        assert.ok(Math.abs(RoadHit.Position[2]) < 0.15, `Expected road hit near Z=0, got Z=${RoadHit.Position[2]}`);
        assert.ok(RoadHit.Normal[2] > 0.95, "Expected road normal pointing +Z");

        // 3. Verify transparent VehicleGlass is skipped so rays enter the cabin interior
        const GlassFacet = BodyFacets.find((F) => F.Family === CourseMaterial.VehicleGlass);
        assert.ok(GlassFacet, "Expected ControlVehicle body to contain VehicleGlass facets");
        const GlassCenterLocal = [
            (GlassFacet.Corners[0][0] + GlassFacet.Corners[1][0] + GlassFacet.Corners[2][0]) / 3,
            (GlassFacet.Corners[0][1] + GlassFacet.Corners[1][1] + GlassFacet.Corners[2][1]) / 3,
            (GlassFacet.Corners[0][2] + GlassFacet.Corners[1][2] + GlassFacet.Corners[2][2]) / 3
        ];
        const GlassRayHit = RtSpace.TraceRayCpu(
            Scene,
            [SpawnPoint.Position[0] + GlassCenterLocal[0], SpawnPoint.Position[1] + GlassCenterLocal[1], 3.0],
            [0, 0, -1],
            0.01,
            10.0
        );
        assert.equal(GlassRayHit.Hit, true);
        assert.notEqual(GlassRayHit.Family, CourseMaterial.VehicleGlass, "Ray must pass through VehicleGlass and hit interior or body");

        // 4. Verify shredded wheel skips TyreFamily and hits the metal wheel rim
        const WheelOccIdx = Scene.VehicleOccupants.Wheels[0];
        Scene.AssignOccupantTransform(WheelOccIdx, ConstructRigidTransform([SpawnPoint.Position[0] + 5.0, SpawnPoint.Position[1] + 2.0, 0.36], [0, 0, 0, 1]));
        RtSpace.UpdateTlas(Scene, null);
        const WheelBounds = Scene.QueryWorldBounds(WheelOccIdx);
        const WheelCenter = [
            0.5 * (WheelBounds.Minimum[0] + WheelBounds.Maximum[0]),
            0.5 * (WheelBounds.Minimum[1] + WheelBounds.Maximum[1]),
            0.5 * (WheelBounds.Minimum[2] + WheelBounds.Maximum[2])
        ];
        const IntactWheelHit = RtSpace.TraceRayCpu(
            Scene,
            [WheelCenter[0], WheelCenter[1] + 1.0, WheelCenter[2]],
            [0, -1, 0],
            0.01,
            2.0
        );
        assert.equal(IntactWheelHit.Hit, true, "Expected ray to hit intact wheel");
        assert.equal(IntactWheelHit.Occupant, WheelOccIdx);

        // Cast from top down onto tread
        const TreadHit = RtSpace.TraceRayCpu(
            Scene,
            [WheelCenter[0], WheelCenter[1], WheelBounds.Maximum[2] + 0.5],
            [0, 0, -1],
            0.01,
            2.0
        );
        assert.equal(TreadHit.Hit, true, "Expected ray to hit intact wheel tread");
        assert.equal(TreadHit.Family, CourseMaterial.Tyre, "Intact wheel top hit should be Tyre rubber");

        RtSpace.UpdateTlas(Scene, {
            Wheels: [{ Shredded: true }, { Shredded: false }, { Shredded: false }, { Shredded: false }]
        });
        const ShreddedWheelHit = RtSpace.TraceRayCpu(
            Scene,
            [WheelCenter[0], WheelCenter[1], WheelBounds.Maximum[2] + 0.5],
            [0, 0, -1],
            0.01,
            2.0
        );
        assert.equal(ShreddedWheelHit.Hit, true, "Expected ray to hit inner rim of shredded wheel");
        assert.notEqual(ShreddedWheelHit.Family, CourseMaterial.Tyre, "Shredded wheel must skip Tyre rubber");
    });

    it("dynamically registers Voronoi fracture fragments into BLAS and TLAS upon prop destruction", async () =>
    {
        const Scene = new SceneStructure();
        await Scene.ConstructScene("Archive", 16);
        const RtSpace = new RayTracingSpace(null);
        RtSpace.ConstructResources(Scene, null, null);

        const Breakable = Scene.Breakables[0];
        const Solid     = ConstructSolid(Breakable.Shape);
        const RawResult = FractureSolid(Solid, {
            Material:    Breakable.Material,
            Energy:      Breakable.Energy || 6000,
            Seed:        7331,
            Ceiling:     Breakable.Capacity,
            MinimumSize: 0.05,
            Impact:      Breakable.Impact || [0, 0, 0]
        });
        const Fragments = ResolveFragments(RawResult, Breakable.Recipe ? Breakable.Recipe.Material : Breakable.Material, 1.0);
        assert.ok(Fragments.length >= 2, "Expected at least 2 fracture fragments");

        RtSpace.RegisterPropFragments(0, Breakable.PieceFirst, Breakable.Capacity, Fragments);

        // Activate the first fragment at a known test position [15, -25, 4]
        const PieceOccIdx = Breakable.PieceFirst;
        const PieceOcc    = Scene.Occupants[PieceOccIdx];
        PieceOcc.Inactive = false;
        PieceOcc.LocalMinimum = Fragments[0].Minimum;
        PieceOcc.LocalMaximum = Fragments[0].Maximum;
        PieceOcc.Transform = ConstructRigidTransform([15.0, -25.0, 4.0], [0, 0, 0, 1]);

        RtSpace.UpdateTlas(Scene, null);

        const FragHit = RtSpace.TraceRayCpu(Scene, [15.0, -25.0, 6.0], [0, 0, -1], 0.01, 5.0);
        assert.equal(FragHit.Hit, true, "Expected ray to hit dynamically fractured debris piece");
        assert.equal(FragHit.Occupant, PieceOccIdx, "Expected hit occupant to be the active fracture fragment");

        // Clear fragments and re-verify
        PieceOcc.Inactive = true;
        RtSpace.ClearPropFragments(Breakable.PieceFirst, Breakable.Capacity);
        RtSpace.UpdateTlas(Scene, null);
        const AfterClearHit = RtSpace.TraceRayCpu(Scene, [15.0, -25.0, 6.0], [0, 0, -1], 0.01, 5.0);
        assert.notEqual(AfterClearHit.Occupant, PieceOccIdx, "Cleared fragment must no longer be hit in TLAS");
    });

    it("contains complete ReSTIR Path Tracer and Multi-Bounce Ray-Traced Reflection stages in WGSL shaders", () =>
    {
        const SceneRecords = fs.readFileSync(path.join(RootDir, "Shader/SceneRecords.wgsl"), "utf8");
        assert.match(SceneRecords, /const FeatureRayTracedReflections\s*:\s*u32 = 262144u;/);
        assert.match(SceneRecords, /fn ResolveReflectionBounces\(/);
        assert.match(SceneRecords, /fn ResolveReflectionRoughness\(/);
        assert.match(SceneRecords, /fn ResolveReflectionScale\(/);

        const RayTraversal = fs.readFileSync(path.join(RootDir, "Shader/RayTraversal.wgsl"), "utf8");
        assert.match(RayTraversal, /fn TraceSceneRay\(/);
        assert.match(RayTraversal, /fn TraceSceneVisibility\(/);
        assert.match(RayTraversal, /fn SampleSurfaceBsdf\(/);
        assert.match(RayTraversal, /fn EvaluateDirectNextEvent\(/);
        assert.match(RayTraversal, /fn TraceMultiBouncePath\(/);
        assert.match(RayTraversal, /fn TraceMultiBounceReflection\(/);

        const Reflection = fs.readFileSync(path.join(RootDir, "Shader/ReflectionTraversal.wgsl"), "utf8");
        assert.match(Reflection, /TraceMultiBounceReflection\(/);
        assert.match(Reflection, /FeatureRayTracedReflections/);

        const Display = fs.readFileSync(path.join(RootDir, "Shader/DisplayRecombination.wgsl"), "utf8");
        assert.match(Display, /ResolveReflectionRoughness\(/);
        assert.match(Display, /ResolveReflectionScale\(/);

        const Temporal = fs.readFileSync(path.join(RootDir, "Shader/ReservoirTemporal.wgsl"), "utf8");
        assert.match(Temporal, /TraceMultiBouncePath\(/);
        assert.match(Temporal, /TraceSceneVisibility\(/);

        const Spatial = fs.readFileSync(path.join(RootDir, "Shader/ReservoirSpatial.wgsl"), "utf8");
        assert.match(Spatial, /ResolveReconnectionJacobian\(/);
        assert.match(Spatial, /TraceSceneVisibility\(/);
    });
});
