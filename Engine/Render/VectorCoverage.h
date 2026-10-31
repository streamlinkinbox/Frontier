/*============================================================================================================================================
                                                             VECTORCOVERAGE.H
============================================================================================================================================*/
// 🧩 Rebuildable CPU descriptors for GPU vector coverage, non-destructive Boolean expressions and masking.

#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace Frontier
{
inline constexpr uint32_t VectorCoverageStrokeColourExplicit = 0x80000000u;
// 📝 Keep these two mirrored exactly in Engine/Render/Shader/VectorCoverage.slang and VectorSurface.slang
// (fixed-size Evaluations[]/Instructions[] array bounds there cannot include this header). Raising the
// instruction capacity lets one composited document hold hundreds of shapes instead of the former 128-node
// wall; the extension region packs the same 64-slot budget already shared by selection candidates and
// surface contributions, now addressed starting right after the (larger) instruction region.
inline constexpr uint32_t VectorCoverageInstructionCapacity = 256;
inline constexpr uint32_t VectorCoverageExtensionCapacity = 64;
// 📝 Wang's formula (Wang 1984): the number of uniform chords needed to flatten a cubic Bezier so no point
// on the chord polyline deviates from the true analytic curve by more than Tolerance. Mirror this exactly
// in VectorCoverage.slang and VectorSurface.slang's ShapeDistance (Slang/HLSL cannot include this header,
// so GPU adaptive curve flattening is derived/tested here and hand-copied there). This CPU copy exists to
// validate the formula and its clamping in isolation with real unit tests -- it is not itself called by
// the GPU rasterizer and cannot by itself prove the hand-copied Slang text matches bit-for-bit; shader
// execution still cannot be verified in this environment (see VectorCoverageCurveFlatteningValidation.cpp).
// Small/flat ornamental curves (the common case across hundreds of authored shapes, previously forced
// through a fixed 32-chord flatten regardless of size) collapse to as few as 2 chords; sharp or large
// curves still subdivide enough to stay within Tolerance of the analytic curve.
[[nodiscard]] inline uint32_t EstimateCubicFlattenSegments(std::array<float, 2> First, std::array<float, 2> Second,
    std::array<float, 2> Third, std::array<float, 2> Fourth, float Tolerance) noexcept
{
    const auto Deviation = [](std::array<float, 2> A, std::array<float, 2> B, std::array<float, 2> C)
    {
        const float X = A[0] - 2 * B[0] + C[0], Y = A[1] - 2 * B[1] + C[1];
        return std::sqrt(X * X + Y * Y);
    };
    const float ControlDeviation = std::max(Deviation(First, Second, Third), Deviation(Second, Third, Fourth));
    const float Bounded = std::max(Tolerance, 1e-4f);
    const float Count = std::ceil(std::sqrt(ControlDeviation * 3.f / (4.f * Bounded)));
    return uint32_t(std::clamp(Count, 2.f, 32.f));
}
enum class VectorCoverageCategory : uint32_t
{
    Rectangle, Ellipse, Diamond, Triangle, Polygon, Star, CubicContour,
    Union = 16, Difference, Intersection, Exclude,
    AlphaMask = 24, LuminanceMask, Over = 28, Opacity = 29
};
struct alignas(16) VectorCoverageInstruction
{
    std::array<uint32_t, 4> Topology{};
    std::array<float, 4> Bounds{256, 256, 100, 100};
    std::array<float, 4> Transform{0, 0, 1, 0};
    std::array<float, 4> Colour{1, 1, 1, 1};
    std::array<float, 4> ColourEnd{1, 1, 1, 1};
    std::array<float, 4> StrokeColour{1, 1, 1, 1};
    std::array<float, 4> Paint{0, .45f, 6, 1};
    // 🧩 Multi-stop gradient ramp: up to 4 colour stops per shape. Slot 0's colour is Colour, slot 1's
    // is ColourEnd (kept for backward authoring compatibility with the original 2-stop model), slots 2
    // and 3 are these two extra channels. GradientStopPositions holds every slot's Fraction (the same
    // 0..1 axis the shader already computes for the 2-stop lerp) in strictly ascending order starting
    // at index 0; a position of -1 marks that slot and every slot after it as unused, so a plain 2-stop
    // gradient (the only kind this engine supported before) is just {0, 1, -1, -1} — its default below.
    // Mirror this struct's field order and the new slots exactly in VectorCoverage.slang/VectorSurface.slang.
    std::array<float, 4> GradientStopPositions{0.f, 1.f, -1.f, -1.f};
    std::array<float, 4> GradientStopColour2{1, 1, 1, 1};
    std::array<float, 4> GradientStopColour3{1, 1, 1, 1};
    // 🧩 The gradient's own geometric line/radius, independent of the shape's bounding box: (AxisStartX,
    // AxisStartY, AxisEndX, AxisEndY) in the shape's local, unrotated, bounds-normalized frame (the same
    // "Local / Bounds.zw" space the shader already computes everything else in) — so dragging these two
    // points on the canvas (see StampWorkspacePanel.cpp/PatternWorkspacePanel.cpp's gradient-axis handles)
    // places the ramp's start/end anywhere relative to the shape, exactly like Illustrator/Figma's gradient
    // tool, instead of only ever spanning corner-to-corner of the bounding box at a fixed angle. Linear
    // reads it as the two ends of the ramp line; radial reads slot 0/1 as the centre and slot 2/3 as a
    // point on the circle's edge (so dragging it both re-centres and resizes/re-orients the radius). The
    // default below spans the box horizontally, matching this engine's original angle-based linear default
    // exactly; PaintCategory switches re-seed it with the category-appropriate default (see Stamp/Pattern's
    // "Component/Motif fill source" dropdown handler) since a plain centre-to-edge default would misplace a
    // freshly chosen radial gradient. Mirror this exactly in VectorCoverage.slang/VectorSurface.slang.
    std::array<float, 4> GradientAxis{-.5f, 0.f, .5f, 0.f};
    // x: rectangle corner radius in document units; y: stroke source (0 solid, 1 linear, 2 radial);
    // z: fill gradient interpolation (0 linear, 1 hermite, 2 catmull-rom, 3 b-spline, 4 step);
    // w: stroke gradient interpolation (0..4).
    std::array<float, 4> ShapeParameters{};
    std::array<float, 4> StrokeColourEnd{1, 1, 1, 1};
    std::array<float, 4> StrokeGradientStopPositions{0, 1, -1, -1};
    std::array<float, 4> StrokeGradientStopColour2{1, 1, 1, 1};
    std::array<float, 4> StrokeGradientStopColour3{1, 1, 1, 1};
    std::array<float, 4> StrokeGradientAxis{-.5f, 0, .5f, 0};
    // 🧩 Multi-point freeform 2D gradient (Transform[3] == 3): up to 8 arbitrary 2D control points in
    // the shape's bounds-normalized LocalNorm frame (Local / Bounds.zw). Points 0..3 read Colour,
    // ColourEnd, GradientStopColour2 and GradientStopColour3; points 4..7 read FreeformPointColour4..7.
    // FreeformParameters stores {PointCount (2..8), FalloffSharpness (0.5..6), 0, 0}.
    std::array<float, 4> FreeformPointPositions01{-.3f, -.3f, .3f, -.3f};
    std::array<float, 4> FreeformPointPositions23{-.3f, .3f, .3f, .3f};
    std::array<float, 4> FreeformPointPositions45{0.f, -.35f, .35f, 0.f};
    std::array<float, 4> FreeformPointPositions67{0.f, .35f, -.35f, 0.f};
    std::array<float, 4> FreeformPointColour4{1, 1, 1, 1};
    std::array<float, 4> FreeformPointColour5{1, 1, 1, 1};
    std::array<float, 4> FreeformPointColour6{1, 1, 1, 1};
    std::array<float, 4> FreeformPointColour7{1, 1, 1, 1};
    std::array<float, 4> FreeformParameters{4.f, 2.f, 0.f, 0.f};
    bool operator==(const VectorCoverageInstruction &) const = default;
};
struct alignas(16) VectorCubicSegment
{
    std::array<float, 4> StartControl{};
    std::array<float, 4> ControlEnd{};
    bool operator==(const VectorCubicSegment &) const = default;
};
static_assert(sizeof(VectorCoverageInstruction) == 416 && sizeof(VectorCubicSegment) == 32);
struct VectorCoverageProgram
{
    std::vector<VectorCoverageInstruction> Instructions;
    std::vector<VectorCubicSegment> Segments;
    uint32_t Root = 0;
    bool operator==(const VectorCoverageProgram &) const = default;
};
struct VectorSelectionProgram
{
    VectorCoverageProgram Coverage;
    // [idx] - Ordered coverage roots and authenticated source tokens, lowest to highest.
    std::vector<std::array<uint32_t, 2>> Candidates;
};
// 📝 The one stroke-width rule every consumer must agree on (the GPU shaders mirror it by hand in
// VectorCoverage.slang/VectorSurface.slang): a cubic contour (category 6) that is currently
// closed+filled reads its own independent outline width from Paint[0]; every other leaf - open
// cubic lines included - reads Transform[1]. The two widths are authored independently so closing
// a line into a shape (or reopening it) never inherits or overwrites the other state's thickness.
// ⚠️ Reading Transform[1] unconditionally is what made closed-shape outlines render in CPU layer
// thumbnails but vanish on the GPU canvas (text/pen shapes keep Transform[1] at 0).
[[nodiscard]] inline float QueryVectorCoverageStrokeWidth(const VectorCoverageInstruction &Instruction) noexcept
{
    return Instruction.Topology[0] == uint32_t(VectorCoverageCategory::CubicContour) && Instruction.Paint[3] != 0
        ? Instruction.Paint[0] : Instruction.Transform[1];
}
inline void ValidateVectorCoverage(const VectorCoverageProgram &Program)
{
    if (Program.Instructions.empty() || Program.Instructions.size() > VectorCoverageInstructionCapacity || Program.Root >= Program.Instructions.size() || Program.Segments.size() > 32768)
        throw std::runtime_error("Vector coverage exceeds bounded GPU program limits.");
    for (size_t Index = 0; Index < Program.Instructions.size(); ++Index)
    {
        const auto &Instruction = Program.Instructions[Index];
        for (const auto &Coordinates : {Instruction.Bounds, Instruction.Transform, Instruction.Colour, Instruction.ColourEnd, Instruction.StrokeColour, Instruction.Paint,
            Instruction.GradientStopPositions, Instruction.GradientStopColour2, Instruction.GradientStopColour3, Instruction.GradientAxis, Instruction.ShapeParameters,
            Instruction.StrokeColourEnd, Instruction.StrokeGradientStopPositions, Instruction.StrokeGradientStopColour2, Instruction.StrokeGradientStopColour3, Instruction.StrokeGradientAxis,
            Instruction.FreeformPointPositions01, Instruction.FreeformPointPositions23, Instruction.FreeformPointPositions45, Instruction.FreeformPointPositions67,
            Instruction.FreeformPointColour4, Instruction.FreeformPointColour5, Instruction.FreeformPointColour6, Instruction.FreeformPointColour7, Instruction.FreeformParameters})
            for (float Coordinate : Coordinates) if (!std::isfinite(Coordinate)) throw std::runtime_error("Non-finite vector coverage descriptor.");
        const auto Category = Instruction.Topology[0];
        if (Category <= 6)
        {
            if (std::abs(Instruction.Bounds[0]) > 16384 || std::abs(Instruction.Bounds[1]) > 16384 || Instruction.Bounds[2] < .01f || Instruction.Bounds[3] < .01f || Instruction.Bounds[2] > 8192 || Instruction.Bounds[3] > 8192)
                throw std::runtime_error("Vector extent exceeds the finite GPU authoring domain.");
            if (Category == 6 && (Instruction.Topology[2] == 0 || uint64_t(Instruction.Topology[1]) + Instruction.Topology[2] > Program.Segments.size())) throw std::runtime_error("Cubic segment range is invalid.");
            if (Category == 6 && Instruction.Paint[3] != 0)
            {
                std::array<float, 2> ContourStart{}, PreviousEnd{};
                const auto Coincident = [](const std::array<float, 2> &First, const std::array<float, 2> &Second)
                {
                    return std::abs(First[0] - Second[0]) <= .000001f && std::abs(First[1] - Second[1]) <= .000001f;
                };
                for (uint32_t Curve = 0; Curve < Instruction.Topology[2]; ++Curve)
                {
                    const auto &Segment = Program.Segments[Instruction.Topology[1] + Curve];
                    const std::array<float, 2> Start{Segment.StartControl[0], Segment.StartControl[1]};
                    if (!Curve) ContourStart = Start;
                    else if (!Coincident(Start, PreviousEnd))
                    {
                        if (!Coincident(PreviousEnd, ContourStart)) throw std::runtime_error("GPU fill contours must be explicitly closed.");
                        ContourStart = Start;
                    }
                    PreviousEnd = {Segment.ControlEnd[2], Segment.ControlEnd[3]};
                }
                if (!Coincident(PreviousEnd, ContourStart)) throw std::runtime_error("GPU fill contour is open.");
            }
            if ((Category == 4 || Category == 5) && (Instruction.Paint[2] < 3 || Instruction.Paint[2] > 32 || std::floor(Instruction.Paint[2]) != Instruction.Paint[2])) throw std::runtime_error("Polygon corner count must be 3–32.");
            if (Category == 0 && (Instruction.ShapeParameters[0] < 0 || Instruction.ShapeParameters[0] > 50)) throw std::runtime_error("Rectangle corner radius is invalid.");
            if (Category == 5 && (Instruction.Paint[1] < .05f || Instruction.Paint[1] > .95f)) throw std::runtime_error("Star inner radius is invalid.");
        }
        else if ((Category >= 16 && Category <= 19) || Category == 24 || Category == 25 || Category == 28 || Category == 29)
        {
            if (Instruction.Topology[1] >= Index || Instruction.Topology[2] >= Index) throw std::runtime_error("Vector coverage operands must precede their operation; cycles are forbidden.");
            if ((Category == 24 || Category == 25) && Instruction.Topology[3] > 1) throw std::runtime_error("Mask inversion must be zero or one.");
        }
        else throw std::runtime_error("Unsupported GPU vector coverage operation.");
        if (Instruction.Transform[1] < 0 || Instruction.Transform[1] > 100 || Instruction.Transform[2] < 0 || Instruction.Transform[2] > 1 || Instruction.Transform[3] < 0 || Instruction.Transform[3] > 3 || std::floor(Instruction.Transform[3]) != Instruction.Transform[3] || std::abs(Instruction.Transform[0]) > 1e6f || std::abs(Instruction.Paint[0]) > 1e6f || (Instruction.Paint[3] != 0 && Instruction.Paint[3] != 1))
            throw std::runtime_error("Vector stroke, opacity or paint category is invalid.");
        if (Instruction.ShapeParameters[1] < 0 || Instruction.ShapeParameters[1] > 2 || std::floor(Instruction.ShapeParameters[1]) != Instruction.ShapeParameters[1]) throw std::runtime_error("Invalid stroke paint category.");
        if (Instruction.ShapeParameters[2] < 0 || Instruction.ShapeParameters[2] > 4 || std::floor(Instruction.ShapeParameters[2]) != Instruction.ShapeParameters[2] ||
            Instruction.ShapeParameters[3] < 0 || Instruction.ShapeParameters[3] > 4 || std::floor(Instruction.ShapeParameters[3]) != Instruction.ShapeParameters[3])
            throw std::runtime_error("Invalid vector gradient interpolation category.");
        if (Instruction.FreeformParameters[0] < 2.f || Instruction.FreeformParameters[0] > 8.f || std::floor(Instruction.FreeformParameters[0]) != Instruction.FreeformParameters[0] ||
            Instruction.FreeformParameters[1] < .25f || Instruction.FreeformParameters[1] > 8.f)
            throw std::runtime_error("Invalid vector freeform gradient parameters.");
        for (const auto &Coordinates : {Instruction.FreeformPointPositions01, Instruction.FreeformPointPositions23, Instruction.FreeformPointPositions45, Instruction.FreeformPointPositions67})
            for (float Coordinate : Coordinates) if (std::abs(Coordinate) > 64.f) throw std::runtime_error("Vector freeform gradient point extends absurdly far past the shape's bounds.");
        for (const auto &Colours : {Instruction.StrokeColourEnd, Instruction.StrokeGradientStopColour2, Instruction.StrokeGradientStopColour3,
            Instruction.FreeformPointColour4, Instruction.FreeformPointColour5, Instruction.FreeformPointColour6, Instruction.FreeformPointColour7})
            for (float Channel : Colours) if (Channel < 0 || Channel > 1) throw std::runtime_error("Vector stroke gradient colour must be normalized.");
        for (float Coordinate : Instruction.StrokeGradientAxis) if (std::abs(Coordinate) > 64.f) throw std::runtime_error("Vector stroke gradient axis is invalid.");
        for (float Coordinate : Instruction.GradientAxis) if (std::abs(Coordinate) > 64.f) throw std::runtime_error("Vector gradient axis extends absurdly far past the shape's own bounds.");
        for (float Channel : Instruction.Colour) if (Channel < 0 || Channel > 1) throw std::runtime_error("Vector colour must be normalized.");
        for (float Channel : Instruction.ColourEnd) if (Channel < 0 || Channel > 1) throw std::runtime_error("Vector gradient colour must be normalized.");
        for (float Channel : Instruction.StrokeColour) if (Channel < 0 || Channel > 1) throw std::runtime_error("Vector stroke colour must be normalized.");
        for (float Channel : Instruction.GradientStopColour2) if (Channel < 0 || Channel > 1) throw std::runtime_error("Vector gradient colour must be normalized.");
        for (float Channel : Instruction.GradientStopColour3) if (Channel < 0 || Channel > 1) throw std::runtime_error("Vector gradient colour must be normalized.");
        // 📝 GradientStopPositions must describe a contiguous, strictly ascending run of 2–4 active stops
        // starting at slot 0 (a bare -1 everywhere past the active run) so both the CPU authoring tools and
        // the GPU shader's piecewise lerp can walk it without ever needing to re-sort at draw time.
        {
            for (const auto &Positions : {Instruction.GradientStopPositions, Instruction.StrokeGradientStopPositions})
            {
            float Previous = -1.f; bool Closed = false;
            for (int Stop = 0; Stop < 4; ++Stop)
            {
                const float StopPosition = Positions[Stop];
                if (StopPosition < 0.f)
                {
                    if (StopPosition != -1.f) throw std::runtime_error("Vector gradient stop position must be 0..1, or exactly -1 for an unused stop.");
                    Closed = true; continue;
                }
                if (Closed) throw std::runtime_error("Vector gradient stops must be contiguous starting at slot 0.");
                if (StopPosition > 1.f) throw std::runtime_error("Vector gradient stop position must be 0..1.");
                if (Stop > 0 && StopPosition <= Previous) throw std::runtime_error("Vector gradient stops must be strictly ascending.");
                Previous = StopPosition;
            }
            if (Positions[0] < 0.f || Positions[1] < 0.f)
                throw std::runtime_error("Vector gradient requires at least two active stops.");
            }
        }
    }
    for (const auto &Segment : Program.Segments)
        for (const auto &Coordinates : {Segment.StartControl, Segment.ControlEnd})
            for (float Coordinate : Coordinates) if (!std::isfinite(Coordinate) || std::abs(Coordinate) > 16) throw std::runtime_error("Cubic control coordinate exceeds its normalized GPU domain.");
}
inline void ValidateVectorSelection(const VectorSelectionProgram &Program)
{
    ValidateVectorCoverage(Program.Coverage);
    if (Program.Candidates.size() > VectorCoverageExtensionCapacity) throw std::runtime_error("Vector selection exceeds candidate capacity.");
    for (size_t Index = 0; Index < Program.Candidates.size(); ++Index)
    {
        const auto &Candidate = Program.Candidates[Index];
        if (Candidate[0] >= Program.Coverage.Instructions.size() || !Candidate[1])
            throw std::runtime_error("Invalid vector selection root or source token.");
        for (size_t Earlier = 0; Earlier < Index; ++Earlier)
            if (Program.Candidates[Earlier][1] == Candidate[1]) throw std::runtime_error("Duplicate vector selection token.");
    }
}

}
