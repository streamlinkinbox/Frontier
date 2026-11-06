/*==========================================================================================================================================
                                                         ENVIRONMENTENCODING.H
==========================================================================================================================================*/
// 🧩 Refresh scalars 128..267 (EnvironmentSettings.js EncodeEnvironment), the six-body moon roster (CelestialAtlas.js) and the
//    low-precision orbital solver for planets and sidereal time (CelestialSolver.js), ported 1:1 from the VehicleHMI demo.

#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Frontier::GlobalIllumination
{

using Vector3 = std::array<double, 3>;

[[nodiscard]] inline double ComputeHypot(double X, double Y) noexcept { return std::hypot(X, Y); }
[[nodiscard]] inline double StoreSingle(double Value) noexcept { return static_cast<double>(static_cast<float>(Value)); }

struct AtmosphereSettings
{
    bool    AirMass            = true;
    double  SunTemperature     = 5778.0;
    bool    SunColourIsRgb     = false;
    Vector3 SunTint{ 1.0, 1.0, 1.0 };
    bool    SunEnabled         = true;
    double  MoonElevation      = 38.0;
    double  MoonAzimuth        = 0.0;
    bool    MoonLightEnabled   = true;
    double  MoonLightIntensity = 1.0;
    bool    MoonEnabled        = true;
    double  MoonBrightness     = 1.0;
    double  MoonPhase          = 14.765;
    double  Rayleigh           = 1.0;
    double  Mie                = 1.0;
    double  Ozone              = 1.0;
    double  MieG               = 0.8;
    double  RayleighHeight     = 8000.0;
    double  MieHeight          = 1200.0;
    Vector3 GroundAlbedo{ 0.3, 0.3, 0.3 };
    Vector3 SkyTint{ 1.0, 1.0, 1.0 };
    double  HorizonGlow        = 1.0;
    bool    MultipleScattering = true;
    bool    AtmosphereEnabled  = true;
    double  TierSamples        = 40.0;
    bool    UseBakedAtmosphere = true;
};

[[nodiscard]] inline Vector3 ResolveSunTransmittance(const Vector3& Direction, const AtmosphereSettings& Settings) noexcept
{
    if (!Settings.AtmosphereEnabled || !Settings.AirMass)
        return { 1.0, 1.0, 1.0 };
    const double SinEl = std::max(0.02, Direction[2]);
    const double Am    = 1.0 / (SinEl + 0.15 * std::pow(std::max(1.0, 93.885 - std::acos(std::min(1.0, SinEl)) * 57.2957795), -1.253));
    return {
        std::exp(-Am * (0.058 * Settings.Rayleigh + 0.042 * Settings.Mie + 0.012 * Settings.Ozone)),
        std::exp(-Am * (0.135 * Settings.Rayleigh + 0.042 * Settings.Mie + 0.018 * Settings.Ozone)),
        std::exp(-Am * (0.331 * Settings.Rayleigh + 0.042 * Settings.Mie + 0.002 * Settings.Ozone)),
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   MOON ROSTER
//------------------------------------------------------------------------------------------------------------------------

// 📝 Native MoonAtlasPreset order: Luna, Ember, Glacier, Sulfur, Shroud, Shard (CelestialAtlas.js MoonBodies).
struct MoonBodyProfile
{
    const char* Name  = "";
    double      Size  = 25.0;   // [°] default angular size
    double      Tilt  = 0.0;    // [°] axial tilt
    double      Haze  = 0.0;    // [-]
    double      Gamma = 1.0;    // [-]
};

[[nodiscard]] const std::array<MoonBodyProfile, 6>& QueryMoonBodies() noexcept;

// 📝 One moon instance; the primary moon is the settings themselves, the others live in MoonInstances.
struct MoonInstanceSettings
{
    uint32_t MoonBody       = 0;
    double   MoonAzimuth    = 0.0;      // [°] counter-clockwise from +X east
    double   MoonElevation  = 0.0;      // [°]
    double   MoonRoll       = 0.0;      // [°]
    double   MoonPitch      = 0.0;      // [°]
    double   MoonSpin       = 0.0;      // [°]
    double   MoonSize       = 25.0;     // [°] full apparent diameter × 2 (the demo's unit)
    bool     MoonEnabled    = false;
    double   MoonBrightness = 1.0;
    double   MoonPhase      = 0.0;      // [day] of 29.53
    double   MoonGlow       = 0.0;
    bool     MoonFollowSky  = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   ENVIRONMENT SETTINGS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The EnvironmentSettings keys EncodeEnvironment and the celestial solver read, with the demo's EnvironmentDefaults
//    (and its host overrides: SunElevation 38°, SunAzimuth 145°, SolarIlluminance 12).
struct EnvironmentSettings
{
    // ① sun
    bool        SunEnabled       = true;
    bool        SunDiscEnabled   = true;
    bool        SunAutoPosition  = false;
    double      SunElevation     = 38.0;      // [°]
    double      SunAzimuth       = 145.0;     // [°]
    double      SolarIlluminance = 12.0;      // [-] exhibit scale
    double      SunRadius        = 0.27;      // [°]
    double      SunTemperature   = 5778.0;    // [K]
    double      SunGlow          = 1.0;
    double      SunTime          = 12.0;      // [h] local
    bool        DayAnimate       = false;
    double      DayDuration      = 6.0;       // [min] per simulated day
    std::string SkyDate          = "2026-10-20";
    double      Latitude         = -26.18;    // [°]
    double      Longitude        = 28.32;     // [°]
    double      TimeZone         = 2.0;       // [h]
    // ② stars
    bool        StarsEnabled     = true;
    double      StarBrightness   = 2.0;
    double      StarLimit        = 6.0;       // [mag]
    double      StarSize         = 1.0;
    double      StarRotation     = 0.0;       // [°]
    bool        TwinkleEnabled   = true;
    double      TwinkleDepth     = 0.25;
    double      TwinkleRate      = 1.0;
    double      MilkyWay         = 1.0;
    double      StarHalo         = 1.0;
    bool        PlanetsEnabled   = true;
    double      PlanetBrightness = 1.0;
    // ③ primary moon (instance 0) and up to three more
    MoonInstanceSettings              PrimaryMoon{0, -8.0, 38.0, 0.0, 0.0, 0.0, 25.0, true, 1.0, 14.765, 0.2, false};
    std::vector<MoonInstanceSettings> MoonInstances{};
    // ④ fog and aerial perspective
    bool        FogEnabled       = false;
    double      FogDensity       = 0.002;
    double      FogHeight        = 30.0;      // [m]
    double      FogScatter       = 0.5;
    Vector3     FogColour{0.52, 0.62, 0.69};
    bool        AerialEnabled    = true;
    double      AerialDensity    = 4.0e-5;
    double      AerialStart      = 100.0;     // [m]
    double      AerialMie        = 0.5;
    // ⑤ lens flare
    bool        FlareEnabled         = true;
    double      FlareIntensity       = 0.3;
    double      FlareSpread          = 1.0;
    double      FlareGhosts          = 6.0;
    double      FlareGhostBrightness = 0.3;
    double      FlareHaloRadius      = 0.22;
    double      FlareHaloWidth       = 0.025;
    double      FlareHaloBrightness  = 0.2;
    double      FlareStreak          = 0.3;
    double      FlareRotation        = 0.0;   // [°]
    double      FlareRays            = 6.0;
    double      FlareChromatic       = 0.2;
    bool        FlareCustomLayers    = false; // [-] FlarePreset === 'Custom layers'
    double      FlareGhostSpacing    = 1.0;
    double      FlareGhostShape      = 6.0;
    bool        FlareAnamorphic      = true;
    bool        FlareWarmStreak      = false;
    bool        FlareBurst           = true;
    double      FlareAperture        = 6.0;
    // ⑥ twilight line
    double      HorizonGlow       = 1.0;
    double      WhiteLine         = 1.0;
    bool        LineAtCivilOnly   = true;
    bool        AtmosphereEnabled = true;
    // ⑦ atmosphere constants the moon transmittance reads
    AtmosphereSettings Atmosphere{};
};

// 📝 The parts of the resolved light EncodeEnvironment reads (AtmosphereIntegrator.ResolveLight plus the celestial solver).
struct EnvironmentLight
{
    Vector3                       SunDirection{0.0, 0.0, 1.0};
    std::optional<Vector3>        SolarIlluminance{};   // [-] absent before the first ResolveLight
    std::vector<Vector3>          Planets{};            // [-] Venus, Jupiter, Mars, Saturn directions
    std::optional<double>         PixelAngle{};         // [rad] 2·tan(fov/2)/height
    std::optional<double>         Sidereal{};           // [rad] local sidereal angle
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   CELESTIAL SOLVER
//------------------------------------------------------------------------------------------------------------------------

struct HorizontalPlacement
{
    Vector3 Direction{0.0, 0.0, 1.0};   // [-] east, north, up
    double  Azimuth   = 0.0;            // [°] counter-clockwise from +X east
    double  Elevation = 0.0;            // [°]
};

struct CelestialSolution
{
    HorizontalPlacement                Sun{};
    HorizontalPlacement                Moon{};
    double                             MoonPhase = 0.0;   // [day]
    std::array<HorizontalPlacement, 4> Planets{};         // Venus, Jupiter, Mars, Saturn
    double                             JulianDay = 0.0;
};

[[nodiscard]] CelestialSolution SolveCelestial(const EnvironmentSettings& Settings);
// 📝 Animates the day when DayAnimate is set, then places sun/moons that follow the sky (AdvanceCelestial).
CelestialSolution AdvanceCelestial(EnvironmentSettings& Settings, double DeltaSeconds);
[[nodiscard]] double ResolveSiderealAngle(double JulianDay, double Longitude) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                   ENCODER
//------------------------------------------------------------------------------------------------------------------------

class EnvironmentEncoder
{
  public:
    // 📝 Returns the 140 scalars written at refresh scalar 128, rounded to float exactly as Float32Array stores them.
    [[nodiscard]] std::vector<float> Encode(const EnvironmentSettings& Settings, const EnvironmentLight& Light);

  private:
    struct MoonTransmissionMemo
    {
        std::string Key{};
        Vector3     Transmittance{};
    };
    std::array<std::optional<MoonTransmissionMemo>, 4> MoonTransmission_{};
};

} // namespace Frontier::GlobalIllumination
