/*==========================================================================================================================================
                                                           SESSIONPOLICY.CPP
==========================================================================================================================================*/
// 🧩 Default rules. Note what is NOT here: no procedures, no level scripts. A policy only answers questions
//    (may this unit respawn? where? is it time to leave?) and keeps the small amount of state its rule needs.

#include "Frontier/Engine/Session/SessionPolicy.h"
#include "Frontier/Engine/Session/SessionSpace.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         BASE POLICY
//------------------------------------------------------------------------------------------------------------------------

InputPacket InputRoutingPolicy::Route(const InputPacket& Packet) const noexcept
{
    InputPacket Routed{};
    Routed.SequenceIndex = Packet.SequenceIndex;
    if (AllowDriving)
    {
        Routed.ThrottleAxis = Packet.ThrottleAxis;
        Routed.BrakeAxis = Packet.BrakeAxis;
        Routed.SteerAxis = Packet.SteerAxis;
        Routed.HandbrakeAxis = Packet.HandbrakeAxis;
    }
    if (AllowCameraLook)
    {
        Routed.LookYawDelta = Packet.LookYawDelta;
        Routed.LookPitchDelta = Packet.LookPitchDelta;
    }

    const uint32_t NavigationActions = InputActionMenu | InputActionNavigatePrev | InputActionNavigateNext
                                     | InputActionConfirm | InputActionBack;
    uint32_t Allowed = AllowLevelNavigation ? NavigationActions : 0u;
    if (AllowDriving) Allowed |= InputActionHandbrake | InputActionReverse;
    if (AllowBoost) Allowed |= InputActionBoost;
    if (AllowReady) Allowed |= InputActionReady;
    if (AllowRespawn) Allowed |= InputActionResetUnit;
    if (AllowCameraCycle) Allowed |= InputActionCycleCamera;
    if (AllowLights) Allowed |= InputActionLights;
    Routed.ActionBits = Packet.ActionBits & Allowed;
    return Routed;
}

const InputRoutingPolicy& SessionPolicy::QueryInputRoutingPolicy() const noexcept
{
    static const InputRoutingPolicy Driving{};
    return Driving;
}

void SessionPolicy::OnSessionBegin(SessionSpace&) {}
void SessionPolicy::OnPlayerJoined(SessionSpace&, PlayerToken) {}
void SessionPolicy::OnPlayerLeft(SessionSpace&, PlayerToken) {}
void SessionPolicy::OnSessionStep(SessionSpace&, float) {}

bool SessionPolicy::RequestAgentRespawn(SessionSpace& Session, PlayerToken Token)
{
    if (!RespawnRules.IsRespawnAllowed())
        return false;   // the Lobby answers every respawn request this way
    if (DeployableUnit* Unit = Session.FindUnit(Token))
    {
        Unit->ReturnToSpawnStand();
        return true;
    }
    return false;
}

bool SessionPolicy::ShouldTransitionLevel(const SessionSpace&, std::string&, std::string&) const
{
    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         LOBBY RULES
//------------------------------------------------------------------------------------------------------------------------

LobbyPolicy::LobbyPolicy()
{
    RespawnRules.Rule = RespawnRule::Disabled;              // no respawning in the lobby
    RespawnRules.RespawnDelaySeconds = 0.0f;
    SpawnRules = SpawnPolicy::StudioParked();               // parked on the handbrake, on a level pad
    NetRules.AllowJoinInProgress = true;                    // the studio door is open
}

const InputRoutingPolicy& LobbyPolicy::QueryInputRoutingPolicy() const noexcept
{
    static const InputRoutingPolicy Navigation{
        InputControlMode::Navigation, false, true, false, true, true, false, false, false,
        "Arrows/D-pad choose; Enter selects; Space readies; A selects/readies; Esc/B backs; mouse orbits the parked car"
    };
    return Navigation;
}

void LobbyPolicy::OnSessionStep(SessionSpace& Session, float StepSeconds)
{
    // All ready starts the countdown; anyone un-readying cancels it. Nothing else happens in a lobby.
    if (Session.AllPlayersReady())
    {
        if (!CountdownActive)
        {
            CountdownActive = true;
            CountdownRemainingSeconds = CountdownDurationSeconds;
        }
        else
            CountdownRemainingSeconds = std::max(0.0f, CountdownRemainingSeconds - StepSeconds);
    }
    else
    {
        CountdownActive = false;
        CountdownRemainingSeconds = 0.0f;
    }
}

bool LobbyPolicy::ShouldTransitionLevel(const SessionSpace& Session, std::string& OutTargetLevelPath,
                                        std::string& OutTargetPolicyName) const
{
    if (!CountdownActive || CountdownRemainingSeconds > 0.0f || !Session.AllPlayersReady())
        return false;
    OutTargetLevelPath = TargetLevelPath;
    OutTargetPolicyName = NextMatchPolicyName;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PROVING GROUNDS RULES
//------------------------------------------------------------------------------------------------------------------------

ProvingGroundsPolicy::ProvingGroundsPolicy()
{
    RespawnRules.Rule = RespawnRule::SpawnGrid;             // back at your stand, whenever you ask
    RespawnRules.RespawnDelaySeconds = 0.0f;
    SpawnRules = SpawnPolicy::GridRelease();                // surveyed stand, live handover
    NetRules.AllowJoinInProgress = false;
}

const InputRoutingPolicy& ProvingGroundsPolicy::QueryInputRoutingPolicy() const noexcept
{
    static const InputRoutingPolicy Driving{
        InputControlMode::Driving, true, true, true, true, false, true, true, true,
        "WASD or left stick drive; triggers throttle/brake; Space/X handbrake; Shift/Y boost; mouse camera; C/RB camera; R/Back reset; Esc/Start routes"
    };
    return Driving;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         RACE RULES
//------------------------------------------------------------------------------------------------------------------------

RacePolicy::RacePolicy(uint32_t TargetLapCount) : LapTarget(TargetLapCount == 0 ? 1 : TargetLapCount)
{
    RespawnRules.Rule = RespawnRule::LastCheckpoint;
    RespawnRules.RespawnDelaySeconds = 1.0f;
}

void RacePolicy::OnSessionBegin(SessionSpace& Session)
{
    Progress.clear();
    RaceFinished = false;
    WinnerToken = {};
    PostFinishSeconds = 0.0f;
    for (const PlayerProfile& Profile : Session.QueryPlayers())
        OnPlayerJoined(Session, Profile.Token);
}

void RacePolicy::OnPlayerJoined(SessionSpace&, PlayerToken Token)
{
    if (std::none_of(Progress.begin(), Progress.end(),
                     [Token](const RacePlayerProgress& Entry) { return Entry.Token == Token; }))
        Progress.push_back(RacePlayerProgress{ Token });
}

void RacePolicy::OnPlayerLeft(SessionSpace&, PlayerToken Token)
{
    Progress.erase(std::remove_if(Progress.begin(), Progress.end(),
                                  [Token](const RacePlayerProgress& Entry) { return Entry.Token == Token; }),
                   Progress.end());
}

void RacePolicy::OnSessionStep(SessionSpace& Session, float StepSeconds)
{
    if (Checkpoints.empty())
        return;
    for (RacePlayerProgress& Entry : Progress)
    {
        if (Entry.Finished)
            continue;
        const DeployableUnit* Unit = Session.FindUnit(Entry.Token);
        if (!Unit)
            continue;
        Entry.ElapsedSeconds += StepSeconds;
        const RaceCheckpointGate& Gate = Checkpoints[Entry.NextCheckpointIndex % Checkpoints.size()];
        const std::array<float, 3>& Position = Unit->QueryPositionMetres();
        const float DeltaX = Position[0] - Gate.PositionMetres[0];
        const float DeltaY = Position[1] - Gate.PositionMetres[1];
        if (DeltaX * DeltaX + DeltaY * DeltaY > Gate.RadiusMetres * Gate.RadiusMetres)
            continue;
        Entry.LastClearedPositionMetres = Position;
        Entry.LastClearedYawDegrees = Unit->QueryYawDegrees();
        Entry.NextCheckpointIndex++;
        if (Entry.NextCheckpointIndex >= Checkpoints.size())
        {
            Entry.NextCheckpointIndex = 0;
            Entry.CompletedLaps++;
            if (Entry.CompletedLaps >= LapTarget)
            {
                Entry.Finished = true;
                if (!RaceFinished)
                {
                    RaceFinished = true;
                    WinnerToken = Entry.Token;
                }
            }
        }
    }
    if (RaceFinished)
        PostFinishSeconds += StepSeconds;
}

bool RacePolicy::RequestAgentRespawn(SessionSpace& Session, PlayerToken Token)
{
    if (!RespawnRules.IsRespawnAllowed())
        return false;
    DeployableUnit* Unit = Session.FindUnit(Token);
    if (!Unit)
        return false;
    const auto Entry = std::find_if(Progress.begin(), Progress.end(),
                                    [Token](const RacePlayerProgress& Candidate) { return Candidate.Token == Token; });
    if (Entry != Progress.end() && (Entry->LastClearedPositionMetres != std::array<float, 3>{ 0.0f, 0.0f, 0.0f }))
        Unit->Place(Entry->LastClearedPositionMetres, Entry->LastClearedYawDegrees);   // last cleared checkpoint
    else
        Unit->ReturnToSpawnStand();                                                    // none cleared yet: the grid
    return true;
}

bool RacePolicy::ShouldTransitionLevel(const SessionSpace&, std::string& OutTargetLevelPath,
                                       std::string& OutTargetPolicyName) const
{
    if (!RaceFinished || PostFinishSeconds < 5.0f)
        return false;
    OutTargetLevelPath = ReturnLobbyLevelPath;
    OutTargetPolicyName = "LobbyPolicy";
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ELIMINATION RULES
//------------------------------------------------------------------------------------------------------------------------

EliminationPolicy::EliminationPolicy(float Interval)
    : IntervalSeconds(Interval > 0.0f ? Interval : 15.0f), RoundRemainingSeconds(IntervalSeconds)
{
    RespawnRules.Rule = RespawnRule::SpawnGrid;
}

void EliminationPolicy::OnSessionBegin(SessionSpace& Session)
{
    Standings.clear();
    MatchComplete = false;
    WinnerToken = {};
    PostMatchSeconds = 0.0f;
    RoundRemainingSeconds = IntervalSeconds;
    for (const PlayerProfile& Profile : Session.QueryPlayers())
        OnPlayerJoined(Session, Profile.Token);
}

void EliminationPolicy::OnPlayerJoined(SessionSpace&, PlayerToken Token)
{
    if (std::none_of(Standings.begin(), Standings.end(),
                     [Token](const EliminationStanding& Entry) { return Entry.Token == Token; }))
        Standings.push_back(EliminationStanding{ Token });
}

void EliminationPolicy::OnPlayerLeft(SessionSpace&, PlayerToken Token)
{
    Standings.erase(std::remove_if(Standings.begin(), Standings.end(),
                                   [Token](const EliminationStanding& Entry) { return Entry.Token == Token; }),
                    Standings.end());
}

uint32_t EliminationPolicy::QueryActiveCount() const noexcept
{
    return static_cast<uint32_t>(std::count_if(Standings.begin(), Standings.end(),
                                               [](const EliminationStanding& Entry) { return !Entry.Eliminated; }));
}

void EliminationPolicy::OnSessionStep(SessionSpace& Session, float StepSeconds)
{
    for (EliminationStanding& Entry : Standings)
        if (const DeployableUnit* Unit = Session.FindUnit(Entry.Token))
            Entry.DistanceMetres = Unit->QueryOdometerMetres();

    if (MatchComplete)
    {
        PostMatchSeconds += StepSeconds;
        return;
    }
    if (QueryActiveCount() <= 1)
    {
        MatchComplete = true;
        const auto Winner = std::find_if(Standings.begin(), Standings.end(),
                                         [](const EliminationStanding& Entry) { return !Entry.Eliminated; });
        if (Winner != Standings.end())
            WinnerToken = Winner->Token;
        return;
    }
    RoundRemainingSeconds -= StepSeconds;
    if (RoundRemainingSeconds > 0.0f)
        return;
    RoundRemainingSeconds = IntervalSeconds;

    // The clock eliminates whoever has covered the least ground this match.
    EliminationStanding* Lowest = nullptr;
    for (EliminationStanding& Entry : Standings)
        if (!Entry.Eliminated && (!Lowest || Entry.DistanceMetres < Lowest->DistanceMetres))
            Lowest = &Entry;
    if (Lowest)
        Lowest->Eliminated = true;
}

bool EliminationPolicy::ShouldTransitionLevel(const SessionSpace&, std::string& OutTargetLevelPath,
                                              std::string& OutTargetPolicyName) const
{
    if (!MatchComplete || PostMatchSeconds < 5.0f)
        return false;
    OutTargetLevelPath = ReturnLobbyLevelPath;
    OutTargetPolicyName = "LobbyPolicy";
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          FACTORY
//------------------------------------------------------------------------------------------------------------------------

std::unique_ptr<SessionPolicy> MakeSessionPolicy(std::string_view PolicyName)
{
    if (PolicyName == "LobbyPolicy")
        return std::make_unique<LobbyPolicy>();
    if (PolicyName == "ProvingGroundsPolicy")
        return std::make_unique<ProvingGroundsPolicy>();
    if (PolicyName == "RacePolicy")
        return std::make_unique<RacePolicy>();
    if (PolicyName == "EliminationPolicy")
        return std::make_unique<EliminationPolicy>();
    return nullptr;
}

} // namespace Frontier
