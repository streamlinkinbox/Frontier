/*==========================================================================================================================================
                                                        SCENEINSPECTORPANEL.H
==========================================================================================================================================*/
// 🧩 Dockable Inspector panel delegating active selection inspection and removal requests to InspectorRuntime.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Scene/RecordToken.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
#include "Engine/Interface/PanelDockspace.h"
#endif

namespace Frontier
{

class SceneDirectoryPanel;

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCENE INSPECTOR PANEL
//------------------------------------------------------------------------------------------------------------------------

class SceneInspectorPanel
{
  public:
    explicit SceneInspectorPanel(SceneDirectoryPanel& DirectoryPanel) noexcept
        : OutlinerPanel(DirectoryPanel)
    {
    }

    void ConstructInterface();

    [[nodiscard]] RecordToken QueryInspectedToken() const noexcept
    {
        return LastInspectedToken;
    }

  private:
    SceneDirectoryPanel& OutlinerPanel;
    RecordToken          LastInspectedToken;
};

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
[[nodiscard]] DockPanelSpecification MakeSceneInspectorPanelSpecification(SceneInspectorPanel& Panel);
#endif

} // namespace Frontier
