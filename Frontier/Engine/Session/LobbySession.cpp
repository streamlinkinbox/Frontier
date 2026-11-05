/*==========================================================================================================================================
                                                              LOBBYSESSION.CPP
==========================================================================================================================================*/

#include "Frontier/Engine/Session/LobbySession.h"

namespace Frontier
{

LobbySession::LobbySession(SessionConfiguration InitialConfiguration) : Configuration(std::move(InitialConfiguration))
{
    // 📝 The default level is the Lobby: the session opens there, under the policy the configuration names.
    //    Creating the room reads its rules from that policy (capacity five, no respawn, no derez) — nothing
    //    about the lobby is hard-coded here.
    Session.ConfigureActiveLevelPath(Configuration.LobbyLevelPath);
    Session.BeginPolicy(MakeSessionPolicy(Configuration.LobbyPolicyName));

    // One vehicle for now, and its driver: the local player, deployed at the centre stand by the policy's
    // SpawnPolicy (StudioParked under LobbyPolicy: level pad, handbrake latched).
    LocalPlayer = Session.AddPlayer("NOVA-7", "GOLD II", "local", true);

#if FRONTIER_DEBUG_LEVEL_SWITCH
    DebugLevels = {
        { Configuration.LobbyLevelPath, Configuration.LobbyPolicyName },
        { Configuration.MatchLevelPath, "ProvingGroundsPolicy" },
        { Configuration.MatchLevelPath, Configuration.MatchPolicyName },
        { Configuration.MatchLevelPath, "EliminationPolicy" }
    };
#endif
}

void LobbySession::Step(float StepSeconds)
{
    Session.StepSession(StepSeconds);
    Transition.TryTransition(Session);   // the rules decide; mostly this does nothing
}

#if FRONTIER_DEBUG_LEVEL_SWITCH
void LobbySession::HandleDebugKeys(bool Control, bool Shift, bool KeyL, bool Escape, int Digit)
{
    const bool ComboDown = Control && Shift && KeyL;
    if (ComboDown && !DebugComboWasDown)
        DebugPopupOpen = !DebugPopupOpen;
    DebugComboWasDown = ComboDown;
    if (!DebugPopupOpen)
        return;
    if (Escape)
    {
        DebugPopupOpen = false;
        return;
    }
    if (Digit >= 1 && static_cast<size_t>(Digit) <= DebugLevels.size())
        SwitchLevelForDebug(static_cast<size_t>(Digit - 1));
}

bool LobbySession::SwitchLevelForDebug(size_t Index)
{
    if (Index >= DebugLevels.size())
        return false;
    const GameLevelEntry& Target = DebugLevels[Index];

    // Preserve the room's players exactly as a real transition would, then rebuild under the target rules.
    TransferConfiguration Transfer{};
    Transfer.SourceLevelPath = Session.QueryActiveLevelPath();
    Transfer.TargetLevelPath = Target.LevelPath;
    Transfer.TargetPolicyName = Target.PolicyName;
    Transfer.TransitionEpoch = Session.QueryTransitionEpoch() + 1;
    Transfer.PreservedProfiles = Session.QueryPlayers();

    Session.Players.clear();
    Session.Units.clear();
    Session.Agents.clear();
    Session.HumanDrivers.clear();
    Session.SyntheticDrivers.clear();
    Session.ConfigureActiveLevelPath(Target.LevelPath);
    Session.TransitionEpoch = Transfer.TransitionEpoch;
    Session.BeginPolicy(MakeSessionPolicy(Target.PolicyName));
    for (const PlayerProfile& Profile : Transfer.PreservedProfiles)
    {
        const PlayerToken Token = Profile.SyntheticAi
            ? Session.AddSyntheticPlayer(Profile.NameTag, Profile.Rank)
            : Session.AddPlayer(Profile.NameTag, Profile.Rank, Profile.NetworkEndpoint, Profile.IsLocalHost);
        if (PlayerProfile* Restored = Session.FindPlayer(Token))
        {
            Restored->VehicleSpec = Profile.VehicleSpec;
            Restored->Score = Profile.Score;
        }
        if (DeployableUnit* Unit = Session.FindUnit(Token))
            Unit->AssignVehicleConfiguration(Profile.VehicleSpec);
        if (Profile.IsLocalHost)
            LocalPlayer = Token;
    }
    DebugPopupOpen = false;
    return true;
}
#endif

} // namespace Frontier
