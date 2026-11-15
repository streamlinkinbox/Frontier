/*============================================================================================================================================
                                                   DIAGNOSTICINFOGRAPHICCANVAS.CPP
============================================================================================================================================*/
// 📊 Implementation of DiagnosticInfographicCanvas rendering crisp animated solver diagrams & interactive bokeh aperture cards

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Diagnostics/DiagnosticInfographicCanvas.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace Frontier::DiagnosticInfographicCanvas
{

namespace
{

constexpr float TwoPi = 6.28318530718f;

void ConstructBokehApertureCards(
    PixelSpace&                   Surface,
    Rectangle2D                   Bounds,
    uint32_t                      ActiveCode,
    float                         ScaleScalar,
    const ControlPointer&         Pointer,
    DiagnosticInfographicReading& OutReading,
    float                         Opacity) noexcept
{
    Surface.FillRectangle(Bounds, ColorRgba{ 16u, 17u, 22u, 250u }.ScaledAlpha(Opacity), 12.0f);
    Surface.StrokeRectangle(Bounds, ColorRgba{ 44u, 47u, 58u, 220u }.ScaledAlpha(Opacity), 12.0f, 1.0f);

    Surface.Text(
        { Bounds.X + 12.0f, Bounds.Y + 10.0f },
        10.0f,
        ColorRgba{ 138u, 144u, 158u, 255u }.ScaledAlpha(Opacity),
        "OPTICAL BOKEH APERTURE");

    char ScaleBadge[24]{};
    std::snprintf(ScaleBadge, sizeof(ScaleBadge), "SCALE %.2fx", static_cast<double>(ScaleScalar));
    const Rectangle2D BadgeRect{ Bounds.Right() - 88.0f, Bounds.Y + 7.0f, 76.0f, 18.0f };
    Surface.FillRectangle(BadgeRect, ColorRgba{ 24u, 52u, 42u, 255u }.ScaledAlpha(Opacity), 6.0f);
    Surface.Text(
        { BadgeRect.X + 8.0f, BadgeRect.Y + 3.5f },
        9.5f,
        ColorRgba{ 92u, 224u, 158u, 255u }.ScaledAlpha(Opacity),
        ScaleBadge);

    struct CardMeta
    {
        const char* Title;
        const char* Sub;
    };
    constexpr std::array<CardMeta, 4u> Cards {{
        { "Mixed",    "Depth Bokeh" },
        { "Sharp",    "Pinpoint" },
        { "Creamy",   "Soft Disc" },
        { "Rim Ring", "Aperture" },
    }};

    const float Gap      = 7.0f;
    const float InnerW   = std::max(40.0f, Bounds.Width - 24.0f - Gap * 3.0f);
    const float CardW    = InnerW * 0.25f;
    const float CardY    = Bounds.Y + 32.0f;
    const float CardH    = std::max(72.0f, Bounds.Height - 62.0f);

    for (uint32_t Idx = 0u; Idx < 4u; ++Idx)
    {
        const Rectangle2D Card{ Bounds.X + 12.0f + static_cast<float>(Idx) * (CardW + Gap), CardY, CardW, CardH };
        const bool        Selected = (ActiveCode == Idx);
        const bool        Hover    = Pointer.Enabled && Card.Contains({ Pointer.X, Pointer.Y });

        const ColorRgba CardFill = Selected
            ? ColorRgba{ 44u, 38u, 32u, 255u }
            : (Hover ? ColorRgba{ 32u, 34u, 42u, 255u } : ColorRgba{ 23u, 25u, 31u, 255u });
        const ColorRgba CardStroke = Selected
            ? ColorRgba{ 196u, 152u, 96u, 255u }
            : ColorRgba{ 48u, 52u, 64u, 220u };
        Surface.FillRectangle(Card, CardFill.ScaledAlpha(Opacity), 9.0f);
        Surface.StrokeRectangle(Card, CardStroke.ScaledAlpha(Opacity), 9.0f, Selected ? 1.4f : 1.0f);

        const Point2D GlyphCenter{ Card.X + Card.Width * 0.5f, Card.Y + Card.Height * 0.36f };
        if (Idx == 0u)
        {
            Surface.FillCircle({ GlyphCenter.X - 4.0f, GlyphCenter.Y }, 7.0f, ColorRgba{ 165u, 174u, 186u, 130u }.ScaledAlpha(Opacity));
            Surface.FillCircle({ GlyphCenter.X + 6.0f, GlyphCenter.Y - 2.0f }, 3.2f, ColorRgba{ 244u, 246u, 252u, 255u }.ScaledAlpha(Opacity));
        }
        else if (Idx == 1u)
        {
            Surface.FillCircle(GlyphCenter, 4.0f, ColorRgba{ 244u, 246u, 252u, 255u }.ScaledAlpha(Opacity));
        }
        else if (Idx == 2u)
        {
            Surface.FillCircle(GlyphCenter, 10.0f, ColorRgba{ 152u, 176u, 198u, 90u }.ScaledAlpha(Opacity));
            Surface.FillCircle(GlyphCenter, 6.0f, ColorRgba{ 216u, 228u, 240u, 185u }.ScaledAlpha(Opacity));
        }
        else
        {
            Surface.StrokeCircle(GlyphCenter, 9.5f, ColorRgba{ 236u, 240u, 248u, 240u }.ScaledAlpha(Opacity), 1.8f);
        }

        Surface.Text(
            { Card.X + 8.0f, Card.Bottom() - 28.0f },
            11.0f,
            ColorRgba{ 232u, 235u, 242u, 255u }.ScaledAlpha(Opacity),
            Cards[Idx].Title);
        Surface.Text(
            { Card.X + 8.0f, Card.Bottom() - 15.0f },
            9.0f,
            ColorRgba{ 138u, 144u, 158u, 255u }.ScaledAlpha(Opacity),
            Cards[Idx].Sub);

        if (Hover && Pointer.Pressed)
        {
            OutReading.ChosenApertureCode = Idx;
            OutReading.Modified           = true;
        }
    }

    Surface.Text(
        { Bounds.X + 12.0f, Bounds.Bottom() - 18.0f },
        9.0f,
        ColorRgba{ 132u, 138u, 152u, 255u }.ScaledAlpha(Opacity),
        "Click any aperture lens to switch bokeh head style");
}

} // namespace

DiagnosticInfographicReading Construct(
    PixelSpace&                      Surface,
    Rectangle2D                      Bounds,
    const DiagnosticInfographicSpec& Spec,
    const ControlPointer&            Pointer,
    float                            Opacity) noexcept
{
    DiagnosticInfographicReading Reading{};
    Reading.ChosenApertureCode = Spec.ActiveApertureCode;

    if (Spec.Archetype == InfographicArchetype::OpticalBokehAperture)
    {
        ConstructBokehApertureCards(Surface, Bounds, Spec.ActiveApertureCode, Spec.PrimaryScalar, Pointer, Reading, Opacity);
        return Reading;
    }

    Surface.FillRectangle(Bounds, ColorRgba{ 16u, 17u, 22u, 250u }.ScaledAlpha(Opacity), 12.0f);
    Surface.StrokeRectangle(Bounds, ColorRgba{ 44u, 47u, 58u, 220u }.ScaledAlpha(Opacity), 12.0f, 1.0f);

    const float Phase = static_cast<float>(std::fmod(Spec.AnimationSeconds * 1.4, 6.2831853));
    const Rectangle2D Plot = Bounds.InsetXY(12.0f, 26.0f);
    Surface.FillRectangle(Plot, ColorRgba{ 11u, 12u, 16u, 255u }.ScaledAlpha(Opacity), 8.0f);

    const Point2D C{ Plot.X + Plot.Width * 0.5f, Plot.Y + Plot.Height * 0.5f };

    switch (Spec.Archetype)
    {
    case InfographicArchetype::SpiderwebWeave:
    {
        for (int R = 0; R < 8; ++R)
        {
            const float Ang = TwoPi * static_cast<float>(R) / 8.0f;
            Surface.StrokeLine(C, { C.X + 38.0f * std::cos(Ang), C.Y + 34.0f * std::sin(Ang) }, ColorRgba{ 160u, 190u, 220u, 160u }.ScaledAlpha(Opacity), 1.1f);
        }
        for (int Ring = 1; Ring <= 3; ++Ring)
        {
            Surface.StrokeCircle(C, static_cast<float>(Ring) * 11.0f, ColorRgba{ 210u, 225u, 240u, 180u }.ScaledAlpha(Opacity), 1.0f);
        }
        break;
    }
    case InfographicArchetype::FerrofluidCrown:
    case InfographicArchetype::BotanicalSway:
    {
        std::vector<Point2D> Wave;
        for (int I = 0; I <= 24; ++I)
        {
            const float T = static_cast<float>(I) / 24.0f;
            const float X = Plot.X + 12.0f + T * (Plot.Width - 24.0f);
            const float Amp = (Spec.Archetype == InfographicArchetype::FerrofluidCrown) ? std::abs(std::sin(T * 18.0f + Phase)) * 22.0f : std::sin(T * 8.0f + Phase) * 14.0f;
            Wave.push_back({ X, Plot.Bottom() - 14.0f - Amp });
        }
        Surface.StrokePolyline(Wave, ColorRgba{ 110u, 212u, 245u, 255u }.ScaledAlpha(Opacity), 2.0f, false);
        break;
    }
    case InfographicArchetype::ShieldBarrier:
    case InfographicArchetype::GravitationalLens:
    case InfographicArchetype::SplashCrown:
    {
        for (int Ring = 1; Ring <= 3; ++Ring)
        {
            const float Rad = 10.0f + static_cast<float>(Ring) * 9.0f + 2.5f * std::sin(Phase + static_cast<float>(Ring));
            Surface.StrokeCircle(C, Rad, ColorRgba{ 98u, 214u, 232u, static_cast<uint8_t>(220 - Ring * 45) }.ScaledAlpha(Opacity), 1.5f);
        }
        Surface.FillCircle(C, 5.0f, ColorRgba{ 242u, 241u, 220u, 255u }.ScaledAlpha(Opacity));
        break;
    }
    default:
    {
        for (int S = 0; S < 6; ++S)
        {
            const float YOff = -18.0f + static_cast<float>(S) * 7.5f;
            std::vector<Point2D> Stream;
            for (int K = 0; K <= 12; ++K)
            {
                const float T = static_cast<float>(K) / 12.0f;
                const float X = Plot.X + 14.0f + T * (Plot.Width - 28.0f);
                const float Y = C.Y + YOff + 6.0f * std::sin(T * 6.0f + Phase + static_cast<float>(S) * 0.7f);
                Stream.push_back({ X, Y });
            }
            Surface.StrokePolyline(Stream, ColorRgba{ 130u, 198u, 255u, 210u }.ScaledAlpha(Opacity), 1.4f, false);
            Surface.FillCircle(Stream.back(), 2.2f, ColorRgba{ 242u, 241u, 220u, 255u }.ScaledAlpha(Opacity));
        }
        break;
    }
    }

    if (!Spec.CaptionText.empty())
    {
        Surface.Text(
            { Bounds.X + 12.0f, Bounds.Bottom() - 18.0f },
            9.5f,
            ColorRgba{ 142u, 148u, 162u, 255u }.ScaledAlpha(Opacity),
            Spec.CaptionText.data(),
            Spec.CaptionText.data() + Spec.CaptionText.size());
    }

    return Reading;
}

} // namespace Frontier::DiagnosticInfographicCanvas
