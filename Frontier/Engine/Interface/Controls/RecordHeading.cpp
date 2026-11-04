/*==========================================================================================================================================
                                                             RECORDHEADING.CPP
==========================================================================================================================================*/
// 🧩 The Project Zero inspector heading (.inspector-heading): the breadcrumb, the dashed notes pill, the title and the
//    subtitle of one record's page.

#include "Engine/Interface/Controls/RecordHeading.h"

#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/GlyphRun.h"
#include "Engine/Interface/Controls/GlyphSymbol.h"
#include "Engine/Interface/InspectorControls.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Frontier
{

namespace
{

// 📝 A dashed rounded rectangle (border: 1px dashed): the outline walked as one polyline, emitted Dash on, Gap off.
void RasterizeDashedOutline(ImDrawList* DrawList, ImVec2 Min, ImVec2 Max, float Radius, ImU32 Colour, float Dash, float Gap, float Thickness)
{
    DrawList->PathRect(Min, Max, Radius, ImDrawFlags_RoundCornersAll);
    std::vector<ImVec2> Outline(DrawList->_Path.Data, DrawList->_Path.Data + DrawList->_Path.Size);
    DrawList->PathClear();
    if (Outline.size() < 2) return;
    Outline.push_back(Outline.front());
    bool  Drawing  = true;
    float Remain   = Dash;
    for (size_t Index = 0; Index + 1 < Outline.size(); ++Index)
    {
        ImVec2       From   = Outline[Index];
        const ImVec2 To     = Outline[Index + 1];
        float        Length = std::hypot(To.x - From.x, To.y - From.y);
        while (Length > 0.0f)
        {
            const float  Step = std::min(Remain, Length);
            const float  T    = Step / Length;
            const ImVec2 Next(From.x + (To.x - From.x) * T, From.y + (To.y - From.y) * T);
            if (Drawing) DrawList->AddLine(From, Next, Colour, Thickness);
            From = Next;
            Length -= Step;
            Remain -= Step;
            if (Remain <= 0.0f)
            {
                Drawing = !Drawing;
                Remain  = Drawing ? Dash : Gap;
            }
        }
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    RECORD HEADING
//------------------------------------------------------------------------------------------------------------------------

bool RecordHeading::Construct(ControlColumn& Column, const char* Id, const char* Breadcrumb, const char* Title, const char* Subtitle, const char* PillLabel,
                              const char* PillGlyph)
{
    const EditorTypefaces& Faces    = Column.QueryFaces();
    ImDrawList* const      DrawList = Column.QueryDrawList();
    const float            S        = Column.QueryScale();
    const float            Width    = Column.QueryWidth();
    const ImVec2           Top      = Column.Space(110.0f, 0.0f, 4.0f);

    (void)GlyphRun::Construct(Column, Faces.Regular, 9.0f * S, ImVec2(Top.x, GlyphRun::ResolveBaseline(Top.y, 9.0f * S, 14.0f / 9.0f)),
                              ResolveCssColour(0x919191FF), Breadcrumb);
    bool Pressed = false;
    if (PillLabel != nullptr)
    {
        // 📝 .entity-notes-add: min-height 30 px, padding 5 px 9 px, 1 px dashed #424242, 9 px #898989, the glyph 13 px, gap 6 px.
        const float  LabelWidth = GlyphRun::Measure(Column, Faces.Regular, 9.0f * S, PillLabel);
        const float  PillWidth  = 9.0f * S + 13.0f * S + 6.0f * S + LabelWidth + 9.0f * S;
        const ImVec2 PillMin(Top.x + Width - PillWidth, Top.y);
        const ImVec2 PillMax(PillMin.x + PillWidth, PillMin.y + 30.0f * S);
        ImGui::SetCursorScreenPos(PillMin);
        ImGui::PushID(Id);
        Pressed = ImGui::InvisibleButton("##HeadingPill", ImVec2(PillWidth, 30.0f * S), ImGuiButtonFlags_EnableNav);
        ImGui::PopID();
        const bool  Hovered = ImGui::IsItemHovered();
        const ImU32 Ink     = Hovered ? ResolveCssColour(0xD2D2D2FF) : ResolveCssColour(0x898989FF);
        RasterizeDashedOutline(DrawList, PillMin, PillMax, 15.0f * S, Hovered ? ResolveCssColour(0x676767FF) : ResolveCssColour(0x424242FF), 3.0f * S,
                               3.0f * S, S);
        GlyphSymbol::Construct(DrawList, PillGlyph, {PillMin.x + 9.0f * S, PillMin.y + 8.5f * S}, 13.0f * S, 1.5f, Ink);
        (void)GlyphRun::Construct(Column, Faces.Regular, 9.0f * S,
                                  ImVec2(PillMin.x + 28.0f * S, PillMin.y + 15.0f * S - 0.5f * GlyphRun::EmPixelHeight * 9.0f * S + GlyphRun::EmAscent * 9.0f * S),
                                  Ink, PillLabel);
    }
    (void)GlyphRun::Construct(Column, Faces.Regular, 25.0f * S, ImVec2(Top.x, GlyphRun::ResolveBaseline(Top.y + 41.0f * S, 25.0f * S, 1.2f)),
                              ResolveCssColour(0xF0F0F0FF), Title, -0.4f * S);
    if (Subtitle != nullptr && Subtitle[0] != '\0')
        (void)ParagraphBlock::Construct(Column, Faces.Regular, 10.0f * S, 1.5f, ImVec2(Top.x, Top.y + 80.0f * S), Width, ResolveCssColour(0x8A8A8AFF),
                                        Subtitle);
    return Pressed;
}

} // namespace Frontier
