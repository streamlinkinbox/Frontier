/*==========================================================================================================================================
                                                            FRACTUREINSPECTOR.H
==========================================================================================================================================*/
// 🧩 FractureConfiguration: Voronoi fracture, with its FractureEditor.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

namespace Frontier
{

#if FRONTIER_EDITOR

//------------------------------------------------------------------------------------------------------------------------
//                                                    FRACTURE INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

class FractureInspector final : public RecordInspector
{
  public:
    explicit FractureInspector(FractureConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "SOLVER · VORONOI FRACTURE"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "Convex hull decomposition"; }
    [[nodiscard]] const char* QueryEditorButtonLabel() const noexcept override { return "Fracture Editor"; }
    void                      ConstructEditor(RecordToken Token) override;
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    FractureConfiguration& Parameters;
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
