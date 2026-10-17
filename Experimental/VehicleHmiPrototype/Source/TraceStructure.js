//============================================================================================================================================
//                                                            TRACESTRUCTURE.JS
//============================================================================================================================================
// 🧩 The ray-tracing acceleration structure: a binned-SAH BVH per facet set (BLAS), a per-frame BVH over the drawn
//    occupants (TLAS), the triangle records the traversal reads, the emissive-triangle light table and the trace record.

// 📝 WebGPU exposes no ray-tracing API, so the path tracer traverses these on compute shaders (TraceTraversal.wgsl).
//    Everything the raster draws is traced: the static level, the ControlVehicle (body and four wheels as rigid
//    instances — the vertex-stage tyre deformation is a few centimetres and is left to the primary G-buffer), the cones,
//    the intact props, the stress pool and every live fragment, whose convex hull gets a BLAS of its own the frame it is
//    born (FractureSequence → AssignFragments). The TLAS is rebuilt every frame on the CPU — a few hundred to two
//    thousand instances take well under a millisecond — and uploaded with the instance records.
//
//    Buffers (group 1 of every path-tracing program, TraceRecords.wgsl):
//      Nodes      32 B  min.xyz · LeftOrFirst · max.xyz · Count          (Count 0: interior, children at LeftOrFirst, +1)
//      Triangles  64 B  V0 · family|offset · E1 · n0 · E2 · n1 · n2 · original index · pad   (BLAS leaf order)
//      Instances  80 B  WorldToLocal · BlasRoot · Occupant · Flags · pad  (TLAS leaf order)
//      LightTable       32 point lights (LightRecord, 64 B) then the emissive-triangle entries (16 B, power CDF)
//      TraceRecord      uniform: counts, roots, the sun's disc and the ReSTIR settings (EncodeTraceRecord)

import { OccupantRole, VertexStride } from "./FacetStructure.js";
import { CourseMaterial } from "./MaterialSpecification.js";
import { LightBufferBytes, MaxSceneLights } from "./LightingSystem.js";
import { TransformBounds, InvertTransform } from "./TransformProjection.js";

export const NodeStride     = 32;   // [B]
export const TriangleStride = 64;   // [B]
export const InstanceStride = 80;   // [B]
export const EmissiveStride = 16;   // [B]
export const TraceRecordExtent = 128;   // [B] TraceRecord in TraceRecords.wgsl (32 words)

// 📝 Instance flags (TraceRecords.wgsl InstanceFlag*).
export const InstanceFlag = Object.freeze({ Vehicle: 1, SkipTyre: 2, Fragment: 4 });

const LeafTriangles = 4;    // [-] largest BLAS leaf
const LeafInstances = 2;    // [-] largest TLAS leaf
const BinCount      = 12;   // [-] SAH bins per axis
const EmissionFloor = 0.5;  // [-] emission luminance below which a family is not a sampled light (cone bands, hubs)

//------------------------------------------------------------------------------------------------------------------------
//                                                  HIERARCHY BUILDER
//------------------------------------------------------------------------------------------------------------------------

// 📐 Binned SAH over N primitives given their bounds and centroids: nodes as 8 words each (floats and u32s share the
//    words through the two views), primitives reordered into leaf order (Order). Children are allocated as a pair so
//    the right child is always LeftOrFirst + 1. Node indices are local to this build; the caller rebases them.
export function BuildHierarchy(Count, BoundsMinimum, BoundsMaximum, Centroids, LeafSize)
{
    const Capacity = Math.max(1, 2 * Count - 1);
    const Words = new ArrayBuffer(Capacity * NodeStride);
    const Scalars = new Float32Array(Words);
    const Integers = new Uint32Array(Words);
    const Order = new Uint32Array(Count);
    for (let Index = 0; Index < Count; ++Index)
        Order[Index] = Index;
    let NodeCount = 1;
    const BinMinimum = new Float32Array(BinCount * 3), BinMaximum = new Float32Array(BinCount * 3), BinCounts = new Uint32Array(BinCount);
    const LeftArea = new Float32Array(BinCount), LeftCount = new Uint32Array(BinCount);

    const WriteBounds = (Node, First, Last) =>
    {
        let MinX = Infinity, MinY = Infinity, MinZ = Infinity, MaxX = -Infinity, MaxY = -Infinity, MaxZ = -Infinity;
        for (let Slot = First; Slot < Last; ++Slot)
        {
            const Primitive = Order[Slot] * 3;
            MinX = Math.min(MinX, BoundsMinimum[Primitive]);     MaxX = Math.max(MaxX, BoundsMaximum[Primitive]);
            MinY = Math.min(MinY, BoundsMinimum[Primitive + 1]); MaxY = Math.max(MaxY, BoundsMaximum[Primitive + 1]);
            MinZ = Math.min(MinZ, BoundsMinimum[Primitive + 2]); MaxZ = Math.max(MaxZ, BoundsMaximum[Primitive + 2]);
        }
        const Offset = Node * 8;
        Scalars[Offset] = MinX; Scalars[Offset + 1] = MinY; Scalars[Offset + 2] = MinZ;
        Scalars[Offset + 4] = MaxX; Scalars[Offset + 5] = MaxY; Scalars[Offset + 6] = MaxZ;
    };
    const Area = (MinX, MinY, MinZ, MaxX, MaxY, MaxZ) =>
    {
        const X = Math.max(0, MaxX - MinX), Y = Math.max(0, MaxY - MinY), Z = Math.max(0, MaxZ - MinZ);
        return 2.0 * (X * Y + Y * Z + Z * X);
    };

    const Stack = [[0, 0, Count]];
    while (Stack.length > 0)
    {
        const [Node, First, Last] = Stack.pop();
        WriteBounds(Node, First, Last);
        const Offset = Node * 8;
        const Span = Last - First;
        if (Span <= LeafSize)
        {
            Integers[Offset + 3] = First;
            Integers[Offset + 7] = Span;
            continue;
        }
        // centroid bounds choose the axis
        let CMin = [Infinity, Infinity, Infinity], CMax = [-Infinity, -Infinity, -Infinity];
        for (let Slot = First; Slot < Last; ++Slot)
        {
            const Primitive = Order[Slot] * 3;
            for (let Axis = 0; Axis < 3; ++Axis)
            {
                CMin[Axis] = Math.min(CMin[Axis], Centroids[Primitive + Axis]);
                CMax[Axis] = Math.max(CMax[Axis], Centroids[Primitive + Axis]);
            }
        }
        let Axis = 0;
        for (let Candidate = 1; Candidate < 3; ++Candidate)
            if (CMax[Candidate] - CMin[Candidate] > CMax[Axis] - CMin[Axis]) Axis = Candidate;
        const Extent = CMax[Axis] - CMin[Axis];
        let Split = -1, Middle = First;
        if (Extent > 1.0e-7)
        {
            BinMinimum.fill(Infinity); BinMaximum.fill(-Infinity); BinCounts.fill(0);
            const Scale = BinCount / Extent;
            for (let Slot = First; Slot < Last; ++Slot)
            {
                const Primitive = Order[Slot] * 3;
                const Bin = Math.min(BinCount - 1, Math.max(0, Math.floor((Centroids[Primitive + Axis] - CMin[Axis]) * Scale)));
                BinCounts[Bin]++;
                for (let Component = 0; Component < 3; ++Component)
                {
                    BinMinimum[Bin * 3 + Component] = Math.min(BinMinimum[Bin * 3 + Component], BoundsMinimum[Primitive + Component]);
                    BinMaximum[Bin * 3 + Component] = Math.max(BinMaximum[Bin * 3 + Component], BoundsMaximum[Primitive + Component]);
                }
            }
            // sweep left
            let LMinX = Infinity, LMinY = Infinity, LMinZ = Infinity, LMaxX = -Infinity, LMaxY = -Infinity, LMaxZ = -Infinity, LCount = 0;
            for (let Bin = 0; Bin < BinCount - 1; ++Bin)
            {
                LCount += BinCounts[Bin];
                LMinX = Math.min(LMinX, BinMinimum[Bin * 3]); LMinY = Math.min(LMinY, BinMinimum[Bin * 3 + 1]); LMinZ = Math.min(LMinZ, BinMinimum[Bin * 3 + 2]);
                LMaxX = Math.max(LMaxX, BinMaximum[Bin * 3]); LMaxY = Math.max(LMaxY, BinMaximum[Bin * 3 + 1]); LMaxZ = Math.max(LMaxZ, BinMaximum[Bin * 3 + 2]);
                LeftCount[Bin] = LCount;
                LeftArea[Bin] = LCount > 0 ? Area(LMinX, LMinY, LMinZ, LMaxX, LMaxY, LMaxZ) : 0.0;
            }
            // sweep right
            let RMinX = Infinity, RMinY = Infinity, RMinZ = Infinity, RMaxX = -Infinity, RMaxY = -Infinity, RMaxZ = -Infinity, RCount = 0;
            let BestCost = Infinity;
            for (let Bin = BinCount - 1; Bin >= 1; --Bin)
            {
                RCount += BinCounts[Bin];
                RMinX = Math.min(RMinX, BinMinimum[Bin * 3]); RMinY = Math.min(RMinY, BinMinimum[Bin * 3 + 1]); RMinZ = Math.min(RMinZ, BinMinimum[Bin * 3 + 2]);
                RMaxX = Math.max(RMaxX, BinMaximum[Bin * 3]); RMaxY = Math.max(RMaxY, BinMaximum[Bin * 3 + 1]); RMaxZ = Math.max(RMaxZ, BinMaximum[Bin * 3 + 2]);
                const Left = Bin - 1;
                if (LeftCount[Left] === 0 || RCount === 0)
                    continue;
                const Cost = LeftArea[Left] * LeftCount[Left] + (RCount > 0 ? Area(RMinX, RMinY, RMinZ, RMaxX, RMaxY, RMaxZ) : 0.0) * RCount;
                if (Cost < BestCost)
                {
                    BestCost = Cost;
                    Split = Bin;
                }
            }
            if (Split >= 0)
            {
                const ParentArea = Area(Scalars[Offset], Scalars[Offset + 1], Scalars[Offset + 2], Scalars[Offset + 4], Scalars[Offset + 5], Scalars[Offset + 6]);
                // 📝 A leaf costs Span intersections; a split costs a traversal plus the SAH estimate.
                if (BestCost / Math.max(ParentArea, 1.0e-12) + 1.0 >= Span && Span <= 2 * LeafSize)
                    Split = -1;
            }
            if (Split >= 0)
            {
                // partition Order[First, Last) by bin < Split
                let Low = First, High = Last - 1;
                while (Low <= High)
                {
                    const Primitive = Order[Low] * 3;
                    const Bin = Math.min(BinCount - 1, Math.max(0, Math.floor((Centroids[Primitive + Axis] - CMin[Axis]) * Scale)));
                    if (Bin < Split)
                        ++Low;
                    else
                    {
                        const Swap = Order[Low]; Order[Low] = Order[High]; Order[High] = Swap;
                        --High;
                    }
                }
                Middle = Low;
            }
        }
        if (Split < 0 || Middle === First || Middle === Last)
        {
            // 📝 Degenerate centroids: a median split keeps the tree balanced instead of making a giant leaf.
            if (Span > 2 * LeafSize)
            {
                const Slice = Array.from(Order.subarray(First, Last));
                Slice.sort((A, B) => Centroids[A * 3 + Axis] - Centroids[B * 3 + Axis]);
                Order.set(Slice, First);
                Middle = First + (Span >> 1);
            }
            else
            {
                Integers[Offset + 3] = First;
                Integers[Offset + 7] = Span;
                continue;
            }
        }
        const Left = NodeCount;
        NodeCount += 2;
        Integers[Offset + 3] = Left;
        Integers[Offset + 7] = 0;
        Stack.push([Left, First, Middle]);
        Stack.push([Left + 1, Middle, Last]);
    }
    return { Nodes: Words.slice(0, NodeCount * NodeStride), NodeCount, Order };
}

// 📐 Bounds and centroids of T triangles given 9 floats each.
function ProjectTriangleBounds(Corners, Count)
{
    const Minimum = new Float32Array(Count * 3), Maximum = new Float32Array(Count * 3), Centroid = new Float32Array(Count * 3);
    for (let Triangle = 0; Triangle < Count; ++Triangle)
    {
        const Base = Triangle * 9;
        for (let Axis = 0; Axis < 3; ++Axis)
        {
            const A = Corners[Base + Axis], B = Corners[Base + 3 + Axis], C = Corners[Base + 6 + Axis];
            const Low = Math.min(A, B, C), High = Math.max(A, B, C);
            // 📝 A flat triangle gets a sliver of thickness so its slab test cannot be skipped by a coplanar ray origin.
            const Pad = 1.0e-5 + 1.0e-6 * Math.max(Math.abs(Low), Math.abs(High));
            Minimum[Triangle * 3 + Axis] = Low - Pad;
            Maximum[Triangle * 3 + Axis] = High + Pad;
            Centroid[Triangle * 3 + Axis] = (A + B + C) / 3.0;
        }
    }
    return { Minimum, Maximum, Centroid };
}

// 📐 Octahedral unit-vector encoding into two snorm16 (matches PackOctahedral in PackingCodec.wgsl).
function PackOctahedral(X, Y, Z)
{
    const Sum = Math.abs(X) + Math.abs(Y) + Math.abs(Z) || 1.0;
    let U = X / Sum, V = Y / Sum;
    if (Z < 0.0)
    {
        const FU = (1.0 - Math.abs(V)) * (U >= 0.0 ? 1.0 : -1.0);
        const FV = (1.0 - Math.abs(U)) * (V >= 0.0 ? 1.0 : -1.0);
        U = FU; V = FV;
    }
    const Snorm = (Value) => (Math.round(Math.max(-1.0, Math.min(1.0, Value)) * 32767) & 0xFFFF);
    return (Snorm(U) | (Snorm(V) << 16)) >>> 0;
}

// 📝 One facet set's BLAS: triangles from the set's authored facets (Corners, Normals, Family, OccupantOffset).
//    Returns the node words (local indices), the triangle records (64 B each, leaf order) and the counts.
export function BuildFacetBlas(Facets, TriangleBase, NodeBase)
{
    const Count = Facets.length;
    const Corners = new Float32Array(Count * 9);
    for (let Index = 0; Index < Count; ++Index)
        for (let Corner = 0; Corner < 3; ++Corner)
            for (let Axis = 0; Axis < 3; ++Axis)
                Corners[Index * 9 + Corner * 3 + Axis] = Facets[Index].Corners[Corner][Axis];
    return BuildTriangleBlas(Corners, Count, (Index) => Facets[Index], TriangleBase, NodeBase);
}

export function BuildTriangleBlas(Corners, Count, Describe, TriangleBase, NodeBase)
{
    if (Count === 0)
        return { Nodes: new ArrayBuffer(0), NodeCount: 0, Triangles: new ArrayBuffer(0), TriangleCount: 0 };
    const Bounds = ProjectTriangleBounds(Corners, Count);
    const Hierarchy = BuildHierarchy(Count, Bounds.Minimum, Bounds.Maximum, Bounds.Centroid, LeafTriangles);
    const Integers = new Uint32Array(Hierarchy.Nodes);
    for (let Node = 0; Node < Hierarchy.NodeCount; ++Node)
    {
        const Offset = Node * 8;
        if (Integers[Offset + 7] === 0)
            Integers[Offset + 3] += NodeBase;
        else
            Integers[Offset + 3] += TriangleBase;
    }
    const Triangles = new ArrayBuffer(Count * TriangleStride);
    const Scalars = new Float32Array(Triangles), Words = new Uint32Array(Triangles);
    for (let Slot = 0; Slot < Count; ++Slot)
    {
        const Source = Hierarchy.Order[Slot];
        const Facet = Describe(Source);
        const Base = Source * 9;
        const Offset = Slot * 16;
        const Ax = Corners[Base], Ay = Corners[Base + 1], Az = Corners[Base + 2];
        Scalars[Offset] = Ax; Scalars[Offset + 1] = Ay; Scalars[Offset + 2] = Az;
        Words[Offset + 3] = ((Facet.Family & 0xFFFF) | ((Facet.OccupantOffset || 0) << 16)) >>> 0;
        Scalars[Offset + 4] = Corners[Base + 3] - Ax; Scalars[Offset + 5] = Corners[Base + 4] - Ay; Scalars[Offset + 6] = Corners[Base + 5] - Az;
        Scalars[Offset + 8] = Corners[Base + 6] - Ax; Scalars[Offset + 9] = Corners[Base + 7] - Ay; Scalars[Offset + 10] = Corners[Base + 8] - Az;
        // 📝 Corner normals when authored, else the facet normal three times.
        const E1x = Scalars[Offset + 4], E1y = Scalars[Offset + 5], E1z = Scalars[Offset + 6];
        const E2x = Scalars[Offset + 8], E2y = Scalars[Offset + 9], E2z = Scalars[Offset + 10];
        let Nx = E1y * E2z - E1z * E2y, Ny = E1z * E2x - E1x * E2z, Nz = E1x * E2y - E1y * E2x;
        const Length = Math.hypot(Nx, Ny, Nz) || 1.0;
        Nx /= Length; Ny /= Length; Nz /= Length;
        for (let Corner = 0; Corner < 3; ++Corner)
        {
            const Normal = Facet.Normals ? Facet.Normals[Corner] : [Nx, Ny, Nz];
            Words[Offset + [7, 11, 12][Corner]] = PackOctahedral(Normal[0], Normal[1], Normal[2]);
        }
        Words[Offset + 13] = Source;
        Words[Offset + 14] = 0;
        Words[Offset + 15] = 0;
    }
    return { Nodes: Hierarchy.Nodes, NodeCount: Hierarchy.NodeCount, Triangles, TriangleCount: Count, Order: Hierarchy.Order };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  TRACE STRUCTURE
//------------------------------------------------------------------------------------------------------------------------

export class TraceStructure
{
    constructor(Exchange)
    {
        this.Exchange = Exchange;
        this.SetBlas = [];          // per facet set: { NodeBase, TriangleBase, NodeCapacity, TriangleCapacity, Root, Count }
        this.FragmentBlas = new Map();   // occupant index → { Root, TriangleBase, Count }
        this.InstanceCount = 0;
        this.EmissiveCount = 0;
        this.EmissiveSignature = "";
        this.Statistics = { BlasNodes: 0, Triangles: 0, TlasNodes: 0, Instances: 0, Emissive: 0, BuildMilliseconds: 0.0 };
    }

    // 📝 Builds every facet set's BLAS and sizes the device buffers: static sets take their exact node and triangle
    //    counts, composite (fragment) sets reserve room for their capacity, and the TLAS region holds every occupant.
    ConstructStructure(Scene, Materials)
    {
        const Started = performance.now();
        this.Scene = Scene;
        this.Materials = Materials;
        const StaticNodes = [], StaticTriangles = [];
        let NodeCursor = 0, TriangleCursor = 0;
        Scene.FacetSets.forEach((Set, Index) =>
        {
            const Capacity = Set.Composite ? Math.ceil((Set.VertexCapacity || 0) / 3) : Set.Facets.length;
            const Entry = { NodeBase: NodeCursor, TriangleBase: TriangleCursor, NodeCapacity: Math.max(1, 2 * Capacity), TriangleCapacity: Capacity,
                            Root: NodeCursor, Count: 0, NodeCursor: 0, TriangleCursor: 0 };
            if (!Set.Composite && Set.Facets.length > 0)
            {
                const Blas = BuildFacetBlas(Set.Facets, Entry.TriangleBase, Entry.NodeBase);
                StaticNodes.push({ Offset: Entry.NodeBase, Bytes: Blas.Nodes });
                StaticTriangles.push({ Offset: Entry.TriangleBase, Bytes: Blas.Triangles });
                Entry.Count = Blas.TriangleCount;
                Entry.NodeCapacity = Blas.NodeCount;
                Entry.Order = Blas.Order;
            }
            NodeCursor += Entry.NodeCapacity;
            TriangleCursor += Entry.TriangleCapacity;
            this.SetBlas[Index] = Entry;
        });
        this.TlasBase = NodeCursor;
        this.TlasCapacity = Math.max(1, 2 * Scene.Occupants.length);
        this.TriangleCapacity = Math.max(1, TriangleCursor);
        const NodeCapacity = this.TlasBase + this.TlasCapacity;
        const Usage = GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST;
        this.NodeRecords     = this.Exchange.ConstructRecords(NodeCapacity * NodeStride, Usage, "BVH nodes");
        this.TriangleRecords = this.Exchange.ConstructRecords(this.TriangleCapacity * TriangleStride, Usage, "BVH triangles");
        this.InstanceRecords = this.Exchange.ConstructRecords(Math.max(1, Scene.Occupants.length) * InstanceStride, Usage, "BVH instances");
        this.EmissiveCapacity = 65536;
        this.LightTable      = this.Exchange.ConstructRecords(LightBufferBytes + this.EmissiveCapacity * EmissiveStride, Usage, "Light table");
        this.TraceRecords    = this.Exchange.ConstructRecords(TraceRecordExtent, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "Trace record");
        const Queue = this.Exchange.Device.queue;
        for (const { Offset, Bytes } of StaticNodes)
            if (Bytes.byteLength > 0) Queue.writeBuffer(this.NodeRecords, Offset * NodeStride, Bytes);
        for (const { Offset, Bytes } of StaticTriangles)
            if (Bytes.byteLength > 0) Queue.writeBuffer(this.TriangleRecords, Offset * TriangleStride, Bytes);
        this.TlasNodes = new ArrayBuffer(this.TlasCapacity * NodeStride);
        this.InstanceBytes = new ArrayBuffer(Math.max(1, Scene.Occupants.length) * InstanceStride);
        this.EmissiveFamilies = new Set();
        Materials.forEach((Material, Family) =>
        {
            const Luminance = 0.2126 * Material.Emission[0] + 0.7152 * Material.Emission[1] + 0.0722 * Material.Emission[2];
            if (Luminance >= EmissionFloor) this.EmissiveFamilies.add(Family);
        });
        this.Statistics.BlasNodes = this.TlasBase;
        this.Statistics.Triangles = TriangleCursor;
        this.Statistics.BuildMilliseconds = performance.now() - Started;
    }

    // 📝 Fragments of a broken prop: each gets a BLAS over its own triangles (local to its centroid, as the composite
    //    set stores them), written into the set's reserved node and triangle ranges. Fragments are the Written list
    //    of FractureSequence.WriteFragments, in slot order; their occupants are PieceFirst + slot.
    AssignFragments(Prop, Fragments, CompositeSet)
    {
        const Entry = this.SetBlas[CompositeSet];
        if (!Entry)
            return;
        Entry.NodeCursor = 0;
        Entry.TriangleCursor = 0;
        const Queue = this.Exchange.Device.queue;
        for (let Slot = 0; Slot < Prop.Definition.Capacity; ++Slot)
            this.FragmentBlas.delete(Prop.Definition.PieceFirst + Slot);
        Fragments.forEach((Fragment, Slot) =>
        {
            const Count = Fragment.Triangles.length;
            if (Count === 0 || Entry.TriangleCursor + Count > Entry.TriangleCapacity || Entry.NodeCursor + 2 * Count > Entry.NodeCapacity)
                return;
            const Corners = new Float32Array(Count * 9);
            Fragment.Triangles.forEach((Triangle, Index) =>
            {
                for (let Corner = 0; Corner < 3; ++Corner)
                    for (let Axis = 0; Axis < 3; ++Axis)
                        Corners[Index * 9 + Corner * 3 + Axis] = Triangle.Corners[Corner][Axis];
            });
            const TriangleBase = Entry.TriangleBase + Entry.TriangleCursor;
            const NodeBase = Entry.NodeBase + Entry.NodeCursor;
            const Blas = BuildTriangleBlas(Corners, Count, (Index) => ({ Family: Fragment.Triangles[Index].Family, OccupantOffset: Slot }), TriangleBase, NodeBase);
            Queue.writeBuffer(this.NodeRecords, NodeBase * NodeStride, Blas.Nodes);
            Queue.writeBuffer(this.TriangleRecords, TriangleBase * TriangleStride, Blas.Triangles);
            this.FragmentBlas.set(Prop.Definition.PieceFirst + Slot, { Root: NodeBase, TriangleBase, Count });
            Entry.NodeCursor += Blas.NodeCount;
            Entry.TriangleCursor += Count;
        });
        this.EmissiveSignature = "";
    }

    // 📝 The frame's instances: every drawn occupant with a BLAS, every live fragment with one of its own. Vehicle
    //    instances carry the flags the traversal filters by (see-through glass; a shredded tyre's rubber is gone).
    ResolveInstances(Scene, Vehicle)
    {
        const Instances = [];
        const Body = Scene.VehicleOccupants.Body;
        const Wheels = Scene.VehicleOccupants.Wheels;
        Scene.Occupants.forEach((Occupant, Index) =>
        {
            if (Occupant.Inactive)
                return;
            if (Occupant.Fragment)
            {
                const Blas = this.FragmentBlas.get(Index);
                if (Blas && Occupant.LocalMinimum)
                    Instances.push({ Occupant: Index, Root: Blas.Root, Flags: InstanceFlag.Fragment,
                                     Bounds: TransformBounds(Occupant.Transform, Occupant.LocalMinimum, Occupant.LocalMaximum) });
                return;
            }
            if (!(Occupant.Roles & OccupantRole.Raster))
                return;
            const Set = Scene.FacetSets[Occupant.FacetSet];
            const Entry = this.SetBlas[Occupant.FacetSet];
            if (!Entry || Entry.Count === 0 || Set.Composite)
                return;
            let Flags = 0;
            if (Index === Body)
                Flags |= InstanceFlag.Vehicle;
            const Wheel = Wheels.indexOf(Index);
            if (Wheel >= 0)
            {
                Flags |= InstanceFlag.Vehicle;
                if (Vehicle && Vehicle.Wheels && Vehicle.Wheels[Wheel] && Vehicle.Wheels[Wheel].Shredded)
                    Flags |= InstanceFlag.SkipTyre;
            }
            Instances.push({ Occupant: Index, Root: Entry.Root, Flags, Bounds: TransformBounds(Occupant.Transform, Set.LocalMinimum, Set.LocalMaximum) });
        });
        return Instances;
    }

    // 📝 Per frame: the TLAS over the instances and their records, uploaded; the emissive table when the active set
    //    changed. Returns the instance count.
    AdvanceFrame(Scene, Vehicle)
    {
        const Started = performance.now();
        const Instances = this.ResolveInstances(Scene, Vehicle);
        const Count = Instances.length;
        const Minimum = new Float32Array(Math.max(1, Count) * 3), Maximum = new Float32Array(Math.max(1, Count) * 3), Centroid = new Float32Array(Math.max(1, Count) * 3);
        Instances.forEach((Instance, Index) =>
        {
            const Low = Instance.Bounds.Minimum, High = Instance.Bounds.Maximum;
            for (let Axis = 0; Axis < 3; ++Axis)
            {
                Minimum[Index * 3 + Axis] = Low[Axis] - 1.0e-4;
                Maximum[Index * 3 + Axis] = High[Axis] + 1.0e-4;
                Centroid[Index * 3 + Axis] = 0.5 * (Low[Axis] + High[Axis]);
            }
        });
        const Queue = this.Exchange.Device.queue;
        let TlasNodeCount = 1;
        if (Count > 0)
        {
            const Hierarchy = BuildHierarchy(Count, Minimum, Maximum, Centroid, LeafInstances);
            const Words = new Uint32Array(Hierarchy.Nodes);
            for (let Node = 0; Node < Hierarchy.NodeCount; ++Node)
                if (Words[Node * 8 + 7] === 0) Words[Node * 8 + 3] += this.TlasBase;
            TlasNodeCount = Hierarchy.NodeCount;
            Queue.writeBuffer(this.NodeRecords, this.TlasBase * NodeStride, Hierarchy.Nodes);
            // instance records in leaf order
            const Bytes = new ArrayBuffer(Count * InstanceStride);
            const Scalars = new Float32Array(Bytes), Integers = new Uint32Array(Bytes);
            for (let Slot = 0; Slot < Count; ++Slot)
            {
                const Instance = Instances[Hierarchy.Order[Slot]];
                const Occupant = Scene.Occupants[Instance.Occupant];
                const WorldToLocal = Occupant.InverseTransform && Occupant.InverseTransformOf === Occupant.Transform
                                   ? Occupant.InverseTransform : InvertTransform(Occupant.Transform);
                Occupant.InverseTransform = WorldToLocal;
                Occupant.InverseTransformOf = Occupant.Transform;
                Scalars.set(WorldToLocal, Slot * 20);
                Integers[Slot * 20 + 16] = Instance.Root;
                Integers[Slot * 20 + 17] = Instance.Occupant;
                Integers[Slot * 20 + 18] = Instance.Flags;
                Integers[Slot * 20 + 19] = 0;
            }
            Queue.writeBuffer(this.InstanceRecords, 0, Bytes);
        }
        else
        {
            // an empty TLAS: one leaf with no instances
            const Empty = new ArrayBuffer(NodeStride);
            new Float32Array(Empty).set([0, 0, 0, 0, 0, 0, 0, 0]);
            Queue.writeBuffer(this.NodeRecords, this.TlasBase * NodeStride, Empty);
        }
        this.InstanceCount = Count;
        this.Statistics.TlasNodes = TlasNodeCount;
        this.Statistics.Instances = Count;
        this.RefreshEmissive(Scene, Instances);
        this.Statistics.BuildMilliseconds = this.Statistics.BuildMilliseconds * 0.9 + (performance.now() - Started) * 0.1;
        return Count;
    }

    // 📝 The emissive-triangle table: every triangle of an emissive family on a live instance, with a CDF over
    //    power (area × emission luminance) for proportional selection. Rebuilt only when the live set changes.
    RefreshEmissive(Scene, Instances)
    {
        const Signature = Instances.map((Instance) => Instance.Occupant).join(",");
        if (Signature === this.EmissiveSignature)
            return;
        this.EmissiveSignature = Signature;
        const Entries = [];
        for (const Instance of Instances)
        {
            const Occupant = Scene.Occupants[Instance.Occupant];
            if (Occupant.Fragment)
                continue;
            const Set = Scene.FacetSets[Occupant.FacetSet];
            const Entry = this.SetBlas[Occupant.FacetSet];
            if (!Entry || !Entry.Order)
                continue;
            if (!Set.EmissiveSlots)
            {
                // 📝 Per set, once: leaf-order slots of every facet with its area, and of the emissive facets with
                //    their power (area × emission luminance). An occupant whose material is overridden (stress lamps)
                //    is emissive on every facet or on none.
                Set.EmissiveSlots = [];
                Set.AreaSlots = [];
                Entry.Order.forEach((Source, Slot) =>
                {
                    const Facet = Set.Facets[Source];
                    const [A, B, C] = Facet.Corners;
                    const Ux = B[0] - A[0], Uy = B[1] - A[1], Uz = B[2] - A[2], Vx = C[0] - A[0], Vy = C[1] - A[1], Vz = C[2] - A[2];
                    const Area = 0.5 * Math.hypot(Uy * Vz - Uz * Vy, Uz * Vx - Ux * Vz, Ux * Vy - Uy * Vx);
                    if (Area > 1.0e-9)
                        Set.AreaSlots.push({ Slot, Area });
                    if (!this.EmissiveFamilies.has(Facet.Family))
                        return;
                    const Luminance = this.ResolveLuminance(this.Materials[Facet.Family].Emission);
                    if (Area * Luminance > 1.0e-6)
                        Set.EmissiveSlots.push({ Slot, Power: Area * Luminance });
                });
            }
            let Slots = Set.EmissiveSlots;
            if (Occupant.MaterialOverride >= 0)
            {
                if (!this.EmissiveFamilies.has(Occupant.MaterialOverride))
                    continue;
                const Luminance = this.ResolveLuminance(this.Materials[Occupant.MaterialOverride].Emission);
                Slots = Set.AreaSlots.map(({ Slot, Area }) => ({ Slot, Power: Area * Luminance }));
            }
            for (const Emissive of Slots)
            {
                if (Entries.length >= this.EmissiveCapacity)
                    break;
                Entries.push({ Occupant: Instance.Occupant, Triangle: Entry.TriangleBase + Emissive.Slot, Power: Emissive.Power });
            }
        }
        const Total = Entries.reduce((Sum, Entry) => Sum + Entry.Power, 0.0);
        const Bytes = new ArrayBuffer(Math.max(1, Entries.length) * EmissiveStride);
        const Scalars = new Float32Array(Bytes), Integers = new Uint32Array(Bytes);
        let Cumulative = 0.0;
        Entries.forEach((Entry, Index) =>
        {
            Cumulative += Entry.Power;
            Integers[Index * 4] = Entry.Occupant;
            Integers[Index * 4 + 1] = Entry.Triangle;
            Scalars[Index * 4 + 2] = Total > 0.0 ? Cumulative / Total : 1.0;
            Scalars[Index * 4 + 3] = Entry.Power;
        });
        if (Entries.length > 0)
            this.Exchange.Device.queue.writeBuffer(this.LightTable, LightBufferBytes, Bytes);
        this.EmissiveCount = Entries.length;
        this.EmissiveTotalPower = Total;
        this.Statistics.Emissive = Entries.length;
    }

    ResolveLuminance(Colour)
    {
        return 0.2126 * Colour[0] + 0.7152 * Colour[1] + 0.0722 * Colour[2];
    }

    // 📝 The clustered point lights (LightingSystem.Update's records) lead the light table every frame.
    UploadPointLights(LightData, Count)
    {
        this.PointCount = Math.max(0, Math.min(MaxSceneLights, Count));
        this.Exchange.Device.queue.writeBuffer(this.LightTable, 0, LightData.buffer, LightData.byteOffset, LightBufferBytes);
    }

    // 📝 The trace record (TraceRecords.wgsl TraceRecord): roots, counts, the sun's disc, the ReSTIR settings.
    EncodeTraceRecord(Settings, SunAboveHorizon, FrameIndex)
    {
        const Bytes = new ArrayBuffer(TraceRecordExtent);
        const Scalars = new Float32Array(Bytes), Integers = new Uint32Array(Bytes);
        const SunRadius = 0.004675;   // [rad] 0.268°
        Integers[0] = this.TlasBase;
        Integers[1] = this.InstanceCount;
        Integers[2] = this.PointCount || 0;
        Integers[3] = this.EmissiveCount;
        Scalars[4] = Math.PI * Math.sin(SunRadius) * Math.sin(SunRadius);   // sun solid angle [sr]
        Scalars[5] = Math.cos(SunRadius);
        Scalars[6] = this.EmissiveTotalPower || 0.0;
        Integers[7] = SunAboveHorizon ? 1 : 0;
        Integers[8] = Math.max(1, Math.min(8, Settings.PathBounces | 0));
        Integers[9] = Math.max(1, Math.min(32, Settings.DirectCandidates | 0));
        Integers[10] = Math.max(0, Math.min(8, Settings.DirectSpatialCount | 0));
        Integers[11] = Math.max(0, Math.min(8, Settings.PathSpatialCount | 0));
        Scalars[12] = Settings.DirectSpatialRadius;
        Scalars[13] = Settings.PathSpatialRadius;
        Scalars[14] = Settings.DirectTallyLimit;
        Scalars[15] = Settings.PathTallyLimit;
        Scalars[16] = Settings.ReconnectionRoughness;
        Scalars[17] = Settings.ReconnectionDistance;
        Scalars[18] = Settings.FireflyClamp;
        Integers[19] = (Settings.PathReplay ? 1 : 0) | (Settings.PairwiseMis ? 2 : 0) | (Settings.DirectBsdfCandidate ? 4 : 0)
                     | (Settings.MisVisibility ? 8 : 0) | (Settings.RussianRoulette ? 16 : 0) | (Settings.Reflection ? 32 : 0)
                     | (Settings.TyreMarks ? 64 : 0);
        Integers[20] = FrameIndex;
        Scalars[21] = Settings.SpecularLobeFloor !== undefined ? Settings.SpecularLobeFloor : 0.0;
        Integers[22] = this.EmissiveCapacity;
        Integers[23] = MaxSceneLights;
        this.Exchange.Device.queue.writeBuffer(this.TraceRecords, 0, Bytes);
    }

    QueryLines()
    {
        const S = this.Statistics;
        return [`BVH · ${S.Triangles} tris · ${S.BlasNodes} BLAS nodes · ${S.Instances} instances · ${S.TlasNodes} TLAS nodes`,
                `lights · ${this.PointCount || 0} point · ${S.Emissive} emissive tris · build ${S.BuildMilliseconds.toFixed(2)} ms`];
    }
}

// 📝 Retained for tests: a CPU ray cast through one BLAS (closest hit) to compare against brute force.
export function CastBlas(Nodes, Triangles, Root, Origin, Direction, MaximumTravel = Infinity)
{
    const NodeScalars = new Float32Array(Nodes), NodeWords = new Uint32Array(Nodes);
    const Scalars = new Float32Array(Triangles), Words = new Uint32Array(Triangles);
    const Inverse = Direction.map((Component) => 1.0 / (Component === 0.0 ? 1.0e-30 : Component));
    let Best = MaximumTravel, Hit = -1;
    const Stack = [Root];
    while (Stack.length > 0)
    {
        const Node = Stack.pop();
        const Offset = Node * 8;
        let Near = 0.0, Far = Best;
        for (let Axis = 0; Axis < 3; ++Axis)
        {
            const T0 = (NodeScalars[Offset + Axis] - Origin[Axis]) * Inverse[Axis];
            const T1 = (NodeScalars[Offset + 4 + Axis] - Origin[Axis]) * Inverse[Axis];
            Near = Math.max(Near, Math.min(T0, T1));
            Far = Math.min(Far, Math.max(T0, T1));
        }
        if (Near > Far)
            continue;
        const Count = NodeWords[Offset + 7];
        if (Count === 0)
        {
            Stack.push(NodeWords[Offset + 3], NodeWords[Offset + 3] + 1);
            continue;
        }
        const First = NodeWords[Offset + 3];
        for (let Index = First; Index < First + Count; ++Index)
        {
            const Base = Index * 16;
            const V0 = [Scalars[Base], Scalars[Base + 1], Scalars[Base + 2]];
            const E1 = [Scalars[Base + 4], Scalars[Base + 5], Scalars[Base + 6]];
            const E2 = [Scalars[Base + 8], Scalars[Base + 9], Scalars[Base + 10]];
            const P = [Direction[1] * E2[2] - Direction[2] * E2[1], Direction[2] * E2[0] - Direction[0] * E2[2], Direction[0] * E2[1] - Direction[1] * E2[0]];
            const Determinant = E1[0] * P[0] + E1[1] * P[1] + E1[2] * P[2];
            if (Math.abs(Determinant) < 1.0e-12)
                continue;
            const InverseDeterminant = 1.0 / Determinant;
            const T = [Origin[0] - V0[0], Origin[1] - V0[1], Origin[2] - V0[2]];
            const U = (T[0] * P[0] + T[1] * P[1] + T[2] * P[2]) * InverseDeterminant;
            if (U < 0.0 || U > 1.0)
                continue;
            const Q = [T[1] * E1[2] - T[2] * E1[1], T[2] * E1[0] - T[0] * E1[2], T[0] * E1[1] - T[1] * E1[0]];
            const V = (Direction[0] * Q[0] + Direction[1] * Q[1] + Direction[2] * Q[2]) * InverseDeterminant;
            if (V < 0.0 || U + V > 1.0)
                continue;
            const Travel = (E2[0] * Q[0] + E2[1] * Q[1] + E2[2] * Q[2]) * InverseDeterminant;
            if (Travel > 1.0e-6 && Travel < Best)
            {
                Best = Travel;
                Hit = Words[Base + 13];
            }
        }
    }
    return { Travel: Best, Triangle: Hit };
}

export { VertexStride, CourseMaterial };
