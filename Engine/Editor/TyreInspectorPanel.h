//============================================================================================================================================
//                                                       TYREINSPECTORPANEL.H
//============================================================================================================================================
// 📦 The tyre's editor surface: the quick strip that lands in the shared Inspector, and Tyre Generator, the separate
//    dockable asset window that owns the tread layer sequence.

#pragma once

#include "../Generators/Tyre/TreadSpecification.h"
#include "../Generators/Tyre/TreadPatternSpecification.h"

namespace Frontier {

class ControlPanel;
struct EditorInstance;
struct EditorSheet;

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE QUICK STRIP
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Draws the picked tyre row's sheet into the shared Inspector, as one card per group.
/// in    Controls  [-]   the shared widget vocabulary; every figure is edited through it
/// in    Picked    [-]   the outliner row, for its label and tint
/// in    Sheet     [-]   the project-filled property sheet; the panel draws what it is given
/// note  The signature matches RecordCameraInspector exactly, so the dispatch in InspectorPanel is one more
///       Appearance arm rather than a special case.
/// tag   api
void RecordTyreInspector(ControlPanel& Controls, EditorInstance& Picked, EditorSheet& Sheet);

}   // namespace Frontier
