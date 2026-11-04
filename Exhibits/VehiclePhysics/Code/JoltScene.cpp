//============================================================================================================================
// 🧩 VehiclePhysics exhibit — JoltScene.cpp
//============================================================================================================================
// 📝 Jolt C++ 5.2.0 loose-body world + chassis proxy, the demonstration JoltWorld.js Step() order: pose/velocate the
//    proxy from the vehicle, record prior velocities, step Jolt, apply only the velocity delta back. See JoltScene.h.
//============================================================================================================================

#include "JoltScene.h"

#include <cmath>
#include <cstdio>

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

using namespace Frontier;

namespace
{
    namespace Layers { constexpr JPH::ObjectLayer NonMoving = 0; constexpr JPH::ObjectLayer Moving = 1; }
    namespace BroadPhaseLayers { constexpr JPH::BroadPhaseLayer NonMoving(0); constexpr JPH::BroadPhaseLayer Moving(1); }

    class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
    {
    public:
        JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer Layer) const override
        {
            return Layer == Layers::NonMoving ? BroadPhaseLayers::NonMoving : BroadPhaseLayers::Moving;
        }
    };
    class ObjectVsBPLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter
    {
    public:
        bool ShouldCollide(JPH::ObjectLayer Layer1, JPH::BroadPhaseLayer Layer2) const override
        {
            if (Layer1 == Layers::NonMoving)
                return Layer2 == BroadPhaseLayers::Moving;
            return true;
        }
    };
    class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter
    {
    public:
        bool ShouldCollide(JPH::ObjectLayer Layer1, JPH::ObjectLayer Layer2) const override
        {
            if (Layer1 == Layers::NonMoving)
                return Layer2 == Layers::Moving;
            return true;
        }
    };

    JPH::Vec3 V3(const double V[3]) { return JPH::Vec3((float)V[0], (float)V[1], (float)V[2]); }
    JPH::Quat Q4(const double Q[4]) { return JPH::Quat((float)Q[0], (float)Q[1], (float)Q[2], (float)Q[3]); }
    bool Finite(const double V[3]) { return std::isfinite(V[0]) && std::isfinite(V[1]) && std::isfinite(V[2]); }
}

JoltSceneResult RunJoltScene(const std::string& TraceCsvPath)
{
    JoltSceneResult Result;
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    JPH::TempAllocatorImpl TempAllocator(32 * 1024 * 1024);
    JPH::JobSystemThreadPool JobSystem(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, 1);
    BPLayerInterfaceImpl BPLayers;
    ObjectVsBPLayerFilterImpl ObjectVsBP;
    ObjectLayerPairFilterImpl ObjectPairs;
    JPH::PhysicsSystem Physics;
    Physics.Init(1024, 0, 1024, 1024, BPLayers, ObjectVsBP, ObjectPairs);
    Physics.SetGravity(JPH::Vec3(0.0f, 0.0f, -9.81f));
    JPH::BodyInterface& Bodies = Physics.GetBodyInterface();

    // Statics: ground slab (top at z = 0) and the impact wall (face at x = 59.5).
    {
        JPH::BodyCreationSettings Ground(new JPH::BoxShape(JPH::Vec3(200.0f, 200.0f, 0.5f)), JPH::RVec3(0.0, 0.0, -0.5),
                                         JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::NonMoving);
        Bodies.CreateAndAddBody(Ground, JPH::EActivation::DontActivate);
        JPH::BodyCreationSettings Wall(new JPH::BoxShape(JPH::Vec3(0.5f, 8.0f, 2.0f)), JPH::RVec3(60.0, 0.0, 2.0),
                                       JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::NonMoving);
        Bodies.CreateAndAddBody(Wall, JPH::EActivation::DontActivate);
    }
    // Cones: 3.2 kg cylinders (Jolt's cylinder axis is Y; rotate about X into Z-up), a row the car must clip through.
    struct LooseBody { JPH::BodyID Id; double Start[3]; };
    std::vector<LooseBody> Cones;
    {
        JPH::RefConst<JPH::Shape> ConeShape = new JPH::CylinderShape(0.35f, 0.16f);
        const JPH::Quat Upright = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 0.5f * 3.141592653589793f);
        for (int Index = 0; Index < 10; ++Index)
        {
            const double X = 14.0 + 4.0 * Index;
            const double Y = (Index % 2 == 0) ? 0.85 : -0.85;
            JPH::BodyCreationSettings Settings(ConeShape, JPH::RVec3(X, Y, 0.35), Upright, JPH::EMotionType::Dynamic, Layers::Moving);
            Settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            Settings.mMassPropertiesOverride.mMass = 3.2f;
            Settings.mFriction = 0.6f;
            Settings.mRestitution = 0.25f;
            LooseBody Cone;
            Cone.Id = Bodies.CreateAndAddBody(Settings, JPH::EActivation::Activate);
            Cone.Start[0] = X; Cone.Start[1] = Y; Cone.Start[2] = 0.35;
            Cones.push_back(Cone);
        }
        Result.ConeCount = (int)Cones.size();
    }
    // The car proxy: the demo's fallback hull points as a convex hull, vehicle mass, no gravity, no damping.
    JPH::BodyID CarProxy;
    {
        JPH::Array<JPH::Vec3> Points;
        const float Hull[8][3] = { { -3, -1, -0.1f }, { -3, 1, -0.1f }, { 3, -1, -0.1f }, { 3, 1, -0.1f },
                                   { -2, -1, 1 }, { -2, 1, 1 }, { 2, -1, 1 }, { 2, 1, 1 } };
        for (auto& P : Hull)
            Points.push_back(JPH::Vec3(P[0], P[1], P[2]));
        JPH::ConvexHullShapeSettings HullSettings(Points);
        JPH::Shape::ShapeResult HullResult = HullSettings.Create();
        JPH::BodyCreationSettings Settings(HullResult.Get(), JPH::RVec3(0.0, 0.0, 0.55), JPH::Quat::sIdentity(),
                                           JPH::EMotionType::Dynamic, Layers::Moving);
        Settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        Settings.mMassPropertiesOverride.mMass = 1300.0f;
        Settings.mGravityFactor = 0.0f;
        Settings.mLinearDamping = 0.0f;
        Settings.mAngularDamping = 0.0f;
        Settings.mFriction = 0.4f;
        Settings.mRestitution = 0.1f;
        CarProxy = Bodies.CreateAndAddBody(Settings, JPH::EActivation::Activate);
    }

    std::FILE* Trace = std::fopen(TraceCsvPath.c_str(), "w");
    if (Trace)
        std::fprintf(Trace, "# t,carX,carY,speed,exchange\n");

    VehicleSolver Car;
    bool CountExchange = true;   // F1 counts the cone phase only; the wall phase reports its own impact numbers
    const double Dt = 1.0 / 120.0;
    const auto HeadingOf = [&]()
    {
        const double* Q = Car.Orientation;
        const double Fx = 1.0 - 2.0 * (Q[1] * Q[1] + Q[2] * Q[2]);
        const double Fy = 2.0 * (Q[0] * Q[1] + Q[2] * Q[3]);
        return std::atan2(Fy, Fx);
    };
    // --- Phase 1: settle, then drive the cone row at ~12 m/s with a gentle weave so both rows are clipped ---
    const double Spawn[3] = { 0.0, 0.0, 0.55 };
    Car.Respawn(Spawn, 0.0);
    double Time = 0.0;
    const auto Tick = [&](const VehicleCommand& Command)
    {
        Car.Advance(Command, Dt);
        Bodies.SetPositionAndRotation(CarProxy, V3(Car.Position), Q4(Car.Orientation), JPH::EActivation::Activate);
        Bodies.SetLinearAndAngularVelocity(CarProxy, V3(Car.LinearVelocity), V3(Car.AngularVelocity));
        const double Before[3] = { Car.LinearVelocity[0], Car.LinearVelocity[1], Car.LinearVelocity[2] };
        const double BeforeAng[3] = { Car.AngularVelocity[0], Car.AngularVelocity[1], Car.AngularVelocity[2] };
        Physics.Update((float)Dt, 1, &TempAllocator, &JobSystem);
        const JPH::Vec3 After = Bodies.GetLinearVelocity(CarProxy);
        const JPH::Vec3 AfterAng = Bodies.GetAngularVelocity(CarProxy);
        double Exchange = 0.0;
        const double Delta[3] = { After.GetX() - Before[0], After.GetY() - Before[1], After.GetZ() - Before[2] };
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            Car.LinearVelocity[Axis] += Delta[Axis];
            Exchange += Delta[Axis] * Delta[Axis];
        }
        Car.AngularVelocity[0] += AfterAng.GetX() - BeforeAng[0];
        Car.AngularVelocity[1] += AfterAng.GetY() - BeforeAng[1];
        Car.AngularVelocity[2] += AfterAng.GetZ() - BeforeAng[2];
        Exchange = std::sqrt(Exchange);
        if (CountExchange)
            Result.TotalVelocityExchange += Exchange;
        if (!Finite(Car.Position) || !Finite(Car.LinearVelocity))
            Result.FiniteThroughout = false;
        Time += Dt;
        return Exchange;
    };
    VehicleCommand Idle;
    for (int S = 0; S < 120; ++S)
        Tick(Idle);
    for (int S = 0; S < 120 * 9; ++S)
    {
        const double Speed = std::sqrt(Car.LinearVelocity[0] * Car.LinearVelocity[0] + Car.LinearVelocity[1] * Car.LinearVelocity[1]);
        VehicleCommand Command;
        Command.Throttle = Speed < 12.0 ? 0.85 : 0.25;
        // Closed-loop driver: follow a gentle weave line through the row (cones at y = ±0.85, proxy half-width 1.0,
        // so tracking near y = 0 clips every cone with a hull corner). Open-loop steering just wanders off the row.
        const double TargetY = 0.45 * std::sin(2.0 * 3.141592653589793 * Car.Position[0] / 16.0);
        Command.Steer = std::fmax(-1.0, std::fmin(1.0, -1.1 * HeadingOf() + 0.30 * (TargetY - Car.Position[1])));
        if (Car.Position[0] > 52.0)
            break;
        const double Exchange = Tick(Command);
        if (Trace && S % 4 == 0)
            std::fprintf(Trace, "%.3f,%.3f,%.3f,%.3f,%.4f\n", Time, Car.Position[0], Car.Position[1], Speed, Exchange);
    }
    // Brake to a stop past the row.
    for (int S = 0; S < 120 * 4; ++S)
    {
        VehicleCommand Command;
        Command.Brake = 1.0;
        Tick(Command);
        if (std::fabs(Car.LinearVelocity[0]) < 0.1)
            break;
    }
    for (const LooseBody& Cone : Cones)
    {
        const JPH::RVec3 Position = Bodies.GetPosition(Cone.Id);
        const JPH::Quat Rotation = Bodies.GetRotation(Cone.Id);
        const double Moved = std::hypot(Position.GetX() - Cone.Start[0], Position.GetY() - Cone.Start[1]);
        // Tipped: the cone's local Y (its axis, initially world Z) has fallen away from vertical.
        const JPH::Vec3 Axis = Rotation * JPH::Vec3::sAxisY();
        const bool Tipped = std::fabs(Axis.GetZ()) < 0.7f;
        if (Moved > 0.4 || Tipped)
            ++Result.ConesDisplaced;
        if (Trace)
            std::fprintf(Trace, "# cone %.3f %.3f %.3f %.3f %.3f %.3f %d\n", Cone.Start[0], Cone.Start[1],
                         (double)Position.GetX(), (double)Position.GetY(), (double)Position.GetZ(), Moved, Tipped ? 1 : 0);
    }

    // --- Phase 2: wall impact — respawn west of the wall, accelerate into it ---
    CountExchange = false;
    const double Spawn2[3] = { 38.0, 0.0, 0.55 };
    Car.Respawn(Spawn2, 0.0);
    Bodies.SetPositionAndRotation(CarProxy, V3(Car.Position), Q4(Car.Orientation), JPH::EActivation::Activate);
    Bodies.SetLinearAndAngularVelocity(CarProxy, JPH::Vec3::sZero(), JPH::Vec3::sZero());
    double ExchangeBaseline = Result.TotalVelocityExchange;
    bool ImpactSeen = false;
    double ImpactTime = 0.0;
    for (int S = 0; S < 120 * 10; ++S)
    {
        VehicleCommand Command;
        Command.Throttle = ImpactSeen ? 0.0 : 0.9;
        if (ImpactSeen)
            Command.Brake = 1.0;
        const double SpeedBefore = std::sqrt(Car.LinearVelocity[0] * Car.LinearVelocity[0] + Car.LinearVelocity[1] * Car.LinearVelocity[1]);
        const double Exchange = Tick(Command);
        const double NoseX = Car.Position[0] + 3.0;
        Result.WallPenetration = std::fmax(Result.WallPenetration, NoseX - 59.5);
        if (!ImpactSeen && Exchange > 0.8)
        {
            ImpactSeen = true;
            ImpactTime = Time;
            Result.WallImpactSpeedBefore = SpeedBefore;
        }
        if (ImpactSeen && Time > ImpactTime + 0.5)
        {
            Result.WallSpeedAfter = std::sqrt(Car.LinearVelocity[0] * Car.LinearVelocity[0] + Car.LinearVelocity[1] * Car.LinearVelocity[1]);
            break;
        }
        if (!ImpactSeen && Car.Position[0] > 58.0)
            break;   // reached the wall zone without exchange — recorded as failure by the checks
    }
    (void)ExchangeBaseline;
    if (Trace)
    {
        std::fprintf(Trace, "# wall x=59.5..60.5 car-final %.3f %.3f %.3f\n", Car.Position[0], Car.Position[1], Car.Position[2]);
        std::fclose(Trace);
    }
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
    return Result;
}
