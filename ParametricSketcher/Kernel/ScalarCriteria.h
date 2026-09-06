//============================================================================================================================================
// 📦 ParametricSketcher/Kernel/ScalarCriteria.h — Tolerance policy, scalar comparison and numeric constants for the SolidArc kernel
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
    static constexpr double MergeTolerance    = 1e-6;                                   // [m]   topological sewing
    static constexpr double AngularTolerance  = 1e-7;                                   // [rad] direction equality
    static constexpr double ChordTolerance    = 1e-4;                                   // [m]   tessellation sagitta
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

    // Quantise to a step (grid snap, angle snap, scale snap). Step ≤ 0 disables quantisation.
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
    NonManifold,              // shell validation failed
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
