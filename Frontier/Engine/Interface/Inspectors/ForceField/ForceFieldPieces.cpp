/*==========================================================================================================================================
                                                          FORCEFIELDPIECES.CPP
==========================================================================================================================================*/
// 🧩 The hero card, the series chart and the curve plot of the Force Field and Wind pages, drawn with the shared column, card
//    and diagram pen (the prototype's .hero-card, TimeSeries and CurvePlot).

#include "Engine/Interface/Inspectors/ForceField/ForceFieldPieces.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/PhysicalDynamics/ForceSourceCatalogue.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace ForceFieldPieces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    COLOURS AND FIGURES
//------------------------------------------------------------------------------------------------------------------------

ImU32 ResolveContributionColour(ForceContribution Contribution, float Alpha) noexcept
{
    const uint32_t Rgb = ResolveForceContributionDescriptor(Contribution).ColourRgb;
    return ResolveAlpha(IM_COL32((Rgb >> 16u) & 255u, (Rgb >> 8u) & 255u, Rgb & 255u, 255), Alpha);
}

void FormatBytes(uint64_t Bytes, char* Text, size_t Capacity) noexcept
{
    const double Value = static_cast<double>(Bytes);
    if (Value >= 1024.0 * 1024.0 * 1024.0) std::snprintf(Text, Capacity, "%.2f GB", Value / (1024.0 * 1024.0 * 1024.0));
    else if (Value >= 1024.0 * 1024.0) std::snprintf(Text, Capacity, "%.1f MB", Value / (1024.0 * 1024.0));
    else if (Value >= 1024.0) std::snprintf(Text, Capacity, "%.0f KB", Value / 1024.0);
    else std::snprintf(Text, Capacity, "%.0f B", Value);
}

void FormatFigure(double Reading, char* Text, size_t Capacity) noexcept
{
    std::snprintf(Text, Capacity, std::abs(Reading) >= 100.0 ? "%.0f" : "%.1f", Reading);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    HERO CARD
//------------------------------------------------------------------------------------------------------------------------

void BeginHero(ControlColumn& Page, const HeroFigures& Figures)
{
    CardLook Look;
    Look.Radius        = 24.0f;
    Look.PaddingTop    = 16.0f;
    Look.PaddingSide   = 16.0f;
    Look.PaddingBottom = 12.0f;
    Look.MarginBottom  = 14.0f;
    switch (Figures.Tone)
    {
    case HeroTone::Severe: Look.HeadingGlyph = "TriangleAlert"; break;
    case HeroTone::Warm: Look.HeadingGlyph = "Wind"; break;
    case HeroTone::Waiting: Look.HeadingGlyph = "Timer"; break;
    default: Look.HeadingGlyph = "Check"; break;
    }
    Page.BeginCard(Look, Figures.Chip, Figures.Tag);
    ReadoutBlock::Construct(Page, Figures.Title, Figures.Figure, Figures.Unit, 34.0f);
}

void EndHero(ControlColumn& Page, const char* Note)
{
    if (Note != nullptr && Note[0] != '\0') CaptionText::Construct(Page, Note, 4.0f, 2.0f);
    Page.EndCard();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SERIES
//------------------------------------------------------------------------------------------------------------------------

void ConstructSeries(ControlColumn& Page, const char* Id, const float* Ring, uint32_t Capacity, uint32_t Count, uint32_t Cursor,
                     const SeriesLook& Look)
{
    const ControlTheme& Theme  = Page.QueryTheme();
    const float         Height = std::max(80.0f, Look.Height);
    const float         Floor  = Height - 26.0f;
    const float         Right  = 580.0f;
    DiagramPen          Pen    = DiagramCanvas::Begin(Page, Id, 640, Height, 4, 6);
    Count                      = std::min(Count, Capacity);
    double Ceiling             = std::max(1.0e-6, Look.Target);
    for (uint32_t Index = 0; Index < Count; ++Index) Ceiling = std::max(Ceiling, static_cast<double>(Ring[Index]));
    Ceiling *= std::max(1.0, Look.Headroom);
    const auto Vertical = [&](double Reading) { return Floor - static_cast<float>(std::clamp(Reading / Ceiling, 0.0, 1.0)) * (Floor - 12.0f); };
    for (int Guide = 1; Guide <= 3; ++Guide)
    {
        char         Label[32];
        const double Level = Ceiling * Guide / 3.0;
        FormatFigure(Level, Label, sizeof(Label));
        Pen.Line({0, Vertical(Level)}, {Right, Vertical(Level)}, ResolveCssColour(0xFFFFFF10));
        Pen.Text({640, Vertical(Level) + 6}, Label, Theme.Muted, 15, 1);
    }
    Pen.Line({0, Floor}, {Right, Floor}, ResolveCssColour(0xFFFFFF1C));
    if (Look.Target > 0.0)
    {
        char Label[64];
        char Figure[24];
        FormatFigure(Look.Target, Figure, sizeof(Figure));
        std::snprintf(Label, sizeof(Label), "%s %s %s", Look.TargetLabel, Figure, Look.Unit);
        Pen.Line({0, Vertical(Look.Target)}, {Right, Vertical(Look.Target)}, ResolveCssColour(0xFFFFFF80), 1.5f, 8, 6);
        Pen.Text({4, Vertical(Look.Target) - 8}, Label, Theme.Muted, 15, -1);
    }
    if (Count >= 2)
    {
        // 📝 The ring in time order, oldest at the left; the newest sits just before Cursor.
        std::vector<ImVec2> Line;
        const float         Slot = Right / static_cast<float>(std::max<uint32_t>(1u, Capacity - 1u));
        for (uint32_t Step = 0; Step < Count; ++Step)
        {
            const uint32_t Index = (Cursor + Capacity - Count + Step) % Capacity;
            Line.push_back({Right - Slot * static_cast<float>(Count - 1u - Step), Vertical(Ring[Index])});
        }
        Pen.ShadedArea(Line, Floor, std::vector<ImU32>(Line.size(), ResolveCssColour(0xFFFFFF14)));
        Pen.Path(Line, ResolveCssColour(0xF0F0F0E6), 2.0f);
        Pen.Circle(Line.back(), 5.0f, ResolveCssColour(0xFFFFFFFF), ResolveCssColour(0x00000080));
    }
    else
    {
        Pen.Text({Right / 2.0f, Floor / 2.0f + 6.0f}, Look.Empty, Theme.Muted, 16, 0);
    }
    Pen.Text({0, Height - 4}, Look.Early, Theme.Muted, 14, -1);
    Pen.Text({Right / 2.0f, Height - 4}, Look.Middle, Theme.Muted, 14, 0);
    Pen.Text({Right, Height - 4}, Look.Late, Theme.Muted, 14, 1);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CURVE PLOT
//------------------------------------------------------------------------------------------------------------------------

void ConstructCurvePlot(ControlColumn& Page, const char* Id, const CurvePlot& Plot)
{
    const ControlTheme& Theme  = Page.QueryTheme();
    const float         Height = std::max(120.0f, Plot.Height);
    const float         Left = 70.0f, Right = 630.0f, Top = 14.0f, Bottom = Height - (Plot.Caption.empty() ? 34.0f : 58.0f);
    DiagramPen          Pen    = DiagramCanvas::Begin(Page, Id, 640, Height, 4, 8);
    const double        SpanX  = std::max(1.0e-9, Plot.XRange[1] - Plot.XRange[0]);
    const double        SpanY  = std::max(1.0e-9, Plot.YRange[1] - Plot.YRange[0]);
    const auto          Map    = [&](double X, double Y)
    {
        return ImVec2(Left + static_cast<float>((X - Plot.XRange[0]) / SpanX) * (Right - Left),
                      Bottom - static_cast<float>((Y - Plot.YRange[0]) / SpanY) * (Bottom - Top));
    };
    Pen.Rect({Left, Top}, {Right - Left, Bottom - Top}, ResolveCssColour(0xFFFFFF06), 8.0f);
    for (const PlotTick& Tick : Plot.YTicks)
    {
        const ImVec2 At = Map(Plot.XRange[0], Tick.At);
        Pen.Line({Left, At.y}, {Right, At.y}, ResolveCssColour(0xFFFFFF0E));
        Pen.Text({Left - 10.0f, At.y + 5.0f}, Tick.Text.c_str(), Theme.Muted, 15, 1);
    }
    for (const PlotTick& Tick : Plot.XTicks)
    {
        const ImVec2 At = Map(Tick.At, Plot.YRange[0]);
        Pen.Line({At.x, Top}, {At.x, Bottom}, ResolveCssColour(0xFFFFFF0A));
        Pen.Text({At.x, Bottom + 22.0f}, Tick.Text.c_str(), Theme.Muted, 15, 0);
    }
    for (const PlotCurve& Curve : Plot.Curves)
    {
        if (Curve.Points.size() < 2) continue;
        std::vector<ImVec2> Line;
        Line.reserve(Curve.Points.size());
        for (const auto& Point : Curve.Points) Line.push_back(Map(Point[0], Point[1]));
        if (Curve.Fill != 0) Pen.ShadedArea(Line, Bottom, std::vector<ImU32>(Line.size(), Curve.Fill));
        Pen.Path(Line, Curve.Stroke, Curve.Width, Curve.Dashed ? 5.0f : 0.0f, Curve.Dashed ? 5.0f : 0.0f);
    }
    for (const PlotGuide& Guide : Plot.Guides)
    {
        const ImVec2 At = Map(Plot.XRange[0], Guide.Y);
        Pen.Line({Left, At.y}, {Right, At.y}, ResolveCssColour(0xFFFFFF70), 1.2f, 6, 5);
        // 📝 A backing under the label (about half an em a glyph), so the curve passing beneath it stays readable.
        const float Extent = 8.0f * static_cast<float>(Guide.Text.size()) + 12.0f;
        Pen.Rect({Left + 4.0f, At.y - 26.0f}, {Extent, 22.0f}, ResolveCssColour(0x1A1C1FE0), 6.0f);
        Pen.Text({Left + 10.0f, At.y - 9.0f}, Guide.Text.c_str(), Theme.Muted, 15, -1);
    }
    for (const PlotMarker& Marker : Plot.Markers)
    {
        const ImVec2 At = Map(Marker.X, Marker.Y);
        Pen.Circle(At, 4.5f, ResolveCssColour(0xFFFFFFFF), ResolveCssColour(0x00000090));
        if (!Marker.Label.empty())
            Pen.Text({std::min(At.x + 9.0f, Right - 4.0f), At.y - 8.0f}, Marker.Label.c_str(), ResolveCssColour(0xDADADAFF), 14,
                     At.x + 9.0f > Right - 90.0f ? 1 : -1);
    }
    if (!Plot.Caption.empty()) Pen.Text({Left, Height - 6.0f}, Plot.Caption.c_str(), Theme.Muted, 15, -1);
}

} // namespace ForceFieldPieces

} // namespace Frontier

#endif
