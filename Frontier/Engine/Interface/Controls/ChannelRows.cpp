/*==========================================================================================================================================
                                                              CHANNELROWS.CPP
==========================================================================================================================================*/
// 🧩 The Project Zero material channel list (.channel-list): one row per channel with its swatch or glyph, name, source,
//    value and mode.

#include "Engine/Interface/Controls/ChannelRows.h"

#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/GlyphRun.h"
#include "Engine/Interface/InspectorControls.h"

#include <imgui.h>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CHANNEL ROWS
//------------------------------------------------------------------------------------------------------------------------

int ChannelRows::Construct(ControlColumn& Column, const char* Id, const ChannelRow* Rows, int Count, int Selected)
{
    // 📝 .channel-list > button: min-height 40 px (compact), gap 4 px, radius 12 px, padding 6 px; the active row #ffffff09 over
    //    #c7b29744; swatch 20 px; name 10 px #bbb over its source 8 px; value 9 px over the mode 8 px, right-aligned.
    const EditorTypefaces& Faces    = Column.QueryFaces();
    ImDrawList* const      DrawList = Column.QueryDrawList();
    const float            S        = Column.QueryScale();
    const float            Width    = Column.QueryWidth();
    constexpr float        Tall = 40.0f, Gap = 4.0f;
    const ImVec2 Top     = Column.Space(Tall * static_cast<float>(Count) + Gap * static_cast<float>(Count - 1), 0.0f, 12.0f);
    int          Pressed = -1;
    ImGui::PushID(Id);
    for (int Index = 0; Index < Count; ++Index)
    {
        const ChannelRow& Row = Rows[Index];
        const ImVec2      Min(Top.x, Top.y + static_cast<float>(Index) * (Tall + Gap) * S);
        const ImVec2      Max(Min.x + Width, Min.y + Tall * S);
        ImGui::SetCursorScreenPos(Min);
        ImGui::PushID(Index);
        if (ImGui::InvisibleButton("##Channel", ImVec2(Width, Tall * S))) Pressed = Index;
        const bool Hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        if (Index == Selected)
        {
            DrawList->AddRectFilled(Min, Max, ResolveCssColour(0xFFFFFF09), 12.0f * S);
            DrawList->AddRect(Min, Max, ResolveCssColour(0xC7B29744), 12.0f * S, ImDrawFlags_None, S);
        }
        else if (Hovered)
        {
            DrawList->AddRectFilled(Min, Max, ResolveCssColour(0xFFFFFF06), 12.0f * S);
        }
        const ImVec2 SwatchMin(Min.x + 8.0f * S, Min.y + 10.0f * S);
        const ImVec2 SwatchMax(SwatchMin.x + 20.0f * S, SwatchMin.y + 20.0f * S);
        if (Row.Swatch != 0)
        {
            DrawList->AddRectFilled(SwatchMin, SwatchMax, ResolveCssColour(Row.Swatch), 6.0f * S);
            DrawList->AddRect(SwatchMin, SwatchMax, ResolveCssColour(0xFFFFFF1A), 6.0f * S, ImDrawFlags_None, S);
        }
        else
        {
            // 📝 The scalar glyph: a diamond in a ring, as the reference's channel icon.
            const ImVec2 Centre(0.5f * (SwatchMin.x + SwatchMax.x), 0.5f * (SwatchMin.y + SwatchMax.y));
            DrawList->AddRect(SwatchMin, SwatchMax, ResolveCssColour(0xFFFFFF22), 6.0f * S, ImDrawFlags_None, S);
            DrawList->AddQuad(ImVec2(Centre.x, Centre.y - 5.0f * S), ImVec2(Centre.x + 5.0f * S, Centre.y), ImVec2(Centre.x, Centre.y + 5.0f * S),
                              ImVec2(Centre.x - 5.0f * S, Centre.y), ResolveCssColour(0xBDBDBDFF), 1.4f * S);
        }
        (void)GlyphRun::Construct(Column, Faces.Regular, 10.0f * S, ImVec2(Min.x + 38.0f * S, Min.y + 18.0f * S), ResolveCssColour(0xBBBBBBFF), Row.Name);
        (void)GlyphRun::Construct(Column, Faces.Regular, 8.0f * S, ImVec2(Min.x + 38.0f * S, Min.y + 31.0f * S), ResolveCssColour(0x6E6E6EFF), Row.Source);
        const float ValueWidth = GlyphRun::Measure(Column, Faces.Regular, 9.0f * S, Row.Value);
        (void)GlyphRun::Construct(Column, Faces.Regular, 9.0f * S, ImVec2(Max.x - 10.0f * S - ValueWidth, Min.y + 18.0f * S), ResolveCssColour(0xD0D0D0FF),
                                  Row.Value);
        const float ModeWidth = GlyphRun::Measure(Column, Faces.Regular, 8.0f * S, Row.Mode);
        (void)GlyphRun::Construct(Column, Faces.Regular, 8.0f * S, ImVec2(Max.x - 10.0f * S - ModeWidth, Min.y + 31.0f * S), ResolveCssColour(0x6E6E6EFF),
                                  Row.Mode);
    }
    ImGui::PopID();
    return Pressed;
}

} // namespace Frontier
