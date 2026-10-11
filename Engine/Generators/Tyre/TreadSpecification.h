//============================================================================================================================================
//                                                            TREADSPECIFICATION.H
//============================================================================================================================================
// 📦 Tyre carcass parameters and the derived values every tread and sidewall stage reads.

#pragma once

#include <cmath>
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                   CARCASS PARAMETERS
//------------------------------------------------------------------------------------------------------------------------

/// 📦 The marked size of a tyre plus the shaping controls the generator needs beyond it.
/// note  Width, Aspect and Rim are the sidewall marking — 245/40 R18 is Width 245, Aspect 40, Rim 18.
/// tag   schema
struct TreadSpecification
{
    float    Width         = 245.0f;   // [mm]   - section width, the marked first number
    float    Aspect        = 40.0f;    // [%]    - section height as a percentage of width
    float    Rim           = 18.0f;    // [in]   - wheel diameter, the marked R number
    float    TreadDepth    = 8.0f;     // [mm]   - full depth of the pattern cut into the crown
    float    Wear          = 0.0f;     // [-]    - 0 unworn, 1 cut away to the wear bar
    float    WearBias      = 0.0f;     // [-]    - lateral wear asymmetry, -1 inner to +1 outer
    float    TreadFraction = 0.88f;    // [-]    - fraction of the half width carrying pattern
    float    Crown         = 2.0f;     // [mm]   - radius drop from centreline to the tread edge
    float    Shoulder      = 18.0f;    // [mm]   - radius of the arc rolling off into the sidewall
    float    Bulge         = 1.0f;     // [-]    - sidewall convexity multiplier
    float    RimWidthFraction = 0.78f; // [-]    - bead seat half width as a fraction of the half width

    float    SegmentsAround = 720.0f;  // [-]    - lathe segments around the circumference
    float    SegmentsAcross = 96.0f;   // [-]    - profile samples across the moulded surface
    float    PolygonDetail  = 6.0f;    // [mm]   - target edge length where the tread is triangulated

    [[nodiscard]] bool IsValid() const noexcept
    {
        return Width > 0.0f && Aspect > 0.0f && Rim > 0.0f && TreadDepth >= 0.0f
            && TreadFraction > 0.0f && TreadFraction <= 1.0f && Shoulder > 0.0f;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     DERIVED VALUES
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Everything the builders need that follows from a TreadSpecification by arithmetic alone.
/// note  Recomputed rather than cached on the specification, so a parameter edit cannot leave a stale radius
///       behind. The cost is a handful of multiplications and one sine.
/// tag   schema, nonallocating
struct TreadDerivedValues
{
    float    RimRadius   = 0.0f;   // [mm]   - bead seat radius, Rim inches halved
    float    SectionHeight = 0.0f; // [mm]   - sidewall height, Width × Aspect / 100
    float    OuterRadius = 0.0f;   // [mm]   - unloaded crown radius at the centreline
    float    HalfWidth   = 0.0f;   // [mm]   - half the section width
    float    TreadHalf   = 0.0f;   // [mm]   - half width of the patterned band
    float    ShoulderRadius = 0.0f;// [mm]   - shoulder arc radius, capped against section height
    float    ShoulderExtent = 0.0f;// [mm]   - lateral reach of the shoulder arc at the 80° cut
    float    AcrossHalf  = 0.0f;   // [mm]   - half width of the whole moulded surface
    float    Circumference = 0.0f; // [mm]   - 2π at the outer radius, the pattern's periodic length
    float    BeadHalf    = 0.0f;   // [mm]   - half width at the bead seat
};

/// 📦 Derives the radii and extents a TreadSpecification implies.
/// in    Specification  [-]   carcass parameters; IsValid() is the caller's responsibility
/// out   TreadDerivedValues   all fields populated
/// cost  ✔️
/// note  📐 The shoulder arc is clamped to 80°, not 90°, so the surface never turns exactly parallel to
///       the axis. A tangent there would make the lateral parameter stationary and the chart would fold.
/// tag   api, nonallocating, nonthrowing
[[nodiscard]] inline TreadDerivedValues DeriveTreadValues(const TreadSpecification& Specification) noexcept
{
    constexpr float π           = 3.14159265358979323846f;
    constexpr float ShoulderCut = 80.0f * π / 180.0f;   // [rad] - arc is cut here, never at 90°

    TreadDerivedValues Derived;
    Derived.RimRadius      = Specification.Rim * 25.4f * 0.5f;
    Derived.SectionHeight  = Specification.Width * Specification.Aspect * 0.01f;
    Derived.OuterRadius    = Derived.RimRadius + Derived.SectionHeight;
    Derived.HalfWidth      = Specification.Width * 0.5f;
    Derived.TreadHalf      = Derived.HalfWidth * Specification.TreadFraction;
    Derived.ShoulderRadius = std::fmin(Specification.Shoulder, Derived.SectionHeight * 0.6f);
    Derived.ShoulderExtent = Derived.ShoulderRadius * std::sin(ShoulderCut);
    Derived.AcrossHalf     = Derived.TreadHalf + Derived.ShoulderExtent;
    Derived.Circumference  = 2.0f * π * Derived.OuterRadius;
    Derived.BeadHalf       = Derived.HalfWidth * Specification.RimWidthFraction;
    return Derived;
}

}   // namespace Frontier
