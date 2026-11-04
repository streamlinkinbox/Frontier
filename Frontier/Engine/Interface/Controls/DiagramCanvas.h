/*==========================================================================================================================================
                                                              DIAGRAMCANVAS.H
==========================================================================================================================================*/
// 🧩 .ne-diagram: a logical W×H canvas scaled to the column width; reports the pointer drag in logical units.

#pragma once

#include "Engine/Interface/Controls/DiagramPen.h"

namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    DIAGRAM CANVAS
//------------------------------------------------------------------------------------------------------------------------

// 📝 What the pointer did on an interactive diagram this frame, in logical coordinates (the demo's Drag event).
struct DiagramDrag
{
    ImGuiKey Key = ImGuiKey_None;
    float    Step = 1.0f;
    bool   Engaged = false;   // the pointer is held on the diagram
    bool   Start   = false;   // pressed this frame
    ImVec2 Point   = ImVec2(0.0f, 0.0f);
    ImVec2 Delta   = ImVec2(0.0f, 0.0f);
};

class DiagramCanvas
{
  public:
    static DiagramPen Begin(ControlColumn& Column, const char* Id, float LogicalWidth, float LogicalHeight, float MarginTop, float MarginBottom,
                            DiagramDrag* Drag = nullptr, const char* Tooltip = nullptr);
};

} // namespace Frontier
