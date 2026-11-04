/*==========================================================================================================================================
                                                             CARDTITLEROW.CPP
==========================================================================================================================================*/
// 🧩 The title line of a Project Zero card (.transform-card header, .fracture-card header): the light title, an optional
//    tracked caption under or beside it, and an optional round action at the right.

#include "Engine/Interface/Controls/CardTitleRow.h"

#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/GlyphRun.h"
#include "Engine/Interface/Controls/GlyphSymbol.h"
#include "Engine/Interface/InspectorControls.h"

#include <imgui.h>

#include <algorithm>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CARD TITLE ROW
//------------------------------------------------------------------------------------------------------------------------

bool CardTitleRow::Construct(ControlColumn& Column, const char* Id, const char* Title, const CardTitleLook& Look)
{
    const EditorTypefaces& Faces    = Column.QueryFaces();
    ImDrawList* const      DrawList = Column.QueryDrawList();
    const float            S        = Column.QueryScale();
    const float            Width    = Column.QueryWidth();
    const bool             Under    = Look.Caption != nullptr && !Look.CaptionBeside;
    const float            Tall     = std::max(Look.ActionGlyph != nullptr ? 30.0f : 0.0f, Look.TitleSize * 1.3f + (Under ? 16.0f : 0.0f));
    const ImVec2           Top      = Column.Space(Tall, 0.0f, Look.MarginBottom);
    float                  Indent   = 0.0f;
    if (Look.Glyph != nullptr)
    {
        GlyphSymbol::Construct(DrawList, Look.Glyph, {Top.x, Top.y + (Look.TitleSize * 1.3f - 16.0f) * 0.5f * S}, 16.0f * S, 1.5f, ResolveCssColour(0xBDBDBDFF));
        Indent = 24.0f;
    }
    (void)GlyphRun::Construct(Column, Faces.Light, Look.TitleSize * S,
                              ImVec2(Top.x + Indent * S, GlyphRun::ResolveBaseline(Top.y, Look.TitleSize * S, 1.3f)), ResolveCssColour(0xDDDDDDFF), Title,
                              -0.2f * S);
    const float ActionWidth = Look.ActionGlyph != nullptr ? 30.0f * S : 0.0f;
    if (Look.Caption != nullptr)
    {
        if (Under)
        {
            (void)GlyphRun::Construct(Column, Faces.Regular, 9.0f * S,
                                      ImVec2(Top.x + Indent * S, GlyphRun::ResolveBaseline(Top.y + (Look.TitleSize * 1.3f + 4.0f) * S, 9.0f * S, 1.3f)),
                                      ResolveCssColour(0x686868FF), Look.Caption, 1.2f * S);
        }
        else
        {
            const float Advance = GlyphRun::Measure(Column, Faces.Regular, 8.0f * S, Look.Caption, 1.0f * S);
            (void)GlyphRun::Construct(Column, Faces.Regular, 8.0f * S,
                                      ImVec2(Top.x + Width - ActionWidth - (ActionWidth > 0 ? 10.0f * S : 0.0f) - Advance,
                                             GlyphRun::ResolveBaseline(Top.y + (Look.TitleSize * 1.3f - 8.0f * 1.3f) * 0.5f * S, 8.0f * S, 1.3f)),
                                      ResolveCssColour(0x777777FF), Look.Caption, 1.0f * S);
        }
    }
    bool Pressed = false;
    if (Look.ActionGlyph != nullptr)
    {
        const ImVec2 Min(Top.x + Width - ActionWidth, Top.y);
        ImGui::SetCursorScreenPos(Min);
        ImGui::PushID(Id);
        if (!Look.ActionEnabled) ImGui::BeginDisabled();
        Pressed = ImGui::InvisibleButton("##CardAction", ImVec2(ActionWidth, ActionWidth), ImGuiButtonFlags_EnableNav) && Look.ActionEnabled;
        if (!Look.ActionEnabled) ImGui::EndDisabled();
        ControlColumn::ConstructTooltip(Look.ActionTooltip);
        ImGui::PopID();
        const bool   Hovered = Look.ActionEnabled && ImGui::IsItemHovered();
        const ImVec2 Centre(Min.x + 0.5f * ActionWidth, Min.y + 0.5f * ActionWidth);
        DrawList->AddCircleFilled(Centre, 0.5f * ActionWidth, Hovered ? ResolveCssColour(0x2A2A2AFF) : ResolveCssColour(0x202020FF), 32);
        DrawList->AddCircle(Centre, 0.5f * ActionWidth - 0.5f * S, ResolveCssColour(0xFFFFFF12), 32, S);
        GlyphSymbol::Construct(DrawList, Look.ActionGlyph, {Centre.x - 7.0f * S, Centre.y - 7.0f * S}, 14.0f * S, 1.5f,
                               FadeColour(ResolveCssColour(0xD0D0D0FF), Look.ActionEnabled ? 1.0f : 0.4f));
    }
    return Pressed;
}

} // namespace Frontier
