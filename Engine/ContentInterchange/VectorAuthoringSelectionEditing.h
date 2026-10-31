/*============================================================================================================================================
                                                        VECTORAUTHORINGSELECTIONEDITING.H
============================================================================================================================================*/
// 🧩 Shared atomic source edits for native vector selection, transforms, paint, Boolean expressions and reclamation.

#pragma once
#include "VectorAuthoringEditing.h"

namespace Frontier
{
struct VectorAuthoringSourcePatch
{
    float TranslationX = 0;
    float TranslationY = 0;
    float ScaleX       = 1;
    float ScaleY       = 1;
    float Rotation     = 0; // [rad]
    float ScaleBasis   = 0; // [rad] - Axes in which independent scale factors apply.
    std::optional<std::array<float, 4>> Colour;
    std::optional<std::array<float, 4>> ColourEnd;
    std::optional<std::array<float, 4>> GradientStopPositions;
    std::optional<std::array<float, 4>> GradientStopColour2;
    std::optional<std::array<float, 4>> GradientStopColour3;
    std::optional<std::array<float, 4>> GradientAxis;
    std::optional<std::array<float, 4>> StrokeColour;
    std::optional<std::array<float, 4>> StrokeColourEnd, StrokeGradientStopPositions, StrokeGradientStopColour2, StrokeGradientStopColour3, StrokeGradientAxis;
    std::optional<uint32_t> StrokePaintCategory;
    std::optional<uint32_t> GradientInterpolation;
    std::optional<uint32_t> StrokeGradientInterpolation;
    std::optional<std::array<float, 4>> FreeformPointPositions01, FreeformPointPositions23, FreeformPointPositions45, FreeformPointPositions67;
    std::optional<std::array<float, 4>> FreeformPointColour4, FreeformPointColour5, FreeformPointColour6, FreeformPointColour7;
    std::optional<std::array<float, 4>> FreeformParameters;
    std::optional<float> Opacity;
    std::optional<float> StrokeWidth;
    std::optional<uint32_t> PaintCategory;
    std::optional<bool> FillEnabled;
    // 📝 Regular-polygon/star corner count (Paint[2], clamped [3,35] by the UI) and the star's own inner-vs-
    // outer radius ratio (Paint[1]) - both only meaningful while the shape is still one of the parametric
    // Polygon/Star categories; once a corner is dragged with the Points tool it becomes a plain cubic path
    // and these no longer apply.
    std::optional<uint32_t> PolygonSides;
    std::optional<float> StarInnerRadius;
    std::optional<float> CornerRadius;
    // 📝 Category 6 (cubic path, Pen/Polyline) alone can swing between an open line and a closed filled
    // shape during its own lifetime - this stores the filled/closed state's own outline-stroke width
    // (Paint[0]) completely independently of StrokeWidth (Transform[1], the open line's own thickness),
    // so closing a line into a shape never silently inherits or overwrites the line's thickness setting.
    std::optional<float> ClosedOutlineWidth;
};
VectorSelectionProgram ConstructVectorSelectionProgram(const VectorAuthoringDocument &Document);

struct VectorSelectionExtent
{
    std::array<float, 4> Bounds;
    float Rotation = 0; // [rad]
};
VectorSelectionExtent QueryVectorSelectionExtent(const VectorAuthoringDocument &Document, std::span<const uint32_t> Selection);
std::vector<uint32_t> QueryVectorSelectionRegion(
    const VectorAuthoringDocument &Document,
    std::array<float, 2>           Start,
    std::array<float, 2>           Finish,
    std::span<const uint32_t>     Existing = {});
VectorAuthoringSourcePatch EvaluateVectorSelectionResize(
    const VectorAuthoringDocument &Document,
    std::span<const uint32_t>      Selection,
    std::array<int, 2>             Direction,
    std::array<float, 2>           Pointer,
    bool                          Aspect,
    bool                          Centred);
VectorAuthoringSourcePatch EvaluateVectorSelectionRotation(
    const VectorAuthoringDocument &Document,
    std::span<const uint32_t>      Selection,
    std::array<float, 2>           Start,
    std::array<float, 2>           Pointer,
    bool                          Constrained);
std::array<float, 4> QueryVectorAuthoringSelectionBounds(const VectorAuthoringDocument &Document, std::span<const uint32_t> Selection);
void ReclaimVectorAuthoringSources(VectorAuthoringDocument &Document);
void TransformVectorAuthoringSelection(VectorAuthoringDocument &Document, std::span<const uint32_t> Selection, const VectorAuthoringSourcePatch &Patch);
uint32_t ConstructVectorAuthoringPrimitive(VectorAuthoringDocument &Document, VectorCoverageInstruction Source, std::string Caption);
void PartitionVectorAuthoringSelection(VectorAuthoringDocument &Document, std::span<const uint32_t> Selection);
VectorAuthoringDocument ExtractVectorAuthoringSelection(const VectorAuthoringDocument &Document, std::span<const uint32_t> Selection);
std::vector<uint32_t> IntegrateVectorAuthoringSelection(
    VectorAuthoringDocument       &Document,
    const VectorAuthoringDocument &Fragment,
    std::array<float, 2>           Displacement = {18, 18});
enum class VectorSelectionOrder
{
    Back, Backward, Forward, Front
};
void ReorderVectorAuthoringSelection(
    VectorAuthoringDocument   &Document,
    std::span<const uint32_t>  Selection,
    VectorSelectionOrder      Order);
// 🧩 Shared by the Pattern and Stamp layer-list drag-and-drop reorder handlers: move SourceToken to sit
// beside DestinationToken. Dropping onto a row used to always insert *before* it, which meant a layer
// dragged onto the very last row could never actually become the last layer (at best it landed
// second-to-last, and with exactly two layers the drag was a silent no-op). Inserting after the
// destination when the drag moved the layer downward in the original order - and before it otherwise -
// fixes both of those without changing the feel of reordering anywhere else in the list. A no-op
// (unknown/matching/locked tokens) leaves Document untouched.
void ReorderVectorAuthoringLayerByDrag(VectorAuthoringDocument &Document, uint32_t SourceToken, uint32_t DestinationToken);

uint32_t IntegrateVectorAuthoringBoolean(VectorAuthoringDocument &Document, std::span<const uint32_t> Selection, VectorCoverageCategory Category);
}
