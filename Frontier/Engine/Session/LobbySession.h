#pragma once
/*==========================================================================================================================================
                                                               LOBBYSESSION.H
==========================================================================================================================================*/
// 🧩 The session/game harness opens on the Lobby by default, creates a vehicle for every player, and
//    applies the active level's policy. Project Everest supplies a normal two-way Navigation app through
//    NavigateToLevel; the generic debug popup remains optional and can be compiled out.

#include "Frontier/Engine/Session/LevelTransition.h"
#include "Frontier/Engine/Session/SessionSpace.h"

#include <string>
#include <vector>

namespace Frontier
{

#ifndef FRONTIER_DEBUG_LEVEL_SWITCH
#define FRONTIER_DEBUG_LEVEL_SWITCH 1
#endif

// 📝 What the debug popup can switch to. The Lobby is first because it is the default level.
struct GameLevelEntry
{
    std::string LevelPath;
    std::string PolicyName;
};

class LobbySession
{
  public:
    explicit LobbySession(SessionConfiguration Configuration = {}, LevelTransition::PolicyFactory PolicyFactory = {});

    // 📝 Construction has already opened the Lobby: session begun under the configured lobby policy, local
    //    player joined and deployed, one vehicle standing in the studio.
    [[nodiscard]] const SessionSpace& QuerySession() const noexcept { return Session; }
    [[nodiscard]] SessionSpace& AccessSession() noexcept { return Session; }
    [[nodiscard]] const std::string& QueryCurrentLevelPath() const noexcept { return Session.QueryActiveLevelPath(); }
    [[nodiscard]] PlayerToken QueryLocalPlayer() const noexcept { return LocalPlayer; }
    [[nodiscard]] DeployableUnit* QueryLocalVehicle() noexcept { return Session.FindUnit(LocalPlayer); }

    // 📝 One step of the game: drivers drive, the policy's rules run, and a finished lobby countdown (or a
    //    finished match) performs its level transition through the normal rules.
    void Step(float StepSeconds);

    // 📝 Navigation-app route: rebuild the current room at the selected level with the selected policy,
    //    preserving player profiles and deploying a fresh vehicle for every player.
    bool NavigateToLevel(const std::string& LevelPath, const std::string& PolicyName);

    // 📝 The local player's driving input, as a packet (whatever encoded it — GLFW, a test, a replay).
    void SubmitLocalInput(const InputPacket& Packet) { Session.SubmitPlayerInput(LocalPlayer, Packet); }

#if FRONTIER_DEBUG_LEVEL_SWITCH
    // 📝 DEBUG ONLY: the level-switch popup. Opened with Ctrl+Shift+L; while open, a digit picks a level
    //    and Esc closes it. It switches through the same profile-preserving transition as real play, with
    //    readiness forced (a debug room has nobody to wait for).
    [[nodiscard]] bool IsDebugPopupOpen() const noexcept { return DebugPopupOpen; }
    void HandleDebugKeys(bool Control, bool Shift, bool KeyL, bool Escape, int Digit);
    [[nodiscard]] const std::vector<GameLevelEntry>& QueryDebugLevels() const noexcept { return DebugLevels; }
    bool SwitchLevelForDebug(size_t Index);
#endif

  private:
    SessionConfiguration Configuration{};
    LevelTransition::PolicyFactory PolicyFactory{};
    SessionSpace Session;
    LevelTransition Transition;
    PlayerToken LocalPlayer{};

    void RefreshLocalPlayerToken();

#if FRONTIER_DEBUG_LEVEL_SWITCH
    bool DebugPopupOpen = false;
    bool DebugComboWasDown = false;
    std::vector<GameLevelEntry> DebugLevels{};
#endif
};

} // namespace Frontier
