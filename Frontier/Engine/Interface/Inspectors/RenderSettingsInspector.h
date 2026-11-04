/*==========================================================================================================================================
                                                         RENDERSETTINGSINSPECTOR.H
==========================================================================================================================================*/
// 🧩 The Render settings inspector: the whole renderer as one Outliner component (the RenderPipeline record) and one page, after the
//    approved HTML prototype (Experimental/RenderSettingsInspector).

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

#include <algorithm>
#include <string>

namespace Frontier
{

class ControlColumn;
struct RenderSettingsReading;

#if FRONTIER_EDITOR

//------------------------------------------------------------------------------------------------------------------------
//                                                    RENDER SETTINGS INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

class RenderSettingsInspector final : public RecordInspector
{
  public:
    static constexpr int SectionCount = 9;   // [-] Sampling, Lighting, Reflections, Shadows, Denoise, Exposure, Geometry, Views, Timing

    explicit RenderSettingsInspector(RenderPipelineConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "RENDERING / PIPELINE"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override
    {
        return Parameters.AnalyticIllumination ? "Analytic illumination" : (Parameters.HardwareRayQueryTier ? "RTX \xC2\xB7 ReSTIR" : "GTX \xC2\xB7 Distance field");
    }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    [[nodiscard]] bool        QueryOccupiesColumn() const noexcept override { return true; }
    [[nodiscard]] int         QuerySectionCount() const noexcept override { return SectionCount; }
    void                      ConfigureSection(int Section) noexcept override { ActiveSection = std::clamp(Section, 0, SectionCount - 1); }
    [[nodiscard]] std::string QueryDirectoryReadout() const override;
    void                      ConstructInspector() override;

  private:
    bool ConstructRenderer(ControlColumn& Page, const RenderSettingsReading& Reading);
    void ConstructTimingCard(ControlColumn& Page);
    void ConstructReadouts(ControlColumn& Page, const RenderSettingsReading& Reading);
    void ConstructSectionTabs(ControlColumn& Page);

    RenderPipelineConfiguration& Parameters;
    int                          ActiveSection = 0;
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
