/*==========================================================================================================================================
                                                  PLANETARY ENSEMBLE
                                     ------------------------------------------------------------------
             Multi-realm & cross-portal planetary environment ensemble: per-realm atmospheres,
             multi-suns, 6-body moons, ringed planets, non-tiled 3D clouds, volumetric fog,
             photometric IES/lightning/fire emitters, portal apertures, and GPU frame submission.
==========================================================================================================================================*/
// 🧩 Multi-instance & portal-ready planetary ensemble and GPU submission sequence.

#pragma once

#include "Engine/DeviceExchange/VulkanComputeExchange.h"
#include "Engine/GeometricRaster/VisibilityProjection.h"
#include "Engine/PhysicalDynamics/ForceField.h"
#include "Engine/PlanetaryAtmosphere/AstronomicalEphemeris.h"
#include "Engine/PlanetaryAtmosphere/AtmosphereProjection.h"
#include "Engine/PlanetaryAtmosphere/AtmosphereScattering.h"
#include "Engine/PlanetaryAtmosphere/CloudSpace.h"
#include "Engine/PlanetaryAtmosphere/FogSpace.h"
#include "Engine/PlanetaryAtmosphere/OpticalLensFlare.h"
#include "Engine/PlanetaryAtmosphere/PlanetaryProjection.h"
#include "Engine/PlanetaryAtmosphere/VolumetricClouds.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Frontier
{

inline constexpr uint32_t kEnsembleHeaderFloat4Count    = 10u;
inline constexpr uint32_t kRealmRecordFloat4Stride      = 19u;
inline constexpr uint32_t kSunRecordFloat4Stride        = 6u;
inline constexpr uint32_t kMoonRecordFloat4Stride       = 7u;
inline constexpr uint32_t kPlanetRecordFloat4Stride    = 8u;
inline constexpr uint32_t kCloudRecordFloat4Stride     = 8u;
inline constexpr uint32_t kFogBoxRecordFloat4Stride    = 6u;
inline constexpr uint32_t kLuminaireRecordFloat4Stride = 3u;
inline constexpr uint32_t kLightningRecordFloat4Stride = 3u;
inline constexpr uint32_t kFacetRecordFloat4Stride     = 3u;
inline constexpr uint32_t kSphereRecordFloat4Stride    = 2u;
inline constexpr uint32_t kGlassRecordFloat4Stride     = 3u;
inline constexpr uint32_t kPortalRecordFloat4Stride    = 9u;
inline constexpr uint32_t kFlareSourceFloat4Stride     = 4u;
inline constexpr uint32_t kCustomStarRecordFloat4Stride = 2u;

struct RealmSolarSource
{
    std::array<float, 3> DirectionUnit     = {0.0f, 0.85f, 0.526f};
    std::array<float, 3> DiscIrradiance    = {1.45f, 1.38f, 1.30f};
    SunDiscProfile       Disc{};
    FlareOpticsProfile   Flare{};
    float                MasterFlareGain   = 1.0f;
    float                ChromaticShift    = 0.012f;
    float                HaloRadiusNdc     = 0.42f;
    float                HaloWidthNdc      = 0.025f;
    float                ApertureRoundness = 0.22f;
    bool                 EnableLensFlare   = true;
};

struct RealmSphereObstacle
{
    std::array<float, 3> CenterMeters = {0.0f, 1.4f, 6.0f};
    float                RadiusMeters = 1.2f;
    std::array<float, 3> AlbedoRgb    = {0.42f, 0.44f, 0.48f};
    float                Roughness    = 0.55f;
};

struct RealmGlassPane
{
    std::array<float, 3> CenterMeters      = {0.0f, 2.0f, 4.0f};
    float                HalfWidthMeters   = 1.2f;
    std::array<float, 3> NormalUnit        = {0.0f, 0.0f, -1.0f};
    float                HalfHeightMeters  = 1.2f;
    std::array<float, 3> TintTransmittance = {0.42f, 0.86f, 0.94f};
    float                FresnelF0         = 0.04f;
};

struct PortalAperture
{
    uint32_t             SourceRealmIndex   = 0u;
    uint32_t             TargetRealmIndex   = 1u;
    std::array<float, 3> CenterMeters       = {0.0f, 2.0f, 7.5f};
    std::array<float, 3> NormalUnit         = {0.0f, 0.0f, -1.0f};
    std::array<float, 3> RightUnit          = {1.0f, 0.0f, 0.0f};
    std::array<float, 3> UpUnit             = {0.0f, 1.0f, 0.0f};
    float                HalfWidthMeters    = 2.2f;
    float                HalfHeightMeters   = 1.75f;
    float                FrameWidthMeters   = 0.14f;
    std::array<float, 3> FrameAlbedoRgb     = {0.72f, 0.58f, 0.28f};
    std::array<float, 3> TargetOriginMeters = {0.0f, 2.0f, 0.0f};
    std::array<float, 3> TargetRightUnit    = {1.0f, 0.0f, 0.0f};
    std::array<float, 3> TargetUpUnit       = {0.0f, 1.0f, 0.0f};
    std::array<float, 3> TargetForwardUnit  = {0.0f, 0.0f, 1.0f};
};

struct PlanetaryRealm
{
    std::string                 Name                    = "DefaultRealm";
    uint64_t                    AtmosphereRevision      = 1u;
    uint64_t                    SkyViewRevision         = 1u;
    AtmosphereParameters        Atmosphere{};
    float                       ObserverAltitudeKm      = 0.05f;
    float                       GroundPlaneY            = 0.0f;
    std::array<float, 3>        GroundAlbedoRgb         = {0.22f, 0.23f, 0.20f};
    float                       TwilightStrength        = 1.15f;
    float                       StarBrightnessScale     = 1.0f;
    float                       ScintillationStrength   = 0.35f;   // [0-1] the twinkle's peak brightness swing at the zenith
    float                       MilkyWayIntensity       = 1.0f;
    float                       StarPointSize           = 1.0f;
    float                       StarTwinkleClock        = 0.0f;    // [s] the twinkle signal's clock: elapsed time × the twinkle rate
    float                       StarHalo                = 0.0f;
    float                       StarLimitingMagnitude   = 7.0f;
    std::array<float, 3>        StarBasisRow0           = {1.0f, 0.0f, 0.0f};
    std::array<float, 3>        StarBasisRow1           = {0.0f, 1.0f, 0.0f};
    FogParameters               HeightFog{0.0f, 0.0f, 120.0f, {0.65, 0.72, 0.82}};
    float                       HeightFogPhaseG         = 0.52f;
    float                       HeightFogScatterAlbedo  = 0.88f;
    bool                        EnableAerialPerspective = true;
    float                       CloudShadowHalfSpanM    = 1200.0f;
    float                       CloudWindSeconds        = 0.0f;
    ForceFieldSpace             ForceField{};
    uint32_t AtmosphereSamples = 40;
    bool MultipleScattering = true;

    std::vector<RealmSolarSource>        Suns{};
    std::vector<LunarBodyDescriptor>     Moons{};
    std::vector<PlanetaryBodyDescriptor> Planets{};
    std::vector<CustomStarDescriptor>    CustomStars{};
    std::vector<CloudVolumeProfile>      CloudVolumes{};
    std::vector<LocalFogVolumeProfile>   LocalFogVolumes{};
    std::vector<PhotometricFogLuminaire> Luminaires{};
    std::vector<LightningFogChannel>     LightningChannels{};
    std::vector<RadiantFogFacet>         RadiantFacets{};
    std::vector<RealmSphereObstacle>     SolidOccluders{};
    std::vector<RealmGlassPane>          GlassPanes{};
};

struct PlanetaryCameraView
{
    uint32_t             ActiveRealmIndex = 0u;
    std::array<float, 3> PositionMeters   = {0.0f, 1.8f, 0.0f};
    std::array<float, 3> RightUnit        = {1.0f, 0.0f, 0.0f};
    std::array<float, 3> UpUnit           = {0.0f, 1.0f, 0.0f};
    std::array<float, 3> ForwardUnit      = {0.0f, 0.0f, 1.0f};
    float                VerticalFovRad   = 1.04719755f; // 60 deg
    float                ExposureScale    = 1.0f;
    float                ElapsedSeconds   = 0.0f;
    uint32_t             FrameIndex       = 0u;
    uint32_t             ViewportWidth    = 960u;
    uint32_t             ViewportHeight   = 540u;
    uint32_t             CloudResolutionPercent = 100;
    uint32_t             FogMarchSamples = 36;
};

struct PlanetaryRealmEnsemble
{
    std::vector<PlanetaryRealm> Realms{};
    std::vector<PortalAperture> Portals{};
};

struct RoutedRealmRay
{
    uint32_t             RealmIndex       = 0u;
    int32_t              PortalIndex      = -1;
    bool                 HitPortalFrame   = false;
    float                PortalHitMeters  = 0.0f;
    std::array<float, 3> FrameAlbedoRgb   = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> RayOriginMeters  = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> RayDirectionUnit = {0.0f, 0.0f, 1.0f};
};

[[nodiscard]] RoutedRealmRay RouteRayAcrossPortals(const PlanetaryRealmEnsemble& Ensemble,
                                                   uint32_t                      StartRealmIndex,
                                                   std::array<float, 3>          RayOriginMeters,
                                                   std::array<float, 3>          RayDirectionUnit) noexcept;

[[nodiscard]] bool ProjectDirectionToScreenUv(const PlanetaryCameraView& View,
                                              std::array<float, 3>       WorldDirectionUnit,
                                              std::array<float, 2>&      OutUv) noexcept;

struct FlareVisibilityCandidate
{
    uint32_t             RealmIndex        = 0u;
    uint32_t             SunIndexInRealm   = 0u;
    int32_t              PortalIndex       = -1;
    uint32_t             GlobalSunSlot     = 0u;
    std::array<float, 3> CameraWorldDir    = {0.0f, 0.0f, 1.0f};
    std::array<float, 2> ScreenUv          = {0.5f, 0.5f};
    RealmSolarSource     SourceCopy{};
};

struct PackedEnsemblePayload
{
    std::vector<std::array<float, 4>>     Float4Slots{};
    std::vector<FlareVisibilityCandidate> FlareCandidates{};
};

[[nodiscard]] PackedEnsemblePayload PackEnsemblePayload(const PlanetaryRealmEnsemble& Ensemble,
                                                        const PlanetaryCameraView&    View,
                                                        uint32_t                      HipparcosStarCount);

//------------------------------------------------------------------------------------------------------------------------
//                           END-TO-END MULTI-REALM & PORTAL-READY GPU COORDINATOR
//------------------------------------------------------------------------------------------------------------------------

class PlanetarySubmissionSequence
{
  public:
    bool Initialize(VulkanComputeExchange& Exchange,
                    const std::string&     ShaderBinaryDirectory,
                    const std::string&     LunaSourceJpgPath = "EngineContent/EnvironmentArchives/CelestialMaps/MoonAlbedoSource2048.jpg",
                    const std::string&     HipparcosCsvPath  = "EngineContent/EnvironmentArchives/CelestialMaps/Hipparcos8.csv",
                    std::string*           OutDiagnostic     = nullptr);
    void Shutdown(VulkanComputeExchange& Exchange) noexcept;

    bool ExecuteFrame(VulkanComputeExchange&        Exchange,
                      const PlanetaryRealmEnsemble& Ensemble,
                      const PlanetaryCameraView&    CameraView);

    // 📝 Externally supplied visibility: the caller allocates the targets for the viewport (EnsureViewportTargets), fills all four
    //    VisibilityArray layers itself (another renderer's radiance, reversed depth and rays, on these y-up axes), then every
    //    step except the built-in visibility raster draws the dome, clouds, fog and flare into HdrTarget().
    bool EnsureViewportTargets(VulkanComputeExchange& Exchange, uint32_t ViewportWidth, uint32_t ViewportHeight);
    bool ExecuteOverVisibility(VulkanComputeExchange&        Exchange,
                               const PlanetaryRealmEnsemble& Ensemble,
                               const PlanetaryCameraView&    CameraView);
    // 📝 The first steps of ExecuteOverVisibility on their own — payload, atmosphere LUTs, cloud shadow map — so another renderer
    //    can light its surfaces with them first; the next ExecuteOverVisibility then reuses them instead of repeating them.
    bool ExecuteSkyAndShadows(VulkanComputeExchange&        Exchange,
                              const PlanetaryRealmEnsemble& Ensemble,
                              const PlanetaryCameraView&    CameraView);
    // 📝 Another renderer's sun shadow cascades for the fog's sun shafts; nullptr: shafts shadowed by clouds and spheres only.
    void ConfigureFogSceneShadow(const FogSceneShadow* SceneShadow) noexcept { FogSceneShadow_ = SceneShadow; }
    [[nodiscard]] const DeviceImageArraySlot& HdrTarget() const noexcept { return HdrFrameTarget_; }

    bool ReadbackHdrFrame(VulkanComputeExchange& Exchange, std::vector<float>& OutLinearRgbaFloats);
    bool ReadbackVisibilityLayer(VulkanComputeExchange& Exchange, uint32_t LayerIndex, std::vector<float>& OutRgbaFloats);

    [[nodiscard]] AtmosphereProjection&            AtmosphereLuts() noexcept { return AtmosphereLuts_; }
    [[nodiscard]] const AtmosphereProjection&      AtmosphereLuts() const noexcept { return AtmosphereLuts_; }
    [[nodiscard]] VisibilityProjection&            Visibility() noexcept { return Visibility_; }
    [[nodiscard]] const VisibilityProjection&      Visibility() const noexcept { return Visibility_; }
    [[nodiscard]] PlanetarySkyRasterization&       PlanetaryDome() noexcept { return PlanetaryDome_; }
    [[nodiscard]] const PlanetarySkyRasterization& PlanetaryDome() const noexcept { return PlanetaryDome_; }
    [[nodiscard]] VolumetricClouds&                Clouds() noexcept { return Clouds_; }
    [[nodiscard]] const VolumetricClouds&          Clouds() const noexcept { return Clouds_; }
    [[nodiscard]] FogSpace&                        Fog() noexcept { return Fog_; }
    [[nodiscard]] const FogSpace&                  Fog() const noexcept { return Fog_; }
    [[nodiscard]] OpticalLensFlare&                LensFlare() noexcept { return LensFlare_; }
    [[nodiscard]] const OpticalLensFlare&          LensFlare() const noexcept { return LensFlare_; }
    [[nodiscard]] const PackedEnsemblePayload&     LastPackedPayload() const noexcept { return LastPayload_; }

  private:
    DeviceMemorySpan          EnsembleSpan_{};
    DeviceImageArraySlot      HdrFrameTarget_{};
    AtmosphereProjection      AtmosphereLuts_{};
    VisibilityProjection      Visibility_{};
    PlanetarySkyRasterization PlanetaryDome_{};
    VolumetricClouds          Clouds_{};
    FogSpace                  Fog_{};
    OpticalLensFlare          LensFlare_{};
    PackedEnsemblePayload     LastPayload_{};
    bool                      SkyAndShadowsReady_ = false;     // [-] ExecuteSkyAndShadows ran for the coming overlay
    const FogSceneShadow*     FogSceneShadow_     = nullptr;   // [-] borrowed; outlives the overlay that sets it
};

void TonemapHdrToSrgb8(std::span<const float> LinearRgbaFloats,
                       uint32_t               Width,
                       uint32_t               Height,
                       float                  ExposureScale,
                       std::vector<uint8_t>&  OutSrgbRgbaBytes);

} // namespace Frontier
