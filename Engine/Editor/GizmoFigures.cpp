//============================================================================================================================================
//                                                      GIZMOFIGURES.CPP
//============================================================================================================================================
// 🧩 The transform gizmo's vertices and pointer arithmetic — References/Gizmo.html reproduced figure for figure.
//    See GizmoFigures.h for the contract; every constant here is the reference's own, and the interaction
//    functions are its axisParamUnderPointer / planePointUnderPointer / angleUnderPointer ported line for line.

#include "GizmoFigures.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier {

namespace {

constexpr float kTau = 6.28318530717958647692f;
constexpr float kQuarterTurn = 0.78539816339744830962f;   // the arc sits centred on the u→v bisector, 45°

//------------------------------------------------------------------------------------------------------------------------
//                                                  SMALL VECTOR IDIOM
//------------------------------------------------------------------------------------------------------------------------

struct V3 { float X, Y, Z; };

inline V3   Seat(const float A[3])          { return { A[0], A[1], A[2] }; }
inline V3   Add(V3 A, V3 B)                 { return { A.X + B.X, A.Y + B.Y, A.Z + B.Z }; }
inline V3   Sub(V3 A, V3 B)                 { return { A.X - B.X, A.Y - B.Y, A.Z - B.Z }; }
inline V3   Mul(V3 A, float S)              { return { A.X * S, A.Y * S, A.Z * S }; }
inline float Dot(V3 A, V3 B)                { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
inline V3   Cross(V3 A, V3 B)               { return { A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X }; }
inline float Length(V3 A)                   { return std::sqrt(Dot(A, A)); }
inline V3   Normalise(V3 A)                 { const float L = Length(A); return L > 1e-12f ? Mul(A, 1.0f / L) : V3{ 0.0f, 0.0f, 0.0f }; }

//------------------------------------------------------------------------------------------------------------------------
//                                                     EMISSION
//------------------------------------------------------------------------------------------------------------------------

struct Sink
{
    GizmoVertex* Vertices  = nullptr;
    uint32_t     Count     = 0u;
    uint32_t     Capacity  = 0u;
    GizmoGrip*   Grips     = nullptr;   // one per TRIANGLE, for the pointer probe; null when only drawing
    uint32_t     GripCount = 0u;
};

void PushVertex(Sink& Out, V3 Location, V3 Normal, const float Tint[3], float Opacity, float Emissive, float Unlit)
{
    if (Out.Count >= Out.Capacity) return;
    GizmoVertex& V = Out.Vertices[Out.Count++];
    V.Location[0] = Location.X; V.Location[1] = Location.Y; V.Location[2] = Location.Z;
    V.Normal[0] = Normal.X; V.Normal[1] = Normal.Y; V.Normal[2] = Normal.Z;
    V.Tint[0] = Tint[0]; V.Tint[1] = Tint[1]; V.Tint[2] = Tint[2]; V.Tint[3] = Opacity;
    V.Emissive = Emissive; V.Unlit = Unlit;
    V.Spare[0] = 0.0f; V.Spare[1] = 0.0f; V.Spare[2] = 0.0f; V.Spare[3] = 0.0f;
}

void PushTriangle(Sink& Out, GizmoGrip Grip, V3 A, V3 B, V3 C, V3 Na, V3 Nb, V3 Nc,
                  const float Tint[3], float Opacity, float Emissive, float Unlit)
{
    if (Out.Count + 3u > Out.Capacity) return;
    if (Out.Grips != nullptr) Out.Grips[Out.GripCount] = Grip;
    ++Out.GripCount;
    PushVertex(Out, A, Na, Tint, Opacity, Emissive, Unlit);
    PushVertex(Out, B, Nb, Tint, Opacity, Emissive, Unlit);
    PushVertex(Out, C, Nc, Tint, Opacity, Emissive, Unlit);
}

// ── the translate cone: ConeGeometry(0.06, 0.18, 24) seated at dir·TIP, +Y oriented along dir ─────────────────────────
void EmitCone(Sink& Out, GizmoGrip Grip, V3 Origin, V3 Toward, V3 U, V3 V, float Reach,
              const float Tint[3], float Emissive)
{
    const float Radius = kGizmoConeRadius * Reach;
    const float Half   = kGizmoConeHeight * 0.5f * Reach;
    const V3 Apex   = Add(Origin, Mul(Toward, kGizmoTipReach * Reach + Half));
    const V3 Centre = Add(Origin, Mul(Toward, kGizmoTipReach * Reach - Half));
    const float Slant = std::sqrt(kGizmoConeRadius * kGizmoConeRadius + kGizmoConeHeight * kGizmoConeHeight);
    for (uint32_t I = 0u; I < kGizmoRoundSegments; ++I)
    {
        const float A0 = kTau * static_cast<float>(I) / static_cast<float>(kGizmoRoundSegments);
        const float A1 = kTau * static_cast<float>(I + 1u) / static_cast<float>(kGizmoRoundSegments);
        const float Am = 0.5f * (A0 + A1);
        const V3 R0 = Add(Mul(U, std::cos(A0)), Mul(V, std::sin(A0)));
        const V3 R1 = Add(Mul(U, std::cos(A1)), Mul(V, std::sin(A1)));
        const V3 Rm = Add(Mul(U, std::cos(Am)), Mul(V, std::sin(Am)));
        const V3 B0 = Add(Centre, Mul(R0, Radius));
        const V3 B1 = Add(Centre, Mul(R1, Radius));
        // Smooth lateral normals: radial · height + axial · radius over the slant, the classic cone normal.
        const V3 N0 = Normalise(Add(Mul(R0, kGizmoConeHeight / Slant), Mul(Toward, kGizmoConeRadius / Slant)));
        const V3 N1 = Normalise(Add(Mul(R1, kGizmoConeHeight / Slant), Mul(Toward, kGizmoConeRadius / Slant)));
        const V3 Nm = Normalise(Add(Mul(Rm, kGizmoConeHeight / Slant), Mul(Toward, kGizmoConeRadius / Slant)));
        PushTriangle(Out, Grip, Apex, B0, B1, Nm, N0, N1, Tint, 1.0f, Emissive, 0.0f);
        PushTriangle(Out, Grip, Centre, B1, B0, Mul(Toward, -1.0f), Mul(Toward, -1.0f), Mul(Toward, -1.0f),
                     Tint, 1.0f, Emissive, 0.0f);
    }
}

// ── the scale grip: CylinderGeometry(0.06, 0.06, 0.14, 24) seated at dir·(TIP − 0.28) ─────────────────────────────────
void EmitCylinder(Sink& Out, GizmoGrip Grip, V3 Origin, V3 Toward, V3 U, V3 V, float Reach,
                  const float Tint[3], float Emissive)
{
    const float Radius = kGizmoCylinderRadius * Reach;
    const float Half   = kGizmoCylinderHeight * 0.5f * Reach;
    const V3 Centre = Add(Origin, Mul(Toward, (kGizmoTipReach - kGizmoCylinderInset) * Reach));
    const V3 Top    = Add(Centre, Mul(Toward, Half));
    const V3 Foot   = Sub(Centre, Mul(Toward, Half));
    for (uint32_t I = 0u; I < kGizmoRoundSegments; ++I)
    {
        const float A0 = kTau * static_cast<float>(I) / static_cast<float>(kGizmoRoundSegments);
        const float A1 = kTau * static_cast<float>(I + 1u) / static_cast<float>(kGizmoRoundSegments);
        const V3 R0 = Add(Mul(U, std::cos(A0)), Mul(V, std::sin(A0)));
        const V3 R1 = Add(Mul(U, std::cos(A1)), Mul(V, std::sin(A1)));
        const V3 T0 = Add(Top,  Mul(R0, Radius)), T1 = Add(Top,  Mul(R1, Radius));
        const V3 F0 = Add(Foot, Mul(R0, Radius)), F1 = Add(Foot, Mul(R1, Radius));
        PushTriangle(Out, Grip, F0, F1, T1, R0, R1, R1, Tint, 1.0f, Emissive, 0.0f);
        PushTriangle(Out, Grip, F0, T1, T0, R0, R1, R0, Tint, 1.0f, Emissive, 0.0f);
        PushTriangle(Out, Grip, Top, T0, T1, Toward, Toward, Toward, Tint, 1.0f, Emissive, 0.0f);
        PushTriangle(Out, Grip, Foot, F1, F0, Mul(Toward, -1.0f), Mul(Toward, -1.0f), Mul(Toward, -1.0f),
                     Tint, 1.0f, Emissive, 0.0f);
    }
}

// ── the plane grip's translucent fill: PlaneGeometry(0.16, 0.16) at (u+v)·(TIP − half) ────────────────────────────────
void EmitQuad(Sink& Out, GizmoGrip Grip, V3 Origin, V3 U, V3 V, float Reach,
              const float Tint[3], float Opacity)
{
    const float Half = kGizmoQuadHalf * Reach;
    const V3 Corner = Add(Origin, Mul(Add(U, V), (kGizmoTipReach - kGizmoQuadHalf) * Reach));
    const V3 A = Add(Add(Corner, Mul(U, -Half)), Mul(V, -Half));
    const V3 B = Add(Add(Corner, Mul(U,  Half)), Mul(V, -Half));
    const V3 C = Add(Add(Corner, Mul(U,  Half)), Mul(V,  Half));
    const V3 D = Add(Add(Corner, Mul(U, -Half)), Mul(V,  Half));
    const V3 N = Normalise(Cross(U, V));
    PushTriangle(Out, Grip, A, B, C, N, N, N, Tint, Opacity, 0.0f, 1.0f);
    PushTriangle(Out, Grip, A, C, D, N, N, N, Tint, Opacity, 0.0f, 1.0f);
}

// ── the plane grip's two opaque edges: backU → outer → backV, drawn as a line list ────────────────────────────────────
void EmitQuadEdges(Sink& Out, V3 Origin, V3 U, V3 V, float Reach, const float Tint[3])
{
    const float Half = kGizmoQuadHalf * Reach;
    const V3 Corner = Add(Origin, Mul(Add(U, V), (kGizmoTipReach - kGizmoQuadHalf) * Reach));
    const V3 Outer  = Add(Add(Corner, Mul(U, Half)), Mul(V, Half));
    const V3 BackU  = Add(Outer, Mul(U, -2.0f * Half));
    const V3 BackV  = Add(Outer, Mul(V, -2.0f * Half));
    const V3 N = Normalise(Cross(U, V));
    PushVertex(Out, BackU, N, Tint, 1.0f, 0.0f, 1.0f);
    PushVertex(Out, Outer, N, Tint, 1.0f, 0.0f, 1.0f);
    PushVertex(Out, Outer, N, Tint, 1.0f, 0.0f, 1.0f);
    PushVertex(Out, BackV, N, Tint, 1.0f, 0.0f, 1.0f);
}

// ── the rotation grip: a flat annular sector in the (u, v) plane, sweep 31° centred on the 45° bisector ───────────────
void EmitArc(Sink& Out, GizmoGrip Grip, V3 Origin, V3 U, V3 V, float Reach,
             const float Tint[3], float Emissive)
{
    const float Inner = (kGizmoArcRadius - kGizmoArcBand) * Reach;
    const float Outer = (kGizmoArcRadius + kGizmoArcBand) * Reach;
    const float Start = kQuarterTurn - kGizmoArcSweep * 0.5f;
    const V3 N = Normalise(Cross(U, V));
    for (uint32_t I = 0u; I < kGizmoArcSegments; ++I)
    {
        const float A0 = Start + kGizmoArcSweep * static_cast<float>(I) / static_cast<float>(kGizmoArcSegments);
        const float A1 = Start + kGizmoArcSweep * static_cast<float>(I + 1u) / static_cast<float>(kGizmoArcSegments);
        const V3 D0 = Add(Mul(U, std::cos(A0)), Mul(V, std::sin(A0)));
        const V3 D1 = Add(Mul(U, std::cos(A1)), Mul(V, std::sin(A1)));
        const V3 I0 = Add(Origin, Mul(D0, Inner)), I1 = Add(Origin, Mul(D1, Inner));
        const V3 O0 = Add(Origin, Mul(D0, Outer)), O1 = Add(Origin, Mul(D1, Outer));
        PushTriangle(Out, Grip, I0, O0, O1, N, N, N, Tint, 1.0f, Emissive, 0.0f);
        PushTriangle(Out, Grip, I0, O1, I1, N, N, N, Tint, 1.0f, Emissive, 0.0f);
    }
}

// ── the central white ring: TorusGeometry(0.16, 0.008, 12, 48), billboarded toward the eye ────────────────────────────
void EmitRing(Sink& Out, V3 Origin, V3 Right, V3 Up, float Reach, const float Tint[3])
{
    const V3 N = Normalise(Cross(Right, Up));
    const float Ring = kGizmoRingRadius * Reach;
    const float Tube = kGizmoRingTube * Reach;
    for (uint32_t T = 0u; T < kGizmoRingTubular; ++T)
    {
        const float P0 = kTau * static_cast<float>(T) / static_cast<float>(kGizmoRingTubular);
        const float P1 = kTau * static_cast<float>(T + 1u) / static_cast<float>(kGizmoRingTubular);
        const V3 D0 = Add(Mul(Right, std::cos(P0)), Mul(Up, std::sin(P0)));
        const V3 D1 = Add(Mul(Right, std::cos(P1)), Mul(Up, std::sin(P1)));
        for (uint32_t S = 0u; S < kGizmoRingRadial; ++S)
        {
            const float Q0 = kTau * static_cast<float>(S) / static_cast<float>(kGizmoRingRadial);
            const float Q1 = kTau * static_cast<float>(S + 1u) / static_cast<float>(kGizmoRingRadial);
            const V3 N00 = Add(Mul(D0, std::cos(Q0)), Mul(N, std::sin(Q0)));
            const V3 N01 = Add(Mul(D0, std::cos(Q1)), Mul(N, std::sin(Q1)));
            const V3 N10 = Add(Mul(D1, std::cos(Q0)), Mul(N, std::sin(Q0)));
            const V3 N11 = Add(Mul(D1, std::cos(Q1)), Mul(N, std::sin(Q1)));
            const V3 P00 = Add(Add(Origin, Mul(D0, Ring)), Mul(N00, Tube));
            const V3 P01 = Add(Add(Origin, Mul(D0, Ring)), Mul(N01, Tube));
            const V3 P10 = Add(Add(Origin, Mul(D1, Ring)), Mul(N10, Tube));
            const V3 P11 = Add(Add(Origin, Mul(D1, Ring)), Mul(N11, Tube));
            PushTriangle(Out, GizmoGrip::None, P00, P10, P11, N00, N10, N11, Tint, 1.0f, 0.0f, 1.0f);
            PushTriangle(Out, GizmoGrip::None, P00, P11, P01, N00, N11, N01, Tint, 1.0f, 0.0f, 1.0f);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   SHARED LAYOUT
//------------------------------------------------------------------------------------------------------------------------
// The reference's per-axis vocabulary: direction, tint, the two OTHER axes (u, v) and the plane accent.

struct AxisSeat
{
    uint32_t     Axis;         // 0 X, 1 Y, 2 Z
    const float* Tint;
    const float* PlaneTint;
};

constexpr AxisSeat kAxisSeats[3] = {
    { 0u, kGizmoTintX, kGizmoTintCyan },
    { 1u, kGizmoTintY, kGizmoTintMagenta },
    { 2u, kGizmoTintZ, kGizmoTintYellow },
};

void SeatBasis(const GizmoPose& Pose, uint32_t Axis, V3* Toward, V3* U, V3* V)
{
    const V3 X = Normalise(Seat(Pose.AxisX));
    const V3 Y = Normalise(Seat(Pose.AxisY));
    const V3 Z = Normalise(Seat(Pose.AxisZ));
    if (Axis == 0u)      { *Toward = X; *U = Y; *V = Z; }   // OTHERS.x = (Y, Z)
    else if (Axis == 1u) { *Toward = Y; *U = X; *V = Z; }   // OTHERS.y = (X, Z)
    else                 { *Toward = Z; *U = X; *V = Y; }   // OTHERS.z = (X, Y)
}

constexpr GizmoGrip kMoveGrips[3]  = { GizmoGrip::MoveX,  GizmoGrip::MoveY,  GizmoGrip::MoveZ  };
constexpr GizmoGrip kPlaneGrips[3] = { GizmoGrip::PlaneX, GizmoGrip::PlaneY, GizmoGrip::PlaneZ };
constexpr GizmoGrip kScaleGrips[3] = { GizmoGrip::ScaleX, GizmoGrip::ScaleY, GizmoGrip::ScaleZ };
constexpr GizmoGrip kTurnGrips[3]  = { GizmoGrip::TurnX,  GizmoGrip::TurnY,  GizmoGrip::TurnZ  };

void EmitMode(Sink& Triangles, Sink* Strokes, GizmoMode Mode, const GizmoPose& Pose, GizmoGrip Hot,
              const float CameraRight[3], const float CameraUp[3])
{
    const V3 Origin = Seat(Pose.Origin);
    const float Reach = Pose.Reach > 1e-6f ? Pose.Reach : 1.0f;

    for (const AxisSeat& Seated : kAxisSeats)
    {
        V3 Toward, U, V;
        SeatBasis(Pose, Seated.Axis, &Toward, &U, &V);

        if (Mode == GizmoMode::Translate)
        {
            const float ConeLift = Hot == kMoveGrips[Seated.Axis] ? 0.3333f : 0.0f;
            EmitCone(Triangles, kMoveGrips[Seated.Axis], Origin, Toward, U, V, Reach, Seated.Tint, ConeLift);
            const float Opacity = Hot == kPlaneGrips[Seated.Axis] ? kGizmoQuadHoverOpacity : kGizmoQuadOpacity;
            EmitQuad(Triangles, kPlaneGrips[Seated.Axis], Origin, U, V, Reach, Seated.PlaneTint, Opacity);
            if (Strokes != nullptr)
                EmitQuadEdges(*Strokes, Origin, U, V, Reach, Seated.PlaneTint);
        }
        else if (Mode == GizmoMode::Rotate)
        {
            const float ArcLift = Hot == kTurnGrips[Seated.Axis] ? 0.3333f : 0.0f;
            EmitArc(Triangles, kTurnGrips[Seated.Axis], Origin, U, V, Reach, Seated.Tint, ArcLift);
        }
        else
        {
            const float GripLift = Hot == kScaleGrips[Seated.Axis] ? 0.3333f : 0.0f;
            EmitCylinder(Triangles, kScaleGrips[Seated.Axis], Origin, Toward, U, V, Reach, Seated.Tint, GripLift);
        }
    }

    EmitRing(Triangles, Origin, Normalise(Seat(CameraRight)), Normalise(Seat(CameraUp)), Reach, kGizmoTintRing);
}

//------------------------------------------------------------------------------------------------------------------------
//                                             THE REFERENCE'S POINTER ARITHMETIC
//------------------------------------------------------------------------------------------------------------------------

// axisParamUnderPointer: the signed distance along the axis of the point on the axis line nearest the ray.
float AlongAxisUnderRay(V3 AxisToward, V3 Origin, V3 RayOrigin, V3 RayToward)
{
    const V3 W0 = Sub(Origin, RayOrigin);
    const float A = Dot(AxisToward, AxisToward), B = Dot(AxisToward, RayToward), C = Dot(RayToward, RayToward);
    const float D = Dot(AxisToward, W0), E = Dot(RayToward, W0);
    const float Denominator = A * C - B * B;
    if (std::fabs(Denominator) < 1e-6f) return 0.0f;
    return (B * E - C * D) / Denominator;
}

// planePointUnderPointer: the ray against the plane through the origin with the given normal.
bool TouchPlaneUnderRay(V3 Normal, V3 Origin, V3 RayOrigin, V3 RayToward, V3* Touch)
{
    const float Facing = Dot(RayToward, Normal);
    if (std::fabs(Facing) < 1e-8f) return false;
    const float Along = Dot(Sub(Origin, RayOrigin), Normal) / Facing;
    if (Along < 0.0f) return false;
    *Touch = Add(RayOrigin, Mul(RayToward, Along));
    return true;
}

// angleUnderPointer: the pointer's angle around the axis, measured against a fixed reference direction.
bool AngleUnderRay(V3 AxisToward, V3 Zero, V3 Origin, V3 RayOrigin, V3 RayToward, float* Angle)
{
    V3 Touch;
    if (!TouchPlaneUnderRay(AxisToward, Origin, RayOrigin, RayToward, &Touch)) return false;
    const V3 Swing = Sub(Touch, Origin);
    const V3 Reference = Normalise(Sub(Zero, Mul(AxisToward, Dot(Zero, AxisToward))));
    const V3 Perpendicular = Normalise(Cross(AxisToward, Reference));
    *Angle = std::atan2(Dot(Swing, Perpendicular), Dot(Swing, Reference));
    return true;
}

float SnapTo(float Figure, float Step) { return std::round(Figure / Step) * Step; }

// Which family a grip belongs to, and which axis it rides.
uint32_t AxisOfGrip(GizmoGrip Grip)
{
    switch (Grip)
    {
        case GizmoGrip::MoveX: case GizmoGrip::PlaneX: case GizmoGrip::ScaleX: case GizmoGrip::TurnX: return 0u;
        case GizmoGrip::MoveY: case GizmoGrip::PlaneY: case GizmoGrip::ScaleY: case GizmoGrip::TurnY: return 1u;
        default: return 2u;
    }
}

char LetterOfGrip(GizmoGrip Grip)
{
    const uint32_t Axis = AxisOfGrip(Grip);
    return Axis == 0u ? 'X' : Axis == 1u ? 'Y' : 'Z';
}

bool IsMoveGrip(GizmoGrip G)  { return G == GizmoGrip::MoveX  || G == GizmoGrip::MoveY  || G == GizmoGrip::MoveZ;  }
bool IsPlaneGrip(GizmoGrip G) { return G == GizmoGrip::PlaneX || G == GizmoGrip::PlaneY || G == GizmoGrip::PlaneZ; }
bool IsScaleGrip(GizmoGrip G) { return G == GizmoGrip::ScaleX || G == GizmoGrip::ScaleY || G == GizmoGrip::ScaleZ; }
bool IsTurnGrip(GizmoGrip G)  { return G == GizmoGrip::TurnX  || G == GizmoGrip::TurnY  || G == GizmoGrip::TurnZ;  }

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     COMPOSITION
//------------------------------------------------------------------------------------------------------------------------

uint32_t ComposeGizmoVertices(GizmoMode Mode, const GizmoPose& Pose, GizmoGrip Hot,
                              const float CameraRight[3], const float CameraUp[3],
                              GizmoVertex* Triangles, uint32_t TriangleCapacity,
                              GizmoVertex* Strokes, uint32_t StrokeCapacity, uint32_t* StrokeCount) noexcept
{
    Sink TriangleSink{ Triangles, 0u, TriangleCapacity, nullptr, 0u };
    Sink StrokeSink{ Strokes, 0u, StrokeCapacity, nullptr, 0u };
    EmitMode(TriangleSink, Strokes != nullptr ? &StrokeSink : nullptr, Mode, Pose, Hot, CameraRight, CameraUp);
    if (StrokeCount != nullptr) *StrokeCount = StrokeSink.Count;
    return TriangleSink.Count;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE PROBE
//------------------------------------------------------------------------------------------------------------------------
// The reference raycasts its registered grips; these triangles ARE those grips, so a double-sided
//    Möller–Trumbore over them lands on the same answer. The ring is decoration and never probed —
//    the mode's grips are composed WITHOUT it by walking the axis emitters directly.

GizmoGrip ProbeGizmoGrip(GizmoMode Mode, const GizmoPose& Pose,
                         const float RayOrigin[3], const float RayToward[3]) noexcept
{
    // Grips only: 3 cones (48 tri) + 3 quads (2 tri) for translate, 3 arcs (48) or 3 cylinders (96) otherwise.
    constexpr uint32_t kProbeCapacity = 3u * 96u * 3u;
    GizmoVertex Vertices[kProbeCapacity];
    GizmoGrip   Grips[kProbeCapacity / 3u];
    Sink Feeler{ Vertices, 0u, kProbeCapacity, Grips, 0u };

    const V3 Origin = Seat(Pose.Origin);
    const float Reach = Pose.Reach > 1e-6f ? Pose.Reach : 1.0f;
    for (const AxisSeat& Seated : kAxisSeats)
    {
        V3 Toward, U, V;
        SeatBasis(Pose, Seated.Axis, &Toward, &U, &V);
        if (Mode == GizmoMode::Translate)
        {
            EmitCone(Feeler, kMoveGrips[Seated.Axis], Origin, Toward, U, V, Reach, Seated.Tint, 0.0f);
            EmitQuad(Feeler, kPlaneGrips[Seated.Axis], Origin, U, V, Reach, Seated.PlaneTint, 1.0f);
        }
        else if (Mode == GizmoMode::Rotate)
        {
            EmitArc(Feeler, kTurnGrips[Seated.Axis], Origin, U, V, Reach, Seated.Tint, 0.0f);
        }
        else
        {
            EmitCylinder(Feeler, kScaleGrips[Seated.Axis], Origin, Toward, U, V, Reach, Seated.Tint, 0.0f);
        }
    }

    const V3 O = Seat(RayOrigin);
    const V3 T = Normalise(Seat(RayToward));
    GizmoGrip Nearest = GizmoGrip::None;
    float NearestAlong = 3.4e38f;
    for (uint32_t Triangle = 0u; Triangle * 3u + 2u < Feeler.Count; ++Triangle)
    {
        const V3 A = Seat(Vertices[Triangle * 3u + 0u].Location);
        const V3 B = Seat(Vertices[Triangle * 3u + 1u].Location);
        const V3 C = Seat(Vertices[Triangle * 3u + 2u].Location);
        const V3 EdgeAb = Sub(B, A), EdgeAc = Sub(C, A);
        const V3 Across = Cross(T, EdgeAc);
        const float Steepness = Dot(EdgeAb, Across);
        if (std::fabs(Steepness) < 1e-9f) continue;   // double sided: only true edge-on parallels are skipped
        const float Inverse = 1.0f / Steepness;
        const V3 Toward = Sub(O, A);
        const float BaryU = Dot(Toward, Across) * Inverse;
        if (BaryU < 0.0f || BaryU > 1.0f) continue;
        const V3 Swing = Cross(Toward, EdgeAb);
        const float BaryV = Dot(T, Swing) * Inverse;
        if (BaryV < 0.0f || BaryU + BaryV > 1.0f) continue;
        const float Along = Dot(EdgeAc, Swing) * Inverse;
        if (Along <= 1e-6f || Along >= NearestAlong) continue;
        NearestAlong = Along;
        Nearest = Grips[Triangle];
    }
    return Nearest;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE DRAG
//------------------------------------------------------------------------------------------------------------------------

bool BeginGizmoDrag(GizmoGrip Grip, const GizmoPose& Pose,
                    const float RayOrigin[3], const float RayToward[3], GizmoDrag* Drag) noexcept
{
    if (Grip == GizmoGrip::None || Drag == nullptr) return false;
    *Drag = GizmoDrag{};
    Drag->Grip = Grip;

    const V3 Origin = Seat(Pose.Origin);
    const V3 O = Seat(RayOrigin);
    const V3 T = Normalise(Seat(RayToward));
    V3 Toward, U, V;
    SeatBasis(Pose, AxisOfGrip(Grip), &Toward, &U, &V);

    if (IsMoveGrip(Grip) || IsScaleGrip(Grip))
    {
        Drag->AxisToward[0] = Toward.X; Drag->AxisToward[1] = Toward.Y; Drag->AxisToward[2] = Toward.Z;
        Drag->StartAlong = AlongAxisUnderRay(Toward, Origin, O, T);
        return true;
    }
    if (IsPlaneGrip(Grip))
    {
        V3 Touch;
        if (!TouchPlaneUnderRay(Toward, Origin, O, T, &Touch)) return false;
        Drag->AxisToward[0] = Toward.X; Drag->AxisToward[1] = Toward.Y; Drag->AxisToward[2] = Toward.Z;
        Drag->PlaneU[0] = U.X; Drag->PlaneU[1] = U.Y; Drag->PlaneU[2] = U.Z;
        Drag->PlaneV[0] = V.X; Drag->PlaneV[1] = V.Y; Drag->PlaneV[2] = V.Z;
        Drag->StartTouch[0] = Touch.X; Drag->StartTouch[1] = Touch.Y; Drag->StartTouch[2] = Touch.Z;
        return true;
    }
    if (IsTurnGrip(Grip))
    {
        // The reference's turn zero: worldDir(OTHERS[axis].u) — the first of the two other axes.
        float Angle;
        if (!AngleUnderRay(Toward, U, Origin, O, T, &Angle)) return false;
        Drag->AxisToward[0] = Toward.X; Drag->AxisToward[1] = Toward.Y; Drag->AxisToward[2] = Toward.Z;
        Drag->TurnZero[0] = U.X; Drag->TurnZero[1] = U.Y; Drag->TurnZero[2] = U.Z;
        Drag->StartAngle = Angle;
        return true;
    }
    return false;
}

bool AdvanceGizmoDrag(const GizmoDrag& Drag, const GizmoPose& Pose,
                      const float RayOrigin[3], const float RayToward[3],
                      bool Snapping, GizmoDemand* Demand) noexcept
{
    if (Drag.Grip == GizmoGrip::None || Demand == nullptr) return false;
    *Demand = GizmoDemand{};
    Demand->Grip = Drag.Grip;

    const V3 Origin = Seat(Pose.Origin);
    const V3 O = Seat(RayOrigin);
    const V3 T = Normalise(Seat(RayToward));
    const V3 Axis = Seat(Drag.AxisToward);
    const char Letter = LetterOfGrip(Drag.Grip);

    if (IsMoveGrip(Drag.Grip))
    {
        float Delta = AlongAxisUnderRay(Axis, Origin, O, T) - Drag.StartAlong;
        if (Snapping) Delta = SnapTo(Delta, kGizmoSnapMove);
        Demand->Move[0] = Axis.X * Delta; Demand->Move[1] = Axis.Y * Delta; Demand->Move[2] = Axis.Z * Delta;
        std::snprintf(Demand->Readout, sizeof(Demand->Readout), "%c  move %.3f", Letter, static_cast<double>(Delta));
        return true;
    }
    if (IsScaleGrip(Drag.Grip))
    {
        const float Delta = AlongAxisUnderRay(Axis, Origin, O, T) - Drag.StartAlong;
        float Factor = 1.0f + Delta;                       // the reference: 1 unit of drag = +100%
        if (Snapping) Factor = std::fmax(kGizmoSnapScale, SnapTo(Factor, kGizmoSnapScale));
        Factor = std::fmax(0.05f, Factor);
        Demand->ScaleAxis   = static_cast<float>(AxisOfGrip(Drag.Grip));
        Demand->ScaleFactor = Factor;
        std::snprintf(Demand->Readout, sizeof(Demand->Readout), "%c  scale %.3fx", Letter, static_cast<double>(Factor));
        return true;
    }
    if (IsPlaneGrip(Drag.Grip))
    {
        V3 Touch;
        if (!TouchPlaneUnderRay(Axis, Origin, O, T, &Touch)) return false;
        V3 Move = Sub(Touch, Seat(Drag.StartTouch));
        if (Snapping)
        {
            const V3 U = Seat(Drag.PlaneU), V = Seat(Drag.PlaneV);
            const float AlongU = SnapTo(Dot(Move, U), kGizmoSnapMove);
            const float AlongV = SnapTo(Dot(Move, V), kGizmoSnapMove);
            Move = Add(Mul(U, AlongU), Mul(V, AlongV));
        }
        Demand->Move[0] = Move.X; Demand->Move[1] = Move.Y; Demand->Move[2] = Move.Z;
        std::snprintf(Demand->Readout, sizeof(Demand->Readout), "%c-plane  move %.3f",
                      Letter, static_cast<double>(Length(Move)));
        return true;
    }
    if (IsTurnGrip(Drag.Grip))
    {
        float Angle;
        if (!AngleUnderRay(Axis, Seat(Drag.TurnZero), Origin, O, T, &Angle)) return false;
        float Swept = Angle - Drag.StartAngle;
        if (Snapping) Swept = SnapTo(Swept, kGizmoSnapTurn);
        Demand->TurnAxis[0] = Axis.X; Demand->TurnAxis[1] = Axis.Y; Demand->TurnAxis[2] = Axis.Z;
        Demand->TurnAngle   = Swept;
        std::snprintf(Demand->Readout, sizeof(Demand->Readout), "%c  rotate %.1f\xC2\xB0",
                      Letter, static_cast<double>(Swept * 57.29577951308232f));
        return true;
    }
    return false;
}

} // namespace Frontier
