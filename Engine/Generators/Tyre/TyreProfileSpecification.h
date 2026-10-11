//============================================================================================================================================
//                                                         TYREPROFILESPECIFICATION.H
//============================================================================================================================================
// 📦 The moulded cross-section: crown drop, shoulder arc, and the surface frame at any lateral position.

#pragma once

#include "TreadSpecification.h"

#include <cmath>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                     SURFACE SAMPLE
//------------------------------------------------------------------------------------------------------------------------

/// 📦 The moulded surface at one lateral position: where it is, which way it faces, how much it grips.
/// note  Lateral and Radius are a cross-section in the plane of the axis; the ring is swept from them.
/// tag   schema
struct TyreProfileSample
{
    float    Lateral      = 0.0f;   // [mm]  - position along the axis, 0 at the centreline
    float    Radius       = 0.0f;   // [mm]  - distance from the axis
    float    NormalLateral = 0.0f;  // [-]   - surface normal, axial component
    float    NormalRadial = 1.0f;   // [-]   - surface normal, radial component
    float    ContactWeight = 1.0f;  // [-]   - 1 on the crown falling to 0 at the shoulder cut
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   PROFILE EVALUATION
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Evaluates the moulded cross-section at a lateral coordinate.
/// in    Lateral        [mm]  axial position, signed, 0 at the centreline
/// in    Specification  [-]   carcass parameters, read for Crown
/// in    Derived        [-]   values from DeriveTreadValues for the same specification
/// out   TyreProfileSample    position, normal and contact weight at that coordinate
/// cost  ✔️
/// note  📐 Two pieces meeting at ±TreadHalf. Inboard the crown is a parabola, radius falling by Crown
///       at the tread edge. Outboard it is a circular arc of ShoulderRadius, cut at 80°. The pieces are
///       position-continuous but not curvature-continuous, which is what a real mould does.
/// note  ⚠️ This is the only place the cross-section is defined. The tread builder, the sidewall lathe and
///       the chart layout must all call it rather than re-deriving a radius, or the shoulder seam opens.
/// tag   api, nonallocating, nonthrowing
[[nodiscard]] inline TyreProfileSample EvaluateTyreProfile(float                     Lateral,
                                                           const TreadSpecification& Specification,
                                                           const TreadDerivedValues& Derived) noexcept
{
    constexpr float π           = 3.14159265358979323846f;
    constexpr float ShoulderCut = 80.0f * π / 180.0f;   // [rad] - never 90°, see DeriveTreadValues

    const float Magnitude = std::fabs(Lateral);
    const float Sign      = Lateral < 0.0f ? -1.0f : 1.0f;

    TyreProfileSample Sample;

    // 📝 ① Crown band. A parabola in the normalised lateral coordinate, flat-topped at the centreline.
    if (Magnitude <= Derived.TreadHalf)
    {
        const float Normalised = Derived.TreadHalf > 0.0f ? Lateral / Derived.TreadHalf : 0.0f;
        Sample.Lateral       = Lateral;
        Sample.Radius        = Derived.OuterRadius - Specification.Crown * Normalised * Normalised;
        Sample.NormalLateral = 0.0f;
        Sample.NormalRadial  = 1.0f;
        Sample.ContactWeight = 1.0f;
        return Sample;
    }

    // 📝 ② Shoulder arc. φ sweeps from 0 at the tread edge to the 80° cut at the widest point.
    const float Reach = Derived.ShoulderRadius > 0.0f
                      ? (Magnitude - Derived.TreadHalf) / Derived.ShoulderRadius
                      : 1.0f;
    const float φ     = std::fmin(ShoulderCut, std::asin(std::fmin(1.0f, Reach)));
    const float CutCos = std::cos(ShoulderCut);

    Sample.Lateral       = Sign * (Derived.TreadHalf + Derived.ShoulderRadius * std::sin(φ));
    Sample.Radius        = Derived.OuterRadius - Specification.Crown
                         - Derived.ShoulderRadius + Derived.ShoulderRadius * std::cos(φ);
    Sample.NormalLateral = Sign * std::sin(φ);
    Sample.NormalRadial  = std::cos(φ);
    Sample.ContactWeight = std::fmax(0.0f, (std::cos(φ) - CutCos) / (1.0f - CutCos));
    return Sample;
}

}   // namespace Frontier
