//============================================================================================================================================
//                                                         DISTANCETRANSFER.SLANG
//============================================================================================================================================
// 🧩 Transfers one facet set into its signed distance volume — closest-facet distance, sign from back-facing ray hits.

#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"

// 📝 The per-occupant volume is the analogue of a mesh distance field: built once, in the facet set's own refresh, and
//    only ever SAMPLED by the union dispatch that assembles the one scene distance field. No tracer reads it directly.
//
//    ① unsigned distance: closest point on any facet, by a pruned march of the bounding structure;
//    ② sign: RayCount rays in a spherical Fibonacci set, randomly rotated per voxel; the voxel is inside when more
//       than a quarter of all rays first strike a back face. That is the heuristic that survives open and
//       non-manifold input — the ControlVehicle body has 124 open and 76 non-manifold edges — where parity cannot.

struct TransferConstants
{
    float3 VolumeMinimum;    // [m]     local corner of texel (0,0,0)
    float VoxelExtent;        // [m]
    uint3 TexelOrigin;      // [texel] placement inside the volume space
    uint EntryCount;          // [-]
    uint3 TexelExtent;      // [texel] resolution
    uint FacetCount;         // [-]
    uint SlabOrigin;         // [texel] first z slice of this submission
    uint SlabExtent;         // [texel] z slices in this submission
    uint RayCount;           // [-]
    uint Pad;           
};

struct BoundingEntry
{
    float3 Minimum;     
    uint LeftOrFirst; 
    float3 Maximum;     
    uint Count;       
};

  [[vk::binding(0, 0)]] ConstantBuffer<TransferConstants>           Transfer;
  [[vk::binding(1, 0)]] StructuredBuffer<BoundingEntry>        Entries;
  [[vk::binding(2, 0)]] StructuredBuffer<float4> FacetCorners;
  [[vk::binding(3, 0)]] [[vk::image_format("rgba16f")]] RWTexture3D<float4>                 VolumeSpace;

static const uint MarchDepth = 40u;

// 📝 Closest point on a triangle by Voronoi-region classification (Ericson, Real-Time Collision Detection §5.1.5).
float3 ProjectClosestOnFacet(float3 Point, float3 A, float3 B, float3 C)
{
    let AB = B - A;
    let AC = C - A;
    let AP = Point - A;
    let D1 = dot(AB, AP);
    let D2 = dot(AC, AP);
    if (D1 <= 0.0 && D2 <= 0.0) { return A; }
    let BP = Point - B;
    let D3 = dot(AB, BP);
    let D4 = dot(AC, BP);
    if (D3 >= 0.0 && D4 <= D3) { return B; }
    let VC = D1 * D4 - D3 * D2;
    if (VC <= 0.0 && D1 >= 0.0 && D3 <= 0.0) { return A + AB * (D1 / (D1 - D3)); }
    let CP = Point - C;
    let D5 = dot(AB, CP);
    let D6 = dot(AC, CP);
    if (D6 >= 0.0 && D5 <= D6) { return C; }
    let VB = D5 * D2 - D1 * D6;
    if (VB <= 0.0 && D2 >= 0.0 && D6 <= 0.0) { return A + AC * (D2 / (D2 - D6)); }
    let VA = D3 * D6 - D5 * D4;
    if (VA <= 0.0 && (D4 - D3) >= 0.0 && (D5 - D6) >= 0.0)
    {
        return B + (C - B) * ((D4 - D3) / ((D4 - D3) + (D5 - D6)));
    }
    let Reciprocal = 1.0 / (VA + VB + VC);
    return A + AB * (VB * Reciprocal) + AC * (VC * Reciprocal);
}

float ResolveBoundsDistanceSquared(float3 Point, float3 Minimum, float3 Maximum)
{
    let Outside = max(max(Minimum - Point, Point - Maximum), float3(0.0));
    return dot(Outside, Outside);
}

// 📝 Slab test; returns the entry distance or a large value when the ray misses the box before Limit.
float IntersectBounds(float3 Origin, float3 Reciprocal, float3 Minimum, float3 Maximum, float Limit)
{
    let Near = (Minimum - Origin) * Reciprocal;
    let Far = (Maximum - Origin) * Reciprocal;
    let Entry = max(max(min(Near.x, Far.x), min(Near.y, Far.y)), max(min(Near.z, Far.z), 0.0));
    let Exit = min(min(max(Near.x, Far.x), max(Near.y, Far.y)), min(max(Near.z, Far.z), Limit));
    return SelectBranch(1.0e30, Entry, Entry <= Exit);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 CLOSEST-FACET QUERY
//------------------------------------------------------------------------------------------------------------------------

float ResolveClosestDistanceSquared(float3 Point)
{
    var Best = 1.0e30;
    var Stack : uint[40] = {};
    var Depth = 1u;
    Stack[0] = 0u;
    while (Depth > 0u)
    {
        Depth--;
        let Entry = Entries[Stack[Depth]];
        if (ResolveBoundsDistanceSquared(Point, Entry.Minimum, Entry.Maximum) >= Best)
        {
            continue;
        }
        if (Entry.Count > 0u)
        {
            for (var Facet = Entry.LeftOrFirst; Facet < Entry.LeftOrFirst + Entry.Count; Facet++)
            {
                let A = FacetCorners[Facet * 3u].xyz;
                let B = FacetCorners[Facet * 3u + 1u].xyz;
                let C = FacetCorners[Facet * 3u + 2u].xyz;
                let Offset = Point - ProjectClosestOnFacet(Point, A, B, C);
                Best = min(Best, dot(Offset, Offset));
            }
            continue;
        }
        let Left = Entries[Entry.LeftOrFirst];
        let Right = Entries[Entry.LeftOrFirst + 1u];
        let LeftDistance = ResolveBoundsDistanceSquared(Point, Left.Minimum, Left.Maximum);
        let RightDistance = ResolveBoundsDistanceSquared(Point, Right.Minimum, Right.Maximum);
        if (Depth + 2u > MarchDepth)
        {
            continue;
        }
        // 📝 The nearer child is pushed last so it is visited first and tightens Best before the farther one.
        if (LeftDistance < RightDistance)
        {
            Stack[Depth] = Entry.LeftOrFirst + 1u;
            Stack[Depth + 1u] = Entry.LeftOrFirst;
        }
        else
        {
            Stack[Depth] = Entry.LeftOrFirst;
            Stack[Depth + 1u] = Entry.LeftOrFirst + 1u;
        }
        Depth += 2u;
    }
    return Best;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                FIRST-HIT FACING QUERY
//------------------------------------------------------------------------------------------------------------------------

// 📝 Returns +1 when the first hit is a front face, −1 for a back face, 0 for a miss.
float ResolveFirstHitFacing(float3 Origin, float3 Direction)
{
    let Reciprocal = 1.0 / SelectBranch(Direction, float3(1.0e-9), abs(Direction) < float3(1.0e-9));
    var Nearest = 1.0e30;
    var Facing = 0.0;
    var Stack : uint[40] = {};
    var Depth = 1u;
    Stack[0] = 0u;
    while (Depth > 0u)
    {
        Depth--;
        let Entry = Entries[Stack[Depth]];
        if (IntersectBounds(Origin, Reciprocal, Entry.Minimum, Entry.Maximum, Nearest) >= Nearest)
        {
            continue;
        }
        if (Entry.Count > 0u)
        {
            for (var Facet = Entry.LeftOrFirst; Facet < Entry.LeftOrFirst + Entry.Count; Facet++)
            {
                let A = FacetCorners[Facet * 3u].xyz;
                let E1 = FacetCorners[Facet * 3u + 1u].xyz - A;
                let E2 = FacetCorners[Facet * 3u + 2u].xyz - A;
                // 📝 Möller–Trumbore, two-sided; det = −dot(direction, e₁×e₂), positive for a front-face hit.
                let P = cross(Direction, E2);
                let Determinant = dot(E1, P);
                if (abs(Determinant) < 1.0e-12)
                {
                    continue;
                }
                let Inverse = 1.0 / Determinant;
                let T = Origin - A;
                let U = dot(T, P) * Inverse;
                if (U < 0.0 || U > 1.0)
                {
                    continue;
                }
                let Q = cross(T, E1);
                let V = dot(Direction, Q) * Inverse;
                if (V < 0.0 || U + V > 1.0)
                {
                    continue;
                }
                let Travel = dot(E2, Q) * Inverse;
                if (Travel > 1.0e-5 && Travel < Nearest)
                {
                    Nearest = Travel;
                    Facing  = SelectBranch(-1.0, 1.0, Determinant > 0.0);
                }
            }
            continue;
        }
        if (Depth + 2u > MarchDepth)
        {
            continue;
        }
        let LeftEntry = IntersectBounds(Origin, Reciprocal, Entries[Entry.LeftOrFirst].Minimum, Entries[Entry.LeftOrFirst].Maximum, Nearest);
        let RightEntry = IntersectBounds(Origin, Reciprocal, Entries[Entry.LeftOrFirst + 1u].Minimum, Entries[Entry.LeftOrFirst + 1u].Maximum, Nearest);
        if (LeftEntry < RightEntry)
        {
            Stack[Depth] = Entry.LeftOrFirst + 1u;
            Stack[Depth + 1u] = Entry.LeftOrFirst;
        }
        else
        {
            Stack[Depth] = Entry.LeftOrFirst;
            Stack[Depth + 1u] = Entry.LeftOrFirst + 1u;
        }
        Depth += 2u;
    }
    return Facing;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRANSFER ENTRY
//------------------------------------------------------------------------------------------------------------------------

uint PermuteTransfer(uint Input)
{
    let Advanced = Input * 747796405u + 2891336453u;
    let Word = ((Advanced >> ((Advanced >> 28u) + 4u)) ^ Advanced) * 277803737u;
    return (Word >> 22u) ^ Word;
}

// 📝 Spherical Fibonacci point i of n: z = 1 − (2i + 1)/n, φ = 2π·i·(1 − 1/golden), uniform over the sphere.
float3 ProjectSphericalFibonacci(uint Index, uint Count)
{
    let z = 1.0 - (2.0 * float(Index) + 1.0) / float(Count);
    let Radius = sqrt(max(0.0, 1.0 - z * z));
    let φ = 2.399963229728653 * float(Index);
    return float3(Radius * cos(φ), Radius * sin(φ), z);
}

 
[shader("compute")]
[numthreads(4, 4, 4)]
void TransferDistance(uint3 Invocation : SV_DispatchThreadID)
{
    let Voxel = uint3(Invocation.x, Invocation.y, Invocation.z + Transfer.SlabOrigin);
    if (Voxel.x >= Transfer.TexelExtent.x || Voxel.y >= Transfer.TexelExtent.y || Voxel.z >= Transfer.TexelExtent.z
        || Invocation.z >= Transfer.SlabExtent)
    {
        return;
    }
    let Point = Transfer.VolumeMinimum + (float3(Voxel) + 0.5) * Transfer.VoxelExtent;
    let Unsigned = sqrt(ResolveClosestDistanceSquared(Point));

    // 📝 A random rotation per voxel about a random axis keeps the ray set from imprinting a lattice on the sign.
    var Seed = PermuteTransfer((Voxel.x * 73856093u) ^ (Voxel.y * 19349663u) ^ (Voxel.z * 83492791u));
    Seed = PermuteTransfer(Seed);
    let Angle = float(Seed >> 8u) * (6.283185307 / 16777216.0);
    Seed = PermuteTransfer(Seed);
    let AxisZ = float(Seed >> 8u) * (2.0 / 16777216.0) - 1.0;
    Seed = PermuteTransfer(Seed);
    let AxisAngle = float(Seed >> 8u) * (6.283185307 / 16777216.0);
    let AxisRadius = sqrt(max(0.0, 1.0 - AxisZ * AxisZ));
    let Axis = float3(AxisRadius * cos(AxisAngle), AxisRadius * sin(AxisAngle), AxisZ);
    let Cosine = cos(Angle);
    let Sine = sin(Angle);

    var BackHits = 0u;
    for (var Ray = 0u; Ray < Transfer.RayCount; Ray++)
    {
        let Fibonacci = ProjectSphericalFibonacci(Ray, Transfer.RayCount);
        // 📝 Rodrigues rotation of the Fibonacci direction about Axis by Angle.
        let Direction = Fibonacci * Cosine + cross(Axis, Fibonacci) * Sine + Axis * dot(Axis, Fibonacci) * (1.0 - Cosine);
        if (ResolveFirstHitFacing(Point, Direction) < 0.0)
        {
            BackHits++;
        }
    }
    let Inside = float(BackHits) > 0.25 * float(Transfer.RayCount);
    let Signed = SelectBranch(Unsigned, -Unsigned, Inside);
    VolumeSpace[int3(Transfer.TexelOrigin + Voxel)] = float4(Signed, 0.0, 0.0, 1.0);
}
