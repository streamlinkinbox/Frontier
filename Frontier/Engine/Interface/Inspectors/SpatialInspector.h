/*==========================================================================================================================================
                                                             SPATIALINSPECTOR.H
==========================================================================================================================================*/
// 🧩 SpatialConfiguration: position, orientation and scale.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

namespace Frontier
{

#if FRONTIER_EDITOR

//------------------------------------------------------------------------------------------------------------------------
//                                                    SPATIAL INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

class SpatialInspector final : public RecordInspector
{
  public:
    explicit SpatialInspector(SpatialConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "SPATIAL · TRANSFORM"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "Position · Orientation · Scale"; }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      ConstructInspector() override;

  private:
    SpatialConfiguration& Parameters;
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
