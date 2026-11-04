/*==========================================================================================================================================
                                                            OPTICALREFERENCE.CPP
==========================================================================================================================================*/
// 🧩 NativeLensFlareKernel.js equations for the inspector's optical reference; not a replacement scene renderer.

#include "Engine/Interface/Inspectors/OpticalReference.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>

namespace Frontier
{

namespace
{

constexpr double Pi = 3.14159265358979323846;

double Fraction(double Quantity)
{
    return Quantity - std::floor(Quantity);
}

double Smooth(double First, double Last, double Quantity)
{
    const double Fraction = std::clamp((Quantity - First) / (Last - First), 0.0, 1.0);
    return Fraction * Fraction * (3 - 2 * Fraction);
}

double Hue(double Angle, size_t Channel)
{
    constexpr double Offsets[] = {0, 0.6666667, 0.3333333};
    return std::clamp(std::abs(Fraction(Angle + Offsets[Channel]) * 6 - 3) - 1, 0.0, 1.0);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    LAYER PRESETS
//------------------------------------------------------------------------------------------------------------------------

void ConfigureOpticalPreset(OpticalFlareConfiguration &Parameters, uint32_t Preset)
{
    Parameters.Preset = std::min(4u, Preset);
    if (Preset >= 4) return;
    constexpr uint32_t Rays[] = {8, 2, 8, 6}, Ghosts[] = {6, 4, 3, 0};
    constexpr float    GhostBrightness[] = {0.35f, 0.3f, 0.2f, 0}, HaloBrightness[] = {0.5f, 0.2f, 0.1f, 1};
    constexpr float    Streak[]     = {1, 1.5f, 0, 0};
    Parameters.AnamorphicStreak     = Preset < 2;
    Parameters.WarmStreak           = false;
    Parameters.DiffractionStarburst = Preset == 0 || Preset == 2;
    Parameters.RayCount             = Rays[Preset];
    Parameters.GhostCount           = Ghosts[Preset];
    Parameters.GhostBrightness      = GhostBrightness[Preset];
    Parameters.GhostShape           = 0;
    Parameters.GhostSpacing         = 1;
    Parameters.HaloBrightness       = HaloBrightness[Preset];
    Parameters.StreakStrength       = Streak[Preset];
}

uint32_t ResolveFlareRayPairs(const OpticalFlareConfiguration &Parameters) noexcept
{
    return std::clamp(Parameters.Preset == 4 ? Parameters.RayCount : Parameters.ApertureBladeCount, 2u, 12u);
}

std::array<double, 2> EvaluateFlareGhost(const OpticalFlareConfiguration &Parameters, uint32_t Ghost) noexcept
{
    const uint32_t Count = std::min(24u, Parameters.GhostCount);
    if (Count == 0 || Ghost >= Count) return {0.5, 0.5};
    const double Scale = 1 - (0.6 + (Ghost + 1.0) / Count * (1.7 + Parameters.GhostSpacing));
    return {0.5 + (Parameters.PreviewPosition[0] - 0.5) * Scale, 0.5 + (Parameters.PreviewPosition[1] - 0.5) * Scale};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    LINEAR RADIANCE
//------------------------------------------------------------------------------------------------------------------------

std::array<double, 3> EvaluateOpticalReference(
    const OpticalFlareConfiguration &Parameters, double U, double V, double Aspect, double Visibility) noexcept
{
    const double Scalars[] = {U,
                              V,
                              Aspect,
                              Visibility,
                              Parameters.Intensity,
                              Parameters.Spread,
                              Parameters.ChromaticDispersion,
                              Parameters.RotationDegrees,
                              Parameters.PreviewPosition[0],
                              Parameters.PreviewPosition[1],
                              Parameters.GhostSpacing,
                              Parameters.GhostBrightness,
                              Parameters.HaloRadius,
                              Parameters.HaloWidth,
                              Parameters.HaloBrightness,
                              Parameters.StreakStrength};
    for (double Quantity : Scalars)
        if (!std::isfinite(Quantity)) return {};
    if (Aspect <= 0 || Visibility <= 0 || Parameters.Intensity <= 0) return {};
    const double SunU = Parameters.PreviewPosition[0], SunV = Parameters.PreviewPosition[1];
    const double Edge =
        Smooth(-0.15, 0.05, SunU) * (1 - Smooth(0.95, 1.15, SunU)) * Smooth(-0.15, 0.05, SunV) * (1 - Smooth(0.95, 1.15, SunV));
    const double Along = (U - 0.5) * Aspect, Across = V - 0.5;
    const double SourceAlong = (SunU - 0.5) * Aspect, SourceAcross = SunV - 0.5;
    const double ΔAlong = Along - SourceAlong, ΔAcross = Across - SourceAcross;
    const double Spread   = std::clamp(static_cast<double>(Parameters.Spread), 0.25, 2.0);
    const double Distance = std::hypot(ΔAlong, ΔAcross), θ = Parameters.RotationDegrees * Pi / 180;
    const double TurnedAlong  = ΔAlong * std::cos(θ) + ΔAcross * std::sin(θ);
    const double TurnedAcross = -ΔAlong * std::sin(θ) + ΔAcross * std::cos(θ);
    const double Chromatic    = Parameters.ChromaticDispersion;
    const double Cool         = std::exp(-std::abs(ΔAcross) * 95 / Spread) * std::exp(-std::abs(ΔAlong) * 2.2 / Spread) * 0.55 *
                        Parameters.AnamorphicStreak * Parameters.StreakStrength;
    const double Warm = std::exp(-std::abs(TurnedAcross) * 160 / Spread) * std::exp(-std::abs(TurnedAlong) * 3 / Spread) * 0.65 *
                        Parameters.WarmStreak * Parameters.StreakStrength;
    const double Angle = Distance > 1.0e-8 ? std::atan2(TurnedAcross, TurnedAlong) : 0;
    const double Burst = std::pow(std::abs(std::sin(Angle * ResolveFlareRayPairs(Parameters))), 32) * std::exp(-Distance * 5 / Spread) *
                         0.6 * Parameters.DiffractionStarburst;
    const double          Source = Parameters.AnamorphicStreak || Parameters.WarmStreak || Parameters.DiffractionStarburst
                                       ? std::exp(-Distance * 80 / Spread) * 0.8 + std::exp(-Distance * 9 / Spread) * 0.035
                                       : 0;
    std::array<double, 3> Rgb    = {Cool * (1 - 0.55 * Chromatic) + Warm + Burst + Source,
                                    Cool * (1 - 0.35 * Chromatic) + Warm * 0.8 + Burst * 0.95 + Source * 0.9,
                                    Cool + Warm * 0.55 + Burst * 0.82 + Source * 0.75};
    const uint32_t        Count  = std::min(24u, Parameters.GhostCount);
    for (uint32_t Ghost = 0; Ghost < Count; ++Ghost)
    {
        const double Scale      = 1 - (0.6 + (Ghost + 1.0) / Count * (1.7 + Parameters.GhostSpacing));
        const double GhostAlong = Along - SourceAlong * Scale, GhostAcross = Across - SourceAcross * Scale;
        double       GhostDistance = std::hypot(GhostAlong, GhostAcross);
        if (Parameters.GhostShape >= 3)
        {
            const uint32_t Sides   = std::min(32u, Parameters.GhostShape);
            const double   Step    = Pi * 2 / Sides;
            const double   Azimuth = GhostDistance > 1.0e-8 ? std::atan2(GhostAcross, GhostAlong) : 0;
            const double   Fold    = Azimuth - Step * std::floor((Azimuth + Step * 0.5) / Step);
            GhostDistance *= std::cos(Fold) / std::cos(Pi / Sides);
        }
        const double Radius         = (0.025 + 0.06 * Fraction(Ghost * 0.618 + 0.31)) * Spread;
        const double ChromaticAngle = Fraction(Ghost * 0.23 + 0.5);
        for (size_t Band = 0; Band < 3; ++Band)
        {
            const double BandRadius = Radius * (1 + (static_cast<double>(Band) - 1) * Chromatic * 0.035);
            const double Spectrum =
                (Smooth(BandRadius, BandRadius * 0.35, GhostDistance) * 0.6 + std::exp(-std::abs(GhostDistance - BandRadius) * 350)) *
                0.14 * Parameters.GhostBrightness;
            Rgb[Band] += Spectrum * (1 + (Hue(ChromaticAngle, Band) - 1) * Chromatic);
        }
    }
    const double HaloAlong = Along - SourceAlong * 0.25, HaloAcross = Across - SourceAcross * 0.25;
    const double HaloDistance = std::hypot(HaloAlong, HaloAcross);
    const double Offset       = std::abs(HaloDistance - Parameters.HaloRadius);
    const double Halo =
        Smooth(std::clamp(static_cast<double>(Parameters.HaloWidth), 0.005, 0.2), 0, Offset) * 0.09 * Parameters.HaloBrightness;
    const double HaloAngle = Fraction((HaloDistance > 1.0e-8 ? std::atan2(HaloAcross, HaloAlong) : 0) / (Pi * 2) + Offset * 8);
    for (size_t Band = 0; Band < 3; ++Band)
    {
        Rgb[Band] += Halo * (1 + (Hue(HaloAngle, Band) - 1) * Chromatic * 0.8);
        Rgb[Band] *= Parameters.Intensity * Visibility * Edge;
    }
    return Rgb;
}

std::vector<float> GenerateOpticalReference(const OpticalFlareConfiguration &Parameters, int Width, int Height)
{
    if (Width <= 0 || Height <= 0 || Width > 4096 || Height > 4096 || static_cast<int64_t>(Width) * Height > 1048576) return {};
    std::vector<float> Radiance(static_cast<size_t>(Width) * Height * 3);
    for (int Row = 0; Row < Height; ++Row)
    {
        for (int Column = 0; Column < Width; ++Column)
        {
            const auto Sample =
                EvaluateOpticalReference(Parameters, (Column + 0.5) / Width, (Row + 0.5) / Height, static_cast<double>(Width) / Height);
            for (size_t Band = 0; Band < 3; ++Band)
            {
                if (!std::isfinite(Sample[Band]) || std::abs(Sample[Band]) > std::numeric_limits<float>::max()) return {};
                Radiance[(static_cast<size_t>(Row) * Width + Column) * 3 + Band] = static_cast<float>(Sample[Band]);
            }
        }
    }
    return Radiance;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PORTABLE FLOAT MAP
//------------------------------------------------------------------------------------------------------------------------

bool EncodeLinearPfm(
    const std::filesystem::path &Destination, std::span<const float> Radiance, int Width, int Height, std::string &Diagnostic)
{
    Diagnostic.clear();
    if (Width <= 0 || Height <= 0 || Width > 4096 || Height > 4096 || static_cast<uint64_t>(Width) * Height * 3 != Radiance.size() ||
        std::any_of(Radiance.begin(), Radiance.end(), [](float Channel) { return !std::isfinite(Channel); }))
    {
        Diagnostic = "Invalid PFM dimensions or non-finite radiance";
        return false;
    }
    std::error_code Failure;
    if (std::filesystem::exists(Destination, Failure))
    {
        Diagnostic = "The destination already exists; choose a new filename";
        return false;
    }
    std::ofstream Output(Destination, std::ios::binary);
    if (!Output)
    {
        Diagnostic = "Could not open the PFM destination";
        return false;
    }
    Output << "PF\n" << Width << ' ' << Height << "\n-1.0\n";
    for (int Row = Height - 1; Row >= 0; --Row)
    {
        for (int Channel = 0; Channel < Width * 3; ++Channel)
        {
            const uint32_t Word    = std::bit_cast<uint32_t>(Radiance[static_cast<size_t>(Row) * Width * 3 + Channel]);
            const char     Bytes[] = {static_cast<char>(Word), static_cast<char>(Word >> 8), static_cast<char>(Word >> 16),
                                      static_cast<char>(Word >> 24)};
            Output.write(Bytes, 4);
        }
    }
    Output.flush();
    if (!Output)
    {
        Diagnostic = "PFM write failed";
        return false;
    }
    Diagnostic = "Saved linear HDR PFM";
    return true;
}

} // namespace Frontier
