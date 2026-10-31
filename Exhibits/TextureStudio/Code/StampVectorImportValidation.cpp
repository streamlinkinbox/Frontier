/*============================================================================================================================================
                                                     STAMPVECTORIMPORTVALIDATION.CPP
============================================================================================================================================*/
// 🧩 A Stamp ImportedVector component resolves imported SVG markup to real cubic-path geometry via
//    ThorVG, keeps each shape's own colour/fill-rule/ancestor-transform, round-trips through TOML,
//    and keeps the Stamp-only component contract (Text/ImportedVector/ImportedImage require Document.Stamp).

#include "Engine/ContentInterchange/StampVectorImport.h"
#include "Engine/ContentInterchange/VectorAuthoringDocument.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool Condition, const char *Message)
{
    if (!Condition) throw std::runtime_error(Message);
}
template <typename Callable> void RequireThrows(Callable &&Body, const char *Message)
{
    try { Body(); } catch (const std::exception &) { return; }
    throw std::runtime_error(Message);
}
constexpr const char *TwoShapeSvg =
    "<svg xmlns='http://www.w3.org/2000/svg' width='100' height='100' viewBox='0 0 100 100'>"
    "<g transform='translate(50,50) rotate(45)'><rect x='0' y='0' width='40' height='40' fill='#3366ff'/></g>"
    "<path d='M 10 10 L 190 10 L 100 190 Z' fill='#ffaa00' fill-rule='evenodd'/>"
    "</svg>";
}

int main()
{
    using namespace Frontier;
    try
    {
        VectorAuthoringDocument Document;
        Document.Stamp = true;
        Document.TokenClock = 1;
        const uint32_t RootIndex = AppendStampVectorImport(Document, TwoShapeSvg);
        Require(RootIndex < Document.Instructions.size(), "Vector import root index is out of range.");

        // 📝 Two distinctly-coloured shapes must combine with the Over category (28), not a Boolean
        // category, so each shape keeps its own colour instead of collapsing to one silhouette.
        const auto &Root = Document.Instructions[RootIndex];
        Require(Root.Topology[0] == 28, "Multi-shape import must chain shapes with the Over category.");
        const auto &First = Document.Instructions[Root.Topology[1]];
        const auto &Second = Document.Instructions[Root.Topology[2]];
        Require(First.Topology[0] == 6 && Second.Topology[0] == 6, "Over operands must be CubicContour leaves.");

        // 📝 The first shape sits inside a rotated+translated <g>; the ancestor chain must be
        // composed (not ignored), so its bounds reflect a 40x40 square rotated 45 degrees in place.
        Require(std::abs(First.Bounds[2] - 56.57f) < .1f && std::abs(First.Bounds[3] - 56.57f) < .1f,
                "Imported shape ignored its ancestor <g transform>.");
        Require(First.Colour[2] > .9f && First.Colour[0] < .3f, "Imported shape lost its solid fill colour.");

        // 📝 The second shape is an evenodd-filled path; FillRule must map straight through to Topology[3] bit0.
        Require((Second.Topology[3] & 1) == 1, "Imported evenodd fill rule did not propagate.");
        Require(Second.Colour[0] > .9f && Second.Colour[1] > .5f, "Imported path lost its solid fill colour.");

        VectorAuthoringLayer Layer;
        Layer.Token = ++Document.TokenClock;
        Layer.Root = RootIndex;
        Layer.Name = "Imported";
        Layer.ComponentKind = VectorAuthoringComponentKind::ImportedVector;
        Layer.SourceMarkup = TwoShapeSvg;
        Document.Layers.push_back(Layer);
        (void)ConstructVectorAuthoringCoverage(Document); // Throws on any structural inconsistency, exercises the
                                                           // same recursive composite-bounds walk the selection
                                                           // hitbox uses, confirming the Over chain is compatible.

        const auto Encoded = EncodeVectorAuthoring(Document);
        const auto Decoded = DecodeVectorAuthoring(Encoded);
        Require(Decoded.Layers.size() == 1 && Decoded.Layers[0].ComponentKind == VectorAuthoringComponentKind::ImportedVector,
                "Stamp component kind did not round-trip through TOML.");
        Require(Decoded.Layers[0].SourceMarkup == TwoShapeSvg, "Stamp imported SVG markup did not round-trip through TOML.");
        Require(Decoded.Instructions[Decoded.Layers[0].Root].Topology[0] == 28, "Decoded Stamp vector layer lost its Over chain.");

        // 📝 A fully transparent shape contributes no visible ink and must be dropped rather than
        // authored as an invisible instruction; a gradient-filled shape must still import (falls
        // back to a neutral colour) rather than being silently discarded as if it had no paint.
        {
            VectorAuthoringDocument Single;
            Single.Stamp = true;
            const char *TransparentAndGreen =
                "<svg xmlns='http://www.w3.org/2000/svg' width='20' height='20' viewBox='0 0 20 20'>"
                "<rect x='0' y='0' width='20' height='20' fill='#ff0000' fill-opacity='0'/>"
                "<circle cx='10' cy='10' r='5' fill='#00ff00'/></svg>";
            const uint32_t OnlyShape = AppendStampVectorImport(Single, TransparentAndGreen);
            Require(Single.Instructions[OnlyShape].Topology[0] == 6, "A single surviving shape must not be wrapped in an Over node.");
            Require(Single.Instructions[OnlyShape].Colour[1] > .9f, "Transparent shape leaked into the import; only the green circle should remain.");
        }
        {
            VectorAuthoringDocument Gradient;
            Gradient.Stamp = true;
            const char *GradientSvg =
                "<svg xmlns='http://www.w3.org/2000/svg' width='50' height='50' viewBox='0 0 50 50'>"
                "<defs><linearGradient id='g' x1='0' y1='0' x2='50' y2='0'><stop offset='0' stop-color='#fff'/><stop offset='1' stop-color='#000'/></linearGradient></defs>"
                "<rect x='0' y='0' width='50' height='50' fill='url(#g)'/></svg>";
            const uint32_t GradientShape = AppendStampVectorImport(Gradient, GradientSvg);
            Require(Gradient.Instructions[GradientShape].Topology[0] == 6, "A gradient-filled shape must import as a CubicContour.");
            Require(Gradient.Instructions[GradientShape].Transform[3] == 1.f &&
                Gradient.Instructions[GradientShape].Colour[0] > .95f &&
                Gradient.Instructions[GradientShape].ColourEnd[0] < .05f,
                "Linear SVG gradient stops and category did not propagate into the imported instruction.");
            Gradient.Layers.push_back({1, GradientShape});
            Gradient.TokenClock = 1;
            (void)ConstructVectorAuthoringCoverage(Gradient);
        }
        {
            VectorAuthoringDocument Stroked;
            Stroked.Stamp = true;
            const char *StrokedSvg =
                "<svg xmlns='http://www.w3.org/2000/svg' width='100' height='100' viewBox='0 0 100 100'>"
                "<g opacity='0.5'><path d='M 15 25 L 85 75' fill='none' stroke='#ff5500' stroke-width='6'/></g></svg>";
            const uint32_t StrokedShape = AppendStampVectorImport(Stroked, StrokedSvg);
            const auto &Inst = Stroked.Instructions[StrokedShape];
            Require(Inst.Topology[0] == 6 && Inst.Paint[3] == 0.f && Inst.Transform[1] > 5.5f,
                "Stroke-only open SVG path was dropped or force-filled.");
            Require(std::abs(Inst.Transform[2] - (128.f / 255.f)) < .02f && Inst.StrokeColour[0] > .95f,
                "Group opacity or stroke colour did not propagate onto stroked SVG import.");
            Stroked.Layers.push_back({1, StrokedShape});
            Stroked.TokenClock = 1;
            (void)ConstructVectorAuthoringCoverage(Stroked);
        }

        RequireThrows([] { VectorAuthoringDocument Empty; (void)AppendStampVectorImport(Empty, ""); }, "Empty SVG markup must be refused.");
        RequireThrows([] { VectorAuthoringDocument Empty; (void)AppendStampVectorImport(Empty, "not svg at all {{{"); }, "Unparseable markup must be refused.");
        RequireThrows([] { VectorAuthoringDocument Empty; (void)AppendStampVectorImport(Empty, "<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'><defs></defs></svg>"); },
                      "An SVG with no renderable shape must be refused.");
        RequireThrows([] { VectorAuthoringDocument Empty; (void)AppendStampVectorImport(Empty, std::string(StampVectorMarkupLimit + 1, 'A')); }, "Oversized SVG markup must be refused.");

        // 📝 Text/ImportedVector/ImportedImage are a Stamp-only contract: an ordinary Pattern document
        // (Stamp == false) must refuse a non-Freeform component kind even if its geometry is valid.
        RequireThrows([&First] {
            VectorAuthoringDocument Pattern;
            Pattern.Stamp = false;
            Pattern.TokenClock = 1;
            Pattern.Instructions.push_back(First);
            VectorAuthoringLayer Mislabelled;
            Mislabelled.Token = 1; Mislabelled.Root = 0; Mislabelled.ComponentKind = VectorAuthoringComponentKind::ImportedVector;
            Pattern.Layers.push_back(Mislabelled);
            (void)ConstructVectorAuthoringCoverage(Pattern);
        }, "A non-Stamp document must refuse Text/ImportedVector/ImportedImage component kinds.");

        std::cout << "StampVectorImportValidation: instructions=" << Document.Instructions.size()
                   << ", segments=" << Document.Segments.size() << "\n";
        return 0;
    }
    catch (const std::exception &Error)
    {
        std::cerr << "StampVectorImportValidation failed: " << Error.what() << "\n";
        return 1;
    }
}
