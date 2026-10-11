//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/Vehicle/VehicleSolver.cpp — Phase 3 drivable vehicle implementation
//============================================================================================================================================

#include "VehicleSolver.h"

#include <algorithm>
#include <cmath>

namespace Frontier::Vehicle {

namespace {
[[nodiscard]] inline float Sign(float Amount) noexcept { return Amount > 0.0f ? 1.0f : (Amount < 0.0f ? -1.0f : 0.0f); }
[[nodiscard]] inline Vec3  PlanarNormalized(const Vec3& Vector) noexcept
{
    Vec3 Planar{Vector.x, Vector.y, 0.0f};
    const float Magnitude = Planar.Length();
    return (Magnitude > 1e-6f) ? Vec3{Planar.x / Magnitude, Planar.y / Magnitude, 0.0f} : Vec3{0, 0, 0};
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
void VehicleSolver::Build(const VehicleSolverConfiguration& config, const Hooks& hooks, const ChassisState& initial) noexcept
{
    ActiveConfiguration = config;
    ChassisHooks  = hooks;
    DriverCommand  = {};
    SteerAngle = 0.0f;
    CurrentTelemetry = {};


    // ── Production driving layer (PacejkaDrivetrain) setup ───────────────────────────────────────────────────────────
    //   Wire the Phase-1 slip + drivetrain models. The slip integrator holds a const reference to PacejkaTyre, so PacejkaTyre
    //   must be configured (and must outlive SlipDynamics) before the first Step. All per-wheel spin/deflection state is zeroed.
    PacejkaTyre.AssignParameters(ActiveConfiguration.TyrePacejka);
    SlipDynamics = std::make_unique<TyreSlipDynamics>(PacejkaTyre);
    SlipDynamics->AssignSolver(ActiveConfiguration.SlipSolverSelection);

    Powertrain.AssignEngine(ActiveConfiguration.Engine);
    Powertrain.AssignTurbo(ActiveConfiguration.Turbo);
    Powertrain.AssignTransmission(ActiveConfiguration.Transmission);
    Powertrain.AssignClutch(ActiveConfiguration.Clutch);
    Powertrain.AssignDifferential(ActiveConfiguration.Differential);
    Powertrain.AssignInduction(ActiveConfiguration.Induction);
    Powertrain.AssignSupercharger(ActiveConfiguration.Supercharger);
    Powertrain.AssignRaceTune(ActiveConfiguration.RaceTune);
    Powertrain.Reset(ActiveConfiguration.Engine.IdleRPM);
    BrakingHydraulics.Configure(ActiveConfiguration.Wheels.size(), ActiveConfiguration.Brakes, ActiveConfiguration.Abs);

    GearIndex  = ActiveConfiguration.Transmission.NeutralIndex;       // start in neutral (auto-clutch engages 1st on throttle)
    ShiftTimer = ActiveConfiguration.ShiftCooldownSeconds;
    WheelSpin.assign(ActiveConfiguration.Wheels.size(), 0.0f);
    // Start every strut at the sag its own spring settles to, so the car stands at its authored ride height on step
    //    zero instead of dropping into it and ringing for the first second of every run.
    StrutTravel.resize(ActiveConfiguration.Wheels.size());
    StrutRate.assign(ActiveConfiguration.Wheels.size(), 0.0f);
    for (size_t i = 0; i < StrutTravel.size(); ++i)
        StrutTravel[i] = StrutFor(i).StaticCompression;
    SlipDeflections.assign(ActiveConfiguration.Wheels.size(), SlipState{});

    SoftTyres.clear();
    SoftTyres.resize(ActiveConfiguration.Wheels.size());
    for (size_t i = 0; i < ActiveConfiguration.Wheels.size(); ++i)
    {
        Vec3 MountWorld, AxisUp;
        const Vec3 hub = ResolveHub(i, initial, MountWorld, AxisUp);
        SoftTyres[i].Build(ActiveConfiguration.Tyre, hub, initial.Orientation);
        if (i < CurrentTelemetry.Wheels.size())
        {
            CurrentTelemetry.Wheels[i].HubPosition = hub;
            CurrentTelemetry.Wheels[i].HubRotation = initial.Orientation;
        }
    }
    CurrentTelemetry.WheelCount = static_cast<uint32_t>(SoftTyres.size());

    CurrentTelemetry.Chassis = initial;
    CurrentTelemetry.EngineRPM = ActiveConfiguration.Engine.IdleRPM;
    CurrentTelemetry.GearIndex = GearIndex;
    CurrentTelemetry.PacejkaActive = ActiveConfiguration.ActiveScheme == DrivingScheme::PacejkaDrivetrain;

    ConstructionComplete = !SoftTyres.empty() && static_cast<bool>(ChassisHooks.ReadChassis) &&
             static_cast<bool>(ChassisHooks.ApplyForceAtPoint) && static_cast<bool>(ChassisHooks.Ground);
}

//------------------------------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------------
//                                                   SUSPENSION STRUTS
//------------------------------------------------------------------------------------------------------------------------

// Which axle's specification this corner runs. Wheels 0/1 are front, 2/3 rear — the order MakeWheelMounts emits.
const SuspensionSpecification& VehicleSolver::StrutFor(size_t Wheel) const noexcept
{
    return (Wheel < 2u) ? ActiveConfiguration.FrontStrut : ActiveConfiguration.RearStrut;
}

// Where this corner's hub currently sits, given the strut compression it has integrated to.
//    📐 The hub hangs FreeLength − compression below the mount along the strut axis, so a compressing strut pulls
//    the hub up toward the chassis. MountWorld and AxisUp come back for the force application below.
Vec3 VehicleSolver::ResolveHub(size_t Wheel, const ChassisState& cs, Vec3& MountWorld, Vec3& AxisUp) const noexcept
{
    const WheelMount& w = ActiveConfiguration.Wheels[Wheel];
    if (!ActiveConfiguration.SuspensionEnabled)
    {
        MountWorld = cs.Position + cs.Orientation.Rotate(w.SuspensionMount);
        AxisUp     = cs.Orientation.Rotate({0.0f, 0.0f, 1.0f});
        return cs.Position + cs.Orientation.Rotate(w.LocalOffset);
    }

    const SuspensionSpecification& spec = StrutFor(Wheel);
    // The axis is authored for the LEFT side; mirror Y for the right, exactly as GRIT does by wheel code.
    const float mirror = (w.LocalOffset.y < 0.0f) ? -1.0f : 1.0f;
    const Vec3  axisLocal{ spec.AxisX, spec.AxisY * mirror, spec.AxisZ };
    const Vec3  axisWorld = cs.Orientation.Rotate(axisLocal);          // points DOWN the strut (compression is −axis)

    MountWorld = cs.Position + cs.Orientation.Rotate(w.SuspensionMount);
    AxisUp     = Vec3{ -axisWorld.x, -axisWorld.y, -axisWorld.z };
    const float hang = spec.FreeLength - StrutTravel[Wheel];           // [m] mount-to-hub distance right now
    return MountWorld + axisWorld * hang;
}

// Advance one strut by Δτ and return the force it pushes the chassis up with, along AxisUp.
//    📐 The unsprung corner is a particle on the strut axis. Along AxisUp it feels the tyre pushing it up, the
//    spring and damper pushing it back down toward full extension, the anti-roll bar, and its own weight:
//
//        𝑚ᵤ ẍ = F_z·û_z − (F_spring + F_damper + F_bar) − 𝑚ᵤ g û_z
//
//    Integrating that is what lets the wheel move over a bump while the body stays where it is — which is the
//    entire point of a suspension, and precisely what the old rigid hub could not do.
float VehicleSolver::IntegrateStrut(size_t Wheel, float Δτ, float TyreLoad, const Vec3& AxisUp, float BarForce) noexcept
{
    const SuspensionSpecification& spec = StrutFor(Wheel);
    const float g  = std::fabs(ActiveConfiguration.Gravity.z);
    const float mu = std::max(1.0f, ActiveConfiguration.UnsprungMass);
    float x = StrutTravel[Wheel];
    float v = StrutRate[Wheel];

    const float spring = spec.Preload + spec.SpringForce(x);           // [N] pushing the hub away from the mount
    float other = spring + BarForce;                                   // [N] everything that is NOT rate-dependent

    // Bump stop: past the stroke limit the strut is steel on rubber, not a spring. One stiff penalty term keeps the
    //    chassis off the axle. Its damping goes into the implicit coefficient below, not on top of the force.
    const float overTravel = x - spec.MaxTravel;
    float rate = spec.DampingRate;                                     // [N·s/m] total rate-dependent coefficient
    if (overTravel > 0.0f)
    {
        other += overTravel * spec.SpringRate * 12.0f;
        rate  += spec.DampingRate * 4.0f;
    }

    // 🔴 The damper is solved IMPLICITLY. Explicitly, a 4.5 kN·s/m damper on a 20 kg unsprung corner at 240 Hz sits
    //    right on the stability limit (𝑐Δτ/𝑚 ≈ 0.94), so any load spike — a kerb, or a tyre resolving an initial
    //    penetration — rings the corner apart instead of absorbing it. Rearranging
    //        𝑣' = 𝑣 + (Δτ/𝑚)(F − 𝑐𝑣')   ⇒   𝑣' = (𝑣 + (Δτ/𝑚)F) / (1 + 𝑐Δτ/𝑚)
    //    makes it unconditionally stable for any rate, which is what lets the bump stop be as stiff as it needs.
    const float applied = TyreLoad * AxisUp.z - other - mu * g * AxisUp.z;   // [N] rate-independent net force
    v = (v + (Δτ / mu) * applied) / (1.0f + rate * Δτ / mu);
    x += v * Δτ;

    const float damper = rate * v;                                     // [N] the damper force actually delivered
    float strut = spring + damper + BarForce;
    if (overTravel > 0.0f) strut += overTravel * spec.SpringRate * 12.0f;

    // A strut cannot pull: at full extension the spring is done and the wheel simply hangs.
    if (x < 0.0f) { x = 0.0f; if (v < 0.0f) v = 0.0f; }
    const float hardStop = spec.MaxTravel + 0.02f;
    if (x > hardStop) { x = hardStop; if (v > 0.0f) v = 0.0f; }

    StrutTravel[Wheel] = x;
    StrutRate[Wheel]   = v;
    return std::max(0.0f, strut);
}

// Anti-roll bar force for one corner: proportional to how much MORE compressed it is than its partner. Equal
//    compression on both sides cancels, so the bar changes roll stiffness without touching ride rate.
float VehicleSolver::AntiRollForce(size_t Wheel) const noexcept
{
    const AntiRollBar& bar = (Wheel < 2u) ? ActiveConfiguration.FrontBar : ActiveConfiguration.RearBar;
    if (!bar.Enabled) return 0.0f;
    const size_t partner = (Wheel == bar.LeftWheel) ? bar.RightWheel : bar.LeftWheel;
    if (partner >= StrutTravel.size() || Wheel >= StrutTravel.size()) return 0.0f;
    return bar.Stiffness * (StrutTravel[Wheel] - StrutTravel[partner])
         + bar.Damping   * (StrutRate[Wheel]   - StrutRate[partner]);
}

void VehicleSolver::Step(float Δτ) noexcept
{
    if (!ConstructionComplete || Δτ <= 0.0f) return;
    if (ActiveConfiguration.ActiveScheme == DrivingScheme::PacejkaDrivetrain) StepPacejka(Δτ);
    else                                                  StepSimple(Δτ);
}

//------------------------------------------------------------------------------------------------------------------------
// SimpleFrictionCircle — the validated Phase-3 arcade layer (kept as a selectable fallback).
//------------------------------------------------------------------------------------------------------------------------
void VehicleSolver::StepSimple(float Δτ) noexcept
{
    const ChassisState cs = ChassisHooks.ReadChassis();

    // Chassis basis in world.
    const Vec3 forward = cs.Orientation.Rotate({1.0f, 0.0f, 0.0f});
    const Vec3 up      = cs.Orientation.Rotate({0.0f, 0.0f, 1.0f});
    const float speed      = cs.LinearVelocity.Length();
    const float forwardVel = Dot(cs.LinearVelocity, forward);

    // First-order steer smoothing toward the commanded lock.
    const float targetSteer = Clamp(DriverCommand.Steer, -1.0f, 1.0f) * ActiveConfiguration.MaxSteerAngleRad;
    const float alpha = std::min(1.0f, Δτ * ActiveConfiguration.SteerRatePerSecond);
    SteerAngle += (targetSteer - SteerAngle) * alpha;

    CurrentTelemetry.Chassis = cs;
    CurrentTelemetry.SpeedMetresPerSecond = speed;
    CurrentTelemetry.ForwardSpeed = forwardVel;
    CurrentTelemetry.TotalVerticalLoad = 0.0f;
    CurrentTelemetry.WheelsInContact = 0u;

    const float mu = ActiveConfiguration.GripCoefficient;

    for (size_t i = 0; i < SoftTyres.size(); ++i)
    {
        const WheelMount& wheel = ActiveConfiguration.Wheels[i];
        XPBDSoftTyre&     tyre  = SoftTyres[i];

        // Rest hub world transform (the tyre models the compliance; the hub follows the chassis rigidly).
        const Vec3 armLocal = wheel.LocalOffset;
        const Vec3 hub = cs.Position + cs.Orientation.Rotate(armLocal);
        const Quat steerQ  = wheel.Steered ? Quat::AxisAngle(up, SteerAngle) : Quat{0, 0, 0, 1};
        const Quat hubRot  = QuatNormalize(QuatMul(steerQ, cs.Orientation));

        // Hub velocity from rigid-body kinematics (v + ω × r).
        const Vec3 r      = hub - cs.Position;
        const Vec3 hubVel = cs.LinearVelocity + Cross(cs.AngularVelocity, r);

        // The carcass ROLLS: the hub is handed its travel and its spin, and the road is a road — static ground,
        //    not a belt moving at hub speed. The belt was a stand-in for a tyre that could not rotate, and it
        //    made every contacting node scrub at road speed. The in-plane forces the carcass now generates are
        //    its own; this scheme still takes only Fz from it and drives through the friction-circle layer below.
        HubMotion motion;
        motion.Position       = hub;
        motion.Orientation    = hubRot;
        motion.LinearVelocity = hubVel;
        motion.SpinRate       = WheelSpin[i];
        tyre.Step(Δτ, ActiveConfiguration.TyreSubsteps, motion, Vec3{0.0f, 0.0f, 0.0f}, ChassisHooks.Ground);
        const TyreReaction& reaction = tyre.Reaction();

        const float Fz    = std::max(0.0f, reaction.Force.z);
        const bool  onGnd = reaction.ContactCount > 0u && Fz > 1.0f;
        const Vec3  patch = onGnd ? reaction.PatchCentre : Vec3{hub.x, hub.y, hub.z - ActiveConfiguration.Tyre.Radius};

        // Wheel planar axes (with steer).
        const Vec3 wf = PlanarNormalized(hubRot.Rotate({1.0f, 0.0f, 0.0f}));
        const Vec3 wl = PlanarNormalized(hubRot.Rotate({0.0f, 1.0f, 0.0f}));
        const float vLong = Dot(hubVel, wf);
        const float vLat  = Dot(hubVel, wl);

        // Longitudinal demand: engine (driven) minus a braking magnitude that always OPPOSES motion and can never
        //    reverse the car — the braking force is clamped to at most what brings this wheel's share to a standstill
        //    this step (mass·|v|/Δτ / wheels), so brakes + rolling resistance decelerate cleanly to zero and hold.
        const float engine = wheel.Driven ? DriverCommand.Throttle * ActiveConfiguration.DriveForcePerWheel : 0.0f;
        float brakeMag = 0.0f;
        if (wheel.Braked)                        brakeMag += DriverCommand.Brake * ActiveConfiguration.BrakeForcePerWheel;
        if (DriverCommand.Handbrake && !wheel.Steered)  brakeMag += ActiveConfiguration.HandbrakeForce;
        brakeMag += ActiveConfiguration.RollingResistance * Fz;
        const float wheelCount = static_cast<float>(std::max<size_t>(1, SoftTyres.size()));
        const float maxStopForce = std::fabs(vLong) * ActiveConfiguration.ChassisMass / (Δτ * wheelCount);
        const float brakeApplied = std::min(brakeMag, maxStopForce);
        float longDemand = engine - brakeApplied * Sign(vLong);

        // Lateral demand: a load-scaled linear cornering force opposing side-slip velocity (damped, so it settles).
        const float loadFraction = (ActiveConfiguration.ChassisMass > 1.0f)
                                 ? Fz / (ActiveConfiguration.ChassisMass * 9.81f / std::max<size_t>(1, SoftTyres.size()))
                                 : 1.0f;
        float latDemand = -ActiveConfiguration.CorneringStiffness * loadFraction * vLat;

        // Friction circle: clamp the combined in-plane force to μ·Fz.
        Vec3 planar = wf * longDemand + wl * latDemand;
        const float grip = mu * Fz;
        const float mag  = planar.Length();
        if (grip > 0.0f && mag > grip) planar = planar * (grip / mag);
        else if (grip <= 0.0f)          planar = {0, 0, 0};   // airborne wheel: no in-plane force

        // Suspension damper: resist vertical hub velocity so the soft tyre does not bounce (a shock absorber). The tyre
        //    can only push, so the damped vertical force is clamped non-negative.
        float verticalForce = Fz;
        if (onGnd)
        {
            const float vVert = hubVel.z;
            verticalForce = std::max(0.0f, Fz - ActiveConfiguration.SuspensionDamping * vVert);
        }

        // Apply the tyre's vertical reaction (plus damper) and the driving force at the contact patch.
        const Vec3 wheelForce = Vec3{0.0f, 0.0f, verticalForce} + planar;
        ChassisHooks.ApplyForceAtPoint(wheelForce, patch);

        // Telemetry.
        WheelTelemetry& wt = CurrentTelemetry.Wheels[std::min<size_t>(i, CurrentTelemetry.Wheels.size() - 1)];
        wt.HubPosition       = hub;
        wt.HubRotation       = hubRot;
        wt.ContactPoint      = patch;
        wt.VerticalLoad      = Fz;
        wt.LongitudinalForce = Dot(planar, wf);
        wt.LateralForce      = Dot(planar, wl);
        wt.SteerAngleRad     = wheel.Steered ? SteerAngle : 0.0f;
        wt.ContactCount      = reaction.ContactCount;
        wt.InContact         = onGnd;

        CurrentTelemetry.TotalVerticalLoad += Fz;
        if (onGnd) ++CurrentTelemetry.WheelsInContact;
    }
}

//------------------------------------------------------------------------------------------------------------------------
// PacejkaDrivetrain — production layer.
//
//   Same Fz-from-soft-tyre / direct-heightfield-contact path as StepSimple, but the in-plane forces are now the full
//   Magic-Formula combined-slip forces (Phase 1) driven by real per-wheel spin state and a real drivetrain:
//
//       engine → turbo → clutch → gearbox → differential ─┐   (Drivetrain::Step, GT-R defaults)
//                                                          ▼
//       per wheel:   Iw·ω̇ = T_drive − Fx·Reff − T_brake·sign(ω) − T_roll         (wheel spin ODE)
//                    slip κ,α from (Vx, Vsy, ω·Reff)  →  Fx, Fy, Mz              (TyreSlipDynamics + Pacejka)
//                    force  fwd·Fx + left·Fy + up·Fz  applied at the contact patch
//
//   The relaxation-length slip solver keeps this stable through standstill (no velocity-in-denominator singularity), so
//   the vehicle can launch from rest, hold under braking, and settle without the arcade friction-circle clamp.
//------------------------------------------------------------------------------------------------------------------------
void VehicleSolver::StepPacejka(float Δτ) noexcept
{
    const ChassisState cs = ChassisHooks.ReadChassis();

    const Vec3 forward = cs.Orientation.Rotate({1.0f, 0.0f, 0.0f});
    const Vec3 up      = cs.Orientation.Rotate({0.0f, 0.0f, 1.0f});
    const float speed      = cs.LinearVelocity.Length();
    const float forwardVel = Dot(cs.LinearVelocity, forward);

    // First-order steer smoothing toward the commanded lock.
    const float targetSteer = Clamp(DriverCommand.Steer, -1.0f, 1.0f) * ActiveConfiguration.MaxSteerAngleRad;
    const float steerBlend  = std::min(1.0f, Δτ * ActiveConfiguration.SteerRatePerSecond);
    SteerAngle += (targetSteer - SteerAngle) * steerBlend;

    const float Reff = (ActiveConfiguration.EffectiveRadius > 0.01f) ? ActiveConfiguration.EffectiveRadius : ActiveConfiguration.Tyre.Radius;
    const float Iw   = std::max(0.05f, ActiveConfiguration.WheelInertia);

    // ── Drivetrain step: gather driven-wheel spin as rpm feedback, split left/right by hub Y sign ────────────────────
    float sumRpm = 0.0f; int nDriven = 0;
    float leftRpm = 0.0f, rightRpm = 0.0f; bool haveL = false, haveR = false;
    for (size_t i = 0; i < ActiveConfiguration.Wheels.size(); ++i)
    {
        if (!ActiveConfiguration.Wheels[i].Driven) continue;
        const float rpm = WheelSpin[i] * Drivetrain::kRadToRpm;
        sumRpm += rpm; ++nDriven;
        if (ActiveConfiguration.Wheels[i].LocalOffset.y > 0.0f) { leftRpm = rpm; haveL = true; }
        else                                        { rightRpm = rpm; haveR = true; }
    }
    const float drivenAvgRpm = (nDriven > 0) ? sumRpm / static_cast<float>(nDriven) : 0.0f;
    if (!haveL) leftRpm  = drivenAvgRpm;
    if (!haveR) rightRpm = drivenAvgRpm;

    // Auto-clutch: an AMT holds NEUTRAL at a standstill (clutch open) and engages 1st only when the driver asks for
    //    drive. Without this the clutch would transmit full idle-slip torque in gear and the car would creep off with no
    //    throttle. Brakes act directly on the wheels, so they still work in neutral.
    const int neutral   = ActiveConfiguration.Transmission.NeutralIndex;
    const int firstGear = neutral + 1;
    const int lastGear  = static_cast<int>(ActiveConfiguration.Transmission.GearRatios.size()) - 1;
    const float throttleCmd = Clamp(DriverCommand.Throttle, 0.0f, 1.0f);
    if (GearIndex == neutral && (throttleCmd > 0.05f || forwardVel > 1.0f))
        GearIndex = firstGear;

    DrivetrainInputs di;
    di.Throttle       = throttleCmd;
    di.GearIndex      = GearIndex;
    di.dt             = Δτ;
    di.DrivenWheelRPM = drivenAvgRpm;
    di.LeftWheelRPM   = leftRpm;
    di.RightWheelRPM  = rightRpm;
    const DrivetrainOutputs dOut = Powertrain.Step(di);
    const int gearUsed = di.GearIndex;   // gear the drive torque was produced in — Ieff must match it (below)

    // ── Automatic gearbox (AMT): up/down-shift on engine rpm with a cooldown, drop to neutral once nearly stopped ─────
    ShiftTimer += Δτ;
    if (GearIndex != neutral)
    {
        if (ShiftTimer >= ActiveConfiguration.ShiftCooldownSeconds)
        {
            if (dOut.EngineRPM > ActiveConfiguration.UpshiftRPM && GearIndex < lastGear && throttleCmd > 0.1f)
            {
                ++GearIndex; ShiftTimer = 0.0f;
            }
            else if (dOut.EngineRPM < ActiveConfiguration.DownshiftRPM && GearIndex > firstGear)
            {
                --GearIndex; ShiftTimer = 0.0f;
            }
        }
        // Declutch once nearly stopped, OR when braking firmly at low speed — otherwise idle creep through the clutch
        //    fights the brake and the car settles at a crawl instead of coming fully to rest (as a real driver would
        //    clutch in / the auto would decouple against the held brake).
        const bool nearlyStopped = speed < 0.5f && throttleCmd < 0.05f;
        const bool brakingToStop = DriverCommand.Brake > 0.4f && throttleCmd < 0.05f && speed < 3.0f;
        if (nearlyStopped || brakingToStop) GearIndex = neutral;
    }

    CurrentTelemetry.Chassis = cs;
    CurrentTelemetry.SpeedMetresPerSecond = speed;
    CurrentTelemetry.ForwardSpeed = forwardVel;
    CurrentTelemetry.TotalVerticalLoad = 0.0f;
    CurrentTelemetry.WheelsInContact = 0u;
    CurrentTelemetry.EngineRPM = dOut.EngineRPM;
    CurrentTelemetry.TurboRPM  = dOut.TurboRPM;
    CurrentTelemetry.BoostBar  = dOut.BoostPressure_Bar;
    CurrentTelemetry.ParasiticDrag_Nm = dOut.ParasiticDrag_Nm;
    CurrentTelemetry.GearIndex = GearIndex;
    CurrentTelemetry.PacejkaActive = true;

    // ── Aerodynamics (GRIT source-only port, physically CLOSED-LOOP) ─────────────────────────────────────────────────
    //   Compute the whole aero package ONCE per step from the chassis state. Drag, side-force and the YAW/ROLL moments
    //   act on the chassis directly. DOWNFORCE is applied as a REAL downward force at each wheel's contact patch (front/
    //   rear share) inside the loop below — so the soft tyre physically compresses, its Fz rises, the car squats, and the
    //   lower ride height feeds back into the ground-effect terms next step. This is the closed loop a compliant tyre
    //   makes possible (no analytic load-transfer solver, no free vertical momentum): grip EMERGES from the higher Fz.
    //   The front/rear split applied at the axles also produces the aero pitch moment via the lever arms, so we do NOT
    //   additionally apply aero.PitchMoment_Nm here (that would double-count it). Ride height comes from the live CoM
    //   height above the sampled ground, so it responds to squat.
    AeroForces aero{};
    int nFrontWheels = 0, nRearWheels = 0;
    for (const WheelMount& w : ActiveConfiguration.Wheels) { if (w.LocalOffset.x > 0.0f) ++nFrontWheels; else ++nRearWheels; }
    if (ActiveConfiguration.Aero.Enabled)
    {
        const Vec3  right   = cs.Orientation.Rotate({0.0f, 1.0f, 0.0f});
        const float yawRate = Dot(cs.AngularVelocity, up);
        // Ride height wants the floor under the car, and the surface query now returns a point rather than a
        //    height, so take its z. Directly below the CoM that is the same number it always was.
        Vec3 groundPt{}; Vec3 groundN{0.0f, 0.0f, 1.0f};
        float rideHeight = ActiveConfiguration.Aero.FloorDiffuser.RideHeightOptimum_m;
        if (ChassisHooks.Ground(cs.Position, groundPt, groundN))
            rideHeight = std::max(0.005f, (cs.Position.z - groundPt.z) - ActiveConfiguration.Aero.ComHeightAboveFloor_m);

        aero = ComputeAerodynamicForces(
            ActiveConfiguration.Aero, cs.LinearVelocity, rideHeight, forward, right, up,
            Clamp(DriverCommand.Brake, 0.0f, 1.0f), DriverCommand.Handbrake ? 1.0f : 0.0f, throttleCmd, yawRate);

        // Drag (opposes velocity) + body side-force → chassis at the CoM.
        ChassisHooks.ApplyForceAtPoint(aero.DragForceWorld, cs.Position);
        ChassisHooks.ApplyForceAtPoint(aero.SideForceWorld, cs.Position);
        // Yaw + roll moments about the CoM (roll↦body-X/forward, yaw↦body-Z/up). Pitch is produced by the axle-applied
        // downforce below, so it is intentionally omitted here.
        if (ChassisHooks.ApplyTorque)
            ChassisHooks.ApplyTorque(forward * aero.RollMoment_Nm + up * aero.YawMoment_Nm);
    }
    const float frontDownforcePerWheel = (nFrontWheels > 0) ? std::max(0.0f, aero.FrontDownforce_N) / static_cast<float>(nFrontWheels) : 0.0f;
    const float rearDownforcePerWheel  = (nRearWheels  > 0) ? std::max(0.0f, aero.RearDownforce_N)  / static_cast<float>(nRearWheels)  : 0.0f;
    CurrentTelemetry.Aero = aero;

    for (size_t i = 0; i < SoftTyres.size(); ++i)
    {
        const WheelMount& wheel = ActiveConfiguration.Wheels[i];
        XPBDSoftTyre&     tyre  = SoftTyres[i];

        Vec3 mountWorld{}, axisUp{};
        const Vec3 hub    = ResolveHub(i, cs, mountWorld, axisUp);
        const Quat steerQ = wheel.Steered ? Quat::AxisAngle(up, SteerAngle) : Quat{0, 0, 0, 1};
        const Quat hubRot = QuatNormalize(QuatMul(steerQ, cs.Orientation));

        const Vec3 r      = hub - cs.Position;
        // The hub rides the chassis AND slides along its own strut, so its velocity carries both terms. Feeding the
        //    tyre a hub velocity that ignored the strut rate is what made a bump shove the whole car instead of
        //    just the wheel.
        Vec3 hubVel = cs.LinearVelocity + Cross(cs.AngularVelocity, r);
        if (ActiveConfiguration.SuspensionEnabled) hubVel += axisUp * StrutRate[i];

        // Vertical load Fz from the soft tyre, which now rolls: hub travel and wheel spin go in, the ground is
        //    static. WheelSpin is this wheel's own rotation, integrated at the foot of this routine from the
        //    drivetrain and brake torques, so the carcass turns at the rate the rest of the model believes it
        //    does. Traction and cornering still come from the Pacejka layer below; only Fz is read back here.
        HubMotion motion;
        motion.Position       = hub;
        motion.Orientation    = hubRot;
        motion.LinearVelocity = hubVel;
        motion.SpinRate       = WheelSpin[i];
        tyre.Step(Δτ, ActiveConfiguration.TyreSubsteps, motion, Vec3{0.0f, 0.0f, 0.0f}, ChassisHooks.Ground);
        const TyreReaction& reaction = tyre.Reaction();

        const float Fz    = std::max(0.0f, reaction.Force.z);
        const bool  onGnd = reaction.ContactCount > 0u && Fz > 1.0f;
        const Vec3  patch = onGnd ? reaction.PatchCentre : Vec3{hub.x, hub.y, hub.z - ActiveConfiguration.Tyre.Radius};

        // Aero downforce for THIS wheel (front axle vs rear axle share). Applied as a REAL downward force at the patch
        //    below (closed loop) — it presses the tyre, the tyre compresses, and its Fz rises over the next steps, so
        //    grip emerges from the genuine load. We therefore feed the MEASURED Fz to the slip model, not an injected one.
        const float aeroDownforce = onGnd ? ((wheel.LocalOffset.x > 0.0f) ? frontDownforcePerWheel : rearDownforcePerWheel) : 0.0f;

        // Wheel planar axes (with steer) and the slip-velocity components in that frame.
        const Vec3  wf  = PlanarNormalized(hubRot.Rotate({1.0f, 0.0f, 0.0f}));
        const Vec3  wl  = PlanarNormalized(hubRot.Rotate({0.0f, 1.0f, 0.0f}));
        const float Vx  = Dot(hubVel, wf);
        // Physical lateral velocity of the hub (+ = toward +wl / chassis-left).
        const float VlatHub = Dot(hubVel, wl);
        // Slip-angle convention: the model uses α = atan(Vsy/Vx) with NO sign inversion and returns Fy with the same
        //    sign as α, so a restoring (grip) force requires Vsy = −(lateral hub velocity) — the standard SAE definition
        //    α = −atan(Vy/Vx). Feed the negated velocity here; the returned Fy is then restoring along +wl.
        const float Vsy = -VlatHub;

        // ── Tyre slip forces (transient Magic-Formula) ──────────────────────────────────────────────────────────────
        float Fx = 0.0f, Fy = 0.0f, kappa = 0.0f, alpha = 0.0f;
        if (onGnd)
        {
            WheelKinematics K;
            K.Vx        = Vx;
            K.Vsy       = Vsy;
            K.OmegaR    = WheelSpin[i] * Reff;
            K.CamberRad = 0.0f;
            K.Fz_N      = Fz;
            const SlipResult sr = SlipDynamics->Step(K, SlipDeflections[i], Δτ);
            Fx = sr.Forces.Fx; Fy = sr.Forces.Fy; kappa = sr.Kappa; alpha = sr.AlphaRad;

            // Lateral contact damping: viscous carcass damping opposing the lateral slide velocity (physical, +wl frame).
            //    Reacted on the chassis (it is a real tyre force) and faded out with speed so fast cornering stays pure
            //    Pacejka.
            const float fade = 1.0f / (1.0f + (speed / std::max(0.1f, ActiveConfiguration.ContactDampingFadeSpeed)) *
                                              (speed / std::max(0.1f, ActiveConfiguration.ContactDampingFadeSpeed)));
            Fy -= ActiveConfiguration.LateralDampingCoefficient * fade * VlatHub;

            // Numerical safety net only (NOT the arcade clamp): keep the combined force inside a generous friction
            //    circle so a transient slip spike can never inject unbounded energy. Well-behaved slip stays untouched.
            const float grip = ActiveConfiguration.GripCoefficient * Fz * 1.3f;
            const float pmag = std::sqrt(Fx * Fx + Fy * Fy);
            if (grip > 0.0f && pmag > grip) { const float s = grip / pmag; Fx *= s; Fy *= s; }

            // ── Low-speed longitudinal STATIC friction (stiction) ──────────────────────────────────────────────────
            //   The transient slip model gives Fx → 0 as slip → 0, and the low-speed spin stabiliser holds the wheel
            //   near free-rolling, so at a crawl the tyre cannot represent the STATIC friction that actually (a) brings
            //   a braked car fully to rest and holds it, and (b) keeps a parked car from sliding down a grade up to
            //   arctan(μ). Blend in a direct arresting force — the reaction that pins this wheel's mass share, capped by
            //   available grip μ·Fz — whenever the car is NOT being driven. It fades out with speed (blend→0 by ~2 m/s),
            //   so launches and all normal driving keep pure Pacejka behaviour. Only engages while braking/coasting.
            if (throttleCmd < 0.05f)
            {
                const float crawl = 2.0f;   // [m/s] below this, stiction takes over from the slip model
                const float blend = 1.0f - std::min(1.0f, speed / crawl);
                if (blend > 0.0f)
                {
                    const float share   = ActiveConfiguration.ChassisMass / static_cast<float>(std::max<size_t>(1, SoftTyres.size()));
                    const float maxHold = ActiveConfiguration.GripCoefficient * Fz;         // static friction cap μ·Fz
                    const float hold    = Clamp(-Vx * share / Δτ, -maxHold, maxHold);
                    Fx = Fx * (1.0f - blend) + hold * blend;
                }
            }
        }

        const Vec3 planar      = wf * Fx + wl * Fy;
        const Vec3 aeroDownVec = up * (-aeroDownforce);
        float barForce   = 0.0f;
        float strutForce = 0.0f;

        if (ActiveConfiguration.SuspensionEnabled)
        {
            // 💡 The load path now has two stages, which is the whole difference. The tyre pushes the UNSPRUNG
            //    corner up; the strut is what pushes the SPRUNG body up, at the mount, and it only transmits what
            //    the spring and damper are actually carrying. Previously the tyre force went straight into the
            //    chassis at the patch, which is a car with its axles welded to the floor pan.
            barForce   = AntiRollForce(i);
            strutForce = IntegrateStrut(i, Δτ, Fz, axisUp, barForce);
            ChassisHooks.ApplyForceAtPoint(axisUp * strutForce, mountWorld);
            ChassisHooks.ApplyForceAtPoint(planar + aeroDownVec, patch);
        }
        else
        {
            float verticalForce = Fz;
            if (onGnd) verticalForce = std::max(0.0f, Fz - ActiveConfiguration.SuspensionDamping * hubVel.z);
            ChassisHooks.ApplyForceAtPoint(Vec3{0.0f, 0.0f, verticalForce} + planar + aeroDownVec, patch);
        }

        // ── Wheel spin ODE:  Iw·ω̇ = T_drive − Fx·Reff − T_brake·sign(ω) − T_roll ───────────────────────────────────
        float driveTq = 0.0f;
        if (wheel.Driven)
            driveTq = (wheel.LocalOffset.y > 0.0f) ? dOut.LeftDriveTorque_Nm : dOut.RightDriveTorque_Nm;

        float brakeTq = 0.0f;
        if (wheel.Braked)
        {
            if (ActiveConfiguration.BrakeThermalEnabled)
            {
                // Hydraulic/thermal disk model with ABS. ABS watches this wheel's slip κ (computed above) and, above the
                //    ABS min speed, pulses the line pressure to hold slip near the peak-grip target — the brake torque is
                //    still applied through the anti-reversal cap below, so a modulated wheel decelerates without locking.
                BrakeWheelInput bi;
                bi.PedalCommand = Clamp(DriverCommand.Brake, 0.0f, 1.0f);
                bi.WheelOmega   = WheelSpin[i];
                bi.SlipRatio    = kappa;
                bi.Airspeed     = speed;
                bi.Braked       = true;
                const BrakeWheelOutput bo = BrakingHydraulics.Step(i, bi, Δτ);
                brakeTq += bo.BrakeTorque_Nm;
                WheelTelemetry& bt = CurrentTelemetry.Wheels[std::min<size_t>(i, CurrentTelemetry.Wheels.size() - 1)];
                bt.BrakeTorque_Nm   = bo.BrakeTorque_Nm;
                bt.BrakePressure_Pa = bo.Pressure_Pa;
                bt.BrakeTemp_K      = bo.Temperature_K;
                bt.AbsActive        = bo.AbsActive;
            }
            else
            {
                brakeTq += DriverCommand.Brake * ActiveConfiguration.MaxBrakeTorquePerWheel;
            }
        }
        if (DriverCommand.Handbrake && !wheel.Steered) brakeTq += ActiveConfiguration.HandbrakeTorque;
        const float rollTq = ActiveConfiguration.RollingResistance * Fz * Reff;

        // Effective spin inertia. A driven wheel is rigidly geared to the engine + gearbox through a stiff clutch, so its
        //    effective rotational inertia is the wheel PLUS the drivetrain inertia reflected through (gear·finalDrive)².
        //    This is not just realism: with only the bare wheel inertia the stiff clutch coupling makes explicit Euler at
        //    240 Hz blow up (Δτ ≫ 2·I/c). The reflected inertia (~40 kg·m² in 1st) keeps the integration stable.
        float Ieff = Iw;
        if (wheel.Driven && nDriven > 0)
        {
            const float g = ActiveConfiguration.Transmission.RatioAt(gearUsed) * ActiveConfiguration.Transmission.FinalDriveRatio;
            Ieff += ActiveConfiguration.Engine.EngineInertia * g * g / static_cast<float>(nDriven);
        }

        // ── Semi-implicit wheel-spin integration with low-speed stabilisation ───────────────────────────────────────
        //   At a standstill the relaxation deflection is a pure integrator and the Magic-Formula force saturates at
        //   ±μ·Fz, so the contact behaves like dry friction (a relay). Coupled to the wheel inertia that is a stick-slip
        //   limit cycle no explicit damping can tame without itself going unstable. So the spin is integrated IMPLICITLY
        //   with a viscous pull toward the kinematic free-rolling speed ω_roll = Vx/Reff:
        //
        //        (Iw/Δτ + b)·ω¹ = (Iw/Δτ)·ω⁰ + T_drive − Fx·Reff + b·ω_roll
        //
        //   This is unconditionally stable for any b. b is strong at low speed (kills the relay) and FADES OUT with
        //   speed (fade → 0), so at speed the wheel spins/locks under the full slip dynamics with no artificial pull.
        //   ω_roll → drive still produces slip = (T_drive − Fx·Reff)/b, i.e. genuine wheelspin/launch behaviour.
        const float spinFade = 1.0f / (1.0f + (speed / std::max(0.1f, ActiveConfiguration.ContactDampingFadeSpeed)) *
                                              (speed / std::max(0.1f, ActiveConfiguration.ContactDampingFadeSpeed)));
        const float b        = onGnd ? ActiveConfiguration.SpinDampingCoefficient * spinFade : 0.0f;   // [N·m per rad/s]
        const float omegaRoll = onGnd ? Vx / Reff : WheelSpin[i];

        const float omega0 = WheelSpin[i];
        const float invMass = 1.0f / (Ieff / Δτ + b);
        float omega1 = ((Ieff / Δτ) * omega0 + driveTq - Fx * Reff + b * omegaRoll) * invMass;

        // Brake + rolling resistance oppose spin but can never drive it through zero (anti-reversal): cap the resistive
        //    torque at exactly what brings ω to rest this step. A locked wheel then stays locked (κ → −1) while the car
        //    is moving, and the vehicle brakes cleanly to a standstill and holds there.
        const float resist = std::min(brakeTq + rollTq, std::fabs(omega1) * Ieff / Δτ);
        omega1 -= resist * Sign(omega1) * (Δτ / Ieff);
        WheelSpin[i] = omega1;

        // Telemetry.
        WheelTelemetry& wt = CurrentTelemetry.Wheels[std::min<size_t>(i, CurrentTelemetry.Wheels.size() - 1)];
        wt.HubPosition       = hub;
        wt.HubRotation       = hubRot;
        wt.ContactPoint      = patch;
        wt.VerticalLoad      = Fz;
        wt.LongitudinalForce = Fx;
        wt.LateralForce      = Fy;
        wt.SteerAngleRad     = wheel.Steered ? SteerAngle : 0.0f;
        wt.ContactCount      = reaction.ContactCount;
        wt.InContact         = onGnd;
        wt.WheelOmega        = WheelSpin[i];
        wt.SlipRatio         = kappa;
        wt.SlipAngleRad      = alpha;
        wt.AeroDownforce     = aeroDownforce;
        wt.StrutCompression  = ActiveConfiguration.SuspensionEnabled ? StrutTravel[i] : 0.0f;
        wt.StrutRate         = ActiveConfiguration.SuspensionEnabled ? StrutRate[i]   : 0.0f;
        wt.StrutForce        = strutForce;
        wt.AntiRollForce     = barForce;

        CurrentTelemetry.TotalVerticalLoad += Fz;
        if (onGnd) ++CurrentTelemetry.WheelsInContact;
    }
}

} // namespace Frontier::Vehicle
