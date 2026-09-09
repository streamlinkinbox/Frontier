//============================================================================================================================================
//                                                   INTERFACETEXTPROJECTION.H
//============================================================================================================================================
// 🧩 P1 — lays a string out as glyph figures. One figure per visible character, each an InterfaceCategory::Glyph
//    whose ScalarAlpha carries the character code.
//
//    This is deliberately thin. It advances a pen and appends figures; it has no opinion about what the text says,
//    and it does no shaping, kerning or bidirectional reordering. The glyphs themselves are strokes evaluated in
//    the fragment shader (Shaders/InterfaceSignedDistance.slang), so text costs no texture, no upload and no extra
//    draw — the existing batcher already collapses every figure into one instanced draw, and a label is just more
//    figures in that same batch.
//
//    Scope, stated plainly: an analytic stroke font covering A–Z, 0–9 and the punctuation a cluster needs beside a
//    number. It suits labels, units and readouts. It is not a body-text engine, it renders one case, and it cannot
//    load a font you supply. A sampled distance sheet would be the answer to those, and it would cost a sampler
//    binding and a bake step this panel does not otherwise need.

#pragma once

#include "InterfaceStructure.h"

#include <cstdint>
#include <string_view>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    TEXT PLACEMENT
//------------------------------------------------------------------------------------------------------------------------

enum class TextAlignment : uint32_t
{
    Left   = 0u,
    Centre = 1u,
    Right  = 2u
};

struct TextPlacement
{
    float         OriginX      = 0.0f;    // [m]  pen start, in the ancestor's plane
    float         OriginY      = 0.0f;    // [m]
    float         OriginZ      = 0.0f;    // [m]  lift out of the plane, so a label clears its bed
    float         CapHeight    = 0.020f;  // [m]  the em box height — glyphs are authored 0..1 and scaled by this
    float         Advance      = 0.62f;   // [-]  pen step per character, as a fraction of CapHeight
    float         StrokeWidth  = 0.0022f; // [m]  full stroke width; the shader halves it
    TextAlignment Alignment    = TextAlignment::Left;
    PaletteSlot   Palette      = PaletteSlot::Marking;      // [-]  the slot the palette reserves for text and ticks
    uint32_t      TintOverride = 0u;      // [-]  RGBA8; 0 = use the palette slot
    uint32_t      OrderingRank = 0u;      // [-]  carried onto every glyph so a label sorts as one unit
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 INTERFACE TEXT PROJECTION
//------------------------------------------------------------------------------------------------------------------------

class InterfaceTextProjection
{
public:
    // Appends one figure per visible character beneath Ancestor and returns how many were created. Spaces advance
    //    the pen without producing a figure, so a padded string costs nothing extra in the batch.
    static uint32_t Compose(InterfaceStructure& Structure, uint32_t Ancestor,
                            std::string_view Text, const TextPlacement& Placement) noexcept;

    // Width the string will occupy, without creating anything. Used for alignment and for fitting a label into a
    //    housing before committing to it.
    [[nodiscard]] static float MeasureWidth(std::string_view Text, const TextPlacement& Placement) noexcept;
};

} // namespace Frontier
