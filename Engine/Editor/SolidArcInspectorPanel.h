//============================================================================================================================================
//                                                          SOLIDARCINSPECTORPANEL.H
//============================================================================================================================================
// 📦 SolidArc inspector card style — the glass-card look for the CAD object sheet (header, stat tiles, XYZ transform rows).

#pragma once

#include <cstdint>

namespace Frontier {

class ControlPanel;
struct EditorInstance;
struct EditorSheet;

// Paints the whole sheet inside the caller's child window. Shut points at the caller's eight card-fold flags.
void RecordSolidArcInspector(ControlPanel& Controls, EditorInstance& Picked, uint32_t PickedIndex, EditorSheet& Sheet, bool* Shut) noexcept;

// The glass card shown while nothing is picked.
void RecordSolidArcEmpty(ControlPanel& Controls) noexcept;

} // namespace Frontier
