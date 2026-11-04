/*==========================================================================================================================================
                                                                 GLYPHRUN.H
==========================================================================================================================================*/
// 🧩 Glyph runs on CSS baselines and wrapped paragraph blocks. ImGui (stb_truetype) sizes a face by ascent − descent,
//    CSS by the em: DM Sans hhea 992 / −310 over 1000 units, so one em is 1.302 ImGui pixel heights.

#pragma once

#include <imgui.h>

namespace Frontier
{

class ControlColumn;
struct EditorTypefaces;

//------------------------------------------------------------------------------------------------------------------------
//                                                    GLYPH RUN
//------------------------------------------------------------------------------------------------------------------------

class GlyphRun
{
  public:
    static constexpr float EmPixelHeight = 1.302f;   // [-] ImGui pixel height per CSS em
    static constexpr float EmAscent      = 0.992f;   // [-] baseline depth below the content-area top, per em

    [[nodiscard]] static ImFont* ResolveFace(ImFont* Face) noexcept;
    // 📝 The baseline of a CSS line box: content area (1.302 em) centred in LineHeight × Size, ascent below its top.
    [[nodiscard]] static float   ResolveBaseline(float Top, float Size, float LineHeight) noexcept;

    // 📝 A run of glyphs on a CSS baseline (font-size Size, letter-spacing Tracking); code points the face lacks (↗, λ, ≈)
    //    fall back to the monospace face. Returns the advance; Measure only measures (DrawList may be null).
    static float Rasterize(ImDrawList* DrawList, const EditorTypefaces* Faces, ImFont* Face, float Size, ImVec2 Baseline, ImU32 Colour,
                           const char* Text, float Tracking, bool Measure);
    static float Construct(const ControlColumn& Column, ImFont* Face, float Size, ImVec2 Baseline, ImU32 Colour, const char* Text,
                           float Tracking = 0.0f);
    [[nodiscard]] static float Measure(const ControlColumn& Column, ImFont* Face, float Size, const char* Text, float Tracking = 0.0f);
};

// 📝 Wrapped paragraph lines (font-size Size, line-height LineHeight × Size) from Min; returns the height.
class ParagraphBlock
{
  public:
    static float Construct(const ControlColumn& Column, ImFont* Face, float Size, float LineHeight, ImVec2 Min, float WrapWidth, ImU32 Colour,
                           const char* Text, bool Measure = false);
};

} // namespace Frontier
