/*============================================================================================================================================
                                                        PARTICLEPANEAPPEARANCE.H
============================================================================================================================================*/
// 🎨 Theme-synchronized visual tokens & helper formatting utilities for ParticleWorkspace panels

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/PixelSpace.h"

#include <algorithm>
#include <cstdint>

namespace Frontier::Workspaces::ParticleAppearance
{

struct ParticleThemeColors
{
    ColorRgba CanvasBg       {  11u,  12u,  16u, 255u };
    ColorRgba PanelBg        {  16u,  17u,  22u, 255u };
    ColorRgba CardBg         {  20u,  22u,  28u, 255u };
    ColorRgba InnerTileBg    {  26u,  28u,  36u, 255u };
    ColorRgba HoverBg        {  36u,  39u,  49u, 255u };
    ColorRgba WarmCreamBg    { 242u, 241u, 220u, 255u };
    ColorRgba WarmCreamInk   {  20u,  21u,  25u, 255u };
    ColorRgba BorderSubtle   {  44u,  47u,  58u, 220u };
    ColorRgba BorderStrong   {  68u,  73u,  88u, 240u };
    ColorRgba TextPrimary    { 235u, 238u, 245u, 255u };
    ColorRgba TextMuted      { 148u, 154u, 168u, 255u };
    ColorRgba TextFaint      { 102u, 108u, 122u, 255u };
    ColorRgba EmeraldInk     {  58u, 214u, 138u, 255u };
    ColorRgba EmeraldSoft    {  24u,  54u,  42u, 255u };
    ColorRgba AmberInk       { 234u, 166u,  68u, 255u };
    ColorRgba AmberSoft      {  58u,  42u,  24u, 255u };
    ColorRgba ParticleAccent { 255u, 138u,  76u, 255u };
    ColorRgba ForceAccent    {  70u, 210u, 103u, 255u };
    ColorRgba CyanAccent     {  92u, 198u, 242u, 255u };
};

[[nodiscard]] inline ParticleThemeColors QueryThemeColors() noexcept
{
    const ControlColorSet& C = ControlAppearance::Colors();
    ParticleThemeColors Out{};
    if (C.LightSurface)
    {
        Out.CanvasBg     = C.Panel.ToRgba8();
        Out.PanelBg      = WorkspaceAppearance::QueryDockedPaneBackground();
        Out.CardBg       = C.SectionSurface.ToRgba8();
        Out.InnerTileBg  = C.Field.ToRgba8();
        Out.HoverBg      = C.Selected.ToRgba8();
        Out.BorderSubtle = C.Stroke.ToRgba8();
        Out.BorderStrong = C.StrokeStrong.ToRgba8();
        Out.TextPrimary  = C.Text.ToRgba8();
        Out.TextMuted    = C.TextDim.ToRgba8();
        Out.WarmCreamBg  = C.Accent.ToRgba8();
        Out.WarmCreamInk = C.AccentInk.ToRgba8();
    }
    return Out;
}

} // namespace Frontier::Workspaces::ParticleAppearance
