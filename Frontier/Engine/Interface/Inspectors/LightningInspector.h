/*==========================================================================================================================================
                                                            LIGHTNINGINSPECTOR.H
==========================================================================================================================================*/
// 🧩 LightningConfiguration: storm flashes and delayed thunder.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

#include <string>

namespace Frontier
{

class ControlColumn;

#if FRONTIER_EDITOR

//------------------------------------------------------------------------------------------------------------------------
//                                                    LIGHTNING INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

class LightningInspector final : public RecordInspector
{
  public:
    explicit LightningInspector(LightningConfiguration &InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char *QueryEyebrowText() const noexcept override
    {
        return "ENVIRONMENT · LIGHTNING";
    }
    [[nodiscard]] const char *QuerySectionNote() const noexcept override
    {
        return "Dialectric discharge";
    }
    [[nodiscard]] bool QueryToggleSupported() const noexcept override
    {
        return true;
    }
    [[nodiscard]] bool QueryToggleEnabled() const noexcept override
    {
        return Parameters.Enabled;
    }
    void ConfigureToggle(bool Enabled) override
    {
        Parameters.Enabled = Enabled;
        Parameters.AdvanceRevision();
    }
    [[nodiscard]] uint64_t QueryRevision() const noexcept override
    {
        return Parameters.Revision;
    }
    void               EnumerateSwitches(std::vector<InspectorSwitchEntry> &OutSwitches) override;
    void               ConstructInspector() override;
    [[nodiscard]] bool QueryOccupiesColumn() const noexcept override
    {
        return true;
    }
    static void ConstructControls(ControlColumn &Page, LightningConfiguration &Parameters);
    // 📝 The demo's Lightning entity: Meta "<strikes> strikes" (no storm runs in the editor yet, so none have struck).
    [[nodiscard]] std::string QueryDirectoryReadout() const override
    {
        return "0 strikes";
    }

  private:
    LightningConfiguration &Parameters;
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
