//============================================================================================================================================
//                                                   VEHICLEINSTANCESEQUENCE.H
//============================================================================================================================================
// 📦 Project-Drive's engine ⇄ project seam — the bridge from the live VehicleSolver to renderer instance
//    transforms, exactly the role PhysicsInstanceSequence plays for the drop scene in Project-Zero.
//
//    RigidBodySolver / VisibilityExchange know nothing about vehicles; VehicleSolver knows nothing about
//    InstanceRecord rows. This class is the one place they meet, and it lives in the PROJECT because deciding that
//    "instance 0 is the car body and instances 1..4 are its wheels" is game semantics.
//
//    Body collision is DROPPED by design (only the wheels contact the ground), so the chassis is integrated by a
//    self-contained rigid integrator owned here — the same one the headless DriveTelemetry reference and the
//    FieldDemo use — rather than a Jolt body with a collider. The tyres reach the ground through the DriveCourse
//    heightfield query, never a raycast. This keeps the vehicle physics fully project-owned and identical between
//    the Vulkan app and the headless reference.
//
//    Per frame the host calls AdvanceVehicle(): it steps the solver at a FIXED sub-step (240 Hz) accumulating
//    the frame's wall time, then writes World rows —
//        instance BodyInstance      = T(chassis) · R(chassis)                       (mesh baked object-local)
//        instance WheelInstance[i]  = T(hub_i) · R(chassis) · R(steer_i,Z) · R(spin_i,Y)
//    PreviousWorld is rolled first so motion vectors and ReSTIR reprojection stay valid, exactly as
//    InstanceMotionSequence / PhysicsInstanceSequence do.

#pragma once

#include "../../../Engine/GeometricRaster/SceneStructure.h"   // InstanceRecord, TriangleIndex
#include "../../../Engine/PhysicalDynamics/Vehicle/VehicleSolver.h"
#include "../../../Engine/PhysicalDynamics/Vehicle/VehicleGeometry.h"

#include <cstdint>
#include <vector>

namespace Frontier {
namespace Drive {

struct VehicleInstanceConfiguration
{
    uint32_t BodyInstance   = 0u;          // [idx] renderer instance ordinal of the car body
    uint32_t FirstWheel     = 1u;          // [idx] instance ordinal of wheel 0 (wheels are contiguous)
    uint32_t WheelCount     = 4u;          // [cnt]
    float    SubStepSeconds = 1.0f/240.0f; // [s]   fixed physics step; the frame's dt is consumed in whole sub-steps
    float    SpawnHeight     = 0.42f;      // [m]   CoM z so the wheels rest on the pad (hub 0.0914 below + r 0.34)
    Frontier::Vehicle::Vec3 SpawnLocation{};      // [m] - requested XY; SpawnHeight supplies initial Z
    Frontier::Vehicle::Quat SpawnRotation{};      // [-] - heading retained when fitting the support plane
    bool FitTerrain = true;                       // [-] - equilibrium placement before physics begins
    float    MaxFrameSeconds = 0.10f;      // [s]   clamp a long frame so a hitch never explodes the integrator
};

// The self-contained rigid chassis (no collider — body collision is dropped). Identical integrator to the
//    headless DriveTelemetry reference: semi-implicit Euler with gravity, world-frame inertia, quaternion spin.
struct VehicleChassisBody
{
    Frontier::Vehicle::Vec3  Position{};
    Frontier::Vehicle::Quat  Orientation{0,0,0,1};
    Frontier::Vehicle::Vec3  LinearVelocity{}, AngularVelocity{};
    Frontier::Vehicle::Vec3  ForceAccum{}, TorqueAccum{};
    Frontier::Vehicle::Vec3  InvInertiaDiag{};
    Frontier::Vehicle::Vec3  Gravity{0.0f, 0.0f, -9.81f};
    float Mass = 1300.0f, LinearDamping = 0.0f, AngularDamping = 0.05f;

    [[nodiscard]] Frontier::Vehicle::Quat Conjugate() const noexcept;
    [[nodiscard]] Frontier::Vehicle::Vec3 WorldAngularAccel(const Frontier::Vehicle::Vec3& torqueWorld) const noexcept;
    void ApplyForceAtPoint(const Frontier::Vehicle::Vec3& f, const Frontier::Vehicle::Vec3& p) noexcept;
    void ApplyTorque(const Frontier::Vehicle::Vec3& t) noexcept;
    void Integrate(float dt) noexcept;
    [[nodiscard]] Frontier::Vehicle::ChassisState State() const noexcept;
};

class VehicleInstanceSequence
{
public:
    // Brings the solver up over the DriveCourse ground and seeds the chassis at the spawn pose. `geometry`
    //    supplies the real ControlVehicle socket layout; `config` is filled from it via ApplyGeometry.
    bool Construct(const Frontier::Vehicle::VehicleGeometry& Geometry,
                   const VehicleInstanceConfiguration& Configuration,
                   Frontier::Vehicle::XPBDSoftTyre::GroundQuery Ground = {}) noexcept;

    // The one call the frame loop makes. Consumes `input` (already mapped from the device by DriverInputExchange),
    //    advances the physics by whole sub-steps covering DeltaSeconds, and writes the body + wheel World rows into
    //    Rows (which must be the full instance list). Rolls PreviousWorld first.
    void AdvanceVehicle(const Frontier::Vehicle::DriverInput& Input,
                        std::vector<InstanceRecord>& Rows, float DeltaSeconds) noexcept;

    // Rewrite the vehicle's entries in the FLAT triangle list from the poses of the last AdvanceVehicle, so the
    //    acceleration structure (shadows, reflections, GI) follows the car once refitted. Rest is captured once.
    void RefreshBodyFacets(std::vector<TriangleIndex>& Facets, const std::vector<InstanceRecord>& Instances) noexcept;

    // R — teleport back to the spawn pose with zero velocity (DriverInputExchange maps the reset key here).
    void ResetToSpawn() noexcept;

    //-- Chase/player camera + inspector read the live state through these ---------------------------------------
    [[nodiscard]] const Frontier::Vehicle::ChassisState&   Chassis()   const noexcept { return ChassisState_; }
    [[nodiscard]] const Frontier::Vehicle::VehicleTelemetry& Telemetry() const noexcept { return ActiveVehicleSolver.Telemetry(); }
    [[nodiscard]] Frontier::Vehicle::VehicleSolverConfiguration& Configuration() noexcept { return ActiveConfiguration; }
    [[nodiscard]] const Frontier::Vehicle::VehicleSolverConfiguration& Configuration() const noexcept { return ActiveConfiguration; }

    [[nodiscard]] const std::vector<Frontier::Vehicle::XPBDSoftTyre>& Tyres() const noexcept
    { return ActiveVehicleSolver.Tyres(); }

    // Apply an inspector edit to the live config (rebuilds the tyres so curve/geometry edits take effect).
    void Reconfigure(const Frontier::Vehicle::VehicleSolverConfiguration& Edited) noexcept;

private:
    void WriteBodyRow(std::vector<InstanceRecord>& Rows) noexcept;
    void WriteWheelRows(std::vector<InstanceRecord>& Rows) noexcept;
    static void ComposeTRS(const Frontier::Vehicle::Vec3& Translation, const Frontier::Vehicle::Quat& Rotation,
                           float Out[16]) noexcept;

    VehicleInstanceConfiguration            Instancing;
    Frontier::Vehicle::VehicleSolverConfiguration ActiveConfiguration;
    Frontier::Vehicle::VehicleSolver    ActiveVehicleSolver;
    VehicleChassisBody                      Body;
    Frontier::Vehicle::ChassisState         ChassisState_{};
    Frontier::Vehicle::Vec3                 SpawnPosition{0.0f,0.0f,0.42f};

    Frontier::Vehicle::Quat SpawnOrientation{};
    Frontier::Vehicle::XPBDSoftTyre::GroundQuery GroundSurface;
    std::vector<float>                      WheelSpin;      // [rad] integrated spin angle per wheel (visual)
    float                                   Accumulator = 0.0f;

    // Flat-triangle refit bookkeeping, mirroring PhysicsInstanceSequence.
    std::vector<TriangleIndex>              RestFacets;     // vehicle geometry captured object-local, once
    std::vector<uint32_t>                   FacetFirst;     // [idx] first flat triangle of body + each wheel
    std::vector<uint32_t>                   FacetCount;     // [cnt]
    bool                                    RestCaptured = false;
    bool                                    Built        = false;
};

} // namespace Drive
} // namespace Frontier
