//============================================================================================================================================
// 📦 Projects/Project-Drive/Source/DriveTelemetry.cpp — headless physics reference: drive the course, write telemetry + timings
//============================================================================================================================================
//
//    Project-Drive's headless CPU reference for the PHYSICS half (the Vulkan app runs the same VehicleSolver live). It
//    drives the REAL ControlVehicle (socket-derived VehicleGeometry, XPBD soft tyres, Pacejka drivetrain, aero) over the
//    shared DriveCourse with NO body collision — only the wheels touch the ground — and writes, into Diagnostics/:
//
//        telemetry.csv   one row per recorded frame: chassis pose + speed + engine + per-wheel load/slip/steer/spin/brake
//        timing.log      wall-clock + per-stage timing (physics step ms, real-time factor)
//        run.log         a human-readable run summary (also echoed to stdout)
//
//    Build (plain g++, no GPU — the sandbox stand-in, mirroring Project-Zero's Makefile discipline):
//        see Projects/Project-Drive/Makefile  (target: DriveTelemetry)

#include "VehicleSolver.h"
#include "VehicleGeometry.h"
#include "DriveCourse.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>

using namespace Frontier::Vehicle;
namespace DC = Frontier::Drive;

//------------------------------------------------------------------------------------------------------------------------ mock rigid chassis (same integrator the suites/FieldDemo use)
struct MockChassis
{
    Vec3  Position{};
    Quat  Orientation{0,0,0,1};
    Vec3  LinearVelocity{}, AngularVelocity{};
    Vec3  ForceAccum{}, TorqueAccum{};
    Vec3  InvInertiaDiag{};
    Vec3  Gravity{0.0f, 0.0f, -9.81f};
    float Mass = 1300.0f, LinearDamping = 0.0f, AngularDamping = 0.05f;

    [[nodiscard]] Quat Conjugate() const noexcept { return {-Orientation.x,-Orientation.y,-Orientation.z,Orientation.w}; }
    [[nodiscard]] Vec3 WorldAngularAccel(const Vec3& tw) const noexcept
    {
        const Vec3 tb = Conjugate().Rotate(tw);
        const Vec3 ab{tb.x*InvInertiaDiag.x, tb.y*InvInertiaDiag.y, tb.z*InvInertiaDiag.z};
        return Orientation.Rotate(ab);
    }
    void ApplyForceAtPoint(const Vec3& f, const Vec3& p) noexcept { ForceAccum += f; TorqueAccum += Cross(p-Position,f); }
    void ApplyTorque(const Vec3& t) noexcept { TorqueAccum += t; }
    void Integrate(float dt) noexcept
    {
        ForceAccum += Gravity*Mass;
        LinearVelocity += ForceAccum*(dt/Mass);
        if (LinearDamping>0) LinearVelocity = LinearVelocity*std::exp(-LinearDamping*dt);
        Position += LinearVelocity*dt;
        AngularVelocity += WorldAngularAccel(TorqueAccum)*dt;
        AngularVelocity = AngularVelocity*std::exp(-AngularDamping*dt);
        const Quat wq{AngularVelocity.x,AngularVelocity.y,AngularVelocity.z,0.0f};
        const Quat dq = QuatMul(wq,Orientation);
        Orientation = QuatNormalize(Quat{Orientation.x+0.5f*dt*dq.x, Orientation.y+0.5f*dt*dq.y,
                                         Orientation.z+0.5f*dt*dq.z, Orientation.w+0.5f*dt*dq.w});
        ForceAccum = Vec3{}; TorqueAccum = Vec3{};
    }
    [[nodiscard]] ChassisState State() const noexcept { return {Position,Orientation,LinearVelocity,AngularVelocity}; }
};

int main(int argc, char** argv)
{
    const char* outDir = (argc > 1) ? argv[1] : "Projects/Project-Drive/Diagnostics";
    const float dt = 1.0f/240.0f;
    const float T  = 12.0f;
    const int   steps  = static_cast<int>(T/dt);
    const int   stride = 4;                                  // record at 60 Hz

    VehicleGeometry g;                                       // real ControlVehicle socket geometry
    VehicleSolverConfiguration cfg; cfg.ActiveScheme = DrivingScheme::PacejkaDrivetrain;
    ApplyGeometry(cfg, g);
    cfg.Aero.Enabled = true;

    MockChassis ch;
    // Spawn so the wheels rest on the pad: hub z 0.0914 below CoM, tyre radius 0.34 → CoM ≈ 0.42 m.
    // Start at the geometry's own resting CoM height. A literal here means every change to tyre radius or CoM
    //    drops the car through the floor on step zero, and the tyre answers an 11 cm penetration with 88 kN.
    ch.Position = {0.0f, 0.0f, g.CoMHeight};
    ch.Mass = cfg.ChassisMass;
    ch.InvInertiaDiag = g.InvInertia();

    VehicleSolver ctl;
    VehicleSolver::Hooks h;
    h.ReadChassis       = [&]{ return ch.State(); };
    h.ApplyForceAtPoint = [&](const Vec3& f, const Vec3& p){ ch.ApplyForceAtPoint(f,p); };
    h.ApplyTorque       = [&](const Vec3& t){ ch.ApplyTorque(t); };
    h.Ground            = [](const Vec3& p, Vec3& s, Vec3& n)
    {
        // The surface query, not a height: CourseSurface answers with the nearest point ON the course and
        //    its outward normal, so a kerb's vertical face can stop a tyre sideways instead of lifting it.
        float Sx, Sy, Sz, Nx, Ny, Nz;
        DC::CourseSurface(p.x, p.y, p.z, Sx, Sy, Sz, Nx, Ny, Nz);
        s = Vec3{Sx, Sy, Sz};
        n = Vec3{Nx, Ny, Nz};
        return true;
    };
    ctl.Build(cfg, h, ch.State());

    // Scripted drive: settle → full throttle → gentle slalom → launch off the ramp → brake after landing.
    auto InputAt = [](float t)->DriverInput
    {
        DriverInput in{};
        if (t < 1.0f) return in;
        in.Throttle = 1.0f;
        if (t >= 2.5f && t < 6.5f) in.Steer = 0.14f*std::sin(2.0f*3.14159265f*(t-2.5f)/2.2f);
        if (t > 9.0f) { in.Throttle = 0.0f; in.Brake = 1.0f; }
        return in;
    };

    // Outputs.
    std::string csvPath = std::string(outDir) + "/telemetry.csv";
    std::string logPath = std::string(outDir) + "/run.log";
    std::string timPath = std::string(outDir) + "/timing.log";
    FILE* csv = std::fopen(csvPath.c_str(), "w");
    if (!csv) { std::fprintf(stderr, "[DriveTelemetry] cannot write %s\n", csvPath.c_str()); return 1; }

    // The aero columns are the solver's OWN AeroForces breakdown (Aerodynamics.h), recorded so the sheets can
    //    plot drag and downforce against speed instead of asserting them.
    std::fprintf(csv, "t,x,y,z,speed_mps,fwd_mps,rpm,gear,boost_bar,throttle,brake,steer,airborne,"
                      "aero_drag_N,aero_downforce_N,aero_front_N,aero_rear_N,aero_side_N,aero_pitch_Nm");
    for (int w = 0; w < 4; ++w)
        std::fprintf(csv, ",w%d_strut_m,w%d_strutrate_mps,w%d_strutforce_N,w%d_arb_N", w, w, w, w);
    for (int w = 0; w < 4; ++w)
        std::fprintf(csv, ",w%d_load_N,w%d_slip,w%d_slipang,w%d_steer,w%d_omega,w%d_contact,w%d_braketemp_K,w%d_abs",
                     w,w,w,w,w,w,w,w);
    std::fprintf(csv, "\n");

    float maxSpeed=0, takeoffSpeed=0, minUp=1, maxComp=0, finalX=0;
    int airFrames=0; bool wasAir=false;

    using Clock = std::chrono::steady_clock;
    double physicsMs = 0.0;
    const auto wall0 = Clock::now();

    for (int s = 0; s <= steps; ++s)
    {
        const float t = s*dt;
        ctl.AssignInput(InputAt(t));

        const auto p0 = Clock::now();
        ctl.Step(dt);
        ch.Integrate(dt);
        physicsMs += std::chrono::duration<double,std::milli>(Clock::now()-p0).count();

        const auto& tl = ctl.Telemetry();
        const float up = ch.Orientation.Rotate({0,0,1}).z;
        minUp = std::min(minUp, up);
        maxSpeed = std::max(maxSpeed, tl.SpeedMetresPerSecond);

        bool airborne = true;
        for (uint32_t wi=0; wi<tl.WheelCount; ++wi) if (tl.Wheels[wi].InContact) airborne = false;
        if (airborne && !wasAir) takeoffSpeed = tl.SpeedMetresPerSecond;
        if (airborne) ++airFrames;
        wasAir = airborne;
        for (uint32_t wi=0; wi<tl.WheelCount; ++wi)
        { const float comp = tl.Wheels[wi].HubPosition.z - tl.Wheels[wi].ContactPoint.z; maxComp = std::max(maxComp, std::fabs(comp)); }

        if (s % stride == 0)
        {
            const DriverInput in = InputAt(t);
            std::fprintf(csv, "%.4f,%.4f,%.4f,%.4f,%.3f,%.3f,%.1f,%d,%.3f,%.3f,%.3f,%.3f,%d",
                         t, ch.Position.x, ch.Position.y, ch.Position.z,
                         tl.SpeedMetresPerSecond, tl.ForwardSpeed, tl.EngineRPM, tl.GearIndex, tl.BoostBar,
                         in.Throttle, in.Brake, in.Steer, airborne?1:0);
            std::fprintf(csv, ",%.2f,%.2f,%.2f,%.2f,%.2f,%.2f",
                         tl.Aero.TotalDrag_N, tl.Aero.TotalDownforce_N, tl.Aero.FrontDownforce_N,
                         tl.Aero.RearDownforce_N, tl.Aero.SideForce_N, tl.Aero.PitchMoment_Nm);
            for (int w=0; w<4; ++w)
            {
                const auto& st = tl.Wheels[w];
                std::fprintf(csv, ",%.5f,%.4f,%.1f,%.1f",
                             st.StrutCompression, st.StrutRate, st.StrutForce, st.AntiRollForce);
            }
            for (int w=0; w<4; ++w)
            {
                const auto& wt = tl.Wheels[w];
                std::fprintf(csv, ",%.1f,%.4f,%.4f,%.4f,%.3f,%d,%.2f,%d",
                             wt.VerticalLoad, wt.SlipRatio, wt.SlipAngleRad, wt.SteerAngleRad,
                             wt.WheelOmega, wt.InContact?1:0, wt.BrakeTemp_K, wt.AbsActive?1:0);
            }
            std::fprintf(csv, "\n");
        }
    }
    std::fclose(csv);
    finalX = ch.Position.x;

    const double wallMs = std::chrono::duration<double,std::milli>(Clock::now()-wall0).count();
    const double simMs  = static_cast<double>(steps+1) * dt * 1000.0;
    const double rtf    = simMs / wallMs;

    // timing.log
    if (FILE* tf = std::fopen(timPath.c_str(), "w"))
    {
        std::fprintf(tf, "# Project-Drive DriveTelemetry timing\n");
        std::fprintf(tf, "steps                 %d\n", steps+1);
        std::fprintf(tf, "fixed_dt_s            %.6f (%.0f Hz)\n", dt, 1.0/dt);
        std::fprintf(tf, "sim_time_s            %.3f\n", simMs/1000.0);
        std::fprintf(tf, "wall_time_ms          %.2f\n", wallMs);
        std::fprintf(tf, "physics_total_ms      %.2f\n", physicsMs);
        std::fprintf(tf, "physics_per_step_us   %.2f\n", physicsMs*1000.0/(steps+1));
        std::fprintf(tf, "realtime_factor       %.1fx\n", rtf);
        std::fclose(tf);
    }

    // run.log + stdout
    char summary[2048];
    std::snprintf(summary, sizeof(summary),
        "Project-Drive — headless physics reference\n"
        "  Vehicle .......... real ControlVehicle (socket geometry, XPBD soft tyres, Pacejka drivetrain, aero ON)\n"
        "  Course ........... checker pad + ramp (%.1f m run, %.2f m crest) + %d speed bumps + %d cones\n"
        "  Sim .............. %.1f s @ %.0f Hz (%d steps), recorded at %.0f Hz\n"
        "  Peak speed ....... %.2f m/s (%.1f km/h)\n"
        "  Takeoff speed .... %.2f m/s (ramp launch)\n"
        "  Airborne ......... %.2f s\n"
        "  Max susp. travel . %.3f m\n"
        "  Min chassis up ... %.3f (1.0 = level; dips while cornering / airborne)\n"
        "  Final X .......... %.1f m\n"
        "  Physics .......... %.2f us/step, %.1fx real-time\n"
        "  Wrote ............ telemetry.csv, timing.log, run.log in %s\n",
        DC::CourseConstants::RampRunX, DC::CourseConstants::RampRise,
        DC::CourseConstants::BumpCount, DC::CourseConstants::ConeCount,
        T, 1.0/dt, steps+1, 1.0/(dt*stride),
        maxSpeed, maxSpeed*3.6f, takeoffSpeed, airFrames*dt, maxComp, minUp, finalX,
        physicsMs*1000.0/(steps+1), rtf, outDir);
    std::fputs(summary, stdout);
    if (FILE* lf = std::fopen(logPath.c_str(), "w")) { std::fputs(summary, lf); std::fclose(lf); }
    return 0;
}
