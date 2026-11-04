/*==========================================================================================================================================
                                                        TRANSFORMPROJECTION.CPP
==========================================================================================================================================*/
// 🧩 Column-major transform, view/projection and vector algebra ported 1:1 from the demo's TransformProjection.js.

#include "TransformProjection.h"

#include <algorithm>
#include <cmath>

namespace Frontier::GlobalIllumination
{

// ⚠️ The round trip goes through a volatile float: GCC 12 -O2 SLP vectorization was observed dropping the
//    double → float → double truncation for paired stores (View[12], View[13]), which broke parity with the demo.
double StoreSingle(double Scalar) noexcept
{
    volatile float Single = static_cast<float>(Scalar);
    return static_cast<double>(Single);
}

// 📝 V8's Math.hypot: the largest magnitude scales every term, the squares are summed with Kahan compensation.
namespace
{
template <size_t Count>
double ComputeHypotOf(const std::array<double, Count>& Values) noexcept
{
    double Largest = 0.0;
    std::array<double, Count> Magnitudes{};
    for (size_t Index = 0; Index < Count; ++Index)
    {
        Magnitudes[Index] = std::fabs(Values[Index]);
        if (std::isinf(Magnitudes[Index])) return INFINITY;
        if (Magnitudes[Index] > Largest) Largest = Magnitudes[Index];
    }
    for (double Magnitude : Magnitudes)
    {
        if (std::isnan(Magnitude)) return NAN;
    }
    if (Largest == 0.0) return 0.0;
    double Sum          = 0.0;
    double Compensation = 0.0;
    for (double Magnitude : Magnitudes)
    {
        const double Normalized  = Magnitude / Largest;
        const double Summand     = Normalized * Normalized - Compensation;
        const double Preliminary = Sum + Summand;
        Compensation             = (Preliminary - Sum) - Summand;
        Sum                      = Preliminary;
    }
    return std::sqrt(Sum) * Largest;
}
} // namespace

double ComputeHypot(double X, double Y) noexcept
{
    return ComputeHypotOf<2>({X, Y});
}

double ComputeHypot(double X, double Y, double Z) noexcept
{
    return ComputeHypotOf<3>({X, Y, Z});
}

double ComputeHypot(double X, double Y, double Z, double W) noexcept
{
    return ComputeHypotOf<4>({X, Y, Z, W});
}

Vector3 AddVector(const Vector3& Left, const Vector3& Right) noexcept
{
    return {Left[0] + Right[0], Left[1] + Right[1], Left[2] + Right[2]};
}

Vector3 SubtractVector(const Vector3& Left, const Vector3& Right) noexcept
{
    return {Left[0] - Right[0], Left[1] - Right[1], Left[2] - Right[2]};
}

Vector3 ScaleVector(const Vector3& Vector, double Factor) noexcept
{
    return {Vector[0] * Factor, Vector[1] * Factor, Vector[2] * Factor};
}

Vector3 MultiplyAddVector(const Vector3& Origin, const Vector3& Direction, double Factor) noexcept
{
    return {Origin[0] + Direction[0] * Factor, Origin[1] + Direction[1] * Factor, Origin[2] + Direction[2] * Factor};
}

double DotVector(const Vector3& Left, const Vector3& Right) noexcept
{
    return Left[0] * Right[0] + Left[1] * Right[1] + Left[2] * Right[2];
}

Vector3 CrossVector(const Vector3& Left, const Vector3& Right) noexcept
{
    return {Left[1] * Right[2] - Left[2] * Right[1],
            Left[2] * Right[0] - Left[0] * Right[2],
            Left[0] * Right[1] - Left[1] * Right[0]};
}

double LengthVector(const Vector3& Vector) noexcept
{
    return ComputeHypot(Vector[0], Vector[1], Vector[2]);
}

Vector3 NormalizeVector(const Vector3& Vector) noexcept
{
    const double Length = LengthVector(Vector);
    return Length > 1.0e-12 ? ScaleVector(Vector, 1.0 / Length) : Vector3{0.0, 0.0, 1.0};
}

double ClampScalar(double Scalar, double Minimum, double Maximum) noexcept
{
    return Scalar < Minimum ? Minimum : (Scalar > Maximum ? Maximum : Scalar);
}

Vector3 RotateVector(const Vector4& Quaternion, const Vector3& Vector) noexcept
{
    const Vector3 Axis  = {Quaternion[0], Quaternion[1], Quaternion[2]};
    const Vector3 Twice = ScaleVector(CrossVector(Axis, Vector), 2.0);
    return AddVector(AddVector(Vector, ScaleVector(Twice, Quaternion[3])), CrossVector(Axis, Twice));
}

Vector4 ConjugateQuaternion(const Vector4& Quaternion) noexcept
{
    return {-Quaternion[0], -Quaternion[1], -Quaternion[2], Quaternion[3]};
}

Transform ConstructIdentityTransform() noexcept
{
    Transform Result{};
    Result[0] = Result[5] = Result[10] = Result[15] = 1.0;
    return Result;
}

Transform MultiplyTransform(const Transform& Left, const Transform& Right) noexcept
{
    Transform Result{};
    for (int Column = 0; Column < 4; ++Column)
    {
        for (int Row = 0; Row < 4; ++Row)
        {
            double Sum = 0.0;
            for (int Inner = 0; Inner < 4; ++Inner)
            {
                Sum += Left[Inner * 4 + Row] * Right[Column * 4 + Inner];
            }
            Result[Column * 4 + Row] = StoreSingle(Sum);
        }
    }
    return Result;
}

Transform InvertTransform(const Transform& S) noexcept
{
    Transform Inverse{};
    Inverse[0]  =  S[5] * S[10] * S[15] - S[5] * S[11] * S[14] - S[9] * S[6] * S[15] + S[9] * S[7] * S[14] + S[13] * S[6] * S[11] - S[13] * S[7] * S[10];
    Inverse[4]  = -S[4] * S[10] * S[15] + S[4] * S[11] * S[14] + S[8] * S[6] * S[15] - S[8] * S[7] * S[14] - S[12] * S[6] * S[11] + S[12] * S[7] * S[10];
    Inverse[8]  =  S[4] * S[9]  * S[15] - S[4] * S[11] * S[13] - S[8] * S[5] * S[15] + S[8] * S[7] * S[13] + S[12] * S[5] * S[11] - S[12] * S[7] * S[9];
    Inverse[12] = -S[4] * S[9]  * S[14] + S[4] * S[10] * S[13] + S[8] * S[5] * S[14] - S[8] * S[6] * S[13] - S[12] * S[5] * S[10] + S[12] * S[6] * S[9];
    Inverse[1]  = -S[1] * S[10] * S[15] + S[1] * S[11] * S[14] + S[9] * S[2] * S[15] - S[9] * S[3] * S[14] - S[13] * S[2] * S[11] + S[13] * S[3] * S[10];
    Inverse[5]  =  S[0] * S[10] * S[15] - S[0] * S[11] * S[14] - S[8] * S[2] * S[15] + S[8] * S[3] * S[14] + S[12] * S[2] * S[11] - S[12] * S[3] * S[10];
    Inverse[9]  = -S[0] * S[9]  * S[15] + S[0] * S[11] * S[13] + S[8] * S[1] * S[15] - S[8] * S[3] * S[13] - S[12] * S[1] * S[11] + S[12] * S[3] * S[9];
    Inverse[13] =  S[0] * S[9]  * S[14] - S[0] * S[10] * S[13] - S[8] * S[1] * S[14] + S[8] * S[2] * S[13] + S[12] * S[1] * S[10] - S[12] * S[2] * S[9];
    Inverse[2]  =  S[1] * S[6]  * S[15] - S[1] * S[7]  * S[14] - S[5] * S[2] * S[15] + S[5] * S[3] * S[14] + S[13] * S[2] * S[7]  - S[13] * S[3] * S[6];
    Inverse[6]  = -S[0] * S[6]  * S[15] + S[0] * S[7]  * S[14] + S[4] * S[2] * S[15] - S[4] * S[3] * S[14] - S[12] * S[2] * S[7]  + S[12] * S[3] * S[6];
    Inverse[10] =  S[0] * S[5]  * S[15] - S[0] * S[7]  * S[13] - S[4] * S[1] * S[15] + S[4] * S[3] * S[13] + S[12] * S[1] * S[7]  - S[12] * S[3] * S[5];
    Inverse[14] = -S[0] * S[5]  * S[14] + S[0] * S[6]  * S[13] + S[4] * S[1] * S[14] - S[4] * S[2] * S[13] - S[12] * S[1] * S[6]  + S[12] * S[2] * S[5];
    Inverse[3]  = -S[1] * S[6]  * S[11] + S[1] * S[7]  * S[10] + S[5] * S[2] * S[11] - S[5] * S[3] * S[10] - S[9]  * S[2] * S[7]  + S[9]  * S[3] * S[6];
    Inverse[7]  =  S[0] * S[6]  * S[11] - S[0] * S[7]  * S[10] - S[4] * S[2] * S[11] + S[4] * S[3] * S[10] + S[8]  * S[2] * S[7]  - S[8]  * S[3] * S[6];
    Inverse[11] = -S[0] * S[5]  * S[11] + S[0] * S[7]  * S[9]  + S[4] * S[1] * S[11] - S[4] * S[3] * S[9]  - S[8]  * S[1] * S[7]  + S[8]  * S[3] * S[5];
    Inverse[15] =  S[0] * S[5]  * S[10] - S[0] * S[6]  * S[9]  - S[4] * S[1] * S[10] + S[4] * S[2] * S[9]  + S[8]  * S[1] * S[6]  - S[8]  * S[2] * S[5];
    // 📝 The JS original writes these into a Float32Array before using them.
    for (double& Element : Inverse) Element = StoreSingle(Element);
    const double Determinant = S[0] * Inverse[0] + S[1] * Inverse[4] + S[2] * Inverse[8] + S[3] * Inverse[12];
    if (std::fabs(Determinant) < 1.0e-20) return ConstructIdentityTransform();
    const double Reciprocal = 1.0 / Determinant;
    for (double& Element : Inverse) Element = StoreSingle(Element * Reciprocal);
    return Inverse;
}

Transform ConstructRigidTransform(const Vector3& Position, const Vector4& Quaternion, const Vector3& Scale) noexcept
{
    const double x = Quaternion[0], y = Quaternion[1], z = Quaternion[2], w = Quaternion[3];
    Transform Result{};
    Result[0]  = StoreSingle((1.0 - 2.0 * (y * y + z * z)) * Scale[0]);
    Result[1]  = StoreSingle((2.0 * (x * y + z * w)) * Scale[0]);
    Result[2]  = StoreSingle((2.0 * (x * z - y * w)) * Scale[0]);
    Result[4]  = StoreSingle((2.0 * (x * y - z * w)) * Scale[1]);
    Result[5]  = StoreSingle((1.0 - 2.0 * (x * x + z * z)) * Scale[1]);
    Result[6]  = StoreSingle((2.0 * (y * z + x * w)) * Scale[1]);
    Result[8]  = StoreSingle((2.0 * (x * z + y * w)) * Scale[2]);
    Result[9]  = StoreSingle((2.0 * (y * z - x * w)) * Scale[2]);
    Result[10] = StoreSingle((1.0 - 2.0 * (x * x + y * y)) * Scale[2]);
    Result[12] = StoreSingle(Position[0]);
    Result[13] = StoreSingle(Position[1]);
    Result[14] = StoreSingle(Position[2]);
    Result[15] = 1.0;
    return Result;
}

Vector3 TransformPoint(const Transform& M, const Vector3& Point) noexcept
{
    const double x = Point[0], y = Point[1], z = Point[2];
    const double w          = M[3] * x + M[7] * y + M[11] * z + M[15];
    const double Reciprocal = std::fabs(w) > 1.0e-20 ? 1.0 / w : 1.0;
    return {(M[0] * x + M[4] * y + M[8] * z + M[12]) * Reciprocal,
            (M[1] * x + M[5] * y + M[9] * z + M[13]) * Reciprocal,
            (M[2] * x + M[6] * y + M[10] * z + M[14]) * Reciprocal};
}

Vector3 TransformDirection(const Transform& M, const Vector3& Direction) noexcept
{
    const double x = Direction[0], y = Direction[1], z = Direction[2];
    return {M[0] * x + M[4] * y + M[8] * z,
            M[1] * x + M[5] * y + M[9] * z,
            M[2] * x + M[6] * y + M[10] * z};
}

WorldBounds TransformBounds(const Transform& M, const Vector3& LocalMinimum, const Vector3& LocalMaximum) noexcept
{
    const Vector3 Centre      = ScaleVector(AddVector(LocalMinimum, LocalMaximum), 0.5);
    const Vector3 Half        = ScaleVector(SubtractVector(LocalMaximum, LocalMinimum), 0.5);
    const Vector3 WorldCentre = TransformPoint(M, Centre);
    Vector3       WorldHalf{};
    for (int Row = 0; Row < 3; ++Row)
    {
        WorldHalf[Row] = std::fabs(M[Row]) * Half[0] + std::fabs(M[4 + Row]) * Half[1] + std::fabs(M[8 + Row]) * Half[2];
    }
    return {SubtractVector(WorldCentre, WorldHalf), AddVector(WorldCentre, WorldHalf)};
}

Transform ConstructViewTransform(const Vector3& Eye, const Vector3& Target, const Vector3& Up) noexcept
{
    const Vector3 Forward = NormalizeVector(SubtractVector(Target, Eye));
    Vector3       Right   = CrossVector(Forward, Up);
    if (LengthVector(Right) < 1.0e-6) Right = CrossVector(Forward, {0.0, 1.0, 0.0});
    Right                = NormalizeVector(Right);
    const Vector3 Upward = CrossVector(Right, Forward);
    Transform View{};
    View[0]  = StoreSingle(Right[0]);    View[4] = StoreSingle(Right[1]);    View[8]  = StoreSingle(Right[2]);
    View[1]  = StoreSingle(Upward[0]);   View[5] = StoreSingle(Upward[1]);   View[9]  = StoreSingle(Upward[2]);
    View[2]  = StoreSingle(-Forward[0]); View[6] = StoreSingle(-Forward[1]); View[10] = StoreSingle(-Forward[2]);
    View[12] = StoreSingle(-DotVector(Right, Eye));
    View[13] = StoreSingle(-DotVector(Upward, Eye));
    View[14] = StoreSingle(DotVector(Forward, Eye));
    View[15] = 1.0;
    return View;
}

Transform ConstructPerspectiveTransform(double VerticalFieldOfView, double Aspect, double Near, double JitterX, double JitterY) noexcept
{
    const double Focal = 1.0 / std::tan(0.5 * VerticalFieldOfView);
    Transform    Projection{};
    Projection[0]  = StoreSingle(Focal / Aspect);
    Projection[5]  = StoreSingle(Focal);
    Projection[8]  = StoreSingle(-JitterX);
    Projection[9]  = StoreSingle(-JitterY);
    Projection[11] = -1.0;
    Projection[14] = StoreSingle(Near);
    return Projection;
}

Transform ConstructOrthographicTransform(double Left, double Right, double Bottom, double Top, double Near, double Far) noexcept
{
    Transform Projection{};
    Projection[0]  = StoreSingle(2.0 / (Right - Left));
    Projection[5]  = StoreSingle(2.0 / (Top - Bottom));
    Projection[10] = StoreSingle(-1.0 / (Far - Near));
    Projection[12] = StoreSingle(-(Right + Left) / (Right - Left));
    Projection[13] = StoreSingle(-(Top + Bottom) / (Top - Bottom));
    Projection[14] = StoreSingle(-Near / (Far - Near));
    Projection[15] = 1.0;
    return Projection;
}

double ProjectRadicalInverse(uint32_t Index, uint32_t Radix) noexcept
{
    double   Fraction  = 1.0;
    double   Result    = 0.0;
    uint32_t Remaining = Index;
    while (Remaining > 0)
    {
        Fraction /= Radix;
        Result += Fraction * static_cast<double>(Remaining % Radix);
        Remaining /= Radix;
    }
    return Result;
}

} // namespace Frontier::GlobalIllumination
