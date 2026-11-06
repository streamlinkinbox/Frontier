#pragma once
/*==========================================================================================================================================
                                                            SESSIONPOLICY.H
==========================================================================================================================================*/
// 🧩 The rules of a session, as policies rather than procedures: NetPolicy (who is authoritative, who may
//    join), RespawnPolicy (whether a unit comes back, and where — never in the Lobby, at the grid or the last
//    checkpoint elsewhere), SpawnPolicy (how it stands and which brakes it starts with), and the game rules
//    themselves (RacePolicy: laps and checkpoints; EliminationPolicy: the clock and the standings).
//
//    Engine provides the defaults below. A project keeps its own in Projects/<Name>/Source/Session and hands
//    SessionSpace a factory — subclassing or replacing any of these without touching engine code.

#include "Frontier/Engine/Input/InputPacket.h"
#include "Frontier/Engine/Session/PlayerToken.h"
#include "Frontier/Engine/Session/SpawnPolicy.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{

class SessionSpace;

//------------------------------------------------------------------------------------------------------------------------
//                                                        NET POLICY
//------------------------------------------------------------------------------------------------------------------------

enum class NetAuthorityRule : uint8_t { HostAuthority = 0, Standalone = 1, ClientInterpolated = 2 };

struct NetPolicy
{
    NetAuthorityRule AuthorityRule = NetAuthorityRule::HostAuthority;
    bool AllowJoinInProgress = true;
    bool AllowSyntheticBackfill = true;
    float ReplicationHz = 60.0f;
};

// 📝 The active level tells the GLFW/input router what the controls mean. Input is still one InputPacket;
//    policies only whitelist the channels that make sense here.
struct InputRoutingPolicy
{
    InputControlMode Mode = InputControlMode::Driving;
    bool AllowDriving = true;
    bool AllowCameraLook = true;
    bool AllowCameraCycle = true;
    bool AllowLevelNavigation = true;
    bool AllowReady = false;
    bool AllowRespawn = true;
    bool AllowBoost = true;
    bool AllowLights = true;
    std::string_view ControlsHint = "WASD drive; mouse camera; Esc navigation";

    [[nodiscard]] InputPacket Route(const InputPacket& Packet) const noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      RESPAWN POLICY
//------------------------------------------------------------------------------------------------------------------------

enum class RespawnRule : uint8_t
{
    Disabled = 0,       // Lobby: no respawning, ever
    SpawnGrid = 1,      // Proving Grounds: back at the unit's spawn stand
    LastCheckpoint = 2  // Race: back at the last cleared checkpoint
};

struct RespawnPolicy
{
    RespawnRule Rule = RespawnRule::Disabled;
    float RespawnDelaySeconds = 0.0f;
    float OutOfBoundsAltitudeMetres = -15.0f;   // below this, the session offers a respawn

    [[nodiscard]] constexpr bool IsRespawnAllowed() const noexcept { return Rule != RespawnRule::Disabled; }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   SESSION POLICY (DEFAULTS)
//------------------------------------------------------------------------------------------------------------------------

class SessionPolicy
{
  public:
    virtual ~SessionPolicy() = default;

    [[nodiscard]] virtual std::string_view QueryPolicyName() const noexcept = 0;
    [[nodiscard]] const NetPolicy& QueryNetPolicy() const noexcept { return NetRules; }
    [[nodiscard]] const RespawnPolicy& QueryRespawnPolicy() const noexcept { return RespawnRules; }
    [[nodiscard]] const SpawnPolicy& QuerySpawnPolicy() const noexcept { return SpawnRules; }
    [[nodiscard]] virtual const InputRoutingPolicy& QueryInputRoutingPolicy() const noexcept;

    // 📝 Creation reads these from the assigned policy, never from a level script: how many players the
    //    room holds (0 = the session configuration decides) and whether units may be derezzed/rezzed here.
    [[nodiscard]] virtual uint32_t QueryMaximumPlayers() const noexcept { return 0; }
    [[nodiscard]] virtual bool AllowsDerez() const noexcept { return true; }

    virtual void OnSessionBegin(SessionSpace& Session);
    virtual void OnPlayerJoined(SessionSpace& Session, PlayerToken Token);
    virtual void OnPlayerLeft(SessionSpace& Session, PlayerToken Token);
    virtual void OnSessionStep(SessionSpace& Session, float StepSeconds);

    // 📝 Enforces the RespawnPolicy: false where respawning is disabled (the Lobby); otherwise stands the
    //    player's unit back up after the delay — at its spawn stand, or (RacePolicy) its last checkpoint.
    virtual bool RequestAgentRespawn(SessionSpace& Session, PlayerToken Token);

    // 📝 Whether the session should leave for another level now, and if so, where and under which rules.
    [[nodiscard]] virtual bool ShouldTransitionLevel(const SessionSpace& Session, std::string& OutTargetLevelPath,
                                                     std::string& OutTargetPolicyName) const;

  protected:
    NetPolicy NetRules{};
    RespawnPolicy RespawnRules{};
    SpawnPolicy SpawnRules{};
    float RespawnDelayRemainingSeconds = 0.0f;
    PlayerToken PendingRespawnToken{};
};

// 📝 The configurator studio: players join, customise, walk around their cars and ready up. Nobody drives
//    away, nobody respawns; when everyone is ready the session leaves for the match level.
class LobbyPolicy : public SessionPolicy
{
  public:
    LobbyPolicy();
    [[nodiscard]] std::string_view QueryPolicyName() const noexcept override { return "LobbyPolicy"; }

    // 📝 The studio's own limits: five cars, and no derez/rez — a parked fleet is never dissolved.
    [[nodiscard]] uint32_t QueryMaximumPlayers() const noexcept override { return LobbyMaximumPlayers; }
    [[nodiscard]] bool AllowsDerez() const noexcept override { return false; }
    static constexpr uint32_t LobbyMaximumPlayers = 5;
    void OnSessionStep(SessionSpace& Session, float StepSeconds) override;
    [[nodiscard]] bool ShouldTransitionLevel(const SessionSpace& Session, std::string& OutTargetLevelPath,
                                             std::string& OutTargetPolicyName) const override;
    [[nodiscard]] const InputRoutingPolicy& QueryInputRoutingPolicy() const noexcept override;

    void SelectNextMatchPolicy(std::string PolicyName) { NextMatchPolicyName = std::move(PolicyName); }
    void SelectTargetLevelPath(std::string LevelPath) { TargetLevelPath = std::move(LevelPath); }
    [[nodiscard]] float QueryCountdownRemainingSeconds() const noexcept { return CountdownRemainingSeconds; }
    [[nodiscard]] bool IsCountdownActive() const noexcept { return CountdownActive; }

  protected:
    std::string NextMatchPolicyName = "RacePolicy";
    std::string TargetLevelPath = "Content/Levels/ProvingGrounds.levelspace";
    float CountdownDurationSeconds = 3.0f;
    float CountdownRemainingSeconds = 0.0f;
    bool CountdownActive = false;
};

// 📝 Free driving on the proving grounds: surveyed grid stands, live handovers, respawn at your stand.
class ProvingGroundsPolicy : public SessionPolicy
{
  public:
    ProvingGroundsPolicy();
    [[nodiscard]] std::string_view QueryPolicyName() const noexcept override { return "ProvingGroundsPolicy"; }
    [[nodiscard]] const InputRoutingPolicy& QueryInputRoutingPolicy() const noexcept override;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        RACE RULES
//------------------------------------------------------------------------------------------------------------------------

struct RaceCheckpointGate
{
    std::array<float, 3> PositionMetres = { 0.0f, 0.0f, 0.0f };
    float RadiusMetres = 8.5f;
    std::string GateLabel{};
};

struct RacePlayerProgress
{
    PlayerToken Token{};
    uint32_t CompletedLaps = 0;
    uint32_t NextCheckpointIndex = 0;
    double ElapsedSeconds = 0.0;
    bool Finished = false;
    std::array<float, 3> LastClearedPositionMetres = { 0.0f, 0.0f, 0.0f };
    float LastClearedYawDegrees = 0.0f;
};

class RacePolicy : public ProvingGroundsPolicy
{
  public:
    explicit RacePolicy(uint32_t TargetLapCount = 2);
    [[nodiscard]] std::string_view QueryPolicyName() const noexcept override { return "RacePolicy"; }

    void OnSessionBegin(SessionSpace& Session) override;
    void OnPlayerJoined(SessionSpace& Session, PlayerToken Token) override;
    void OnPlayerLeft(SessionSpace& Session, PlayerToken Token) override;
    void OnSessionStep(SessionSpace& Session, float StepSeconds) override;
    bool RequestAgentRespawn(SessionSpace& Session, PlayerToken Token) override;
    [[nodiscard]] bool ShouldTransitionLevel(const SessionSpace& Session, std::string& OutTargetLevelPath,
                                             std::string& OutTargetPolicyName) const override;

    void ConfigureCheckpoints(std::vector<RaceCheckpointGate> Gates) { Checkpoints = std::move(Gates); }
    [[nodiscard]] const std::vector<RaceCheckpointGate>& QueryCheckpoints() const noexcept { return Checkpoints; }
    [[nodiscard]] const std::vector<RacePlayerProgress>& QueryProgress() const noexcept { return Progress; }
    [[nodiscard]] PlayerToken QueryWinnerToken() const noexcept { return WinnerToken; }
    [[nodiscard]] bool IsRaceFinished() const noexcept { return RaceFinished; }
    [[nodiscard]] uint32_t QueryTargetLapCount() const noexcept { return LapTarget; }

  protected:
    uint32_t LapTarget;
    std::vector<RaceCheckpointGate> Checkpoints{};
    std::vector<RacePlayerProgress> Progress{};
    PlayerToken WinnerToken{};
    bool RaceFinished = false;
    float PostFinishSeconds = 0.0f;
    std::string ReturnLobbyLevelPath = "Content/Levels/Lobby.levelspace";
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     ELIMINATION RULES
//------------------------------------------------------------------------------------------------------------------------

struct EliminationStanding
{
    PlayerToken Token{};
    float DistanceMetres = 0.0f;   // odometer at the last reading
    bool Eliminated = false;
};

class EliminationPolicy : public ProvingGroundsPolicy
{
  public:
    explicit EliminationPolicy(float IntervalSeconds = 15.0f);
    [[nodiscard]] std::string_view QueryPolicyName() const noexcept override { return "EliminationPolicy"; }

    void OnSessionBegin(SessionSpace& Session) override;
    void OnPlayerJoined(SessionSpace& Session, PlayerToken Token) override;
    void OnPlayerLeft(SessionSpace& Session, PlayerToken Token) override;
    void OnSessionStep(SessionSpace& Session, float StepSeconds) override;
    [[nodiscard]] bool ShouldTransitionLevel(const SessionSpace& Session, std::string& OutTargetLevelPath,
                                             std::string& OutTargetPolicyName) const override;

    [[nodiscard]] const std::vector<EliminationStanding>& QueryStandings() const noexcept { return Standings; }
    [[nodiscard]] PlayerToken QueryWinnerToken() const noexcept { return WinnerToken; }
    [[nodiscard]] float QueryRoundRemainingSeconds() const noexcept { return RoundRemainingSeconds; }
    [[nodiscard]] uint32_t QueryActiveCount() const noexcept;

  protected:
    float IntervalSeconds;
    float RoundRemainingSeconds;
    std::vector<EliminationStanding> Standings{};
    PlayerToken WinnerToken{};
    bool MatchComplete = false;
    float PostMatchSeconds = 0.0f;
    std::string ReturnLobbyLevelPath = "Content/Levels/Lobby.levelspace";
};

// 📝 Engine-default policy factory, by name. Projects replace this with their own (see Everest).
[[nodiscard]] std::unique_ptr<SessionPolicy> MakeSessionPolicy(std::string_view PolicyName);

} // namespace Frontier
