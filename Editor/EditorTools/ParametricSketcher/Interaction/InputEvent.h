//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Interaction/InputEvent.h — Toolkit-neutral input: keys, modifiers, pointer, wheel, text
//============================================================================================================================================
// The console synthesises these today; a GLFW/ImGui window will synthesise the same events later. Nothing below knows
//    about either.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Frontier
{

enum class Key : uint8_t
{
    None = 0,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9,
    Numpad0, Numpad1, Numpad2, Numpad3, Numpad4, Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
    NumpadPeriod, NumpadMinus, NumpadPlus,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Enter, Escape, Tab, Backspace, Delete, Space, Minus, Equal, Period, Comma, Slash, Backslash,
    Left, Right, Up, Down, Home, End,
};

enum Modifier : uint8_t { ModifierNone = 0, ModifierShift = 1, ModifierCtrl = 2, ModifierAlt = 4 };

enum class PointerButton : uint8_t { None, Left, Middle, Right };

enum class InputAction : uint8_t
{
    KeyPress,                                                                           // Key + Modifiers
    KeyRelease,
    Text,                                                                               // Character typed (digits, '.', '-', operators)
    PointerMove,                                                                        // PixelX/Y
    PointerPress,                                                                       // Button + PixelX/Y
    PointerRelease,
    Wheel,                                                                              // WheelSteps
};

struct InputEvent
{
    InputAction   Action = InputAction::PointerMove;                                    // [-]
    Key           KeyCode = Key::None;                                                  // [-]
    uint8_t       Modifiers = ModifierNone;                                             // [-] bit set
    char          Character = 0;                                                        // [-] for Text
    PointerButton Button = PointerButton::None;                                         // [-]
    double        PixelX = 0.0, PixelY = 0.0;                                           // [px]
    double        WheelSteps = 0.0;                                                     // [-]

    [[nodiscard]] bool Shift() const noexcept { return Modifiers & ModifierShift; }
    [[nodiscard]] bool Ctrl() const noexcept { return Modifiers & ModifierCtrl; }
    [[nodiscard]] bool Alt() const noexcept { return Modifiers & ModifierAlt; }
};

// "shift+a", "ctrl+alt+z", "numpad1", "enter", "esc", "f3" → Key + modifiers. False on unknown names.
[[nodiscard]] bool ParseKeyChord(std::string_view Text, Key& KeyOut, uint8_t& ModifiersOut) noexcept;
[[nodiscard]] std::string DescribeKeyChord(Key KeyCode, uint8_t Modifiers) noexcept;

} // namespace Frontier
