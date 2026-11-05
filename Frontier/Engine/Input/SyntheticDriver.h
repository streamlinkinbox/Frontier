#pragma once
/*==========================================================================================================================================
                                                           SYNTHETICDRIVER.H
==========================================================================================================================================*/
// 🧩 The AI driver: steers its Agent's DeployableUnit along a waypoint circuit and writes the same
//    InputPackets a human's InputDriver writes — throttle, brake, steer, nothing else. Because the contract
//    is identical, a session can hand a car between a player and the AI (backfill, takeover) without the
//    unit, the policy or the replication noticing.

#include "Frontier/Engine/Input/InputPacket.h"
#include "Frontier/Engine/Scene/Agent.h"
#include "Frontier/Engine/Session/PlayerToken.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Frontier
{

struct SyntheticWaypoint
{
    std::array<float, 3> PositionMetres = { 0.0f, 0.0f, 0.0f };
    float TargetSpeedMps = 24.0f;    // [m/s] speed to carry through this point
    float AcceptanceRadiusM = 8.0f;  // [m] close enough: advance to the next point
};

class SyntheticDriver
{
  public:
    SyntheticDriver() = default;
    explicit SyntheticDriver(PlayerToken Owner) : OwnerToken(Owner) {}

    void BindAgent(Agent* Target) noexcept
    {
        BoundAgent = Target;
        if (BoundAgent)
            BoundAgent->AssignDriverKind(AgentDriverKind::SyntheticDriven);
    }
    void UnbindAgent() noexcept { BoundAgent = nullptr; }

    [[nodiscard]] bool HasBoundAgent() const noexcept { return BoundAgent != nullptr; }
    [[nodiscard]] Agent* QueryBoundAgent() const noexcept { return BoundAgent; }
    [[nodiscard]] PlayerToken QueryPlayerToken() const noexcept { return OwnerToken; }
    [[nodiscard]] const InputPacket& QueryLatestPacket() const noexcept { return LatestPacket; }
    [[nodiscard]] size_t QueryCurrentWaypointIndex() const noexcept { return CurrentWaypointIndex; }
    [[nodiscard]] const std::vector<SyntheticWaypoint>& QueryWaypoints() const noexcept { return Waypoints; }

    void ConfigureWaypoints(std::vector<SyntheticWaypoint> NewWaypoints)
    {
        Waypoints = std::move(NewWaypoints);
        CurrentWaypointIndex = 0;
    }
    void ConfigureTargetSpeedMps(float Speed) noexcept { DefaultTargetSpeedMps = Speed; }

    // 📝 Evaluates one step toward the active waypoint and drives the bound Agent. Returns the packet.
    InputPacket StepDrive(float StepSeconds) noexcept;

  private:
    PlayerToken OwnerToken{};
    Agent* BoundAgent = nullptr;
    std::vector<SyntheticWaypoint> Waypoints{};
    size_t CurrentWaypointIndex = 0;
    float DefaultTargetSpeedMps = 22.0f;
    uint64_t StepCounter = 0;
    InputPacket LatestPacket{};
};

} // namespace Frontier
