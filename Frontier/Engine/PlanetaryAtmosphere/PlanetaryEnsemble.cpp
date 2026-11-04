/*==========================================================================================================================================
                                             PLANETARY ENSEMBLE DEFINITION
==========================================================================================================================================*/
// 🧩 Multi-realm ray portal routing, float4 payload packing, and end-to-end GPU submission sequence.

#include "Engine/PlanetaryAtmosphere/PlanetaryEnsemble.h"

#include <utility>

#include <algorithm>
#include <cmath>

namespace Frontier
{
namespace
{

inline float Dot3(std::array<float, 3> A, std::array<float, 3> B) noexcept
{
    return A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
}

inline std::array<float, 3> Normalize3(std::array<float, 3> Vector) noexcept
{
    const float Len2 = Dot3(Vector, Vector);
    if (Len2 <= 1.0e-20f)
    {
        return {0.0f, 1.0f, 0.0f};
    }
    const float Inv = 1.0f / std::sqrt(Len2);
    return {Vector[0] * Inv, Vector[1] * Inv, Vector[2] * Inv};
}

inline std::array<float, 3> FromVec3(Vec3 Vector) noexcept
{
    return {Vector.X, Vector.Y, Vector.Z};
}

inline std::array<float, 3> FromSpatial(SpatialVector Vector) noexcept
{
    return {static_cast<float>(Vector.X), static_cast<float>(Vector.Y), static_cast<float>(Vector.Z)};
}

bool RaySphereNearestHit(std::array<float, 3> Origin,
                         std::array<float, 3> Dir,
                         std::array<float, 3> Center,
                         float                Radius,
                         float&               OutT) noexcept
{
    const std::array<float, 3> Oc  = {Origin[0] - Center[0], Origin[1] - Center[1], Origin[2] - Center[2]};
    const float                B   = Dot3(Oc, Dir);
    const float                C0  = Dot3(Oc, Oc) - Radius * Radius;
    const float                Det = B * B - C0;
    if (Det < 0.0f)
    {
        return false;
    }
    const float SqrtD = std::sqrt(Det);
    const float T0    = -B - SqrtD;
    const float T1    = -B + SqrtD;
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

float NearestRealmSolidDistance(const PlanetaryRealm& Realm, std::array<float, 3> Origin, std::array<float, 3> Dir) noexcept
{
    float BestT = 1.0e30f;
    if (Dir[1] < -1.0e-5f)
    {
        const float Tg = (Realm.GroundPlaneY - Origin[1]) / Dir[1];
        if (Tg > 1.0e-3f)
        {
            BestT = std::min(BestT, Tg);
        }
    }
    for (const RealmSphereObstacle& Sphere : Realm.SolidOccluders)
    {
        float Ts = 0.0f;
        if (RaySphereNearestHit(Origin, Dir, Sphere.CenterMeters, Sphere.RadiusMeters, Ts))
        {
            BestT = std::min(BestT, Ts);
        }
    }
    return BestT;
}

inline uint8_t LinearToSrgbByte(float Linear) noexcept
{
    const float Clamped = std::clamp(Linear, 0.0f, 1.0f);
    const float Encoded = (Clamped <= 0.0031308f) ? (12.92f * Clamped) : (1.055f * std::pow(Clamped, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::lround(Encoded * 255.0f), 0l, 255l));
}

inline float AcesFilmicChannel(float Value) noexcept
{
    const float Pos = std::max(0.0f, Value);
    return std::clamp((Pos * (2.51f * Pos + 0.03f)) / (Pos * (2.43f * Pos + 0.59f) + 0.14f), 0.0f, 1.0f);
}

} // namespace

RoutedRealmRay RouteRayAcrossPortals(const PlanetaryRealmEnsemble& Ensemble,
                                     uint32_t                      StartRealmIndex,
                                     std::array<float, 3>          RayOriginMeters,
                                     std::array<float, 3>          RayDirectionUnit) noexcept
{
    RoutedRealmRay Out{};
    if (Ensemble.Realms.empty())
    {
        Out.RayOriginMeters  = RayOriginMeters;
        Out.RayDirectionUnit = Normalize3(RayDirectionUnit);
        return Out;
    }

    const uint32_t StartIdx = std::min<uint32_t>(StartRealmIndex, static_cast<uint32_t>(Ensemble.Realms.size() - 1u));
    const auto     Dir      = Normalize3(RayDirectionUnit);
    Out.RealmIndex          = StartIdx;
    Out.RayOriginMeters     = RayOriginMeters;
    Out.RayDirectionUnit    = Dir;

    const float NearestSolidT = NearestRealmSolidDistance(Ensemble.Realms[StartIdx], RayOriginMeters, Dir);
    float       BestPortalT   = NearestSolidT;

    for (size_t PortalIdx = 0u; PortalIdx < Ensemble.Portals.size(); ++PortalIdx)
    {
        const PortalAperture& Portal = Ensemble.Portals[PortalIdx];
        if (Portal.SourceRealmIndex != StartIdx || Portal.TargetRealmIndex >= Ensemble.Realms.size())
        {
            continue;
        }
        const float Denom = Dot3(Dir, Portal.NormalUnit);
        if (std::abs(Denom) <= 1.0e-5f)
        {
            continue;
        }
        const std::array<float, 3> Delta = {
            Portal.CenterMeters[0] - RayOriginMeters[0],
            Portal.CenterMeters[1] - RayOriginMeters[1],
            Portal.CenterMeters[2] - RayOriginMeters[2],
        };
        const float HitT = Dot3(Delta, Portal.NormalUnit) / Denom;
        if (HitT <= 1.0e-3f || HitT >= BestPortalT)
        {
            continue;
        }

        const std::array<float, 3> Hit = {
            RayOriginMeters[0] + Dir[0] * HitT,
            RayOriginMeters[1] + Dir[1] * HitT,
            RayOriginMeters[2] + Dir[2] * HitT,
        };
        const std::array<float, 3> Rel = {
            Hit[0] - Portal.CenterMeters[0],
            Hit[1] - Portal.CenterMeters[1],
            Hit[2] - Portal.CenterMeters[2],
        };
        const float LocalX = Dot3(Rel, Portal.RightUnit);
        const float LocalY = Dot3(Rel, Portal.UpUnit);

        const float OuterW = Portal.HalfWidthMeters + Portal.FrameWidthMeters;
        const float OuterH = Portal.HalfHeightMeters + Portal.FrameWidthMeters;
        if (std::abs(LocalX) > OuterW || std::abs(LocalY) > OuterH)
        {
            continue;
        }

        BestPortalT         = HitT;
        Out.PortalHitMeters = HitT;

        if (std::abs(LocalX) > Portal.HalfWidthMeters || std::abs(LocalY) > Portal.HalfHeightMeters)
        {
            Out.RealmIndex       = StartIdx;
            Out.PortalIndex      = static_cast<int32_t>(PortalIdx);
            Out.HitPortalFrame   = true;
            Out.FrameAlbedoRgb   = Portal.FrameAlbedoRgb;
            Out.RayOriginMeters  = RayOriginMeters;
            Out.RayDirectionUnit = Dir;
        }
        else
        {
            const std::array<float, 3> InwardNormal = {-Portal.NormalUnit[0], -Portal.NormalUnit[1], -Portal.NormalUnit[2]};
            const float                DirRight     = Dot3(Dir, Portal.RightUnit);
            const float                DirUp        = Dot3(Dir, Portal.UpUnit);
            const float                DirForward   = Dot3(Dir, InwardNormal);

            Out.RealmIndex     = Portal.TargetRealmIndex;
            Out.PortalIndex    = static_cast<int32_t>(PortalIdx);
            Out.HitPortalFrame = false;
            Out.RayOriginMeters = {
                Portal.TargetOriginMeters[0] + Portal.TargetRightUnit[0] * LocalX + Portal.TargetUpUnit[0] * LocalY,
                Portal.TargetOriginMeters[1] + Portal.TargetRightUnit[1] * LocalX + Portal.TargetUpUnit[1] * LocalY,
                Portal.TargetOriginMeters[2] + Portal.TargetRightUnit[2] * LocalX + Portal.TargetUpUnit[2] * LocalY,
            };
            Out.RayDirectionUnit = Normalize3({
                Portal.TargetRightUnit[0] * DirRight + Portal.TargetUpUnit[0] * DirUp + Portal.TargetForwardUnit[0] * DirForward,
                Portal.TargetRightUnit[1] * DirRight + Portal.TargetUpUnit[1] * DirUp + Portal.TargetForwardUnit[1] * DirForward,
                Portal.TargetRightUnit[2] * DirRight + Portal.TargetUpUnit[2] * DirUp + Portal.TargetForwardUnit[2] * DirForward,
            });
        }
    }

    return Out;
}

bool ProjectDirectionToScreenUv(const PlanetaryCameraView& View,
                                std::array<float, 3>       WorldDirectionUnit,
                                std::array<float, 2>&      OutUv) noexcept
{
    const auto  Dir  = Normalize3(WorldDirectionUnit);
    const float CamZ = Dot3(Dir, View.ForwardUnit);
    if (CamZ <= 1.0e-4f)
    {
        return false;
    }
    const float TanHalfY = std::tan(0.5f * View.VerticalFovRad);
    const float Aspect   = static_cast<float>(std::max(1u, View.ViewportWidth)) /
                           static_cast<float>(std::max(1u, View.ViewportHeight));
    const float CamX     = Dot3(Dir, View.RightUnit);
    const float CamY     = Dot3(Dir, View.UpUnit);
    const float NdcX     = (CamX / CamZ) / std::max(Aspect * TanHalfY, 1.0e-5f);
    const float NdcY     = (CamY / CamZ) / std::max(TanHalfY, 1.0e-5f);
    OutUv[0]             = 0.5f + 0.5f * NdcX;
    OutUv[1]             = 0.5f - 0.5f * NdcY;
    return true;
}

PackedEnsemblePayload PackEnsemblePayload(const PlanetaryRealmEnsemble& Ensemble,
                                          const PlanetaryCameraView&    View,
                                          uint32_t                      HipparcosStarCount)
{
    PackedEnsemblePayload Out{};
    auto&                 Slots = Out.Float4Slots;
    Slots.resize(kEnsembleHeaderFloat4Count, {0.0f, 0.0f, 0.0f, 0.0f});

    const uint32_t RealmCount  = static_cast<uint32_t>(Ensemble.Realms.size());
    const uint32_t PortalCount = static_cast<uint32_t>(Ensemble.Portals.size());

    std::vector<std::array<float, 4>> SunSlots;
    std::vector<std::array<float, 4>> MoonSlots;
    std::vector<std::array<float, 4>> PlanetSlots;
    std::vector<std::array<float, 4>> CloudSlots;
    std::vector<std::array<float, 4>> FogBoxSlots;
    std::vector<std::array<float, 4>> LuminaireSlots;
    std::vector<std::array<float, 4>> LightningSlots;
    std::vector<std::array<float, 4>> FacetSlots;
    std::vector<std::array<float, 4>> SphereSlots;
    std::vector<std::array<float, 4>> GlassSlots;
    std::vector<std::array<float, 4>> PortalSlots;
    std::vector<std::array<float, 4>> FlareSourceSlots;
    std::vector<std::array<float, 4>> CustomStarSlots;
    std::vector<std::array<float, 4>> ForceSourceSlots;
    std::vector<std::array<float, 4>> RealmSlots;
    RealmSlots.reserve(RealmCount * kRealmRecordFloat4Stride);

    std::vector<uint32_t> RealmFirstSunGlobalSlot(RealmCount, 0u);

    for (uint32_t RealmIdx = 0u; RealmIdx < RealmCount; ++RealmIdx)
    {
        const PlanetaryRealm& Realm = Ensemble.Realms[RealmIdx];

        const uint32_t SunFirst           = static_cast<uint32_t>(SunSlots.size() / kSunRecordFloat4Stride);
        RealmFirstSunGlobalSlot[RealmIdx] = SunFirst;
        for (const RealmSolarSource& Sun : Realm.Suns)
        {
            const auto           Dir = Normalize3(Sun.DirectionUnit);
            std::array<float, 2> Uv{0.5f, 0.5f};
            (void)ProjectDirectionToScreenUv(View, Dir, Uv);

            SunSlots.push_back({Dir[0], Dir[1], Dir[2], static_cast<float>(Sun.Disc.AngularRadiusRad)});
            SunSlots.push_back({Sun.DiscIrradiance[0],
                                Sun.DiscIrradiance[1],
                                Sun.DiscIrradiance[2],
                                static_cast<float>(Sun.Disc.LimbLinear)});
            SunSlots.push_back({Uv[0], Uv[1], Sun.MasterFlareGain, static_cast<float>(Sun.Flare.GhostStrength)});
            SunSlots.push_back({Sun.ChromaticShift,
                                static_cast<float>(Sun.Flare.HaloStrength),
                                Sun.HaloRadiusNdc,
                                Sun.HaloWidthNdc});
            SunSlots.push_back({static_cast<float>(Sun.Flare.StreakStrength),
                                static_cast<float>(Sun.Flare.AnamorphicLengthNdc),
                                static_cast<float>(Sun.Flare.StreakAngleRad),
                                static_cast<float>(Sun.Flare.AnamorphicStrength)});
            SunSlots.push_back({static_cast<float>(Sun.Flare.StarburstStrength),
                                static_cast<float>(Sun.Flare.ApertureBlades),
                                Sun.ApertureRoundness,
                                static_cast<float>(Sun.Flare.VeilingStrength)});
        }

        const uint32_t MoonFirst = static_cast<uint32_t>(MoonSlots.size() / kMoonRecordFloat4Stride);
        for (const LunarBodyDescriptor& Moon : Realm.Moons)
        {
            const auto  Dir      = Normalize3(FromVec3(Moon.DirectionUnit));
            const float Haze     = ResolveLunarAtmosphereHaze(Moon);
            const Vec3  HazeTint = ResolveLunarAtmosphereTint(Moon);
            MoonSlots.push_back({Dir[0], Dir[1], Dir[2], Moon.AngularRadiusRad});
            MoonSlots.push_back({1.0f, 1.0f, 1.0f, 0.28f * Moon.SurfaceBrightness});
            MoonSlots.push_back({Moon.EarthshineStrength, 0.8f, Moon.SpinLongitudeRad, Moon.LibrationLatitudeRad});
            MoonSlots.push_back({static_cast<float>(static_cast<uint32_t>(Moon.Preset)),
                                 Moon.SurfaceBrightness,
                                 Moon.RollAngleRad,
                                 Haze});
            MoonSlots.push_back({HazeTint.X, HazeTint.Y, HazeTint.Z, Haze});
            MoonSlots.push_back({Moon.PhaseLightDirection.X,Moon.PhaseLightDirection.Y,Moon.PhaseLightDirection.Z,Moon.OverridePhase ? 1.0f : 0.0f});
            MoonSlots.push_back({Moon.PhaseIrradiance.X,Moon.PhaseIrradiance.Y,Moon.PhaseIrradiance.Z,Moon.GlowStrength});
        }

        const uint32_t PlanetFirst = static_cast<uint32_t>(PlanetSlots.size() / kPlanetRecordFloat4Stride);
        for (const PlanetaryBodyDescriptor& Planet : Realm.Planets)
        {
            const auto Dir  = Normalize3(FromVec3(Planet.DirectionUnit));
            const auto Axis = Normalize3(FromVec3(Planet.SpinAxisUnit));
            std::array<float, 3> SurfTint = {0.90f, 0.80f, 0.64f};
            if (Planet.Archetype == PlanetarySurfaceArchetype::OceanicTerrestrial)
            {
                SurfTint = {0.16f, 0.44f, 0.86f};
            }
            else if (Planet.Archetype == PlanetarySurfaceArchetype::RustyDesertWorld)
            {
                SurfTint = {0.82f, 0.42f, 0.26f};
            }
            else if (Planet.Archetype == PlanetarySurfaceArchetype::MethaneIceGiant)
            {
                SurfTint = {0.32f, 0.78f, 0.94f};
            }

            const auto AtmoTint = FromVec3(Planet.Atmosphere.RayleighTintRgb);
            const float RingOpacity = Planet.Rings.Enabled
                                          ? std::clamp(1.0f - std::exp(-Planet.Rings.PeakOpticalDepth), 0.0f, 0.96f)
                                          : 0.0f;

            PlanetSlots.push_back({Dir[0], Dir[1], Dir[2], Planet.AngularRadiusRad});
            PlanetSlots.push_back({Axis[0], Axis[1], Axis[2], Planet.SpinPhaseRad});
            PlanetSlots.push_back({SurfTint[0], SurfTint[1], SurfTint[2], 0.55f * Planet.SurfaceAlbedoScale});
            PlanetSlots.push_back({AtmoTint[0] * Planet.SurfaceAlbedoScale,
                                   AtmoTint[1] * Planet.SurfaceAlbedoScale,
                                   AtmoTint[2] * Planet.SurfaceAlbedoScale,
                                   Planet.Atmosphere.Enabled ? Planet.Atmosphere.ShellHeightFraction : 0.0f});
            PlanetSlots.push_back({Planet.Atmosphere.LimbGlowStrength,
                                   static_cast<float>(static_cast<uint32_t>(Planet.Archetype)),
                                   Planet.Rings.Enabled ? Planet.Rings.InnerRadiusRatio : 0.0f,
                                   Planet.Rings.Enabled ? Planet.Rings.OuterRadiusRatio : 0.0f});
            PlanetSlots.push_back({RingOpacity,
                                   Planet.Rings.OuterTintRgb.X * Planet.SurfaceAlbedoScale,
                                   Planet.Rings.OuterTintRgb.Y * Planet.SurfaceAlbedoScale,
                                   Planet.Rings.OuterTintRgb.Z * Planet.SurfaceAlbedoScale});
            PlanetSlots.push_back({Planet.BandContrast,Planet.SurfaceDetail,Planet.CloudVeil,float(Planet.ProceduralSeed)});
            PlanetSlots.push_back({Planet.SurfaceTint.X,Planet.SurfaceTint.Y,Planet.SurfaceTint.Z,0});
        }

        const uint32_t CloudFirst = static_cast<uint32_t>(CloudSlots.size() / kCloudRecordFloat4Stride);
        for (const CloudVolumeProfile& Cloud : Realm.CloudVolumes)
        {
            const float LowerAlt  = static_cast<float>(Cloud.Slab.LowerHeightM);
            const float Thickness = static_cast<float>(std::max(0.0, Cloud.Slab.TopHeightM - Cloud.Slab.LowerHeightM));
            const auto  BoxCenter = FromSpatial(Cloud.BoxCenterM);
            const auto  BoxHalf   = FromSpatial(Cloud.BoxHalfExtentsM);

            CloudSlots.push_back({LowerAlt,
                                  Thickness,
                                  static_cast<float>(Cloud.Slab.Coverage),
                                  static_cast<float>(Cloud.Slab.DensityScale)});
            CloudSlots.push_back({static_cast<float>(Cloud.WorleyErosionStrength),
                                  static_cast<float>(Cloud.Slab.ExtinctionPerM),
                                  static_cast<float>(std::clamp(Cloud.ScatteringAlbedo, 0.0, 1.0)),
                                  static_cast<float>(Cloud.Slab.PhaseG)});
            CloudSlots.push_back({static_cast<float>(Cloud.BackscatterPhaseG),
                                  static_cast<float>(static_cast<uint32_t>(Cloud.HeightProfile)),
                                  static_cast<float>(Cloud.PowderStrength),
                                  Cloud.Domain == CloudVolumeDomain::BoundedLocalBox ? 1.0f : 0.0f});
            CloudSlots.push_back({BoxCenter[0], BoxCenter[1], BoxCenter[2], static_cast<float>(Cloud.BoxEdgeFalloffM)});
            CloudSlots.push_back({BoxHalf[0],
                                  BoxHalf[1],
                                  BoxHalf[2],
                                  static_cast<float>(Cloud.Slab.WindXMps) * Realm.CloudWindSeconds});
            CloudSlots.push_back({static_cast<float>(std::clamp(Cloud.SilverLiningWeight, 0.0, 0.8)),
                static_cast<float>(std::clamp(Cloud.ViewSamples, 8u, 192u)),
                static_cast<float>(std::min(Cloud.ShadowSamples, 12u)), 0.0f});
            SpatialVector Offset = Cloud.WindDisplacementM;
            if (Cloud.FollowField && !Cloud.DisplacementReady)
            {
                const auto Flow = Realm.ForceField.SampleStep(Cloud.BoxCenterM);
                const auto Swirl = Realm.ForceField.SampleSwirl(Cloud.BoxCenterM);
                const double Time = Realm.ForceField.ElapsedSeconds();
                Offset = {(Flow.X + Swirl.X) * Time, (Flow.Y + Swirl.Y) * Time, (Flow.Z + Swirl.Z) * Time};
            }
            CloudSlots.push_back({static_cast<float>(Cloud.BoundsShape), static_cast<float>(Cloud.FeatureScaleMetres),
                static_cast<float>(Cloud.AnvilShaping), (Cloud.FollowField || Cloud.DisplacementReady) ? 1.0f : 0.0f});
            CloudSlots.push_back({static_cast<float>(Offset.X), static_cast<float>(Offset.Y), static_cast<float>(Offset.Z), 0.0f});
        }

        const uint32_t FogBoxFirst = static_cast<uint32_t>(FogBoxSlots.size() / kFogBoxRecordFloat4Stride);
        for (const LocalFogVolumeProfile& FogVol : Realm.LocalFogVolumes)
        {
            const auto  Cent = FromSpatial(FogVol.BoxCenterM);
            const auto  Half = FromSpatial(FogVol.BoxHalfExtentsM);
            const float Freq = (FogVol.NoiseScaleM > 1.0e-3) ? static_cast<float>(1.0 / FogVol.NoiseScaleM) : 0.1f;
            FogBoxSlots.push_back({Cent[0], Cent[1], Cent[2], static_cast<float>(FogVol.ExtinctionPerM)});
            FogBoxSlots.push_back({Half[0], Half[1], Half[2], static_cast<float>(FogVol.ScatteringAlbedo)});
            FogBoxSlots.push_back({static_cast<float>(FogVol.NoiseModulation),
                                   Freq,
                                   static_cast<float>(FogVol.PhaseAsymmetryG),
                                   static_cast<float>(FogVol.BoxEdgeFalloffM)});
            FogBoxSlots.push_back({static_cast<float>(FogVol.ScatteringTint.R), static_cast<float>(FogVol.ScatteringTint.G),
                static_cast<float>(FogVol.ScatteringTint.B), static_cast<float>(FogVol.HeightFalloffM)});
            FogBoxSlots.push_back({static_cast<float>(FogVol.BoundsShape), FogVol.SelfShadow ? 1.0f : 0.0f, 0.0f, 0.0f});
            SpatialVector Offset = FogVol.DisplacementReady ? FogVol.WindDisplacementM : SpatialVector{
                FogVol.WindVelocityMps.X * FogVol.WindTimeS,
                FogVol.WindVelocityMps.Y * FogVol.WindTimeS,
                FogVol.WindVelocityMps.Z * FogVol.WindTimeS};
            if (FogVol.FollowField && !FogVol.DisplacementReady)
            {
                const auto Flow = Realm.ForceField.SampleStep(FogVol.BoxCenterM);
                const auto Swirl = Realm.ForceField.SampleSwirl(FogVol.BoxCenterM);
                const double Time = Realm.ForceField.ElapsedSeconds();
                Offset = {(Flow.X + Swirl.X) * Time, (Flow.Y + Swirl.Y) * Time, (Flow.Z + Swirl.Z) * Time};
            }
            FogBoxSlots.push_back({static_cast<float>(Offset.X), static_cast<float>(Offset.Y), static_cast<float>(Offset.Z),
                static_cast<float>(FogVol.Seed & 0xFFFFFFu)});
        }

        const uint32_t LumFirst = static_cast<uint32_t>(LuminaireSlots.size() / kLuminaireRecordFloat4Stride);
        for (const PhotometricFogLuminaire& Luminaire : Realm.Luminaires)
        {
            const auto  Pos  = FromSpatial(Luminaire.PositionM);
            const auto  Axis = Normalize3(FromSpatial(Luminaire.AxisUnit));
            const float Peak = static_cast<float>(
                std::max({Luminaire.PeakIntensity.R, Luminaire.PeakIntensity.G, Luminaire.PeakIntensity.B, 1.0e-4}));
            LuminaireSlots.push_back({Pos[0], Pos[1], Pos[2], Peak});
            LuminaireSlots.push_back({Axis[0], Axis[1], Axis[2], static_cast<float>(Luminaire.OuterConeCos)});
            LuminaireSlots.push_back({static_cast<float>(Luminaire.PeakIntensity.R) / Peak,
                                      static_cast<float>(Luminaire.PeakIntensity.G) / Peak,
                                      static_cast<float>(Luminaire.PeakIntensity.B) / Peak,
                                      static_cast<float>(Luminaire.RangeMeters)});
        }

        const uint32_t BoltFirst = static_cast<uint32_t>(LightningSlots.size() / kLightningRecordFloat4Stride);
        for (const LightningFogChannel& Bolt : Realm.LightningChannels)
        {
            const auto  Start = FromSpatial(Bolt.SegmentStartM);
            const auto  End   = FromSpatial(Bolt.SegmentEndM);
            const float Peak  = static_cast<float>(
                std::max({Bolt.LineRadiance.R, Bolt.LineRadiance.G, Bolt.LineRadiance.B, 1.0e-4}));
            LightningSlots.push_back({Start[0], Start[1], Start[2], Peak});
            LightningSlots.push_back({End[0], End[1], End[2], static_cast<float>(Bolt.CoreRadiusM)});
            LightningSlots.push_back({static_cast<float>(Bolt.LineRadiance.R) / Peak,
                                      static_cast<float>(Bolt.LineRadiance.G) / Peak,
                                      static_cast<float>(Bolt.LineRadiance.B) / Peak,
                                      0.65f});
        }

        const uint32_t FacetFirst = static_cast<uint32_t>(FacetSlots.size() / kFacetRecordFloat4Stride);
        for (const RadiantFogFacet& Facet : Realm.RadiantFacets)
        {
            const auto V0 = FromSpatial(Facet.CornerA);
            const auto V1 = FromSpatial(Facet.CornerB);
            const auto V2 = FromSpatial(Facet.CornerC);
            const std::array<float, 3> Cent = {
                (V0[0] + V1[0] + V2[0]) / 3.0f,
                (V0[1] + V1[1] + V2[1]) / 3.0f,
                (V0[2] + V1[2] + V2[2]) / 3.0f,
            };
            const std::array<float, 3> E1 = {V1[0] - V0[0], V1[1] - V0[1], V1[2] - V0[2]};
            const std::array<float, 3> E2 = {V2[0] - V0[0], V2[1] - V0[1], V2[2] - V0[2]};
            const std::array<float, 3> Cr = {
                E1[1] * E2[2] - E1[2] * E2[1],
                E1[2] * E2[0] - E1[0] * E2[2],
                E1[0] * E2[1] - E1[1] * E2[0],
            };
            const float Area = 0.5f * std::sqrt(Dot3(Cr, Cr));
            const auto  Nrm  = Normalize3(Cr);
            FacetSlots.push_back({Cent[0], Cent[1], Cent[2], Area});
            FacetSlots.push_back({Nrm[0], Nrm[1], Nrm[2], 16.0f});
            FacetSlots.push_back({static_cast<float>(Facet.SurfaceRadiance.R),
                                  static_cast<float>(Facet.SurfaceRadiance.G),
                                  static_cast<float>(Facet.SurfaceRadiance.B),
                                  1.0f});
        }

        const uint32_t SphFirst = static_cast<uint32_t>(SphereSlots.size() / kSphereRecordFloat4Stride);
        for (const RealmSphereObstacle& Sphere : Realm.SolidOccluders)
        {
            SphereSlots.push_back({Sphere.CenterMeters[0], Sphere.CenterMeters[1], Sphere.CenterMeters[2], Sphere.RadiusMeters});
            SphereSlots.push_back({Sphere.AlbedoRgb[0], Sphere.AlbedoRgb[1], Sphere.AlbedoRgb[2], Sphere.Roughness});
        }

        const uint32_t GlsFirst = static_cast<uint32_t>(GlassSlots.size() / kGlassRecordFloat4Stride);
        for (const RealmGlassPane& Glass : Realm.GlassPanes)
        {
            const auto Nrm = Normalize3(Glass.NormalUnit);
            GlassSlots.push_back({Glass.CenterMeters[0], Glass.CenterMeters[1], Glass.CenterMeters[2], Glass.HalfWidthMeters});
            GlassSlots.push_back({Nrm[0], Nrm[1], Nrm[2], Glass.HalfHeightMeters});
            GlassSlots.push_back({Glass.TintTransmittance[0], Glass.TintTransmittance[1], Glass.TintTransmittance[2], Glass.FresnelF0});
        }

        const uint32_t CStarFirst = static_cast<uint32_t>(CustomStarSlots.size() / kCustomStarRecordFloat4Stride);
        for (const CustomStarDescriptor& Star : Realm.CustomStars)
        {
            const auto  Dir  = Normalize3(FromVec3(Star.DirectionUnit));
            const Vec3  Tint = PlanetaryDomeProjection::KelvinToBlackbodyChromaticity(Star.TemperatureKelvin);
            const float Mag  = -2.5f * std::log10(std::max(Star.RadianceGain * 0.1f, 1.0e-4f));
            CustomStarSlots.push_back({Dir[0], Dir[1], Dir[2], Mag});
            CustomStarSlots.push_back({Tint.X, Tint.Y, Tint.Z, Star.CoronaGlowStrength});
        }

        const AtmosphereParameters& Atmo    = Realm.Atmosphere;
        const float                 Falloff = (Realm.HeightFog.FalloffHeightM > 1.0e-3)
                                                  ? static_cast<float>(1.0 / Realm.HeightFog.FalloffHeightM)
                                                  : 0.04f;
        RealmSlots.push_back({static_cast<float>(Atmo.GroundRadiusKm),
                              static_cast<float>(Atmo.TopRadiusKm),
                              Realm.ObserverAltitudeKm,
                              static_cast<float>(Atmo.GroundAlbedo)});
        RealmSlots.push_back({static_cast<float>(Atmo.RayleighScattering.R),
                              static_cast<float>(Atmo.RayleighScattering.G),
                              static_cast<float>(Atmo.RayleighScattering.B),
                              static_cast<float>(Atmo.RayleighScaleHeightKm)});
        RealmSlots.push_back({static_cast<float>(Atmo.MieScattering),
                              static_cast<float>(Atmo.MieScattering + Atmo.MieAbsorption),
                              static_cast<float>(Atmo.MieScaleHeightKm),
                              static_cast<float>(Atmo.MiePhaseG)});
        RealmSlots.push_back({static_cast<float>(Atmo.OzoneAbsorption.R),
                              static_cast<float>(Atmo.OzoneAbsorption.G),
                              static_cast<float>(Atmo.OzoneAbsorption.B),
                              static_cast<float>(Atmo.OzonePeakKm)});
        RealmSlots.push_back({static_cast<float>(Atmo.OzoneWidthKm),
                              Realm.GroundPlaneY,
                              Realm.GroundAlbedoRgb[0],
                              Realm.GroundAlbedoRgb[1]});
        RealmSlots.push_back({Realm.GroundAlbedoRgb[2],
                              Realm.StarBrightnessScale,
                              Realm.ScintillationStrength,
                              Realm.MilkyWayIntensity});
        RealmSlots.push_back({static_cast<float>(Realm.HeightFog.ReferenceDensity),
                              Falloff,
                              static_cast<float>(Realm.HeightFog.ReferenceHeightM),
                              0.92f});
        RealmSlots.push_back({0.0f,
                              Realm.HeightFogPhaseG,
                              Realm.HeightFogScatterAlbedo,
                              Realm.EnableAerialPerspective ? 1.0f : 0.0f});
        RealmSlots.push_back({static_cast<float>(SunFirst),
                              static_cast<float>(Realm.Suns.size()),
                              static_cast<float>(MoonFirst),
                              static_cast<float>(Realm.Moons.size())});
        RealmSlots.push_back({static_cast<float>(PlanetFirst),
                              static_cast<float>(Realm.Planets.size()),
                              static_cast<float>(CloudFirst),
                              static_cast<float>(Realm.CloudVolumes.size())});
        RealmSlots.push_back({static_cast<float>(FogBoxFirst),
                              static_cast<float>(Realm.LocalFogVolumes.size()),
                              static_cast<float>(LumFirst),
                              static_cast<float>(Realm.Luminaires.size())});
        RealmSlots.push_back({static_cast<float>(BoltFirst),
                              static_cast<float>(Realm.LightningChannels.size()),
                              static_cast<float>(SphFirst),
                              static_cast<float>(Realm.SolidOccluders.size())});
        RealmSlots.push_back({static_cast<float>(GlsFirst),
                              static_cast<float>(Realm.GlassPanes.size()),
                              static_cast<float>(FacetFirst),
                              static_cast<float>(Realm.RadiantFacets.size())});
        RealmSlots.push_back({static_cast<float>(CStarFirst),
                              static_cast<float>(Realm.CustomStars.size()),
                              Realm.CloudShadowHalfSpanM,
                              Realm.CloudWindSeconds});
        const uint32_t ForceFirst = static_cast<uint32_t>(ForceSourceSlots.size() / kForceSourceFloat4Stride);
        const uint32_t ForceCount = Realm.ForceField.AppendGpuForceRecords(ForceSourceSlots);
        const auto&    WindProf   = Realm.ForceField.WindProfile();
        const float    HasSources = (ForceCount > 0u) ? 1.0f : 0.0f;
        const float    EffWindSpd = (Realm.ForceField.QueryEnabled() && (Realm.ForceField.ElapsedSeconds() > 0.0 || HasSources > 0.0f))
                                        ? static_cast<float>(WindProf.SpeedMetersPerSecond)
                                        : 0.0f;
        const float    EffWindSec = (Realm.ForceField.ElapsedSeconds() > 0.0)
                                        ? static_cast<float>(Realm.ForceField.ElapsedSeconds())
                                        : Realm.CloudWindSeconds;

        RealmSlots.push_back({Realm.StarBasisRow0[0], Realm.StarBasisRow0[1], Realm.StarBasisRow0[2], Realm.TwilightStrength});
        RealmSlots.push_back({Realm.StarBasisRow1[0], Realm.StarBasisRow1[1], Realm.StarBasisRow1[2], Realm.StarLimitingMagnitude});
        RealmSlots.push_back({static_cast<float>(WindProf.BearingRadians),
                              EffWindSpd,
                              static_cast<float>(WindProf.ShearPerKilometer),
                              static_cast<float>(WindProf.VeerRadiansPerKilometer)});
        RealmSlots.push_back({static_cast<float>(ForceFirst),
                              static_cast<float>(ForceCount),
                              static_cast<float>(WindProf.GustAmplitude),
                              EffWindSec});
        RealmSlots.push_back({Realm.StarPointSize, Realm.StarTwinkleClock, Realm.StarHalo, 0.0f});
    }

    for (const PortalAperture& Portal : Ensemble.Portals)
    {
        PortalSlots.push_back({static_cast<float>(Portal.SourceRealmIndex),
                               static_cast<float>(Portal.TargetRealmIndex),
                               Portal.HalfWidthMeters,
                               Portal.HalfHeightMeters});
        PortalSlots.push_back({Portal.CenterMeters[0], Portal.CenterMeters[1], Portal.CenterMeters[2], Portal.FrameWidthMeters});
        PortalSlots.push_back({Portal.NormalUnit[0], Portal.NormalUnit[1], Portal.NormalUnit[2], Portal.FrameAlbedoRgb[0]});
        PortalSlots.push_back({Portal.RightUnit[0], Portal.RightUnit[1], Portal.RightUnit[2], Portal.FrameAlbedoRgb[1]});
        PortalSlots.push_back({Portal.UpUnit[0], Portal.UpUnit[1], Portal.UpUnit[2], Portal.FrameAlbedoRgb[2]});
        PortalSlots.push_back({Portal.TargetOriginMeters[0], Portal.TargetOriginMeters[1], Portal.TargetOriginMeters[2], 0.0f});
        PortalSlots.push_back({Portal.TargetRightUnit[0], Portal.TargetRightUnit[1], Portal.TargetRightUnit[2], 0.0f});
        PortalSlots.push_back({Portal.TargetUpUnit[0], Portal.TargetUpUnit[1], Portal.TargetUpUnit[2], 0.0f});
        PortalSlots.push_back({Portal.TargetForwardUnit[0], Portal.TargetForwardUnit[1], Portal.TargetForwardUnit[2], 0.0f});
    }

    if (View.ActiveRealmIndex < RealmCount)
    {
        const PlanetaryRealm& ActiveRealm = Ensemble.Realms[View.ActiveRealmIndex];
        for (uint32_t SunIdx = 0u; SunIdx < ActiveRealm.Suns.size(); ++SunIdx)
        {
            const RealmSolarSource& Sun = ActiveRealm.Suns[SunIdx];
            if (!Sun.EnableLensFlare || Sun.MasterFlareGain <= 0.0f)
            {
                continue;
            }
            const auto           Dir = Normalize3(Sun.DirectionUnit);
            std::array<float, 2> Uv{0.5f, 0.5f};
            if (!ProjectDirectionToScreenUv(View, Dir, Uv))
            {
                continue;
            }
            const uint32_t GlobalSun = RealmFirstSunGlobalSlot[View.ActiveRealmIndex] + SunIdx;
            Out.FlareCandidates.push_back({View.ActiveRealmIndex, SunIdx, -1, GlobalSun, Dir, Uv, Sun});

            FlareSourceSlots.push_back({static_cast<float>(View.ActiveRealmIndex),
                                        static_cast<float>(SunIdx),
                                        0.0f,
                                        static_cast<float>(GlobalSun)});
            FlareSourceSlots.push_back({Dir[0], Dir[1], Dir[2], static_cast<float>(Sun.Disc.AngularRadiusRad)});
            FlareSourceSlots.push_back({Uv[0], Uv[1], Sun.DiscIrradiance[0], Sun.DiscIrradiance[1]});
            FlareSourceSlots.push_back({Sun.DiscIrradiance[2], static_cast<float>(Sun.Disc.LimbLinear), 0.0f, 0.0f});
        }

        for (uint32_t PortalIdx = 0u; PortalIdx < PortalCount; ++PortalIdx)
        {
            const PortalAperture& Portal = Ensemble.Portals[PortalIdx];
            if (Portal.SourceRealmIndex != View.ActiveRealmIndex || Portal.TargetRealmIndex >= RealmCount)
            {
                continue;
            }
            const PlanetaryRealm&      TargetRealm  = Ensemble.Realms[Portal.TargetRealmIndex];
            const std::array<float, 3> InwardNormal = {-Portal.NormalUnit[0], -Portal.NormalUnit[1], -Portal.NormalUnit[2]};

            for (uint32_t SunIdx = 0u; SunIdx < TargetRealm.Suns.size(); ++SunIdx)
            {
                const RealmSolarSource& Sun = TargetRealm.Suns[SunIdx];
                if (!Sun.EnableLensFlare || Sun.MasterFlareGain <= 0.0f)
                {
                    continue;
                }
                const auto  TargetDir = Normalize3(Sun.DirectionUnit);
                const float Dr        = Dot3(TargetDir, Portal.TargetRightUnit);
                const float Du        = Dot3(TargetDir, Portal.TargetUpUnit);
                const float Df        = Dot3(TargetDir, Portal.TargetForwardUnit);
                if (Df <= 1.0e-3f)
                {
                    continue;
                }
                const auto CamWorldDir = Normalize3({
                    Portal.RightUnit[0] * Dr + Portal.UpUnit[0] * Du + InwardNormal[0] * Df,
                    Portal.RightUnit[1] * Dr + Portal.UpUnit[1] * Du + InwardNormal[1] * Df,
                    Portal.RightUnit[2] * Dr + Portal.UpUnit[2] * Du + InwardNormal[2] * Df,
                });
                std::array<float, 2> Uv{0.5f, 0.5f};
                if (!ProjectDirectionToScreenUv(View, CamWorldDir, Uv))
                {
                    continue;
                }
                const uint32_t GlobalSun = RealmFirstSunGlobalSlot[Portal.TargetRealmIndex] + SunIdx;
                Out.FlareCandidates.push_back(
                    {Portal.TargetRealmIndex, SunIdx, static_cast<int32_t>(PortalIdx), GlobalSun, CamWorldDir, Uv, Sun});

                FlareSourceSlots.push_back({static_cast<float>(Portal.TargetRealmIndex),
                                            static_cast<float>(SunIdx),
                                            static_cast<float>(PortalIdx + 1u),
                                            static_cast<float>(GlobalSun)});
                FlareSourceSlots.push_back({CamWorldDir[0], CamWorldDir[1], CamWorldDir[2], static_cast<float>(Sun.Disc.AngularRadiusRad)});
                FlareSourceSlots.push_back({Uv[0], Uv[1], Sun.DiscIrradiance[0], Sun.DiscIrradiance[1]});
                FlareSourceSlots.push_back({Sun.DiscIrradiance[2], static_cast<float>(Sun.Disc.LimbLinear), 0.0f, 0.0f});
            }
        }
    }

    auto AppendTable = [&](const std::vector<std::array<float, 4>>& Table) -> uint32_t {
        const uint32_t Offset = static_cast<uint32_t>(Slots.size());
        Slots.insert(Slots.end(), Table.begin(), Table.end());
        return Offset;
    };

    const uint32_t RealmOffset       = AppendTable(RealmSlots);
    const uint32_t SunOffset         = AppendTable(SunSlots);
    const uint32_t MoonOffset        = AppendTable(MoonSlots);
    const uint32_t PlanetOffset      = AppendTable(PlanetSlots);
    const uint32_t CloudOffset       = AppendTable(CloudSlots);
    const uint32_t FogBoxOffset      = AppendTable(FogBoxSlots);
    const uint32_t LuminaireOffset   = AppendTable(LuminaireSlots);
    const uint32_t LightningOffset   = AppendTable(LightningSlots);
    const uint32_t FacetOffset       = AppendTable(FacetSlots);
    const uint32_t SphereOffset      = AppendTable(SphereSlots);
    const uint32_t GlassOffset       = AppendTable(GlassSlots);
    const uint32_t PortalOffset      = AppendTable(PortalSlots);
    const uint32_t FlareSourceOffset = AppendTable(FlareSourceSlots);
    const uint32_t CustomStarOffset  = AppendTable(CustomStarSlots);
    const uint32_t ForceSourceOffset = AppendTable(ForceSourceSlots);

    const float TanHalfY = std::tan(0.5f * View.VerticalFovRad);
    const float Aspect   = static_cast<float>(std::max(1u, View.ViewportWidth)) /
                           static_cast<float>(std::max(1u, View.ViewportHeight));

    Slots[0] = {static_cast<float>(RealmCount),
                static_cast<float>(PortalCount),
                static_cast<float>(Out.FlareCandidates.size()),
                static_cast<float>(View.ActiveRealmIndex)};
    Slots[1] = {View.PositionMeters[0], View.PositionMeters[1], View.PositionMeters[2], TanHalfY};
    Slots[2] = {View.RightUnit[0], View.RightUnit[1], View.RightUnit[2], Aspect};
    Slots[3] = {View.UpUnit[0], View.UpUnit[1], View.UpUnit[2], View.ElapsedSeconds};
    Slots[4] = {View.ForwardUnit[0], View.ForwardUnit[1], View.ForwardUnit[2], View.ExposureScale};
    Slots[5] = {static_cast<float>(RealmOffset),
                static_cast<float>(SunOffset),
                static_cast<float>(MoonOffset),
                static_cast<float>(PlanetOffset)};
    Slots[6] = {static_cast<float>(CloudOffset),
                static_cast<float>(FogBoxOffset),
                static_cast<float>(LuminaireOffset),
                static_cast<float>(LightningOffset)};
    Slots[7] = {static_cast<float>(FacetOffset),
                static_cast<float>(SphereOffset),
                static_cast<float>(GlassOffset),
                static_cast<float>(PortalOffset)};
    Slots[8] = {static_cast<float>(FlareSourceOffset),
                static_cast<float>(CustomStarOffset),
                static_cast<float>(HipparcosStarCount),
                static_cast<float>(View.FrameIndex)};
    Slots[9] = {static_cast<float>(View.ViewportWidth),
                static_cast<float>(View.ViewportHeight),
                static_cast<float>(ForceSourceOffset),
                static_cast<float>(ForceSourceSlots.size() / kForceSourceFloat4Stride)};

    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                           PLANETARY SUBMISSION SEQUENCE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

bool PlanetarySubmissionSequence::Initialize(VulkanComputeExchange& Exchange,
                                             const std::string&     ShaderBinaryDirectory,
                                             const std::string&     LunaSourceJpgPath,
                                             const std::string&     HipparcosCsvPath,
                                             std::string*           OutDiagnostic)
{
    if (!Visibility_.Initialize(Exchange, ShaderBinaryDirectory, OutDiagnostic) ||
        !AtmosphereLuts_.Initialize(Exchange, ShaderBinaryDirectory, OutDiagnostic) ||
        !PlanetaryDome_.Initialize(Exchange, ShaderBinaryDirectory, LunaSourceJpgPath, HipparcosCsvPath, OutDiagnostic) ||
        !Clouds_.Initialize(Exchange, ShaderBinaryDirectory, OutDiagnostic) ||
        !Fog_.Initialize(Exchange, ShaderBinaryDirectory, OutDiagnostic) ||
        !LensFlare_.Initialize(Exchange, ShaderBinaryDirectory, OutDiagnostic))
    {
        return false;
    }

    return Exchange.AllocateRgba16fArray(8u, 8u, 1u, HdrFrameTarget_);
}

void PlanetarySubmissionSequence::Shutdown(VulkanComputeExchange& Exchange) noexcept
{
    LensFlare_.Shutdown(Exchange);
    Fog_.Shutdown(Exchange);
    Clouds_.Shutdown(Exchange);
    PlanetaryDome_.Shutdown(Exchange);
    AtmosphereLuts_.Shutdown(Exchange);
    Visibility_.Shutdown(Exchange);
    Exchange.ReleaseImage(HdrFrameTarget_);
    Exchange.ReleaseMemorySpan(EnsembleSpan_);
    LastPayload_ = PackedEnsemblePayload{};
}

bool PlanetarySubmissionSequence::ExecuteFrame(VulkanComputeExchange&        Exchange,
                                               const PlanetaryRealmEnsemble& Ensemble,
                                               const PlanetaryCameraView&    CameraView)
{
    Clouds_.ConfigureResolution(CameraView.CloudResolutionPercent);
    Fog_.ConfigureSamples(CameraView.FogMarchSamples);
    const uint32_t W = std::max(1u, CameraView.ViewportWidth);
    const uint32_t H = std::max(1u, CameraView.ViewportHeight);

    if (!Visibility_.EnsureDimensions(Exchange, W, H))
    {
        return false;
    }
    if (HdrFrameTarget_.Width != W || HdrFrameTarget_.Height != H)
    {
        if (!Exchange.AllocateRgba16fArray(W, H, 1u, HdrFrameTarget_))
        {
            return false;
        }
    }

    LastPayload_ = PackEnsemblePayload(Ensemble, CameraView, PlanetaryDome_.IndexedStarCount());
    if (!Exchange.UploadMemorySpan(EnsembleSpan_,
                                   LastPayload_.Float4Slots.data(),
                                   LastPayload_.Float4Slots.size() * sizeof(float) * 4u))
    {
        return false;
    }

    std::vector<AtmosphereRealmRevision> Revisions;
    Revisions.reserve(Ensemble.Realms.size());
    for (const PlanetaryRealm& Realm : Ensemble.Realms)
    {
        Revisions.push_back({Realm.AtmosphereRevision, Realm.SkyViewRevision, Realm.AtmosphereSamples, Realm.MultipleScattering});
    }

    // 1. Dirty-gated per-realm Transmittance (256x64xR), Multi-Scattering (32x32xR), and AtmosphereProjection (192x108xR) LUTs
    if (!AtmosphereLuts_.Execute(Exchange, EnsembleSpan_, Revisions))
    {
        return false;
    }

    // 2. Orthographic 256x256xR CloudShadowProjection per realm (sampled by VisibilityProjection + FogSpace)
    if (!Clouds_.ExecuteShadowProjections(
            Exchange, EnsembleSpan_, AtmosphereLuts_, static_cast<uint32_t>(Ensemble.Realms.size()), Visibility_.VisibilityArray()))
    {
        return false;
    }

    // 3. Multi-realm & cross-portal VisibilityProjection (reversed-Z depth + cloud-shadowed + moonlit opaque surfaces + glass)
    if (!Visibility_.Execute(Exchange, EnsembleSpan_, AtmosphereLuts_, Clouds_, W, H, CameraView.FrameIndex))
    {
        return false;
    }

    // 4. Planetary Dome (atmosphere + twilight + Hipparcos twinkling stars + 2K 6-body hazy lunar atlas + ringed planets + suns)
    if (!PlanetaryDome_.Execute(
            Exchange, EnsembleSpan_, AtmosphereLuts_, Visibility_.VisibilityArray(), HdrFrameTarget_, W, H))
    {
        return false;
    }

    // 5. Volumetric Clouds (non-tiled 3D procedural global slabs + ForceField tornado funnel + bounded local boxes + temporal filter)
    if (!Clouds_.ExecuteTraceAndReconstruct(
            Exchange, EnsembleSpan_, AtmosphereLuts_, Visibility_.VisibilityArray(), HdrFrameTarget_, W, H, CameraView.FrameIndex))
    {
        return false;
    }

    // 6. FogSpace (analytic height fog + planet-curved aerial perspective + 3D froxel fog + ForceField dust/debris + emitters)
    if (!Fog_.Execute(
            Exchange, EnsembleSpan_, AtmosphereLuts_, Clouds_, Visibility_.VisibilityArray(), HdrFrameTarget_, W, H))
    {
        return false;
    }

    // 7. Optical Lens Flare (1x1-per-source GPU visibility reduction + chromatic ghosts + halo + streaks + aperture starburst)
    const auto FlareCount = static_cast<uint32_t>(LastPayload_.FlareCandidates.size());
    if (!LensFlare_.Execute(
            Exchange, EnsembleSpan_, Visibility_.VisibilityArray(), Clouds_, HdrFrameTarget_, FlareCount, W, H))
    {
        return false;
    }

    return true;
}

bool PlanetarySubmissionSequence::EnsureViewportTargets(VulkanComputeExchange& Exchange, uint32_t ViewportWidth, uint32_t ViewportHeight)
{
    const uint32_t W = std::max(1u, ViewportWidth);
    const uint32_t H = std::max(1u, ViewportHeight);
    if (!Visibility_.EnsureDimensions(Exchange, W, H))
    {
        return false;
    }
    if (HdrFrameTarget_.Width != W || HdrFrameTarget_.Height != H)
    {
        return Exchange.AllocateRgba16fArray(W, H, 1u, HdrFrameTarget_);
    }
    return true;
}

bool PlanetarySubmissionSequence::ExecuteSkyAndShadows(VulkanComputeExchange&        Exchange,
                                                       const PlanetaryRealmEnsemble& Ensemble,
                                                       const PlanetaryCameraView&    CameraView)
{
    SkyAndShadowsReady_ = false;
    LastPayload_ = PackEnsemblePayload(Ensemble, CameraView, PlanetaryDome_.IndexedStarCount());
    if (!Exchange.UploadMemorySpan(EnsembleSpan_,
                                   LastPayload_.Float4Slots.data(),
                                   LastPayload_.Float4Slots.size() * sizeof(float) * 4u))
    {
        return false;
    }

    std::vector<AtmosphereRealmRevision> Revisions;
    Revisions.reserve(Ensemble.Realms.size());
    for (const PlanetaryRealm& Realm : Ensemble.Realms)
    {
        Revisions.push_back({Realm.AtmosphereRevision, Realm.SkyViewRevision, Realm.AtmosphereSamples, Realm.MultipleScattering});
    }
    SkyAndShadowsReady_ = AtmosphereLuts_.Execute(Exchange, EnsembleSpan_, Revisions) &&
                          Clouds_.ExecuteShadowProjections(Exchange, EnsembleSpan_, AtmosphereLuts_,
                                                           static_cast<uint32_t>(Ensemble.Realms.size()), Visibility_.VisibilityArray());
    return SkyAndShadowsReady_;
}

bool PlanetarySubmissionSequence::ExecuteOverVisibility(VulkanComputeExchange&        Exchange,
                                                        const PlanetaryRealmEnsemble& Ensemble,
                                                        const PlanetaryCameraView&    CameraView)
{
    Clouds_.ConfigureResolution(CameraView.CloudResolutionPercent);
    Fog_.ConfigureSamples(CameraView.FogMarchSamples);
    const uint32_t W = std::max(1u, CameraView.ViewportWidth);
    const uint32_t H = std::max(1u, CameraView.ViewportHeight);
    if (Visibility_.VisibilityArray().Width != W || Visibility_.VisibilityArray().Height != H || HdrFrameTarget_.Width != W ||
        HdrFrameTarget_.Height != H)
    {
        return false;
    }

    // Same order as ExecuteFrame, step 3 (the visibility raster) supplied by the caller; the payload, LUTs and cloud shadow map
    // come from ExecuteSkyAndShadows when it ran for this frame.
    const bool Prepared = std::exchange(SkyAndShadowsReady_, false) || ExecuteSkyAndShadows(Exchange, Ensemble, CameraView);
    SkyAndShadowsReady_ = false;
    return Prepared &&
           PlanetaryDome_.Execute(Exchange, EnsembleSpan_, AtmosphereLuts_, Visibility_.VisibilityArray(), HdrFrameTarget_, W, H) &&
           Clouds_.ExecuteTraceAndReconstruct(
               Exchange, EnsembleSpan_, AtmosphereLuts_, Visibility_.VisibilityArray(), HdrFrameTarget_, W, H, CameraView.FrameIndex) &&
           Fog_.Execute(Exchange, EnsembleSpan_, AtmosphereLuts_, Clouds_, Visibility_.VisibilityArray(), HdrFrameTarget_, W, H, FogSceneShadow_) &&
           LensFlare_.Execute(Exchange,
                              EnsembleSpan_,
                              Visibility_.VisibilityArray(),
                              Clouds_,
                              HdrFrameTarget_,
                              static_cast<uint32_t>(LastPayload_.FlareCandidates.size()),
                              W,
                              H);
}

bool PlanetarySubmissionSequence::ReadbackHdrFrame(VulkanComputeExchange& Exchange,
                                                   std::vector<float>&    OutLinearRgbaFloats)
{
    return Exchange.DownloadRgba16fLayer(HdrFrameTarget_, 0u, OutLinearRgbaFloats);
}

bool PlanetarySubmissionSequence::ReadbackVisibilityLayer(VulkanComputeExchange& Exchange,
                                                          uint32_t               LayerIndex,
                                                          std::vector<float>&    OutRgbaFloats)
{
    return Visibility_.ReadbackLayer(Exchange, LayerIndex, OutRgbaFloats);
}

void TonemapHdrToSrgb8(std::span<const float> LinearRgbaFloats,
                       uint32_t               Width,
                       uint32_t               Height,
                       float                  ExposureScale,
                       std::vector<uint8_t>&  OutSrgbRgbaBytes)
{
    const size_t PixelCount = static_cast<size_t>(Width) * Height;
    OutSrgbRgbaBytes.resize(PixelCount * 4u, 255u);
    if (LinearRgbaFloats.size() < PixelCount * 4u)
    {
        return;
    }
    for (size_t Index = 0u; Index < PixelCount; ++Index)
    {
        const float Red                 = AcesFilmicChannel(LinearRgbaFloats[Index * 4u + 0u] * ExposureScale);
        const float Green               = AcesFilmicChannel(LinearRgbaFloats[Index * 4u + 1u] * ExposureScale);
        const float Blue                = AcesFilmicChannel(LinearRgbaFloats[Index * 4u + 2u] * ExposureScale);
        OutSrgbRgbaBytes[Index * 4u + 0u] = LinearToSrgbByte(Red);
        OutSrgbRgbaBytes[Index * 4u + 1u] = LinearToSrgbByte(Green);
        OutSrgbRgbaBytes[Index * 4u + 2u] = LinearToSrgbByte(Blue);
        OutSrgbRgbaBytes[Index * 4u + 3u] = 255u;
    }
}

} // namespace Frontier
