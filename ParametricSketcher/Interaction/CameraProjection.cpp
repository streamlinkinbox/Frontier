//============================================================================================================================================
// 📦 ParametricSketcher/Interaction/CameraProjection.cpp — Orbit camera arithmetic and ViewRecord assembly
//============================================================================================================================================

#include "CameraProjection.h"
#include <algorithm>
#include <cmath>

namespace Frontier
{

Vec3 CameraProjection::Eye() const noexcept
{
    double CP = std::cos(Pitch), SP = std::sin(Pitch);
    // Yaw 0 → eye on −Y looking toward +Y (Blender "front"). Yaw increases counter-clockwise seen from +Z.
    Vec3 Offset{ std::sin(Yaw) * CP, -std::cos(Yaw) * CP, SP };
    return Pivot + Offset * Distance;
}

Vec3 CameraProjection::Forward() const noexcept { return (Pivot - Eye()).Normalised(); }
Vec3 CameraProjection::Right() const noexcept { return Forward().Cross(Vec3::UnitZ()).Normalised(); }
Vec3 CameraProjection::Up() const noexcept { return Right().Cross(Forward()); }

Mat4 CameraProjection::ViewMatrix() const noexcept
{
    Vec3 UpHint = std::fabs(Forward().Z) > 0.9999 ? (Pitch > 0 ? Vec3{ std::sin(Yaw), -std::cos(Yaw), 0 } * -1.0 : Vec3{ std::sin(Yaw), -std::cos(Yaw), 0 }) : Vec3::UnitZ();
    return Mat4::LookAt(Eye(), Pivot, UpHint);
}

Mat4 CameraProjection::ProjectionMatrix(double Aspect) const noexcept
{
    if (Orthographic)
    {
        double HalfH = OrthographicHalfHeight();
        return Mat4::Orthographic(HalfH * Aspect, HalfH, -FarPlane, FarPlane);
    }
    return Mat4::Perspective(FovY, Aspect, NearPlane, FarPlane);
}

void CameraProjection::Orbit(double DeltaYaw, double DeltaPitch) noexcept
{
    Yaw = std::fmod(Yaw + DeltaYaw, ScalarCriteria::TwoPi);
    Pitch = ScalarCriteria::Clamp(Pitch + DeltaPitch, -ScalarCriteria::HalfPi + 1e-3, ScalarCriteria::HalfPi - 1e-3);
}

void CameraProjection::Pan(double DeltaRightPixels, double DeltaUpPixels, double ViewportHeight) noexcept
{
    double WorldPerPixel = 2.0 * OrthographicHalfHeight() / ViewportHeight;
    Pivot -= Right() * (DeltaRightPixels * WorldPerPixel);
    Pivot -= Up() * (DeltaUpPixels * WorldPerPixel);
}

void CameraProjection::Dolly(double Steps) noexcept
{
    Distance = std::max(0.01, Distance * std::pow(0.85, Steps));
}

void CameraProjection::Look(CanonicalView View) noexcept
{
    const double Limit = ScalarCriteria::HalfPi - 1e-4;
    switch (View)
    {
        case CanonicalView::Front:     Yaw = 0.0;                        Pitch = 0.0;    Orthographic = true; break;
        case CanonicalView::Back:      Yaw = ScalarCriteria::Pi;         Pitch = 0.0;    Orthographic = true; break;
        case CanonicalView::Right:     Yaw = ScalarCriteria::HalfPi;     Pitch = 0.0;    Orthographic = true; break;
        case CanonicalView::Left:      Yaw = -ScalarCriteria::HalfPi;    Pitch = 0.0;    Orthographic = true; break;
        case CanonicalView::Top:       Yaw = 0.0;                        Pitch = Limit;  Orthographic = true; break;
        case CanonicalView::Bottom:    Yaw = 0.0;                        Pitch = -Limit; Orthographic = true; break;
        case CanonicalView::Isometric: Yaw = ScalarCriteria::Radians(-45.0); Pitch = std::atan(1.0 / std::sqrt(2.0)); Orthographic = false; break;
    }
}

void CameraProjection::Frame(const Box3& Bounds, double Aspect) noexcept
{
    if (Bounds.Empty()) return;
    Pivot = Bounds.Centre();
    double Radius = std::max(Bounds.Diagonal() * 0.5, 1e-3);
    // Exact fit of the eight corners: project each onto the view axes, then the smallest distance that keeps every
    //    corner inside the vertical and horizontal half-angles (perspective) or half-extents (orthographic).
    Vec3 F = Forward(), R = Right(), U = Up();
    double TanV = std::tan(FovY * 0.5), TanH = TanV * Aspect;
    double Needed = 0.0;
    for (int I = 0; I < 8; ++I)
    {
        Vec3 Corner{ (I & 1) ? Bounds.High.X : Bounds.Low.X, (I & 2) ? Bounds.High.Y : Bounds.Low.Y, (I & 4) ? Bounds.High.Z : Bounds.Low.Z };
        Vec3 D = Corner - Pivot;
        double Depth = D.Dot(F), Side = std::fabs(D.Dot(R)), Rise = std::fabs(D.Dot(U));
        Needed = std::max({ Needed, Side / TanH - Depth, Rise / TanV - Depth });
    }
    Distance = std::max(Needed * 1.08, Radius * 0.5);
    NearPlane = std::max(0.001, Distance - Radius * 4.0);
    FarPlane = Distance + Radius * 8.0 + 1000.0;
}

Ray CameraProjection::PixelRay(double PixelX, double PixelY, double ViewportWidth, double ViewportHeight) const noexcept
{
    Mat4 ClipView = (ProjectionMatrix(ViewportWidth / ViewportHeight) * ViewMatrix()).Inverse();
    double NdcX = (PixelX / ViewportWidth) * 2.0 - 1.0;
    double NdcY = (PixelY / ViewportHeight) * 2.0 - 1.0;                              // Vulkan: +Y down, matches pixel rows
    Vec3 NearP = ClipView.TransformPoint({ NdcX, NdcY, 0.0 });
    Vec3 FarP  = ClipView.TransformPoint({ NdcX, NdcY, 1.0 });
    Ray R;
    R.Origin = Orthographic ? NearP : Eye();
    R.Direction = (FarP - NearP).Normalised();
    return R;
}

ViewRecord CameraProjection::ToViewRecord(uint32_t Width, uint32_t Height, double GridCell) const noexcept
{
    ViewRecord R{};
    double Aspect = static_cast<double>(Width) / Height;
    Mat4 ViewClip = ProjectionMatrix(Aspect) * ViewMatrix();
    Mat4 ClipView = ViewClip.Inverse();
    for (int I = 0; I < 16; ++I) { R.ViewClip[I] = static_cast<float>(ViewClip.M[I]); R.ClipView[I] = static_cast<float>(ClipView.M[I]); }
    Vec3 E = Orthographic ? Forward() * -1.0 : Eye();
    R.EyePosition[0] = float(E.X); R.EyePosition[1] = float(E.Y); R.EyePosition[2] = float(E.Z); R.EyePosition[3] = Orthographic ? 0.0f : 1.0f;
    R.Viewport[0] = float(Width); R.Viewport[1] = float(Height); R.Viewport[2] = 1.0f / Width; R.Viewport[3] = 1.0f / Height;
    R.GridStyle[0] = float(GridCell); R.GridStyle[1] = 10.0f; R.GridStyle[2] = float(std::max(Distance * 6.0, 40.0)); R.GridStyle[3] = 0.6f;
    Vec3 Key = Vec3{ -0.45, -0.35, 0.82 }.Normalised();
    R.Illumination[0] = float(Key.X); R.Illumination[1] = float(Key.Y); R.Illumination[2] = float(Key.Z); R.Illumination[3] = 0.42f;
    R.PixelAngle = Orthographic ? 0.0f : float(2.0 * std::tan(FovY * 0.5) / Height);
    R.PixelWorld = Orthographic ? float(2.0 * OrthographicHalfHeight() / Height) : 0.0f;
    return R;
}

} // namespace Frontier
