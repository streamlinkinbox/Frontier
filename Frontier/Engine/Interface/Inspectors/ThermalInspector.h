/*==========================================================================================================================================
                                                             THERMALINSPECTOR.H
==========================================================================================================================================*/
// 🧩 ThermalConfiguration: conduction, capacity and emissivity.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

namespace Frontier
{

#if FRONTIER_EDITOR

//------------------------------------------------------------------------------------------------------------------------
//                                                    THERMAL INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

class ThermalInspector final : public RecordInspector
{
  public:
    explicit ThermalInspector(ThermalConfiguration& InParameters, const MassConfiguration* InOptionalMass = nullptr) noexcept
        : Parameters(InParameters)
        , OptionalMass(InOptionalMass)
    {
    }

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "THERMODYNAMICS · HEAT TRANSFER"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "Conduction & Stefan-Boltzmann"; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    ThermalConfiguration&    Parameters;
    const MassConfiguration* OptionalMass = nullptr;
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
