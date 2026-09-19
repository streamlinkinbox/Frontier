//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Kernel/ScalarCriteria.h — Tolerance policy, scalar comparison and numeric constants for the SolidArc kernel
//============================================================================================================================================
// One place for every epsilon. Every solver in the kernel reads its tolerance from here, so a tolerance change is a
//    one-line edit rather than a hunt. Three bands exist, and they are deliberately far apart:
//
//      KernelTolerance   1e-9   [m]   exact-arithmetic stand-in: two coordinates closer than this ARE the same point
//      MergeTolerance    1e-6   [m]   topological sewing: trim endpoints within this are joined into one vertex
//      AngularTolerance  1e-7   [rad] parallel / perpendicular / tangent classification
//
// The bands never meet: MergeTolerance / KernelTolerance = 1000, so a merge decision is never flipped by round-off.
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  SCALAR CRITERIA
//------------------------------------------------------------------------------------------------------------------------

struct ScalarCriteria
{
    static constexpr double KernelTolerance   = 1e-9;                                   // [m]   coincidence
    static constexpr double GeometricTolerance = 1e-8;                                // [m or -] strict geometric classification
    static constexpr double MergeTolerance    = 1e-6;                                   // [m]   topological sewing
    static constexpr double AngularTolerance  = 1e-7;                                   // [rad] direction equality
    static constexpr double SweepTolerance  = 1e-6;                                   // [rad] arc endpoint classification
    static constexpr double CircularTolerance = 1e-6;                               // [m] circular rim / radius matching
    static constexpr double CurveTolerance = 1e-7;                                  // [m or -] curve intersection / subdivision
    static constexpr double ScaledPositionTolerance = 1e-7;                      // [m or -] bounded rim / parameter positions
    static constexpr double DirectionTolerance = 1e-6;                           // [rad or -] legacy face/edge direction tests
    static constexpr double DistanceTolerance = 1e-6;                            // [m] local surface/intersection distance
    static constexpr double ChordTolerance    = 1e-4;                                   // [m]   tessellation sagitta
    static constexpr double VolumeTolerance  = 1e-3;                                   // [m³]  analytic-vs-tessellated volume gate
    static constexpr int    ChordSubdivision  = 32;                                     // [-]   tessellation cap per knot span
    // Verification-only refinement, never on an acceptance path. Measured floors of the tessellated integrator at the
    //    shipping chord (Scratchpad/ProbeVolumeBand.cpp, tapered frustum r 2.0 → 1.25 h 8 m):
    //        chord 1e-4, cap   32 (shipping)  3.79e-4 relative — inside VolumeTolerance with 2.6x margin
    //        chord 1e-6, cap  512             5.45e-5 relative  ← what analytic proofs are compared against
    //        chord 1e-7, cap 2048             5.33e-5 relative  — the floor does not fall further
    //    A wall-only body keeps converging (sphere: 1.02e-3 → 2.48e-7), so the floor is the *trim sampling* of trimmed
    //    planar faces, not the sagitta. Verification states the measured band explicitly instead of hiding it behind the
    //    1e-3 acceptance gate.
    static constexpr double VerificationChord   = 1e-6;                                  // [m]   refined sagitta for analytic proofs
    static constexpr int    VerificationCap     = 512;                                   // [-]   refined span cap for analytic proofs
    static constexpr double MeasuredVolumeBand  = 2e-4;                                  // [-]   relative band: measured vs analytic volume
    static constexpr double ParametricEpsilon = 1e-12;                                  // [-]   knot / parameter equality
    static constexpr double Infinity          = std::numeric_limits<double>::infinity(); // [-]
    static constexpr double Pi                = std::numbers::pi_v<double>;             // [rad]
    static constexpr double TwoPi             = 2.0 * std::numbers::pi_v<double>;       // [rad]
    static constexpr double HalfPi            = 0.5 * std::numbers::pi_v<double>;       // [rad]

    [[nodiscard]] static constexpr bool Coincident(double A, double B, double Tolerance = KernelTolerance) noexcept
    {
        double Difference = A - B;
        return Difference <= Tolerance && Difference >= -Tolerance;
    }

    [[nodiscard]] static constexpr bool Vanishing(double A, double Tolerance = KernelTolerance) noexcept
    {
        return A <= Tolerance && A >= -Tolerance;
    }

    // A tolerance is a hard limit: a value that sits exactly on it is inside, and a caller who builds that value as
    //    `Exact ± Tolerance` must not be told otherwise by the rounding of the sum itself. (π + 1e-6) − π is
    //    1.0000000000288e-6, not 1e-6, and 12 + 12·VolumeTolerance − 12 overshoots by 3.8e-14 of its own limit; both are
    //    boundary values, both must accept. BoundarySlack absorbs that last rounding step. It is a *relative* widening of
    //    the limit itself — 1e-9 of 1e-3 m³ is a cubic picometre — so nothing that was refused before is accepted now.
    static constexpr double BoundarySlack = 1e-9;                                       // [-]   boundary rounding guard

    [[nodiscard]] static bool WithinScaledTolerance(double Measured, double Exact, double Tolerance) noexcept
    {
        return std::fabs(Measured - Exact) <= Tolerance * std::fmax(1.0, std::fabs(Exact)) * (1.0 + BoundarySlack);
    }

    [[nodiscard]] static bool WithinAngularTolerance(double Measured, double Exact) noexcept
    {
        return std::fabs(Measured - Exact) <= SweepTolerance * (1.0 + BoundarySlack);
    }

    [[nodiscard]] static bool WithinCircularTolerance(double Measured, double Exact) noexcept
    {
        return std::fabs(Measured - Exact) <= CircularTolerance * (1.0 + BoundarySlack);
    }

    [[nodiscard]] static bool WithinVolumeTolerance(double Measured, double Exact) noexcept
    {
        return WithinScaledTolerance(Measured, Exact, VolumeTolerance);
    }

    // Tightened counterpart of the acceptance gate, for verification: the measured tessellated volume must sit inside
    //    MeasuredVolumeBand of the closed form. Five times tighter than what the kernel accepts, so a regression in the
    //    analytic construction shows up as a failed check rather than as a body that is merely inside the shipped band.
    [[nodiscard]] static bool WithinMeasuredBand(double Measured, double Exact) noexcept
    {
        return std::fabs(Measured - Exact) <= MeasuredVolumeBand * std::fmax(1.0, std::fabs(Exact)) * (1.0 + BoundarySlack);
    }

    [[nodiscard]] static constexpr double Clamp(double A, double Low, double High) noexcept
    {
        return A < Low ? Low : (A > High ? High : A);
    }

    [[nodiscard]] static constexpr double Lerp(double A, double B, double T) noexcept
    {
        return A + (B - A) * T;
    }

    [[nodiscard]] static constexpr double Radians(double Degrees) noexcept
    {
        return Degrees * (Pi / 180.0);
    }

    [[nodiscard]] static constexpr double Degrees(double Radians) noexcept
    {
        return Radians * (180.0 / Pi);
    }

    // Wrap an angle into [0, 2π). Used by every arc and revolve so sweep angles are always canonical.
    [[nodiscard]] static double WrapAngle(double Angle) noexcept
    {
        double Wrapped = std::fmod(Angle, TwoPi);
        return Wrapped < 0.0 ? Wrapped + TwoPi : Wrapped;
    }

    // Quantise to a step (lattice snap, angle snap, scale snap). Step ≤ 0 disables quantisation.
    [[nodiscard]] static double Quantise(double A, double Step) noexcept
    {
        return Step > 0.0 ? std::round(A / Step) * Step : A;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  REFUSAL
//------------------------------------------------------------------------------------------------------------------------
// A domain failure carried as a value. No exception ever crosses a kernel seam: an operation that cannot complete
//    returns a Refusal naming the reason, and leaves its operands untouched.

enum class RefusalReason : uint8_t
{
    None = 0,
    DegenerateInput,          // zero-length line, zero radius, coincident points
    InvalidDegree,            // degree < 1 or ≥ control point count
    InvalidKnotVector,        // non-monotone or wrong length
    OutOfDomain,              // parameter outside [t0, t1]
    NoConvergence,            // Newton / marching failed to reach tolerance
    OpenWire,                 // closure required and absent
    NonPlanar,                // planarity required and absent
    NonManifold,              // hull validation failed
    SelfIntersecting,         // profile crosses itself
    Unsupported,              // valid input, not implemented yet
};

struct Refusal
{
    RefusalReason Reason = RefusalReason::None;                                         // [-]
    const char*   Detail = "";                                                          // [-] static text only

    [[nodiscard]] constexpr explicit operator bool() const noexcept { return Reason != RefusalReason::None; }
    [[nodiscard]] static constexpr Refusal Accept() noexcept { return Refusal{}; }
    [[nodiscard]] static constexpr Refusal Reject(RefusalReason Reason, const char* Detail) noexcept { return Refusal{ Reason, Detail }; }

    [[nodiscard]] static constexpr const char* Describe(RefusalReason Reason) noexcept
    {
        switch (Reason)
        {
            case RefusalReason::None:              return "None";
            case RefusalReason::DegenerateInput:   return "DegenerateInput";
            case RefusalReason::InvalidDegree:     return "InvalidDegree";
            case RefusalReason::InvalidKnotVector: return "InvalidKnotVector";
            case RefusalReason::OutOfDomain:       return "OutOfDomain";
            case RefusalReason::NoConvergence:     return "NoConvergence";
            case RefusalReason::OpenWire:          return "OpenWire";
            case RefusalReason::NonPlanar:         return "NonPlanar";
            case RefusalReason::NonManifold:       return "NonManifold";
            case RefusalReason::SelfIntersecting:  return "SelfIntersecting";
            case RefusalReason::Unsupported:       return "Unsupported";
        }
        return "Unknown";
    }
};

// Deliver<T>: either a T or a Refusal. Checked with `if (Delivered)`; unwrapped with `.Payload`.
template<typename Payload_>
struct Deliver
{
    Payload_ Payload{};                                                                 // [-]
    Refusal  Denial{};                                                                  // [-]

    [[nodiscard]] constexpr explicit operator bool() const noexcept { return !Denial; }
    [[nodiscard]] static constexpr Deliver Accept(Payload_ Payload) noexcept { return Deliver{ std::move(Payload), Refusal::Accept() }; }
    [[nodiscard]] static constexpr Deliver Reject(RefusalReason Reason, const char* Detail) noexcept { return Deliver{ Payload_{}, Refusal::Reject(Reason, Detail) }; }
};

} // namespace Frontier
