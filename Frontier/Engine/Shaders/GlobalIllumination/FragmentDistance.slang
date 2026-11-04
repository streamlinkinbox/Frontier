//============================================================================================================================================
//                                                         FRAGMENTDISTANCE.SLANG
//============================================================================================================================================
// 🧩 A fragment's signed distance volume, computed on the device the refresh it is born — one workgroup per fragment.

#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"

// 📝 A fragment is a closed convex solid, so its distance has a closed form on both sides: inside, the largest face
//    plane distance (exact for a convex solid); outside, the nearest of its triangles (exact). A 12³ volume
//    is 1728 voxels against at most 48 triangles — about eighty thousand point-triangle tests, a few microseconds,
//    so a break that throws forty pieces puts all forty into the scene distance field the same refresh.

struct FragmentJob
{
    uint3 TexelOrigin;    // [texel] the pool slot in the occupant distance space
    uint Counts;           // [-]     triangles | planes << 16
    float3 Minimum;        // [m]     local corner of texel (0, 0, 0)
    float VoxelExtent;      // [m]
};

  [[vk::binding(0, 0)]] StructuredBuffer<FragmentJob>     Jobs;
  [[vk::binding(1, 0)]] StructuredBuffer<float4> Geometry;   // per job: MaxTriangles·3 corners, then MaxPlanes planes
  [[vk::binding(2, 0)]] [[vk::image_format("rgba16f")]] RWTexture3D<float4>                  Target;

static const uint Edge = 12u;
static const uint MaxTriangles = 48u;
static const uint MaxPlanes = 40u;
static const uint Stride = MaxTriangles * 3u + MaxPlanes;

groupshared float3 Corners[144];
groupshared float4 Planes[40];

// 📝 Ericson, Real-Time Collision Detection §5.1.5: the closest point of triangle ABC to P, by Voronoi region.
float MeasureTriangleDistanceSquared(float3 P, float3 A, float3 B, float3 C)
{
    let AB = B - A;
    let AC = C - A;
    let AP = P - A;
    let D1 = dot(AB, AP);
    let D2 = dot(AC, AP);
    if (D1 <= 0.0 && D2 <= 0.0) { return dot(AP, AP); }
    let BP = P - B;
    let D3 = dot(AB, BP);
    let D4 = dot(AC, BP);
    if (D3 >= 0.0 && D4 <= D3) { return dot(BP, BP); }
    let VC = D1 * D4 - D3 * D2;
    if (VC <= 0.0 && D1 >= 0.0 && D3 <= 0.0)
    {
        let V = D1 / (D1 - D3);
        let Q = P - (A + V * AB);
        return dot(Q, Q);
    }
    let CP = P - C;
    let D5 = dot(AB, CP);
    let D6 = dot(AC, CP);
    if (D6 >= 0.0 && D5 <= D6) { return dot(CP, CP); }
    let VB = D5 * D2 - D1 * D6;
    if (VB <= 0.0 && D2 >= 0.0 && D6 <= 0.0)
    {
        let W = D2 / (D2 - D6);
        let Q = P - (A + W * AC);
        return dot(Q, Q);
    }
    let VA = D3 * D6 - D5 * D4;
    if (VA <= 0.0 && (D4 - D3) >= 0.0 && (D5 - D6) >= 0.0)
    {
        let W = (D4 - D3) / ((D4 - D3) + (D5 - D6));
        let Q = P - (B + W * (C - B));
        return dot(Q, Q);
    }
    let Denominator = 1.0 / (VA + VB + VC);
    let V = VB * Denominator;
    let W = VC * Denominator;
    let Q = P - (A + AB * V + AC * W);
    return dot(Q, Q);
}

 
[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeFragmentDistance(uint3 Group : SV_GroupID, uint Lane : SV_GroupIndex)
{
    let Job = Jobs[Group.x];
    let TriangleCount = min(Job.Counts & 0xFFFFu, MaxTriangles);
    let PlaneCount = min(Job.Counts >> 16u, MaxPlanes);
    let Base = Group.x * Stride;
    for (var Index = Lane; Index < TriangleCount * 3u; Index += 64u)
    {
        Corners[Index] = Geometry[Base + Index].xyz;
    }
    for (var Index = Lane; Index < PlaneCount; Index += 64u)
    {
        Planes[Index] = Geometry[Base + MaxTriangles * 3u + Index];
    }
    GroupMemoryBarrierWithGroupSync();

    for (var Voxel = Lane; Voxel < Edge * Edge * Edge; Voxel += 64u)
    {
        let Cell = uint3(Voxel % Edge, (Voxel / Edge) % Edge, Voxel / (Edge * Edge));
        let Point = Job.Minimum + (float3(Cell) + 0.5) * Job.VoxelExtent;
        var Inside = -1.0e9;
        for (var Index = 0u; Index < PlaneCount; Index++)
        {
            Inside = max(Inside, dot(Planes[Index].xyz, Point) - Planes[Index].w);
        }
        var Signed = Inside;
        if (Inside > 0.0)
        {
            var Nearest = 1.0e18;
            for (var Triangle = 0u; Triangle < TriangleCount; Triangle++)
            {
                Nearest = min(Nearest, MeasureTriangleDistanceSquared(Point, Corners[Triangle * 3u], Corners[Triangle * 3u + 1u], Corners[Triangle * 3u + 2u]));
            }
            Signed = sqrt(Nearest);
        }
        Target[int3(Job.TexelOrigin + Cell)] = float4(Signed, 0.0, 0.0, 1.0);
    }
}
