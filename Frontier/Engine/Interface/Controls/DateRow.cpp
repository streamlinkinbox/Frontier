/*==========================================================================================================================================
                                                                DATEROW.CPP
==========================================================================================================================================*/
// 🧩 .co-data-row holding a text field (the calendar date).

#include "Engine/Interface/Controls/DateRow.h"

#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/GlyphRun.h"
#include "Engine/Interface/InspectorControls.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    DATE ROW
//------------------------------------------------------------------------------------------------------------------------

bool DateRow::Construct(ControlColumn& Column, const char* Id, const char* Label, std::string& InOutText, bool Enabled, const char* Tooltip)
{
    const ControlTheme&    Theme    = Column.QueryTheme();
    const EditorTypefaces& Faces    = Column.QueryFaces();
    ImDrawList* const      DrawList = Column.QueryDrawList();
    const float            Scale    = Column.QueryScale();
    const float            Width    = Column.QueryWidth();
    // 📝 .co-data-row: border-top 1 px, padding 11 px 0, 10 px label; .so-date 12 px field, padding 6 px, radius 9 px, ≤ 58 %.
    const float  S     = Scale;
    const float  Field = 6.0f + 12.0f * GlyphRun::EmPixelHeight + 6.0f + 2.0f;
    const ImVec2 Top   = Column.Space(1.0f + 11.0f + Field + 11.0f, 0.0f, 0.0f);
    const float  Fade  = Enabled ? 1.0f : 0.45f;
    DrawList->AddLine(ImVec2(Top.x, Top.y + 0.5f * S), ImVec2(Top.x + Width, Top.y + 0.5f * S), Theme.FieldRowRule, S);
    const float RowMid = Top.y + (1.0f + 11.0f + 0.5f * Field) * S;
    (void)GlyphRun::Construct(Column, Faces.Regular, 10.0f * S, ImVec2(Top.x, RowMid - 0.5f * GlyphRun::EmPixelHeight * 10.0f * S + GlyphRun::EmAscent * 10.0f * S),
                            FadeColour(Theme.FieldRowText, Fade), Label);
    const float  FieldWide = std::min(0.58f * Width, 120.0f * S);
    const ImVec2 FieldMin(Top.x + Width - FieldWide, RowMid - 0.5f * Field * S);
    const ImVec2 FieldMax(Top.x + Width, RowMid + 0.5f * Field * S);
    bool         Changed = false;
    ImGui::PushID(Id);
    ImGui::SetCursorScreenPos(ImVec2(FieldMin.x + 6.0f * S, FieldMin.y + 1.0f * S));
    if (!Enabled) ImGui::BeginDisabled();
    char Buffer[32] = {};
    std::snprintf(Buffer, sizeof(Buffer), "%s", InOutText.c_str());
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ResolveCssColour(0x00000000));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ResolveCssColour(0x00000000));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ResolveCssColour(0x00000000));
    ImGui::PushStyleColor(ImGuiCol_Text, ResolveCssColour(0xE7D8B9FF));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 5.0f * S));
    // 📝 PushFont takes the size before the style's global font scales.
    const ImGuiStyle& Style = ImGui::GetStyle();
    ImGui::PushFont(Faces.Regular, 12.0f * GlyphRun::EmPixelHeight * S / std::max(0.01f, Style.FontScaleMain * Style.FontScaleDpi));
    ImGui::SetNextItemWidth(FieldWide - 12.0f * S);
    if (ImGui::InputText("##Date", Buffer, sizeof(Buffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsScientific) && Enabled)
    {
        InOutText = Buffer;
        Changed   = true;
    }
    ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    if (!Enabled) ImGui::EndDisabled();
    ControlColumn::ConstructTooltip(Tooltip);
    ImGui::PopID();
    DrawList->AddRectFilled(FieldMin, FieldMax, FadeColour(ResolveCssColour(0x17161080), Fade), 9.0f * S);
    DrawList->AddRect(FieldMin, FieldMax, FadeColour(ResolveCssColour(0xE8C78B25), Fade), 9.0f * S, ImDrawFlags_None, S);
    return Changed;
}

} // namespace Frontier
