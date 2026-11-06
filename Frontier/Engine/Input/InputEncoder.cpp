/*==========================================================================================================================================
                                                             INPUTENCODER.CPP
==========================================================================================================================================*/

#include "Frontier/Engine/Input/InputEncoder.h"

#include <algorithm>
#include <cmath>

#if defined(FRONTIER_WITH_GLFW)
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#endif

namespace Frontier
{

float InputEncoder::ApplyDeadzone(float Value, float Deadzone) noexcept
{
    const float Magnitude = std::fabs(Value);
    if (Magnitude <= Deadzone)
        return 0.0f;
    return std::copysign((Magnitude - Deadzone) / (1.0f - Deadzone), Value);
}

float InputEncoder::ApproachAxis(float Current, float Target, float Rate, float StepSeconds) noexcept
{
    const float Step = std::max(Rate, 0.0f) * std::max(StepSeconds, 0.0f);
    if (Current < Target)
        return std::min(Current + Step, Target);
    if (Current > Target)
        return std::max(Current - Step, Target);
    return Current;
}

InputPacket InputEncoder::EncodeState(const RawInputState& State, float StepSeconds, InputControlMode Mode) noexcept
{
    PreviousPacket = LatestPacket;
    InputPacket Packet{};
    Packet.SequenceIndex = ++SequenceCounter;
    Packet.LookYawDelta = -State.CursorDeltaX * Settings.LookSensitivity;
    Packet.LookPitchDelta = -State.CursorDeltaY * Settings.LookSensitivity;

    if (Mode == InputControlMode::Navigation)
    {
        // 📝 The Lobby is not a driving mode: arrows navigate, Enter selects, Space readies, Escape backs
        //    out, and the mouse remains a camera input. No drive axes are emitted in this context.
        uint32_t Bits = InputActionNone;
        if (State.KeyUp || State.KeyLeft || State.GamepadNavigatePrev) Bits |= InputActionNavigatePrev;
        if (State.KeyDown || State.KeyRight || State.GamepadNavigateNext) Bits |= InputActionNavigateNext;
        if (State.KeyEnter || State.GamepadConfirm) Bits |= InputActionConfirm;
        if (State.KeySpace || State.GamepadReady) Bits |= InputActionReady;
        if (State.KeyEscape || State.GamepadBack) Bits |= InputActionBack;
        if (State.KeyC || State.GamepadCycleCamera) Bits |= InputActionCycleCamera;
        Packet.ActionBits = Bits;
        LatestPacket = Packet;
        return Packet;
    }

    // Steering: keys are a wheel that winds on and centres itself; the stick overrides while it is alive.
    const float KeySteer = (State.KeyA || State.KeyLeft ? -1.0f : 0.0f) + (State.KeyD || State.KeyRight ? 1.0f : 0.0f);
    const float StickSteer = ApplyDeadzone(std::clamp(State.GamepadSteer, -1.0f, 1.0f), Settings.StickDeadzone);
    if (StickSteer != 0.0f)
        Packet.SteerAxis = StickSteer;
    else if (KeySteer != 0.0f)
        Packet.SteerAxis = ApproachAxis(LatestPacket.SteerAxis, KeySteer, Settings.SteerRiseRate, StepSeconds);
    else
        Packet.SteerAxis = ApproachAxis(LatestPacket.SteerAxis, 0.0f, Settings.SteerReturnRate, StepSeconds);

    // Pedals: keys ramp, triggers are already analog.
    const float ThrottleTarget = (State.KeyW || State.KeyUp) ? 1.0f : std::clamp(State.GamepadThrottle, 0.0f, 1.0f);
    const float BrakeTarget = (State.KeyS || State.KeyDown) ? 1.0f : std::clamp(State.GamepadBrake, 0.0f, 1.0f);
    Packet.ThrottleAxis = ApproachAxis(LatestPacket.ThrottleAxis, ThrottleTarget,
                                       ThrottleTarget > LatestPacket.ThrottleAxis ? Settings.PedalRiseRate : Settings.PedalReturnRate, StepSeconds);
    Packet.BrakeAxis = ApproachAxis(LatestPacket.BrakeAxis, BrakeTarget,
                                    BrakeTarget > LatestPacket.BrakeAxis ? Settings.PedalRiseRate : Settings.PedalReturnRate, StepSeconds);

    Packet.HandbrakeAxis = (State.KeySpace || State.GamepadHandbrake) ? 1.0f : 0.0f;

    uint32_t Bits = InputActionNone;
    if (State.KeySpace || State.GamepadHandbrake) Bits |= InputActionHandbrake;
    if (State.KeyS || State.KeyDown) Bits |= InputActionReverse;
    if (State.KeyShift || State.GamepadBoost) Bits |= InputActionBoost;
    if (State.KeyEnter || State.GamepadReady) Bits |= InputActionReady;
    if (State.KeyR || State.GamepadReset) Bits |= InputActionResetUnit;
    if (State.KeyC || State.GamepadCycleCamera) Bits |= InputActionCycleCamera;
    if (State.KeyL) Bits |= InputActionLights;
    if (State.KeyEscape || State.GamepadMenu) Bits |= InputActionMenu;
    Packet.ActionBits = Bits;

    LatestPacket = Packet;
    return Packet;
}

void InputEncoder::Reset() noexcept
{
    PreviousPacket = InputPacket{};
    LatestPacket = InputPacket{};
    SequenceCounter = 0;
}

bool InputEncoder::WasActionPressed(InputActionBit Bit) const noexcept
{
    return LatestPacket.HasAction(Bit) && !PreviousPacket.HasAction(Bit);
}

bool InputEncoder::WasActionReleased(InputActionBit Bit) const noexcept
{
    return !LatestPacket.HasAction(Bit) && PreviousPacket.HasAction(Bit);
}

#if defined(FRONTIER_WITH_GLFW)
InputPacket InputEncoder::SampleGlfwWindow(GLFWwindow* Window, float StepSeconds, InputControlMode Mode) noexcept
{
    RawInputState State{};
    if (Window)
    {
        const auto Key = [Window](int Code) { return glfwGetKey(Window, Code) == GLFW_PRESS; };
        State.KeyW = Key(GLFW_KEY_W);     State.KeyS = Key(GLFW_KEY_S);
        State.KeyA = Key(GLFW_KEY_A);     State.KeyD = Key(GLFW_KEY_D);
        State.KeyUp = Key(GLFW_KEY_UP);   State.KeyDown = Key(GLFW_KEY_DOWN);
        State.KeyLeft = Key(GLFW_KEY_LEFT); State.KeyRight = Key(GLFW_KEY_RIGHT);
        State.KeySpace = Key(GLFW_KEY_SPACE);
        State.KeyShift = Key(GLFW_KEY_LEFT_SHIFT) || Key(GLFW_KEY_RIGHT_SHIFT);
        State.KeyEnter = Key(GLFW_KEY_ENTER);
        State.KeyEscape = Key(GLFW_KEY_ESCAPE);
        State.KeyR = Key(GLFW_KEY_R);     State.KeyC = Key(GLFW_KEY_C);   State.KeyL = Key(GLFW_KEY_L);

        double CursorX = 0.0, CursorY = 0.0;
        glfwGetCursorPos(Window, &CursorX, &CursorY);
        if (CursorSeeded)
        {
            State.CursorDeltaX = static_cast<float>(CursorX - PriorCursorX);
            State.CursorDeltaY = static_cast<float>(CursorY - PriorCursorY);
        }
        PriorCursorX = CursorX; PriorCursorY = CursorY; CursorSeeded = true;

        if (glfwJoystickIsGamepad(GLFW_JOYSTICK_1))
        {
            GLFWgamepadstate Pad{};
            if (glfwGetGamepadState(GLFW_JOYSTICK_1, &Pad))
            {
                State.GamepadSteer = Pad.axes[GLFW_GAMEPAD_AXIS_LEFT_X];
                State.GamepadThrottle = std::max(0.0f, Pad.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER]);
                State.GamepadBrake = std::max(0.0f, Pad.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER]);
                const auto Button = [&Pad](int Code) { return Pad.buttons[Code] == GLFW_PRESS; };
                State.GamepadNavigatePrev = Button(GLFW_GAMEPAD_BUTTON_DPAD_UP) || Button(GLFW_GAMEPAD_BUTTON_DPAD_LEFT);
                State.GamepadNavigateNext = Button(GLFW_GAMEPAD_BUTTON_DPAD_DOWN) || Button(GLFW_GAMEPAD_BUTTON_DPAD_RIGHT);
                State.GamepadConfirm = Button(GLFW_GAMEPAD_BUTTON_A);
                State.GamepadReady = Button(GLFW_GAMEPAD_BUTTON_A);
                State.GamepadBack = Button(GLFW_GAMEPAD_BUTTON_B);
                State.GamepadHandbrake = Button(GLFW_GAMEPAD_BUTTON_X);
                State.GamepadBoost = Button(GLFW_GAMEPAD_BUTTON_Y);
                State.GamepadReset = Button(GLFW_GAMEPAD_BUTTON_BACK);
                State.GamepadCycleCamera = Button(GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER);
                State.GamepadMenu = Button(GLFW_GAMEPAD_BUTTON_START);
            }
        }
    }
    return EncodeState(State, StepSeconds, Mode);
}
#endif

} // namespace Frontier
