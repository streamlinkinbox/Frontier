/*==========================================================================================================================================
                                                     VIEWPORTCHROMECONTROLS.CPP
==========================================================================================================================================*/
// 🧩 The VehicleHMI demo's viewport chrome drawn with ImGui, 1:1 with FrontierEditor.css and the demo's Lucide glyphs.

#include "Engine/Interface/ViewportChromeControls.h"

#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/Controls/GlyphSymbol.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

namespace Frontier
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 FrontierEditor.css colours (the demo's dark scheme, its only one).
// 📝 stb_truetype sizes a face by (ascent − descent), CSS by the em: ImGui pixel height per CSS pixel of font-size.
constexpr float DmSansEm        = 1.302f;   // DM Sans hhea 992 / −310 over 1000 units
constexpr float JetBrainsMonoEm = 1.320f;   // JetBrains Mono hhea 1020 / −300 over 1000 units
// 📝 Half the leading of a CSS line box (LineHeight × em) around the face's content area, per em.
[[nodiscard]] constexpr float ResolveHalfLeading(float LineHeight, float EmHeight) noexcept
{
    return 0.5f * (LineHeight - EmHeight);
}

constexpr ImU32 BarSurface        = IM_COL32(0x16, 0x16, 0x16, 0xFF);   // --column
constexpr ImU32 EdgeStroke        = IM_COL32(0x2A, 0x2A, 0x2A, 0xFF);   // --edge
constexpr ImU32 TextTone          = IM_COL32(0xDC, 0xDC, 0xDC, 0xFF);   // --text
constexpr ImU32 MutedTone         = IM_COL32(0x7D, 0x7D, 0x7D, 0xFF);   // --muted
constexpr ImU32 OnTone            = IM_COL32(0x3E, 0xC4, 0x6D, 0xFF);   // --on
constexpr ImU32 FailTone          = IM_COL32(0xFF, 0x6B, 0x6B, 0xFF);   // --fail
constexpr ImU32 SubtitleTone      = IM_COL32(0x70, 0x70, 0x70, 0xFF);   // .scene-heading span
constexpr ImU32 LivePillText      = IM_COL32(0xC9, 0xA2, 0x4A, 0xFF);   // .live-pill
constexpr ImU32 LivePillStroke    = IM_COL32(0x5A, 0x4A, 0x24, 0xFF);
constexpr ImU32 LivePillSurface   = IM_COL32(0x2A, 0x24, 0x16, 0xFF);
constexpr ImU32 IconSurface       = IM_COL32(0x1D, 0x1D, 0x1D, 0xFF);   // .icon-button
constexpr ImU32 IconStroke        = IM_COL32(0x2C, 0x2C, 0x2C, 0xFF);
constexpr ImU32 IconGlyph         = IM_COL32(0xA9, 0xA9, 0xA9, 0xFF);
constexpr ImU32 IconHoverSurface  = IM_COL32(0x2A, 0x2A, 0x2A, 0xFF);   // .icon-button:hover, .active
constexpr ImU32 IconHoverStroke   = IM_COL32(0x44, 0x44, 0x44, 0xFF);
constexpr ImU32 IconHoverGlyph    = IM_COL32(0xF0, 0xF0, 0xF0, 0xFF);
constexpr ImU32 IconActiveInset   = IM_COL32(0xFF, 0xFF, 0xFF, 0x10);   // inset 0 0 0 1px #ffffff10
constexpr ImU32 SegmentedSurface  = IM_COL32(0x12, 0x12, 0x12, 0xFF);   // .segmented
constexpr ImU32 SegmentedStroke   = IM_COL32(0x26, 0x26, 0x26, 0xFF);
constexpr ImU32 SegmentedText     = IM_COL32(0x8B, 0x8B, 0x8B, 0xFF);
constexpr ImU32 SegmentedActive   = IM_COL32(0x2E, 0x2E, 0x2E, 0xFF);
constexpr ImU32 SegmentedShadow   = IM_COL32(0x00, 0x00, 0x00, 0x66);   // 0 1px 3px #0006
constexpr ImU32 SegmentedHighlight = IM_COL32(0xFF, 0xFF, 0xFF, 0x0A);  // inset 0 1px #ffffff0a
constexpr ImU32 SelectSurface     = IM_COL32(0x19, 0x19, 0x19, 0xFF);   // .select-field
constexpr ImU32 SelectStroke      = IM_COL32(0x39, 0x39, 0x39, 0xFF);
constexpr ImU32 SelectText        = IM_COL32(0xC8, 0xC8, 0xC8, 0xFF);
constexpr ImU32 ReadoutSurface    = IM_COL32(0x10, 0x10, 0x10, 0xC7);   // #hud rgba(16, 16, 16, 0.78)
constexpr ImU32 ReadoutStroke     = IM_COL32(0x2C, 0x2C, 0x2C, 0xFF);
constexpr ImU32 ReadoutText       = IM_COL32(0xCF, 0xCF, 0xCF, 0xFF);
constexpr ImU32 CloseStroke       = IM_COL32(0x33, 0x33, 0x33, 0xFF);   // .overlay-close
constexpr ImU32 CloseText         = IM_COL32(0x8F, 0x8F, 0x8F, 0xFF);
constexpr ImU32 CloseHoverStroke  = IM_COL32(0x55, 0x55, 0x55, 0xFF);
constexpr ImU32 NoticeSurface     = IM_COL32(0x12, 0x12, 0x12, 0xE6);   // #overlay-notice rgba(18, 18, 18, 0.9)
constexpr ImU32 NoticeText        = IM_COL32(0xBD, 0xBD, 0xBD, 0xFF);
constexpr ImU32 BringUpVeil       = IM_COL32(0x0A, 0x0A, 0x0A, 0xF2);   // #bring-up rgba(10, 10, 10, 0.95)
constexpr ImU32 BringUpHeading    = IM_COL32(0xE2, 0xE2, 0xE2, 0xFF);
constexpr ImU32 BringUpFill       = IM_COL32(0xD9, 0xD9, 0xD9, 0xFF);
constexpr ImU32 FailureSurface    = IM_COL32(0x28, 0x0C, 0x0E, 0xF0);   // #failure rgba(40, 12, 14, 0.94)
constexpr ImU32 FailureText       = IM_COL32(0xFF, 0xD5, 0xD9, 0xFF);
constexpr ImU32 StatusSurface     = IM_COL32(0x0D, 0x0D, 0x0D, 0xFF);   // #status
constexpr ImU32 StatusCaption     = IM_COL32(0x62, 0x62, 0x62, 0xFF);
constexpr ImU32 StatusMeasurement = IM_COL32(0xCF, 0xCF, 0xCF, 0xFF);

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

ImFont* ResolveFace(ImFont* Face) noexcept
{
    return Face != nullptr ? Face : ImGui::GetFont();
}

float MeasureText(ImFont* Face, float Size, const char* Text) noexcept
{
    return (Text == nullptr || Text[0] == '\0') ? 0.0f : ResolveFace(Face)->CalcTextSizeA(Size, FLT_MAX, 0.0f, Text).x;
}

// 📝 CSS letter-spacing: one glyph at a time, the spacing added after each (UTF-8 aware).
float ConstructSpacedText(ImDrawList* DrawList, ImFont* Face, float Size, ImVec2 Origin, ImU32 Colour, const char* Text, float Spacing) noexcept
{
    if (Text == nullptr) return 0.0f;
    Face              = ResolveFace(Face);
    const char* Glyph = Text;
    const char* End   = Text + std::strlen(Text);
    float       Pen   = 0.0f;
    while (Glyph < End)
    {
        unsigned int Codepoint = 0;
        const int    Length    = std::max(1, ImTextCharFromUtf8(&Codepoint, Glyph, End));
        if (DrawList != nullptr) DrawList->AddText(Face, Size, ImVec2(Origin.x + Pen, Origin.y), Colour, Glyph, Glyph + Length);
        Pen   += Face->CalcTextSizeA(Size, FLT_MAX, 0.0f, Glyph, Glyph + Length).x + Spacing;
        Glyph += Length;
    }
    return std::max(0.0f, Pen - Spacing);
}

void ConstructTooltip(const char* Tooltip) noexcept
{
    if (Tooltip != nullptr && Tooltip[0] != '\0' && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
    {
        ImGui::SetTooltip("%s", Tooltip);
    }
}

ImU32 FadeColour(ImU32 Colour, float Factor) noexcept
{
    const ImU32 Alpha = static_cast<ImU32>(static_cast<float>((Colour >> IM_COL32_A_SHIFT) & 0xFF) * Factor);
    return (Colour & ~IM_COL32_A_MASK) | (Alpha << IM_COL32_A_SHIFT);
}

// 📝 Approved vector glyphs are reused by camera controls and each panel's status row.
void ConstructLucideGlyph(ImDrawList* DrawList, ImVec2 Centre, float Extent, ImU32 Colour, const char* Glyph) noexcept
{
    GlyphSymbol::Construct(DrawList,Glyph,{Centre.x-Extent*.5f,Centre.y-Extent*.5f},Extent,1.7f,Colour);
}

// 📝 .overlay-close: 20 px, radius 6 px, ×; Min is its top-left corner.
bool ConstructOverlayClose(const char* Id, const EditorTypefaces& Faces, ImVec2 Min, const char* Tooltip, float Scale) noexcept
{
    const float Extent = 20.0f * Scale;
    ImGui::PushID(Id);
    ImGui::SetCursorScreenPos(Min);
    const bool Pressed = ImGui::InvisibleButton("##OverlayClose", ImVec2(Extent, Extent));
    ConstructTooltip(Tooltip);
    const bool Hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    ImDrawList*  DrawList = ImGui::GetWindowDrawList();
    const ImVec2 Max(Min.x + Extent, Min.y + Extent);
    DrawList->AddRectFilled(Min, Max, IconSurface, 6.0f * Scale);
    DrawList->AddRect(Min, Max, Hovered ? CloseHoverStroke : CloseStroke, 6.0f * Scale, ImDrawFlags_None, Scale);
    const char* Cross     = "\xC3\x97";
    const float CrossSize = 13.0f * DmSansEm * Scale;
    const float CrossW    = MeasureText(Faces.Regular, CrossSize, Cross);
    DrawList->AddText(ResolveFace(Faces.Regular), CrossSize, ImVec2(Min.x + 0.5f * (Extent - CrossW), Min.y + 0.5f * (Extent - CrossSize)),
                      Hovered ? IconHoverGlyph : CloseText, Cross);
    return Pressed;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    VIEWPORT BAR
//------------------------------------------------------------------------------------------------------------------------



void ViewportChromeControls::ConstructBarBackground(ImDrawList* DrawList, ImVec2 Min, ImVec2 Max, float Scale)
{
    DrawList->AddRectFilled(Min, Max, BarSurface);
    DrawList->AddLine(ImVec2(Min.x, Max.y - 0.5f * Scale), ImVec2(Max.x, Max.y - 0.5f * Scale), EdgeStroke, Scale);
}





float ViewportChromeControls::MeasureLabelButton(const EditorTypefaces& Faces, const char* Label, float Scale)
{
    // 📝 .icon-button.particle-launch: width auto, min-width 72 px, padding 0 12 px, 11 px.
    return std::max(72.0f * Scale, MeasureText(Faces.Regular, 11.0f * DmSansEm * Scale, Label) + 24.0f * Scale + 2.0f * Scale);
}

bool ViewportChromeControls::ConstructLabelButton(const char* Id, const EditorTypefaces& Faces, ImVec2 Min, ImVec2 Size, const char* Label, bool Enabled,
                                                  const char* Tooltip, float Scale)
{
    ImGui::SetCursorScreenPos(Min);
    if (!Enabled) ImGui::BeginDisabled();
    const bool Pressed = ImGui::InvisibleButton(Id, Size) && Enabled;
    if (!Enabled) ImGui::EndDisabled();
    ConstructTooltip(Tooltip);
    const bool   Hovered  = Enabled && ImGui::IsItemHovered();
    ImDrawList*  DrawList = ImGui::GetWindowDrawList();
    const ImVec2 Max(Min.x + Size.x, Min.y + Size.y);
    const float  Fade = Enabled ? 1.0f : 0.45f;
    DrawList->AddRectFilled(Min, Max, FadeColour(Hovered ? IconHoverSurface : IconSurface, Fade), 9.0f * Scale);
    DrawList->AddRect(Min, Max, FadeColour(Hovered ? IconHoverStroke : IconStroke, Fade), 9.0f * Scale, ImDrawFlags_None, Scale);
    const float TextSize = 11.0f * DmSansEm * Scale;
    const float TextW    = MeasureText(Faces.Regular, TextSize, Label);
    DrawList->AddText(ResolveFace(Faces.Regular), TextSize, ImVec2(Min.x + 0.5f * (Size.x - TextW), Min.y + 0.5f * (Size.y - TextSize)),
                      FadeColour(Hovered ? IconHoverGlyph : IconGlyph, Fade), Label);
    return Pressed;
}

bool ViewportChromeControls::ConstructIconButton(const char* Id, ImVec2 Min, float Extent, const char* Glyph, bool Active, bool Enabled, const char* Tooltip)
{
    ImGui::SetCursorScreenPos(Min);
    if (!Enabled) ImGui::BeginDisabled();
    const bool Pressed = ImGui::InvisibleButton(Id, ImVec2(Extent, Extent)) && Enabled;
    if (!Enabled) ImGui::EndDisabled();
    ConstructTooltip(Tooltip);
    const bool   Lit      = Enabled && (ImGui::IsItemHovered() || Active);
    ImDrawList*  DrawList = ImGui::GetWindowDrawList();
    const ImVec2 Centre(Min.x + 0.5f * Extent, Min.y + 0.5f * Extent);
    const float  Radius = 0.5f * Extent;
    const float  Fade   = Enabled ? 1.0f : 0.45f;
    DrawList->AddCircleFilled(Centre, Radius, FadeColour(Lit ? IconHoverSurface : IconSurface, Fade), 40);
    DrawList->AddCircle(Centre, Radius - 0.5f, FadeColour(Lit ? IconHoverStroke : IconStroke, Fade), 40, 1.0f);
    if (Active && Enabled) DrawList->AddCircle(Centre, Radius - 1.5f, IconActiveInset, 40, 1.0f);
    const ImU32 GlyphTone = Active && Enabled ? OnTone : (Lit ? IconHoverGlyph : IconGlyph);
    ConstructLucideGlyph(DrawList, Centre, Extent * (15.0f / 34.0f), FadeColour(GlyphTone, Fade), Glyph);
    return Pressed;
}

bool ViewportChromeControls::ConstructSegmentedSwitch(const char* Id, const EditorTypefaces& Faces, ImVec2 Min, ImVec2 Size, const char* const* Options,
                                                      int OptionCount, int& InOutIndex, const bool* EnabledOptions, const char* const* Tooltips, float Scale)
{
    // 📝 .segmented: padding 4 px, gap 4 px, radius 11 px; buttons radius 8 px, 10 px text; .active raised.
    if (Options == nullptr || OptionCount <= 0) return false;
    ImDrawList*  DrawList = ImGui::GetWindowDrawList();
    const ImVec2 Max(Min.x + Size.x, Min.y + Size.y);
    DrawList->AddRectFilled(Min, Max, SegmentedSurface, 11.0f * Scale);
    DrawList->AddRect(Min, Max, SegmentedStroke, 11.0f * Scale, ImDrawFlags_None, Scale);
    const float Inset   = 4.0f * Scale + Scale;
    const float Gap     = 4.0f * Scale;
    const float CellW   = (Size.x - 2.0f * Inset - Gap * static_cast<float>(OptionCount - 1)) / static_cast<float>(OptionCount);
    const float TextSz  = 10.0f * DmSansEm * Scale;
    bool        Changed = false;
    for (int Index = 0; Index < OptionCount; ++Index)
    {
        const bool   Enabled = EnabledOptions == nullptr || EnabledOptions[Index];
        const ImVec2 CellMin(Min.x + Inset + static_cast<float>(Index) * (CellW + Gap), Min.y + Inset);
        const ImVec2 CellMax(CellMin.x + CellW, Max.y - Inset);
        ImGui::PushID(Id);
        ImGui::PushID(Index);
        ImGui::SetCursorScreenPos(CellMin);
        if (!Enabled) ImGui::BeginDisabled();
        if (ImGui::InvisibleButton("##Segment", ImVec2(CellMax.x - CellMin.x, CellMax.y - CellMin.y)) && Enabled && InOutIndex != Index)
        {
            InOutIndex = Index;
            Changed    = true;
        }
        if (!Enabled) ImGui::EndDisabled();
        ConstructTooltip(Tooltips != nullptr ? Tooltips[Index] : nullptr);
        const bool Hovered = Enabled && ImGui::IsItemHovered();
        ImGui::PopID();
        ImGui::PopID();
        const bool Active = Index == InOutIndex;
        if (Active)
        {
            DrawList->AddRectFilled(ImVec2(CellMin.x, CellMin.y + Scale), ImVec2(CellMax.x, CellMax.y + 2.0f * Scale), SegmentedShadow, 8.0f * Scale);
            DrawList->AddRectFilled(CellMin, CellMax, SegmentedActive, 8.0f * Scale);
            DrawList->AddLine(ImVec2(CellMin.x + 6.0f * Scale, CellMin.y + 0.5f), ImVec2(CellMax.x - 6.0f * Scale, CellMin.y + 0.5f), SegmentedHighlight, Scale);
        }
        const ImU32 Tone  = Active ? IconHoverGlyph : (Hovered ? TextTone : SegmentedText);
        const float TextW = MeasureText(Faces.Regular, TextSz, Options[Index]);
        DrawList->AddText(ResolveFace(Faces.Regular), TextSz, ImVec2(CellMin.x + 0.5f * (CellW - TextW), CellMin.y + 0.5f * (CellMax.y - CellMin.y - TextSz)),
                          FadeColour(Tone, Enabled ? 1.0f : 0.45f), Options[Index]);
    }
    return Changed;
}

bool ViewportChromeControls::ConstructSelectField(const char* Id, const EditorTypefaces& Faces, ImVec2 Min, ImVec2 Size, const char* const* Options,
                                                  int OptionCount, int& InOutIndex, float Scale)
{
    // 📝 .select-field: #191919, 1 px #393939, radius 9 px, padding 9 px, 11 px #c8c8c8; the list opens beneath it.
    if (Options == nullptr || OptionCount <= 0) return false;
    InOutIndex = std::clamp(InOutIndex, 0, OptionCount - 1);
    ImGui::PushID(Id);
    ImGui::SetCursorScreenPos(Min);
    const bool   Pressed  = ImGui::InvisibleButton("##SelectField", Size);
    const bool   Hovered  = ImGui::IsItemHovered();
    ImDrawList*  DrawList = ImGui::GetWindowDrawList();
    const ImVec2 Max(Min.x + Size.x, Min.y + Size.y);
    DrawList->AddRectFilled(Min, Max, SelectSurface, 9.0f * Scale);
    DrawList->AddRect(Min, Max, Hovered ? IconHoverStroke : SelectStroke, 9.0f * Scale, ImDrawFlags_None, Scale);
    const float TextSize = 11.0f * DmSansEm * Scale;
    DrawList->PushClipRect(Min, ImVec2(Max.x - 26.0f * Scale, Max.y), true);
    DrawList->AddText(ResolveFace(Faces.Regular), TextSize, ImVec2(Min.x + 10.0f * Scale, Min.y + 0.5f * (Size.y - TextSize)), SelectText, Options[InOutIndex]);
    DrawList->PopClipRect();
    ConstructLucideGlyph(DrawList, ImVec2(Max.x - 15.0f * Scale, Min.y + 0.5f * Size.y), 13.0f * Scale, SelectText, "ChevronDown");
    if (Pressed) ImGui::OpenPopup("##SelectList");

    bool Changed = false;
    ImGui::SetNextWindowPos(ImVec2(Min.x, Max.y + 4.0f * Scale));
    ImGui::SetNextWindowSizeConstraints(ImVec2(Size.x, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 9.0f * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f * Scale, 6.0f * Scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 2.0f * Scale));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(SelectSurface));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(SelectStroke));
    ImGui::PushStyleColor(ImGuiCol_Header, ImGui::ColorConvertU32ToFloat4(SegmentedActive));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::ColorConvertU32ToFloat4(IconHoverSurface));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(SelectText));
    if (ImGui::BeginPopup("##SelectList"))
    {
        ImGui::PushFont(ResolveFace(Faces.Regular), 11.0f);   // ⚠️ unscaled: ImGui applies the interface scale itself
        for (int Index = 0; Index < OptionCount; ++Index)
        {
            if (ImGui::Selectable(Options[Index], Index == InOutIndex, ImGuiSelectableFlags_None, ImVec2(0.0f, 20.0f * Scale)))
            {
                Changed    = InOutIndex != Index;
                InOutIndex = Index;
            }
        }
        ImGui::PopFont();
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(4);
    ImGui::PopID();
    return Changed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    STAGE OVERLAYS
//------------------------------------------------------------------------------------------------------------------------

bool ViewportChromeControls::ConstructReadoutOverlay(const char* Id, const EditorTypefaces& Faces, ImVec2 StageMin, ImVec2 StageMax,
                                                     const std::vector<std::string>& Lines, const char* CloseTooltip, float Scale)
{
    // 📝 #hud: top/left 12 px, padding 10 px 34 px 10 px 13 px, radius 12 px, 11 px / 1.5 monospace; max-width 100 % − 24 px.
    ImFont*     Mono     = ResolveFace(Faces.Monospace);
    const float TextCss  = 11.0f * Scale;
    const float TextSize = TextCss * JetBrainsMonoEm;
    const float Leading  = 1.5f * TextCss;
    float       Widest   = 0.0f;
    for (const std::string& Line : Lines) Widest = std::max(Widest, MeasureText(Mono, TextSize, Line.c_str()));
    const ImVec2 Min(StageMin.x + 12.0f * Scale, StageMin.y + 12.0f * Scale);
    const float  Width  = std::min(Widest + 47.0f * Scale + 2.0f * Scale, std::max(0.0f, StageMax.x - StageMin.x - 24.0f * Scale));
    const float  Height = Leading * static_cast<float>(Lines.size()) + 20.0f * Scale + 2.0f * Scale;
    const ImVec2 Max(Min.x + Width, Min.y + Height);
    ImDrawList*  DrawList = ImGui::GetWindowDrawList();
    DrawList->AddRectFilled(Min, Max, ReadoutSurface, 12.0f * Scale);
    DrawList->AddRect(Min, Max, ReadoutStroke, 12.0f * Scale, ImDrawFlags_None, Scale);
    DrawList->PushClipRect(Min, ImVec2(Max.x - 34.0f * Scale, Max.y), true);
    for (size_t Line = 0; Line < Lines.size(); ++Line)
    {
        DrawList->AddText(Mono, TextSize, ImVec2(Min.x + 14.0f * Scale, Min.y + 11.0f * Scale + Leading * static_cast<float>(Line) + ResolveHalfLeading(1.5f, JetBrainsMonoEm) * TextCss),
                          ReadoutText, Lines[Line].c_str());
    }
    DrawList->PopClipRect();

    // 📝 .overlay-close at top/right 6 px.
    return ConstructOverlayClose(Id, Faces, ImVec2(Max.x - 26.0f * Scale, Min.y + 6.0f * Scale), CloseTooltip, Scale);
}



void ViewportChromeControls::ConstructOverlayNotice(ImDrawList* DrawList, const EditorTypefaces& Faces, ImVec2 StageMin, ImVec2 StageMax, const char* Text,
                                                    float Scale)
{
    // 📝 #overlay-notice: bottom 18 px, centred, padding 7 px 14 px, radius 10 px, 11 px #bdbdbd.
    if (Text == nullptr || Text[0] == '\0') return;
    const float  Size  = 11.0f * DmSansEm * Scale;
    const float  Width = MeasureText(Faces.Regular, Size, Text) + 28.0f * Scale + 2.0f * Scale;
    const float  Tall  = 11.0f * 1.35f * Scale + 14.0f * Scale + 2.0f * Scale;
    const ImVec2 Min(0.5f * (StageMin.x + StageMax.x - Width), StageMax.y - 18.0f * Scale - Tall);
    const ImVec2 Max(Min.x + Width, Min.y + Tall);
    DrawList->AddRectFilled(Min, Max, NoticeSurface, 10.0f * Scale);
    DrawList->AddRect(Min, Max, CloseStroke, 10.0f * Scale, ImDrawFlags_None, Scale);
    DrawList->AddText(ResolveFace(Faces.Regular), Size, ImVec2(Min.x + 15.0f * Scale, Min.y + 0.5f * (Tall - Size)), NoticeText, Text);
}

void ViewportChromeControls::ConstructBringUpCard(ImDrawList* DrawList, const EditorTypefaces& Faces, ImVec2 StageMin, ImVec2 StageMax, const char* Heading,
                                                  const char* Caption, float Fraction, float Scale)
{
    // 📝 #bring-up: veil over the stage; h2 15 px / 500 #e2e2e2, caption 11 px muted, 360 × 3 px bar; 12 px gaps; centred.
    DrawList->AddRectFilled(StageMin, StageMax, BringUpVeil);
    const float HeadingCss  = 15.0f * Scale;
    const float CaptionCss  = 11.0f * Scale;
    const float HeadingSize = HeadingCss * DmSansEm;
    const float CaptionSize = CaptionCss * DmSansEm;
    const float HalfLeading = ResolveHalfLeading(1.35f, DmSansEm);
    const float Gap         = 12.0f * Scale;
    const float BarW        = 360.0f * Scale;
    const float BarH        = 3.0f * Scale;
    const float Block       = HeadingCss * 1.35f + Gap + CaptionCss * 1.35f + Gap + BarH;
    const float CentreX     = 0.5f * (StageMin.x + StageMax.x);
    float       Pen         = 0.5f * (StageMin.y + StageMax.y - Block);
    DrawList->AddText(ResolveFace(Faces.Medium), HeadingSize,
                      ImVec2(CentreX - 0.5f * MeasureText(Faces.Medium, HeadingSize, Heading), Pen + HalfLeading * HeadingCss), BringUpHeading, Heading);
    Pen += HeadingCss * 1.35f + Gap;
    DrawList->AddText(ResolveFace(Faces.Regular), CaptionSize,
                      ImVec2(CentreX - 0.5f * MeasureText(Faces.Regular, CaptionSize, Caption), Pen + HalfLeading * CaptionCss), MutedTone, Caption);
    Pen += CaptionCss * 1.35f + Gap;
    const ImVec2 BarMin(CentreX - 0.5f * BarW, Pen);
    DrawList->AddRectFilled(BarMin, ImVec2(BarMin.x + BarW, BarMin.y + BarH), EdgeStroke, 2.0f * Scale);
    const float Filled = BarW * std::clamp(Fraction, 0.0f, 1.0f);
    if (Filled > 0.0f) DrawList->AddRectFilled(BarMin, ImVec2(BarMin.x + Filled, BarMin.y + BarH), BringUpFill, 2.0f * Scale);
}

void ViewportChromeControls::ConstructFailurePanel(ImDrawList* DrawList, const EditorTypefaces& Faces, ImVec2 StageMin, ImVec2 StageMax, const std::string& Text,
                                                   float Scale)
{
    // 📝 #failure: left/bottom 12 px, max 70 % × 40 %, padding 10 px 14 px, radius 10 px, 11 px / 1.45 monospace, pre-wrap.
    if (Text.empty()) return;
    ImFont*      Mono     = ResolveFace(Faces.Monospace);
    const float  Size     = 11.0f * JetBrainsMonoEm * Scale;
    const float  MaxWidth = 0.70f * (StageMax.x - StageMin.x);
    const float  WrapW    = std::max(40.0f * Scale, MaxWidth - 28.0f * Scale - 2.0f * Scale);
    const ImVec2 TextSize = Mono->CalcTextSizeA(Size, FLT_MAX, WrapW, Text.c_str());
    const float  Width    = std::min(MaxWidth, TextSize.x + 28.0f * Scale + 2.0f * Scale);
    const float  Height   = std::min(0.40f * (StageMax.y - StageMin.y), TextSize.y + 20.0f * Scale + 2.0f * Scale);
    const ImVec2 Min(StageMin.x + 12.0f * Scale, StageMax.y - 12.0f * Scale - Height);
    const ImVec2 Max(Min.x + Width, Min.y + Height);
    DrawList->AddRectFilled(Min, Max, FailureSurface, 10.0f * Scale);
    DrawList->AddRect(Min, Max, FailTone, 10.0f * Scale, ImDrawFlags_None, Scale);
    DrawList->PushClipRect(Min, Max, true);
    DrawList->AddText(Mono, Size, ImVec2(Min.x + 15.0f * Scale, Min.y + 11.0f * Scale), FailureText, Text.c_str(), nullptr, WrapW);
    DrawList->PopClipRect();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    STATUS STRIP
//------------------------------------------------------------------------------------------------------------------------

void ViewportChromeControls::ConstructStatusStrip(ImDrawList* DrawList, const EditorTypefaces& Faces, ImVec2 Min, ImVec2 Max,
                                                  const std::vector<ViewportStatusItem>& Leading, const std::vector<ViewportStatusItem>& Trailing,
                                                  float Scale)
{
    // 📝 #status: #0d0d0d, 1 px edge on top, padding 0 16 px, 22 px between items; caption 8 px letter-spacing 1.3 px #626262,
    //    8 px gap, measurement 10 px #cfcfcf; captions and measurements share a baseline.
    DrawList->AddRectFilled(Min, Max, StatusSurface);
    DrawList->AddLine(ImVec2(Min.x, Min.y + 0.5f * Scale), ImVec2(Max.x, Min.y + 0.5f * Scale), EdgeStroke, Scale);
    const float CaptionSize     = 8.0f * DmSansEm * Scale;
    const float MeasurementSize = 10.0f * DmSansEm * Scale;
    const float Spacing         = 1.3f * Scale;
    const float MeasurementTop  = 0.5f * (Min.y + Max.y - MeasurementSize);
    const float CaptionTop      = MeasurementTop + 0.8f * (MeasurementSize - CaptionSize);
    auto MeasureItem = [&](const ViewportStatusItem& Item) {
        return (Item.Glyph ? 18 * Scale : 0) + ConstructSpacedText(nullptr, Faces.Regular, CaptionSize, ImVec2(), 0, Item.Caption.c_str(), Spacing) + 8.0f * Scale +
               MeasureText(Faces.Regular, MeasurementSize, Item.Measurement.c_str());
    };
    auto ConstructItem = [&](const ViewportStatusItem& Item, float Pen) {
        if (Item.Glyph)
        {
            GlyphSymbol::Construct(DrawList,Item.Glyph,{Pen,(Min.y+Max.y)*.5f-6*Scale},12*Scale,1.6f,Item.Colour ? Item.Colour : StatusMeasurement);
            Pen += 18*Scale;
        }
        const float CaptionW = ConstructSpacedText(DrawList, Faces.Regular, CaptionSize, ImVec2(Pen, CaptionTop), StatusCaption, Item.Caption.c_str(), Spacing);
        DrawList->AddText(ResolveFace(Faces.Regular), MeasurementSize, ImVec2(Pen + CaptionW + 8.0f * Scale, MeasurementTop), Item.Colour ? Item.Colour : StatusMeasurement,
                          Item.Measurement.c_str());
    };
    float TrailingWidth = 0.0f;
    for (const ViewportStatusItem& Item : Trailing) TrailingWidth += MeasureItem(Item) + 22.0f * Scale;
    const float TrailingStart = Max.x - 16.0f * Scale - std::max(0.0f, TrailingWidth - 22.0f * Scale);
    DrawList->PushClipRect(Min, ImVec2(Trailing.empty() ? Max.x : TrailingStart - 22.0f * Scale, Max.y), true);
    float Pen = Min.x + 16.0f * Scale;
    for (const ViewportStatusItem& Item : Leading)
    {
        ConstructItem(Item, Pen);
        Pen += MeasureItem(Item) + 22.0f * Scale;
    }
    DrawList->PopClipRect();
    Pen = TrailingStart;
    for (const ViewportStatusItem& Item : Trailing)
    {
        ConstructItem(Item, Pen);
        Pen += MeasureItem(Item) + 22.0f * Scale;
    }
}

} // namespace Frontier
