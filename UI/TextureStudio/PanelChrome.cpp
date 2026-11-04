/*============================================================================================================================================
                                                      PANELCHROME.CPP
============================================================================================================================================*/
// 🧩 Single implementation of the Texture Studio panels' shared UI primitives - see PanelChrome.h.

#include "UI/TextureStudio/PanelChrome.h"
#include "Engine/Interface/GlyphSpace.h"
#include "Engine/Interface/PixelSpace.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cfloat>
#include <cstdio>

namespace Frontier::PanelChrome
{
ImVec4 Spectrum(uint32_t Colour, float Alpha) noexcept
{
    return ImVec4(float((Colour >> 16) & 0xff) / 255.f, float((Colour >> 8) & 0xff) / 255.f, float(Colour & 0xff) / 255.f, Alpha);
}
ColorQuad SpectrumQuad(uint32_t Colour, float Alpha) noexcept
{
    return ColorQuad{float((Colour >> 16) & 0xff) / 255.f, float((Colour >> 8) & 0xff) / 255.f, float(Colour & 0xff) / 255.f, Alpha};
}
ImVec4 ThemeQuad(const ColorQuad &Colour, float AlphaOverride) noexcept
{
    return {Colour.Red, Colour.Green, Colour.Blue, AlphaOverride >= 0.0f ? AlphaOverride : Colour.Alpha};
}
ImU32 Ink32(uint32_t Colour, float Alpha)
{
    ImVec4 Quad = Spectrum(Colour);
    Quad.w = Alpha;
    return ImGui::GetColorU32(Quad);
}
ImU32 Colour32(uint32_t Colour, float Alpha) noexcept
{
    return IM_COL32((Colour >> 16) & 0xff, (Colour >> 8) & 0xff, Colour & 0xff, uint8_t(std::clamp(Alpha, 0.f, 1.f) * 255.f));
}
ImU32 Ink255(uint32_t Colour, unsigned Alpha) noexcept
{
    return IM_COL32((Colour >> 16) & 255, (Colour >> 8) & 255, Colour & 255, Alpha);
}

void Caption(ImVec2 Position, const char *Text, float Size, uint32_t Colour, void *Typeface)
{
    ImGui::GetWindowDrawList()->AddText(Typeface ? static_cast<ImFont *>(Typeface) : ImGui::GetFont(), Size * 1.302f, Position,
        ImGui::GetColorU32(Spectrum(Colour)), Text);
}
float CaptionWidth(const char *Text, float Size, void *Typeface)
{
    return (Typeface ? static_cast<ImFont *>(Typeface) : ImGui::GetFont())->CalcTextSizeA(Size * 1.302f, FLT_MAX, 0.f, Text).x;
}
void DrawGlyph(const char *Path, ImVec2 Position, float Size, uint32_t Colour, float StrokeWidth)
{
    PixelSpace Surface;
    (void)Surface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 1);
    GlyphSpace::Stroke(Surface, Path, {Position.x, Position.y, Size, StrokeWidth, SpectrumQuad(Colour)});
}
std::string Lowered(std::string Text) noexcept
{
    for (auto &Character : Text) Character = char(std::tolower(uint8_t(Character)));
    return Text;
}

bool BareIconButton(const char *Id, const char *Path, float Size, float Scale, bool Selected, uint32_t Colour)
{
    const ImVec2 Position = ImGui::GetCursorScreenPos();
    ImGui::PushID(Id);
    const bool Pressed = ImGui::InvisibleButton("##Icon", ImVec2(Size, Size));
    ImGui::PopID();
    const bool Hovered = ImGui::IsItemHovered();
    const bool Disabled = ImGui::GetItemFlags() & ImGuiItemFlags_Disabled;
    auto *Ink = ImGui::GetWindowDrawList();
    if (Selected || (Hovered && !Disabled))
        Ink->AddRectFilled(Position, ImVec2(Position.x + Size, Position.y + Size), Ink32(Selected ? 0x2a2a27 : 0x232320, Disabled ? .4f : 1.f), 6 * Scale);
    const float Glyph = Size * .55f;
    DrawGlyph(Path, ImVec2(Position.x + (Size - Glyph) * .5f, Position.y + (Size - Glyph) * .5f), Glyph, Disabled ? 0x4c4c44 : Colour);
    return Pressed && !Disabled;
}

bool Pill(const char *Id, const char *Label, ImVec2 Position, float Width, float Height, bool Selected,
    float Scale, const char *IconPath, uint32_t Accent, float TextSize, bool ClipText)
{
    ImGui::SetCursorScreenPos(Position);
    ImGui::PushID(Id);
    const bool Pressed = ImGui::InvisibleButton("##Pill", ImVec2(Width, Height));
    ImGui::PopID();
    const bool Hovered = ImGui::IsItemHovered();
    const bool Disabled = ImGui::GetItemFlags() & ImGuiItemFlags_Disabled;
    auto *Ink = ImGui::GetWindowDrawList();
    const ImVec2 Far(Position.x + Width, Position.y + Height);
    const uint32_t Background = Selected ? (Accent ? Accent : 0x2a2a28) : Hovered && !Disabled ? 0x2e2e2a : 0x1d1d1b;
    Ink->AddRectFilled(Position, Far, Colour32(Background, Disabled ? .45f : 1.f), Height * .5f);
    Ink->AddRect(Position, Far, Colour32(0xffffff, Selected ? .10f : .045f), Height * .5f);
    if (ClipText) Ink->PushClipRect(Position, Far, true);
    float TextX = Position.x + 10 * Scale;
    if (IconPath)
    {
        DrawGlyph(IconPath, ImVec2(Position.x + 8 * Scale, Position.y + (Height - 12 * Scale) * .5f), 12 * Scale, Selected ? 0xedc391 : 0xa8a89a);
        TextX += 15 * Scale;
    }
    else
    {
        TextX = Position.x + (Width - CaptionWidth(Label, TextSize * Scale)) * .5f;
        if (ClipText) TextX = std::max(TextX, Position.x + 4 * Scale);
    }
    Caption(ImVec2(TextX, Position.y + (Height - (TextSize + .5f) * Scale) * .42f), Label, TextSize * Scale,
        Disabled ? 0x5c5c54 : Selected ? 0xf0e9d8 : 0xc8c8ba);
    if (ClipText) Ink->PopClipRect();
    return Pressed && !Disabled;
}

void CheckDisc(ImDrawList *Ink, ImVec2 CardPosition, float Scale)
{
    Ink->AddCircleFilled(ImVec2(CardPosition.x + 14 * Scale, CardPosition.y + 14 * Scale), 6 * Scale, Ink255(0xa5b99c));
    Ink->AddLine(ImVec2(CardPosition.x + 11 * Scale, CardPosition.y + 14 * Scale), ImVec2(CardPosition.x + 13 * Scale, CardPosition.y + 16 * Scale), Ink255(0x2a3927));
    Ink->AddLine(ImVec2(CardPosition.x + 13 * Scale, CardPosition.y + 16 * Scale), ImVec2(CardPosition.x + 18 * Scale, CardPosition.y + 11 * Scale), Ink255(0x2a3927));
}

void VisibilityStatCards(ImVec2 Origin, float CardWidth, float LeftInset, float Scale,
    size_t VisibleCount, size_t HiddenCount, int PressedIndex, int &ClickedIndex,
    void *NumberTypeface, void *NumberFallbackTypeface, const std::function<void(const char *)> &OnCardExtent)
{
    ClickedIndex = -1;
    const size_t Counts[2]{VisibleCount, HiddenCount};
    const struct { const char *Label; const char *Icon; uint32_t Disc; uint32_t Glyph; } Cards[2]{
        {"Visible", "M20 6L9 17l-5-5", 0x58876a, 0x0b2f17},
        {"Hidden", "M2 12Q12 0 22 12Q12 24 2 12ZM12 9A3 3 0 1 0 12 15A3 3 0 1 0 12 9ZM4 4L20 20", 0x292929, 0x676760}};
    auto *Ink = ImGui::GetWindowDrawList();
    for (int Index = 0; Index < 2; ++Index)
    {
        const ImVec2 CardMin(Origin.x + LeftInset + Index * (CardWidth + 8 * Scale), Origin.y);
        const ImVec2 CardMax(CardMin.x + CardWidth, CardMin.y + 66 * Scale);
        const bool Pressed = PressedIndex == Index;
        ImGui::SetCursorScreenPos(CardMin);
        ImGui::PushID(Index);
        const bool Clicked = ImGui::InvisibleButton("##Stat", ImVec2(CardWidth, 66 * Scale));
        ImGui::PopID();
        if (OnCardExtent)
        {
            char StatLabel[32];
            std::snprintf(StatLabel, sizeof(StatLabel), "%02zu\n%s", Counts[Index], Cards[Index].Label);
            OnCardExtent(StatLabel); // same caption the pre-restyle stat buttons registered
        }
        if (Clicked) ClickedIndex = Index;
        Ink->AddRectFilled(CardMin, CardMax, Ink32(Pressed ? 0x25221d : 0x191919), 20 * Scale);
        Ink->AddRect(CardMin, CardMax, Ink32(Pressed ? 0x9a8564 : 0x2c2c2c, Pressed ? .34f : 1.f), 20 * Scale);
        Ink->AddCircleFilled(ImVec2(CardMin.x + 22.5f * Scale, CardMin.y + 21.5f * Scale), 10.5f * Scale, Ink32(Cards[Index].Disc), 24);
        DrawGlyph(Cards[Index].Icon, ImVec2(CardMin.x + 16.5f * Scale, CardMin.y + 15.5f * Scale), 12 * Scale, Cards[Index].Glyph, 2.2f);
        Caption(ImVec2(CardMin.x + 12 * Scale, CardMax.y - 21 * Scale), Cards[Index].Label, 9 * Scale, 0x99998f);
        char Number[8];
        std::snprintf(Number, sizeof(Number), "%02zu", Counts[Index]);
        Caption(ImVec2(CardMax.x - 13 * Scale - CaptionWidth(Number, 29 * Scale, NumberTypeface), CardMax.y - 40 * Scale), Number, 29 * Scale, 0xddddcf,
            NumberTypeface ? NumberTypeface : NumberFallbackTypeface);
    }
    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 66 * Scale));
}

ControlKitPalette StudioPalette(const ControlKitPalette &Saved, ColorQuad Selected, ColorQuad Accent)
{
    auto Palette = Saved;
    Palette.Panel = {.0706f, .0706f, .0706f, 1};
    Palette.Inset = {.098f, .098f, .098f, 1};
    Palette.Raised = {.133f, .133f, .133f, 1};
    Palette.Selected = Selected;
    Palette.Text = {.808f, .808f, .753f, 1};
    Palette.TextDim = {.56f, .58f, .50f, 1};
    Palette.TextFaint = {.35f, .37f, .30f, 1};
    Palette.Accent = Accent;
    return Palette;
}

DockChromeToken PushDockChrome(ImVec4 Title, ImVec4 Tab, bool IncludeCollapsedAndEmpty, bool SuppressTabClose, float Scale)
{
    DockChromeToken Token;
    ImGui::PushStyleColor(ImGuiCol_TitleBg, Title);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, Title);
    Token.Colours += 2;
    if (IncludeCollapsedAndEmpty)
    {
        ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, Title);
        ImGui::PushStyleColor(ImGuiCol_DockingEmptyBg, Title);
        Token.Colours += 2;
    }
    for (ImGuiCol Slot : {ImGuiCol_Tab, ImGuiCol_TabHovered, ImGuiCol_TabSelected, ImGuiCol_TabDimmed, ImGuiCol_TabDimmedSelected})
        ImGui::PushStyleColor(Slot, Tab);
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelectedOverline, ImVec4(0, 0, 0, 0));
    Token.Colours += 7;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_DockingSeparatorSize, 2 * Scale);
    // No visible scrollbars anywhere in the workspaces: wheel scrolling stays live (and eased, with
    // overscroll, via ImGui Patch H) but the bar/track chrome is gone.
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.f);
    Token.Vars += 3;
    if (SuppressTabClose)
    {
        Token.SavedCloseSelected = ImGui::GetStyle().TabCloseButtonMinWidthSelected;
        Token.SavedCloseUnselected = ImGui::GetStyle().TabCloseButtonMinWidthUnselected;
        Token.CloseSaved = true;
        ImGui::GetStyle().TabCloseButtonMinWidthSelected = FLT_MAX;
        ImGui::GetStyle().TabCloseButtonMinWidthUnselected = FLT_MAX;
    }
    return Token;
}

void PopDockChrome(const DockChromeToken &Token)
{
    if (Token.CloseSaved)
    {
        ImGui::GetStyle().TabCloseButtonMinWidthSelected = Token.SavedCloseSelected;
        ImGui::GetStyle().TabCloseButtonMinWidthUnselected = Token.SavedCloseUnselected;
    }
    if (Token.Vars) ImGui::PopStyleVar(Token.Vars);
    if (Token.Colours) ImGui::PopStyleColor(Token.Colours);
}
}
