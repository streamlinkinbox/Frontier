/*==========================================================================================================================================
                                                 THEME CARD PALETTE
                                     ------------------------------------------------------------------
             Editor & Development ONLY native C++ card styling resolver bound directly to ThemeStructure.
             Ensures diagnostic and cluster-detail cards inherit background, foreground, border, accent,
             and font tokens from ThemeStructure (zero hardcoded emerald/forest green).
==========================================================================================================================================*/
// 🧩 Native C++ ThemeStructure card-token binding (Editor/Development only).

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"

#if FRONTIER_EDITOR

#include "ThemeStructure.h"

#include <string_view>

namespace Frontier
{

struct ThemeCardPalette
{
    ColorQuad        WorkspaceBackground{};
    ColorQuad        DockPanelBackground{};
    ColorQuad        InstrumentCardBackground{};
    ColorQuad        ParameterCardBackground{};
    ColorQuad        ForegroundPrimary{};
    ColorQuad        ForegroundMuted{};
    ColorQuad        CardBorder{};
    ColorQuad        SectionDivider{};
    ColorQuad        ControlTrackBackground{};
    ColorQuad        ActivePillBackground{};
    ColorQuad        AccentPrimary{};
    ColorQuad        AccentTintFill{};
    float            CardCornerRadiusPx  = 24.0f;
    float            TitleFontSizePx     = 24.0f;
    float            BodyFontSizePx      = 14.0f;
    float            LabelFontSizePx     = 12.0f;
    std::string_view FontFamilyAssetStem = "GeneralSans";
};

[[nodiscard]] inline ThemeCardPalette ResolveThemeCardPalette(const ThemeStructure& Theme) noexcept
{
    const ThemePalette& Palette = Theme.QueryPalette();
    const ColorQuad     Accent  = Theme.QueryAccentColor();

    ThemeCardPalette Card{};
    Card.WorkspaceBackground      = Palette.MainBackground;
    Card.DockPanelBackground      = Palette.PanelBackground;
    Card.InstrumentCardBackground = Palette.CardBackground;
    Card.ParameterCardBackground  = Palette.CardSubBackground;
    Card.ForegroundPrimary        = Palette.TextMain;
    Card.ForegroundMuted          = Palette.TextMuted;
    Card.CardBorder               = Palette.PanelBorder;
    Card.SectionDivider           = Palette.DividerColor;
    Card.ControlTrackBackground   = Palette.InputBackground;
    Card.ActivePillBackground     = Palette.ActiveBackground;
    Card.AccentPrimary            = Accent;
    Card.AccentTintFill           = ColorQuad{Accent.Red, Accent.Green, Accent.Blue, 0.16f};
    Card.CardCornerRadiusPx       = Theme.QueryCornerRadius();
    Card.TitleFontSizePx          = Theme.QueryTypographySize(TypographyRoleCategory::Title);
    Card.BodyFontSizePx           = Theme.QueryTypographySize(TypographyRoleCategory::Body);
    Card.LabelFontSizePx          = Theme.QueryTypographySize(TypographyRoleCategory::Label);
    switch (Theme.QueryActiveFontFamily())
    {
        case FontFamilyCategory::JetBrainsMono: Card.FontFamilyAssetStem = "JetBrainsMono"; break;
        case FontFamilyCategory::GeneralSans:   Card.FontFamilyAssetStem = "GeneralSans";   break;
        default:                                Card.FontFamilyAssetStem = "Inter";         break;
    }
    return Card;
}

} // namespace Frontier

#endif // FRONTIER_EDITOR
