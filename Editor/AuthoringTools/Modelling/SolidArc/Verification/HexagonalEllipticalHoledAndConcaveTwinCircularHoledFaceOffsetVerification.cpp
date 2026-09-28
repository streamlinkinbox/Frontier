//=============================================================================================================================================
// SolidArc · Batch 61 · exact hexagonal elliptical-holed and concave twin-circular-holed prism face offsets
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
constexpr double HexRadius = 4.0;
constexpr double HoleMajor = 1.8;
constexpr double HoleMinor = 1.1;
constexpr double ConcaveHoleRadius = 1.0;
constexpr double ConcaveProfileArea = 61.0;

[[nodiscard]] std::vector<Vec3> HexagonProfile(double Radius) noexcept
{
    std::vector<Vec3> Points;
    Points.reserve(6);
    for (int I = 0; I < 6; ++I)
    {
        const double Angle = ScalarCriteria::TwoPi * static_cast<double>(I) / 6.0;
        Points.push_back({ Radius * std::cos(Angle), Radius * std::sin(Angle), 0.0 });
    }
    return Points;
}

[[nodiscard]] std::vector<Vec3> ConcaveProfile() noexcept
{
    return { { -6, -4, 0 }, { 6, -4, 0 }, { 6, -1, 0 }, { -1, -1, 0 }, { -1, 4, 0 }, { -6, 4, 0 } };
}

[[nodiscard]] Deliver<BrepBody> HexagonalEllipticalHoledPrism(double Radius = HexRadius, double Major = HoleMajor,
                                                              double Minor = HoleMinor) noexcept
{
    const auto Outer = NurbsCurve::Polyline(HexagonProfile(Radius), true);
    const auto Hole = NurbsCurve::Ellipse({ 0, 0, 0 }, Vec3::UnitZ(), Vec3::UnitX(), Major, Minor);
    if (!Outer || !Hole) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "hexagonal elliptical-holed fixture");
    return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Hole.Payload }, Vec3::UnitZ(), Height);
}

[[nodiscard]] Deliver<BrepBody> ConcaveTwinCircularHoledPrism(double Radius = ConcaveHoleRadius,
                                                              Vec3 Left = { -3, -2.5, 0 }, Vec3 Right = { 3, -2.5, 0 }) noexcept
{
    const auto Outer = NurbsCurve::Polyline(ConcaveProfile(), true);
    const auto LeftHole = NurbsCurve::Circle(Left, Vec3::UnitZ(), Radius);
    const auto RightHole = NurbsCurve::Circle(Right, Vec3::UnitZ(), Radius);
    if (!Outer || !LeftHole || !RightHole) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "concave twin-holed fixture");
    return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, LeftHole.Payload, RightHole.Payload }, Vec3::UnitZ(), Height);
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

[[nodiscard]] bool ExactSupports(const BrepBody& Body) noexcept
{
    for (const BrepFace& F : Body.Faces)
        if (F.Surface.Classification != SurfaceClassification::Plane && F.Surface.Classification != SurfaceClassification::Extrusion)
            return false;
    return true;
}

// Reads one cap loop: returns the ordered polygon of a line-only loop, or the single closed conic edge of a hole loop.
[[nodiscard]] bool CapLoopPolygon(const BrepBody& Body, int Loop, std::vector<Vec3>& Polygon) noexcept
{
    if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size())) return false;
    Polygon.clear();
    for (int Coedge : Body.Loops[Loop].Coedges)
    {
        if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return false;
        const BrepCoedge& C = Body.Coedges[Coedge];
        if (C.Edge < 0 || C.Edge >= static_cast<int>(Body.Edges.size())) return false;
        const BrepEdge& E = Body.Edges[C.Edge];
        if (E.Curve.Classification != CurveClassification::Line || E.Curve.Degree != 1 ||
            E.VertexStart < 0 || E.VertexEnd < 0) return false;
        Polygon.push_back(Body.Vertices[C.Reversed ? E.VertexEnd : E.VertexStart].Point);
    }
    return !Polygon.empty();
}

[[nodiscard]] bool CapLoopConic(const BrepBody& Body, int Loop, bool& Circle, Vec3& Centre, double& Major, double& Minor) noexcept
{
    if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size()) || Body.Loops[Loop].Coedges.size() != 1) return false;
    const int Coedge = Body.Loops[Loop].Coedges.front();
    if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return false;
    const int Edge = Body.Coedges[Coedge].Edge;
    if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size())) return false;
    const NurbsCurve& Curve = Body.Edges[Edge].Curve;
    if (!Curve.Closed() || !Curve.Rational() || Curve.Degree != 2 ||
        std::fabs(Curve.AxisZ.Normalised().Dot(Vec3::UnitZ())) < 1.0 - 1e-6) return false;
    const Box3 B = Curve.Bounds();
    const double SpanX = B.High.X - B.Low.X, SpanY = B.High.Y - B.Low.Y;
    if (SpanX <= 1e-9 || SpanY <= 1e-9 || std::fabs(B.High.Z - B.Low.Z) > 1e-9) return false;
    Circle = Curve.Classification == CurveClassification::Circle;
    Centre = { 0.5 * (B.Low.X + B.High.X), 0.5 * (B.Low.Y + B.High.Y), B.Low.Z };
    Major = 0.5 * std::max(SpanX, SpanY);
    Minor = 0.5 * std::min(SpanX, SpanY);
    return Circle ? Curve.PoleCount() == 9 : Curve.PoleCount() == 9 && std::fabs(SpanX - SpanY) > 1e-6;
}

[[nodiscard]] bool HexagonalEllipticalCap(const BrepBody& Body, int Face) noexcept
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
        if (Body.Loops[Loop].Outer && Outer < 0) Outer = Loop;
        else if (!Body.Loops[Loop].Outer && Hole < 0) Hole = Loop;
        else return false;
    }
    std::vector<Vec3> Polygon;
    if (!CapLoopPolygon(Body, Outer, Polygon) || Polygon.size() != 6) return false;
    const std::vector<Vec3> Canonical = HexagonProfile(HexRadius);
    for (const Vec3& P : Polygon)
    {
        if (std::fabs(std::hypot(P.X, P.Y) - HexRadius) > 1e-9) return false;
        bool Found = false;
        for (const Vec3& C : Canonical)
            if (std::fabs(P.X - C.X) <= 1e-9 && std::fabs(P.Y - C.Y) <= 1e-9) { Found = true; break; }
        if (!Found) return false;
    }
    bool Circle = true; Vec3 Centre{}; double Major = 0.0, Minor = 0.0;
    return CapLoopConic(Body, Hole, Circle, Centre, Major, Minor) && !Circle &&
           std::fabs(Centre.X) <= 1e-9 && std::fabs(Centre.Y) <= 1e-9 &&
           std::fabs(Major - HoleMajor) <= 1e-9 && std::fabs(Minor - HoleMinor) <= 1e-9;
}

[[nodiscard]] bool ConcaveTwinCircularCap(const BrepBody& Body, int Face) noexcept
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
    if (Holes.size() != 2) return false;
    std::vector<Vec3> Polygon;
    if (!CapLoopPolygon(Body, Outer, Polygon) || Polygon.size() != 6) return false;
    const std::vector<Vec3> Canonical = ConcaveProfile();
    for (const Vec3& P : Polygon)
    {
        bool Found = false;
        for (const Vec3& C : Canonical)
            if (std::fabs(P.X - C.X) <= 1e-9 && std::fabs(P.Y - C.Y) <= 1e-9) { Found = true; break; }
        if (!Found) return false;
    }
    int Positive = 0, Negative = 0;
    for (size_t I = 0; I < Polygon.size(); ++I)
    {
        const Vec3& A = Polygon[I];
        const Vec3& B = Polygon[(I + 1) % Polygon.size()];
        const Vec3& C = Polygon[(I + 2) % Polygon.size()];
        if ((std::fabs(A.X - B.X) <= 1e-9) == (std::fabs(A.Y - B.Y) <= 1e-9)) return false;
        const double Cross = (B.X - A.X) * (C.Y - B.Y) - (B.Y - A.Y) * (C.X - B.X);
        if (std::fabs(Cross) <= 1e-9) return false;
        if (Cross > 0.0) ++Positive; else ++Negative;
    }
    if (std::min(Positive, Negative) != 1 || std::max(Positive, Negative) != 5) return false;
    std::vector<Vec3> Centres;
    for (int Loop : Holes)
    {
        bool Circle = false; Vec3 Centre{}; double Major = 0.0, Minor = 0.0;
        if (!CapLoopConic(Body, Loop, Circle, Centre, Major, Minor) || !Circle ||
            std::fabs(Major - ConcaveHoleRadius) > 1e-9 || std::fabs(Minor - ConcaveHoleRadius) > 1e-9) return false;
        Centres.push_back(Centre);
    }
    std::sort(Centres.begin(), Centres.end(), [](const Vec3& A, const Vec3& B) { return A.X < B.X; });
    return std::fabs(Centres[0].X + 3.0) <= 1e-9 && std::fabs(Centres[1].X - 3.0) <= 1e-9 &&
           std::fabs(Centres[0].Y + 2.5) <= 1e-9 && std::fabs(Centres[1].Y + 2.5) <= 1e-9;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Batch 61 · hexagonal elliptical-holed and concave twin-circular-holed prism offsets");
    const auto HexDeliver = HexagonalEllipticalHoledPrism();
    const auto ConcaveDeliver = ConcaveTwinCircularHoledPrism();
    Panel.Expect("Both exact non-rectangular holed prism fixtures are constructed", HexDeliver && ConcaveDeliver);
    if (!HexDeliver || !ConcaveDeliver) return Panel.Conclude();

    const BrepBody Hexagon = HexDeliver.Payload, HexSnapshot = Hexagon;
    const BrepBody Concave = ConcaveDeliver.Payload, ConcaveSnapshot = Concave;
    const BodyReport HexBefore = Hexagon.Validate(), ConcaveBefore = Concave.Validate();
    const int HexTop = FaceToward(Hexagon, Vec3::UnitZ()), HexBottom = FaceToward(Hexagon, -Vec3::UnitZ()), HexSide = FaceToward(Hexagon, Vec3::UnitX());
    const int ConcaveTop = FaceToward(Concave, Vec3::UnitZ()), ConcaveBottom = FaceToward(Concave, -Vec3::UnitZ()), ConcaveSide = FaceToward(Concave, Vec3::UnitY());

    Panel.Section("Exact non-rectangular holed source recognition");
    Panel.Expect("Hexagonal elliptical-holed source is closed genus-one V14/E21/C42/L11/F9",
                 ExactCounts(Hexagon, 1, 14, 21, 42, 11, 9) && ExactSupports(Hexagon) && HexagonalEllipticalCap(Hexagon, HexTop));
    Panel.Expect("Concave twin-circular-holed source is closed genus-two V16/E24/C48/L14/F10",
                 ExactCounts(Concave, 2, 16, 24, 48, 14, 10) && ExactSupports(Concave) && ConcaveTwinCircularCap(Concave, ConcaveTop));
    Panel.Expect("Each source selects distinct upper, lower, and side faces",
                 HexTop != HexBottom && HexTop != HexSide && ConcaveTop != ConcaveBottom && ConcaveTop != ConcaveSide);

    Panel.Section("Multiple positive upper-cap distances");
    const double HexArea = 1.5 * std::sqrt(3.0) * HexRadius * HexRadius - ScalarCriteria::Pi * HoleMajor * HoleMinor;
    const double ConcaveArea = ConcaveProfileArea - 2.0 * ScalarCriteria::Pi * ConcaveHoleRadius * ConcaveHoleRadius;
    for (const double Distance : { OffsetA, OffsetB, OffsetC })
    {
        const auto HexResult = FaceEditSolver::OffsetExtrudedHexagonalEllipticalHoledPrism(Hexagon, HexTop, Distance);
        const auto ConcaveResult = FaceEditSolver::OffsetExtrudedConcaveTwinCircularHoledPrism(Concave, ConcaveTop, Distance);
        Panel.Expect("Hexagonal elliptical-holed offset retains genus-one topology and cap",
                     HexResult && ExactCounts(HexResult.Payload, 1, 14, 21, 42, 11, 9) && ExactSupports(HexResult.Payload) &&
                     HexagonalEllipticalCap(HexResult.Payload, FaceToward(HexResult.Payload, Vec3::UnitZ())));
        Panel.Expect("Concave twin-circular-holed offset retains genus-two topology and cap",
                     ConcaveResult && ExactCounts(ConcaveResult.Payload, 2, 16, 24, 48, 14, 10) && ExactSupports(ConcaveResult.Payload) &&
                     ConcaveTwinCircularCap(ConcaveResult.Payload, FaceToward(ConcaveResult.Payload, Vec3::UnitZ())));
        if (HexResult) Panel.Within("Hexagonal elliptical-holed volume follows the exact hexagon/ellipse identity",
                                    std::fabs(HexResult.Payload.Validate().Volume - HexArea * (Height + Distance)) / (HexArea * (Height + Distance)), 1e-3);
        if (ConcaveResult) Panel.Within("Concave twin-holed volume follows the exact L-profile/circle identity",
                                        std::fabs(ConcaveResult.Payload.Validate().Volume - ConcaveArea * (Height + Distance)) / (ConcaveArea * (Height + Distance)), 1e-3);
    }
    Panel.Expect("The public dispatcher reaches both non-rectangular holed routes",
                 FaceEditSolver::OffsetFace(Hexagon, HexTop, OffsetB) && FaceEditSolver::OffsetFace(Concave, ConcaveTop, OffsetB));
    Panel.Expect("Both non-rectangular holed reconstructions preserve their sources",
                 SameSource(Hexagon, HexSnapshot, HexBefore) && SameSource(Concave, ConcaveSnapshot, ConcaveBefore));

    Panel.Section("Transactional refusal boundaries");
    Panel.Expect("Only the hexagonal elliptical-holed upper cap is supported",
                 !FaceEditSolver::OffsetExtrudedHexagonalEllipticalHoledPrism(Hexagon, HexBottom, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedHexagonalEllipticalHoledPrism(Hexagon, HexSide, OffsetB));
    Panel.Expect("Only the concave twin-holed upper cap is supported",
                 !FaceEditSolver::OffsetExtrudedConcaveTwinCircularHoledPrism(Concave, ConcaveBottom, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedConcaveTwinCircularHoledPrism(Concave, ConcaveSide, OffsetB));
    Panel.Expect("The two strict routes refuse each other's source",
                 !FaceEditSolver::OffsetExtrudedHexagonalEllipticalHoledPrism(Concave, ConcaveTop, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedConcaveTwinCircularHoledPrism(Hexagon, HexTop, OffsetB));
    Panel.Expect("Zero, negative, and non-finite distances refuse for both routes",
                 !FaceEditSolver::OffsetExtrudedHexagonalEllipticalHoledPrism(Hexagon, HexTop, 0.0) &&
                 !FaceEditSolver::OffsetExtrudedHexagonalEllipticalHoledPrism(Hexagon, HexTop, -0.1) &&
                 !FaceEditSolver::OffsetExtrudedHexagonalEllipticalHoledPrism(Hexagon, HexTop, std::numeric_limits<double>::infinity()) &&
                 !FaceEditSolver::OffsetExtrudedConcaveTwinCircularHoledPrism(Concave, ConcaveTop, 0.0) &&
                 !FaceEditSolver::OffsetExtrudedConcaveTwinCircularHoledPrism(Concave, ConcaveTop, -0.1) &&
                 !FaceEditSolver::OffsetExtrudedConcaveTwinCircularHoledPrism(Concave, ConcaveTop, std::numeric_limits<double>::infinity()));
    const BrepBody HexMoved = Hexagon.Transformed(Mat4::Translation({ 2, 0, 0 }));
    const BrepBody ConcaveMoved = Concave.Transformed(Mat4::Translation({ 0, 2, 0 }));
    Panel.Expect("Translated non-rectangular holed profiles refuse the origin-anchored routes",
                 !FaceEditSolver::OffsetExtrudedHexagonalEllipticalHoledPrism(HexMoved, FaceToward(HexMoved, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedConcaveTwinCircularHoledPrism(ConcaveMoved, FaceToward(ConcaveMoved, Vec3::UnitZ()), OffsetB));
    const auto NarrowHexagon = HexagonalEllipticalHoledPrism(3.5);
    const auto NarrowEllipse = HexagonalEllipticalHoledPrism(HexRadius, 1.6, HoleMinor);
    const auto SmallConcaveHoles = ConcaveTwinCircularHoledPrism(0.8);
    const auto ShiftedConcaveHoles = ConcaveTwinCircularHoledPrism(ConcaveHoleRadius, { -3, 1.5, 0 }, { 3, -2.5, 0 });
    Panel.Expect("Non-canonical polygon, ellipse, and hole dimensions remain refused",
                 NarrowHexagon && NarrowEllipse && SmallConcaveHoles && ShiftedConcaveHoles &&
                 !FaceEditSolver::OffsetExtrudedHexagonalEllipticalHoledPrism(NarrowHexagon.Payload, FaceToward(NarrowHexagon.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedHexagonalEllipticalHoledPrism(NarrowEllipse.Payload, FaceToward(NarrowEllipse.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedConcaveTwinCircularHoledPrism(SmallConcaveHoles.Payload, FaceToward(SmallConcaveHoles.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedConcaveTwinCircularHoledPrism(ShiftedConcaveHoles.Payload, FaceToward(ShiftedConcaveHoles.Payload, Vec3::UnitZ()), OffsetB));
    const auto TwinEllipticalHexagon = [&]() noexcept
    {
        const auto Outer = NurbsCurve::Polyline(HexagonProfile(HexRadius), true);
        const auto Left = NurbsCurve::Ellipse({ -2, 0, 0 }, Vec3::UnitZ(), Vec3::UnitX(), 1.3, 0.9);
        const auto Right = NurbsCurve::Ellipse({ 2, 0, 0 }, Vec3::UnitZ(), Vec3::UnitX(), 1.3, 0.9);
        if (!Outer || !Left || !Right) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "twin elliptical hexagon fixture");
        return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Left.Payload, Right.Payload }, Vec3::UnitZ(), Height);
    }();
    const auto SingleConcaveHole = [&]() noexcept
    {
        const auto Outer = NurbsCurve::Polyline(ConcaveProfile(), true);
        const auto Hole = NurbsCurve::Circle({ 3, -2.5, 0 }, Vec3::UnitZ(), ConcaveHoleRadius);
        if (!Outer || !Hole) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "single concave hole fixture");
        return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Hole.Payload }, Vec3::UnitZ(), Height);
    }();
    Panel.Expect("Wrong hole counts remain outside both strict routes",
                 TwinEllipticalHexagon && SingleConcaveHole &&
                 !FaceEditSolver::OffsetExtrudedHexagonalEllipticalHoledPrism(TwinEllipticalHexagon.Payload, FaceToward(TwinEllipticalHexagon.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedConcaveTwinCircularHoledPrism(SingleConcaveHole.Payload, FaceToward(SingleConcaveHole.Payload, Vec3::UnitZ()), OffsetB));
    const auto CircularHexagonHole = [&]() noexcept
    {
        const auto Outer = NurbsCurve::Polyline(HexagonProfile(HexRadius), true);
        const auto Hole = NurbsCurve::Circle({ 0, 0, 0 }, Vec3::UnitZ(), 1.5);
        if (!Outer || !Hole) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "circular hexagon hole fixture");
        return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Hole.Payload }, Vec3::UnitZ(), Height);
    }();
    const auto EllipticalConcaveHoles = [&]() noexcept
    {
        const auto Outer = NurbsCurve::Polyline(ConcaveProfile(), true);
        const auto Left = NurbsCurve::Ellipse({ -3, -2.5, 0 }, Vec3::UnitZ(), Vec3::UnitX(), 1.8, 1.0);
        const auto Right = NurbsCurve::Ellipse({ 3, -2.5, 0 }, Vec3::UnitZ(), Vec3::UnitX(), 1.8, 1.0);
        if (!Outer || !Left || !Right) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "elliptical concave holes fixture");
        return BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Left.Payload, Right.Payload }, Vec3::UnitZ(), Height);
    }();
    Panel.Expect("Mismatched hole supports refuse the opposite strict route",
                 CircularHexagonHole && EllipticalConcaveHoles &&
                 !FaceEditSolver::OffsetExtrudedHexagonalEllipticalHoledPrism(CircularHexagonHole.Payload, FaceToward(CircularHexagonHole.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedConcaveTwinCircularHoledPrism(EllipticalConcaveHoles.Payload, FaceToward(EllipticalConcaveHoles.Payload, Vec3::UnitZ()), OffsetB));
    const auto PlainHexagon = NurbsCurve::Polyline(HexagonProfile(HexRadius), true);
    const auto PlainConcave = NurbsCurve::Polyline(ConcaveProfile(), true);
    const auto HoleFreeHexagon = PlainHexagon ? BrepBody::Extrude(PlainHexagon.Payload, Vec3::UnitZ(), Height)
                                              : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "hole-free hexagon");
    const auto HoleFreeConcave = PlainConcave ? BrepBody::Extrude(PlainConcave.Payload, Vec3::UnitZ(), Height)
                                              : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "hole-free concave prism");
    Panel.Expect("Hole-free hexagonal and concave prisms stay outside the holed routes",
                 HoleFreeHexagon && HoleFreeConcave &&
                 !FaceEditSolver::OffsetExtrudedHexagonalEllipticalHoledPrism(HoleFreeHexagon.Payload, FaceToward(HoleFreeHexagon.Payload, Vec3::UnitZ()), OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedConcaveTwinCircularHoledPrism(HoleFreeConcave.Payload, FaceToward(HoleFreeConcave.Payload, Vec3::UnitZ()), OffsetB));
    BrepBody MalformedHexagon = Hexagon; MalformedHexagon.Edges.front().Coedges.push_back(MalformedHexagon.Edges.front().Coedges.front());
    BrepBody MalformedConcave = Concave; MalformedConcave.Edges.front().Coedges.push_back(MalformedConcave.Edges.front().Coedges.front());
    Panel.Expect("Malformed non-rectangular holed profiles refuse without healing",
                 !FaceEditSolver::OffsetExtrudedHexagonalEllipticalHoledPrism(MalformedHexagon, HexTop, OffsetB) &&
                 !FaceEditSolver::OffsetExtrudedConcaveTwinCircularHoledPrism(MalformedConcave, ConcaveTop, OffsetB));
    Panel.Expect("All refusal paths preserve both valid sources",
                 SameSource(Hexagon, HexSnapshot, HexBefore) && SameSource(Concave, ConcaveSnapshot, ConcaveBefore));

    Panel.Section("Durable non-rectangular holed contact sheet");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Batch61_HexagonalEllipticalHoledAndConcaveTwinCircularHoledFaceOffset.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    const auto HexResult = FaceEditSolver::OffsetExtrudedHexagonalEllipticalHoledPrism(Hexagon, HexTop, OffsetB);
    const auto ConcaveResult = FaceEditSolver::OffsetExtrudedConcaveTwinCircularHoledPrism(Concave, ConcaveTop, OffsetB);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1800, 900);
    const bool Added = HexResult && ConcaveResult &&
        Host.Document().AddBody("SharpHexagonalEllipticalHoled", Hexagon.Transformed(Mat4::Translation({ -12, -7, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetHexagonalEllipticalHoled", HexResult.Payload.Transformed(Mat4::Translation({ -12, 7, 0 }))).Identity > 0 &&
        Host.Document().AddBody("SharpConcaveTwinCircularHoled", Concave.Transformed(Mat4::Translation({ 12, -7, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetConcaveTwinCircularHoled", ConcaveResult.Payload.Transformed(Mat4::Translation({ 12, 7, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") && Host.Execute("view orbit 25 -18") &&
        Host.Execute("view fit") && Host.Execute("render Batch61_HexagonalEllipticalHoledAndConcaveTwinCircularHoledFaceOffset");
    Panel.Expect("The consolidated non-rectangular holed proof render completes", Rendered);
    Panel.Expect("The durable non-rectangular holed proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
