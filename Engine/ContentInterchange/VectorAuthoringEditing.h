/*============================================================================================================================================
                                                              VECTORAUTHORINGEDITING.H
============================================================================================================================================*/
// 🧩 Shared source-only arrays, enclosure navigation and selected-channel edits for vector and future texture authoring.

#pragma once
#include "VectorAuthoringDocument.h"
#include "VectorAuthoringArrayPlacement.h"

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     ARRAY SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------

struct VectorAuthoringArrayResult
{
    VectorAuthoringDocument Document;
    std::vector<uint32_t>  Selection;
};
//------------------------------------------------------------------------------------------------------------------------
//                                                     AUTHORING OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

bool QueryVectorAuthoringEnclosureContains(const VectorAuthoringDocument &Document, uint32_t BoundaryToken, uint32_t EnclosureToken);
void RelocateVectorAuthoringEnclosure(VectorAuthoringDocument &Document, uint32_t EnclosureToken, uint32_t DestinationToken);
void NormalizeVectorAuthoringCaptions(VectorAuthoringDocument &Document);
bool ApplyVectorAuthoringChannelPatch(VectorAuthoringDocument &Document, std::span<const uint32_t> Selection, const VectorAuthoringChannelPatch &Patch);
VectorAuthoringArrayResult GenerateVectorAuthoringArray(const VectorAuthoringDocument &Document, std::span<const uint32_t> Selection, const VectorAuthoringArraySpecification &Specification);
// 📝 Reflects the selection across a line through (PivotX,PivotY): see VectorAuthoringMirrorSpecification.
VectorAuthoringArrayResult GenerateVectorAuthoringMirror(const VectorAuthoringDocument &Document, std::span<const uint32_t> Selection, const VectorAuthoringMirrorSpecification &Specification);
// 📝 One seeded random position/rotation/scale nudge applied to the whole selection as a rigid group.
VectorAuthoringArrayResult GenerateVectorAuthoringNoise(const VectorAuthoringDocument &Document, std::span<const uint32_t> Selection, const VectorAuthoringNoiseSpecification &Specification);
// 📝 Grows/shrinks the selection's own bounding frame by Distance; see VectorAuthoringOffsetSpecification.
VectorAuthoringArrayResult GenerateVectorAuthoringOffset(const VectorAuthoringDocument &Document, std::span<const uint32_t> Selection, const VectorAuthoringOffsetSpecification &Specification);
// 📝 Resolves AutoGroup into a concrete GroupSize: when Previous is the layer's immediately preceding
// stack entry and it is an Array, matches that Array's own row (Grid) or ring (Radial) population;
// otherwise/when AutoGroup is false the Specification's own GroupSize is kept as given.
VectorAuthoringColourSpecification ResolveVectorAuthoringColourGrouping(VectorAuthoringColourSpecification Specification, const VectorAuthoringModifier *Previous);
// 📝 Paints one flat Colour/StrokeColour per selected element; see VectorAuthoringColourSpecification.
VectorAuthoringArrayResult GenerateVectorAuthoringColour(const VectorAuthoringDocument &Document, std::span<const uint32_t> Selection, const VectorAuthoringColourSpecification &Specification);
// 📝 Single dispatch point shared by ConstructEvaluatedVectorAuthoringDocument (live preview) and the UI's
// "Apply" action (bake), so every call site routes a stack entry's Operation to its matching Generate*
// function. Previous (the layer's immediately preceding Enabled stack entry, if any) only matters for
// Colour's AutoGroup resolution; every other operation ignores it.
VectorAuthoringArrayResult ApplyVectorAuthoringModifier(const VectorAuthoringDocument &Document, std::span<const uint32_t> Selection, const VectorAuthoringModifier &Modifier, const VectorAuthoringModifier *Previous = nullptr);
// 📝 Blender-style modifier evaluation: expands every layer's ORDERED stack of Enabled modifiers into real
// baked copies for RENDER/PREVIEW purposes only - the returned document is ephemeral and must never be
// written back into history/persistence. A layer with no Enabled modifier passes through as-is. Layers
// without any Modifiers at all incur zero cost (the input Document is returned unchanged/uncopied).
VectorAuthoringDocument ConstructEvaluatedVectorAuthoringDocument(const VectorAuthoringDocument &Document);
}
