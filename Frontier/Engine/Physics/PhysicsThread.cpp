/*============================================================================================================================================
                                                            PHYSICTHREAD.CPP
============================================================================================================================================*/
// 🧩 Physics thread mechanics: one Consume cycle = a fixed WorldSliceSeconds slice, advancing every vehicle (its
//    internal 480 Hz substeps), driving the kinematic proxies, substepping the adapter world, exchanging the
//    collision Δv back into each solver; StepAll publishes one whole-frame snapshot per batch.

#include "Engine/Physics/PhysicsThread.h"

#include <cmath>

namespace Frontier
{

namespace
{
    // 📝 The demonstration's fallback hull points (JoltWorld.js): the vehicle proxy when no production hull is assigned.
    constexpr double FallbackHull[][3] = { { -3.0, -1.0, -0.1 }, { -3.0, 1.0, -0.1 }, { 3.0, -1.0, -0.1 }, { 3.0, 1.0, -0.1 },
                                           { -2.0, -1.0, 1.0 },  { -2.0, 1.0, 1.0 },  { 2.0, -1.0, 1.0 },  { 2.0, 1.0, 1.0 } };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

PhysicsThread::~PhysicsThread()
{
    Stop();
}

bool PhysicsThread::Start(PhysicsAdapter& Adapter, const PhysicsThreadConfiguration& InConfiguration)
{
    if (Running.load()) return false;
    WorldAdapter   = &Adapter;
    Configuration  = InConfiguration;
    Pending        = 0.0;
    SimulationTime = 0.0;
    SlicesConsumed = 0;
    {
        std::lock_guard<std::mutex> Guard(Mutex);
        Stopping = false;
    }
    Running.store(true);
    Lane = std::thread(&PhysicsThread::ThreadMain, this);
    return true;
}

void PhysicsThread::Stop() noexcept
{
    if (!Running.load()) return;
    {
        std::lock_guard<std::mutex> Guard(Mutex);
        Stopping = true;
    }
    Wake.notify_all();
    if (Lane.joinable()) Lane.join();
    Running.store(false);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     VEHICLE ATTACHMENT
//------------------------------------------------------------------------------------------------------------------------

int PhysicsThread::AttachVehicle(VehicleSolver& Solver, double MassKilograms)
{
    if (WorldAdapter == nullptr) return -1;
    for (int Slot = 0; Slot < MaxVehicles; ++Slot)
    {
        if (Vehicles[Slot].Solver != nullptr) continue;

        PhysicsBodyDescription Proxy;
        Proxy.Motion           = PhysicsBodyMotion::Kinematic;
        Proxy.GravityShare     = 0.0;
        Proxy.LinearDamping    = 0.0;
        Proxy.AngularDamping   = 0.0;
        Proxy.Friction         = 0.4;
        Proxy.Restitution      = 0.1;
        Proxy.MassKilograms    = MassKilograms;
        for (int Axis = 0; Axis < 3; ++Axis) Proxy.Position[Axis]    = Solver.Position[Axis];
        for (int Axis = 0; Axis < 4; ++Axis) Proxy.Orientation[Axis] = Solver.Orientation[Axis];
        PhysicsColliderDescription Hull;
        Hull.Shape  = PhysicsColliderShape::ConvexHull;
        Hull.Radius = 0.0;
        for (const auto& P : FallbackHull)
        {
            Hull.HullPositions.push_back(P[0]);
            Hull.HullPositions.push_back(P[1]);
            Hull.HullPositions.push_back(P[2]);
        }
        Proxy.Colliders.push_back(Hull);

        Vehicles[Slot].Solver = &Solver;
        Vehicles[Slot].Proxy  = WorldAdapter->CreateBody(Proxy);
        return Slot;
    }
    return -1;
}

void PhysicsThread::ConfigureCommand(int Slot, const VehicleCommand& Command)
{
    if (Slot < 0 || Slot >= MaxVehicles) return;
    std::lock_guard<std::mutex> Guard(Mutex);
    Vehicles[Slot].Command = Command;
}

bool PhysicsThread::TrackBody(PhysicsBodyToken Body)
{
    if (Body == PhysicsBodyInvalid) return false;
    std::lock_guard<std::mutex> Guard(Mutex);
    if (TrackedBodies.size() >= 64) return false;
    TrackedBodies.push_back(Body);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      HOST FEEDING
//------------------------------------------------------------------------------------------------------------------------

void PhysicsThread::EnqueueAdvance(double Seconds)
{
    {
        std::lock_guard<std::mutex> Guard(Mutex);
        Pending += Seconds;
    }
    Wake.notify_all();
}

int PhysicsThread::StepAll(double Seconds)
{
    double Remaining = Seconds;
    int    Slices    = 0;
    while (Remaining > 1.0e-9 && Slices < Configuration.MaxSlicesPerCycle * 4)
    {
        const double Slice = Remaining < Configuration.WorldSliceSeconds ? Remaining : Configuration.WorldSliceSeconds;
        Consume(Slice);
        Remaining -= Slice;
        ++Slices;
    }
    if (Configuration.PublishEveryCycle) PublishSnapshot();
    return Slices;
}

void PhysicsThread::PublishSnapshot()
{
    PhysicsFrameSnapshot Frame;
    Frame.SequenceNumber = ++SequenceNumber;
    Frame.SimulationTime = SimulationTime;
    for (int Slot = 0; Slot < MaxVehicles; ++Slot)
    {
        VehicleSolver* Car = Vehicles[Slot].Solver;
        if (Car == nullptr) continue;
        PhysicsVehicleSnapshot& V = Frame.Vehicles[Slot];
        V.Present = true;
        for (int Axis = 0; Axis < 3; ++Axis) V.Position[Axis]       = Car->Position[Axis];
        for (int Axis = 0; Axis < 4; ++Axis) V.Orientation[Axis]    = Car->Orientation[Axis];
        for (int Axis = 0; Axis < 3; ++Axis) V.LinearVelocity[Axis] = Car->LinearVelocity[Axis];
        V.SpeedMps      = std::sqrt(Car->LinearVelocity[0] * Car->LinearVelocity[0]
                                  + Car->LinearVelocity[1] * Car->LinearVelocity[1]
                                  + Car->LinearVelocity[2] * Car->LinearVelocity[2]);
        V.EngineRPM     = Car->EngineRPM;
        V.GearIndex     = Car->GearIndex;
        V.BoostBar      = Car->Boost;
        V.ChargerRPM    = Car->ChargerRPM;
        V.ParasiticDrag = Car->ParasiticDrag;
        V.Burnout       = Car->Burnout;
        for (int Wheel = 0; Wheel < 4; ++Wheel)
        {
            PhysicsWheelSnapshot& W = V.Wheels[Wheel];
            Car->QueryWheelPose(Wheel, W.Hub, W.Orientation);
            const VehicleWheelState& Reading = Car->Wheels[Wheel];
            W.Spin           = Reading.Spin;
            W.TyreDeflection = Reading.TyreDeflection;
            W.Pressure       = Reading.Pressure;
            W.Grip           = Reading.Grip;
            W.Load           = Reading.Load;
            W.SlipRatio      = Reading.SlipRatio;
            W.SlipAngle      = Reading.SlipAngle;
            W.Contact        = Reading.Contact;
            W.Shredded       = Reading.Shredded;
            W.TreadRemaining = Car->Thermal.QueryTreadFraction(Wheel);
            W.TreadKelvin    = Car->Thermal.Wheels[Wheel].Surface;
            W.CarcassKelvin  = Car->Thermal.Wheels[Wheel].Carcass;
            W.GasKelvin      = Car->Thermal.Wheels[Wheel].Gas;
        }
    }
    std::vector<PhysicsBodyToken> Bodies;
    {
        std::lock_guard<std::mutex> Guard(Mutex);
        Bodies = TrackedBodies;
    }
    Frame.TrackedCount = static_cast<int>(Bodies.size());
    for (std::size_t Index = 0; Index < Bodies.size(); ++Index)
    {
        Frame.TrackedBodies[Index].Token = Bodies[Index];
        WorldAdapter->QueryBodyTransform(Bodies[Index], Frame.TrackedBodies[Index].Position,
                                         Frame.TrackedBodies[Index].Orientation);
    }
    Snapshots.Publish(Frame);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        THREAD LANE
//------------------------------------------------------------------------------------------------------------------------

void PhysicsThread::ThreadMain()
{
    for (;;)
    {
        double Batch = 0.0;
        {
            std::unique_lock<std::mutex> Lock(Mutex);
            Wake.wait(Lock, [&] { return Stopping || Pending > 1.0e-9; });
            if (Stopping) return;
            Batch   = Pending;
            Pending = 0.0;
        }
        StepAll(Batch);
    }
}

int PhysicsThread::Consume(double Seconds)
{
    if (WorldAdapter == nullptr) return 0;

    for (int Slot = 0; Slot < MaxVehicles; ++Slot)
    {
        VehicleSolver* Car = Vehicles[Slot].Solver;
        if (Car == nullptr) continue;

        VehicleCommand Command;
        {
            std::lock_guard<std::mutex> Guard(Mutex);
            Command = Vehicles[Slot].Command;
        }

        Car->Advance(Command, Seconds);

        // 📝 Drive the kinematic proxy from the fresh solver pose before the world step (JoltWorld.js order).
        WorldAdapter->ConfigureBodyTransform(Vehicles[Slot].Proxy, Car->Position, Car->Orientation);
        WorldAdapter->ConfigureBodyVelocities(Vehicles[Slot].Proxy, Car->LinearVelocity, Car->AngularVelocity);
    }

    WorldAdapter->Advance(Seconds);

    for (int Slot = 0; Slot < MaxVehicles; ++Slot)
    {
        VehicleSolver* Car = Vehicles[Slot].Solver;
        if (Car == nullptr) continue;
        double Delta[3] = { 0.0, 0.0, 0.0 };
        WorldAdapter->CollectProxyDeltaVelocity(Vehicles[Slot].Proxy, Delta);
        for (int Axis = 0; Axis < 3; ++Axis) Car->LinearVelocity[Axis] += Delta[Axis];
    }

    SimulationTime += Seconds;
    ++SlicesConsumed;
    return 1;
}

} // namespace Frontier
