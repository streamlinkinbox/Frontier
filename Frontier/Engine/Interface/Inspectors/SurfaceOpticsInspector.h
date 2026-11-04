/*==========================================================================================================================================
                                                          SURFACEOPTICSINSPECTOR.H
==========================================================================================================================================*/
// 🧩 SurfaceOpticsConfiguration: albedo, roughness and metalness.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

namespace Frontier
{

#if FRONTIER_EDITOR

//------------------------------------------------------------------------------------------------------------------------
//                                                    SURFACE OPTICS INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

class SurfaceOpticsInspector final : public RecordInspector
{
  public:
    explicit SurfaceOpticsInspector(SurfaceOpticsConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "OPTICS · SURFACE SCATTERING"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "BSDF Albedo & Microfacet"; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    SurfaceOpticsConfiguration& Parameters;
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
