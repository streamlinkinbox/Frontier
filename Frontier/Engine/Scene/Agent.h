#pragma once
/*==========================================================================================================================================
                                                                  AGENT.H
==========================================================================================================================================*/
// 🧩 The control stand between a driver and a DeployableUnit: an Agent belongs to one player, knows whether
//    a human (InputDriver) or an AI (SyntheticDriver) is feeding it, and forwards their InputPackets to the
//    unit it possesses. Swapping who drives a car is rebinding the Agent's driver kind — the unit, the
//    session and the packet contract never change.

#include "Frontier/Engine/Input/InputPacket.h"
#include "Frontier/Engine/Scene/DeployableUnit.h"
#include "Frontier/Engine/Session/PlayerToken.h"

#include <cstdint>

namespace Frontier
{

enum class AgentDriverKind : uint8_t
{
    Unassigned = 0,
    InputDriven = 1,     // a human, through InputDriver
    SyntheticDriven = 2  // an AI, through SyntheticDriver
};

class Agent
{
  public:
    Agent() = default;
    Agent(PlayerToken Owner, DeployableUnit* PossessedUnit) : OwnerToken(Owner), Unit(PossessedUnit) {}

    [[nodiscard]] PlayerToken QueryOwnerPlayerToken() const noexcept { return OwnerToken; }
    [[nodiscard]] AgentDriverKind QueryDriverKind() const noexcept { return DriverKind; }
    [[nodiscard]] DeployableUnit* QueryUnit() const noexcept { return Unit; }
    [[nodiscard]] const InputPacket& QueryLatestInput() const noexcept { return LatestInput; }
    [[nodiscard]] bool HasUnit() const noexcept { return Unit != nullptr; }

    void AssignOwnerPlayerToken(PlayerToken Token) noexcept { OwnerToken = Token; }
    void AssignDriverKind(AgentDriverKind Kind) noexcept { DriverKind = Kind; }
    void Possess(DeployableUnit* NewUnit) noexcept { Unit = NewUnit; }
    void Release() noexcept { Unit = nullptr; }

    // 📝 One packet in, one step of the unit. The packet is kept verbatim: replay, replication and the HMI
    //    all read what the driver actually asked for, before spawn brake rules blended it.
    void ApplyInputPacket(const InputPacket& Packet, float StepSeconds) noexcept
    {
        LatestInput = Packet;
        if (Unit)
            Unit->ApplyInputPacket(Packet, StepSeconds);
    }

  private:
    PlayerToken OwnerToken{};
    AgentDriverKind DriverKind = AgentDriverKind::Unassigned;
    DeployableUnit* Unit = nullptr;
    InputPacket LatestInput{};
};

} // namespace Frontier
