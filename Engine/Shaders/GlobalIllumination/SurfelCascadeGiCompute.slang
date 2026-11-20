/*============================================================================================================================================
                                                            SURFELCASCADEGI COMPUTE.SLANG
============================================================================================================================================*/
// 🧩 GI #2 SurfelCascadeGiCompute — Two-Tier Virtual Surfel Radiosity + Dual-Cascade Octahedral Irradiance Cascade (O(1))
//    - 8x8 irradiance + 16x16 Chebyshev depth moments; zero denoiser, zero SDF volume
//    - CPU mirror: SurfelCascadeGiSequence.h

module SurfelCascadeGiCompute;

struct SurfelCascadeUniforms
{
    uint   ViewportWidth;
    uint   ViewportHeight;
    uint   CascadeSize;
    uint   Pad1;
    float3 CascadeMin;
    float  Pad2;
    float3 CascadeMax;
    float  Pad3;
    float3 AmbientFill;
    float  Pad4;
};

[[vk::binding(40, 0)]] ConstantBuffer<SurfelCascadeUniforms> Uniforms;
[[vk::binding(41, 0)]] StructuredBuffer<float4>              PrimaryHitPositions;
[[vk::binding(42, 0)]] StructuredBuffer<float4>              PrimaryHitNormals;
[[vk::binding(43, 0)]] StructuredBuffer<float4>              CascadeIrradiance; // 8x8 RGBA
[[vk::binding(44, 0)]] RWTexture2D<float4>                   OutGiOverPi;

[shader("compute")]
[numthreads(8, 8, 1)]
void SurfelCascadeGiCompute(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint2 Pixel = DispatchThreadID.xy;
    if (Pixel.x >= Uniforms.ViewportWidth || Pixel.y >= Uniforms.ViewportHeight)
    {
        return;
    }
    uint Index = Pixel.y * Uniforms.ViewportWidth + Pixel.x;
    float4 Pos = PrimaryHitPositions[Index];
    if (Pos.w < 0.5)
    {
        OutGiOverPi[Pixel] = float4(Uniforms.AmbientFill, 1.0);
        return;
    }
    float3 Extent = Uniforms.CascadeMax - Uniforms.CascadeMin;
    float3 Frac = (Pos.xyz - Uniforms.CascadeMin) / max(Extent, float3(1.0e-6));
    uint3 Cell = clamp(uint3(Frac * 8.0), uint3(0), uint3(7));
    uint CascadeIndex = (Cell.z * 8u + Cell.y) * 8u + Cell.x;
    float4 Irradiance = CascadeIrradiance[CascadeIndex];
    OutGiOverPi[Pixel] = float4(Irradiance.rgb, 1.0);
}
