//==========================================================================================================================================
//                                                    CLOUD SYNTHESIS SLANG
//==========================================================================================================================================
// Non-tiled 3D world-space procedural cloud extinction (6 tropospheric height profiles, rotated 3D octaves, Worley erosion),
// ForceField coupling (Ekman wind turning/shear + curved-spine Burgers-Rott tornado condensation funnel & swirling wall cloud),
// and hybrid orthographic + analytic receiver-to-sun cloud shadow transmittance.

#include "AtmosphereScattering.slang"
#include "ForceField.slang"
#include "VolumeBounds.slang"

float EvaluateTroposphericEnvelope(uint ProfileId, float HeightFraction)
{
    float H = saturate(HeightFraction);
    if (ProfileId == 0u) // Stratus
    {
        return smoothstep(0.0f, 0.12f, H) * smoothstep(1.0f, 0.75f, H);
    }
    if (ProfileId == 1u) // Stratocumulus
    {
        return smoothstep(0.0f, 0.16f, H) * smoothstep(1.0f, 0.65f, H);
    }
    if (ProfileId == 3u) // Cumulonimbus
    {
        return smoothstep(0.0f, 0.10f, H) * (0.72f + 0.28f * smoothstep(0.55f, 0.85f, H)) * smoothstep(1.0f, 0.92f, H);
    }
    if (ProfileId == 4u) // Altostratus
    {
        return smoothstep(0.0f, 0.22f, H) * smoothstep(1.0f, 0.72f, H);
    }
    if (ProfileId == 5u) // Cirrus
    {
        return sin(π * H) * (0.65f + 0.35f * H);
    }
    // Default (2u): Cumulus — crisp flat condensation level, billowing rounded top
    return smoothstep(0.0f, 0.13f, H) * smoothstep(1.0f, 0.68f, H);
}

float EvaluateCloudExtinctionAtPoint(StructuredBuffer<float4> Ensemble,
                                     uint                     RealmId,
                                     uint                     CloudOffset,
                                     float3                   PointMeters)
{
    float4 C0 = Ensemble[CloudOffset + 0u]; // LowerAltM, ThicknessM, Coverage, DensityScale
    float4 C1 = Ensemble[CloudOffset + 1u]; // DetailErosion, ExtinctionPerM, ScatterAlbedo, ForwardG
    float4 C2 = Ensemble[CloudOffset + 2u]; // BackG, HeightProfileId, PowderStrength, IsLocalBox
    float4 C3 = Ensemble[CloudOffset + 3u]; // BoxCenterXYZ, EdgeSoftnessM
    float4 C4 = Ensemble[CloudOffset + 4u]; // BoxHalfXYZ, WindOffsetX

    float4 C6 = Ensemble[CloudOffset + 6u]; // Bounds shape, feature scale metres, anvil, integrated displacement
    float3 Offset = Ensemble[CloudOffset + 7u].xyz;
    float3 AdvectedMeters = C6.w > 0.5f ? PointMeters - Offset : PointMeters + float3(C4.w, 0.0f, 0.38f * C4.w);
    float FunnelCondensation = 0.0f;
    float WallCollarBoost = 0.0f;
    if (C2.w <= 0.5f)
    {
        float3 DomainPoint = AdvectedMeters;
        EvaluateForceFieldCloudDomain(Ensemble, RealmId, PointMeters, DomainPoint, FunnelCondensation, WallCollarBoost);
        if (C6.w <= 0.5f) AdvectedMeters = DomainPoint;
    }

    float HeightFraction = 0.0f;
    float BoxEnvelope    = 1.0f;
    bool  InsideMainSlab = false;

    if (C2.w > 0.5f)
    {
        float3 LocalPos = PointMeters - C3.xyz;
        uint Shape = uint(C6.x + 0.5f);
        float3 HalfSpan = ResolveVolumeHalfExtent(C4.xyz, Shape);
        BoxEnvelope = EvaluateVolumeEnvelope(LocalPos, HalfSpan, Shape, C3.w);
        if (BoxEnvelope <= 0.0f) return 0.0f;
        HeightFraction = Shape == 3u ? saturate(LocalPos.y / HalfSpan.y) :
            saturate((LocalPos.y + HalfSpan.y) / (2.0f * HalfSpan.y));
        InsideMainSlab = true;
    }
    else if (C0.y > 0.0f && PointMeters.y >= C0.x && PointMeters.y <= C0.x + C0.y)
    {
        HeightFraction = saturate((PointMeters.y - C0.x) / C0.y);
        InsideMainSlab = true;
    }

    float SlabDensity = 0.0f;
    if (InsideMainSlab)
    {
        uint  ProfileId = uint(max(0.0f, C2.y) + 0.5f);
        float Envelope  = EvaluateTroposphericEnvelope(ProfileId, HeightFraction);
        if (ProfileId == 3u) Envelope *= 1.0f + saturate(C6.z) * HeightFraction * 0.5f;
        if (WallCollarBoost > 0.0f)
        {
            Envelope = max(Envelope, saturate(WallCollarBoost * (1.0f - 0.65f * HeightFraction)));
        }
        if (Envelope > 1.0e-5f && BoxEnvelope > 1.0e-5f)
        {
            float  SpatialFreq = (C2.w > 0.5f) ? (1.8f / max(max(C4.x, C4.z), 4.0f)) : 1.15e-3f;
            if (C6.y > 0.0f) SpatialFreq = 1.0f / max(1.0f, C6.y);
            float3 SampleCoord = AdvectedMeters * float3(SpatialFreq, SpatialFreq * 1.45f, SpatialFreq);

            float MacroFbm    = FbmRotated3D(SampleCoord, 4, 17u);
            float MacroWorley = WorleyInverted3D(RotateOctave3D(SampleCoord) * 1.85f, 53u);
            float MacroShape  = 0.60f * MacroFbm + 0.40f * MacroWorley;
            float EffCoverage = saturate(C0.z + 0.35f * WallCollarBoost);
            float Threshold   = 1.0f - EffCoverage;
            float Remapped    = saturate((MacroShape - Threshold) / max(1.0f - Threshold, 1.0e-3f)) * Envelope * BoxEnvelope;
            if (Remapped > 1.0e-5f)
            {
                float3 DetailCoord = RotateOctave3D(SampleCoord * 5.2f);
                float  DetailField = 0.65f * WorleyInverted3D(DetailCoord, 131u) +
                                     0.35f * FbmRotated3D(DetailCoord * 2.1f, 3, 257u);
                SlabDensity = saturate((Remapped - C1.x * (1.0f - DetailField) * (1.0f - 0.50f * HeightFraction)) /
                                       max(1.0f - 0.5f * C1.x, 0.15f));
            }
        }
    }

    float ErodedFunnel = 0.0f;
    if (FunnelCondensation > 1.0e-4f)
    {
        float3 SwirlCoord  = AdvectedMeters * float3(0.038f, 0.026f, 0.038f);
        float  FunnelMacro = 0.58f * WorleyInverted3D(SwirlCoord, 311u) +
                             0.42f * FbmRotated3D(SwirlCoord * 1.85f, 3, 613u);
        ErodedFunnel       = saturate((FunnelCondensation * (0.62f + 0.72f * FunnelMacro) - 0.10f) / 0.90f) * 2.25f;
    }

    float CombinedDensity = max(SlabDensity, ErodedFunnel);
    if (CombinedDensity <= 1.0e-5f)
    {
        return 0.0f;
    }
    return CombinedDensity * max(0.0f, C0.w) * max(0.0f, C1.y);
}

// Analytic receiver-to-sun cloud transmittance through all active cloud volumes in RealmId.
float EvaluateCloudSolarTransmittance(StructuredBuffer<float4> Ensemble,
                                      uint                     RealmId,
                                      float3                   ReceiverMeters,
                                      float3                   SolarDirection)
{
    if (SolarDirection.y <= 0.02f)
    {
        return 1.0f;
    }
    uint   RealmOffset = RealmSlotOffset(Ensemble, RealmId);
    float4 R9          = Ensemble[RealmOffset + 9u];
    uint   CloudTable  = uint(Ensemble[6].x + 0.5f);
    uint   CloudFirst  = uint(R9.z + 0.5f);
    uint   CloudCount  = uint(R9.w + 0.5f);
    if (CloudCount == 0u)
    {
        return 1.0f;
    }

    float VortexNearT = 1.0e30f;
    float VortexFarT  = 0.0f;
    bool  HitVortex   = IntersectActiveVortexColumns(
        Ensemble, RealmId, ReceiverMeters, SolarDirection, 18000.0f, VortexNearT, VortexFarT);

    float τAccum = 0.0f;
    for (uint Idx = 0u; Idx < CloudCount; ++Idx)
    {
        uint   CloudOffset = CloudTable + (CloudFirst + Idx) * kCloudStride;
        float4 C0          = Ensemble[CloudOffset + 0u];
        float4 C2          = Ensemble[CloudOffset + 2u];
        float4 C3          = Ensemble[CloudOffset + 3u];
        float4 C4          = Ensemble[CloudOffset + 4u];

        float NearT = 1.0e30f;
        float FarT  = 0.0f;
        if (C2.w > 0.5f)
        {
            float3 InvDir = 1.0f / select(abs(SolarDirection) > 1.0e-6f, SolarDirection, float3(1.0e-6f));
            float3 T0v    = (C3.xyz - C4.xyz - ReceiverMeters) * InvDir;
            float3 T1v    = (C3.xyz + C4.xyz - ReceiverMeters) * InvDir;
            float3 TMin   = min(T0v, T1v);
            float3 TMax   = max(T0v, T1v);
            NearT         = max(0.0f, max(TMin.x, max(TMin.y, TMin.z)));
            FarT          = max(NearT, min(TMax.x, min(TMax.y, TMax.z)));
        }
        else
        {
            NearT = max(0.0f, (C0.x - ReceiverMeters.y) / SolarDirection.y);
            FarT  = max(NearT, (C0.x + C0.y - ReceiverMeters.y) / SolarDirection.y);
            if (HitVortex && Idx == 0u)
            {
                NearT = min(NearT, VortexNearT);
                FarT  = max(FarT, VortexFarT);
            }
        }
        if (FarT <= NearT)
        {
            continue;
        }

        const int kShadowSteps = 6;
        float     Δs           = (FarT - NearT) / float(kShadowSteps);
        [unroll]
        for (int Step = 0; Step < kShadowSteps; ++Step)
        {
            float3 SamplePos = ReceiverMeters + SolarDirection * (NearT + (float(Step) + 0.5f) * Δs);
            τAccum += EvaluateCloudExtinctionAtPoint(Ensemble, RealmId, CloudOffset, SamplePos) * Δs;
        }
    }
    return clamp(0.94f * exp(-τAccum) + 0.06f * exp(-0.25f * τAccum), 0.0f, 1.0f);
}

// Evaluates exact analytic receiver-to-sun transmittance through BoundedLocalBox cloud volumes (sub-meter shadow detail).
float EvaluateLocalBoxCloudTransmittance(StructuredBuffer<float4> Ensemble,
                                         uint                     RealmId,
                                         float3                   ReceiverMeters,
                                         float3                   SolarDirection)
{
    uint   RealmOffset = RealmSlotOffset(Ensemble, RealmId);
    float4 R9          = Ensemble[RealmOffset + 9u];
    uint   CloudTable  = uint(Ensemble[6].x + 0.5f);
    uint   CloudFirst  = uint(R9.z + 0.5f);
    uint   CloudCount  = uint(R9.w + 0.5f);
    float  τLocal      = 0.0f;

    for (uint Idx = 0u; Idx < CloudCount; ++Idx)
    {
        uint   CloudOffset = CloudTable + (CloudFirst + Idx) * kCloudStride;
        float4 C2          = Ensemble[CloudOffset + 2u];
        if (C2.w <= 0.5f)
        {
            continue;
        }
        float4 C3     = Ensemble[CloudOffset + 3u];
        float4 C4     = Ensemble[CloudOffset + 4u];
        float3 InvDir = 1.0f / select(abs(SolarDirection) > 1.0e-6f, SolarDirection, float3(1.0e-6f));
        float3 T0v    = (C3.xyz - C4.xyz - ReceiverMeters) * InvDir;
        float3 T1v    = (C3.xyz + C4.xyz - ReceiverMeters) * InvDir;
        float3 TMin   = min(T0v, T1v);
        float3 TMax   = max(T0v, T1v);
        float  NearT  = max(0.0f, max(TMin.x, max(TMin.y, TMin.z)));
        float  FarT   = min(TMax.x, min(TMax.y, TMax.z));
        if (FarT <= NearT)
        {
            continue;
        }
        const int kSteps = 6;
        float     Δs     = (FarT - NearT) / float(kSteps);
        [unroll]
        for (int Step = 0; Step < kSteps; ++Step)
        {
            float3 SamplePos = ReceiverMeters + SolarDirection * (NearT + (float(Step) + 0.5f) * Δs);
            τLocal += EvaluateCloudExtinctionAtPoint(Ensemble, RealmId, CloudOffset, SamplePos) * Δs;
        }
    }
    return (τLocal > 0.0f) ? clamp(0.94f * exp(-τLocal) + 0.06f * exp(-0.25f * τLocal), 0.0f, 1.0f) : 1.0f;
}

// Samples the 256x256 orthographic CloudShadowLut for planetary cloud slabs inside ±CloudShadowHalfSpanM,
// multiplies exact analytic BoundedLocalBox shadows, and transitions smoothly to analytic marching beyond the span.
float SampleOrTraceCloudTransmittance(StructuredBuffer<float4> Ensemble,
                                      Sampler2DArray<float4>   CloudShadowLut,
                                      uint                     RealmId,
                                      uint                     SunIndexInRealm,
                                      float3                   ReceiverMeters,
                                      float3                   SolarDirection)
{
    if (SolarDirection.y <= 0.02f)
    {
        return 1.0f;
    }
    uint RealmOffset = RealmSlotOffset(Ensemble, RealmId);
    uint CloudCount  = uint(Ensemble[RealmOffset + 9u].w + 0.5f);
    if (CloudCount == 0u)
    {
        return 1.0f;
    }
    if (SunIndexInRealm > 0u)
    {
        return EvaluateCloudSolarTransmittance(Ensemble, RealmId, ReceiverMeters, SolarDirection);
    }

    float  GroundPlaneY        = Ensemble[RealmOffset + 4u].y;
    float  HalfSpanM           = max(Ensemble[RealmOffset + 13u].z, 1.0f);
    float  RayToGroundScale    = (ReceiverMeters.y - GroundPlaneY) / max(SolarDirection.y, 0.05f);
    float2 GroundInterceptXZ   = ReceiverMeters.xz - SolarDirection.xz * RayToGroundScale;
    float  NormalizedChebyshev = max(abs(GroundInterceptXZ.x), abs(GroundInterceptXZ.y)) / HalfSpanM;

    if (NormalizedChebyshev >= 1.0f)
    {
        return EvaluateCloudSolarTransmittance(Ensemble, RealmId, ReceiverMeters, SolarDirection);
    }

    float2 ShadowUv = 0.5f + 0.5f * (GroundInterceptXZ / HalfSpanM);
    float  SlabTr   = CloudShadowLut.SampleLevel(float3(ShadowUv, float(RealmId)), 0.0f).g;
    float  LocalTr  = EvaluateLocalBoxCloudTransmittance(Ensemble, RealmId, ReceiverMeters, SolarDirection);
    float  OrthoTr  = min(SlabTr, LocalTr);
    if (NormalizedChebyshev <= 0.82f)
    {
        return OrthoTr;
    }

    float AnalyticTr = EvaluateCloudSolarTransmittance(Ensemble, RealmId, ReceiverMeters, SolarDirection);
    float BlendT     = smoothstep(0.82f, 1.0f, NormalizedChebyshev);
    return lerp(OrthoTr, AnalyticTr, BlendT);
}

// Fast O(1) orthographic cloud shadow lookup for multi-slice volumetric fog shafts.
float SampleFogCloudTransmittance(StructuredBuffer<float4> Ensemble,
                                  Sampler2DArray<float4>   CloudShadowLut,
                                  uint                     RealmId,
                                  float3                   ReceiverMeters,
                                  float3                   SolarDirection)
{
    if (SolarDirection.y <= 0.02f)
    {
        return 1.0f;
    }
    uint RealmOffset = RealmSlotOffset(Ensemble, RealmId);
    if (uint(Ensemble[RealmOffset + 9u].w + 0.5f) == 0u)
    {
        return 1.0f;
    }
    float  GroundPlaneY      = Ensemble[RealmOffset + 4u].y;
    float  HalfSpanM         = max(Ensemble[RealmOffset + 13u].z, 1.0f);
    float  RayToGroundScale  = (ReceiverMeters.y - GroundPlaneY) / max(SolarDirection.y, 0.05f);
    float2 GroundInterceptXZ = ReceiverMeters.xz - SolarDirection.xz * RayToGroundScale;
    float  Chebyshev         = max(abs(GroundInterceptXZ.x), abs(GroundInterceptXZ.y)) / HalfSpanM;
    if (Chebyshev >= 1.0f)
    {
        return 1.0f;
    }
    float2 ShadowUv = 0.5f + 0.5f * (GroundInterceptXZ / HalfSpanM);
    return CloudShadowLut.SampleLevel(float3(ShadowUv, float(RealmId)), 0.0f).g;
}
