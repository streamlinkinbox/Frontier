/*==========================================================================================================================================
                                              PROJECT DRIVE VISUAL BRIDGE VALIDATION
==========================================================================================================================================*/
#include "Engine/Physics/ProjectDrive/TyreVisualBridge.h"
#include "Engine/Physics/ProjectDrive/VehicleGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace Frontier;
using namespace Frontier::Vehicle;

struct Chassis
{
    Vec3 Position{0,0,1}, Velocity{}, Angular{}, Force{}, Torque{}, InvInertia{};
    Quat Rotation{};
    float Mass = 1200;
    ChassisState State() const { return {Position, Rotation, Velocity, Angular}; }
    void Push(Vec3 F, Vec3 Point) { Force += F; Torque += Cross(Point-Position, F); }
    void Integrate(float Dt)
    {
        Force += Vec3{0,0,-9.81f} * Mass;
        Velocity += Force * (Dt/Mass); Position += Velocity * Dt;
        const Vec3 BodyTorque = Rotation.Conjugate().Rotate(Torque);
        const Vec3 BodyAccel{BodyTorque.x*InvInertia.x, BodyTorque.y*InvInertia.y, BodyTorque.z*InvInertia.z};
        Angular += Rotation.Rotate(BodyAccel) * Dt;
        Angular = Angular * std::exp(-0.05f*Dt);
        const Quat W{Angular.x, Angular.y, Angular.z, 0};
        const Quat D = QuatMul(W, Rotation);
        Rotation = QuatNormalize({Rotation.x+0.5f*Dt*D.x, Rotation.y+0.5f*Dt*D.y,
                                  Rotation.z+0.5f*Dt*D.z, Rotation.w+0.5f*Dt*D.w});
        Force = {}; Torque = {};
    }
};

struct Rig
{
    Chassis Body;
    Frontier::Vehicle::VehicleSolver Solver;
    void Build()
    {
        VehicleGeometry Geometry;
        VehicleSolverConfiguration Config;
        Config.ActiveScheme = DrivingScheme::PacejkaDrivetrain;
        ApplyGeometry(Config, Geometry); Config.Aero.Enabled = false;
        Body.Mass = Config.ChassisMass; Body.InvInertia = Geometry.InvInertia();
        Frontier::Vehicle::VehicleSolver::Hooks Hooks;
        Hooks.ReadChassis = [this] { return Body.State(); };
        Hooks.ApplyForceAtPoint = [this](const Vec3& F, const Vec3& P) { Body.Push(F, P); };
        Hooks.ApplyTorque = [this](const Vec3& T) { Body.Torque += T; };
        Hooks.Ground = [](const Vec3& P, Vec3& Hit, Vec3& Normal)
        { Hit = {P.x,P.y,0}; Normal = {0,0,1}; return true; };
        Solver.Build(Config, Hooks, Body.State());
    }
    void Step(int Index)
    {
        DriverInput Command; Command.Throttle = Index < 120 ? 0.0f : 0.4f;
        Command.Steer = Index < 240 ? 0.0f : 0.1f;
        Solver.AssignInput(Command); Solver.Step(1.0f/240.0f); Body.Integrate(1.0f/240.0f);
    }
};

TyreContinuumInput ManualInput(const WheelTelemetry& Wheel, double Pressure)
{
    TyreContinuumInput Out;
    Out.Centre[0] = Wheel.HubPosition.x; Out.Centre[1] = Wheel.HubPosition.y; Out.Centre[2] = Wheel.HubPosition.z;
    const Quat Material = Wheel.HubRotation * Quat::AxisAngle({0,1,0}, Wheel.SpinAngle);
    Out.Orientation[0] = Material.x; Out.Orientation[1] = Material.y;
    Out.Orientation[2] = Material.z; Out.Orientation[3] = Material.w;
    Out.GroundNormal[0] = Wheel.ContactNormal.x; Out.GroundNormal[1] = Wheel.ContactNormal.y;
    Out.GroundNormal[2] = Wheel.ContactNormal.z;
    Out.Deflection = Wheel.InContact ? std::max(0.0f, Wheel.TyreDeflection) : 0.0;
    Out.LateralForce = Wheel.LateralForce; Out.Pressure = Pressure;
    return Out;
}

int main()
{
    Rig WithBridge, WithoutBridge; WithBridge.Build(); WithoutBridge.Build();
    ProjectDriveTyreVisualBridge Bridge;
    TyreCarcassProjection DirectProjection; TyreCarcassSolid DirectSolid;
    double WorstLocal = 0, WorstWorld = 0, WorstDynamics = 0, Displacement = 0;
    int ContactSamples = 0; bool Finite = true;
    const double Pressure[4] = {220000, 30000, 220000, 30000};
    for (int Step = 0; Step < 480; ++Step)
    {
        WithBridge.Step(Step); WithoutBridge.Step(Step);
        const auto& T = WithBridge.Solver.Telemetry();
        if (T.WheelCount != 4) return 2;
        const auto Input = ManualInput(T.Wheels[0], Pressure[0]);
        if (Step == 0)
        {
            Bridge.Reset(T); DirectProjection.Reset(Input); DirectSolid.Reset(Input);
        }
        const auto Mode = Step < 240 ? DemoTyreVisualMode::Projection : DemoTyreVisualMode::Solid;
        Bridge.Advance(T, Pressure, Mode, 1.0/240.0);
        if (Mode == DemoTyreVisualMode::Projection) DirectProjection.Advance(Input, 1.0/240.0, 2);
        else DirectSolid.Advance(Input, 1.0/240.0, 2);
        const auto& Local = Mode == DemoTyreVisualMode::Projection ? Bridge.Projection(0).QueryLocalPositions()
                                                                  : Bridge.Solid(0).QueryLocalPositions();
        const auto& World = Mode == DemoTyreVisualMode::Projection ? Bridge.Projection(0).QueryWorldPositions()
                                                                  : Bridge.Solid(0).QueryWorldPositions();
        const auto& Rest = Mode == DemoTyreVisualMode::Projection ? Bridge.Projection(0).QueryRestPositions()
                                                                 : Bridge.Solid(0).QueryRestPositions();
        const auto& ExpectedLocal = Mode == DemoTyreVisualMode::Projection ? DirectProjection.QueryLocalPositions()
                                                                          : DirectSolid.QueryLocalPositions();
        const auto& ExpectedWorld = Mode == DemoTyreVisualMode::Projection ? DirectProjection.QueryWorldPositions()
                                                                          : DirectSolid.QueryWorldPositions();
        for (int I = 0; I < TyreContinuumParticleCount*3; ++I)
        {
            WorstLocal = std::max(WorstLocal, std::abs(static_cast<double>(Local[I])-ExpectedLocal[I]));
            WorstWorld = std::max(WorstWorld, std::abs(static_cast<double>(World[I])-ExpectedWorld[I]));
            Displacement = std::max(Displacement, std::abs(static_cast<double>(Local[I])-Rest[I]));
            Finite &= std::isfinite(Local[I]) && std::isfinite(World[I]);
        }
        ContactSamples += T.Wheels[0].InContact;
        const auto A = WithBridge.Body.State(), B = WithoutBridge.Body.State();
        WorstDynamics = std::max({WorstDynamics, static_cast<double>(std::abs(A.Position.x-B.Position.x)),
            static_cast<double>(std::abs(A.Position.y-B.Position.y)), static_cast<double>(std::abs(A.Position.z-B.Position.z)),
            static_cast<double>(std::abs(A.LinearVelocity.x-B.LinearVelocity.x)),
            static_cast<double>(std::abs(A.AngularVelocity.z-B.AngularVelocity.z))});
    }
    const bool Success = Finite && ContactSamples > 100 && Displacement > 0.001 && WorstLocal < 1e-5 &&
                         WorstWorld < 1e-5 && WorstDynamics == 0.0;
    std::printf("%s Project Drive mock-chassis telemetry drives demo shape bridge over 480 steps\n", Success ? "PASS" : "FAIL");
    std::printf("contact steps %d, peak deformation %.9g m; manual-pose errors local %.9g/world %.9g m\n",
                ContactSamples, Displacement, WorstLocal, WorstWorld);
    std::printf("with/without visual bridge max chassis difference %.9g (visual path supplies no forces)\n", WorstDynamics);
    return Success ? 0 : 1;
}
