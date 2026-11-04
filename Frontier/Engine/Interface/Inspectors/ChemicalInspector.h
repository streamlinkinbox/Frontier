/*==========================================================================================================================================
                                                            CHEMICALINSPECTOR.H
==========================================================================================================================================*/
// 🧩 ChemicalConfiguration: reaction kinetics.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

namespace Frontier
{

#if FRONTIER_EDITOR

//------------------------------------------------------------------------------------------------------------------------
//                                                    CHEMICAL INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

class ChemicalInspector final : public RecordInspector
{
  public:
    explicit ChemicalInspector(ChemicalConfiguration& InParameters, const ThermalConfiguration* InOptionalThermal = nullptr) noexcept
        : Parameters(InParameters)
        , OptionalThermal(InOptionalThermal)
    {
    }

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "CHEMISTRY · STOICHIOMETRY & KINETICS"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "Arrhenius Rate & Enthalpy"; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    ChemicalConfiguration&      Parameters;
    const ThermalConfiguration* OptionalThermal = nullptr;
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
