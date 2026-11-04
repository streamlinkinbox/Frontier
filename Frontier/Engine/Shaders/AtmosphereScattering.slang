//==========================================================================================================================================
//                                                ATMOSPHERE SCATTERING SLANG
//==========================================================================================================================================
// Coupled 6-point quadratic-warped Gauss-Legendre solar/sky/twilight evaluator + Transmittance & Multi-Scattering LUT bakes.

#include "PlanetaryRecords.slang"

float2 DirectionToTransmittanceUv(float GroundRadiusKm, float TopRadiusKm, float RadiusKm, float μSolar)
{
    float MaxChord = sqrt(max(TopRadiusKm * TopRadiusKm - GroundRadiusKm * GroundRadiusKm, 1.0e-5f));
    float ρ        = sqrt(max(RadiusKm * RadiusKm - GroundRadiusKm * GroundRadiusKm, 0.0f));
    float Discrim  = RadiusKm * RadiusKm * (μSolar * μSolar - 1.0f) + TopRadiusKm * TopRadiusKm;
    float DistTop  = max(0.0f, -RadiusKm * μSolar + sqrt(max(Discrim, 0.0f)));
    float DistMin  = TopRadiusKm - RadiusKm;
    float DistMax  = ρ + MaxChord;
    float CoordMu  = (DistMax > DistMin + 1.0e-5f) ? ((DistTop - DistMin) / (DistMax - DistMin)) : 0.0f;
    float CoordR   = ρ / MaxChord;
    return saturate(float2(CoordMu, CoordR));
}

float3 SampleTransmittanceLutArray(Sampler2DArray<float4> TransLut,
                                   uint                   RealmId,
                                   float                  GroundRadiusKm,
                                   float                  TopRadiusKm,
                                   float                  RadiusKm,
                                   float                  μSolar)
{
    float2 Uv = DirectionToTransmittanceUv(
        GroundRadiusKm, TopRadiusKm, clamp(RadiusKm, GroundRadiusKm, TopRadiusKm), clamp(μSolar, -1.0f, 1.0f));
    return TransLut.SampleLevel(float3(Uv, float(RealmId)), 0.0f).rgb;
}

float2 DirectionToSkyViewUv(float GroundRadiusKm, float ObserverRadiusKm, float3 ViewDirection, float3 PrimarySunDirection)
{
    float ClampedR      = max(ObserverRadiusKm, GroundRadiusKm + 1.0e-4f);
    float sinθLimb      = clamp(GroundRadiusKm / ClampedR, 0.0f, 1.0f);
    float θLimb         = -asin(sinθLimb);
    float θView         = asin(clamp(ViewDirection.y, -1.0f, 1.0f));

    float CoordV = 0.5f;
    if (θView < θLimb)
    {
        float Ratio = (θView - θLimb) / max(-0.5f * π - θLimb, -1.0e-4f);
        CoordV      = 0.5f - 0.5f * sqrt(saturate(Ratio));
    }
    else
    {
        float Ratio = (θView - θLimb) / max(0.5f * π - θLimb, 1.0e-4f);
        CoordV      = 0.5f + 0.5f * sqrt(saturate(Ratio));
    }

    float2 ViewHorizontal        = float2(ViewDirection.x, ViewDirection.z);
    float2 SolarHorizontal       = float2(PrimarySunDirection.x, PrimarySunDirection.z);
    float  ViewHorizontalLength  = length(ViewHorizontal);
    float  SolarHorizontalLength = length(SolarHorizontal);
    float  ϕAzimuth              = 0.0f;
    if (ViewHorizontalLength > 1.0e-5f && SolarHorizontalLength > 1.0e-5f)
    {
        float cosϕ = clamp(dot(ViewHorizontal / ViewHorizontalLength, SolarHorizontal / SolarHorizontalLength), -1.0f, 1.0f);
        ϕAzimuth   = acos(cosϕ);
    }
    return saturate(float2(ϕAzimuth / π, CoordV));
}

void EvaluateMediumCoefficientsGpu(StructuredBuffer<float4> Ensemble,
                                   uint                     RealmOffset,
                                   float                    AltitudeKm,
                                   out float3               OutσRayleigh,
                                   out float                OutσMie,
                                   out float3               OutσExtinction)
{
    float4 R1 = Ensemble[RealmOffset + 1u];
    float4 R2 = Ensemble[RealmOffset + 2u];
    float4 R3 = Ensemble[RealmOffset + 3u];
    float4 R4 = Ensemble[RealmOffset + 4u];

    float Alt       = max(0.0f, AltitudeKm);
    float ρRayleigh = exp(-Alt / max(R1.w, 1.0e-3f));
    float ρMie      = exp(-Alt / max(R2.z, 1.0e-3f));
    float ρOzone    = max(0.0f, 1.0f - abs(Alt - R3.w) / max(R4.x, 1.0e-3f));

    OutσRayleigh   = R1.xyz * ρRayleigh;
    OutσMie        = R2.x * ρMie;
    OutσExtinction = max(OutσRayleigh + float3(R2.y * ρMie) + R3.xyz * ρOzone, float3(1.0e-8f));
}

// 6-point quadratic-warped Gauss-Legendre coupled solar/sky/twilight evaluator (< 2.85% vs FP64 continuum oracle)
float3 EvaluateCoupledSolarSky6Pt(StructuredBuffer<float4> Ensemble,
                                  Sampler2DArray<float4>   TransLut,
                                  Sampler2DArray<float4>   MultiLut,
                                  uint                     RealmId,
                                  float3                   ViewDirection,
                                  float                    MaxMarchKm)
{
    uint   RealmOffset = RealmSlotOffset(Ensemble, RealmId);
    float4 R0          = Ensemble[RealmOffset + 0u];
    float4 R2          = Ensemble[RealmOffset + 2u];
    float4 R8          = Ensemble[RealmOffset + 8u];
    float  Twilight    = Ensemble[RealmOffset + 14u].w;

    float  GroundR = R0.x;
    float  TopR    = R0.y;
    float  ObsR    = GroundR + max(R0.z, 1.0e-3f);
    float3 Origin  = float3(0.0f, ObsR, 0.0f);

    float MarchLen = DistanceToSphereExit(Origin, ViewDirection, TopR);
    float GroundT;
    if (HitsPlanetGround(Origin, ViewDirection, GroundR, GroundT))
    {
        MarchLen = min(MarchLen, GroundT);
    }
    if (MaxMarchKm > 0.0f)
    {
        MarchLen = min(MarchLen, MaxMarchKm);
    }
    if (MarchLen <= 1.0e-5f)
    {
        return float3(0.0f);
    }

    static const float kGlNodes[6] = {
        0.03376524f, 0.16939531f, 0.38069041f, 0.61930959f, 0.83060469f, 0.96623476f
    };
    static const float kGlWeights[6] = {
        0.08566225f, 0.18038079f, 0.23395697f, 0.23395697f, 0.18038079f, 0.08566225f
    };

    uint SunFirst = uint(R8.x + 0.5f);
    uint SunCount = uint(R8.y + 0.5f);

    float3 Radiance   = float3(0.0f);
    float3 Throughput = float3(1.0f);
    float  PrevDist   = 0.0f;

    const float WarpExponent = 2.22f;
    [unroll]
    for (int NodeIdx = 0; NodeIdx < 6; ++NodeIdx)
    {
        float NodeU       = kGlNodes[NodeIdx];
        float WarpedU     = pow(NodeU, WarpExponent);
        float RayDist     = MarchLen * WarpedU;
        float Jacobian    = MarchLen * (WarpExponent * pow(NodeU, WarpExponent - 1.0f));
        float StepWeight  = Jacobian * kGlWeights[NodeIdx];
        float ΔsSegment   = max(RayDist - PrevDist, 0.0f);
        PrevDist          = RayDist;

        float3 SamplePos  = Origin + ViewDirection * RayDist;
        float  RadiusKm   = length(SamplePos);
        float3 ZenithUnit = SamplePos / max(RadiusKm, 1.0e-4f);
        float  AltitudeKm = max(0.0f, RadiusKm - GroundR);

        float3 σRayleigh;
        float  σMie;
        float3 σExtinction;
        EvaluateMediumCoefficientsGpu(Ensemble, RealmOffset, AltitudeKm, σRayleigh, σMie, σExtinction);

        float3 HalfStepTrans = exp(-σExtinction * (0.5f * ΔsSegment));
        Throughput *= HalfStepTrans;

        float3 InScatter = float3(0.0f);
        for (uint SunIdx = 0u; SunIdx < SunCount; ++SunIdx)
        {
            uint   SunOffset      = SunSlotOffset(Ensemble, SunFirst + SunIdx);
            float3 SolarDirection = Ensemble[SunOffset + 0u].xyz;
            float3 SolarIrradiance = Ensemble[SunOffset + 1u].xyz;
            float  μSolar         = dot(ZenithUnit, SolarDirection);
            float  cosθ           = dot(ViewDirection, SolarDirection);
            float  PhaseRayleigh  = RayleighPhaseValue(cosθ);
            float  PhaseMie       = CornetteShanksPhaseValue(cosθ, R2.w);

            float  GroundBlockT;
            bool   UmbraBlocked = HitsPlanetGround(SamplePos, SolarDirection, GroundR, GroundBlockT);
            float3 SunTrans     = UmbraBlocked
                                      ? float3(0.0f)
                                      : SampleTransmittanceLutArray(TransLut, RealmId, GroundR, TopR, RadiusKm, μSolar);

            float2 MultiUv = saturate(float2(0.5f + 0.5f * μSolar, AltitudeKm / max(TopR - GroundR, 1.0e-3f)));
            float3 ΨMulti  = MultiLut.SampleLevel(float3(MultiUv, float(RealmId)), 0.0f).rgb;

            float TwilightGain = (μSolar < 0.06f && μSolar > -0.24f)
                                     ? (1.0f + 0.22f * max(Twilight - 1.0f, 0.0f) * smoothstep(-0.24f, 0.02f, μSolar))
                                     : 1.0f;

            float3 SingleOrder = (σRayleigh * PhaseRayleigh + float3(σMie * PhaseMie)) * SunTrans * TwilightGain;
            float3 MultiOrder  = (σRayleigh + float3(σMie)) * ΨMulti;
            InScatter         += (SingleOrder + MultiOrder) * SolarIrradiance;
        }

        Radiance   += Throughput * InScatter * StepWeight;
        Throughput *= HalfStepTrans;
    }

    return max(Radiance, float3(0.0f));
}

//------------------------------------------------------------------------------------------------------------------------
//                      TRANSMITTANCE (256x64) & MULTI-SCATTERING (32x32) COMPUTE KERNELS
//------------------------------------------------------------------------------------------------------------------------

#ifdef ATMOSPHERE_LUT_KERNEL

[[vk::push_constant]] PushDispatchHeader Push;

[[vk::binding(0, 0)]] StructuredBuffer<float4>                     Ensemble;
[[vk::binding(1, 0)]] Sampler2DArray<float4>                       SampledTransLut;
[[vk::binding(2, 0)]] Sampler2DArray<float4>                       SampledMultiLut;
[[vk::binding(3, 0)]] [format("rgba16f")] RWTexture2DArray<float4> OutputLut;

void TransmittanceUvToRadiusMu(float GroundR, float TopR, float2 Uv, out float OutRadiusKm, out float Outμ)
{
    float MaxChord = sqrt(max(TopR * TopR - GroundR * GroundR, 1.0e-5f));
    float ρ        = MaxChord * saturate(Uv.y);
    OutRadiusKm    = sqrt(ρ * ρ + GroundR * GroundR);
    float DistMin  = TopR - OutRadiusKm;
    float DistMax  = ρ + MaxChord;
    float DistTop  = DistMin + saturate(Uv.x) * (DistMax - DistMin);
    Outμ           = (DistTop <= 1.0e-5f)
                         ? 1.0f
                         : clamp((MaxChord * MaxChord - ρ * ρ - DistTop * DistTop) / (2.0f * OutRadiusKm * DistTop), -1.0f, 1.0f);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void BakeTransmittanceLutMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }
    uint   RealmId     = Push.Counts.z;
    uint   RealmOffset = RealmSlotOffset(Ensemble, RealmId);
    float4 R0          = Ensemble[RealmOffset + 0u];
    float  GroundR     = R0.x;
    float  TopR        = R0.y;

    float2 Uv = (float2(Tid.xy) + 0.5f) / float2(Push.Counts.xy);
    float  RadiusKm, μ;
    TransmittanceUvToRadiusMu(GroundR, TopR, Uv, RadiusKm, μ);

    float3 Origin    = float3(0.0f, RadiusKm, 0.0f);
    float3 Direction = float3(sqrt(max(0.0f, 1.0f - μ * μ)), μ, 0.0f);

    float ExitLen = DistanceToSphereExit(Origin, Direction, TopR);
    if (ExitLen <= 0.0f)
    {
        OutputLut[uint3(Tid.xy, RealmId)] = float4(1.0f, 1.0f, 1.0f, 1.0f);
        return;
    }

    const int kSteps = Push.Tuning.x > 0.0f ? clamp(int(Push.Tuning.x),8,64) : 40;
    float     Δs     = ExitLen / float(kSteps);
    float3    τ      = float3(0.0f);
    for (int Step = 0; Step < kSteps; ++Step)
    {
        float3 SamplePos  = Origin + Direction * ((float(Step) + 0.5f) * Δs);
        float  AltitudeKm = max(0.0f, length(SamplePos) - GroundR);
        float3 σRayleigh, σExtinction;
        float  σMie;
        EvaluateMediumCoefficientsGpu(Ensemble, RealmOffset, AltitudeKm, σRayleigh, σMie, σExtinction);
        τ += σExtinction * Δs;
    }

    OutputLut[uint3(Tid.xy, RealmId)] = float4(exp(-τ), 1.0f);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void BakeMultiScatterLutMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }
    if (Push.Tuning.y < 0.5f)
    {
        OutputLut[uint3(Tid.xy, Push.Counts.z)] = float4(0.0f,0.0f,0.0f,1.0f);
        return;
    }
    uint   RealmId      = Push.Counts.z;
    uint   RealmOffset  = RealmSlotOffset(Ensemble, RealmId);
    float4 R0           = Ensemble[RealmOffset + 0u];
    float  GroundR      = R0.x;
    float  TopR         = R0.y;
    float  GroundAlbedo = R0.w;

    float2 Uv             = (float2(Tid.xy) + 0.5f) / float2(Push.Counts.xy);
    float  μSolar         = clamp(Uv.x * 2.0f - 1.0f, -1.0f, 1.0f);
    float  RadiusKm       = GroundR + saturate(Uv.y) * max(TopR - GroundR - 1.0e-3f, 1.0e-3f);
    float3 Origin         = float3(0.0f, RadiusKm, 0.0f);
    float3 SolarDirection = float3(sqrt(max(0.0f, 1.0f - μSolar * μSolar)), μSolar, 0.0f);

    const int kDirectionCount = 16;
    const int kMarchSteps     = 16;
    float     ΔΩ              = 4.0f * π / float(kDirectionCount);
    float3    SecondOrderSum  = float3(0.0f);
    float3    TransferSum     = float3(0.0f);

    for (int DirIdx = 0; DirIdx < kDirectionCount; ++DirIdx)
    {
        float  cosθ         = 1.0f - 2.0f * (float(DirIdx) + 0.5f) / float(kDirectionCount);
        float  sinθ         = sqrt(max(0.0f, 1.0f - cosθ * cosθ));
        float  ϕ            = 2.39996323f * float(DirIdx);
        float3 RayDirection = float3(sinθ * cos(ϕ), cosθ, sinθ * sin(ϕ));

        float MaxT = DistanceToSphereExit(Origin, RayDirection, TopR);
        float GroundT;
        bool  HitGround = HitsPlanetGround(Origin, RayDirection, GroundR, GroundT);
        if (HitGround)
        {
            MaxT = min(MaxT, GroundT);
        }
        float  Δs       = MaxT / float(kMarchSteps);
        float3 PathTr   = float3(1.0f);
        float3 DirLum   = float3(0.0f);
        float3 DirTrans = float3(0.0f);

        for (int Step = 0; Step < kMarchSteps; ++Step)
        {
            float3 SamplePos  = Origin + RayDirection * ((float(Step) + 0.5f) * Δs);
            float  SampleR    = length(SamplePos);
            float3 ZenithUnit = SamplePos / max(SampleR, 1.0e-4f);
            float  AltitudeKm = max(0.0f, SampleR - GroundR);

            float3 σRayleigh, σExtinction;
            float  σMie;
            EvaluateMediumCoefficientsGpu(Ensemble, RealmOffset, AltitudeKm, σRayleigh, σMie, σExtinction);
            float3 σScatter = σRayleigh + float3(σMie);
            float3 StepTr   = exp(-σExtinction * Δs);
            float3 ScatInt  = σScatter * (1.0f - StepTr) / max(σExtinction, float3(1.0e-6f));

            float  GroundBlockT;
            bool   Blocked = HitsPlanetGround(SamplePos, SolarDirection, GroundR, GroundBlockT);
            float3 SunTr   = Blocked
                                 ? float3(0.0f)
                                 : SampleTransmittanceLutArray(
                                       SampledTransLut, RealmId, GroundR, TopR, SampleR, dot(ZenithUnit, SolarDirection));

            DirLum   += PathTr * ScatInt * SunTr * kInv4π;
            DirTrans += PathTr * ScatInt * kInv4π;
            PathTr   *= StepTr;
        }

        if (HitGround)
        {
            float3 GroundPos   = Origin + RayDirection * GroundT;
            float3 GroundUp    = SafeNormalize(GroundPos);
            float  μGroundSun  = max(0.0f, dot(GroundUp, SolarDirection));
            if (μGroundSun > 0.0f)
            {
                float3 SunTrG = SampleTransmittanceLutArray(
                    SampledTransLut, RealmId, GroundR, TopR, GroundR + 1.0e-3f, μGroundSun);
                DirLum += PathTr * SunTrG * (μGroundSun * GroundAlbedo / π);
            }
        }

        SecondOrderSum += DirLum * ΔΩ;
        TransferSum    += DirTrans * ΔΩ;
    }

    float3 Ψ = SecondOrderSum / max(1.0f - TransferSum, float3(0.05f));
    OutputLut[uint3(Tid.xy, RealmId)] = float4(max(Ψ, float3(0.0f)), 1.0f);
}

#endif
