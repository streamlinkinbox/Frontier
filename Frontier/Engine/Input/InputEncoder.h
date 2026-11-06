#pragma once
/*==========================================================================================================================================
                                                              INPUTENCODER.H
==========================================================================================================================================*/
// 🧩 Turns raw device state into a smoothed InputPacket: digital keys ramp like real pedals and a real wheel,
//    the stick gets a deadzone, the cursor becomes look deltas. GLFW is the live source when the engine is
//    built with FRONTIER_WITH_GLFW; the encoder itself only ever sees a RawInputState, which is also what
//    headless validation feeds it.

#include "Frontier/Engine/Input/InputPacket.h"

#include <cstdint>

struct GLFWwindow;

namespace Frontier
{

// 📝 One sample of the devices, however it was obtained (GLFW poll, a test, a replay).
struct RawInputState
{
    bool  KeyW = false, KeyS = false, KeyA = false, KeyD = false;
    bool  KeyUp = false, KeyDown = false, KeyLeft = false, KeyRight = false;
    bool  KeySpace = false, KeyShift = false, KeyEnter = false, KeyEscape = false;
    bool  KeyR = false, KeyC = false, KeyL = false;
    bool  GamepadNavigatePrev = false, GamepadNavigateNext = false;
    bool  GamepadConfirm = false, GamepadBack = false, GamepadReady = false;
    bool  GamepadHandbrake = false, GamepadBoost = false, GamepadReset = false;
    bool  GamepadCycleCamera = false, GamepadMenu = false;
    float GamepadSteer = 0.0f;    // [-1..1] left stick X
    float GamepadThrottle = 0.0f; // [0..1]  right trigger
    float GamepadBrake = 0.0f;    // [0..1]  left trigger
    float CursorDeltaX = 0.0f;    // [px] since the previous sample
    float CursorDeltaY = 0.0f;    // [px]
};

struct InputEncoderSettings
{
    float StickDeadzone   = 0.08f;   // [-]
    float SteerRiseRate   = 4.50f;   // [1/s] digital wheel ramp-in
    float SteerReturnRate = 6.50f;   // [1/s] self-centring
    float PedalRiseRate   = 6.00f;   // [1/s]
    float PedalReturnRate = 8.00f;   // [1/s]
    float LookSensitivity = 0.0035f; // [rad/px]
};

class InputEncoder
{
  public:
    InputEncoder() = default;
    explicit InputEncoder(const InputEncoderSettings& InitialSettings) : Settings(InitialSettings) {}

    void Configure(const InputEncoderSettings& UpdatedSettings) noexcept { Settings = UpdatedSettings; }
    [[nodiscard]] const InputEncoderSettings& QuerySettings() const noexcept { return Settings; }

    // 📝 Encodes one raw sample, advancing the ramps by StepSeconds.
    InputPacket EncodeState(const RawInputState& State, float StepSeconds,
                            InputControlMode Mode = InputControlMode::Driving) noexcept;

#if defined(FRONTIER_WITH_GLFW)
    // 📝 Polls a live GLFW window (keyboard + cursor + gamepad 0) and encodes the sample. Only compiled when
    //    the engine is built against GLFW; everything else in this file is device-free.
    InputPacket SampleGlfwWindow(GLFWwindow* Window, float StepSeconds,
                                 InputControlMode Mode = InputControlMode::Driving) noexcept;
#endif

    void Reset() noexcept;

    [[nodiscard]] const InputPacket& QueryLatestPacket() const noexcept { return LatestPacket; }
    [[nodiscard]] const InputPacket& QueryPreviousPacket() const noexcept { return PreviousPacket; }
    [[nodiscard]] bool WasActionPressed(InputActionBit Bit) const noexcept;
    [[nodiscard]] bool WasActionReleased(InputActionBit Bit) const noexcept;

  private:
    static float ApplyDeadzone(float Value, float Deadzone) noexcept;
    static float ApproachAxis(float Current, float Target, float Rate, float StepSeconds) noexcept;

    InputEncoderSettings Settings{};
    InputPacket PreviousPacket{};
    InputPacket LatestPacket{};
    uint64_t SequenceCounter = 0;
    double PriorCursorX = 0.0;   // GLFW cursor bookkeeping (unused without FRONTIER_WITH_GLFW)
    double PriorCursorY = 0.0;
    bool CursorSeeded = false;
};

} // namespace Frontier
