//============================================================================================================================================
// 📦 Projects/Project-Tractrix/Source/TractrixVehicleScene.h — Phase-3 drivable scene: chassis + soft tyres on the heightfield
//============================================================================================================================================
//
//    The engine-side assembly that turns the Phase 0–3 parts into a playable scene inside Project-Tractrix:
//
//        RigidBodySolver (Jolt)  ──creates──▶  TractrixProvingGround heightfield + a box chassis body
//              ▲                                     │
//              │ StepOnce()                          │ hooks (read pose / apply force at point / ground sample)
//        VehiclePhysicsThread (240 Hz)  ──drives──▶  VehicleSolver (chassis + 4 XPBD soft tyres)
//              ▲                                     │
//              │ DataChannel<DriverInput>            │ DataChannel<VehicleTelemetry>
//        game thread: AssignInput() / Telemetry()  ◀────┘
//
//    Threading: the rigid-body solver and vehicle solver live entirely on the physics thread (touched only inside Step()). The game
//    thread hands input in and reads a telemetry snapshot out through the Phase-0 lock-free DataChannels. Rendering reads
//    the chassis + wheel transforms straight out of the telemetry snapshot (no extra solver access needed).
//
//    This TU includes RigidBodySolver.h and therefore only compiles inside the Frontier tree (Jolt headers present). The
//    engine-agnostic VehicleSolver it drives is separately validated headless (VehicleSceneValidation.cpp).

#pragma once

#include "TractrixProvingGround.h"

#include "../../../Engine/PhysicalDynamics/RigidBodySolver.h"
#include "../../../Engine/PhysicalDynamics/VehiclePhysicsThread.h"
#include "../../../Engine/PhysicalDynamics/Vehicle/VehicleSolver.h"

namespace Frontier::Tractrix {

//------------------------------------------------------------------------------------------------------------------------
//                                        Vehicle::Vec3/Quat  ↔  Frontier::Vector3/Quaternion
//------------------------------------------------------------------------------------------------------------------------
[[nodiscard]] inline Frontier::Vehicle::Vec3 ToVehicle(const Vector3& v) noexcept { return {v.x, v.y, v.z}; }
[[nodiscard]] inline Frontier::Vehicle::Quat ToVehicle(const Quaternion& q) noexcept { return {q.x, q.y, q.z, q.w}; }
[[nodiscard]] inline Vector3    FromVehicle(const Frontier::Vehicle::Vec3& v) noexcept { return Vector3{v.x, v.y, v.z}; }

//------------------------------------------------------------------------------------------------------------------------
class TractrixVehicleScene
{
public:
    struct Settings
    {
        double   PhysicsHz          = 240.0;
        Vector3  ChassisHalfExtents { 2.0f, 0.85f, 0.35f };  // [m]
        float    ChassisMass        = 1200.0f;               // [kg]
        float    HubDrop            = 0.55f;                  // [m] hub below the CoM (box floats clear of the wheels)
        float    HalfTrack          = 0.78f;                  // [m] |y| wheel offset
        float    HalfWheelBase      = 1.30f;                  // [m] |x| wheel offset
    };

    [[nodiscard]] bool Bring(const Settings& settings = {}) noexcept
    {
        settings_ = settings;

        RigidBodyConfiguration rc;
        rc.Gravity          = Vector3{0.0f, 0.0f, -9.81f};
        rc.FixedStepSeconds = 1.0f / static_cast<float>(settings_.PhysicsHz);
        if (!solver_.Bring(rc)) return false;

        // Terrain first (static), then optimise the broad phase once.
        track_.Create(solver_);
        solver_.OptimizeBroadPhase();

        // Chassis box.
        const Vector3 spawn = track_.SpawnPosition(settings_.HubDrop + settings_.ChassisHalfExtents.z + 0.02f);
        RigidBodyDescription cd;
        cd.Name             = "Tractrix.Chassis";
        cd.Shape.Category   = CollisionShapeCategory::Box;
        cd.Shape.HalfExtents= settings_.ChassisHalfExtents;
        cd.Motion           = RigidBodyMotionCategory::Dynamic;
        cd.Position         = spawn;
        cd.MassKilograms    = settings_.ChassisMass;
        cd.LinearDamping    = 0.05f;
        cd.AngularDamping   = 0.10f;
        cd.AllowSleeping    = false;
        chassis_ = solver_.CreateBody(cd);
        if (chassis_ == InvalidRigidBody) { solver_.Retire(); return false; }

        // Solver configuration: 4 wheels, RWD, front-steer. Soft tyre uses the Phase-2 calibrated defaults.
        Frontier::Vehicle::VehicleSolverConfiguration vc;
        vc.ChassisMass = settings_.ChassisMass;
        const float z  = -settings_.HubDrop;
        const float hx = settings_.HalfWheelBase, hy = settings_.HalfTrack;
        vc.Wheels = {
            {{ hx,  hy, z}, /*steer*/true,  /*drive*/false, /*brake*/true},
            {{ hx, -hy, z}, true,  false, true},
            {{-hx,  hy, z}, false, true,  true},
            {{-hx, -hy, z}, false, true,  true},
        };

        Frontier::Vehicle::VehicleSolver::Hooks hooks;
        hooks.ReadChassis = [this]() -> Frontier::Vehicle::ChassisState
        {
            RigidBodyPose pose{};
            solver_.QueryPose(chassis_, pose);
            return { ToVehicle(pose.Position), ToVehicle(pose.Orientation),
                     ToVehicle(pose.LinearVelocity), ToVehicle(pose.AngularVelocity) };
        };
        hooks.ApplyForceAtPoint = [this](const Frontier::Vehicle::Vec3& f, const Frontier::Vehicle::Vec3& p)
        {
            solver_.ApplyForceAtPoint(chassis_, FromVehicle(f), FromVehicle(p));
        };
        hooks.ApplyTorque = [this](const Frontier::Vehicle::Vec3& t)
        {
            solver_.ApplyTorque(chassis_, FromVehicle(t));
        };
        hooks.Ground = [this](const Frontier::Vehicle::Vec3& p,
                              Frontier::Vehicle::Vec3& s, Frontier::Vehicle::Vec3& n)
        {
            // TrackSurface answers with a HEIGHT, so the surface point is the one directly below: the
            //    reduction a heightfield is, and valid here because this track has no vertical faces.
            float gz = 0.0f;
            if (!track_.Sample(p, gz, n)) return false;
            s = Frontier::Vehicle::Vec3{p.x, p.y, gz};
            return true;
        };

        Frontier::Vehicle::ChassisState initial{ ToVehicle(spawn), {0,0,0,1}, {0,0,0}, {0,0,0} };
        ActiveVehicleSolver.Build(vc, hooks, initial);
        if (!ActiveVehicleSolver.Constructed()) { solver_.Retire(); return false; }

        VehiclePhysicsThreadConfiguration tc;
        tc.StepHz = settings_.PhysicsHz;
        return thread_.Start(tc, [this](uint64_t i, float dt) { Step(i, dt); });
    }

    void Retire() noexcept { thread_.Stop(); solver_.Retire(); }

    // Game thread → physics thread.
    //   Feed a `DriverInput` here every game frame. To turn raw device state (WASD keys, a gamepad, or a racing wheel)
    //   into that `DriverInput`, use the engine-agnostic `Frontier::Vehicle::DriverInputIntegrator`
    //   (Engine/PhysicalDynamics/Vehicle/DriverInputIntegrator.h): push button/axis state into it, call Advance(Δτ),
    //   and forward the resulting `DriverCommand::Drive` to AssignInput(). Its ShiftUp/ShiftDown pulses are reserved for
    //   the manual-gearbox phase (the solver currently auto-shifts). Example:
    //       input.ForwardThrottleKey(wDown); input.ForwardSteerLeftKey(aDown); ...
    //       scene.AssignInput(input.Advance(Δτ).Drive);
    void AssignInput(const Frontier::Vehicle::DriverInput& input) noexcept { inputChannel_.Write(input); }

    // Physics thread → game/render thread (latest snapshot; false before the first step completes).
    [[nodiscard]] bool Telemetry(Frontier::Vehicle::VehicleTelemetry& out) const noexcept { return telemetryChannel_.Peek(out); }

    [[nodiscard]] const RigidBodySolver&     Solver() const noexcept { return solver_; }
    [[nodiscard]] RigidBodyIdentity          Chassis() const noexcept { return chassis_; }
    [[nodiscard]] const TractrixProvingGround&   Track() const noexcept { return track_; }
    [[nodiscard]] VehiclePhysicsThreadMetrics ThreadMetrics() const noexcept { return thread_.QueryMetrics(); }

private:
    // Physics-thread step: pull the latest input, advance the solver (steps the soft tyres + queues wheel forces),
    //    integrate the chassis once, then publish a telemetry snapshot.
    void Step(uint64_t /*index*/, float dt) noexcept
    {
        Frontier::Vehicle::DriverInput in;
        if (inputChannel_.Peek(in)) ActiveVehicleSolver.AssignInput(in);
        ActiveVehicleSolver.Step(dt);
        solver_.StepOnce();
        telemetryChannel_.Write(ActiveVehicleSolver.Telemetry());
    }

    Settings                                                     settings_{};
    RigidBodySolver                                              solver_;
    VehiclePhysicsThread                                         thread_;
    Frontier::Vehicle::VehicleSolver                         ActiveVehicleSolver;
    TractrixProvingGround                                            track_;
    RigidBodyIdentity                                            chassis_ = InvalidRigidBody;
    DataChannel<Frontier::Vehicle::DriverInput>                  inputChannel_;
    DataChannel<Frontier::Vehicle::VehicleTelemetry>            telemetryChannel_;
};

} // namespace Frontier::Tractrix
