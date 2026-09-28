//=============================================================================================================================================
// SolidArc · Batch 62 · exact circular-bored elliptical prism and pentagonal twin-circular-holed prism face offsets
//============================================================================================================================================
#include "Kernel/FaceEditSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <algorithm>
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
constexpr double OuterMajor = 6.0;
constexpr double OuterMinor = 4.0;
constexpr double BoreRadius = 1.2;
constexpr double BoreX = 2.0;
constexpr double PentagonRadius = 4.0;
constexpr double PentagonHoleRadius = 0.9;
constexpr double PentagonHoleX = 1.5;

[[nodiscard]] std::vector<Vec3> PentagonProfile(double Radius) noexcept
{
    std::vector<Vec3> Points;
    Points.reserve(5);
    for (int I = 0; I < 5; ++I)
    {
        const double Angle = ScalarCriteria::TwoPi * static_cast<double>(I) / 5.0;
        Points.push_back({ Radius * std::cos(Angle), Radius * std::sin(Angle), 0.0 });
    }
    return Points;
}

[[nodiscard]] Deliver<BrepBody> CircularBoredEllipticalPrism(double Major = OuterMajor, double Minor = OuterMinor,
                                                             double Bore = BoreRadius, double CentreX = BoreX) noexcept
{
    const auto Outer = NurbsCurve::Ellipse({ 0, 0, 0 }, Vec3::UnitZ(), Vec3::UnitX(), Major, Minor);
    const auto Hole = NurbsCurve::Circle({ CentreX, 0, 0 }, Vec3::UnitZ(), Bore);
    if (!Outer || !Hole) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "circular-bored elliptical fixture");
    return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Hole.Payload }, Vec3::UnitZ(), Height);
}

[[nodiscard]] Deliver<BrepBody> PentagonalTwinCircularHoledPrism(double Radius = PentagonRadius, double Hole = PentagonHoleRadius,
                                                                 double OffsetX = PentagonHoleX) noexcept
{
    const auto Outer = NurbsCurve::Polyline(PentagonProfile(Radius), true);
    const auto Left = NurbsCurve::Circle({ -OffsetX, 0, 0 }, Vec3::UnitZ(), Hole);
    const auto Right = NurbsCurve::Circle({ OffsetX, 0, 0 }, Vec3::UnitZ(), Hole);
    if (!Outer || !Left || !Right) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "pentagonal twin-holed fixture");
    return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Left.Payload, Right.Payload }, Vec3::UnitZ(), Height);
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

[[nodiscard]] bool ExactFaceCensus(const BrepBody& Body, size_t TwoLoopPlanes, size_t ThreeLoopPlanes, size_t Extrusions) noexcept
{
    size_t Planes2 = 0, Planes3 = 0, Walls = 0;
    for (const BrepFace& F : Body.Faces)
    {
        if (F.Surface.Classification == SurfaceClassification::Plane && F.Loops.size() == 2) ++Planes2;
        else if (F.Surface.Classification == SurfaceClassification::Plane && F.Loops.size() == 3) ++Planes3;
        else if (F.Surface.Classification == SurfaceClassification::Extrusion && F.Loops.size() == 1) ++Walls;
        else return false;
    }
    return Planes2 == TwoLoopPlanes && Planes3 == ThreeLoopPlanes && Walls == Extrusions;
}

// Reads the single closed conic edge of a one-coedge cap loop.
[[nodiscard]] bool CapLoopConic(const BrepBody& Body, int Loop, bool& Circle, Vec3& Centre, double& Major, double& Minor) noexcept
{
    if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size()) || Body.Loops[Loop].Coedges.size() != 1) return false;
    const int Coedge = Body.Loops[Loop].Coedges.front();
    if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return false;
    const int Edge = Body.Coedges[Coedge].Edge;
    if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size())) return false;
    const NurbsCurve& Curve = Body.Edges[Edge].Curve;
    if (!Curve.Closed() || !Curve.Rational() || Curve.Degree != 2 || Curve.PoleCount() != 9 ||
        std::fabs(Curve.AxisZ.Normalised().Dot(Vec3::UnitZ())) < 1.0 - 1e-6) return false;
    const Box3 B = Curve.Bounds();
    const double SpanX = B.High.X - B.Low.X, SpanY = B.High.Y - B.Low.Y;
    if (SpanX <= 1e-9 || SpanY <= 1e-9 || std::fabs(B.High.Z - B.Low.Z) > 1e-9) return false;
    Circle = std::fabs(SpanX - SpanY) <= 1e-9;
    Centre = { 0.5 * (B.Low.X + B.High.X), 0.5 * (B.Low.Y + B.High.Y), B.Low.Z };
    Major = 0.5 * std::max(SpanX, SpanY);
    Minor = 0.5 * std::min(SpanX, SpanY);
    return true;
}

[[nodiscard]] bool CircularBoredEllipticalCap(const BrepBody& Body, int Face) noexcept
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
    bool Circle = true; Vec3 Centre{}; double Major = 0.0, Minor = 0.0;
    if (!CapLoopConic(Body, Outer, Circle, Centre, Major, Minor) || Circle ||
        std::fabs(Major - OuterMajor) > 1e-9 || std::fabs(Minor - OuterMinor) > 1e-9 ||
        std::fabs(Centre.X) > 1e-9 || std::fabs(Centre.Y) > 1e-9) return false;
    if (!CapLoopConic(Body, Bore, Circle, Centre, Major, Minor) || !Circle ||
        std::fabs(Major - BoreRadius) > 1e-9 || std::fabs(Minor - BoreRadius) > 1e-9 ||
        std::fabs(Centre.X - BoreX) > 1e-9 || std::fabs(Centre.Y) > 1e-9) return false;
    return true;
}

[[nodiscard]] bool PentagonalTwinCircularCap(const BrepBody& Body, int Face) noexcept
{
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return false;
    const BrepFace& F = Body.Faces[Face];
    if (F.Surface.Classification != SurfaceClassification::Plane || F.Loops.size() != 3) return false;
    const Vec3 N = Body.FaceNormal(Face, 0.5 * (F.Surface.DomainStartU() + F.Surface.DomainEndU()),
                                   0.5 * (F.Surface.DomainStartV() + F.Surface.DomainEndV())).Normalised();
    if (N.Dot(Vec3::UnitZ()) < 1.0 - 1e-6) return false;
    int Outer = -1; std::vector<int> Holes;
    for (int Loop : F.Loops)
    {
        if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size())) return false;
        if (Body.Loops[Loop].Outer && Outer < 0) Outer = Loop;
        else if (!Body.Loops[Loop].Outer) Holes.push_back(Loop);
        else return false;
    }
    if (Holes.size() != 2 || Body.Loops[Outer].Coedges.size() != 5) return false;
    const std::vector<Vec3> Canonical = PentagonProfile(PentagonRadius);
    for (int Coedge : Body.Loops[Outer].Coedges)
    {
        if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return false;
        const BrepCoedge& C = Body.Coedges[Coedge];
        if (C.Edge < 0 || C.Edge >= static_cast<int>(Body.Edges.size())) return false;
        const BrepEdge& E = Body.Edges[C.Edge];
        if (E.Curve.Classification != CurveClassification::Line || E.Curve.Degree != 1 ||
            E.VertexStart < 0 || E.VertexEnd < 0) return false;
        const Vec3 P = Body.Vertices[C.Reversed ? E.VertexEnd : E.VertexStart].Point;
        if (std::fabs(std::hypot(P.X, P.Y) - PentagonRadius) > 1e-9) return false;
        bool Found = false;
        for (const Vec3& C2 : Canonical)
            if (std::fabs(P.X - C2.X) <= 1e-9 && std::fabs(P.Y - C2.Y) <= 1e-9) { Found = true; break; }
        if (!Found) return false;
    }
    std::vector<Vec3> Centres;
    for (int Loop : Holes)
    {
        bool Circle = false; Vec3 Centre{}; double Major = 0.0, Minor = 0.0;
        if (!CapLoopConic(Body, Loop, Circle, Centre, Major, Minor) || !Circle ||
            std::fabs(Major - PentagonHoleRadius) > 1e-9 || std::fabs(Minor - PentagonHoleRadius) > 1e-9) return false;
        Centres.push_back(Centre);
    }
    std::sort(Centres.begin(), Centres.end(), [](const Vec3& A, const Vec3& B) { return A.X < B.X; });
    return std::fabs(Centres[0].X + PentagonHoleX) <= 1e-9 && std::fabs(Centres[1].X - PentagonHoleX) <= 1e-9 &&
           std::fabs(Centres[0].Y) <= 1e-9 && std::fabs(Centres[1].Y) <= 1e-9;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Batch 62 · circular-bored elliptical and pentagonal twin-circular-holed prism offsets");
    const auto BoredDeliver = CircularBoredEllipticalPrism();
    const auto PentagonDeliver = PentagonalTwinCircularHoledPrism();
    Panel.Expect("Both exact curved-profile and pentagonal holed fixtures are constructed", BoredDeliver && PentagonDeliver);
    if (!BoredDeliver || !PentagonDeliver) return Panel.Conclude();

    const BrepBody Bored = BoredDeliver.Payload, BoredSnapshot = Bored;
    const BrepBody Pentagon = PentagonDeliver.Payload, PentagonSnapshot = Pentagon;
    const BodyReport BoredBefore = Bored.Validate(), PentagonBefore = Pentagon.Validate();
    const int BoredTop = FaceToward(Bored, Vec3::UnitZ()), BoredBottom = FaceToward(Bored, -Vec3::UnitZ()), BoredSide = FaceToward(Bored, Vec3::UnitX());
    const int PentagonTop = FaceToward(Pentagon, Vec3::UnitZ()), PentagonBottom = FaceToward(Pentagon, -Vec3::UnitZ()), PentagonSide = FaceToward(Pentagon, Vec3::UnitY());

    Panel.Section("Exact curved-profile and pentagonal holed source recognition");
    Panel.Expect("Circular-bored elliptical source is closed genus-one V4/E6/C12/L6/F4",
                 ExactCounts(Bored, 1, 4, 6, 12, 6, 4) && ExactFaceCensus(Bored, 2, 0, 2) && CircularBoredEllipticalCap(Bored, BoredTop));
    Panel.Expect("Pentagonal twin-holed source is closed genus-two V14/E21/C42/L13/F9",
                 ExactCounts(Pentagon, 2, 14, 21, 42, 13, 9) && ExactFaceCensus(Pentagon, 0, 2, 7) && PentagonalTwinCircularCap(Pentagon, PentagonTop));
    Panel.Expect("Each source selects distinct upper, lower, and side faces",
                 BoredTop != BoredBottom && BoredTop != BoredSide && PentagonTop != PentagonBottom && PentagonTop != PentagonSide);

    Panel.Section("Multiple positive upper-cap distances");
    const double BoredArea = ScalarCriteria::Pi * OuterMajor * OuterMinor - ScalarCriteria::Pi * BoreRadius * BoreRadius;
    const double PentagonArea = 2.5 * PentagonRadius * PentagonRadius * std::sin(ScalarCriteria::TwoPi / 5.0)
                              - 2.0 * ScalarCriteria::Pi * PentagonHoleRadius * PentagonHoleRadius;
    for (const double Distance : { OffsetA, OffsetB, OffsetC })
    {
        const auto BoredResult = FaceEditSolver::OffsetCircularBoredEllipticalPrism(Bored, BoredTop, Distance);
        const auto PentagonResult = FaceEditSolver::OffsetExtrudedPentagonalTwinCircularHoledPrism(Pentagon, PentagonTop, Distance);
        Panel.Expect("Circular-bored elliptical offset retains genus-one topology and bore",
                     BoredResult && ExactCounts(BoredResult.Payload, 1, 4, 6, 12, 6, 4) &&
                     ExactFaceCensus(BoredResult.Payload, 2, 0, 2) &&
                     CircularBoredEllipticalCap(BoredResult.Payload, FaceToward(BoredResult.Payload, Vec3::UnitZ())));
        Panel.Expect("Pentagonal twin-holed offset retains genus-two topology and holes",
                     PentagonResult && ExactCounts(PentagonResult.Payload, 2, 14, 21, 42, 13, 9) &&
                     ExactFaceCensus(PentagonResult.Payload, 0, 2, 7) &&
                     PentagonalTwinCircularCap(PentagonResult.Payload, FaceToward(PentagonResult.Payload, Vec3::UnitZ())));
        if (BoredResult) Panel.Within("Circular-bored elliptical volume follows the exact ellipse/bore identity",
                                      std::fabs(BoredResult.Payload.Validate().Volume - BoredArea * (Height + Distance)) / (BoredArea * (Height + Distance)), 1e-3);
        if (PentagonResult) Panel.Within("Pentagonal twin-holed volume follows the exact pentagon/hole identity",
                                         std::fabs(PentagonResult.Payload.Validate().Volume - PentagonArea * (Height + Distance)) / (PentagonArea * (Height + Distance)), 1e-3);
    }
    Panel.Expect("The public dispatcher reaches both new holed routes",
                 FaceEditSolver::OffsetFace(Bored, BoredTop, OffsetB) && FaceEditSolver::OffsetFace(Pentagon, PentagonTop, OffsetB));
    Panel.Expect("Both new holed reconstructions preserve their sources",
                 SameSource(Bored, BoredSnapshot, BoredBefore) && SameSource(Pentagon, PentagonSnapshot, PentagonBefore));

    Panel.Section("Transactional refusal boundaries");
    Panel.Expect("Only the circular-bored elliptical upper cap is supported",
                 !FaceEditSolver::OffsetCircularBoredEllipticalPrism(Bored, BoredBottom, OffsetB) &&
                 !FaceEditSolver::OffsetCircularBoredEllipticalPrism(Bored, BoredSide, OffsetB));
    Panel.Expect("Only the pentagonal twin-holed upper cap is supported",
                 !FaceEditSolver::OffsetExtrudedPentagonalTwinCircularHoledPrism(Pentagon, PentagonBottom, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedPentagonalTwinCircularHoledPrism(Pentagon, PentagonSide, OffsetB));
    Panel.Expect("The two strict routes refuse each other's source",
                 !FaceEditSolver::OffsetCircularBoredEllipticalPrism(Pentagon, PentagonTop, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedPentagonalTwinCircularHoledPrism(Bored, BoredTop, OffsetB));
    Panel.Expect("Zero, negative, and non-finite distances refuse for both routes",
                 !FaceEditSolver::OffsetCircularBoredEllipticalPrism(Bored, BoredTop, 0.0) &&
                 !FaceEditSolver::OffsetCircularBoredEllipticalPrism(Bored, BoredTop, -0.1) &&
                 !FaceEditSolver::OffsetCircularBoredEllipticalPrism(Bored, BoredTop, std::numeric_limits<double>::infinity()) &&
                 !FaceEditSolver::OffsetExtrudedPentagonalTwinCircularHoledPrism(Pentagon, PentagonTop, 0.0) &&
                 !FaceEditSolver::OffsetExtrudedPentagonalTwinCircularHoledPrism(Pentagon, PentagonTop, -0.1) &&
                 !FaceEditSolver::OffsetExtrudedPentagonalTwinCircularHoledPrism(Pentagon, PentagonTop, std::numeric_limits<double>::infinity()));
    const BrepBody BoredMoved = Bored.Transformed(Mat4::Translation({ 2, 0, 0 }));
    const BrepBody PentagonMoved = Pentagon.Transformed(Mat4::Translation({ 0, 2, 0 }));
    Panel.Expect("Translated curved-profile and pentagonal sources refuse the anchored routes",
                 !FaceEditSolver::OffsetCircularBoredEllipticalPrism(BoredMoved, FaceToward(BoredMoved, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedPentagonalTwinCircularHoledPrism(PentagonMoved, FaceToward(PentagonMoved, Vec3::UnitZ()), OffsetB));
    const auto NarrowEllipse = CircularBoredEllipticalPrism(5.0, OuterMinor);
    const auto SmallBore = CircularBoredEllipticalPrism(OuterMajor, OuterMinor, 0.9);
    const auto ShiftedBore = CircularBoredEllipticalPrism(OuterMajor, OuterMinor, BoreRadius, 1.0);
    const auto NarrowPentagon = PentagonalTwinCircularHoledPrism(3.5);
    const auto SmallPentagonHoles = PentagonalTwinCircularHoledPrism(PentagonRadius, 0.7);
    const auto ShiftedPentagonHoles = PentagonalTwinCircularHoledPrism(PentagonRadius, PentagonHoleRadius, 1.0);
    Panel.Expect("Non-canonical profile, bore, and hole dimensions remain refused",
                 NarrowEllipse && SmallBore && ShiftedBore && NarrowPentagon && SmallPentagonHoles && ShiftedPentagonHoles &&
                 !FaceEditSolver::OffsetCircularBoredEllipticalPrism(NarrowEllipse.Payload, FaceToward(NarrowEllipse.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetCircularBoredEllipticalPrism(SmallBore.Payload, FaceToward(SmallBore.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetCircularBoredEllipticalPrism(ShiftedBore.Payload, FaceToward(ShiftedBore.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedPentagonalTwinCircularHoledPrism(NarrowPentagon.Payload, FaceToward(NarrowPentagon.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedPentagonalTwinCircularHoledPrism(SmallPentagonHoles.Payload, FaceToward(SmallPentagonHoles.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedPentagonalTwinCircularHoledPrism(ShiftedPentagonHoles.Payload, FaceToward(ShiftedPentagonHoles.Payload, Vec3::UnitZ()), OffsetB));
    const auto EllipticalBore = [&]() noexcept
    {
        const auto Outer = NurbsCurve::Ellipse({ 0, 0, 0 }, Vec3::UnitZ(), Vec3::UnitX(), OuterMajor, OuterMinor);
        const auto Inner = NurbsCurve::Ellipse({ 0, 0, 0 }, Vec3::UnitZ(), Vec3::UnitX(), 3.0, 2.0);
        if (!Outer || !Inner) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "concentric elliptical annulus fixture");
        return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Inner.Payload }, Vec3::UnitZ(), Height);
    }();
    const auto SinglePentagonHole = [&]() noexcept
    {
        const auto Outer = NurbsCurve::Polyline(PentagonProfile(PentagonRadius), true);
        const auto Hole = NurbsCurve::Circle({ 0, 0, 0 }, Vec3::UnitZ(), 1.1);
        if (!Outer || !Hole) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "single pentagon hole fixture");
        return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Hole.Payload }, Vec3::UnitZ(), Height);
    }();
    const auto EllipticalPentagonHoles = [&]() noexcept
    {
        const auto Outer = NurbsCurve::Polyline(PentagonProfile(PentagonRadius), true);
        const auto Left = NurbsCurve::Ellipse({ -PentagonHoleX, 0, 0 }, Vec3::UnitZ(), Vec3::UnitX(), 1.2, 0.7);
        const auto Right = NurbsCurve::Ellipse({ PentagonHoleX, 0, 0 }, Vec3::UnitZ(), Vec3::UnitX(), 1.2, 0.7);
        if (!Outer || !Left || !Right) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "elliptical pentagon holes fixture");
        return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Left.Payload, Right.Payload }, Vec3::UnitZ(), Height);
    }();
    Panel.Expect("Mismatched supports and wrong hole counts stay outside the new routes",
                 EllipticalBore && SinglePentagonHole && EllipticalPentagonHoles &&
                 !FaceEditSolver::OffsetCircularBoredEllipticalPrism(EllipticalBore.Payload, FaceToward(EllipticalBore.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedPentagonalTwinCircularHoledPrism(SinglePentagonHole.Payload, FaceToward(SinglePentagonHole.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedPentagonalTwinCircularHoledPrism(EllipticalPentagonHoles.Payload, FaceToward(EllipticalPentagonHoles.Payload, Vec3::UnitZ()), OffsetB));
    const auto HoleFreePentagon = [&]() noexcept
    {
        const auto Outer = NurbsCurve::Polyline(PentagonProfile(PentagonRadius), true);
        return Outer ? BrepBody::Extrude(Outer.Payload, Vec3::UnitZ(), Height)
                     : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "hole-free pentagon fixture");
    }();
    Panel.Expect("Earlier concentric-annulus and hole-free pentagon domains still route exactly",
                 EllipticalBore && HoleFreePentagon &&
                 FaceEditSolver::OffsetExtrudedEllipticalAnnularPrism(EllipticalBore.Payload, FaceToward(EllipticalBore.Payload, Vec3::UnitZ()), OffsetB) &&
                 FaceEditSolver::OffsetExtrudedConvexPrism(HoleFreePentagon.Payload, FaceToward(HoleFreePentagon.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetCircularBoredEllipticalPrism(HoleFreePentagon.Payload, FaceToward(HoleFreePentagon.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedPentagonalTwinCircularHoledPrism(HoleFreePentagon.Payload, FaceToward(HoleFreePentagon.Payload, Vec3::UnitZ()), OffsetB));
    BrepBody MalformedBored = Bored; MalformedBored.Edges.front().Coedges.push_back(MalformedBored.Edges.front().Coedges.front());
    BrepBody MalformedPentagon = Pentagon; MalformedPentagon.Edges.front().Coedges.push_back(MalformedPentagon.Edges.front().Coedges.front());
    Panel.Expect("Malformed curved-profile and pentagonal sources refuse without healing",
                 !FaceEditSolver::OffsetCircularBoredEllipticalPrism(MalformedBored, BoredTop, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedPentagonalTwinCircularHoledPrism(MalformedPentagon, PentagonTop, OffsetB));
    Panel.Expect("All refusal paths preserve both valid sources",
                 SameSource(Bored, BoredSnapshot, BoredBefore) && SameSource(Pentagon, PentagonSnapshot, PentagonBefore));

    Panel.Section("Durable curved-profile and pentagonal holed contact sheet");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Batch62_CircularBoredEllipticalAndPentagonalTwinCircularHoledFaceOffset.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    const auto BoredResult = FaceEditSolver::OffsetCircularBoredEllipticalPrism(Bored, BoredTop, OffsetB);
    const auto PentagonResult = FaceEditSolver::OffsetExtrudedPentagonalTwinCircularHoledPrism(Pentagon, PentagonTop, OffsetB);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1800, 900);
    const bool Added = BoredResult && PentagonResult &&
        Host.Document().AddBody("SharpCircularBoredElliptical", Bored.Transformed(Mat4::Translation({ -11, -7, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetCircularBoredElliptical", BoredResult.Payload.Transformed(Mat4::Translation({ -11, 7, 0 }))).Identity > 0 &&
        Host.Document().AddBody("SharpPentagonalTwinCircularHoled", Pentagon.Transformed(Mat4::Translation({ 11, -7, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetPentagonalTwinCircularHoled", PentagonResult.Payload.Transformed(Mat4::Translation({ 11, 7, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") && Host.Execute("view orbit 25 -18") &&
        Host.Execute("view fit") && Host.Execute("render Batch62_CircularBoredEllipticalAndPentagonalTwinCircularHoledFaceOffset");
    Panel.Expect("The consolidated curved-profile and pentagonal holed proof render completes", Rendered);
    Panel.Expect("The durable curved-profile and pentagonal holed proof PNG is visible",
                 std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
