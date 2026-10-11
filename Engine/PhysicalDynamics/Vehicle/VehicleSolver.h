//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/Vehicle/VehicleSolver.h — drivable vehicle: rigid chassis + four XPBD soft tyres (Phase 3)
//============================================================================================================================================
//
//    Phase 3 assembles the pieces built in Phases 0–2 into one drivable vehicle:
//
//        • the chassis is a single rigid box in Jolt (RigidBodySolver);
//        • each corner carries an `XPBDSoftTyre` (Phase 2) whose nodes contact the Jolt heightfield DIRECTLY through the
//          same GroundQuery the rest of the sim uses — no raycast, exactly the user's Phase-3 requirement;
//        • the soft tyre carries the vertical wheel load (it IS the suspension spring — the one deformable element), and
//          its friction coefficient sets the available grip;
//        • a thin driving layer turns throttle / brake / steer into in-plane tractive & cornering forces, bounded by the
//          per-wheel friction circle (μ · Fz from the tyre), applied at the contact patch.
//
//    Like the rest of the vehicle layer this class is ENGINE-AGNOSTIC and header-only-math (it reuses the self-contained
//    Vec3/Quat from XPBDSoftTyre.h). It never mentions Jolt: it reaches the rigid chassis through a small set of `Hooks`
//    (read chassis pose, apply force at a world point, apply torque) and reaches the ground through the tyre's GroundQuery.
//    In the engine `TractrixVehicleScene` binds those hooks to `RigidBodySolver`; the headless `VehicleSceneValidation`
//    binds them to a mock box integrator, so the whole solver compiles and is validated in the sandbox without Jolt.
//
//    Call order per fixed physics step (on the Phase-0 physics thread):
//        solver.AssignInput(input);
//        solver.Step(Δτ);          // reads chassis, steps tyres, applies wheel forces to the chassis
//        solver.StepOnce();            // integrates the chassis with those forces
//
//    Frame conventions (match RigidBodySolver + XPBDSoftTyre): right-handed, +Z up, chassis-local +X forward, +Y left,
//    tyre spin axis +Y.

#pragma once

#include "SuspensionSpecification.h"
#include "XPBDSoftTyre.h"
#include "PacejkaMagicFormula.h"
#include "TyreSlipDynamics.h"
#include "Drivetrain.h"
#include "Aerodynamics.h"
#include "BrakingSystem.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace Frontier::Vehicle {

// Which in-plane tyre-force model the solver runs.
enum class DrivingScheme
{
    SimpleFrictionCircle,   // Phase-3 arcade layer: throttle/brake/steer → forces clamped to μ·Fz (no wheel spin)
    PacejkaDrivetrain,      // production: engine→clutch→gearbox→diff→wheel-spin→Pacejka slip forces (Phases 1+2 combined)
};

//------------------------------------------------------------------------------------------------------------------------
//                                          quaternion helpers (compose / normalise)
//------------------------------------------------------------------------------------------------------------------------
// XPBDSoftTyre.h's Quat only ships AxisAngle + Rotate; the solver also needs Hamilton product and renormalisation to
//    build a steered hub rotation (yaw ∘ chassis) and to keep the mock chassis quaternion unit.
[[nodiscard]] inline Quat QuatMul(const Quat& a, const Quat& b) noexcept
{
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    };
}
[[nodiscard]] inline Quat QuatNormalize(const Quat& q) noexcept
{
    const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return (l > 1e-12f) ? Quat{q.x / l, q.y / l, q.z / l, q.w / l} : Quat{0, 0, 0, 1};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   STATE / INPUT
//------------------------------------------------------------------------------------------------------------------------
struct ChassisState
{
    Vec3 Position;          // [m]     world-space centre of mass
    Quat Orientation;       // [-]     world-space rotation
    Vec3 LinearVelocity;    // [m/s]   world
    Vec3 AngularVelocity;   // [rad/s] world
};

struct DriverInput
{
    float Throttle = 0.0f;  // [0..1]
    float Brake    = 0.0f;  // [0..1]
    float Steer    = 0.0f;  // [-1..1]  +1 = steer left (+Y)
    bool  Handbrake = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHEEL LAYOUT
//------------------------------------------------------------------------------------------------------------------------
struct WheelMount
{
    Vec3 LocalOffset;           // [m]  hub REST position in chassis-local frame, relative to the CoM
    Vec3 SuspensionMount;       // [m]  strut top in the same frame (Socket_SuspensionMount_*); hub hangs below it
    bool Steered = false;       //      front wheels steer
    bool Driven  = false;       //      powered wheels get engine force
    bool Braked  = true;        //      wheels the brake acts on
};

struct VehicleSolverConfiguration
{
    float ChassisMass = 1200.0f;                 // [kg] informational; the solver owns the real mass
    SoftTyreParameters Tyre;                     // shared soft-tyre parameters (Phase-2 calibration by default)
    std::vector<WheelMount> Wheels;              // typically 4

    float    MaxSteerAngleRad   = 0.52f;         // [rad] ~30° lock
    float    SteerRatePerSecond = 6.0f;          // [1/s] first-order steer smoothing
    float    DriveForcePerWheel = 5000.0f;       // [N]   tractive demand at full throttle (before the grip clamp)
    float    BrakeForcePerWheel = 9000.0f;       // [N]   braking demand at full brake
    float    HandbrakeForce     = 12000.0f;      // [N]   rear-axle lock demand
    float    RollingResistance  = 0.015f;        // [-]   fraction of Fz opposing motion
    // 🔴 Superseded by the real struts below. It was never a shock absorber: it subtracted a velocity term from the
    //    tyre's vertical force to stop the carcass ringing. Retained only for the SimpleFrictionCircle fallback.
    float    SuspensionDamping  = 6000.0f;        // [N·s/m] legacy bounce damping (SimpleFrictionCircle only)

    // ---- Suspension: one strut per corner, plus a bar per axle (see SuspensionSpecification.h) ----
    SuspensionSpecification FrontStrut{};         // [-]   front pair, calibrated by ResolveSuspension
    SuspensionSpecification RearStrut{};          // [-]   rear pair
    AntiRollBar             FrontBar{};           // [-]   front anti-roll bar
    AntiRollBar             RearBar{ true, 80000.0f, 1500.0f, 2u, 3u };
    float    UnsprungMass       = 20.0f;          // [kg]  per corner; the mass the strut accelerates
    bool     SuspensionEnabled  = true;           // [-]   false welds the hubs to the chassis (the old behaviour)
    float    CorneringStiffness = 30000.0f;      // [N per (m/s) of lateral slip, scaled by load fraction]
    float    GripCoefficient    = 1.15f;         // [-]   friction-circle μ for the driving layer (grip = μ·Fz)
    uint32_t TyreSubsteps       = 8u;            // [-]   XPBD substeps per fixed step
    Vec3     Gravity            = {0.0f, 0.0f, -9.81f};

    //-- Production driving layer (DrivingScheme::PacejkaDrivetrain) --------------------------------------------------------
    //   These are ignored by the SimpleFrictionCircle path. They wire the Phase-1 slip/drivetrain models onto the same
    //   Fz-from-soft-tyre / direct-heightfield-contact path used above.
    DrivingScheme           ActiveScheme   = DrivingScheme::PacejkaDrivetrain;
    PacejkaParameters      TyrePacejka;                                     // MF6.1 slip-force spec (defaults = Phase-1)
    SlipSolver             SlipSolverSelection = SlipSolver::RelaxationLength;   // zero-speed-stable transient tyre integrator
    float    WheelInertia           = 1.2f;      // [kg·m²] rotational inertia of one wheel+tyre about its spin axis
    float    EffectiveRadius        = 0.0f;      // [m]  rolling radius; <= 0 ⇒ fall back to Tyre.Radius (0.34 m)
    float    MaxBrakeTorquePerWheel = 2600.0f;   // [N·m] foot-brake torque at full pedal, per braked wheel
    float    HandbrakeTorque        = 4200.0f;   // [N·m] extra torque on the rear (non-steered) wheels when latched
    float    UpshiftRPM             = 6800.0f;   // [rpm] AMT auto-upshift threshold
    float    DownshiftRPM           = 2600.0f;   // [rpm] AMT auto-downshift threshold
    float    ShiftCooldownSeconds   = 0.35f;     // [s]   minimum dwell between gear changes
    // Contact/tread damping. At low speed the Magic-Formula deflection spring + wheel inertia form an UNDAMPED oscillator
    //    (the relaxation σ-decay term → 0 as Vx → 0), which explicit integration amplifies. These viscous terms model the
    //    carcass/tread damping that suppresses it. They vanish at the steady-state operating point (slip speed → 0), so
    //    they do not alter the physical tyre forces — they only keep the transient stable.
    float    SpinDampingCoefficient    = 1400.0f;  // [N per m/s] longitudinal slip-speed damping on wheel spin
    float    LateralDampingCoefficient = 1600.0f;  // [N per m/s] lateral slip-speed damping on the contact patch
    float    ContactDampingFadeSpeed   = 6.0f;     // [m/s] damping fades out above this hub speed (pure Pacejka at speed)
    EngineParameters       Engine        = EngineParameters::DefaultGTR();
    TurbochargerParameters Turbo         = TurbochargerParameters::DefaultGTR();
    TransmissionParameters Transmission;                                   // GT-R 6-speed defaults
    ClutchParameters       Clutch;
    DifferentialParameters Differential;

    //-- Forced induction (Fuel deferred; ABS + Supercharger ported from GRIT `source-only`) -------------------------------
    //   Induction selects turbo (default), a belt-driven supercharger, or NA. RaceTune raises the boost ceiling on
    //   race fuel. See Drivetrain.h / SuperchargerParameters::DefaultTwinScrew().
    InductionType          Induction     = InductionType::Turbocharged;
    SuperchargerParameters Supercharger  = SuperchargerParameters::DefaultTwinScrew();
    bool                   RaceTune      = false;

    //-- Brakes + ABS (disk thermal model, ported from GRIT BrakingSpecifications; ABS is a standard slip modulator) -------
    //   When BrakeThermalEnabled is true the foot brake torque comes from the hydraulic/thermal disk model (with fade)
    //   and, if Abs.Enabled, is modulated to keep each wheel near its peak-grip slip. When false, the legacy constant
    //   MaxBrakeTorquePerWheel path is used (keeps existing scenes/checks unchanged). The handbrake always uses the
    //   constant HandbrakeTorque path (mechanical, no ABS).
    bool               BrakeThermalEnabled = false;
    BrakingParameters  Brakes = BrakingParameters::DefaultGT3();
    AbsParameters      Abs;

    //-- Aerodynamics (first subsystem re-ported from GRIT `source-only`; see Aerodynamics.h) -----------------------------
    //   Drag + side-force + aero moments are applied to the chassis; the front/rear downforce is added to the per-axle
    //   tyre vertical load Fz that feeds the Pacejka slip model (GRIT's "downforce → wheel loads → grip" rule). Set
    //   `Aero.Enabled = false` (or use AerodynamicPackage::DefaultBody()) to disable the winged devices. Only the
    //   PacejkaDrivetrain driving layer consumes aero; SimpleFrictionCircle ignores it.
    AerodynamicPackage     Aero = AerodynamicPackage::DefaultGT3();
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     TELEMETRY
//------------------------------------------------------------------------------------------------------------------------
struct WheelTelemetry
{
    Vec3     HubPosition;            // [m] world (rest hub, follows chassis)
    Quat     HubRotation;           // [-] world (includes steer yaw)
    Vec3     ContactPoint;          // [m] world contact-patch centre
    float    VerticalLoad = 0.0f;   // [N] Fz from the soft tyre
    float    LongitudinalForce = 0.0f; // [N] applied traction/brake (post-clamp)
    float    LateralForce = 0.0f;   // [N] applied cornering (post-clamp)
    float    SteerAngleRad = 0.0f;  // [rad]
    uint32_t ContactCount = 0u;     // [-] tyre nodes touching ground
    bool     InContact = false;
    // Production layer (PacejkaDrivetrain) — per-wheel spin & slip state:
    float    WheelOmega   = 0.0f;   // [rad/s] wheel spin (+ = rolling forward)
    float    SlipRatio    = 0.0f;   // [-]  longitudinal slip κ
    float    SlipAngleRad = 0.0f;   // [rad] lateral slip angle α
    float    AeroDownforce = 0.0f;  // [N] aero downforce added to this wheel's grip load this step
    // Suspension (PacejkaDrivetrain): the strut state this corner settled at.
    float    StrutCompression = 0.0f;  // [m]   compression from free length (+ = compressed)
    float    StrutRate        = 0.0f;  // [m/s] compression rate (+ = compressing)
    float    StrutForce       = 0.0f;  // [N]   total force the strut pushes the chassis up with
    float    AntiRollForce    = 0.0f;  // [N]   this corner's share of its axle bar
    // Brakes (only populated when BrakeThermalEnabled is on):
    float    BrakeTorque_Nm  = 0.0f;   // [N·m] foot-brake torque this wheel this step
    float    BrakePressure_Pa= 0.0f;   // [Pa]  hydraulic line pressure
    float    BrakeTemp_K     = 293.15f;// [K]   disk temperature
    bool     AbsActive       = false;  // [-]   ABS dumping pressure on this wheel this step
};

struct VehicleTelemetry
{
    ChassisState Chassis;
    float SpeedMetresPerSecond = 0.0f;
    float ForwardSpeed         = 0.0f;   // [m/s] signed, along chassis +X
    float TotalVerticalLoad    = 0.0f;   // [N] Σ Fz
    uint32_t WheelCount        = 0u;
    uint32_t WheelsInContact   = 0u;
    std::array<WheelTelemetry, 8> Wheels{};
    // Production layer (PacejkaDrivetrain) — powertrain state:
    float    EngineRPM         = 0.0f;
    float    TurboRPM          = 0.0f;   // turbo shaft rpm, or supercharger charger rpm when blown
    float    BoostBar          = 0.0f;
    float    ParasiticDrag_Nm  = 0.0f;   // supercharger crank load (0 for turbo/NA)
    int      GearIndex         = 0;      // index into Transmission.GearRatios (3 = 1st)
    bool     PacejkaActive     = false;  // true when running DrivingScheme::PacejkaDrivetrain
    AeroForces Aero{};                   // aerodynamics computed this step (drag/downforce/side/moments breakdown)
};

//------------------------------------------------------------------------------------------------------------------------
// VEHICLE SOLVER
//------------------------------------------------------------------------------------------------------------------------
class VehicleSolver
{
public:
    struct Hooks
    {
        std::function<ChassisState()>                                 ReadChassis;        // world chassis pose + velocities
        std::function<void(const Vec3& forceN, const Vec3& worldPt)>  ApplyForceAtPoint;  // accumulate for next step
        std::function<void(const Vec3& torqueNm)>                     ApplyTorque;        // optional (may be null)
        XPBDSoftTyre::GroundQuery                                     Ground;             // heightfield sample (no raycast)
    };

    // Builds one soft tyre per wheel mount at its current world hub. `initial` seeds the hub placement.
    void Build(const VehicleSolverConfiguration& config, const Hooks& hooks, const ChassisState& initial) noexcept;

    void AssignInput(const DriverInput& input) noexcept { DriverCommand = input; }

    // One fixed physics step: read chassis, step every tyre (nodes vs heightfield), apply wheel forces at the patches.
    void Step(float Δτ) noexcept;

    [[nodiscard]] const VehicleTelemetry& Telemetry() const noexcept { return CurrentTelemetry; }
    [[nodiscard]] const std::vector<XPBDSoftTyre>& Tyres() const noexcept { return SoftTyres; }
    [[nodiscard]] bool Constructed() const noexcept { return ConstructionComplete; }

private:
    [[nodiscard]] static float Clamp(float Amount, float Lower, float Upper) noexcept { return Amount < Lower ? Lower : (Amount > Upper ? Upper : Amount); }

    // The two driving layers Step() dispatches to (selected by ActiveConfiguration.ActiveScheme).
    void StepSimple(float Δτ) noexcept;   // Phase-3 friction-circle layer (validated fallback)
    void StepPacejka(float Δτ) noexcept;  // production drivetrain + Pacejka slip layer

    VehicleSolverConfiguration    ActiveConfiguration;
    Hooks                      ChassisHooks;
    std::vector<XPBDSoftTyre>  SoftTyres;
    DriverInput                DriverCommand;
    VehicleTelemetry           CurrentTelemetry;
    float                      SteerAngle = 0.0f;  // filtered steer (rad)
    bool                       ConstructionComplete = false;

    // Production driving-layer state (PacejkaDrivetrain):
    PacejkaMagicFormula                  PacejkaTyre;      // Magic-Formula tyre (shared by all wheels)
    std::unique_ptr<TyreSlipDynamics> SlipDynamics;         // transient slip integrator (holds a const ref to PacejkaTyre)
    Drivetrain                        Powertrain;    // engine → clutch → gearbox → differential
    BrakingSystem                     BrakingHydraulics;       // per-wheel disk-brake hydraulics + thermal + ABS
    // Suspension ---------------------------------------------------------------------------------------------
    [[nodiscard]] const SuspensionSpecification& StrutFor(size_t Wheel) const noexcept;
    [[nodiscard]] Vec3  ResolveHub(size_t Wheel, const ChassisState& cs, Vec3& MountWorld, Vec3& AxisUp) const noexcept;
    [[nodiscard]] float AntiRollForce(size_t Wheel) const noexcept;
    float IntegrateStrut(size_t Wheel, float Δτ, float TyreLoad, const Vec3& AxisUp, float BarForce) noexcept;

    std::vector<float>                WheelSpin;    // [rad/s] per-wheel spin
    std::vector<float>                StrutTravel;  // [m]     per-corner strut compression from free length
    std::vector<float>                StrutRate;    // [m/s]   per-corner compression rate
    std::vector<SlipState>            SlipDeflections;     // per-wheel relaxation-length deflection state
    int                               GearIndex = 3; // current gear (3 = 1st)
    float                             ShiftTimer = 0.0f;
};

} // namespace Frontier::Vehicle
