//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Interaction/TransformGizmo.cpp — GizmoPRO geometry, ray tests and drag solvers (transcribed from Gizmo.html)
//============================================================================================================================================
#include "TransformGizmo.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
    constexpr double Pi = 3.14159265358979323846;

    // Colours from Gizmo.html (sRGB hex → linear-ish floats used directly, the raster does no colour management).
    constexpr float AxisColour[3][3]  = { { 0.878f, 0.078f, 0.078f }, { 0.070f, 0.831f, 0.039f }, { 0.082f, 0.376f, 0.878f } };   // #e01414 #12d40a #1560e0
    constexpr float PlaneColour[3][3] = { { 0.122f, 0.780f, 0.780f }, { 0.784f, 0.118f, 0.784f }, { 0.878f, 0.804f, 0.071f } };  // #1fc7c7 #c81ec8 #e0cd12

    struct GripBuild                                                                  // one grip's triangles in world space
    {
        SurfaceStream Surface;
        void Vertex(Vec3 P, Vec3 N)
        {
            Surface.Positions.push_back(float(P.X)); Surface.Positions.push_back(float(P.Y)); Surface.Positions.push_back(float(P.Z));
            Surface.Normals.push_back(float(N.X)); Surface.Normals.push_back(float(N.Y)); Surface.Normals.push_back(float(N.Z));
            Surface.Parameters.push_back(0.0f); Surface.Parameters.push_back(0.0f);
        }
        void Triangle(uint32_t A, uint32_t B, uint32_t C) { Surface.Triangles.push_back(A); Surface.Triangles.push_back(B); Surface.Triangles.push_back(C); }
        void Quad(Vec3 A, Vec3 B, Vec3 C, Vec3 D, Vec3 N)
        {
            uint32_t I = Surface.VertexCount();
            Vertex(A, N); Vertex(B, N); Vertex(C, N); Vertex(D, N);
            Triangle(I, I + 1, I + 2); Triangle(I, I + 2, I + 3);
        }
    };

    void BuildCone(GripBuild& B, Vec3 Foot, Vec3 Axis, Vec3 U, Vec3 V, double Radius, double Height, int Segments)
    {
        Vec3 Apex = Foot + Axis * Height;
        for (int I = 0; I < Segments; ++I)
        {
            double A0 = 2 * Pi * I / Segments, A1 = 2 * Pi * (I + 1) / Segments;
            Vec3 R0 = U * std::cos(A0) + V * std::sin(A0), R1 = U * std::cos(A1) + V * std::sin(A1);
            Vec3 P0 = Foot + R0 * Radius, P1 = Foot + R1 * Radius;
            Vec3 Rm = (R0 + R1).Normalised();
            Vec3 Side = (Rm * Height + Axis * Radius).Normalised();                     // cone slant normal
            uint32_t K = B.Surface.VertexCount();
            B.Vertex(P0, Side); B.Vertex(P1, Side); B.Vertex(Apex, Side); B.Triangle(K, K + 1, K + 2);
            K = B.Surface.VertexCount();
            B.Vertex(P1, Axis * -1.0); B.Vertex(P0, Axis * -1.0); B.Vertex(Foot, Axis * -1.0); B.Triangle(K, K + 1, K + 2);    // cap
        }
    }

    void BuildCylinder(GripBuild& B, Vec3 Centre, Vec3 Axis, Vec3 U, Vec3 V, double Radius, double Height, int Segments)
    {
        Vec3 Bottom = Centre - Axis * (Height * 0.5), Top = Centre + Axis * (Height * 0.5);
        for (int I = 0; I < Segments; ++I)
        {
            double A0 = 2 * Pi * I / Segments, A1 = 2 * Pi * (I + 1) / Segments;
            Vec3 R0 = U * std::cos(A0) + V * std::sin(A0), R1 = U * std::cos(A1) + V * std::sin(A1);
            uint32_t K = B.Surface.VertexCount();
            B.Vertex(Bottom + R0 * Radius, R0); B.Vertex(Bottom + R1 * Radius, R1); B.Vertex(Top + R1 * Radius, R1); B.Vertex(Top + R0 * Radius, R0);
            B.Triangle(K, K + 1, K + 2); B.Triangle(K, K + 2, K + 3);
            K = B.Surface.VertexCount();
            B.Vertex(Top, Axis); B.Vertex(Top + R0 * Radius, Axis); B.Vertex(Top + R1 * Radius, Axis); B.Triangle(K, K + 1, K + 2);
            K = B.Surface.VertexCount();
            B.Vertex(Bottom, Axis * -1.0); B.Vertex(Bottom + R1 * Radius, Axis * -1.0); B.Vertex(Bottom + R0 * Radius, Axis * -1.0); B.Triangle(K, K + 1, K + 2);
        }
    }

    void BuildSector(GripBuild& B, Vec3 Centre, Vec3 U, Vec3 V, Vec3 Normal, double Inner, double Outer, double Start, double End, int Segments)
    {
        for (int I = 0; I < Segments; ++I)
        {
            double A0 = Start + (End - Start) * I / Segments, A1 = Start + (End - Start) * (I + 1) / Segments;
            Vec3 R0 = U * std::cos(A0) + V * std::sin(A0), R1 = U * std::cos(A1) + V * std::sin(A1);
            B.Quad(Centre + R0 * Inner, Centre + R1 * Inner, Centre + R1 * Outer, Centre + R0 * Outer, Normal);
            B.Quad(Centre + R0 * Outer, Centre + R1 * Outer, Centre + R1 * Inner, Centre + R0 * Inner, Normal * -1.0);      // double sided
        }
    }

    void BuildTorus(GripBuild& B, Vec3 Centre, Vec3 U, Vec3 V, Vec3 N, double Radius, double Tube, int Major, int Minor)
    {
        for (int I = 0; I < Major; ++I)
        {
            double A0 = 2 * Pi * I / Major, A1 = 2 * Pi * (I + 1) / Major;
            Vec3 R0 = U * std::cos(A0) + V * std::sin(A0), R1 = U * std::cos(A1) + V * std::sin(A1);
            for (int J = 0; J < Minor; ++J)
            {
                double B0 = 2 * Pi * J / Minor, B1 = 2 * Pi * (J + 1) / Minor;
                auto Point = [&](Vec3 R, double Bb, Vec3& Nrm) { Nrm = R * std::cos(Bb) + N * std::sin(Bb); return Centre + R * Radius + Nrm * Tube; };
                Vec3 N00, N01, N10, N11;
                Vec3 P00 = Point(R0, B0, N00), P01 = Point(R0, B1, N01), P10 = Point(R1, B0, N10), P11 = Point(R1, B1, N11);
                uint32_t K = B.Surface.VertexCount();
                B.Vertex(P00, N00); B.Vertex(P10, N10); B.Vertex(P11, N11); B.Vertex(P01, N01);
                B.Triangle(K, K + 1, K + 2); B.Triangle(K, K + 2, K + 3);
            }
        }
    }

    // --- analytic ray tests --------------------------------------------------------------------------------------------
    // Distance between a ray and a segment, plus the parameters at closest approach.
    double RaySegmentDistance(const Ray& R, Vec3 A, Vec3 B, double& RayT)
    {
        Vec3 D = B - A; double L = D.Length(); if (L < 1e-12) { RayT = (A - R.Origin).Dot(R.Direction); return (R.At(RayT) - A).Length(); }
        Vec3 Du = D * (1.0 / L);
        Vec3 W = R.Origin - A;
        double Bd = R.Direction.Dot(Du), Dd = R.Direction.Dot(W), Ed = Du.Dot(W), Den = 1 - Bd * Bd;
        double S, T;
        if (Den < 1e-12) { S = 0; T = Ed; } else { S = (Bd * Ed - Dd) / Den; T = (Ed - Bd * Dd) / Den; }
        T = std::clamp(T, 0.0, L); S = std::max(S, 0.0);
        RayT = S;
        return (R.At(S) - (A + Du * T)).Length();
    }

    std::optional<double> RayPlane(const Ray& R, Vec3 Origin, Vec3 Normal)
    {
        double Den = R.Direction.Dot(Normal);
        if (std::fabs(Den) < 1e-9) return std::nullopt;
        double T = (Origin - R.Origin).Dot(Normal) / Den;
        if (T < 0) return std::nullopt;
        return T;
    }
}

const char* GizmoGripName(GizmoGrip Grip) noexcept
{
    switch (Grip)
    {
        case GizmoGrip::TranslateX: return "X move";   case GizmoGrip::TranslateY: return "Y move";   case GizmoGrip::TranslateZ: return "Z move";
        case GizmoGrip::ScaleX:     return "X scale";  case GizmoGrip::ScaleY:     return "Y scale";  case GizmoGrip::ScaleZ:     return "Z scale";
        case GizmoGrip::PlaneX:     return "X-plane move"; case GizmoGrip::PlaneY:  return "Y-plane move"; case GizmoGrip::PlaneZ: return "Z-plane move";
        case GizmoGrip::RotateX:    return "X rotate"; case GizmoGrip::RotateY:    return "Y rotate"; case GizmoGrip::RotateZ:    return "Z rotate";
        default: return "none";
    }
}

int TransformGizmo::AxisOf(GizmoGrip H) noexcept
{
    switch (H)
    {
        case GizmoGrip::TranslateX: case GizmoGrip::ScaleX: case GizmoGrip::PlaneX: case GizmoGrip::RotateX: return 0;
        case GizmoGrip::TranslateY: case GizmoGrip::ScaleY: case GizmoGrip::PlaneY: case GizmoGrip::RotateY: return 1;
        case GizmoGrip::TranslateZ: case GizmoGrip::ScaleZ: case GizmoGrip::PlaneZ: case GizmoGrip::RotateZ: return 2;
        default: return -1;
    }
}

void TransformGizmo::AimAt(const CameraProjection& Camera) noexcept
{
    AlignedAxis = -1;
    if (!Camera.Orthographic) return;
    Vec3 F = Camera.Forward();
    for (int A = 0; A < 3; ++A)
    {
        Vec3 Axis = AxisBasis(A).Dir;
        if (std::fabs(std::fabs(F.Dot(Axis)) - 1.0) < 1e-6) AlignedAxis = A;
    }
}

bool TransformGizmo::Visible(GizmoGrip H) const noexcept
{
    if (AlignedAxis >= 0)
    {
        int A = AxisOf(H);
        switch (H)
        {
            case GizmoGrip::TranslateX: case GizmoGrip::TranslateY: case GizmoGrip::TranslateZ:
            case GizmoGrip::ScaleX: case GizmoGrip::ScaleY: case GizmoGrip::ScaleZ:
                if (A == AlignedAxis) return false;                                                // cannot move along the view line
                break;
            case GizmoGrip::PlaneX: case GizmoGrip::PlaneY: case GizmoGrip::PlaneZ:
                if (A != AlignedAxis) return false;                                                // only the screen-parallel plane
                break;
            case GizmoGrip::RotateX: case GizmoGrip::RotateY: case GizmoGrip::RotateZ:
                if (A != AlignedAxis) return false;                                                // only the ring facing the camera
                break;
            default: break;
        }
    }
    if (Layout == GizmoLayout::Combined) return true;
    switch (H)
    {
        case GizmoGrip::TranslateX: case GizmoGrip::TranslateY: case GizmoGrip::TranslateZ:
        case GizmoGrip::PlaneX: case GizmoGrip::PlaneY: case GizmoGrip::PlaneZ: return Layout == GizmoLayout::Translate;
        case GizmoGrip::ScaleX: case GizmoGrip::ScaleY: case GizmoGrip::ScaleZ: return Layout == GizmoLayout::Scale;
        case GizmoGrip::RotateX: case GizmoGrip::RotateY: case GizmoGrip::RotateZ: return Layout == GizmoLayout::Rotate;
        default: return false;
    }
}

TransformGizmo::Basis TransformGizmo::AxisBasis(int Axis) const noexcept
{
    // OTHERS in Gizmo.html: x → (y, z), y → (z, x), z → (x, y)
    const Vec3 Unit[3] = { Vec3::UnitX(), Vec3::UnitY(), Vec3::UnitZ() };
    Basis B;
    B.Dir = Pivot.Orientation.Rotate(Unit[Axis]);
    B.U   = Pivot.Orientation.Rotate(Unit[(Axis + 1) % 3]);
    B.V   = Pivot.Orientation.Rotate(Unit[(Axis + 2) % 3]);
    return B;
}

double TransformGizmo::WorldSize(const CameraProjection& Camera, uint32_t ViewportHeight) const noexcept
{
    // metres per pixel at the pivot depth, times the requested pixel length
    double HalfHeight;
    if (Camera.Orthographic) HalfHeight = Camera.OrthographicHalfHeight();
    else HalfHeight = std::max(1e-6, (Pivot.Origin - Camera.Eye()).Dot(Camera.Forward())) * std::tan(Camera.FovY * 0.5);
    return PixelSize * (2.0 * HalfHeight / double(ViewportHeight));
}

GizmoGrip TransformGizmo::Locate(double PixelX, double PixelY, const CameraProjection& Camera, uint32_t Width, uint32_t Height) const noexcept
{
    const Ray R = Camera.PixelRay(PixelX, PixelY, Width, Height);
    const double L = WorldSize(Camera, Height);
    const double PixelToWorld = L / PixelSize;
    const double Slack = 4.0 * PixelToWorld;                                            // [m] ~4 px tolerance for thin parts
    GizmoGrip Best = GizmoGrip::None; double BestT = 1e300;
    auto Consider = [&](GizmoGrip H, double T) { if (Visible(H) && T < BestT) { BestT = T; Best = H; } };

    for (int Axis = 0; Axis < 3; ++Axis)
    {
        Basis B = AxisBasis(Axis);
        const Vec3 O = Pivot.Origin;
        // cone: segment from TIP to TIP+height, radius cone foot (generous, like three.js Raycaster on a cone tessellation)
        double T;
        Vec3 ConeFoot = O + B.Dir * (Tip * L), ConeApex = O + B.Dir * ((Tip + ConeHeight) * L);
        if (RaySegmentDistance(R, ConeFoot, ConeApex, T) <= ConeRadius * L + Slack) Consider(static_cast<GizmoGrip>(int(GizmoGrip::TranslateX) + Axis), T);
        // scale cylinder
        Vec3 Sc = O + B.Dir * ((Tip - ScaleOffset) * L);
        if (RaySegmentDistance(R, Sc - B.Dir * (ScaleHeight * 0.5 * L), Sc + B.Dir * (ScaleHeight * 0.5 * L), T) <= ScaleRadius * L + Slack)
            Consider(static_cast<GizmoGrip>(int(GizmoGrip::ScaleX) + Axis), T);
        // plane quad
        if (auto Tp = RayPlane(R, O, B.Dir))
        {
            Vec3 P = R.At(*Tp) - O;
            double Cu = (Tip - PlaneHalf) * L, Cv = Cu;
            double Du = P.Dot(B.U) - Cu, Dv = P.Dot(B.V) - Cv;
            if (std::fabs(Du) <= PlaneHalf * L + Slack && std::fabs(Dv) <= PlaneHalf * L + Slack) Consider(static_cast<GizmoGrip>(int(GizmoGrip::PlaneX) + Axis), *Tp);
            // rotate sector (same plane)
            double Radius = P.Length();
            double Rad = ArcRadiusFactor * Tip * L;
            if (std::fabs(Radius - Rad) <= ArcBand * L + Slack)
            {
                double Angle = std::atan2(P.Dot(B.V), P.Dot(B.U));
                double Half = ArcSweepDegrees * 0.5 * Pi / 180.0;
                double Rel = Angle - Pi / 4;
                if (std::fabs(Rel) <= Half + Slack / Rad) Consider(static_cast<GizmoGrip>(int(GizmoGrip::RotateX) + Axis), *Tp);
            }
        }
    }
    return Best;
}

Vec3 TransformGizmo::GripAnchor(GizmoGrip H, const CameraProjection& Camera, uint32_t ViewportHeight) const noexcept
{
    const double L = WorldSize(Camera, ViewportHeight);
    int Axis = AxisOf(H);
    if (Axis < 0) return Pivot.Origin;
    Basis B = AxisBasis(Axis);
    switch (H)
    {
        case GizmoGrip::TranslateX: case GizmoGrip::TranslateY: case GizmoGrip::TranslateZ: return Pivot.Origin + B.Dir * ((Tip + ConeHeight * 0.4) * L);
        case GizmoGrip::ScaleX: case GizmoGrip::ScaleY: case GizmoGrip::ScaleZ:             return Pivot.Origin + B.Dir * ((Tip - ScaleOffset) * L);
        case GizmoGrip::PlaneX: case GizmoGrip::PlaneY: case GizmoGrip::PlaneZ:             return Pivot.Origin + (B.U + B.V) * ((Tip - PlaneHalf) * L);
        default: { double R = ArcRadiusFactor * Tip * L; return Pivot.Origin + (B.U + B.V) * (R / std::sqrt(2.0)); }
    }
}

double TransformGizmo::AxisParameter(const Ray& R, Vec3 AxisDir) const noexcept
{
    // Closest point on the axis line (origin Pivot, dir AxisDir) to the ray: standard skew-line solve.
    Vec3 W = Pivot.Origin - R.Origin;
    double B = AxisDir.Dot(R.Direction), D = AxisDir.Dot(W), E = R.Direction.Dot(W), Den = 1 - B * B;
    if (std::fabs(Den) < 1e-9) return 0.0;
    return (B * E - D) / Den;
}

std::optional<Vec3> TransformGizmo::PlanePoint(const Ray& R, Vec3 Normal) const noexcept
{
    double Den = R.Direction.Dot(Normal);
    if (std::fabs(Den) < 1e-9) return std::nullopt;
    double T = (Pivot.Origin - R.Origin).Dot(Normal) / Den;
    return R.At(T);
}

std::optional<double> TransformGizmo::AngleAround(const Ray& R, Vec3 AxisDir, Vec3 Reference) const noexcept
{
    auto P = PlanePoint(R, AxisDir);
    if (!P) return std::nullopt;
    Vec3 D = *P - Pivot.Origin;
    Vec3 Second = AxisDir.Cross(Reference);
    return std::atan2(D.Dot(Second), D.Dot(Reference));
}

bool TransformGizmo::BeginDrag(double PixelX, double PixelY, const CameraProjection& Camera, uint32_t Width, uint32_t Height) noexcept
{
    GizmoGrip H = Locate(PixelX, PixelY, Camera, Width, Height);
    if (H == GizmoGrip::None) return false;
    Active = {}; Active.Grip = H; Hover = H;
    Basis B = AxisBasis(AxisOf(H));
    AxisWorld = B.Dir; UWorld = B.U; VWorld = B.V; NormalWorld = B.Dir; ReferenceWorld = B.U;
    SizeAtStart = WorldSize(Camera, Height);
    const Ray R = Camera.PixelRay(PixelX, PixelY, Width, Height);
    switch (H)
    {
        case GizmoGrip::TranslateX: case GizmoGrip::TranslateY: case GizmoGrip::TranslateZ:
        case GizmoGrip::ScaleX: case GizmoGrip::ScaleY: case GizmoGrip::ScaleZ:
            StartParameter = AxisParameter(R, AxisWorld); break;
        case GizmoGrip::PlaneX: case GizmoGrip::PlaneY: case GizmoGrip::PlaneZ:
            StartHit = PlanePoint(R, NormalWorld).value_or(Pivot.Origin); break;
        default:
            StartAngle = AngleAround(R, AxisWorld, ReferenceWorld).value_or(0.0); break;
    }
    Active.Readout = std::string(GizmoGripName(H)) + " 0";
    return true;
}

bool TransformGizmo::UpdateDrag(double PixelX, double PixelY, bool Snapping, const CameraProjection& Camera, uint32_t Width, uint32_t Height) noexcept
{
    if (!Dragging()) return false;
    const Ray R = Camera.PixelRay(PixelX, PixelY, Width, Height);
    char Text[96] = {};
    const int Axis = std::clamp(AxisOf(Active.Grip), 0, 2);
    const char AxisLetter = "XYZ"[Axis];
    switch (Active.Grip)
    {
        case GizmoGrip::TranslateX: case GizmoGrip::TranslateY: case GizmoGrip::TranslateZ:
        {
            double Delta = AxisParameter(R, AxisWorld) - StartParameter;
            if (Snapping) Delta = std::round(Delta / SnapMove) * SnapMove;
            Active.Amount = Delta;
            Active.Delta = Mat4::Translation(AxisWorld * Delta);
            std::snprintf(Text, sizeof Text, "%c move %.3f", AxisLetter, Delta);
            break;
        }
        case GizmoGrip::ScaleX: case GizmoGrip::ScaleY: case GizmoGrip::ScaleZ:
        {
            // Gizmo.html: one gizmo unit of travel = +100 %; the gizmo unit is L on screen
            double Delta = (AxisParameter(R, AxisWorld) - StartParameter) / SizeAtStart;
            double Factor = 1.0 + Delta;
            if (Snapping) Factor = std::round(Factor / SnapScale) * SnapScale;
            Factor = std::max(Factor, 0.05);
            Active.Amount = Factor;
            Vec3 S = Vec3{ 1, 1, 1 }; if (Axis == 0) S.X = Factor; else if (Axis == 1) S.Y = Factor; else S.Z = Factor;
            // scale about the pivot along the local fit
            Mat4 ToLocal = Mat4::Rotation(Pivot.Orientation);
            Mat4 FromLocal = Mat4::Rotation(Pivot.Orientation.Conjugate());
            Active.Delta = Mat4::Translation(Pivot.Origin) * ToLocal * Mat4::Scaling(S) * FromLocal * Mat4::Translation(Pivot.Origin * -1.0);
            std::snprintf(Text, sizeof Text, "%c scale %.3fx", AxisLetter, Factor);
            break;
        }
        case GizmoGrip::PlaneX: case GizmoGrip::PlaneY: case GizmoGrip::PlaneZ:
        {
            auto Hit = PlanePoint(R, NormalWorld);
            if (!Hit) return true;
            Vec3 D = *Hit - StartHit;
            double Du = D.Dot(UWorld), Dv = D.Dot(VWorld);
            if (Snapping) { Du = std::round(Du / SnapMove) * SnapMove; Dv = std::round(Dv / SnapMove) * SnapMove; }
            Vec3 Move = UWorld * Du + VWorld * Dv;
            Active.Amount = Move.Length();
            Active.Delta = Mat4::Translation(Move);
            std::snprintf(Text, sizeof Text, "%c-plane move %.3f %.3f", AxisLetter, Du, Dv);
            break;
        }
        default:
        {
            auto A = AngleAround(R, AxisWorld, ReferenceWorld);
            if (!A) return true;
            double Angle = *A - StartAngle;
            while (Angle > Pi) Angle -= 2 * Pi;
            while (Angle < -Pi) Angle += 2 * Pi;
            if (Snapping) { double Step = SnapAngleDegrees * Pi / 180.0; Angle = std::round(Angle / Step) * Step; }
            Active.Amount = Angle;
            Active.Delta = Mat4::Translation(Pivot.Origin) * Mat4::Rotation(AxisWorld, Angle) * Mat4::Translation(Pivot.Origin * -1.0);
            std::snprintf(Text, sizeof Text, "%c rotate %.1f°", AxisLetter, Angle * 180.0 / Pi);
            break;
        }
    }
    Active.Readout = Text;
    return true;
}

GizmoDrag TransformGizmo::EndDrag() noexcept
{
    GizmoDrag Result = Active;
    Active = {};
    return Result;
}

void TransformGizmo::Draw(RasterExchange& Raster, const CameraProjection& Camera, uint32_t Width, uint32_t Height) const noexcept
{
    (void)Width;
    const double L = WorldSize(Camera, Height);
    const Vec3 O = Pivot.Origin;
    const GizmoGrip Lit = Dragging() ? Active.Grip : Hover;

    auto Record = [&](const float* Rgb, float Alpha, GizmoGrip H)
    {
        DrawRecord D;
        D.Tint[0] = Rgb[0]; D.Tint[1] = Rgb[1]; D.Tint[2] = Rgb[2]; D.Tint[3] = Alpha;
        D.Shading = static_cast<uint8_t>(SurfaceShading::Plastic);
        D.Emissive = (H == Lit) ? 0.5f : 0.0f;                                          // Gizmo.html hover: emissive 0x555555
        D.LineWidth = 1.5f;
        return D;
    };

    // Rotate sectors first (largest, semi-flat), then planes, then scale + translate, then the ring on top.
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        GizmoGrip H = static_cast<GizmoGrip>(int(GizmoGrip::RotateX) + Axis);
        if (!Visible(H)) continue;
        Basis B = AxisBasis(Axis);
        GripBuild Build;
        double Rad = ArcRadiusFactor * Tip * L, Half = ArcSweepDegrees * 0.5 * Pi / 180.0;
        BuildSector(Build, O, B.U, B.V, B.Dir, Rad - ArcBand * L, Rad + ArcBand * L, Pi / 4 - Half, Pi / 4 + Half, 12);
        Raster.DrawSurface(Build.Surface, Record(AxisColour[Axis], 1.0f, H));
    }
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        GizmoGrip H = static_cast<GizmoGrip>(int(GizmoGrip::PlaneX) + Axis);
        if (!Visible(H)) continue;
        Basis B = AxisBasis(Axis);
        double C = (Tip - PlaneHalf) * L, Hh = PlaneHalf * L;
        Vec3 Centre = O + (B.U + B.V) * C;
        GripBuild Build;
        Build.Quad(Centre - B.U * Hh - B.V * Hh, Centre + B.U * Hh - B.V * Hh, Centre + B.U * Hh + B.V * Hh, Centre - B.U * Hh + B.V * Hh, B.Dir);
        Build.Quad(Centre - B.U * Hh + B.V * Hh, Centre + B.U * Hh + B.V * Hh, Centre + B.U * Hh - B.V * Hh, Centre - B.U * Hh - B.V * Hh, B.Dir * -1.0);
        DrawRecord Fill = Record(PlaneColour[Axis], H == Lit ? 0.55f : 0.28f, H);
        Fill.Shading = static_cast<uint8_t>(SurfaceShading::Flat);
        Raster.DrawSurface(Build.Surface, Fill);
        // L-shaped edge: outer corner, then along −u and −v by the full side length
        SegmentStream Edge;
        Vec3 Outer = Centre + B.U * Hh + B.V * Hh;
        Edge.Append(Outer, Outer - B.U * (2 * Hh));
        Edge.Append(Outer, Outer - B.V * (2 * Hh));
        Raster.DrawSegments(Edge, Record(PlaneColour[Axis], 1.0f, H));
    }
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        Basis B = AxisBasis(Axis);
        GizmoGrip Hs = static_cast<GizmoGrip>(int(GizmoGrip::ScaleX) + Axis);
        if (Visible(Hs))
        {
            GripBuild Build;
            BuildCylinder(Build, O + B.Dir * ((Tip - ScaleOffset) * L), B.Dir, B.U, B.V, ScaleRadius * L, ScaleHeight * L, 16);
            Raster.DrawSurface(Build.Surface, Record(AxisColour[Axis], 1.0f, Hs));
        }
        GizmoGrip Ht = static_cast<GizmoGrip>(int(GizmoGrip::TranslateX) + Axis);
        if (Visible(Ht))
        {
            GripBuild Build;
            BuildCone(Build, O + B.Dir * (Tip * L), B.Dir, B.U, B.V, ConeRadius * L, ConeHeight * L, 24);
            Raster.DrawSurface(Build.Surface, Record(AxisColour[Axis], 1.0f, Ht));
        }
    }
    // Billboarded white ring
    {
        GripBuild Build;
        Vec3 N = Camera.Forward() * -1.0, U = Camera.Right(), V = Camera.Up();
        BuildTorus(Build, O, U, V, N, RingRadius * L, RingTube * L, 32, 6);
        const float White[3] = { 1.0f, 1.0f, 1.0f };
        Raster.DrawSurface(Build.Surface, Record(White, 1.0f, GizmoGrip::None));
    }
}

} // namespace Frontier
