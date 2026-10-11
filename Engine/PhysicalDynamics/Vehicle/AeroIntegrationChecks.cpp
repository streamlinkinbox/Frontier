//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/Vehicle/AeroIntegrationChecks.cpp — end-to-end proof that the ported aerodynamics changes how the CAR drives.
//   Drives the full VehicleSolver (PacejkaDrivetrain + XPBD soft tyres) on a mock rigid chassis, comparing an
//   aero-equipped GT3 against an identical car with aero disabled. Confirms the two headline effects:
//       • DRAG      → lower terminal speed + faster coast-down
//       • DOWNFORCE → more cornering grip (higher sustained lateral acceleration before the tyres let go)
//   No engine, no Jolt — same mock-chassis harness as VehicleSceneValidation.
//
//   Build:  g++ -std=c++17 -O2 AeroIntegrationChecks.cpp VehicleSolver.cpp Aerodynamics.cpp XPBDSoftTyre.cpp \
//                 PacejkaMagicFormula.cpp TyreSlipDynamics.cpp Drivetrain.cpp -o /tmp/aerodrive && /tmp/aerodrive
//============================================================================================================================================

#include "VehicleSolver.h"
#include "VehicleGeometry.h"

#include <cmath>
#include <cstdio>

using namespace Frontier::Vehicle;

namespace {
int g_pass = 0, g_fail = 0;
void Check(const char* name, bool ok, double got, const char* rel, double ref)
{
    std::printf("  [%s] %-52s % 10.3f  %s % .3f\n", ok ? "PASS" : "FAIL", name, got, rel, ref);
    if (ok) ++g_pass; else ++g_fail;
}

struct MockChassis
{
    Vec3  Position{0, 0, 0.40f};
    Quat  Orientation{0, 0, 0, 1};
    Vec3  LinearVelocity{};
    Vec3  AngularVelocity{};
    float Mass = 1200.0f;
    Vec3  InvInertiaDiag{};
    Vec3  Gravity{0, 0, -9.81f};
    float AngularDamping = 0.05f;
    bool  PlanarOnly = false;   // lock roll+pitch (yaw only) — a flat cornering fixture that removes the placeholder
                                //   box's high-CoM rollover so a corner measures GRIP, not tip-over.
    Vec3  ForceAccum{};
    Vec3  TorqueAccum{};

    void AssignBoxInertia(const Vec3& h) noexcept
    {
        const float fx = 2 * h.x, fy = 2 * h.y, fz = 2 * h.z;
        InvInertiaDiag = {1.0f / (Mass * (fy * fy + fz * fz) / 12.0f),
                          1.0f / (Mass * (fx * fx + fz * fz) / 12.0f),
                          1.0f / (Mass * (fx * fx + fy * fy) / 12.0f)};
    }
    [[nodiscard]] Quat Conjugate() const noexcept { return {-Orientation.x, -Orientation.y, -Orientation.z, Orientation.w}; }
    [[nodiscard]] Vec3 WorldAngularAccel(const Vec3& t) const noexcept
    {
        const Vec3 tb = Conjugate().Rotate(t);
        const Vec3 ab{tb.x * InvInertiaDiag.x, tb.y * InvInertiaDiag.y, tb.z * InvInertiaDiag.z};
        return Orientation.Rotate(ab);
    }
    void ApplyForceAtPoint(const Vec3& f, const Vec3& p) noexcept { ForceAccum += f; TorqueAccum += Cross(p - Position, f); }
    void ApplyTorque(const Vec3& t) noexcept { TorqueAccum += t; }
    void Integrate(float dt) noexcept
    {
        ForceAccum += Gravity * Mass;
        LinearVelocity += ForceAccum * (dt / Mass);
        Position += LinearVelocity * dt;
        AngularVelocity += WorldAngularAccel(TorqueAccum) * dt;
        AngularVelocity = AngularVelocity * std::exp(-AngularDamping * dt);
        if (PlanarOnly) AngularVelocity = {0.0f, 0.0f, AngularVelocity.z};   // keep yaw only
        const Quat wq{AngularVelocity.x, AngularVelocity.y, AngularVelocity.z, 0.0f};
        const Quat dq = QuatMul(wq, Orientation);
        Orientation = QuatNormalize(Quat{Orientation.x + 0.5f * dt * dq.x, Orientation.y + 0.5f * dt * dq.y,
                                         Orientation.z + 0.5f * dt * dq.z, Orientation.w + 0.5f * dt * dq.w});
        if (PlanarOnly)   // re-level: strip roll/pitch, keep pure yaw about world +Z
        {
            const float yaw = std::atan2(2.0f * (Orientation.w * Orientation.z + Orientation.x * Orientation.y),
                                         1.0f - 2.0f * (Orientation.y * Orientation.y + Orientation.z * Orientation.z));
            Orientation = Quat{0.0f, 0.0f, std::sin(0.5f * yaw), std::cos(0.5f * yaw)};
        }
        ForceAccum = {0, 0, 0}; TorqueAccum = {0, 0, 0};
    }
    [[nodiscard]] ChassisState State() const noexcept { return {Position, Orientation, LinearVelocity, AngularVelocity}; }
};

XPBDSoftTyre::GroundQuery Flat() { return [](const Vec3& p, Vec3& s, Vec3& n){ s = {p.x,p.y,0}; n = {0,0,1}; return true; }; }

VehicleGeometry Geo() { return VehicleGeometry{}; }   // documented GT3-class estimates

VehicleSolverConfiguration MakeConfig(bool aero)
{
    VehicleSolverConfiguration c;
    c.ActiveScheme = DrivingScheme::PacejkaDrivetrain;
    c.Aero = AerodynamicPackage::DefaultGT3();
    ApplyGeometry(c, Geo());                 // real wheel offsets, aero force points, CoM-above-floor, mass
    if (!aero) c.Aero.Enabled = false;
    return c;
}

struct Rig
{
    MockChassis chassis;
    VehicleSolver controller;
    void Build(const VehicleSolverConfiguration& cfg, const Vec3& spawn)
    {
        chassis = MockChassis{};
        chassis.Position = spawn;
        chassis.Mass = cfg.ChassisMass;
        chassis.InvInertiaDiag = Geo().InvInertia();   // real GT3-class inertia tensor
        VehicleSolver::Hooks h;
        h.ReadChassis       = [this]{ return chassis.State(); };
        h.ApplyForceAtPoint = [this](const Vec3& f, const Vec3& p){ chassis.ApplyForceAtPoint(f, p); };
        h.ApplyTorque       = [this](const Vec3& t){ chassis.ApplyTorque(t); };
        h.Ground            = Flat();
        controller.Build(cfg, h, chassis.State());
    }
    void Run(const DriverInput& in, float seconds, float dt)
    {
        const int steps = int(seconds / dt);
        controller.AssignInput(in);
        for (int s = 0; s < steps; ++s) { controller.Step(dt); chassis.Integrate(dt); }
    }
};
} // namespace

int main()
{
    const float dt = 1.0f / 240.0f;
    std::printf("\n=== Aerodynamics integration drive checks (aero ON vs OFF) ===\n\n");

    // ---- 1. TOP SPEED: aero drag must lower terminal velocity -----------------------------------------------------
    float vTopAero = 0.0f, vTopNoAero = 0.0f;
    {
        Rig a; a.Build(MakeConfig(true),  {0, 0, 0.40f});
        Rig b; b.Build(MakeConfig(false), {0, 0, 0.40f});
        a.Run({1.0f, 0.0f, 0.0f, false}, 45.0f, dt);
        b.Run({1.0f, 0.0f, 0.0f, false}, 45.0f, dt);
        vTopAero   = a.controller.Telemetry().ForwardSpeed;
        vTopNoAero = b.controller.Telemetry().ForwardSpeed;
        std::printf("  drag @ terminal (aero): %.0f N | downforce total: %.0f N\n",
                    a.controller.Telemetry().Aero.TotalDrag_N, a.controller.Telemetry().Aero.TotalDownforce_N);
        Check("aero car has lower terminal speed than no-aero", vTopAero < vTopNoAero - 2.0f, vTopAero, "<", vTopNoAero);
        Check("aero terminal speed is still substantial (>40 m/s)", vTopAero > 40.0f, vTopAero, ">", 40.0);
    }

    // ---- 2. COAST-DOWN: with throttle released, aero drag decelerates the car faster -----------------------------
    {
        // Short window so neither car crosses zero (engine overrun braking is strong); compare final speed directly.
        Rig a; a.Build(MakeConfig(true),  {0, 0, 0.40f}); a.chassis.LinearVelocity = {55.0f, 0, 0};
        Rig b; b.Build(MakeConfig(false), {0, 0, 0.40f}); b.chassis.LinearVelocity = {55.0f, 0, 0};
        a.Run({0.0f, 0.0f, 0.0f, false}, 2.5f, dt);
        b.Run({0.0f, 0.0f, 0.0f, false}, 2.5f, dt);
        const float vAero   = a.controller.Telemetry().ForwardSpeed;
        const float vNoAero = b.controller.Telemetry().ForwardSpeed;
        Check("aero car is slower after coasting (drag) ", vAero < vNoAero - 1.0f, vAero, "<", vNoAero);
    }

    // ---- 3. DOWNFORCE → cornering grip: sustained lateral accel is higher with aero ------------------------------
    {
        // Flat cornering fixture (roll/pitch locked): drive to speed, hold a hard steer, and measure the SUSTAINED
        // lateral acceleration the tyres can hold in steady state (average over the last second). Downforce lifts Fz,
        // so the grip-limited plateau is higher for the aero car.
        auto SustainedLateralG = [&](bool aero) {
            Rig r; r.Build(MakeConfig(aero), {0, 0, 0.40f});
            r.chassis.PlanarOnly = true;
            r.Run({1.0f, 0.0f, 0.0f, false}, 18.0f, dt);      // reach ~high speed straight
            r.controller.AssignInput({0.6f, 0.0f, 1.0f, false}); // full lock, maintain throttle
            const int steps = int(4.0f / dt);
            const int tail  = int(1.0f / dt);
            double sum = 0.0; int n = 0;
            for (int s = 0; s < steps; ++s)
            {
                r.controller.Step(dt); r.chassis.Integrate(dt);
                if (s >= steps - tail)
                {
                    const auto& t = r.controller.Telemetry();
                    float latForce = 0.0f;
                    for (uint32_t w = 0; w < t.WheelCount; ++w) latForce += t.Wheels[w].LateralForce;
                    sum += std::fabs(latForce) / (r.chassis.Mass * 9.81f);
                    ++n;
                }
            }
            return (n > 0) ? static_cast<float>(sum / n) : 0.0f;
        };
        const float gAero   = SustainedLateralG(true);
        const float gNoAero = SustainedLateralG(false);
        Check("downforce raises sustained cornering grip (lateral g)", gAero > gNoAero + 0.03f, gAero, ">", gNoAero);
        Check("aero cornering grip exceeds 1.0 g", gAero > 1.0f, gAero, ">", 1.0);
    }

    // ---- 4. Downforce is actually reaching the wheels (telemetry sanity) ------------------------------------------
    {
        Rig a; a.Build(MakeConfig(true), {0, 0, 0.40f});
        a.Run({1.0f, 0.0f, 0.0f, false}, 20.0f, dt);
        const auto& t = a.controller.Telemetry();
        float frontDf = 0.0f, rearDf = 0.0f;
        for (uint32_t w = 0; w < t.WheelCount; ++w)
            (t.Wheels[w].HubPosition.x > a.chassis.Position.x ? frontDf : rearDf) += t.Wheels[w].AeroDownforce;
        Check("front wheels receive aero downforce at speed", frontDf > 100.0f, frontDf, ">", 100.0);
        Check("rear wheels receive aero downforce at speed",  rearDf  > 100.0f, rearDf,  ">", 100.0);
        Check("telemetry exposes total downforce",  t.Aero.TotalDownforce_N > 1000.0f, t.Aero.TotalDownforce_N, ">", 1000.0);
    }

    std::printf("\n=== %d passed, %d failed ===\n\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
