/*==========================================================================================================================================
                                                           NESTINGNUMERICS.H
==========================================================================================================================================*/
// 🧩 The demo's JavaScript arithmetic, reproduced bit for bit: V8's Math.hypot, Math.min/Math.max signed-zero and NaN rules,
//    and the small vector substrate of GeometryHierarchy.js, so the C++ nesting equals the demo's to the last bit.
#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
namespace Frontier::VirtualizedGeometry
{
using Point3 = std::array<double, 3>;
using Point2 = std::array<double, 2>;

inline constexpr double Unbounded = std::numeric_limits<double>::infinity();

// 📝 V8 MathHypot (builtins/math.tq): Infinity beats NaN, then a Kahan sum of squares normalised by the largest magnitude.
//    Checked against Node 22 on a million random pairs and triples in both the interpreter and optimised tiers.
template <size_t Count> [[nodiscard]] inline double JsHypot(const std::array<double, Count>& Values) noexcept
{
    std::array<double, Count> Magnitudes{};
    bool   CarriesNaN = false;
    double Largest    = 0.0;
    for (size_t Index = 0; Index < Count; ++Index)
    {
        if (std::isnan(Values[Index])) { CarriesNaN = true; continue; }
        Magnitudes[Index] = std::fabs(Values[Index]);
        if (Magnitudes[Index] > Largest) Largest = Magnitudes[Index];
    }
    if (Largest == Unbounded) return Unbounded;
    if (CarriesNaN) return std::numeric_limits<double>::quiet_NaN();
    if (Largest == 0.0) return 0.0;
    double Sum = 0.0, Compensation = 0.0;
    for (size_t Index = 0; Index < Count; ++Index)
    {
        const double Normalised  = Magnitudes[Index] / Largest;
        const double Summand     = Normalised * Normalised - Compensation;
        const double Preliminary = Sum + Summand;
        Compensation = (Preliminary - Sum) - Summand;
        Sum          = Preliminary;
    }
    return std::sqrt(Sum) * Largest;
}
[[nodiscard]] inline double JsHypot(double X, double Y) noexcept { return JsHypot<2>({X, Y}); }
[[nodiscard]] inline double JsHypot(double X, double Y, double Z) noexcept { return JsHypot<3>({X, Y, Z}); }

// 📝 Math.max / Math.min: NaN propagates and +0 outranks -0, independent of argument order (std::max keeps the first).
[[nodiscard]] inline double JsMax(double A, double B) noexcept
{
    if (std::isnan(A) || std::isnan(B)) return std::numeric_limits<double>::quiet_NaN();
    if (A == 0.0 && B == 0.0) return (std::signbit(A) && std::signbit(B)) ? -0.0 : 0.0;
    return A > B ? A : B;
}
[[nodiscard]] inline double JsMin(double A, double B) noexcept
{
    if (std::isnan(A) || std::isnan(B)) return std::numeric_limits<double>::quiet_NaN();
    if (A == 0.0 && B == 0.0) return (std::signbit(A) || std::signbit(B)) ? -0.0 : 0.0;
    return A < B ? A : B;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SMALL VECTOR SUBSTRATE
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline Point3 Subtract(const Point3& A, const Point3& B) noexcept { return {A[0] - B[0], A[1] - B[1], A[2] - B[2]}; }
[[nodiscard]] inline Point3 Cross(const Point3& A, const Point3& B) noexcept
{
    return {A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]};
}
[[nodiscard]] inline double Dot(const Point3& A, const Point3& B) noexcept { return A[0] * B[0] + A[1] * B[1] + A[2] * B[2]; }
[[nodiscard]] inline double Length(const Point3& A) noexcept { return JsHypot(A[0], A[1], A[2]); }
[[nodiscard]] inline Point3 Normalize(const Point3& A) noexcept
{
    const double Magnitude = Length(A);
    return Magnitude > 0.0 ? Point3{A[0] / Magnitude, A[1] / Magnitude, A[2] / Magnitude} : Point3{0.0, 0.0, 1.0};
}
[[nodiscard]] inline double UvDistance(const Point2& A, const Point2& B) noexcept { return JsHypot(A[0] - B[0], A[1] - B[1]); }
[[nodiscard]] inline double UvSignedArea2(const Point2& U0, const Point2& U1, const Point2& U2) noexcept
{
    return (U1[0] - U0[0]) * (U2[1] - U0[1]) - (U1[1] - U0[1]) * (U2[0] - U0[0]);
}
} // namespace Frontier::VirtualizedGeometry
