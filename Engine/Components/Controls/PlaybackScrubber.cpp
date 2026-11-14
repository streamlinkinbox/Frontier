/*============================================================================================================================================
                                                            PLAYBACKSCRUBBER.CPP
============================================================================================================================================*/
// 🧩 Implementation of the reusable Frontier animation play slider and simulation playback transport control

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/PlaybackScrubber.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace PlaybackScrubber
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    LOCAL ROUTINES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] ColorQuad QueryWarmAmberAccent(const ControlColorSet& Colors, float Alpha) noexcept
{
    if (Colors.LightSurface)
    {
        return ColorQuad::FromHex(0xB47318u, Alpha);
    }
    return ColorQuad::FromHex(0xE8C27Au, Alpha);
}

void ConstructPlayWedgeSymbol(PixelSpace& Surface, Point2D Center, float HalfExtent, ColorQuad Ink) noexcept
{
    const Point2D Vertices[3] = {
        { Center.X - HalfExtent * 0.62f, Center.Y - HalfExtent * 0.92f },
        { Center.X + HalfExtent * 0.96f, Center.Y                      },
        { Center.X - HalfExtent * 0.62f, Center.Y + HalfExtent * 0.92f },
    };
    Surface.FillPolygon(Vertices, Ink.ToRgba8());
}

void ConstructPauseTwinStrokes(PixelSpace& Surface, Point2D Center, float HalfExtent, ColorQuad Ink) noexcept
{
    const float StrokeW = std::max(2.5f, HalfExtent * 0.46f);
    const float StrokeH = HalfExtent * 1.72f;
    const float OffsetX = HalfExtent * 0.48f;
    const float TopY    = Center.Y - StrokeH * 0.5f;
    const ColorRgba Fill = Ink.ToRgba8();
    Surface.FillRectangle({ Center.X - OffsetX - StrokeW * 0.5f, TopY, StrokeW, StrokeH }, Fill, 1.2f);
    Surface.FillRectangle({ Center.X + OffsetX - StrokeW * 0.5f, TopY, StrokeW, StrokeH }, Fill, 1.2f);
}

void ConstructStepForwardSymbol(PixelSpace& Surface, Point2D Center, float HalfExtent, ColorQuad Ink) noexcept
{
    const ColorRgba Fill = Ink.ToRgba8();
    const Point2D Vertices[3] = {
        { Center.X - HalfExtent * 0.85f, Center.Y - HalfExtent * 0.82f },
        { Center.X + HalfExtent * 0.28f, Center.Y                      },
        { Center.X - HalfExtent * 0.85f, Center.Y + HalfExtent * 0.82f },
    };
    Surface.FillPolygon(Vertices, Fill);

    const float StopW = std::max(1.8f, HalfExtent * 0.34f);
    const float StopH = HalfExtent * 1.62f;
    Surface.FillRectangle(
        { Center.X + HalfExtent * 0.52f, Center.Y - StopH * 0.5f, StopW, StopH },
        Fill,
        0.9f);
}

[[nodiscard]] bool ConstructRoundTransportButton(
    PixelSpace&           Surface,
    Rectangle2D           Bounds,
    bool                  PrimaryStyle,
    const ControlPointer& Pointer,
    float                 Opacity,
    bool&                 OutHovered) noexcept
{
    const ControlColorSet& Colors  = ControlAppearance::Colors();
    const bool             Hovered = ControlAppearance::QueryPointerOver(Bounds, Pointer);
    OutHovered                     = OutHovered || Hovered;
    const bool Pressed             = Hovered && Pointer.Down;
    const float Radius             = std::min(Bounds.Width, Bounds.Height) * 0.5f;

    ColorQuad Fill{};
    if (PrimaryStyle)
    {
        if (Colors.LightSurface)
        {
            Fill = Hovered ? ColorQuad::FromHex(0x111111u) : ColorQuad::FromHex(0x1E1E22u);
        }
        else
        {
            Fill = Hovered ? ColorQuad::FromHex(0xFFFFFFu) : ColorQuad::FromHex(0xE4E4E4u);
        }
    }
    else
    {
        Fill = Pressed ? Colors.Selected : (Hovered ? Colors.Raised : Colors.Inset);
    }

    Surface.FillRectangle(Bounds, Fill.Faded(Opacity).ToRgba8(), Radius);
    if (!PrimaryStyle)
    {
        ControlAppearance::ConstructRoundedOutline(
            Surface,
            Bounds,
            (Hovered ? Colors.StrokeStrong : Colors.Stroke).Faded(Opacity),
            Radius,
            1.0f);
    }
    return Hovered && Pointer.Released;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  PUBLIC IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

float QueryNextSpeedMultiplier(float CurrentSpeedMultiplier) noexcept
{
    if (CurrentSpeedMultiplier < 0.375f)
    {
        return 0.5f;
    }
    if (CurrentSpeedMultiplier < 0.75f)
    {
        return 1.0f;
    }
    if (CurrentSpeedMultiplier < 1.5f)
    {
        return 2.0f;
    }
    return 0.25f;
}

void FormatTimecode(float ElapsedSeconds, char* OutBuffer, uint32_t BufferCapacity) noexcept
{
    if (OutBuffer == nullptr || BufferCapacity == 0u)
    {
        return;
    }
    const float    SafeSec   = std::max(0.0f, ElapsedSeconds);
    const uint32_t TotalMs   = static_cast<uint32_t>(std::lround(SafeSec * 1000.0f));
    const uint32_t Minutes   = (TotalMs / 60000u) % 100u;
    const uint32_t Seconds   = (TotalMs / 1000u) % 60u;
    const uint32_t Millis    = TotalMs % 1000u;
    std::snprintf(OutBuffer, BufferCapacity, "%02u:%02u.%03u", Minutes, Seconds, Millis);
}

PlaybackScrubberInteraction ConstructScrubberRail(
    PixelSpace&                    Surface,
    Rectangle2D                    Bounds,
    const PlaybackClockParameters& Clock,
    bool                           DraggingPlayhead,
    const ControlPointer&          Pointer,
    float                          Opacity,
    const OptionalTooltip&         Tooltip) noexcept
{
    PlaybackScrubberInteraction Result{};
    Result.UpdatedSeconds         = std::max(0.0f, Clock.ElapsedSeconds);
    Result.UpdatedSpeedMultiplier = Clock.SpeedMultiplier;

    const ControlColorSet& Colors   = ControlAppearance::Colors();
    const float            Duration = std::max(Clock.LoopDurationSeconds, 0.1f);
    const float            Fps      = std::max(Clock.FrameRateHz, 1.0f);

    const ColorQuad RailBg = Colors.LightSurface
        ? ColorQuad::FromHex(0xF1F2F4u)
        : ColorQuad::FromHex(0x101012u);
    Surface.FillRectangle(Bounds, RailBg.Faded(Opacity).ToRgba8(), 0.0f);
    ControlAppearance::ConstructHairline(Surface, Bounds.X, Bounds.Y, Bounds.Width, Colors.Divider.Faded(Opacity));

    constexpr float PadX       = 16.0f;
    constexpr float LabelColW  = 90.0f;
    constexpr float ColumnGap  = 14.0f;
    const float     ContentTop = Bounds.Y + 6.0f;

    const char* Heading = (Clock.RulerHeading != nullptr && Clock.RulerHeading[0] != '\0') ? Clock.RulerHeading : "SIMULATION";
    Surface.Text(
        { Bounds.X + PadX, ContentTop + 1.0f },
        9.0f,
        Colors.TextDim.Faded(Opacity).ToRgba8(),
        Heading);

    const uint32_t FrameIndex = static_cast<uint32_t>(std::floor(std::max(0.0f, Clock.ElapsedSeconds) * Fps + 1.0e-4f));
    char           FrameBuf[32]{};
    std::snprintf(FrameBuf, sizeof(FrameBuf), "FRAME %04u", FrameIndex);
    Surface.Text(
        { Bounds.X + PadX, ContentTop + 15.0f },
        10.0f,
        Colors.Text.Faded(Opacity * 0.78f).ToRgba8(),
        FrameBuf);

    const float RulerX = Bounds.X + PadX + LabelColW + ColumnGap;
    const float RulerW = std::max(80.0f, Bounds.Right() - PadX - RulerX);
    const float RulerY = ContentTop;
    const float TrackY = RulerY + 14.0f;
    const float TrackH = 14.0f;

    const Rectangle2D TrackRect{ RulerX, TrackY, RulerW, TrackH };
    const Rectangle2D HitRect{ RulerX - 6.0f, RulerY, RulerW + 12.0f, Bounds.Height - 10.0f };

    const bool OverTrack = ControlAppearance::QueryPointerOver(HitRect, Pointer);
    Result.Hovered       = OverTrack;

    bool ScrubActive = DraggingPlayhead;
    if (OverTrack && Pointer.Pressed && Pointer.Enabled)
    {
        ScrubActive = true;
    }
    if (Pointer.Released)
    {
        ScrubActive = false;
    }

    float Fraction = std::clamp(std::fmod(std::max(0.0f, Clock.ElapsedSeconds), Duration) / Duration, 0.0f, 1.0f);
    if (Clock.ElapsedSeconds >= Duration - 1.0e-4f && Clock.ElapsedSeconds <= Duration + 1.0e-4f)
    {
        Fraction = 1.0f;
    }

    if ((ScrubActive && Pointer.Down && Pointer.Enabled) || (OverTrack && Pointer.Pressed && Pointer.Enabled))
    {
        Fraction              = std::clamp((Pointer.X - RulerX) / std::max(RulerW, 1.0f), 0.0f, 1.0f);
        Result.UpdatedSeconds = Fraction * Duration;
        Result.Scrubbed       = true;
    }
    Result.DraggingPlayhead = ScrubActive && Pointer.Down && Pointer.Enabled;

    constexpr int32_t TickSegments = 6;
    const ColorRgba   TickTextInk  = Colors.TextFaint.Faded(Opacity * 1.15f).ToRgba8();
    const ColorRgba   TickMarkInk  = Colors.Stroke.Faded(Opacity * 1.4f).ToRgba8();
    for (int32_t TickIndex = 0; TickIndex <= TickSegments; ++TickIndex)
    {
        const float T         = static_cast<float>(TickIndex) / static_cast<float>(TickSegments);
        const float TickX     = RulerX + T * RulerW;
        const float TickSec   = T * Duration;
        char        TickBuf[16]{};
        if (std::fabs(TickSec - std::round(TickSec)) < 0.05f)
        {
            std::snprintf(TickBuf, sizeof(TickBuf), "%.0fs", std::round(TickSec));
        }
        else
        {
            std::snprintf(TickBuf, sizeof(TickBuf), "%.1fs", TickSec);
        }

        if (TickIndex == 0)
        {
            Surface.Text({ TickX, RulerY - 1.0f }, 9.0f, TickTextInk, TickBuf);
        }
        else if (TickIndex == TickSegments)
        {
            const float TextW = Surface.MeasureTextWidth(9.0f, TickBuf);
            Surface.Text({ TickX - TextW, RulerY - 1.0f }, 9.0f, TickTextInk, TickBuf);
        }
        else
        {
            const float TextW = Surface.MeasureTextWidth(9.0f, TickBuf);
            Surface.Text({ TickX - TextW * 0.5f, RulerY - 1.0f }, 9.0f, TickTextInk, TickBuf);
        }

        Surface.FillRectangle({ TickX, TrackY - 3.0f, 1.0f, 3.0f }, TickMarkInk, 0.0f);
    }

    const ColorQuad TrackFill = Colors.LightSurface
        ? ColorQuad::FromHex(0xE2E4E8u)
        : ColorQuad::FromHex(0x1E1E22u);
    Surface.FillRectangle(TrackRect, TrackFill.Faded(Opacity).ToRgba8(), 5.0f);
    ControlAppearance::ConstructRoundedOutline(Surface, TrackRect, Colors.Stroke.Faded(Opacity * 1.35f), 5.0f, 1.0f);

    const float FillW = std::clamp(Fraction * RulerW, 0.0f, RulerW);
    if (FillW > 1.0f)
    {
        const ColorQuad AccentWash = QueryWarmAmberAccent(Colors, Colors.LightSurface ? 0.24f : 0.20f);
        const ColorQuad AccentEdge = QueryWarmAmberAccent(Colors, Colors.LightSurface ? 0.65f : 0.52f);
        Surface.FillRectangle({ TrackRect.X, TrackRect.Y, FillW, TrackRect.Height }, AccentWash.Faded(Opacity).ToRgba8(), 5.0f);
        Surface.FillRectangle(
            { TrackRect.X + std::max(0.0f, FillW - 1.0f), TrackRect.Y + 1.0f, 1.5f, TrackRect.Height - 2.0f },
            AccentEdge.Faded(Opacity).ToRgba8(),
            0.0f);
    }

    if (Clock.TrackCaption != nullptr && Clock.TrackCaption[0] != '\0')
    {
        Surface.PushClip(TrackRect);
        Surface.Text(
            { TrackRect.X + 8.0f, TrackRect.Y + 2.5f },
            9.0f,
            Colors.TextDim.Faded(Opacity * 1.1f).ToRgba8(),
            Clock.TrackCaption);
        Surface.PopClip();
    }

    const float PlayheadX = RulerX + Fraction * RulerW;
    const ColorQuad NeedleColor = Colors.LightSurface
        ? ColorQuad::FromHex(0x1A1A1Eu)
        : ColorQuad::FromHex(0xEAEAEFu);
    Surface.FillRectangle(
        { PlayheadX - 0.75f, RulerY + 9.0f, 1.5f, TrackH + 9.0f },
        NeedleColor.Faded(Opacity).ToRgba8(),
        0.5f);

    const float HandleRadius = (Result.DraggingPlayhead || OverTrack) ? 6.0f : 4.5f;
    const float HandleCenterY = TrackRect.Y + TrackRect.Height * 0.5f;
    ControlAppearance::ConstructCircleFill(
        Surface,
        PlayheadX,
        HandleCenterY,
        HandleRadius + 1.5f,
        QueryWarmAmberAccent(Colors, (Result.DraggingPlayhead || OverTrack) ? 0.42f : 0.20f).Faded(Opacity));
    ControlAppearance::ConstructCircleFill(
        Surface,
        PlayheadX,
        HandleCenterY,
        HandleRadius,
        NeedleColor.Faded(Opacity));

    if (OverTrack && Tooltip.Enabled)
    {
        ControlAppearance::RecordHoveredTooltip(TrackRect, Tooltip);
    }

    return Result;
}

PlaybackScrubberInteraction ConstructFullTransport(
    PixelSpace&                    Surface,
    Rectangle2D                    Bounds,
    const PlaybackClockParameters& Clock,
    bool                           DraggingPlayhead,
    const ControlPointer&          Pointer,
    float                          Opacity,
    const OptionalTooltip&         Tooltip) noexcept
{
    PlaybackScrubberInteraction Result{};
    Result.UpdatedSeconds         = std::max(0.0f, Clock.ElapsedSeconds);
    Result.UpdatedSpeedMultiplier = Clock.SpeedMultiplier;

    const ControlColorSet& Colors = ControlAppearance::Colors();

    const float UpperH = std::min(TransportRowHeight, std::max(36.0f, Bounds.Height - ScrubberRailHeight));
    const Rectangle2D UpperBounds{ Bounds.X, Bounds.Y, Bounds.Width, UpperH };
    const Rectangle2D LowerBounds{ Bounds.X, Bounds.Y + UpperH, Bounds.Width, std::max(ScrubberRailHeight, Bounds.Height - UpperH) };

    const ColorQuad TransportBg = Colors.LightSurface
        ? ColorQuad::FromHex(0xF7F7F9u)
        : ColorQuad::FromHex(0x161618u);
    Surface.FillRectangle(UpperBounds, TransportBg.Faded(Opacity).ToRgba8(), 0.0f);
    ControlAppearance::ConstructHairline(Surface, UpperBounds.X, UpperBounds.Y, UpperBounds.Width, Colors.Divider.Faded(Opacity));

    const float CenterY = UpperBounds.Y + UpperBounds.Height * 0.5f;
    float       CursorX = UpperBounds.X + 16.0f;

    const Rectangle2D ResetRect{ CursorX, CenterY - 15.0f, 30.0f, 30.0f };
    if (ConstructRoundTransportButton(Surface, ResetRect, false, Pointer, Opacity, Result.Hovered))
    {
        Result.ResetTriggered = true;
    }
    VectorIconRepository::ConstructNavigation(
        Surface,
        NavigationIcon::RotateCounterClockwise,
        { ResetRect.X + 8.0f, ResetRect.Y + 8.0f, 14.0f, Colors.Text.Faded(Opacity * 0.85f).ToRgba8(), 1.8f });
    CursorX += 36.0f;

    const Rectangle2D PlayRect{ CursorX, CenterY - 17.0f, 34.0f, 34.0f };
    if (ConstructRoundTransportButton(Surface, PlayRect, true, Pointer, Opacity, Result.Hovered))
    {
        Result.PlayPauseToggled = true;
    }
    const ColorQuad PlaySymbolInk = Colors.LightSurface
        ? ColorQuad::FromHex(0xF8F8FAu)
        : ColorQuad::FromHex(0x111114u);
    if (Clock.Playing)
    {
        ConstructPauseTwinStrokes(Surface, { PlayRect.X + 17.0f, PlayRect.Y + 17.0f }, 6.0f, PlaySymbolInk.Faded(Opacity));
    }
    else
    {
        ConstructPlayWedgeSymbol(Surface, { PlayRect.X + 18.0f, PlayRect.Y + 17.0f }, 6.2f, PlaySymbolInk.Faded(Opacity));
    }
    CursorX += 40.0f;

    const Rectangle2D StepRect{ CursorX, CenterY - 15.0f, 30.0f, 30.0f };
    if (ConstructRoundTransportButton(Surface, StepRect, false, Pointer, Opacity, Result.Hovered))
    {
        Result.StepForwardTriggered = true;
    }
    ConstructStepForwardSymbol(Surface, { StepRect.X + 15.0f, StepRect.Y + 15.0f }, 5.0f, Colors.Text.Faded(Opacity * 0.85f));
    CursorX += 42.0f;

    char TimeBuf[24]{};
    FormatTimecode(Clock.ElapsedSeconds, TimeBuf, static_cast<uint32_t>(sizeof(TimeBuf)));
    Surface.Text(
        { CursorX, CenterY - 6.0f },
        12.0f,
        Colors.Text.Faded(Opacity * 0.90f).ToRgba8(),
        TimeBuf);
    CursorX += 78.0f;

    Surface.FillRectangle(
        { CursorX, CenterY - 9.0f, 1.0f, 18.0f },
        Colors.StrokeStrong.Faded(Opacity * 0.95f).ToRgba8(),
        0.0f);
    CursorX += 14.0f;

    if (Clock.ShowSpeedSelector)
    {
        const Rectangle2D SpeedRect{ CursorX, CenterY - 14.0f, 54.0f, 28.0f };
        const bool        OverSpeed = ControlAppearance::QueryPointerOver(SpeedRect, Pointer);
        Result.Hovered              = Result.Hovered || OverSpeed;
        const ColorQuad   SpeedFill = OverSpeed ? Colors.Raised : Colors.Field;
        Surface.FillRectangle(SpeedRect, SpeedFill.Faded(Opacity).ToRgba8(), 14.0f);
        ControlAppearance::ConstructRoundedOutline(
            Surface,
            SpeedRect,
            (OverSpeed ? Colors.StrokeStrong : Colors.Stroke).Faded(Opacity * 1.4f),
            14.0f,
            1.0f);

        char SpeedBuf[16]{};
        std::snprintf(SpeedBuf, sizeof(SpeedBuf), "%.2gx", Clock.SpeedMultiplier);
        if (std::fabs(Clock.SpeedMultiplier - 1.0f) < 0.01f)
        {
            std::snprintf(SpeedBuf, sizeof(SpeedBuf), "1.0x");
        }
        else if (std::fabs(Clock.SpeedMultiplier - 2.0f) < 0.01f)
        {
            std::snprintf(SpeedBuf, sizeof(SpeedBuf), "2.0x");
        }
        else if (std::fabs(Clock.SpeedMultiplier - 0.5f) < 0.01f)
        {
            std::snprintf(SpeedBuf, sizeof(SpeedBuf), "0.5x");
        }
        else if (std::fabs(Clock.SpeedMultiplier - 0.25f) < 0.01f)
        {
            std::snprintf(SpeedBuf, sizeof(SpeedBuf), "0.25x");
        }
        ControlAppearance::TextCentered(Surface, SpeedRect, Colors.Text.Faded(Opacity * 0.88f), SpeedBuf, 11.0f);

        if (OverSpeed && Pointer.Released)
        {
            Result.SpeedCycled            = true;
            Result.UpdatedSpeedMultiplier = QueryNextSpeedMultiplier(Clock.SpeedMultiplier);
        }
        CursorX += 66.0f;
    }

    const ColorQuad DotColor = Clock.LiveUncached ? Colors.Success : Colors.TextDim;
    ControlAppearance::ConstructCircleFill(Surface, CursorX + 3.0f, CenterY, 3.2f, DotColor.Faded(Opacity));
    Surface.Text(
        { CursorX + 12.0f, CenterY - 5.5f },
        11.0f,
        Colors.TextDim.Faded(Opacity * 1.15f).ToRgba8(),
        Clock.LiveUncached ? "Live / uncached" : "Cached playback");

    if (Clock.ActionLabel != nullptr && Clock.ActionLabel[0] != '\0')
    {
        const bool  HasHint   = (Clock.ActionShortcut != nullptr && Clock.ActionShortcut[0] != '\0');
        const float LabelW    = Surface.MeasureTextWidth(11.0f, Clock.ActionLabel);
        const float ButtonW   = std::max(72.0f, LabelW + (HasHint ? 42.0f : 26.0f));
        const Rectangle2D ActionRect{ UpperBounds.Right() - 16.0f - ButtonW, CenterY - 15.0f, ButtonW, 30.0f };
        const bool        OverAction = ControlAppearance::QueryPointerOver(ActionRect, Pointer);
        Result.Hovered               = Result.Hovered || OverAction;

        const ColorQuad ActionFill   = QueryWarmAmberAccent(Colors, OverAction ? 0.24f : 0.14f);
        const ColorQuad ActionBorder = QueryWarmAmberAccent(Colors, OverAction ? 0.52f : 0.34f);
        const ColorQuad ActionText   = Colors.LightSurface
            ? ColorQuad::FromHex(0x6E4208u)
            : ColorQuad::FromHex(0xF6DCB0u);

        Surface.FillRectangle(ActionRect, ActionFill.Faded(Opacity).ToRgba8(), 9.0f);
        ControlAppearance::ConstructRoundedOutline(Surface, ActionRect, ActionBorder.Faded(Opacity), 9.0f, 1.0f);

        const float LabelX = ActionRect.X + 12.0f;
        Surface.Text({ LabelX, ActionRect.Y + 9.0f }, 11.0f, ActionText.Faded(Opacity).ToRgba8(), Clock.ActionLabel);
        if (HasHint)
        {
            const Rectangle2D HintRect{ ActionRect.Right() - 22.0f, ActionRect.Y + 7.0f, 15.0f, 16.0f };
            ControlAppearance::ConstructRoundedOutline(
                Surface,
                HintRect,
                ActionBorder.Faded(Opacity * 0.9f),
                4.0f,
                1.0f);
            ControlAppearance::TextCentered(Surface, HintRect, ActionText.Faded(Opacity * 0.85f), Clock.ActionShortcut, 9.0f);
        }

        if (OverAction && Pointer.Released)
        {
            Result.ActionTriggered = true;
        }
    }

    const PlaybackScrubberInteraction RailResult = ConstructScrubberRail(
        Surface,
        LowerBounds,
        Clock,
        DraggingPlayhead,
        Pointer,
        Opacity,
        Tooltip);

    Result.Scrubbed         = RailResult.Scrubbed;
    Result.DraggingPlayhead = RailResult.DraggingPlayhead;
    Result.Hovered          = Result.Hovered || RailResult.Hovered;
    if (RailResult.Scrubbed)
    {
        Result.UpdatedSeconds = RailResult.UpdatedSeconds;
    }

    return Result;
}

} // namespace PlaybackScrubber

} // namespace Frontier
