//============================================================================================================================================
//                                                            TREADMESHSOLVER.CPP
//============================================================================================================================================
// 📦 Grid-clipped floors, extruded groove walls, and the mapping from the unrolled domain onto the carcass.

#include "TreadMeshSolver.h"
#include "TyreProfileSpecification.h"

#include "clipper2/clipper.h"
#include "mapbox/earcut.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier {

using Clipper2Lib::Path64;
using Clipper2Lib::Paths64;
using Clipper2Lib::Point64;
using Clipper2Lib::FillRule;

namespace {

constexpr double ClipperScale = 1000.0;                // [-]  - must match TreadRegionSolver
constexpr float  π            = 3.14159265358979323846f;

//------------------------------------------------------------------------------------------------------------------------
//                                                   DOMAIN TO CARCASS
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Places one point of the unrolled tread onto the moulded carcass at a given depth.
/// note  📐 The depth is scaled by the profile's contact weight, so a groove fades out as the shoulder
///       turns away. Cutting a groove at full depth where the surface is nearly parallel to the axis would
///       drive the floor through the sidewall.
struct PlacedPoint
{
    float X, Y, Z;      // [mm]  - carcass position
    float Nx, Ny, Nz;   // [-]   - outward normal of the moulded surface at that point
};

[[nodiscard]] PlacedPoint Place(double                    Circumferential,
                                double                    Lateral,
                                double                    Depth,
                                const TreadDerivedValues& Derived,
                                const TreadSpecification& Specification) noexcept
{
    const TyreProfileSample Sample = EvaluateTyreProfile(static_cast<float>(Lateral),
                                                         Specification, Derived);
    const double Sink  = Depth * static_cast<double>(Sample.ContactWeight);
    const double Axial = Sample.Lateral - Sample.NormalLateral * Sink;
    const double Reach = Sample.Radius  - Sample.NormalRadial  * Sink;
    const double θ     = Circumferential / Derived.Circumference * 2.0 * π;

    PlacedPoint Result;
    Result.X  = static_cast<float>(Axial);
    Result.Y  = static_cast<float>(Reach * std::cos(θ));
    Result.Z  = static_cast<float>(Reach * std::sin(θ));
    Result.Nx = Sample.NormalLateral;
    Result.Ny = static_cast<float>(Sample.NormalRadial * std::cos(θ));
    Result.Nz = static_cast<float>(Sample.NormalRadial * std::sin(θ));
    return Result;
}

[[nodiscard]] TyreCornerRecord Corner(const PlacedPoint&        Point,
                                      double                    Circumferential,
                                      double                    Lateral,
                                      const TreadDerivedValues& Derived) noexcept
{
    TyreCornerRecord Record;
    Record.NormalX = Point.Nx;
    Record.NormalY = Point.Ny;
    Record.NormalZ = Point.Nz;
    Record.U       = static_cast<float>(Circumferential / Derived.Circumference);
    Record.V       = static_cast<float>((Lateral + Derived.AcrossHalf) / (2.0 * Derived.AcrossHalf));
    return Record;
}

/// 📦 Welds a domain point at a depth and returns its index together with its shading corner.
struct WeldedPoint
{
    uint32_t         Index = 0u;
    TyreCornerRecord Attribute;
};

[[nodiscard]] WeldedPoint Weld(double                    Circumferential,
                               double                    Lateral,
                               double                    Depth,
                               const TreadDerivedValues& Derived,
                               const TreadSpecification& Specification,
                               TyreMeshStructure&        Mesh) noexcept
{
    const PlacedPoint Point = Place(Circumferential, Lateral, Depth, Derived, Specification);
    WeldedPoint Result;
    Result.Index     = Mesh.WeldPosition(Point.X, Point.Y, Point.Z);
    Result.Attribute = Corner(Point, Circumferential, Lateral, Derived);
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          GRID
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Lateral grid lines, refined where the shoulder arc starts to turn.
/// note  💡 A uniform lateral pitch wastes vertices on the flat crown and still under-resolves the
///       shoulder, where the surface is curving hardest and the silhouette is decided.
[[nodiscard]] std::vector<double> LateralLines(const TreadDerivedValues& Derived,
                                               const TreadMeshSettings&  Settings) noexcept
{
    std::vector<double> Lines;
    const double Across    = Derived.AcrossHalf;
    const double TreadHalf = Derived.TreadHalf;
    const double Coarse    = std::max(0.25, static_cast<double>(Settings.CellLateral));
    const double Fine      = std::max(0.25, static_cast<double>(Settings.ShoulderLateral));

    const int CrownSteps = std::max(1, static_cast<int>(std::ceil(2.0 * TreadHalf / Coarse)));
    for (int Step = 0; Step <= CrownSteps; ++Step)
        Lines.push_back(-TreadHalf + 2.0 * TreadHalf * Step / CrownSteps);

    const double Shoulder = Across - TreadHalf;
    const int    Steps    = std::max(1, static_cast<int>(std::ceil(Shoulder / Fine)));
    for (int Step = 1; Step <= Steps; ++Step)
    {
        const double Offset = Shoulder * Step / Steps;
        Lines.push_back(TreadHalf + Offset);
        Lines.push_back(-TreadHalf - Offset);
    }

    std::sort(Lines.begin(), Lines.end());
    Lines.erase(std::unique(Lines.begin(), Lines.end(),
                            [](double L, double R) { return std::fabs(L - R) < 1.0e-4; }),
                Lines.end());
    return Lines;
}

[[nodiscard]] int64_t Unit(double Millimetres) noexcept
{
    return static_cast<int64_t>(std::llround(Millimetres * ClipperScale));
}

[[nodiscard]] Path64 CellPath(double X0, double Y0, double X1, double Y1) noexcept
{
    return Path64{ Point64(Unit(X0), Unit(Y0)), Point64(Unit(X1), Unit(Y0)),
                   Point64(Unit(X1), Unit(Y1)), Point64(Unit(X0), Unit(Y1)) };
}

[[nodiscard]] Paths64 ToPaths(const std::vector<TreadContour>& Contours) noexcept
{
    Paths64 Result;
    Result.reserve(Contours.size());
    for (const TreadContour& Contour : Contours)
    {
        Path64 Single;
        Single.reserve(Contour.Points.size());
        for (const TreadContourPoint& Point : Contour.Points)
            Single.push_back(Point64(Unit(Point.Circumferential), Unit(Point.Lateral)));
        Result.push_back(std::move(Single));
    }
    return Result;
}

/// 📦 Inserts a vertex wherever a path crosses a grid line, in the boolean stage's own integer space.
/// note  ⚠️ The band and the walls must both be subdivided, and by the same lines. A vertex one side has
///       and the other lacks is a T-junction, and a T-junction is the one crack welding cannot close.
[[nodiscard]] Path64 SubdivideGrid(const Path64&              Loop,
                                   double                     Circumference,
                                   int                        Columns,
                                   const std::vector<double>& Lateral) noexcept
{
    Path64 Result;
    const size_t Count = Loop.size();
    Result.reserve(Count * 2u);
    const double Pitch = Circumference / Columns;

    for (size_t Step = 0; Step < Count; ++Step)
    {
        const Point64& From = Loop[Step];
        const Point64& To   = Loop[(Step + 1u) % Count];
        Result.push_back(From);

        const double Ax = static_cast<double>(From.x), Ay = static_cast<double>(From.y);
        const double Dx = static_cast<double>(To.x) - Ax, Dy = static_cast<double>(To.y) - Ay;

        // 📝 A crossing carries the grid coordinate it lies on, snapped with the identical
        //    expression the cell grid uses, and only the other coordinate is interpolated.
        //    ⚠️ Interpolating both is what broke the seam. Circumference * Line / Columns and
        //    Ax + Dx * Fraction are the same real number reached by different arithmetic, so they
        //    disagree in the last bit and llround turns that into one integer — one micrometre, which
        //    is precisely WeldTolerance, so the two sides fail to weld. Worse, the error depends on
        //    Ax and Dx, so an edge walked in +x rounds differently from one walked in −x: one side of
        //    a groove closed and the other opened.
        struct GridCut
        {
            double  Fraction;   // [0..1]  - position along this edge
            int64_t Snapped;    // [µm]    - the exact grid coordinate
            bool    Across;     // []      - true when Snapped is lateral, false when circumferential
        };
        std::vector<GridCut> Crossings;

        if (std::fabs(Dx) > 0.5)
        {
            const double Low  = std::min(Ax, static_cast<double>(To.x)) / ClipperScale;
            const double High = std::max(Ax, static_cast<double>(To.x)) / ClipperScale;
            for (int Line = static_cast<int>(std::floor(Low / Pitch)) + 1; Line * Pitch < High; ++Line)
            {
                const int64_t Snapped  = Unit(Circumference * Line / Columns);
                const double  Fraction = (static_cast<double>(Snapped) - Ax) / Dx;
                if (Fraction > 1.0e-9 && Fraction < 1.0 - 1.0e-9)
                    Crossings.push_back(GridCut{ Fraction, Snapped, false });
            }
        }

        if (std::fabs(Dy) > 0.5)
        {
            for (const double Line : Lateral)
            {
                const int64_t Snapped  = Unit(Line);
                const double  Fraction = (static_cast<double>(Snapped) - Ay) / Dy;
                if (Fraction > 1.0e-9 && Fraction < 1.0 - 1.0e-9)
                    Crossings.push_back(GridCut{ Fraction, Snapped, true });
            }
        }

        std::sort(Crossings.begin(), Crossings.end(),
                  [](const GridCut& Left, const GridCut& Right)
                  { return Left.Fraction < Right.Fraction; });

        for (const GridCut& Crossing : Crossings)
        {
            const Point64 Cut = Crossing.Across
                ? Point64(static_cast<int64_t>(std::llround(Ax + Dx * Crossing.Fraction)), Crossing.Snapped)
                : Point64(Crossing.Snapped, static_cast<int64_t>(std::llround(Ay + Dy * Crossing.Fraction)));
            if (Cut != Result.back())
                Result.push_back(Cut);
        }
    }
    return Result;
}

/// 📦 One outer loop with the holes that belong to it, ready to triangulate.
struct BandFigure
{
    Path64              Outer;
    std::vector<Path64> Holes;
};

/// 📦 Flattens a Clipper nesting tree into outer-plus-holes figures.
/// note  ⚠️ Orientation alone cannot do this. A negative area says "this is a hole", not "this is a hole
///       in that one", and a block band routinely holds several islands each with their own holes. Handing
///       a triangulator the wrong pairing fills a groove in or cuts a tread block away.
void CollectFigures(const Clipper2Lib::PolyPath64& Node, std::vector<BandFigure>& Figures) noexcept
{
    for (const auto& Child : Node)
    {
        if (Child->IsHole())
        {
            for (const auto& Inner : *Child)
                CollectFigures(*Inner, Figures);
            continue;
        }

        BandFigure Figure;
        Figure.Outer = Child->Polygon();
        for (const auto& Hole : *Child)
        {
            Figure.Holes.push_back(Hole->Polygon());
            for (const auto& Inner : *Hole)
                CollectFigures(*Inner, Figures);
        }
        Figures.push_back(std::move(Figure));
    }
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       MESH SOLVE
//------------------------------------------------------------------------------------------------------------------------

TreadMeshMetrics SolveTreadMesh(const TreadRegionResult&  Regions,
                                const TreadSpecification& Specification,
                                const TreadDerivedValues& Derived,
                                const TreadMeshSettings&  Settings,
                                TyreMeshStructure&        Mesh) noexcept
{
    TreadMeshMetrics Metrics;
    Mesh.Clear();

    const double Circumference = Derived.Circumference;
    const std::vector<double> Lateral = LateralLines(Derived, Settings);
    const double Pitch   = std::max(0.25, static_cast<double>(Settings.CellCircumferential));
    const int    Columns = std::max(1, static_cast<int>(std::llround(Circumference / Pitch)));

    // 📝 ① Floors. Each piece is clipped against the grid in three levels — row, block, cell — because
    //    clipping ten thousand cells against six hundred contours directly is quadratic and this is not.
    //    Each level narrows the geometry the next one has to consider.
    constexpr int BlockWidth = 16;

    for (const TreadFloorPiece& Piece : Regions.Pieces)
    {
        const Paths64 PiecePaths = ToPaths(Piece.Contours);
        if (PiecePaths.empty())
            continue;

        const double Depth = static_cast<double>(Piece.Depth);

        for (size_t Row = 0; Row + 1u < Lateral.size(); ++Row)
        {
            const double Y0 = Lateral[Row];
            const double Y1 = Lateral[Row + 1u];
            const Paths64 RowPaths = Clipper2Lib::Intersect(
                PiecePaths, Paths64{ CellPath(0.0, Y0, Circumference, Y1) }, FillRule::NonZero);
            if (RowPaths.empty())
                continue;

            // 📝 ② Work a block at a time. A cell the outline never entered is emitted as one quad;
            //    everything the quads leave behind in that block is then taken as ONE region and
            //    triangulated once.
            //    💡 This is the correction that matters. Clipping each cell separately and triangulating
            //    each result made two cells sharing a grid line into two boolean problems that merely
            //    happened to agree — the same mistake the browser prototype made one level up, where it
            //    triangulated each floor piece independently. A single triangulation cannot disagree with
            //    itself, and the band's inner boundary is by construction the outline of the quads it meets.
            //    ⚠️ The band is cut at block edges rather than taken a whole row at a time, because a row
            //    band spans the entire circumference and the triangulator exhausts memory on it. Block
            //    edges are grid lines and both sides are subdivided by the same lines, so the seam closes.
            for (int Block = 0; Block < Columns; Block += BlockWidth)
            {
                const int    Last = std::min(Columns, Block + BlockWidth);
                const double Bx0  = Circumference * Block / Columns;
                const double Bx1  = Circumference * Last  / Columns;
                const Paths64 BlockPaths = Clipper2Lib::Intersect(
                    RowPaths, Paths64{ CellPath(Bx0, Y0, Bx1, Y1) }, FillRule::NonZero);
                if (BlockPaths.empty())
                    continue;

                Paths64 Whole;

                for (int Column = Block; Column < Last; ++Column)
                {
                    const double X0 = Circumference * Column / Columns;
                    const double X1 = Circumference * (Column + 1) / Columns;
                    const Path64 Cell = CellPath(X0, Y0, X1, Y1);
                    const Paths64 Clipped = Clipper2Lib::Intersect(BlockPaths, Paths64{ Cell },
                                                                   FillRule::NonZero);
                    if (Clipped.size() != 1u || Clipped.front().size() != 4u)
                        continue;
                    if (std::fabs(std::fabs(Clipper2Lib::Area(Clipped.front()))
                                  - std::fabs(Clipper2Lib::Area(Cell))) > 1.0)
                        continue;

                    std::vector<WeldedPoint> Ring;
                    Ring.reserve(4u);
                    for (const Point64& Vertex : Clipped.front())
                    {
                        Ring.push_back(Weld(static_cast<double>(Vertex.x) / ClipperScale,
                                            static_cast<double>(Vertex.y) / ClipperScale,
                                            Depth, Derived, Specification, Mesh));
                    }
                    Mesh.AddQuad(Ring[0].Index, Ring[1].Index, Ring[2].Index, Ring[3].Index,
                                 Ring[0].Attribute, Ring[1].Attribute,
                                 Ring[2].Attribute, Ring[3].Attribute);
                    ++Metrics.FloorQuad;
                    Whole.push_back(Cell);
                }

                Paths64 Band = Whole.empty()
                             ? BlockPaths
                             : Clipper2Lib::Difference(BlockPaths,
                                                       Clipper2Lib::Union(Whole, FillRule::NonZero),
                                                       FillRule::NonZero);
                if (Band.empty())
                    continue;

                Clipper2Lib::Clipper64 Divider;
                Divider.AddSubject(BlockPaths);
                if (!Whole.empty())
                    Divider.AddClip(Clipper2Lib::Union(Whole, FillRule::NonZero));
                Clipper2Lib::PolyTree64 Tree;
                Divider.Execute(Whole.empty() ? Clipper2Lib::ClipType::Union
                                              : Clipper2Lib::ClipType::Difference,
                                FillRule::NonZero, Tree);

                std::vector<BandFigure> Figures;
                CollectFigures(Tree, Figures);

                Paths64 Facets;
                for (const BandFigure& Figure : Figures)
                {
                    std::vector<std::vector<std::array<double, 2>>> Rings;
                    std::vector<Point64>                            Lookup;

                    auto Append = [&](const Path64& Loop)
                    {
                        const Path64 Dense = SubdivideGrid(Loop, Circumference, Columns, Lateral);
                        if (Dense.size() < 3u)
                            return;
                        std::vector<std::array<double, 2>> Ring;
                        Ring.reserve(Dense.size());
                        for (const Point64& Vertex : Dense)
                        {
                            Ring.push_back({ static_cast<double>(Vertex.x),
                                             static_cast<double>(Vertex.y) });
                            Lookup.push_back(Vertex);
                        }
                        Rings.push_back(std::move(Ring));
                    };

                    Append(Figure.Outer);
                    for (const Path64& Hole : Figure.Holes)
                        Append(Hole);
                    if (Rings.empty() || Rings.front().size() < 3u)
                        continue;

                    const std::vector<uint32_t> Woven = mapbox::earcut<uint32_t>(Rings);
                    if (Woven.empty())
                    {
                        ++Metrics.BandFailure;
                        continue;
                    }

                    for (size_t Step = 0; Step + 2u < Woven.size(); Step += 3u)
                    {
                        Facets.push_back(Path64{ Lookup[Woven[Step]],
                                                 Lookup[Woven[Step + 1u]],
                                                 Lookup[Woven[Step + 2u]] });
                    }
                }

                for (const Path64& Facet : Facets)
                {
                    if (Facet.size() != 3u)
                        continue;
                    if (std::fabs(Clipper2Lib::Area(Facet)) < ClipperScale * ClipperScale * 1.0e-4)
                        continue;

                    std::vector<WeldedPoint> Ring;
                    Ring.reserve(3u);
                    for (const Point64& Vertex : Facet)
                    {
                        Ring.push_back(Weld(static_cast<double>(Vertex.x) / ClipperScale,
                                            static_cast<double>(Vertex.y) / ClipperScale,
                                            Depth, Derived, Specification, Mesh));
                    }
                    Mesh.AddTriangle(Ring[0].Index, Ring[1].Index, Ring[2].Index,
                                     Ring[0].Attribute, Ring[1].Attribute, Ring[2].Attribute);
                    ++Metrics.FloorTriangle;
                }
            }
        }
    }

    // 📝 ③ Walls. Every piece deeper than the crown is bounded by the outline it was cut along, and the
    //    rubber between that floor and the floor above is a vertical face. Extruding each outline edge
    //    gives exactly one quad, so the walls are pure quads and need no triangulation at all.
    for (size_t Index = 1u; Index < Regions.Pieces.size(); ++Index)
    {
        const TreadFloorPiece& Piece = Regions.Pieces[Index];
        const double Lower = static_cast<double>(Piece.Depth);
        const double Upper = static_cast<double>(Regions.Pieces[Index - 1u].Depth);
        if (Lower <= Upper)
            continue;

        for (const TreadContour& Contour : Piece.Contours)
        {
            if (Contour.Points.size() < 3u)
                continue;

            // 📝 ⚠️ Only the outer contours of a piece border the floor above it. A hole in this piece is
            //    the outline of a deeper piece sitting inside it, and the wall there runs from this floor
            //    down to that one — which the deeper piece raises from its own outer contour. Extruding it
            //    here as well gave the same outline two different walls, three faces on one edge, and the
            //    non-manifold count that exposed it.
            if (Contour.Hole)
                continue;

            Path64 Raw;
            Raw.reserve(Contour.Points.size());
            for (const TreadContourPoint& Point : Contour.Points)
                Raw.push_back(Point64(Unit(Point.Circumferential), Unit(Point.Lateral)));

            const Path64 Dense = SubdivideGrid(Raw, Circumference, Columns, Lateral);
            const size_t Count = Dense.size();

            for (size_t Step = 0; Step < Count; ++Step)
            {
                const TreadContourPoint From{
                    static_cast<float>(static_cast<double>(Dense[Step].x) / ClipperScale),
                    static_cast<float>(static_cast<double>(Dense[Step].y) / ClipperScale) };
                const TreadContourPoint To{
                    static_cast<float>(static_cast<double>(Dense[(Step + 1u) % Count].x) / ClipperScale),
                    static_cast<float>(static_cast<double>(Dense[(Step + 1u) % Count].y) / ClipperScale) };

                const WeldedPoint TopFrom = Weld(From.Circumferential, From.Lateral, Upper,
                                                 Derived, Specification, Mesh);
                const WeldedPoint TopTo   = Weld(To.Circumferential,   To.Lateral,   Upper,
                                                 Derived, Specification, Mesh);
                const WeldedPoint LowFrom = Weld(From.Circumferential, From.Lateral, Lower,
                                                 Derived, Specification, Mesh);
                const WeldedPoint LowTo   = Weld(To.Circumferential,   To.Lateral,   Lower,
                                                 Derived, Specification, Mesh);

                if (TopFrom.Index == TopTo.Index || LowFrom.Index == LowTo.Index)
                    continue;

                Mesh.AddQuad(TopFrom.Index, TopTo.Index, LowTo.Index, LowFrom.Index,
                             TopFrom.Attribute, TopTo.Attribute, LowTo.Attribute, LowFrom.Attribute);
                ++Metrics.WallQuad;
            }
        }
    }

    const double Faces = static_cast<double>(Metrics.FloorQuad + Metrics.WallQuad
                                             + Metrics.FloorTriangle);
    Metrics.QuadFraction = Faces > 0.0
                         ? static_cast<double>(Metrics.FloorQuad + Metrics.WallQuad) / Faces
                         : 0.0;
    return Metrics;
}

}   // namespace Frontier
