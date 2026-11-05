#pragma once
/*==========================================================================================================================================
                                                         SESSIONCONFIGURATION.H
==========================================================================================================================================*/
// 🧩 What a session is, before anyone joins: its levels, its policies by name, its capacity. Engine defaults
//    live here; a project (Projects/<Name>/Source/Session) supplies its own values and its own policy
//    factory — the session code never hard-codes a project's names.

#include <cstdint>
#include <string>

namespace Frontier
{

struct SessionConfiguration
{
    std::string SessionTitle = "Car Studio Session";
    std::string LobbyLevelPath = "Content/Levels/Lobby.levelspace";
    std::string MatchLevelPath = "Content/Levels/ProvingGrounds.levelspace";
    std::string LobbyPolicyName = "LobbyPolicy";
    std::string MatchPolicyName = "RacePolicy";
    uint32_t MaximumPlayers = 5;      // the studio holds five cars
    float CountdownSeconds = 3.0f;    // [s] all-ready countdown before a transition fires
    bool AllowSyntheticBackfill = true;
    bool AllowJoinInProgress = false; // lobby rule by default: join the studio, not a running match
};

} // namespace Frontier
