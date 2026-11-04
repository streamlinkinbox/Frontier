/*==========================================================================================================================================
                                                              CONTROLTHEME.CPP
==========================================================================================================================================*/
// 🧩 Page colours after each stylesheet cascade (CloudInspector.css, SunInspector.css,
//    AtmosphereInspector.css …), one resolver per page family.

#include "Engine/Interface/Controls/ControlTheme.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THEMES
//------------------------------------------------------------------------------------------------------------------------

static ControlTheme ResolveObservatoryPalette() noexcept
{
    // 📝 CloudInspector.css on its own (the cloud page's base, before its page rules).
    ControlTheme Theme;
    Theme.PageFill            = ResolveCssColour(0x151915FF);
    Theme.Ink                 = ResolveCssColour(0xE5EDDAFF);
    Theme.Muted               = ResolveCssColour(0x8F9A88FF);
    Theme.Accent              = ResolveCssColour(0xD5E6A5FF);
    Theme.Eyebrow             = ResolveCssColour(0x8E988CFF);
    Theme.Subtitle            = Theme.Muted;
    Theme.CardHeading         = ResolveCssColour(0xDCE1D4FF);
    Theme.Index               = ResolveCssColour(0x7F8D78FF);
    Theme.Label               = ResolveCssColour(0x929B8BFF);
    Theme.ReadoutUnit         = ResolveCssColour(0x909985FF);
    Theme.Caption             = ResolveCssColour(0x8F9A88FF);
    Theme.ControlLabel        = ResolveCssColour(0x8F9A88FF);
    Theme.CardBorder          = ResolveCssColour(0xFFFFFF07);
    Theme.TabRailFill         = ResolveCssColour(0x1C201CFF);
    Theme.TabRailBorder       = ResolveCssColour(0xFFFFFF07);
    Theme.TabText             = ResolveCssColour(0x8F988AFF);
    Theme.TabSelectedFill     = ResolveCssColour(0x30382DFF);
    Theme.TabSelectedText     = ResolveCssColour(0xE0E9CDFF);
    Theme.TabSelectedRing     = ResolveCssColour(0xD5E6A512);
    Theme.TabHoverFill        = ResolveCssColour(0x2C3329FF);
    Theme.TabHoverText        = ResolveCssColour(0xEEF1E7FF);
    Theme.MetricFill          = ResolveCssColour(0x20251FFF);
    Theme.MetricBorder        = ResolveCssColour(0xFFFFFF05);
    Theme.MetricLeadFill      = ResolveCssColour(0xD5E6A5FF);
    Theme.MetricLeadInk       = ResolveCssColour(0x222C19FF);
    Theme.MetricLeadMuted     = ResolveCssColour(0x576443FF);
    Theme.TagFill             = ResolveCssColour(0xD5E6A50A);
    Theme.TagBorder           = ResolveCssColour(0xD5E6A51A);
    Theme.TagText             = ResolveCssColour(0xCCD9B7FF);
    Theme.ModeFill            = ResolveCssColour(0xD5E6A514);
    Theme.ModeBorder          = ResolveCssColour(0x56624855);
    Theme.ModeText            = ResolveCssColour(0xD5E6A5FF);
    Theme.ModeHoverFill       = ResolveCssColour(0xD5E6A524);
    Theme.NextFill            = ResolveCssColour(0xD5E6A50D);
    Theme.NextBorder          = ResolveCssColour(0xD5E6A522);
    Theme.NextText            = ResolveCssColour(0xCBD8B5FF);
    Theme.NextHoverFill       = ResolveCssColour(0xD5E6A51C);
    Theme.NextHoverBorder     = ResolveCssColour(0xD5E6A555);
    Theme.SliderRule          = ResolveCssColour(0xFFFFFF0A);
    Theme.SliderEmpty         = ResolveCssColour(0xFFFFFF12);
    Theme.ThumbRing           = ResolveCssColour(0x24281FFF);
    Theme.ThumbHalo           = ResolveCssColour(0xFFFFFF22);
    Theme.SliderNumber        = Theme.Ink;
    Theme.SliderUnit          = Theme.Muted;
    Theme.FieldRowText        = ResolveCssColour(0xA2AC99FF);
    Theme.FieldRowRule        = ResolveCssColour(0xFFFFFF0A);
    Theme.SelectFill          = ResolveCssColour(0x161C14FF);
    Theme.SelectBorder        = ResolveCssColour(0x414B3866);
    Theme.SelectText          = ResolveCssColour(0xD8E2CCFF);
    Theme.PresetFill          = Theme.MetricFill;
    Theme.PresetBorder        = Theme.TagBorder;
    Theme.PresetText          = Theme.TagText;
    Theme.PresetPressedFill   = Theme.TabSelectedFill;
    Theme.PresetPressedBorder = Theme.NextHoverBorder;
    Theme.PresetPressedText   = Theme.TabSelectedText;
    Theme.ToggleFill          = ResolveCssColour(0x12181066);
    Theme.ToggleBorder        = ResolveCssColour(0xFFFFFF09);
    Theme.ToggleText          = ResolveCssColour(0xABB7A0FF);
    Theme.ToggleCaption       = ResolveCssColour(0x8E9889FF);
    Theme.ToggleCaptionOn     = ResolveCssColour(0xD5E6A5FF);
    Theme.FooterText          = ResolveCssColour(0x697760FF);
    Theme.FooterRule          = ResolveCssColour(0xFFFFFF0A);
    Theme.AtlasFooterText     = ResolveCssColour(0x8E9A8AFF);
    Theme.AtlasFooterRule     = ResolveCssColour(0xFFFFFF0B);
    return Theme;
}

ControlTheme ControlTheme::ResolveCloud() noexcept
{
    auto Theme         = ResolveObservatoryPalette();
    Theme.PageFill     = ResolveCssColour(0x171817FF);
    Theme.Ink          = ResolveCssColour(0xE7E8E0FF);
    Theme.Muted        = ResolveCssColour(0x95988EFF);
    Theme.Accent       = ResolveCssColour(0xD3DFA5FF);
    Theme.Eyebrow      = ResolveCssColour(0x8C9185FF);
    Theme.Subtitle     = ResolveCssColour(0x999C92FF);
    Theme.CardHeading  = ResolveCssColour(0xE0E3D8FF);
    Theme.Index        = ResolveCssColour(0x8C9482FF);
    Theme.Caption      = ResolveCssColour(0x989F90FF);
    Theme.ControlLabel = Theme.Muted;
    Theme.SliderNumber = Theme.Ink;
    Theme.SliderUnit   = Theme.Muted;
    Theme.TabRailFill = Theme.TabRailBorder = Theme.TabSelectedRing = 0;
    Theme.TabText                                                   = ResolveCssColour(0x8D9386FF);
    Theme.TabSelectedFill                                           = ResolveCssColour(0x30332CFF);
    Theme.TabSelectedText                                           = ResolveCssColour(0xE5EBD8FF);
    Theme.MetricFill                                                = ResolveCssColour(0x242622FF);
    Theme.MetricBorder                                              = 0;
    Theme.MetricLeadFill                                            = ResolveCssColour(0x2C3125FF);
    Theme.MetricLeadInk                                             = ResolveCssColour(0xE5EDCCFF);
    Theme.MetricLeadMuted                                           = ResolveCssColour(0xA6B294FF);
    Theme.PaddingSide                                               = 18;
    Theme.PaddingTop                                                = 22;
    Theme.TitleGap                                                  = 17;
    Theme.SubtitleGap                                               = 9;
    Theme.SubtitleSize                                              = 11;
    Theme.EyebrowTracking                                           = 1.4f;
    Theme.TabHeight                                                 = 33;
    Theme.TabMargin                                                 = 20;
    Theme.CompactMetrics                                            = true;
    Theme.ActionFontSize                                            = 10;
    Theme.ActionHeight                                              = 29;
    Theme.ActionOffset                                              = 4;
    return Theme;
}

ControlTheme ControlTheme::ResolveSolar() noexcept
{
    // 📝 SunInspector.css over CloudInspector.css; its second block (the neutral olive page) wins where both set a rule.
    ControlTheme Theme        = ResolveObservatoryPalette();
    Theme.PageFill            = ResolveCssColour(0x191917FF);
    Theme.Ink                 = ResolveCssColour(0xE8E7DFFF);
    Theme.Muted               = ResolveCssColour(0x9A988EFF);
    Theme.Accent              = ResolveCssColour(0xE3D2A6FF);
    Theme.Eyebrow             = ResolveCssColour(0xA09680FF);
    Theme.Subtitle            = ResolveCssColour(0xA19786FF);
    Theme.CardHeading         = ResolveCssColour(0xE7DFCDFF);
    Theme.Index               = ResolveCssColour(0xA0967DFF);
    Theme.Label               = ResolveCssColour(0xAD9F83FF);
    Theme.ReadoutUnit         = ResolveCssColour(0xA09478FF);
    Theme.Caption             = ResolveCssColour(0xA3977EFF);
    Theme.ControlLabel        = ResolveCssColour(0xB3A58AFF);
    Theme.CardBorder          = ResolveCssColour(0xF0D99C09);
    Theme.TabRailFill         = 0;
    Theme.TabRailBorder       = 0;
    Theme.TabText             = ResolveCssColour(0xA79D87FF);
    Theme.TabSelectedFill     = ResolveCssColour(0x343329FF);
    Theme.TabSelectedText     = ResolveCssColour(0xE7DFC8FF);
    Theme.TabSelectedRing     = ResolveCssColour(0xE8C78B1A);
    Theme.TabHoverFill        = ResolveCssColour(0x352F24FF);
    Theme.TabHoverText        = ResolveCssColour(0xF4DFB9FF);
    Theme.MetricFill          = ResolveCssColour(0x262720FF);
    Theme.MetricLeadFill      = ResolveCssColour(0xD7DBB8FF);
    Theme.MetricLeadInk       = ResolveCssColour(0x292E20FF);
    Theme.MetricLeadMuted     = ResolveCssColour(0x626B51FF);
    Theme.TagFill             = ResolveCssColour(0xE8C78B0C);
    Theme.TagBorder           = ResolveCssColour(0xE8C78B28);
    Theme.TagText             = ResolveCssColour(0xE8C78BFF);
    Theme.ModeFill            = ResolveCssColour(0xE8C78B0B); // .so-reset
    Theme.ModeBorder          = ResolveCssColour(0xE8C78B22);
    Theme.ModeText            = ResolveCssColour(0xD5BF95FF);
    Theme.ModeHoverFill       = ResolveCssColour(0xE8C78B1C);
    Theme.NextFill            = ResolveCssColour(0xE8C78B0C);
    Theme.NextBorder          = ResolveCssColour(0xE8C78B22);
    Theme.NextText            = ResolveCssColour(0xE8C78BFF);
    Theme.NextHoverFill       = ResolveCssColour(0xE8C78B20);
    Theme.NextHoverBorder     = ResolveCssColour(0xE8C78B55);
    Theme.SliderNumber        = Theme.Ink;
    Theme.SliderUnit          = Theme.Muted;
    Theme.FieldRowText        = ResolveCssColour(0xB8AD96FF);
    Theme.FieldRowRule        = ResolveCssColour(0xE8C78B10);
    Theme.SelectFill          = ResolveCssColour(0x211E17FF);
    Theme.SelectBorder        = ResolveCssColour(0xE8C78B22);
    Theme.SelectText          = ResolveCssColour(0xE9D7B6FF);
    Theme.PresetFill          = ResolveCssColour(0x25231CFF);
    Theme.PresetBorder        = ResolveCssColour(0xE8C78B12);
    Theme.PresetText          = ResolveCssColour(0xBBAE91FF);
    Theme.PresetPressedFill   = ResolveCssColour(0x4E3E23FF);
    Theme.PresetPressedBorder = ResolveCssColour(0xE8C78B44);
    Theme.PresetPressedText   = ResolveCssColour(0xF4D6A0FF);
    Theme.FooterText          = ResolveCssColour(0xA29473FF);
    Theme.FooterRule          = ResolveCssColour(0xE8C78B10);
    Theme.AtlasFooterText     = ResolveCssColour(0xAD9F83FF);
    Theme.AtlasFooterRule     = ResolveCssColour(0xE8C78B12);
    return Theme;
}

ControlTheme ControlTheme::ResolveAtmosphere() noexcept
{
    // 📝 AtmosphereInspector.css over CloudInspector.css.
    ControlTheme Theme    = ResolveObservatoryPalette();
    Theme.PageFill        = ResolveCssColour(0x151815FF); // .atmosphere-observatory background-color
    Theme.Ink             = ResolveCssColour(0xE8EAE0FF); // --co-ink
    Theme.Muted           = ResolveCssColour(0x989E8FFF); // --co-muted
    Theme.Accent          = ResolveCssColour(0xD6E3A6FF); // --co-accent
    Theme.SliderNumber    = Theme.Ink;
    Theme.SliderUnit      = Theme.Muted;
    Theme.Eyebrow         = ResolveCssColour(0x8C9783FF);
    Theme.Subtitle        = ResolveCssColour(0x9AA38FFF);
    Theme.ModeFill        = ResolveCssColour(0xD6E3A60C);
    Theme.ModeBorder      = ResolveCssColour(0xD6E3A626);
    Theme.ModeText        = ResolveCssColour(0xD6E3A6FF);
    Theme.TabRailFill     = 0;
    Theme.TabRailBorder   = 0;
    Theme.TabSelectedFill = ResolveCssColour(0x343A2CFF);
    Theme.TabSelectedText = ResolveCssColour(0xE0E9C7FF);
    Theme.TabSelectedRing = 0;
    Theme.TabHoverFill    = ResolveCssColour(0x2A3125FF);
    Theme.TabHoverText    = ResolveCssColour(0xE0E9C7FF);
    Theme.CardBorder      = 0;
    Theme.Index           = ResolveCssColour(0x8C9A7DFF);
    Theme.CardHeading     = ResolveCssColour(0xDDE5D0FF);
    Theme.Caption         = ResolveCssColour(0x96A18BFF);
    Theme.TagFill         = ResolveCssColour(0xD6E3A608);
    Theme.TagText         = ResolveCssColour(0xC1D3A3FF);
    Theme.TagBorder       = ResolveCssColour(0xD6E3A620);
    Theme.Label           = ResolveCssColour(0x95A383FF);
    Theme.ReadoutUnit     = ResolveCssColour(0x95A383FF);
    Theme.MetricFill      = ResolveCssColour(0x252B21FF);
    Theme.MetricBorder    = 0;
    Theme.MetricLeadFill  = ResolveCssColour(0xD7E3ADFF);
    Theme.MetricLeadInk   = ResolveCssColour(0x2D3823FF);
    Theme.MetricLeadMuted = ResolveCssColour(0x687951FF);
    Theme.ToggleFill      = ResolveCssColour(0x171D14FF);
    Theme.ToggleText      = ResolveCssColour(0xBCC9ABFF);
    Theme.ToggleBorder    = ResolveCssColour(0xD6E3A615);
    Theme.ToggleCaptionOn = ResolveCssColour(0xD6E3A6FF);
    Theme.FooterText      = ResolveCssColour(0x7C8E69FF);
    Theme.FooterRule      = ResolveCssColour(0xD6E3A615);
    Theme.ControlLabel    = ResolveCssColour(0x9CAB8BFF);
    return Theme;
}

ControlTheme ControlTheme::ResolveNative() noexcept
{
    ControlTheme Theme       = ResolveObservatoryPalette();
    Theme.PageFill           = ResolveCssColour(0x1B1D20FF);
    Theme.Ink                = ResolveCssColour(0xE0E0D8FF);
    Theme.Accent             = ResolveCssColour(0xB2C5DDFF);
    Theme.Muted              = ResolveCssColour(0x919191FF);
    Theme.Eyebrow            = Theme.Muted;
    Theme.Subtitle           = Theme.Muted;
    Theme.Caption            = Theme.Muted;
    Theme.ControlLabel       = ResolveCssColour(0xA5A5A5FF);
    Theme.SliderNumber       = Theme.Ink;
    Theme.SliderUnit         = ResolveCssColour(0xB2B2AAFF);
    Theme.SliderEmpty        = ResolveCssColour(0x171818FF);
    Theme.SliderRule         = 0;
    Theme.SliderCapsule      = ResolveCssColour(0x080808FF);
    Theme.TabRailFill        = 0;
    Theme.TabRailBorder      = 0;
    Theme.TabSelectedFill    = ResolveCssColour(0x343D46FF);
    Theme.TabSelectedText    = ResolveCssColour(0xE1EAF4FF);
    Theme.TabSelectedRing    = 0;
    Theme.TabText            = ResolveCssColour(0x91A0ADFF);
    Theme.TabHoverFill       = ResolveCssColour(0x29333BFF);
    Theme.TabHoverText       = ResolveCssColour(0xD1E2EEFF);
    Theme.SelectFill         = ResolveCssColour(0x171B20FF);
    Theme.SelectText         = Theme.Ink;
    Theme.SelectBorder       = ResolveCssColour(0xB1C7E125);
    Theme.NextFill           = ResolveCssColour(0xA6BCE00A);
    Theme.NextBorder         = ResolveCssColour(0xBED1ED25);
    Theme.NextText           = ResolveCssColour(0xC0D2EBFF);
    Theme.NextHoverFill      = ResolveCssColour(0xA6BCE019);
    Theme.NextHoverBorder    = Theme.NextBorder;
    Theme.PaddingSide        = 18.0f;
    Theme.PaddingTop         = 22.0f;
    Theme.TitleGap           = 13.0f;
    Theme.SubtitleGap        = 8.0f;
    Theme.SubtitleSize       = 11.0f;
    Theme.SubtitleLineHeight = 1.6f;
    Theme.CapsuleSliders     = true;
    Theme.SliderMargin       = 22.0f;
    Theme.TabHeight          = 33.0f;
    Theme.TabGap             = 4.0f;
    Theme.TabMargin          = 20.0f;
    return Theme;
}

// 📝 The Project Zero editor's inspector (Editor.css): page #141414, ink #f0f0f0, muted #888, captions #757575, the warm accent
//    #c8b59d of its material headings, segmented controls #111 with the active #343434.
ControlTheme ControlTheme::ResolveGeometry() noexcept
{
    ControlTheme Theme    = ResolveNative();
    Theme.PageFill        = ResolveCssColour(0x141414FF);
    Theme.Ink             = ResolveCssColour(0xF0F0F0FF);
    Theme.Muted           = ResolveCssColour(0x888888FF);
    Theme.Accent          = ResolveCssColour(0xC8B59DFF);
    Theme.Eyebrow         = ResolveCssColour(0x919191FF);
    Theme.Subtitle        = ResolveCssColour(0x8A8A8AFF);
    Theme.Caption         = ResolveCssColour(0x757575FF);
    Theme.ControlLabel    = ResolveCssColour(0xBDBDBDFF);
    Theme.CardHeading     = ResolveCssColour(0xDDDDDDFF);
    Theme.Index           = ResolveCssColour(0x777777FF);
    Theme.TabSelectedFill = ResolveCssColour(0x343434FF);
    Theme.TabSelectedText = ResolveCssColour(0xEEEEEEFF);
    Theme.TabText         = ResolveCssColour(0x8A8A8AFF);
    Theme.TabHoverFill    = ResolveCssColour(0x262626FF);
    Theme.TabHoverText    = ResolveCssColour(0xD8D8D8FF);
    Theme.MetricFill      = ResolveCssColour(0x222222FF);
    Theme.MetricBorder    = ResolveCssColour(0xFFFFFF06);
    Theme.SelectFill      = ResolveCssColour(0x111111FF);
    Theme.SelectBorder    = ResolveCssColour(0xFFFFFF12);
    Theme.SliderCapsule   = ResolveCssColour(0x0E0E0EFF);
    Theme.SliderEmpty     = ResolveCssColour(0x262626FF);   // [-] the kslider track (raised #222) on the #1a1a1a cards
    // 📝 .fracture-mode: the segmented track #0e0e0e over #ffffff0b, the pressed segment #343434 with #eee.
    Theme.PresetFill          = ResolveCssColour(0x0E0E0EFF);
    Theme.PresetBorder        = ResolveCssColour(0xFFFFFF0B);
    Theme.PresetText          = ResolveCssColour(0x9A9A9AFF);
    Theme.PresetPressedFill   = ResolveCssColour(0x343434FF);
    Theme.PresetPressedBorder = ResolveCssColour(0xFFFFFF14);
    Theme.PresetPressedText   = ResolveCssColour(0xEEEEEEFF);
    Theme.CaptionSize       = 10.0f;
    Theme.CaptionLineHeight = 1.7f;
    return Theme;
}

ControlTheme ControlTheme::ResolveWind() noexcept
{
    ControlTheme Theme       = ResolveNative();
    Theme.PageFill           = ResolveCssColour(0x191C19FF);
    Theme.Ink                = ResolveCssColour(0xD2DDC7FF);
    Theme.Accent             = ResolveCssColour(0xCBD9AEFF);
    Theme.Eyebrow            = ResolveCssColour(0x8E9F82FF);
    Theme.Subtitle           = ResolveCssColour(0x9AAB90FF);
    Theme.Caption            = ResolveCssColour(0x95A68BFF);
    Theme.TabText            = ResolveCssColour(0x98A88FFF);
    Theme.TabSelectedFill    = ResolveCssColour(0x3D4B34FF);
    Theme.TabSelectedText    = ResolveCssColour(0xE1EACDFF);
    Theme.SliderCapsule      = ResolveCssColour(0x141B15FF);
    Theme.CaptionSize        = 11.0f;
    Theme.CaptionLineHeight  = 1.7f;
    Theme.TitleSize          = 32.0f;
    Theme.TitleGap           = 12.0f;
    Theme.SubtitleSize       = 12.0f;
    Theme.SubtitleLineHeight = 1.7f;
    Theme.SliderMargin       = 19.0f;
    Theme.PaddingTop         = 27;
    Theme.TitleSize          = 32;
    Theme.TitleGap           = 12;
    Theme.TitleLineHeight    = 1.1f;
    Theme.SubtitleSize       = 12;
    Theme.SubtitleGap        = 8;
    Theme.SubtitleLineHeight = 1.7f;
    Theme.SubtitleMaxWidth   = 235;
    Theme.ActionFontSize     = 11;
    Theme.ActionHeight       = 32;
    Theme.ActionPadding      = 16;
    Theme.ActionOffset       = 10.4f;
    return Theme;
}

ControlTheme ControlTheme::ResolveFog() noexcept
{
    ControlTheme Theme       = ResolveNative();
    Theme.Ink                = ResolveCssColour(0xC9D8CFFF);
    Theme.Muted              = ResolveCssColour(0x8C9F95FF);
    Theme.Eyebrow            = Theme.Muted;
    Theme.Subtitle           = ResolveCssColour(0x91A398FF);
    Theme.Caption            = Theme.Muted;
    Theme.Accent             = ResolveCssColour(0xBBCEBCFF);
    Theme.TitleSize          = 30.0f;
    Theme.TitleGap           = 12.0f;
    Theme.TitleLineHeight    = 1.1f;
    Theme.SubtitleSize       = 12.0f;
    Theme.CaptionSize        = 11.0f;
    Theme.CaptionLineHeight  = 1.7f;
    Theme.SliderMargin       = 17.0f;
    Theme.PageFill           = ResolveCssColour(0x101010FF);
    Theme.PaddingTop         = 30;
    Theme.PaddingSide        = 20;
    Theme.TitleSize          = 30;
    Theme.TitleLineHeight    = 1.1f;
    Theme.TitleGap           = 12;
    Theme.SubtitleGap        = 8;
    Theme.SubtitleSize       = 12;
    Theme.SubtitleLineHeight = 1.6f;
    Theme.SubtitleFraction   = 0.75f;
    Theme.ActionFontSize     = 11;
    Theme.ActionHeight       = 32;
    Theme.ActionPadding      = 16;
    Theme.ActionOffset       = 10.4f;
    Theme.ModeFill           = ResolveCssColour(0x282828FF);
    Theme.ModeBorder         = ResolveCssColour(0x3E3E3EFF);
    Theme.ModeText           = ResolveCssColour(0xD4E0D4FF);
    return Theme;
}

ControlTheme ControlTheme::ResolveStars() noexcept
{
    ControlTheme Theme       = ResolveNative();
    Theme.PageFill           = ResolveCssColour(0x15181DFF);
    Theme.Ink                = ResolveCssColour(0xE5E8EEFF);
    Theme.Muted              = ResolveCssColour(0x8E9AAAFF);
    Theme.Accent             = ResolveCssColour(0xBECDE5FF);
    Theme.Eyebrow            = ResolveCssColour(0x7D899BFF);
    Theme.Subtitle           = ResolveCssColour(0x9AA4B2FF);
    Theme.Caption            = ResolveCssColour(0x8D9BAEFF);
    Theme.ControlLabel       = Theme.Muted;
    Theme.SliderNumber       = Theme.Ink;
    Theme.SliderUnit         = Theme.Muted;
    Theme.SliderEmpty        = ResolveCssColour(0xFFFFFF0C);
    Theme.SliderRule         = ResolveCssColour(0xB5C7E311);
    Theme.ThumbRing          = ResolveCssColour(0x26303BFF);
    Theme.TabText            = ResolveCssColour(0x8794A6FF);
    Theme.TabSelectedFill    = ResolveCssColour(0x303740FF);
    Theme.TabSelectedText    = ResolveCssColour(0xE3EBF8FF);
    Theme.TabHoverFill       = ResolveCssColour(0x252C35FF);
    Theme.TabHoverText       = ResolveCssColour(0xDBE5F6FF);
    Theme.TitleGap           = 17.0f;
    Theme.SubtitleLineHeight = 1.5f;
    Theme.TabMargin          = 21.0f;
    Theme.EyebrowTracking    = 1.4f;
    Theme.CapsuleSliders     = false;
    Theme.SliderMargin       = 0.0f;
    return Theme;
}

ControlTheme ControlTheme::ResolveMoon() noexcept
{
    ControlTheme Theme        = ResolveNative();
    Theme.PageFill            = ResolveCssColour(0x191B1EFF);
    Theme.Ink                 = ResolveCssColour(0xE1E5EBFF);
    Theme.Eyebrow             = ResolveCssColour(0x8490A1FF);
    Theme.Subtitle            = ResolveCssColour(0x97A3B3FF);
    Theme.Caption             = ResolveCssColour(0x95A4B7FF);
    Theme.TitleSize           = 29.0f;
    Theme.EyebrowTracking     = 0.5f;
    Theme.SliderCapsule       = ResolveCssColour(0x181D23FF);
    Theme.SliderMargin        = 17.0f;
    Theme.PresetFill          = ResolveCssColour(0x20252BFF);
    Theme.PresetBorder        = ResolveCssColour(0xB5C9E31A);
    Theme.PresetText          = ResolveCssColour(0x9DAFC7FF);
    Theme.PresetPressedFill   = ResolveCssColour(0x45566AFF);
    Theme.PresetPressedText   = ResolveCssColour(0xE2EBF7FF);
    Theme.PresetPressedBorder = ResolveCssColour(0xAEBFD540);
    Theme.SelectFill          = ResolveCssColour(0x282828FF);
    Theme.SelectBorder        = ResolveCssColour(0x3E3E3EFF);
    return Theme;
}

ControlTheme ControlTheme::ResolveOptics() noexcept
{
    ControlTheme Theme       = ResolveNative();
    Theme.Ink                = ResolveCssColour(0xD6D3DBFF);
    Theme.Eyebrow            = ResolveCssColour(0x9993A2FF);
    Theme.Subtitle           = ResolveCssColour(0xA19AA6FF);
    Theme.Caption            = ResolveCssColour(0xA39BA9FF);
    Theme.TabSelectedFill    = ResolveCssColour(0x403846FF);
    Theme.TabSelectedText    = ResolveCssColour(0xEDDFEBFF);
    Theme.SelectFill         = ResolveCssColour(0x16161BFF);
    Theme.SelectText         = ResolveCssColour(0xD8D0DFFF);
    Theme.SubtitleSize       = 12.0f;
    Theme.SubtitleLineHeight = 1.7f;
    Theme.CaptionSize        = 11.0f;
    Theme.CaptionLineHeight  = 1.7f;
    Theme.SliderMargin       = 18.0f;
    Theme.PageFill           = ResolveCssColour(0x101010FF);
    Theme.PaddingTop         = 32;
    Theme.PaddingSide        = 20;
    Theme.TitleGap           = 13;
    Theme.TitleLineHeight    = 1.1f;
    Theme.SubtitleGap        = 9;
    Theme.SubtitleSize       = 12;
    Theme.SubtitleLineHeight = 1.7f;
    Theme.SubtitleMaxWidth   = 260;
    return Theme;
}

} // namespace Frontier
