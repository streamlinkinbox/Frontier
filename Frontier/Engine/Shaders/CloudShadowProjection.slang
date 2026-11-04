//==========================================================================================================================================
//                                              CLOUD SHADOW PROJECTION SLANG
//==========================================================================================================================================
// Per-realm 256x256 RGBA16F orthographic optical-depth & solar-transmittance projection across ±CloudShadowHalfSpanM.

#include "CloudSynthesis.slang"

[[vk::push_constant]] PushDispatchHeader Push;

[[vk::binding(0, 0)]] StructuredBuffer<float4>                     Ensemble;
[[vk::binding(1, 0)]] Sampler2DArray<float4>                       TransLut;
[[vk::binding(2, 0)]] [format("rgba32f")] RWTexture2DArray<float4> VisibilityUnused;
[[vk::binding(3, 0)]] [format("rgba16f")] RWTexture2DArray<float4> CloudShadowTarget;
[[vk::binding(4, 0)]] [format("rgba16f")] RWTexture2DArray<float4> SecondaryUnused;

[shader("compute")]
[numthreads(8, 8, 1)]
void ProjectCloudShadowMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }
    uint   RealmId     = Push.Counts.z;
    uint   RealmOffset = RealmSlotOffset(Ensemble, RealmId);
    float4 R8          = Ensemble[RealmOffset + 8u];
    float4 R9          = Ensemble[RealmOffset + 9u];
    float  HalfSpanM   = max(Ensemble[RealmOffset + 13u].z, 1.0f);

    uint   SunFirst   = uint(R8.x + 0.5f);
    uint   SunCount   = uint(R8.y + 0.5f);
    uint   CloudTable = uint(Ensemble[6].x + 0.5f);
    uint   CloudFirst = uint(R9.z + 0.5f);
    uint   CloudCount = uint(R9.w + 0.5f);

    if (CloudCount == 0u || SunCount == 0u)
    {
        CloudShadowTarget[uint3(Tid.xy, RealmId)] = float4(0.0f, 1.0f, 0.0f, 1.0f);
        return;
    }

    float3 PrimarySunDirection = Ensemble[SunSlotOffset(Ensemble, SunFirst)].xyz;
    if (PrimarySunDirection.y <= 0.02f)
    {
        CloudShadowTarget[uint3(Tid.xy, RealmId)] = float4(0.0f, 1.0f, 0.0f, 1.0f);
        return;
    }

    float2 Uv          = (float2(Tid.xy) + 0.5f) / float2(Push.Counts.xy);
    float3 GroundPoint = float3((Uv.x * 2.0f - 1.0f) * HalfSpanM,
                                Ensemble[RealmOffset + 4u].y,
                                (Uv.y * 2.0f - 1.0f) * HalfSpanM);

    float VortexNearT = 1.0e30f;
    float VortexFarT  = 0.0f;
    bool  HitVortex   = IntersectActiveVortexColumns(
        Ensemble, RealmId, GroundPoint, PrimarySunDirection, 18000.0f, VortexNearT, VortexFarT);

    float τAccum         = 0.0f;
    float WeightedHeight = 0.0f;
    for (uint CloudIdx = 0u; CloudIdx < CloudCount; ++CloudIdx)
    {
        uint   CloudOffset = CloudTable + (CloudFirst + CloudIdx) * kCloudStride;
        float4 C0          = Ensemble[CloudOffset + 0u];
        float4 C2          = Ensemble[CloudOffset + 2u];
        float4 C3          = Ensemble[CloudOffset + 3u];
        float4 C4          = Ensemble[CloudOffset + 4u];

        float LowerY = (C2.w > 0.5f) ? (C3.y - C4.y) : C0.x;
        float UpperY = (C2.w > 0.5f) ? (C3.y + C4.y) : (C0.x + C0.y);
        float NearT  = max(0.0f, (LowerY - GroundPoint.y) / PrimarySunDirection.y);
        float FarT   = max(NearT, (UpperY - GroundPoint.y) / PrimarySunDirection.y);
        if (C2.w <= 0.5f && HitVortex && CloudIdx == 0u)
        {
            NearT = min(NearT, VortexNearT);
            FarT  = max(FarT, VortexFarT);
        }
        if (FarT <= NearT)
        {
            continue;
        }
        const int kSteps = 12;
        float     Δs     = (FarT - NearT) / float(kSteps);
        for (int Step = 0; Step < kSteps; ++Step)
        {
            float3 SamplePos   = GroundPoint + PrimarySunDirection * (NearT + (float(Step) + 0.5f) * Δs);
            float  σExtinction = EvaluateCloudExtinctionAtPoint(Ensemble, RealmId, CloudOffset, SamplePos);
            if (σExtinction > 0.0f)
            {
                float Δτ = σExtinction * Δs;
                τAccum         += Δτ;
                WeightedHeight += Δτ * SamplePos.y;
            }
        }
    }

    float MeanHeight    = (τAccum > 1.0e-5f) ? (WeightedHeight / τAccum) : 0.0f;
    float Transmittance = clamp(0.94f * exp(-τAccum) + 0.06f * exp(-0.25f * τAccum), 0.0f, 1.0f);
    CloudShadowTarget[uint3(Tid.xy, RealmId)] = float4(τAccum, Transmittance, MeanHeight, 1.0f);
}
