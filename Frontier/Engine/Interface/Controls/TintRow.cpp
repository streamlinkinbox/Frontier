/*==========================================================================================================================================
                                                                TINTROW.CPP
==========================================================================================================================================*/
// 🧩 .co-data-row.so-tint-row: a label and the tint pill (dot, hex, ↗) opening the colour picker.

#include "Engine/Interface/Controls/TintRow.h"

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
//                                                    TINT ROW
//------------------------------------------------------------------------------------------------------------------------

bool TintRow::Construct(ControlColumn& Column, const char* Id, const char* Label, float (&InOutLinear)[3], bool Enabled, const char* Tooltip)
{
    const ControlTheme&    Theme    = Column.QueryTheme();
    const EditorTypefaces& Faces    = Column.QueryFaces();
    ImDrawList* const      DrawList = Column.QueryDrawList();
    const float            Scale    = Column.QueryScale();
    const float            Width    = Column.QueryWidth();
    // 📝 .so-tint-row: the pill (padding 8 px 12 px, ≥ 145 px, radius 999 px) with a 16 px dot, the sRGB hex and ↗.
    const float  S     = Scale;
    const float  Pill  = 8.0f + 11.0f * 1.4f + 8.0f + 2.0f;
    const ImVec2 Top   = Column.Space(1.0f + 11.0f + Pill + 11.0f, 0.0f, 0.0f);
    const float  Fade  = Enabled ? 1.0f : 0.45f;
    DrawList->AddLine(ImVec2(Top.x, Top.y + 0.5f * S), ImVec2(Top.x + Width, Top.y + 0.5f * S), Theme.FieldRowRule, S);
    const float RowMid = Top.y + (1.0f + 11.0f + 0.5f * Pill) * S;
    (void)GlyphRun::Construct(Column, Faces.Regular, 10.0f * S, ImVec2(Top.x, RowMid - 0.5f * GlyphRun::EmPixelHeight * 10.0f * S + GlyphRun::EmAscent * 10.0f * S),
                            FadeColour(Theme.FieldRowText, Fade), Label);
    // 📝 Linear → sRGB the demo's way: channel^(1/2.2).
    int Channels[3] = {};
    for (int Channel = 0; Channel < 3; ++Channel)
    {
        Channels[Channel] = static_cast<int>(std::lround(std::pow(std::clamp(InOutLinear[Channel], 0.0f, 1.0f), 1.0f / 2.2f) * 255.0f));
    }
    char Hex[16] = {};
    std::snprintf(Hex, sizeof(Hex), "#%02X%02X%02X", Channels[0], Channels[1], Channels[2]);
    const float  PillWide = std::max(145.0f * S, GlyphRun::Measure(Column, Faces.Regular, 11.0f * S, Hex) + (12.0f + 16.0f + 9.0f + 9.0f + 12.0f + 12.0f) * S);
    const ImVec2 PillMin(Top.x + Width - PillWide, RowMid - 0.5f * Pill * S);
    const ImVec2 PillMax(Top.x + Width, RowMid + 0.5f * Pill * S);
    bool         Changed = false;
    ImGui::PushID(Id);
    ImGui::SetCursorScreenPos(PillMin);
    if (!Enabled) ImGui::BeginDisabled();
    if (ImGui::InvisibleButton("##Tint", ImVec2(PillWide, Pill * S)) && Enabled) ImGui::OpenPopup("##TintPicker");
    if (!Enabled) ImGui::EndDisabled();
    ControlColumn::ConstructTooltip(Tooltip);
    if (ImGui::BeginPopup("##TintPicker"))
    {
        float Picked[3] = {static_cast<float>(Channels[0]) / 255.0f, static_cast<float>(Channels[1]) / 255.0f, static_cast<float>(Channels[2]) / 255.0f};
        if (ImGui::ColorPicker3("##TintColour", Picked, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_DisplayHex))
        {
            for (int Channel = 0; Channel < 3; ++Channel) InOutLinear[Channel] = std::pow(std::clamp(Picked[Channel], 0.0f, 1.0f), 2.2f);
            Changed = true;
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    DrawList->AddRectFilled(PillMin, PillMax, FadeColour(Theme.TagFill, Fade), 999.0f);
    DrawList->AddRect(PillMin, PillMax, FadeColour(ResolveCssColour(0xE8C78B30), Fade), 999.0f, ImDrawFlags_None, S);
    const ImVec2 Dot(PillMin.x + (13.0f + 8.0f) * S, RowMid);
    DrawList->AddCircleFilled(Dot, 11.0f * S, FadeColour(ResolveCssColour(0xFFFFFF0A), Fade), 32);
    DrawList->AddCircleFilled(Dot, 8.0f * S, FadeColour(IM_COL32(Channels[0], Channels[1], Channels[2], 255), Fade), 32);
    const float TextBaseline = RowMid - 0.5f * GlyphRun::EmPixelHeight * 11.0f * S + GlyphRun::EmAscent * 11.0f * S;
    (void)GlyphRun::Construct(Column, Faces.Regular, 11.0f * S, ImVec2(Dot.x + (8.0f + 9.0f) * S, TextBaseline), FadeColour(ResolveCssColour(0xE5D8BAFF), Fade), Hex);
    const float ArrowWide = GlyphRun::Measure(Column, Faces.Regular, 11.0f * S, "\xE2\x86\x97");
    (void)GlyphRun::Construct(Column, Faces.Regular, 11.0f * S, ImVec2(PillMax.x - 13.0f * S - ArrowWide, TextBaseline), FadeColour(ResolveCssColour(0xE5D8BAFF), Fade),
                            "\xE2\x86\x97");
    return Changed;
}

} // namespace Frontier
