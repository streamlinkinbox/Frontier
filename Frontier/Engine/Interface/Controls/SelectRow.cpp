/*==========================================================================================================================================
                                                               SELECTROW.CPP
==========================================================================================================================================*/
// 🧩 .ne-control with .ne-select: a label and its dropdown.

#include "Engine/Interface/Controls/SelectRow.h"

#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/GlyphRun.h"
#include "Engine/Interface/InspectorControls.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "Engine/Interface/DropdownControls.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    SELECT ROW
//------------------------------------------------------------------------------------------------------------------------

bool SelectRow::Construct(ControlColumn& Column, const char* Id, const char* Label, const char* const* Options, int Count, int& InOutIndex,
                          bool Enabled, const char* Tooltip)
{
    const ControlTheme&    Theme    = Column.QueryTheme();
    const EditorTypefaces& Faces    = Column.QueryFaces();
    ImDrawList* const      DrawList = Column.QueryDrawList();
    const float            Scale    = Column.QueryScale();
    const float            Width    = Column.QueryWidth();
    // 📝 .co-card .ne-control (margin 12 px 0): the 10 px label (line 16 px, margin-bottom 9 px), then the 32 px select.
    const float  S    = Scale;
    const ImVec2 Top  = Column.Space(16.0f + 9.0f + 32.0f, 12.0f, 12.0f);
    const float  Fade = Enabled ? 1.0f : 0.45f;
    (void)GlyphRun::Construct(Column, Faces.Regular, 10.0f * S, ImVec2(Top.x, GlyphRun::ResolveBaseline(Top.y, 10.0f * S, 1.6f)), FadeColour(Theme.ControlLabel, Fade), Label);
    const ImVec2 FieldMin(Top.x, Top.y + 25.0f * S);
    return ConstructAt(Column, Id, FieldMin, Width, Options, Count, InOutIndex, Enabled, Tooltip);
}

bool SelectRow::ConstructAt(ControlColumn& Column, const char* Id, ImVec2 FieldMin, float Width,
                           const char* const* Options, int Count, int& InOutIndex, bool Enabled, const char* Tooltip)
{
    const auto& Theme = Column.QueryTheme();
    const auto& Faces = Column.QueryFaces();
    auto* DrawList = Column.QueryDrawList();
    const float S = Column.QueryScale();
    const float Fade = Enabled ? 1.0f : 0.45f;
    const ImVec2 FieldMax(FieldMin.x + Width, FieldMin.y + 32.0f * S);
    bool         Changed = false;
    ImGui::PushID(Id);
    ImGui::SetCursorScreenPos(FieldMin);
    if (!Enabled) ImGui::BeginDisabled();
    if (ImGui::InvisibleButton("##Select", ImVec2(Width, 32.0f * S), ImGuiButtonFlags_EnableNav) && Enabled) ImGui::OpenPopup("##SelectOptions");
    if (!Enabled) ImGui::EndDisabled();
    ControlColumn::ConstructTooltip(Tooltip);
    DrawList->AddRectFilled(FieldMin, FieldMax, FadeColour(Theme.SelectFill, Fade), 12.0f * S);
    DrawList->AddRect(FieldMin, FieldMax, FadeColour(Theme.SelectBorder, Fade), 12.0f * S, ImDrawFlags_None, S);
    const char* Current = (InOutIndex >= 0 && InOutIndex < Count) ? Options[InOutIndex] : "";
    (void)GlyphRun::Construct(Column, Faces.Regular, 11.0f * S,
                            ImVec2(FieldMin.x + 12.0f * S, FieldMin.y + 0.5f * (32.0f * S - GlyphRun::EmPixelHeight * 11.0f * S) + GlyphRun::EmAscent * 11.0f * S),
                            FadeColour(Theme.SelectText, Fade), Current);
    // 📝 The native select's chevron.
    const ImVec2 Chevron(FieldMax.x - 16.0f * S, 0.5f * (FieldMin.y + FieldMax.y));
    DrawList->AddLine(ImVec2(Chevron.x - 4.0f * S, Chevron.y - 2.0f * S), ImVec2(Chevron.x, Chevron.y + 2.0f * S), FadeColour(Theme.SelectText, Fade), 1.4f * S);
    DrawList->AddLine(ImVec2(Chevron.x, Chevron.y + 2.0f * S), ImVec2(Chevron.x + 4.0f * S, Chevron.y - 2.0f * S), FadeColour(Theme.SelectText, Fade), 1.4f * S);
    ImGui::SetNextWindowPos(ImVec2(FieldMin.x, FieldMax.y + 2.0f * S));
    ImGui::SetNextWindowSize(ImVec2(Width, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, Theme.SelectFill);
    ImGui::PushStyleColor(ImGuiCol_Border, Theme.SelectBorder);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 12.0f * S);
    if (ImGui::BeginPopup("##SelectOptions"))
    {
        for (int Index = 0; Index < Count; ++Index)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme.SelectText);
            if (ImGui::Selectable(Options[Index], Index == InOutIndex) && Index != InOutIndex)
            {
                InOutIndex = Index;
                Changed    = true;
            }
            ImGui::PopStyleColor();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::PopID();
    return Changed;
}

} // namespace Frontier
