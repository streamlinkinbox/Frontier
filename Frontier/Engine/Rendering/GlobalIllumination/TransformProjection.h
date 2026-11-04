/*==========================================================================================================================================
                                                         TRANSFORMPROJECTION.H
==========================================================================================================================================*/
// 🧩 Column-major transform, view/projection and vector algebra ported 1:1 from the demo's TransformProjection.js.

#pragma once

#include <array>
#include <cstdint>

namespace Frontier::GlobalIllumination
{

// 📝 Precision contract: the demo computes in JS numbers (IEEE double) and stores transforms in Float32Array. Here every
//    scalar is a double; a function whose JS original returns a Float32Array rounds each element through float before
//    returning (StoreSingle), so a value read back from a transform is exactly what the demo read back.
using Vector3   = std::array<double, 3>;
using Vector4   = std::array<double, 4>;
using Transform = std::array<double, 16>;   // [-] column-major, element [Column * 4 + Row]

struct WorldBounds
{
    Vector3 Minimum{};   // [m]
    Vector3 Maximum{};   // [m]
};

[[nodiscard]] double  StoreSingle(double Scalar) noexcept;
[[nodiscard]] double  ComputeHypot(double X, double Y) noexcept;
[[nodiscard]] double  ComputeHypot(double X, double Y, double Z) noexcept;
[[nodiscard]] double  ComputeHypot(double X, double Y, double Z, double W) noexcept;

[[nodiscard]] Vector3 AddVector(const Vector3& Left, const Vector3& Right) noexcept;
[[nodiscard]] Vector3 SubtractVector(const Vector3& Left, const Vector3& Right) noexcept;
[[nodiscard]] Vector3 ScaleVector(const Vector3& Vector, double Factor) noexcept;
[[nodiscard]] Vector3 MultiplyAddVector(const Vector3& Origin, const Vector3& Direction, double Factor) noexcept;
[[nodiscard]] double  DotVector(const Vector3& Left, const Vector3& Right) noexcept;
[[nodiscard]] Vector3 CrossVector(const Vector3& Left, const Vector3& Right) noexcept;
[[nodiscard]] double  LengthVector(const Vector3& Vector) noexcept;
[[nodiscard]] Vector3 NormalizeVector(const Vector3& Vector) noexcept;
[[nodiscard]] double  ClampScalar(double Scalar, double Minimum, double Maximum) noexcept;
[[nodiscard]] Vector3 RotateVector(const Vector4& Quaternion, const Vector3& Vector) noexcept;
[[nodiscard]] Vector4 ConjugateQuaternion(const Vector4& Quaternion) noexcept;

[[nodiscard]] Transform ConstructIdentityTransform() noexcept;
[[nodiscard]] Transform MultiplyTransform(const Transform& Left, const Transform& Right) noexcept;
[[nodiscard]] Transform InvertTransform(const Transform& Source) noexcept;
[[nodiscard]] Transform ConstructRigidTransform(const Vector3& Position, const Vector4& Quaternion, const Vector3& Scale = {1.0, 1.0, 1.0}) noexcept;
[[nodiscard]] Vector3   TransformPoint(const Transform& Matrix, const Vector3& Point) noexcept;
[[nodiscard]] Vector3   TransformDirection(const Transform& Matrix, const Vector3& Direction) noexcept;
[[nodiscard]] WorldBounds TransformBounds(const Transform& Matrix, const Vector3& LocalMinimum, const Vector3& LocalMaximum) noexcept;
[[nodiscard]] Transform ConstructViewTransform(const Vector3& Eye, const Vector3& Target, const Vector3& Up = {0.0, 0.0, 1.0}) noexcept;
[[nodiscard]] Transform ConstructPerspectiveTransform(double VerticalFieldOfView, double Aspect, double Near, double JitterX = 0.0,
                                                      double JitterY = 0.0) noexcept;
[[nodiscard]] Transform ConstructOrthographicTransform(double Left, double Right, double Bottom, double Top, double Near, double Far) noexcept;
[[nodiscard]] double    ProjectRadicalInverse(uint32_t Index, uint32_t Radix) noexcept;

} // namespace Frontier::GlobalIllumination
