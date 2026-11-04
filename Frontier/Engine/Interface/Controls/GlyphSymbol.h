/*==========================================================================================================================================
                                                               GLYPHSYMBOL.H
==========================================================================================================================================*/
// 🧩 The demo's glyphs (EditorIcons.js ConstructGlyph): its lucide strokes drawn from their own path data at any
//    size and stroke width, and its Native* environment artwork drawn from the ThorVG icon atlas.

#pragma once

#include <imgui.h>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    GLYPH SYMBOL
//------------------------------------------------------------------------------------------------------------------------

class GlyphSymbol
{
  public:
    // 📝 Name is the demo's glyph name ("Activity", "EyeOff", "NativeSun" …). Strokes use the 24-unit lucide box at Min
    //    with Size pixels and StrokeWidth units; artwork fills the same box at Colour's alpha. False when Name is unknown.
    static bool Construct(ImDrawList* DrawList, const char* Name, ImVec2 Min, float Size, float StrokeWidth, ImU32 Colour);
    // 📝 True for the Native* environment artwork (the outliner scales it 1.5×: .tree-row .native-glyph).
    [[nodiscard]] static bool QueryArtwork(const char* Name) noexcept;
};

} // namespace Frontier
