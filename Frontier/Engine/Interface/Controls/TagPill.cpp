/*==========================================================================================================================================
                                                                TAGPILL.CPP
==========================================================================================================================================*/
// 🧩 .co-tag (and the pressable .so-mode pill).

#include "Engine/Interface/Controls/TagPill.h"

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
//                                                    TAG PILL
//------------------------------------------------------------------------------------------------------------------------

float TagPill::Measure(const ControlColumn& Column, const char* Text, float FontSize, float PadX)
{
    const EditorTypefaces& Faces    = Column.QueryFaces();
    const float            Scale    = Column.QueryScale();
    return GlyphRun::Measure(Column, Faces.Regular, FontSize * Scale, Text) + (2.0f * PadX + 2.0f) * Scale;
}

ImVec2 TagPill::ConstructAt(ControlColumn& Column, const char* Id, ImVec2 Min, const char* Text, bool Pressable, bool* Pressed, float FontSize,
                            float PadX, float PadY, float MinHeight, ImU32 Fill, ImU32 TextColour, const char* Tooltip)
{
    const ControlTheme&    Theme    = Column.QueryTheme();
    const EditorTypefaces& Faces    = Column.QueryFaces();
    ImDrawList* const      DrawList = Column.QueryDrawList();
    const float            Scale    = Column.QueryScale();
    const float  S    = Scale;
    const float  Wide = TagPill::Measure(Column, Text, FontSize, PadX);
    const float  Tall = std::max(MinHeight, 2.0f * PadY + FontSize * (Pressable ? GlyphRun::EmPixelHeight : 1.4f) + 2.0f) * S;
    const ImVec2 Max(Min.x + Wide, Min.y + Tall);
    bool         Hovered = false;
    if (Pressable)
    {
        ImGui::SetCursorScreenPos(Min);
        const bool Clicked = ImGui::InvisibleButton(Id, ImVec2(Wide, Tall));
        if (Pressed != nullptr) *Pressed = Clicked;
        Hovered = ImGui::IsItemHovered();
        ControlColumn::ConstructTooltip(Tooltip);
    }
    DrawList->AddRectFilled(Min, Max, Fill != 0 ? Fill : (Hovered ? Theme.ModeHoverFill : Theme.TagFill), 20.0f * S);
    DrawList->AddRect(Min, Max, Theme.TagBorder, 20.0f * S, ImDrawFlags_None, S);
    (void)GlyphRun::Construct(Column, Faces.Regular, FontSize * S,
                            ImVec2(Min.x + (PadX + 1.0f) * S, Min.y + 0.5f * (Tall - GlyphRun::EmPixelHeight * FontSize * S) + GlyphRun::EmAscent * FontSize * S),
                            TextColour != 0 ? TextColour : Theme.TagText, Text);
    return ImVec2(Wide, Tall);
}

} // namespace Frontier
