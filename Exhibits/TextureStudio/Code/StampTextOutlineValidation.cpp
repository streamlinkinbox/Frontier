/*============================================================================================================================================
                                                      STAMPTEXTOUTLINEVALIDATION.CPP
============================================================================================================================================*/
// 🧩 A Stamp text component resolves to real glyph-outline geometry, round-trips through TOML, and
//    keeps the Stamp-only component contract (Text/ImportedVector/ImportedImage require Document.Stamp).

#include "Engine/ContentInterchange/StampTextOutline.h"
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
}

int main()
{
    using namespace Frontier;
    try
    {
        VectorAuthoringDocument Document;
        Document.Stamp = true;
        Document.TokenClock = 1;
        const uint32_t InstructionIndex = AppendStampTextOutline(Document, "Stamp", 180.f, {.1f, .8f, .3f, 1});
        Require(InstructionIndex < Document.Instructions.size(), "Text outline instruction index is out of range.");
        const auto &Instruction = Document.Instructions[InstructionIndex];
        Require(Instruction.Topology[0] == 6, "Text outline must author a CubicContour instruction.");
        Require(Instruction.Topology[2] > 0, "Text outline authored no segments.");
        Require((Instruction.Topology[3] & 1) == 0, "Text outline must use the nonzero fill rule, matching TrueType contours.");
        Require(Instruction.Paint[3] == 1, "Text outline must be filled.");
        Require(Instruction.Bounds[2] > 0 && Instruction.Bounds[3] > 0, "Text outline bounds must be a positive extent.");
        Require(Instruction.Colour[1] == .8f, "Text outline did not retain its authored colour.");

        // 📝 Every sub-contour this module authors must close back onto its own starting point —
        // the nonzero winding walk the shader performs assumes a closed edge loop per contour.
        const uint32_t Start = Instruction.Topology[1], Count = Instruction.Topology[2];
        std::array<float, 2> ContourOrigin{Document.Segments[Start].StartControl[0], Document.Segments[Start].StartControl[1]};
        std::array<float, 2> Cursor = ContourOrigin;
        uint32_t ClosedContours = 0;
        for (uint32_t Index = 0; Index < Count; ++Index)
        {
            const auto &Segment = Document.Segments[Start + Index];
            Require(Segment.StartControl[0] == Cursor[0] && Segment.StartControl[1] == Cursor[1], "Text outline segments are not contiguous within a contour.");
            Cursor = {Segment.ControlEnd[2], Segment.ControlEnd[3]};
            if (Cursor[0] == ContourOrigin[0] && Cursor[1] == ContourOrigin[1])
            {
                ++ClosedContours;
                if (Index + 1 < Count) ContourOrigin = Cursor = {Document.Segments[Start + Index + 1].StartControl[0], Document.Segments[Start + Index + 1].StartControl[1]};
            }
        }
        Require(ClosedContours >= 1, "Text outline authored no closed contour.");

        VectorAuthoringLayer Layer;
        Layer.Token = ++Document.TokenClock;
        Layer.Root = InstructionIndex;
        Layer.Name = "Stamp";
        Layer.ComponentKind = VectorAuthoringComponentKind::Text;
        Layer.SourceMarkup = "Stamp";
        Document.Layers.push_back(Layer);
        (void)ConstructVectorAuthoringCoverage(Document); // Throws on any structural inconsistency

        const auto Encoded = EncodeVectorAuthoring(Document);
        const auto Decoded = DecodeVectorAuthoring(Encoded);
        Require(Decoded.Layers.size() == 1 && Decoded.Layers[0].ComponentKind == VectorAuthoringComponentKind::Text, "Stamp component kind did not round-trip through TOML.");
        Require(Decoded.Layers[0].SourceMarkup == "Stamp", "Stamp text source markup did not round-trip through TOML.");
        Require(Decoded.Instructions[Decoded.Layers[0].Root].Topology[0] == 6, "Decoded Stamp text layer does not reference its cubic contour.");

        RequireThrows([] { VectorAuthoringDocument Empty; (void)AppendStampTextOutline(Empty, "", 100.f, {1, 1, 1, 1}); }, "Empty Stamp text must be refused.");
        RequireThrows([] { VectorAuthoringDocument Empty; (void)AppendStampTextOutline(Empty, "   ", 100.f, {1, 1, 1, 1}); }, "Whitespace-only Stamp text must be refused.");
        RequireThrows([] { VectorAuthoringDocument Empty; (void)AppendStampTextOutline(Empty, "Stamp", -1.f, {1, 1, 1, 1}); }, "Non-positive point size must be refused.");
        RequireThrows([] { VectorAuthoringDocument Empty; (void)AppendStampTextOutline(Empty, std::string(StampTextCharacterLimit + 1, 'A'), 100.f, {1, 1, 1, 1}); }, "Oversized Stamp text must be refused.");

        // 📝 Text/ImportedVector/ImportedImage are a Stamp-only contract: an ordinary Pattern document
        // (Stamp == false) must refuse a non-Freeform component kind even if its geometry is valid.
        RequireThrows([&Instruction] {
            VectorAuthoringDocument Pattern;
            Pattern.Stamp = false;
            Pattern.TokenClock = 1;
            Pattern.Instructions.push_back(Instruction);
            VectorAuthoringLayer Mislabelled;
            Mislabelled.Token = 1; Mislabelled.Root = 0; Mislabelled.ComponentKind = VectorAuthoringComponentKind::Text;
            Pattern.Layers.push_back(Mislabelled);
            (void)ConstructVectorAuthoringCoverage(Pattern);
        }, "A non-Stamp document must refuse Text/ImportedVector/ImportedImage component kinds.");

        std::cout << "StampTextOutlineValidation: glyph contours=" << ClosedContours << ", segments=" << Count << "\n";
        return 0;
    }
    catch (const std::exception &Error)
    {
        std::cerr << "StampTextOutlineValidation failed: " << Error.what() << "\n";
        return 1;
    }
}
