/*==========================================================================================================================================
                                                               CONTROLTHEME.H
==========================================================================================================================================*/
// 🧩 One Inspector page's colours after its stylesheet cascade (CloudInspector.css first, then the page's
//    own sheet), with the CSS colour literals and alpha arithmetic every control uses.

#pragma once

#include <imgui.h>

#include <algorithm>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONTROL THEME
//------------------------------------------------------------------------------------------------------------------------

// 📝 A CSS #RRGGBBAA literal as an ImGui colour.
[[nodiscard]] constexpr ImU32 ResolveCssColour(uint32_t RedGreenBlueAlpha) noexcept
{
    return IM_COL32((RedGreenBlueAlpha >> 24) & 0xFF, (RedGreenBlueAlpha >> 16) & 0xFF, (RedGreenBlueAlpha >> 8) & 0xFF,
                    RedGreenBlueAlpha & 0xFF);
}

// 📝 Colour with its alpha multiplied by Fade (CSS opacity on an element).
[[nodiscard]] inline ImU32 FadeColour(ImU32 Colour, float Fade) noexcept
{
    const float Alpha = static_cast<float>((Colour >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(Fade, 0.0f, 1.0f);
    return (Colour & ~IM_COL32_A_MASK) | (static_cast<ImU32>(Alpha + 0.5f) << IM_COL32_A_SHIFT);
}

// 📝 Rgb with its alpha replaced by Alpha (rgba(r, g, b, Alpha)).
[[nodiscard]] inline ImU32 ResolveAlpha(ImU32 Rgb, float Alpha) noexcept
{
    return (Rgb & ~IM_COL32_A_MASK) | (static_cast<ImU32>(std::clamp(Alpha, 0.0f, 1.0f) * 255.0f + 0.5f) << IM_COL32_A_SHIFT);
}

struct ControlTheme
{
    ImU32 PageFill            = 0; // .native-environment background
    ImU32 Ink                 = 0; // --co-ink
    ImU32 Muted               = 0; // --co-muted
    ImU32 Accent              = 0; // --co-accent
    ImU32 Eyebrow             = 0; // .co-eyebrow
    ImU32 Subtitle            = 0; // .co-subtitle
    ImU32 CardHeading         = 0; // .co-card-heading
    ImU32 Index               = 0; // .co-index
    ImU32 Label               = 0; // .co-label
    ImU32 ReadoutUnit         = 0; // .co-readout small
    ImU32 Caption             = 0; // .co-caption
    ImU32 ControlLabel        = 0; // .co-card .ne-control-label
    ImU32 CardBorder          = 0; // .co-card border
    ImU32 TabRailFill         = 0; // .co-tabs background (0: no rail)
    ImU32 TabRailBorder       = 0;
    ImU32 TabText             = 0; // .co-tab
    ImU32 TabSelectedFill     = 0; // .co-tab[aria-selected=true]
    ImU32 TabSelectedText     = 0;
    ImU32 TabSelectedRing     = 0;
    ImU32 TabHoverFill        = 0; // .co-tab:hover
    ImU32 TabHoverText        = 0;
    ImU32 MetricFill          = 0; // .co-kpis .co-readout
    ImU32 MetricBorder        = 0;
    ImU32 MetricLeadFill      = 0; // .co-kpis .co-readout:last-child
    ImU32 MetricLeadInk       = 0;
    ImU32 MetricLeadMuted     = 0;
    ImU32 TagFill             = 0; // .co-tag
    ImU32 TagBorder           = 0;
    ImU32 TagText             = 0;
    ImU32 ModeFill            = 0; // the title-line action pill (.so-reset / .co-power)
    ImU32 ModeBorder          = 0;
    ImU32 ModeText            = 0;
    ImU32 ModeHoverFill       = 0;
    ImU32 NextFill            = 0; // .co-next
    ImU32 NextBorder          = 0;
    ImU32 NextText            = 0;
    ImU32 NextHoverFill       = 0;
    ImU32 NextHoverBorder     = 0;
    ImU32 SliderRule          = 0; // .co-slider border-top
    ImU32 SliderEmpty         = 0; // the track past --slider-fill
    ImU32 ThumbRing           = 0; // the thumb's 3 px border
    ImU32 ThumbHalo           = 0; // the thumb's 1 px outer shadow
    ImU32 SliderNumber        = 0; // .co-slider .ne-number
    ImU32 SliderUnit          = 0; // .co-slider .ne-unit
    ImU32 FieldRowText        = 0; // .co-data-row
    ImU32 FieldRowRule        = 0;
    ImU32 SelectFill          = 0; // .co-card .ne-select
    ImU32 SelectBorder        = 0;
    ImU32 SelectText          = 0;
    ImU32 PresetFill          = 0; // .so-preset
    ImU32 PresetBorder        = 0;
    ImU32 PresetText          = 0;
    ImU32 PresetPressedFill   = 0; // .so-preset[aria-pressed=true]
    ImU32 PresetPressedBorder = 0;
    ImU32 PresetPressedText   = 0;
    ImU32 ToggleFill          = 0; // .co-toggle
    ImU32 ToggleBorder        = 0;
    ImU32 ToggleText          = 0;
    ImU32 ToggleCaption       = 0; // .co-toggle > span:last-child
    ImU32 ToggleCaptionOn     = 0;
    ImU32 FooterText          = 0; // .co-footer
    ImU32 FooterRule          = 0;
    ImU32 AtlasFooterText     = 0; // .co-atlas-footer
    ImU32 AtlasFooterRule     = 0;

    float PaddingSide        = 20.0f;
    float PaddingTop         = 20.0f;
    float TitleSize          = 31.0f;
    float TitleGap           = 24.0f;
    float TitleLineHeight    = 1.15f;
    float ActionFontSize     = 9;
    float ActionPadding      = 11;
    float ActionHeight       = 28;
    float ActionOffset       = 5;
    float SubtitleFraction   = 1;
    float SubtitleMaxWidth   = 0.0f;
    float SubtitleSize       = 10.0f;
    float SubtitleGap        = 15.0f;
    float SubtitleLineHeight = 1.4f;
    float EyebrowTracking    = 1.7f;
    float CaptionSize        = 10.0f;
    float CaptionLineHeight  = 1.6f;
    float TabHeight          = 31.0f;
    float TabGap             = 3.0f;
    float TabMargin          = 19.0f;
    bool  CompactMetrics     = false;
    bool  CapsuleSliders     = false;
    float SliderMargin       = 0.0f;
    ImU32 SliderCapsule      = 0;

    [[nodiscard]] static ControlTheme ResolveNative() noexcept;
    [[nodiscard]] static ControlTheme ResolveGeometry() noexcept;   // [-] the Project Zero editor's object inspector
    [[nodiscard]] static ControlTheme ResolveWind() noexcept;
    [[nodiscard]] static ControlTheme ResolveFog() noexcept;
    [[nodiscard]] static ControlTheme ResolveStars() noexcept;
    [[nodiscard]] static ControlTheme ResolveMoon() noexcept;
    [[nodiscard]] static ControlTheme ResolveOptics() noexcept;
    [[nodiscard]] static ControlTheme ResolveSolar() noexcept;
    [[nodiscard]] static ControlTheme ResolveCloud() noexcept;
    [[nodiscard]] static ControlTheme ResolveAtmosphere() noexcept;
};

} // namespace Frontier
