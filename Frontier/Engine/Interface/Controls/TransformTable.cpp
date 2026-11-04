/*==========================================================================================================================================
                                                            TRANSFORMTABLE.CPP
==========================================================================================================================================*/
// 🧩 The Project Zero transform table (.transform-table): a component column and X / Y / Z drag fields per row, with one
//    reset pill per row under it.

#include "Engine/Interface/Controls/TransformTable.h"

#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/GlyphRun.h"
#include "Engine/Interface/Controls/GlyphSymbol.h"
#include "Engine/Interface/InspectorControls.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRANSFORM TABLE
//------------------------------------------------------------------------------------------------------------------------

bool TransformTable::Construct(ControlColumn& Column, const char* Id, const TransformTableRow* Rows, int Count, bool Enabled, bool Compact)
{
    // 📝 .transform-table: margin 18 px 0 14 px; the first column 28 %; header 10 px #777 (8 px for "Component"), axes #cf9a93 /
    //    #9ec3a6 / #91aace; cells padded 5 px 2 px; fields 29 px tall, radius 7 px, #151515 over #ffffff09, 11 px centred.
    const EditorTypefaces& Faces    = Column.QueryFaces();
    ImDrawList* const      DrawList = Column.QueryDrawList();
    const float            S        = Column.QueryScale();
    const float            Width    = Column.QueryWidth();
    const float     HeaderTall = Compact ? 0.0f : 26.0f;
    constexpr float RowTall = 39.0f, ResetTall = 26.0f;
    const ImVec2 Top = Column.Space(HeaderTall + RowTall * static_cast<float>(Count), Compact ? 2.0f : 18.0f, Compact ? 6.0f : 10.0f);
    const float  LabelWidth = 0.28f * Width;
    const float  FieldWidth = (Width - LabelWidth) / 3.0f;
    const float  Fade       = Enabled ? 1.0f : 0.45f;
    static constexpr const char* Axes[3] = {"X", "Y", "Z"};
    static constexpr uint32_t    Inks[3] = {0xCF9A93FF, 0x9EC3A6FF, 0x91AACEFF};
    if (!Compact) (void)GlyphRun::Construct(Column, Faces.Regular, 8.0f * S, ImVec2(Top.x, Top.y + 13.0f * S), FadeColour(ResolveCssColour(0x777777FF), Fade), "Component");
    for (int Axis = 0; Axis < 3 && !Compact; ++Axis)
    {
        const float Advance = GlyphRun::Measure(Column, Faces.Regular, 10.0f * S, Axes[Axis]);
        (void)GlyphRun::Construct(Column, Faces.Regular, 10.0f * S,
                                  ImVec2(Top.x + LabelWidth + FieldWidth * (static_cast<float>(Axis) + 0.5f) - 0.5f * Advance, Top.y + 14.0f * S),
                                  FadeColour(ResolveCssColour(Inks[Axis]), Fade), Axes[Axis]);
    }
    bool Changed = false;
    ImGui::PushID(Id);
    ImGui::PushFont(GlyphRun::ResolveFace(Faces.Regular), 11.0f * GlyphRun::EmPixelHeight * S);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f * S);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, S);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f * S, (29.0f - 11.0f * GlyphRun::EmPixelHeight) * 0.5f * S));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ResolveCssColour(0x151515FF));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ResolveCssColour(0x1C1C1CFF));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ResolveCssColour(0x202020FF));
    ImGui::PushStyleColor(ImGuiCol_Border, ResolveCssColour(0xFFFFFF09));
    ImGui::PushStyleColor(ImGuiCol_Text, ResolveCssColour(0xE6E6E6FF));
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ResolveCssColour(0xB9A68B66));
    if (!Enabled) ImGui::BeginDisabled();
    for (int Row = 0; Row < Count; ++Row)
    {
        const TransformTableRow& Entry  = Rows[Row];
        const float              RowTop = Top.y + (HeaderTall + RowTall * static_cast<float>(Row)) * S;
        (void)GlyphRun::Construct(Column, Faces.Regular, 10.0f * S, ImVec2(Top.x, RowTop + 17.0f * S), FadeColour(ResolveCssColour(0xCCCCCCFF), Fade), Entry.Label);
        (void)GlyphRun::Construct(Column, Faces.Regular, 8.0f * S, ImVec2(Top.x, RowTop + 29.0f * S), FadeColour(ResolveCssColour(0x707070FF), Fade), Entry.Unit);
        ImGui::PushID(Row);
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            ImGui::PushID(Axis);
            ImGui::SetCursorScreenPos(ImVec2(Top.x + LabelWidth + FieldWidth * static_cast<float>(Axis) + 2.0f * S, RowTop + 5.0f * S));
            ImGui::SetNextItemWidth(FieldWidth - 4.0f * S);
            float Quantity = Entry.Values[Axis];
            if (ImGui::DragFloat("##Field", &Quantity, Entry.Step, Entry.Minimum, Entry.Maximum, Entry.Format, ImGuiSliderFlags_AlwaysClamp) &&
                std::isfinite(Quantity) && Quantity != Entry.Values[Axis])
            {
                Entry.Values[Axis] = Quantity;
                Changed            = true;
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(3);
    ImGui::PopFont();

    if (Compact)
    {
        if (!Enabled) ImGui::EndDisabled();
        ImGui::PopID();
        return Changed;
    }
    // 📝 .transform-resets: one pill per row, gap 4 px, 9 px, "↺ Label".
    const ImVec2 ResetTop  = Column.Space(ResetTall, 0.0f, 12.0f);
    const float  PillWidth = (Width - 4.0f * S * static_cast<float>(Count - 1)) / static_cast<float>(std::max(1, Count));
    for (int Row = 0; Row < Count; ++Row)
    {
        const TransformTableRow& Entry = Rows[Row];
        const ImVec2 Min(ResetTop.x + (PillWidth + 4.0f * S) * static_cast<float>(Row), ResetTop.y);
        const ImVec2 Max(Min.x + PillWidth, Min.y + ResetTall * S);
        ImGui::SetCursorScreenPos(Min);
        ImGui::PushID(100 + Row);
        if (ImGui::InvisibleButton("##Reset", ImVec2(PillWidth, ResetTall * S)) && Enabled)
        {
            for (int Axis = 0; Axis < 3; ++Axis) Changed |= Entry.Values[Axis] != Entry.Reset[static_cast<size_t>(Axis)];
            for (int Axis = 0; Axis < 3; ++Axis) Entry.Values[Axis] = Entry.Reset[static_cast<size_t>(Axis)];
        }
        const bool Hovered = Enabled && ImGui::IsItemHovered();
        ImGui::PopID();
        DrawList->AddRectFilled(Min, Max, FadeColour(Hovered ? ResolveCssColour(0x2A2A2AFF) : ResolveCssColour(0x202020FF), Fade), 13.0f * S);
        DrawList->AddRect(Min, Max, FadeColour(ResolveCssColour(0xFFFFFF12), Fade), 13.0f * S, ImDrawFlags_None, S);
        const float TextWidth = 15.0f * S + GlyphRun::Measure(Column, Faces.Regular, 9.0f * S, Entry.Label);
        const float Left      = Min.x + 0.5f * (PillWidth - TextWidth);
        GlyphSymbol::Construct(DrawList, "RotateCcw", {Left, Min.y + 8.0f * S}, 10.0f * S, 1.6f, FadeColour(ResolveCssColour(0xCFCFCFFF), Fade));
        (void)GlyphRun::Construct(Column, Faces.Regular, 9.0f * S, ImVec2(Left + 15.0f * S, Min.y + 16.5f * S), FadeColour(ResolveCssColour(0xCFCFCFFF), Fade),
                                  Entry.Label);
    }
    if (!Enabled) ImGui::EndDisabled();
    ImGui::PopID();
    return Changed;
}

} // namespace Frontier
