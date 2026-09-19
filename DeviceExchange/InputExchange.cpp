//============================================================================================================================================
// 📦 Frontier/DeviceExchange/InputExchange.cpp — Multi-Device Input Polling Implementation
//============================================================================================================================================

#include "InputExchange.h"
#include <cmath>
#include <algorithm>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

InputExchange::InputExchange() noexcept
    : PrimaryGamepad{}
    , CursorDelta{ 0.0f, 0.0f, 0.0f }
    , CursorPositionX(0.0f)
    , CursorPositionY(0.0f)
    , AnalogDeadzone(0.15f)
    , MouseScrollDelta(0.0f)
    , KeyStates{}
    , MouseButtonStates{}
{
    PrimaryGamepad.ConnectedCondition = true;
}

void InputExchange::AssignCursorPosition(float PositionX, float PositionY) noexcept
{
    CursorDelta.x += (PositionX - CursorPositionX);
    CursorDelta.y += (PositionY - CursorPositionY);
    CursorPositionX = PositionX;
    CursorPositionY = PositionY;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                INPUT POLLING
//------------------------------------------------------------------------------------------------------------------------

void InputExchange::PollInputDevices() noexcept
{
    // Resets per-frame instantaneous deltas
    CursorDelta      = Vector3{ 0.0f, 0.0f, 0.0f };
    MouseScrollDelta = 0.0f;
}

void InputExchange::AssignKeyState(VirtualKeyCategory Key, bool Pressed) noexcept
{
    size_t Index = static_cast<size_t>(Key);
    if (Index < KeyStates.size())
    {
        KeyStates[Index] = Pressed;
    }
}

void InputExchange::AssignCursorDelta(float DeltaX, float DeltaY) noexcept
{
    CursorDelta.x += DeltaX;
    CursorDelta.y += DeltaY;
}

void InputExchange::AssignMouseButton(MouseButtonCategory Button, bool Pressed) noexcept
{
    size_t Index = static_cast<size_t>(Button);
    if (Index < MouseButtonStates.size())
    {
        MouseButtonStates[Index] = Pressed;
    }
}

void InputExchange::AssignMouseScroll(float ScrollDelta) noexcept
{
    MouseScrollDelta += ScrollDelta;
}

void InputExchange::AssignGamepadAxis(float LeftX, float LeftY, float RightX, float RightY, float LeftTrig, float RightTrig) noexcept
{
    auto ApplyDeadzone = [this](float x, float y) -> Vector3
    {
        float Length = std::sqrt(x * x + y * y);
        if (Length < AnalogDeadzone)
        {
            return Vector3{ 0.0f, 0.0f, 0.0f };
        }
        float ScaledLength = (Length - AnalogDeadzone) / (1.0f - AnalogDeadzone);
        return Vector3{ (x / Length) * ScaledLength, (y / Length) * ScaledLength, 0.0f };
    };

    PrimaryGamepad.LeftStickDirection   = ApplyDeadzone(LeftX, LeftY);
    PrimaryGamepad.RightStickDirection  = ApplyDeadzone(RightX, RightY);
    PrimaryGamepad.LeftTriggerPressure  = std::clamp(LeftTrig, 0.0f, 1.0f);
    PrimaryGamepad.RightTriggerPressure = std::clamp(RightTrig, 0.0f, 1.0f);
}

bool InputExchange::IsKeyPressed(VirtualKeyCategory Key) const noexcept
{
    size_t Index = static_cast<size_t>(Key);
    if (Index < KeyStates.size())
    {
        return KeyStates[Index];
    }
    return false;
}

bool InputExchange::IsMouseButtonPressed(MouseButtonCategory Button) const noexcept
{
    size_t Index = static_cast<size_t>(Button);
    if (Index < MouseButtonStates.size())
    {
        return MouseButtonStates[Index];
    }
    return false;
}

} // namespace Frontier
