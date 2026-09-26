//============================================================================================================================================
//                                                       GIZMOFIGURES.H
//============================================================================================================================================
// 🧩 The transform gizmo's geometry and pointer arithmetic, generated on the CPU and drawn on the GPU.
//
//    The visual is a 1:1 reproduction of References/Gizmo.html (the Slate reference): the same cones, the same
//    short cylinders, the same corner quads with their two opaque edges, the same flat annular arcs and the same
//    billboarded white ring, with the reference's own radii, sweeps, offsets and tints written out below as named
//    figures. The reference draws all three modes at once; the engine draws each mode individually —
//    Translate shows the cones, the corner quads and the ring; Rotate shows the arcs and the ring; Scale shows
//    the cylinders and the ring — exactly the grips that mode drags.
//
//    The pointer arithmetic (closest point along an axis under a ray, ray ∧ plane, angle around an axis) is the
//    reference's own, ported line for line, and the drag rules are Blender's: a grab moves along the grip's axis
//    or plane, a scale multiplies from the drag's start with a 0.05 floor, a turn measures the angle swept in the
//    grip's plane, and Ctrl snaps to 0.25 units / 0.1× / 5°.
//
//    Everything here is dependency-free CPU arithmetic so the editor proof compiles and drives this exact text;
//    the engine's GizmoExchange draws the emitted vertices with Engine/Shaders/GizmoRaster.*.slang.

#pragma once

#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                     VOCABULARY
//------------------------------------------------------------------------------------------------------------------------

enum class GizmoMode : uint32_t
{
    Translate = 0u,
    Rotate    = 1u,
    Scale     = 2u,
};

// Every grip the pointer can seize. The reference registers the cones, the cylinders, the corner quads and the
//    arcs; the ring is decoration in both builds.
enum class GizmoGrip : uint32_t
{
    None   = 0u,
    MoveX  = 1u,  MoveY  = 2u,  MoveZ  = 3u,   // translate cones
    PlaneX = 4u,  PlaneY = 5u,  PlaneZ = 6u,   // corner quads (X names the YZ quad, as the reference does)
    ScaleX = 7u,  ScaleY = 8u,  ScaleZ = 9u,   // scale cylinders
    TurnX  = 10u, TurnY  = 11u, TurnZ  = 12u,  // rotation arcs
};

// One emitted vertex: the GPU shader (GizmoRaster) and the CPU proof shade this identically through
//    Engine/Shaders/GizmoRecords.slang. Unlit pieces (quads, edges, ring) keep their tint exactly.
struct GizmoVertex
{
    float Location[3];   // [m]  world
    float Emissive;      // [-]  the hover lift, 0 at rest
    float Normal[3];     // [-]  unit; unused when Unlit
    float Unlit;         // [-]  1 keeps the tint exactly, 0 shades ambient + directional
    float Tint[4];       // [-]  linear rgb + opacity (the corner quads are the only translucent pieces)
    float Spare[4];      // [-]  keeps the extent at 64 bytes, the GPU mirror's stride
};
static_assert(sizeof(GizmoVertex) == 64u, "GizmoVertex must match the GPU mirror's 64-byte stride");

// Where the gizmo sits and how its axes point: the columns of the seized instance's placement, normalised, so
//    the grips ride the object's own axes exactly as the reference's gizmo copies its target's quaternion.
struct GizmoPose
{
    float Origin[3] = { 0.0f, 0.0f, 0.0f };
    float AxisX[3]  = { 1.0f, 0.0f, 0.0f };
    float AxisY[3]  = { 0.0f, 1.0f, 0.0f };
    float AxisZ[3]  = { 0.0f, 0.0f, 1.0f };
    float Reach     = 1.0f;   // [-] overall size; the reference is authored at 1
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 THE REFERENCE'S FIGURES
//------------------------------------------------------------------------------------------------------------------------
// Verbatim from References/Gizmo.html — change nothing here without changing the reference.

inline constexpr float kGizmoAxisReach       = 1.0f;            // L
inline constexpr float kGizmoTipReach        = 0.95f;           // TIP = L * 0.95
inline constexpr float kGizmoConeRadius      = 0.06f;           // ConeGeometry(0.06, 0.18, 24)
inline constexpr float kGizmoConeHeight      = 0.18f;
inline constexpr float kGizmoCylinderRadius  = 0.06f;           // CylinderGeometry(0.06, 0.06, 0.14, 24)
inline constexpr float kGizmoCylinderHeight  = 0.14f;
inline constexpr float kGizmoCylinderInset   = 0.28f;           // seated at dir * (TIP - 0.28)
inline constexpr float kGizmoQuadHalf        = 0.08f;           // corner quad half-width; corner at (u+v)*(TIP-half)
inline constexpr float kGizmoQuadOpacity     = 0.28f;           // fill opacity at rest / 0.55 hovered
inline constexpr float kGizmoQuadHoverOpacity= 0.55f;
inline constexpr float kGizmoArcRadius       = 0.95f * 0.62f;   // TIP * 0.62
inline constexpr float kGizmoArcBand         = 0.038f;          // half-width in the radial direction
inline constexpr float kGizmoArcSweep        = 0.5410521f;      // 31° in radians
inline constexpr uint32_t kGizmoArcSegments  = 24u;
inline constexpr uint32_t kGizmoRoundSegments= 24u;             // cones and cylinders
inline constexpr float kGizmoRingRadius      = 0.16f;           // TorusGeometry(0.16, 0.008, 12, 48)
inline constexpr float kGizmoRingTube        = 0.008f;
inline constexpr uint32_t kGizmoRingRadial   = 12u;
inline constexpr uint32_t kGizmoRingTubular  = 48u;

inline constexpr float kGizmoTintX[3]      = { 0.8784f, 0.0784f, 0.0784f };   // 0xe01414
inline constexpr float kGizmoTintY[3]      = { 0.0706f, 0.8314f, 0.0392f };   // 0x12d40a
inline constexpr float kGizmoTintZ[3]      = { 0.0824f, 0.3765f, 0.8784f };   // 0x1560e0
inline constexpr float kGizmoTintCyan[3]   = { 0.1216f, 0.7804f, 0.7804f };   // 0x1fc7c7 — the X (YZ) quad
inline constexpr float kGizmoTintMagenta[3]= { 0.7843f, 0.1176f, 0.7843f };   // 0xc81ec8 — the Y (XZ) quad
inline constexpr float kGizmoTintYellow[3] = { 0.8784f, 0.8039f, 0.0706f };   // 0xe0cd12 — the Z (XY) quad
inline constexpr float kGizmoTintRing[3]   = { 1.0f, 1.0f, 1.0f };

inline constexpr float kGizmoSnapMove  = 0.25f;         // world units, Ctrl held
inline constexpr float kGizmoSnapScale = 0.1f;          // scale factor step
inline constexpr float kGizmoSnapTurn  = 0.0872665f;    // 5° in radians

//------------------------------------------------------------------------------------------------------------------------
//                                                      GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

// Fills Triangles (a triangle list) and Strokes (a line list, the corner quads' two opaque edges) with the
//    mode's pieces, hover lift applied to Hot. CameraRight/CameraUp billboard the white ring toward the eye,
//    exactly as the reference re-poses its ring every drawn tick. Returns the triangle vertex count written;
//    StrokeCount receives the line vertex count. Capacities are in vertices.
uint32_t ComposeGizmoVertices(GizmoMode Mode, const GizmoPose& Pose, GizmoGrip Hot,
                              const float CameraRight[3], const float CameraUp[3],
                              GizmoVertex* Triangles, uint32_t TriangleCapacity,
                              GizmoVertex* Strokes, uint32_t StrokeCapacity, uint32_t* StrokeCount) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                     INTERACTION
//------------------------------------------------------------------------------------------------------------------------

// Which grip sits under the pointer: the mode's own triangles are ray-tested, nearest hit wins — the same
//    outcome as the reference's raycaster against its registered pieces, because these ARE those pieces.
[[nodiscard]] GizmoGrip ProbeGizmoGrip(GizmoMode Mode, const GizmoPose& Pose,
                                       const float RayOrigin[3], const float RayToward[3]) noexcept;

// A live drag: seeded at the press, advanced every pointer move. The start figures let every advance measure
//    from the press (Blender's rule: Ctrl snaps the TOTAL, not the increment).
struct GizmoDrag
{
    GizmoGrip Grip          = GizmoGrip::None;
    float     AxisToward[3] = { 0.0f, 0.0f, 0.0f };   // move/scale/turn axis, world
    float     PlaneU[3]     = { 0.0f, 0.0f, 0.0f };   // the corner quad's two axes, world
    float     PlaneV[3]     = { 0.0f, 0.0f, 0.0f };
    float     TurnZero[3]   = { 0.0f, 0.0f, 0.0f };   // the turn's reference direction, world
    float     StartAlong    = 0.0f;                   // axis parameter at the press
    float     StartAngle    = 0.0f;                   // angle at the press
    float     StartTouch[3] = { 0.0f, 0.0f, 0.0f };   // plane touch at the press
};

// What a drag currently demands of the seized object, measured from the press. Exactly one of the three arms
//    is live, named by Grip's family. Readout mirrors the reference's on-screen line ("X  move 0.250").
struct GizmoDemand
{
    GizmoGrip Grip        = GizmoGrip::None;
    float     Move[3]     = { 0.0f, 0.0f, 0.0f };   // translate: world offset from the press
    float     TurnAxis[3] = { 0.0f, 0.0f, 0.0f };   // rotate: world axis…
    float     TurnAngle   = 0.0f;                   //          …and radians swept from the press
    float     ScaleAxis   = 0u;                     // scale: 0/1/2 names the local axis…
    float     ScaleFactor = 1.0f;                   //         …multiplied by this (≥ 0.05)
    char      Readout[48] = {};
};

// Seeds the drag at the press. False when the ray cannot seed the grip (a plane press seen edge-on).
[[nodiscard]] bool BeginGizmoDrag(GizmoGrip Grip, const GizmoPose& Pose,
                                  const float RayOrigin[3], const float RayToward[3], GizmoDrag* Drag) noexcept;

// Advances the drag under the moved pointer. Snapping is the Ctrl figure. False when the pointer ray cannot
//    reach the drag's plane this tick (the demand then keeps its last figures).
[[nodiscard]] bool AdvanceGizmoDrag(const GizmoDrag& Drag, const GizmoPose& Pose,
                                    const float RayOrigin[3], const float RayToward[3],
                                    bool Snapping, GizmoDemand* Demand) noexcept;

} // namespace Frontier
