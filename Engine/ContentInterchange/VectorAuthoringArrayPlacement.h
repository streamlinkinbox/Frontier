/*============================================================================================================================================
                                                          VECTORAUTHORINGARRAYPLACEMENT.H
============================================================================================================================================*/
// 🧩 Workspace-independent array transforms and scale envelopes, reusable by vector vector and texture-paint layers.

#pragma once
#include <cstdint>
#include <vector>
#include <array>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     ARRAY SPECIFICATION AND PLACEMENTS
//------------------------------------------------------------------------------------------------------------------------

enum class VectorAuthoringArrayCategory : uint32_t
{
    Linear, Grid, Circular, Radial
};
enum class VectorAuthoringScaleProfile : uint32_t
{
    Uniform, Linear, Circular, Radial, Noise
};
struct VectorAuthoringArraySpecification
{
    VectorAuthoringArrayCategory Category       = VectorAuthoringArrayCategory::Linear;
    VectorAuthoringScaleProfile  ScaleProfile   = VectorAuthoringScaleProfile::Uniform;
    uint32_t             Count          = 4;
    uint32_t             Columns        = 2;
    uint32_t             Rows           = 2;
    uint32_t             Rings          = 1;
    uint32_t             Seed           = 1;
    float                StartScale     = 100;
    float                EndScale       = 100;
    float                StepX          = 64;
    float                StepY          = 0;
    float                CentreX        = 256;
    float                CentreY        = 256;
    float                Radius         = 100;
    float                RadiusStep     = 64;
    float                StartAngle     = 0;
    float                Sweep          = 360;
    bool                 RotateCopies   = false;
    bool operator==(const VectorAuthoringArraySpecification &) const = default;
};
struct VectorAuthoringArrayPlacement
{
    float X     = 0;
    float Y     = 0;
    float Angle = 0;
    float Phase = 0;
    float Scale = 1;
};

std::vector<VectorAuthoringArrayPlacement> ConstructVectorAuthoringArrayPlacements(const VectorAuthoringArraySpecification &Specification, float OriginX, float OriginY);

//------------------------------------------------------------------------------------------------------------------------
//                                                     MIRROR / NOISE / OFFSET SPECIFICATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Reflects the selection across a line through (PivotX,PivotY) at AxisAngle degrees. KeepOriginal=true
// adds the reflected result as a companion layer (classic "Mirror modifier" look); false replaces the
// source layer in place (useful once the user is happy and wants a single, already-mirrored shape).
struct VectorAuthoringMirrorSpecification
{
    float PivotX       = 256;
    float PivotY       = 256;
    float AxisAngle    = 90; // 📝 90° = a vertical axis (left/right mirror), matching the common default.
    bool  KeepOriginal = true;
    bool operator==(const VectorAuthoringMirrorSpecification &) const = default;
};
// 📝 One deterministic (seeded) random nudge of position/rotation/scale, applied to the whole selection
// as a rigid group - distinct from Array's per-copy randomization in that it never duplicates anything.
struct VectorAuthoringNoiseSpecification
{
    float    PositionJitter = 8;   // [px] maximum absolute offset along each axis
    float    RotationJitter = 10;  // [deg] maximum absolute rotation nudge
    float    ScaleJitter    = 10;  // [%] maximum absolute scale nudge
    uint32_t Seed           = 1;
    bool operator==(const VectorAuthoringNoiseSpecification &) const = default;
};
// 📝 Grows (positive) or shrinks (negative) the selection's own bounding frame by Distance on every side.
// Exact for Rectangle/Ellipse; a disclosed bounding-frame approximation for Diamond/Triangle/Polygon/Star/
// CubicContour, since true per-edge (Minkowski) polygon/path offsetting is out of scope for this tool.
struct VectorAuthoringOffsetSpecification
{
    float Distance = 4;
    bool operator==(const VectorAuthoringOffsetSpecification &) const = default;
};
enum class VectorAuthoringColourGenerator : uint32_t
{
    Linear, Radial, Ramp
};
enum class VectorAuthoringColourTarget : uint32_t
{
    Fill, Stroke, Both
};
// 📝 Paints the selection a flat colour per element, computed by one of three generators: Linear/Radial
// are spatial gradients (sampled continuously from each element's own world position, auto-fit across the
// whole selection for Linear, from an explicit CentreX/Y/Radius for Radial); Ramp is sequential/discrete -
// it cycles through up to 4 Stops by GROUP, which is what answers "every Nth array copy/row shares a
// colour". GroupSize is either resolved automatically from a preceding Array modifier's own row/ring
// population (AutoGroup, Grid -> Columns, Radial -> Count, everything else -> the whole population as one
// group) or set manually. This never touches a shape's own single-instruction gradient fill (Transform.w);
// it always writes one flat Colour/StrokeColour per element, matching "give each array copy its own colour".
struct VectorAuthoringColourSpecification
{
    VectorAuthoringColourGenerator Generator = VectorAuthoringColourGenerator::Ramp;
    VectorAuthoringColourTarget    Target    = VectorAuthoringColourTarget::Fill;
    float    AxisAngle = 0;              // [deg] Linear gradient direction
    float    CentreX   = 256;            // Radial gradient centre
    float    CentreY   = 256;
    float    Radius    = 128;            // Radial gradient reach
    std::array<std::array<float, 4>, 4> Stops{{{.95f, .55f, .18f, 1}, {.22f, .45f, .85f, 1}, {1, 1, 1, 1}, {.1f, .1f, .1f, 1}}};
    uint32_t StopCount = 2;              // 2..4 active entries in Stops
    bool     AutoGroup = true;
    uint32_t GroupSize = 1;              // only used when AutoGroup is false
    bool operator==(const VectorAuthoringColourSpecification &) const = default;
};
enum class VectorAuthoringModifierOperation : uint32_t
{
    Array, Mirror, Noise, Offset, Colour
};
// 📝 A single Blender-style modifier-stack entry: re-evaluated live from the layer's own untouched source
// every frame while Enabled (see ConstructEvaluatedVectorAuthoringDocument), never mutating that source.
// "Apply" bakes one into real duplicate/replacement layers via the matching Generate* function and removes
// the entry; "Delete" removes it with no other change. Every operation's specification lives on the struct
// at once (fat-struct-with-Operation-tag, matching VectorCoverageInstruction's own category-tagged field
// reuse) rather than a std::variant, so the UI/codec can stay simple and future operations only add a field.
struct VectorAuthoringModifier
{
    VectorAuthoringModifierOperation         Operation = VectorAuthoringModifierOperation::Array;
    bool                                 Enabled = true;
    VectorAuthoringArraySpecification   Array;
    VectorAuthoringMirrorSpecification  Mirror;
    VectorAuthoringNoiseSpecification   Noise;
    VectorAuthoringOffsetSpecification  Offset;
    VectorAuthoringColourSpecification  Colour;
    bool operator==(const VectorAuthoringModifier &) const = default;
};
}

