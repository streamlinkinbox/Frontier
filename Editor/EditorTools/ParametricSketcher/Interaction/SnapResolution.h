//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Interaction/SnapResolution.h — Pixel-radius snapping: lattice, geometry points, on-curve, axis alignment, intersections
//============================================================================================================================================
// Input is a pixel and a camera; candidates are ranked by pixel distance with a priority tie-break (endpoint beats
//    midpoint beats on-curve beats lattice). Every candidate reports what it snapped to so the console can print it and
//    the presentation can draw the matching glyph. Priorities mirror Plasticity: geometry always wins over the lattice.
#pragma once

#include "CameraProjection.h"
#include "Document/SceneDocument.h"
#include <string>

namespace Frontier
{

enum class SnapClassification : uint8_t
{
    None,
    Lattice,                                                                               // workplane lattice intersection
    Endpoint,
    Midpoint,
    Centre,                                                                             // circle / arc / ellipse centre
    Quadrant,                                                                           // 0/90/180/270° on circular curves
    ControlPoint,
    OnCurve,                                                                            // nearest point on a curve
    Perpendicular,                                                                      // foot of perpendicular from the anchor
    Tangent,                                                                            // tangent from the anchor to a circle
    Intersection,                                                                       // two curves crossing (planar)
    AxisX, AxisY, AxisZ,                                                                // aligned with the anchor along an axis
    Origin,
    Free                                                                                // workplane hit, nothing snapped
};

[[nodiscard]] const char* SnapName(SnapClassification Classification) noexcept;

struct SnapCandidate
{
    SnapClassification Classification = SnapClassification::None;                                                     // [-]
    Vec3     Position;                                                                  // [m] world
    double   PixelDistance = ScalarCriteria::Infinity;                                  // [px]
    uint32_t FigureIdentity = 0;                                                          // [-] the curve it belongs to (0 = none)
    double   Parameter = 0.0;                                                           // [-] curve parameter when on a curve
    int      Priority = 0;                                                              // [-] higher wins ties within the radius
};

struct SnapSettings
{
    bool   Enabled = true;                                                              // [-] master toggle (Ctrl inverts)
    bool   Lattice = true;                                                                 // [-]
    bool   Geometry = true;                                                             // [-] endpoints/midpoints/centres/quadrants/control points
    bool   OnCurve = true;                                                              // [-]
    bool   Axis = true;                                                                 // [-] orthogonal alignment with the anchor
    bool   Intersections = true;                                                        // [-]
    double Radius = 12.0;                                                               // [px]
    double LatticeStep = 1.0;                                                              // [m]
    double AngleStep = ScalarCriteria::Radians(15.0);                                   // [rad] angle quantisation with Ctrl held
};

class SnapResolution
{
public:
    // Anchor is the previous confirmed point of the running tool (for axis/perpendicular/tangent snaps); nullptr = none.
    [[nodiscard]] static SnapCandidate Resolve(double PixelX, double PixelY, const CameraProjection& Camera, uint32_t Width, uint32_t Height,
                                               const Workplane& Plane, const SceneDocument& Scene, const SnapSettings& Settings,
                                               const Vec3* Anchor, uint32_t IgnoreIdentity = 0) noexcept;

    // The plain workplane hit under a pixel (no snapping); false when the ray is parallel to the plane.
    [[nodiscard]] static bool PlaneHit(double PixelX, double PixelY, const CameraProjection& Camera, uint32_t Width, uint32_t Height,
                                       const Workplane& Plane, Vec3& Out) noexcept;

    [[nodiscard]] static std::vector<SnapCandidate> GeometryCandidates(const SceneDocument& Scene, uint32_t IgnoreIdentity) noexcept;
};

} // namespace Frontier
