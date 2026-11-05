//============================================================================================================================================
//                                                         EMISSIVELIGHTSPACE.JS
//============================================================================================================================================
// 🧩 The second clustered-light structure: EMISSIVE SURFACES as lights. Emissive triangles are grouped into small spatial clusters, every cluster instance (a cluster on a
//    placed occupant) is binned into the same 16 × 16 × 4 world grid the point, beam and flood lights use, and the ray tracer's next-event estimation samples them there.

// 📝 Why. Until now an emitter (a head or tail lamp, a street lamp's lens, the tunnel strips, a traffic head) lit the world only when a path happened to HIT it: a cosine or BSDF ray
//    landing on a few square centimetres of lens, then down-weighted by a heuristic (0.15 to 1) that had no light pdf to balance against, so it lost energy as well as being noisy.
//    Sampling the emitters directly, with exact area pdfs, makes their light low-variance and lets the partition below put the energy back.
//
// 📝 The structure (all of it lives inside LightClusterRecords, so every consumer reads one binding; LightClusterSpace.wgsl declares the records):
//      EmissiveTriangles   the emissive triangles of each facet set, local space: A, E1, E2 and the material family. A set's triangles are ordered so that every cluster is a
//                          contiguous run of ≤ 8 (recursive median split of the centroids). The table is per SET, shared by every occupant of it (sixty pole heads, one table entry).
//      EmissiveClusters    one record per cluster INSTANCE: world centre and radius, occupant, first triangle, count, and the cluster's power (area × luminance, world).
//      EmissiveCells       per grid cell, up to 8 cluster ids (u16, empty = 0x7FFF): the cell's light technique draws from exactly this list, the strongest by power / distance²
//                          at the cell. A cell with more candidates keeps the eight strongest; the rest are simply not part of that cell's light technique.
//
// 📝 Unbiased by construction, with no approximation. For an emitter point the BSDF technique keeps its old weight w_b(ω) (the renderer's heuristic) and the light technique takes
//    1 − w_b(ω) — any partition of unity is a valid combination — but ONLY where the light technique can actually draw that point: the point's triangle is in a cluster listed in the
//    cell of the vertex it is seen from (the shader tests exactly that with the BVH triangle index stored here). Every other emitter point, listed nowhere, outside the grid, or with the
//    feature off, takes weight 1 from the BSDF technique, as it always did. The 'reach' below only decides which clusters are candidates for a cell's eight slots; it is not a cutoff
//    on correctness.

import { LightClusterBounds, EmissiveCapacity } from "./LightClusterSpace.js";
import { OccupantRole } from "./FacetStructure.js";

export const EmissiveFloor = 0.5;              // [-] luminance of an emission below which a family is not a light (matches the Frontier emissive-table floor)
export const EmissiveReachThreshold = 5.0e-4;  // [-] irradiance at which a cluster stops being listed
export const EmissiveClusterSize = 8;          // [-] triangles per cluster at most
export const EmissiveEmptySlot = 0x7FFF;

const CellCount = LightClusterBounds.Resolution[0] * LightClusterBounds.Resolution[1] * LightClusterBounds.Resolution[2];
const TriangleStride = 48, ClusterStride = 32;
export { EmissiveCapacity };

export const Luminance = (Emission) => 0.2126 * Emission[0] + 0.7152 * Emission[1] + 0.0722 * Emission[2];

const CellSize = LightClusterBounds.Extent.map((Value, Axis) => Value / LightClusterBounds.Resolution[Axis]);

// 📝 Inserts one candidate into a cell's list, which is kept sorted (importance descending, slot ascending) and at most Capacity long; returns the new length. Insertion keeps the
//    exact order a full sort of every candidate would give, so a list built candidate by candidate is the list built all at once.
function InsertCandidate(Slots, Importances, Base, Count, Capacity, Slot, Importance)
{
    let Position = Count;
    while (Position > 0 && (Importance > Importances[Base + Position - 1] || (Importance === Importances[Base + Position - 1] && Slot < Slots[Base + Position - 1]))) Position--;
    if (Position >= Capacity) return Count;                    // not among the Capacity strongest
    for (let K = Math.min(Count, Capacity - 1); K > Position; --K) { Slots[Base + K] = Slots[Base + K - 1]; Importances[Base + K] = Importances[Base + K - 1]; }
    Slots[Base + Position] = Slot; Importances[Base + Position] = Importance;
    return Math.min(Count + 1, Capacity);
}

// The per-cell lists of one set of instances: up to Slots (slot id, importance) per cell, how many each cell holds, and how many candidates each cell was offered in all.
function CellLists()
{
    return { Slot: new Uint16Array(CellCount * EmissiveCapacity.Slots), Importance: new Float64Array(CellCount * EmissiveCapacity.Slots), Count: new Int8Array(CellCount), Total: new Uint16Array(CellCount) };
}

// 📝 Splits a list of triangle records into runs of at most Limit by recursive median split of the centroids along the longest axis; the order of the runs is the table order.
export function PartitionTriangles(Triangles, Limit = EmissiveClusterSize)
{
    if (Triangles.length <= Limit) return [Triangles];
    let Low = [Infinity, Infinity, Infinity], High = [-Infinity, -Infinity, -Infinity];
    for (const Triangle of Triangles)
        for (let Axis = 0; Axis < 3; ++Axis) { Low[Axis] = Math.min(Low[Axis], Triangle.Centroid[Axis]); High[Axis] = Math.max(High[Axis], Triangle.Centroid[Axis]); }
    const Extent = High.map((Value, Axis) => Value - Low[Axis]);
    const Axis = Extent[0] >= Extent[1] && Extent[0] >= Extent[2] ? 0 : (Extent[1] >= Extent[2] ? 1 : 2);
    const Sorted = [...Triangles].sort((A, B) => A.Centroid[Axis] - B.Centroid[Axis] || A.Index - B.Index);
    const Middle = Sorted.length >> 1;
    return [...PartitionTriangles(Sorted.slice(0, Middle), Limit), ...PartitionTriangles(Sorted.slice(Middle), Limit)];
}

export class EmissiveLightSpace
{
    constructor()
    {
        this.TriangleBytes = new ArrayBuffer(EmissiveCapacity.Triangles * TriangleStride);
        this.TriangleFloats = new Float32Array(this.TriangleBytes);
        this.TriangleWords = new Uint32Array(this.TriangleBytes);
        this.ClusterBytes = new ArrayBuffer(EmissiveCapacity.Clusters * ClusterStride);
        this.ClusterFloats = new Float32Array(this.ClusterBytes);
        this.ClusterWords = new Uint32Array(this.ClusterBytes);
        this.Cells = new Uint32Array(CellCount * 4);
        this.Sets = new Map();                 // facet set index → { Clusters: [{ First, Count, Centre, Radius }], Triangles }
        this.Instances = [];                   // { Occupant, Set, Cluster, Dynamic } in cluster order
        this.Included = new Set();             // occupant indices represented in the table
        this.Count = 0;                        // cluster instances in the table; 0 means the structure is off
        this.TriangleCount = 0;
        this.Statistics = { Sets: 0, Triangles: 0, Instances: 0, TruncatedCells: 0, EmptyCells: 0, Excluded: 0, BuildMilliseconds: 0.0 };
        this.StaticSignature = "";
        this.DynamicMatrices = new Map();
        this.LastScale = NaN;
        this.MembershipChanged = false;
        // 📝 Incremental binning. The cell lists of the instances that never move (street lamps, the tunnel, the hangar: all but the car's lamps) are built once, and only
        //    rebuilt when those change. Each frame the few MOVING instances are merged into a copy of them. The eight strongest of a union are always among (the eight strongest of
        //    one part) ∪ (the other part), so the merged lists are exactly the lists a full rebuild would give: only the work differs (about 1 ms against about 30 ms a frame).
        this.Static = CellLists();             // the unmoving instances' lists
        this.Work = CellLists();               // this frame's lists in the cells the moving instances touch (Count −1 = not touched)
        this.StaticCells = new Uint32Array(CellCount * 4);
        this.StaticStatistics = { Truncated: 0, Empty: CellCount };
        this.StaticBuilt = false;
        this.Touched = new Int32Array(CellCount);
        this.TouchedCount = 0;
    }

    // 📝 Per facet set: its emissive triangles (a family whose emission luminance reaches the floor), grouped into clusters and appended to the table. Sets that no longer fit
    //    the table's capacity are left out and their occupants stay unrepresented, which the shader treats as "not an NEE light".
    // 📝 RayTracing: the RayTracingSpace whose BVH the shader traverses; its facet → triangle slots let the table name each triangle by its BVH index (0xFFFFFFFF when absent).
    Construct(Scene, Materials, RayTracing = null)
    {
        const Started = performance.now();
        this.Sets.clear();
        let Cursor = 0, Excluded = 0;
        Scene.FacetSets.forEach((FacetSet, SetIndex) =>
        {
            if (!FacetSet.Facets || FacetSet.Facets.length === 0) return;
            const Triangles = [];
            FacetSet.Facets.forEach((Facet, Index) =>
            {
                const Material = Materials[Facet.Family];
                if (!Material || Luminance(Material.Emission) < EmissiveFloor) return;
                const [A, B, C] = Facet.Corners;
                const E1 = [B[0] - A[0], B[1] - A[1], B[2] - A[2]], E2 = [C[0] - A[0], C[1] - A[1], C[2] - A[2]];
                const Cross = [E1[1] * E2[2] - E1[2] * E2[1], E1[2] * E2[0] - E1[0] * E2[2], E1[0] * E2[1] - E1[1] * E2[0]];
                const Area = 0.5 * Math.hypot(...Cross);
                if (!(Area > 1.0e-9)) return;
                const Slot = FacetSet.TriangleSlots ? FacetSet.TriangleSlots[Index] : 0xFFFFFFFF;
                Triangles.push({ Index, Bvh: Slot, A, E1, E2, Area, Family: Facet.Family, Centroid: [(A[0] + B[0] + C[0]) / 3, (A[1] + B[1] + C[1]) / 3, (A[2] + B[2] + C[2]) / 3] });
            });
            if (Triangles.length === 0) return;
            if (Cursor + Triangles.length > EmissiveCapacity.Triangles) { Excluded += 1; return; }
            const Clusters = [];
            for (const Run of PartitionTriangles(Triangles))
            {
                const First = Cursor;
                const Low = [Infinity, Infinity, Infinity], High = [-Infinity, -Infinity, -Infinity];
                for (const Triangle of Run)
                {
                    const O = Cursor * 12;
                    this.TriangleFloats.set([Triangle.A[0], Triangle.A[1], Triangle.A[2], 0.0, Triangle.E1[0], Triangle.E1[1], Triangle.E1[2], Triangle.Area, Triangle.E2[0], Triangle.E2[1], Triangle.E2[2], 0.0], O);
                    this.TriangleWords[O + 3] = Triangle.Bvh >>> 0;      // A.w: the triangle's index in the BVH triangle buffer
                    this.TriangleWords[O + 11] = Triangle.Family;
                    for (const Corner of [Triangle.A, [Triangle.A[0] + Triangle.E1[0], Triangle.A[1] + Triangle.E1[1], Triangle.A[2] + Triangle.E1[2]],
                                          [Triangle.A[0] + Triangle.E2[0], Triangle.A[1] + Triangle.E2[1], Triangle.A[2] + Triangle.E2[2]]])
                        for (let Axis = 0; Axis < 3; ++Axis) { Low[Axis] = Math.min(Low[Axis], Corner[Axis]); High[Axis] = Math.max(High[Axis], Corner[Axis]); }
                    Cursor++;
                }
                const Centre = Low.map((Value, Axis) => 0.5 * (Value + High[Axis]));
                Clusters.push({ First, Count: Run.length, Centre, Radius: 0.5 * Math.hypot(High[0] - Low[0], High[1] - Low[1], High[2] - Low[2]), Triangles: Run });
            }
            this.Sets.set(SetIndex, { Clusters });
        });
        this.TriangleCount = Cursor;
        this.Statistics.Sets = this.Sets.size;
        this.Statistics.Triangles = Cursor;
        this.Statistics.Excluded = Excluded;
        this.Statistics.BuildMilliseconds = performance.now() - Started;
        this.StaticSignature = "";
        this.Constructed = true;
        return this.Sets.size;
    }

    // 📝 The world cluster of one instance: centre, radius (largest axis scale) and power (area × luminance, areas scaled by the occupant's uniform scale squared).
    InstanceOf(Cluster, Transform, Materials)
    {
        const S0 = Math.hypot(Transform[0], Transform[1], Transform[2]), S1 = Math.hypot(Transform[4], Transform[5], Transform[6]), S2 = Math.hypot(Transform[8], Transform[9], Transform[10]);
        const Scale = Math.max(S0, S1, S2), Scale2 = S0 * S1;
        const [X, Y, Z] = Cluster.Centre;
        let Power = 0.0;
        for (const Triangle of Cluster.Triangles) Power += Triangle.Area * Luminance(Materials[Triangle.Family].Emission);
        return { Centre: [Transform[0] * X + Transform[4] * Y + Transform[8] * Z + Transform[12], Transform[1] * X + Transform[5] * Y + Transform[9] * Z + Transform[13],
                          Transform[2] * X + Transform[6] * Y + Transform[10] * Z + Transform[14]], Radius: Cluster.Radius * Scale, Power: Power * Scale2 };
    }

    // 📝 Refreshes the instances and the cell lists from the scene; returns whether anything the GPU holds changed. EmissionScale multiplies every emission (the host's slider), so it
    //    scales the powers the lists are ranked by (ranking only: the shader reads the live material emission).
    Update(Scene, Materials, EmissionScale = 1.0)
    {
        if (!this.Constructed) return false;
        const Started = performance.now();
        const Occupants = Scene.Occupants;
        // 1. which occupants are represented: active, not carrying a material override (an override changes the family of every triangle), their set has emissive clusters
        const Included = [];
        Occupants.forEach((Occupant, Index) =>
        {
            if (Occupant.Inactive || !this.Sets.has(Occupant.FacetSet)) return;
            if (Occupant.MaterialOverride !== undefined && Occupant.MaterialOverride >= 0) return;
            if (!(Occupant.Roles & OccupantRole.Raster)) return;
            Included.push(Index);
        });
        const Instances = [];
        for (const Index of Included)
            for (const Cluster of this.Sets.get(Occupants[Index].FacetSet).Clusters)
                Instances.push({ Occupant: Index, Cluster, Dynamic: (Occupants[Index].Roles & OccupantRole.Dynamic) !== 0 });
        let Changed = false;
        // 2. the instance table; a different set of instances, or any moved transform, rewrites it
        const Signature = Instances.map((Instance) => `${Instance.Occupant}:${Instance.Cluster.First}:${Instance.Dynamic ? 1 : 0}`).join(",");
        let StaticDirty = false;                  // whether anything the unmoving instances' lists depend on changed
        if (EmissionScale !== this.LastScale) { this.LastScale = EmissionScale; this.DynamicMatrices.clear(); Changed = true; StaticDirty = true; }
        const Capacity = EmissiveCapacity.Clusters;
        const Overfull = Instances.length > Capacity;
        const Kept = Overfull ? [] : Instances;
        if (Signature !== this.StaticSignature)
        {
            const Before = this.Included;
            this.StaticSignature = Signature; this.Instances = Kept; this.Included = new Set(Overfull ? [] : Included);
            this.DynamicMatrices.clear(); Changed = true; StaticDirty = true;
            this.MembershipChanged = true;
            // 📝 Mirror the membership into the occupants' roles; the host re-uploads the occupant records when MembershipChanged is set.
            for (const Index of Before) if (!this.Included.has(Index)) Occupants[Index].Roles &= ~OccupantRole.EmissiveCluster;
            for (const Index of this.Included) Occupants[Index].Roles |= OccupantRole.EmissiveCluster;
        }
        const Moved = [];
        this.Instances.forEach((Instance, Slot) =>
        {
            const Transform = Occupants[Instance.Occupant].Transform;
            const Previous = this.DynamicMatrices.get(Slot);
            let Same = Previous !== undefined;
            if (Same) for (let K = 0; K < 16; ++K) if (Previous[K] !== Transform[K]) { Same = false; break; }
            if (!Same) { this.DynamicMatrices.set(Slot, Float32Array.from(Transform)); Moved.push(Slot); if (!Instance.Dynamic) StaticDirty = true; }
        });
        const World = this.World || (this.World = []);
        for (const Slot of Moved)
        {
            const Instance = this.Instances[Slot];
            World[Slot] = this.InstanceOf(Instance.Cluster, Occupants[Instance.Occupant].Transform, Materials);
            const W = World[Slot], Cluster = Instance.Cluster;
            const O = Slot * 8;
            this.ClusterFloats.set([W.Centre[0], W.Centre[1], W.Centre[2], W.Radius], O);
            this.ClusterWords[O + 4] = Instance.Occupant; this.ClusterWords[O + 5] = Cluster.First; this.ClusterWords[O + 6] = Cluster.Count;
            this.ClusterFloats[O + 7] = W.Power * EmissionScale;
        }
        if (Moved.length > 0) Changed = true;
        this.Count = this.Instances.length;
        this.Statistics.Instances = this.Count;
        if (!Changed) return false;
        if (Overfull)
            console.warn(`[lights] ${Instances.length} emissive cluster instances exceed the table's ${Capacity}: the emissive light structure is off (emission falls back to its BSDF-hit weight)`);
        this.BinIntoGrid(World, EmissionScale, StaticDirty);
        this.Statistics.BuildMilliseconds = performance.now() - Started;
        return true;
    }

    // 📝 The cell lists. A cluster is listed in every cell its reach (the distance where its irradiance P / d² drops below EmissiveReachThreshold) touches; a cell keeps the eight strongest
    //    by power / (distance² + radius²) at the cell centre (ties: the lower cluster id). The unmoving instances' lists are cached (StaticDirty rebuilds them); the moving ones are merged in.
    BinIntoGrid(World, EmissionScale, StaticDirty = true)
    {
        const Slots = EmissiveCapacity.Slots;
        const StaticSlots = [], DynamicSlots = [];
        this.Instances.forEach((Instance, Slot) => { if (World[Slot]) (Instance.Dynamic ? DynamicSlots : StaticSlots).push(Slot); });
        if (StaticDirty || !this.StaticBuilt)
        {
            this.Static.Count.fill(0); this.Static.Total.fill(0);
            for (const Slot of StaticSlots) this.Accumulate(Slot, World[Slot], EmissionScale, this.Static, false);
            let Truncated = 0, Empty = 0;
            for (let Cell = 0; Cell < CellCount; ++Cell)
            {
                this.WriteCell(this.StaticCells, Cell, this.Static);
                if (this.Static.Total[Cell] > Slots) Truncated++; else if (this.Static.Total[Cell] === 0) Empty++;
            }
            this.StaticStatistics = { Truncated, Empty };
            this.StaticBuilt = true;
        }
        this.Cells.set(this.StaticCells);
        let Truncated = this.StaticStatistics.Truncated, Empty = this.StaticStatistics.Empty;
        this.TouchedCount = 0;
        if (DynamicSlots.length)
        {
            this.Work.Count.fill(-1);
            for (const Slot of DynamicSlots) this.Accumulate(Slot, World[Slot], EmissionScale, this.Work, true);
            for (let Index = 0; Index < this.TouchedCount; ++Index)
            {
                const Cell = this.Touched[Index];
                const Before = this.Static.Total[Cell], After = this.Work.Total[Cell];
                if (Before > Slots) Truncated--; else if (Before === 0) Empty--;
                if (After > Slots) Truncated++; else if (After === 0) Empty++;
                this.WriteCell(this.Cells, Cell, this.Work);
            }
        }
        this.Statistics.TruncatedCells = Truncated; this.Statistics.EmptyCells = Empty;
    }

    // Offers one cluster instance to every cell its reach touches. Into Target.Slot / Importance; a Lazy target (this frame's lists) first copies the cell's cached static list.
    Accumulate(Slot, W, EmissionScale, Target, Lazy)
    {
        const Power = W.Power * EmissionScale;
        if (!(Power > 0.0)) return;
        const Slots = EmissiveCapacity.Slots;
        const Minimum = LightClusterBounds.Minimum, Resolution = LightClusterBounds.Resolution, Size = CellSize;
        const Reach = Math.sqrt(Power / EmissiveReachThreshold) + W.Radius;
        const Centre = W.Centre, Radius2 = W.Radius * W.Radius + 1.0e-4;
        const X0 = Math.max(0, Math.floor((Centre[0] - Reach - Minimum[0]) / Size[0])), X1 = Math.min(Resolution[0] - 1, Math.floor((Centre[0] + Reach - Minimum[0]) / Size[0]));
        const Y0 = Math.max(0, Math.floor((Centre[1] - Reach - Minimum[1]) / Size[1])), Y1 = Math.min(Resolution[1] - 1, Math.floor((Centre[1] + Reach - Minimum[1]) / Size[1]));
        const Z0 = Math.max(0, Math.floor((Centre[2] - Reach - Minimum[2]) / Size[2])), Z1 = Math.min(Resolution[2] - 1, Math.floor((Centre[2] + Reach - Minimum[2]) / Size[2]));
        for (let Z = Z0; Z <= Z1; ++Z)
            for (let Y = Y0; Y <= Y1; ++Y)
                for (let X = X0; X <= X1; ++X)
                {
                    // the same expressions, in the same order, as the independent recomputation in Tools/EmissiveHooks.mjs: equal to the last bit
                    const CornerX = Minimum[0] + X * Size[0], CornerY = Minimum[1] + Y * Size[1], CornerZ = Minimum[2] + Z * Size[2];
                    let Nearest2 = 0.0, Centre2 = 0.0;
                    Nearest2 += (Centre[0] - Math.min(Math.max(Centre[0], CornerX), CornerX + Size[0])) ** 2;
                    Centre2  += (Centre[0] - (CornerX + 0.5 * Size[0])) ** 2;
                    Nearest2 += (Centre[1] - Math.min(Math.max(Centre[1], CornerY), CornerY + Size[1])) ** 2;
                    Centre2  += (Centre[1] - (CornerY + 0.5 * Size[1])) ** 2;
                    Nearest2 += (Centre[2] - Math.min(Math.max(Centre[2], CornerZ), CornerZ + Size[2])) ** 2;
                    Centre2  += (Centre[2] - (CornerZ + 0.5 * Size[2])) ** 2;
                    if (Nearest2 >= Reach * Reach) continue;
                    const Cell = X + Resolution[0] * (Y + Resolution[1] * Z);
                    if (Lazy && Target.Count[Cell] < 0)
                    {
                        const Cached = this.Static, Base = Cell * Slots, Count = Cached.Count[Cell];
                        for (let K = 0; K < Count; ++K) { Target.Slot[Base + K] = Cached.Slot[Base + K]; Target.Importance[Base + K] = Cached.Importance[Base + K]; }
                        Target.Count[Cell] = Count; Target.Total[Cell] = Cached.Total[Cell];
                        this.Touched[this.TouchedCount++] = Cell;
                    }
                    Target.Total[Cell]++;
                    Target.Count[Cell] = InsertCandidate(Target.Slot, Target.Importance, Cell * Slots, Target.Count[Cell], Slots, Slot, Power / (Centre2 + Radius2));
                }
    }

    // One cell's eight cluster ids as four words (two u16 each; 0x7FFF = empty), from a set of lists.
    WriteCell(Words, Cell, Lists)
    {
        const Slots = EmissiveCapacity.Slots, Base = Cell * Slots, Count = Lists.Count[Cell];
        const Id = (K) => K < Count ? Lists.Slot[Base + K] : EmissiveEmptySlot;
        for (let Word = 0; Word < 4; ++Word) Words[Cell * 4 + Word] = (Id(2 * Word) | (Id(2 * Word + 1) << 16)) >>> 0;
    }

    // 📝 Whether the roles flag should be set on an occupant (it is represented in the table), for the host to mirror into the occupant records.
    Represents(OccupantIndex) { return this.Included.has(OccupantIndex); }
}
