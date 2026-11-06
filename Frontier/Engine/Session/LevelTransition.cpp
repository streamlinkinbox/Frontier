/*==========================================================================================================================================
                                                           LEVELTRANSITION.CPP
==========================================================================================================================================*/

#include "Frontier/Engine/Session/LevelTransition.h"

#include <utility>

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
    return TryTransitionTo(Session, std::move(TargetLevelPath), std::move(TargetPolicyName));
}

bool LevelTransition::TryTransitionTo(SessionSpace& Session, std::string TargetLevelPath, std::string TargetPolicyName)
{
    if (TargetLevelPath.empty() || TargetPolicyName.empty())
        return false;
    const SessionPolicy* CurrentPolicy = Session.QueryPolicy();
    if (Session.QueryActiveLevelPath() == TargetLevelPath && CurrentPolicy
        && CurrentPolicy->QueryPolicyName() == TargetPolicyName)
        return false;

    std::unique_ptr<SessionPolicy> NewPolicy = Factory ? Factory(TargetPolicyName) : nullptr;
    if (!NewPolicy)
        NewPolicy = MakeSessionPolicy(TargetPolicyName);
    if (!NewPolicy)
        return false;

    // Capture who is coming: profiles are preserved except readiness, which a new level re-earns.
    LastTransfer.SourceLevelPath = Session.QueryActiveLevelPath();
    LastTransfer.TargetLevelPath = std::move(TargetLevelPath);
    LastTransfer.TargetPolicyName = std::move(TargetPolicyName);
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
    Session.ConfigureActiveLevelPath(LastTransfer.TargetLevelPath);
    Session.TransitionEpoch = LastTransfer.TransitionEpoch;

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
