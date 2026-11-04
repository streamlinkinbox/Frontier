/*==========================================================================================================================================
                                                              PAGEHEADING.CPP
==========================================================================================================================================*/
// 🧩 .co-header: the eyebrow, the h1 title with its title-line pill (Reset ↗ / ON) and the subtitle.

#include "Engine/Interface/Controls/PageHeading.h"

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
//                                                    PAGE HEADING
//------------------------------------------------------------------------------------------------------------------------

bool PageHeading::Construct(ControlColumn &Column,
                            const char    *Id,
                            const char    *Eyebrow,
                            const char    *Title,
                            const char    *ActionLabel,
                            const char    *Subtitle,
                            const char    *ActionTooltip)
{
    const ControlTheme    &Theme    = Column.QueryTheme();
    const EditorTypefaces &Faces    = Column.QueryFaces();
    ImDrawList *const      DrawList = Column.QueryDrawList();
    const float            Scale    = Column.QueryScale();
    const float            Width    = Column.QueryWidth();
    const float            S        = Scale;
    // 📝 .co-eyebrow: 9 px, letter-spacing 1.7 px, margin-bottom 24 px.
    const ImVec2 EyebrowTop = Column.Space(9.0f * 1.4f, 0.0f, Theme.TitleGap);
    (void)GlyphRun::Construct(Column, Faces.Regular, 9.0f * S,
                              ImVec2(EyebrowTop.x, GlyphRun::ResolveBaseline(EyebrowTop.y, 9.0f * S, 1.4f)), Theme.Eyebrow, Eyebrow,
                              Theme.EyebrowTracking * S);

    // 📝 .co-title-line: h1 31 px / 1.15, weight 400, letter-spacing −1 px; the pill (.so-reset) margin-top 5 px, 28 px tall.
    const float  TitleLine = std::max(Theme.TitleSize * Theme.TitleLineHeight, Theme.ActionOffset + Theme.ActionHeight);
    const ImVec2 TitleTop  = Column.Space(TitleLine, 0.0f, 0.0f);
    (void)GlyphRun::Construct(Column, Faces.Regular, Theme.TitleSize * S,
                              ImVec2(TitleTop.x, GlyphRun::ResolveBaseline(TitleTop.y, Theme.TitleSize * S, Theme.TitleLineHeight)),
                              Theme.Ink, Title, -1.0f * S);
    bool Pressed = false;
    if (ActionLabel != nullptr)
    {
        const float PillWidth =
            GlyphRun::Measure(Column, Faces.Regular, Theme.ActionFontSize * S, ActionLabel) + 2 * Theme.ActionPadding * S;
        const ImVec2 PillMin(TitleTop.x + Width - PillWidth, TitleTop.y + Theme.ActionOffset * S);
        const ImVec2 PillMax(PillMin.x + PillWidth, PillMin.y + Theme.ActionHeight * S);
        ImGui::SetCursorScreenPos(PillMin);
        ImGui::PushID(Id);
        Pressed = ImGui::InvisibleButton("##TitleAction", ImVec2(PillWidth, Theme.ActionHeight * S), ImGuiButtonFlags_EnableNav);
        ImGui::PopID();
        ControlColumn::ConstructTooltip(ActionTooltip);
        const bool Hovered = ImGui::IsItemHovered();
        DrawList->AddRectFilled(PillMin, PillMax, Hovered ? Theme.ModeHoverFill : Theme.ModeFill, 22.0f * S);
        DrawList->AddRect(PillMin, PillMax, Theme.ModeBorder, 22.0f * S, ImDrawFlags_None, S);
        (void)GlyphRun::Construct(Column, Faces.Regular, Theme.ActionFontSize * S,
                                  ImVec2(PillMin.x + Theme.ActionPadding * S,
                                         PillMin.y + 0.5f * (Theme.ActionHeight * S - GlyphRun::EmPixelHeight * Theme.ActionFontSize * S) +
                                             GlyphRun::EmAscent * Theme.ActionFontSize * S),
                                  Theme.ModeText, ActionLabel);
    }

    // 📝 .co-subtitle: 10 px, margin-top 15 px; .co-header margin-bottom 20 px.
    const float SubtitleWidth =
        Theme.SubtitleMaxWidth > 0 ? std::min(Width * Theme.SubtitleFraction, Theme.SubtitleMaxWidth * S) : Width * Theme.SubtitleFraction;
    const float SubtitleHeight = ParagraphBlock::Construct(Column, Faces.Regular, Theme.SubtitleSize * S, Theme.SubtitleLineHeight,
                                                           Column.QueryCursor(), SubtitleWidth, Theme.Subtitle, Subtitle, true) /
                                 S;
    const ImVec2 SubtitleTop = Column.Space(SubtitleHeight, Theme.SubtitleGap, 20.0f);
    (void)ParagraphBlock::Construct(Column, Faces.Regular, Theme.SubtitleSize * S, Theme.SubtitleLineHeight, SubtitleTop, SubtitleWidth,
                                    Theme.Subtitle, Subtitle);
    return Pressed;
}

} // namespace Frontier
