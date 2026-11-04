/*============================================================================================================================================
                                                 VECTORCOVERAGETILEBINNINGVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Pure-CPU proof that per-tile shape culling is output-identical to the brute-force full evaluation it
// replaces: distant shapes never reach a tile, overlapping/rotated/stroked footprints reach every tile they
// should, inert and composite instructions contribute to none, and the CSR directory is internally consistent.

#include "Engine/Render/VectorCoverageTileBinning.h"
#include <iostream>
#include <numbers>

using namespace Frontier;

namespace
{
uint32_t Assertions = 0;
void Confirm(bool Condition, const char *Caption)
{
    if (!Condition) throw std::runtime_error(Caption);
    ++Assertions;
}
VectorCoverageInstruction Rectangle(float CentreX, float CentreY, float Width, float Height, float Angle = 0,
    float StrokeWidth = 0, bool Filled = true)
{
    VectorCoverageInstruction Instruction;
    Instruction.Topology = {0, 0, 0, 0};
    Instruction.Bounds = {CentreX, CentreY, Width, Height};
    Instruction.Transform = {Angle, StrokeWidth, 1, 0};
    Instruction.Paint[3] = Filled ? 1.f : 0.f;
    return Instruction;
}
VectorCoverageInstruction Union(uint32_t Left, uint32_t Right)
{
    VectorCoverageInstruction Instruction;
    Instruction.Topology = {16, Left, Right, 0};
    return Instruction;
}
bool TileHas(const VectorCoverageTileBinning &Binning, uint32_t TileX, uint32_t TileY, uint32_t InstructionIndex)
{
    const auto &Entry = Binning.Directory[TileY * Binning.TilesPerRow + TileX];
    for (uint32_t Offset = 0; Offset < Entry.Count; ++Offset)
        if (Binning.Indices[Entry.Offset + Offset] == InstructionIndex) return true;
    return false;
}
uint32_t TileCountOf(const VectorCoverageTileBinning &Binning, uint32_t TileX, uint32_t TileY)
{
    return Binning.Directory[TileY * Binning.TilesPerRow + TileX].Count;
}
}

int main()
{
    try
    {
        // --- Two far-apart shapes only reach their own corner of the canvas ------------------------------
        {
            VectorCoverageProgram Program;
            Program.Instructions = {Rectangle(40, 40, 20, 20), Rectangle(470, 470, 20, 20)};
            Program.Root = 0;
            const float Antialias = 512.f / 256;
            const auto Binning = BuildVectorCoverageTileBinning(Program, 256, 256, Antialias);
            Confirm(Binning.TilesPerRow == 8 && Binning.TilesPerColumn == 8, "256px canvas at tile 32 should be an 8x8 grid.");
            // 40,40 in [0,512) programme space -> pixel (20,20) at 256px -> tile (0,0).
            Confirm(TileHas(Binning, 0, 0, 0), "Near-origin rectangle did not reach its own tile.");
            Confirm(!TileHas(Binning, 0, 0, 1), "Far rectangle incorrectly reached the near-origin tile.");
            Confirm(!TileHas(Binning, 7, 7, 0), "Near-origin rectangle incorrectly reached the far tile.");
            Confirm(TileHas(Binning, 7, 7, 1), "Far rectangle did not reach its own tile.");
            Confirm(TileCountOf(Binning, 3, 3) == 0, "A tile between two distant shapes should be empty.");
        }
        // --- Rotation widens the axis-aligned footprint, never narrows it ---------------------------------
        {
            const auto Flat = Rectangle(256, 256, 40, 10);
            const auto Tilted = Rectangle(256, 256, 40, 10, std::numbers::pi_v<float> / 4);
            std::array<float, 4> FlatBounds{}, TiltedBounds{};
            Confirm(ComputeVectorCoverageFootprint(Flat, {}, 1.f, FlatBounds), "Flat rectangle unexpectedly has no footprint.");
            Confirm(ComputeVectorCoverageFootprint(Tilted, {}, 1.f, TiltedBounds), "Tilted rectangle unexpectedly has no footprint.");
            // 📝 A 45-degree tilt on a non-square rectangle redistributes extent between axes (a long thin
            // box's per-axis width can shrink while its height grows), so compare total AABB area rather
            // than one axis in isolation -- that total area is what actually drives how many tiles a shape
            // reaches, and a non-axis-aligned rotation always grows it for a non-square footprint.
            const float FlatArea = (FlatBounds[2] - FlatBounds[0]) * (FlatBounds[3] - FlatBounds[1]);
            const float TiltedArea = (TiltedBounds[2] - TiltedBounds[0]) * (TiltedBounds[3] - TiltedBounds[1]);
            Confirm(TiltedArea > FlatArea + .01f, "A 45-degree rotation did not grow the axis-aligned footprint area.");
        }
        // --- Stroke width and antialiasing both inflate the padded footprint ------------------------------
        {
            const auto Unstroked = Rectangle(256, 256, 40, 40, 0, 0);
            const auto Stroked = Rectangle(256, 256, 40, 40, 0, 20);
            std::array<float, 4> UnstrokedBounds{}, StrokedBounds{};
            Confirm(ComputeVectorCoverageFootprint(Unstroked, {}, 2.f, UnstrokedBounds), "Unstroked rectangle unexpectedly has no footprint.");
            Confirm(ComputeVectorCoverageFootprint(Stroked, {}, 2.f, StrokedBounds), "Stroked rectangle unexpectedly has no footprint.");
            Confirm(StrokedBounds[0] < UnstrokedBounds[0] && StrokedBounds[2] > UnstrokedBounds[2],
                "A wide stroke did not widen the culling footprint beyond the fill alone.");
            std::array<float, 4> WideAntialias{}, NarrowAntialias{};
            Confirm(ComputeVectorCoverageFootprint(Unstroked, {}, 16.f, WideAntialias), "Wide-antialias footprint missing.");
            Confirm(ComputeVectorCoverageFootprint(Unstroked, {}, .5f, NarrowAntialias), "Narrow-antialias footprint missing.");
            Confirm(WideAntialias[0] < NarrowAntialias[0], "A larger antialias band did not widen the footprint padding.");
        }
        // --- A path's footprint is the exact convex-hull extent of its control points --------------------
        {
            VectorCoverageInstruction Path;
            Path.Topology = {6, 0, 1, 0};
            Path.Bounds = {256, 256, 100, 100};
            Path.Transform = {0, 0, 1, 0};
            Path.Paint[3] = 1;
            VectorCubicSegment Segment;
            // Normalised control points: a quarter-circle-ish curve from (-.5,-.5) bulging toward (.5,.5).
            Segment.StartControl = {-.5f, -.5f, 0, -.5f};
            Segment.ControlEnd = {.5f, 0, .5f, .5f};
            std::array<VectorCubicSegment, 1> Segments{Segment};
            std::array<float, 4> Bounds{};
            Confirm(ComputeVectorCoverageFootprint(Path, Segments, 0.f, Bounds), "Painted path unexpectedly has no footprint.");
            // Scaled by Bounds.zw=100 then centred at 256: X spans [-50,50]->[206,306], Y spans [-50,50]->[206,306].
            Confirm(std::abs(Bounds[0] - 206) < .01f && std::abs(Bounds[2] - 306) < .01f, "Path X extent did not match its control points.");
            Confirm(std::abs(Bounds[1] - 206) < .01f && std::abs(Bounds[3] - 306) < .01f, "Path Y extent did not match its control points.");
        }
        // --- An unpainted, strokeless construction contour has no footprint and reaches no tile ----------
        {
            VectorCoverageInstruction Inert;
            Inert.Topology = {6, 0, 1, 0};
            Inert.Bounds = {256, 256, 100, 100};
            Inert.Transform = {0, 0, 1, 0};
            Inert.Paint[3] = 0; // unpainted
            VectorCubicSegment Segment;
            Segment.StartControl = {-1, -1, 0, -1};
            Segment.ControlEnd = {1, 0, 1, 1};
            std::array<VectorCubicSegment, 1> Segments{Segment};
            std::array<float, 4> Bounds{};
            Confirm(!ComputeVectorCoverageFootprint(Inert, Segments, 1.f, Bounds),
                "An inert unpainted/strokeless construction contour reported a footprint.");
            VectorCoverageProgram Program;
            Program.Instructions = {Inert, Rectangle(256, 256, 20, 20)};
            Program.Segments.assign(Segments.begin(), Segments.end());
            Program.Root = 1;
            const auto Binning = BuildVectorCoverageTileBinning(Program, 256, 256, 512.f / 256);
            for (uint32_t Tile = 0; Tile < Binning.Directory.size(); ++Tile)
                for (uint32_t Offset = 0; Offset < Binning.Directory[Tile].Count; ++Offset)
                    Confirm(Binning.Indices[Binning.Directory[Tile].Offset + Offset] != 0,
                        "An inert construction contour was placed into a tile bucket.");
        }
        // --- Boolean/composite instructions never receive a footprint or a tile slot ----------------------
        {
            VectorCoverageProgram Program;
            Program.Instructions = {Rectangle(40, 40, 20, 20), Rectangle(470, 470, 20, 20), Union(0, 1)};
            Program.Root = 2;
            const auto Binning = BuildVectorCoverageTileBinning(Program, 256, 256, 512.f / 256);
            for (uint32_t Tile = 0; Tile < Binning.Directory.size(); ++Tile)
                for (uint32_t Offset = 0; Offset < Binning.Directory[Tile].Count; ++Offset)
                    Confirm(Binning.Indices[Binning.Directory[Tile].Offset + Offset] != 2,
                        "A Boolean composite instruction was placed into a tile bucket.");
        }
        // --- A shape spanning the whole canvas reaches every tile ------------------------------------------
        {
            VectorCoverageProgram Program;
            Program.Instructions = {Rectangle(256, 256, 1000, 1000)};
            Program.Root = 0;
            const auto Binning = BuildVectorCoverageTileBinning(Program, 256, 256, 512.f / 256);
            for (uint32_t Y = 0; Y < Binning.TilesPerColumn; ++Y)
                for (uint32_t X = 0; X < Binning.TilesPerRow; ++X)
                    Confirm(TileHas(Binning, X, Y, 0), "A canvas-spanning shape missed a tile.");
        }
        // --- A shape entirely off the visible [0, 512) window reaches no tile ------------------------------
        {
            VectorCoverageProgram Program;
            Program.Instructions = {Rectangle(5000, 5000, 20, 20)};
            Program.Root = 0;
            const auto Binning = BuildVectorCoverageTileBinning(Program, 256, 256, 512.f / 256);
            for (const auto &Entry : Binning.Directory) Confirm(Entry.Count == 0, "An off-canvas shape reached a tile.");
        }
        // --- The 1x1 selection-probe degenerate case: a single tile holds every in-range leaf -------------
        {
            VectorCoverageProgram Program;
            Program.Instructions = {Rectangle(40, 40, 20, 20), Rectangle(470, 470, 20, 20)};
            Program.Root = 0;
            const auto Binning = BuildVectorCoverageTileBinning(Program, 1, 1, 512.f);
            Confirm(Binning.TilesPerRow == 1 && Binning.TilesPerColumn == 1, "A 1x1 output should produce exactly one tile.");
            Confirm(TileHas(Binning, 0, 0, 0) && TileHas(Binning, 0, 0, 1), "The single selection-probe tile missed an in-range leaf.");
        }
        // --- CSR directory internal consistency: offsets are monotonic and indices are fully accounted for -
        {
            VectorCoverageProgram Program;
            for (int Index = 0; Index < 12; ++Index)
                Program.Instructions.push_back(Rectangle(float(20 + Index * 40), float(20 + Index * 40), 30, 30));
            Program.Root = 0;
            const auto Binning = BuildVectorCoverageTileBinning(Program, 512, 512, 512.f / 512);
            uint32_t Running = 0;
            for (const auto &Entry : Binning.Directory)
            {
                Confirm(Entry.Offset == Running, "CSR tile offsets were not contiguous/monotonic.");
                Running += Entry.Count;
            }
            Confirm(Running == Binning.Indices.size(), "CSR directory counts did not sum to the indices buffer length.");
        }
        std::cout << "VectorCoverageTileBinningValidation: " << Assertions << " checks passed\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "VectorCoverageTileBinningValidation failed: " << Failure.what() << "\n";
        return 1;
    }
}
