/*==========================================================================================================================================
                                                          FLUIDPLUMEPAGES.H
==========================================================================================================================================*/
// 🧩 The Smoke & fire editor's six cards — Domain, Source, Simulation, Appearance, Fire and Presets — built from the shared inspector controls on a
//    FluidPlume record, in the order and with the controls, ranges, steps, units and captions of the HMI prototype's "Smoke & fire" page
//    (InspectorPages.js BuildGas). The Simulation and Fire cards carry the prototype's two instruments (GasInstrumentProjection).

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/FluidPlumeEditor.h"
#include "Engine/Scene/EngineConfigurations.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

class ControlColumn;

// 📝 What a card reads and edits. Requests carries the verbs a card cannot carry out itself: "clear-smoke" (empty the lattices).
struct FluidPlumeContext
{
    FluidPlumeConfiguration&  Plume;
    const FluidPlumeReadout*  Readout = nullptr;
    std::vector<std::string>& Requests;
};

namespace FluidPlumePages
{
// 📝 The tallest column of the four-column plan at scale 1: Simulation alone, its canvas, ten sliders and the page's own spacing (1023 px measured, so
//    1030 leaves a margin). A window shorter than it shrinks the page a little to fit instead of cutting the last slider. FluidPlumeEditorValidation
//    measures every column of the plan against it.
inline constexpr float TallestColumnHeight = 1030.0f;   // [px]

void ConstructDomainCard(ControlColumn& Page, FluidPlumeContext& Context);
void ConstructSourceCard(ControlColumn& Page, FluidPlumeContext& Context);
void ConstructSimulationCard(ControlColumn& Page, FluidPlumeContext& Context);
void ConstructAppearanceCard(ControlColumn& Page, FluidPlumeContext& Context);
void ConstructFireCard(ControlColumn& Page, FluidPlumeContext& Context);
void ConstructPresetsCard(ControlColumn& Page, FluidPlumeContext& Context);

// 📝 The prototype's one column: the six cards in order (the inspector panel's page).
void ConstructPage(ControlColumn& Page, FluidPlumeContext& Context);

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
