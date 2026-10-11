//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Interaction/InputEvent.cpp — Key chord text ↔ Key
//============================================================================================================================================

#include "InputEvent.h"
#include <cctype>
#include <utility>

namespace Frontier
{

namespace
{
struct KeyName { const char* Name; Key Code; };
const KeyName Names[] =
{
    { "a", Key::A }, { "b", Key::B }, { "c", Key::C }, { "d", Key::D }, { "e", Key::E }, { "f", Key::F }, { "g", Key::G }, { "h", Key::H },
    { "i", Key::I }, { "j", Key::J }, { "k", Key::K }, { "l", Key::L }, { "m", Key::M }, { "n", Key::N }, { "o", Key::O }, { "p", Key::P },
    { "q", Key::Q }, { "r", Key::R }, { "s", Key::S }, { "t", Key::T }, { "u", Key::U }, { "v", Key::V }, { "w", Key::W }, { "x", Key::X },
    { "y", Key::Y }, { "z", Key::Z },
    { "0", Key::Digit0 }, { "1", Key::Digit1 }, { "2", Key::Digit2 }, { "3", Key::Digit3 }, { "4", Key::Digit4 },
    { "5", Key::Digit5 }, { "6", Key::Digit6 }, { "7", Key::Digit7 }, { "8", Key::Digit8 }, { "9", Key::Digit9 },
    { "numpad0", Key::Numpad0 }, { "numpad1", Key::Numpad1 }, { "numpad2", Key::Numpad2 }, { "numpad3", Key::Numpad3 }, { "numpad4", Key::Numpad4 },
    { "numpad5", Key::Numpad5 }, { "numpad6", Key::Numpad6 }, { "numpad7", Key::Numpad7 }, { "numpad8", Key::Numpad8 }, { "numpad9", Key::Numpad9 },
    { "numpad.", Key::NumpadPeriod }, { "numpad-", Key::NumpadMinus }, { "numpad+", Key::NumpadPlus },
    { "f1", Key::F1 }, { "f2", Key::F2 }, { "f3", Key::F3 }, { "f4", Key::F4 }, { "f5", Key::F5 }, { "f6", Key::F6 },
    { "f7", Key::F7 }, { "f8", Key::F8 }, { "f9", Key::F9 }, { "f10", Key::F10 }, { "f11", Key::F11 }, { "f12", Key::F12 },
    { "enter", Key::Enter }, { "return", Key::Enter }, { "esc", Key::Escape }, { "escape", Key::Escape }, { "tab", Key::Tab },
    { "backspace", Key::Backspace }, { "delete", Key::Delete }, { "del", Key::Delete }, { "space", Key::Space },
    { "minus", Key::Minus }, { "-", Key::Minus }, { "equal", Key::Equal }, { "=", Key::Equal }, { "period", Key::Period }, { ".", Key::Period },
    { "comma", Key::Comma }, { ",", Key::Comma }, { "slash", Key::Slash }, { "/", Key::Slash }, { "backslash", Key::Backslash },
    { "left", Key::Left }, { "right", Key::Right }, { "up", Key::Up }, { "down", Key::Down }, { "home", Key::Home }, { "end", Key::End },
};
}

bool ParseKeyChord(std::string_view Text, Key& KeyOut, uint8_t& ModifiersOut) noexcept
{
    ModifiersOut = ModifierNone;
    std::string Lower;
    for (char C : Text) Lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(C))));
    size_t Start = 0;
    while (true)
    {
        size_t Plus = Lower.find('+', Start);
        // A trailing "+" as the key itself ("numpad+", "ctrl+=") — only treat '+' as separator when text follows it.
        if (Plus == std::string::npos || Plus + 1 >= Lower.size()) break;
        std::string Part = Lower.substr(Start, Plus - Start);
        if (Part == "shift") ModifiersOut |= ModifierShift;
        else if (Part == "ctrl" || Part == "control") ModifiersOut |= ModifierCtrl;
        else if (Part == "alt") ModifiersOut |= ModifierAlt;
        else break;
        Start = Plus + 1;
    }
    std::string KeyText = Lower.substr(Start);
    for (const KeyName& N : Names) if (KeyText == N.Name) { KeyOut = N.Code; return true; }
    return false;
}

std::string DescribeKeyChord(Key KeyCode, uint8_t Modifiers) noexcept
{
    std::string Out;
    if (Modifiers & ModifierCtrl) Out += "Ctrl+";
    if (Modifiers & ModifierShift) Out += "Shift+";
    if (Modifiers & ModifierAlt) Out += "Alt+";
    for (const KeyName& N : Names) if (N.Code == KeyCode) { std::string K = N.Name; if (!K.empty()) K[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(K[0]))); Out += K; return Out; }
    return Out + "?";
}

} // namespace Frontier
