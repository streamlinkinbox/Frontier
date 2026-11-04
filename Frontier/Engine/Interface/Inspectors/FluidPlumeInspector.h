/*==========================================================================================================================================
                                                           FLUIDPLUMEINSPECTOR.H
==========================================================================================================================================*/
// 🧩 FluidPlumeConfiguration: the Eulerian plume, with its FluidPlumeEditor.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

namespace Frontier
{

#if FRONTIER_EDITOR

//------------------------------------------------------------------------------------------------------------------------
//                                                    FLUID PLUME INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

class FluidPlumeInspector final : public RecordInspector
{
  public:
    explicit FluidPlumeInspector(FluidPlumeConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "SOLVER · FLUID PLUME"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "128x64x128 Eulerian"; }
    [[nodiscard]] const char* QueryEditorButtonLabel() const noexcept override { return "Plume Editor"; }
    void                      ConstructEditor(RecordToken Token) override;
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    FluidPlumeConfiguration& Parameters;
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
