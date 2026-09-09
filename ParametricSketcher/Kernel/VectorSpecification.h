//============================================================================================================================================
// 📦 ParametricSketcher/Kernel/VectorSpecification.h — Double-precision vectors, matrices, quaternions, planes, rays and bounds
//============================================================================================================================================
// The kernel computes in double; only the presentation layer narrows to float when it fills GPU buffers. World is
//    right-handed, Z-up: X right, Y forward (into the screen in the default top view), Z up. Matrices are column-major
//    (Vulkan / GLSL / Slang convention) so a Mat4 can be memcpy'd straight into a uniform after narrowing.
#pragma once

#include "ScalarCriteria.h"
#include <cmath>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  VEC2
//------------------------------------------------------------------------------------------------------------------------

struct Vec2
{
    double X = 0.0;                                                                     // [m]
    double Y = 0.0;                                                                     // [m]

    constexpr Vec2() noexcept = default;
    constexpr Vec2(double X_, double Y_) noexcept : X(X_), Y(Y_) {}

    [[nodiscard]] constexpr Vec2 operator+(Vec2 B) const noexcept { return { X + B.X, Y + B.Y }; }
    [[nodiscard]] constexpr Vec2 operator-(Vec2 B) const noexcept { return { X - B.X, Y - B.Y }; }
    [[nodiscard]] constexpr Vec2 operator*(double S) const noexcept { return { X * S, Y * S }; }
    [[nodiscard]] constexpr Vec2 operator/(double S) const noexcept { return { X / S, Y / S }; }
    [[nodiscard]] constexpr Vec2 operator-() const noexcept { return { -X, -Y }; }
    constexpr Vec2& operator+=(Vec2 B) noexcept { X += B.X; Y += B.Y; return *this; }
    constexpr Vec2& operator-=(Vec2 B) noexcept { X -= B.X; Y -= B.Y; return *this; }
    constexpr Vec2& operator*=(double S) noexcept { X *= S; Y *= S; return *this; }

    [[nodiscard]] constexpr double Dot(Vec2 B) const noexcept { return X * B.X + Y * B.Y; }
    [[nodiscard]] constexpr double Cross(Vec2 B) const noexcept { return X * B.Y - Y * B.X; }   // z of the 3D cross
    [[nodiscard]] constexpr double LengthSquared() const noexcept { return X * X + Y * Y; }
    [[nodiscard]] double Length() const noexcept { return std::sqrt(LengthSquared()); }
    [[nodiscard]] Vec2 Normalised() const noexcept { double L = Length(); return L > ScalarCriteria::KernelTolerance ? *this / L : Vec2{}; }
    [[nodiscard]] constexpr Vec2 Perpendicular() const noexcept { return { -Y, X }; }             // CCW 90°
    [[nodiscard]] double Angle() const noexcept { return std::atan2(Y, X); }
    [[nodiscard]] bool Coincident(Vec2 B, double Tolerance = ScalarCriteria::KernelTolerance) const noexcept { return (*this - B).LengthSquared() <= Tolerance * Tolerance; }
    [[nodiscard]] double Distance(Vec2 B) const noexcept { return (*this - B).Length(); }

    [[nodiscard]] static Vec2 Polar(double Radius, double Angle) noexcept { return { Radius * std::cos(Angle), Radius * std::sin(Angle) }; }
    [[nodiscard]] static constexpr Vec2 Lerp(Vec2 A, Vec2 B, double T) noexcept { return A + (B - A) * T; }
};

[[nodiscard]] constexpr Vec2 operator*(double S, Vec2 V) noexcept { return V * S; }

//------------------------------------------------------------------------------------------------------------------------
//                                                  VEC3
//------------------------------------------------------------------------------------------------------------------------

struct Vec3
{
    double X = 0.0;                                                                     // [m]
    double Y = 0.0;                                                                     // [m]
    double Z = 0.0;                                                                     // [m]

    constexpr Vec3() noexcept = default;
    constexpr Vec3(double X_, double Y_, double Z_) noexcept : X(X_), Y(Y_), Z(Z_) {}
    constexpr Vec3(Vec2 XY, double Z_) noexcept : X(XY.X), Y(XY.Y), Z(Z_) {}

    [[nodiscard]] constexpr Vec3 operator+(Vec3 B) const noexcept { return { X + B.X, Y + B.Y, Z + B.Z }; }
    [[nodiscard]] constexpr Vec3 operator-(Vec3 B) const noexcept { return { X - B.X, Y - B.Y, Z - B.Z }; }
    [[nodiscard]] constexpr Vec3 operator*(double S) const noexcept { return { X * S, Y * S, Z * S }; }
    [[nodiscard]] constexpr Vec3 operator/(double S) const noexcept { return { X / S, Y / S, Z / S }; }
    [[nodiscard]] constexpr Vec3 operator-() const noexcept { return { -X, -Y, -Z }; }
    constexpr Vec3& operator+=(Vec3 B) noexcept { X += B.X; Y += B.Y; Z += B.Z; return *this; }
    constexpr Vec3& operator-=(Vec3 B) noexcept { X -= B.X; Y -= B.Y; Z -= B.Z; return *this; }
    constexpr Vec3& operator*=(double S) noexcept { X *= S; Y *= S; Z *= S; return *this; }
    [[nodiscard]] constexpr double operator[](int Axis) const noexcept { return Axis == 0 ? X : (Axis == 1 ? Y : Z); }
    [[nodiscard]] constexpr double& operator[](int Axis) noexcept { return Axis == 0 ? X : (Axis == 1 ? Y : Z); }

    [[nodiscard]] constexpr double Dot(Vec3 B) const noexcept { return X * B.X + Y * B.Y + Z * B.Z; }
    [[nodiscard]] constexpr Vec3 Cross(Vec3 B) const noexcept { return { Y * B.Z - Z * B.Y, Z * B.X - X * B.Z, X * B.Y - Y * B.X }; }
    [[nodiscard]] constexpr double LengthSquared() const noexcept { return X * X + Y * Y + Z * Z; }
    [[nodiscard]] double Length() const noexcept { return std::sqrt(LengthSquared()); }
    [[nodiscard]] Vec3 Normalised() const noexcept { double L = Length(); return L > ScalarCriteria::KernelTolerance ? *this / L : Vec3{}; }
    [[nodiscard]] bool Coincident(Vec3 B, double Tolerance = ScalarCriteria::KernelTolerance) const noexcept { return (*this - B).LengthSquared() <= Tolerance * Tolerance; }
    [[nodiscard]] double Distance(Vec3 B) const noexcept { return (*this - B).Length(); }
    [[nodiscard]] constexpr Vec2 XY() const noexcept { return { X, Y }; }
    [[nodiscard]] constexpr Vec3 Scaled(Vec3 B) const noexcept { return { X * B.X, Y * B.Y, Z * B.Z }; }
    [[nodiscard]] constexpr double MaxComponent() const noexcept { return X > Y ? (X > Z ? X : Z) : (Y > Z ? Y : Z); }
    [[nodiscard]] constexpr Vec3 Abs() const noexcept { return { X < 0 ? -X : X, Y < 0 ? -Y : Y, Z < 0 ? -Z : Z }; }

    // Any unit vector perpendicular to this one — stable choice by smallest component (Duff et al. would also do).
    [[nodiscard]] Vec3 AnyPerpendicular() const noexcept
    {
        Vec3 A = Abs();
        Vec3 Axis = (A.X <= A.Y && A.X <= A.Z) ? Vec3{ 1, 0, 0 } : (A.Y <= A.Z ? Vec3{ 0, 1, 0 } : Vec3{ 0, 0, 1 });
        return Cross(Axis).Normalised();
    }

    [[nodiscard]] bool Parallel(Vec3 B, double Tolerance = ScalarCriteria::AngularTolerance) const noexcept
    {
        return Normalised().Cross(B.Normalised()).LengthSquared() <= Tolerance * Tolerance;
    }

    [[nodiscard]] static constexpr Vec3 Lerp(Vec3 A, Vec3 B, double T) noexcept { return A + (B - A) * T; }
    [[nodiscard]] static constexpr Vec3 Min(Vec3 A, Vec3 B) noexcept { return { A.X < B.X ? A.X : B.X, A.Y < B.Y ? A.Y : B.Y, A.Z < B.Z ? A.Z : B.Z }; }
    [[nodiscard]] static constexpr Vec3 Max(Vec3 A, Vec3 B) noexcept { return { A.X > B.X ? A.X : B.X, A.Y > B.Y ? A.Y : B.Y, A.Z > B.Z ? A.Z : B.Z }; }
    [[nodiscard]] static constexpr Vec3 UnitX() noexcept { return { 1, 0, 0 }; }
    [[nodiscard]] static constexpr Vec3 UnitY() noexcept { return { 0, 1, 0 }; }
    [[nodiscard]] static constexpr Vec3 UnitZ() noexcept { return { 0, 0, 1 }; }
    [[nodiscard]] static constexpr Vec3 Up() noexcept { return UnitZ(); }                   // Z-up world
};

[[nodiscard]] constexpr Vec3 operator*(double S, Vec3 V) noexcept { return V * S; }

//------------------------------------------------------------------------------------------------------------------------
//                                                  VEC4 (homogeneous)
//------------------------------------------------------------------------------------------------------------------------
// Rational control points live here as (wx, wy, wz, w). Divide() returns the Euclidean point.

struct Vec4
{
    double X = 0.0;                                                                     // [m·w]
    double Y = 0.0;                                                                     // [m·w]
    double Z = 0.0;                                                                     // [m·w]
    double W = 1.0;                                                                     // [-]

    constexpr Vec4() noexcept = default;
    constexpr Vec4(double X_, double Y_, double Z_, double W_) noexcept : X(X_), Y(Y_), Z(Z_), W(W_) {}
    constexpr Vec4(Vec3 P, double W_) noexcept : X(P.X), Y(P.Y), Z(P.Z), W(W_) {}

    [[nodiscard]] constexpr Vec4 operator+(Vec4 B) const noexcept { return { X + B.X, Y + B.Y, Z + B.Z, W + B.W }; }
    [[nodiscard]] constexpr Vec4 operator-(Vec4 B) const noexcept { return { X - B.X, Y - B.Y, Z - B.Z, W - B.W }; }
    [[nodiscard]] constexpr Vec4 operator*(double S) const noexcept { return { X * S, Y * S, Z * S, W * S }; }
    constexpr Vec4& operator+=(Vec4 B) noexcept { X += B.X; Y += B.Y; Z += B.Z; W += B.W; return *this; }

    [[nodiscard]] constexpr Vec3 XYZ() const noexcept { return { X, Y, Z }; }
    [[nodiscard]] constexpr Vec3 Divide() const noexcept { return { X / W, Y / W, Z / W }; }
    [[nodiscard]] static constexpr Vec4 Weighted(Vec3 P, double W) noexcept { return { P.X * W, P.Y * W, P.Z * W, W }; }
    [[nodiscard]] static constexpr Vec4 Lerp(Vec4 A, Vec4 B, double T) noexcept { return A + (B - A) * T; }
};

[[nodiscard]] constexpr Vec4 operator*(double S, Vec4 V) noexcept { return V * S; }

//------------------------------------------------------------------------------------------------------------------------
//                                                  QUAT
//------------------------------------------------------------------------------------------------------------------------

struct Quat
{
    double X = 0.0;                                                                     // [-]
    double Y = 0.0;                                                                     // [-]
    double Z = 0.0;                                                                     // [-]
    double W = 1.0;                                                                     // [-]

    [[nodiscard]] static Quat AxisAngle(Vec3 Axis, double Angle) noexcept
    {
        Vec3 N = Axis.Normalised();
        double S = std::sin(Angle * 0.5);
        return { N.X * S, N.Y * S, N.Z * S, std::cos(Angle * 0.5) };
    }

    [[nodiscard]] constexpr Quat operator*(Quat B) const noexcept
    {
        return {
            W * B.X + X * B.W + Y * B.Z - Z * B.Y,
            W * B.Y - X * B.Z + Y * B.W + Z * B.X,
            W * B.Z + X * B.Y - Y * B.X + Z * B.W,
            W * B.W - X * B.X - Y * B.Y - Z * B.Z };
    }

    [[nodiscard]] constexpr Quat Conjugate() const noexcept { return { -X, -Y, -Z, W }; }

    [[nodiscard]] Quat Normalised() const noexcept
    {
        double L = std::sqrt(X * X + Y * Y + Z * Z + W * W);
        return L > 0.0 ? Quat{ X / L, Y / L, Z / L, W / L } : Quat{};
    }

    [[nodiscard]] constexpr Vec3 Rotate(Vec3 V) const noexcept
    {
        Vec3 U{ X, Y, Z };
        Vec3 T = U.Cross(V) * 2.0;
        return V + T * W + U.Cross(T);
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  MAT4 (column-major)
//------------------------------------------------------------------------------------------------------------------------
// M[Column * 4 + Row]. Column 3 is translation. Matches Vulkan uniform layout after narrowing to float.

struct Mat4
{
    double M[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };                              // [-]

    [[nodiscard]] constexpr double At(int Row, int Column) const noexcept { return M[Column * 4 + Row]; }
    constexpr void Set(int Row, int Column, double A) noexcept { M[Column * 4 + Row] = A; }

    [[nodiscard]] static constexpr Mat4 Identity() noexcept { return Mat4{}; }

    [[nodiscard]] static constexpr Mat4 Translation(Vec3 T) noexcept
    {
        Mat4 R; R.M[12] = T.X; R.M[13] = T.Y; R.M[14] = T.Z; return R;
    }

    [[nodiscard]] static constexpr Mat4 Scaling(Vec3 S) noexcept
    {
        Mat4 R; R.M[0] = S.X; R.M[5] = S.Y; R.M[10] = S.Z; return R;
    }

    [[nodiscard]] static constexpr Mat4 Rotation(Quat Q) noexcept
    {
        double XX = Q.X * Q.X, YY = Q.Y * Q.Y, ZZ = Q.Z * Q.Z;
        double XY = Q.X * Q.Y, XZ = Q.X * Q.Z, YZ = Q.Y * Q.Z;
        double WX = Q.W * Q.X, WY = Q.W * Q.Y, WZ = Q.W * Q.Z;
        Mat4 R;
        R.M[0] = 1 - 2 * (YY + ZZ); R.M[4] = 2 * (XY - WZ);     R.M[8]  = 2 * (XZ + WY);
        R.M[1] = 2 * (XY + WZ);     R.M[5] = 1 - 2 * (XX + ZZ); R.M[9]  = 2 * (YZ - WX);
        R.M[2] = 2 * (XZ - WY);     R.M[6] = 2 * (YZ + WX);     R.M[10] = 1 - 2 * (XX + YY);
        return R;
    }

    [[nodiscard]] static Mat4 Rotation(Vec3 Axis, double Angle) noexcept { return Rotation(Quat::AxisAngle(Axis, Angle)); }

    // Basis from an origin and orthonormal basis: columns are the basis vectors, so local→world.
    [[nodiscard]] static constexpr Mat4 Axes(Vec3 Origin, Vec3 AxisX, Vec3 AxisY, Vec3 AxisZ) noexcept
    {
        Mat4 R;
        R.M[0] = AxisX.X; R.M[1] = AxisX.Y; R.M[2]  = AxisX.Z;
        R.M[4] = AxisY.X; R.M[5] = AxisY.Y; R.M[6]  = AxisY.Z;
        R.M[8] = AxisZ.X; R.M[9] = AxisZ.Y; R.M[10] = AxisZ.Z;
        R.M[12] = Origin.X; R.M[13] = Origin.Y; R.M[14] = Origin.Z;
        return R;
    }

    // Right-handed look-at, Z-up world: view space has X right, Y up, camera looks down −Z (Vulkan/GL clip convention).
    [[nodiscard]] static Mat4 LookAt(Vec3 Eye, Vec3 Target, Vec3 Up) noexcept
    {
        Vec3 F = (Target - Eye).Normalised();
        Vec3 S = F.Cross(Up).Normalised();
        Vec3 U = S.Cross(F);
        Mat4 R;
        R.M[0] = S.X; R.M[4] = S.Y; R.M[8]  = S.Z;  R.M[12] = -S.Dot(Eye);
        R.M[1] = U.X; R.M[5] = U.Y; R.M[9]  = U.Z;  R.M[13] = -U.Dot(Eye);
        R.M[2] = -F.X; R.M[6] = -F.Y; R.M[10] = -F.Z; R.M[14] = F.Dot(Eye);
        R.M[3] = 0; R.M[7] = 0; R.M[11] = 0; R.M[15] = 1;
        return R;
    }

    // Vulkan clip space: depth 0..1, Y down. FovY in radians.
    [[nodiscard]] static Mat4 Perspective(double FovY, double Aspect, double Near, double Far) noexcept
    {
        double F = 1.0 / std::tan(FovY * 0.5);
        Mat4 R;
        for (double& A : R.M) A = 0.0;
        R.M[0]  = F / Aspect;
        R.M[5]  = -F;
        R.M[10] = Far / (Near - Far);
        R.M[11] = -1.0;
        R.M[14] = (Near * Far) / (Near - Far);
        return R;
    }

    [[nodiscard]] static Mat4 Orthographic(double HalfWidth, double HalfHeight, double Near, double Far) noexcept
    {
        Mat4 R;
        for (double& A : R.M) A = 0.0;
        R.M[0]  = 1.0 / HalfWidth;
        R.M[5]  = -1.0 / HalfHeight;
        R.M[10] = 1.0 / (Near - Far);
        R.M[14] = Near / (Near - Far);
        R.M[15] = 1.0;
        return R;
    }

    [[nodiscard]] constexpr Mat4 operator*(const Mat4& B) const noexcept
    {
        Mat4 R;
        for (int C = 0; C < 4; ++C)
            for (int Rw = 0; Rw < 4; ++Rw)
                R.M[C * 4 + Rw] = M[Rw] * B.M[C * 4] + M[4 + Rw] * B.M[C * 4 + 1] + M[8 + Rw] * B.M[C * 4 + 2] + M[12 + Rw] * B.M[C * 4 + 3];
        return R;
    }

    [[nodiscard]] constexpr Vec4 operator*(Vec4 V) const noexcept
    {
        return {
            M[0] * V.X + M[4] * V.Y + M[8]  * V.Z + M[12] * V.W,
            M[1] * V.X + M[5] * V.Y + M[9]  * V.Z + M[13] * V.W,
            M[2] * V.X + M[6] * V.Y + M[10] * V.Z + M[14] * V.W,
            M[3] * V.X + M[7] * V.Y + M[11] * V.Z + M[15] * V.W };
    }

    [[nodiscard]] constexpr Vec3 TransformPoint(Vec3 P) const noexcept { Vec4 R = (*this) * Vec4(P, 1.0); return R.Divide(); }
    [[nodiscard]] constexpr Vec3 TransformDirection(Vec3 D) const noexcept { return ((*this) * Vec4(D, 0.0)).XYZ(); }

    [[nodiscard]] constexpr Mat4 Transposed() const noexcept
    {
        Mat4 R;
        for (int C = 0; C < 4; ++C) for (int Rw = 0; Rw < 4; ++Rw) R.M[C * 4 + Rw] = M[Rw * 4 + C];
        return R;
    }

    // General 4×4 inverse by cofactors. Returns identity if singular (caller checks Determinant when it matters).
    [[nodiscard]] Mat4 Inverse() const noexcept
    {
        const double* A = M;
        double Inv[16];
        Inv[0]  =  A[5]*A[10]*A[15] - A[5]*A[11]*A[14] - A[9]*A[6]*A[15] + A[9]*A[7]*A[14] + A[13]*A[6]*A[11] - A[13]*A[7]*A[10];
        Inv[4]  = -A[4]*A[10]*A[15] + A[4]*A[11]*A[14] + A[8]*A[6]*A[15] - A[8]*A[7]*A[14] - A[12]*A[6]*A[11] + A[12]*A[7]*A[10];
        Inv[8]  =  A[4]*A[9]*A[15]  - A[4]*A[11]*A[13] - A[8]*A[5]*A[15] + A[8]*A[7]*A[13] + A[12]*A[5]*A[11] - A[12]*A[7]*A[9];
        Inv[12] = -A[4]*A[9]*A[14]  + A[4]*A[10]*A[13] + A[8]*A[5]*A[14] - A[8]*A[6]*A[13] - A[12]*A[5]*A[10] + A[12]*A[6]*A[9];
        Inv[1]  = -A[1]*A[10]*A[15] + A[1]*A[11]*A[14] + A[9]*A[2]*A[15] - A[9]*A[3]*A[14] - A[13]*A[2]*A[11] + A[13]*A[3]*A[10];
        Inv[5]  =  A[0]*A[10]*A[15] - A[0]*A[11]*A[14] - A[8]*A[2]*A[15] + A[8]*A[3]*A[14] + A[12]*A[2]*A[11] - A[12]*A[3]*A[10];
        Inv[9]  = -A[0]*A[9]*A[15]  + A[0]*A[11]*A[13] + A[8]*A[1]*A[15] - A[8]*A[3]*A[13] - A[12]*A[1]*A[11] + A[12]*A[3]*A[9];
        Inv[13] =  A[0]*A[9]*A[14]  - A[0]*A[10]*A[13] - A[8]*A[1]*A[14] + A[8]*A[2]*A[13] + A[12]*A[1]*A[10] - A[12]*A[2]*A[9];
        Inv[2]  =  A[1]*A[6]*A[15]  - A[1]*A[7]*A[14]  - A[5]*A[2]*A[15] + A[5]*A[3]*A[14] + A[13]*A[2]*A[7]  - A[13]*A[3]*A[6];
        Inv[6]  = -A[0]*A[6]*A[15]  + A[0]*A[7]*A[14]  + A[4]*A[2]*A[15] - A[4]*A[3]*A[14] - A[12]*A[2]*A[7]  + A[12]*A[3]*A[6];
        Inv[10] =  A[0]*A[5]*A[15]  - A[0]*A[7]*A[13]  - A[4]*A[1]*A[15] + A[4]*A[3]*A[13] + A[12]*A[1]*A[7]  - A[12]*A[3]*A[5];
        Inv[14] = -A[0]*A[5]*A[14]  + A[0]*A[6]*A[13]  + A[4]*A[1]*A[14] - A[4]*A[2]*A[13] - A[12]*A[1]*A[6]  + A[12]*A[2]*A[5];
        Inv[3]  = -A[1]*A[6]*A[11]  + A[1]*A[7]*A[10]  + A[5]*A[2]*A[11] - A[5]*A[3]*A[10] - A[9]*A[2]*A[7]   + A[9]*A[3]*A[6];
        Inv[7]  =  A[0]*A[6]*A[11]  - A[0]*A[7]*A[10]  - A[4]*A[2]*A[11] + A[4]*A[3]*A[10] + A[8]*A[2]*A[7]   - A[8]*A[3]*A[6];
        Inv[11] = -A[0]*A[5]*A[11]  + A[0]*A[7]*A[9]   + A[4]*A[1]*A[11] - A[4]*A[3]*A[9]  - A[8]*A[1]*A[7]   + A[8]*A[3]*A[5];
        Inv[15] =  A[0]*A[5]*A[10]  - A[0]*A[6]*A[9]   - A[4]*A[1]*A[10] + A[4]*A[2]*A[9]  + A[8]*A[1]*A[6]   - A[8]*A[2]*A[5];
        double Det = A[0] * Inv[0] + A[1] * Inv[4] + A[2] * Inv[8] + A[3] * Inv[12];
        if (ScalarCriteria::Vanishing(Det, 1e-300)) return Mat4{};
        Mat4 R;
        for (int I = 0; I < 16; ++I) R.M[I] = Inv[I] / Det;
        return R;
    }

    [[nodiscard]] constexpr Vec3 Origin() const noexcept { return { M[12], M[13], M[14] }; }
    [[nodiscard]] constexpr Vec3 AxisX() const noexcept { return { M[0], M[1], M[2] }; }
    [[nodiscard]] constexpr Vec3 AxisY() const noexcept { return { M[4], M[5], M[6] }; }
    [[nodiscard]] constexpr Vec3 AxisZ() const noexcept { return { M[8], M[9], M[10] }; }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  PLANE
//------------------------------------------------------------------------------------------------------------------------
// Signed form: Normal·P = Offset. A workplane carries a full frame (Origin, AxisX, AxisY) so sketches have a 2D basis.

struct Plane
{
    Vec3   Normal = Vec3::UnitZ();                                                      // [-] unit
    double Offset = 0.0;                                                                // [m]

    [[nodiscard]] static Plane FromPointNormal(Vec3 P, Vec3 N) noexcept { Vec3 U = N.Normalised(); return { U, U.Dot(P) }; }
    [[nodiscard]] static Plane FromThreePoints(Vec3 A, Vec3 B, Vec3 C) noexcept { return FromPointNormal(A, (B - A).Cross(C - A)); }
    [[nodiscard]] constexpr double SignedDistance(Vec3 P) const noexcept { return Normal.Dot(P) - Offset; }
    [[nodiscard]] constexpr Vec3 Project(Vec3 P) const noexcept { return P - Normal * SignedDistance(P); }
    [[nodiscard]] constexpr Vec3 Origin() const noexcept { return Normal * Offset; }
};

struct Workplane
{
    Vec3 Origin = {};                                                                   // [m]
    Vec3 AxisX  = Vec3::UnitX();                                                        // [-] unit
    Vec3 AxisY  = Vec3::UnitY();                                                        // [-] unit

    [[nodiscard]] Vec3 Normal() const noexcept { return AxisX.Cross(AxisY).Normalised(); }
    [[nodiscard]] Plane ToPlane() const noexcept { return Plane::FromPointNormal(Origin, Normal()); }
    [[nodiscard]] constexpr Vec3 ToWorld(Vec2 UV) const noexcept { return Origin + AxisX * UV.X + AxisY * UV.Y; }
    [[nodiscard]] constexpr Vec3 ToWorld(Vec2 UV, double Height) const noexcept { return ToWorld(UV) + AxisX.Cross(AxisY) * Height; }
    [[nodiscard]] constexpr Vec2 ToLocal(Vec3 P) const noexcept { Vec3 D = P - Origin; return { D.Dot(AxisX), D.Dot(AxisY) }; }
    [[nodiscard]] Mat4 ToMatrix() const noexcept { return Mat4::Axes(Origin, AxisX, AxisY, Normal()); }

    [[nodiscard]] static constexpr Workplane XY() noexcept { return { {}, Vec3::UnitX(), Vec3::UnitY() }; }   // top (Z normal)
    [[nodiscard]] static constexpr Workplane XZ() noexcept { return { {}, Vec3::UnitX(), Vec3::UnitZ() }; }   // front (−Y normal)
    [[nodiscard]] static constexpr Workplane YZ() noexcept { return { {}, Vec3::UnitY(), Vec3::UnitZ() }; }   // right (X normal)

    // Basis from a point and a normal: AxisX chosen stable against world axes so sketches don't twist unexpectedly.
    [[nodiscard]] static Workplane FromNormal(Vec3 Origin, Vec3 Normal) noexcept
    {
        Vec3 N = Normal.Normalised();
        Vec3 Reference = std::fabs(N.Z) < 0.999 ? Vec3::UnitZ() : Vec3::UnitY();
        Vec3 AxisX = Reference.Cross(N).Normalised();
        Vec3 AxisY = N.Cross(AxisX);
        return { Origin, AxisX, AxisY };
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  RAY
//------------------------------------------------------------------------------------------------------------------------

struct Ray
{
    Vec3 Origin    = {};                                                                // [m]
    Vec3 Direction = Vec3::UnitZ();                                                     // [-] unit

    [[nodiscard]] constexpr Vec3 At(double T) const noexcept { return Origin + Direction * T; }

    // Parametric hit with a plane; false when parallel.
    [[nodiscard]] bool Intersect(const Plane& P, double& T) const noexcept
    {
        double Denominator = P.Normal.Dot(Direction);
        if (ScalarCriteria::Vanishing(Denominator, ScalarCriteria::AngularTolerance)) return false;
        T = (P.Offset - P.Normal.Dot(Origin)) / Denominator;
        return true;
    }

    // Parameter along an infinite line (LineOrigin + LineDirection·S) closest to this ray — the axis-drag primitive.
    [[nodiscard]] double ClosestParameterOnLine(Vec3 LineOrigin, Vec3 LineDirection) const noexcept
    {
        Vec3 W0 = LineOrigin - Origin;
        double A = LineDirection.Dot(LineDirection), B = LineDirection.Dot(Direction), C = Direction.Dot(Direction);
        double D = LineDirection.Dot(W0), E = Direction.Dot(W0);
        double Denominator = A * C - B * B;
        if (ScalarCriteria::Vanishing(Denominator)) return 0.0;
        return (B * E - C * D) / Denominator;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  BOX3
//------------------------------------------------------------------------------------------------------------------------

struct Box3
{
    Vec3 Low  = {  ScalarCriteria::Infinity,  ScalarCriteria::Infinity,  ScalarCriteria::Infinity };   // [m]
    Vec3 High = { -ScalarCriteria::Infinity, -ScalarCriteria::Infinity, -ScalarCriteria::Infinity };   // [m]

    [[nodiscard]] constexpr bool Empty() const noexcept { return Low.X > High.X; }
    constexpr void Include(Vec3 P) noexcept { Low = Vec3::Min(Low, P); High = Vec3::Max(High, P); }
    constexpr void Include(const Box3& B) noexcept { if (!B.Empty()) { Include(B.Low); Include(B.High); } }
    [[nodiscard]] constexpr Vec3 Centre() const noexcept { return (Low + High) * 0.5; }
    [[nodiscard]] constexpr Vec3 Extent() const noexcept { return High - Low; }
    [[nodiscard]] double Diagonal() const noexcept { return Empty() ? 0.0 : Extent().Length(); }
    [[nodiscard]] Box3 Inflated(double Margin) const noexcept { Box3 R = *this; Vec3 M{ Margin, Margin, Margin }; R.Low -= M; R.High += M; return R; }

    [[nodiscard]] constexpr bool Overlaps(const Box3& B) const noexcept
    {
        return !(B.Low.X > High.X || B.High.X < Low.X || B.Low.Y > High.Y || B.High.Y < Low.Y || B.Low.Z > High.Z || B.High.Z < Low.Z);
    }

    [[nodiscard]] constexpr bool Contains(Vec3 P) const noexcept
    {
        return P.X >= Low.X && P.X <= High.X && P.Y >= Low.Y && P.Y <= High.Y && P.Z >= Low.Z && P.Z <= High.Z;
    }
};

} // namespace Frontier
