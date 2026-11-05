#pragma once
/*==========================================================================================================================================
                                                             SESSIONSPACE.H
==========================================================================================================================================*/
// 🧩 A running session: the players in it, the Agent and DeployableUnit each one possesses, who drives them
//    (InputDriver for humans, SyntheticDriver for AI), and the SessionPolicy whose rules are in force. The
//    Lobby is one of these with a LobbyPolicy; a race is the same room after a level transition, wearing a
//    RacePolicy. Joining, leaving, readying up and respawning are all answered here, under the policy.

#include "Frontier/Engine/Input/InputDriver.h"
#include "Frontier/Engine/Input/SyntheticDriver.h"
#include "Frontier/Engine/Scene/Agent.h"
#include "Frontier/Engine/Scene/DeployableUnit.h"
#include "Frontier/Engine/Session/PlayerProfile.h"
#include "Frontier/Engine/Session/SessionConfiguration.h"
#include "Frontier/Engine/Session/SessionPolicy.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{

class SessionSpace
{
  public:
    SessionSpace() = default;
    explicit SessionSpace(SessionConfiguration InitialConfiguration) : Configuration(std::move(InitialConfiguration)) {}

    [[nodiscard]] const SessionConfiguration& QueryConfiguration() const noexcept { return Configuration; }
    [[nodiscard]] const std::vector<PlayerProfile>& QueryPlayers() const noexcept { return Players; }
    [[nodiscard]] const SessionPolicy* QueryPolicy() const noexcept { return ActivePolicy.get(); }
    [[nodiscard]] const std::string& QueryActiveLevelPath() const noexcept { return ActiveLevelPath; }
    [[nodiscard]] uint64_t QueryTransitionEpoch() const noexcept { return TransitionEpoch; }

    void ConfigureActiveLevelPath(std::string LevelPath) { ActiveLevelPath = std::move(LevelPath); }
    void AssignSurfaceSampler(SpawnSurfaceSampler Sampler) { SurfaceSampler = std::move(Sampler); }
    void BeginPolicy(std::unique_ptr<SessionPolicy> Policy);

    // 📝 Joining and leaving. A join deploys the player's unit at the next studio slot (or grid slot) under
    //    the active policy's SpawnPolicy; synthetic joins also get a SyntheticDriver on a waypoint circuit.
    PlayerToken AddPlayer(std::string_view NameTag, std::string_view Rank = "BRONZE I",
                          std::string_view NetworkEndpoint = "local", bool IsLocalHost = false);
    PlayerToken AddSyntheticPlayer(std::string_view NameTag, std::string_view Rank = "BRONZE I",
                                   const std::vector<SyntheticWaypoint>& Waypoints = {});
    bool RemovePlayer(PlayerToken Token);
    void SetPlayerReady(PlayerToken Token, bool Ready);

    // 📝 Human input arrives as packets (encoded locally or replicated); synthetic players drive themselves.
    void SubmitPlayerInput(PlayerToken Token, const InputPacket& Packet);
    void StepSession(float StepSeconds);

    // 📝 Answered by the active policy: nowhere in the Lobby, at the stand (or checkpoint) elsewhere.
    bool RequestAgentRespawn(PlayerToken Token);

    [[nodiscard]] PlayerProfile* FindPlayer(PlayerToken Token) noexcept;
    [[nodiscard]] const PlayerProfile* FindPlayer(PlayerToken Token) const noexcept;
    [[nodiscard]] Agent* FindAgent(PlayerToken Token) noexcept;
    [[nodiscard]] DeployableUnit* FindUnit(PlayerToken Token) noexcept;
    [[nodiscard]] const DeployableUnit* FindUnit(PlayerToken Token) const noexcept;
    [[nodiscard]] bool AllPlayersReady() const noexcept;
    [[nodiscard]] uint32_t QueryConnectedCount() const noexcept;

    friend class LevelTransition;
    friend class LobbySession;

  private:
    std::array<float, 3> ResolveSlotPosition(uint32_t SlotIndex) const noexcept;
    float ResolveSlotYawDegrees(uint32_t SlotIndex) const noexcept;

    SessionConfiguration Configuration{};
    std::vector<PlayerProfile> Players{};
    std::vector<std::unique_ptr<DeployableUnit>> Units{};
    std::vector<std::unique_ptr<Agent>> Agents{};
    std::vector<std::unique_ptr<InputDriver>> HumanDrivers{};
    std::vector<std::unique_ptr<SyntheticDriver>> SyntheticDrivers{};
    std::unique_ptr<SessionPolicy> ActivePolicy{};
    std::string ActiveLevelPath{};
    SpawnSurfaceSampler SurfaceSampler{};
    uint64_t NextPlayerValue = 1;
    uint64_t TransitionEpoch = 0;
};

} // namespace Frontier
