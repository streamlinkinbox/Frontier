//============================================================================================================================================
// 📦 Frontier/DisplayPresentation/ThemeStructure.cpp — Design Tokens, Surface Color Palettes and Accent Color Implementations
//============================================================================================================================================

#include "ThemeStructure.h"
#include <algorithm>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

ThemeStructure::ThemeStructure() noexcept
    : ActiveTheme(ThemeCategory::Oled)                          // Default OLED (pure black #000000)
    , ActiveAccent(AccentCategory::Blue)                        // Default Blue (#3B82F6)
    , ActiveFontFamily(FontFamilyCategory::GeneralSans)         // Default General Sans
    , CornerRadiusPixels(24.0f)                                 // Default 24px radius
    , PaletteArray{}
    , AccentArray{}
    , TypographySizes{}
{
    // 0: OLED (Default Pure Black #000000)
    PaletteArray[static_cast<size_t>(ThemeCategory::Oled)] = ThemePalette{
        "OLED",
        ColorQuad{ 0.000f, 0.000f, 0.000f, 1.0f },              // MainBackground #000000
        ColorQuad{ 0.039f, 0.039f, 0.039f, 1.0f },              // PanelBackground #0A0A0A
        ColorQuad{ 0.078f, 0.078f, 0.082f, 1.0f },              // CardBackground #141415
        ColorQuad{ 0.110f, 0.110f, 0.118f, 1.0f },              // CardSubBackground #1C1C1E
        ColorQuad{ 0.950f, 0.950f, 0.950f, 1.0f },              // TextMain
        ColorQuad{ 0.400f, 0.400f, 0.400f, 1.0f },              // TextMuted
        ColorQuad{ 1.000f, 1.000f, 1.000f, 0.04f },             // PanelBorder
        ColorQuad{ 1.000f, 1.000f, 1.000f, 0.04f }              // DividerColor
    };

    // 1: Dark (#111111)
    PaletteArray[static_cast<size_t>(ThemeCategory::Dark)] = ThemePalette{
        "Dark",
        ColorQuad{ 0.067f, 0.067f, 0.067f, 1.0f },              // MainBackground #111111
        ColorQuad{ 0.102f, 0.102f, 0.102f, 1.0f },              // PanelBackground #1A1A1A
        ColorQuad{ 0.133f, 0.133f, 0.145f, 1.0f },              // CardBackground #222225
        ColorQuad{ 0.173f, 0.173f, 0.188f, 1.0f },              // CardSubBackground #2C2C30
        ColorQuad{ 0.900f, 0.900f, 0.900f, 1.0f },              // TextMain
        ColorQuad{ 0.500f, 0.500f, 0.500f, 1.0f },              // TextMuted
        ColorQuad{ 1.000f, 1.000f, 1.000f, 0.06f },             // PanelBorder
        ColorQuad{ 1.000f, 1.000f, 1.000f, 0.06f }              // DividerColor
    };

    // 2: Dim (#0F172A)
    PaletteArray[static_cast<size_t>(ThemeCategory::Dim)] = ThemePalette{
        "Dim",
        ColorQuad{ 0.059f, 0.090f, 0.165f, 1.0f },              // MainBackground #0F172A
        ColorQuad{ 0.118f, 0.161f, 0.231f, 1.0f },              // PanelBackground #1E293B
        ColorQuad{ 0.157f, 0.208f, 0.282f, 1.0f },              // CardBackground #283548
        ColorQuad{ 0.200f, 0.255f, 0.333f, 1.0f },              // CardSubBackground #334155
        ColorQuad{ 0.945f, 0.961f, 0.976f, 1.0f },              // TextMain #F1F5F9
        ColorQuad{ 0.580f, 0.639f, 0.722f, 1.0f },              // TextMuted #94A3B8
        ColorQuad{ 0.200f, 0.255f, 0.333f, 0.5f },              // PanelBorder
        ColorQuad{ 0.200f, 0.255f, 0.333f, 0.5f }               // DividerColor
    };

    // 3: Light (#F1F5F9)
    PaletteArray[static_cast<size_t>(ThemeCategory::Light)] = ThemePalette{
        "Light",
        ColorQuad{ 0.945f, 0.961f, 0.976f, 1.0f },              // MainBackground #F1F5F9
        ColorQuad{ 1.000f, 1.000f, 1.000f, 1.0f },              // PanelBackground #FFFFFF
        ColorQuad{ 0.973f, 0.980f, 0.988f, 1.0f },              // CardBackground #F8FAFC
        ColorQuad{ 0.886f, 0.910f, 0.941f, 1.0f },              // CardSubBackground #E2E8F0
        ColorQuad{ 0.059f, 0.090f, 0.165f, 1.0f },              // TextMain #0F172A
        ColorQuad{ 0.392f, 0.455f, 0.545f, 1.0f },              // TextMuted #64748B
        ColorQuad{ 0.886f, 0.910f, 0.941f, 1.0f },              // PanelBorder #E2E8F0
        ColorQuad{ 0.886f, 0.910f, 0.941f, 1.0f }               // DividerColor
    };

    // 4: Sepia (#EADDCF)
    PaletteArray[static_cast<size_t>(ThemeCategory::Sepia)] = ThemePalette{
        "Sepia",
        ColorQuad{ 0.918f, 0.867f, 0.812f, 1.0f },              // MainBackground #EADDCF
        ColorQuad{ 0.957f, 0.922f, 0.882f, 1.0f },              // PanelBackground #F4EBE1
        ColorQuad{ 0.980f, 0.933f, 0.851f, 1.0f },              // CardBackground #FAEED9
        ColorQuad{ 0.933f, 0.863f, 0.765f, 1.0f },              // CardSubBackground #EEDCC3
        ColorQuad{ 0.361f, 0.294f, 0.227f, 1.0f },              // TextMain #5C4B3A
        ColorQuad{ 0.549f, 0.478f, 0.420f, 1.0f },              // TextMuted #8C7A6B
        ColorQuad{ 0.847f, 0.765f, 0.690f, 1.0f },              // PanelBorder #D8C3B0
        ColorQuad{ 0.847f, 0.765f, 0.690f, 1.0f }               // DividerColor
    };

    // 5: Dracula (#282A36)
    PaletteArray[static_cast<size_t>(ThemeCategory::Dracula)] = ThemePalette{
        "Dracula",
        ColorQuad{ 0.157f, 0.165f, 0.212f, 1.0f },              // MainBackground #282A36
        ColorQuad{ 0.267f, 0.278f, 0.353f, 1.0f },              // PanelBackground #44475A
        ColorQuad{ 0.220f, 0.227f, 0.349f, 1.0f },              // CardBackground #383A59
        ColorQuad{ 0.384f, 0.447f, 0.643f, 1.0f },              // CardSubBackground #6272A4
        ColorQuad{ 0.973f, 0.973f, 0.949f, 1.0f },              // TextMain #F8F8F2
        ColorQuad{ 0.384f, 0.447f, 0.643f, 1.0f },              // TextMuted #6272A4
        ColorQuad{ 0.384f, 0.447f, 0.643f, 0.3f },              // PanelBorder
        ColorQuad{ 0.384f, 0.447f, 0.643f, 0.3f }               // DividerColor
    };

    // 6: Nord (#2E3440)
    PaletteArray[static_cast<size_t>(ThemeCategory::Nord)] = ThemePalette{
        "Nord",
        ColorQuad{ 0.180f, 0.204f, 0.251f, 1.0f },              // MainBackground #2E3440
        ColorQuad{ 0.231f, 0.259f, 0.322f, 1.0f },              // PanelBackground #3B4252
        ColorQuad{ 0.263f, 0.298f, 0.369f, 1.0f },              // CardBackground #434C5E
        ColorQuad{ 0.298f, 0.337f, 0.416f, 1.0f },              // CardSubBackground #4C566A
        ColorQuad{ 0.925f, 0.937f, 0.957f, 1.0f },              // TextMain #ECEFF4
        ColorQuad{ 0.847f, 0.871f, 0.914f, 1.0f },              // TextMuted #D8DEE9
        ColorQuad{ 0.298f, 0.337f, 0.416f, 1.0f },              // PanelBorder #4C566A
        ColorQuad{ 0.298f, 0.337f, 0.416f, 1.0f }               // DividerColor
    };

    // 7: GitHub (#0D1117)
    PaletteArray[static_cast<size_t>(ThemeCategory::GitHub)] = ThemePalette{
        "GitHub",
        ColorQuad{ 0.051f, 0.067f, 0.090f, 1.0f },              // MainBackground #0D1117
        ColorQuad{ 0.086f, 0.106f, 0.133f, 1.0f },              // PanelBackground #161B22
        ColorQuad{ 0.129f, 0.149f, 0.176f, 1.0f },              // CardBackground #21262D
        ColorQuad{ 0.188f, 0.212f, 0.239f, 1.0f },              // CardSubBackground #30363D
        ColorQuad{ 0.788f, 0.820f, 0.851f, 1.0f },              // TextMain #C9D1D9
        ColorQuad{ 0.545f, 0.580f, 0.620f, 1.0f },              // TextMuted #8B949E
        ColorQuad{ 0.188f, 0.212f, 0.239f, 1.0f },              // PanelBorder #30363D
        ColorQuad{ 0.188f, 0.212f, 0.239f, 1.0f }               // DividerColor
    };

    // 10 Accent Color Swatches
    AccentArray[static_cast<size_t>(AccentCategory::White)]   = ColorQuad{ 1.000f, 1.000f, 1.000f, 1.0f }; // #FFFFFF
    AccentArray[static_cast<size_t>(AccentCategory::Orange)]  = ColorQuad{ 0.976f, 0.451f, 0.086f, 1.0f }; // #F97316
    AccentArray[static_cast<size_t>(AccentCategory::Amber)]   = ColorQuad{ 0.961f, 0.620f, 0.043f, 1.0f }; // #F59E0B
    AccentArray[static_cast<size_t>(AccentCategory::Lime)]    = ColorQuad{ 0.518f, 0.800f, 0.086f, 1.0f }; // #84CC16
    AccentArray[static_cast<size_t>(AccentCategory::Emerald)] = ColorQuad{ 0.063f, 0.725f, 0.506f, 1.0f }; // #10B981
    AccentArray[static_cast<size_t>(AccentCategory::Cyan)]    = ColorQuad{ 0.024f, 0.714f, 0.831f, 1.0f }; // #06B6D4
    AccentArray[static_cast<size_t>(AccentCategory::Blue)]    = ColorQuad{ 0.231f, 0.510f, 0.965f, 1.0f }; // #3B82F6 (Default)
    AccentArray[static_cast<size_t>(AccentCategory::Violet)]  = ColorQuad{ 0.545f, 0.361f, 0.965f, 1.0f }; // #8B5CF6
    AccentArray[static_cast<size_t>(AccentCategory::Fuchsia)] = ColorQuad{ 0.851f, 0.275f, 0.937f, 1.0f }; // #D946EF
    AccentArray[static_cast<size_t>(AccentCategory::Rose)]    = ColorQuad{ 0.957f, 0.247f, 0.369f, 1.0f }; // #F43F5E

    // 8 Typography Role Standard Sizes [px]
    TypographySizes[static_cast<size_t>(TypographyRoleCategory::Title)]     = 24.0f;
    TypographySizes[static_cast<size_t>(TypographyRoleCategory::Header)]    = 20.0f;
    TypographySizes[static_cast<size_t>(TypographyRoleCategory::Subheader)] = 16.0f;
    TypographySizes[static_cast<size_t>(TypographyRoleCategory::Body)]      = 14.0f;
    TypographySizes[static_cast<size_t>(TypographyRoleCategory::Label)]     = 12.0f;
    TypographySizes[static_cast<size_t>(TypographyRoleCategory::Caption)]   = 11.0f;
    TypographySizes[static_cast<size_t>(TypographyRoleCategory::Warning)]   = 13.0f;
    TypographySizes[static_cast<size_t>(TypographyRoleCategory::Alert)]     = 13.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THEME ACCESSORS
//------------------------------------------------------------------------------------------------------------------------

void ThemeStructure::AssignTheme(ThemeCategory DesiredTheme) noexcept
{
    ActiveTheme = DesiredTheme;
}

void ThemeStructure::AssignAccent(AccentCategory DesiredAccent) noexcept
{
    ActiveAccent = DesiredAccent;
}

void ThemeStructure::AssignCornerRadius(float RadiusPixels) noexcept
{
    CornerRadiusPixels = std::clamp(RadiusPixels, 0.0f, 32.0f);
}

const ThemePalette& ThemeStructure::QueryPalette() const noexcept
{
    size_t Index = static_cast<size_t>(ActiveTheme);
    if (Index < PaletteArray.size())
    {
        return PaletteArray[Index];
    }
    return PaletteArray[0];
}

ColorQuad ThemeStructure::QueryAccentColor() const noexcept
{
    size_t Index = static_cast<size_t>(ActiveAccent);
    if (Index < AccentArray.size())
    {
        return AccentArray[Index];
    }
    return AccentArray[static_cast<size_t>(AccentCategory::Blue)];
}

float ThemeStructure::QueryTypographySize(TypographyRoleCategory Role) const noexcept
{
    size_t Index = static_cast<size_t>(Role);
    if (Index < TypographySizes.size())
    {
        return TypographySizes[Index];
    }
    return 14.0f;
}

} // namespace Frontier
