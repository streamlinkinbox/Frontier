/*==========================================================================================================================================
                                                               DIAGRAMPEN.CPP
==========================================================================================================================================*/
// 🧩 The demo's canvas Pen (NativeEnvironmentControls.js) in a diagram's logical coordinates, plus the radial-fan
//    and dashed-segment rasters the cards and diagrams share.

#include "Engine/Interface/Controls/DiagramPen.h"

#include "Engine/Interface/Controls/ControlTheme.h"
#include "Engine/Interface/Controls/GlyphRun.h"
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
//                                                    RASTER PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

void DiagramPen::RasterizeRadialFan(ImDrawList* DrawList, ImVec2 Centre, ImVec2 Radius, ImU32 CentreColour, ImU32 RimColour,
                                    int Segments)
{
    if (Radius.x <= 0.5f || Radius.y <= 0.5f) return;
    const ImVec2 WhiteUv = DrawList->_Data->TexUvWhitePixel;
    DrawList->PrimReserve(Segments * 3, Segments + 1);
    const ImDrawIdx FanOrigin = static_cast<ImDrawIdx>(DrawList->_VtxCurrentIdx);
    DrawList->PrimWriteVtx(Centre, WhiteUv, CentreColour);
    for (int Segment = 0; Segment < Segments; ++Segment)
    {
        const float Angle = 2.0f * PiSingle * static_cast<float>(Segment) / static_cast<float>(Segments);
        DrawList->PrimWriteVtx(ImVec2(Centre.x + std::cos(Angle) * Radius.x, Centre.y + std::sin(Angle) * Radius.y), WhiteUv, RimColour);
    }
    for (int Segment = 0; Segment < Segments; ++Segment)
    {
        DrawList->PrimWriteIdx(FanOrigin);
        DrawList->PrimWriteIdx(static_cast<ImDrawIdx>(FanOrigin + 1 + Segment));
        DrawList->PrimWriteIdx(static_cast<ImDrawIdx>(FanOrigin + 1 + (Segment + 1) % Segments));
    }
}

void DiagramPen::RasterizeDashedSegment(ImDrawList* DrawList, ImVec2 From, ImVec2 To, ImU32 Colour, float Width, float DashOn,
                                        float DashOff)
{
    const ImVec2 Span(To.x - From.x, To.y - From.y);
    const float  Length = std::sqrt(Span.x * Span.x + Span.y * Span.y);
    if (Length <= 0.0f) return;
    if (DashOn <= 0.0f || DashOff <= 0.0f)
    {
        DrawList->AddLine(From, To, Colour, Width);
        return;
    }
    const ImVec2 Unit(Span.x / Length, Span.y / Length);
    for (float Offset = 0.0f; Offset < Length; Offset += DashOn + DashOff)
    {
        const float End = std::min(Length, Offset + DashOn);
        DrawList->AddLine(ImVec2(From.x + Unit.x * Offset, From.y + Unit.y * Offset), ImVec2(From.x + Unit.x * End, From.y + Unit.y * End), Colour,
                          Width);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DIAGRAM PEN
//------------------------------------------------------------------------------------------------------------------------

DiagramPen::DiagramPen(ImDrawList* InDrawList, const EditorTypefaces* InFaces, ImVec2 InOrigin, float InScale, float InLogicalWidth,
                               float InLogicalHeight) noexcept
    : DrawList(InDrawList), Faces(InFaces), Origin(InOrigin), Scale(InScale), LogicalWidth(InLogicalWidth), LogicalHeight(InLogicalHeight)
{
}

ImVec2 DiagramPen::QueryScreen(ImVec2 Logical) const noexcept
{
    return ImVec2(Origin.x + Logical.x * Scale, Origin.y + Logical.y * Scale);
}

ImVec2 DiagramPen::QueryLogical(ImVec2 Screen) const noexcept
{
    return ImVec2((Screen.x - Origin.x) / Scale, (Screen.y - Origin.y) / Scale);
}

void DiagramPen::Line(ImVec2 From, ImVec2 To, ImU32 Colour, float Width, float DashOn, float DashOff) const
{
    if (DrawList == nullptr) return;
    DiagramPen::RasterizeDashedSegment(DrawList, QueryScreen(From), QueryScreen(To), Colour, std::max(0.5f, Width * Scale), DashOn * Scale, DashOff * Scale);
}

void DiagramPen::Path(const std::vector<ImVec2>& Points, ImU32 Colour, float Width, float DashOn, float DashOff) const
{
    if (DrawList == nullptr || Points.size() < 2) return;
    if (DashOn > 0.0f && DashOff > 0.0f)
    {
        for (size_t Index = 1; Index < Points.size(); ++Index) Line(Points[Index - 1], Points[Index], Colour, Width, DashOn, DashOff);
        return;
    }
    for (const ImVec2& Point : Points) DrawList->PathLineTo(QueryScreen(Point));
    DrawList->PathStroke(Colour, ImDrawFlags_None, std::max(0.5f, Width * Scale));
}

void DiagramPen::Circle(ImVec2 Centre, float Radius, ImU32 Fill, ImU32 Stroke) const
{
    Ellipse(Centre, Radius, Radius, Fill, Stroke);
}

void DiagramPen::Ellipse(ImVec2 Centre, float RadiusX, float RadiusY, ImU32 Fill, ImU32 Stroke) const
{
    if (DrawList == nullptr) return;
    const ImVec2 Screen = QueryScreen(Centre);
    const ImVec2 Radius(RadiusX * Scale, RadiusY * Scale);
    const int    Segments = std::clamp(static_cast<int>(std::max(Radius.x, Radius.y) * 0.9f), 16, 96);
    if (Fill != 0) DrawList->AddEllipseFilled(Screen, Radius, Fill, 0.0f, Segments);
    if (Stroke != 0) DrawList->AddEllipse(Screen, Radius, Stroke, 0.0f, Segments, 1.0f);
}

void DiagramPen::Text(ImVec2 Baseline, const char* Caption, ImU32 Colour, float Size, int Alignment) const
{
    if (DrawList == nullptr || Caption == nullptr || Caption[0] == '\0') return;
    ImFont*      Face    = Faces != nullptr ? Faces->Regular : nullptr;
    const float  CssSize = Size * Scale;
    const float  Advance = GlyphRun::Rasterize(nullptr, Faces, Face, CssSize, ImVec2(0.0f, 0.0f), 0, Caption, 0.0f, true);
    const ImVec2 Screen  = QueryScreen(Baseline);
    const float  Left    = Alignment < 0 ? Screen.x : (Alignment > 0 ? Screen.x - Advance : Screen.x - 0.5f * Advance);
    (void)GlyphRun::Rasterize(DrawList, Faces, Face, CssSize, ImVec2(Left, Screen.y), Colour, Caption, 0.0f, false);
}

void DiagramPen::Rect(ImVec2 Min, ImVec2 Size, ImU32 Fill, float Radius) const
{
    if (DrawList == nullptr) return;
    DrawList->AddRectFilled(QueryScreen(Min), QueryScreen(ImVec2(Min.x + Size.x, Min.y + Size.y)), Fill, Radius * Scale);
}

void DiagramPen::Glow(ImVec2 Centre, float Radius, ImU32 Rgb, float Alpha) const
{
    if (DrawList == nullptr) return;
    DiagramPen::RasterizeRadialFan(DrawList, QueryScreen(Centre), ImVec2(Radius * Scale, Radius * Scale), ResolveAlpha(Rgb, Alpha), ResolveAlpha(Rgb, 0.0f));
}

void DiagramPen::ShadedArea(const std::vector<ImVec2>& Curve, float Floor, const std::vector<ImU32>& Colours) const
{
    if (DrawList == nullptr || Curve.size() < 2 || Colours.size() != Curve.size()) return;
    // 📝 One quad per curve step down to the baseline, so any curve shape fills exactly.
    const ImVec2 WhiteUv = DrawList->_Data->TexUvWhitePixel;
    const int    Steps   = static_cast<int>(Curve.size()) - 1;
    DrawList->PrimReserve(Steps * 6, Steps * 4);
    for (int Step = 0; Step < Steps; ++Step)
    {
        const ImDrawIdx Corner = static_cast<ImDrawIdx>(DrawList->_VtxCurrentIdx);
        DrawList->PrimWriteVtx(QueryScreen(ImVec2(Curve[Step].x, Floor)), WhiteUv, Colours[Step]);
        DrawList->PrimWriteVtx(QueryScreen(Curve[Step]), WhiteUv, Colours[Step]);
        DrawList->PrimWriteVtx(QueryScreen(Curve[Step + 1]), WhiteUv, Colours[Step + 1]);
        DrawList->PrimWriteVtx(QueryScreen(ImVec2(Curve[Step + 1].x, Floor)), WhiteUv, Colours[Step + 1]);
        DrawList->PrimWriteIdx(Corner);
        DrawList->PrimWriteIdx(static_cast<ImDrawIdx>(Corner + 1));
        DrawList->PrimWriteIdx(static_cast<ImDrawIdx>(Corner + 2));
        DrawList->PrimWriteIdx(Corner);
        DrawList->PrimWriteIdx(static_cast<ImDrawIdx>(Corner + 2));
        DrawList->PrimWriteIdx(static_cast<ImDrawIdx>(Corner + 3));
    }
}

void DiagramPen::GradientBar(ImVec2 Min, ImVec2 Size, const std::vector<ImU32>& Stops) const
{
    if (DrawList == nullptr || Stops.size() < 2) return;
    const ImVec2 ScreenMin = QueryScreen(Min);
    const ImVec2 ScreenMax = QueryScreen(ImVec2(Min.x + Size.x, Min.y + Size.y));
    const int    Segments  = static_cast<int>(Stops.size()) - 1;
    DrawList->PushClipRect(ScreenMin, ScreenMax, true);
    for (int Segment = 0; Segment < Segments; ++Segment)
    {
        const float Left  = ScreenMin.x + (ScreenMax.x - ScreenMin.x) * static_cast<float>(Segment) / static_cast<float>(Segments);
        const float Right = ScreenMin.x + (ScreenMax.x - ScreenMin.x) * static_cast<float>(Segment + 1) / static_cast<float>(Segments);
        DrawList->AddRectFilledMultiColor(ImVec2(Left, ScreenMin.y), ImVec2(Right + 0.5f, ScreenMax.y), Stops[Segment], Stops[Segment + 1],
                                          Stops[Segment + 1], Stops[Segment]);
    }
    DrawList->PopClipRect();
}

} // namespace Frontier
