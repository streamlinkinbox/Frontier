//==========================================================================================================================================
//                                                VISIBILITY PROJECTION SLANG
//==========================================================================================================================================
// Routes primary camera rays across PortalApertures into (RealmId, RayOrigin, RayDirection), evaluates reversed-Z visibility,
// dielectric glass transmission masks, and shades opaque surfaces using wide CloudShadowLut + analytic cloud shadow fallback + moons.

#include "CloudSynthesis.slang"

[[vk::push_constant]] PushDispatchHeader Push;

[[vk::binding(0, 0)]] StructuredBuffer<float4>                     Ensemble;
[[vk::binding(1, 0)]] Sampler2DArray<float4>                       TransLut;
[[vk::binding(2, 0)]] Sampler2DArray<float4>                       CloudShadowLut;
[[vk::binding(3, 0)]] [format("rgba32f")] RWTexture2DArray<float4> VisibilityOut;

bool RaySphereNearest(float3 Origin, float3 Direction, float3 Center, float Radius, out float OutT)
{
    float3 Offset = Origin - Center;
    float  HalfB  = dot(Offset, Direction);
    float  TermC  = dot(Offset, Offset) - Radius * Radius;
    float  Disc   = HalfB * HalfB - TermC;
    OutT          = 0.0f;
    if (Disc < 0.0f)
    {
        return false;
    }
    float Root = sqrt(Disc);
    float T0   = -HalfB - Root;
    float T1   = -HalfB + Root;
    if (T0 > 1.0e-3f)
    {
        OutT = T0;
        return true;
    }
    if (T1 > 1.0e-3f)
    {
        OutT = T1;
        return true;
    }
    return false;
}

float NearestRealmOpaqueHit(StructuredBuffer<float4> Ens,
                            uint                     RealmId,
                            float3                   Origin,
                            float3                   Direction,
                            out float3               OutNormal,
                            out float3               OutAlbedo)
{
    uint   RealmOffset  = RealmSlotOffset(Ens, RealmId);
    float  GroundY      = Ens[RealmOffset + 4u].y;
    float3 GroundAlbedo = float3(Ens[RealmOffset + 4u].z, Ens[RealmOffset + 4u].w, Ens[RealmOffset + 5u].x);

    float BestT = 1.0e30f;
    OutNormal   = float3(0.0f, 1.0f, 0.0f);
    OutAlbedo   = GroundAlbedo;

    if (Direction.y < -1.0e-5f)
    {
        float Tg = (GroundY - Origin.y) / Direction.y;
        if (Tg > 1.0e-3f)
        {
            BestT         = Tg;
            float3 HitPos = Origin + Direction * Tg;
            float2 Cell   = floor(HitPos.xz * 0.25f);
            float  Parity = fmod(abs(Cell.x + Cell.y), 2.0f);
            OutAlbedo     = GroundAlbedo * lerp(0.88f, 1.12f, Parity);
        }
    }

    uint SphereTable = uint(Ens[7].y + 0.5f);
    uint SphereFirst = uint(Ens[RealmOffset + 11u].z + 0.5f);
    uint SphereCount = uint(Ens[RealmOffset + 11u].w + 0.5f);

    for (uint Idx = 0u; Idx < SphereCount; ++Idx)
    {
        uint   SphereOffset = SphereTable + (SphereFirst + Idx) * kSphereStride;
        float4 S0           = Ens[SphereOffset + 0u];
        float4 S1           = Ens[SphereOffset + 1u];
        float  Ts;
        if (RaySphereNearest(Origin, Direction, S0.xyz, S0.w, Ts) && Ts < BestT)
        {
            BestT     = Ts;
            OutNormal = SafeNormalize((Origin + Direction * Ts) - S0.xyz);
            OutAlbedo = S1.xyz;
        }
    }
    return BestT;
}

bool IsShadowedBySpheres(StructuredBuffer<float4> Ens, uint RealmId, float3 HitPos, float3 LightDirection)
{
    uint RealmOffset = RealmSlotOffset(Ens, RealmId);
    uint SphereTable = uint(Ens[7].y + 0.5f);
    uint SphereFirst = uint(Ens[RealmOffset + 11u].z + 0.5f);
    uint SphereCount = uint(Ens[RealmOffset + 11u].w + 0.5f);
    for (uint Idx = 0u; Idx < SphereCount; ++Idx)
    {
        uint   SphereOffset = SphereTable + (SphereFirst + Idx) * kSphereStride;
        float4 S0           = Ens[SphereOffset + 0u];
        float  Ts;
        if (RaySphereNearest(HitPos + LightDirection * 0.02f, LightDirection, S0.xyz, S0.w, Ts))
        {
            return true;
        }
    }
    return false;
}

float3 EvaluateGlassTransmission(StructuredBuffer<float4> Ens, uint RealmId, float3 Origin, float3 Direction, float MaxT)
{
    uint   RealmOffset = RealmSlotOffset(Ens, RealmId);
    uint   GlassTable  = uint(Ens[7].z + 0.5f);
    uint   GlassFirst  = uint(Ens[RealmOffset + 12u].x + 0.5f);
    uint   GlassCount  = uint(Ens[RealmOffset + 12u].y + 0.5f);
    float3 Tint        = float3(1.0f, 1.0f, 1.0f);

    for (uint Idx = 0u; Idx < GlassCount; ++Idx)
    {
        uint   GlassOffset = GlassTable + (GlassFirst + Idx) * kGlassStride;
        float4 G0          = Ens[GlassOffset + 0u];
        float4 G1          = Ens[GlassOffset + 1u];
        float4 G2          = Ens[GlassOffset + 2u];
        float  Denom       = dot(Direction, G1.xyz);
        if (abs(Denom) <= 1.0e-5f)
        {
            continue;
        }
        float HitT = dot(G0.xyz - Origin, G1.xyz) / Denom;
        if (HitT <= 1.0e-3f || HitT >= MaxT)
        {
            continue;
        }
        float3 HitPt    = Origin + Direction * HitT;
        float3 RelPt    = HitPt - G0.xyz;
        float3 UpRef    = (abs(G1.y) < 0.95f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
        float3 PaneRight = SafeNormalize(cross(UpRef, G1.xyz));
        float3 PaneUp    = SafeNormalize(cross(G1.xyz, PaneRight));
        if (abs(dot(RelPt, PaneRight)) <= G0.w && abs(dot(RelPt, PaneUp)) <= G1.w)
        {
            float cosθInc = abs(Denom);
            float Fresnel = G2.w + (1.0f - G2.w) * pow(1.0f - cosθInc, 5.0f);
            Tint         *= G2.xyz * (1.0f - Fresnel);
        }
    }
    return Tint;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void RasterizeVisibilityProjectionMain(uint3 Tid : SV_DispatchThreadID)
{
    if (Tid.x >= Push.Counts.x || Tid.y >= Push.Counts.y)
    {
        return;
    }

    uint   RealmCount    = uint(Ensemble[0].x + 0.5f);
    uint   PortalCount   = uint(Ensemble[0].y + 0.5f);
    uint   StartRealm    = min(uint(Ensemble[0].w + 0.5f), max(RealmCount, 1u) - 1u);
    float3 CameraOrigin  = Ensemble[1].xyz;
    float2 ScreenUv      = (float2(Tid.xy) + 0.5f) / float2(Push.Counts.xy);
    float3 CameraDir     = CameraRayDirection(Ensemble, ScreenUv);

    float3 SolidNormal, SolidAlbedo;
    float  PrimarySolidT = NearestRealmOpaqueHit(Ensemble, StartRealm, CameraOrigin, CameraDir, SolidNormal, SolidAlbedo);

    uint   ActiveRealm   = StartRealm;
    int    ActivePortal  = -1;
    bool   HitFrame      = false;
    float3 FrameAlbedo   = float3(0.0f);
    float  BestPortalT   = PrimarySolidT;
    float3 RoutedOrigin  = CameraOrigin;
    float3 RoutedDir     = CameraDir;

    uint PortalTable = uint(Ensemble[7].w + 0.5f);
    for (uint PortalIdx = 0u; PortalIdx < PortalCount; ++PortalIdx)
    {
        uint   PortalOffset = PortalTable + PortalIdx * kPortalStride;
        float4 P0           = Ensemble[PortalOffset + 0u];
        uint   SrcRealm     = uint(P0.x + 0.5f);
        uint   DstRealm     = uint(P0.y + 0.5f);
        if (SrcRealm != StartRealm || DstRealm >= RealmCount)
        {
            continue;
        }
        float4 P1    = Ensemble[PortalOffset + 1u];
        float4 P2    = Ensemble[PortalOffset + 2u];
        float4 P3    = Ensemble[PortalOffset + 3u];
        float4 P4    = Ensemble[PortalOffset + 4u];
        float  Denom = dot(CameraDir, P2.xyz);
        if (abs(Denom) <= 1.0e-5f)
        {
            continue;
        }
        float HitT = dot(P1.xyz - CameraOrigin, P2.xyz) / Denom;
        if (HitT <= 1.0e-3f || HitT >= BestPortalT)
        {
            continue;
        }
        float3 RelPt  = (CameraOrigin + CameraDir * HitT) - P1.xyz;
        float  LocalX = dot(RelPt, P3.xyz);
        float  LocalY = dot(RelPt, P4.xyz);
        float  OuterW = P0.z + P1.w;
        float  OuterH = P0.w + P1.w;
        if (abs(LocalX) > OuterW || abs(LocalY) > OuterH)
        {
            continue;
        }

        BestPortalT  = HitT;
        ActivePortal = int(PortalIdx);
        if (abs(LocalX) > P0.z || abs(LocalY) > P0.w)
        {
            ActiveRealm  = StartRealm;
            HitFrame     = true;
            FrameAlbedo  = float3(P2.w, P3.w, P4.w);
            RoutedOrigin = CameraOrigin;
            RoutedDir    = CameraDir;
        }
        else
        {
            float3 TargetOrigin = Ensemble[PortalOffset + 5u].xyz;
            float3 TargetRight  = Ensemble[PortalOffset + 6u].xyz;
            float3 TargetUp     = Ensemble[PortalOffset + 7u].xyz;
            float3 TargetFwd    = Ensemble[PortalOffset + 8u].xyz;
            float3 InwardNormal = -P2.xyz;

            ActiveRealm  = DstRealm;
            HitFrame     = false;
            RoutedOrigin = TargetOrigin + TargetRight * LocalX + TargetUp * LocalY;
            RoutedDir    = SafeNormalize(
                TargetRight * dot(CameraDir, P3.xyz) +
                TargetUp * dot(CameraDir, P4.xyz) +
                TargetFwd * dot(CameraDir, InwardNormal));
        }
    }

    float  SurfaceT      = PrimarySolidT;
    float3 SurfaceNormal = SolidNormal;
    float3 SurfaceAlbedo = SolidAlbedo;

    if (HitFrame)
    {
        SurfaceT      = BestPortalT;
        SurfaceNormal = -SafeNormalize(Ensemble[PortalTable + uint(ActivePortal) * kPortalStride + 2u].xyz);
        SurfaceAlbedo = FrameAlbedo;
    }
    else if (ActivePortal >= 0)
    {
        SurfaceT = NearestRealmOpaqueHit(Ensemble, ActiveRealm, RoutedOrigin, RoutedDir, SurfaceNormal, SurfaceAlbedo);
    }

    float3 GlassTint = EvaluateGlassTransmission(Ensemble, StartRealm, CameraOrigin, CameraDir, min(BestPortalT, PrimarySolidT));
    if (!HitFrame && ActivePortal >= 0)
    {
        GlassTint *= EvaluateGlassTransmission(Ensemble, ActiveRealm, RoutedOrigin, RoutedDir, SurfaceT);
    }

    float  ReversedZ       = 0.0f;
    float  HitDistanceM    = -1.0f;
    float3 SurfaceRadiance = float3(0.0f);

    if (SurfaceT < 1.0e25f)
    {
        float TotalCameraDist = (ActivePortal >= 0 && !HitFrame) ? (BestPortalT + SurfaceT) : SurfaceT;
        ReversedZ             = clamp(0.1f / max(TotalCameraDist, 0.1f), 1.0e-6f, 1.0f);
        HitDistanceM          = SurfaceT;

        float3 HitPos      = RoutedOrigin + RoutedDir * SurfaceT;
        uint   RealmOffset = RealmSlotOffset(Ensemble, ActiveRealm);
        float4 R0          = Ensemble[RealmOffset + 0u];
        float4 R8          = Ensemble[RealmOffset + 8u];

        uint SunFirst  = uint(R8.x + 0.5f);
        uint SunCount  = uint(R8.y + 0.5f);
        uint MoonFirst = uint(R8.z + 0.5f);
        uint MoonCount = uint(R8.w + 0.5f);

        float3 Irradiance = float3(0.0f);
        for (uint SunIdx = 0u; SunIdx < SunCount; ++SunIdx)
        {
            uint   SunOffset       = SunSlotOffset(Ensemble, SunFirst + SunIdx);
            float3 SolarDirection  = Ensemble[SunOffset + 0u].xyz;
            float3 SolarIrradiance = Ensemble[SunOffset + 1u].xyz;
            float  μIncidence      = max(0.0f, dot(SurfaceNormal, SolarDirection));

            float3 SunTrans = (SolarDirection.y > -0.05f)
                                  ? SampleTransmittanceLutArray(
                                        TransLut, ActiveRealm, R0.x, R0.y, R0.x + max(R0.z, 1.0e-3f), SolarDirection.y)
                                  : float3(0.0f);

            float CloudTr = SampleOrTraceCloudTransmittance(
                Ensemble, CloudShadowLut, ActiveRealm, SunIdx, HitPos, SolarDirection);
            float SolidVis = IsShadowedBySpheres(Ensemble, ActiveRealm, HitPos, SolarDirection) ? 0.0f : 1.0f;

            Irradiance += SolarIrradiance * SunTrans * (μIncidence * CloudTr * SolidVis);
            float Hemisphere = 0.5f + 0.5f * SurfaceNormal.y;
            Irradiance += SolarIrradiance * float3(0.045f, 0.068f, 0.110f) * saturate(SolarDirection.y + 0.18f) * Hemisphere;
        }

        // Nocturnal moonlit surface illumination + ambient starlight floor
        uint MoonTable = uint(Ensemble[5].z + 0.5f);
        for (uint MoonIdx = 0u; MoonIdx < MoonCount; ++MoonIdx)
        {
            uint   MoonOffset    = MoonTable + (MoonFirst + MoonIdx) * kMoonStride;
            float3 MoonDirection = Ensemble[MoonOffset + 0u].xyz;
            float3 MoonTint      = Ensemble[MoonOffset + 1u].xyz;
            float  MoonGain      = Ensemble[MoonOffset + 3u].y;
            float  μMoon         = max(0.0f, dot(SurfaceNormal, MoonDirection));
            Irradiance += MoonTint * ((0.018f * μMoon + 0.005f * (0.5f + 0.5f * SurfaceNormal.y)) * MoonGain);
        }
        Irradiance += float3(0.0025f, 0.0032f, 0.0048f) * (0.5f + 0.5f * SurfaceNormal.y);

        SurfaceRadiance = (SurfaceAlbedo / π) * Irradiance * GlassTint;
    }

    VisibilityOut[uint3(Tid.xy, 0u)] = float4(SurfaceRadiance, ReversedZ);
    VisibilityOut[uint3(Tid.xy, 1u)] = float4(RoutedOrigin, float(ActiveRealm));
    VisibilityOut[uint3(Tid.xy, 2u)] = float4(RoutedDir, HitDistanceM);
    VisibilityOut[uint3(Tid.xy, 3u)] = float4(GlassTint, float(ActivePortal + 1));
}
