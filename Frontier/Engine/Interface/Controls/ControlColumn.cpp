/*==========================================================================================================================================
                                                             CONTROLCOLUMN.CPP
==========================================================================================================================================*/
// 🧩 Lays one Inspector page down the column: CSS block margins, the page fill and padding, and the
//    instrument cards (fill, gradient, radial glow, border, heading, index).

#include "Engine/Interface/Controls/ControlColumn.h"

#include "Engine/Interface/Controls/DiagramPen.h"
#include "Engine/Interface/Controls/GlyphRun.h"
#include "Engine/Interface/Controls/GlyphSymbol.h"
#include "Engine/Interface/InspectorControls.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

constexpr float PiSingle = 3.14159265358979f;

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    LAYOUT
//------------------------------------------------------------------------------------------------------------------------

ControlColumn::ControlColumn(const ControlTheme& InTheme, const EditorTypefaces& InFaces, float InScale)
    : Theme(InTheme), Faces(InFaces), DrawList(ImGui::GetWindowDrawList()), Scale(std::max(0.5f, InScale))
{
    // 📝 .native-environment: the page fill under the whole visible column, 20 px padding around the page.
    const ImVec2 WindowMin = ImGui::GetWindowPos();
    const ImVec2 WindowMax(WindowMin.x + ImGui::GetWindowWidth(), WindowMin.y + ImGui::GetWindowHeight());
    DrawList->AddRectFilled(WindowMin, WindowMax, Theme.PageFill);
    const ImVec2 Start = ImGui::GetCursorScreenPos();
    Left               = Start.x + Theme.PaddingSide * Scale;
    Width              = std::max(120.0f * Scale, ImGui::GetContentRegionAvail().x - 2.0f * Theme.PaddingSide * Scale);
    PageWidth          = Width;
    Cursor             = Start.y + Theme.PaddingTop * Scale;
    Lowest             = Cursor;
}

ImVec2 ControlColumn::Space(float CssHeight, float MarginTop, float MarginBottom)
{
    Cursor += std::max(PendingMargin, MarginTop) * Scale;
    const ImVec2 Top(Left, Cursor);
    Cursor += CssHeight * Scale;
    PendingMargin = MarginBottom;
    Lowest        = std::max(Lowest, Cursor);
    return Top;
}

void ControlColumn::Finalize()
{
    const float Bottom = Cursor + (PendingMargin + Theme.PaddingSide) * Scale;
    ImGui::SetCursorScreenPos(ImVec2(Left - Theme.PaddingSide * Scale, Bottom));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
}

void ControlColumn::ConstructTooltip(const char* Tooltip)
{
    if (Tooltip != nullptr && Tooltip[0] != '\0' && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
    {
        ImGui::SetTooltip("%s", Tooltip);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CARDS
//------------------------------------------------------------------------------------------------------------------------

void ControlColumn::BeginCard(const CardLook& Look, const char* Heading, const char* Index)
{
    const float  S   = Scale;
    const ImVec2 Top = Space(0.0f, Look.MarginTop, 0.0f);
    CardScope    Scope;
    Scope.Look       = Look;
    Scope.Top        = Top.y;
    Scope.OuterLeft  = Left;
    Scope.OuterWidth = Width;
    Cards.push_back(Scope);
    DrawList->ChannelsSplit(2);
    DrawList->ChannelsSetCurrent(1);

    const float Border = (Look.Border != 0 || Look.TopRule != 0) ? 1.0f : 0.0f;
    const float Sides  = (Look.Border != 0 ? 1.0f : 0.0f) + Look.PaddingSide;
    const float Rule   = Look.LeftRule != 0 ? Look.LeftRuleWidth : 0.0f;
    Left += (Sides + Rule) * S;
    Width -= (2.0f * Sides + Rule) * S;
    Cursor        = Top.y + (Border + Look.PaddingTop) * S;
    PendingMargin = 0.0f;

    if (Heading != nullptr || Index != nullptr)
    {
        // 📝 .co-card-heading: 12 px, space-between, margin-bottom 14 px; .co-index 8px/1.8, letter-spacing 1 px.
        const ImVec2 HeadingTop = Space(12.0f * 1.4f, 0.0f, Look.HeadingMargin);
        if (Heading != nullptr)
        {
            if (Look.HeadingGlyph != nullptr)
                GlyphSymbol::Construct(DrawList, Look.HeadingGlyph, {HeadingTop.x, HeadingTop.y}, 17 * S, 1.5f, Theme.Accent);
            (void)GlyphRun::Construct(*this, Faces.Regular, 12.0f * S, ImVec2(HeadingTop.x + (Look.HeadingGlyph ? 27 : 0) * S, GlyphRun::ResolveBaseline(HeadingTop.y, 12.0f * S, 1.4f)), Theme.CardHeading,
                                    Heading, -0.1f * S);
        }
        if (Index != nullptr)
        {
            const float Advance = GlyphRun::Measure(*this, Faces.Regular, 8.0f * S, Index, 1.0f * S);
            (void)GlyphRun::Construct(*this, Faces.Regular, 8.0f * S, ImVec2(HeadingTop.x + Width - Advance, GlyphRun::ResolveBaseline(HeadingTop.y, 8.0f * S, 1.8f)),
                                    Theme.Index, Index, 1.0f * S);
        }
    }
}

void ControlColumn::PushIndent(float LeftCss, float RightCss)
{
    Indents.push_back(ImVec2(Left, Width));
    Left += LeftCss * Scale;
    Width = std::max(1.0f, Width - (LeftCss + RightCss) * Scale);
}

void ControlColumn::PopIndent()
{
    if (Indents.empty()) return;
    Left  = Indents.back().x;
    Width = Indents.back().y;
    Indents.pop_back();
}

ImVec2 ControlColumn::QueryCardOrigin() const noexcept
{
    return Cards.empty() ? ImVec2(Left, Cursor) : ImVec2(Cards.back().OuterLeft, Cards.back().Top);
}

float ControlColumn::QueryCardWidth() const noexcept
{
    return Cards.empty() ? Width : Cards.back().OuterWidth;
}

void ControlColumn::EndCard()
{
    if (Cards.empty()) return;
    const CardScope Scope = Cards.back();
    Cards.pop_back();
    const CardLook& Look   = Scope.Look;
    const float                S      = Scale;
    const float                Border = (Look.Border != 0 || Look.TopRule != 0) ? 1.0f : 0.0f;
    const float                Bottom = Cursor + (PendingMargin + Look.PaddingBottom + (Look.Border != 0 ? 1.0f : 0.0f)) * S;
    const ImVec2               Min(Scope.OuterLeft, Scope.Top);
    const ImVec2               Max(Scope.OuterLeft + Scope.OuterWidth, Bottom);
    (void)Border;

    DrawList->ChannelsSetCurrent(0);
    const float Rounding = Look.Radius * S;
    if (Look.Fill != 0)
    {
        const int VertexStart = DrawList->VtxBuffer.Size;
        DrawList->AddRectFilled(Min, Max, Look.Fill, Rounding, Look.Corners);
        if (Look.FillEnd != 0)
        {
            // 📝 linear-gradient(125deg, Fill, FillEnd 65%): the gradient line through the centre at 125° (0° = up).
            const float  Angle = Look.GradientDegrees * PiSingle / 180.0f;
            const ImVec2 Direction(std::sin(Angle), -std::cos(Angle));
            const ImVec2 Size(Max.x - Min.x, Max.y - Min.y);
            const float  Length = std::fabs(Size.x * Direction.x) + std::fabs(Size.y * Direction.y);
            const ImVec2 Centre(0.5f * (Min.x + Max.x), 0.5f * (Min.y + Max.y));
            const ImVec2 From(Centre.x - Direction.x * 0.5f * Length, Centre.y - Direction.y * 0.5f * Length);
            const ImVec2 To(From.x + Direction.x * Look.GradientStop * Length, From.y + Direction.y * Look.GradientStop * Length);
            ImGui::ShadeVertsLinearColorGradientKeepAlpha(DrawList, VertexStart, DrawList->VtxBuffer.Size, From, To, Look.Fill, Look.FillEnd);
        }
    }
    if (Look.Glow != 0)
    {
        // 📝 radial-gradient(ellipse at X% Y%, Glow, transparent Extent): the farthest-corner ellipse.
        const ImVec2 Size(Max.x - Min.x, Max.y - Min.y);
        const ImVec2 Centre(Min.x + Look.GlowCentre.x * Size.x, Min.y + Look.GlowCentre.y * Size.y);
        const float  FarX = std::max(Centre.x - Min.x, Max.x - Centre.x);
        const float  FarY = std::max(Centre.y - Min.y, Max.y - Centre.y);
        const ImVec2 Radius(FarX * 1.41421356f * Look.GlowExtent, FarY * 1.41421356f * Look.GlowExtent);
        DrawList->PushClipRect(Min, Max, true);
        DiagramPen::RasterizeRadialFan(DrawList, Centre, Radius, Look.Glow, ResolveAlpha(Look.Glow, 0.0f), 64);
        DrawList->PopClipRect();
    }
    if (Look.Border != 0)
    {
        DrawList->AddRect(ImVec2(Min.x + 0.5f * S, Min.y + 0.5f * S), ImVec2(Max.x - 0.5f * S, Max.y - 0.5f * S), Look.Border, Rounding,
                          ImDrawFlags_None, S);
    }
    else if (Look.TopRule != 0)
    {
        DrawList->AddLine(ImVec2(Min.x, Min.y + 0.5f * S), ImVec2(Max.x, Min.y + 0.5f * S), Look.TopRule, S);
    }
    if (Look.LeftRule != 0)
    {
        DrawList->AddRectFilled(Min, ImVec2(Min.x + Look.LeftRuleWidth * S, Max.y), Look.LeftRule);
    }
    DrawList->ChannelsMerge();

    Left          = Scope.OuterLeft;
    Width         = Scope.OuterWidth;
    Cursor        = Bottom;
    PendingMargin = Look.MarginBottom;
    Lowest        = std::max(Lowest, Cursor);
}

} // namespace Frontier
