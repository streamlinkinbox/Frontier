/*==========================================================================================================================================
                                                              WINDINSPECTOR.H
==========================================================================================================================================*/
// 🧩 The Wind page, the Force Field's guest: the wind speed now at 10 m with its last twelve seconds, the bearings, shear and
//    veer, the quick switches and five tab-switched sections (Flow, Gusts, Altitude, Eddies, Mesoscale), after the approved
//    prototype (Experimental/ForceFieldEditor). Its receivers moved to the Force Field page.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace Frontier
{

class ControlColumn;
class ForceFieldVisualization;

#if FRONTIER_EDITOR

//------------------------------------------------------------------------------------------------------------------------
//                                                    WIND INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

class WindInspector final : public RecordInspector
{
  public:
    static constexpr int SectionCount = 5;

    explicit WindInspector(WindConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "FORCES \xC2\xB7 FORCE FIELD \xC2\xB7 WIND"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "Guest of the Force Field"; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override
    {
        Parameters.Enabled = Enabled;
        Parameters.AdvanceRevision();
    }
    [[nodiscard]] uint64_t QueryRevision() const noexcept override { return Parameters.Revision; }
    void                   EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                   ConstructInspector() override;
    [[nodiscard]] bool     QueryOccupiesColumn() const noexcept override { return true; }
    [[nodiscard]] int      QuerySectionCount() const noexcept override { return SectionCount; }
    void                   ConfigureSection(int Section) noexcept override { ActiveSection = std::clamp(Section, 0, SectionCount - 1); }

  private:
    void ConstructHero(ControlColumn& Page);
    void ConstructReadouts(ControlColumn& Page);
    bool ConstructQuickControls(ControlColumn& Page);
    void ConstructSectionTabs(ControlColumn& Page);

    WindConfiguration&                       Parameters;
    int                                      ActiveSection   = 0;
    float                                    StoredGust      = 0.25f;   // [-] what the Gust wave tile restores
    float                                    StoredMesoscale = 0.22f;   // [-] what the Mesoscale tile restores
    std::shared_ptr<ForceFieldVisualization> EddyPreview;     // [-] shared: its deleter is bound where it is made
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
