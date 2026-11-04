/*==========================================================================================================================================
                                                           CHEMICALINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 ChemicalConfiguration: reaction kinetics.

#include "Engine/Interface/Inspectors/ChemicalInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/DiagramControls.h"
#include "Engine/Interface/InspectorControls.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#endif

namespace Frontier
{

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

void ChemicalInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Reversible", "Dynamic eq.", &Parameters.ReversibleEquilibrium});
    OutSwitches.push_back({"Catalyzed", "-35% Ea", &Parameters.CatalyzedPathway});
}

void ChemicalInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    const float ReferenceKelvin = OptionalThermal ? OptionalThermal->TemperatureKelvin : 298.15f;
    std::array<float, 32> ArrheniusSamples{};
    const float MaxRate = std::max(1.0e-6f, Parameters.ComputeArrheniusRateConstant(900.0f));
    for (size_t Index = 0; Index < ArrheniusSamples.size(); ++Index)
    {
        const float SampleK     = 250.0f + (static_cast<float>(Index) / 31.0f) * 650.0f;
        const float NormalizedK = Parameters.ComputeArrheniusRateConstant(SampleK) / MaxRate;
        ArrheniusSamples[Index] = std::pow(std::clamp(NormalizedK, 0.0f, 1.0f), 0.35f);
    }
    char RateHeader[80]{};
    std::snprintf(RateHeader, sizeof(RateHeader), "ARRHENIUS KINETICS · k(%.0fK)=%.2e /s",
                  ReferenceKelvin, Parameters.ComputeArrheniusRateConstant(ReferenceKelvin));
    DiagramControls::ConstructCurvePlot(Palette, Faces, RateHeader,
                                        ArrheniusSamples.data(), ArrheniusSamples.size(),
                                        ParseHexSpectrum("#62c3a5", Palette.LiveGreen));

    static const char* const PhaseLabels[4] = {"Solid", "Liquid", "Gas", "Plasma"};
    bool Changed = false;
    Changed |= InspectorControls::ConstructSegmentedRow(Palette, Faces, "##MatterPhase", "Phase", Parameters.MatterPhaseIndex, PhaseLabels, 4);
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##MolMass", "Molar Mass", Parameters.MolarMassGramsPerMole, 1.0f, 500.0f, "%.2f g/mol");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Moles", "Amount", Parameters.AmountMoles, 0.01f, 200.0f, "%.2f mol");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Conc", "Concentration", Parameters.ConcentrationMolPerLitre, 0.001f, 20.0f, "%.3f mol/L");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##ActEa", "Activation Ea", Parameters.ActivationEnergyKjPerMol, 2.0f, 250.0f, "%.1f kJ/mol");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Enthalpy", "Reaction dH", Parameters.EnthalpyDeltaKjPerMol, -500.0f, 500.0f, "%.1f kJ/mol");

    char MassBuf[48]{};
    char HeatBuf[48]{};
    std::snprintf(MassBuf, sizeof(MassBuf), "%.4f kg", Parameters.ComputeSubstanceMassKilograms());
    std::snprintf(HeatBuf, sizeof(HeatBuf), "%.2f kJ", Parameters.ComputeReactionHeatKilojoules());
    const std::array<InspectorReadoutCell, 2> Cells = {{
        {"REAGENT MASS", MassBuf},
        {"REACTION HEAT", HeatBuf},
    }};
    InspectorControls::ConstructReadoutStrip(Palette, Faces, Cells.data(), Cells.size());

    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

#elif FRONTIER_EDITOR

void ChemicalInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void ChemicalInspector::ConstructInspector() {}

#endif

} // namespace Frontier
