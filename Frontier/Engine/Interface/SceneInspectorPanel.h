/*====================================================================================================================================
                                                    SCENEINSPECTORPANEL.H
====================================================================================================================================*/
// 🧩 Inspector host: draws the description the selected record built for itself.

#pragma once

namespace Frontier
{

class SceneDirectoryPanel;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSPECTOR HOST
//------------------------------------------------------------------------------------------------------------------------

// 📝 Draws SceneDirectoryPanel::QueryInspectorModel inside the current ImGui window using the
//    VehicleHMI Inspector presentation. Generic scalar controls use the main/UI ControlKit slider
//    through PixelSpace; a record may instead provide its own native Inspector page, as Sun does.
//    Without the interface SDK this compiles to an empty stub so portable builds keep linking.
void ConstructSceneInspectorInterface(SceneDirectoryPanel &Panel);

} // namespace Frontier
