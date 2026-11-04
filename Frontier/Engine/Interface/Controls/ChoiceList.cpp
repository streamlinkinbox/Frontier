/*==========================================================================================================================================
                                                              CHOICELIST.CPP
==========================================================================================================================================*/
// 🧩 Selectable rows under group headings, one chosen: an index chip, a caption, a dimmed note and an optional tag (the Render
//    settings page's debug views).

#include "Engine/Interface/Controls/ChoiceList.h"

#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/GlyphRun.h"
#include "Engine/Interface/InspectorControls.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CHOICE LIST
//------------------------------------------------------------------------------------------------------------------------

bool ChoiceList::Construct(ControlColumn& Column, const char* Id, std::span<const ChoiceEntry> Entries, int& InOutChosen)
{
    const ControlTheme&    Theme    = Column.QueryTheme();
    const EditorTypefaces& Faces    = Column.QueryFaces();
    ImDrawList* const      DrawList = Column.QueryDrawList();
    const float            S        = Column.QueryScale();
    const float            Width    = Column.QueryWidth();
    const int              Before   = InOutChosen;
    const char*            Group    = nullptr;
    ImGui::PushID(Id);
    for (int Index = 0; Index < static_cast<int>(Entries.size()); ++Index)
    {
        const ChoiceEntry& Entry = Entries[static_cast<size_t>(Index)];
        if (Group == nullptr || std::strcmp(Group, Entry.Group) != 0)
        {
            // 📝 Group heading: 9 px tracked capitals, muted, 16 px above the first row of the group.
            Group = Entry.Group;
            const ImVec2 Heading = Column.Space(14.0f, Index == 0 ? 2.0f : 10.0f, 6.0f);
            char Upper[64]{};
            for (size_t Glyph = 0; Glyph + 1 < sizeof(Upper) && Entry.Group[Glyph] != '\0'; ++Glyph)
                Upper[Glyph] = static_cast<char>(Entry.Group[Glyph] >= 'a' && Entry.Group[Glyph] <= 'z' ? Entry.Group[Glyph] - 32 : Entry.Group[Glyph]);
            (void)GlyphRun::Construct(Column, Faces.Regular, 9.0f * S, ImVec2(Heading.x + 2.0f * S, Heading.y + GlyphRun::EmAscent * 9.0f * S),
                                      Theme.Muted, Upper, 1.2f * S);
        }
        const bool   Noted = Entry.Note != nullptr && Entry.Note[0] != '\0';
        const float  Tall  = Noted ? 46.0f : 34.0f;
        const ImVec2 Min   = Column.Space(Tall, 0.0f, 6.0f);
        const ImVec2 Max(Min.x + Width, Min.y + Tall * S);
        const float  Fade  = Entry.Enabled ? 1.0f : 0.45f;
        ImGui::SetCursorScreenPos(Min);
        ImGui::PushID(Index);
        if (!Entry.Enabled) ImGui::BeginDisabled();
        if (ImGui::InvisibleButton("##Choice", ImVec2(Width, Tall * S), ImGuiButtonFlags_EnableNav) && Entry.Enabled) InOutChosen = Index;
        if (!Entry.Enabled) ImGui::EndDisabled();
        const bool Hovered = Entry.Enabled && ImGui::IsItemHovered();
        ImGui::PopID();
        const bool Chosen = Index == InOutChosen;
        DrawList->AddRectFilled(Min, Max, FadeColour(Chosen ? Theme.PresetPressedFill : (Hovered ? Theme.TabHoverFill : Theme.ToggleFill), Fade), 12.0f * S);
        DrawList->AddRect(Min, Max, FadeColour(Chosen ? Theme.Accent : Theme.ToggleBorder, Chosen ? 0.75f : Fade), 12.0f * S, ImDrawFlags_None,
                          Chosen ? 1.5f * S : S);

        // 📝 Index chip: 26 × 20 px, radius 7 px, at 10 px from the left; caption 11 px, note 9 px under it.
        const ImVec2 ChipMin(Min.x + 10.0f * S, Min.y + 7.0f * S);
        const ImVec2 ChipMax(ChipMin.x + 26.0f * S, ChipMin.y + 20.0f * S);
        DrawList->AddRectFilled(ChipMin, ChipMax, FadeColour(Chosen ? Theme.Accent : Theme.TagFill, Chosen ? 0.9f : Fade), 7.0f * S);
        const float ChipAdvance = GlyphRun::Measure(Column, Faces.Regular, 9.0f * S, Entry.Chip);
        (void)GlyphRun::Construct(Column, Faces.Regular, 9.0f * S,
                                  ImVec2(ChipMin.x + 0.5f * (26.0f * S - ChipAdvance),
                                         ChipMin.y + 0.5f * (20.0f * S - GlyphRun::EmPixelHeight * 9.0f * S) + GlyphRun::EmAscent * 9.0f * S),
                                  Chosen ? Theme.PageFill : FadeColour(Theme.TagText, Fade), Entry.Chip);
        const float TextLeft = ChipMax.x + 10.0f * S;
        (void)GlyphRun::Construct(Column, Faces.Regular, 11.0f * S,
                                  ImVec2(TextLeft, Min.y + 0.5f * (34.0f * S - GlyphRun::EmPixelHeight * 11.0f * S) + GlyphRun::EmAscent * 11.0f * S),
                                  FadeColour(Chosen ? Theme.Ink : Theme.ToggleText, Fade), Entry.Caption);
        if (Noted)
            (void)GlyphRun::Construct(Column, Faces.Regular, 9.0f * S, ImVec2(TextLeft, Min.y + 30.0f * S + GlyphRun::EmAscent * 9.0f * S),
                                      FadeColour(Theme.Muted, Fade), Entry.Note);
        if (Entry.Tag != nullptr && Entry.Tag[0] != '\0')
        {
            const float  TagAdvance = GlyphRun::Measure(Column, Faces.Regular, 8.0f * S, Entry.Tag);
            const ImVec2 TagMax(Max.x - 10.0f * S, Min.y + 25.0f * S);
            const ImVec2 TagMin(TagMax.x - TagAdvance - 14.0f * S, Min.y + 9.0f * S);
            DrawList->AddRectFilled(TagMin, TagMax, FadeColour(Theme.TagFill, Fade), 8.0f * S);
            DrawList->AddRect(TagMin, TagMax, FadeColour(Theme.TagBorder, Fade), 8.0f * S, ImDrawFlags_None, S);
            (void)GlyphRun::Construct(Column, Faces.Regular, 8.0f * S,
                                      ImVec2(TagMin.x + 7.0f * S, TagMin.y + 0.5f * (16.0f * S - GlyphRun::EmPixelHeight * 8.0f * S) + GlyphRun::EmAscent * 8.0f * S),
                                      FadeColour(Theme.TagText, Fade), Entry.Tag);
        }
    }
    ImGui::PopID();
    return InOutChosen != Before;
}

} // namespace Frontier
