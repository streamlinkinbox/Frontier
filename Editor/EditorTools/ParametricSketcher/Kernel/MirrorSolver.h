//--------------------------------------------------------------------------------------------------------------------------------------//
// 📦 Editor/EditorTools/ParametricSketcher/Kernel/MirrorSolver.h — Phase 19: pure 3D reflection + radial rotation math                                       //
//--------------------------------------------------------------------------------------------------------------------------------------//
// All functions are pure: take Vec3 / direction / angle, return Vec3. No host, no scene, no Blueprint. The host's Blueprint-reflection
//    helpers wrap these: read every position-bearing Blueprint cell, call Reflect / Rotate, write back, then rebuild the figure from the
//    reflected source via the same per-Form builder that ApplyLiveEdit uses.
//
// The three primitive operations are:
//    ReflectAcrossPlane(P, Plane)    — P' = P - 2 * n * dot(P - O, n)  where O is plane origin, n is unit normal
//    ReflectAcrossAxis(P, Axis)      — P' = P - 2 * perpendicular(P - O)  where O is axis origin, d is unit direction
//    RotateAroundAxis(P, Axis, θ)   — Rodrigues: P' = O + parallel + cos(θ)·perp + sin(θ)·(d̂ × parallel)
//
// Multi-axis composition (mirror across N planes/axes) is a sequence of Reflect calls. For 2 perpendicular planes the composition is a
//    180° rotation around the intersection line, but we don't pre-compose — we just call Reflect twice. The verification confirms the
//    composition is geometrically correct.
//
// Axis is encoded as a 3D line: origin (Vec3) + unit direction (Vec3). The constructor normalises the direction, so callers can pass a
//    non-unit vector and we won't complain. A zero-length direction is a refusal case; MirrorSolver doesn't refuse — it returns P unchanged
//    (the safe fallback).
#pragma once

#include "VectorSpecification.h"

namespace Frontier
{

struct MirrorAxis
{
    Vec3 Origin     = Vec3{};                                                  // [m] a point on the line
    Vec3 Direction  = Vec3::UnitZ();                                           // [-] unit (normalised on construction)

    [[nodiscard]] static MirrorAxis FromPoints(Vec3 A, Vec3 B) noexcept
    {
        MirrorAxis R; R.Origin = A; R.Direction = (B - A).Normalised(); return R;
    }
};

// Reflect a point across a plane (defined by a normal — the plane passes through the origin). For the world XY/XZ/YZ planes the origin
//    is (0,0,0) and the normal is the world axis. For a custom workplane the origin is the plane's origin and the normal is the plane's
//    `Normal()`. Direction is normalised internally.
[[nodiscard]] Vec3 ReflectAcrossPlane(Vec3 P, Vec3 PlaneOrigin, Vec3 PlaneNormal) noexcept;

// Reflect a point across an axis (a 3D line through Axis.Origin in Axis.Direction). Decompose (P - Origin) into parallel + perpendicular
//    to the direction; flip the perpendicular. If the direction is zero, return P (safe fallback).
[[nodiscard]] Vec3 ReflectAcrossAxis(Vec3 P, MirrorAxis Axis) noexcept;

// Rotate a point around an axis by θ radians. Rodrigues' formula. θ = 0 returns P (no-op); θ = 2π returns P (full revolution).
[[nodiscard]] Vec3 RotateAroundAxis(Vec3 P, MirrorAxis Axis, double ThetaRadians) noexcept;

// Multi-axis composition: apply Reflects in order. The result is the geometric composition. For 2 perpendicular planes this is a 180°
//    rotation around the line of intersection; for 2 parallel planes it is a translation. We do not pre-detect these cases — the caller
//    can wrap the calls in their own optimiser if they want.
struct MirrorOp
{
    enum class Kind : uint8_t { Plane, Axis };
    Kind  Kind         = Kind::Plane;
    Vec3  Origin       = Vec3{};
    Vec3  NormalOrDir  = Vec3::UnitZ();
};
[[nodiscard]] Vec3 ComposeMirrors(Vec3 P, const MirrorOp* Ops, size_t Count) noexcept;

// Convenience helpers for the workplane-encoded mirrors used by the `mirror` verb.
inline Vec3 ReflectAcrossXY(Vec3 P) noexcept { return ReflectAcrossPlane(P, Vec3{}, Vec3::UnitZ()); }
inline Vec3 ReflectAcrossXZ(Vec3 P) noexcept { return ReflectAcrossPlane(P, Vec3{}, Vec3::UnitY()); }
inline Vec3 ReflectAcrossYZ(Vec3 P) noexcept { return ReflectAcrossPlane(P, Vec3{}, Vec3::UnitX()); }

} // namespace Frontier
