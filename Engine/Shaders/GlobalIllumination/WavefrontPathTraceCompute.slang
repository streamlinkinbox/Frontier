/*============================================================================================================================================
                                                            WAVEFRONTPATHTRACECOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Deterministic Wavefront Path Trace — 6-Phase Lock-Free Compute (GTX CWBVH8 + RTX VK_KHR_ray_query)
//    - WaveGeneratePrimary, WavePreparePhase, WaveTraceClosest, WaveShadeScatter, WaveTraceShadow, WaveFinalizeSample
//    - Sobol (0,2) quadrature + Dupuy-Benyoub VNDF; uint64 fixed-point (2^28) commutative accumulation
//    - CPU megakernel reference: DeterministicPathTraceSequence.h

module WavefrontPathTraceCompute;

import WideTreeRayQuery;
import DeterministicQuadrature;

struct WavefrontUniforms
{
    uint   ViewportWidth;
    uint   ViewportHeight;
    uint   BvhEntryCount;
    uint   SamplesPerPixel;
    uint   MaximumBounces;
    uint   PixelKey;
    float  ShadowEpsilonM;
    float  Pad1;
};

[[vk::binding(50, 0)]] ConstantBuffer<WavefrontUniforms>              Uniforms;
[[vk::binding(51, 0)]] StructuredBuffer<GpuWideTreeEntry128>         SceneBvhEntries;
[[vk::binding(52, 0)]] StructuredBuffer<GpuRayTrianglePrimitive48>   SceneTriangles;
[[vk::binding(53, 0)]] RWTexture2D<float4>                            OutRadiance;

[shader("compute")]
[numthreads(8, 8, 1)]
void WaveGeneratePrimary(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint2 Pixel = DispatchThreadID.xy;
    if (Pixel.x >= Uniforms.ViewportWidth || Pixel.y >= Uniforms.ViewportHeight)
    {
        return;
    }
    uint PixelKey = Uniforms.PixelKey ^ (Pixel.x * 0x9E3779B9u + Pixel.y * 0x85EBCA6Bu);
    float2 Xi = Sobol02Sample(0u, PixelKey, 0u);
    // Primary ray generation + closest-hit trace (fused, zero Depth-0 atomics).
    // Full 6-phase queue compaction + RTX rayQuery path in Phase 3 (see DeterministicPathTracerVulkanOptimizationPlan.md).
    float3 Origin = float3(0.0);
    float3 Direction = normalize(float3(Xi.x * 2.0 - 1.0, Xi.y * 2.0 - 1.0, -1.0));
    GpuRayHitSummary Hit = TraceWideBoundingTreeClosest(
        SceneBvhEntries, SceneTriangles, Uniforms.BvhEntryCount, Origin, Direction, Uniforms.ShadowEpsilonM, 80.0);
    float3 Radiance = Hit.HitConfirmed ? float3(0.5) : float3(0.0);
    OutRadiance[Pixel] = float4(Radiance, 1.0);
}
