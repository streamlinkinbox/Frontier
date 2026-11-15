/*============================================================================================================================================
                                                       SIMULATIONPLAYBACKCONTROLS.CPP
============================================================================================================================================*/
// ⏯️ Implementation of SimulationPlaybackControls matching Frontier Studio dark-glass transport bar styling

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/SimulationPlaybackControls.h"

#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/Components/Controls/DropdownSelector.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier::SimulationPlaybackControls
{

namespace
{

constexpr std::array<float, 4u> ScalePresets {{ 0.25f, 0.50f, 1.00f, 2.00f }};
constexpr std::array<const char*, 4u> ScaleLabels {{ "0.25x", "0.5x", "1.0x", "2.0x" }};

[[nodiscard]] uint32_t FindScaleIndex(float Scale) noexcept
{
    uint32_t Best = 2u;
    float    Dist = 1.0e9f;
    for (uint32_t Idx = 0u; Idx < ScalePresets.size(); ++Idx)
    {
        const float Diff = std::abs(Scale - ScalePresets[Idx]);
        if (Diff < Dist)
        {
            Dist = Diff;
            Best = Idx;
        }
    }
    return Best;
}

void FormatTimecode(double Seconds, char* OutBuf, std::size_t BufSize) noexcept
{
    const double Clamped = std::max(0.0, Seconds);
    const int    TotalMs = static_cast<int>(std::llround(Clamped * 1000.0));
    const int    Minutes = (TotalMs / 60000) % 100;
    const int    Sec     = (TotalMs / 1000) % 60;
    const int    Millis  = TotalMs % 1000;
    std::snprintf(OutBuf, BufSize, "%02d:%02d.%03d", Minutes, Sec, Millis);
}

[[nodiscard]] bool DrawRoundIcon(
    PixelSpace&           Surface,
    Rectangle2D           BtnBounds,
    ControlIcon           Icon,
    bool                  Highlighted,
    ColorRgba             Fill,
    ColorRgba             Border,
    ColorRgba             Ink,
    const ControlPointer& Pointer,
    bool&                 OutClicked) noexcept
{
    const bool Hover = Pointer.Enabled && BtnBounds.Contains({ Pointer.X, Pointer.Y });
    const ColorRgba ActualFill = Highlighted ? Fill : (Hover ? ColorRgba{ 46u, 50u, 60u, 255u } : Fill);
    Surface.FillRectangle(BtnBounds, ActualFill, BtnBounds.Height * 0.5f);
    if (Border.Alpha > 0u)
    {
        Surface.StrokeRectangle(BtnBounds, Border, BtnBounds.Height * 0.5f, 1.0f);
    }

    const float IconSize = std::min(15.0f, BtnBounds.Height * 0.56f);
    VectorIconRepository::ConstructControl(
        Surface,
        Icon,
        VectorIconPlacement{
            BtnBounds.X + (BtnBounds.Width - IconSize) * 0.5f,
            BtnBounds.Y + (BtnBounds.Height - IconSize) * 0.5f,
            IconSize,
            Ink,
            1.7f,
        });

    OutClicked = Hover && Pointer.Pressed;
    return Hover;
}

} // namespace

SimulationPlaybackReading Construct(
    PixelSpace&               Surface,
    Rectangle2D               Bounds,
    SimulationPlaybackTuning& Tuning,
    const ControlPointer&     Pointer,
    float                     Opacity) noexcept
{
    SimulationPlaybackReading Reading{};
    const ColorRgba PanelFill   = ColorRgba{ 19u, 20u, 25u, 244u }.ScaledAlpha(Opacity);
    const ColorRgba PanelStroke = ColorRgba{ 46u, 49u, 59u, 220u }.ScaledAlpha(Opacity);
    Surface.FillRectangle(Bounds, PanelFill, 12.0f);
    Surface.StrokeRectangle(Bounds, PanelStroke, 12.0f, 1.0f);

    const float BtnSize = std::min(28.0f, std::max(22.0f, Bounds.Height - 10.0f));
    const float MidY    = Bounds.Y + (Bounds.Height - BtnSize) * 0.5f;
    float       CursorX = Bounds.X + 10.0f;

    // 1. Restart
    bool Clicked = false;
    (void)DrawRoundIcon(
        Surface,
        { CursorX, MidY, BtnSize, BtnSize },
        ControlIcon::ParticleRestart,
        false,
        ColorRgba{ 32u, 34u, 42u, 255u }.ScaledAlpha(Opacity),
        ColorRgba{ 58u, 62u, 74u, 220u }.ScaledAlpha(Opacity),
        ColorRgba{ 218u, 222u, 230u, 255u }.ScaledAlpha(Opacity),
        Pointer,
        Clicked);
    if (Clicked)
    {
        Tuning.ElapsedSeconds   = 0.0;
        Reading.TriggeredAction = SimulationPlaybackAction::Restart;
        Reading.Modified        = true;
    }
    CursorX += BtnSize + 6.0f;

    // 2. Play / Pause primary warm-cream disc matching HTML Particle Studio
    const float PlayDiameter = BtnSize + 4.0f;
    const float PlayY        = Bounds.Y + (Bounds.Height - PlayDiameter) * 0.5f;
    (void)DrawRoundIcon(
        Surface,
        { CursorX, PlayY, PlayDiameter, PlayDiameter },
        Tuning.Playing ? ControlIcon::ParticlePause : ControlIcon::ParticlePlay,
        true,
        ColorRgba{ 242u, 241u, 220u, 255u }.ScaledAlpha(Opacity),
        ColorRgba{ 255u, 254u, 240u, 255u }.ScaledAlpha(Opacity),
        ColorRgba{ 20u, 21u, 25u, 255u }.ScaledAlpha(Opacity),
        Pointer,
        Clicked);
    if (Clicked)
    {
        Tuning.Playing          = !Tuning.Playing;
        Reading.TriggeredAction = SimulationPlaybackAction::TogglePlay;
        Reading.Modified        = true;
    }
    CursorX += PlayDiameter + 6.0f;

    // 3. Single Step
    (void)DrawRoundIcon(
        Surface,
        { CursorX, MidY, BtnSize, BtnSize },
        ControlIcon::ParticleStep,
        false,
        ColorRgba{ 32u, 34u, 42u, 255u }.ScaledAlpha(Opacity),
        ColorRgba{ 58u, 62u, 74u, 220u }.ScaledAlpha(Opacity),
        ColorRgba{ 218u, 222u, 230u, 255u }.ScaledAlpha(Opacity),
        Pointer,
        Clicked);
    if (Clicked)
    {
        Tuning.Playing          = false;
        Reading.TriggeredAction = SimulationPlaybackAction::SingleStep;
        Reading.Modified        = true;
    }
    CursorX += BtnSize + 6.0f;

    // 4. Loop toggle
    (void)DrawRoundIcon(
        Surface,
        { CursorX, MidY, BtnSize, BtnSize },
        ControlIcon::ParticleLoop,
        Tuning.Looping,
        Tuning.Looping ? ColorRgba{ 36u, 64u, 52u, 255u }.ScaledAlpha(Opacity) : ColorRgba{ 32u, 34u, 42u, 255u }.ScaledAlpha(Opacity),
        Tuning.Looping ? ColorRgba{ 62u, 196u, 132u, 255u }.ScaledAlpha(Opacity) : ColorRgba{ 58u, 62u, 74u, 220u }.ScaledAlpha(Opacity),
        Tuning.Looping ? ColorRgba{ 98u, 232u, 166u, 255u }.ScaledAlpha(Opacity) : ColorRgba{ 165u, 170u, 182u, 255u }.ScaledAlpha(Opacity),
        Pointer,
        Clicked);
    if (Clicked)
    {
        Tuning.Looping          = !Tuning.Looping;
        Reading.TriggeredAction = SimulationPlaybackAction::ToggleLoop;
        Reading.Modified        = true;
    }
    CursorX += BtnSize + 14.0f;

    // 5. Tabular Timecode
    char TimecodeText[24]{};
    FormatTimecode(Tuning.ElapsedSeconds, TimecodeText, sizeof(TimecodeText));
    Surface.Text(
        { CursorX, Bounds.Y + (Bounds.Height - 13.0f) * 0.5f },
        13.0f,
        ColorRgba{ 226u, 229u, 236u, 255u }.ScaledAlpha(Opacity),
        TimecodeText);
    CursorX += 78.0f;

    // 6. TimeScale selector pill
    const uint32_t    ScaleIdx = FindScaleIndex(Tuning.TimeScale);
    const Rectangle2D ScaleBounds{ CursorX, MidY, 64.0f, BtnSize };
    const ControlContact ScaleContact = DropdownSelector::ConstructButton(
        Surface,
        ScaleBounds,
        ScaleLabels[ScaleIdx],
        Tuning.ScaleMenuOpen,
        Pointer,
        Opacity);
    if (ScaleContact.Pressed)
    {
        Tuning.ScaleMenuOpen = !Tuning.ScaleMenuOpen;
    }
    if (Tuning.ScaleMenuOpen)
    {
        uint32_t Chosen = ScaleIdx;
        const ControlContact SheetContact = DropdownSelector::ConstructOptionSheet(
            Surface,
            ScaleBounds,
            ScaleLabels.data(),
            static_cast<uint32_t>(ScaleLabels.size()),
            ScaleIdx,
            Pointer,
            Chosen,
            Opacity);
        if (SheetContact.Pressed)
        {
            Tuning.TimeScale        = ScalePresets[Chosen];
            Tuning.ScaleMenuOpen    = false;
            Reading.TriggeredAction = SimulationPlaybackAction::CycleScale;
            Reading.Modified        = true;
        }
    }
    CursorX += 76.0f;

    // 7. Live / Cached status pill
    const ColorRgba DotColor = Tuning.LiveMode ? ColorRgba{ 58u, 214u, 138u, 255u } : ColorRgba{ 232u, 158u, 56u, 255u };
    Surface.FillCircle({ CursorX + 4.0f, Bounds.Y + Bounds.Height * 0.5f }, 3.5f, DotColor.ScaledAlpha(Opacity));
    Surface.Text(
        { CursorX + 13.0f, Bounds.Y + (Bounds.Height - 11.5f) * 0.5f },
        11.5f,
        ColorRgba{ 165u, 170u, 182u, 255u }.ScaledAlpha(Opacity),
        Tuning.LiveMode ? "Live / uncached" : "Cached scrub");

    // 8. Right-hand Burst [B] pill
    if (Tuning.ShowBurst)
    {
        const float       BurstW = 88.0f;
        const Rectangle2D BurstBounds{ Bounds.Right() - BurstW - 10.0f, MidY, BurstW, BtnSize };
        const bool        HoverBurst = Pointer.Enabled && BurstBounds.Contains({ Pointer.X, Pointer.Y });
        Surface.FillRectangle(
            BurstBounds,
            (HoverBurst ? ColorRgba{ 58u, 44u, 26u, 255u } : ColorRgba{ 38u, 31u, 22u, 255u }).ScaledAlpha(Opacity),
            BurstBounds.Height * 0.5f);
        Surface.StrokeRectangle(
            BurstBounds,
            ColorRgba{ 168u, 120u, 52u, 230u }.ScaledAlpha(Opacity),
            BurstBounds.Height * 0.5f,
            1.0f);
        Surface.Text(
            { BurstBounds.X + 14.0f, BurstBounds.Y + (BurstBounds.Height - 12.0f) * 0.5f },
            12.0f,
            ColorRgba{ 238u, 198u, 128u, 255u }.ScaledAlpha(Opacity),
            "Burst");
        const Rectangle2D KeyBadge{ BurstBounds.Right() - 24.0f, BurstBounds.Y + 4.0f, 18.0f, BurstBounds.Height - 8.0f };
        Surface.FillRectangle(KeyBadge, ColorRgba{ 62u, 50u, 32u, 255u }.ScaledAlpha(Opacity), 4.0f);
        Surface.Text(
            { KeyBadge.X + 5.5f, KeyBadge.Y + (KeyBadge.Height - 10.5f) * 0.5f },
            10.5f,
            ColorRgba{ 238u, 198u, 128u, 255u }.ScaledAlpha(Opacity),
            "B");
        if (HoverBurst && Pointer.Pressed)
        {
            Reading.TriggeredAction = SimulationPlaybackAction::TriggerBurst;
            Reading.Modified        = true;
        }
    }

    return Reading;
}

} // namespace Frontier::SimulationPlaybackControls
