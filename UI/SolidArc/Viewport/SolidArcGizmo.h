/*====================================================================================================================================
                                                          SOLIDARCGIZMO.H
====================================================================================================================================*/
// 🧩 Transform gizmo (Slate GizmoPRO look): CPU-built handle triangles drawn by the GPU program, CPU pick and drag maths

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Geometry/VectorAlgebra.h"
#include "Engine/Render/OrbitCamera.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace Frontier::SolidArc
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Mode bits: any combination (move + rotate, rotate + scale, all three …); 0 hides the gizmo.
constexpr uint32_t GizmoTranslate = 1u;                                                      // Cones + plane quads (+ centre ring: screen move)
constexpr uint32_t GizmoRotate    = 2u;                                                      // Arcs
constexpr uint32_t GizmoScale     = 4u;                                                      // Cylinders (+ centre ring: uniform, without move)

constexpr double GizmoPixels = 150.0;                                                        // [px] - Screen length of the unit axis L

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

enum class SolidArcGizmoHandle : uint8_t
{
    None       = 0,
    TranslateX = 1,                                                                          // Cone on +x
    TranslateY = 2,
    TranslateZ = 3,
    PlaneYZ    = 4,                                                                          // Quad between y and z (normal x)
    PlaneXZ    = 5,
    PlaneXY    = 6,
    RotateX    = 7,                                                                          // Arc in the y–z plane (about x)
    RotateY    = 8,
    RotateZ    = 9,
    ScaleX     = 10,                                                                         // Cylinder on +x
    ScaleY     = 11,
    ScaleZ     = 12,
    Centre     = 13,                                                                         // White ring facing the eye
};

// 📝 32-byte vertex of the gizmo program (Shaders/SolidArcGizmo.slang). Normal (0, 0, 0) = unlit (quads, arcs, ring,
//    edge strokes); Colour is RGBA8 (R in the low byte) read as UNORM; Handle is the owning handle (CPU pick only).
struct SolidArcGizmoVertex
{
    float    Position[3] = {};                                                               // [len] - World point
    float    Normal[3]   = {};                                                               // [-]   - Shading normal (0: unlit)
    uint32_t Colour      = 0;                                                                // [-]   - RGBA8
    uint32_t Handle      = 0;                                                                // [-]   - SolidArcGizmoHandle
};
static_assert(sizeof(SolidArcGizmoVertex) == 32, "SolidArcGizmoVertex must stay 32 bytes");

struct SolidArcGizmoPose
{
    GeometryProcessing::Vector3 Origin  = {};                                                // [len] - Gizmo centre (pivot)
    GeometryProcessing::Vector3 Axes[3] = { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } }; // [-] - Right-handed unit frame
    double                      Size    = 1.0;                                               // [len] - World length of L (constant on screen)
    uint32_t                    Modes   = 0;                                                 // [-]   - GizmoTranslate | GizmoRotate | GizmoScale
    uint32_t                    Hidden  = 0;                                                 // [-]   - Bit (1 << handle): handle left out
};

// 📝 The CPU copy of everything the GPU draws: the same triangles serve the pick, so what is seen is what is grabbed.
struct SolidArcGizmoGeometry
{
    std::vector<SolidArcGizmoVertex> Vertices    = {};                                       // [-] - Triangle list: opaque first, then translucent
    uint32_t                         OpaqueCount = 0;                                        // [-] - Vertices before the translucent quads
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// World length that spans Pixels on screen at Origin's depth (perspective or orthographic).
[[nodiscard]] double MeasureGizmoSize(const OrbitCameraFrame& Frame, GeometryProcessing::Vector3 Origin, double Pixels);

// Handle triangles in world space: Hover / Grabbed glow (Slate emissive #555555), the ring billboarded to Frame.
[[nodiscard]] SolidArcGizmoGeometry BuildSolidArcGizmo(const SolidArcGizmoPose& Pose, const OrbitCameraFrame& Frame, SolidArcGizmoHandle Hover,
                                                       SolidArcGizmoHandle Grabbed);

// Handle under the viewport pixel (local, top-left origin): inside a projected triangle or within Reach pixels of one
// (inside first, opaque handles before see-through plane quads, then nearest); None when the pointer misses the gizmo.
[[nodiscard]] SolidArcGizmoHandle PickSolidArcGizmo(const SolidArcGizmoGeometry& Geometry, const OrbitCameraFrame& Frame, double PixelX, double PixelY,
                                                    double Reach);

// Axis index 0 / 1 / 2 a handle acts along / about (−1: plane-free or centre).
[[nodiscard]] int GizmoHandleAxis(SolidArcGizmoHandle Handle) noexcept;
[[nodiscard]] const char* GizmoHandleLabel(SolidArcGizmoHandle Handle) noexcept;

// 💡 Drag maths (Slate GizmoPRO): the axis parameter of the point on Origin + t·Axis closest to the pointer ray (skew
//    lines; empty when they run parallel), the ray / plane hit, and the angle of that hit about Axis measured from U.
[[nodiscard]] std::optional<double> MeasureAxisParameter(const OrbitRay& Ray, GeometryProcessing::Vector3 Origin, GeometryProcessing::Vector3 Axis);
[[nodiscard]] std::optional<GeometryProcessing::Vector3> IntersectGizmoPlane(const OrbitRay& Ray, GeometryProcessing::Vector3 Origin,
                                                                             GeometryProcessing::Vector3 Normal);
[[nodiscard]] std::optional<double> MeasureGizmoAngle(const OrbitRay& Ray, GeometryProcessing::Vector3 Origin, GeometryProcessing::Vector3 Axis,
                                                      GeometryProcessing::Vector3 Reference);

} // namespace Frontier::SolidArc
