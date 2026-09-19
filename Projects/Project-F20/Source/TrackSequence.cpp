//============================================================================================================================================
// 📦 Project-F20/Source/TrackSequence.cpp — Track Waypoints and Lap Timing Implementation
//============================================================================================================================================

#include "TrackSequence.h"
#include <cmath>

namespace Frontier::ProjectF20 {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

TrackSequence::TrackSequence() noexcept
    : NextWaypointIndex(0)
    , CurrentLap(1)
    , CurrentLapTimeSeconds(0.0f)
    , BestLapTimeSeconds(9999.0f)
{
}

//------------------------------------------------------------------------------------------------------------------------
//                                                WAYPOINT INGESTION
//------------------------------------------------------------------------------------------------------------------------

void TrackSequence::AppendWaypoint(const Frontier::Vector3& Center, float Width) noexcept
{
    TrackWaypoint Point{};
    Point.CenterLocation = Center;
    Point.TrackRadius    = Width;
    Point.WaypointIndex  = static_cast<uint32_t>(Waypoints.size());
    Waypoints.push_back(Point);
}

void TrackSequence::AdvanceTracking(const Frontier::Vector3& VehiclePosition, float Δτ) noexcept
{
    CurrentLapTimeSeconds += Δτ;

    if (Waypoints.empty())
    {
        return;
    }

    const auto& TargetWaypoint = Waypoints[NextWaypointIndex];
    Frontier::Vector3 Delta = VehiclePosition - TargetWaypoint.CenterLocation;
    float DistanceSquared = Delta.LengthSquared();

    if (DistanceSquared <= (TargetWaypoint.TrackRadius * TargetWaypoint.TrackRadius))
    {
        NextWaypointIndex = (NextWaypointIndex + 1) % static_cast<uint32_t>(Waypoints.size());

        // Completed circuit
        if (NextWaypointIndex == 0)
        {
            if (CurrentLapTimeSeconds < BestLapTimeSeconds)
            {
                BestLapTimeSeconds = CurrentLapTimeSeconds;
            }
            CurrentLap++;
            CurrentLapTimeSeconds = 0.0f;
        }
    }
}

} // namespace Frontier::ProjectF20
