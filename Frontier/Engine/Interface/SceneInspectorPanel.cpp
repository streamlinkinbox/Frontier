/*==========================================================================================================================================
                                                       SCENEINSPECTORPANEL.CPP
==========================================================================================================================================*/
// 🧩 Dockable Inspector panel delegating active selection inspection and removal requests to InspectorRuntime.

#include "Engine/Interface/SceneInspectorPanel.h"

#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Engine/Scene/SceneDirectory.h"

namespace Frontier
{

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

DockPanelSpecification MakeSceneInspectorPanelSpecification(SceneInspectorPanel& Panel)
{
    DockPanelSpecification Specification;
    Specification.Key              = "Inspector";
    Specification.Caption          = "Inspector";
    Specification.ConstructContent = [&Panel]() { Panel.ConstructInterface(); };
    Specification.Open             = true;
    Specification.WheelScrollOnly  = true;
    return Specification;
}

#endif

void SceneInspectorPanel::ConstructInterface()
{
#if FRONTIER_EDITOR
    const RecordToken Selected = OutlinerPanel.QuerySelected();
    if (!Selected.IsValid() || !SceneDirectory::Instance().QueryRegistered(Selected))
    {
        LastInspectedToken = RecordToken{};
    }
    else
    {
        LastInspectedToken = Selected;
    }

    bool DeleteRequested = false;
    InspectorRuntime::Instance().InspectRecord(LastInspectedToken, DeleteRequested);
    if (DeleteRequested && LastInspectedToken.IsValid())
    {
        OutlinerPanel.RequestDeleteRecord(LastInspectedToken);
    }
#endif
}

} // namespace Frontier
