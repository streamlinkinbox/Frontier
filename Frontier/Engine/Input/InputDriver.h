#pragma once
/*==========================================================================================================================================
                                                              INPUTDRIVER.H
==========================================================================================================================================*/
// 🧩 The human driver: binds one player to one Agent and feeds it InputPackets encoded from that player's
//    devices (GLFW through the InputEncoder when built with FRONTIER_WITH_GLFW, a RawInputState otherwise),
//    or packets that arrived from elsewhere (a remote peer, a replay). The Agent cannot tell the difference.

#include "Frontier/Engine/Input/InputEncoder.h"
#include "Frontier/Engine/Input/InputPacket.h"
#include "Frontier/Engine/Scene/Agent.h"
#include "Frontier/Engine/Session/PlayerToken.h"

struct GLFWwindow;

namespace Frontier
{

class InputDriver
{
  public:
    InputDriver() = default;
    explicit InputDriver(PlayerToken Owner) : OwnerToken(Owner) {}

    void BindAgent(Agent* Target) noexcept
    {
        BoundAgent = Target;
        if (BoundAgent)
            BoundAgent->AssignDriverKind(AgentDriverKind::InputDriven);
    }
    void UnbindAgent() noexcept { BoundAgent = nullptr; }

    [[nodiscard]] bool HasBoundAgent() const noexcept { return BoundAgent != nullptr; }
    [[nodiscard]] Agent* QueryBoundAgent() const noexcept { return BoundAgent; }
    [[nodiscard]] PlayerToken QueryPlayerToken() const noexcept { return OwnerToken; }
    [[nodiscard]] const InputPacket& QueryLatestPacket() const noexcept { return Encoder.QueryLatestPacket(); }
    [[nodiscard]] InputEncoder& AccessEncoder() noexcept { return Encoder; }

    // 📝 Samples the devices and drives the bound Agent one step. Returns the packet either way, so callers
    //    can replicate or record exactly what was applied.
    InputPacket DriveFromState(const RawInputState& State, float StepSeconds) noexcept;
#if defined(FRONTIER_WITH_GLFW)
    InputPacket DriveFromWindow(GLFWwindow* Window, float StepSeconds,
                                 InputControlMode Mode = InputControlMode::Driving) noexcept;
#endif
    // 📝 Applies an already-encoded packet (remote peer, replay, script).
    void DriveFromPacket(const InputPacket& Packet, float StepSeconds) noexcept;

  private:
    PlayerToken OwnerToken{};
    Agent* BoundAgent = nullptr;
    InputEncoder Encoder{};
};

} // namespace Frontier
