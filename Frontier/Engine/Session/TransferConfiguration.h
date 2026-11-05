#pragma once
/*==========================================================================================================================================
                                                         TRANSFERCONFIGURATION.H
==========================================================================================================================================*/
// 🧩 What survives a level transition: every player's profile (nametag, Rank, readiness reset, car
//    appearance, score) and where the session is going. Units, Agents and drivers are rebuilt on the other
//    side; identities are not.

#include "Frontier/Engine/Session/PlayerProfile.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

struct TransferConfiguration
{
    std::string SourceLevelPath{};
    std::string TargetLevelPath{};
    std::string TargetPolicyName{};
    uint64_t TransitionEpoch = 0;
    std::vector<PlayerProfile> PreservedProfiles{};
};

} // namespace Frontier
