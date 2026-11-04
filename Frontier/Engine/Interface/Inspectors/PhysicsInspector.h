/*==========================================================================================================================================
                                                             PHYSICSINSPECTOR.H
==========================================================================================================================================*/
// 🧩 PhysicsConfiguration: mass, kinematics and contact response.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

namespace Frontier
{

#if FRONTIER_EDITOR

//------------------------------------------------------------------------------------------------------------------------
//                                                    PHYSICS INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

class PhysicsInspector final : public RecordInspector
{
  public:
    explicit PhysicsInspector(PhysicsConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "MECHANICS · MASS & DYNAMICS"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "MassConfiguration + Kinematics"; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    PhysicsConfiguration& Parameters;
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
