/*============================================================================================================================================
                                                               SCALARSLIDER.CPP
============================================================================================================================================*/
// 🧩 Neutral-track scalar slider, split readout capsules, and labeled inspector slider row implementation.

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ScalarSlider.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace ScalarSlider
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  PUBLIC IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

ControlContact Construct(
    PixelSpace&            Surface,
    Rectangle2D            Bounds,
    float                  Minimum,
    float                  Maximum,
    float                  CurrentPosition,
    bool                   DraggingActive,
    const ControlPointer&  Pointer,
    float&                 UpdatedPosition,
    float                  Opacity,
    float                  RailHeight,
    float                  KnobDiameter,
    const OptionalTooltip& Tooltip) noexcept
{
    ControlContact         Contact{};
    const ControlColorSet& Colors = ControlAppearance::Colors();

    const float       TrackHeight   = std::clamp(RailHeight, 4.0f, 48.0f);
    const float       ThumbDiameter = std::min(std::clamp(KnobDiameter, 4.0f, 48.0f), std::max(1.0f, Bounds.Width));
    const float       CenterY       = Bounds.Y + Bounds.Height * 0.5f;
    const Rectangle2D Track{ Bounds.X, CenterY - TrackHeight * 0.5f, Bounds.Width, TrackHeight };
    const Rectangle2D HitBand{ Bounds.X, CenterY - ThumbDiameter * 0.5f - 2.0f, Bounds.Width, ThumbDiameter + 4.0f };

    Contact.Hovered  = ControlAppearance::QueryPointerOver(HitBand, Pointer);
    Contact.Pressed  = Contact.Hovered && Pointer.Pressed;
    Contact.Dragging = DraggingActive || Contact.Pressed;

    const float Span     = std::max(Maximum - Minimum, 1.0e-6f);
    float       Fraction = std::clamp((CurrentPosition - Minimum) / Span, 0.0f, 1.0f);
    if (Contact.Dragging && Pointer.Down && Pointer.Enabled)
    {
        const float Usable = std::max(Bounds.Width - ThumbDiameter, 1.0f);
        Fraction           = std::clamp((Pointer.X - Bounds.X - ThumbDiameter * 0.5f) / Usable, 0.0f, 1.0f);
        UpdatedPosition    = Minimum + Fraction * Span;
    }

    const float Radius = TrackHeight * 0.5f;
    Surface.FillRectangle(Track, Colors.SliderTrack.Faded(Opacity).ToRgba8(), Radius);

    const float ThumbCenterX = Bounds.X + ThumbDiameter * 0.5f + Fraction * (Bounds.Width - ThumbDiameter);
    const float FillWidth    = std::max(ThumbCenterX - Track.X, 0.0f);
    if (FillWidth > 1.0f)
    {
        Surface.FillRectangle({ Track.X, Track.Y, std::min(FillWidth, Track.Width), Track.Height },
                              Colors.SliderFill.Faded(Opacity).ToRgba8(), Radius);
    }

    const float ThumbRadius = ThumbDiameter * 0.5f * (Contact.Dragging && Pointer.Down ? 1.12f : 1.0f);
    ControlAppearance::ConstructCircleFill(Surface, ThumbCenterX, CenterY, ThumbRadius, Colors.SliderThumb.Faded(Opacity));
    if (Contact.Hovered && Tooltip.Enabled)
    {
        ControlAppearance::RecordHoveredTooltip(Track, Tooltip);
    }
    return Contact;
}

void ConstructReadoutCapsule(
    PixelSpace& Surface,
    float       X,
    float       Y,
    const char* NumberText,
    const char* UnitText,
    float       Opacity,
    float       TotalWidth,
    float       UnitCellWidth) noexcept
{
    ConstructCompactReadoutCapsule(
        Surface,
        { X, Y, TotalWidth, ControlMetrics::ControlHeight },
        NumberText,
        UnitText,
        Opacity,
        UnitCellWidth);
}

void ConstructCompactReadoutCapsule(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    const char* NumberText,
    const char* UnitText,
    float       Opacity,
    float       UnitCellWidth) noexcept
{
    const ControlColorSet& Colors = ControlAppearance::Colors();
    const float            Height = std::max(Bounds.Height, 16.0f);
    const float            Width  = std::max(Bounds.Width, 32.0f);
    UnitCellWidth                 = std::clamp(UnitCellWidth, 12.0f, std::max(Width - 20.0f, 12.0f));
    const float Radius            = std::min(Height * 0.5f, 10.0f);
    const float EffectiveUnitW    = (UnitText != nullptr && UnitText[0] != '\0') ? UnitCellWidth : 0.0f;
    const float SeamX             = Bounds.X + Width - EffectiveUnitW;

    const Rectangle2D Whole{ Bounds.X, Bounds.Y, Width, Height };
    const Rectangle2D NumberCell{ Bounds.X, Bounds.Y, std::max(Width - EffectiveUnitW, 1.0f), Height };
    const Rectangle2D UnitCell{ SeamX, Bounds.Y, EffectiveUnitW, Height };

    Surface.FillRectangle(Whole, Colors.Inset.Faded(Opacity).ToRgba8(), Radius);
    Surface.FillRectangle(NumberCell, Colors.Field.Faded(Opacity).ToRgba8(), Radius);
    if (EffectiveUnitW > 0.0f && NumberCell.Width > Radius)
    {
        Surface.FillRectangle({ Bounds.X + Radius, Bounds.Y, NumberCell.Width - Radius, Height },
                              Colors.Field.Faded(Opacity).ToRgba8(), 0.0f);
        Surface.FillRectangle({ SeamX, Bounds.Y, 1.0f, Height }, Colors.Stroke.Faded(Opacity).ToRgba8(), 0.0f);
    }

    const float NumberFontSize = (Height >= 34.0f) ? 13.5f : 11.0f;
    const float UnitFontSize   = (Height >= 34.0f) ? 11.5f : 9.5f;
    ControlAppearance::ConstructRoundedOutline(Surface, Whole, Colors.Stroke.Faded(Opacity), Radius);
    ControlAppearance::TextCentered(Surface, NumberCell, Colors.Text.Faded(Opacity), NumberText != nullptr ? NumberText : "", NumberFontSize);
    if (EffectiveUnitW > 0.0f)
    {
        ControlAppearance::TextCentered(Surface, UnitCell, Colors.TextFaint.Faded(Opacity), UnitText, UnitFontSize);
    }
}

ControlContact ConstructWithReadout(
    PixelSpace&            Surface,
    Rectangle2D            RowBounds,
    const char*            NumberText,
    const char*            UnitText,
    float                  Minimum,
    float                  Maximum,
    float                  CurrentPosition,
    bool                   DraggingActive,
    const ControlPointer&  Pointer,
    float&                 UpdatedPosition,
    float                  Opacity,
    const OptionalTooltip& Tooltip) noexcept
{
    ConstructReadoutCapsule(Surface, RowBounds.X, RowBounds.Y, NumberText, UnitText, Opacity);
    const float       SliderLeft = RowBounds.X + ControlMetrics::ReadoutCapsuleWidth + ControlMetrics::RowGap;
    const Rectangle2D SliderBounds{ SliderLeft, RowBounds.Y, std::max(1.0f, RowBounds.Right() - SliderLeft), RowBounds.Height };
    const ControlContact Contact = Construct(
        Surface, SliderBounds, Minimum, Maximum, CurrentPosition, DraggingActive, Pointer, UpdatedPosition, Opacity);
    if (Tooltip.Enabled && ControlAppearance::QueryPointerOver(RowBounds, Pointer))
    {
        ControlAppearance::RecordHoveredTooltip(RowBounds, Tooltip);
    }
    return Contact;
}

bool ConstructLabeledInspectorRow(
    PixelSpace&            Surface,
    Rectangle2D            RowBounds,
    int32_t                SliderId,
    const char*            Label,
    float                  Minimum,
    float                  Maximum,
    float&                 InOutScalar,
    const char*            UnitSuffix,
    int32_t                DecimalDigits,
    const ControlPointer&  Pointer,
    int32_t&               ActiveSliderId,
    float                  Opacity,
    float                  RailHeight,
    float                  KnobDiameter,
    const OptionalTooltip& Tooltip) noexcept
{
    const ControlColorSet& Colors   = ControlAppearance::Colors();
    const ColorRgba        LabelInk   = Colors.Text.Faded(Opacity * 0.80f).ToRgba8();
    const ColorRgba        ReadoutInk = Colors.Text.Faded(Opacity * 0.96f).ToRgba8();

    if (Label != nullptr && Label[0] != '\0')
    {
        Surface.Text({ RowBounds.X, RowBounds.Y }, 11.0f, LabelInk, Label);
    }

    char FormattedBuf[64]{};
    if (DecimalDigits <= 0)
    {
        std::snprintf(FormattedBuf, sizeof(FormattedBuf), "%.0f%s",
                      std::round(InOutScalar),
                      (UnitSuffix != nullptr) ? UnitSuffix : "");
    }
    else
    {
        std::snprintf(FormattedBuf, sizeof(FormattedBuf), "%.*f%s",
                      static_cast<int>(DecimalDigits),
                      InOutScalar,
                      (UnitSuffix != nullptr) ? UnitSuffix : "");
    }
    const float ReadoutW = Surface.MeasureTextWidth(11.0f, FormattedBuf);
    Surface.Text({ RowBounds.Right() - ReadoutW, RowBounds.Y }, 11.0f, ReadoutInk, FormattedBuf);

    const float       TrackHeight = std::max(RailHeight, 12.0f);
    const Rectangle2D TrackRect{ RowBounds.X, RowBounds.Y + 18.0f, RowBounds.Width, TrackHeight };
    float             Updated = InOutScalar;
    const ControlContact Contact = Construct(
        Surface,
        TrackRect,
        Minimum,
        Maximum,
        InOutScalar,
        ActiveSliderId == SliderId,
        Pointer,
        Updated,
        Opacity,
        RailHeight,
        KnobDiameter,
        Tooltip);

    bool Changed = false;
    if (Contact.Pressed)
    {
        ActiveSliderId = SliderId;
    }
    if (ActiveSliderId == SliderId && Pointer.Down && Pointer.Enabled)
    {
        const float Clamped = std::clamp(Updated, std::min(Minimum, Maximum), std::max(Minimum, Maximum));
        if (std::fabs(Clamped - InOutScalar) > 1.0e-5f)
        {
            InOutScalar = Clamped;
            Changed     = true;
        }
    }
    if (Pointer.Released && ActiveSliderId == SliderId)
    {
        ActiveSliderId = -1;
    }

    return Changed;
}

} // namespace ScalarSlider

} // namespace Frontier
