//==========================================================================================================================================
//                                                OPTICAL LENS FLARE SLANG
//==========================================================================================================================================
// 1. EvaluateFlareVisibilityMain: 1x1-per-source GPU visibility & spectral transmittance reduction across
//    reversed-Z VisibilityImage, dielectric glass tint, VolumetricClouds transmittance, and cross-realm PortalAperture bounds.
// 2. BlendFlareApertureMain: multi-element chromatic ghosts, polygonal blade starburst, anamorphic streak,
//    22-degree atmospheric halo, and physically scaled veiling glare.

#include "PlanetaryRecords.slang"

[[vk::push_constant]] PushDispatchHeader Push;

[[vk::binding(0, 0)]] StructuredBuffer<float4>                     Ensemble;
[[vk::binding(1, 0)]] [format("rgba32f")] RWTexture2DArray<float4> VisibilityImage;
[[vk::binding(2, 0)]] [format("rgba16f")] RWTexture2DArray<float4> CloudWorkArray;
[[vk::binding(3, 0)]] [format("rgba16f")] RWTexture2DArray<float4> FlareVisibility;
[[vk::binding(4, 0)]] [format("rgba16f")] RWTexture2DArray<float4> HdrFrame;

[shader("compute")]
[numthreads(1, 1, 1)]
void EvaluateFlareVisibilityMain(uint3 Tid : SV_DispatchThreadID)
{
    uint FlareCount = Push.Counts.z;
    if (Tid.x >= FlareCount)
    {
        return;
    }

    uint   FlareTable  = uint(Ensemble[8].x + 0.5f);
    uint   FlareOffset = FlareTable + Tid.x * kFlareSourceStride;
    float4 Fs0         = Ensemble[FlareOffset + 0u]; // TargetRealm, SunIdxInRealm, PortalIdxPlusOne, GlobalSunSlot
    float4 Fs1         = Ensemble[FlareOffset + 1u]; // CamWorldDirXYZ, DiscAngularRadiusRad
    float4 Fs2         = Ensemble[FlareOffset + 2u]; // ScreenUvXY,IrrR,IrrG

    uint   SrcRealm   = uint(Fs0.x + 0.5f);
    int    PortalReq  = int(Fs0.z + 0.5f) - 1;
    float  θDiscRad   = max(Fs1.w, 1.0e-4f);
    float2 CenterUv   = Fs2.xy;

    float  TanHalfY   = max(Ensemble[1].w, 1.0e-4f);
    float  Aspect     = max(Ensemble[2].w, 1.0e-4f);
    float  DiscUvY    = max(θDiscRad / TanHalfY, 1.5f / max(float(Push.Counts.y), 1.0f));
    float  DiscUvX    = DiscUvY / Aspect;

    const int kDiscSamples = 16;
    float     VisAccum     = 0.0f;
    float3    TintAccum    = float3(0.0f);

    for (int SampleIdx = 0; SampleIdx < kDiscSamples; ++SampleIdx)
    {
        float  ρRadius  = sqrt((float(SampleIdx) + 0.5f) / float(kDiscSamples));
        float  ϕAngle   = 2.39996323f * float(SampleIdx);
        float2 SampleUv = CenterUv + float2(cos(ϕAngle) * DiscUvX, sin(ϕAngle) * DiscUvY) * ρRadius;
        if (SampleUv.x < 0.0f || SampleUv.x >= 1.0f || SampleUv.y < 0.0f || SampleUv.y >= 1.0f)
        {
            continue;
        }
        uint2  Pix        = min(uint2(SampleUv * float2(Push.Counts.xy)), Push.Counts.xy - uint2(1u, 1u));
        float  RevZ       = VisibilityImage[uint3(Pix, 0u)].w;
        uint   PixRealm   = uint(VisibilityImage[uint3(Pix, 1u)].w + 0.5f);
        float4 Glass4     = VisibilityImage[uint3(Pix, 3u)];
        int    PixPortal  = int(Glass4.w + 0.5f) - 1;
        float  CloudTrans = ResolveSolarDiscTransmittance(CloudWorkArray[uint3(Pix, 1u)].w);

        bool RealmMatch  = (PixRealm == SrcRealm);
        bool PortalMatch = (PortalReq < 0) ? (PixPortal < 0) : (PixPortal == PortalReq);
        bool Unoccluded  = (RevZ <= 0.0f);

        if (RealmMatch && PortalMatch && Unoccluded)
        {
            VisAccum  += CloudTrans;
            TintAccum += Glass4.rgb * CloudTrans;
        }
    }

    float  Visibility = VisAccum / float(kDiscSamples);
    float3 SpecVis    = TintAccum / float(kDiscSamples);
    FlareVisibility[uint3(Tid.x, 0u, 0u)] = float4(SpecVis, Visibility);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void BlendFlareApertureMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }

    uint FlareCount = Push.Counts.z;
    if (FlareCount == 0u)
    {
        return;
    }

    float  Aspect     = max(Ensemble[2].w, 1.0e-4f);
    float2 Uv         = (float2(Tid.xy) + 0.5f) / float2(Push.Counts.xy);
    float2 PixelNdc   = float2(Uv.x * 2.0f - 1.0f, 1.0f - Uv.y * 2.0f);
    uint   FlareTable = uint(Ensemble[8].x + 0.5f);

    static const float kGhostScales[6] = { -0.35f, -0.78f, -1.22f, 0.48f, -1.68f, 0.22f };
    static const float kGhostRadii[6]  = {  0.075f, 0.130f, 0.055f, 0.180f, 0.240f, 0.042f };
    static const float3 kGhostTints[6] = {
        float3(1.00f, 0.52f, 0.28f),
        float3(0.32f, 0.86f, 1.00f),
        float3(0.75f, 0.42f, 1.00f),
        float3(0.38f, 1.00f, 0.62f),
        float3(1.00f, 0.80f, 0.36f),
        float3(0.48f, 0.66f, 1.00f)
    };

    float3 FlareSum = float3(0.0f);
    for (uint FIdx = 0u; FIdx < FlareCount; ++FIdx)
    {
        float4 VisRecord = FlareVisibility[uint3(FIdx, 0u, 0u)];
        if (VisRecord.w <= 1.0e-4f)
        {
            continue;
        }

        uint   FlareOffset = FlareTable + FIdx * kFlareSourceStride;
        float4 Fs0         = Ensemble[FlareOffset + 0u];
        float4 Fs2         = Ensemble[FlareOffset + 2u];
        float4 Fs3         = Ensemble[FlareOffset + 3u];
        uint   SunOffset   = SunSlotOffset(Ensemble, uint(Fs0.w + 0.5f));

        float4 S2 = Ensemble[SunOffset + 2u]; // UvXY, MasterFlareGain, GhostStrength
        float4 S3 = Ensemble[SunOffset + 3u]; // ChromaticShift, HaloStrength, HaloRadiusNdc, HaloWidthNdc
        float4 S4 = Ensemble[SunOffset + 4u]; // StreakStrength, AnamorphicLengthNdc, StreakAngleRad, AnamorphicStrength
        float4 S5 = Ensemble[SunOffset + 5u]; // StarburstStrength, ApertureBlades, ApertureRoundness, VeilingStrength

        float2 SrcUv        = Fs2.xy;
        float2 SrcNdc       = float2(SrcUv.x * 2.0f - 1.0f, 1.0f - SrcUv.y * 2.0f);
        float3 SolarIrr     = float3(Fs2.z, Fs2.w, Fs3.x);
        float3 SrcFlux      = SolarIrr * VisRecord.rgb * max(S2.z, 0.0f);
        float2 Delta        = float2((PixelNdc.x - SrcNdc.x) * Aspect, PixelNdc.y - SrcNdc.y);
        float  ρDist        = length(Delta);
        float  ϕAngle       = atan2(Delta.y, Delta.x) + S4.z;
        float  EdgeVignette = saturate(1.25f - 0.65f * length(SrcNdc));

        float3 SourceContrib = float3(0.0f);

        // 1. Aperture-blade diffraction starburst + coronagraph glow
        if (S5.x > 0.0f)
        {
            float Blades = max(3.0f, round(S5.y));
            float Spoke  = pow(abs(cos(0.5f * Blades * ϕAngle)), 22.0f);
            float Radial = exp(-ρDist * 9.5f) + 0.28f * Spoke / (1.0f + 38.0f * ρDist * ρDist);
            SourceContrib += SrcFlux * (S5.x * 0.055f * Radial);
        }

        // 2. Anamorphic & directional streak
        float StreakGain = max(S4.x, S4.w);
        if (StreakGain > 0.0f)
        {
            float cosα  = cos(S4.z);
            float sinα  = sin(S4.z);
            float AxisU =  Delta.x * cosα + Delta.y * sinα;
            float AxisV = -Delta.x * sinα + Delta.y * cosα;
            float Streak = exp(-abs(AxisV) * 52.0f) * exp(-abs(AxisU) / max(S4.y * 0.45f, 0.25f));
            SourceContrib += SrcFlux * float3(0.68f, 0.84f, 1.0f) * (StreakGain * 0.040f * Streak);
        }

        // 3. Multi-element chromatic anti-reflection ghosts along optical axis
        if (S2.w > 0.0f)
        {
            float ChromShift = max(S3.x, 0.004f);
            [unroll]
            for (int GhostIdx = 0; GhostIdx < 6; ++GhostIdx)
            {
                float2 GhostCtr  = SrcNdc * kGhostScales[GhostIdx];
                float  Rad       = kGhostRadii[GhostIdx];
                float2 Disp      = SafeNormalize(float3(GhostCtr, 1.0e-5f)).xy * (ChromShift * Rad * 1.4f);
                float2 DiffR     = float2((PixelNdc.x - (GhostCtr.x + Disp.x)) * Aspect, PixelNdc.y - (GhostCtr.y + Disp.y));
                float2 DiffG     = float2((PixelNdc.x - GhostCtr.x) * Aspect, PixelNdc.y - GhostCtr.y);
                float2 DiffB     = float2((PixelNdc.x - (GhostCtr.x - Disp.x)) * Aspect, PixelNdc.y - (GhostCtr.y - Disp.y));
                float  NormR     = length(DiffR) / Rad;
                float  NormG     = length(DiffG) / Rad;
                float  NormB     = length(DiffB) / Rad;
                float3 RingShape = float3(
                    smoothstep(1.0f, 0.72f, NormR) * (0.35f + 0.65f * smoothstep(0.0f, 0.88f, NormR)),
                    smoothstep(1.0f, 0.72f, NormG) * (0.35f + 0.65f * smoothstep(0.0f, 0.88f, NormG)),
                    smoothstep(1.0f, 0.72f, NormB) * (0.35f + 0.65f * smoothstep(0.0f, 0.88f, NormB)));
                SourceContrib += SrcFlux * kGhostTints[GhostIdx] * RingShape * (S2.w * 0.024f * EdgeVignette);
            }
        }

        // 4. 22-degree hexagonal ice-crystal atmospheric halo
        if (S3.y > 0.0f)
        {
            float HaloRadius = max(S3.z, 0.25f);
            float HaloWidth  = max(S3.w, 0.03f);
            float HaloBand   = exp(-pow((ρDist - HaloRadius) / HaloWidth, 2.0f));
            SourceContrib   += SrcFlux * float3(1.04f, 0.98f, 0.92f) * (S3.y * 0.020f * HaloBand);
        }

        // 5. Physically scaled intra-ocular / lens veiling glare (preserves night-portal contrast)
        if (S5.w > 0.0f)
        {
            float VeilProfile = 0.004f + 0.008f * exp(-pow(ρDist / 1.2f, 2.0f));
            SourceContrib    += SrcFlux * (S5.w * VeilProfile * EdgeVignette);
        }

        FlareSum += SourceContrib;
    }

    float3 InitialHdr            = HdrFrame[uint3(Tid.xy, 0u)].rgb;
    HdrFrame[uint3(Tid.xy, 0u)]  = float4(InitialHdr + FlareSum, 1.0f);
}
