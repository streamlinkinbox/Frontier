/*==========================================================================================================================================
                                                              LOBBYSESSION.CPP
==========================================================================================================================================*/

#include "Frontier/Engine/Session/LobbySession.h"

namespace Frontier
{

LobbySession::LobbySession(SessionConfiguration InitialConfiguration, LevelTransition::PolicyFactory InitialPolicyFactory)
    : Configuration(std::move(InitialConfiguration)), PolicyFactory(std::move(InitialPolicyFactory)), Transition(PolicyFactory)
{
    // 📝 The default level is the Lobby: the session opens there, under the policy the configuration names.
    //    Creating the room reads its rules from that policy (capacity five, no respawn, no derez) — nothing
    //    about the lobby is hard-coded here. Projects such as Everest can inject their policy factory.
    Session.ConfigureActiveLevelPath(Configuration.LobbyLevelPath);
    std::unique_ptr<SessionPolicy> LobbyRules = PolicyFactory ? PolicyFactory(Configuration.LobbyPolicyName) : nullptr;
    Session.BeginPolicy(LobbyRules ? std::move(LobbyRules) : MakeSessionPolicy(Configuration.LobbyPolicyName));

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
    const uint64_t PriorEpoch = Session.QueryTransitionEpoch();
    if (Transition.TryTransition(Session) && Session.QueryTransitionEpoch() != PriorEpoch)
        RefreshLocalPlayerToken();
}

bool LobbySession::NavigateToLevel(const std::string& LevelPath, const std::string& PolicyName)
{
    if (!Transition.TryTransitionTo(Session, LevelPath, PolicyName))
        return false;
    RefreshLocalPlayerToken();
    return true;
}

void LobbySession::RefreshLocalPlayerToken()
{
    for (const PlayerProfile& Profile : Session.QueryPlayers())
        if (Profile.IsLocalHost)
        {
            LocalPlayer = Profile.Token;
            return;
        }
    if (!Session.QueryPlayers().empty())
        LocalPlayer = Session.QueryPlayers().front().Token;
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
    const bool Switched = NavigateToLevel(Target.LevelPath, Target.PolicyName);
    DebugPopupOpen = false;
    return Switched;
}
#endif

} // namespace Frontier
