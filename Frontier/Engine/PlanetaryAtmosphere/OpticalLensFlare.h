/*==========================================================================================================================================
                                                   OPTICAL LENS FLARE
                                     ------------------------------------------------------------------
             Solar limb-darkening & finite-pixel filter, 6-element / 8-element aperture-ghost chain,
             diffraction starburst, warm & anamorphic streaks, antipode halo, PSF glow, veiling glare,
                    and GPU 1x1-per-source solar disc visibility reduction + aperture blend.
==========================================================================================================================================*/
// 🧩 Optical lens flare & solar disc filter: host analytical optics + Vulkan 1.3 GPU visibility & aperture blend.

#pragma once

#include "Engine/DeviceExchange/VulkanComputeExchange.h"
#include "Engine/PlanetaryAtmosphere/AtmosphereScattering.h"
#include "Engine/PlanetaryAtmosphere/VolumetricClouds.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

struct FlareGhost
{
    double PositionX = 0.0;   // [NDC]
    double PositionY = 0.0;   // [NDC]
    double RadiusNdc = 0.0;   // [NDC] of frame height
    double Strength  = 0.0;   // [-] relative, includes visibility
};

struct FlareGhostSpec
{
    double AxisFraction;
    double RadiusNdc;
    double Strength;
};

inline constexpr std::array<FlareGhostSpec, 6> DefaultFlareGhosts{{
    {0.25, 0.06, 0.50},
    {0.55, 0.13, 0.30},
    {0.90, 0.04, 0.65},
    {1.25, 0.20, 0.18},
    {1.70, 0.08, 0.40},
    {2.30, 0.30, 0.10},
}};

[[nodiscard]] inline std::array<FlareGhost, 6> LayoutFlareGhosts(double SunX, double SunY, double Visibility) noexcept
{
    std::array<FlareGhost, 6> Out{};
    const double ClampedV = std::clamp(Visibility, 0.0, 1.0);
    for (uint32_t Index = 0; Index < Out.size(); ++Index)
    {
        const FlareGhostSpec& Spec = DefaultFlareGhosts[Index];
        Out[Index] = {-SunX * Spec.AxisFraction, -SunY * Spec.AxisFraction, Spec.RadiusNdc, Spec.Strength * ClampedV};
    }
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      SUN DISC
//------------------------------------------------------------------------------------------------------------------------

struct SunDiscProfile
{
    double AngularRadiusRad = 0.0046524;   // [rad] 0.2666 deg mean apparent solar semi-diameter
    double LimbLinear       = 0.38;        // [-] u1
    double LimbQuadratic    = 0.24;        // [-] u2
    double EdgeBlurRad      = 0.00030;     // [rad] σ of point-spread Gaussian at limb
};

[[nodiscard]] inline double SunDiscSolidAngleSr(double AngularRadiusRad) noexcept
{
    return 2.0 * 3.14159265358979323846 * (1.0 - std::cos(AngularRadiusRad));
}

[[nodiscard]] inline double SunDiscMeanLimbFactor(const SunDiscProfile& Profile) noexcept
{
    return 1.0 - Profile.LimbLinear / 3.0 - Profile.LimbQuadratic / 6.0;
}

[[nodiscard]] inline SpectrumRgb EvaluateSunDiscRadiance(double                θAngleRad,
                                                         const SunDiscProfile& Profile,
                                                         const SpectrumRgb&    CentreRadiance) noexcept
{
    constexpr double RootTwo = 1.4142135623730951;
    const double     ρ       = std::sin(θAngleRad) / std::sin(Profile.AngularRadiusRad);
    const double     μ       = ρ < 1.0 ? std::sqrt(1.0 - ρ * ρ) : 0.0;
    const double     Limb    = 1.0 - Profile.LimbLinear * (1.0 - μ) - Profile.LimbQuadratic * (1.0 - μ) * (1.0 - μ);
    const double     σ       = Profile.EdgeBlurRad > 0.0 ? Profile.EdgeBlurRad : 1.0e-9;
    const double     Edge    = 0.5 * std::erfc((θAngleRad - Profile.AngularRadiusRad) / (RootTwo * σ));
    const double     Scale   = Limb * Edge;
    return {CentreRadiance.R * Scale, CentreRadiance.G * Scale, CentreRadiance.B * Scale};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FLARE OPTICS
//------------------------------------------------------------------------------------------------------------------------

struct FlareOpticsProfile
{
    uint32_t ApertureBlades         = 6;
    double   ApertureRotationRad    = 0.0;
    double   GhostStrength          = 1.0;
    double   StarburstStrength      = 1.0;
    double   StarburstLengthNdc     = 0.55;
    double   AnamorphicStrength     = 1.0;
    double   AnamorphicAngleRad     = 0.0;
    double   AnamorphicLengthNdc    = 1.35;
    double   AnamorphicThicknessNdc = 0.006;
    double   StreakStrength         = 0.0;
    double   StreakAngleRad         = 0.5;
    double   StreakSpread           = 1.0;
    double   GlowStrength           = 1.0;
    double   HaloStrength           = 1.0;
    double   VeilingStrength        = 1.0;
};

struct FlareGhostAppearance
{
    SpectrumRgb Tint;
    double      RingFraction;
};

inline constexpr std::array<FlareGhostAppearance, 6> DefaultFlareGhostAppearances{{
    {{1.00, 0.74, 0.48}, 0.0},
    {{0.46, 0.96, 0.62}, 0.82},
    {{0.56, 0.66, 1.00}, 0.0},
    {{1.00, 0.52, 0.58}, 0.0},
    {{0.86, 0.96, 0.52}, 0.78},
    {{0.52, 0.90, 1.00}, 0.0},
}};

[[nodiscard]] SpectrumRgb EvaluateFlareGhostRadiance(double                    PixelX,
                                                     double                    PixelY,
                                                     double                    SunX,
                                                     double                    SunY,
                                                     const SpectrumRgb&        DiscIrradiance,
                                                     double                    Visibility,
                                                     const FlareOpticsProfile& Profile,
                                                     int                       OnlyGhost = -1) noexcept;

[[nodiscard]] SpectrumRgb EvaluateDiffractionStarburst(double                    PixelX,
                                                       double                    PixelY,
                                                       double                    SunX,
                                                       double                    SunY,
                                                       const SpectrumRgb&        DiscIrradiance,
                                                       double                    Visibility,
                                                       const FlareOpticsProfile& Profile) noexcept;

[[nodiscard]] SpectrumRgb EvaluateAnamorphicStreak(double                    PixelX,
                                                   double                    PixelY,
                                                   double                    SunX,
                                                   double                    SunY,
                                                   const SpectrumRgb&        DiscIrradiance,
                                                   double                    Visibility,
                                                   const FlareOpticsProfile& Profile) noexcept;

[[nodiscard]] SpectrumRgb EvaluateWarmStreak(double                    PixelX,
                                             double                    PixelY,
                                             double                    SunX,
                                             double                    SunY,
                                             const SpectrumRgb&        DiscIrradiance,
                                             double                    Visibility,
                                             const FlareOpticsProfile& Profile) noexcept;

[[nodiscard]] SpectrumRgb EvaluateLensFlareRadiance(double                    PixelX,
                                                    double                    PixelY,
                                                    double                    SunX,
                                                    double                    SunY,
                                                    const SpectrumRgb&        DiscIrradiance,
                                                    double                    Visibility,
                                                    const FlareOpticsProfile& Profile) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                               SOLAR DISC PIXEL FILTER
//------------------------------------------------------------------------------------------------------------------------

class SolarDiscPixelFilter
{
  public:
    explicit SolarDiscPixelFilter(SunDiscProfile Profile) noexcept;

    [[nodiscard]] SpectrumRgb Evaluate(double             Dx,
                                       double             Dy,
                                       double             PixelWidth,
                                       double             PixelHeight,
                                       const SpectrumRgb& Irradiance) const noexcept;

  private:
    static constexpr int Rings = 32, Sectors = 48;
    struct SolarSourceSample
    {
        double X      = 0.0;
        double Y      = 0.0;
        double Weight = 0.0;
    };
    SunDiscProfile                                 DiscProfile_;
    bool                                           ProfileValid_       = false;
    double                                         ResolvedCorrection_ = 1.0;
    std::array<SolarSourceSample, Rings * Sectors> SourceSamples_{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                             SOLAR OPTICS SHADER PAYLOAD
//------------------------------------------------------------------------------------------------------------------------

struct SolarOpticsParameters
{
    SpatialVector      SunDirection{0, 0, 1};
    SpectrumRgb        DiscIrradiance{1, 1, 1};
    SunDiscProfile     Disc{};
    FlareOpticsProfile Flare{};
    double             CloudTransmission = 1.0;
    double             PlanetLimbZ       = -1.0;
    bool               DiscEnabled       = true;
};

inline constexpr uint32_t SolarSourceRings        = 32;
inline constexpr uint32_t SolarSourceSectors      = 48;
inline constexpr uint32_t SolarSourceCount        = SolarSourceRings * SolarSourceSectors;
inline constexpr uint32_t SolarOpticsHeaderFloats = 32;
inline constexpr uint32_t SolarOpticsPayloadFloats = SolarOpticsHeaderFloats + 4 * SolarSourceCount;

struct SolarSensorCalibration
{
    double FocalLengthMm         = 50.0;
    double PixelPitchMicrometres = 4.0;
    double GaussianFwhmPixels    = 2.0;
};

[[nodiscard]] double             CalibrateSolarBlur(const SolarSensorCalibration& Calibration);
[[nodiscard]] std::vector<float> CompileSolarOpticsPayload(const SolarOpticsParameters& Parameters);

//------------------------------------------------------------------------------------------------------------------------
//                                         GPU OPTICAL LENS FLARE SUBMISSION
//------------------------------------------------------------------------------------------------------------------------

class OpticalLensFlare
{
  public:
    bool Initialize(VulkanComputeExchange& Exchange,
                    const std::string&     ShaderBinaryDirectory,
                    std::string*           OutDiagnostic = nullptr);
    void Shutdown(VulkanComputeExchange& Exchange) noexcept;

    bool Execute(VulkanComputeExchange&      Exchange,
                 const DeviceMemorySpan&     EnsembleSpan,
                 const DeviceImageArraySlot& VisibilityImage,
                 const VolumetricClouds&     Clouds,
                 DeviceImageArraySlot&       HdrFrameTarget,
                 uint32_t                    FlareSourceCount,
                 uint32_t                    ViewportWidth,
                 uint32_t                    ViewportHeight);

    [[nodiscard]] const DeviceImageArraySlot& FlareVisibilityImage() const noexcept { return FlareVisibilityImage_; }

    bool ReadbackVisibilityValues(VulkanComputeExchange&             Exchange,
                                  uint32_t                           FlareSourceCount,
                                  std::vector<std::array<float, 4>>& OutVisibilities);

  private:
    ComputeProgramSlot   VisibilityProgram_{};
    ComputeProgramSlot   ApertureProgram_{};
    DeviceImageArraySlot FlareVisibilityImage_{};
};

} // namespace Frontier
