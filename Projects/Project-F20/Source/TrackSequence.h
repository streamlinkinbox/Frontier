//============================================================================================================================================
// 📦 Project-F20/Source/TrackSequence.h — Track Checkpoint Verification, Waypoints and Lap Timing Sequence
//============================================================================================================================================

#pragma once

#include "../../../DeviceExchange/OrientationClassifier.h"
#include <vector>
#include <string>

namespace Frontier::ProjectF20 {

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRACK WAYPOINT
//------------------------------------------------------------------------------------------------------------------------

struct TrackWaypoint
{
    Frontier::Vector3       CenterLocation;                     // [m] waypoint center coordinate
    float                   TrackRadius;                        // [m] road width radius
    uint32_t                WaypointIndex;                      // [index] sequential checkpoint index
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   TRACK SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class TrackSequence
{
public:
    TrackSequence() noexcept;
    ~TrackSequence() noexcept = default;

    void                    AppendWaypoint(const Frontier::Vector3& Center, float Width) noexcept;
    void                    AdvanceTracking(const Frontier::Vector3& VehiclePosition, float Δτ) noexcept;

    [[nodiscard]] uint32_t  QueryCurrentLap() const noexcept { return CurrentLap; }
    [[nodiscard]] float     QueryCurrentLapTime() const noexcept { return CurrentLapTimeSeconds; }
    [[nodiscard]] float     QueryBestLapTime() const noexcept { return BestLapTimeSeconds; }
    [[nodiscard]] uint32_t  QueryNextWaypointIndex() const noexcept { return NextWaypointIndex; }

    // Single unified conversion operator for current lap
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    std::vector<TrackWaypoint> Waypoints;                       // [waypoints] circuit waypoints
    uint32_t                NextWaypointIndex;                  // [index] target checkpoint
    uint32_t                CurrentLap;                         // [count] current completed laps
    float                   CurrentLapTimeSeconds;              // [s] current lap stopwatch
    float                   BestLapTimeSeconds;                 // [s] best lap record
};

template<>
inline uint32_t TrackSequence::Convert<uint32_t>() const noexcept
{
    return CurrentLap;
}

template<>
inline float TrackSequence::Convert<float>() const noexcept
{
    return CurrentLapTimeSeconds;
}

} // namespace Frontier::ProjectF20
