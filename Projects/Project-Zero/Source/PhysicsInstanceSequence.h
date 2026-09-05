//============================================================================================================================================
//                                                   PHYSICSINSTANCESEQUENCE.H
//============================================================================================================================================
// 🧩 D4 — the bridge from rigid-body poses to renderer instance transforms. Owns the drop scene inside the
//    showroom: creates the static colliders, creates one dynamic body per drop instance, and each frame turns
//    Jolt's poses into InstanceRecord::World rows.
//
//    Engine ⇄ project seam: RigidBodySolver knows nothing about instances, and VisibilityExchange knows nothing
//    about physics. This class is the only place the two meet, and it lives in the PROJECT because deciding that
//    "instance 11 is a falling ball" is game semantics.
//
//    The rest-pose subtraction is the part worth reading twice. Showroom geometry is baked in WORLD space at the
//    body's rest position, but the renderer multiplies that geometry by World. Writing Jolt's absolute pose into
//    World would therefore apply the position twice. So the matrix written is
//        World = Translate(pose.Position) · Rotate(pose.Orientation) · Translate(−restOrigin)
//    which moves the baked geometry to the origin, rotates it, then places it where physics says. At the rest
//    pose with identity orientation this collapses to the identity matrix, so frame zero renders exactly as the
//    static showroom does — which is the D4 regression gate.

#pragma once

#include "../../../Engine/GeometricRaster/SceneStructure.h"
#include "../../../Engine/PhysicalDynamics/RigidBodySolver.h"

#include <cstdint>
#include <vector>

namespace Frontier {
namespace ProjectZero {

struct PhysicsInstanceConfiguration
{
    uint32_t FirstDropInstance = 0u;      // [idx] instance ordinal of drop body 0
    uint32_t DropCount         = 0u;      // [cnt]
    float    BodyRadius        = 0.16f;   // [m]   must match ShowroomStructure::QueryDropRadius()
    float    FloorHeight       = 0.0f;    // [m]   showroom floor plane, z = 0
    float    Restitution       = 0.35f;   // [-]   a little bounce so the drop reads as physical
    float    Friction          = 0.55f;   // [-]
};

class PhysicsInstanceSequence
{
public:
    // Brings the solver up, creates the showroom's static colliders and one sphere per drop instance. Returns
    //    false if the solver refuses; the caller then renders the scene statically rather than failing.
    [[nodiscard]] bool Construct(RigidBodySolver& Solver, const PhysicsInstanceConfiguration& Configuration) noexcept;

    // Steps the solver and writes the resulting transforms into Rows. PreviousWorld is rolled first so motion
    //    vectors and ReSTIR reprojection stay valid, exactly as InstanceMotionSequence does.
    void AdvancePhysics(RigidBodySolver& Solver, std::vector<InstanceRecord>& Rows, float DeltaSeconds) noexcept;

    // Diagnostics: how many bodies are still awake, and the lowest body centre — the settling proof reads these.
    [[nodiscard]] uint32_t QueryActiveCount() const noexcept { return ActiveCount; }
    [[nodiscard]] float    QueryLowestHeight() const noexcept { return LowestHeight; }
    [[nodiscard]] uint32_t QueryBodyCount()   const noexcept { return static_cast<uint32_t>(Bodies.size()); }

private:
    PhysicsInstanceConfiguration    Config;
    std::vector<RigidBodyIdentity>  Bodies;        // [-] one per drop instance, creation order
    std::vector<Vector3>            RestOrigins;   // [m] where the geometry was baked, subtracted out each frame
    std::vector<RigidBodyPose>      Poses;         // reused each frame; no per-frame allocation in the loop
    uint32_t                        ActiveCount  = 0u;
    float                           LowestHeight = 0.0f;   // [m]
};

} // namespace ProjectZero
} // namespace Frontier
