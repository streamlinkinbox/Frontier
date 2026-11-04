/*==========================================================================================================================================
                                                           FORCEFIELDINSPECTOR.H
==========================================================================================================================================*/
// 🧩 The Force Field page, the master component: what the Field view measures, the counts and cost of the localized forces,
//    the quick switches, and seven tab-switched sections (View, Range, Forces, Receivers, Timeline, Vectors, Budget), after the
//    approved prototype (Experimental/ForceFieldEditor). The Wind is its guest record; the forces sit under it.

#pragma once

#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

#include <algorithm>
#include <string>

namespace Frontier
{

class ControlColumn;
struct ForceFieldPageReading;

#if FRONTIER_EDITOR

class ForceFieldInspector final : public RecordInspector
{
  public:
    static constexpr int SectionCount = 7;

    explicit ForceFieldInspector(ForceFieldConfiguration& Configuration) : Parameters(Configuration) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "FORCES \xC2\xB7 FORCE FIELD"; }
    [[nodiscard]] bool        QueryOccupiesColumn() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override
    {
        Parameters.Enabled = Enabled;
        Parameters.AdvanceRevision();
    }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    [[nodiscard]] const char* QueryDirectoryGlyph() const noexcept override { return "Waves"; }
    [[nodiscard]] std::string QueryDirectoryReadout() const override;
    [[nodiscard]] int         QuerySectionCount() const noexcept override { return SectionCount; }
    void                      ConfigureSection(int Section) noexcept override { ActiveSection = std::clamp(Section, 0, SectionCount - 1); }
    void                      ConstructInspector() override;

    bool QueryViewTarget(InspectorViewTarget& View) const noexcept override
    {
        View.Position = {Parameters.CentreMetres[0], Parameters.CentreMetres[1], Parameters.CentreMetres[2]};
        View.Radius   = std::max({Parameters.HalfExtentsMetres[0], Parameters.HalfExtentsMetres[1], Parameters.HalfExtentsMetres[2]});
        return true;
    }

  private:
    bool ConstructContribution(ControlColumn& Page);
    void ConstructHero(ControlColumn& Page, const ForceFieldPageReading& Reading);
    void ConstructReadouts(ControlColumn& Page, const ForceFieldPageReading& Reading);
    bool ConstructQuickControls(ControlColumn& Page, const ForceFieldPageReading& Reading);
    void ConstructSectionTabs(ControlColumn& Page);

    ForceFieldConfiguration& Parameters;
    int                      ActiveSection = 0;
};

#endif

} // namespace Frontier
