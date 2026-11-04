/*==========================================================================================================================================
                                                              PRESETSTRIP.CPP
==========================================================================================================================================*/
// 🧩 .so-presets: equal preset buttons, one pressed.

#include "Engine/Interface/Controls/PresetStrip.h"

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
//                                                    PRESET STRIP
//------------------------------------------------------------------------------------------------------------------------

int PresetStrip::Construct(ControlColumn& Column, const char* Id, const char* const* Labels, int Count, int PressedIndex)
{
    const ControlTheme&    Theme    = Column.QueryTheme();
    const EditorTypefaces& Faces    = Column.QueryFaces();
    ImDrawList* const      DrawList = Column.QueryDrawList();
    const float            Scale    = Column.QueryScale();
    const float            Width    = Column.QueryWidth();
    if (Count <= 0) return -1;
    // 📝 .so-presets: grid of Count, gap 6 px, margin 12 px 0; .so-preset 34 px, radius 15 px, 10 px text.
    const float  S       = Scale;
    const ImVec2 Top     = Column.Space(34.0f, 12.0f, 12.0f);
    const float  Gap     = 6.0f * S;
    const float  Wide    = (Width - Gap * static_cast<float>(Count - 1)) / static_cast<float>(Count);
    int          Chosen  = -1;
    ImGui::PushID(Id);
    for (int Index = 0; Index < Count; ++Index)
    {
        const ImVec2 Min(Top.x + static_cast<float>(Index) * (Wide + Gap), Top.y);
        const ImVec2 Max(Min.x + Wide, Min.y + 34.0f * S);
        ImGui::SetCursorScreenPos(Min);
        ImGui::PushID(Index);
        if (ImGui::InvisibleButton("##Preset", ImVec2(Wide, 34.0f * S))) Chosen = Index;
        const bool Hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        const bool Pressed = Index == PressedIndex;
        DrawList->AddRectFilled(Min, Max, Pressed ? Theme.PresetPressedFill : (Hovered ? Theme.TabHoverFill : Theme.PresetFill), 15.0f * S);
        DrawList->AddRect(Min, Max, Pressed ? Theme.PresetPressedBorder : Theme.PresetBorder, 15.0f * S, ImDrawFlags_None, S);
        const float Advance = GlyphRun::Measure(Column, Faces.Regular, 10.0f * S, Labels[Index]);
        (void)GlyphRun::Construct(Column, Faces.Regular, 10.0f * S,
                                ImVec2(Min.x + 0.5f * (Wide - Advance), Min.y + 0.5f * (34.0f * S - GlyphRun::EmPixelHeight * 10.0f * S) + GlyphRun::EmAscent * 10.0f * S),
                                Pressed ? Theme.PresetPressedText : Theme.PresetText, Labels[Index]);
    }
    ImGui::PopID();
    return Chosen;
}

} // namespace Frontier
