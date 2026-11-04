/*==========================================================================================================================================
                                                          FLUIDPLUMEINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 FluidPlumeConfiguration: the Eulerian plume, with its FluidPlumeEditor.

#include "Engine/Interface/Inspectors/FluidPlumeInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/DiagramControls.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/FluidPlumeEditor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#endif

namespace Frontier
{

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

void FluidPlumeInspector::ConstructEditor(RecordToken Token)
{
    FluidPlumeEditor::ConstructInterface(Token, *this);
}

void FluidPlumeInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Combustion", "Blackbody core", &Parameters.CombustionEnabled});
    OutSwitches.push_back({"Self-Shadow", "Beer-Lambert", &Parameters.SelfShadowing});
}

void FluidPlumeInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    const std::array<ImU32, 5> BlackbodyStops = {{
        ParseHexSpectrum("#2a120a", Palette.InsetFieldBg),
        ParseHexSpectrum("#8f2412", Palette.WarmAmber),
        ParseHexSpectrum("#e86820", Palette.WarmAmber),
        ParseHexSpectrum("#ffbe44", Palette.WarmAmber),
        ParseHexSpectrum("#fff7df", Palette.TextPrimary),
    }};
    const float CoreFraction = std::clamp((Parameters.BlackbodyCoreKelvin - 800.0f) / (3200.0f - 800.0f), 0.0f, 1.0f);
    char        CoreHeader[64]{};
    std::snprintf(CoreHeader, sizeof(CoreHeader), "BLACKBODY EMISSION · %.0f K", Parameters.BlackbodyCoreKelvin);
    DiagramControls::ConstructSpectrumBar(Palette, Faces, CoreHeader,
                                          BlackbodyStops.data(), BlackbodyStops.size(), CoreFraction);

    bool Changed = false;
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PlmBuoy", "Buoyancy Lift", Parameters.BuoyancyLift, 0.0f, 6.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PlmVort", "Vorticity Confinement", Parameters.VorticityConfinement, 0.0f, 5.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PlmDiss", "Dissipation", Parameters.DissipationRate, 0.01f, 1.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PlmFuel", "Fuel Injection", Parameters.FuelInjectionRate, 0.0f, 4.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PlmKelvin", "Blackbody Core", Parameters.BlackbodyCoreKelvin, 800.0f, 3200.0f, "%.0f K");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PlmSoot", "Soot Extinction", Parameters.SootExtinction, 0.0f, 4.0f, "%.2f");
    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

#elif FRONTIER_EDITOR

void FluidPlumeInspector::ConstructEditor(RecordToken) {}
void FluidPlumeInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void FluidPlumeInspector::ConstructInspector() {}

#endif

} // namespace Frontier
