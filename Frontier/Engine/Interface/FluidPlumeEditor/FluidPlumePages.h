/*==========================================================================================================================================
                                                          FLUIDPLUMEPAGES.H
==========================================================================================================================================*/
// 🧩 The Smoke & fire editor's Inspector pages on a FluidPlume record: the HMI prototype's "Smoke & fire" page (InspectorPages.js BuildGas) taken
//    apart by the Outliner's records — the Domain with its looks, the tyre Sources, the Test emitter, the gas's Simulation and Appearance, and the
//    Fire — with the controls, ranges, steps, units and captions of the prototype's cards. The Simulation and Fire pages carry the prototype's two
//    instruments (GasInstrumentProjection).

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/FluidPlumeEditor.h"
#include "Engine/Interface/FluidPlumeEditor/FluidPlumeLibrary.h"
#include "Engine/Scene/EngineConfigurations.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{

class ControlColumn;

// 📝 What a card reads and edits. Requests carries the verbs a card cannot carry out itself: "clear-smoke" (empty the lattices) and "library:on" (show the
//    libraries). Titled says whether a card draws its own heading: a page whose heading already names the card leaves it out.
struct FluidPlumeContext
{
    FluidPlumeConfiguration&  Plume;
    const FluidPlumeReadout*  Readout = nullptr;
    std::vector<std::string>& Requests;
    bool                      Titled  = true;
};

// 📝 The records the Outliner lists, one Inspector page each.
enum class FluidPlumePage : uint8_t
{
    Domain,
    Sources,
    Emitter,
    Simulation,
    Appearance,
    Fire
};

inline constexpr size_t FluidPlumePageCount = 6;

namespace FluidPlumePages
{
// 📝 The edits a page, the Outliner and the libraries share. Each changes the record and leaves a "noted:<kind>[:<argument>]" line in Requests for the
//    window's Log Stream (kinds: look, defaults, enabled, emitter).
void ApplyLook(FluidPlumeContext& Context, const FluidPlumeLook& Look);
void RestoreDefaults(FluidPlumeContext& Context);
void ToggleEnabled(FluidPlumeContext& Context);
void ChooseEmitter(FluidPlumeContext& Context, uint32_t Emitter);
void ToggleEmitter(FluidPlumeContext& Context);

void ConstructDomainCard(ControlColumn& Page, FluidPlumeContext& Context);
void ConstructEmitterCard(ControlColumn& Page, FluidPlumeContext& Context);
void ConstructSourceCard(ControlColumn& Page, FluidPlumeContext& Context);
void ConstructSimulationCard(ControlColumn& Page, FluidPlumeContext& Context);
void ConstructAppearanceCard(ControlColumn& Page, FluidPlumeContext& Context);
void ConstructFireCard(ControlColumn& Page, FluidPlumeContext& Context);
// 📝 The looks: the six presets as buttons, the look the readings hold now, and the way to the libraries.
void ConstructLooksCard(ControlColumn& Page, FluidPlumeContext& Context);

// 📝 One record's page: its heading (the eyebrow, the title and, for the Domain and the Test emitter, the switch in the title line) and its cards.
void ConstructWindowPage(ControlColumn& Page, FluidPlumeContext& Context, FluidPlumePage Which);

// 📝 The Outliner's token of a page ("Domain", "Sources", "Emitter", "Simulation", "Appearance", "Fire"), and back.
[[nodiscard]] const char* ResolveToken(FluidPlumePage Which) noexcept;
[[nodiscard]] bool        ResolvePage(std::string_view Token, FluidPlumePage& OutPage) noexcept;

// 📝 The grade the cards describe: the host's, High (the prototype's default) without one.
[[nodiscard]] uint32_t ResolveGrade(const FluidPlumeContext& Context) noexcept;
// 📝 "128×128×32" — the fine lattice of a grade, as the Domain card's metric reads it.
[[nodiscard]] std::string FormatLatticeExtent(uint32_t Grade);
// 📝 The Presets card's two readout lines (PlumeSpace.QueryDescription and SmokeSequence.QueryReadout).
[[nodiscard]] std::string FormatPlumeDescription(uint32_t Grade);
[[nodiscard]] std::string FormatPlumeCounters(const FluidPlumeReadout* Readout);
} // namespace FluidPlumePages

} // namespace Frontier

#endif
