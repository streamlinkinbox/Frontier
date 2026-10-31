/*==========================================================================================================================================
                                                 SCENE INSPECTOR PANEL REGISTRATION
                                     ------------------------------------------------------------------
                       DockPanelSpecification factory binding SceneInspectorPanel into PanelDockspace.
==========================================================================================================================================*/

// 🧩 DockPanelSpecification factory binding SceneInspectorPanel into PanelDockspace.

#pragma once

#if defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/PanelDockspace.h"

namespace Frontier
{

class SceneDirectoryPanel;
class SceneInspectorPanel;

[[nodiscard]] DockPanelSpecification MakeSceneInspectorPanelSpecification(SceneInspectorPanel& InspectorPanel,
                                                                          SceneDirectoryPanel& DirectoryPanel);

} // namespace Frontier

#endif // FRONTIER_WITH_INTERFACE_SDK
