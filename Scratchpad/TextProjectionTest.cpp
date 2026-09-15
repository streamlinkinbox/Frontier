//============================================================================================================================================
//                                                    TEXTPROJECTIONTEST.CPP
//============================================================================================================================================
// 🧩 The P1 text gate. Compiles the SAME stroke-glyph code the fragment shader runs (GlslShim.h maps the Slang to
//    C++) and rasterises it, so this proves the glyphs actually draw — not merely that the layout arithmetic adds up.
//
//    The distinction matters. A text system can place figures perfectly and render nothing: a glyph table with a
//    wrong branch, an em box mapped inside out, or a stroke width scaled into oblivion all produce correct-looking
//    metrics and a blank panel. So every check below that says "renders" actually counts covered pixels.
//
//    It also writes Diagnostics/SpatialInterface_P1_Text.png so the glyphs can be looked at, because "the pixel
//    count is plausible" is not the same as "that is the letter A".
//
//    Build: bash Scratchpad/CheckTextProjection.sh

#include "GlslShim.h"
#include "/tmp/InterfaceSignedDistance.port.inc"

#include "SpatialInterface/InterfaceTextProjection.h"
#include "SpatialInterface/InterfaceStructure.h"
#include "SpatialInterface/InterfaceSequence.h"

#include "PngWriteShim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int Failures = 0;

void CheckTrue(const char* Name, bool Condition)
{
    std::printf("  %-64s %s\n", Name, Condition ? "PASS" : "FAIL");
    if (!Condition) ++Failures;
}

// Rasterise one glyph into a square grid and report the fraction of covered pixels. This calls the very function
//    the GPU calls, through the shim, so a divergence between proof and shader is impossible by construction.
double GlyphCoverage(uint32_t Code, uint32_t Resolution = 64u, float StrokeHalf = 0.06f)
{
    uint32_t Covered = 0u;
    for (uint32_t Y = 0u; Y < Resolution; ++Y)
        for (uint32_t X = 0u; X < Resolution; ++X)
        {
            const float U = (static_cast<float>(X) + 0.5f) / static_cast<float>(Resolution);
            const float V = (static_cast<float>(Y) + 0.5f) / static_cast<float>(Resolution);
            if (DistanceStrokeGlyph(vec2(U, V), Code, StrokeHalf) <= 0.0f) ++Covered;
        }
    return static_cast<double>(Covered) / static_cast<double>(Resolution * Resolution);
}

} // namespace

int main()
{
    std::printf("\n=== P1 stroke-text gate ===\n\n");

    //──────────────────────────────────────────────────────────────────────────
    // ① Every supported glyph must actually put ink down.
    //──────────────────────────────────────────────────────────────────────────
    const std::string Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const std::string Digits   = "0123456789";
    const std::string Symbols  = ".,:-+/%";

    uint32_t Blank = 0u;
    double   Lowest = 1.0, Highest = 0.0;
    std::string BlankList;
    for (char Character : Alphabet + Digits + Symbols)
    {
        const double Coverage = GlyphCoverage(static_cast<uint32_t>(Character));
        Lowest  = std::min(Lowest,  Coverage);
        Highest = std::max(Highest, Coverage);
        if (Coverage < 0.004) { ++Blank; BlankList += Character; }
    }
    std::printf("  %zu glyphs, coverage %.1f%%..%.1f%%, %u blank%s%s\n",
                Alphabet.size() + Digits.size() + Symbols.size(), Lowest * 100.0, Highest * 100.0,
                Blank, BlankList.empty() ? "" : ": ", BlankList.c_str());
    CheckTrue("every supported glyph renders visible ink", Blank == 0u);

    // A glyph that fills the whole cell is a bug — usually a distance sign error, which reads as a solid block.
    CheckTrue("no glyph floods its cell", Highest < 0.60);

    //──────────────────────────────────────────────────────────────────────────
    // ② Glyphs must be DISTINCT. A table where every branch falls through to the
    //    same strokes still passes a coverage test.
    //──────────────────────────────────────────────────────────────────────────
    {
        const double A = GlyphCoverage('A'), B = GlyphCoverage('B'), I = GlyphCoverage('I');
        const double O = GlyphCoverage('O'), W = GlyphCoverage('W');
        CheckTrue("I is the lightest of I, A, B, O, W", I < A && I < B && I < O && I < W);
        CheckTrue("W carries more ink than I",          W > I * 1.5);
        CheckTrue("A and B differ measurably",          std::fabs(A - B) > 0.005);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ③ Unsupported characters render NOTHING rather than a wrong glyph.
    //──────────────────────────────────────────────────────────────────────────
    CheckTrue("an unmapped character renders nothing", GlyphCoverage('~') < 0.001);
    CheckTrue("a space renders nothing",               GlyphCoverage(' ') < 0.001);

    // Lowercase folds to uppercase rather than vanishing.
    CheckTrue("lowercase folds to uppercase",
              std::fabs(GlyphCoverage('a') - GlyphCoverage('A')) < 1.0e-9);

    //──────────────────────────────────────────────────────────────────────────
    // ④ Stroke width behaves — thicker ink means more of it.
    //──────────────────────────────────────────────────────────────────────────
    {
        const double Thin  = GlyphCoverage('H', 64u, 0.03f);
        const double Thick = GlyphCoverage('H', 64u, 0.09f);
        CheckTrue("a thicker stroke covers more", Thick > Thin * 1.5);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ⑤ Layout: figures, positions and alignment.
    //──────────────────────────────────────────────────────────────────────────
    {
        Frontier::InterfaceStructure Structure;
        Frontier::InterfaceFigure Housing;
        Housing.HalfWidth = 0.20f; Housing.HalfHeight = 0.06f;
        const uint32_t Panel = Structure.Construct(Housing);

        Frontier::TextPlacement Placement;
        Placement.CapHeight = 0.02f;

        const uint32_t Made = Frontier::InterfaceTextProjection::Compose(Structure, Panel, "AB CD", Placement);
        CheckTrue("a space costs no figure", Made == 4u);
        CheckTrue("one figure per visible character", Structure.QueryCount() == 5u);

        // The glyphs must carry the character code, not an ordinal.
        const Frontier::InterfaceFigure& First = Structure.Query(1u);
        CheckTrue("the first glyph carries 'A'", static_cast<int>(First.ScalarAlpha) == static_cast<int>('A'));
        CheckTrue("the glyph uses the Glyph category", First.Category == Frontier::InterfaceCategory::Glyph);

        // Advance: the second glyph sits one step right of the first, and the space leaves a gap.
        const Frontier::InterfaceFigure& Second = Structure.Query(2u);
        const Frontier::InterfaceFigure& Third  = Structure.Query(3u);
        const float Step = Placement.CapHeight * Placement.Advance;
        CheckTrue("glyphs advance by one step",
                  std::fabs((Second.Placement.Origin.X - First.Placement.Origin.X) - Step) < 1.0e-5f);
        CheckTrue("a space leaves a two-step gap",
                  std::fabs((Third.Placement.Origin.X - Second.Placement.Origin.X) - Step * 2.0f) < 1.0e-5f);
    }

    {
        // Alignment must move the run, and centred text must straddle the origin.
        Frontier::InterfaceStructure Left, Centre, Right;
        Frontier::TextPlacement Placement;
        Placement.CapHeight = 0.02f;
        (void)Frontier::InterfaceTextProjection::Compose(Left, Frontier::InterfaceStructure::Detached, "MPH", Placement);
        Placement.Alignment = Frontier::TextAlignment::Centre;
        (void)Frontier::InterfaceTextProjection::Compose(Centre, Frontier::InterfaceStructure::Detached, "MPH", Placement);
        Placement.Alignment = Frontier::TextAlignment::Right;
        (void)Frontier::InterfaceTextProjection::Compose(Right, Frontier::InterfaceStructure::Detached, "MPH", Placement);

        const float LeftFirst   = Left.Query(0u).Placement.Origin.X;
        const float CentreFirst = Centre.Query(0u).Placement.Origin.X;
        const float RightFirst  = Right.Query(0u).Placement.Origin.X;
        CheckTrue("centred text starts left of left-aligned", CentreFirst < LeftFirst);
        CheckTrue("right-aligned starts furthest left",       RightFirst  < CentreFirst);

        const float Width = Frontier::InterfaceTextProjection::MeasureWidth("MPH", Placement);
        CheckTrue("measured width is three steps",
                  std::fabs(Width - Placement.CapHeight * Placement.Advance * 3.0f) < 1.0e-6f);
        CheckTrue("an empty string measures zero",
                  Frontier::InterfaceTextProjection::MeasureWidth("", Placement) == 0.0f);
        // Centred text straddles the origin: half the run to each side.
        CheckTrue("centred text straddles the origin", std::fabs(CentreFirst + Width * 0.5f
                                                                 - Placement.CapHeight * Placement.Advance * 0.5f) < 1.0e-5f);
    }

    //──────────────────────────────────────────────────────────────────────────
    // ⑥ Text costs no extra draw — the whole point of composing into the batch.
    //──────────────────────────────────────────────────────────────────────────
    {
        Frontier::InterfaceStructure Structure;
        Frontier::InterfaceFigure Housing;
        Housing.HalfWidth = 0.25f; Housing.HalfHeight = 0.08f;
        const uint32_t Panel = Structure.Construct(Housing);

        Frontier::TextPlacement Placement;
        Placement.CapHeight = 0.018f;
        (void)Frontier::InterfaceTextProjection::Compose(Structure, Panel, "SPEED 120 KMH", Placement);

        Frontier::InterfaceSequence Composition;
        Frontier::InterfaceViewConfiguration View;
        View.EyeZ = 1.0f; View.ForwardY = 1.0f;
        Composition.AssignView(View);
        Composition.Advance(Structure, 0.0);

        std::printf("  composed 'SPEED 120 KMH': %u figures -> %u instances, %u draw(s)\n",
                    Structure.QueryCount(), Composition.QueryInstanceCount(),
                    Composition.QueryMetrics().DrawCount);
        CheckTrue("a whole label still costs one draw", Composition.QueryMetrics().DrawCount == 1u);
        CheckTrue("every glyph reached the batch",      Composition.QueryInstanceCount() == Structure.QueryCount());
    }

    //──────────────────────────────────────────────────────────────────────────
    // ⑦ Write a sheet so the glyphs can be inspected by eye.
    //──────────────────────────────────────────────────────────────────────────
    {
        const std::string Sheet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,:-+/%";
        const uint32_t Cell = 48u, Columns = 11u;
        const uint32_t Rows = (static_cast<uint32_t>(Sheet.size()) + Columns - 1u) / Columns;
        const uint32_t Width = Cell * Columns, Height = Cell * Rows;
        std::vector<unsigned char> Pixels(static_cast<size_t>(Width) * Height * 3u, 18u);

        for (uint32_t Index = 0u; Index < Sheet.size(); ++Index)
        {
            const uint32_t Column = Index % Columns, Row = Index / Columns;
            for (uint32_t Y = 0u; Y < Cell; ++Y)
                for (uint32_t X = 0u; X < Cell; ++X)
                {
                    const float U = (static_cast<float>(X) + 0.5f) / static_cast<float>(Cell);
                    const float V = 1.0f - (static_cast<float>(Y) + 0.5f) / static_cast<float>(Cell);
                    const float D = DistanceStrokeGlyph(vec2(U, V),
                                                        static_cast<uint32_t>(Sheet[Index]), 0.055f);
                    const float Coverage = std::clamp(0.5f - D / 0.03f, 0.0f, 1.0f);
                    const unsigned char Value = static_cast<unsigned char>(18.0f + 220.0f * Coverage);
                    const size_t Offset = ((static_cast<size_t>(Row) * Cell + Y) * Width
                                        +  (static_cast<size_t>(Column) * Cell + X)) * 3u;
                    Pixels[Offset + 0u] = Value;
                    Pixels[Offset + 1u] = Value;
                    Pixels[Offset + 2u] = Value;
                }
        }
        const bool Written = PngWriteShim::WritePng("Diagnostics/SpatialInterface_P1_Text.png",
                                      static_cast<int>(Width), static_cast<int>(Height), 3,
                                      Pixels.data(), static_cast<int>(Width) * 3) != 0;
        CheckTrue("a glyph sheet is written for inspection", Written);
        if (Written) std::printf("  wrote Diagnostics/SpatialInterface_P1_Text.png (%u x %u)\n", Width, Height);
    }

    std::printf(Failures ? "\n>>> %d FAILURE(S)\n\n" : "\n>>> ALL PASS (0 failures)\n\n", Failures);
    return Failures ? 1 : 0;
}
