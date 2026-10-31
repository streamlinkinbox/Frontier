/*==========================================================================================================================================
                                                       LENS FLARE MODEL
                                     ------------------------------------------------------------------
                 Ghost placement along the sun-centre axis (plan §8.1: LensFlare/FlareImage/
                      FlareVisibility). Header-only geometry; intensity/visibility supplied by caller.
==========================================================================================================================================*/

// 🧩 Lens flare: the classic aperture-ghost chain. Ghosts lie on the line through the screen centre
//    and the sun's screen position (NDC, centre = 0), at fixed fractional positions along the
//    centre-mirrored axis. Deterministic table; no textures involved at this layer.

#pragma once

#include <array>
#include <cstdint>

namespace Frontier
{

struct FlareGhost
{
    double PositionX = 0.0;   // [NDC]
    double PositionY = 0.0;   // [NDC]
    double RadiusNdc = 0.0;   // [NDC] of frame height
    double Strength  = 0.0;   // [-] relative, already includes visibility
};

struct FlareGhostSpec
{
    double AxisFraction;   // [-] position = -SunNdc * AxisFraction (through centre)
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

// Places the spec ghosts for a sun at (SunX, SunY) NDC with the given visibility [0..1]
// (fraction of the sun disc unoccluded, from the caller's FlareVisibility samples).
[[nodiscard]] inline std::array<FlareGhost, 6> LayoutFlareGhosts(double SunX, double SunY, double Visibility) noexcept
{
    std::array<FlareGhost, 6> Out{};
    const double V = Visibility < 0.0 ? 0.0 : (Visibility > 1.0 ? 1.0 : Visibility);
    for (uint32_t I = 0; I < Out.size(); ++I)
    {
        const FlareGhostSpec& Spec = DefaultFlareGhosts[I];
        Out[I] = {-SunX * Spec.AxisFraction, -SunY * Spec.AxisFraction, Spec.RadiusNdc, Spec.Strength * V};
    }
    return Out;
}

} // namespace Frontier
