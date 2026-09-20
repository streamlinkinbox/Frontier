//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Interaction/HotkeyChart.h — Key chord → command verb, Plasticity layout with Blender equivalents
//============================================================================================================================================
// Two layers: a global chart (works when no tool is running) and a modal chart (consumed by the running tool before
//    anything else: axis locks, numeric entry, snap toggles, confirm/cancel). Both are payload, editable at runtime via
//    the console (`bind`, `unbind`, `hotkeys`).
#pragma once

#include "InputEvent.h"
#include <string>
#include <vector>

namespace Frontier
{

struct HotkeyEntry
{
    Key         KeyCode = Key::None;                                                    // [-]
    uint8_t     Modifiers = ModifierNone;                                               // [-]
    std::string Verb;                                                                   // [-] console command line to run
    std::string Description;                                                            // [-]
};

class HotkeyChart
{
public:
    // Loads the Plasticity + Blender defaults.
    static HotkeyChart Defaults() noexcept;

    void Bind(Key KeyCode, uint8_t Modifiers, std::string Verb, std::string Description) noexcept;
    bool Unbind(Key KeyCode, uint8_t Modifiers) noexcept;
    [[nodiscard]] const HotkeyEntry* Find(Key KeyCode, uint8_t Modifiers) const noexcept;
    [[nodiscard]] const std::vector<HotkeyEntry>& Bindings() const noexcept { return Entries; }

private:
    std::vector<HotkeyEntry> Entries;
};

} // namespace Frontier
