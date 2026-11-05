/*==========================================================================================================================================
                                                          SYNTHETICDRIVER.CPP
==========================================================================================================================================*/

#include "Frontier/Engine/Input/SyntheticDriver.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{
constexpr float RadiansPerDegree = 0.017453292519943295f;

// Wraps an angle to [-180, 180).
float WrapDegrees(float Angle) noexcept
{
    while (Angle >= 180.0f) Angle -= 360.0f;
    while (Angle < -180.0f) Angle += 360.0f;
    return Angle;
}
}

InputPacket SyntheticDriver::StepDrive(float StepSeconds) noexcept
{
    InputPacket Packet{};
    Packet.SequenceIndex = ++StepCounter;

    DeployableUnit* Unit = BoundAgent ? BoundAgent->QueryUnit() : nullptr;
    if (Unit && !Waypoints.empty())
    {
        const std::array<float, 3>& Position = Unit->QueryPositionMetres();
        const SyntheticWaypoint& Target = Waypoints[CurrentWaypointIndex];
        const float DeltaX = Target.PositionMetres[0] - Position[0];
        const float DeltaY = Target.PositionMetres[1] - Position[1];
        const float Distance = std::sqrt(DeltaX * DeltaX + DeltaY * DeltaY);

        if (Distance <= Target.AcceptanceRadiusM)
        {
            CurrentWaypointIndex = (CurrentWaypointIndex + 1) % Waypoints.size();
        }
        else
        {
            // Steer at the point: heading error becomes steer demand, clamped like a human's wheel.
            const float DesiredYaw = std::atan2(DeltaY, DeltaX) / RadiansPerDegree;
            const float HeadingError = WrapDegrees(DesiredYaw - Unit->QueryYawDegrees());
            Packet.SteerAxis = std::clamp(HeadingError / 35.0f, -1.0f, 1.0f);

            // Carry the waypoint's speed: throttle below it, brake above it, coast in the band between.
            const float TargetSpeed = Target.TargetSpeedMps > 0.0f ? Target.TargetSpeedMps : DefaultTargetSpeedMps;
            const float Speed = Unit->QueryForwardSpeedMps();
            if (Speed < TargetSpeed - 0.5f)
                Packet.ThrottleAxis = std::clamp((TargetSpeed - Speed) / 6.0f, 0.15f, 1.0f);
            else if (Speed > TargetSpeed + 0.5f)
                Packet.BrakeAxis = std::clamp((Speed - TargetSpeed) / 6.0f, 0.15f, 1.0f);

            // A sharp turn ahead is taken slower: demand fades with heading error.
            if (std::fabs(HeadingError) > 55.0f)
            {
                Packet.ThrottleAxis *= 0.35f;
                if (Speed > 8.0f)
                    Packet.BrakeAxis = std::max(Packet.BrakeAxis, 0.4f);
            }
        }
    }

    LatestPacket = Packet;
    if (BoundAgent)
        BoundAgent->ApplyInputPacket(Packet, StepSeconds);
    return Packet;
}

} // namespace Frontier
