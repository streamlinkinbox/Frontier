//==========================================================================================================================================
//                                                PLANETARY PROJECTION SLANG
//==========================================================================================================================================
// Multi-realm sky-view + 6-pt twilight coupling, Hipparcos octahedral starfield + Kolmogorov scintillation + Milky Way,
// 2048x1024x6 sRGB 6-body lunar sphere atlas (Luna, Ember, Glacier, Sulfur, Shroud, Shard; roll, libration, Lommel-Seeliger,
// opposition surge, earthshine), 3D ringed/atmospheric planets, and limb-darkened multi-sun discs.

#include "AtmosphereScattering.slang"

[[vk::push_constant]] PushDispatchHeader Push;

[[vk::binding(0, 0)]] StructuredBuffer<float4>                     Ensemble;
[[vk::binding(1, 0)]] StructuredBuffer<uint2>                      StarCellSpans;
[[vk::binding(2, 0)]] StructuredBuffer<float4>                     StarRecords;
[[vk::binding(3, 0)]] Sampler2DArray<float4>                       TransLut;
[[vk::binding(4, 0)]] Sampler2DArray<float4>                       MultiLut;
[[vk::binding(5, 0)]] Sampler2DArray<float4>                       SkyViewLut;
[[vk::binding(6, 0)]] Sampler2DArray<float4>                       LunarAtlas; // 2048x1024x6 VK_FORMAT_R8G8B8A8_SRGB
[[vk::binding(7, 0)]] [format("rgba32f")] RWTexture2DArray<float4> VisibilityImage;
[[vk::binding(8, 0)]] [format("rgba16f")] RWTexture2DArray<float4> HdrFrame;

static const uint kOctGridDim = 32u;

float2 DirectionToOctUv(float3 Direction)
{
    float InvL1 = 1.0f / max(abs(Direction.x) + abs(Direction.y) + abs(Direction.z), 1.0e-7f);
    float OctX  = Direction.x * InvL1;
    float OctY  = Direction.y * InvL1;
    if (Direction.z < 0.0f)
    {
        float WrappedX = (1.0f - abs(OctY)) * ((OctX >= 0.0f) ? 1.0f : -1.0f);
        float WrappedY = (1.0f - abs(OctX)) * ((OctY >= 0.0f) ? 1.0f : -1.0f);
        OctX           = WrappedX;
        OctY           = WrappedY;
    }
    return saturate(float2(0.5f * OctX + 0.5f, 0.5f * OctY + 0.5f));
}

// 📝 Scintillation: the demo's three-frequency twinkle signal (NightStars) on the realm's twinkle clock τ. Its peak swing is the
//    twinkle depth at the zenith, deeper toward the horizon where the light crosses more air: × √(1.15 / (sin h + 0.15)), at most
//    × 2.5 (× 1.33 at 30°, × 1.9 at 10°). PlanetaryDomeProjection::EvaluateStarScintillation is its CPU twin.
float EvaluateScintillationGpu(float sinθAltitude, float Seed, float TwinkleClock, float Depth)
{
    if (Depth <= 0.0f)
    {
        return 1.0f;
    }
    float Phase        = Seed * kTwoπ;
    float Signal       = 0.60f * sin(TwinkleClock * kTwoπ + Phase) +
                         0.28f * sin(TwinkleClock * 11.71f + Phase * 1.7f) +
                         0.12f * sin(TwinkleClock * 19.37f + Phase * 2.3f);
    float AirmassDepth = min(2.5f, sqrt(1.15f / (max(sinθAltitude, 0.0f) + 0.15f)));
    return max(0.05f, 1.0f + Depth * AirmassDepth * Signal);
}

float3 EvaluateMilkyWayGpu(float3 CelestialDirection, float Intensity)
{
    if (Intensity <= 0.0f)
    {
        return float3(0.0f);
    }
    float3 GalacticNorth  = float3(-0.867666f, -0.198076f, 0.455984f);
    float3 GalacticCenter = float3(-0.054876f, -0.873437f, -0.483835f);
    float  sinβ           = dot(CelestialDirection, GalacticNorth);
    float  Band           = exp(-pow(sinβ / 0.16f, 2.0f));
    if (Band < 1.0e-4f)
    {
        return float3(0.0f);
    }
    float CoreBulge = 0.45f + 0.85f * pow(0.5f + 0.5f * dot(CelestialDirection, GalacticCenter), 2.2f);
    float Cloud     = 0.65f + 0.70f * FbmRotated3D(CelestialDirection * 6.5f, 4, 307u);
    float DustRift  = exp(-pow(sinβ / 0.045f, 2.0f)) *
                      smoothstep(0.42f, 0.78f, FbmRotated3D(CelestialDirection * 11.0f + float3(3.1f, -1.7f, 2.4f), 4, 911u));
    float Luminance = Intensity * 4.6e-3f * Band * CoreBulge * Cloud * (1.0f - 0.68f * DustRift);
    return float3(Luminance * 1.04f, Luminance * 0.99f, Luminance * 1.18f);
}

float3 EvaluatePlanetGpu(StructuredBuffer<float4> Ens,
                         uint                     PlanetOffset,
                         uint                     SunFirst,
                         uint                     SunCount,
                         float3                   ViewDirection,
                         float                    PixelSpanRad,
                         out float                OutAlpha)
{
    OutAlpha         = 0.0f;
    float4 P0        = Ens[PlanetOffset + 0u];
    float4 P1        = Ens[PlanetOffset + 1u];
    float4 P2        = Ens[PlanetOffset + 2u];
    float4 P3        = Ens[PlanetOffset + 3u];
    float4 P4        = Ens[PlanetOffset + 4u];
    float4 P5        = Ens[PlanetOffset + 5u];
    float4 P6 = Ens[PlanetOffset + 6u];
    float4 P7 = Ens[PlanetOffset + 7u];

    float3 CenterDir = P0.xyz;
    float  θRadius   = max(P0.w, 1.0e-5f);
    float  MaxReach  = max(1.0f + P3.w, P4.w) * 1.15f;
    if (dot(ViewDirection, CenterDir) < cos(min(π, θRadius * MaxReach + PixelSpanRad * 3.0f)))
    {
        return float3(0.0f);
    }

    float  Distance  = 1.0f / sin(min(θRadius, 1.2f));
    float3 PlanetPos = CenterDir * Distance;
    float3 SpinAxis  = P1.xyz;
    float3 RefAxis   = (abs(SpinAxis.y) < 0.9f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 EquatorX  = SafeNormalize(cross(RefAxis, SpinAxis));
    float3 EquatorZ  = SafeNormalize(cross(SpinAxis, EquatorX));

    float3 OffsetC = -PlanetPos;
    float  HalfB   = dot(OffsetC, ViewDirection);
    float  TermC   = dot(OffsetC, OffsetC) - 1.0f;
    float  Disc    = HalfB * HalfB - TermC;
    bool   HitSph  = (Disc >= 0.0f) && ((-HalfB - sqrt(max(Disc, 0.0f))) > 0.0f);
    float  BodyNear = HitSph ? (-HalfB - sqrt(max(Disc, 0.0f))) : 1.0e30f;

    bool   HitRing   = false;
    float  RingT     = 1.0e30f;
    float  RingAlpha = 0.0f;
    float3 RingCol   = float3(0.0f);
    if (P4.w > P4.z && P5.x > 0.0f)
    {
        float Denom = dot(ViewDirection, SpinAxis);
        if (abs(Denom) > 1.0e-6f)
        {
            float Tr = dot(PlanetPos, SpinAxis) / Denom;
            if (Tr > 0.0f)
            {
                float3 RingHit = ViewDirection * Tr - PlanetPos;
                float  RingR   = length(RingHit);
                if (RingR >= P4.z && RingR <= P4.w)
                {
                    float NormU = (RingR - P4.z) / max(P4.w - P4.z, 1.0e-4f);
                    float Gap   = smoothstep(0.02f, 0.08f, abs(NormU - 0.56f));
                    float Fine  = 0.65f + 0.35f * sin(NormU * 95.0f) * cos(NormU * 37.0f);
                    RingAlpha   = saturate(P5.x * Gap * Fine);
                    if (RingAlpha > 1.0e-4f)
                    {
                        HitRing        = true;
                        RingT          = Tr;
                        float3 RingAlb = P5.yzw * (0.82f + 0.30f * NormU);
                        for (uint SunIdx = 0u; SunIdx < SunCount; ++SunIdx)
                        {
                            uint   SunOffset       = SunSlotOffset(Ens, SunFirst + SunIdx);
                            float3 SolarDirection  = Ens[SunOffset + 0u].xyz;
                            float3 SolarIrradiance = Ens[SunOffset + 1u].xyz;
                            float  Sb              = dot(RingHit, SolarDirection);
                            float  Sc              = dot(RingHit, RingHit) - 1.0f;
                            float  Sdet            = Sb * Sb - Sc;
                            bool   InShadow        = (Sdet > 0.0f) && ((-Sb - sqrt(Sdet)) > 0.0f);
                            if (!InShadow)
                            {
                                float Tilt = 0.35f + 0.65f * abs(dot(SpinAxis, SolarDirection));
                                RingCol   += RingAlb * SolarIrradiance * (0.14f * Tilt);
                            }
                        }
                    }
                }
            }
        }
    }

    float3 BodyCol = float3(0.0f);
    if (HitSph)
    {
        float3 Normal = SafeNormalize(ViewDirection * BodyNear - PlanetPos);
        float  φLat   = asin(clamp(dot(Normal, SpinAxis), -1.0f, 1.0f));
        float  λLon   = atan2(dot(Normal, EquatorZ), dot(Normal, EquatorX)) + P1.w;
        uint   Arch   = uint(P4.y + 0.5f);

        float3 Albedo = P2.xyz;
        if (Arch == 0u)
        {
            float Zones = 0.5f + 0.5f * sin(φLat * 18.0f + 0.35f * sin(λLon * 4.0f + φLat * 9.0f));
            Albedo     *= lerp(float3(0.76f, 0.68f, 0.58f), float3(1.16f, 1.10f, 1.02f), saturate(.5f + (Zones-.5f)*P6.x));
        }
        else if (Arch == 1u)
        {
            float3 SpherePt  = float3(cos(φLat) * cos(λLon), sin(φLat), cos(φLat) * sin(λLon));
            float  Continent = FbmRotated3D(SpherePt * 3.4f, 4, 509u);
            float  CloudDeck = smoothstep(0.52f, 0.78f, FbmRotated3D(SpherePt * 5.8f + float3(1.7f, 0.4f, -1.2f), 4, 811u));
            Albedo           = (Continent > 0.53f) ? float3(0.24f, 0.34f, 0.18f) : P2.xyz;
            Albedo           = lerp(Albedo, float3(0.92f, 0.94f, 0.97f), 0.72f * CloudDeck);
        }
        else if (Arch == 2u)
        {
            float3 SpherePt = float3(cos(φLat) * cos(λLon), sin(φLat), cos(φLat) * sin(λLon));
            Albedo         *= (1.0f + P6.y * (-0.28f + 0.48f * FbmRotated3D(SpherePt * 4.2f, 4, 613u + uint(P6.w))));
        }
        else if (Arch == 4u)
        {
            float3 SpherePt = float3(cos(φLat)*cos(λLon),sin(φLat),cos(φLat)*sin(λLon));
            float Cloud = FbmRotated3D(SpherePt*5.0f,4,997u + uint(P6.w));
            Albedo = lerp(float3(.60f,.40f,.18f),float3(.96f,.87f,.66f),saturate(.25f + Cloud * P6.z));
        }
        else
        {
            Albedo *= (0.90f + 0.14f * cos(φLat * 8.0f));
        }

        Albedo *= max(P7.rgb,float3(0.0f));

        float μView = max(0.02f, dot(Normal, -ViewDirection));
        for (uint SunIdx = 0u; SunIdx < SunCount; ++SunIdx)
        {
            uint   SunOffset       = SunSlotOffset(Ens, SunFirst + SunIdx);
            float3 SolarDirection  = Ens[SunOffset + 0u].xyz;
            float3 SolarIrradiance = Ens[SunOffset + 1u].xyz;
            float  μ₀              = max(0.0f, dot(Normal, SolarDirection));
            if (μ₀ <= 0.0f)
            {
                continue;
            }

            float RingTrans = 1.0f;
            if (P4.w > P4.z && P5.x > 0.0f)
            {
                float Den = dot(SolarDirection, SpinAxis);
                if (abs(Den) > 1.0e-5f)
                {
                    float Trs = -dot(Normal, SpinAxis) / Den;
                    if (Trs > 0.0f)
                    {
                        float RRad = length(Normal + SolarDirection * Trs);
                        if (RRad >= P4.z && RRad <= P4.w)
                        {
                            float NormU = (RRad - P4.z) / max(P4.w - P4.z, 1.0e-4f);
                            float Gap   = smoothstep(0.02f, 0.08f, abs(NormU - 0.56f));
                            RingTrans   = 1.0f - saturate(P5.x * Gap);
                        }
                    }
                }
            }
            float Limb = pow(μ₀, 0.82f) * pow(μView, 0.14f);
            BodyCol   += Albedo * (P2.w / π) * SolarIrradiance * (0.28f * Limb * RingTrans);
        }
        BodyCol += Albedo * (P2.w / .55f) * 2.5e-4f;
    }

    float3 AtmoGlow = float3(0.0f);
    if (P3.w > 0.0f && P4.x > 0.0f)
    {
        float AtmoR = 1.0f + P3.w;
        float Adet  = HalfB * HalfB - (dot(OffsetC, OffsetC) - AtmoR * AtmoR);
        if (Adet > 0.0f)
        {
            float T0   = max(0.0f, -HalfB - sqrt(Adet));
            float T1   = HitSph ? min(-HalfB + sqrt(Adet), BodyNear) : (-HalfB + sqrt(Adet));
            float Plen = max(0.0f, T1 - T0);
            if (Plen > 0.0f)
            {
                float3 Mid   = ViewDirection * (0.5f * (T0 + T1)) - PlanetPos;
                float3 MidUp = SafeNormalize(Mid);
                float  Opt   = P4.x * Plen * exp(-max(0.0f, (length(Mid) - 1.0f) / max(P3.w * 0.45f, 1.0e-3f)));
                for (uint SunIdx = 0u; SunIdx < SunCount; ++SunIdx)
                {
                    uint   SunOffset       = SunSlotOffset(Ens, SunFirst + SunIdx);
                    float3 SolarDirection  = Ens[SunOffset + 0u].xyz;
                    float3 SolarIrradiance = Ens[SunOffset + 1u].xyz;
                    float  Lit             = smoothstep(-0.22f, 0.35f, dot(MidUp, SolarDirection));
                    AtmoGlow              += P3.xyz * SolarIrradiance * (0.09f * Opt * Lit);
                }
            }
        }
    }

    float3 Combined = float3(0.0f);
    if (HitSph && HitRing)
    {
        Combined = (RingT < BodyNear) ? (RingCol * RingAlpha + (BodyCol + AtmoGlow) * (1.0f - RingAlpha)) : (BodyCol + AtmoGlow);
        OutAlpha = 1.0f;
    }
    else if (HitSph)
    {
        Combined = BodyCol + AtmoGlow;
        OutAlpha = 1.0f;
    }
    else if (HitRing)
    {
        Combined = RingCol * RingAlpha + AtmoGlow;
        OutAlpha = RingAlpha;
    }
    else
    {
        Combined = AtmoGlow;
        OutAlpha = 0.0f;
    }
    return Combined;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void RasterizePlanetaryProjectionMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }

    // 📝 Alpha carries the solar discs' share of the pixel to ReconstructVolumetricCloudsMain (which writes 1 back): a cloud
    //    hides the disc faster than the sky behind it. Surfaces carry none.
    float4 Vis0 = VisibilityImage[uint3(Tid.xy, 0u)];
    if (Vis0.w > 0.0f)
    {
        HdrFrame[uint3(Tid.xy, 0u)] = float4(Vis0.rgb, 0.0f);
        return;
    }

    uint   RealmId       = uint(VisibilityImage[uint3(Tid.xy, 1u)].w + 0.5f);
    float3 ViewDirection = SafeNormalize(VisibilityImage[uint3(Tid.xy, 2u)].xyz);
    float3 GlassTint     = VisibilityImage[uint3(Tid.xy, 3u)].rgb;

    uint   RealmOffset = RealmSlotOffset(Ensemble, RealmId);
    float4 R0          = Ensemble[RealmOffset + 0u];
    float4 R5          = Ensemble[RealmOffset + 5u];
    float4 R8          = Ensemble[RealmOffset + 8u];
    float4 R9          = Ensemble[RealmOffset + 9u];
    float  GroundR     = R0.x;
    float  TopR        = R0.y;
    float  ObserverR   = GroundR + max(R0.z, 1.0e-3f);

    uint SunFirst    = uint(R8.x + 0.5f);
    uint SunCount    = uint(R8.y + 0.5f);
    uint MoonFirst   = uint(R8.z + 0.5f);
    uint MoonCount   = uint(R8.w + 0.5f);
    uint PlanetFirst = uint(R9.x + 0.5f);
    uint PlanetCount = uint(R9.y + 0.5f);

    // 1. Coupled Solar + Sky + Twilight scattered atmosphere
    float3 SkyScattering = float3(0.0f);
    if (SunCount == 1u)
    {
        float3 PrimarySunDir = Ensemble[SunSlotOffset(Ensemble, SunFirst)].xyz;
        float2 SkyUv         = DirectionToSkyViewUv(GroundR, ObserverR, ViewDirection, PrimarySunDir);
        SkyScattering        = SkyViewLut.SampleLevel(float3(SkyUv, float(RealmId)), 0.0f).rgb;
    }
    else if (SunCount > 1u)
    {
        SkyScattering = EvaluateCoupledSolarSky6Pt(Ensemble, TransLut, MultiLut, RealmId, ViewDirection, 0.0f);
    }

    float3 SkyTrans = (ViewDirection.y > -0.08f)
                          ? SampleTransmittanceLutArray(TransLut, RealmId, GroundR, TopR, ObserverR, ViewDirection.y)
                          : float3(0.0f);

    float TanHalfY     = Ensemble[1].w;
    float PixelSpanRad = (2.0f * TanHalfY) / max(float(Push.Counts.y), 1.0f);

    // 2. Transform ViewDirection into Realm's celestial star frame
    float3 StarRow0      = Ensemble[RealmOffset + 14u].xyz;
    float3 StarRow1      = Ensemble[RealmOffset + 15u].xyz;
    float3 StarRow2      = SafeNormalize(cross(StarRow0, StarRow1));
    float3 CelestialDir  = SafeNormalize(float3(dot(StarRow0, ViewDirection), dot(StarRow1, ViewDirection), dot(StarRow2, ViewDirection)));
    float  StarLimitMag  = Ensemble[RealmOffset + 15u].w;

    float3 ExoRadiance = EvaluateMilkyWayGpu(CelestialDir, R5.w);

    // 3. Cell-indexed Hipparcos stars (3x3 octahedral neighborhood) + Kolmogorov scintillation
    if (R5.y > 0.0f && ViewDirection.y > -0.02f && uint(Ensemble[8].z + 0.5f) > 0u)
    {
        float2 OctUv  = DirectionToOctUv(CelestialDir);
        int    Cx0    = clamp(int(OctUv.x * float(kOctGridDim)), 0, int(kOctGridDim) - 1);
        int    Cy0    = clamp(int(OctUv.y * float(kOctGridDim)), 0, int(kOctGridDim) - 1);
        float  σPsf   = max(0.85f * PixelSpanRad, 3.5e-4f) * max(.2f,Ensemble[RealmOffset+18u].x);
        float  Cutoff = (Ensemble[RealmOffset+18u].z > 0 ? 8.0f : 3.6f) * σPsf;

        for (int Dy = -1; Dy <= 1; ++Dy)
        {
            int Cy = Cy0 + Dy;
            if (Cy < 0 || Cy >= int(kOctGridDim))
            {
                continue;
            }
            for (int Dx = -1; Dx <= 1; ++Dx)
            {
                int Cx = Cx0 + Dx;
                if (Cx < 0 || Cx >= int(kOctGridDim))
                {
                    continue;
                }
                uint2 Span = StarCellSpans[uint(Cy) * kOctGridDim + uint(Cx)];
                for (uint StarIter = 0u; StarIter < Span.y; ++StarIter)
                {
                    uint   StIdx = (Span.x + StarIter) * 2u;
                    float4 St0   = StarRecords[StIdx + 0u];
                    if (St0.w > StarLimitMag)
                    {
                        continue;
                    }
                    float cosθ = dot(CelestialDir, St0.xyz);
                    if (cosθ <= 0.998f)
                    {
                        continue;
                    }
                    float θStar = atan2(length(cross(CelestialDir, St0.xyz)), cosθ);
                    if (θStar > Cutoff)
                    {
                        continue;
                    }
                    float4 St1     = StarRecords[StIdx + 1u];
                    float  Flux    = pow(10.0f, -0.34f * (St0.w - 0.5f)) * 0.28f * R5.y;
                    float  Core    = exp(-0.5f * (θStar * θStar) / (σPsf * σPsf));
                    float  Twinkle = EvaluateScintillationGpu(ViewDirection.y, St1.w, Ensemble[RealmOffset + 18u].y, R5.z);
                    float Halo = Ensemble[RealmOffset+18u].z * .08f * exp(-.125f*(θStar*θStar)/(σPsf*σPsf));
                    ExoRadiance   += St1.xyz * (Flux * (Core+Halo) * Twinkle);
                }
            }
        }
    }

    // Custom 6-blade Airy stars
    uint CustomStarTable = uint(Ensemble[8].y + 0.5f);
    uint CustomStarFirst = uint(Ensemble[RealmOffset + 13u].x + 0.5f);
    uint CustomStarCount = uint(Ensemble[RealmOffset + 13u].y + 0.5f);
    for (uint CIdx = 0u; CIdx < CustomStarCount; ++CIdx)
    {
        uint   CSOffset = CustomStarTable + (CustomStarFirst + CIdx) * kCustomStarStride;
        float4 Cs0      = Ensemble[CSOffset + 0u];
        float4 Cs1      = Ensemble[CSOffset + 1u];
        float  cosθ     = dot(ViewDirection, Cs0.xyz);
        if (cosθ > 0.996f)
        {
            float θStar = atan2(length(cross(ViewDirection, Cs0.xyz)), cosθ);
            float σCore = max(1.1f * PixelSpanRad, 4.0e-4f);
            float Flux  = pow(10.0f, -0.4f * (Cs0.w + 1.5f)) * 0.045f;
            float Core  = exp(-0.5f * (θStar * θStar) / (σCore * σCore));
            ExoRadiance += Cs1.xyz * (Flux * Core);
        }
    }

    // 4. Exogenous 3D ringed & atmospheric planets (occlude stars behind them)
    uint PlanetTable = uint(Ensemble[5].w + 0.5f);
    for (uint PIdx = 0u; PIdx < PlanetCount; ++PIdx)
    {
        float  PlanetAlpha = 0.0f;
        float3 PlanetCol   = EvaluatePlanetGpu(
            Ensemble, PlanetTable + (PlanetFirst + PIdx) * kPlanetStride, SunFirst, SunCount, ViewDirection, PixelSpanRad, PlanetAlpha);
        ExoRadiance = ExoRadiance * (1.0f - PlanetAlpha) + PlanetCol;
    }

    // 5. 6-Body 2048x1024 sRGB Lunar Atlas spheres (Luna, Ember, Glacier, Sulfur, Shroud, Shard)
    uint MoonTable = uint(Ensemble[5].z + 0.5f);
    for (uint MIdx = 0u; MIdx < MoonCount; ++MIdx)
    {
        uint   MoonOffset    = MoonTable + (MoonFirst + MIdx) * kMoonStride;
        float4 M0            = Ensemble[MoonOffset + 0u];
        float4 M1            = Ensemble[MoonOffset + 1u];
        float4 M2            = Ensemble[MoonOffset + 2u];
        float4 M3            = Ensemble[MoonOffset + 3u];
        float4 M4            = Ensemble[MoonOffset + 4u]; // AtmosphereTintRgb.xyz, AtmosphereHaze
        float4 M5 = Ensemble[MoonOffset + 5u];
        float4 M6 = Ensemble[MoonOffset + 6u];
        float3 MoonDirection = M0.xyz;
        float  θMoonRadius   = max(M0.w, 1.0e-5f);
        float  Haze          = saturate(M4.w);
        float3 HazeTint      = M4.xyz;

        float θSeparation = atan2(length(cross(ViewDirection, MoonDirection)), dot(ViewDirection, MoonDirection));

        // Atmospheric moonlit aureole + sunlit exospheric rim corona around hazy moons
        if (θSeparation < θMoonRadius * 3.8f)
        {
            float AureoleDist = max(0.0f, θSeparation - θMoonRadius) / max(θMoonRadius, 1.0e-4f);
            float Aureole     = (0.012f + 0.022f * Haze) * M3.y * M6.w * exp(-AureoleDist * (2.1f - 0.7f * Haze));
            ExoRadiance      += lerp(float3(0.76f, 0.84f, 1.00f), HazeTint, Haze) * Aureole;
        }

        float OuterLimb = θMoonRadius * (1.0f + 0.14f * max(Haze, 0.08f)) + 0.55f * PixelSpanRad;
        if (Haze > 1.0e-3f && θSeparation < OuterLimb)
        {
            float  RimRise   = smoothstep(θMoonRadius * 0.78f, θMoonRadius, θSeparation);
            float  RimDecay  = 1.0f - smoothstep(θMoonRadius, OuterLimb, θSeparation);
            float  RimMask   = RimRise * RimDecay * Haze;
            float3 TangDir   = SafeNormalize(ViewDirection - MoonDirection * dot(ViewDirection, MoonDirection));
            float3 ShellNorm = SafeNormalize(TangDir * 0.94f - MoonDirection * 0.34f);
            for (uint SunIdx = 0u; SunIdx < (M5.w > .5f ? 1u : SunCount); ++SunIdx)
            {
                uint   SunOffset       = SunSlotOffset(Ensemble, SunFirst + SunIdx);
                float3 SolarDirection  = M5.w > .5f ? M5.xyz : Ensemble[SunOffset + 0u].xyz;
                float3 SolarIrradiance = M5.w > .5f ? M6.xyz : Ensemble[SunOffset + 1u].xyz;
                float  ShellLit        = smoothstep(-0.28f, 0.45f, dot(ShellNorm, SolarDirection));
                ExoRadiance           += HazeTint * SolarIrradiance * (0.14f * M3.y * (0.18f + 0.82f * ShellLit) * RimMask);
            }
        }

        float EdgeInner = θMoonRadius * (0.985f - 0.105f * Haze) - 0.55f * PixelSpanRad;
        float EdgeOuter = θMoonRadius * (1.0f + 0.065f * Haze) + 0.55f * PixelSpanRad;
        float Coverage  = smoothstep(EdgeOuter, max(0.0f, EdgeInner), θSeparation);
        if (Coverage <= 0.0f)
        {
            continue;
        }

        float  ClampedSep = min(θSeparation, θMoonRadius * 0.9995f);
        float3 RadDir     = SafeNormalize(ViewDirection - MoonDirection * dot(ViewDirection, MoonDirection));
        float3 ClampedRay = SafeNormalize(MoonDirection * cos(ClampedSep) + RadDir * sin(ClampedSep));
        float  Distance   = 1.0f / sin(min(θMoonRadius, 1.5707f));
        float3 MoonPos    = MoonDirection * Distance;
        float3 OffsetC    = -MoonPos;
        float  HalfB      = dot(OffsetC, ClampedRay);
        float  Disc       = max(0.0f, HalfB * HalfB - (dot(OffsetC, OffsetC) - 1.0f));
        float3 Normal     = SafeNormalize(ClampedRay * (-HalfB - sqrt(Disc)) - MoonPos);

        float3 UpRef         = (abs(MoonDirection.y) < 0.94f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
        float3 UnrolledRight = SafeNormalize(cross(UpRef, MoonDirection));
        float3 UnrolledUp    = SafeNormalize(cross(MoonDirection, UnrolledRight));
        float  cosRoll       = cos(M3.z);
        float  sinRoll       = sin(M3.z);
        float3 MoonRight     = UnrolledRight * cosRoll + UnrolledUp * sinRoll;
        float3 MoonUp        = UnrolledUp * cosRoll - UnrolledRight * sinRoll;
        float3 MoonFront     = -MoonDirection;

        float  DiscX   = dot(Normal, MoonRight);
        float  DiscY   = dot(Normal, MoonUp);
        float  DiscZ   = dot(Normal, MoonFront);
        float  sinφ₀   = sin(M2.w);
        float  cosφ₀   = cos(M2.w);
        float  φLat    = asin(clamp(DiscZ * sinφ₀ + DiscY * cosφ₀, -1.0f, 1.0f));
        float  λLon    = M2.z + atan2(DiscX, DiscZ * cosφ₀ - DiscY * sinφ₀);
        float2 AtlasUv = float2(frac(λLon / kTwoπ + 0.5f), saturate(0.5f - φLat / π));

        uint   AtlasLayer   = min(uint(M3.x + 0.5f), 5u);
        float3 Albedo       = LunarAtlas.SampleLevel(float3(AtlasUv, float(AtlasLayer)), 0.0f).rgb * M1.xyz;
        float  μ            = max(1.0e-3f, dot(Normal, -ClampedRay));
        float  LimbOptical  = (1.0f - μ) * (1.0f - μ) * Haze;
        float  SurfaceAtten = 1.0f - 0.52f * LimbOptical;
        float  Wrap         = 0.30f * Haze;
        float3 LitSurface   = float3(0.0f);

        for (uint SunIdx = 0u; SunIdx < (M5.w > .5f ? 1u : SunCount); ++SunIdx)
        {
            uint   SunOffset       = SunSlotOffset(Ensemble, SunFirst + SunIdx);
            float3 SolarDirection  = M5.w > .5f ? M5.xyz : Ensemble[SunOffset + 0u].xyz;
            float3 SolarIrradiance = M5.w > .5f ? M6.xyz : Ensemble[SunOffset + 1u].xyz;
            float  μ₀              = max(0.0f, (dot(Normal, SolarDirection) + Wrap) / (1.0f + Wrap));
            if (μ₀ > 0.0f)
            {
                float LommelSeeliger  = (2.0f * μ₀) / (μ₀ + μ);
                float ψPhase          = acos(clamp(dot(SolarDirection, -ViewDirection), -1.0f, 1.0f));
                float OppositionSurge = 1.0f + 0.42f * exp(-ψPhase / 0.14f);
                float PhaseCurve      = exp(-0.95f * ψPhase) * OppositionSurge;
                LitSurface           += Albedo * SolarIrradiance *
                                        (M1.w * 1.45f * SurfaceAtten * LommelSeeliger * (0.38f + 0.62f * PhaseCurve));
                if (Haze > 1.0e-3f)
                {
                    LitSurface += HazeTint * SolarIrradiance * (0.16f * M3.y * LimbOptical * (0.25f + 0.75f * μ₀));
                }
            }
        }
        // Earthshine on the unlit hemisphere
        LitSurface += Albedo * float3(0.58f, 0.72f, 1.0f) * (M2.x * (0.4f + 0.6f * μ));
        ExoRadiance = ExoRadiance * (1.0f - Coverage) + LitSurface * Coverage;
    }

    // 6. Finite-pixel limb-darkened multi-sun discs (kept apart: the clouds attenuate them as ResolveSolarDiscTransmittance)
    float3 SolarDiscs = float3(0.0f);
    for (uint SunIdx = 0u; SunIdx < SunCount; ++SunIdx)
    {
        uint   SunOffset      = SunSlotOffset(Ensemble, SunFirst + SunIdx);
        float4 S0             = Ensemble[SunOffset + 0u];
        float4 S1             = Ensemble[SunOffset + 1u];
        float3 SolarDirection = S0.xyz;
        float  θSolarRadius   = max(S0.w, 1.0e-5f);
        float  θSeparation    = atan2(length(cross(ViewDirection, SolarDirection)), dot(ViewDirection, SolarDirection));
        float  HalfPixel      = 0.5f * PixelSpanRad;
        if (θSeparation < θSolarRadius + HalfPixel)
        {
            float Coverage = smoothstep(θSolarRadius + HalfPixel, max(0.0f, θSolarRadius - HalfPixel), θSeparation);
            float ρNorm    = min(θSeparation / θSolarRadius, 0.999f);
            float μ        = sqrt(max(0.0f, 1.0f - ρNorm * ρNorm));
            float Limb     = (1.0f - S1.w * (1.0f - μ)) / max(1.0f - S1.w / 3.0f, 1.0e-3f);
            SolarDiscs    += S1.xyz * (18.0f * Limb * Coverage);
        }
    }

    float3       TotalSky  = min((SkyScattering + (ExoRadiance + SolarDiscs) * SkyTrans) * GlassTint, float3(60000.0f));
    const float3 Weights   = float3(0.2126f, 0.7152f, 0.0722f);
    float        SkyLuma   = dot(TotalSky, Weights);
    float        DiscShare = SkyLuma > 1.0e-6f ? saturate(dot(SolarDiscs * SkyTrans * GlassTint, Weights) / SkyLuma) : 0.0f;
    HdrFrame[uint3(Tid.xy, 0u)] = float4(TotalSky, DiscShare);
}
