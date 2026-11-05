//============================================================================================================================================
//                                                         EMISSIVELIGHTS.TEST.MJS
//============================================================================================================================================
// 🧩 The emissive clustered lights. The data structure (partition, clusters, cell lists), the central claim (light technique + BSDF technique with the partition reproduce the
//    analytic irradiance), the real scene's invariants, and — opt-in with RUN_GPU=1 — the WGSL sampler executed beside its CPU twin and the whole app with the structure on.

import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { EmissiveLightSpace, PartitionTriangles, EmissiveCapacity, EmissiveEmptySlot, EmissiveReachThreshold } from "../Source/EmissiveLightSpace.js";
import { OccupantRole } from "../Source/FacetStructure.js";
import { LightClusterBounds, LightClusterBytes } from "../Source/LightClusterSpace.js";
import { SampleEmissiveLight, BsdfWeight, CellWords, Slot, Luminance, IsListedEmitter } from "./EmissiveReference.mjs";

const Here = path.dirname(fileURLToPath(import.meta.url)), Root = path.resolve(Here, "..");
const Enabled = process.env.RUN_GPU === "1";
const Identity = (X = 0, Y = 0, Z = 0) => Float32Array.from([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, X, Y, Z, 1]);

// a synthetic scene: one facet set, a rectangle (two triangles) of emissive family 1 facing down at the origin, placed at the given transforms
function Rectangle(Width, Height, Family = 1)
{
    const A = [-Width / 2, -Height / 2, 0], B = [Width / 2, -Height / 2, 0], C = [Width / 2, Height / 2, 0], D = [-Width / 2, Height / 2, 0];
    return [{ Corners: [A, C, B], Family }, { Corners: [A, D, C], Family }];   // wound so the normal is −Z
}
function Scene(Placements, Facets = Rectangle(1, 1))
{
    return { FacetSets: [{ Facets }], Occupants: Placements.map((Transform) => ({ FacetSet: 0, Transform, Roles: 1, Inactive: false, MaterialOverride: -1 })) };
}
const Materials = [{ Emission: [0, 0, 0] }, { Emission: [3.0, 3.0, 3.0] }];

test("partition: runs of at most eight, every triangle exactly once, deterministic", () =>
{
    const Triangles = Array.from({ length: 53 }, (_, Index) => ({ Index, Centroid: [Math.sin(Index) * 3, Math.cos(Index * 1.7) * 2, (Index % 7) * 0.4] }));
    const Runs = PartitionTriangles(Triangles);
    assert.ok(Runs.every((Run) => Run.length >= 1 && Run.length <= 8));
    assert.equal(Runs.flat().length, 53);
    assert.equal(new Set(Runs.flat().map((T) => T.Index)).size, 53);
    assert.deepEqual(PartitionTriangles(Triangles).map((Run) => Run.map((T) => T.Index)), Runs.map((Run) => Run.map((T) => T.Index)));
    assert.ok(Runs.length >= 7, "53 triangles need at least seven runs of eight");
});

test("structure: records land where the shader reads them and the layout fits one uniform binding", () =>
{
    assert.ok(LightClusterBytes <= 65536, `the light record (${LightClusterBytes} B) fits a 64 KiB uniform binding`);
    const Space = new EmissiveLightSpace();
    const Placed = Scene([Identity(20, 5, 3), Identity(30, 5, 3)]);
    assert.equal(Space.Construct(Placed, Materials), 1, "one emissive facet set");
    assert.ok(Space.Update(Placed, Materials, 1.0));
    assert.equal(Space.Count, 2);
    assert.equal(Space.TriangleCount, 2);
    assert.deepEqual([...Space.Included].sort(), [0, 1]);
    assert.equal(Placed.Occupants[0].Roles & 64, 64, "the role bit is mirrored into the occupant");
    // each cluster record: world centre and radius, occupant, first triangle, count, power
    const F = Space.ClusterFloats, W = Space.ClusterWords;
    assert.ok(Math.abs(F[0] - 20) < 1e-6 && Math.abs(F[1] - 5) < 1e-6 && Math.abs(F[2] - 3) < 1e-6, "world centre");
    assert.equal(W[4], 0); assert.equal(W[5], 0); assert.equal(W[6], 2);
    assert.ok(Math.abs(F[7] - 1.0 * Luminance([3, 3, 3])) < 1e-5, `power = area × luminance (${F[7]})`);
    // cells: ids < Count, no duplicates, strongest first, empty slots 0x7FFF
    for (let Cell = 0; Cell < 1024; ++Cell)
    {
        const Words = [0, 1, 2, 3].map((K) => Space.Cells[Cell * 4 + K]);
        const Ids = [...Array(8).keys()].map((Index) => Slot(Words, Index));
        const Used = Ids.filter((Id) => Id !== EmissiveEmptySlot);
        assert.ok(Used.every((Id) => Id < 2) && new Set(Used).size === Used.length, "valid, unique ids");
        assert.ok(Ids.slice(Used.length).every((Id) => Id === EmissiveEmptySlot), "empty slots follow the used ones");
    }
    // moving one occupant changes only that instance; nothing else is rewritten
    Placed.Occupants[1].Transform = Identity(31, 5, 3);
    assert.ok(Space.Update(Placed, Materials, 1.0));
    assert.ok(Math.abs(Space.ClusterFloats[8] - 31) < 1e-6 && Math.abs(Space.ClusterFloats[0] - 20) < 1e-6);
    assert.equal(Space.Update(Placed, Materials, 1.0), false, "nothing moved, nothing to upload");
    // a deactivated occupant leaves the table and loses its role
    Placed.Occupants[1].Inactive = true;
    assert.ok(Space.Update(Placed, Materials, 1.0));
    assert.equal(Space.Count, 1); assert.equal(Placed.Occupants[1].Roles & 64, 0);
    // an occupant with a material override is not represented (the override changes the emission of its every triangle)
    const Overridden = Scene([Identity(20, 5, 3)]); Overridden.Occupants[0].MaterialOverride = 1;
    const Other = new EmissiveLightSpace(); Other.Construct(Overridden, Materials); Other.Update(Overridden, Materials, 1.0);
    assert.equal(Other.Count, 0);
});

test("cell lists: a cell keeps the eight strongest candidates; the partition applies to listed emitters only", () =>
{
    const Space = new EmissiveLightSpace();
    // twelve emitters of rising power in one corner of the grid (rising scale² = rising power), all within reach of the same cell
    const Placements = Array.from({ length: 12 }, (_, Index) => { const T = Identity(10 + Index * 0.5, 4, 2); T[0] = T[5] = 1.0 + 0.2 * Index; return T; });
    const Packed = Scene(Placements);
    // give every facet its BVH slot, as RayTracingSpace would: facet f of the one set is triangle 100 + f
    Packed.FacetSets[0].TriangleSlots = Uint32Array.from([100, 101]);
    Space.Construct(Packed, Materials); Space.Update(Packed, Materials, 1.0);
    const Words = CellWords(Space, LightClusterBounds, [10.2, 4.0, 2.0]);
    const Listed = [...Array(8).keys()].map((Index) => Slot(Words, Index));
    assert.ok(Listed.every((Id) => Id !== EmissiveEmptySlot), "a full list of eight");
    assert.equal(new Set(Listed).size, 8);
    assert.ok(Space.Statistics.TruncatedCells > 0, "the cell had more candidates than slots");
    const Strongest = [...Array(12).keys()].sort((A, B) => Space.ClusterFloats[B * 8 + 7] - Space.ClusterFloats[A * 8 + 7]).slice(0, 8);
    assert.ok(Listed.every((Id) => Strongest.includes(Id)), `the listed clusters are the strongest by power: ${Listed} vs ${Strongest}`);
    // membership is exact: an emitter in the list is 'listed' by its (occupant, BVH triangle); one that lost its slot, or a triangle that is not an emitter, is not
    const InList = Listed[0], Out = [...Array(12).keys()].find((Index) => !Listed.includes(Index));
    assert.ok(IsListedEmitter(Space, Words, InList, 100) && IsListedEmitter(Space, Words, InList, 101));
    assert.ok(!IsListedEmitter(Space, Words, Out, 100), "an emitter that lost its slot is not part of this cell's light technique");
    assert.ok(!IsListedEmitter(Space, Words, InList, 999), "a triangle the cluster does not hold is not listed");
    assert.ok(!IsListedEmitter(Space, null, InList, 100), "outside the grid nothing is listed");
});

// The cell lists the SLOW way: every candidate of every instance collected, each cell sorted (importance descending, cluster id ascending) and cut at eight. This is the algorithm the
// incremental binning replaced, kept here as the oracle it must equal exactly.
function OracleCells(Space, EmissionScale)
{
    const Minimum = LightClusterBounds.Minimum, Extent = LightClusterBounds.Extent, Resolution = LightClusterBounds.Resolution;
    const Size = Extent.map((Value, Axis) => Value / Resolution[Axis]), CellCount = Resolution[0] * Resolution[1] * Resolution[2];
    const Candidates = Array.from({ length: CellCount }, () => []);
    Space.Instances.forEach((Instance, Slot) =>
    {
        const W = Space.World[Slot], Power = W.Power * EmissionScale;
        if (!(Power > 0.0)) return;
        const Reach = Math.sqrt(Power / EmissiveReachThreshold) + W.Radius;
        const Low = [0, 1, 2].map((Axis) => Math.max(0, Math.floor((W.Centre[Axis] - Reach - Minimum[Axis]) / Size[Axis])));
        const High = [0, 1, 2].map((Axis) => Math.min(Resolution[Axis] - 1, Math.floor((W.Centre[Axis] + Reach - Minimum[Axis]) / Size[Axis])));
        for (let Z = Low[2]; Z <= High[2]; ++Z) for (let Y = Low[1]; Y <= High[1]; ++Y) for (let X = Low[0]; X <= High[0]; ++X)
        {
            const Corner = [Minimum[0] + X * Size[0], Minimum[1] + Y * Size[1], Minimum[2] + Z * Size[2]];
            let Nearest2 = 0.0, Centre2 = 0.0;
            for (let Axis = 0; Axis < 3; ++Axis)
            {
                const Near = Math.min(Math.max(W.Centre[Axis], Corner[Axis]), Corner[Axis] + Size[Axis]);
                Nearest2 += (W.Centre[Axis] - Near) ** 2; Centre2 += (W.Centre[Axis] - (Corner[Axis] + 0.5 * Size[Axis])) ** 2;
            }
            if (Nearest2 >= Reach * Reach) continue;
            Candidates[X + Resolution[0] * (Y + Resolution[1] * Z)].push({ Slot, Importance: Power / (Centre2 + W.Radius * W.Radius + 1.0e-4) });
        }
    });
    const Words = new Uint32Array(CellCount * 4); let Truncated = 0, Empty = 0;
    Candidates.forEach((List, Cell) =>
    {
        List.sort((A, B) => B.Importance - A.Importance || A.Slot - B.Slot);
        if (List.length > EmissiveCapacity.Slots) Truncated++; else if (List.length === 0) Empty++;
        const Ids = Array.from({ length: EmissiveCapacity.Slots }, (_, K) => K < List.length ? List[K].Slot : EmissiveEmptySlot);
        for (let Word = 0; Word < 4; ++Word) Words[Cell * 4 + Word] = (Ids[2 * Word] | (Ids[2 * Word + 1] << 16)) >>> 0;
    });
    return { Words, Truncated, Empty };
}

test("incremental binning: with moving instances, every frame's cell lists equal a from-scratch rebuild, bit for bit", () =>
{
    let Seed = 12345; const Rand = () => (Seed = (Seed * 1664525 + 1013904223) >>> 0) / 4294967296;
    const Minimum = LightClusterBounds.Minimum, Extent = LightClusterBounds.Extent;
    const Position = (Margin = 0) => [0, 1, 2].map((Axis) => Minimum[Axis] - Margin + Rand() * (Extent[Axis] + 2 * Margin));
    const Placed = Scene([]);
    const Add = (Transform, Dynamic) => Placed.Occupants.push({ FacetSet: 0, Transform, Roles: OccupantRole.Raster | (Dynamic ? OccupantRole.Dynamic : 0), Inactive: false, MaterialOverride: -1 });
    for (let Index = 0; Index < 44; ++Index) { const [X, Y, Z] = Position(10); const S = 0.4 + 2.5 * Rand(); const T = Identity(X, Y, Z); T[0] = S; T[5] = S; T[10] = S; Add(T, false); }
    const Twin = Placed.Occupants[3].Transform; Add(Float32Array.from(Twin), false);              // an exact twin: equal importance everywhere, so the tie rule decides
    for (let Index = 0; Index < 4; ++Index) { const [X, Y, Z] = Position(); Add(Identity(X, Y, Z), true); }
    const Dynamics = Placed.Occupants.map((Occupant, Index) => [Occupant, Index]).filter(([Occupant]) => Occupant.Roles & OccupantRole.Dynamic);
    const Space = new EmissiveLightSpace(); Space.Construct(Placed, Materials);
    let Scale = 1.0, Checked = 0, Truncated = 0;
    const Verify = (Label) =>
    {
        const Expected = OracleCells(Space, Scale);
        assert.deepEqual([...Space.Cells.subarray(0, 8)], [...Expected.Words.subarray(0, 8)]);
        for (let Word = 0; Word < Expected.Words.length; ++Word) if (Space.Cells[Word] !== Expected.Words[Word]) assert.fail(`${Label}: cell word ${Word} is ${Space.Cells[Word].toString(16)}, a rebuild gives ${Expected.Words[Word].toString(16)}`);
        assert.equal(Space.Statistics.TruncatedCells, Expected.Truncated, `${Label}: truncated cells`); assert.equal(Space.Statistics.EmptyCells, Expected.Empty, `${Label}: empty cells`);
        Checked++; Truncated += Expected.Truncated;
    };
    assert.ok(Space.Update(Placed, Materials, Scale)); Verify("first build");
    for (let Step = 1; Step <= 30; ++Step)
    {
        for (const [Occupant] of Dynamics) { const [X, Y, Z] = Position(); Occupant.Transform = Identity(X, Y, Z); }               // the car's lamps move every frame
        assert.ok(Space.Update(Placed, Materials, Scale), `step ${Step}`); Verify(`step ${Step}, moving instances`);
        if (Step === 8) { Placed.Occupants[10].Transform = Identity(...Position()); assert.ok(Space.Update(Placed, Materials, Scale)); Verify("a static instance moved"); }
        if (Step === 12) { Placed.Occupants[5].Inactive = true; assert.ok(Space.Update(Placed, Materials, Scale)); Verify("a static instance removed"); }
        if (Step === 16) { Scale = 3.5; assert.ok(Space.Update(Placed, Materials, Scale)); Verify("the emission scale changed"); }
        if (Step === 20) { Placed.Occupants[5].Inactive = false; assert.ok(Space.Update(Placed, Materials, Scale)); Verify("a static instance came back"); }
        if (Step === 24) { Scale = 0.05; assert.ok(Space.Update(Placed, Materials, Scale)); Verify("a dim scale: reaches shrink, cells empty out"); }
    }
    assert.equal(Space.Update(Placed, Materials, Scale), false, "nothing moved: nothing to rebuild");
    assert.ok(Checked >= 33 && Truncated > 0, `${Checked} frames checked, with truncated cells present in the checks (${Truncated})`);
});

test("the claim: light technique + BSDF technique with the partition reproduce the analytic irradiance", () =>
{
    // a 1 m × 1 m emitter facing down, 2 m above a diffuse receiver at the origin facing up
    const Height = 2.0, Le = 3.0;
    const Space = new EmissiveLightSpace();
    const Placed = Scene([Identity(0, 0, Height)]);
    Space.Construct(Placed, Materials); Space.Update(Placed, Materials, 1.0);
    const Position = [0, 0, 0], Normal = [0, 0, 1];
    const Words = CellWords(Space, LightClusterBounds, [LightClusterBounds.Minimum[0] + 1.0, LightClusterBounds.Minimum[1] + 1.0, 0.5]);
    // the grid places the receiver cell; for this unit test the receiver is wherever the cell is, so rebuild the world around it
    const Origin = [LightClusterBounds.Minimum[0] + 1.0, LightClusterBounds.Minimum[1] + 1.0, 0.0];
    const Moved = Scene([Identity(Origin[0], Origin[1], Height)]);
    const Space2 = new EmissiveLightSpace(); Space2.Construct(Moved, Materials); Space2.Update(Moved, Materials, 1.0);
    const Cell = CellWords(Space2, LightClusterBounds, Origin);
    assert.ok(Cell && Slot(Cell, 0) === 0, "the emitter is listed in the receiver's cell");
    // analytic: midpoint quadrature of ∫ Le cosθ_r cosθ_e / d² dA over the rectangle
    let Analytic = 0.0; const N = 400;
    for (let I = 0; I < N; ++I) for (let J = 0; J < N; ++J)
    {
        const X = -0.5 + (I + 0.5) / N, Y = -0.5 + (J + 0.5) / N, D2 = X * X + Y * Y + Height * Height, D = Math.sqrt(D2);
        Analytic += Le * (Height / D) * (Height / D) / D2 / (N * N);
    }
    let Seed = 20260117;
    const Rand = () => { Seed = (Math.imul(Seed, 1664525) + 1013904223) >>> 0; return Seed / 4294967296; };
    const Receiver = Origin, Samples = 400000;
    const Cosine = (Index) => 1;   // placeholder to keep the estimators below explicit
    let NeeOnly = 0.0, BsdfOnly = 0.0, Partition = 0.0;
    for (let K = 0; K < Samples; ++K)
    {
        // light technique
        const E = SampleEmissiveLight(Space2, Moved.Occupants, Materials, 1.0, Receiver, Cell, [Rand(), Rand(), Rand(), Rand()]);
        let Nee = 0.0, WeightNee = 0.0;
        if (E.Valid)
        {
            const To = [E.Position[0] - Receiver[0], E.Position[1] - Receiver[1], E.Position[2] - Receiver[2]], D = Math.hypot(...To), L = To.map((V) => V / D);
            const CosR = L[2], CosE = Math.abs(E.Normal[0] * L[0] + E.Normal[1] * L[1] + E.Normal[2] * L[2]);
            if (CosR > 0) { Nee = E.Radiance[0] * CosE * CosR / (D * D * E.Pdf); WeightNee = 1.0 - BsdfWeight(CosR / Math.PI, D, CosE); }
        }
        NeeOnly += Nee; Partition += Nee * WeightNee;
        // BSDF technique: a cosine-weighted direction, does it hit the emitter?
        const U = Rand(), V = Rand(), R = Math.sqrt(U), Phi = 2 * Math.PI * V;
        const Dir = [R * Math.cos(Phi), R * Math.sin(Phi), Math.sqrt(Math.max(0, 1 - U))];
        const T = Height / Dir[2];
        const Hx = Dir[0] * T, Hy = Dir[1] * T;
        if (Math.abs(Hx) <= 0.5 && Math.abs(Hy) <= 0.5)
        {
            const Dist = T, CosE = Dir[2];
            BsdfOnly += Le * Math.PI;
            Partition += Le * Math.PI * BsdfWeight(Dir[2] / Math.PI, Dist, CosE);
        }
    }
    NeeOnly /= Samples; BsdfOnly /= Samples; Partition /= Samples;
    const Relative = (Value) => Math.abs(Value - Analytic) / Analytic;
    assert.ok(Relative(NeeOnly) < 0.02, `light technique alone: ${NeeOnly.toFixed(4)} vs analytic ${Analytic.toFixed(4)}`);
    assert.ok(Relative(BsdfOnly) < 0.03, `BSDF technique alone: ${BsdfOnly.toFixed(4)} vs analytic ${Analytic.toFixed(4)}`);
    assert.ok(Relative(Partition) < 0.02, `the partition: ${Partition.toFixed(4)} vs analytic ${Analytic.toFixed(4)}`);
});

test("the claim, with several emitters: cluster choice and triangle choice stay unbiased", () =>
{
    // three emitters of different size, power and distance in the receiver's cell, the receiver tilted
    const Origin = [LightClusterBounds.Minimum[0] + 3.0, LightClusterBounds.Minimum[1] + 3.0, 0.0];
    const Facets = Rectangle(0.8, 0.5);
    const Placements = [Identity(Origin[0] + 1.0, Origin[1] + 0.5, 2.5), Identity(Origin[0] - 2.0, Origin[1] + 1.0, 3.5), Identity(Origin[0] + 0.5, Origin[1] - 3.0, 1.6)];
    const Placed = Scene(Placements, Facets);
    Placed.Occupants[1].Transform[0] = 2.0; Placed.Occupants[1].Transform[5] = 2.0;   // a bigger one (uniform scale 2 in x and y)
    const Space = new EmissiveLightSpace(); Space.Construct(Placed, Materials); Space.Update(Placed, Materials, 1.0);
    const Cell = CellWords(Space, LightClusterBounds, Origin);
    assert.ok(Cell, "a complete cell");
    const Normal = [0.3, 0.2, 0.93].map((V, _, A) => V / Math.hypot(...A));
    // reference: dense quadrature over each emitter's world triangles
    let Analytic = 0.0;
    for (const Occupant of Placed.Occupants)
    {
        const M = Occupant.Transform;
        for (const Facet of Facets)
        {
            const P = Facet.Corners.map((C) => [M[0] * C[0] + M[4] * C[1] + M[8] * C[2] + M[12], M[1] * C[0] + M[5] * C[1] + M[9] * C[2] + M[13], M[2] * C[0] + M[6] * C[1] + M[10] * C[2] + M[14]]);
            const E1 = [P[1][0] - P[0][0], P[1][1] - P[0][1], P[1][2] - P[0][2]], E2 = [P[2][0] - P[0][0], P[2][1] - P[0][1], P[2][2] - P[0][2]];
            const Cr = [E1[1] * E2[2] - E1[2] * E2[1], E1[2] * E2[0] - E1[0] * E2[2], E1[0] * E2[1] - E1[1] * E2[0]], Area = 0.5 * Math.hypot(...Cr), Ne = Cr.map((V) => V / (2 * Area));
            const Q = 200;
            for (let I = 0; I < Q; ++I) for (let J = 0; J < Q - I; ++J)
                for (const [U, V] of [[(I + 1 / 3) / Q, (J + 1 / 3) / Q], [(I + 2 / 3) / Q, (J + 2 / 3) / Q]])
                {
                    if (U + V > 1) continue;
                    const Point = [P[0][0] + E1[0] * U + E2[0] * V, P[0][1] + E1[1] * U + E2[1] * V, P[0][2] + E1[2] * U + E2[2] * V];
                    const To = [Point[0] - Origin[0], Point[1] - Origin[1], Point[2] - Origin[2]], D = Math.hypot(...To), L = To.map((X) => X / D);
                    const CosR = L[0] * Normal[0] + L[1] * Normal[1] + L[2] * Normal[2], CosE = Math.abs(Ne[0] * L[0] + Ne[1] * L[1] + Ne[2] * L[2]);
                    if (CosR > 0) Analytic += 3.0 * CosR * CosE / (D * D) * Area / (Q * Q) / 0.5 * 0.5 * (1 / 1) / 1 * 0.5 * 2 / 1 * (1 / 1) * (1) * 0.5 * 2 / 1;
                }
        }
    }
    // (the quadrature weight above: each lattice cell of the triangle carries Area / (Q² / 2) = 2·Area / Q², and two sample points per lattice cell share it)
    let Seed = 77;
    const Rand = () => { Seed = (Math.imul(Seed, 1664525) + 1013904223) >>> 0; return Seed / 4294967296; };
    let Estimate = 0.0; const Samples = 600000;
    for (let K = 0; K < Samples; ++K)
    {
        const E = SampleEmissiveLight(Space, Placed.Occupants, Materials, 1.0, Origin, Cell, [Rand(), Rand(), Rand(), Rand()]);
        if (!E.Valid) continue;
        const To = [E.Position[0] - Origin[0], E.Position[1] - Origin[1], E.Position[2] - Origin[2]], D = Math.hypot(...To), L = To.map((X) => X / D);
        const CosR = L[0] * Normal[0] + L[1] * Normal[1] + L[2] * Normal[2], CosE = Math.abs(E.Normal[0] * L[0] + E.Normal[1] * L[1] + E.Normal[2] * L[2]);
        if (CosR > 0) Estimate += E.Radiance[0] * CosR * CosE / (D * D * E.Pdf);
    }
    Estimate /= Samples;
    assert.ok(Analytic > 0.05, `a lit receiver (${Analytic.toFixed(4)})`);
    assert.ok(Math.abs(Estimate - Analytic) / Analytic < 0.03, `${Estimate.toFixed(4)} vs ${Analytic.toFixed(4)}`);
});

//------------------------------------------------------------------------------------------------------------------------
//   The real scene (null backend, Dawn validating): coverage, bounding spheres, powers, conservative cell lists.
//------------------------------------------------------------------------------------------------------------------------

test("the real scene: every emissive triangle is in the table once and IS its BVH triangle; spheres contain them; cell lists match an independent recomputation", () =>
{
    const Run = spawnSync(process.execPath, [path.join(Here, "HeadlessHarness.mjs"), "..", "3", "?capture=1"], { cwd: Here, encoding: "utf8", env: { ...process.env, HOOKS: path.join(Here, "EmissiveHooks.mjs") }, timeout: 120000 });
    assert.equal(Run.status, 0, (Run.stdout + Run.stderr).slice(-1500));
    const Line = Run.stdout.split("\n").find((Text) => Text.startsWith("EMISSIVE "));
    assert.ok(Line, "the hook reported");
    const Report = JSON.parse(Line.slice(9));
    assert.deepEqual(Report.Failures, [], Report.Failures.join("\n"));
    assert.ok(Report.Sets >= 5 && Report.Instances >= 50 && Report.Instances <= EmissiveCapacity.Clusters, `sets ${Report.Sets}, instances ${Report.Instances}`);
    assert.ok(Report.UsableCells === 1024 && Report.MeanListLength >= 1.0, `every cell has a list (mean length ${Report.MeanListLength})`);
    assert.equal(Report.CellsChecked, Report.CellsMatching, "every cell list equals an independent recomputation of its eight strongest candidates");
    assert.equal(Report.BvhMatched, Report.TableTriangles, "every table triangle IS the BVH triangle it names (geometry and family)");
    assert.ok(Report.TableTriangles >= 80 && Report.TableTriangles <= EmissiveCapacity.Triangles, `table triangles ${Report.TableTriangles}`);
    assert.ok(Report.Sampled > 400, "sampling worked from real cells");
});

//------------------------------------------------------------------------------------------------------------------------
//   Executed (RUN_GPU=1): the WGSL sampler beside its CPU twin.
//------------------------------------------------------------------------------------------------------------------------

test("executed: the shader's SampleEmissiveLight is the algorithm the unit tests verified", { skip: !Enabled, timeout: 900000 }, () =>
{
    const Run = spawnSync(process.execPath, [path.join(Here, "RunOnSoftwareGpu.mjs"), process.execPath, path.join(Here, "EmissiveParityGpu.mjs")], { cwd: Here, encoding: "utf8", timeout: 800000 });
    assert.equal(Run.status, 0, (Run.stdout + Run.stderr).slice(-1800));
    const Result = JSON.parse(Run.stdout.split("\n").find((Text) => Text.startsWith("RESULT ")).slice(7));
    assert.ok(Result.Count >= 2000 && Result.ValidFraction > 0.95, `valid samples: ${Result.ValidFraction}`);
    assert.ok(Result.SameChoice / Result.Count > 0.985, `same cluster and triangle in ${(100 * Result.SameChoice / Result.Count).toFixed(2)} % of the draws`);
    assert.ok(Result.WorstPosition < 2.0e-3 && Result.WorstPdfRelative < 2.0e-3 && Result.WorstRadiance < 1.0e-4, `positions ${Result.WorstPosition}, pdf ${Result.WorstPdfRelative}, radiance ${Result.WorstRadiance}`);
    assert.equal(Result.UsableMismatch, 0, "the shader and the reference agree on which cells are usable (inside the grid)");
    assert.equal(Result.ListedMismatch, 0, "the shader's IsListedEmitter equals the reference's on every probe");
    assert.ok(Result.ListedTrue > 50 && Result.ListedTrue < Result.ListedChecked - 50, `both outcomes of the membership test were exercised (${Result.ListedTrue} of ${Result.ListedChecked} listed)`);
    assert.ok(Result.Truncated > 0, "the synthetic world has cells with more candidates than slots");
});
