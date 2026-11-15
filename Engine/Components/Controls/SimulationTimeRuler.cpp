/*============================================================================================================================================
                                                         SIMULATIONTIMERULER.CPP
============================================================================================================================================*/
// ⏱️ Implementation of SimulationTimeRuler matching Frontier Particle Studio bottom timeline ruler & active track strip

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/SimulationTimeRuler.h"

#include <algorithm>
#include <cstdio>

namespace Frontier::SimulationTimeRuler
{

SimulationRulerReading Construct(
    PixelSpace&           Surface,
    Rectangle2D           Bounds,
    SimulationRulerSpec&  Spec,
    const ControlPointer& Pointer,
    float                 Opacity) noexcept
{
    SimulationRulerReading Reading{};
    Reading.ScrubbedSeconds = Spec.ElapsedSeconds;

    const ColorRgba PanelFill   = ColorRgba{ 16u, 17u, 22u, 244u }.ScaledAlpha(Opacity);
    const ColorRgba PanelStroke = ColorRgba{ 42u, 45u, 55u, 220u }.ScaledAlpha(Opacity);
    Surface.FillRectangle(Bounds, PanelFill, 10.0f);
    Surface.StrokeRectangle(Bounds, PanelStroke, 10.0f, 1.0f);

    const float LeftWidth = 110.0f;
    const float TrackX    = Bounds.X + LeftWidth;
    const float TrackW    = std::max(80.0f, Bounds.Width - LeftWidth - 14.0f);

    // Left domain heading + step counter
    Surface.Text(
        { Bounds.X + 12.0f, Bounds.Y + 8.0f },
        10.0f,
        ColorRgba{ 120u, 125u, 138u, 255u }.ScaledAlpha(Opacity),
        Spec.DomainHeading);

    char StepBuf[32]{};
    std::snprintf(StepBuf, sizeof(StepBuf), "STEP %04llu", static_cast<unsigned long long>(Spec.StepCounter));
    Surface.Text(
        { Bounds.X + 12.0f, Bounds.Y + 23.0f },
        11.0f,
        ColorRgba{ 192u, 197u, 208u, 255u }.ScaledAlpha(Opacity),
        StepBuf);

    // Tick header row (0s, 5s, 10s, ..., 30s)
    const double Duration = std::max(1.0, Spec.DurationSeconds);
    const int    TickCount = 7;
    for (int Idx = 0; Idx < TickCount; ++Idx)
    {
        const float Fraction = static_cast<float>(Idx) / static_cast<float>(TickCount - 1);
        const float TickX    = TrackX + Fraction * TrackW;
        Surface.StrokeLine(
            { TickX, Bounds.Y + 14.0f },
            { TickX, Bounds.Y + 20.0f },
            ColorRgba{ 68u, 72u, 85u, 220u }.ScaledAlpha(Opacity),
            1.0f);

        char SecBuf[16]{};
        const int SecVal = static_cast<int>(Duration * static_cast<double>(Fraction) + 0.5);
        std::snprintf(SecBuf, sizeof(SecBuf), "%ds", SecVal);
        const float LabelOffset = (Idx == TickCount - 1) ? -22.0f : -6.0f;
        Surface.Text(
            { TickX + LabelOffset, Bounds.Y + 4.0f },
            9.5f,
            ColorRgba{ 125u, 130u, 144u, 255u }.ScaledAlpha(Opacity),
            SecBuf);
    }

    // Active track strip
    const Rectangle2D TrackBounds{ TrackX, Bounds.Y + 21.0f, TrackW, std::max(16.0f, Bounds.Height - 28.0f) };
    Surface.FillRectangle(TrackBounds, ColorRgba{ 24u, 26u, 33u, 255u }.ScaledAlpha(Opacity), 5.0f);
    Surface.StrokeRectangle(TrackBounds, ColorRgba{ 46u, 50u, 62u, 200u }.ScaledAlpha(Opacity), 5.0f, 1.0f);

    const float Normalized = static_cast<float>(std::clamp(Spec.ElapsedSeconds / Duration, 0.0, 1.0));
    const float FillW      = std::max(4.0f, TrackW * std::max(Normalized, 0.36f));
    const Rectangle2D ActiveBar{ TrackBounds.X + 1.0f, TrackBounds.Y + 1.0f, std::min(TrackW - 2.0f, FillW), TrackBounds.Height - 2.0f };
    Surface.FillRectangle(ActiveBar, ColorRgba{ 58u, 49u, 34u, 215u }.ScaledAlpha(Opacity), 4.0f);
    Surface.Text(
        { TrackBounds.X + 10.0f, TrackBounds.Y + (TrackBounds.Height - 10.5f) * 0.5f },
        10.5f,
        ColorRgba{ 216u, 184u, 128u, 255u }.ScaledAlpha(Opacity),
        Spec.TrackCaption);

    // Interactive scrubbing
    const Rectangle2D HitArea{ TrackX, Bounds.Y + 2.0f, TrackW, Bounds.Height - 4.0f };
    if (Pointer.Enabled && Pointer.Pressed && HitArea.Contains({ Pointer.X, Pointer.Y }))
    {
        Spec.DraggingPlayhead = true;
    }
    if (!Pointer.Down)
    {
        Spec.DraggingPlayhead = false;
    }
    if (Spec.DraggingPlayhead && Pointer.Enabled)
    {
        const double T = std::clamp(static_cast<double>((Pointer.X - TrackX) / TrackW), 0.0, 1.0);
        Spec.ElapsedSeconds     = T * Duration;
        Reading.ScrubbedSeconds = Spec.ElapsedSeconds;
        Reading.ScrubbingActive = true;
        Reading.Modified        = true;
    }

    // Playhead needle
    const float PlayheadNorm = static_cast<float>(std::clamp(Spec.ElapsedSeconds / Duration, 0.0, 1.0));
    const float NeedleX      = TrackX + PlayheadNorm * TrackW;
    Surface.StrokeLine(
        { NeedleX, Bounds.Y + 12.0f },
        { NeedleX, Bounds.Bottom() - 4.0f },
        ColorRgba{ 242u, 210u, 134u, 255u }.ScaledAlpha(Opacity),
        2.0f);
    Surface.FillCircle({ NeedleX, Bounds.Y + 12.0f }, 3.5f, ColorRgba{ 246u, 224u, 162u, 255u }.ScaledAlpha(Opacity));

    return Reading;
}

} // namespace Frontier::SimulationTimeRuler
