//========================================================================================================================
// 🧩 ControlKit — reusable immediate-mode widgets (see ControlKit.h)
//========================================================================================================================
#include "ControlKit.h"
#include "GlyphSpace.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Frontier {

namespace {

constexpr float Pi = 3.14159265358979f;

float PillRadius(const PlaneExtent& Extent) noexcept { return std::min(Extent.Width(), Extent.Height()) * 0.5f; }

void FillCircle(PixelSpace& Surface, float Cx, float Cy, float R, ColorQuad Colour) noexcept
{
    Surface.FillRectangle(Spanning(Cx - R, Cy - R, R * 2.0f, R * 2.0f), Colour, R);
}

} // namespace

ControlKitPalette ControlKit::ActivePalette{};

//------------------------------------------------------------------------------------------------------------------------
//                                                        THEME
//------------------------------------------------------------------------------------------------------------------------

namespace {

// Notch: hover:bg-white/5 on dark themes, hover:bg-black/5 on light ones (OutlinerPanel: 'hover:bg-black/5 dark:hover:bg-white/5').
ColorQuad HoverWash() noexcept
{
    return ControlKit::Palette().LightSurface ? ColorQuad{ 0.0f, 0.0f, 0.0f, 0.05f } : ColorQuad{ 1.0f, 1.0f, 1.0f, 0.05f };
}

float Luminance(ColorQuad C) noexcept { return 0.2126f * C.Red + 0.7152f * C.Green + 0.0722f * C.Blue; }

} // namespace

void ControlKit::AssignTheme(const ThemeStructure& Theme, ColorQuad WarningColour, ColorQuad SuccessColour, ColorQuad InfoColour, ColorQuad CautionColour) noexcept
{
    const ThemePalette& P = Theme.QueryPalette();
    ControlKitPalette& K = ActivePalette;
    K = ControlKitPalette{};                       // geometry-free defaults, then override every themed slot
    K.LightSurface = Luminance(P.MainBackground) > 0.5f;
    K.Panel        = P.PanelBackground;
    K.Inset        = P.InputBackground;
    K.Field        = P.MainBackground;
    K.Raised       = P.ActiveBackground;
    K.Selected     = P.CardSubBackground;
    K.Stroke       = P.PanelBorder;
    K.StrokeStrong = P.CardSubBackground;
    K.Divider      = P.DividerColor;
    K.Card         = P.CardBackground;
    K.CardSub      = P.CardSubBackground;
    K.Text         = P.TextMain;
    K.TextDim      = P.TextMuted;
    K.TextFaint    = P.TextMuted; K.TextFaint.Alpha *= 0.7f;
    // Notch hard-codes the primary pill / active chip / segmented cell as bg-white text-black on dark themes; on light
    //    surfaces that would vanish, so the primary inverts to the text colour with the canvas as ink.
    K.Primary      = K.LightSurface ? P.TextMain : ColorQuad{ 1.0f, 1.0f, 1.0f, 1.0f };
    K.PrimaryInk   = K.LightSurface ? P.MainBackground : ColorQuad{ 0x11 / 255.0f, 0x11 / 255.0f, 0x11 / 255.0f, 1.0f };
    K.Accent       = Theme.QueryAccentColor();
    K.AccentInk    = Luminance(K.Accent) > 0.8f ? ColorQuad{ 0x11 / 255.0f, 0x11 / 255.0f, 0x11 / 255.0f, 1.0f } : ColorQuad{ 1.0f, 1.0f, 1.0f, 1.0f };
    K.AccentSoft   = K.Accent; K.AccentSoft.Alpha = 0.12f;
    K.Highlight    = K.Accent;                     // dashboard pill fill follows the accent
    K.SliderFill   = K.Accent;                     // Notch <Slider>: linear-gradient(accentColor …)
    K.SliderThumb  = K.Accent;                     // --thumb-color: accentColor
    K.SwitchKnobOff= K.LightSurface ? P.TextMuted : ColorQuad{ 0xBD / 255.0f, 0xBD / 255.0f, 0xBD / 255.0f, 1.0f };
    K.Warning      = WarningColour;
    K.Ok           = SuccessColour;
    K.Info         = InfoColour;
    K.Caution      = CautionColour;
}

void ControlKit::BlendPalette(const ControlKitPalette& From, const ControlKitPalette& To, float T) noexcept
{
    // Live theme preview: smoothstepped cross-fade so a tile tap reads as a morph, not a snap.
    const float S = std::clamp(T, 0.0f, 1.0f);
    const float E = S * S * (3.0f - 2.0f * S);
    const auto Mix = [E](const ColorQuad& A, const ColorQuad& B) noexcept -> ColorQuad
    {
        return ColorQuad{ A.Red + (B.Red - A.Red) * E, A.Green + (B.Green - A.Green) * E,
                          A.Blue + (B.Blue - A.Blue) * E, A.Alpha + (B.Alpha - A.Alpha) * E };
    };
    ControlKitPalette& K = ActivePalette;
    K.Panel = Mix(From.Panel, To.Panel);                 K.Inset = Mix(From.Inset, To.Inset);
    K.Field = Mix(From.Field, To.Field);                 K.Raised = Mix(From.Raised, To.Raised);
    K.Selected = Mix(From.Selected, To.Selected);        K.Stroke = Mix(From.Stroke, To.Stroke);
    K.StrokeStrong = Mix(From.StrokeStrong, To.StrokeStrong); K.Divider = Mix(From.Divider, To.Divider);
    K.Card = Mix(From.Card, To.Card);                    K.CardSub = Mix(From.CardSub, To.CardSub);
    K.Text = Mix(From.Text, To.Text);                    K.TextDim = Mix(From.TextDim, To.TextDim);
    K.TextFaint = Mix(From.TextFaint, To.TextFaint);     K.Primary = Mix(From.Primary, To.Primary);
    K.PrimaryInk = Mix(From.PrimaryInk, To.PrimaryInk);  K.Accent = Mix(From.Accent, To.Accent);
    K.AccentInk = Mix(From.AccentInk, To.AccentInk);     K.AccentSoft = Mix(From.AccentSoft, To.AccentSoft);
    K.Highlight = Mix(From.Highlight, To.Highlight);     K.Danger = Mix(From.Danger, To.Danger);
    K.Ok = Mix(From.Ok, To.Ok);                          K.Info = Mix(From.Info, To.Info);
    K.Warning = Mix(From.Warning, To.Warning);           K.Caution = Mix(From.Caution, To.Caution);
    K.SliderFill = Mix(From.SliderFill, To.SliderFill);  K.SliderThumb = Mix(From.SliderThumb, To.SliderThumb);
    K.SwitchKnobOff = Mix(From.SwitchKnobOff, To.SwitchKnobOff);
    K.LightSurface = E < 0.5f ? From.LightSurface : To.LightSurface;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

void ControlKit::OutlineRounded(PixelSpace& Surface, const PlaneExtent& Extent, ColorQuad Colour, float Radius, float Thickness) noexcept
{
    std::vector<PlanePoint> Points;
    const float R = std::min(Radius, PillRadius(Extent));
    constexpr int Segments = 10;
    auto Arc = [&](float Cx, float Cy, float From)
    {
        for (int I = 0; I <= Segments; ++I)
        {
            const float A = From + (Pi * 0.5f) * (static_cast<float>(I) / Segments);
            Points.push_back(PlanePoint{ Cx + std::cos(A) * R, Cy + std::sin(A) * R });
        }
    };
    Arc(Extent.MaximumX - R, Extent.MinimumY + R, -Pi * 0.5f);
    Arc(Extent.MaximumX - R, Extent.MaximumY - R, 0.0f);
    Arc(Extent.MinimumX + R, Extent.MaximumY - R, Pi * 0.5f);
    Arc(Extent.MinimumX + R, Extent.MinimumY + R, Pi);
    Surface.StrokePolyline(Points.data(), static_cast<uint32_t>(Points.size()), Colour, Thickness, true);
}

void ControlKit::OutlineCircle(PixelSpace& Surface, float Cx, float Cy, float R, ColorQuad Colour, float Thickness) noexcept
{
    std::vector<PlanePoint> Points;
    constexpr int Segments = 40;
    for (int I = 0; I < Segments; ++I)
    {
        const float A = (2.0f * Pi) * (static_cast<float>(I) / Segments);
        Points.push_back(PlanePoint{ Cx + std::cos(A) * R, Cy + std::sin(A) * R });
    }
    Surface.StrokePolyline(Points.data(), static_cast<uint32_t>(Points.size()), Colour, Thickness, true);
}

void ControlKit::Divider(PixelSpace& Surface, float X, float Y, float Width, ColorQuad Colour) noexcept
{
    Surface.FillRectangle(Spanning(X, Y, Width, 1.0f), Colour);
}

void ControlKit::TextCentred(PixelSpace& Surface, const PlaneExtent& Extent, ColorQuad Colour, const char* Utf8, float Size) noexcept
{
    const PlanePoint M = Surface.MeasureText(Utf8, Size);
    Surface.Text(Extent.MinimumX + (Extent.Width() - M.X) * 0.5f, Extent.MinimumY + (Extent.Height() - M.Y) * 0.5f, Colour, Utf8, Size);
}

void ControlKit::TextLeading(PixelSpace& Surface, const PlaneExtent& Extent, float PadX, ColorQuad Colour, const char* Utf8, float Size) noexcept
{
    const PlanePoint M = Surface.MeasureText(Utf8, Size);
    Surface.Text(Extent.MinimumX + PadX, Extent.MinimumY + (Extent.Height() - M.Y) * 0.5f, Colour, Utf8, Size);
}

void ControlKit::Glyph(PixelSpace& Surface, float X, float Y, float Size, ColorQuad Colour, ControlCentreIconCategory Icon, float Stroke) noexcept
{
    GlyphPlacement P{};
    P.X = X; P.Y = Y; P.Size = Size; P.StrokeWidth = Stroke; P.Colour = Colour;
    GlyphSpace::Stroke(Surface, VectorCodec::QueryControlCentreSvgPath(Icon), P);
}

void ControlKit::GlyphCentred(PixelSpace& Surface, const PlaneExtent& Extent, float Size, ColorQuad Colour, ControlCentreIconCategory Icon, float Stroke) noexcept
{
    Glyph(Surface, Extent.MinimumX + (Extent.Width() - Size) * 0.5f, Extent.MinimumY + (Extent.Height() - Size) * 0.5f, Size, Colour, Icon, Stroke);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       BUTTONS
//------------------------------------------------------------------------------------------------------------------------

float ControlKit::ButtonWidth(PixelSpace& Surface, const ButtonStructure& Button) noexcept
{
    return Surface.MeasureText(Button.Label, Button.FontSize).X + Button.PaddingX * 2.0f;
}

ControlHit ControlKit::PillButton(PixelSpace& Surface, const PlaneExtent& Extent, const ButtonStructure& Button, const ControlPointer& Pointer, float Opacity) noexcept
{
    ControlHit Hit{};
    const float A = Opacity * (Button.Disabled ? ControlKitTokens::DisabledAlpha : 1.0f);
    Hit.Hovered = !Button.Disabled && Over(Extent, Pointer);
    Hit.Pressed = Hit.Hovered && Pointer.Pressed;
    Hit.Clicked = Hit.Hovered && Pointer.Released;

    const float R = PillRadius(Extent);
    ColorQuad Fill = Palette().Inset, Ink = Palette().Text, Border = Palette().Stroke;
    bool DrawBorder = true;
    switch (Button.Tone)
    {
        case ButtonToneCategory::Primary:
            Fill = Palette().Primary; if (Hit.Hovered) Fill.Alpha *= 0.9f;   // hover:bg-white/90
            Ink = Palette().PrimaryInk; DrawBorder = false; break;
        case ButtonToneCategory::Secondary:
            Fill = Hit.Hovered ? Palette().Raised : Palette().Inset; break;
        case ButtonToneCategory::Ghost:
            Fill = Hit.Hovered ? Palette().Inset : ColorQuad{ 0.0f, 0.0f, 0.0f, 0.0f };
            Ink  = Hit.Hovered ? Palette().Text : Palette().TextDim; DrawBorder = false; break;
        case ButtonToneCategory::Danger:
            Fill = Hit.Hovered ? Palette().Raised : Palette().Inset; Ink = Palette().Danger; break;
        case ButtonToneCategory::Tinted:
            Fill = Button.Tint.Alpha > 0.0f ? Button.Tint : Palette().Accent; if (Hit.Hovered) { Fill.Red *= 0.9f; Fill.Green *= 0.9f; Fill.Blue *= 0.9f; }
            Ink = ColorQuad{ 1.0f, 1.0f, 1.0f, 1.0f }; DrawBorder = false; break;
    }
    if (Fill.Alpha > 0.0f) Surface.FillRectangle(Extent, Faded(Fill, A), R);
    if (DrawBorder) OutlineRounded(Surface, Extent, Faded(Border, A), R);
    TextCentred(Surface, Extent, Faded(Ink, A), Button.Label, Button.FontSize);
    return Hit;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        SWITCH
//------------------------------------------------------------------------------------------------------------------------

ControlHit ControlKit::Switch(PixelSpace& Surface, float X, float Y, bool On, const ControlPointer& Pointer, float Opacity) noexcept
{
    const PlaneExtent Extent = Spanning(X, Y, SwitchWidth, SwitchHeight);
    ControlHit Hit{};
    Hit.Hovered = Over(Extent, Pointer);
    Hit.Pressed = Hit.Hovered && Pointer.Pressed;
    Hit.Clicked = Hit.Hovered && Pointer.Released;

    Surface.FillRectangle(Extent, Faded(On ? Palette().Accent : Palette().Raised, Opacity), SwitchHeight * 0.5f);   // Notch FormToggle: accentColor when on
    if (!On) OutlineRounded(Surface, Extent, Faded(Palette().Stroke, Opacity), SwitchHeight * 0.5f);
    const float KnobX = X + 2.0f + 1.0f + (On ? 20.0f : 0.0f);   // top:2px left:2px (+1 border), translateX(20px)
    FillCircle(Surface, KnobX + 10.0f, Y + 3.0f + 10.0f, 10.0f, Faded(On ? Palette().AccentInk : Palette().SwitchKnobOff, Opacity));   // knob bg-white on the accent
    return Hit;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   CHIP & ROUND ICON
//------------------------------------------------------------------------------------------------------------------------

float ControlKit::ChipWidth(PixelSpace& Surface, const char* Label) noexcept
{
    return Surface.MeasureText(Label, 12.0f).X + 24.0f;   // px-3
}

ControlHit ControlKit::ChipButton(PixelSpace& Surface, const PlaneExtent& Extent, const char* Label, bool Active, const ControlPointer& Pointer, float Opacity) noexcept
{
    ControlHit Hit{};
    Hit.Hovered = Over(Extent, Pointer);
    Hit.Pressed = Hit.Hovered && Pointer.Pressed;
    Hit.Clicked = Hit.Hovered && Pointer.Released;
    if (Active) Surface.FillRectangle(Extent, Faded(Palette().Primary, Opacity), 6.0f);   // rounded-md, bg-white
    const ColorQuad Ink = Active ? Palette().PrimaryInk : (Hit.Hovered ? Palette().Text : Palette().TextDim);
    TextCentred(Surface, Extent, Faded(Ink, Opacity), Label, 12.0f);
    return Hit;
}

ControlHit ControlKit::RoundIconButton(PixelSpace& Surface, float CentreX, float CentreY, ControlCentreIconCategory Icon, const ControlPointer& Pointer, float Opacity) noexcept
{
    const float R = RoundIconDiameter * 0.5f;
    const PlaneExtent Extent = Spanning(CentreX - R, CentreY - R, RoundIconDiameter, RoundIconDiameter);
    ControlHit Hit{};
    Hit.Hovered = Over(Extent, Pointer);
    Hit.Pressed = Hit.Hovered && Pointer.Pressed;
    Hit.Clicked = Hit.Hovered && Pointer.Released;
    if (Hit.Hovered) FillCircle(Surface, CentreX, CentreY, R, Faded(HoverWash(), Opacity));   // hover:bg-white/5
    OutlineCircle(Surface, CentreX, CentreY, R - 0.5f, Faded(Palette().Stroke, Opacity), 1.0f);   // colors.panelBorder
    Glyph(Surface, CentreX - 8.0f, CentreY - 8.0f, 16.0f, Faded(Palette().TextDim, Opacity), Icon, 2.0f);   // colors.textMuted
    return Hit;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      SEGMENTED
//------------------------------------------------------------------------------------------------------------------------

ControlHit ControlKit::Segmented(PixelSpace& Surface, const PlaneExtent& Extent, const char* const* Labels, uint32_t Count, uint32_t Active, const ControlPointer& Pointer, uint32_t& OutIndex, float Opacity) noexcept
{
    ControlHit Hit{};
    const float R = PillRadius(Extent);
    Surface.FillRectangle(Extent, Faded(Palette().Field, Opacity), R);
    OutlineRounded(Surface, Extent, Faded(Palette().Stroke, Opacity), R);

    // padding 4, gap 4, each button px-16 around its label
    float X = Extent.MinimumX + 4.0f;
    const float H = Extent.Height() - 8.0f;
    for (uint32_t I = 0u; I < Count; ++I)
    {
        const float W = Surface.MeasureText(Labels[I], 13.0f).X + 32.0f;
        const PlaneExtent Cell = Spanning(X, Extent.MinimumY + 4.0f, W, H);
        const bool Hover = Over(Cell, Pointer);
        if (Hover) { Hit.Hovered = true; if (Pointer.Pressed) Hit.Pressed = true; if (Pointer.Released) { Hit.Clicked = true; OutIndex = I; } }
        if (I == Active) Surface.FillRectangle(Cell, Faded(Palette().Primary, Opacity), H * 0.5f);
        TextCentred(Surface, Cell, Faded(I == Active ? Palette().PrimaryInk : (Hover ? Palette().Text : Palette().TextDim), Opacity), Labels[I], 13.0f);
        X += W + 4.0f;
    }
    return Hit;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        SLIDER
//------------------------------------------------------------------------------------------------------------------------

ControlHit ControlKit::Slider(PixelSpace& Surface, const PlaneExtent& Extent, float Minimum, float Maximum, float Value, bool Dragging, const ControlPointer& Pointer, float& OutValue, bool Thin, bool HighlightFill, float Opacity) noexcept
{
    ControlHit Hit{};
    const float TrackH = Thin ? SliderThinHeight : SliderHeight;
    const float Thumb  = Thin ? SliderThinThumb  : SliderThumb;
    const float Cy     = Extent.MinimumY + Extent.Height() * 0.5f;
    const PlaneExtent Track = Spanning(Extent.MinimumX, Cy - TrackH * 0.5f, Extent.Width(), TrackH);
    // Hit band: the thumb's full height across the row.
    const PlaneExtent Band = Spanning(Extent.MinimumX, Cy - Thumb * 0.5f - 2.0f, Extent.Width(), Thumb + 4.0f);

    Hit.Hovered  = Over(Band, Pointer);
    Hit.Pressed  = Hit.Hovered && Pointer.Pressed;
    Hit.Dragging = Dragging || Hit.Pressed;

    const float Span = std::max(Maximum - Minimum, 1e-6f);
    float T = std::clamp((Value - Minimum) / Span, 0.0f, 1.0f);
    if (Hit.Dragging && Pointer.Down && Pointer.Enabled)
    {
        // Thumb centre tracks the pointer within [half-thumb, width − half-thumb] (browser range semantics).
        const float Usable = std::max(Extent.Width() - Thumb, 1.0f);
        T = std::clamp((Pointer.X - Extent.MinimumX - Thumb * 0.5f) / Usable, 0.0f, 1.0f);
        OutValue = Minimum + T * Span;
    }

    const float R = TrackH * 0.5f;
    Surface.FillRectangle(Track, Faded(Palette().Raised, Opacity), R);
    const float ThumbCx = Extent.MinimumX + Thumb * 0.5f + T * (Extent.Width() - Thumb);
    if (ThumbCx - Extent.MinimumX > 0.5f)
        Surface.FillRectangle(Spanning(Extent.MinimumX, Track.MinimumY, ThumbCx - Extent.MinimumX, TrackH), Faded(HighlightFill ? Palette().Highlight : Palette().SliderFill, Opacity), R);
    const float ThumbR = Thumb * 0.5f * (Hit.Dragging && Pointer.Down ? 1.1f : 1.0f);   // :active scale(1.1)
    FillCircle(Surface, ThumbCx, Cy, ThumbR, Faded(Palette().SliderThumb, Opacity));
    return Hit;
}

void ControlKit::ValuePill(PixelSpace& Surface, float X, float Y, const char* Number, const char* Unit, float Opacity) noexcept
{
    const float H = ControlKitTokens::ControlHeight;
    const PlaneExtent Whole = Spanning(X, Y, ValuePillWidth, H);
    const PlaneExtent Num   = Spanning(X, Y, ValuePillWidth - ValuePillUnitWidth, H);
    const PlaneExtent Cell  = Spanning(X + ValuePillWidth - ValuePillUnitWidth, Y, ValuePillUnitWidth, H);
    Surface.FillRectangle(Whole, Faded(Palette().Inset, Opacity), H * 0.5f);              // unit cell colour behind
    Surface.FillRectangle(Spanning(X, Y, ValuePillWidth - ValuePillUnitWidth + H * 0.5f, H), Faded(Palette().Field, Opacity), H * 0.5f);   // number cell (left rounded)
    Surface.FillRectangle(Spanning(Cell.MinimumX, Y, 1.0f, H), Faded(Palette().Stroke, Opacity));
    // square the seam: repaint the unit cell's left edge over the number cell's right rounding
    Surface.FillRectangle(Spanning(Cell.MinimumX, Y, H * 0.5f, H), Faded(Palette().Inset, Opacity));
    OutlineRounded(Surface, Whole, Faded(Palette().Stroke, Opacity), H * 0.5f);
    TextCentred(Surface, Num,  Faded(Palette().Text,      Opacity), Number, 15.0f);
    TextCentred(Surface, Cell, Faded(Palette().TextFaint, Opacity), Unit,   12.5f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       DROPDOWN
//------------------------------------------------------------------------------------------------------------------------

ControlHit ControlKit::Dropdown(PixelSpace& Surface, const PlaneExtent& Extent, const char* Current, bool Open, const ControlPointer& Pointer, float Opacity) noexcept
{
    ControlHit Hit{};
    Hit.Hovered = Over(Extent, Pointer);
    Hit.Pressed = Hit.Hovered && Pointer.Pressed;
    Hit.Clicked = Hit.Hovered && Pointer.Released;

    const float H = Extent.Height(), R = H * 0.5f;
    const float CaretW = 44.0f;
    const PlaneExtent Caret = Spanning(Extent.MaximumX - CaretW, Extent.MinimumY, CaretW, H);
    Surface.FillRectangle(Extent, Faded(Palette().Field, Opacity), R);
    Surface.FillRectangle(Spanning(Caret.MinimumX - R, Extent.MinimumY, CaretW + R, H), Faded(Hit.Hovered ? Palette().Raised : Palette().Inset, Opacity), R);
    Surface.FillRectangle(Spanning(Caret.MinimumX - R, Extent.MinimumY, R, H), Faded(Palette().Field, Opacity));   // square the seam
    Surface.FillRectangle(Spanning(Caret.MinimumX, Extent.MinimumY, 1.0f, H), Faded(Palette().Stroke, Opacity));
    OutlineRounded(Surface, Extent, Faded(Palette().Stroke, Opacity), R);
    TextLeading(Surface, Extent, 16.0f, Faded(Palette().Text, Opacity), Current, 13.5f);
    GlyphCentred(Surface, Caret, 16.0f, Faded(Palette().TextDim, Opacity), Open ? ControlCentreIconCategory::ChevronUp : ControlCentreIconCategory::ChevronDown);
    return Hit;
}

PlaneExtent ControlKit::DropdownMenuExtent(const PlaneExtent& ButtonExtent, uint32_t Count) noexcept
{
    return Spanning(ButtonExtent.MinimumX, ButtonExtent.MaximumY + 8.0f, ButtonExtent.Width(), 12.0f + Count * DropdownOptionHeight + (Count > 0u ? (Count - 1u) * 2.0f : 0.0f));
}

ControlHit ControlKit::DropdownMenu(PixelSpace& Surface, const PlaneExtent& ButtonExtent, const char* const* Options, uint32_t Count, uint32_t Selected, const ControlPointer& Pointer, uint32_t& OutIndex, float Opacity) noexcept
{
    ControlHit Hit{};
    const PlaneExtent Menu = DropdownMenuExtent(ButtonExtent, Count);
    Surface.FillRectangle(Menu, Faded(Palette().Field, Opacity), 20.0f);
    OutlineRounded(Surface, Menu, Faded(Palette().StrokeStrong, Opacity), 20.0f);
    float Y = Menu.MinimumY + 6.0f;
    for (uint32_t I = 0u; I < Count; ++I)
    {
        const PlaneExtent Row = Spanning(Menu.MinimumX + 6.0f, Y, Menu.Width() - 12.0f, DropdownOptionHeight);
        const bool Hover = Over(Row, Pointer);
        if (Hover) { Hit.Hovered = true; if (Pointer.Pressed) Hit.Pressed = true; if (Pointer.Released) { Hit.Clicked = true; OutIndex = I; } }
        if (I == Selected)  Surface.FillRectangle(Row, Faded(Palette().Raised, Opacity), DropdownOptionHeight * 0.5f);
        else if (Hover)     Surface.FillRectangle(Row, Faded(Palette().Inset, Opacity), DropdownOptionHeight * 0.5f);
        TextLeading(Surface, Row, 14.0f, Faded(I == Selected || Hover ? Palette().Text : Palette().TextDim, Opacity), Options[I], 13.0f);
        Y += DropdownOptionHeight + 2.0f;
    }
    Hit.Hovered = Hit.Hovered || Over(Menu, Pointer);
    return Hit;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   SECTIONS & ROWS
//------------------------------------------------------------------------------------------------------------------------

PlaneExtent ControlKit::SectionCard(PixelSpace& Surface, const PlaneExtent& Extent, float Radius, float Opacity) noexcept
{
    Surface.FillRectangle(Extent, Faded(Palette().Inset, Opacity), Radius);            // colors.inputBg
    OutlineRounded(Surface, Extent, Faded(Palette().Stroke, Opacity), Radius);         // colors.panelBorder
    return PlaneExtent{ Extent.MinimumX + SectionPadding, Extent.MinimumY + SectionPadding, Extent.MaximumX - SectionPadding, Extent.MaximumY - SectionPadding };
}

float ControlKit::SectionHeading(PixelSpace& Surface, float X, float Y, float, const char* Title, const char* Description, ColorQuad TitleInk, ColorQuad MutedInk, float Opacity) noexcept
{
    const PlanePoint T = Surface.MeasureText(Title, 14.0f);
    Surface.Text(X, Y + (21.0f - T.Y) * 0.5f, Faded(TitleInk, Opacity), Title, 14.0f);          // text-sm leading 20 + mb-1
    float Consumed = 20.0f + 4.0f;
    if (Description && *Description)
    {
        const PlanePoint D = Surface.MeasureText(Description, 12.0f);
        Surface.Text(X, Y + Consumed + (16.0f - D.Y) * 0.5f, Faded(MutedInk, Opacity), Description, 12.0f);   // text-xs leading 16
        Consumed += 16.0f;
    }
    return Consumed;
}

ControlHit ControlKit::Swatch(PixelSpace& Surface, float Cx, float Cy, ColorQuad Colour, bool Selected, bool RingStyle, const ControlPointer& Pointer, float Opacity) noexcept
{
    const float R = SwatchDiameter * 0.5f;
    const PlaneExtent Extent = Spanning(Cx - R, Cy - R, SwatchDiameter, SwatchDiameter);
    ControlHit Hit{};
    Hit.Hovered = Over(Extent, Pointer);
    Hit.Pressed = Hit.Hovered && Pointer.Pressed;
    Hit.Clicked = Hit.Hovered && Pointer.Released;
    const float Scale = Hit.Hovered && RingStyle ? 1.1f : 1.0f;   // Notch accent: hover:scale-110
    FillCircle(Surface, Cx, Cy, R * Scale, Faded(Colour, Opacity));
    if (Selected)
    {
        if (RingStyle)
        {
            OutlineCircle(Surface, Cx, Cy, R + 4.0f, Faded(Colour, Opacity), 2.0f);      // inset-[-4px] border-2 in the colour
            FillCircle(Surface, Cx, Cy, 4.0f, Faded(Palette().Field, Opacity));   // 2×2 → w-2 h-2 dot in the canvas colour (bg-black)
        }
        else
            OutlineCircle(Surface, Cx, Cy, R - 1.0f, Faded(Palette().Primary, Opacity), 2.0f);   // border-2 border-white
    }
    return Hit;
}

PlaneExtent ControlKit::ControlRow(PixelSpace& Surface, float X, float Y, float Width, const char* Label, ColorQuad LabelInk, float Opacity) noexcept
{
    const PlaneExtent Row = Spanning(X, Y, Width, ControlKitTokens::ControlHeight);
    TextLeading(Surface, Spanning(X, Y, ControlKitTokens::LabelWidth, ControlKitTokens::ControlHeight), 0.0f, Faded(LabelInk, Opacity), Label, 13.5f);
    return PlaneExtent{ X + ControlKitTokens::LabelWidth + ControlKitTokens::RowGap, Y, Row.MaximumX, Row.MaximumY };
}

} // namespace Frontier
