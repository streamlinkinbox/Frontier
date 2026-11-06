/*==========================================================================================================================================
                                                          FLUIDPLUMELIBRARY.H
==========================================================================================================================================*/
// 🧩 The Smoke & fire editor's libraries, as its library sheet lists them: the six looks of the HMI prototype's GasPresets (applying one is
//    Object.assign(Gas, ConstructGasParameters(), Values)) and the plume's four quality grades. The values are the catalogue's, unchanged; the
//    group and the first sentence of each look are the window's own words, and what a look changes is read from its values, never written by hand.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"

#if FRONTIER_EDITOR

#include "Engine/PhysicalDynamics/FluidDynamics/GasParameterCatalogue.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{

// 📝 One entry of the Looks library.
struct FluidPlumeLook
{
    std::string                  Identifier;       // "thick-burnout": the verb argument and the card's id
    std::string                  Name;             // the prototype's key, "Thick burnout"
    std::string                  Group;            // "Smoke", "Fire" or "Wind": where the look's changes lie
    std::string                  Lead;             // the window's first sentence
    std::string                  Changes;          // "Emission 2.20× · Density 1.60 · …": the readings that differ from the defaults, read from the values
    const FluidDynamics::GasPreset* Preset = nullptr;
    FluidDynamics::GasParameters Values{};         // [-] the 33 readings as applying the look sets them
    uint32_t                     Differences = 0;  // [-] readings of Values that differ from the catalogue's defaults
};

namespace FluidPlumeLibrary
{
[[nodiscard]] const std::vector<FluidPlumeLook>& QueryLooks();
// 📝 The looks' groups in order of first appearance.
[[nodiscard]] const std::vector<std::string>& QueryGroups();
[[nodiscard]] const FluidPlumeLook* FindLook(std::string_view Identifier) noexcept;
// 📝 The look whose 33 readings the gas holds exactly (bit for bit), or null when the gas was edited or never took a look.
[[nodiscard]] const FluidPlumeLook* MatchLook(const FluidDynamics::GasParameters& Gas) noexcept;
// 📝 How many of the 33 readings differ between two gases.
[[nodiscard]] uint32_t CountDifferences(const FluidDynamics::GasParameters& Left, const FluidDynamics::GasParameters& Right) noexcept;
// 📝 One reading as its slider shows it ("2.20×", "5.5 m/s", "95°"): the digits follow the catalogue's step, the unit follows the figure.
[[nodiscard]] std::string FormatReading(const FluidDynamics::GasParameterEntry& Entry, double Value);
} // namespace FluidPlumeLibrary

} // namespace Frontier

#endif
