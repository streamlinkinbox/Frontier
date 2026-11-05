#pragma once
/*==========================================================================================================================================
                                                            PLAYERPROFILE.H
==========================================================================================================================================*/
// 🧩 Who a player is inside a session: their nametag and Rank (the same pair the Lobby's number plates
//    carry), whether they are local, synthetic or remote, whether they are ready, and what their car looks
//    like. Profiles are what a level transition preserves; everything else about a player is rebuilt.

#include "Frontier/Engine/Input/InputPacket.h"
#include "Frontier/Engine/Scene/DeployableUnit.h"
#include "Frontier/Engine/Session/PlayerToken.h"

#include <cstdint>
#include <string>

namespace Frontier
{

struct PlayerProfile
{
    PlayerToken Token{};
    std::string NameTag = "Driver";        // [-] shown on the number plate and in the session
    std::string Rank = "BRONZE I";         // [-] tier + division, e.g. "GOLD II" — the plate's badge reads this
    std::string NetworkEndpoint = "local"; // [-] "local", or a remote peer's address label
    bool IsLocalHost = false;
    bool SyntheticAi = false;   // driven by SyntheticDriver (AI) rather than InputDriver (human)
    bool Ready = false;         // lobby: ready to leave for the next level
    bool Connected = true;
    VehicleUnitConfiguration VehicleSpec{};
    InputPacket LatestInput{};  // last packet submitted for this player, replicated or local
    int32_t Score = 0;
};

} // namespace Frontier
