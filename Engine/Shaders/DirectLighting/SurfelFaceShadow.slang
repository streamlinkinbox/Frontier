/*============================================================================================================================================
                                                            SURFELFACESHADOW.SLANG
============================================================================================================================================*/
// 🧩 Mode 5 SurfelFaceShadow — Stratified Occluder Surfel Disk Cone Occlusion from Real Faces (Zero SDF)
//    - Analytic cone-vs-disk occlusion + 0.28 m WideBoundingTree contact ray (BVH4 via WideTreeRayQuery)
//    - Disks built CPU-side by SurfelOccluderField; this compute evaluates per-pixel visibility

module SurfelFaceShadow;

import WideTreeRayQuery;

struct SurfelFaceShadowUniforms
{
    uint   ViewportWidth;
    uint   ViewportHeight;
    uint   BvhEntryCount;
    uint   DiskCount;
    float3 SunDirection;
    float  TanSunRadius;
    float3 ContactRayMeters;
    float  Pad1;
};

struct OccluderSurfelDiskGpu
{
    float3 Position;
    float  RadiusM;
    float3 Normal;
    float  Pad1;
};

[[vk::binding(20, 0)]] ConstantBuffer<SurfelFaceShadowUniforms>      Uniforms;
[[vk::binding(21, 0)]] StructuredBuffer<float4>                      PrimaryHitPositions; // xyz = WorldPos, w = ValidHit
[[vk::binding(22, 0)]] StructuredBuffer<float4>                      PrimaryHitNormals;   // xyz = WorldNrm, w = NdotL
[[vk::binding(23, 0)]] StructuredBuffer<GpuWideTreeEntry128>         SceneBvhEntries;
[[vk::binding(24, 0)]] StructuredBuffer<GpuRayTrianglePrimitive48>   SceneTriangles;
[[vk::binding(25, 0)]] StructuredBuffer<OccluderSurfelDiskGpu>       OccluderDisks;
[[vk::binding(26, 0)]] RWTexture2D<float>                            OutVisibility;

float SurfelSmoothHermite(float Edge0, float Edge1, float X)
{
    float T = clamp((X - Edge0) / max(1.0e-6, Edge1 - Edge0), 0.0, 1.0);
    return T * T * (3.0 - 2.0 * T);
}

float SurfelSoftMin(float A, float B, float K)
{
    float H = max(K - abs(A - B), 0.0) / K;
    return min(A, B) - H * H * K * 0.25;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void SurfelFaceShadowCompute(uint3 DispatchThreadID : SV_DispatchThreadID)
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
        OutVisibility[Pixel] = 1.0;
        return;
    }
    float3 ReceiverP = Pos.xyz;
    float3 ReceiverN = normalize(Nrm.xyz);
    float3 SunDir = normalize(Uniforms.SunDirection);
    float NdotL = dot(ReceiverN, SunDir);
    if (NdotL <= 1.0e-4)
    {
        OutVisibility[Pixel] = 0.0;
        return;
    }
    // 0.28 m contact ray (BVH4 early-out).
    GpuRayHitSummary Contact = TraceWideBoundingTreeClosest(
        SceneBvhEntries, SceneTriangles, Uniforms.BvhEntryCount,
        ReceiverP + ReceiverN * 0.003, SunDir, 0.002, Uniforms.ContactRayMeters.x);
    if (Contact.HitConfirmed)
    {
        OutVisibility[Pixel] = 0.0;
        return;
    }
    float MinRatio = 2.0;
    for (uint D = 0u; D < Uniforms.DiskCount; ++D)
    {
        OccluderSurfelDiskGpu Disk = OccluderDisks[D];
        float3 Rel = Disk.Position - ReceiverP;
        float T = dot(Rel, SunDir);
        if (T <= 0.14 || T > 9.5)
        {
            continue;
        }
        float PlaneFade = SurfelSmoothHermite(0.02, 0.14, dot(Rel, ReceiverN));
        if (PlaneFade <= 1.0e-4)
        {
            continue;
        }
        float3 Perp = Rel - SunDir * T;
        float Clearance = length(Perp) - Disk.RadiusM * 0.92;
        float ConeR = max(0.020, T * Uniforms.TanSunRadius);
        float Ratio = clamp(Clearance / ConeR, -1.5, 1.5) * PlaneFade + 1.5 * (1.0 - PlaneFade);
        MinRatio = SurfelSoftMin(MinRatio, Ratio, 0.14);
        if (MinRatio <= -1.0)
        {
            OutVisibility[Pixel] = 0.0;
            return;
        }
    }
    OutVisibility[Pixel] = SurfelSmoothHermite(-0.95, 0.95, MinRatio);
}
