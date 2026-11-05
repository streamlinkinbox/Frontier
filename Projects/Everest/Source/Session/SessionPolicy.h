#pragma once
/*==========================================================================================================================================
                                                  PROJECT EVEREST — SESSIONPOLICY.H
==========================================================================================================================================*/
// 🧩 Everest's own session rules, kept in the project where they belong: the engine supplies the defaults
//    (Frontier/Engine/Session/SessionPolicy.h), Everest overrides the numbers it disagrees with — three laps
//    instead of two, a longer checkpoint respawn delay, a slower elimination clock — and adds its circuit.
//    SessionSpace is handed ConstructEverestSessionPolicy as its factory; no engine file names Everest.

#include "Frontier/Engine/Input/SyntheticDriver.h"
#include "Frontier/Engine/Session/SessionPolicy.h"

#include <memory>
#include <string_view>
#include <vector>

namespace Everest
{

class EverestLobbyPolicy final : public Frontier::LobbyPolicy
{
  public:
    EverestLobbyPolicy();
    [[nodiscard]] std::string_view QueryPolicyName() const noexcept override { return "EverestLobbyPolicy"; }
};

class EverestProvingGroundsPolicy final : public Frontier::ProvingGroundsPolicy
{
  public:
    EverestProvingGroundsPolicy();
    [[nodiscard]] std::string_view QueryPolicyName() const noexcept override { return "EverestProvingGroundsPolicy"; }
};

// 📝 Everest races are three laps, respawn at the last checkpoint after two seconds.
class EverestRacePolicy final : public Frontier::RacePolicy
{
  public:
    explicit EverestRacePolicy(uint32_t TargetLapCount = 3);
    [[nodiscard]] std::string_view QueryPolicyName() const noexcept override { return "EverestRacePolicy"; }
};

// 📝 Everest elimination runs a twenty-second clock instead of fifteen.
class EverestEliminationPolicy final : public Frontier::EliminationPolicy
{
  public:
    explicit EverestEliminationPolicy(float IntervalSeconds = 20.0f);
    [[nodiscard]] std::string_view QueryPolicyName() const noexcept override { return "EverestEliminationPolicy"; }
};

// 📝 The circuit Everest's synthetic drivers run on the proving grounds.
[[nodiscard]] std::vector<Frontier::SyntheticWaypoint> ConstructEverestCircuitWaypoints();

// 📝 Everest's policy factory: understands both the engine names and Everest's own.
[[nodiscard]] std::unique_ptr<Frontier::SessionPolicy> ConstructEverestSessionPolicy(std::string_view PolicyName);

} // namespace Everest
