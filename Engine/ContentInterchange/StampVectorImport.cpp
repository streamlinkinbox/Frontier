/*============================================================================================================================================
                                                         STAMPVECTORIMPORT.CPP
============================================================================================================================================*/
// 🧩 Convert one sanitized SVG document into real cubic-path geometry via ThorVG's software SVG parser.

#include "StampVectorImport.h"
#include <thorvg.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace Frontier
{
namespace
{
using Coordinate = std::array<float, 2>;

//------------------------------------------------------------------------------------------------------------------------
//                                                   AFFINE COMPOSITION
//------------------------------------------------------------------------------------------------------------------------

tvg::Matrix Multiply(const tvg::Matrix &Outer, const tvg::Matrix &Inner)
{
    // 📝 Composes "apply Inner (a shape's own local transform), then Outer (its ancestor chain)",
    // matching SVG's own nested <g transform="..."> semantics instead of ThorVG's per-node storage
    // (each loaded node only carries its OWN transform attribute; the ancestor chain is composed
    // here by walking the Scene tree ourselves instead of relying on the flat Accessor utility).
    tvg::Matrix Result{};
    Result.e11 = Outer.e11 * Inner.e11 + Outer.e12 * Inner.e21;
    Result.e12 = Outer.e11 * Inner.e12 + Outer.e12 * Inner.e22;
    Result.e13 = Outer.e11 * Inner.e13 + Outer.e12 * Inner.e23 + Outer.e13;
    Result.e21 = Outer.e21 * Inner.e11 + Outer.e22 * Inner.e21;
    Result.e22 = Outer.e21 * Inner.e12 + Outer.e22 * Inner.e22;
    Result.e23 = Outer.e21 * Inner.e13 + Outer.e22 * Inner.e23 + Outer.e23;
    Result.e31 = 0; Result.e32 = 0; Result.e33 = 1;
    return Result;
}
Coordinate Apply(const tvg::Matrix &M, tvg::Point P)
{
    return {M.e11 * P.x + M.e12 * P.y + M.e13, M.e21 * P.x + M.e22 * P.y + M.e23};
}
float MatrixMetricScale(const tvg::Matrix &M)
{
    const float SumSquares = M.e11 * M.e11 + M.e21 * M.e21 + M.e12 * M.e12 + M.e22 * M.e22;
    return std::sqrt(std::max(0.f, .5f * SumSquares));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PAINT EXTRACTION
//------------------------------------------------------------------------------------------------------------------------

struct ImportedPaint
{
    bool Active = false;
    float Category = 0.f; // 0 = Solid, 1 = Linear, 2 = Radial
    std::array<float, 4> Colour{.6f, .6f, .6f, 1.f};
    std::array<float, 4> ColourEnd{.6f, .6f, .6f, 1.f};
    std::array<float, 4> StopColour2{0.f, 0.f, 0.f, 1.f};
    std::array<float, 4> StopColour3{0.f, 0.f, 0.f, 1.f};
    std::array<float, 4> StopPositions{0.f, 1.f, -1.f, -1.f};
    Coordinate CanvasAxisStart{0.f, 0.f};
    Coordinate CanvasAxisEnd{1.f, 0.f};
};

ImportedPaint ExtractGradientPaint(const tvg::Fill &Gradient, const tvg::Matrix &Effective)
{
    ImportedPaint Paint;
    Paint.Active = true;
    const tvg::Fill::ColorStop *RawStops = nullptr;
    const uint32_t RawCount = Gradient.colorStops(&RawStops);
    struct StopEntry
    {
        float Position = 0.f;
        std::array<float, 4> Colour{0.f, 0.f, 0.f, 1.f};
    };
    std::vector<StopEntry> Stops;
    if (RawStops && RawCount > 0)
    {
        Stops.reserve(RawCount);
        for (uint32_t Index = 0; Index < RawCount; ++Index)
        {
            const auto &Raw = RawStops[Index];
            Stops.push_back({std::clamp(Raw.offset, 0.f, 1.f),
                {Raw.r / 255.f, Raw.g / 255.f, Raw.b / 255.f, Raw.a / 255.f}});
        }
        std::stable_sort(Stops.begin(), Stops.end(), [](const StopEntry &First, const StopEntry &Second) {
            return First.Position < Second.Position;
        });
    }
    if (Stops.empty())
    {
        Paint.Category = 0.f;
        return Paint;
    }
    bool AnyOpaque = false;
    for (const auto &Stop : Stops)
        if (Stop.Colour[3] > 0.f) AnyOpaque = true;
    if (!AnyOpaque)
    {
        Paint.Active = false;
        return Paint;
    }
    if (Stops.size() == 1)
    {
        Paint.Category = 0.f;
        Paint.Colour = Paint.ColourEnd = Stops[0].Colour;
        return Paint;
    }
    // Select up to 4 representative stops (first, up to two interior, last) with strictly ascending positions.
    std::vector<StopEntry> Chosen;
    if (Stops.size() <= 4) Chosen = Stops;
    else
    {
        Chosen.push_back(Stops.front());
        Chosen.push_back(Stops[Stops.size() / 3]);
        Chosen.push_back(Stops[(Stops.size() * 2) / 3]);
        Chosen.push_back(Stops.back());
    }
    std::vector<StopEntry> Strict;
    Strict.reserve(Chosen.size());
    for (auto Candidate : Chosen)
    {
        Candidate.Position = std::clamp(Candidate.Position, 0.f, 1.f);
        if (!Strict.empty() && Candidate.Position <= Strict.back().Position)
        {
            const float Nudged = Strict.back().Position + 1e-4f;
            if (Nudged <= 1.f) Candidate.Position = Nudged;
            else
            {
                Strict.back().Colour = Candidate.Colour;
                continue;
            }
        }
        Strict.push_back(Candidate);
    }
    if (Strict.size() < 2)
    {
        Paint.Category = 0.f;
        Paint.Colour = Paint.ColourEnd = Strict.front().Colour;
        return Paint;
    }
    Paint.Colour = Strict[0].Colour;
    Paint.ColourEnd = Strict[1].Colour;
    if (Strict.size() >= 3) Paint.StopColour2 = Strict[2].Colour;
    if (Strict.size() >= 4) Paint.StopColour3 = Strict[3].Colour;
    Paint.StopPositions = {-1.f, -1.f, -1.f, -1.f};
    for (size_t Index = 0; Index < Strict.size() && Index < 4; ++Index)
        Paint.StopPositions[Index] = Strict[Index].Position;

    const auto Composed = Multiply(Effective, const_cast<tvg::Fill &>(Gradient).transform());
    if (Gradient.type() == tvg::Type::LinearGradient)
    {
        float X1 = 0.f, Y1 = 0.f, X2 = 1.f, Y2 = 0.f;
        if (static_cast<const tvg::LinearGradient &>(Gradient).linear(&X1, &Y1, &X2, &Y2) == tvg::Result::Success)
        {
            Paint.CanvasAxisStart = Apply(Composed, {X1, Y1});
            Paint.CanvasAxisEnd = Apply(Composed, {X2, Y2});
            const float Dx = Paint.CanvasAxisEnd[0] - Paint.CanvasAxisStart[0];
            const float Dy = Paint.CanvasAxisEnd[1] - Paint.CanvasAxisStart[1];
            if (Dx * Dx + Dy * Dy > 1e-8f) Paint.Category = 1.f;
            else Paint.Colour = Paint.ColourEnd = Strict.back().Colour;
        }
    }
    else if (Gradient.type() == tvg::Type::RadialGradient)
    {
        float Cx = 0.f, Cy = 0.f, Radius = 0.f;
        if (static_cast<const tvg::RadialGradient &>(Gradient).radial(&Cx, &Cy, &Radius) == tvg::Result::Success && Radius > 1e-5f)
        {
            Paint.CanvasAxisStart = Apply(Composed, {Cx, Cy});
            Paint.CanvasAxisEnd = Apply(Composed, {Cx + Radius, Cy});
            Paint.Category = 2.f;
        }
        else Paint.Colour = Paint.ColourEnd = Strict.back().Colour;
    }
    return Paint;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SHAPE COLLECTION
//------------------------------------------------------------------------------------------------------------------------

struct ImportedShape
{
    std::vector<VectorCubicSegment> Segments;
    Coordinate Minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Coordinate Maximum{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
    ImportedPaint Fill;
    ImportedPaint Stroke;
    float StrokeWidth = 0.f;
    float Opacity = 1.f;
    bool EvenOdd = false;
};

void TrackPoint(ImportedShape &Shape, Coordinate Point)
{
    Shape.Minimum[0] = std::min(Shape.Minimum[0], Point[0]); Shape.Minimum[1] = std::min(Shape.Minimum[1], Point[1]);
    Shape.Maximum[0] = std::max(Shape.Maximum[0], Point[0]); Shape.Maximum[1] = std::max(Shape.Maximum[1], Point[1]);
}

std::array<float, 4> NormalizeGradientAxis(Coordinate Start, Coordinate End, float CentreX, float CentreY, float Width, float Height)
{
    return {
        std::clamp((Start[0] - CentreX) / Width, -64.f, 64.f),
        std::clamp((Start[1] - CentreY) / Height, -64.f, 64.f),
        std::clamp((End[0] - CentreX) / Width, -64.f, 64.f),
        std::clamp((End[1] - CentreY) / Height, -64.f, 64.f)};
}

void CollectShape(const tvg::Shape &Source, const tvg::Matrix &Effective, float InheritedOpacity, std::vector<ImportedShape> &Shapes)
{
    const float CombinedOpacity = std::clamp(InheritedOpacity * (Source.opacity() / 255.f), 0.f, 1.f);
    if (CombinedOpacity <= 0.f) return;

    const tvg::PathCommand *Commands = nullptr;
    const tvg::Point *Points = nullptr;
    uint32_t CommandCount = 0, PointCount = 0;
    if (Source.path(&Commands, &CommandCount, &Points, &PointCount) != tvg::Result::Success || !CommandCount || !PointCount) return;

    ImportedShape Shape;
    Shape.Opacity = CombinedOpacity;
    Shape.EvenOdd = Source.fillRule() == tvg::FillRule::EvenOdd;

    if (const auto *FillGradient = Source.fill())
        Shape.Fill = ExtractGradientPaint(*FillGradient, Effective);
    else
    {
        uint8_t Red = 0, Green = 0, Blue = 0, Alpha = 255;
        if (Source.fill(&Red, &Green, &Blue, &Alpha) == tvg::Result::Success && Alpha > 0)
        {
            Shape.Fill.Active = true;
            Shape.Fill.Category = 0.f;
            Shape.Fill.Colour = Shape.Fill.ColourEnd = {Red / 255.f, Green / 255.f, Blue / 255.f, Alpha / 255.f};
        }
    }

    const float RawStrokeWidth = Source.strokeWidth();
    if (RawStrokeWidth > 0.f)
    {
        const float ScaledWidth = std::clamp(RawStrokeWidth * MatrixMetricScale(Effective), 0.f, 100.f);
        if (ScaledWidth > 0.f)
        {
            if (const auto *StrokeGradient = Source.strokeFill())
            {
                Shape.Stroke = ExtractGradientPaint(*StrokeGradient, Effective);
                if (Shape.Stroke.Active) Shape.StrokeWidth = ScaledWidth;
            }
            else
            {
                uint8_t Red = 0, Green = 0, Blue = 0, Alpha = 0;
                if (Source.strokeFill(&Red, &Green, &Blue, &Alpha) == tvg::Result::Success && Alpha > 0)
                {
                    Shape.Stroke.Active = true;
                    Shape.Stroke.Category = 0.f;
                    Shape.Stroke.Colour = Shape.Stroke.ColourEnd = {Red / 255.f, Green / 255.f, Blue / 255.f, Alpha / 255.f};
                    Shape.StrokeWidth = ScaledWidth;
                }
            }
        }
    }

    if (!Shape.Fill.Active && !Shape.Stroke.Active) return;

    Coordinate Cursor{0, 0}, ContourStart{0, 0};
    bool Open = false;
    uint32_t PointIndex = 0;
    const auto CloseSubpathIfFilled = [&]()
    {
        if (Open && Shape.Fill.Active && (Cursor[0] != ContourStart[0] || Cursor[1] != ContourStart[1]))
            Shape.Segments.push_back({{Cursor[0], Cursor[1], Cursor[0], Cursor[1]}, {ContourStart[0], ContourStart[1], ContourStart[0], ContourStart[1]}});
    };
    for (uint32_t Index = 0; Index < CommandCount; ++Index)
    {
        switch (Commands[Index])
        {
        case tvg::PathCommand::MoveTo:
        {
            CloseSubpathIfFilled();
            Cursor = ContourStart = Apply(Effective, Points[PointIndex++]);
            TrackPoint(Shape, Cursor);
            Open = true;
            break;
        }
        case tvg::PathCommand::LineTo:
        {
            if (!Open) { ++PointIndex; break; }
            const Coordinate Finish = Apply(Effective, Points[PointIndex++]);
            Shape.Segments.push_back({{Cursor[0], Cursor[1], Cursor[0], Cursor[1]}, {Finish[0], Finish[1], Finish[0], Finish[1]}});
            TrackPoint(Shape, Finish);
            Cursor = Finish;
            break;
        }
        case tvg::PathCommand::CubicTo:
        {
            if (!Open) { PointIndex += 3; break; }
            const Coordinate ControlA = Apply(Effective, Points[PointIndex++]);
            const Coordinate ControlB = Apply(Effective, Points[PointIndex++]);
            const Coordinate Finish = Apply(Effective, Points[PointIndex++]);
            Shape.Segments.push_back({{Cursor[0], Cursor[1], ControlA[0], ControlA[1]}, {ControlB[0], ControlB[1], Finish[0], Finish[1]}});
            TrackPoint(Shape, ControlA); TrackPoint(Shape, ControlB); TrackPoint(Shape, Finish);
            Cursor = Finish;
            break;
        }
        case tvg::PathCommand::Close:
        {
            if (Open && (Cursor[0] != ContourStart[0] || Cursor[1] != ContourStart[1]))
                Shape.Segments.push_back({{Cursor[0], Cursor[1], Cursor[0], Cursor[1]}, {ContourStart[0], ContourStart[1], ContourStart[0], ContourStart[1]}});
            Cursor = ContourStart;
            Open = false;
            break;
        }
        }
    }
    // Filled contours require closed rings for exact winding; stroke-only open paths stay open so no spurious chord is drawn.
    CloseSubpathIfFilled();

    if (!Shape.Segments.empty() && Shape.Minimum[0] <= Shape.Maximum[0] && Shape.Minimum[1] <= Shape.Maximum[1])
        Shapes.push_back(std::move(Shape));
}

void WalkNode(const tvg::Paint &Node, const tvg::Matrix &Parent, float ParentOpacity, std::vector<ImportedShape> &Shapes, uint32_t &Budget)
{
    if (!Budget) return;
    const float NodeOpacity = ParentOpacity * (Node.opacity() / 255.f);
    if (NodeOpacity <= 0.f) return;
    const auto &Local = const_cast<tvg::Paint &>(Node).transform();
    const auto Effective = Multiply(Parent, Local);
    if (Node.type() == tvg::Type::Shape) { CollectShape(static_cast<const tvg::Shape &>(Node), Effective, ParentOpacity, Shapes); --Budget; }
    else if (Node.type() == tvg::Type::Scene)
        for (const auto *Child : static_cast<const tvg::Scene &>(Node).paints())
        {
            if (!Budget) break;
            WalkNode(*Child, Effective, NodeOpacity, Shapes, Budget);
        }
}
} // namespace

uint32_t AppendStampVectorImport(VectorAuthoringDocument &Document, std::string_view SvgMarkup)
{
    if (SvgMarkup.empty() || SvgMarkup.size() > StampVectorMarkupLimit) throw std::runtime_error("Stamp vector markup is out of its authored bound.");

    if (tvg::Initializer::init(1) != tvg::Result::Success) throw std::runtime_error("The ThorVG software renderer could not start.");
    struct TerminateGuard { ~TerminateGuard() { tvg::Initializer::term(); } } Terminate;

    auto *Picture = tvg::Picture::gen();
    if (!Picture) throw std::runtime_error("Could not allocate the Stamp vector import picture.");
    struct PictureGuard { tvg::Picture *P; ~PictureGuard() { P->unref(true); } } PictureOwner{Picture};
    if (Picture->load(SvgMarkup.data(), uint32_t(SvgMarkup.size()), "svg") != tvg::Result::Success)
        throw std::runtime_error("The Stamp vector import could not parse that SVG markup.");

    auto *Walker = tvg::Accessor::gen();
    if (!Walker) throw std::runtime_error("Could not allocate the Stamp vector import accessor.");
    struct AccessorGuard { tvg::Accessor *A; ~AccessorGuard() { delete A; } } AccessorOwner{Walker};
    const tvg::Paint *Root = nullptr;
    bool Seen = false;
    (void)Walker->set(Picture, [&Root, &Seen](const tvg::Paint *Node, void *) -> bool {
        if (!Seen) { Seen = true; return true; }
        Root = Node;
        return false;
    }, nullptr);

    std::vector<ImportedShape> Shapes;
    uint32_t Budget = 256;
    if (Root) WalkNode(*Root, tvg::Matrix{1, 0, 0, 0, 1, 0, 0, 0, 1}, 1.f, Shapes, Budget);
    if (Shapes.empty()) throw std::runtime_error("That SVG resolved to no renderable shape.");

    uint32_t PreviousIndex = UINT32_MAX;
    for (const auto &Shape : Shapes)
    {
        const float Width = std::max(.01f, Shape.Maximum[0] - Shape.Minimum[0]), Height = std::max(.01f, Shape.Maximum[1] - Shape.Minimum[1]);
        const float CentreX = (Shape.Minimum[0] + Shape.Maximum[0]) * .5f, CentreY = (Shape.Minimum[1] + Shape.Maximum[1]) * .5f;
        const auto SegmentOffset = uint32_t(Document.Segments.size());
        for (auto Segment : Shape.Segments)
        {
            Segment.StartControl[0] = (Segment.StartControl[0] - CentreX) / Width; Segment.StartControl[1] = (Segment.StartControl[1] - CentreY) / Height;
            Segment.StartControl[2] = (Segment.StartControl[2] - CentreX) / Width; Segment.StartControl[3] = (Segment.StartControl[3] - CentreY) / Height;
            Segment.ControlEnd[0] = (Segment.ControlEnd[0] - CentreX) / Width; Segment.ControlEnd[1] = (Segment.ControlEnd[1] - CentreY) / Height;
            Segment.ControlEnd[2] = (Segment.ControlEnd[2] - CentreX) / Width; Segment.ControlEnd[3] = (Segment.ControlEnd[3] - CentreY) / Height;
            Document.Segments.push_back(Segment);
        }
        const bool HasFill = Shape.Fill.Active;
        const bool HasStroke = Shape.Stroke.Active && Shape.StrokeWidth > 0.f;
        VectorCoverageInstruction Instruction;
        Instruction.Topology = {6, SegmentOffset, uint32_t(Shape.Segments.size()),
            (Shape.EvenOdd ? 1u : 0u) | (HasStroke ? 0x80000000u : 0u)};
        Instruction.Bounds = {CentreX, CentreY, Width, Height};
        Instruction.Transform = {0.f, HasFill ? 0.f : Shape.StrokeWidth, Shape.Opacity, HasFill ? Shape.Fill.Category : 0.f};
        Instruction.Colour = HasFill ? Shape.Fill.Colour : Shape.Stroke.Colour;
        Instruction.ColourEnd = HasFill ? Shape.Fill.ColourEnd : Shape.Stroke.ColourEnd;
        Instruction.GradientStopColour2 = Shape.Fill.StopColour2;
        Instruction.GradientStopColour3 = Shape.Fill.StopColour3;
        Instruction.GradientStopPositions = Shape.Fill.StopPositions;
        if (HasFill && Shape.Fill.Category != 0.f)
            Instruction.GradientAxis = NormalizeGradientAxis(Shape.Fill.CanvasAxisStart, Shape.Fill.CanvasAxisEnd, CentreX, CentreY, Width, Height);
        Instruction.StrokeColour = HasStroke ? Shape.Stroke.Colour : Instruction.Colour;
        Instruction.StrokeColourEnd = HasStroke ? Shape.Stroke.ColourEnd : Instruction.ColourEnd;
        Instruction.StrokeGradientStopColour2 = Shape.Stroke.StopColour2;
        Instruction.StrokeGradientStopColour3 = Shape.Stroke.StopColour3;
        Instruction.StrokeGradientStopPositions = Shape.Stroke.StopPositions;
        Instruction.ShapeParameters = {0.f, HasStroke ? Shape.Stroke.Category : 0.f, 0.f, 0.f};
        if (HasStroke && Shape.Stroke.Category != 0.f)
            Instruction.StrokeGradientAxis = NormalizeGradientAxis(Shape.Stroke.CanvasAxisStart, Shape.Stroke.CanvasAxisEnd, CentreX, CentreY, Width, Height);
        Instruction.Paint = {HasFill && HasStroke ? Shape.StrokeWidth : 0.f, .45f, 6.f, HasFill ? 1.f : 0.f};
        const auto ShapeIndex = uint32_t(Document.Instructions.size());
        Document.Instructions.push_back(Instruction);

        if (PreviousIndex == UINT32_MAX) PreviousIndex = ShapeIndex;
        else
        {
            VectorCoverageInstruction Over;
            Over.Topology = {28, PreviousIndex, ShapeIndex, 0}; // 28 = Over: keeps each shape's own colour
            PreviousIndex = uint32_t(Document.Instructions.size());
            Document.Instructions.push_back(Over);
        }
    }
    return PreviousIndex;
}
}
