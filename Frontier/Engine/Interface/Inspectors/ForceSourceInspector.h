/*==========================================================================================================================================
                                                          FORCESOURCEINSPECTOR.H
==========================================================================================================================================*/
// 🧩 One localized force under the Force Field: its type, strength, reach, timing and receivers, after the approved
//    Force Field editor prototype (Experimental/ForceFieldEditor).

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

#include <algorithm>
#include <string>

namespace Frontier
{

class ControlColumn;

#if FRONTIER_EDITOR

//------------------------------------------------------------------------------------------------------------------------
//                                                    FORCE SOURCE INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

class ForceSourceInspector final : public RecordInspector
{
  public:
    static constexpr int SectionCount = 4;   // [-] Strength, Reach, Timing, Receivers

    explicit ForceSourceInspector(ForceSourceConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override;
    [[nodiscard]] const char* QuerySectionNote() const noexcept override;
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    [[nodiscard]] bool        QueryOccupiesColumn() const noexcept override { return true; }
    [[nodiscard]] int         QuerySectionCount() const noexcept override { return SectionCount; }
    void                      ConfigureSection(int Section) noexcept override { ActiveSection = std::clamp(Section, 0, SectionCount - 1); }
    [[nodiscard]] std::string QueryDirectoryReadout() const override;
    [[nodiscard]] const char* QueryDirectoryGlyph() const noexcept override;
    bool                      QueryViewTarget(InspectorViewTarget& View) const noexcept override;
    void                      ConstructInspector() override;

  private:
    bool ConstructStrength(ControlColumn& Page);
    bool ConstructReach(ControlColumn& Page);
    bool ConstructTiming(ControlColumn& Page);
    bool ConstructReceivers(ControlColumn& Page);

    ForceSourceConfiguration& Parameters;
    int                       ActiveSection = 0;
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
