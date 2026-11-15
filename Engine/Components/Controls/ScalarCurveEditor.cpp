/*============================================================================================================================================
                                                           SCALARCURVEEDITOR.CPP
============================================================================================================================================*/
// 📈 Implementation of ScalarCurveEditor with grid guides, smooth curve polyline, interactive knot handles & preset chips

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ScalarCurveEditor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Frontier
{

float ScalarCurveProfile::Evaluate(float T) const noexcept
{
    const float ClampedT = std::clamp(T, 0.0f, 1.0f);
    const uint32_t Count = std::clamp(ActiveKnots, 2u, static_cast<uint32_t>(Knots.size()));
    for (uint32_t Idx = 0u; Idx + 1u < Count; ++Idx)
    {
        const ScalarCurveKnot& A = Knots[Idx];
        const ScalarCurveKnot& B = Knots[Idx + 1u];
        if (ClampedT <= B.TimeNormalized || Idx + 2u == Count)
        {
            const float Span = std::max(1.0e-4f, B.TimeNormalized - A.TimeNormalized);
            const float Local = std::clamp((ClampedT - A.TimeNormalized) / Span, 0.0f, 1.0f);
            const float Smooth = Local * Local * (3.0f - 2.0f * Local);
            return std::clamp(A.ScaleNormalized + (B.ScaleNormalized - A.ScaleNormalized) * Smooth, 0.0f, 1.0f);
        }
    }
    return Knots[Count - 1u].ScaleNormalized;
}

namespace ScalarCurveEditor
{

bool Construct(
    PixelSpace&               Surface,
    Rectangle2D               Bounds,
    std::string_view          HeadingTitle,
    ScalarCurveProfile&       Profile,
    ScalarCurveEditorSession& Session,
    const ControlPointer&     Pointer,
    ColorRgba                 AccentInk,
    float                     Opacity) noexcept
{
    Session.Modified = false;

    Surface.FillRectangle(Bounds, ColorRgba{ 19u, 20u, 25u, 248u }.ScaledAlpha(Opacity), 12.0f);
    Surface.StrokeRectangle(Bounds, ColorRgba{ 46u, 49u, 59u, 220u }.ScaledAlpha(Opacity), 12.0f, 1.0f);

    if (!HeadingTitle.empty())
    {
        Surface.Text(
            { Bounds.X + 12.0f, Bounds.Y + 10.0f },
            11.5f,
            ColorRgba{ 208u, 212u, 222u, 255u }.ScaledAlpha(Opacity),
            HeadingTitle.data(),
            HeadingTitle.data() + HeadingTitle.size());
    }

    // Quick preset pills on top right
    struct PresetChip
    {
        const char*                    Label;
        std::array<ScalarCurveKnot, 4> Knots;
    };
    constexpr std::array<PresetChip, 3u> Chips {{
        { "Bell",    {{ { 0.0f, 0.1f }, { 0.30f, 1.0f }, { 0.72f, 0.78f }, { 1.0f, 0.0f } }} },
        { "EaseOut", {{ { 0.0f, 1.0f }, { 0.33f, 0.72f }, { 0.66f, 0.25f }, { 1.0f, 0.0f } }} },
        { "Flat",    {{ { 0.0f, 1.0f }, { 0.33f, 1.0f }, { 0.66f, 1.0f }, { 1.0f, 1.0f } }} },
    }};

    float ChipX = Bounds.Right() - 12.0f;
    for (int Idx = static_cast<int>(Chips.size()) - 1; Idx >= 0; --Idx)
    {
        const float W = 48.0f;
        ChipX -= W;
        const Rectangle2D ChipBounds{ ChipX, Bounds.Y + 7.0f, W, 18.0f };
        const bool Hover = Pointer.Enabled && ChipBounds.Contains({ Pointer.X, Pointer.Y });
        Surface.FillRectangle(
            ChipBounds,
            (Hover ? ColorRgba{ 48u, 52u, 64u, 255u } : ColorRgba{ 30u, 33u, 41u, 255u }).ScaledAlpha(Opacity),
            9.0f);
        Surface.Text(
            { ChipBounds.X + 9.0f, ChipBounds.Y + 3.5f },
            9.5f,
            ColorRgba{ 188u, 194u, 206u, 255u }.ScaledAlpha(Opacity),
            Chips[static_cast<std::size_t>(Idx)].Label);
        if (Hover && Pointer.Pressed)
        {
            Profile.Knots       = Chips[static_cast<std::size_t>(Idx)].Knots;
            Profile.ActiveKnots = 4u;
            Session.Modified    = true;
        }
        ChipX -= 5.0f;
    }

    const Rectangle2D Plot{
        Bounds.X + 12.0f,
        Bounds.Y + 32.0f,
        std::max(40.0f, Bounds.Width - 24.0f),
        std::max(40.0f, Bounds.Height - 44.0f),
    };
    Surface.FillRectangle(Plot, ColorRgba{ 13u, 14u, 18u, 255u }.ScaledAlpha(Opacity), 8.0f);
    Surface.StrokeRectangle(Plot, ColorRgba{ 38u, 41u, 50u, 220u }.ScaledAlpha(Opacity), 8.0f, 1.0f);

    // Subtle 4x4 grid lines
    for (int G = 1; G < 4; ++G)
    {
        const float F  = static_cast<float>(G) * 0.25f;
        const float Gx = Plot.X + F * Plot.Width;
        const float Gy = Plot.Y + F * Plot.Height;
        Surface.StrokeLine({ Gx, Plot.Y + 2.0f }, { Gx, Plot.Bottom() - 2.0f }, ColorRgba{ 32u, 35u, 44u, 180u }.ScaledAlpha(Opacity), 1.0f);
        Surface.StrokeLine({ Plot.X + 2.0f, Gy }, { Plot.Right() - 2.0f, Gy }, ColorRgba{ 32u, 35u, 44u, 180u }.ScaledAlpha(Opacity), 1.0f);
    }

    // Curve polyline
    constexpr uint32_t SampleCount = 32u;
    std::vector<Point2D> Polyline;
    Polyline.reserve(SampleCount);
    for (uint32_t S = 0u; S < SampleCount; ++S)
    {
        const float T = static_cast<float>(S) / static_cast<float>(SampleCount - 1u);
        const float Y = Profile.Evaluate(T);
        Polyline.push_back({
            Plot.X + 6.0f + T * (Plot.Width - 12.0f),
            Plot.Bottom() - 6.0f - Y * (Plot.Height - 12.0f),
        });
    }
    Surface.StrokePolyline(Polyline, AccentInk.ScaledAlpha(Opacity), 2.2f, false);

    // Interactive knots
    const uint32_t KnotCount = std::clamp(Profile.ActiveKnots, 2u, static_cast<uint32_t>(Profile.Knots.size()));
    if (!Pointer.Down)
    {
        Session.DraggingKnot = false;
    }

    if (Pointer.Enabled && Pointer.Pressed)
    {
        for (uint32_t K = 0u; K < KnotCount; ++K)
        {
            const float Kx = Plot.X + 6.0f + Profile.Knots[K].TimeNormalized * (Plot.Width - 12.0f);
            const float Ky = Plot.Bottom() - 6.0f - Profile.Knots[K].ScaleNormalized * (Plot.Height - 12.0f);
            const float Dx = Pointer.X - Kx;
            const float Dy = Pointer.Y - Ky;
            if (Dx * Dx + Dy * Dy <= 100.0f)
            {
                Session.SelectedKnotIndex = static_cast<int32_t>(K);
                Session.DraggingKnot      = true;
                break;
            }
        }
    }

    if (Session.DraggingKnot && Pointer.Enabled && Session.SelectedKnotIndex >= 0 &&
        static_cast<uint32_t>(Session.SelectedKnotIndex) < KnotCount)
    {
        const uint32_t K    = static_cast<uint32_t>(Session.SelectedKnotIndex);
        const float    NewY = std::clamp((Plot.Bottom() - 6.0f - Pointer.Y) / std::max(1.0f, Plot.Height - 12.0f), 0.0f, 1.0f);
        Profile.Knots[K].ScaleNormalized = NewY;
        if (K > 0u && K + 1u < KnotCount)
        {
            const float MinT = Profile.Knots[K - 1u].TimeNormalized + 0.04f;
            const float MaxT = Profile.Knots[K + 1u].TimeNormalized - 0.04f;
            const float RawT = (Pointer.X - (Plot.X + 6.0f)) / std::max(1.0f, Plot.Width - 12.0f);
            Profile.Knots[K].TimeNormalized = std::clamp(RawT, MinT, MaxT);
        }
        Session.Modified = true;
    }

    for (uint32_t K = 0u; K < KnotCount; ++K)
    {
        const float Kx = Plot.X + 6.0f + Profile.Knots[K].TimeNormalized * (Plot.Width - 12.0f);
        const float Ky = Plot.Bottom() - 6.0f - Profile.Knots[K].ScaleNormalized * (Plot.Height - 12.0f);
        const bool  Selected = (Session.SelectedKnotIndex == static_cast<int32_t>(K));
        Surface.FillCircle({ Kx, Ky }, Selected ? 5.2f : 4.2f, ColorRgba{ 242u, 241u, 220u, 255u }.ScaledAlpha(Opacity));
        Surface.StrokeCircle({ Kx, Ky }, Selected ? 5.2f : 4.2f, AccentInk.ScaledAlpha(Opacity), 1.6f);
    }

    return Session.Modified;
}

} // namespace ScalarCurveEditor

} // namespace Frontier
