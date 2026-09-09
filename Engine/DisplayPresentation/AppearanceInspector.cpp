//========================================================================================================================
// 🧩 AppearanceInspector — see AppearanceInspector.h
//========================================================================================================================
#include "AppearanceInspector.h"
#include "TypefaceRegistry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier {

namespace {

constexpr ColorQuad Hex(uint32_t Rgb, float Alpha = 1.0f) noexcept
{
    return ColorQuad{ ((Rgb >> 16) & 0xFFu) / 255.0f, ((Rgb >> 8) & 0xFFu) / 255.0f, (Rgb & 0xFFu) / 255.0f, Alpha };
}

// Notch dark theme text colours (colors.text / colors.textMuted) used on section headings.
// Notch colour classes resolved against the applied theme each frame (ControlKit::Palette()).
inline ColorQuad Ink90() noexcept { return ControlKit::Palette().Text; }      // colors.text
inline ColorQuad Ink50() noexcept { return ControlKit::Palette().TextDim; }   // colors.textMuted
inline ColorQuad Ink10() noexcept { return ControlKit::Palette().LightSurface ? ColorQuad{ 0.0f, 0.0f, 0.0f, 0.10f } : ColorQuad{ 1.0f, 1.0f, 1.0f, 0.10f }; }   // ring-white/10, colors.activeBg
inline ColorQuad Ink20() noexcept { return ControlKit::Palette().LightSurface ? ColorQuad{ 0.0f, 0.0f, 0.0f, 0.20f } : ColorQuad{ 1.0f, 1.0f, 1.0f, 0.20f }; }   // hover:ring-white/20

// Theme tile miniature palettes — Notch ThemeTab.tsx (bg / sidebar / panel / lines).
struct TilePalette { const char* Name; ColorQuad Background, Sidebar, Panel, Lines; bool OutlinePanel; };
constexpr TilePalette Tiles[7] =
{
    { "OLED",    Hex(0x000000), Hex(0x0A0A0A), Hex(0x141414), Hex(0xFFFFFF), false },
    { "Dark",    Hex(0x18181A), Hex(0x222224), Hex(0x2C2C2E), Hex(0xFFFFFF), false },
    { "Light",   Hex(0xE5E5E5), Hex(0xF4F4F5), Hex(0xFFFFFF), Hex(0x000000), true  },
    { "Sepia",   Hex(0xDCA85B), Hex(0xE6C697), Hex(0xF1DAB0), Hex(0x825619), false },
    { "Dracula", Hex(0x282A36), Hex(0x44475A), Hex(0x6272A4), Hex(0xF8F8F2), false },
    { "Nord",    Hex(0x2E3440), Hex(0x3B4252), Hex(0x4C566A), Hex(0xECEFF4), false },
    { "GitHub",  Hex(0x0D1117), Hex(0x161B22), Hex(0x21262D), Hex(0xC9D1D9), false },
};
constexpr ThemeCategory TileThemes[7] =
{
    ThemeCategory::Oled, ThemeCategory::Dark, ThemeCategory::Light, ThemeCategory::Sepia,
    ThemeCategory::Dracula, ThemeCategory::Nord, ThemeCategory::GitHub
};

constexpr uint32_t AccentHex[10] = { 0xFFFFFF, 0xF97316, 0xF59E0B, 0x84CC16, 0x10B981, 0x06B6D4, 0x3B82F6, 0x8B5CF6, 0xD946EF, 0xF43F5E };

// Semantic swatch rows — Notch ThemeTab.tsx; Caution row added per direction (flagged).
constexpr uint32_t SemanticHex[4][4] =
{
    { 0xF59E0B, 0xEAB308, 0xFBBF24, 0xF97316 },   // Warning
    { 0x10B981, 0x22C55E, 0x34D399, 0x059669 },   // Success
    { 0x3B82F6, 0x0EA5E9, 0x60A5FA, 0x2563EB },   // Info
    { 0xEAB308, 0xFACC15, 0xFDE047, 0xCA8A04 },   // Caution (yellow ladder)
};

constexpr const char* ResolutionNames[] = { "Native", "2560 x 1440", "1920 x 1080", "1280 x 720" };
constexpr const char* VsyncNames[]      = { "Off", "On", "Adaptive" };
constexpr const char* FrameCapNames[]   = { "Unlimited", "60 fps", "120 fps", "144 fps" };
constexpr const char* CornerNames[]     = { "TL", "TR", "BL", "BR" };
constexpr const char* SampleNames[]     = { "1x", "2x", "4x", "8x" };

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

AppearanceInspector::AppearanceInspector() noexcept
    : Applied{}, Draft{}, Revision(0u), OpenDropdown(-1), DraggingSlider(-1), Dropdowns{ DropdownRecord{ {}, nullptr, 0u, 0u, -1 }, DropdownRecord{ {}, nullptr, 0u, 0u, -1 }, DropdownRecord{ {}, nullptr, 0u, 0u, -1 }, DropdownRecord{ {}, nullptr, 0u, 0u, -1 } }, DropdownCount(0u)
    , TileExtents{}, AccentExtents{}, RadiusSliderExtent{}, ScaleSliderExtent{}, DropdownExtents{}, FullscreenSwitchExtent{}, VsyncExtent{}
    , StripScroll(0.0f), StripScrollTarget(0.0f), StripContentWidth(0.0f), StripViewWidth(0.0f)
    , FontCardExtents{}, StripBackExtent{}, StripForwardExtent{}, RoleSliderExtents{}, ChipExtents{}, FontAaSwitchExtent{}, LigatureSwitchExtent{}
{
}

const char* AppearanceInspector::QueryThemeName(ThemeCategory Theme) noexcept
{
    for (uint32_t I = 0u; I < 7u; ++I) if (TileThemes[I] == Theme) return Tiles[I].Name;
    return "Dim";
}

ColorQuad AppearanceInspector::QueryAccentColour(AccentCategory Accent) noexcept
{
    return Hex(AccentHex[std::min<uint32_t>(static_cast<uint32_t>(Accent), 9u)]);
}

ColorQuad AppearanceInspector::QuerySemanticColour(uint32_t Row, uint32_t Swatch) noexcept
{
    return Hex(SemanticHex[std::min(Row, 3u)][std::min(Swatch, 3u)]);
}

AppearanceDifference AppearanceInspector::QueryDifference() const noexcept
{
    AppearanceDifference D{};
    auto Note = [&](bool Changed, const char* Name) { if (Changed) { if (D.Count < 8u) D.Names[D.Count] = Name; ++D.Count; } };
    Note(Draft.Resolution       != Applied.Resolution,       "Resolution");
    Note(Draft.InterfaceScale   != Applied.InterfaceScale,   "UI scale");
    Note(Draft.MatchQualityTier != Applied.MatchQualityTier, "Match quality");
    Note(Draft.VerticalSync     != Applied.VerticalSync,     "V-Sync");
    Note(Draft.FrameCap         != Applied.FrameCap,         "Frame cap");
    Note(Draft.Fullscreen       != Applied.Fullscreen,       "Fullscreen");
    Note(Draft.AntiAliasing     != Applied.AntiAliasing,     "Anti-aliasing");
    Note(Draft.SafeAreaPadding  != Applied.SafeAreaPadding,  "Safe area");
    Note(Draft.FrameRateOverlay != Applied.FrameRateOverlay, "FPS overlay");
    Note(Draft.OverlayCorner    != Applied.OverlayCorner,    "Overlay corner");
    Note(Draft.Theme            != Applied.Theme,            "Theme");
    Note(Draft.CornerRadius     != Applied.CornerRadius,     "Corner radius");
    Note(Draft.Accent           != Applied.Accent,           "Accent");
    Note(Draft.WarningSwatch    != Applied.WarningSwatch,    "Warning colour");
    Note(Draft.SuccessSwatch    != Applied.SuccessSwatch,    "Success colour");
    Note(Draft.InfoSwatch       != Applied.InfoSwatch,       "Info colour");
    Note(Draft.CautionSwatch    != Applied.CautionSwatch,    "Caution colour");
    Note(Draft.FontFamily       != Applied.FontFamily,       "Font family");
    bool Sizes = false, Weights = false;
    for (uint32_t R = 0u; R < AppearanceSettings::TypeRoleCount; ++R) { Sizes |= Draft.RoleSize[R] != Applied.RoleSize[R]; Weights |= Draft.RoleWeight[R] != Applied.RoleWeight[R]; }
    Note(Sizes,   "Type scale");
    Note(Weights, "Font weights");
    Note(Draft.FontAntialiasing != Applied.FontAntialiasing, "Font antialiasing");
    Note(Draft.Ligatures        != Applied.Ligatures,        "Ligatures");
    return D;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     DROPDOWNS
//------------------------------------------------------------------------------------------------------------------------

void AppearanceInspector::RecordDropdown(PixelSpace& Surface, const PlaneExtent& Extent, int Ordinal, const char* const* Options, uint32_t Count, uint32_t& Value, const ControlPointer& Pointer, float Opacity) noexcept
{
    // While a menu is open every other control is inert (the floating layer owns the pointer).
    ControlPointer Local = Pointer;
    if (OpenDropdown >= 0) Local.Enabled = false;
    // A pick made in the floating layer (drawn after this tab last frame) lands here, one frame later.
    if (static_cast<uint32_t>(Ordinal) < 4u && Dropdowns[Ordinal].Pick >= 0) { Value = static_cast<uint32_t>(Dropdowns[Ordinal].Pick); Dropdowns[Ordinal].Pick = -1; }
    const ControlHit Hit = ControlKit::Dropdown(Surface, Extent, Options[std::min(Value, Count - 1u)], OpenDropdown == Ordinal, Local, Opacity);
    if (Hit.Clicked) OpenDropdown = Ordinal;
    if (static_cast<uint32_t>(Ordinal) < 4u) { Dropdowns[Ordinal] = DropdownRecord{ Extent, Options, Count, Value, -1 }; DropdownExtents[Ordinal] = Extent; }
    DropdownCount = std::max(DropdownCount, static_cast<uint32_t>(Ordinal + 1));
}

void AppearanceInspector::ConstructFloatingLayout(PixelSpace& Surface, const ControlPointer& Pointer, float Opacity) noexcept
{
    if (OpenDropdown < 0 || static_cast<uint32_t>(OpenDropdown) >= DropdownCount) return;
    DropdownRecord& R = Dropdowns[OpenDropdown];
    uint32_t Chosen = R.Value;
    const ControlHit Hit = ControlKit::DropdownMenu(Surface, R.Button, R.Options, R.Count, R.Value, Pointer, Chosen, Opacity);
    if (Hit.Clicked) { R.Pick = static_cast<int>(Chosen); R.Value = Chosen; OpenDropdown = -1; return; }
    // Release outside the menu and its button closes it.
    if (Pointer.Released && !Hit.Hovered && !R.Button.Encloses(Pointer.X, Pointer.Y)) OpenDropdown = -1;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     DISPLAY TAB
//------------------------------------------------------------------------------------------------------------------------

float AppearanceInspector::ConstructDisplayTabLayout(PixelSpace& Surface, const PlaneExtent& Body, float ScrollY, const ControlPointer& Pointer, float Opacity) noexcept
{
    DropdownCount = 0u;
    const float Radius = std::clamp(Draft.CornerRadius, 0.0f, SectionRadiusMax);
    const float X = Body.MinimumX, W = Body.Width();
    float Y = Body.MinimumY - ScrollY;
    const float RowH = ControlKitTokens::ControlHeight, RowGap = 16.0f;
    ControlPointer Local = Pointer;
    if (OpenDropdown >= 0) Local.Enabled = false;   // menus own the pointer

    // A section = card with heading + N rows. Height = pad + heading + rows + pad.
    auto Section = [&](const char* Title, const char* Description, uint32_t Rows) -> PlaneExtent
    {
        const float HeadingH = 24.0f + (Description && *Description ? 16.0f : 0.0f) + 24.0f;   // mb-6 after heading
        const float H = ControlKit::SectionPadding * 2.0f + HeadingH + Rows * RowH + (Rows > 0u ? (Rows - 1u) * RowGap : 0.0f);
        const PlaneExtent Card = Spanning(X, Y, W, H);
        const PlaneExtent Content = ControlKit::SectionCard(Surface, Card, Radius, Opacity);
        ControlKit::SectionHeading(Surface, Content.MinimumX, Content.MinimumY, Content.Width(), Title, Description, Ink90(), Ink50(), Opacity);
        Y += H + SectionGap;
        return PlaneExtent{ Content.MinimumX, Content.MinimumY + HeadingH, Content.MaximumX, Content.MaximumY };
    };

    // ① Resolution & Scaling
    {
        PlaneExtent C = Section("Resolution & Scaling", "Render target size and interface scale", 3u);
        float RowY = C.MinimumY;
        PlaneExtent Ctl = ControlKit::ControlRow(Surface, C.MinimumX, RowY, C.Width(), "Resolution", ControlKit::Palette().TextDim, Opacity);
        uint32_t Res = static_cast<uint32_t>(Draft.Resolution);
        RecordDropdown(Surface, Spanning(Ctl.MinimumX, RowY, std::min(Ctl.Width(), 260.0f), RowH), 0, ResolutionNames, 4u, Res, Pointer, Opacity);
        Draft.Resolution = static_cast<RenderResolutionCategory>(Res);
        RowY += RowH + RowGap;

        Ctl = ControlKit::ControlRow(Surface, C.MinimumX, RowY, C.Width(), "UI Scale", ControlKit::Palette().TextDim, Opacity);
        char Num[8]; std::snprintf(Num, sizeof(Num), "%d", static_cast<int>(std::lround(Draft.InterfaceScale)));
        ControlKit::ValuePill(Surface, Ctl.MinimumX, RowY, Num, "%", Opacity);
        ScaleSliderExtent = Spanning(Ctl.MinimumX + ControlKit::ValuePillWidth + ControlKitTokens::RowGap, RowY, Ctl.MaximumX - (Ctl.MinimumX + ControlKit::ValuePillWidth + ControlKitTokens::RowGap), RowH);
        {
            float V = Draft.InterfaceScale;
            const ControlHit Hit = ControlKit::Slider(Surface, ScaleSliderExtent, 50.0f, 200.0f, V, DraggingSlider == 0, Local, V, false, false, Opacity);
            if (Hit.Pressed) DraggingSlider = 0;
            if (DraggingSlider == 0) Draft.InterfaceScale = std::round(V);
        }
        RowY += RowH + RowGap;

        Ctl = ControlKit::ControlRow(Surface, C.MinimumX, RowY, C.Width(), "Match Quality", ControlKit::Palette().TextDim, Opacity);
        if (ControlKit::Switch(Surface, Ctl.MinimumX, RowY + (RowH - ControlKit::SwitchHeight) * 0.5f, Draft.MatchQualityTier, Local, Opacity).Clicked) Draft.MatchQualityTier = !Draft.MatchQualityTier;
        ControlKit::TextLeading(Surface, Spanning(Ctl.MinimumX + ControlKit::SwitchWidth + 12.0f, RowY, 300.0f, RowH), 0.0f, ControlKit::Faded(ControlKit::Palette().TextFaint, Opacity), "Render scale follows the Quality tier", 12.0f);
    }

    // ② Presentation
    {
        PlaneExtent C = Section("Presentation", "Swapchain behaviour", 3u);
        float RowY = C.MinimumY;
        PlaneExtent Ctl = ControlKit::ControlRow(Surface, C.MinimumX, RowY, C.Width(), "V-Sync", ControlKit::Palette().TextDim, Opacity);
        {
            float SegW = 8.0f;
            for (const char* N : VsyncNames) SegW += Surface.MeasureText(N, 13.0f).X + 32.0f + 4.0f;
            SegW -= 4.0f;
            VsyncExtent = Spanning(Ctl.MinimumX, RowY, SegW, RowH);
            uint32_t Pick = static_cast<uint32_t>(Draft.VerticalSync);
            ControlKit::Segmented(Surface, VsyncExtent, VsyncNames, 3u, Pick, Local, Pick, Opacity);
            Draft.VerticalSync = static_cast<VerticalSyncCategory>(Pick);
        }
        RowY += RowH + RowGap;

        Ctl = ControlKit::ControlRow(Surface, C.MinimumX, RowY, C.Width(), "Frame Cap", ControlKit::Palette().TextDim, Opacity);
        uint32_t Cap = static_cast<uint32_t>(Draft.FrameCap);
        RecordDropdown(Surface, Spanning(Ctl.MinimumX, RowY, std::min(Ctl.Width(), 260.0f), RowH), 1, FrameCapNames, 4u, Cap, Pointer, Opacity);
        Draft.FrameCap = static_cast<FrameCapCategory>(Cap);
        RowY += RowH + RowGap;

        Ctl = ControlKit::ControlRow(Surface, C.MinimumX, RowY, C.Width(), "Fullscreen", ControlKit::Palette().TextDim, Opacity);
        FullscreenSwitchExtent = Spanning(Ctl.MinimumX, RowY + (RowH - ControlKit::SwitchHeight) * 0.5f, ControlKit::SwitchWidth, ControlKit::SwitchHeight);
        if (ControlKit::Switch(Surface, FullscreenSwitchExtent.MinimumX, FullscreenSwitchExtent.MinimumY, Draft.Fullscreen, Local, Opacity).Clicked) Draft.Fullscreen = !Draft.Fullscreen;
    }

    // ③ Anti-Aliasing — Notch DisplayTab pill row (1x 2x 4x 8x), kept verbatim.
    {
        const float HeadH = 16.0f + 24.0f;   // uppercase text-xs heading + gap-6
        const float H = ControlKit::SectionPadding * 2.0f + HeadH + 34.0f;
        const PlaneExtent Content = ControlKit::SectionCard(Surface, Spanning(X, Y, W, H), Radius, Opacity);
        Surface.Text(Content.MinimumX + 4.0f, Content.MinimumY + 1.0f, ControlKit::Faded(Ink50(), Opacity), "ANTI-ALIASING", 12.0f);
        float PX = Content.MinimumX;
        for (uint32_t I = 0u; I < 4u; ++I)
        {
            const bool Active = static_cast<uint32_t>(Draft.AntiAliasing) == I;
            const float TextW = Surface.MeasureText(SampleNames[I], 12.0f).X;
            const float PW = 40.0f + TextW + (Active ? 6.0f + 8.0f : 0.0f);   // px-5, dot w-1.5 + mr-2
            const PlaneExtent Pill = Spanning(PX, Content.MinimumY + HeadH, PW, 34.0f);
            const bool Hover = ControlKit::Over(Pill, Local);
            if (Hover && !Active) Surface.FillRectangle(Pill, ControlKit::Faded(Ink10(), Opacity), 17.0f);   // hover:activeBg white/10
            ControlKit::OutlineRounded(Surface, Pill, ControlKit::Faded(Active ? ControlKit::Palette().Primary : ControlKit::Palette().Stroke, Opacity), 17.0f);
            float TX = Pill.MinimumX + 20.0f;
            if (Active) { Surface.FillRectangle(Spanning(TX, Pill.MinimumY + 14.0f, 6.0f, 6.0f), ControlKit::Faded(ControlKit::Palette().Primary, Opacity), 3.0f); TX += 6.0f + 8.0f; }
            const PlanePoint M = Surface.MeasureText(SampleNames[I], 12.0f);
            Surface.Text(TX, Pill.MinimumY + (34.0f - M.Y) * 0.5f, ControlKit::Faded(Active ? ControlKit::Palette().Primary : Ink50(), Opacity), SampleNames[I], 12.0f);
            if (Hover && Local.Released) Draft.AntiAliasing = static_cast<SampleCountCategory>(I);
            PX += PW + 8.0f;
        }
        Y += H + SectionGap;
    }

    // ④ Safe Area Padding — Notch DisplayTab slider 0–128, kept.
    {
        const float HeadH = 16.0f + 24.0f;
        const float H = ControlKit::SectionPadding * 2.0f + HeadH + RowH;
        const PlaneExtent Content = ControlKit::SectionCard(Surface, Spanning(X, Y, W, H), Radius, Opacity);
        Surface.Text(Content.MinimumX + 4.0f, Content.MinimumY + 1.0f, ControlKit::Faded(Ink50(), Opacity), "SAFE AREA PADDING", 12.0f);
        char Num[8]; std::snprintf(Num, sizeof(Num), "%d", static_cast<int>(std::lround(Draft.SafeAreaPadding)));
        ControlKit::ValuePill(Surface, Content.MinimumX, Content.MinimumY + HeadH, Num, "px", Opacity);
        const PlaneExtent Track = Spanning(Content.MinimumX + ControlKit::ValuePillWidth + ControlKitTokens::RowGap, Content.MinimumY + HeadH, Content.Width() - ControlKit::ValuePillWidth - ControlKitTokens::RowGap, RowH);
        float V = Draft.SafeAreaPadding;
        const ControlHit Hit = ControlKit::Slider(Surface, Track, 0.0f, 128.0f, V, DraggingSlider == 1, Local, V, false, false, Opacity);
        if (Hit.Pressed) DraggingSlider = 1;
        if (DraggingSlider == 1) Draft.SafeAreaPadding = std::round(V);
        Y += H + SectionGap;
    }

    // ⑤ Overlay
    {
        PlaneExtent C = Section("Overlay", "Frame-rate overlay placement", 2u);
        float RowY = C.MinimumY;
        PlaneExtent Ctl = ControlKit::ControlRow(Surface, C.MinimumX, RowY, C.Width(), "FPS Overlay", ControlKit::Palette().TextDim, Opacity);
        if (ControlKit::Switch(Surface, Ctl.MinimumX, RowY + (RowH - ControlKit::SwitchHeight) * 0.5f, Draft.FrameRateOverlay, Local, Opacity).Clicked) Draft.FrameRateOverlay = !Draft.FrameRateOverlay;
        RowY += RowH + RowGap;
        Ctl = ControlKit::ControlRow(Surface, C.MinimumX, RowY, C.Width(), "Corner", ControlKit::Palette().TextDim, Opacity);
        float SegW = 8.0f;
        for (const char* N : CornerNames) SegW += Surface.MeasureText(N, 13.0f).X + 32.0f + 4.0f;
        SegW -= 4.0f;
        uint32_t Pick = static_cast<uint32_t>(Draft.OverlayCorner);
        ControlKit::Segmented(Surface, Spanning(Ctl.MinimumX, RowY, SegW, RowH), CornerNames, 4u, Pick, Local, Pick, Opacity);
        Draft.OverlayCorner = static_cast<OverlayCornerCategory>(Pick);
    }

    if (Pointer.Released) DraggingSlider = -1;
    return (Y + ScrollY) - Body.MinimumY - SectionGap;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THEME TAB
//------------------------------------------------------------------------------------------------------------------------

void AppearanceInspector::RecordThemeTile(PixelSpace& Surface, const PlaneExtent& Extent, ThemeCategory Theme, bool Active, float Radius, const ControlPointer& Pointer, float Opacity) noexcept
{
    uint32_t Index = 0u; for (uint32_t I = 0u; I < 7u; ++I) if (TileThemes[I] == Theme) Index = I;
    const TilePalette& T = Tiles[Index];
    const bool Hover = ControlKit::Over(Extent, Pointer);

    // aspect-[4/3] rounded-[1.25rem] (20 px) bg; ring-1 white/10 idle (hover white/20)
    Surface.FillRectangle(Extent, ControlKit::Faded(T.Background, Opacity), 20.0f);
    if (!Active) ControlKit::OutlineRounded(Surface, Extent, ControlKit::Faded(Hover ? Ink20() : Ink10(), Opacity), 20.0f);

    // Inner mock: sidebar from (24, 24) to the tile's bottom-right; 30 % sidebar column + panel.
    const PlaneExtent Inner = PlaneExtent{ Extent.MinimumX + 24.0f, Extent.MinimumY + 24.0f, Extent.MaximumX, Extent.MaximumY };
    Surface.FillRectangle(Inner, ControlKit::Faded(T.Sidebar, Opacity), 0.0f);
    const float SideW = Inner.Width() * 0.30f;
    const PlaneExtent Side = PlaneExtent{ Inner.MinimumX, Inner.MinimumY, Inner.MinimumX + SideW, Inner.MaximumY };
    const PlaneExtent Pane = PlaneExtent{ Side.MaximumX, Inner.MinimumY, Inner.MaximumX, Inner.MaximumY };
    Surface.FillRectangle(Pane, ControlKit::Faded(T.Panel, Opacity), 0.0f);
    if (T.OutlinePanel) ControlKit::OutlineRounded(Surface, Pane, ControlKit::Faded(ColorQuad{ 0.0f, 0.0f, 0.0f, 0.05f }, Opacity), 0.0f);
    Surface.FillRectangle(Spanning(Side.MaximumX, Side.MinimumY, 1.0f, Side.Height()), ControlKit::Faded(ColorQuad{ 0.0f, 0.0f, 0.0f, 0.05f }, Opacity));

    const float LineR = Radius * 0.25f;
    auto Line = [&](float LX, float LY, float LW, float LH, float Alpha, float R) { Surface.FillRectangle(Spanning(LX, LY, LW, LH), ControlKit::Faded(ColorQuad{ T.Lines.Red, T.Lines.Green, T.Lines.Blue, Alpha }, Opacity), R); };
    // Sidebar p-3: three 6 px dots, then 3 lines (full, 2/3, 1/2), then avatar row at the bottom.
    float SX = Side.MinimumX + 12.0f, SY = Side.MinimumY + 12.0f;
    for (int I = 0; I < 3; ++I) Line(SX + I * 10.0f, SY, 6.0f, 6.0f, 0.4f, 3.0f);
    SY += 6.0f + 8.0f;
    const float SW = Side.Width() - 24.0f;
    Line(SX, SY, SW, 6.0f, 0.2f, LineR);              SY += 12.0f;
    Line(SX, SY, SW * 2.0f / 3.0f, 6.0f, 0.2f, LineR);  SY += 12.0f;
    Line(SX, SY, SW * 0.5f, 6.0f, 0.2f, LineR);
    const float AY = Side.MaximumY - 12.0f - 10.0f;
    if (AY > SY + 8.0f) { Line(SX, AY, 10.0f, 10.0f, 0.4f, 5.0f); Line(SX + 16.0f, AY + 2.0f, 16.0f, 6.0f, 0.2f, LineR); }
    // Panel p-4: 1/4 line, 1/3 line, three squares, 1/5 line at the bottom.
    float PX = Pane.MinimumX + 16.0f, PY = Pane.MinimumY + 16.0f;
    const float PW = Pane.Width() - 32.0f;
    Line(PX, PY, PW * 0.25f, 6.0f, 0.3f, LineR); PY += 6.0f + 4.0f + 6.0f;
    Line(PX, PY, PW / 3.0f, 6.0f, 0.2f, LineR);  PY += 6.0f + 6.0f + 4.0f;
    const float Sq = std::max((PW - 16.0f) / 3.0f, 4.0f);
    for (int I = 0; I < 3; ++I) Line(PX + I * (Sq + 8.0f), PY, Sq, Sq, 0.1f, Radius * 0.5f);
    const float BY = Pane.MaximumY - 16.0f - 6.0f;
    if (BY > PY + Sq + 4.0f) Line(PX, BY, PW * 0.2f, 6.0f, 0.2f, LineR);
}

void AppearanceInspector::RecordSemanticRow(PixelSpace& Surface, float X, float Y, float Width, uint32_t Row, const char* Label, uint32_t& Swatch, ControlCentreIconCategory Icon, float Radius, bool Last, const ControlPointer& Pointer, float Opacity) noexcept
{
    // grid-cols-[300px_1fr] gap-8 items-center; left: label + 4 swatches; right: preview card min-h 100.
    const float LeftW = 300.0f;
    const float PreviewH = 100.0f;
    const PlanePoint M = Surface.MeasureText(Label, 14.0f);
    const float LeftH = 20.0f + 24.0f + ControlKit::SwatchDiameter;
    const float RowH = std::max(PreviewH, LeftH);
    const float LeftY = Y + (RowH - LeftH) * 0.5f;
    Surface.Text(X, LeftY + (20.0f - M.Y) * 0.5f, ControlKit::Faded(Ink90(), Opacity), Label, 14.0f);
    for (uint32_t I = 0u; I < 4u; ++I)
    {
        const float Cx = X + 16.0f + I * (ControlKit::SwatchDiameter + 12.0f), Cy = LeftY + 20.0f + 24.0f + 16.0f;
        if (ControlKit::Swatch(Surface, Cx, Cy, QuerySemanticColour(Row, I), Swatch == I, false, Pointer, Opacity).Clicked) Swatch = I;
    }
    const PlaneExtent Preview = Spanning(X + LeftW + 32.0f, Y, Width - LeftW - 32.0f, PreviewH);
    Surface.FillRectangle(Preview, ControlKit::Faded(Ink10(), Opacity), Radius * 0.7f);        // colors.activeBg
    ControlKit::OutlineRounded(Surface, Preview, ControlKit::Faded(ControlKit::Palette().Stroke, Opacity), Radius * 0.7f);
    const ColorQuad Tone = QuerySemanticColour(Row, Swatch);
    ControlKit::Glyph(Surface, Preview.MinimumX + 24.0f, Preview.MinimumY + (PreviewH - 20.0f) * 0.5f, 20.0f, ControlKit::Faded(Tone, Opacity), Icon);
    const char* Message = Row == 0u ? "Warning Message" : Row == 1u ? "Success Message" : Row == 2u ? "Info Message" : "Caution Message";
    const PlanePoint MM = Surface.MeasureText(Message, 18.0f);
    Surface.Text(Preview.MinimumX + 24.0f + 20.0f + 8.0f, Preview.MinimumY + (PreviewH - MM.Y) * 0.5f, ControlKit::Faded(Tone, Opacity), Message, 18.0f);
    if (!Last) ControlKit::Divider(Surface, X, Y + RowH + 32.0f, Width, ControlKit::Faded(ColorQuad{ 1.0f, 1.0f, 1.0f, 0.06f }, Opacity));
}

float AppearanceInspector::ConstructThemeTabLayout(PixelSpace& Surface, const PlaneExtent& Body, float ScrollY, const ControlPointer& Pointer, float Opacity) noexcept
{
    DropdownCount = 0u;
    const float Radius = std::clamp(Draft.CornerRadius, 0.0f, SectionRadiusMax);
    const float X = Body.MinimumX, W = Body.Width();
    float Y = Body.MinimumY - ScrollY;

    // ① Color Scheme — grid-cols-4 gap-6 gap-y-8, tiles aspect 4:3 + name (gap-3).
    {
        const float InnerW = W - ControlKit::SectionPadding * 2.0f;
        const float TileW = (InnerW - 3.0f * 24.0f) / 4.0f, TileH = TileW * 0.75f;
        const float CellH = TileH + 12.0f + 16.0f;
        const float GridH = CellH * 2.0f + 32.0f;
        const float HeadH = 20.0f + 4.0f + 16.0f + 24.0f;   // title mb-1 + desc + mb-6
        const float H = ControlKit::SectionPadding * 2.0f + HeadH + GridH;
        const PlaneExtent Content = ControlKit::SectionCard(Surface, Spanning(X, Y, W, H), Radius, Opacity);
        ControlKit::SectionHeading(Surface, Content.MinimumX, Content.MinimumY, Content.Width(), "Color Scheme", "Choose the overall look of the interface", Ink90(), Ink50(), Opacity);
        for (uint32_t I = 0u; I < 7u; ++I)
        {
            const uint32_t Col = I % 4u, Row = I / 4u;
            const PlaneExtent Tile = Spanning(Content.MinimumX + Col * (TileW + 24.0f), Content.MinimumY + HeadH + Row * (CellH + 32.0f), TileW, TileH);
            TileExtents[I] = Tile;
            const bool Active = Draft.Theme == TileThemes[I];
            RecordThemeTile(Surface, Tile, TileThemes[I], Active, Radius, Pointer, Opacity);
            if (Active) ControlKit::OutlineRounded(Surface, PlaneExtent{ Tile.MinimumX - 2.0f, Tile.MinimumY - 2.0f, Tile.MaximumX + 2.0f, Tile.MaximumY + 2.0f }, ControlKit::Faded(ControlKit::Palette().Accent, Opacity), 22.0f, 2.0f);
            const PlanePoint NM = Surface.MeasureText(Tiles[I].Name, 12.0f);
            Surface.Text(Tile.MinimumX + (TileW - NM.X) * 0.5f, Tile.MaximumY + 12.0f + (16.0f - NM.Y) * 0.5f, ControlKit::Faded(Active ? Ink90() : Ink50(), Opacity), Tiles[I].Name, 12.0f);
            if (ControlKit::Over(Tile, Pointer) && Pointer.Released) Draft.Theme = TileThemes[I];
        }
        Y += H + SectionGap;
    }

    // ② Corner Radius — heading left, "Npx" right, slider below (Notch <Slider>: thin track).
    {
        const float HeadH = 20.0f + 4.0f + 16.0f + 24.0f;
        const float H = ControlKit::SectionPadding * 2.0f + HeadH + ControlKitTokens::ControlHeight;
        const PlaneExtent Content = ControlKit::SectionCard(Surface, Spanning(X, Y, W, H), Radius, Opacity);
        ControlKit::SectionHeading(Surface, Content.MinimumX, Content.MinimumY, Content.Width(), "Corner Radius", "Adjust the roundness of UI elements", Ink90(), Ink50(), Opacity);
        char Value[8]; std::snprintf(Value, sizeof(Value), "%dpx", static_cast<int>(std::lround(Draft.CornerRadius)));
        const PlanePoint VM = Surface.MeasureText(Value, 12.0f);
        Surface.Text(Content.MaximumX - VM.X, Content.MinimumY + HeadH - 24.0f - 16.0f + (16.0f - VM.Y) * 0.5f, ControlKit::Faded(Ink50(), Opacity), Value, 12.0f);
        RadiusSliderExtent = Spanning(Content.MinimumX, Content.MinimumY + HeadH, Content.Width(), ControlKitTokens::ControlHeight);
        float V = Draft.CornerRadius;
        const ControlHit Hit = ControlKit::Slider(Surface, RadiusSliderExtent, 0.0f, 32.0f, V, DraggingSlider == 2, Pointer, V, true, false, Opacity);
        if (Hit.Pressed) DraggingSlider = 2;
        if (DraggingSlider == 2) Draft.CornerRadius = std::round(V);
        Y += H + SectionGap;
    }

    // ③ Accent Color — flex-wrap gap-3 of 32 px swatches + dashed custom circle.
    {
        const float HeadH = 20.0f + 4.0f + 16.0f + 24.0f;
        const float H = ControlKit::SectionPadding * 2.0f + HeadH + ControlKit::SwatchDiameter + 8.0f;
        const PlaneExtent Content = ControlKit::SectionCard(Surface, Spanning(X, Y, W, H), Radius, Opacity);
        ControlKit::SectionHeading(Surface, Content.MinimumX, Content.MinimumY, Content.Width(), "Accent Color", "Highlight colour for active controls", Ink90(), Ink50(), Opacity);
        const float Cy = Content.MinimumY + HeadH + 4.0f + 16.0f;
        for (uint32_t I = 0u; I < 10u; ++I)
        {
            const float Cx = Content.MinimumX + 16.0f + I * (ControlKit::SwatchDiameter + 12.0f);
            AccentExtents[I] = Spanning(Cx - 16.0f, Cy - 16.0f, 32.0f, 32.0f);
            if (ControlKit::Swatch(Surface, Cx, Cy, QueryAccentColour(static_cast<AccentCategory>(I)), static_cast<uint32_t>(Draft.Accent) == I, true, Pointer, Opacity).Clicked)
                Draft.Accent = static_cast<AccentCategory>(I);
        }
        // Custom picker placeholder (dashed border, "+") — picker itself is a later step.
        const float Cx = Content.MinimumX + 16.0f + 10u * (ControlKit::SwatchDiameter + 12.0f);
        {
            for (int I = 0; I < 24; I += 2)
            {
                const float A0 = 6.2831853f * I / 24.0f, A1 = 6.2831853f * (I + 1) / 24.0f;
                PlanePoint P[2] = { { Cx + std::cos(A0) * 16.0f, Cy + std::sin(A0) * 16.0f }, { Cx + std::cos(A1) * 16.0f, Cy + std::sin(A1) * 16.0f } };
                Surface.StrokePolyline(P, 2u, ControlKit::Faded(ColorQuad{ 1.0f, 1.0f, 1.0f, 0.06f }, Opacity), 1.0f, false);
            }
            const PlanePoint PM = Surface.MeasureText("+", 14.0f);
            Surface.Text(Cx - PM.X * 0.5f, Cy - PM.Y * 0.5f, ControlKit::Faded(Ink50(), Opacity), "+", 14.0f);
        }
        Y += H + SectionGap;
    }

    // ④ Semantic Colors — Warning · Success · Info · Caution rows.
    {
        const float HeadH = 20.0f + 4.0f + 16.0f + 24.0f;
        const float RowH = 100.0f, RowGap = 32.0f + 1.0f + 32.0f;   // pb-8 border-b space-y-8
        const float H = ControlKit::SectionPadding * 2.0f + HeadH + RowH * 4.0f + RowGap * 3.0f;
        const PlaneExtent Content = ControlKit::SectionCard(Surface, Spanning(X, Y, W, H), Radius, Opacity);
        ControlKit::SectionHeading(Surface, Content.MinimumX, Content.MinimumY, Content.Width(), "Semantic Colors", "Tones used by alerts, dialogues and toasts", Ink90(), Ink50(), Opacity);
        float RowY = Content.MinimumY + HeadH;
        RecordSemanticRow(Surface, Content.MinimumX, RowY, Content.Width(), 0u, "Warning", Draft.WarningSwatch, ControlCentreIconCategory::TriangleAlert, Radius, false, Pointer, Opacity); RowY += RowH + RowGap;
        RecordSemanticRow(Surface, Content.MinimumX, RowY, Content.Width(), 1u, "Success", Draft.SuccessSwatch, ControlCentreIconCategory::CircleCheck,   Radius, false, Pointer, Opacity); RowY += RowH + RowGap;
        RecordSemanticRow(Surface, Content.MinimumX, RowY, Content.Width(), 2u, "Info",    Draft.InfoSwatch,    ControlCentreIconCategory::CircleInfo,    Radius, false, Pointer, Opacity); RowY += RowH + RowGap;
        RecordSemanticRow(Surface, Content.MinimumX, RowY, Content.Width(), 3u, "Caution", Draft.CautionSwatch, ControlCentreIconCategory::OctagonAlert,  Radius, true,  Pointer, Opacity);
        Y += H + SectionGap;
    }

    if (Pointer.Released) DraggingSlider = -1;
    return (Y + ScrollY) - Body.MinimumY - SectionGap;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       FONTS TAB
//------------------------------------------------------------------------------------------------------------------------
//    Notch FontsTab.tsx — Typography strip · Typeface & Colors playground · Type Scale cards · Font Rendering.

namespace {

struct TypeRole { const char* Label; const char* Sample; };
constexpr TypeRole Roles[AppearanceSettings::TypeRoleCount] =
{
    { "Title",     "Display Title" },
    { "Header",    "Section Header" },
    { "Subheader", "Card Subheader" },
    { "Body",      "The quick brown fox jumps over the lazy dog." },
    { "Label",     "Form Label" },
    { "Caption",   "Small caption text" },
};

// Notch WEIGHTS[] chip order is alphabetical: Bold, ExtraBold, ExtraLight, Light, Medium, Regular, SemiBold (+ Thin, Black appended).
constexpr FontWeightCategory ChipOrder[9] =
{
    FontWeightCategory::Bold, FontWeightCategory::ExtraBold, FontWeightCategory::ExtraLight, FontWeightCategory::Light,
    FontWeightCategory::Medium, FontWeightCategory::Regular, FontWeightCategory::SemiBold, FontWeightCategory::Thin, FontWeightCategory::Black
};

void HexOf(ColorQuad C, char* Out, size_t N)
{
    std::snprintf(Out, N, "#%02X%02X%02X", static_cast<int>(C.Red * 255.0f + 0.5f), static_cast<int>(C.Green * 255.0f + 0.5f), static_cast<int>(C.Blue * 255.0f + 0.5f));
}

} // namespace

const char* AppearanceInspector::QueryTypeRoleLabel(uint32_t Role) noexcept
{
    return Roles[std::min(Role, AppearanceSettings::TypeRoleCount - 1u)].Label;
}

void* AppearanceInspector::QueryAppliedFace(uint32_t Role) const noexcept
{
    const TypefaceRegistry* Reg = TypefaceRegistry::QueryCurrent();
    if (!Reg || Role >= AppearanceSettings::TypeRoleCount) return nullptr;
    return Reg->QueryHandle(Applied.FontFamily, Applied.RoleWeight[Role]);
}

PlaneExtent AppearanceInspector::QueryRoleWeightChipExtent(uint32_t Role, FontWeightCategory Weight) const noexcept
{
    if (Role >= AppearanceSettings::TypeRoleCount) return {};
    return ChipExtents[Role][TypefaceRegistry::WeightOrdinal(Weight)];
}

void AppearanceInspector::AdvanceFontsTab(float DeltaSeconds) noexcept
{
    // scroll-smooth: exponential approach, ~200 ms
    const float Room = std::max(StripContentWidth - StripViewWidth, 0.0f);
    StripScrollTarget = std::clamp(StripScrollTarget, 0.0f, Room);
    const float K = 1.0f - std::exp(-DeltaSeconds * 18.0f);
    StripScroll += (StripScrollTarget - StripScroll) * K;
    if (std::fabs(StripScrollTarget - StripScroll) < 0.25f) StripScroll = StripScrollTarget;
}

float AppearanceInspector::ConstructFontsTabLayout(PixelSpace& Surface, const PlaneExtent& Body, float ScrollY, const ControlPointer& Pointer, float Opacity) noexcept
{
    DropdownCount = 0u;
    const TypefaceRegistry* Reg = TypefaceRegistry::QueryCurrent();
    const float Radius = std::clamp(Draft.CornerRadius, 0.0f, SectionRadiusMax);
    const float X = Body.MinimumX, W = Body.Width();
    float Y = Body.MinimumY - ScrollY;
    const ColorQuad Accent = QueryAccentColour(Draft.Accent);
    const uint32_t FamilyCount = Reg ? Reg->QueryFamilyCount() : 0u;
    if (Reg && Draft.FontFamily >= FamilyCount && FamilyCount > 0u) Draft.FontFamily = 0u;
    auto Face = [&](FontWeightCategory Wt) -> void* { return Reg ? Reg->QueryHandle(Draft.FontFamily, Wt) : nullptr; };
    const char* FamilyName = (Reg && Reg->QueryFamily(Draft.FontFamily)) ? Reg->QueryFamily(Draft.FontFamily)->Name.c_str() : "Default";
    constexpr float SpaceY10 = 40.0f;   // space-y-10

    // ① Typography — heading row + ‹ › + horizontal card strip (flex gap-4, cards w-48 p-5, pb-4 under the strip).
    {
        const float HeadH = 20.0f + 4.0f + 16.0f;   // title + gap-1 + desc  (mb-4 below)
        Surface.Text(X, Y + 2.0f, ControlKit::Faded(Ink90(), Opacity), "Typography", 14.0f);
        Surface.Text(X, Y + 24.0f, ControlKit::Faded(Ink50(), Opacity), "Typeface, type scale, and font weights", 12.0f);
        const float BtnCy = Y + HeadH - 16.0f;   // items-end
        StripForwardExtent = Spanning(X + W - 32.0f, BtnCy - 16.0f, 32.0f, 32.0f);
        StripBackExtent    = Spanning(X + W - 32.0f - 8.0f - 32.0f, BtnCy - 16.0f, 32.0f, 32.0f);
        if (ControlKit::RoundIconButton(Surface, StripBackExtent.MinimumX + 16.0f,    BtnCy, ControlCentreIconCategory::ChevronBack,    Pointer, Opacity).Clicked) StripScrollTarget -= 300.0f;
        if (ControlKit::RoundIconButton(Surface, StripForwardExtent.MinimumX + 16.0f, BtnCy, ControlCentreIconCategory::ChevronForward, Pointer, Opacity).Clicked) StripScrollTarget += 300.0f;
        Y += HeadH + 16.0f;

        const float CardW = 192.0f, CardGap = 16.0f, CardPad = 20.0f;
        const float CardH = CardPad + 32.0f + 16.0f + 20.0f + 4.0f + 16.0f + CardPad;   // Aa(text-2xl ≈32) mb-4, name 20 mb-1, sentence 16
        StripViewWidth = W; StripContentWidth = FamilyCount * (CardW + CardGap) - CardGap;
        const PlaneExtent Strip = Spanning(X, Y, W, CardH);
        Surface.PushClip(PlaneExtent{ Strip.MinimumX, Strip.MinimumY - 2.0f, Strip.MaximumX, Strip.MaximumY + 2.0f });
        // horizontal wheel / shift-less: the host maps vertical wheel to page scroll, so the strip uses the arrows only (Notch: same).
        for (uint32_t I = 0u; I < FamilyCount && I < 10u; ++I)
        {
            const TypefaceFamily* Fam = Reg->QueryFamily(I);
            const PlaneExtent Card = Spanning(X + I * (CardW + CardGap) - StripScroll, Y, CardW, CardH);
            FontCardExtents[I] = Card;
            const bool Active = Draft.FontFamily == I;
            ControlPointer Local = Pointer; Local.Enabled = Pointer.Enabled && Strip.Encloses(Pointer.X, Pointer.Y);
            const bool Hover = ControlKit::Over(Card, Local);
            if (Active) Surface.FillRectangle(Card, ControlKit::Faded(ColorQuad{ 1.0f, 1.0f, 1.0f, 0.05f }, Opacity), Radius);              // fontActiveBg
            else if (Hover) Surface.FillRectangle(Card, ControlKit::Faded(ColorQuad{ 1.0f, 1.0f, 1.0f, 0.05f }, Opacity), Radius);         // hover:activeBg
            ControlKit::OutlineRounded(Surface, Card, ControlKit::Faded(Active ? Accent : Ink10(), Opacity), Radius);
            Surface.PushTypeface(Reg->QueryHandle(I, FontWeightCategory::Regular));
            Surface.Text(Card.MinimumX + CardPad, Card.MinimumY + CardPad, ControlKit::Faded(Ink90(), Opacity), "Aa", 24.0f);
            Surface.PopTypeface();
            Surface.PushTypeface(Face(FontWeightCategory::Medium));
            Surface.Text(Card.MinimumX + CardPad, Card.MinimumY + CardPad + 32.0f + 16.0f + 2.0f, ControlKit::Faded(Ink90(), Opacity), Fam->Name.c_str(), 14.0f);
            Surface.PopTypeface();
            Surface.PushClip(PlaneExtent{ Card.MinimumX + CardPad, Card.MinimumY, Card.MaximumX - CardPad, Card.MaximumY });
            Surface.Text(Card.MinimumX + CardPad, Card.MinimumY + CardPad + 32.0f + 16.0f + 20.0f + 4.0f + 2.0f, ControlKit::Faded(Ink50(), Opacity), "The quick brown fox jumps", 12.0f);
            Surface.PopClip();
            if (Hover && Pointer.Released) Draft.FontFamily = I;
        }
        Surface.PopClip();
        Y += CardH + 16.0f + SpaceY10;   // pb-4 + space-y-10
    }

    // ② Typeface & Colors playground — p-6 card; grid [1fr auto] gap-8: preview box (p-10) | column min-w-280.
    {
        const float HeadH = 20.0f + 24.0f;   // text-sm bold mb-6
        const float ColW = 280.0f;
        const float ColumnH = (3.0f * 18.0f + 2.0f * 4.0f) + 32.0f + (16.0f + 4.0f + 16.0f + 16.0f) + 32.0f + (48.0f + 8.0f + 4.0f + 2.0f * 13.0f + 2.0f);
        const float PreviewH = 40.0f + 72.0f + 16.0f + 20.0f + 40.0f;
        const float InnerH = std::max(ColumnH, PreviewH);
        const float H = ControlKit::SectionPadding * 2.0f + HeadH + InnerH;
        const PlaneExtent Content = ControlKit::SectionCard(Surface, Spanning(X, Y, W, H), Radius, Opacity);
        Surface.Text(Content.MinimumX, Content.MinimumY + 2.0f, ControlKit::Faded(Ink90(), Opacity), "Typeface & Colors", 14.0f);

        const float Top = Content.MinimumY + HeadH;
        const PlaneExtent Preview = PlaneExtent{ Content.MinimumX, Top, Content.MaximumX - 32.0f - ColW, Top + InnerH };
        Surface.FillRectangle(Preview, ControlKit::Faded(ColorQuad{ 1.0f, 1.0f, 1.0f, 0.05f }, Opacity), Radius * 0.8f);
        ControlKit::OutlineRounded(Surface, Preview, ControlKit::Faded(Ink10(), Opacity), Radius * 0.8f);
        {
            Surface.PushClip(Preview);
            const float Cy = (Preview.MinimumY + Preview.MaximumY) * 0.5f - (72.0f + 16.0f + 20.0f) * 0.5f;
            Surface.PushTypeface(Face(FontWeightCategory::Bold));
            Surface.Text(Preview.MinimumX + 40.0f, Cy, ControlKit::Faded(Ink90(), Opacity), FamilyName, 72.0f);
            Surface.PopTypeface();
            Surface.Text(Preview.MinimumX + 40.0f, Cy + 72.0f + 16.0f, ControlKit::Faded(Ink50(), Opacity), "(72px bold)", 14.0f);
            Surface.PopClip();
        }
        {
            const float Cx = Content.MaximumX - ColW;
            float Cy = Top + (InnerH - ColumnH) * 0.5f;   // justify-center
            // Mono glyph rows: text-[11px] leading-relaxed, font-mono — rendered in JetBrains Mono when present.
            void* Mono = nullptr; if (Reg) { const int32_t M = Reg->FindFamily("JetBrains Mono"); if (M >= 0) Mono = Reg->QueryHandle(static_cast<uint32_t>(M), FontWeightCategory::Regular); }
            Surface.PushTypeface(Mono);
            Surface.Text(Cx, Cy,         ControlKit::Faded(Ink50(), Opacity), "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 11.0f);
            Surface.Text(Cx, Cy + 22.0f, ControlKit::Faded(Ink50(), Opacity), "abcdefghijklmnopqrstuvwxyz", 11.0f);
            Surface.Text(Cx, Cy + 44.0f, ControlKit::Faded(Ink50(), Opacity), "0123456789 !@#$%^&*()", 11.0f);
            Surface.PopTypeface();
            Cy += 3.0f * 18.0f + 2.0f * 4.0f + 32.0f;
            char Hex[8]; HexOf(Accent, Hex, sizeof(Hex));
            char Line[96];
            Surface.PushTypeface(Face(FontWeightCategory::SemiBold));
            Surface.Text(Cx, Cy, ControlKit::Faded(Accent, Opacity), "Accent", 12.0f);
            const float AccW = Surface.MeasureText("Accent", 12.0f).X;
            Surface.PopTypeface();
            Surface.PushTypeface(Face(FontWeightCategory::Regular));
            Surface.Text(Cx + AccW, Cy, ControlKit::Faded(Accent, Opacity), " \xE2\x80\x94 The quick brown fox jumps over...", 12.0f);
            std::snprintf(Line, sizeof(Line), "(rendered in %s)", Hex);
            ColorQuad AccentDim = Accent; AccentDim.Alpha *= 0.7f;
            Surface.Text(Cx, Cy + 20.0f, ControlKit::Faded(AccentDim, Opacity), Line, 12.0f);
            Surface.PopTypeface();
            Cy += 16.0f + 4.0f + 16.0f + 16.0f + 32.0f;
            // Three discs: accent / #FFFFFF / #000000 with hex + label (gap-6, 48 px, mt-1, 10 px lines)
            struct Disc { ColorQuad Colour; const char* Label; } Discs[3] = { { Accent, "Accent" }, { ColorQuad{ 1.0f, 1.0f, 1.0f, 1.0f }, "Primary" }, { ColorQuad{ 0.0f, 0.0f, 0.0f, 1.0f }, "Background" } };
            float Dx = Cx;
            for (const Disc& D : Discs)
            {
                ControlKit::FillCircle(Surface, Dx + 24.0f, Cy + 24.0f, 24.0f, ControlKit::Faded(D.Colour, Opacity));
                ControlKit::OutlineCircle(Surface, Dx + 24.0f, Cy + 24.0f, 23.5f, ControlKit::Faded(ColorQuad{ 1.0f, 1.0f, 1.0f, 0.05f }, Opacity), 1.0f);
                HexOf(D.Colour, Hex, sizeof(Hex));
                Surface.PushTypeface(Face(FontWeightCategory::SemiBold));
                Surface.Text(Dx, Cy + 48.0f + 12.0f, ControlKit::Faded(Ink90(), Opacity), Hex, 10.0f);
                Surface.PopTypeface();
                Surface.Text(Dx, Cy + 48.0f + 12.0f + 15.0f, ControlKit::Faded(Ink50(), Opacity), D.Label, 10.0f);
                Dx += std::max(48.0f, Surface.MeasureText(D.Label, 10.0f).X) + 24.0f;
            }
        }
        Y += H + SpaceY10;
    }

    // ③ Type Scale — Desktop: uppercase tracked heading, then per-role cards grid [300px 1fr] gap-8.
    {
        Surface.Text(X, Y + 2.0f, ControlKit::Faded(Ink50(), Opacity), "TYPE SCALE - DESKTOP", 12.0f);
        Y += 16.0f + 24.0f + 16.0f;   // mb-6 + pt-4
        for (uint32_t R = 0u; R < AppearanceSettings::TypeRoleCount; ++R)
        {
            // Chips for the weights this family actually ships (Notch: fixed seven; ours: per-family, flagged).
            uint32_t ChipCount = 0u; FontWeightCategory Chips[9]; float ChipW[9];
            for (FontWeightCategory Wt : ChipOrder)
                if (!Reg || Reg->HasWeight(Draft.FontFamily, Wt)) { Chips[ChipCount] = Wt; ChipW[ChipCount] = ControlKit::ChipWidth(Surface, TypefaceRegistry::WeightLabel(Wt)); ++ChipCount; }
            // Snap the role's weight to a shipped face so the chip row always shows a selection.
            if (Reg) { const TypefaceFace* Snap = Reg->Resolve(Draft.FontFamily, Draft.RoleWeight[R]); if (Snap) Draft.RoleWeight[R] = Snap->Weight; }

            // Wrap chips into rows of 300 px (flex-wrap gap-3).
            const float LeftW = 300.0f;
            uint32_t RowsNeeded = 1u; { float Cx = 0.0f; for (uint32_t C = 0u; C < ChipCount; ++C) { if (Cx > 0.0f && Cx + ChipW[C] > LeftW) { ++RowsNeeded; Cx = 0.0f; } Cx += ChipW[C] + 12.0f; } }
            const float LeftH = 20.0f + 24.0f + ControlKit::SliderThinHeight + 32.0f + RowsNeeded * ControlKit::ChipHeight + (RowsNeeded - 1u) * 12.0f;
            const float H = ControlKit::SectionPadding * 2.0f + std::max(LeftH, 100.0f);
            const PlaneExtent Content = ControlKit::SectionCard(Surface, Spanning(X, Y, W, H), Radius, Opacity);
            const float Top = Content.MinimumY + (Content.Height() - LeftH) * 0.5f;   // items-center

            // Left: label / "Npx" / slider / chips
            char Px[16]; std::snprintf(Px, sizeof(Px), "%dpx", static_cast<int>(std::lround(Draft.RoleSize[R])));
            Surface.PushTypeface(Face(FontWeightCategory::Medium));
            Surface.Text(Content.MinimumX, Top + 2.0f, ControlKit::Faded(Ink90(), Opacity), Roles[R].Label, 14.0f);
            Surface.PopTypeface();
            const PlanePoint PxM = Surface.MeasureText(Px, 12.0f);
            Surface.Text(Content.MinimumX + LeftW - PxM.X, Top + 3.0f, ControlKit::Faded(Ink50(), Opacity), Px, 12.0f);
            RoleSliderExtents[R] = Spanning(Content.MinimumX, Top + 20.0f + 24.0f - (ControlKitTokens::ControlHeight - ControlKit::SliderThinHeight) * 0.5f, LeftW, ControlKitTokens::ControlHeight);
            {
                float V = Draft.RoleSize[R];
                const int Ordinal = 10 + static_cast<int>(R);
                const ControlHit Hit = ControlKit::Slider(Surface, RoleSliderExtents[R], 8.0f, 72.0f, V, DraggingSlider == Ordinal, Pointer, V, true, false, Opacity);
                if (Hit.Pressed) DraggingSlider = Ordinal;
                if (DraggingSlider == Ordinal) Draft.RoleSize[R] = std::round(V);
            }
            {
                float Cx = Content.MinimumX, Cy = Top + 20.0f + 24.0f + ControlKit::SliderThinHeight + 32.0f;
                for (uint32_t O = 0u; O < 9u; ++O) ChipExtents[R][O] = PlaneExtent{};
                for (uint32_t C = 0u; C < ChipCount; ++C)
                {
                    if (Cx > Content.MinimumX && Cx - Content.MinimumX + ChipW[C] > LeftW) { Cx = Content.MinimumX; Cy += ControlKit::ChipHeight + 12.0f; }
                    const PlaneExtent Chip = Spanning(Cx, Cy, ChipW[C], ControlKit::ChipHeight);
                    ChipExtents[R][TypefaceRegistry::WeightOrdinal(Chips[C])] = Chip;
                    if (ControlKit::ChipButton(Surface, Chip, TypefaceRegistry::WeightLabel(Chips[C]), Draft.RoleWeight[R] == Chips[C], Pointer, Opacity).Clicked) Draft.RoleWeight[R] = Chips[C];
                    Cx += ChipW[C] + 12.0f;
                }
            }

            // Right: preview box p-6, radius × 0.7, min-h 100, sample text truncated at the role's size/weight.
            const PlaneExtent Box = PlaneExtent{ Content.MinimumX + LeftW + 32.0f, Content.MinimumY, Content.MaximumX, Content.MaximumY };
            Surface.FillRectangle(Box, ControlKit::Faded(ColorQuad{ 1.0f, 1.0f, 1.0f, 0.05f }, Opacity), Radius * 0.7f);
            ControlKit::OutlineRounded(Surface, Box, ControlKit::Faded(Ink10(), Opacity), Radius * 0.7f);
            Surface.PushClip(PlaneExtent{ Box.MinimumX + 24.0f, Box.MinimumY, Box.MaximumX - 24.0f, Box.MaximumY });
            Surface.PushTypeface(Face(Draft.RoleWeight[R]));
            const float LineH = Surface.MeasureText("Ag", Draft.RoleSize[R]).Y;
            Surface.Text(Box.MinimumX + 24.0f, (Box.MinimumY + Box.MaximumY) * 0.5f - LineH * 0.5f, ControlKit::Faded(Ink90(), Opacity), Roles[R].Sample, Draft.RoleSize[R]);
            Surface.PopTypeface();
            Surface.PopClip();

            Y += H + 32.0f;   // space-y-8
        }
        Y += SpaceY10 - 32.0f;
    }

    // ④ Font Rendering — two switch rows with a divider.
    {
        const float HeadH = 20.0f + 24.0f;
        const float RowH = 20.0f + 4.0f + 16.0f;
        const float H = ControlKit::SectionPadding * 2.0f + HeadH + 8.0f + RowH + 16.0f + 1.0f + 16.0f + RowH;
        const PlaneExtent Content = ControlKit::SectionCard(Surface, Spanning(X, Y, W, H), Radius, Opacity);
        Surface.Text(Content.MinimumX, Content.MinimumY + 2.0f, ControlKit::Faded(Ink90(), Opacity), "Font Rendering", 14.0f);
        float RowY = Content.MinimumY + HeadH + 8.0f;
        auto Row = [&](const char* Title, const char* Sub, bool& Flag, PlaneExtent& Out)
        {
            Surface.PushTypeface(Face(FontWeightCategory::Medium));
            Surface.Text(Content.MinimumX, RowY + 2.0f, ControlKit::Faded(Ink90(), Opacity), Title, 14.0f);
            Surface.PopTypeface();
            Surface.Text(Content.MinimumX, RowY + 24.0f, ControlKit::Faded(Ink50(), Opacity), Sub, 12.0f);
            // Notch: w-10 h-5 pill, knob 16 tinted accent. Kit switch is 46 × 26 — drawn at Notch's size here for fidelity.
            Out = Spanning(Content.MaximumX - 40.0f, RowY + (RowH - 20.0f) * 0.5f, 40.0f, 20.0f);
            const bool Hover = ControlKit::Over(Out, Pointer);
            Surface.FillRectangle(Out, ControlKit::Faded(ColorQuad{ 1.0f, 1.0f, 1.0f, Flag ? 0.20f : 0.05f }, Opacity), 10.0f);
            ControlKit::FillCircle(Surface, Out.MinimumX + 2.0f + 8.0f + (Flag ? 20.0f : 0.0f), Out.MinimumY + 10.0f, 8.0f, ControlKit::Faded(Flag ? Accent : ColorQuad{ 1.0f, 1.0f, 1.0f, 1.0f }, Opacity));
            if (Hover && Pointer.Released) Flag = !Flag;
        };
        Row("Antialiasing", "Enable subpixel antialiasing", Draft.FontAntialiasing, FontAaSwitchExtent);
        RowY += RowH + 16.0f;
        ControlKit::Divider(Surface, Content.MinimumX, RowY, Content.Width(), ControlKit::Faded(Ink10(), Opacity));
        RowY += 1.0f + 16.0f;
        Row("Ligatures", "Enable special character combinations", Draft.Ligatures, LigatureSwitchExtent);
        Y += H + SectionGap;
    }

    if (Pointer.Released) DraggingSlider = -1;
    return (Y + ScrollY) - Body.MinimumY - SectionGap;
}

} // namespace Frontier
