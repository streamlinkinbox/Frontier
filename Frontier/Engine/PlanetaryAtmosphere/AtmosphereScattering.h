/*==========================================================================================================================================
                                                   ATMOSPHERE SCATTERING
                                     ------------------------------------------------------------------
                 Rayleigh/Mie/ozone medium, transmittance integration + LUTs, phase functions,
                  multi-sun coupled sky-view, aerial perspective, and 6-point Gauss-Legendre sky.
==========================================================================================================================================*/
// 🧩 Physical planetary atmosphere scattering: Bruneton & Neyret (2008) / Hillaire (2020) Rayleigh,
//    Cornette-Shanks Mie aerosol, Chappuis ozone tent, second-order isotropic multiple scattering Ψ_ms,
//    twilight arch + Belt-of-Venus glow, finite-distance curved aerial perspective, and coupled
//    6-point Gauss-Legendre solar/sky NEE quadrature.

#pragma once

#include "Engine/Rendering/ClusterNesting.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier
{

// RGB triplet in linear radiance / reflectance / optical-depth space.
struct SpectrumRgb
{
    double R = 0.0;
    double G = 0.0;
    double B = 0.0;
};

struct AtmosphereParameters
{
    double      GroundRadiusKm        = 6360.0;                           // [km]
    double      TopRadiusKm           = 6460.0;                           // [km]
    SpectrumRgb RayleighScattering{5.802e-3, 13.558e-3, 33.1e-3};         // [1/km] at sea level
    double      RayleighScaleHeightKm = 8.0;                              // [km]
    double      MieScattering         = 3.996e-3;                         // [1/km]
    double      MieAbsorption         = 0.404e-3;                         // [1/km] 4.40e-3 total extinction
    double      MieScaleHeightKm      = 1.2;                              // [km]
    double      MiePhaseG             = 0.8;                              // [-] Cornette-Shanks asymmetry 𝑔
    SpectrumRgb OzoneAbsorption{0.65e-3, 1.881e-3, 0.085e-3};             // [1/km] at layer peak
    double      OzonePeakKm           = 25.0;                             // [km]
    double      OzoneWidthKm          = 15.0;                             // [km] tent half-width
    double      GroundAlbedo          = 0.1;                              // [-] spherical-albedo ground bounce
};

[[nodiscard]] inline AtmosphereParameters MakeEarthAtmosphere() noexcept
{
    return AtmosphereParameters{};
}

// Dimensionless vertical density profiles (1.0 at sea level for Rayleigh/Mie; tent peaking at 1.0 for ozone).
[[nodiscard]] double RayleighDensity(const AtmosphereParameters& Parameters, double AltitudeKm) noexcept;
[[nodiscard]] double MieDensity(const AtmosphereParameters& Parameters, double AltitudeKm) noexcept;
[[nodiscard]] double OzoneDensity(const AtmosphereParameters& Parameters, double AltitudeKm) noexcept;

// Sphere-normalized scattering phase functions (integral over 4π sr equals 1).
[[nodiscard]] double RayleighPhase(double cosθ) noexcept;
[[nodiscard]] double HenyeyGreensteinPhase(double cosθ, double 𝑔) noexcept;
[[nodiscard]] double CornetteShanksPhase(double cosθ, double 𝑔) noexcept;

// Transmittance between two planet-centred coordinates [km] by trapezoidal integration.
[[nodiscard]] SpectrumRgb TransmittanceBetween(const AtmosphereParameters& Parameters,
                                               SpatialVector               PointAKm,
                                               SpatialVector               PointBKm,
                                               uint32_t                    Steps = 64) noexcept;

// Transmittance from RadiusKm along zenith cosine μ to top of atmosphere (or planetary ground hit).
[[nodiscard]] SpectrumRgb TransmittanceToTop(const AtmosphereParameters& Parameters,
                                             double                      RadiusKm,
                                             double                      μ,
                                             uint32_t                    Steps = 64) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRANSMITTANCE LUT
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t kTransmittanceLutWidth  = 256u;
inline constexpr uint32_t kTransmittanceLutHeight = 64u;
inline constexpr uint32_t kMultiScatteringLutDim  = 32u;
inline constexpr uint32_t kSkyViewLutWidth        = 192u;
inline constexpr uint32_t kSkyViewLutHeight       = 108u;

// Tabulated transmittance over (altitude, zenith cosine μ) with limb-compressed u = 0.5 + 0.5·sign(μ)·|μ|^0.35.
class TransmittanceLut
{
  public:
    bool Bake(const AtmosphereParameters& Parameters,
              uint32_t                    WidthMu          = 128,
              uint32_t                    HeightAltitude   = 32,
              uint32_t                    IntegrationSteps = 48);

    [[nodiscard]] SpectrumRgb Sample(const AtmosphereParameters& Parameters, double RadiusKm, double μ) const noexcept;
    [[nodiscard]] bool        Baked() const noexcept { return !Texels_.empty(); }
    [[nodiscard]] uint32_t    Width() const noexcept { return WidthMu_; }
    [[nodiscard]] uint32_t    Height() const noexcept { return HeightAltitude_; }
    [[nodiscard]] const std::vector<SpectrumRgb>& Texels() const noexcept { return Texels_; }

  private:
    std::vector<SpectrumRgb> Texels_{};
    uint32_t                 WidthMu_        = 0;
    uint32_t                 HeightAltitude_ = 0;
};

//------------------------------------------------------------------------------------------------------------------------
//                                      MULTI-SCATTERING & SKY-VIEW LUTS (MULTI-SUN)
//------------------------------------------------------------------------------------------------------------------------

struct AtmosphereSolarSource
{
    SpatialVector Direction        = {0.0, 0.70710678, 0.70710678};   // [-] unit vector in local planet frame (Z = up)
    SpectrumRgb   Irradiance       = {18.5, 18.0, 17.2};              // [W/m^2] top-of-atmosphere solar beam
    double        TwilightStrength = 1.0;                             // [-] civil/nautical twilight arch gain
};

// Isotropic second-order Hillaire multiple-scattering transfer table Ψ_ms(μSolar, altitude), default 32×32.
class MultiScatteringLut
{
  public:
    bool Bake(const AtmosphereParameters& Parameters,
              const TransmittanceLut&     Transmittance,
              uint32_t                    WidthSunCos    = 32,
              uint32_t                    HeightAltitude = 32,
              uint32_t                    DirectionCount = 16);

    [[nodiscard]] SpectrumRgb Sample(const AtmosphereParameters& Parameters, double RadiusKm, double μSolar) const noexcept;
    [[nodiscard]] bool        Baked() const noexcept { return !Texels_.empty(); }
    [[nodiscard]] uint32_t    Width() const noexcept { return WidthSunCos_; }
    [[nodiscard]] uint32_t    Height() const noexcept { return HeightAltitude_; }
    [[nodiscard]] const std::vector<SpectrumRgb>& Texels() const noexcept { return Texels_; }

  private:
    std::vector<SpectrumRgb> Texels_{};
    uint32_t                 WidthSunCos_    = 0;
    uint32_t                 HeightAltitude_ = 0;
};

// Limb-compressed sky-view LUT (default 192×108) accumulating arbitrary N solar sources + twilight glow.
class SkyViewLut
{
  public:
    bool Bake(const AtmosphereParameters&            Parameters,
              const TransmittanceLut&                Transmittance,
              const MultiScatteringLut&              MultiScatter,
              double                                 ObserverAltitudeKm,
              std::span<const AtmosphereSolarSource> Suns,
              uint32_t                               WidthAzimuth    = 192,
              uint32_t                               HeightElevation = 108,
              uint32_t                               MarchSteps      = 28);

    [[nodiscard]] SpectrumRgb Sample(SpatialVector LocalDirectionUnit) const noexcept;
    [[nodiscard]] bool        Baked() const noexcept { return !Texels_.empty(); }
    [[nodiscard]] uint32_t    Width() const noexcept { return WidthAzimuth_; }
    [[nodiscard]] uint32_t    Height() const noexcept { return HeightElevation_; }
    [[nodiscard]] const std::vector<SpectrumRgb>& Texels() const noexcept { return Texels_; }

  private:
    std::vector<SpectrumRgb> Texels_{};
    uint32_t                 WidthAzimuth_    = 0;
    uint32_t                 HeightElevation_ = 0;
};

// Analytic Chappuis twilight arch & Belt-of-Venus ozone scattering contribution along a local direction (Z = up).
[[nodiscard]] SpectrumRgb EvaluateTwilightArchRadiance(SpatialVector      LocalDirectionUnit,
                                                       SpatialVector      LocalSunDirectionUnit,
                                                       const SpectrumRgb& SunIrradiance,
                                                       double             TwilightStrength = 1.0) noexcept;

struct AerialPerspectiveSample
{
    SpectrumRgb Inscatter{};
    SpectrumRgb Transmittance{1.0, 1.0, 1.0};
};

// Finite-distance planet-curved aerial perspective between observer and a surface receiver in local metres (Z = up).
[[nodiscard]] AerialPerspectiveSample EvaluateAerialPerspective(
    const AtmosphereParameters&            Parameters,
    const TransmittanceLut*                Transmittance,
    const MultiScatteringLut*              MultiScatter,
    double                                 ObserverAltitudeKm,
    SpatialVector                          OriginLocalMeters,
    SpatialVector                          DirectionLocalUnit,
    double                                 SurfaceDistanceMeters,
    std::span<const AtmosphereSolarSource> Suns,
    uint32_t                               Steps = 16) noexcept;

// Single-scattered sky radiance along a planet-centred view ray [km].
[[nodiscard]] SpectrumRgb SingleScattering(const AtmosphereParameters& Parameters,
                                           SpatialVector               PositionKm,
                                           SpatialVector               Direction,
                                           SpatialVector               SunDirection,
                                           const SpectrumRgb&          SunIrradiance,
                                           const TransmittanceLut*     Lut   = nullptr,
                                           uint32_t                    Steps = 32) noexcept;

// Isotropic second-order multiple-scattering spherical-albedo approximation.
[[nodiscard]] SpectrumRgb MultipleScatteringApprox(const AtmosphereParameters& Parameters,
                                                   SpatialVector               PositionKm,
                                                   SpatialVector               Direction,
                                                   SpatialVector               SunDirection,
                                                   const SpectrumRgb&          SingleScattered,
                                                   const SpectrumRgb&          SunIrradiance) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                          COUPLED SOLAR/SKY SPECIFICATION & 6-POINT GAUSS-LEGENDRE QUADRATURE
//------------------------------------------------------------------------------------------------------------------------

inline constexpr std::uint32_t SolarSkyPayloadFloats = 20u;

struct SolarSkySourceProfile
{
    SpatialVector SolarDirection     = {0.32, 0.86, 0.39799497};   // [-] unit vector toward solar disc centre
    SpectrumRgb   SolarIrradiance    = {18.5, 18.0, 17.2};         // [W/m^2] top-of-atmosphere solar beam
    double        SolarAngularRadius = 0.00465421;                 // [rad] apparent solar half-angle (~0.2667 deg)
    double        LimbDarkeningU1    = 0.397;                      // [-] linear limb-darkening coefficient
    double        LimbDarkeningU2    = 0.196;                      // [-] quadratic limb-darkening coefficient
};

struct SolarSkySpecification
{
    AtmosphereParameters               Atmosphere           = {};
    SpatialVector                      LocalUpDirection     = {0.0, 0.0, 1.0};
    double                             ObserverAltitudeKm   = 0.05;
    double                             MultipleScatterScale = 1.0;
    double                             CloudBottomKm        = 1.6;
    double                             CloudTopKm           = 2.4;
    double                             CloudExtinctionKm    = 0.0;
    double                             CloudCoverage        = 0.0;
    std::vector<SolarSkySourceProfile> SolarSources         = {SolarSkySourceProfile{}};
};

[[nodiscard]] std::array<float, SolarSkyPayloadFloats> CompileSolarSkyPayload(
    const SolarSkySpecification& Specification,
    std::size_t                  SourceIndex = 0u);

void ValidateSolarSkyPayload(std::span<const float> Payload);

[[nodiscard]] double EvaluateSolarSkyCloudTransmittance(const SolarSkySpecification& Specification,
                                                        SpatialVector                OriginMeters,
                                                        SpatialVector                Direction) noexcept;

[[nodiscard]] SpectrumRgb EvaluateSolarSkyOracleContinuum(const SolarSkySpecification& Specification,
                                                          SpatialVector                OriginMeters,
                                                          SpatialVector                Direction,
                                                          std::uint32_t                Steps = 128u) noexcept;

[[nodiscard]] SpectrumRgb EvaluateSolarSkyGaussLegendre6Pt(const SolarSkySpecification& Specification,
                                                           SpatialVector                OriginMeters,
                                                           SpatialVector                Direction) noexcept;

[[nodiscard]] SpectrumRgb EvaluateSolarSkyDiscRadiance(const SolarSkySpecification& Specification,
                                                       SpatialVector                OriginMeters,
                                                       SpatialVector                Direction,
                                                       double*                      OutPrimarySolidAnglePdf = nullptr,
                                                       std::uint32_t                Steps                   = 48u) noexcept;

[[nodiscard]] SpectrumRgb EvaluateSolarSkyTotalRadiance(const SolarSkySpecification& Specification,
                                                        SpatialVector                OriginMeters,
                                                        SpatialVector                Direction) noexcept;

[[nodiscard]] SpectrumRgb IntegrateSolarSkyDiscIrradiance(const SolarSkySpecification& Specification,
                                                          SpatialVector                OriginMeters,
                                                          SpatialVector                SurfaceNormal,
                                                          std::uint32_t                RadialRings    = 32u,
                                                          std::uint32_t                AzimuthSectors = 64u) noexcept;

} // namespace Frontier
