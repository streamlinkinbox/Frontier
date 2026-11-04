/*==========================================================================================================================================
                                                               FOOTERROW.CPP
==========================================================================================================================================*/
// 🧩 .co-footer and .co-atlas-footer: the leading / trailing note rows closing a page.

#include "Engine/Interface/Controls/FooterRow.h"

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
//                                                    FOOTER ROW
//------------------------------------------------------------------------------------------------------------------------

void FooterRow::ConstructAtlas(ControlColumn& Column, const char* Leading, const char* Trailing, float FontSize, float PaddingTop,
                               float PaddingBottom)
{
    const ControlTheme&    Theme    = Column.QueryTheme();
    const EditorTypefaces& Faces    = Column.QueryFaces();
    ImDrawList* const      DrawList = Column.QueryDrawList();
    const float            Scale    = Column.QueryScale();
    const float            Width    = Column.QueryWidth();
    // 📝 .co-atlas-footer: border-top 1 px, space-between, letter-spacing .2 px.
    const float  S   = Scale;
    const ImVec2 Top = Column.Space(1.0f + PaddingTop + FontSize * 1.4f + PaddingBottom, 0.0f, 0.0f);
    DrawList->AddLine(ImVec2(Top.x, Top.y + 0.5f * S), ImVec2(Top.x + Width, Top.y + 0.5f * S), Theme.AtlasFooterRule, S);
    const float Baseline = GlyphRun::ResolveBaseline(Top.y + (1.0f + PaddingTop) * S, FontSize * S, 1.4f);
    (void)GlyphRun::Construct(Column, Faces.Regular, FontSize * S, ImVec2(Top.x, Baseline), Theme.AtlasFooterText, Leading, 0.2f * S);
    const float Advance = GlyphRun::Measure(Column, Faces.Regular, FontSize * S, Trailing, 0.2f * S);
    (void)GlyphRun::Construct(Column, Faces.Regular, FontSize * S, ImVec2(Top.x + Width - Advance, Baseline), Theme.AtlasFooterText, Trailing, 0.2f * S);
}

void FooterRow::Construct(ControlColumn& Column, const char* Leading, const char* Trailing)
{
    const ControlTheme&    Theme    = Column.QueryTheme();
    const EditorTypefaces& Faces    = Column.QueryFaces();
    ImDrawList* const      DrawList = Column.QueryDrawList();
    const float            Scale    = Column.QueryScale();
    const float            Width    = Column.QueryWidth();
    // 📝 .co-footer: border-top 1 px, padding 15 px 2 px 5 px, margin-top 17 px, 8 px, letter-spacing .6 px.
    const float  S   = Scale;
    const ImVec2 Top = Column.Space(1.0f + 15.0f + 8.0f * 1.4f + 5.0f, 17.0f, 0.0f);
    DrawList->AddLine(ImVec2(Top.x, Top.y + 0.5f * S), ImVec2(Top.x + Width, Top.y + 0.5f * S), Theme.FooterRule, S);
    const float Baseline = GlyphRun::ResolveBaseline(Top.y + 16.0f * S, 8.0f * S, 1.4f);
    (void)GlyphRun::Construct(Column, Faces.Regular, 8.0f * S, ImVec2(Top.x + 2.0f * S, Baseline), Theme.FooterText, Leading, 0.6f * S);
    const float Advance = GlyphRun::Measure(Column, Faces.Regular, 8.0f * S, Trailing, 0.6f * S);
    (void)GlyphRun::Construct(Column, Faces.Regular, 8.0f * S, ImVec2(Top.x + Width - 2.0f * S - Advance, Baseline), Theme.FooterText, Trailing, 0.6f * S);
}

} // namespace Frontier
