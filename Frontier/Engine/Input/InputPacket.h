#pragma once
/*==========================================================================================================================================
                                                               INPUTPACKET.H
==========================================================================================================================================*/
// 🧩 One player's normalized control state for a single step. Produced by anything that drives (a human through
//    InputDriver, an AI through SyntheticDriver, a remote peer over the wire) and consumed by Agent /
//    DeployableUnit, which never need to know which of those wrote it. This is the only input contract in the
//    engine — there is no action-map layer; devices are encoded straight into packets (InputEncoder).

#include <cstdint>

namespace Frontier
{

enum InputActionBit : uint32_t
{
    InputActionNone        = 0u,
    InputActionHandbrake   = 1u << 0,
    InputActionReverse     = 1u << 1,
    InputActionBoost       = 1u << 2,
    InputActionReady       = 1u << 3,   // [-] lobby: this player is ready to leave for the next level
    InputActionResetUnit   = 1u << 4,   // [-] ask the session's RespawnPolicy to stand the unit back up
    InputActionCycleCamera = 1u << 5,
    InputActionLights       = 1u << 6,
    InputActionMenu         = 1u << 7,   // [-] open/close the level navigation menu
    InputActionNavigatePrev = 1u << 8,   // [-] previous menu destination
    InputActionNavigateNext = 1u << 9,   // [-] next menu destination
    InputActionConfirm      = 1u << 10,  // [-] select the current menu destination
    InputActionBack         = 1u << 11   // [-] close the menu / cancel a selection
};

enum class InputControlMode : uint8_t
{
    Driving = 0,
    Navigation = 1
};

struct InputPacket
{
    uint64_t SequenceIndex  = 0;      // [-]     monotonic per-driver step counter
    float    ThrottleAxis   = 0.0f;   // [0..1]  forward drive demand
    float    BrakeAxis      = 0.0f;   // [0..1]  service brake demand
    float    SteerAxis      = 0.0f;   // [-1..1] left (-1) to right (+1)
    float    HandbrakeAxis  = 0.0f;   // [0..1]  rear mechanical lock demand
    float    LookYawDelta   = 0.0f;   // [rad]   camera yaw increment this step
    float    LookPitchDelta = 0.0f;   // [rad]   camera pitch increment this step
    uint32_t ActionBits     = 0u;     // [-]     InputActionBit flags

    [[nodiscard]] constexpr bool HasAction(InputActionBit Bit) const noexcept
    {
        return (ActionBits & static_cast<uint32_t>(Bit)) != 0u;
    }
};

} // namespace Frontier
