/*==========================================================================================================================================
                                                        FOG SPACE
                                     ------------------------------------------------------------------
             Closed-form exponential height fog + planet-curved aerial perspective + bounded 3D
             noise-modulated volumetric fog scattering cloud-shadowed sun shafts, IES, lightning & fire.
==========================================================================================================================================*/
// 🧩 Analytic height fog, planet-curved aerial perspective, local 3D volumetric fog, and GPU FogSpace dispatch.

#pragma once

#include "Engine/DeviceExchange/VulkanComputeExchange.h"
#include "Engine/PlanetaryAtmosphere/AtmosphereScattering.h"
#include "Engine/PlanetaryAtmosphere/CloudSpace.h"
#include "Engine/PlanetaryAtmosphere/AtmosphereProjection.h"
#include "Engine/PlanetaryAtmosphere/VolumetricClouds.h"
#include "Engine/Rendering/ClusterNesting.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace Frontier
{

struct FogParameters
{
    double      ReferenceDensity = 0.02;                // [1/m] σ_t at/below ReferenceHeightM
    double      ReferenceHeightM = 0.0;                 // [m] full density below this height
    double      FalloffHeightM   = 150.0;               // [m] exponential scale height above ReferenceHeightM
    SpectrumRgb Colour           = {0.65, 0.72, 0.82};  // in-scattering medium radiance
};

[[nodiscard]] double FogTransmittance(const FogParameters& Parameters,
                                      SpatialVector        Origin,
                                      SpatialVector        Direction,
                                      double               DistanceM) noexcept;

[[nodiscard]] SpectrumRgb ApplyFog(const FogParameters& Parameters,
                                   SpectrumRgb          Radiance,
                                   double               Transmittance) noexcept;

struct FogRadianceIntegral
{
    SpectrumRgb Radiance{};
    double      Transmittance = 1.0;
};

[[nodiscard]] FogRadianceIntegral IntegrateFogRadiance(
    const FogParameters&                             Medium,
    SpatialVector                                    Origin,
    SpatialVector                                    Direction,
    double                                           DistanceM,
    uint32_t                                         Steps,
    double                                           ScatteringAlbedo,
    const std::function<SpectrumRgb(SpatialVector)>& Incident);

//------------------------------------------------------------------------------------------------------------------------
//              COUPLED HEIGHT FOG, AERIAL PERSPECTIVE & BOUNDED 3D NOISE-MODULATED VOLUMETRIC FOG
//------------------------------------------------------------------------------------------------------------------------

struct LocalFogVolumeProfile
{
    SpatialVector BoxCenterM       = {0.0, 0.0, 20.0};
    SpatialVector BoxHalfExtentsM  = {80.0, 80.0, 25.0};
    double        BoxEdgeFalloffM  = 12.0;
    double        ExtinctionPerM   = 0.035;
    double        ScatteringAlbedo = 0.92;
    double        PhaseAsymmetryG  = 0.58;
    SpectrumRgb   ScatteringTint   = {0.92, 0.96, 1.00};
    double        HeightFalloffM   = 40.0;
    double        NoiseScaleM      = 18.0;
    double        NoiseModulation  = 0.55;
    SpatialVector WindVelocityMps  = {2.0, 0.8, 0.2};
    double        WindTimeS        = 0.0;
    uint64_t      Seed             = 77;
    VolumeShape   BoundsShape = VolumeShape::Box;
    uint32_t      VerticalAxis = 2; // [axis] CPU profile convention; native GPU volumes are converted to Y up
    bool          SelfShadow = false;
    bool          FollowField = false;
    bool          DisplacementReady = false;
    SpatialVector WindDisplacementM{};

};

struct PhotometricFogLuminaire
{
    SpatialVector        PositionM      = {0.0, 0.0, 6.0};
    SpatialVector        AxisUnit       = {0.0, 0.0, -1.0};
    SpectrumRgb          PeakIntensity  = {120.0, 100.0, 80.0};
    double               InnerConeCos   = 0.88;
    double               OuterConeCos   = 0.50;
    double               RangeMeters    = 60.0;
    std::array<float, 8> IesPolarWeights = {1.0f, 0.94f, 0.78f, 0.52f, 0.26f, 0.09f, 0.02f, 0.0f};
};

struct LightningFogChannel
{
    SpatialVector SegmentStartM = {0.0, 0.0, 1200.0};
    SpatialVector SegmentEndM   = {20.0, -15.0, 0.0};
    SpectrumRgb   LineRadiance  = {450.0, 560.0, 950.0};
    double        CoreRadiusM   = 2.5;
    double        ReachMeters   = 350.0;
};

struct RadiantFogFacet
{
    SpatialVector CornerA         = {-1.0, 0.0, 0.0};
    SpatialVector CornerB         = {1.0, 0.0, 0.0};
    SpatialVector CornerC         = {0.0, 0.0, 2.0};
    SpectrumRgb   SurfaceRadiance = {48.0, 16.0, 3.5};
};

struct UnifiedVolumetricFogSpecification
{
    FogParameters                             HeightFog{};
    double                                    HeightFogAlbedo         = 0.88;
    double                                    HeightFogPhaseG         = 0.52;
    AtmosphereParameters                      Atmosphere{};
    const TransmittanceLut*                   Transmittance           = nullptr;
    const MultiScatteringLut*                 MultiScatter            = nullptr;
    double                                    ObserverAltitudeKm      = 0.01;
    bool                                      EnableAerialPerspective = true;
    std::span<const AtmosphereSolarSource>    SolarSources{};
    const CloudShadowProjection*              CloudShadow             = nullptr;
    SpectrumRgb                               SkyAmbientRadiance      = {0.015, 0.020, 0.028};
    std::span<const LocalFogVolumeProfile>    LocalVolumes{};
    std::span<const PhotometricFogLuminaire>  Luminaires{};
    std::span<const LightningFogChannel>      LightningChannels{};
    std::span<const RadiantFogFacet>          RadiantFacets{};
    double                                    MaxFogMarchDistanceM    = 1800.0;
    uint32_t                                  FroxelSteps             = 36;
};

[[nodiscard]] double EvaluateLocalFogExtinction(const LocalFogVolumeProfile& Volume,
                                                SpatialVector                PointMeters) noexcept;

[[nodiscard]] FogRadianceIntegral EvaluateUnifiedVolumetricFog(
    const UnifiedVolumetricFogSpecification&                         Specification,
    SpatialVector                                                    OriginMeters,
    SpatialVector                                                    DirectionUnit,
    double                                                           SurfaceDistanceMeters,
    const std::function<bool(SpatialVector, SpatialVector, double)>& IsOccluded = {}) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                           FOG SHADER INPUT ENCODING
//------------------------------------------------------------------------------------------------------------------------

struct FogRadianceSpecification
{
    FogParameters Medium{};
    double        ScatteringAlbedo       = 0.9;
    double        PhaseAsymmetry         = 0.65;
    double        MaximumViewDistanceM   = 2500.0;
    uint32_t      ViewSamples            = 32;
    uint32_t      CloudShadowSamples     = 32;
    SpectrumRgb   AmbientRadiance        = {0.01, 0.012, 0.015};
    bool          GeometryShadowsEnabled = true;
    bool          CloudShadowsEnabled    = true;
    bool          AnalyticLightsEnabled  = true;
    double        MaximumLightDistanceM  = 40000.0;
    uint32_t      FacetSamples           = 0;
};

inline constexpr std::size_t FogRadianceFloatCount = 16;

[[nodiscard]] std::array<float, FogRadianceFloatCount> CompileFogRadiancePayload(
    const FogRadianceSpecification& Specification);

void ValidateFogRadiancePayload(const float* Payload, std::size_t FloatCount);

//------------------------------------------------------------------------------------------------------------------------
//                                      GPU VOLUMETRIC FOG SPACE SUBMISSION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Another renderer's sun shadow cascades, sampled by the fog for its sun shafts. Frame holds kFogSceneShadowFloat4Count float4:
//    three light view-projection matrices as rows (a z-up world, row vectors), the cascades' far distances and count, their
//    world texel sizes, then control (x enabled, y depth bias); Depth is the cascades' depth array on the same device.
inline constexpr uint32_t kFogSceneShadowFloat4Count = 15u;

struct FogSceneShadow
{
    const DeviceMemorySpan*     Frame = nullptr;
    const DeviceImageArraySlot* Depth = nullptr;
};

class FogSpace
{
  public:
    bool Initialize(VulkanComputeExchange& Exchange,
                    const std::string&     ShaderBinaryDirectory,
                    std::string*           OutDiagnostic = nullptr);
    void Shutdown(VulkanComputeExchange& Exchange) noexcept;

    // 📝 SceneShadow null (or incomplete): shafts shadowed by clouds and occluder spheres only, as before.
    bool Execute(VulkanComputeExchange&      Exchange,
                 const DeviceMemorySpan&     EnsembleSpan,
                 const AtmosphereProjection& AtmosphereLuts,
                 const VolumetricClouds&     Clouds,
                 const DeviceImageArraySlot& VisibilityImage,
                 DeviceImageArraySlot&       HdrFrameTarget,
                 uint32_t                    ViewportWidth,
                 uint32_t                    ViewportHeight,
                 const FogSceneShadow*       SceneShadow = nullptr);

    void ConfigureSamples(uint32_t Samples) noexcept { MarchSamples_ = std::clamp(Samples, 8u, 192u); }
    [[nodiscard]] uint32_t QuerySamples() const noexcept { return MarchSamples_; }

  private:
    uint32_t MarchSamples_ = 36;
    ComputeProgramSlot Program_{};
    DeviceMemorySpan   AbsentSceneShadow_{};   // [-] control x = 0: no scene cascades
};

} // namespace Frontier
