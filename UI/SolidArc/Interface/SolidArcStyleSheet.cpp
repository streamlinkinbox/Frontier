/*====================================================================================================================================
                                                        SOLIDARCSTYLESHEET.CPP
====================================================================================================================================*/
// 🧩 The HTML SolidArc style sheet on PixelSpace (see header)

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "UI/SolidArc/Interface/SolidArcStyleSheet.h"

#include "Engine/Interface/ControlKit.h"
#include "Engine/Interface/IconPresentation.h"

#include <imgui_internal.h>

#include <algorithm>
#include <string>

namespace Frontier::SolidArc
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    HELPERS
//------------------------------------------------------------------------------------------------------------------------

SolidArcTypefaces ActiveTypefaces = {};

ImFont* FaceOf(SolidArcWeight Weight)
{
    void* Face = Weight == SolidArcWeight::Regular ? ActiveTypefaces.Regular : ActiveTypefaces.Light;
    return Face ? static_cast<ImFont*>(Face) : ImGui::GetFont();
}

// Length of the UTF-8 sequence that starts with Lead.
int SequenceLength(unsigned char Lead)
{
    return Lead < 0x80 ? 1 : (Lead >> 5) == 0x6 ? 2 : (Lead >> 4) == 0xE ? 3 : (Lead >> 3) == 0x1E ? 4 : 1;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ConfigureSolidArcTypefaces(const SolidArcTypefaces& Typefaces)
{
    ActiveTypefaces = Typefaces;
}

const SolidArcTypefaces& QuerySolidArcTypefaces()
{
    return ActiveTypefaces;
}

ColorQuad SolidArcInk(uint32_t Rgb, float Alpha)
{
    return ColorQuad{ float((Rgb >> 16) & 0xFF) / 255.0f, float((Rgb >> 8) & 0xFF) / 255.0f, float(Rgb & 0xFF) / 255.0f, Alpha };
}

ColorQuad SolidArcWash(uint32_t Alpha)
{
    return ColorQuad{ 1.0f, 1.0f, 1.0f, float(Alpha & 0xFF) / 255.0f };
}

ImU32 SolidArcPack(ColorQuad Ink)
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(Ink.Red, Ink.Green, Ink.Blue, Ink.Alpha));
}

SolidArcPress PressSolidArcRegion(SolidArcWidgetBoard& Widgets, const std::string& Key, const PlaneExtent& Extent, bool EnabledCondition)
{
    SolidArcPress Press;
    if (Extent.Width() <= 0.0f || Extent.Height() <= 0.0f) return Press;
    const ImVec2 Restore = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(Extent.MinimumX, Extent.MinimumY));
    ImGui::BeginDisabled(!EnabledCondition);
    Press.ClickedCondition = ImGui::InvisibleButton(Key.c_str(), ImVec2(Extent.Width(), Extent.Height()));
    ImGui::EndDisabled();
    Press.HoveredCondition = EnabledCondition && ImGui::IsItemHovered();
    Press.HeldCondition    = EnabledCondition && ImGui::IsItemActive();
    Press.DoubleCondition  = Press.HoveredCondition && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    Widgets.Record(Key, ImVec2(Extent.MinimumX, Extent.MinimumY), ImVec2(Extent.MaximumX, Extent.MaximumY));
    ImGui::SetCursorScreenPos(Restore);
    return Press;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    WIDGET BOARD
//------------------------------------------------------------------------------------------------------------------------

void SolidArcWidgetBoard::Record(const std::string& Key, ImVec2 Low, ImVec2 High)
{
    ImGuiWindow* Window = ImGui::GetCurrentContext() ? ImGui::GetCurrentContext()->CurrentWindow : nullptr;
    if (Window && Window->SkipItems)
    {
        // ⚠️ A child scrolled wholly out of view skips its items, so the last-item rectangle is stale: stand in the
        //    child's own rectangle so a reveal can scroll to it (the real rectangle comes back on the next frame).
        Rectangles[Key] = ImVec4(Window->Pos.x, Window->Pos.y, Window->Pos.x + Window->Size.x, Window->Pos.y + Window->Size.y);
        Clipped.insert(Key);
        Low  = Window->Pos;
        High = ImVec2(Window->Pos.x + Window->Size.x, Window->Pos.y + Window->Size.y);
    }
    else Rectangles[Key] = ImVec4(Low.x, Low.y, High.x, High.y);
    // Screen-space metadata can also be registered after all explicit windows have ended.
    // The implicit fallback is not a scrollable control surface; querying its draw list exposes Debug.
    if (!Window || Window->IsFallbackWindow) return;
    const ImVec2 ClipLow  = ImGui::GetWindowDrawList()->GetClipRectMin();
    const ImVec2 ClipHigh = ImGui::GetWindowDrawList()->GetClipRectMax();
    constexpr float Slack = 0.5f;                                                            // [px] - Sub-pixel rounding
    const bool Inside = Low.x >= ClipLow.x - Slack && Low.y >= ClipLow.y - Slack && High.x <= ClipHigh.x + Slack && High.y <= ClipHigh.y + Slack;
    if (!Inside) Clipped.insert(Key);
    if (Reveal != Key) return;
    // Centre the widget in the nearest window that scrolls (cards are auto-sized children): the change lands next frame.
    // Per axis, since a card may overflow sideways without scrolling vertically.
    const auto Scrolling = [Window](int Axis)
    {
        ImGuiWindow* Scroller = Window;
        while (Scroller->ParentWindow && (Scroller->Flags & ImGuiWindowFlags_ChildWindow) && Scroller->ScrollMax[Axis] <= 0.0f) Scroller = Scroller->ParentWindow;
        return Scroller;
    };
    if (Low.y < ClipLow.y - Slack || High.y > ClipHigh.y + Slack)
    {
        ImGuiWindow* Scroller = Scrolling(1);
        const float  Shift    = 0.5f * (Low.y + High.y) - 0.5f * (Scroller->InnerClipRect.Min.y + Scroller->InnerClipRect.Max.y);
        ImGui::SetScrollY(Scroller, std::clamp(Scroller->Scroll.y + Shift, 0.0f, Scroller->ScrollMax.y));
    }
    if (Low.x < ClipLow.x - Slack || High.x > ClipHigh.x + Slack)
    {
        ImGuiWindow* Scroller = Scrolling(0);
        const float  Shift    = 0.5f * (Low.x + High.x) - 0.5f * (Scroller->InnerClipRect.Min.x + Scroller->InnerClipRect.Max.x);
        ImGui::SetScrollX(Scroller, std::clamp(Scroller->Scroll.x + Shift, 0.0f, Scroller->ScrollMax.x));
    }
    Reveal.clear();
}

void SolidArcWidgetBoard::Clear()
{
    Previous.swap(Rectangles);
    Rectangles.clear();
    Clipped.clear();
}

// The widget sat on the same rectangle for the last two frames (ImGui clamps a scroll one frame after content shrinks).
bool SolidArcWidgetBoard::SettledCondition(const std::string& Key) const
{
    const auto Now    = Rectangles.find(Key);
    const auto Before = Previous.find(Key);
    if (Now == Rectangles.end() || Before == Previous.end()) return false;
    const ImVec4& A = Now->second;
    const ImVec4& B = Before->second;
    return A.x == B.x && A.y == B.y && A.z == B.z && A.w == B.w;
}

const ImVec4* SolidArcWidgetBoard::Find(const std::string& Key) const
{
    const auto Found = Rectangles.find(Key);
    return Found == Rectangles.end() ? nullptr : &Found->second;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SHEET
//------------------------------------------------------------------------------------------------------------------------

bool SolidArcSheet::Begin()
{
    const ImVec2 Display = ImGui::GetIO().DisplaySize;
    return Space.Begin(SurfaceLayer::Window, Display.x, Display.y, 1.0f);
}

void SolidArcSheet::Box(const PlaneExtent& Extent, ColorQuad Fill, float Radius, ColorQuad Border)
{
    if (Fill.Alpha > 0.0f) Space.FillRectangle(Extent, Fill, Radius);
    if (Border.Alpha > 0.0f)
    {
        // 📝 CSS draws the 1 px border inside the border box: stroke along the half-pixel inset.
        const PlaneExtent Inset{ Extent.MinimumX + 0.5f, Extent.MinimumY + 0.5f, Extent.MaximumX - 0.5f, Extent.MaximumY - 0.5f };
        ControlKit::OutlineRounded(Space, Inset, Border, std::max(0.0f, Radius - 0.5f), 1.0f);
    }
}

void SolidArcSheet::Line(float FromX, float FromY, float ToX, float ToY, ColorQuad Ink, float Thickness)
{
    const PlanePoint Points[2] = { { FromX, FromY }, { ToX, ToY } };
    Space.StrokePolyline(Points, 2, Ink, Thickness, false);
}

float SolidArcSheet::Measure(const char* Utf8, float CssSize, SolidArcWeight Weight, float LetterSpacing)
{
    if (!Utf8 || !*Utf8) return 0.0f;
    ImFont*     Face  = FaceOf(Weight);
    const float Size  = CssSize * SolidArcStyleTokens::FontScale;
    float       Width = Face->CalcTextSizeA(Size, FLT_MAX, 0.0f, Utf8).x;
    if (LetterSpacing != 0.0f)
    {
        int Count = 0;
        for (const char* Cursor = Utf8; *Cursor; Cursor += SequenceLength(static_cast<unsigned char>(*Cursor))) ++Count;
        Width += LetterSpacing * float(Count);
    }
    return Width;
}

float SolidArcSheet::Text(float X, float CentreY, float CssSize, ColorQuad Ink, const char* Utf8, SolidArcWeight Weight, float LetterSpacing)
{
    if (!Utf8 || !*Utf8) return 0.0f;
    ImFont*     Face = FaceOf(Weight);
    const float Size = CssSize * SolidArcStyleTokens::FontScale;
    const float Top  = CentreY - 0.5f * Size;                                                // [px] - Line box top (line-height: normal)
    Space.PushTypeface(Face);
    if (LetterSpacing == 0.0f)
    {
        Space.Text(X, Top, Ink, Utf8, Size);
        Space.PopTypeface();
        return Face->CalcTextSizeA(Size, FLT_MAX, 0.0f, Utf8).x;
    }
    float Advance = 0.0f;
    for (const char* Cursor = Utf8; *Cursor;)
    {
        const int         Length = SequenceLength(static_cast<unsigned char>(*Cursor));
        const std::string Glyph(Cursor, size_t(Length));
        Space.Text(X + Advance, Top, Ink, Glyph.c_str(), Size);
        Advance += Face->CalcTextSizeA(Size, FLT_MAX, 0.0f, Glyph.c_str()).x + LetterSpacing;
        Cursor  += Length;
    }
    Space.PopTypeface();
    return Advance;
}

float SolidArcSheet::TextRight(float Right, float CentreY, float CssSize, ColorQuad Ink, const char* Utf8, SolidArcWeight Weight, float LetterSpacing)
{
    const float Width = Measure(Utf8, CssSize, Weight, LetterSpacing);
    Text(Right - Width, CentreY, CssSize, Ink, Utf8, Weight, LetterSpacing);
    return Width;
}

float SolidArcSheet::TextClipped(float X, float CentreY, float Width, float CssSize, ColorQuad Ink, const char* Utf8, SolidArcWeight Weight)
{
    if (!Utf8) return 0.0f;
    if (Measure(Utf8, CssSize, Weight) <= Width) return Text(X, CentreY, CssSize, Ink, Utf8, Weight);
    std::string Shown = Utf8;
    while (!Shown.empty())
    {
        do Shown.pop_back();
        while (!Shown.empty() && (static_cast<unsigned char>(Shown.back()) & 0xC0u) == 0x80u);
        if (!Shown.empty() && (static_cast<unsigned char>(Shown.back()) & 0xC0u) == 0xC0u) Shown.pop_back();
        if (Measure((Shown + "…").c_str(), CssSize, Weight) <= Width) break;
    }
    return Text(X, CentreY, CssSize, Ink, (Shown + "…").c_str(), Weight);
}

void SolidArcSheet::Icon(IconSymbol Symbol, float X, float Y, float Size, ColorQuad Ink)
{
    IconPresentation::Draw(ImGui::GetWindowDrawList(), Symbol, ImVec2(X, Y), Size, 1.0f, IconDomain::SolidArc, SolidArcPack(Ink));
}

} // namespace Frontier::SolidArc
