/*==========================================================================================================================================
                                                             DIAGRAMCANVAS.CPP
==========================================================================================================================================*/
// 🧩 .ne-diagram: a logical W×H canvas scaled to the column width, with the pointer drag it reports.

#include "Engine/Interface/Controls/DiagramCanvas.h"

#include "Engine/Interface/Controls/ControlColumn.h"

#include <algorithm>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    DIAGRAM CANVAS
//------------------------------------------------------------------------------------------------------------------------

DiagramPen DiagramCanvas::Begin(ControlColumn& Column, const char* Id, float LogicalWidth, float LogicalHeight, float MarginTop,
                                float MarginBottom, DiagramDrag* Drag, const char* Tooltip)
{
    const EditorTypefaces& Faces    = Column.QueryFaces();
    ImDrawList* const      DrawList = Column.QueryDrawList();
    const float            Scale    = Column.QueryScale();
    const float            Width    = Column.QueryWidth();
    const float  PenScale = Width / LogicalWidth;
    const ImVec2 Top      = Column.Space(LogicalHeight * PenScale / Scale, MarginTop, MarginBottom);
    if (Drag != nullptr)
    {
        *Drag = DiagramDrag{};
        ImGui::SetCursorScreenPos(Top);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton(Id, ImVec2(Width, LogicalHeight * PenScale), ImGuiButtonFlags_EnableNav);
        ControlColumn::ConstructTooltip(Tooltip);
        if (ImGui::IsItemFocused())
        {
            Drag->Step = ImGui::GetIO().KeyShift ? 10.0f : 1.0f;
            for (ImGuiKey Key : {ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow,
                                ImGuiKey_DownArrow, ImGuiKey_Home, ImGuiKey_End})
            {
                if (ImGui::IsKeyPressed(Key)) Drag->Key = Key;
            }
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const ImGuiIO& IO = ImGui::GetIO();
            Drag->Engaged     = true;
            Drag->Start       = ImGui::IsItemActivated();
            Drag->Point       = ImVec2((IO.MousePos.x - Top.x) / PenScale, (IO.MousePos.y - Top.y) / PenScale);
            Drag->Delta       = ImVec2(IO.MouseDelta.x / PenScale, IO.MouseDelta.y / PenScale);
        }
    }
    return DiagramPen(DrawList, &Faces, Top, PenScale, LogicalWidth, LogicalHeight);
}

} // namespace Frontier
