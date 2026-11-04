/*==========================================================================================================================================
                                                               NEXTBUTTON.CPP
==========================================================================================================================================*/
// 🧩 .co-next: the full-width link button at the foot of a section.

#include "Engine/Interface/Controls/NextButton.h"

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
//                                                    NEXT BUTTON
//------------------------------------------------------------------------------------------------------------------------

bool NextButton::Construct(ControlColumn& Column, const char* Id, const char* Label, bool Enabled, const char* Tooltip)
{
    const ControlTheme&    Theme    = Column.QueryTheme();
    const EditorTypefaces& Faces    = Column.QueryFaces();
    ImDrawList* const      DrawList = Column.QueryDrawList();
    const float            Scale    = Column.QueryScale();
    const float            Width    = Column.QueryWidth();
    // 📝 .co-next: full width, ≥ 42 px, radius 18 px, 10 px, margin 4 px 0 12 px.
    const float  S   = Scale;
    const ImVec2 Top = Column.Space(42.0f, 4.0f, 12.0f);
    const ImVec2 Max(Top.x + Width, Top.y + 42.0f * S);
    ImGui::SetCursorScreenPos(Top);
    if (!Enabled) ImGui::BeginDisabled();
    const bool Pressed = ImGui::InvisibleButton(Id, ImVec2(Width, 42.0f * S), ImGuiButtonFlags_EnableNav);
    if (!Enabled) ImGui::EndDisabled();
    ControlColumn::ConstructTooltip(Tooltip);
    const bool Hovered = ImGui::IsItemHovered();
    DrawList->AddRectFilled(Top, Max, Hovered ? Theme.NextHoverFill : Theme.NextFill, 18.0f * S);
    DrawList->AddRect(Top, Max, Hovered ? Theme.NextHoverBorder : Theme.NextBorder, 18.0f * S, ImDrawFlags_None, S);
    const float Advance = GlyphRun::Measure(Column, Faces.Regular, 10.0f * S, Label);
    (void)GlyphRun::Construct(Column, Faces.Regular, 10.0f * S,
                            ImVec2(Top.x + 0.5f * (Width - Advance), Top.y + 0.5f * (42.0f * S - GlyphRun::EmPixelHeight * 10.0f * S) + GlyphRun::EmAscent * 10.0f * S),
                            FadeColour(Theme.NextText, Enabled ? 1.0f : 0.45f), Label);
    return Pressed;
}

} // namespace Frontier
