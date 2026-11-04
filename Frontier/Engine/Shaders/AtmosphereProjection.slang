//==========================================================================================================================================
//                                                 ATMOSPHERE PROJECTION SLANG
//==========================================================================================================================================
// Per-realm 192x108 RGBA16F Atmosphere Projection LUT bake with horizon-concentrated elevation warp and descriptive solar azimuth frame.

#include "AtmosphereScattering.slang"

[[vk::push_constant]] PushDispatchHeader Push;

[[vk::binding(0, 0)]] StructuredBuffer<float4>                     Ensemble;
[[vk::binding(1, 0)]] Sampler2DArray<float4>                       SampledTransLut;
[[vk::binding(2, 0)]] Sampler2DArray<float4>                       SampledMultiLut;
[[vk::binding(3, 0)]] [format("rgba16f")] RWTexture2DArray<float4> OutputAtmosphereProjectionLut;

[shader("compute")]
[numthreads(8, 8, 1)]
void BakeAtmosphereProjectionLutMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }
    uint   RealmId     = Push.Counts.z;
    uint   RealmOffset = RealmSlotOffset(Ensemble, RealmId);
    float4 R0          = Ensemble[RealmOffset + 0u];
    float4 R8          = Ensemble[RealmOffset + 8u];
    float  GroundR     = R0.x;
    float  ObserverR   = GroundR + max(R0.z, 1.0e-3f);

    uint   SunFirst            = uint(R8.x + 0.5f);
    uint   SunCount            = uint(R8.y + 0.5f);
    float3 PrimarySunDirection = (SunCount > 0u) ? Ensemble[SunSlotOffset(Ensemble, SunFirst)].xyz : float3(0.0f, 1.0f, 0.0f);

    float2 Uv       = (float2(Tid.xy) + 0.5f) / float2(Push.Counts.xy);
    float  ϕAzimuth = Uv.x * π;
    float  sinθLimb = clamp(GroundR / max(ObserverR, GroundR + 1.0e-4f), 0.0f, 1.0f);
    float  θLimb    = -asin(sinθLimb);

    float θElevation = 0.0f;
    if (Uv.y < 0.5f)
    {
        float Warp = 1.0f - 2.0f * Uv.y;
        θElevation = θLimb - Warp * Warp * (θLimb + 0.5f * π);
    }
    else
    {
        float Warp = 2.0f * Uv.y - 1.0f;
        θElevation = θLimb + Warp * Warp * (0.5f * π - θLimb);
    }

    // Orthonormal horizontal frame aligned with the primary solar azimuth
    float2 SolarHorizontal       = float2(PrimarySunDirection.x, PrimarySunDirection.z);
    float  SolarHorizontalLength = length(SolarHorizontal);
    float2 AzimuthForwardAxis    = (SolarHorizontalLength > 1.0e-5f)
                                       ? (SolarHorizontal / SolarHorizontalLength)
                                       : float2(0.0f, 1.0f);
    float2 AzimuthRightAxis      = float2(AzimuthForwardAxis.y, -AzimuthForwardAxis.x);

    float  cosθ                = cos(θElevation);
    float2 HorizontalDirection = AzimuthForwardAxis * cos(ϕAzimuth) + AzimuthRightAxis * sin(ϕAzimuth);
    float3 ViewDirection       = SafeNormalize(float3(HorizontalDirection.x * cosθ, sin(θElevation), HorizontalDirection.y * cosθ));

    float3 AtmosphereRadiance = EvaluateCoupledSolarSky6Pt(Ensemble, SampledTransLut, SampledMultiLut, RealmId, ViewDirection, 0.0f);
    OutputAtmosphereProjectionLut[uint3(Tid.xy, RealmId)] = float4(AtmosphereRadiance, 1.0f);
}
