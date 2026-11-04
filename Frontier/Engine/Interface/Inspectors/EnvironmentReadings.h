/*==========================================================================================================================================
                                                          ENVIRONMENTREADINGS.H
==========================================================================================================================================*/
// 🧩 The demo's analytical inspector references. These are diagnostics, not measurements of the native renderer.

#pragma once

#include "Engine/Scene/EngineConfigurations.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::EnvironmentReadings
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    ANGLES AND CLOUD LAYERS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr double Pi = 3.14159265358979323846;

[[nodiscard]] inline float WrapDegrees(float Degrees) noexcept
{
    return std::isfinite(Degrees) ? std::fmod(std::fmod(Degrees, 360.0f) + 360.0f, 360.0f) : 0.0f;
}

[[nodiscard]] inline double CloudTop(const CloudConfiguration &Cloud) noexcept
{
    return std::max<double>(Cloud.BaseAltitudeMetres,
                            std::min<double>(Cloud.CeilingMetres, Cloud.BaseAltitudeMetres + Cloud.LayerThicknessMetres));
}

[[nodiscard]] inline uint32_t CloudSamples(const CloudConfiguration &Cloud) noexcept
{
    const double Depth = CloudTop(Cloud) - Cloud.BaseAltitudeMetres;
    return static_cast<uint32_t>(std::min(96.0, std::ceil(Cloud.SkySamples * std::max(1.0, Depth / 1200.0))));
}

[[nodiscard]] inline double CloudProfile(double Height, uint32_t Formation, double Anvil) noexcept
{
    if (Height <= 0.0 || Height >= 1.0) return 0.0;
    const auto Clamp   = [](double Quantity) { return std::clamp(Quantity, 0.0, 1.0); };
    double     Density = Clamp(Height * 8.0) * Clamp((1.0 - Height) * 3.0);
    if (Formation < 2) Density = Clamp(Height * 12.0) * Clamp((1.0 - Height) * 8.0);
    if (Formation == 3) Density *= 1.0 + Anvil * Height * 0.5;
    if (Formation > 3) Density *= std::max(0.0, 1.0 - std::pow(Height * 2.0 - 1.0, 2.0));
    return Density;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    WIND REFERENCES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline double GustFactor(double Gust, double Phase) noexcept
{
    return 1.0 + Gust * (0.65 * std::sin(Phase) + 0.35 * std::sin(Phase * 2.17));
}

[[nodiscard]] inline std::array<double, 3> WindVelocity(
    const WindConfiguration &Wind, double Altitude, double Time = 0.0, double Phase = 0.0, bool RespectEnabled = false) noexcept
{
    const double Height     = std::max(0.0, Altitude) / 1000.0;
    const double θ          = (Wind.DirectionDegrees + Wind.VeerDegreesPerKilometre * Height) * Pi / 180.0;
    const double Speed      = RespectEnabled && !Wind.Enabled ? 0.0 : Wind.SpeedMetresPerSecond * GustFactor(Wind.GustStrength, Phase);
    const double Turbulence = Wind.CurlNoiseTurbulence * Speed;
    const double Shear      = 1.0 + Wind.ShearPerKilometre * Height;
    return {std::sin(θ) * Speed * Shear + std::sin(Time * 0.31 + Height) * Turbulence,
            std::cos(θ) * Speed * Shear + std::sin(Time * 0.27 + Height * 3.0) * Turbulence,
            std::sin(Time * 0.22 + Height) * Turbulence * 0.12};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FOG REFERENCE TRANSMISSION
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline std::array<double, 3> FogTransmission(const FogConfiguration      &Fog,
                                                           const std::array<double, 3> &Camera,
                                                           const std::array<double, 3> &Position,
                                                           bool                         Aerial) noexcept
{
    const double Distance    = std::hypot(Position[0] - Camera[0], Position[1] - Camera[1], Position[2] - Camera[2]);
    const double Height      = 1.0 / std::max(0.002, static_cast<double>(Fog.HeightFalloff));
    const double Δ           = (Position[2] - Camera[2]) / Height;
    const double Column      = std::abs(Δ) > 0.001 ? (1.0 - std::exp(std::clamp(-Δ, -50.0, 50.0))) / Δ : 1.0;
    const double HeightDepth = Aerial ? 0.0 : Fog.BaseDensity * Distance * std::exp(std::clamp(-Camera[2] / Height, -50.0, 20.0)) * Column;
    const double AerialDepth = Aerial ? Fog.AerialDensity * std::max(0.0, Distance - Fog.AerialStartMetres) : 0.0;
    std::array<double, 3>           Transmission{};
    constexpr std::array<double, 3> Bands = {0.45, 0.7, 1.0};
    for (size_t Band = 0; Band < 3; ++Band)
        Transmission[Band] = std::exp(
            -std::min(80.0, HeightDepth * HeightDepth + (Bands[Band] * (1.0 - Fog.AerialMieBlend) + Fog.AerialMieBlend) * AerialDepth));
    return Transmission;
}

[[nodiscard]] inline double NeutralContrast(const std::array<double, 3> &Transmission) noexcept
{
    return Transmission[0] * 0.2126 + Transmission[1] * 0.7152 + Transmission[2] * 0.0722;
}

[[nodiscard]] inline double FogForwardWeight(double Angle, double Scatter) noexcept
{
    return std::pow(std::max(0.0, std::cos(Angle)), 8.0) * Scatter;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CELESTIAL REFERENCES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline std::array<double, 2> StarChartPoint(const std::array<float, 7> &Star, double Rotation) noexcept
{
    const double Radius = std::sqrt((1.0 - std::clamp(static_cast<double>(Star[2]), -1.0, 1.0)) / 2.0);
    const double θ      = std::atan2(Star[1], Star[0]) - Rotation * Pi / 180.0;
    return {std::cos(θ) * Radius, -std::sin(θ) * Radius};
}

// 📝 The sky's own twinkle (PlanetaryProjection.slang EvaluateScintillationGpu) at the zenith: the demo's three-frequency signal,
//    its peak swing the twinkle depth. The sky deepens it toward the horizon (× 1.33 at 30°, × 1.9 at 10°, at most × 2.5).
[[nodiscard]] inline double StarScintillation(double Time, const StarConfiguration &Stars, const std::array<float, 7> &Star) noexcept
{
    const double Phase  = Star[0] * 91.7 + Star[1] * 213.1 + Star[2] * 57.3;
    const double τ      = Time * Stars.ScintillationRateHz;
    const double Signal =
        0.6 * std::sin(τ * 6.283185 + Phase) + 0.28 * std::sin(τ * 11.71 + Phase * 1.7) + 0.12 * std::sin(τ * 19.37 + Phase * 2.3);
    const double Depth  = Stars.ScintillationEnabled ? std::clamp(static_cast<double>(Stars.AtmosphericScintillate), 0.0, 1.0) : 0.0;
    return std::max(0.05, 1.0 + Depth * Signal);
}

inline constexpr double LunarCycle = 29.53;

[[nodiscard]] inline double LunarIllumination(double Days) noexcept
{
    return 0.5 - 0.5 * std::cos(Days / LunarCycle * 2.0 * Pi);
}

} // namespace Frontier::EnvironmentReadings
