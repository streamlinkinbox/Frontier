//=============================================================================================================================================
// SolidArc · Batch 64 · exact circular-bored rounded-rectangular and rounded-rectangular-holed prism face offsets
//=============================================================================================================================================
#include "Kernel/FaceEditSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <vector>

using namespace Frontier;

namespace
{
constexpr double Height = 6.0;
constexpr double OffsetA = 0.5;
constexpr double OffsetB = 1.5;
constexpr double OffsetC = 3.0;
constexpr double OuterHalfX = 6.0;
constexpr double OuterHalfY = 4.0;
constexpr double CornerRadius = 1.5;
constexpr double BoreRadius = 1.2;
constexpr double BoreX = 2.0;
constexpr double RectHalfX = 7.0;
constexpr double RectHalfY = 5.0;
constexpr double HoleHalfX = 4.0;
constexpr double HoleHalfY = 2.5;
constexpr double HoleRadius = 1.0;

// Exact analytic filleted-rectangle area: the bounding rectangle minus the four corner cut-backs.
[[nodiscard]] constexpr double RoundedRectArea(double HalfX, double HalfY, double Radius) noexcept
{
    return 4.0 * HalfX * HalfY - (4.0 - ScalarCriteria::Pi) * Radius * Radius;
}

[[nodiscard]] Deliver<BrepBody> CircularBoredRoundedRectangularPrism(double HalfX = OuterHalfX, double HalfY = OuterHalfY,
                                                                     double Corner = CornerRadius, double Bore = BoreRadius,
                                                                     double CentreX = BoreX) noexcept
{
    Workplane Plane;
    const auto Outer = NurbsCurve::Rectangle(Plane, { -HalfX, -HalfY }, { HalfX, HalfY }, Corner);
    const auto Hole = NurbsCurve::Circle({ CentreX, 0, 0 }, Vec3::UnitZ(), Bore);
    if (!Outer || !Hole) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "circular-bored rounded-rect fixture");
    return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Hole.Payload }, Vec3::UnitZ(), Height);
}

[[nodiscard]] Deliver<BrepBody> RoundedRectangularHoledPrism(double RectX = RectHalfX, double RectY = RectHalfY,
                                                             double HalfX = HoleHalfX, double HalfY = HoleHalfY,
                                                             double Corner = HoleRadius) noexcept
{
    Workplane Plane;
    const auto Outer = NurbsCurve::Rectangle(Plane, { -RectX, -RectY }, { RectX, RectY });
    const auto Hole = NurbsCurve::Rectangle(Plane, { -HalfX, -HalfY }, { HalfX, HalfY }, Corner);
    if (!Outer || !Hole) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "rounded-rect-holed fixture");
    return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Hole.Payload }, Vec3::UnitZ(), Height);
}

[[nodiscard]] int FaceToward(const BrepBody& Body, Vec3 Direction) noexcept
{
    int Best = -1; double BestDot = -2.0;
    for (int I = 0; I < static_cast<int>(Body.Faces.size()); ++I)
    {
        const BrepFace& F = Body.Faces[I];
        const Vec3 N = Body.FaceNormal(I, 0.5 * (F.Surface.DomainStartU() + F.Surface.DomainEndU()),
                                       0.5 * (F.Surface.DomainStartV() + F.Surface.DomainEndV())).Normalised();
        const double D = N.Dot(Direction.Normalised());
        if (D > BestDot) { BestDot = D; Best = I; }
    }
    return Best;
}

[[nodiscard]] bool SameSource(const BrepBody& Body, const BrepBody& Snapshot, const BodyReport& Before) noexcept
{
    const BodyReport After = Body.Validate();
    return After.Vertices == Before.Vertices && After.Edges == Before.Edges && After.Faces == Before.Faces &&
           After.Loops == Before.Loops && After.EulerCharacteristic == Before.EulerCharacteristic && After.Genus == Before.Genus &&
           After.OpenEdges == Before.OpenEdges && After.NonManifoldEdges == Before.NonManifoldEdges &&
           After.MisorientedEdges == Before.MisorientedEdges && std::fabs(After.Volume - Before.Volume) <= 1e-12 &&
           Body.Vertices.size() == Snapshot.Vertices.size() && Body.Edges.size() == Snapshot.Edges.size() &&
           Body.Coedges.size() == Snapshot.Coedges.size() && Body.Loops.size() == Snapshot.Loops.size() &&
           Body.Faces.size() == Snapshot.Faces.size();
}

[[nodiscard]] bool ExactCounts(const BrepBody& Body, int Genus, size_t Vertices, size_t Edges, size_t Coedges,
                               size_t Loops, size_t Faces) noexcept
{
    const BodyReport R = Body.Validate();
    return R.Solid() && R.Hulls == 1 && R.Genus == Genus && R.OpenEdges == 0 && R.NonManifoldEdges == 0 &&
           R.MisorientedEdges == 0 && Body.Vertices.size() == Vertices && Body.Edges.size() == Edges &&
           Body.Coedges.size() == Coedges && Body.Loops.size() == Loops && Body.Faces.size() == Faces;
}

[[nodiscard]] bool ExactFaceCensus(const BrepBody& Body, size_t TwoLoopPlanes, size_t Extrusions) noexcept
{
    size_t Planes2 = 0, Walls = 0;
    for (const BrepFace& F : Body.Faces)
    {
        if (F.Surface.Classification == SurfaceClassification::Plane && F.Loops.size() == 2) ++Planes2;
        else if (F.Surface.Classification == SurfaceClassification::Extrusion && F.Loops.size() == 1) ++Walls;
        else return false;
    }
    return Planes2 == TwoLoopPlanes && Walls == Extrusions;
}

// The extrusion pipeline flattens a filleted rectangle to a degree-two rational chain, so recognition is structural plus sampled.
[[nodiscard]] bool ExactRoundedRect(const NurbsCurve& Curve, double HalfX, double HalfY, double Radius, Vec3& Centre) noexcept
{
    if (!Curve.Closed() || !Curve.Rational() || Curve.Degree != 2 || Curve.PoleCount() != 17 ||
        std::fabs(Curve.AxisZ.Normalised().Dot(Vec3::UnitZ())) < 1.0 - 1e-6) return false;
    const Box3 B = Curve.Bounds();
    const double SpanX = B.High.X - B.Low.X, SpanY = B.High.Y - B.Low.Y;
    if (std::fabs(SpanX - 2.0 * HalfX) > 1e-9 || std::fabs(SpanY - 2.0 * HalfY) > 1e-9 ||
        std::fabs(B.High.Z - B.Low.Z) > 1e-9 || SpanX <= SpanY + 1e-9) return false;
    if (Radius <= 1e-9 || Radius + 1e-9 >= std::min(HalfX, HalfY)) return false;
    Centre = { 0.5 * (B.Low.X + B.High.X), 0.5 * (B.Low.Y + B.High.Y), B.Low.Z };
    Workplane Plane;
    Plane.Origin = { 0.0, 0.0, Centre.Z };
    const auto Expected = NurbsCurve::Rectangle(Plane, { Centre.X - HalfX, Centre.Y - HalfY },
                                                { Centre.X + HalfX, Centre.Y + HalfY }, Radius);
    if (!Expected) return false;
    for (int I = 0; I <= 64; ++I)
    {
        const double T = Curve.DomainStart() + (Curve.DomainEnd() - Curve.DomainStart()) * static_cast<double>(I) / 64.0;
        double Distance = 0.0;
        (void)Expected.Payload.ClosestParameter(Curve.Sample(T), &Distance);
        if (!std::isfinite(Distance) || Distance > 1e-6) return false;
    }
    return true;
}

[[nodiscard]] bool ExactCircle(const NurbsCurve& Curve, double& Radius, Vec3& Centre) noexcept
{
    if (!Curve.Closed() || !Curve.Rational() || Curve.Degree != 2 || Curve.PoleCount() != 9 ||
        std::fabs(Curve.AxisZ.Normalised().Dot(Vec3::UnitZ())) < 1.0 - 1e-6) return false;
    const Box3 B = Curve.Bounds();
    const double SpanX = B.High.X - B.Low.X, SpanY = B.High.Y - B.Low.Y;
    if (SpanX <= 1e-9 || SpanY <= 1e-9 || std::fabs(SpanX - SpanY) > 1e-9 || std::fabs(B.High.Z - B.Low.Z) > 1e-9) return false;
    Radius = 0.5 * SpanX;
    Centre = { 0.5 * (B.Low.X + B.High.X), 0.5 * (B.Low.Y + B.High.Y), B.Low.Z };
    return true;
}

// Reads the single edge of a one-coedge cap loop.
[[nodiscard]] const NurbsCurve* CapLoopCurve(const BrepBody& Body, int Loop) noexcept
{
    if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size()) || Body.Loops[Loop].Coedges.size() != 1) return nullptr;
    const int Coedge = Body.Loops[Loop].Coedges.front();
    if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return nullptr;
    const int Edge = Body.Coedges[Coedge].Edge;
    if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size())) return nullptr;
    return &Body.Edges[Edge].Curve;
}

[[nodiscard]] bool CircularBoredRoundedRectCap(const BrepBody& Body, int Face) noexcept
{
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return false;
    const BrepFace& F = Body.Faces[Face];
    if (F.Surface.Classification != SurfaceClassification::Plane || F.Loops.size() != 2) return false;
    const Vec3 N = Body.FaceNormal(Face, 0.5 * (F.Surface.DomainStartU() + F.Surface.DomainEndU()),
                                   0.5 * (F.Surface.DomainStartV() + F.Surface.DomainEndV())).Normalised();
    if (N.Dot(Vec3::UnitZ()) < 1.0 - 1e-6) return false;
    int Outer = -1, Bore = -1;
    for (int Loop : F.Loops)
    {
        if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size())) return false;
        if (Body.Loops[Loop].Outer && Outer < 0) Outer = Loop;
        else if (!Body.Loops[Loop].Outer && Bore < 0) Bore = Loop;
        else return false;
    }
    if (Outer < 0 || Bore < 0) return false;
    const NurbsCurve* OuterCurve = CapLoopCurve(Body, Outer);
    const NurbsCurve* BoreCurve = CapLoopCurve(Body, Bore);
    if (!OuterCurve || !BoreCurve) return false;
    Vec3 Centre{};
    if (!ExactRoundedRect(*OuterCurve, OuterHalfX, OuterHalfY, CornerRadius, Centre) ||
        std::fabs(Centre.X) > 1e-9 || std::fabs(Centre.Y) > 1e-9) return false;
    double Radius = 0.0; Vec3 BoreCentre{};
    if (!ExactCircle(*BoreCurve, Radius, BoreCentre) || std::fabs(Radius - BoreRadius) > 1e-9 ||
        std::fabs(BoreCentre.X - BoreX) > 1e-9 || std::fabs(BoreCentre.Y) > 1e-9) return false;
    return true;
}

[[nodiscard]] bool RoundedRectHoledCap(const BrepBody& Body, int Face) noexcept
{
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return false;
    const BrepFace& F = Body.Faces[Face];
    if (F.Surface.Classification != SurfaceClassification::Plane || F.Loops.size() != 2) return false;
    const Vec3 N = Body.FaceNormal(Face, 0.5 * (F.Surface.DomainStartU() + F.Surface.DomainEndU()),
                                   0.5 * (F.Surface.DomainStartV() + F.Surface.DomainEndV())).Normalised();
    if (N.Dot(Vec3::UnitZ()) < 1.0 - 1e-6) return false;
    int Outer = -1, Hole = -1;
    for (int Loop : F.Loops)
    {
        if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size())) return false;
        if (Body.Loops[Loop].Outer && Body.Loops[Loop].Coedges.size() == 4 && Outer < 0) Outer = Loop;
        else if (!Body.Loops[Loop].Outer && Body.Loops[Loop].Coedges.size() == 1 && Hole < 0) Hole = Loop;
        else return false;
    }
    if (Outer < 0 || Hole < 0) return false;
    std::vector<Vec3> Corners;
    Corners.reserve(4);
    for (int Coedge : Body.Loops[Outer].Coedges)
    {
        if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return false;
        const BrepCoedge& C = Body.Coedges[Coedge];
        if (C.Edge < 0 || C.Edge >= static_cast<int>(Body.Edges.size())) return false;
        const BrepEdge& E = Body.Edges[C.Edge];
        if (E.Curve.Classification != CurveClassification::Line || E.Curve.Degree != 1 ||
            E.VertexStart < 0 || E.VertexEnd < 0 || E.VertexStart == E.VertexEnd) return false;
        const Vec3 Start = Body.Vertices[C.Reversed ? E.VertexEnd : E.VertexStart].Point;
        const Vec3 End = Body.Vertices[C.Reversed ? E.VertexStart : E.VertexEnd].Point;
        if (std::fabs(Start.Z - End.Z) > 1e-9) return false;
        if (std::fabs(Start.X - End.X) > 1e-9 && std::fabs(Start.Y - End.Y) > 1e-9) return false;
        Corners.push_back(Start);
    }
    const std::array<Vec3, 4> Canonical{{ { -RectHalfX, -RectHalfY, 0.0 }, { RectHalfX, -RectHalfY, 0.0 },
                                          { RectHalfX, RectHalfY, 0.0 }, { -RectHalfX, RectHalfY, 0.0 } }};
    for (const Vec3& Want : Canonical)
    {
        bool Found = false;
        for (const Vec3& P : Corners)
            if (std::fabs(P.X - Want.X) <= 1e-9 && std::fabs(P.Y - Want.Y) <= 1e-9) { Found = true; break; }
        if (!Found) return false;
    }
    const NurbsCurve* HoleCurve = CapLoopCurve(Body, Hole);
    if (!HoleCurve) return false;
    Vec3 Centre{};
    if (!ExactRoundedRect(*HoleCurve, HoleHalfX, HoleHalfY, HoleRadius, Centre)) return false;
    return std::fabs(Centre.X) <= 1e-9 && std::fabs(Centre.Y) <= 1e-9;
}

[[nodiscard]] bool ExactSupportCensus(const BrepBody& Body, double HalfX, double HalfY, double Radius,
                                      size_t Lines, size_t Circles, size_t Rounded) noexcept
{
    size_t LineCount = 0, CircleCount = 0, RoundedCount = 0;
    for (const BrepEdge& E : Body.Edges)
    {
        if (E.Coedges.size() != 2) return false;
        double EdgeRadius = 0.0; Vec3 Centre{};
        if (E.Curve.Classification == CurveClassification::Line && E.Curve.Degree == 1) ++LineCount;
        else if (ExactRoundedRect(E.Curve, HalfX, HalfY, Radius, Centre)) ++RoundedCount;
        else if (ExactCircle(E.Curve, EdgeRadius, Centre)) ++CircleCount;
        else return false;
    }
    return LineCount == Lines && CircleCount == Circles && RoundedCount == Rounded;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Batch 64 · circular-bored rounded-rectangular and rounded-rectangular-holed prism offsets");
    const auto BoredDeliver = CircularBoredRoundedRectangularPrism();
    const auto HoledDeliver = RoundedRectangularHoledPrism();
    Panel.Expect("Both exact filleted-rectangle fixtures are constructed", BoredDeliver && HoledDeliver);
    if (!BoredDeliver || !HoledDeliver) return Panel.Conclude();

    const BrepBody Bored = BoredDeliver.Payload, BoredSnapshot = Bored;
    const BrepBody Holed = HoledDeliver.Payload, HoledSnapshot = Holed;
    const BodyReport BoredBefore = Bored.Validate(), HoledBefore = Holed.Validate();
    const int BoredTop = FaceToward(Bored, Vec3::UnitZ()), BoredBottom = FaceToward(Bored, -Vec3::UnitZ()),
              BoredSide = FaceToward(Bored, Vec3::UnitX());
    const int HoledTop = FaceToward(Holed, Vec3::UnitZ()), HoledBottom = FaceToward(Holed, -Vec3::UnitZ()),
              HoledSide = FaceToward(Holed, Vec3::UnitY());

    Panel.Section("Exact filleted-rectangle source recognition");
    Panel.Expect("Circular-bored rounded-rectangular source is closed genus-one V4/E6/C12/L6/F4",
                 ExactCounts(Bored, 1, 4, 6, 12, 6, 4) && ExactFaceCensus(Bored, 2, 2) &&
                 CircularBoredRoundedRectCap(Bored, BoredTop));
    Panel.Expect("Rounded-rectangular-holed source is closed genus-one V10/E15/C30/L9/F7",
                 ExactCounts(Holed, 1, 10, 15, 30, 9, 7) && ExactFaceCensus(Holed, 2, 5) &&
                 RoundedRectHoledCap(Holed, HoledTop));
    Panel.Expect("Each source carries exactly two exact seventeen-pole filleted-rectangle curves",
                 ExactSupportCensus(Bored, OuterHalfX, OuterHalfY, CornerRadius, 2, 2, 2) &&
                 ExactSupportCensus(Holed, HoleHalfX, HoleHalfY, HoleRadius, 13, 0, 2));
    Panel.Expect("Each source selects distinct upper, lower, and side faces",
                 BoredTop != BoredBottom && BoredTop != BoredSide && HoledTop != HoledBottom && HoledTop != HoledSide);

    Panel.Section("Multiple positive upper-cap distances");
    const double BoredArea = RoundedRectArea(OuterHalfX, OuterHalfY, CornerRadius) - ScalarCriteria::Pi * BoreRadius * BoreRadius;
    const double HoledArea = 4.0 * RectHalfX * RectHalfY - RoundedRectArea(HoleHalfX, HoleHalfY, HoleRadius);
    for (const double Distance : { OffsetA, OffsetB, OffsetC })
    {
        const auto BoredResult = FaceEditSolver::OffsetCircularBoredRoundedRectangularPrism(Bored, BoredTop, Distance);
        const auto HoledResult = FaceEditSolver::OffsetExtrudedRoundedRectangularHoledPrism(Holed, HoledTop, Distance);
        Panel.Expect("Circular-bored rounded-rectangular offset retains genus-one topology and bore",
                     BoredResult && ExactCounts(BoredResult.Payload, 1, 4, 6, 12, 6, 4) &&
                     ExactFaceCensus(BoredResult.Payload, 2, 2) &&
                     CircularBoredRoundedRectCap(BoredResult.Payload, FaceToward(BoredResult.Payload, Vec3::UnitZ())));
        Panel.Expect("Rounded-rectangular-holed offset retains genus-one topology and filleted hole",
                     HoledResult && ExactCounts(HoledResult.Payload, 1, 10, 15, 30, 9, 7) &&
                     ExactFaceCensus(HoledResult.Payload, 2, 5) &&
                     RoundedRectHoledCap(HoledResult.Payload, FaceToward(HoledResult.Payload, Vec3::UnitZ())));
        if (BoredResult) Panel.Within("Circular-bored rounded-rectangular volume follows the exact fillet/bore identity",
                                      std::fabs(BoredResult.Payload.Validate().Volume - BoredArea * (Height + Distance)) /
                                          (BoredArea * (Height + Distance)), 1e-3);
        if (HoledResult) Panel.Within("Rounded-rectangular-holed volume follows the exact rectangle/fillet identity",
                                      std::fabs(HoledResult.Payload.Validate().Volume - HoledArea * (Height + Distance)) /
                                          (HoledArea * (Height + Distance)), 1e-3);
    }
    const auto DispatchedBored = FaceEditSolver::OffsetFace(Bored, BoredTop, OffsetB);
    const auto DispatchedHoled = FaceEditSolver::OffsetFace(Holed, HoledTop, OffsetB);
    const auto StrictBored = FaceEditSolver::OffsetCircularBoredRoundedRectangularPrism(Bored, BoredTop, OffsetB);
    const auto StrictHoled = FaceEditSolver::OffsetExtrudedRoundedRectangularHoledPrism(Holed, HoledTop, OffsetB);
    Panel.Expect("The public dispatcher reaches both new filleted routes with identical results",
                 DispatchedBored && DispatchedHoled && StrictBored && StrictHoled &&
                 ExactCounts(DispatchedBored.Payload, 1, 4, 6, 12, 6, 4) &&
                 ExactCounts(DispatchedHoled.Payload, 1, 10, 15, 30, 9, 7) &&
                 std::fabs(DispatchedBored.Payload.Validate().Volume - StrictBored.Payload.Validate().Volume) <= 1e-9 &&
                 std::fabs(DispatchedHoled.Payload.Validate().Volume - StrictHoled.Payload.Validate().Volume) <= 1e-9);
    Panel.Expect("Both new filleted reconstructions preserve their sources",
                 SameSource(Bored, BoredSnapshot, BoredBefore) && SameSource(Holed, HoledSnapshot, HoledBefore));

    Panel.Section("Transactional refusal boundaries");
    Panel.Expect("Only the circular-bored rounded-rectangular upper cap is supported",
                 !FaceEditSolver::OffsetCircularBoredRoundedRectangularPrism(Bored, BoredBottom, OffsetB) &&
                 !FaceEditSolver::OffsetCircularBoredRoundedRectangularPrism(Bored, BoredSide, OffsetB));
    Panel.Expect("Only the rounded-rectangular-holed upper cap is supported",
                 !FaceEditSolver::OffsetExtrudedRoundedRectangularHoledPrism(Holed, HoledBottom, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedRoundedRectangularHoledPrism(Holed, HoledSide, OffsetB));
    Panel.Expect("The two strict filleted routes refuse each other's source",
                 !FaceEditSolver::OffsetCircularBoredRoundedRectangularPrism(Holed, HoledTop, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedRoundedRectangularHoledPrism(Bored, BoredTop, OffsetB));
    Panel.Expect("Zero, negative, and non-finite distances refuse for both routes",
                 !FaceEditSolver::OffsetCircularBoredRoundedRectangularPrism(Bored, BoredTop, 0.0) &&
                 !FaceEditSolver::OffsetCircularBoredRoundedRectangularPrism(Bored, BoredTop, -0.1) &&
                 !FaceEditSolver::OffsetCircularBoredRoundedRectangularPrism(Bored, BoredTop, std::numeric_limits<double>::infinity()) &&
                 !FaceEditSolver::OffsetExtrudedRoundedRectangularHoledPrism(Holed, HoledTop, 0.0) &&
                 !FaceEditSolver::OffsetExtrudedRoundedRectangularHoledPrism(Holed, HoledTop, -0.1) &&
                 !FaceEditSolver::OffsetExtrudedRoundedRectangularHoledPrism(Holed, HoledTop, std::numeric_limits<double>::quiet_NaN()));
    const BrepBody BoredMoved = Bored.Transformed(Mat4::Translation({ 2, 0, 0 }));
    const BrepBody HoledMoved = Holed.Transformed(Mat4::Translation({ 0, 2, 0 }));
    Panel.Expect("Translated filleted-rectangle sources refuse the anchored routes",
                 !FaceEditSolver::OffsetCircularBoredRoundedRectangularPrism(BoredMoved, FaceToward(BoredMoved, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedRoundedRectangularHoledPrism(HoledMoved, FaceToward(HoledMoved, Vec3::UnitZ()), OffsetB));

    const auto NarrowOuter = CircularBoredRoundedRectangularPrism(5.0, OuterHalfY);
    const auto TightCorner = CircularBoredRoundedRectangularPrism(OuterHalfX, OuterHalfY, 1.2);
    const auto WideCorner = CircularBoredRoundedRectangularPrism(OuterHalfX, OuterHalfY, 2.0);
    const auto SmallBore = CircularBoredRoundedRectangularPrism(OuterHalfX, OuterHalfY, CornerRadius, 1.0);
    const auto ShiftedBore = CircularBoredRoundedRectangularPrism(OuterHalfX, OuterHalfY, CornerRadius, BoreRadius, 1.0);
    Panel.Expect("Non-canonical half-extents, corner radii, and bore placements remain refused",
                 NarrowOuter && TightCorner && WideCorner && SmallBore && ShiftedBore &&
                 !FaceEditSolver::OffsetCircularBoredRoundedRectangularPrism(NarrowOuter.Payload, FaceToward(NarrowOuter.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetCircularBoredRoundedRectangularPrism(TightCorner.Payload, FaceToward(TightCorner.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetCircularBoredRoundedRectangularPrism(WideCorner.Payload, FaceToward(WideCorner.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetCircularBoredRoundedRectangularPrism(SmallBore.Payload, FaceToward(SmallBore.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetCircularBoredRoundedRectangularPrism(ShiftedBore.Payload, FaceToward(ShiftedBore.Payload, Vec3::UnitZ()), OffsetB));

    const auto NarrowRect = RoundedRectangularHoledPrism(6.5, RectHalfY);
    const auto SmallHole = RoundedRectangularHoledPrism(RectHalfX, RectHalfY, 3.5, HoleHalfY);
    const auto TightHole = RoundedRectangularHoledPrism(RectHalfX, RectHalfY, HoleHalfX, HoleHalfY, 0.8);
    Panel.Expect("Non-canonical rectangle and filleted-hole dimensions remain refused",
                 NarrowRect && SmallHole && TightHole &&
                 !FaceEditSolver::OffsetExtrudedRoundedRectangularHoledPrism(NarrowRect.Payload, FaceToward(NarrowRect.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedRoundedRectangularHoledPrism(SmallHole.Payload, FaceToward(SmallHole.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedRoundedRectangularHoledPrism(TightHole.Payload, FaceToward(TightHole.Payload, Vec3::UnitZ()), OffsetB));

    const auto TwinBored = [&]() noexcept
    {
        Workplane Plane;
        const auto Outer = NurbsCurve::Rectangle(Plane, { -OuterHalfX, -OuterHalfY }, { OuterHalfX, OuterHalfY }, CornerRadius);
        const auto Left = NurbsCurve::Circle({ BoreX, 0, 0 }, Vec3::UnitZ(), BoreRadius);
        const auto Right = NurbsCurve::Circle({ -2.0, 0, 0 }, Vec3::UnitZ(), 0.6);
        if (!Outer || !Left || !Right) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "twin bored fixture");
        return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Left.Payload, Right.Payload }, Vec3::UnitZ(), Height);
    }();
    const auto EllipticalBore = [&]() noexcept
    {
        Workplane Plane;
        const auto Outer = NurbsCurve::Rectangle(Plane, { -OuterHalfX, -OuterHalfY }, { OuterHalfX, OuterHalfY }, CornerRadius);
        const auto Inner = NurbsCurve::Ellipse({ BoreX, 0, 0 }, Vec3::UnitZ(), Vec3::UnitX(), 1.6, 1.0);
        if (!Outer || !Inner) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "elliptical bore fixture");
        return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Inner.Payload }, Vec3::UnitZ(), Height);
    }();
    const auto TwinHoled = [&]() noexcept
    {
        Workplane Plane;
        const auto Outer = NurbsCurve::Rectangle(Plane, { -RectHalfX, -RectHalfY }, { RectHalfX, RectHalfY });
        const auto Left = NurbsCurve::Rectangle(Plane, { -6.0, -1.5 }, { -1.0, 1.5 }, 0.8);
        const auto Right = NurbsCurve::Rectangle(Plane, { 1.0, -1.5 }, { 6.0, 1.5 }, 0.8);
        if (!Outer || !Left || !Right) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "twin filleted-holed fixture");
        return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Left.Payload, Right.Payload }, Vec3::UnitZ(), Height);
    }();
    const auto MixedHoles = [&]() noexcept
    {
        Workplane Plane;
        const auto Outer = NurbsCurve::Rectangle(Plane, { -RectHalfX, -RectHalfY }, { RectHalfX, RectHalfY });
        const auto Filleted = NurbsCurve::Rectangle(Plane, { -HoleHalfX, -HoleHalfY }, { HoleHalfX, HoleHalfY }, HoleRadius);
        const auto Circle = NurbsCurve::Circle({ 0, 0, 0 }, Vec3::UnitZ(), 1.0);
        if (!Outer || !Filleted || !Circle) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "mixed holed fixture");
        return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Filleted.Payload, Circle.Payload }, Vec3::UnitZ(), Height);
    }();
    Panel.Expect("Wrong hole counts and mismatched bore supports stay outside the new routes",
                 TwinBored && EllipticalBore && TwinHoled && MixedHoles &&
                 !FaceEditSolver::OffsetCircularBoredRoundedRectangularPrism(TwinBored.Payload, FaceToward(TwinBored.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetCircularBoredRoundedRectangularPrism(EllipticalBore.Payload, FaceToward(EllipticalBore.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedRoundedRectangularHoledPrism(TwinHoled.Payload, FaceToward(TwinHoled.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedRoundedRectangularHoledPrism(MixedHoles.Payload, FaceToward(MixedHoles.Payload, Vec3::UnitZ()), OffsetB));

    const auto CircularHoledRect = [&]() noexcept
    {
        Workplane Plane;
        const auto Outer = NurbsCurve::Rectangle(Plane, { -RectHalfX, -RectHalfY }, { RectHalfX, RectHalfY });
        const auto Hole = NurbsCurve::Circle({ 0, 0, 0 }, Vec3::UnitZ(), 2.0);
        if (!Outer || !Hole) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "circular holed rectangle fixture");
        return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Hole.Payload }, Vec3::UnitZ(), Height);
    }();
    const auto SlotHoledRect = [&]() noexcept
    {
        Workplane Plane;
        const auto Outer = NurbsCurve::Rectangle(Plane, { -8.0, -5.0 }, { 8.0, 5.0 });
        const auto Hole = NurbsCurve::Slot(Plane, { -3, 0 }, { 3, 0 }, 1.5);
        if (!Outer || !Hole) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "slot holed rectangle fixture");
        return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Hole.Payload }, Vec3::UnitZ(), Height);
    }();
    const auto CircularBoredEllipse = [&]() noexcept
    {
        const auto Outer = NurbsCurve::Ellipse({ 0, 0, 0 }, Vec3::UnitZ(), Vec3::UnitX(), OuterHalfX, OuterHalfY);
        const auto Hole = NurbsCurve::Circle({ BoreX, 0, 0 }, Vec3::UnitZ(), BoreRadius);
        if (!Outer || !Hole) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "circular bored ellipse fixture");
        return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Hole.Payload }, Vec3::UnitZ(), Height);
    }();
    const auto CircularBoredSlot = [&]() noexcept
    {
        Workplane Plane;
        const auto Outer = NurbsCurve::Slot(Plane, { -3, 0 }, { 3, 0 }, 1.5);
        const auto Hole = NurbsCurve::Circle({ 0, 0, 0 }, Vec3::UnitZ(), 0.8);
        if (!Outer || !Hole) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "circular bored slot fixture");
        return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Hole.Payload }, Vec3::UnitZ(), Height);
    }();
    Panel.Expect("Earlier circular-holed, slot-holed, and bored curved-profile domains still route exactly",
                 CircularHoledRect && SlotHoledRect && CircularBoredEllipse && CircularBoredSlot &&
                 FaceEditSolver::OffsetExtrudedHoledPrism(CircularHoledRect.Payload, FaceToward(CircularHoledRect.Payload, Vec3::UnitZ()), OffsetB) &&
                 FaceEditSolver::OffsetExtrudedRectangularSlotHoledPrism(SlotHoledRect.Payload, FaceToward(SlotHoledRect.Payload, Vec3::UnitZ()), OffsetB) &&
                 FaceEditSolver::OffsetCircularBoredEllipticalPrism(CircularBoredEllipse.Payload, FaceToward(CircularBoredEllipse.Payload, Vec3::UnitZ()), OffsetB) &&
                 FaceEditSolver::OffsetExtrudedSlotProfileCircularBoredPrism(CircularBoredSlot.Payload, FaceToward(CircularBoredSlot.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetCircularBoredRoundedRectangularPrism(CircularHoledRect.Payload, FaceToward(CircularHoledRect.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedRoundedRectangularHoledPrism(SlotHoledRect.Payload, FaceToward(SlotHoledRect.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetCircularBoredRoundedRectangularPrism(CircularBoredEllipse.Payload, FaceToward(CircularBoredEllipse.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetCircularBoredRoundedRectangularPrism(CircularBoredSlot.Payload, FaceToward(CircularBoredSlot.Payload, Vec3::UnitZ()), OffsetB));

    const auto HoleFreeRoundedRect = [&]() noexcept
    {
        Workplane Plane;
        const auto Outer = NurbsCurve::Rectangle(Plane, { -OuterHalfX, -OuterHalfY }, { OuterHalfX, OuterHalfY }, CornerRadius);
        return Outer ? BrepBody::Extrude(Outer.Payload, Vec3::UnitZ(), Height)
                     : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "hole-free filleted fixture");
    }();
    Panel.Expect("The hole-free filleted-rectangle prism remains outside both new routes",
                 HoleFreeRoundedRect && ExactCounts(HoleFreeRoundedRect.Payload, 0, 2, 3, 6, 3, 3) &&
                 !FaceEditSolver::OffsetCircularBoredRoundedRectangularPrism(HoleFreeRoundedRect.Payload, FaceToward(HoleFreeRoundedRect.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedRoundedRectangularHoledPrism(HoleFreeRoundedRect.Payload, FaceToward(HoleFreeRoundedRect.Payload, Vec3::UnitZ()), OffsetB));

    BrepBody MalformedBored = Bored; MalformedBored.Edges.front().Coedges.push_back(MalformedBored.Edges.front().Coedges.front());
    BrepBody MalformedHoled = Holed; MalformedHoled.Edges.front().Coedges.push_back(MalformedHoled.Edges.front().Coedges.front());
    Panel.Expect("Malformed filleted-rectangle sources refuse without healing",
                 !FaceEditSolver::OffsetCircularBoredRoundedRectangularPrism(MalformedBored, BoredTop, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedRoundedRectangularHoledPrism(MalformedHoled, HoledTop, OffsetB));
    Panel.Expect("All refusal paths preserve both valid sources",
                 SameSource(Bored, BoredSnapshot, BoredBefore) && SameSource(Holed, HoledSnapshot, HoledBefore));

    Panel.Section("Durable filleted-rectangle contact sheet");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Batch64_CircularBoredRoundedRectangularAndRoundedRectangularHoledFaceOffset.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    const auto ProofBored = FaceEditSolver::OffsetCircularBoredRoundedRectangularPrism(Bored, BoredTop, OffsetB);
    const auto ProofHoled = FaceEditSolver::OffsetExtrudedRoundedRectangularHoledPrism(Holed, HoledTop, OffsetB);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1800, 900);
    const bool Added = ProofBored && ProofHoled &&
        Host.Document().AddBody("SharpCircularBoredRoundedRect", Bored.Transformed(Mat4::Translation({ -14, -8, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetCircularBoredRoundedRect", ProofBored.Payload.Transformed(Mat4::Translation({ -14, 8, 0 }))).Identity > 0 &&
        Host.Document().AddBody("SharpRoundedRectHoled", Holed.Transformed(Mat4::Translation({ 10, -8, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetRoundedRectHoled", ProofHoled.Payload.Transformed(Mat4::Translation({ 10, 8, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") && Host.Execute("view orbit 25 -18") &&
        Host.Execute("view fit") && Host.Execute("render Batch64_CircularBoredRoundedRectangularAndRoundedRectangularHoledFaceOffset");
    Panel.Expect("The consolidated filleted-rectangle proof render completes", Rendered);
    Panel.Expect("The durable filleted-rectangle proof PNG is visible",
                 std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
