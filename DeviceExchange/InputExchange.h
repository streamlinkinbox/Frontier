//============================================================================================================================================
// 📦 Frontier/DeviceExchange/InputExchange.h — Multi-Device Keyboard, Mouse, and Gamepad Input Polling
//============================================================================================================================================

#pragma once

#if defined(_MSC_VER)
    #pragma warning(disable: 4324)                              // Disable structure padding alignment warning under /WX
#endif

#include "OrientationClassifier.h"
#include <cstdint>
#include <array>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                STANDARD VIRTUAL KEYCODES
//------------------------------------------------------------------------------------------------------------------------

enum class VirtualKeyCategory : uint32_t
{
    // Alphabet Letters (A-Z)
    KeyA                                = 0,
    KeyB                                = 1,
    KeyC                                = 2,
    KeyD                                = 3,
    KeyE                                = 4,
    KeyF                                = 5,
    KeyG                                = 6,
    KeyH                                = 7,
    KeyI                                = 8,
    KeyJ                                = 9,
    KeyK                                = 10,
    KeyL                                = 11,
    KeyM                                = 12,
    KeyN                                = 13,
    KeyO                                = 14,
    KeyP                                = 15,
    KeyQ                                = 16,
    KeyR                                = 17,
    KeyS                                = 18,
    KeyT                                = 19,
    KeyU                                = 20,
    KeyV                                = 21,
    KeyW                                = 22,
    KeyX                                = 23,
    KeyY                                = 24,
    KeyZ                                = 25,

    // Numeric Row (0-9)
    Key0                                = 26,
    Key1                                = 27,
    Key2                                = 28,
    Key3                                = 29,
    Key4                                = 30,
    Key5                                = 31,
    Key6                                = 32,
    Key7                                = 33,
    Key8                                = 34,
    Key9                                = 35,

    // Function Keys (F1-F12)
    KeyF1                               = 36,
    KeyF2                               = 37,
    KeyF3                               = 38,
    KeyF4                               = 39,
    KeyF5                               = 40,
    KeyF6                               = 41,
    KeyF7                               = 42,
    KeyF8                               = 43,
    KeyF9                               = 44,
    KeyF10                              = 45,
    KeyF11                              = 46,
    KeyF12                              = 47,

    // Directional Arrow Keys
    KeyUp                               = 48,
    KeyDown                             = 49,
    KeyLeft                             = 50,
    KeyRight                            = 51,

    // Navigation & Editing
    KeyEscape                           = 52,
    KeyEnter                            = 53,
    KeySpace                            = 54,
    KeyTab                              = 55,
    KeyBackspace                        = 56,
    KeyInsert                           = 57,
    KeyDelete                           = 58,
    KeyHome                             = 59,
    KeyEnd                              = 60,
    KeyPageUp                           = 61,
    KeyPageDown                         = 62,

    // Modifiers
    KeyLeftShift                        = 63,
    KeyRightShift                       = 64,
    KeyLeftControl                      = 65,
    KeyRightControl                     = 66,
    KeyLeftAlt                          = 67,
    KeyRightAlt                         = 68,
    KeyCapsLock                         = 69,

    // Punctuation & Symbols
    KeyTilde                            = 70,
    KeyMinus                            = 71,
    KeyEqual                            = 72,
    KeyBracketLeft                      = 73,
    KeyBracketRight                     = 74,
    KeyBackslash                        = 75,
    KeySemicolon                        = 76,
    KeyApostrophe                       = 77,
    KeyComma                            = 78,
    KeyPeriod                           = 79,
    KeySlash                            = 80,

    Count                               = 81
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    MOUSE BUTTON
//------------------------------------------------------------------------------------------------------------------------

enum class MouseButtonCategory : uint32_t
{
    ButtonLeft                          = 0,
    ButtonRight                         = 1,
    ButtonMiddle                        = 2,
    ButtonExtra1                        = 3,
    ButtonExtra2                        = 4,
    Count                               = 5
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    GAMEPAD RECORD
//------------------------------------------------------------------------------------------------------------------------

struct GamepadRecord
{
    Vector3                 LeftStickDirection;                 // [-1..1] analog left joystick axis (x, y)
    Vector3                 RightStickDirection;                // [-1..1] analog right joystick axis (x, y)
    float                   LeftTriggerPressure;                // [0..1] analog brake trigger pressure
    float                   RightTriggerPressure;               // [0..1] analog accelerator trigger pressure
    bool                    ButtonSouthDown;                    // [bool] A / Cross button
    bool                    ButtonEastDown;                     // [bool] B / Circle button
    bool                    ButtonWestDown;                     // [bool] X / Square button
    bool                    ButtonNorthDown;                    // [bool] Y / Triangle button
    bool                    ConnectedCondition;                 // [bool] gamepad connection status
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   INPUT EXCHANGE
//------------------------------------------------------------------------------------------------------------------------

class InputExchange
{
public:
    InputExchange() noexcept;
    ~InputExchange() noexcept = default;

    InputExchange(const InputExchange&) = delete;
    InputExchange& operator=(const InputExchange&) = delete;

    void                    PollInputDevices() noexcept;

    void                    AssignKeyState(VirtualKeyCategory Key, bool Pressed) noexcept;
    void                    AssignCursorDelta(float DeltaX, float DeltaY) noexcept;
    void                    AssignCursorPosition(float PositionX, float PositionY) noexcept;
    void                    AssignMouseButton(MouseButtonCategory Button, bool Pressed) noexcept;
    void                    AssignMouseScroll(float ScrollDelta) noexcept;
    void                    AssignGamepadAxis(float LeftX, float LeftY, float RightX, float RightY, float LeftTrig, float RightTrig) noexcept;

    [[nodiscard]] bool      IsKeyPressed(VirtualKeyCategory Key) const noexcept;
    [[nodiscard]] bool      IsMouseButtonPressed(MouseButtonCategory Button) const noexcept;
    [[nodiscard]] float     QueryMouseScrollDelta() const noexcept { return MouseScrollDelta; }
    [[nodiscard]] Vector3   QueryCursorDelta() const noexcept { return CursorDelta; }
    [[nodiscard]] float     QueryCursorPositionX() const noexcept { return CursorPositionX; }
    [[nodiscard]] float     QueryCursorPositionY() const noexcept { return CursorPositionY; }
    [[nodiscard]] const GamepadRecord& QueryGamepad() const noexcept { return PrimaryGamepad; }

    // Single unified conversion operator for primary gamepad
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    GamepadRecord           PrimaryGamepad;                     // [gamepad] active gamepad record
    Vector3                 CursorDelta;                        // [px] mouse delta movement
    float                   CursorPositionX;                    // [px] absolute horizontal cursor coordinate
    float                   CursorPositionY;                    // [px] absolute vertical cursor coordinate
    float                   AnalogDeadzone;                     // [0..1] joystick deadzone threshold
    float                   MouseScrollDelta;                   // [clicks] mouse scroll wheel increment
    std::array<bool, static_cast<size_t>(VirtualKeyCategory::Count)> KeyStates; // [keys] active pressed keys
    std::array<bool, static_cast<size_t>(MouseButtonCategory::Count)> MouseButtonStates; // [buttons] mouse buttons
};

template<>
inline GamepadRecord InputExchange::Convert<GamepadRecord>() const noexcept
{
    return PrimaryGamepad;
}

template<>
inline bool InputExchange::Convert<bool>() const noexcept
{
    return PrimaryGamepad.ConnectedCondition;
}

} // namespace Frontier
