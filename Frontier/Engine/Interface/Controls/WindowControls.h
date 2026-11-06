/*==========================================================================================================================================
                                                            WINDOWCONTROLS.H
==========================================================================================================================================*/
// 🧩 What the editor's own windows (Smoke & fire, Fluid) lay themselves out with: the palette and typefaces at one scale, the rounded pill button that
//    remembers where it lay, words cut to a width, and the clock the Log Stream stamps its lines with. Moved out of the Smoke & fire window unchanged,
//    so the second window uses the first one's controls and does not draw its own.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/GlyphSymbol.h"
#include "Engine/Interface/InspectorControls.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace Frontier
{

// 📝 Where a control of a window lay at its last layout, in screen pixels: what a capture or a validation needs to press it with the pointer.
struct EditorControlRectangle
{
    std::string Name;   // "row:Domain", "library", "pause", "tool:brush", …
    ImVec2      Minimum{};
    ImVec2      Maximum{};
};

namespace WindowControls
{

struct Look
{
    InspectorPalette Palette = InspectorPalette::Resolve();
    EditorTypefaces  Faces   = EditorTypefaces::Resolve();
    float            Scale   = 1.0f;

    ImFont* Pick(ImFont* Font) const { return Font ? Font : ImGui::GetFont(); }
    void    Text(ImDrawList* Ink, ImFont* Font, float Size, ImVec2 At, ImU32 Colour, const char* Words) const
    {
        Ink->AddText(Pick(Font), Size * Scale, At, Colour, Words);
    }
    float Measure(ImFont* Font, float Size, const char* Words) const
    {
        return Pick(Font)->CalcTextSizeA(Size * Scale, FLT_MAX, 0.0f, Words).x;
    }
};

// 📝 What a pill is wide: its words and, with a glyph, the glyph's room; a compact pill is the glyph alone.
inline float MeasurePillWidth(const Look& Style, const char* Label, bool WithGlyph, bool Compact)
{
    if (Compact && WithGlyph) return 36.0f * Style.Scale;
    return Style.Measure(Style.Faces.Medium, 11.0f, Label) + (WithGlyph ? 34.0f : 22.0f) * Style.Scale;
}

// 📝 A rounded pill button that remembers where it lay; true when pressed. A compact pill shows its glyph alone and names itself in a tooltip.
//    IdPrefix keeps the ImGui identifiers of one window apart from another's.
inline bool ConstructPillButton(const Look& Style, ImDrawList* Ink, std::vector<EditorControlRectangle>& Registry, const char* Name, ImVec2 At, const char* Label,
                                bool Active, const char* Glyph = nullptr, float* OutWidth = nullptr, bool Compact = false, const char* IdPrefix = "Plume")
{
    const float Height = 30.0f * Style.Scale;
    const float Width  = MeasurePillWidth(Style, Label, Glyph != nullptr, Compact);
    ImGui::SetCursorScreenPos(At);
    char Id[96];
    std::snprintf(Id, sizeof(Id), "##%s%s", IdPrefix, Name);
    const bool Pressed = ImGui::InvisibleButton(Id, ImVec2(Width, Height));
    const bool Hovered = ImGui::IsItemHovered();
    if (Compact && Glyph != nullptr && Hovered) ImGui::SetTooltip("%s", Label);
    Registry.push_back({ Name, At, ImVec2(At.x + Width, At.y + Height) });
    Ink->AddRectFilled(At, ImVec2(At.x + Width, At.y + Height), Active ? Style.Palette.SegmentedActiveBg : Hovered ? Style.Palette.RowHoverBg : Style.Palette.CountCardBg,
                       9.0f * Style.Scale);
    Ink->AddRect(At, ImVec2(At.x + Width, At.y + Height), Style.Palette.InsetFieldBorder, 9.0f * Style.Scale);
    if (Compact && Glyph != nullptr)
    {
        GlyphSymbol::Construct(Ink, Glyph, ImVec2(At.x + 11.0f * Style.Scale, At.y + 8.0f * Style.Scale), 14.0f * Style.Scale, 1.4f,
                               Active ? Style.Palette.TextPrimary : Style.Palette.TextSecondary);
        if (OutWidth) *OutWidth = Width;
        return Pressed;
    }
    float Left = At.x + 11.0f * Style.Scale;
    if (Glyph)
    {
        GlyphSymbol::Construct(Ink, Glyph, ImVec2(Left, At.y + 8.0f * Style.Scale), 14.0f * Style.Scale, 1.4f, Style.Palette.TextSecondary);
        Left += 19.0f * Style.Scale;
    }
    Style.Text(Ink, Style.Faces.Medium, 11.0f, ImVec2(Left, At.y + 8.5f * Style.Scale), Active ? Style.Palette.TextPrimary : Style.Palette.TextSecondary, Label);
    if (OutWidth) *OutWidth = Width;
    return Pressed;
}

inline std::string ResolveClock()
{
    const std::time_t Now = std::time(nullptr);
    std::tm           Local{};
#if defined(_WIN32)
    localtime_s(&Local, &Now);
#else
    localtime_r(&Now, &Local);
#endif
    char Text[16];
    std::snprintf(Text, sizeof(Text), "%02d:%02d:%02d", Local.tm_hour, Local.tm_min, Local.tm_sec);
    return Text;
}

inline std::string Lowered(std::string Text)
{
    std::transform(Text.begin(), Text.end(), Text.begin(), [](unsigned char Letter) { return static_cast<char>(std::tolower(Letter)); });
    return Text;
}

// 📝 Words cut to a width: the longest start that fits with "..." after it (whole UTF-8 characters), or the words whole when they fit.
inline std::string FitText(const Look& Style, ImFont* Font, float Size, std::string Words, float Room)
{
    if (Style.Measure(Font, Size, Words.c_str()) <= Room) return Words;
    while (!Words.empty() && Style.Measure(Font, Size, (Words + "...").c_str()) > Room)
    {
        Words.pop_back();
        while (!Words.empty() && (static_cast<unsigned char>(Words.back()) & 0xC0u) == 0x80u) Words.pop_back();   // [-] a continuation byte belongs to the character before it
        if (!Words.empty() && static_cast<unsigned char>(Words.back()) >= 0xC0u) Words.pop_back();
    }
    return Words + "...";
}

} // namespace WindowControls

} // namespace Frontier

#endif
