/*==========================================================================================================================================
                                                 LAYERDRAGREORDERVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Pure, headless proof for ReorderVectorAuthoringLayerByDrag - the shared logic behind the Pattern and
// Stamp Layers panel drag-and-drop row reorder. No ImGui/GPU required: this is plain VectorAuthoringDocument
// value-semantics, identical to what EvaluateEdit applies from the real drag-drop handlers.
//
// 📝 The pre-existing logic always inserted the dragged layer *before* the row it was dropped onto. That
// meant a layer dropped onto the very last row could never actually become last (it landed second-to-last),
// and with exactly two layers, dragging the top one onto the bottom one was a silent no-op - drag the top
// layer to the bottom, see no change. This reproduces that exact user action (and several related ones) on
// a real VectorAuthoringDocument and confirms the fixed behaviour.

#include "Engine/ContentInterchange/VectorAuthoringSelectionEditing.h"
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Frontier;
void Require(bool Condition, const char *Message)
{
    if (!Condition)
        throw std::runtime_error(Message);
}
std::vector<uint32_t> Tokens(const VectorAuthoringDocument &Document)
{
    std::vector<uint32_t> Result;
    for (const auto &Layer : Document.Layers) Result.push_back(Layer.Token);
    return Result;
}
VectorAuthoringDocument ConstructLayers(int Count)
{
    VectorAuthoringDocument Document;
    VectorCoverageInstruction Primitive;
    Primitive.Bounds = {256, 256, 64, 64};
    for (int Index = 0; Index < Count; ++Index)
        (void)ConstructVectorAuthoringPrimitive(Document, Primitive, "Layer " + std::to_string(Index));
    return Document;
}
} // namespace

int main()
{
    try
    {
        // Dragging the top layer onto the bottom layer of a two-layer document used to be a no-op; it must
        // now swap them (top becomes last).
        {
            auto Document = ConstructLayers(2);
            const auto Original = Tokens(Document);
            ReorderVectorAuthoringLayerByDrag(Document, Original[0], Original[1]);
            Require(Tokens(Document) == std::vector<uint32_t>{Original[1], Original[0]},
                    "Dragging the top layer onto the bottom layer of a two-layer document did not move it to the bottom");
        }
        // The same action on a longer list: dragging the very top layer onto the very bottom layer must
        // put it last, not second-to-last.
        {
            auto Document = ConstructLayers(4);
            const auto Original = Tokens(Document); // [A, B, C, D]
            ReorderVectorAuthoringLayerByDrag(Document, Original[0], Original[3]);
            Require(Tokens(Document) == std::vector<uint32_t>{Original[1], Original[2], Original[3], Original[0]},
                    "Dragging the top layer onto the bottom row of a four-layer document did not land it at the bottom");
        }
        // Dragging downward onto a middle row still lands the layer immediately after that row (not before
        // it), matching "drop it here, below the row I dropped onto" for a downward drag.
        {
            auto Document = ConstructLayers(4);
            const auto Original = Tokens(Document); // [A, B, C, D]
            ReorderVectorAuthoringLayerByDrag(Document, Original[0], Original[2]);
            Require(Tokens(Document) == std::vector<uint32_t>{Original[1], Original[2], Original[0], Original[3]},
                    "Dragging a layer downward onto a middle row did not land it just after that row");
        }
        // Dragging *upward* still inserts before the destination row (unchanged, pre-existing behaviour for
        // the direction that was never broken).
        {
            auto Document = ConstructLayers(4);
            const auto Original = Tokens(Document); // [A, B, C, D]
            ReorderVectorAuthoringLayerByDrag(Document, Original[3], Original[1]);
            Require(Tokens(Document) == std::vector<uint32_t>{Original[0], Original[3], Original[1], Original[2]},
                    "Dragging a layer upward onto a row did not land it just before that row");
        }
        // Dropping a layer onto itself must be a true no-op.
        {
            auto Document = ConstructLayers(3);
            const auto Original = Tokens(Document);
            ReorderVectorAuthoringLayerByDrag(Document, Original[1], Original[1]);
            Require(Tokens(Document) == Original, "Dropping a layer onto itself changed the order");
        }
        // A locked layer must not be moved by a drag.
        {
            auto Document = ConstructLayers(3);
            const auto Original = Tokens(Document);
            Document.Layers[0].Locked = true;
            ReorderVectorAuthoringLayerByDrag(Document, Original[0], Original[2]);
            Require(Tokens(Document) == Original, "A locked layer was moved by a drag");
        }
        // Unknown source/destination tokens must not corrupt the document.
        {
            auto Document = ConstructLayers(3);
            const auto Original = Tokens(Document);
            ReorderVectorAuthoringLayerByDrag(Document, 999999, Original[0]);
            ReorderVectorAuthoringLayerByDrag(Document, Original[0], 999999);
            Require(Tokens(Document) == Original, "An unknown source or destination token changed the order");
        }
        std::cout << "PASS: dragging the top layer onto the bottom row now actually moves it to the bottom "
                     "(and every other drag direction/edge case behaves correctly) for both the two-layer "
                     "no-op case and longer lists.\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << '\n';
        return 1;
    }
}
