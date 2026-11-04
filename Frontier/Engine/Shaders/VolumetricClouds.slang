//==========================================================================================================================================
//                                                  VOLUMETRIC CLOUDS SLANG
//==========================================================================================================================================
// 1. TraceVolumetricCloudsMain: non-tiled 3D procedural global slabs + bounded local boxes with Beers-Powder multi-scattering.
// 2. ReconstructVolumetricCloudsMain: RealmId-aware & reversed-Z depth-aware bilateral spatial + temporal filter.

#include "CloudSynthesis.slang"

[[vk::push_constant]] PushDispatchHeader Push;

[[vk::binding(0, 0)]] StructuredBuffer<float4>                     Ensemble;
[[vk::binding(1, 0)]] Sampler2DArray<float4>                       TransLut;
[[vk::binding(2, 0)]] [format("rgba32f")] RWTexture2DArray<float4> VisibilityImage;
[[vk::binding(3, 0)]] [format("rgba16f")] RWTexture2DArray<float4> CloudWorkTarget; // Layer 0 = Raw, Layer 1 = Reconstructed
[[vk::binding(4, 0)]] [format("rgba16f")] RWTexture2DArray<float4> HdrFrameTarget;

[shader("compute")]
[numthreads(8, 8, 1)]
void TraceVolumetricCloudsMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }

    uint2 FullPixel = uint2(min((float2(Tid.xy) + 0.5f) * Push.Tuning.xy / Push.Tuning.zw, Push.Tuning.xy - 1.0f));
    float4 Vis1      = VisibilityImage[uint3(FullPixel, 1u)];
    float4 Vis2      = VisibilityImage[uint3(FullPixel, 2u)];
    float3 GlassTint = VisibilityImage[uint3(FullPixel, 3u)].rgb;
    uint   RealmId   = uint(Vis1.w + 0.5f);
    float3 RayOrigin = Vis1.xyz;
    float3 RayDir    = SafeNormalize(Vis2.xyz);
    float  HitDistM  = Vis2.w;
    float  MaxDistM  = (HitDistM > 0.0f) ? HitDistM : 45000.0f;

    uint   RealmOffset = RealmSlotOffset(Ensemble, RealmId);
    float4 R0          = Ensemble[RealmOffset + 0u];
    float4 R8          = Ensemble[RealmOffset + 8u];
    float4 R9          = Ensemble[RealmOffset + 9u];
    uint   SunFirst    = uint(R8.x + 0.5f);
    uint   SunCount    = uint(R8.y + 0.5f);
    uint   CloudTable  = uint(Ensemble[6].x + 0.5f);
    uint   CloudFirst  = uint(R9.z + 0.5f);
    uint   CloudCount  = uint(R9.w + 0.5f);

    if (CloudCount == 0u)
    {
        CloudWorkTarget[uint3(Tid.xy, 0u)] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    float  Jitter        = frac(52.9829189f * frac(0.06711056f * float(Tid.x) + 0.00583715f * float(Tid.y)));
    float  Transmittance = 1.0f;
    float3 InScatter     = float3(0.0f);

    float VortexNearT = 1.0e30f;
    float VortexFarT  = 0.0f;
    bool  HitVortex   = IntersectActiveVortexColumns(
        Ensemble, RealmId, RayOrigin, RayDir, MaxDistM, VortexNearT, VortexFarT);

    // March active tornado vortex column (-1) at tight sub-core step spacing, near bounded 3D cloud boxes (0), then planetary slabs (1)
    for (int DomainOrder = HitVortex ? -1 : 0; DomainOrder < 2; ++DomainOrder)
    {
        for (uint CloudIdx = 0u; CloudIdx < CloudCount; ++CloudIdx)
        {
            if (Transmittance < 0.01f)
            {
                break;
            }
            if (DomainOrder == -1 && CloudIdx > 0u)
            {
                break;
            }
            uint   CloudOffset = CloudTable + (CloudFirst + CloudIdx) * kCloudStride;
            float4 C0          = Ensemble[CloudOffset + 0u];
            float4 C1          = Ensemble[CloudOffset + 1u];
            float4 C2          = Ensemble[CloudOffset + 2u];
            float4 C3          = Ensemble[CloudOffset + 3u];
            float4 C4          = Ensemble[CloudOffset + 4u];
            float4 C5          = Ensemble[CloudOffset + 5u];
            bool   IsLocalBox  = (C2.w > 0.5f);
            if (DomainOrder >= 0 && ((DomainOrder == 0) != IsLocalBox))
            {
                continue;
            }

            float NearT = 1.0e30f;
            float FarT  = 0.0f;
            bool  MarchingVortexFunnel = false;
            if (DomainOrder == -1)
            {
                // Sub-cloud interval only (Pos.y <= C0.x) so DomainOrder == 1 marches the upper slab without any seam
                float SubCloudMinT = 0.0f;
                float SubCloudMaxT = MaxDistM;
                if (abs(RayDir.y) > 1.0e-4f)
                {
                    float TCloudBase = (C0.x - RayOrigin.y) / RayDir.y;
                    if (RayDir.y > 0.0f)
                    {
                        SubCloudMaxT = min(SubCloudMaxT, max(0.0f, TCloudBase));
                    }
                    else
                    {
                        SubCloudMinT = max(0.0f, TCloudBase);
                    }
                }
                else if (RayOrigin.y >= C0.x)
                {
                    SubCloudMaxT = 0.0f;
                }
                NearT                = max(VortexNearT, SubCloudMinT);
                FarT                 = min(min(MaxDistM, VortexFarT), SubCloudMaxT);
                MarchingVortexFunnel = true;
            }
            else if (IsLocalBox)
            {
                uint Shape = uint(Ensemble[CloudOffset + 6u].x + 0.5f);
                if (!IntersectVolumeBounds(RayOrigin, RayDir, C3.xyz, C4.xyz, Shape, MaxDistM, NearT, FarT)) continue;
            }
            else if (C0.y > 0.0f)
            {
                if (abs(RayDir.y) > 1.0e-3f)
                {
                    float Ta = (C0.x - RayOrigin.y) / RayDir.y;
                    float Tb = (C0.x + C0.y - RayOrigin.y) / RayDir.y;
                    if (max(Ta, Tb) > 0.0f)
                    {
                        NearT = max(0.0f, min(Ta, Tb));
                        FarT  = min(min(MaxDistM, 18000.0f), max(Ta, Tb));
                        FarT  = min(FarT, NearT + 5500.0f);
                    }
                }
                else if (RayOrigin.y >= C0.x && RayOrigin.y <= C0.x + C0.y)
                {
                    NearT = 0.0f;
                    FarT  = min(MaxDistM, 5500.0f);
                }
            }

            if (FarT <= NearT)
            {
                continue;
            }

            int   MarchSteps    = MarchingVortexFunnel ? max(24, int(C5.y)) : clamp(int(C5.y), 8, 192);
            float Δs            = (FarT - NearT) / float(MarchSteps);
            float ShadowStepRef = IsLocalBox ? 3.5f : (MarchingVortexFunnel ? 8.0f : 48.0f);
            float DistanceFade  = IsLocalBox ? 1.0f : saturate(1.0f - NearT / 18000.0f);

            for (int Step = 0; Step < MarchSteps; ++Step)
            {
                if (Transmittance < 0.01f)
                {
                    break;
                }
                float3 SamplePos   = RayOrigin + RayDir * (NearT + (float(Step) + Jitter) * Δs);
                float  σExtinction = EvaluateCloudExtinctionAtPoint(Ensemble, RealmId, CloudOffset, SamplePos) * DistanceFade;
                if (σExtinction <= 1.0e-6f)
                {
                    continue;
                }

                float Albedo         = C1.z;
                float 𝑔Forward       = C1.w;
                float 𝑔Backscatter   = C2.x;
                float PowderStrength = C2.z;

                float3 StepLuminance = float3(0.0f);
                for (uint SunIdx = 0u; SunIdx < SunCount; ++SunIdx)
                {
                    uint   SunOffset       = SunSlotOffset(Ensemble, SunFirst + SunIdx);
                    float3 SolarDirection  = Ensemble[SunOffset + 0u].xyz;
                    float3 SolarIrradiance = Ensemble[SunOffset + 1u].xyz;
                    if (SolarDirection.y <= -0.10f)
                    {
                        continue;
                    }

                    float τSolar = 0.0f;
                    for (int Sub = 0; Sub < min(int(C5.z), 12); ++Sub)
                    {
                        float  StepLength = ShadowStepRef * float(1 << Sub);
                        float3 ShadowPos  = SamplePos + SolarDirection * StepLength;
                        τSolar           += EvaluateCloudExtinctionAtPoint(Ensemble, RealmId, CloudOffset, ShadowPos) * StepLength;
                    }

                    // 📝 Lit from behind: the sharp forward lobe (C5.x of the energy) makes the thin parts of a cloud near the sun
                    //    glow, and the powder darkening of sunlit edges only applies looking away from the sun, where it is seen.
                    float  cosθ   = dot(RayDir, SolarDirection);
                    float  Phase0 = lerp(HenyeyGreensteinValue(cosθ, 𝑔Backscatter), HenyeyGreensteinValue(cosθ, 𝑔Forward), 0.72f);
                    Phase0        = lerp(Phase0, HenyeyGreensteinValue(cosθ, kSilverLiningG), saturate(C5.x));
                    float  Powder = 1.0f - 0.55f * PowderStrength * exp(-2.0f * τSolar) * saturate(0.5f - 0.5f * cosθ);
                    float3 AtmoTr = SampleTransmittanceLutArray(
                        TransLut, RealmId, R0.x, R0.y, R0.x + max(SamplePos.y * 1.0e-3f, 1.0e-3f), SolarDirection.y);

                    float PrimaryOctave   = exp(-τSolar) * Phase0 * Powder;
                    float SecondaryOctave = 0.78f * exp(-0.26f * τSolar) * lerp(kInv4π, Phase0, 0.35f);
                    StepLuminance        += SolarIrradiance * AtmoTr * Albedo * (PrimaryOctave + SecondaryOctave);
                }
                StepLuminance += float3(0.068f, 0.092f, 0.132f) * Albedo;

                float StepTr   = exp(-σExtinction * Δs);
                float Weight   = (1.0f - StepTr);
                InScatter     += Transmittance * StepLuminance * Weight;
                Transmittance *= StepTr;
            }
        }
    }

    CloudWorkTarget[uint3(Tid.xy, 0u)] = float4(InScatter * GlassTint, saturate(Transmittance));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ReconstructVolumetricCloudsMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }

    uint CenterRealm = uint(VisibilityImage[uint3(Tid.xy, 1u)].w + 0.5f);
    float CenterRevZ = VisibilityImage[uint3(Tid.xy, 0u)].w;
    int2 TraceMaximum = int2(Push.Tuning.zw) - 1;
    float2 TracePoint = (float2(Tid.xy) + 0.5f) * Push.Tuning.zw / Push.Tuning.xy - 0.5f;
    int2 Lower = int2(floor(TracePoint));
    float2 Fraction = frac(TracePoint);
    int2 Nearest = clamp(int2(floor(TracePoint + 0.5f)), int2(0), TraceMaximum);
    float4 CenterCloud = CloudWorkTarget[uint3(uint2(Nearest), 0u)];
    bool FullResolution = all(Push.Tuning.xy == Push.Tuning.zw);
    float4 Accum = float4(0.0f);
    float WeightSum = 0.0f;
    for (int Dy = FullResolution ? -1 : 0; Dy <= 1; ++Dy)
    {
        for (int Dx = FullResolution ? -1 : 0; Dx <= 1; ++Dx)
        {
            int2 TracePixel = clamp((FullResolution ? int2(Tid.xy) : Lower) + int2(Dx, Dy), int2(0), TraceMaximum);
            uint2 ScenePixel = uint2(min((float2(TracePixel) + 0.5f) * Push.Tuning.xy / Push.Tuning.zw, Push.Tuning.xy - 1.0f));
            uint NeighborRealm = uint(VisibilityImage[uint3(ScenePixel, 1u)].w + 0.5f);
            if (NeighborRealm != CenterRealm) continue;
            float NeighborRevZ = VisibilityImage[uint3(ScenePixel, 0u)].w;
            float DepthWeight = exp(-abs(NeighborRevZ - CenterRevZ) * 120.0f);
            float Spatial = FullResolution ? ((Dx == 0 && Dy == 0) ? 1.0f : ((Dx == 0 || Dy == 0) ? 0.6f : 0.36f)) :
                (Dx == 0 ? 1.0f - Fraction.x : Fraction.x) * (Dy == 0 ? 1.0f - Fraction.y : Fraction.y);
            float Weight = Spatial * DepthWeight;
            Accum += CloudWorkTarget[uint3(uint2(TracePixel), 0u)] * Weight;
            WeightSum += Weight;
        }
    }
    float4 Filtered = WeightSum > 1.0e-8f ? Accum / WeightSum : float4(0.0f, 0.0f, 0.0f, 1.0f);

    if (Push.Counts.w != 0u)
    {
        float4 PrevHistory = CloudWorkTarget[uint3(Tid.xy, 1u)];
        float4 LowerClamp  = min(CenterCloud, Filtered) * 0.85f;
        float4 UpperClamp  = max(CenterCloud, Filtered) * 1.15f + float4(0.01f);
        PrevHistory        = clamp(PrevHistory, LowerClamp, UpperClamp);
        Filtered           = lerp(Filtered, PrevHistory, 0.80f);
    }

    CloudWorkTarget[uint3(Tid.xy, 1u)] = Filtered;

    // 📝 The dome's alpha is the solar discs' share of the pixel: that share fades as ResolveSolarDiscTransmittance, the sky as
    //    the cloud's transmittance.
    float4 InitialHdr                  = HdrFrameTarget[uint3(Tid.xy, 0u)];
    float  DiscShare                   = saturate(InitialHdr.a);
    float  Attenuation                 = lerp(Filtered.w, ResolveSolarDiscTransmittance(Filtered.w), DiscShare);
    float3 Blended                     = InitialHdr.rgb * Attenuation + Filtered.rgb;
    HdrFrameTarget[uint3(Tid.xy, 0u)]  = float4(Blended, 1.0f);
}
