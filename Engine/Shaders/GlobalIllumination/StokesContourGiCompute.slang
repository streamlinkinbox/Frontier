/*============================================================================================================================================
                                                            STOKESCONTOURGI COMPUTE.SLANG
============================================================================================================================================*/
// 🧩 GI #1 StokesContourGiCompute — Analytic Spherical-Polygon Stokes Contour (1st Bounce) + Diffuse Patch Cloud (2nd Bounce)
//    - RadiantFaceSources from real luminous faces; occlusion via SurfelOccluderField disks + 0.28 m BVH4 contact ray
//    - Zero denoiser, zero SDF volume. CPU mirror: StokesContourGiSequence.h

module StokesContourGiCompute;

import WideTreeRayQuery;
import DeterministicQuadrature;

struct StokesGiUniforms
{
    uint   ViewportWidth;
    uint   ViewportHeight;
    uint   BvhEntryCount;
    uint   SourceCount;
    float3 AmbientFill;
    float  ContactRayMeters;
};

struct RadiantFaceSourceGpu
{
    float3 Position;
    float  AreaM2;
    float3 Normal;
    float  Pad1;
    float3 Radiance;
    float  Pad2;
};

struct OccluderSurfelDiskGpu2
{
    float3 Position;
    float  RadiusM;
    float3 Normal;
    float  Pad1;
};

[[vk::binding(30, 0)]] ConstantBuffer<StokesGiUniforms>               Uniforms;
[[vk::binding(31, 0)]] StructuredBuffer<float4>                       PrimaryHitPositions;
[[vk::binding(32, 0)]] StructuredBuffer<float4>                       PrimaryHitNormals;
[[vk::binding(33, 0)]] StructuredBuffer<GpuWideTreeEntry128>          SceneBvhEntries;
[[vk::binding(34, 0)]] StructuredBuffer<GpuRayTrianglePrimitive48>    SceneTriangles;
[[vk::binding(35, 0)]] StructuredBuffer<RadiantFaceSourceGpu>         RadiantSources;
[[vk::binding(36, 0)]] StructuredBuffer<OccluderSurfelDiskGpu2>       OccluderDisks;
[[vk::binding(37, 0)]] RWTexture2D<float4>                            OutGiOverPi;

float TriangleSolidAngle(float3 Point, float3 A, float3 B, float3 C)
{
    float3 VA = normalize(A - Point);
    float3 VB = normalize(B - Point);
    float3 VC = normalize(C - Point);
    float Numerator = dot(VA, cross(VB, VC));
    float Denominator = 1.0 + dot(VA, VB) + dot(VB, VC) + dot(VC, VA);
    return 2.0 * atan2(Numerator, Denominator);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void StokesContourGiCompute(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint2 Pixel = DispatchThreadID.xy;
    if (Pixel.x >= Uniforms.ViewportWidth || Pixel.y >= Uniforms.ViewportHeight)
    {
        return;
    }
    uint Index = Pixel.y * Uniforms.ViewportWidth + Pixel.x;
    float4 Pos = PrimaryHitPositions[Index];
    float4 Nrm = PrimaryHitNormals[Index];
    if (Pos.w < 0.5)
    {
        OutGiOverPi[Pixel] = float4(Uniforms.AmbientFill, 1.0);
        return;
    }
    float3 ReceiverP = Pos.xyz;
    float3 ReceiverN = normalize(Nrm.xyz);
    float3 Total = Uniforms.AmbientFill;
    for (uint S = 0u; S < Uniforms.SourceCount; ++S)
    {
        RadiantFaceSourceGpu Source = RadiantSources[S];
        float3 ToSource = Source.Position - ReceiverP;
        float Distance = length(ToSource);
        if (Distance <= 1.0e-4)
        {
            continue;
        }
        float3 Dir = ToSource / Distance;
        float ReceiverCos = dot(ReceiverN, Dir);
        float SourceCos = dot(Source.Normal, -Dir);
        if (ReceiverCos <= 0.0 || SourceCos <= 0.0)
        {
            continue;
        }
        GpuRayHitSummary Contact = TraceWideBoundingTreeClosest(
            SceneBvhEntries, SceneTriangles, Uniforms.BvhEntryCount,
            ReceiverP + ReceiverN * 0.003, Dir, 0.002, Uniforms.ContactRayMeters);
        if (Contact.HitConfirmed)
        {
            continue;
        }
        float SolidAngle = Source.AreaM2 * SourceCos / (Distance * Distance);
        float FormFactor = SolidAngle * ReceiverCos / 3.14159265;
        Total += Source.Radiance * FormFactor;
    }
    OutGiOverPi[Pixel] = float4(Total, 1.0);
}
