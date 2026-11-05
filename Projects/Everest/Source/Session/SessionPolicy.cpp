/*==========================================================================================================================================
                                                 PROJECT EVEREST — SESSIONPOLICY.CPP
==========================================================================================================================================*/

#include "Projects/Everest/Source/Session/SessionPolicy.h"

namespace Everest
{

EverestLobbyPolicy::EverestLobbyPolicy()
{
    // Everest's studio leaves for its own race rules by default.
    SelectNextMatchPolicy("EverestRacePolicy");
    SelectTargetLevelPath("Content/Levels/ProvingGrounds.levelspace");
}

EverestProvingGroundsPolicy::EverestProvingGroundsPolicy() = default;

EverestRacePolicy::EverestRacePolicy(uint32_t TargetLapCount) : Frontier::RacePolicy(TargetLapCount)
{
    RespawnRules.RespawnDelaySeconds = 2.0f;   // Everest keeps you off the track a beat longer
}

EverestEliminationPolicy::EverestEliminationPolicy(float IntervalSeconds)
    : Frontier::EliminationPolicy(IntervalSeconds)
{
}

std::vector<Frontier::SyntheticWaypoint> ConstructEverestCircuitWaypoints()
{
    using namespace Frontier;
    return {
        SyntheticWaypoint{ { 60.0f, 0.0f, 0.0f }, 32.0f, 10.0f },
        SyntheticWaypoint{ { 110.0f, 55.0f, 0.0f }, 26.0f, 10.0f },
        SyntheticWaypoint{ { 85.0f, 120.0f, 0.0f }, 22.0f, 10.0f },
        SyntheticWaypoint{ { 10.0f, 130.0f, 0.0f }, 28.0f, 10.0f },
        SyntheticWaypoint{ { -45.0f, 70.0f, 0.0f }, 24.0f, 10.0f },
        SyntheticWaypoint{ { -20.0f, 5.0f, 0.0f }, 30.0f, 10.0f }
    };
}

std::unique_ptr<Frontier::SessionPolicy> ConstructEverestSessionPolicy(std::string_view PolicyName)
{
    if (PolicyName == "EverestLobbyPolicy" || PolicyName == "LobbyPolicy")
        return std::make_unique<EverestLobbyPolicy>();
    if (PolicyName == "EverestProvingGroundsPolicy" || PolicyName == "ProvingGroundsPolicy")
        return std::make_unique<EverestProvingGroundsPolicy>();
    if (PolicyName == "EverestRacePolicy" || PolicyName == "RacePolicy")
        return std::make_unique<EverestRacePolicy>();
    if (PolicyName == "EverestEliminationPolicy" || PolicyName == "EliminationPolicy")
        return std::make_unique<EverestEliminationPolicy>();
    return nullptr;
}

} // namespace Everest
