/*==========================================================================================================================================
                                                               CHOICEROW.CPP
==========================================================================================================================================*/
// 🧩 A label (with an optional note beside it) over equal segments, one chosen: the Render settings page's trace resolution,
//    quality grade, budget and view selectors.

#include "Engine/Interface/Controls/ChoiceRow.h"

#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/GlyphRun.h"
#include "Engine/Interface/InspectorControls.h"

#include <imgui_internal.h>

#include <algorithm>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CHOICE ROW
//------------------------------------------------------------------------------------------------------------------------

bool ChoiceRow::Construct(ControlColumn& Column, const char* Id, const char* Label, const char* const* Options, int Count, int& InOutIndex,
                          bool Enabled, const char* Note, const char* Tooltip)
{
    const ControlTheme&    Theme    = Column.QueryTheme();
    const EditorTypefaces& Faces    = Column.QueryFaces();
    ImDrawList* const      DrawList = Column.QueryDrawList();
    const float            S        = Column.QueryScale();
    const float            Width    = Column.QueryWidth();
    if (Count <= 0) return false;
    const float Fade = Enabled ? 1.0f : 0.45f;

    // 📝 The label line as SliderRow sets it (11 px over a 24 px head), then segments 32 px tall, gap 5 px, radius 13 px.
    const ImVec2 Head = Column.Space(24.0f, 0.0f, 4.0f);
    const float  Baseline = GlyphRun::ResolveBaseline(Head.y + 0.5f * (24.0f - 11.0f * 1.4f) * S, 11.0f * S, 1.4f);
    (void)GlyphRun::Construct(Column, Faces.Regular, 11.0f * S, ImVec2(Head.x, Baseline), FadeColour(Theme.ControlLabel, Fade), Label);
    if (Note != nullptr && Note[0] != '\0')
    {
        const float Advance = GlyphRun::Measure(Column, Faces.Regular, 9.0f * S, Note);
        (void)GlyphRun::Construct(Column, Faces.Regular, 9.0f * S, ImVec2(Head.x + Width - Advance, Baseline), FadeColour(Theme.Muted, Fade), Note);
    }

    const ImVec2 Top  = Column.Space(32.0f, 0.0f, 14.0f);
    const float  Gap  = 5.0f * S;
    const float  Wide = (Width - Gap * static_cast<float>(Count - 1)) / static_cast<float>(Count);
    const int    Before = InOutIndex;
    ImGui::PushID(Id);
    for (int Index = 0; Index < Count; ++Index)
    {
        const ImVec2 Min(Top.x + static_cast<float>(Index) * (Wide + Gap), Top.y);
        const ImVec2 Max(Min.x + Wide, Min.y + 32.0f * S);
        ImGui::SetCursorScreenPos(Min);
        ImGui::PushID(Index);
        if (!Enabled) ImGui::BeginDisabled();
        if (ImGui::InvisibleButton("##Choice", ImVec2(Wide, 32.0f * S), ImGuiButtonFlags_EnableNav) && Enabled) InOutIndex = Index;
        if (!Enabled) ImGui::EndDisabled();
        const bool Hovered = Enabled && ImGui::IsItemHovered();
        if (Tooltip != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ControlColumn::ConstructTooltip(Tooltip);
        ImGui::PopID();
        const bool Chosen = Index == InOutIndex;
        DrawList->AddRectFilled(Min, Max, FadeColour(Chosen ? Theme.PresetPressedFill : (Hovered ? Theme.TabHoverFill : Theme.PresetFill), Fade), 13.0f * S);
        DrawList->AddRect(Min, Max, FadeColour(Chosen ? Theme.PresetPressedBorder : Theme.PresetBorder, Fade), 13.0f * S, ImDrawFlags_None, S);
        const float Advance = GlyphRun::Measure(Column, Faces.Regular, 10.0f * S, Options[Index]);
        (void)GlyphRun::Construct(Column, Faces.Regular, 10.0f * S,
                                  ImVec2(Min.x + 0.5f * (Wide - Advance),
                                         Min.y + 0.5f * (32.0f * S - GlyphRun::EmPixelHeight * 10.0f * S) + GlyphRun::EmAscent * 10.0f * S),
                                  FadeColour(Chosen ? Theme.PresetPressedText : Theme.PresetText, Fade), Options[Index]);
    }
    ImGui::PopID();
    return InOutIndex != Before;
}

} // namespace Frontier
