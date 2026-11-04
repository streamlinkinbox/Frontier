/*==========================================================================================================================================
                                                       CLOUD SPACE
                                     ------------------------------------------------------------------
             Non-tiled 3D procedural cloud synthesis (uint32 bit-mix hash, C2 quintic fade, rotated
             octaves, Worley cellular erosion, 6 tropospheric profiles), Beers-Powder scattering,
                        and wide-span orthographic CloudShadowProjection.
==========================================================================================================================================*/
// 🧩 Continuous 3D world-space volumetric cloud field & shadow projection (zero 2D/3D tiled textures).

#pragma once

#include "Engine/PlanetaryAtmosphere/AtmosphereScattering.h"
#include "Engine/Rendering/ClusterNesting.h"
#include "Engine/PlanetaryAtmosphere/VolumeBounds.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier
{

struct CloudParameters
{
    double      LowerHeightM           = 800.0;   // [m] slab lower altitude
    double      TopHeightM             = 2200.0;  // [m] slab top altitude
    double      Coverage               = 0.45;    // [0..1]
    double      DensityScale           = 1.0;
    double      ExtinctionPerM         = 0.04;    // [1/m] σ_t at unit density
    double      PhaseG                 = 0.6;     // Henyey-Greenstein forward asymmetry 𝑔
    SpectrumRgb AmbientRadiance        = {0.15, 0.18, 0.24};
    double      WindXMps               = 8.0;
    double      WindYMps               = 3.0;
    double      WindTimeS              = 0.0;
    uint64_t    Seed                   = 1;
    double      UniformDensityOverride = -1.0;    // >= 0 bypasses noise for analytical optical-depth verification
};

struct CloudMarchResult
{
    SpectrumRgb Radiance{};
    double      Transmittance = 1.0;
};

[[nodiscard]] double CloudDensity(const CloudParameters& Parameters, SpatialVector Point) noexcept;

[[nodiscard]] CloudMarchResult MarchClouds(const CloudParameters& Parameters,
                                           SpatialVector          Origin,
                                           SpatialVector          Direction,
                                           SpatialVector          SunDirection,
                                           const SpectrumRgb&     SunRadiance,
                                           double                 MaxDistanceM = 20000.0,
                                           uint32_t               Steps        = 48,
                                           uint32_t               SunSteps     = 6) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                      MULTI-VOLUME 3D CLOUD SYNTHESIS, BEERS-POWDER & ORTHO SHADOW PROJECTION
//------------------------------------------------------------------------------------------------------------------------

enum class CloudVolumeDomain : uint8_t
{
    PlanetarySlab   = 0,
    BoundedLocalBox = 1,
};

enum class TroposphericCloudProfile : uint8_t
{
    Stratus       = 0,
    Stratocumulus = 1,
    Cumulus       = 2,
    Cumulonimbus  = 3,
    Altostratus   = 4,
    Cirrus        = 5,
};

struct CloudVolumeProfile
{
    CloudVolumeDomain        Domain                = CloudVolumeDomain::PlanetarySlab;
    TroposphericCloudProfile HeightProfile         = TroposphericCloudProfile::Cumulus;
    CloudParameters          Slab{};
    SpatialVector            BoxCenterM            = {0.0, 0.0, 1200.0};
    SpatialVector            BoxHalfExtentsM       = {600.0, 600.0, 260.0};
    double                   BoxEdgeFalloffM       = 90.0;
    double                   WorleyErosionStrength = 0.35;
    double                   BackscatterPhaseG     = -0.28;
    double                   PowderStrength        = 0.85;
    double                   MultiScatterStrength  = 0.55;
    double                   SilverLiningWeight    = 0.2;     // [0..0.8] share of single scattering in the g 0.9 forward lobe
    VolumeShape              BoundsShape          = VolumeShape::Box;
    double                   FeatureScaleMetres   = 0.0;   // [m] 0 retains the existing procedural frequency
    double                   AnvilShaping         = 0.5;
    double                   ScatteringAlbedo      = 0.97;
    uint32_t                 ViewSamples           = 32;
    uint32_t                 ShadowSamples         = 4;
    bool                     FollowField           = false;
    bool                     DisplacementReady     = false;
    SpatialVector            WindDisplacementM{};           // [m] integrated density-domain offset, never a bounds translation

};

struct CloudSolarBeam
{
    SpatialVector Direction = {0.0, 0.0, 1.0};
    SpectrumRgb   Radiance  = {1.0, 1.0, 1.0};
};

struct VolumetricCloudSample
{
    SpectrumRgb Radiance{};
    double      Transmittance    = 1.0;
    double      MeanHitDistanceM = 0.0;
};

[[nodiscard]] double EvaluateCloudVolumeDensity(const CloudVolumeProfile& Volume, SpatialVector PointMeters) noexcept;

[[nodiscard]] double EvaluateBeersPowderEnergy(double τOpticalDepth,
                                               double cosθScattering,
                                               double 𝑔Forward,
                                               double 𝑔Backscatter,
                                               double PowderStrength,
                                               double MultiScatterStrength) noexcept;

[[nodiscard]] VolumetricCloudSample MarchCloudVolumes(std::span<const CloudVolumeProfile> Volumes,
                                                      SpatialVector                       OriginMeters,
                                                      SpatialVector                       DirectionUnit,
                                                      std::span<const CloudSolarBeam>     SolarBeams,
                                                      const SpectrumRgb&                  SkyAmbientRadiance,
                                                      double                              MaxDistanceM,
                                                      uint32_t                            Steps       = 48,
                                                      uint32_t                            ShadowSteps = 6) noexcept;

class CloudShadowProjection
{
  public:
    bool Bake(std::span<const CloudVolumeProfile> Volumes,
              SpatialVector                       AnchorMeters,
              SpatialVector                       SolarDirectionUnit,
              double                              HalfSpanMeters = 1200.0,
              uint32_t                            Resolution     = 64,
              uint32_t                            MarchSteps     = 16);

    [[nodiscard]] bool     Baked() const noexcept { return !OpticalDepthTexels_.empty(); }
    [[nodiscard]] uint32_t Resolution() const noexcept { return Resolution_; }
    [[nodiscard]] double   HalfSpanMeters() const noexcept { return HalfSpanM_; }

    [[nodiscard]] double SampleOpticalDepth(SpatialVector WorldPointMeters) const noexcept;
    [[nodiscard]] double SampleTransmittance(SpatialVector WorldPointMeters) const noexcept;

  private:
    uint32_t           Resolution_     = 0;
    double             HalfSpanM_      = 1200.0;
    SpatialVector      AnchorM_        = {};
    SpatialVector      SolarDirection_ = {0.0, 0.0, 1.0};
    SpatialVector      OrthoAxisU_     = {1.0, 0.0, 0.0};
    SpatialVector      OrthoAxisV_     = {0.0, 1.0, 0.0};
    std::vector<float> OpticalDepthTexels_{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                           CLOUD SHADER INPUT ENCODING
//------------------------------------------------------------------------------------------------------------------------

struct CloudRadianceSpecification
{
    CloudParameters Medium{};
    SpatialVector   SolarDirection{0, 0, 1};
    SpectrumRgb     SolarRadiance{};
    double          ObserverHeightM    = 0.0;
    double          MaximumDistanceM   = 40000.0;
    uint32_t        ViewSamples        = 64;
    uint32_t        SolarShadowSamples = 6;
};

inline constexpr std::size_t CloudRadianceFloatCount = 28;

[[nodiscard]] std::array<float, CloudRadianceFloatCount> CompileCloudRadiancePayload(
    const CloudRadianceSpecification& Specification);

void ValidateCloudRadiancePayload(const float* Payload, std::size_t FloatCount);

} // namespace Frontier
