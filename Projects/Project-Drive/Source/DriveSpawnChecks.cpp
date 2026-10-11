//============================================================================================================================================
//                                                        DRIVESPAWNCHECKS.CPP
//============================================================================================================================================
// 📦 Calibrated assembly placement: heading, slopes, uneven/missing terrain and reset with the original ground query.

#include "VehicleInstanceSequence.h"
#include "../../../Engine/PhysicalDynamics/Vehicle/VehicleSpawnSolver.h"
#include <cassert>
#include <cmath>
#include <cstdio>

int main()
{
    using namespace Frontier::Vehicle;
    VehicleGeometry Geometry;
    VehicleSolverConfiguration Configuration;
    ApplyGeometry(Configuration, Geometry);
    ChassisState Requested;
    Requested.Position = {4, 3, 8}; Requested.LinearVelocity = {1,2,3};
    const auto CheckClearance = [&](const VehicleSpawnReading& Result, const XPBDSoftTyre::GroundQuery& Ground)
    {
        assert(Result.Supported && Result.GroundContacts == 4u);
        assert(Result.Pose.LinearVelocity.LengthSq() == 0.0f);
        for (uint32_t Wheel = 0; Wheel < 4; ++Wheel)
        {
            const auto& Mount = Configuration.Wheels[Wheel];
            const auto& Strut = Wheel < 2 ? Configuration.FrontStrut : Configuration.RearStrut;
            const float Mirror = Mount.LocalOffset.y < 0 ? -1.0f : 1.0f;
            const Vec3 Local = Mount.SuspensionMount + Vec3{Strut.AxisX, Strut.AxisY * Mirror, Strut.AxisZ} * (Strut.FreeLength - Strut.StaticCompression);
            const Vec3 Hub = Result.Pose.Position + Result.Pose.Orientation.Rotate(Local);
            Vec3 Surface, Normal; assert(Ground(Hub, Surface, Normal)); Normal = Normal.Normalized();
            const float AxleDot = std::abs(Dot(Result.Pose.Orientation.Rotate({0,1,0}), Normal));
            const float Support = Configuration.Tyre.Radius * std::sqrt(std::max(0.0f, 1.0f - AxleDot * AxleDot)) + Configuration.Tyre.Width * .5f * AxleDot;
            assert(Dot(Hub - Surface, Normal) - Support >= .01998f);
        }
    };
    for (float Slope : {0.0f, .2f, -.4f}) for (float Yaw : {0.0f, .7f, 2.1f, 3.14f, 4.3f, 5.9f})
    {
        Requested.Orientation = {0,0,std::sin(Yaw*.5f),std::cos(Yaw*.5f)};
        const Vec3 Up = Vec3{-Slope, -.13f, 1}.Normalized();
        const XPBDSoftTyre::GroundQuery Ground = [=](const Vec3& Point, Vec3& Surface, Vec3& Normal)
        { Surface = {Point.x, Point.y, 2 + Slope*Point.x + .13f*Point.y}; Normal = Up; return true; };
        const auto Result = ResolveVehicleSpawn(Configuration, Requested, Ground);
        CheckClearance(Result, Ground);
        assert(Dot(Result.Pose.Orientation.Rotate({0,0,1}), Up) > .99999f);
        const Vec3 Forward = Requested.Orientation.Rotate({1,0,0});
        assert(Dot(Result.Pose.Orientation.Rotate({1,0,0}), (Forward - Up*Dot(Forward,Up)).Normalized()) > .99999f);
    }
    const XPBDSoftTyre::GroundQuery Uneven = [](const Vec3& Point, Vec3& Surface, Vec3& Normal)
    {
        Surface = {Point.x, Point.y, .1f*std::sin(Point.x)*std::cos(Point.y)};
        Normal = Vec3{-.1f*std::cos(Point.x)*std::cos(Point.y), .1f*std::sin(Point.x)*std::sin(Point.y), 1}.Normalized();
        return true;
    };
    CheckClearance(ResolveVehicleSpawn(Configuration, Requested, Uneven), Uneven);
    assert(!ResolveVehicleSpawn(Configuration, Requested, {}).Supported);
    uint32_t Hits = 0;
    const auto Missing = ResolveVehicleSpawn(Configuration, Requested, [&](const Vec3& Point, Vec3& Surface, Vec3& Normal)
    { Surface = {Point.x, Point.y, 0}; Normal = {0,0,1}; return ++Hits < 4; });
    assert(!Missing.Supported && Missing.GroundContacts == 3);
    assert((Missing.Pose.Position - Requested.Position).LengthSq() == 0);
    const auto Degenerate = ResolveVehicleSpawn(Configuration, Requested, [](const Vec3&, Vec3& Surface, Vec3& Normal)
    { Surface = {}; Normal = {0,0,1}; return true; });
    assert(!Degenerate.Supported);
    Frontier::Drive::VehicleInstanceConfiguration Instance;
    Instance.SpawnLocation = {4,3,0}; Instance.SpawnRotation = Requested.Orientation;
    Frontier::Drive::VehicleInstanceSequence Vehicle;
    uint32_t Queries = 0;
    Vehicle.Construct(Geometry, Instance, [&](const Vec3& Point, Vec3& Surface, Vec3& Normal)
    { ++Queries; return Uneven(Point, Surface, Normal); });
    const auto Spawn = Vehicle.Chassis();
    std::vector<Frontier::InstanceRecord> Rows(5);
    for (uint32_t Tick = 0; Tick < 10; ++Tick) Vehicle.AdvanceVehicle({}, Rows, 1.0f/60);
    Vehicle.ResetToSpawn();
    assert((Vehicle.Chassis().Position - Spawn.Position).LengthSq() < 1e-10f);
    assert((Vehicle.Chassis().Orientation.Rotate({1,0,0}) - Spawn.Orientation.Rotate({1,0,0})).LengthSq() < 1e-10f);
    assert(Vehicle.Chassis().LinearVelocity.LengthSq() == 0);
    Frontier::Drive::VehicleInstanceSequence Unsupported;
    assert(!Unsupported.Construct(Geometry, Instance, [](const Vec3&, Vec3&, Vec3&) { return false; }));
    const auto PreviousQueries = Queries;
    Vehicle.Reconfigure(Vehicle.Configuration()); Vehicle.AdvanceVehicle({}, Rows, 1.0f/60);
    assert(Queries > PreviousQueries);
    std::puts("PASS 18 slope/heading fits, tyre envelopes, uneven terrain, missing/degenerate support, reset orientation and retained ground callback");
}
