/*==========================================================================================================================================
                                                               VOLUMEBOUNDS.H
==========================================================================================================================================*/
// 🧩 Finite box, ellipsoid, sphere and hemispherical density envelopes with a metric edge falloff.

#pragma once

#include "Engine/Rendering/ClusterNesting.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    VOLUME ENVELOPES
//------------------------------------------------------------------------------------------------------------------------

enum class VolumeShape : uint32_t
{
    Ellipsoid  = 0,
    Box        = 1,
    Sphere     = 2,
    Hemisphere = 3
};

[[nodiscard]] inline double EvaluateVolumeEnvelope(SpatialVector Point,
                                                   SpatialVector Centre,
                                                   SpatialVector HalfExtent,
                                                   VolumeShape   Shape,
                                                   double        EdgeMetres,
                                                   uint32_t      VerticalAxis = 2) noexcept
{
    std::array<double, 3> Relative = {Point.X - Centre.X, Point.Y - Centre.Y, Point.Z - Centre.Z};
    std::array<double, 3> Half     = {HalfExtent.X, HalfExtent.Y, HalfExtent.Z};
    VerticalAxis                   = std::min(VerticalAxis, 2u);
    for (size_t Axis = 0; Axis < 3; ++Axis)
        if (!std::isfinite(Relative[Axis]) || !std::isfinite(Half[Axis]) || Half[Axis] <= 0) return 0;
    if (Shape == VolumeShape::Sphere) Half = {Half[0], Half[0], Half[0]};
    if (Shape == VolumeShape::Hemisphere)
    {
        if (Relative[VerticalAxis] < 0) return 0;
        for (size_t Axis = 0; Axis < 3; ++Axis)
            if (Axis != VerticalAxis) Half[Axis] = Half[0];
    }
    const auto Smooth = [](double Fraction)
    {
        const double Clamped = std::clamp(Fraction, 0.0, 1.0);
        return Clamped * Clamped * (3 - 2 * Clamped);
    };
    const double Soft = std::max(0.0001, EdgeMetres);
    if (Shape == VolumeShape::Box)
    {
        double Envelope = 1;
        for (size_t Axis = 0; Axis < 3; ++Axis)
            Envelope *= Smooth((Half[Axis] - std::abs(Relative[Axis])) / Soft);
        return Envelope;
    }
    const double Radius = std::hypot(Relative[0] / Half[0], Relative[1] / Half[1], Relative[2] / Half[2]);
    double       Margin = (1 - Radius) * std::min({Half[0], Half[1], Half[2]});
    if (Shape == VolumeShape::Hemisphere) Margin = std::min(Margin, Relative[VerticalAxis]);
    return Smooth(Margin / Soft);
}

} // namespace Frontier
