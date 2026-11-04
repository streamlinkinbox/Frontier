/*==========================================================================================================================================
                                                               TOGGLEROW.CPP
==========================================================================================================================================*/
// 🧩 .co-toggle: a label and ON / OFF.

#include "Engine/Interface/Controls/ToggleRow.h"

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
//                                                    TOGGLE ROW
//------------------------------------------------------------------------------------------------------------------------

bool ToggleRow::Construct(ControlColumn& Column, const char* Id, const char* Label, bool& InOutEnabled, bool Enabled, const char* Tooltip)
{
    const ControlTheme&    Theme    = Column.QueryTheme();
    const EditorTypefaces& Faces    = Column.QueryFaces();
    ImDrawList* const      DrawList = Column.QueryDrawList();
    const float            Scale    = Column.QueryScale();
    const float            Width    = Column.QueryWidth();
    // 📝 .co-toggle: space-between, padding 11 px 12 px, radius 12 px, margin-bottom 12 px, 11 px label, 10 px ON / OFF.
    const float  S    = Scale;
    const float  Tall = 1.0f + 11.0f + 11.0f * GlyphRun::EmPixelHeight + 11.0f + 1.0f;
    const ImVec2 Top  = Column.Space(Tall, 0.0f, 12.0f);
    const ImVec2 Max(Top.x + Width, Top.y + Tall * S);
    const float  Fade    = Enabled ? 1.0f : 0.45f;
    bool         Flipped = false;
    ImGui::PushID(Id);
    ImGui::SetCursorScreenPos(Top);
    if (!Enabled) ImGui::BeginDisabled();
    if (ImGui::InvisibleButton("##Toggle", ImVec2(Width, Tall * S), ImGuiButtonFlags_EnableNav) && Enabled)
    {
        InOutEnabled = !InOutEnabled;
        Flipped      = true;
    }
    if (!Enabled) ImGui::EndDisabled();
    ControlColumn::ConstructTooltip(Tooltip);
    ImGui::PopID();
    DrawList->AddRectFilled(Top, Max, FadeColour(Theme.ToggleFill, Fade), 12.0f * S);
    DrawList->AddRect(Top, Max, FadeColour(Theme.ToggleBorder, Fade), 12.0f * S, ImDrawFlags_None, S);
    const float Mid = 0.5f * (Top.y + Max.y);
    (void)GlyphRun::Construct(Column, Faces.Regular, 11.0f * S, ImVec2(Top.x + 13.0f * S, Mid - 0.5f * GlyphRun::EmPixelHeight * 11.0f * S + GlyphRun::EmAscent * 11.0f * S),
                            FadeColour(Theme.ToggleText, Fade), Label);
    const char* Reading = InOutEnabled ? "ON" : "OFF";
    const float Advance = GlyphRun::Measure(Column, Faces.Regular, 10.0f * S, Reading);
    (void)GlyphRun::Construct(Column, Faces.Regular, 10.0f * S, ImVec2(Max.x - 13.0f * S - Advance, Mid - 0.5f * GlyphRun::EmPixelHeight * 10.0f * S + GlyphRun::EmAscent * 10.0f * S),
                            FadeColour(InOutEnabled ? Theme.ToggleCaptionOn : Theme.ToggleCaption, Fade), Reading);
    return Flipped;
}

} // namespace Frontier
