//============================================================================================================================================
//                                                           TREADREGIONSOLVER.CPP
//============================================================================================================================================
// 📦 Layer polygons, nested regions and the shells between them, over Clipper2 integer arithmetic.

#include "TreadRegionSolver.h"

#include "clipper2/clipper.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace Frontier {

using Clipper2Lib::Path64;
using Clipper2Lib::Paths64;
using Clipper2Lib::Point64;
using Clipper2Lib::FillRule;
using Clipper2Lib::JoinType;
using Clipper2Lib::EndType;

namespace {

/// 📝 💡 1000 integer units per millimetre, so one unit is one micrometre — deliberately the same figure
///    as TyreMeshStructure::WeldTolerance. Coincident boundaries can disagree by one unit, and tying the
///    boolean resolution to the weld tolerance means any sliver that disagreement can produce is narrower
///    than a weld and collapses to nothing when the mesh stage welds it. The two tolerances are one choice.
constexpr double ClipperScale = 1000.0;                // [-]  - integer units per millimetre, 1 µm
constexpr float  π            = 3.14159265358979323846f;

//------------------------------------------------------------------------------------------------------------------------
//                                                       PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] Point64 ToClipper(double Circumferential, double Lateral) noexcept
{
    return Point64(static_cast<int64_t>(std::llround(Circumferential * ClipperScale)),
                   static_cast<int64_t>(std::llround(Lateral * ClipperScale)));
}

[[nodiscard]] Path64 RectanglePath(double X0, double Y0, double X1, double Y1) noexcept
{
    return Path64{ ToClipper(X0, Y0), ToClipper(X1, Y0), ToClipper(X1, Y1), ToClipper(X0, Y1) };
}

/// 📝 Open polylines become filled groove areas by inflating them to half the groove width. Round joins
///    match a mould cutter; a butt end leaves the slot open where it runs off the shoulder.
[[nodiscard]] Paths64 StrokeToArea(const Paths64& Lines, double Width, bool Closed) noexcept
{
    if (Lines.empty() || Width <= 0.0)
        return Paths64{};
    return Clipper2Lib::InflatePaths(Lines, Width * 0.5 * ClipperScale, JoinType::Round,
                                     Closed ? EndType::Joined : EndType::Butt);
}

[[nodiscard]] Paths64 ShiftBy(const Paths64& Source, double Offset) noexcept
{
    const int64_t Step = static_cast<int64_t>(std::llround(Offset * ClipperScale));
    Paths64 Moved = Source;
    for (Path64& Single : Moved)
        for (Point64& Vertex : Single)
            Vertex.x += Step;
    return Moved;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     LAYER POLYGONS
//------------------------------------------------------------------------------------------------------------------------

/// 📝 One layer's grooves as filled areas. Layers that wrap are repeated one circumference either side
///    so the seam at x = 0 is cut by the same geometry from both directions.
[[nodiscard]] Paths64 LayerArea(const TreadLayerSpecification& Layer,
                                const TreadDerivedValues&      Derived,
                                bool&                          Wraps) noexcept
{
    const double TreadHalf     = Derived.TreadHalf;
    const double Circumference = Derived.Circumference;
    Wraps = true;

    switch (Layer.Kind)
    {
    case TreadLayerKind::Circumferential:
    {
        const double Seat = Layer.Position * TreadHalf;
        Wraps = false;
        if (Layer.Zig > 0.0f)
        {
            const int Periods = std::max(2, static_cast<int>(std::lround(Layer.ZigCount))) * 2;
            Path64 Zigzag;
            for (int Step = -2; Step <= Periods + 2; ++Step)
            {
                const double X = static_cast<double>(Step) / Periods * Circumference;
                const double Y = Seat + ((std::abs(Step) % 2) ? Layer.Zig : -Layer.Zig);
                Zigzag.push_back(ToClipper(X, Y));
            }
            return StrokeToArea(Paths64{ Zigzag }, Layer.Width, false);
        }
        return Paths64{ RectanglePath(-Circumference, Seat - Layer.Width * 0.5,
                                       2.0 * Circumference, Seat + Layer.Width * 0.5) };
    }

    case TreadLayerKind::Lateral:
    case TreadLayerKind::Chevron:
    case TreadLayerKind::Sipe:
    {
        const double Lean  = std::tan(Layer.Angle * π / 180.0f);
        const int    Count = std::max(1, static_cast<int>(std::lround(Layer.Count)));
        const int    Steps = (Layer.Curve != 0.0f || Layer.Zig != 0.0f) ? 8 : 1;
        Paths64 Lines;

        for (int Repeat = 0; Repeat < Count; ++Repeat)
        {
            const double Origin = (Repeat + Layer.Phase) / Count * Circumference;
            const int    Sides  = Layer.Mirror ? 2 : 1;

            for (int Side = 0; Side < Sides; ++Side)
            {
                const double Sign = Side == 0 ? 1.0 : -1.0;
                Path64 Line;
                for (int Step = 0; Step <= Steps; ++Step)
                {
                    const double Fraction = Layer.From + (Layer.To - Layer.From)
                                          * static_cast<double>(Step) / Steps;
                    const double Across   = (Layer.Mirror ? Sign * Fraction : Fraction) * TreadHalf;
                    const double Measure  = Layer.Mirror ? std::fabs(Fraction) : Fraction;
                    double Along = Origin + Measure * TreadHalf * Lean
                                 + Layer.Curve * Measure * Measure * TreadHalf * 0.5;
                    if (Layer.Zig != 0.0f)
                        Along += ((Step % 2) ? Layer.Zig : -Layer.Zig) * 0.5;
                    Line.push_back(ToClipper(Along, Across));
                }
                Lines.push_back(Line);
            }
        }
        return StrokeToArea(Lines, Layer.Width, false);
    }

    case TreadLayerKind::Dimple:
    {
        const int Rows  = std::max(1, static_cast<int>(std::lround(Layer.Rows)));
        const int Count = std::max(1, static_cast<int>(std::lround(Layer.Count)));
        Paths64 Discs;

        for (int Row = 0; Row < Rows; ++Row)
        {
            const double Fraction = Rows == 1
                                  ? (Layer.From + Layer.To) * 0.5
                                  : Layer.From + (Layer.To - Layer.From) * Row / (Rows - 1);
            const double Across = Fraction * TreadHalf;
            const double Offset = (Layer.Stagger && (Row % 2)) ? 0.5 : 0.0;
            const int    Facets = std::clamp(static_cast<int>(std::lround(Layer.Radius * 6.0f)), 12, 48);

            for (int Index = 0; Index < Count; ++Index)
            {
                const double Centre = (Index + Offset) / Count * Circumference;
                Path64 Disc;
                for (int Facet = 0; Facet < Facets; ++Facet)
                {
                    const double Angle = 2.0 * π * Facet / Facets;
                    Disc.push_back(ToClipper(Centre + Layer.Radius * std::cos(Angle),
                                             Across + Layer.Radius * std::sin(Angle)));
                }
                Discs.push_back(Disc);
            }
        }
        if (Layer.Ring)
            return StrokeToArea(Discs, std::max(0.6f, Layer.Radius * 0.4f), true);
        return Clipper2Lib::Union(Discs, FillRule::NonZero);
    }

    case TreadLayerKind::Hexagon:
    {
        const double Size    = std::max(2.0f, Layer.Size);
        const int    Columns = std::max(2, 2 * static_cast<int>(std::lround(Circumference / (3.0 * Size))));
        const double PitchX  = Circumference / Columns;
        const double PitchY  = std::sqrt(3.0) * Size;
        const double Lower   = Layer.From * TreadHalf;
        const double Upper   = Layer.To * TreadHalf;
        const double Turn    = Layer.Rotate * π / 180.0f;
        const int    RowLow  = static_cast<int>(std::floor((Lower - PitchY) / PitchY));
        const int    RowHigh = static_cast<int>(std::ceil((Upper + PitchY) / PitchY));
        Paths64 Cells;

        for (int Column = -1; Column <= Columns; ++Column)
        {
            for (int Row = RowLow; Row <= RowHigh; ++Row)
            {
                const double Cx = Column * PitchX;
                const double Cy = Row * PitchY + ((std::abs(Column) % 2) ? PitchY * 0.5 : 0.0);
                Path64 Cell;
                for (int Vertex = 0; Vertex < 6; ++Vertex)
                {
                    const double Angle = Turn + Vertex * π / 3.0;
                    Cell.push_back(ToClipper(Cx + Size * std::cos(Angle), Cy + Size * std::sin(Angle)));
                }
                Cells.push_back(Cell);
            }
        }
        Wraps = false;
        const Paths64 Lattice = StrokeToArea(Cells, Layer.Width, true);
        const Paths64 Band{ RectanglePath(-Circumference, Lower, 2.0 * Circumference, Upper) };
        return Clipper2Lib::Intersect(Lattice, Band, FillRule::NonZero);
    }

    case TreadLayerKind::Noise:
    default:
        return Paths64{};
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   CONTOUR EXTRACTION
//------------------------------------------------------------------------------------------------------------------------

/// 📝 Clipper2 returns outers counter-clockwise with positive area and holes clockwise with negative
///    area under NonZero, so the sign classifies a contour without walking a nesting tree.
void AppendContours(const Paths64& Source, std::vector<TreadContour>& Target) noexcept
{
    for (const Path64& Single : Source)
    {
        if (Single.size() < 3)
            continue;

        TreadContour Contour;
        Contour.Hole = Clipper2Lib::Area(Single) < 0.0;
        Contour.Points.reserve(Single.size());
        for (const Point64& Vertex : Single)
        {
            Contour.Points.push_back(TreadContourPoint{
                static_cast<float>(static_cast<double>(Vertex.x) / ClipperScale),
                static_cast<float>(static_cast<double>(Vertex.y) / ClipperScale) });
        }
        Target.push_back(std::move(Contour));
    }
}

[[nodiscard]] double NetArea(const Paths64& Source) noexcept
{
    double Total = 0.0;
    for (const Path64& Single : Source)
        Total += Clipper2Lib::Area(Single);
    return Total / (ClipperScale * ClipperScale);
}

[[nodiscard]] double ContourLength(const Paths64& Source) noexcept
{
    double Total = 0.0;
    for (const Path64& Single : Source)
    {
        for (size_t Index = 0; Index < Single.size(); ++Index)
        {
            const Point64& From = Single[Index];
            const Point64& To   = Single[(Index + 1u) % Single.size()];
            const double   Dx   = static_cast<double>(To.x - From.x);
            const double   Dy   = static_cast<double>(To.y - From.y);
            Total += std::sqrt(Dx * Dx + Dy * Dy);
        }
    }
    return Total / ClipperScale;
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      REGION SOLVE
//------------------------------------------------------------------------------------------------------------------------

TreadRegionResult SolveTreadRegions(const TreadPatternSpecification& Pattern,
                                    const TreadSpecification&        Specification,
                                    const TreadDerivedValues&        Derived) noexcept
{
    TreadRegionResult Result;

    const double Circumference = Derived.Circumference;
    const double AcrossHalf    = Derived.AcrossHalf;
    const Paths64 Domain{ RectanglePath(0.0, -AcrossHalf, Circumference, AcrossHalf) };
    Result.DomainArea = NetArea(Domain);

    // 📝 ① Collect every layer's area, keyed by the depth fraction it cuts to. Wear raises the floor: a
    //    layer shallower than the rubber already rubbed off contributes nothing and is dropped here.
    std::vector<std::pair<double, Paths64>> ByDepth;

    for (const TreadLayerSpecification& Layer : Pattern.Layers)
    {
        if (Layer.Kind == TreadLayerKind::Noise)
            continue;

        bool    Wraps = true;
        Paths64 Area  = LayerArea(Layer, Derived, Wraps);
        if (Area.empty())
            continue;

        if (Wraps)
        {
            const Paths64 Ahead  = ShiftBy(Area, Circumference);
            const Paths64 Behind = ShiftBy(Area, -Circumference);
            Area.insert(Area.end(), Ahead.begin(), Ahead.end());
            Area.insert(Area.end(), Behind.begin(), Behind.end());
        }

        const double Depth = std::round(static_cast<double>(Layer.DepthFraction) * 100.0) / 100.0;
        if (Depth <= static_cast<double>(Specification.Wear) + 0.005)
            continue;

        auto Existing = std::find_if(ByDepth.begin(), ByDepth.end(),
                                     [Depth](const auto& Entry) { return Entry.first == Depth; });
        if (Existing == ByDepth.end())
            ByDepth.emplace_back(Depth, Area);
        else
            Existing->second.insert(Existing->second.end(), Area.begin(), Area.end());
    }

    // 📝 ② Deepest first, so each region accumulates everything cut at least that deep.
    std::sort(ByDepth.begin(), ByDepth.end(),
              [](const auto& Left, const auto& Right) { return Left.first > Right.first; });

    std::vector<std::pair<double, Paths64>> Regions;
    Paths64 Accumulated;
    for (const auto& Entry : ByDepth)
    {
        Accumulated.insert(Accumulated.end(), Entry.second.begin(), Entry.second.end());
        Accumulated = Clipper2Lib::Union(Accumulated, FillRule::NonZero);
        Regions.insert(Regions.begin(),
                       std::make_pair(Entry.first, Clipper2Lib::Intersect(Accumulated, Domain,
                                                                          FillRule::NonZero)));
    }

    // 📝 ③ The crown is whatever no layer touched; every other floor is the shell between consecutive
    //    regions. Shells are disjoint by construction because Rᵢ₊₁ ⊆ Rᵢ.
    Paths64 CrownArea = Regions.empty()
                      ? Domain
                      : Clipper2Lib::Difference(Domain, Regions.front().second, FillRule::NonZero);

    TreadFloorPiece Crown;
    Crown.Depth = 0.0f;
    AppendContours(CrownArea, Crown.Contours);
    Result.Pieces.push_back(std::move(Crown));
    Result.Levels.push_back(0.0f);

    std::vector<Paths64> Shells;
    Shells.push_back(CrownArea);

    for (size_t Index = 0; Index < Regions.size(); ++Index)
    {
        const Paths64 Shell = (Index + 1u < Regions.size())
                            ? Clipper2Lib::Difference(Regions[Index].second, Regions[Index + 1u].second,
                                                      FillRule::NonZero)
                            : Regions[Index].second;

        TreadFloorPiece Piece;
        Piece.Depth = static_cast<float>(Regions[Index].first) * Specification.TreadDepth;
        AppendContours(Shell, Piece.Contours);
        Result.Pieces.push_back(std::move(Piece));
        Result.Levels.push_back(static_cast<float>(Regions[Index].first) * Specification.TreadDepth);
        Shells.push_back(Shell);
    }

    // 📝 ④ Measure the partition as set operations. Everything the pieces cover, unioned once, must be
    //    the domain: what the domain has and the union lacks is a gap, what the union has and the domain
    //    lacks is spill. Both are areas of real regions, not differences of two large sums.
    Paths64 Everything;
    for (const Paths64& Shell : Shells)
        Everything.insert(Everything.end(), Shell.begin(), Shell.end());

    const Paths64 Covered = Clipper2Lib::Union(Everything, FillRule::NonZero);
    Result.GapArea        = NetArea(Clipper2Lib::Difference(Domain, Covered, FillRule::NonZero));
    Result.ExcessArea     = NetArea(Clipper2Lib::Difference(Covered, Domain, FillRule::NonZero));
    // 📝 ⚠️ Summed over the pieces, not over their union. Slivers form where two pieces meet, and the
    //    union has no such boundary — its contour is just the rim of the domain. Summing the pieces counts
    //    every shared boundary twice, which is the right side to err on for a tolerance.
    for (const Paths64& Shell : Shells)
        Result.BoundaryLength += ContourLength(Shell);

    // 📝 ⑤ Overlap is what a pair shares. Accumulated against the running union so a region covered by
    //    three pieces is counted twice, which is what "area counted more than once" has to mean.
    Paths64 Running;
    for (const Paths64& Shell : Shells)
    {
        if (!Running.empty())
            Result.OverlapArea += NetArea(Clipper2Lib::Intersect(Running, Shell, FillRule::NonZero));
        Running.insert(Running.end(), Shell.begin(), Shell.end());
        Running = Clipper2Lib::Union(Running, FillRule::NonZero);
    }

    return Result;
}

}   // namespace Frontier
