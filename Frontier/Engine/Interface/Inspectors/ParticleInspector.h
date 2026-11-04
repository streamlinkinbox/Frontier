/*==========================================================================================================================================
                                                            PARTICLEINSPECTOR.H
==========================================================================================================================================*/
// 🧩 ParticleConfiguration: GPU particles, with its ParticleEditor.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

namespace Frontier
{

#if FRONTIER_EDITOR

//------------------------------------------------------------------------------------------------------------------------
//                                                    PARTICLE INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

class ParticleInspector final : public RecordInspector
{
  public:
    explicit ParticleInspector(ParticleConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "SOLVER · GPU PARTICLES"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "Ballistic & precipitation"; }
    [[nodiscard]] const char* QueryEditorButtonLabel() const noexcept override { return "Particle Editor"; }
    void                      ConstructEditor(RecordToken Token) override;
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    ParticleConfiguration& Parameters;
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
