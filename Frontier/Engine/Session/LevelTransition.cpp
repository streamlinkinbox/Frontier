/*==========================================================================================================================================
                                                           LEVELTRANSITION.CPP
==========================================================================================================================================*/

#include "Frontier/Engine/Session/LevelTransition.h"

namespace Frontier
{

bool LevelTransition::TryTransition(SessionSpace& Session)
{
    const SessionPolicy* Policy = Session.QueryPolicy();
    if (!Policy)
        return false;

    std::string TargetLevelPath, TargetPolicyName;
    if (!Policy->ShouldTransitionLevel(Session, TargetLevelPath, TargetPolicyName))
        return false;   // the rules say stay: not everyone ready, or the match is not over

    // Capture who is coming: profiles preserved verbatim except readiness, which a new level re-earns.
    LastTransfer.SourceLevelPath = Session.QueryActiveLevelPath();
    LastTransfer.TargetLevelPath = TargetLevelPath;
    LastTransfer.TargetPolicyName = TargetPolicyName;
    LastTransfer.TransitionEpoch = Session.QueryTransitionEpoch() + 1;
    LastTransfer.PreservedProfiles = Session.QueryPlayers();
    for (PlayerProfile& Profile : LastTransfer.PreservedProfiles)
        Profile.Ready = false;

    // Rebuild the room on the far side. PlayerTokens are reissued in join order; names, ranks, car
    // appearance and scores carry over, and synthetic players stay synthetic (without their old circuit —
    // the receiving level configures new waypoints).
    const auto Profiles = LastTransfer.PreservedProfiles;
    Session.Players.clear();
    Session.Units.clear();
    Session.Agents.clear();
    Session.HumanDrivers.clear();
    Session.SyntheticDrivers.clear();
    Session.ConfigureActiveLevelPath(TargetLevelPath);
    Session.TransitionEpoch = LastTransfer.TransitionEpoch;

    std::unique_ptr<SessionPolicy> NewPolicy = Factory ? Factory(TargetPolicyName) : MakeSessionPolicy(TargetPolicyName);
    Session.BeginPolicy(std::move(NewPolicy));
    for (const PlayerProfile& Profile : Profiles)
    {
        const PlayerToken Token = Profile.SyntheticAi ? Session.AddSyntheticPlayer(Profile.NameTag, Profile.Rank)
                                                      : Session.AddPlayer(Profile.NameTag, Profile.Rank, Profile.NetworkEndpoint, Profile.IsLocalHost);
        if (PlayerProfile* Restored = Session.FindPlayer(Token))
        {
            Restored->VehicleSpec = Profile.VehicleSpec;
            Restored->Score = Profile.Score;
            Restored->SyntheticAi = Profile.SyntheticAi;
        }
        if (DeployableUnit* Unit = Session.FindUnit(Token))
            Unit->AssignVehicleConfiguration(Profile.VehicleSpec);
    }
    return true;
}

} // namespace Frontier
