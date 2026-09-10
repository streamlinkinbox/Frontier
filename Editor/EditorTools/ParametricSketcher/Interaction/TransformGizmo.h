//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Interaction/TransformGizmo.h — GizmoPRO: the combined translate / scale / plane / rotate grip set from Gizmo.html
//============================================================================================================================================
// Geometry and behaviour are transcribed from References/Gizmo.html (three.js prototype) so the two stay in step:
//    · translate: cone tip only at TIP = 0.95·L along each axis (radius 0.06, height 0.18), no axis line
//    · scale:     short cylinder (r 0.06, h 0.14) at TIP − 0.28, same radius as the cone base → one continuous grip
//    · plane:     0.16 × 0.16 semi-transparent quad at (u+v)·(TIP − 0.08) in the plane of the OTHER two axes, framed by
//                 two opaque edges meeting at the outer corner; colours cyan (X plane), magenta (Y), yellow (Z)
//    · rotate:    flat annular sector r = 0.62·TIP, band ±0.038, sweep 31°, centred on the u→v bisector of the other axes
//    · ring:      white torus r 0.16, tube 0.008, always facing the camera
//    Drag maths: axis grips use the closest point on the axis line to the pointer ray; plane and rotate grips
//    intersect the pointer ray with the plane through the pivot; Ctrl snaps to 0.25 m / 0.1× / 5°.
//    The gizmo is drawn in the overlay segment at a constant pixel size (L scales with camera distance) and picks
//    through its own analytic ray tests, not the pick buffer, so it works before anything is rendered.
#pragma once

#include "CameraProjection.h"
#include "InputEvent.h"
#include "Presentation/RasterExchange.h"
#include <optional>
#include <string>

namespace Frontier
{

enum class GizmoGrip : uint8_t
{
    None,
    TranslateX, TranslateY, TranslateZ,
    ScaleX, ScaleY, ScaleZ,
    PlaneX, PlaneY, PlaneZ,                                                             // slide in the plane whose normal is that axis
    RotateX, RotateY, RotateZ,
};
[[nodiscard]] const char* GizmoGripName(GizmoGrip Grip) noexcept;

enum class GizmoLayout : uint8_t { Combined, Translate, Rotate, Scale };                // GizmoPRO shows everything; separable modes sift

struct GizmoDrag
{
    GizmoGrip Grip = GizmoGrip::None;                                             // [-]
    Mat4        Delta;                                                                  // [-] world transform applied to the target so far
    double      Amount = 0.0;                                                           // [m] / [-] / [rad] depending on the grip
    std::string Readout;                                                                // [-] "X  move 1.250"
};

class TransformGizmo
{
public:
    struct PivotBasis
    {
        Vec3 Origin;                                                                    // [m] pivot
        Quat Orientation;                                                               // [-] target fit; identity = world axes
    };

    void   Anchor(const PivotBasis& F) noexcept { Pivot = F; }
    [[nodiscard]] const PivotBasis& CurrentPivot() const noexcept { return Pivot; }
    void   Arrange(GizmoLayout L) noexcept { Layout = L; }
    [[nodiscard]] GizmoLayout CurrentLayout() const noexcept { return Layout; }
    void   Resize(double Pixels) noexcept { PixelSize = Pixels; }                 // [px] length of L on screen
    [[nodiscard]] double WorldSize(const CameraProjection& Camera, uint32_t ViewportHeight) const noexcept;   // L in metres

    // World-space anchor of a grip (cone base, puck centre, quad centre, sector midpoint) — for scripted pointer driving.
    [[nodiscard]] Vec3 GripAnchor(GizmoGrip H, const CameraProjection& Camera, uint32_t ViewportHeight) const noexcept;
    // Hover test: nearest grip hit by the pixel ray (within a small pixel tolerance for thin parts).
    [[nodiscard]] GizmoGrip Locate(double PixelX, double PixelY, const CameraProjection& Camera, uint32_t Width, uint32_t Height) const noexcept;
    [[nodiscard]] GizmoGrip Hovered() const noexcept { return Hover; }
    void MarkHovered(GizmoGrip H) noexcept { Hover = H; }

    // Drag lifecycle. Begin returns false when the pixel hits no grip.
    bool BeginDrag(double PixelX, double PixelY, const CameraProjection& Camera, uint32_t Width, uint32_t Height) noexcept;
    bool UpdateDrag(double PixelX, double PixelY, bool Snapping, const CameraProjection& Camera, uint32_t Width, uint32_t Height) noexcept;
    [[nodiscard]] const GizmoDrag& Drag() const noexcept { return Active; }
    [[nodiscard]] bool Dragging() const noexcept { return Active.Grip != GizmoGrip::None; }
    GizmoDrag EndDrag() noexcept;

    // Draw into the overlay of a bound raster (grips, edges, ring). Highlights the hovered / dragged grip.
    void Draw(RasterExchange& Raster, const CameraProjection& Camera, uint32_t Width, uint32_t Height) const noexcept;

    // Constants from Gizmo.html (unit L = 1).
    static constexpr double Tip = 0.95, ConeRadius = 0.06, ConeHeight = 0.18, ScaleRadius = 0.06, ScaleHeight = 0.14, ScaleOffset = 0.28;
    static constexpr double PlaneHalf = 0.08, ArcRadiusFactor = 0.62, ArcSweepDegrees = 31.0, ArcBand = 0.038, RingRadius = 0.16, RingTube = 0.008;
    static constexpr double SnapMove = 0.25, SnapScale = 0.1, SnapAngleDegrees = 5.0;

    [[nodiscard]] bool Visible(GizmoGrip H) const noexcept;
    // Orthographic views hide what cannot act: an axis parallel to the view direction loses its arrow, scale grip and the
    //    two plane grips that contain it; only its rotation ring stays (that is the one rotation the view can show).
    void AimAt(const CameraProjection& Camera) noexcept;
    [[nodiscard]] int  ViewAxis() const noexcept { return AlignedAxis; }                // -1 = free (perspective / oblique)

private:
    struct Basis { Vec3 Dir; Vec3 U; Vec3 V; };                                         // axis, and the OTHER two axes (u, v) in world
    [[nodiscard]] Basis AxisBasis(int Axis) const noexcept;
    [[nodiscard]] double AxisParameter(const Ray& R, Vec3 AxisDir) const noexcept;      // skew-line closest point along the axis from the pivot
    [[nodiscard]] std::optional<Vec3> PlanePoint(const Ray& R, Vec3 Normal) const noexcept;
    [[nodiscard]] std::optional<double> AngleAround(const Ray& R, Vec3 AxisDir, Vec3 Reference) const noexcept;
    [[nodiscard]] static int AxisOf(GizmoGrip H) noexcept;                            // 0 X, 1 Y, 2 Z

    PivotBasis Pivot;
    GizmoLayout Layout = GizmoLayout::Combined;
    int         AlignedAxis = -1;                                                       // [-] orthographic view axis, -1 free
    double      PixelSize = 110.0;                                                      // [px]
    GizmoGrip Hover = GizmoGrip::None;
    GizmoDrag   Active;
    // drag start state
    double      StartParameter = 0.0;
    Vec3        StartHit;
    double      StartAngle = 0.0;
    Vec3        AxisWorld, NormalWorld, ReferenceWorld, UWorld, VWorld;
    double      SizeAtStart = 1.0;
};

} // namespace Frontier
