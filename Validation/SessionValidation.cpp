/*==========================================================================================================================================
                                                          SESSIONVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Standalone checks for the session/game-mechanics redesign. No engine, no GLFW, no renderer: the flat
//    and sloped samplers below stand in for the world. Build and run:
//      g++ -std=c++17 -I. Validation/SessionValidation.cpp Frontier/Engine/Input/*.cpp
//          Frontier/Engine/Scene/*.cpp Frontier/Engine/Session/*.cpp Projects/Everest/Source/Session/*.cpp -o /tmp/SessionValidation
//    Exit status is the number of failed checks (0 = all passed).

#include "Frontier/Engine/Input/InputDriver.h"
#include "Frontier/Engine/Input/SyntheticDriver.h"
#include "Frontier/Engine/Session/LevelTransition.h"
#include "Frontier/Engine/Session/SessionSpace.h"
#include "Frontier/Game/LobbyGame.h"
#include "Projects/Everest/Source/Session/SessionPolicy.h"

#include <cmath>
#include <cstdio>

namespace
{

int Failures = 0;
int Checks = 0;

void Check(bool Condition, const char* What)
{
    ++Checks;
    if (!Condition)
    {
        ++Failures;
        std::printf("FAIL: %s\n", What);
    }
}

bool Near(float A, float B, float Tolerance = 0.05f) { return std::fabs(A - B) <= Tolerance; }

// Flat world at z=0.
bool FlatSampler(const std::array<float, 3>&, Frontier::SpawnSurfaceProbe& Out)
{
    Out.ContactPointMetres = { 0.0f, 0.0f, 0.0f };
    Out.HasSurface = true;
    return true;
}

// A bank rising along +Y and a slope rising along +X, to stand a car across both.
bool SlopedSampler(const std::array<float, 3>& Above, Frontier::SpawnSurfaceProbe& Out)
{
    Out.ContactPointMetres = { Above[0], Above[1], 0.10f * Above[0] + 0.20f * Above[1] };
    Out.HasSurface = true;
    return true;
}

} // namespace

int main()
{
    using namespace Frontier;

    // --- InputPacket + InputEncoder ---------------------------------------------------------------
    {
        InputEncoder Encoder;
        RawInputState State{};
        State.KeyW = true;
        InputPacket Packet{};
        for (int Step = 0; Step < 60; ++Step)
            Packet = Encoder.EncodeState(State, 1.0f / 60.0f);
        Check(Packet.ThrottleAxis > 0.99f, "held W ramps throttle to full");
        Check(Packet.SequenceIndex == 60, "packet sequence is monotonic");
        State.KeyW = false; State.KeyD = true;
        Packet = Encoder.EncodeState(State, 1.0f / 60.0f);
        Check(Packet.ThrottleAxis < 1.0f && Packet.SteerAxis > 0.0f, "release decays, steer winds on");
        State.KeyEnter = true;
        Encoder.EncodeState(State, 1.0f / 60.0f);
        Check(Encoder.WasActionPressed(InputActionReady), "Enter raises Ready as a pressed edge");
    }

    // --- Deployment: the same stand regardless of slope --------------------------------------------
    {
        const SpawnPolicy Grid = SpawnPolicy::GridRelease();
        const SpawnPose Flat = ResolveSpawnPose({ 0.0f, 0.0f, 0.0f }, 0.0f, Grid, FlatSampler);
        Check(Flat.Supported && Near(Flat.PositionMetres[2], 0.53f), "flat stand rests at centre height");
        Check(Near(Flat.PitchDegrees, 0.0f) && Near(Flat.RollDegrees, 0.0f), "flat stand is level");

        const SpawnPose Banked = ResolveSpawnPose({ 0.0f, 0.0f, 0.0f }, 0.0f, Grid, SlopedSampler);
        Check(Banked.Supported, "sloped stand is supported");
        Check(Near(Banked.PitchDegrees, 5.71f, 0.4f), "pitch follows the slope (atan 0.10)");
        Check(Near(Banked.RollDegrees, 11.31f, 0.4f), "roll follows the bank (atan 0.20)");

        const SpawnPose Drop = ResolveSpawnPose({ 0.0f, 0.0f, 0.0f }, 0.0f, SpawnPolicy::SuspendedDrop(), FlatSampler);
        Check(Near(Drop.PositionMetres[2], 0.53f + 0.60f), "suspended drop starts at hover height");

        const SpawnPose NoGround = ResolveSpawnPose({ 0.0f, 0.0f, 3.0f }, 0.0f, Grid,
                                                    [](const std::array<float, 3>&, SpawnSurfaceProbe&) { return false; });
        Check(!NoGround.Supported && Near(NoGround.PositionMetres[2], 3.53f), "no ground: authored pose kept, flagged");
    }

    // --- Spawn brake rules --------------------------------------------------------------------------
    {
        auto Disabled = SpawnPolicy::SuspendedDrop();   // brakes disabled, 0.75 s hold
        float Brake = -1.0f, Handbrake = -1.0f;
        Disabled.ResolvePedalDemand(0.30f, 1.0f, 1.0f, Brake, Handbrake);
        Check(Brake == 0.0f && Handbrake == 0.0f, "brakes disabled at spawn ignore the driver during the hold");
        Disabled.ResolvePedalDemand(1.00f, 1.0f, 1.0f, Brake, Handbrake);
        Check(Brake == 1.0f && Handbrake == 1.0f, "after the hold the driver is live");

        DeployableUnit Parked;
        Parked.AssignSpawnPolicy(SpawnPolicy::StudioParked());
        Parked.Deploy({ 0.0f, 0.0f, 0.0f }, 0.0f, FlatSampler);
        Check(Parked.IsHandbrakeLatched(), "studio car starts parked on the handbrake");
        InputPacket Drive{};
        Drive.ThrottleAxis = 1.0f;
        Parked.ApplyInputPacket(Drive, 0.5f);
        Check(!Parked.IsHandbrakeLatched() && Parked.QueryForwardSpeedMps() > 0.0f, "asking for drive releases the parked latch");
    }

    // --- Drivers: a human packet and an AI packet are the same contract -----------------------------
    {
        DeployableUnit Unit;
        Unit.AssignSpawnPolicy(SpawnPolicy::GridRelease());
        Unit.Deploy({ 0.0f, 0.0f, 0.0f }, 0.0f, FlatSampler);
        Agent ControlStand(PlayerToken{ 1 }, &Unit);

        InputDriver Human(PlayerToken{ 1 });
        Human.BindAgent(&ControlStand);
        Check(ControlStand.QueryDriverKind() == AgentDriverKind::InputDriven, "InputDriver marks the Agent human-driven");
        RawInputState State{};
        State.KeyW = true;
        for (int Step = 0; Step < 90; ++Step)
            Human.DriveFromState(State, 1.0f / 60.0f);
        Check(Unit.QueryForwardSpeedMps() > 3.0f, "human throttle moves the unit");
        Check(Unit.QueryLatestInput().ThrottleAxis > 0.9f, "the unit saw the human packet verbatim");

        DeployableUnit AiUnit;
        AiUnit.AssignSpawnPolicy(SpawnPolicy::GridRelease());
        AiUnit.Deploy({ 0.0f, 0.0f, 0.0f }, 0.0f, FlatSampler);
        Agent AiStand(PlayerToken{ 2 }, &AiUnit);
        SyntheticDriver Ai(PlayerToken{ 2 });
        Ai.BindAgent(&AiStand);
        Ai.ConfigureWaypoints({ SyntheticWaypoint{ { 40.0f, 0.0f, 0.0f }, 20.0f, 6.0f },
                                SyntheticWaypoint{ { 40.0f, 40.0f, 0.0f }, 20.0f, 6.0f } });
        for (int Step = 0; Step < 300; ++Step)
            Ai.StepDrive(1.0f / 60.0f);
        Check(AiUnit.QueryPositionMetres()[0] > 30.0f, "synthetic driver reaches the first waypoint leg");
        Check(Ai.QueryCurrentWaypointIndex() == 1, "waypoint advances inside its acceptance radius");
    }

    // --- The Lobby session: join, ready, leave for the match ----------------------------------------
    SessionSpace Lobby(SessionConfiguration{});
    Lobby.ConfigureActiveLevelPath(Lobby.QueryConfiguration().LobbyLevelPath);
    Lobby.AssignSurfaceSampler(FlatSampler);
    Lobby.BeginPolicy(MakeSessionPolicy("LobbyPolicy"));
    {
        const PlayerToken Host = Lobby.AddPlayer("NOVA-7", "GOLD II", "local", true);
        const PlayerToken Guest = Lobby.AddPlayer("KESTREL", "DIAMOND I");
        Lobby.AddSyntheticPlayer("MIRAGE", "SILVER III", {});
        Check(Lobby.QueryConnectedCount() == 3, "players join the studio");
        Check(Near(Lobby.FindUnit(Host)->QueryPositionMetres()[2], 0.53f), "host deploys at the centre stand");
        Check(std::fabs(Lobby.FindUnit(Guest)->QueryPositionMetres()[0]) > 5.0f, "guest deploys on the ring");
        Check(Lobby.FindUnit(Host)->IsHandbrakeLatched(), "lobby cars are parked");

        Check(!Lobby.RequestAgentRespawn(Host), "Lobby rule: no respawning in the lobby");

        LevelTransition Transition;
        Check(!Transition.TryTransition(Lobby), "no transition before everyone is ready");
        Lobby.SetPlayerReady(Host, true);
        Lobby.SetPlayerReady(Guest, true);
        for (const PlayerProfile& Profile : Lobby.QueryPlayers())
            Lobby.SetPlayerReady(Profile.Token, true);
        for (int Step = 0; Step < 240; ++Step)
            Lobby.StepSession(1.0f / 60.0f);   // the 3 s all-ready countdown
        Check(Transition.TryTransition(Lobby), "all ready + countdown leaves for the match level");
        Check(Lobby.QueryActiveLevelPath().find("ProvingGrounds") != std::string::npos, "the session changed level");
        Check(Lobby.QueryPolicy() && Lobby.QueryPolicy()->QueryPolicyName() == "RacePolicy", "the match policy is in force");
        Check(Lobby.QueryPlayers().size() == 3 && Lobby.QueryPlayers()[0].NameTag == "NOVA-7"
                  && Lobby.QueryPlayers()[0].Rank == "GOLD II",
              "profiles (nametag + Rank) survive the transition");
        Check(!Lobby.QueryPlayers()[0].Ready, "readiness is re-earned on the far side");

        const PlayerToken Racer = Lobby.QueryPlayers()[0].Token;
        Check(Lobby.RequestAgentRespawn(Racer), "proving grounds rule: respawn is allowed again");
    }

    // --- Race rules: checkpoints, laps, respawn at the last one --------------------------------------
    {
        SessionSpace Race(SessionConfiguration{});
        Race.AssignSurfaceSampler(FlatSampler);
        auto Rules = std::make_unique<RacePolicy>(1);
        RaceCheckpointGate Gate{};
        Gate.PositionMetres = { 30.0f, 0.0f, 0.0f };
        Gate.RadiusMetres = 6.0f;
        Rules->ConfigureCheckpoints({ Gate });
        Race.BeginPolicy(std::move(Rules));
        const PlayerToken One = Race.AddPlayer("ORION", "BRONZE I");
        Race.FindUnit(One)->Place({ 30.0f, 0.0f, 0.53f }, 0.0f);
        Race.StepSession(1.0f / 60.0f);
        const RacePolicy* Active = static_cast<const RacePolicy*>(Race.QueryPolicy());
        Check(Active->IsRaceFinished() && Active->QueryWinnerToken() == One, "clearing the only gate finishes a one-lap race");
        Race.FindUnit(One)->Place({ -50.0f, -50.0f, 0.53f }, 0.0f);
        Check(Race.RequestAgentRespawn(One), "race respawn answers");
        Check(Near(Race.FindUnit(One)->QueryPositionMetres()[0], 30.0f, 0.5f), "race respawn stands at the last checkpoint");
    }

    // --- Elimination rules: the clock takes the lowest distance --------------------------------------
    {
        SessionSpace Match(SessionConfiguration{});
        Match.AssignSurfaceSampler(FlatSampler);
        Match.BeginPolicy(std::make_unique<EliminationPolicy>(0.10f));
        const PlayerToken Slow = Match.AddPlayer("VEGA-9", "GOLD III");
        const PlayerToken Fast = Match.AddPlayer("KESTREL", "DIAMOND I");
        InputPacket Full{};
        Full.ThrottleAxis = 1.0f;
        Match.SubmitPlayerInput(Fast, Full);
        for (int Step = 0; Step < 30; ++Step)
            Match.StepSession(1.0f / 60.0f);
        const EliminationPolicy* Active = static_cast<const EliminationPolicy*>(Match.QueryPolicy());
        Check(Active->QueryActiveCount() == 1, "the clock eliminates down to one");
        Check(Active->QueryWinnerToken() == Fast, "the driver who covered ground wins");
        (void)Slow;
    }

    // --- Everest: the project's own numbers, no engine edits ------------------------------------------
    {
        auto LobbyRules = Everest::ConstructEverestSessionPolicy("LobbyPolicy");
        Check(LobbyRules && LobbyRules->QueryPolicyName() == "EverestLobbyPolicy", "Everest factory replaces the lobby policy");
        std::string Level, Policy;
        SessionSpace Empty(SessionConfiguration{});
        Check(!LobbyRules->ShouldTransitionLevel(Empty, Level, Policy), "an empty room never transitions");
        auto RaceRules = Everest::ConstructEverestSessionPolicy("RacePolicy");
        const RacePolicy* Race = static_cast<const RacePolicy*>(RaceRules.get());
        Check(Race && Race->QueryTargetLapCount() == 3, "Everest races run three laps");
        Check(Near(Race->QueryRespawnPolicy().RespawnDelaySeconds, 2.0f), "Everest respawn delay is two seconds");
        Check(Everest::ConstructEverestCircuitWaypoints().size() == 6, "Everest circuit has six waypoints");
    }

    // --- The Lobby as the game opens it: policy-read limits, one vehicle, empty tablet ---------------
    {
        LobbyGame Game;
        const SessionSpace& Session = Game.QuerySession();
        Check(Game.QueryCurrentLevelPath().find("Lobby") != std::string::npos, "the game opens on the Lobby level");
        Check(Session.QueryPolicy() && Session.QueryPolicy()->QueryPolicyName() == "LobbyPolicy", "the Lobby policy is assigned");
        Check(Session.QueryPolicy()->QueryMaximumPlayers() == 5, "lobby policy: five players at most");
        Check(!Session.QueryPolicy()->AllowsDerez(), "lobby policy: no derez");
        Check(!Session.QueryPolicy()->QueryRespawnPolicy().IsRespawnAllowed(), "lobby policy: no respawn");
        Check(Game.QueryLocalVehicle() != nullptr, "one vehicle stands in the lobby");
        Check(Game.QueryLocalVehicle()->QueryCabin().Tablet.IsEmpty(), "the tablet is empty (no HMI yet)");
        Check(Game.QueryLocalVehicle()->IsHandbrakeLatched(), "the lobby vehicle is parked");

        for (int Guest = 0; Guest < 4; ++Guest)
            Game.AccessSession().AddPlayer("GUEST", "BRONZE I");
        Check(Session.QueryConnectedCount() == 5, "guests fill the studio to the policy's five");
        Check(!Game.AccessSession().AddPlayer("SIXTH", "BRONZE I").IsValid(), "the sixth join is refused by policy, not by the level");

        // Debug level switch (compiled in for now): Ctrl+Shift+L, then a digit.
        Game.HandleDebugKeys(true, true, true, false, -1);
        Check(Game.IsDebugPopupOpen(), "Ctrl+Shift+L opens the debug level popup");
        Game.HandleDebugKeys(false, false, false, false, 2);
        Check(!Game.IsDebugPopupOpen() && Game.QueryCurrentLevelPath().find("ProvingGrounds") != std::string::npos,
              "the popup switches level and closes");
        Check(Game.QuerySession().QueryPlayers().size() == 5, "players are preserved across the debug switch");
    }

    std::printf("SessionValidation: %d checks, %d failures\n", Checks, Failures);
    return Failures;
}
