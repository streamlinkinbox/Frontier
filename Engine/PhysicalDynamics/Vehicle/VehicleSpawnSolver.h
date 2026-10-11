//============================================================================================================================================
//                                                     VEHICLESPAWNSOLVER.H
//============================================================================================================================================
// 📦 Fits the calibrated suspension and tyre assembly to supporting terrain before physics begins.

#pragma once
#include "VehicleSolver.h"
#include <array>
#include <algorithm>
#include <cmath>

namespace Frontier::Vehicle
{
struct VehicleSpawnReading
{
    ChassisState Pose{};
    uint32_t GroundContacts = 0u;   // [-] - successful support queries
    bool Supported = false;        // [-] - a nondegenerate support plane and four clear tyre envelopes
};

/// 📦 GRIT's equilibrium construction, in metres: support plane, heading projection, rest-length and radius offset.
/// err   insufficient/vertical support retains the requested pose and reports Supported=false
inline VehicleSpawnReading ResolveVehicleSpawn(const VehicleSolverConfiguration& Configuration,
    const ChassisState& Requested, const XPBDSoftTyre::GroundQuery& Ground, float Clearance = 0.02f)
{
    VehicleSpawnReading Result;
    Result.Pose = Requested;
    if (!Ground || Configuration.Wheels.size() != 4u) return Result;
    std::array<Vec3, 4> Contacts{}, Hubs{};
    for (uint32_t Wheel = 0u; Wheel < 4u; ++Wheel)
    {
        const auto& Mount = Configuration.Wheels[Wheel];
        const auto& Strut = Wheel < 2u ? Configuration.FrontStrut : Configuration.RearStrut;
        const float Mirror = Mount.LocalOffset.y < 0.0f ? -1.0f : 1.0f;
        Hubs[Wheel] = Configuration.SuspensionEnabled
            ? Mount.SuspensionMount + Vec3{Strut.AxisX, Strut.AxisY * Mirror, Strut.AxisZ} * (Strut.FreeLength - Strut.StaticCompression)
            : Mount.LocalOffset;
        Vec3 Normal;
        if (Ground(Requested.Position + Requested.Orientation.Rotate(Mount.SuspensionMount), Contacts[Wheel], Normal))
            ++Result.GroundContacts;
    }
    // Unlike the old fallback, never invent contact points for missing terrain.
    if (Result.GroundContacts != 4u) return Result;
    Vec3 Normal = Cross(Contacts[1] - Contacts[0], Contacts[2] - Contacts[0]);
    Normal += Cross(Contacts[2] - Contacts[3], Contacts[1] - Contacts[3]);
    if (Normal.LengthSq() < 1e-10f) return Result;
    Normal = Normal.Normalized();
    if (Normal.z < 0.0f) Normal = Normal * -1.0f;
    if (Normal.z < 0.1f) return Result;
    Vec3 Forward = Requested.Orientation.Rotate({1, 0, 0});
    Forward = (Forward - Normal * Dot(Forward, Normal)).Normalized();
    if (Forward.LengthSq() < 0.5f) return Result;
    const Vec3 Left = Cross(Normal, Forward).Normalized();
    // Convert the orthonormal (forward, left, up) columns without Euler-angle singularities.
    const float Matrix[3][3] = {{Forward.x, Left.x, Normal.x}, {Forward.y, Left.y, Normal.y}, {Forward.z, Left.z, Normal.z}};
    Quat Rotation;
    const float Trace = Matrix[0][0] + Matrix[1][1] + Matrix[2][2];
    if (Trace > 0.0f)
    {
        const float Scale = 2.0f * std::sqrt(Trace + 1.0f);
        Rotation = {(Matrix[2][1]-Matrix[1][2])/Scale, (Matrix[0][2]-Matrix[2][0])/Scale,
                    (Matrix[1][0]-Matrix[0][1])/Scale, 0.25f*Scale};
    }
    else
    {
        uint32_t Axis = Matrix[1][1] > Matrix[0][0] ? 1u : 0u;
        if (Matrix[2][2] > Matrix[Axis][Axis]) Axis = 2u;
        const uint32_t Next = (Axis + 1u) % 3u, Last = (Axis + 2u) % 3u;
        const float Scale = 2.0f * std::sqrt(1.0f + Matrix[Axis][Axis] - Matrix[Next][Next] - Matrix[Last][Last]);
        float Parts[4]{};
        Parts[Axis] = 0.25f * Scale;
        Parts[Next] = (Matrix[Next][Axis] + Matrix[Axis][Next]) / Scale;
        Parts[Last] = (Matrix[Last][Axis] + Matrix[Axis][Last]) / Scale;
        Parts[3] = (Matrix[Last][Next] - Matrix[Next][Last]) / Scale;
        Rotation = {Parts[0], Parts[1], Parts[2], Parts[3]};
    }
    Result.Pose.Orientation = Rotation.Normalized();
    Vec3 Centre{}, HubCentre{};
    for (uint32_t Wheel = 0u; Wheel < 4u; ++Wheel) { Centre += Contacts[Wheel] * .25f; HubCentre += Hubs[Wheel] * .25f; }
    Result.Pose.Position = Centre + Normal * (Configuration.Tyre.Radius + Clearance) - Rotation.Rotate(HubCentre);
    // Re-query rotated hubs. A plane average alone embeds a tyre on non-planar terrain.
    for (uint32_t Pass = 0u; Pass < 8u; ++Pass)
    {
        float Lift = 0.0f;
        for (uint32_t Wheel = 0u; Wheel < 4u; ++Wheel)
        {
            const Vec3 Hub = Result.Pose.Position + Rotation.Rotate(Hubs[Wheel]);
            Vec3 Surface, Up;
            if (!Ground(Hub, Surface, Up)) { Result.Pose = Requested; return Result; }
            Up = Up.Normalized();
            const float Vertical = Dot(Up, Normal);
            if (Vertical <= 0.1f) { Result.Pose = Requested; return Result; }
            const float AxleDot = std::clamp(std::abs(Dot(Rotation.Rotate({0,1,0}), Up)), 0.0f, 1.0f);
            const float Support = Configuration.Tyre.Radius * std::sqrt(1.0f - AxleDot * AxleDot)
                                + Configuration.Tyre.Width * .5f * AxleDot;
            Lift = std::max(Lift, (Support + Clearance - Dot(Hub - Surface, Up)) / Vertical);
        }
        if (Lift <= 1e-5f)
        {
            Result.Pose.LinearVelocity = {}; Result.Pose.AngularVelocity = {};
            Result.Supported = true;
            return Result;
        }
        Result.Pose.Position += Normal * Lift;
    }
    Result.Pose = Requested;
    return Result;
}
}
