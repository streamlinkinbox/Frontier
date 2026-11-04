/*==========================================================================================================================================
                                                                ACTIONROW.CPP
==========================================================================================================================================*/
// 🧩 The demo's inline actions and sensor-caption reset button, using native ImGui input regions.

#include "Engine/Interface/Controls/ActionRow.h"
#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/GlyphRun.h"
#include "Engine/Interface/InspectorControls.h"

#include <imgui_internal.h>
#include <algorithm>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    ACTION ROW
//------------------------------------------------------------------------------------------------------------------------

int ActionRow::Construct(ControlColumn &Column, std::span<const ActionEntry> Entries, const ActionRowLook &Look)
{
    const float         Scale  = Column.QueryScale();
    const float         Width  = Column.QueryWidth();
    const auto         &Faces  = Column.QueryFaces();
    const auto         &Theme  = Column.QueryTheme();
    const float         Height = (Look.FontSize * 1.4f + Look.PaddingTop * 2 + 2) * Scale;
    std::vector<ImVec2> Positions;
    std::vector<float>  Widths;
    const float Leading = Look.Leading ? GlyphRun::Measure(Column, Faces.Regular, 9 * Scale, Look.Leading, 0.9f * Scale) + 8 * Scale : 0;
    float       Along = Leading, Across = 0;
    for (size_t Index = 0; Index < Entries.size(); ++Index)
    {
        const float Extent = std::min(Width, GlyphRun::Measure(Column, Faces.Regular, Look.FontSize * Scale, Entries[Index].Caption) +
                                                 (Look.PaddingSide * 2 + 2) * Scale);
        if (Along > 0 && Along + Extent > Width)
        {
            Along = 0;
            Across += Height + 8 * Scale;
        }
        if (Look.Trailing && Index + 1 == Entries.size()) Along = std::max(Along, Width - Extent);
        Positions.emplace_back(Along, Across);
        Widths.push_back(Extent);
        Along += Extent + 8 * Scale;
    }
    const ImVec2 Origin = Column.Space((Across + Height) / Scale, Look.MarginTop, Look.MarginBottom);
    if (Look.Leading)
        GlyphRun::Construct(Column, Faces.Regular, 9 * Scale,
                            {Origin.x, GlyphRun::ResolveBaseline(Origin.y + (Height - 12.6f * Scale) * 0.5f, 9 * Scale, 1.4f)}, Theme.Muted,
                            Look.Leading, 0.9f * Scale);
    int Pressed = -1;
    for (size_t Index = 0; Index < Entries.size(); ++Index)
    {
        const auto  &Entry = Entries[Index];
        const ImVec2 Min(Origin.x + Positions[Index].x, Origin.y + Positions[Index].y);
        const ImVec2 Max(Min.x + Widths[Index], Min.y + Height);
        ImGui::SetCursorScreenPos(Min);
        if (!Entry.Enabled) ImGui::BeginDisabled();
        if (ImGui::InvisibleButton(Entry.Id, {Widths[Index], Height}, ImGuiButtonFlags_EnableNav) && Entry.Enabled)
            Pressed = static_cast<int>(Index);
        if (!Entry.Enabled) ImGui::EndDisabled();
        ControlColumn::ConstructTooltip(Entry.Tooltip);
        auto       *Ink  = Column.QueryDrawList();
        const float Fade = Entry.Enabled ? 1 : 0.45f;
        Ink->AddRectFilled(Min, Max, FadeColour(ImGui::IsItemHovered() ? ResolveCssColour(0x34343AFF) : ResolveCssColour(0x1B1B20FF), Fade),
                           22 * Scale);
        Ink->AddRect(Min, Max, FadeColour(ResolveCssColour(0x444444FF), Fade), 22 * Scale);
        const float TextWidth = GlyphRun::Measure(Column, Faces.Regular, Look.FontSize * Scale, Entry.Caption);
        GlyphRun::Construct(Column, Faces.Regular, Look.FontSize * Scale,
                            {Min.x + (Widths[Index] - TextWidth) * 0.5f,
                             GlyphRun::ResolveBaseline(Min.y + (Look.PaddingTop + 1) * Scale, Look.FontSize * Scale, 1.4f)},
                            FadeColour(Theme.Ink, Fade), Entry.Caption);
        ImGui::RenderNavCursor(ImRect(Min, Max), ImGui::GetItemID());
    }
    return Pressed;
}

} // namespace Frontier
