/*==========================================================================================================================================
                                                            THERMALINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 ThermalConfiguration: conduction, capacity and emissivity.

#include "Engine/Interface/Inspectors/ThermalInspector.h"

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

void ThermalInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Conduction", "Fourier flux", &Parameters.ConductiveExchange});
    OutSwitches.push_back({"Radiation", "Stefan-Boltzmann", &Parameters.RadiativeExchange});
}

void ThermalInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    const std::array<ImU32, 5> ThermalStops = {{
        ParseHexSpectrum("#3b76c4", Palette.ThemeAccent),
        ParseHexSpectrum("#4fa3a5", Palette.LiveGreen),
        ParseHexSpectrum("#e2a23b", Palette.WarmAmber),
        ParseHexSpectrum("#e85d2a", Palette.WarmAmber),
        ParseHexSpectrum("#fff2cc", Palette.TextPrimary),
    }};
    const float MarkerFraction = std::clamp((Parameters.TemperatureKelvin - 150.0f) / (2500.0f - 150.0f), 0.0f, 1.0f);
    char        HeaderLabel[64]{};
    std::snprintf(HeaderLabel, sizeof(HeaderLabel), "THERMAL STATE · %.1f K (%.1f C)",
                  Parameters.TemperatureKelvin, Parameters.QueryCelsius());
    DiagramControls::ConstructSpectrumBar(Palette, Faces, HeaderLabel,
                                          ThermalStops.data(), ThermalStops.size(), MarkerFraction);

    bool Changed = false;
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##TempK", "Temperature", Parameters.TemperatureKelvin, 150.0f, 2500.0f, "%.1f K");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##SpecHeat", "Specific Heat cp", Parameters.SpecificHeatJPerKgK, 100.0f, 4500.0f, "%.0f J/kgK");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Conduct", "Conductivity k", Parameters.ThermalConductivityWPerMK, 0.02f, 450.0f, "%.2f W/mK");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Emissiv", "Emissivity", Parameters.SurfaceEmissivity, 0.01f, 1.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Convect", "Convection h", Parameters.ConvectionCoefficientWPerM2K, 0.5f, 250.0f, "%.1f W/m2K");

    char FluxBuf[48]{};
    char EnthalpyBuf[48]{};
    std::snprintf(FluxBuf, sizeof(FluxBuf), "%.1f W/m2", Parameters.ComputeRadiativeFluxWPerM2());
    const float MassKg = OptionalMass ? OptionalMass->MassKilograms : 1.0f;
    std::snprintf(EnthalpyBuf, sizeof(EnthalpyBuf), "%.2f kJ", Parameters.ComputeSensibleHeatJoules(MassKg) * 1.0e-3f);
    const std::array<InspectorReadoutCell, 2> Cells = {{
        {"RADIATIVE FLUX", FluxBuf},
        {"SENSIBLE HEAT", EnthalpyBuf},
    }};
    InspectorControls::ConstructReadoutStrip(Palette, Faces, Cells.data(), Cells.size());

    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

#elif FRONTIER_EDITOR

void ThermalInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void ThermalInspector::ConstructInspector() {}

#endif

} // namespace Frontier
