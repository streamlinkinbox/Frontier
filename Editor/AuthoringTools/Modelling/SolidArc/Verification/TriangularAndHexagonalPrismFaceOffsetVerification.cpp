//=============================================================================================================================================
// SolidArc · Batch 57 · exact regular triangular and hexagonal prism upper-cap face offsets
//=============================================================================================================================================
#include "Kernel/FaceEditSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <limits>
#include <vector>

using namespace Frontier;

namespace
{
constexpr double Radius = 4.0;
constexpr double Height = 6.0;
constexpr double DistanceA = 0.5;
constexpr double DistanceB = 1.5;
constexpr double DistanceC = 3.0;

[[nodiscard]] Deliver<BrepBody> RegularPrism(int Sides, double RadiusValue = Radius, double HeightValue = Height) noexcept
{
    std::vector<Vec3> Points;
    for (int I = 0; I < Sides; ++I)
    {
        const double A = ScalarCriteria::TwoPi * static_cast<double>(I) / static_cast<double>(Sides);
        Points.push_back({ RadiusValue * std::cos(A), RadiusValue * std::sin(A), 0.0 });
    }
    const auto Profile = NurbsCurve::Polyline(Points, true);
    return Profile ? BrepBody::Extrude(Profile.Payload, Vec3::UnitZ(), HeightValue)
                   : Deliver<BrepBody>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
}

[[nodiscard]] Deliver<BrepBody> NonRegularTriangle() noexcept
{
    const auto Profile = NurbsCurve::Polyline({ { 4, 0, 0 }, { -2, 3, 0 }, { -2, -2, 0 } }, true);
    return Profile ? BrepBody::Extrude(Profile.Payload, Vec3::UnitZ(), Height)
                   : Deliver<BrepBody>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
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
           Body.Coedges.size() == Snapshot.Coedges.size() && Body.Loops.size() == Snapshot.Loops.size() && Body.Faces.size() == Snapshot.Faces.size();
}

[[nodiscard]] bool ExactRegularCap(const BrepBody& Body, int Face, int Sides) noexcept
{
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return false;
    const BrepFace& F = Body.Faces[Face];
    if (F.Surface.Classification != SurfaceClassification::Plane || F.Loops.size() != 1) return false;
    const Vec3 N = Body.FaceNormal(Face, 0.5 * (F.Surface.DomainStartU() + F.Surface.DomainEndU()),
                                   0.5 * (F.Surface.DomainStartV() + F.Surface.DomainEndV())).Normalised();
    if (N.Dot(Vec3::UnitZ()) < 1.0 - 1e-6) return false;
    const int Loop = F.Loops.front();
    if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size()) || Body.Loops[Loop].Coedges.size() != static_cast<size_t>(Sides)) return false;
    std::vector<Vec3> Points;
    for (int Coedge : Body.Loops[Loop].Coedges)
    {
        if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return false;
        const int Edge = Body.Coedges[Coedge].Edge;
        if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size())) return false;
        const BrepEdge& E = Body.Edges[Edge];
        if (E.Curve.Classification != CurveClassification::Line || E.Curve.Degree != 1 || E.VertexStart < 0 || E.VertexEnd < 0) return false;
        Points.push_back(Body.Vertices[E.VertexStart].Point);
    }
    if (Points.size() != static_cast<size_t>(Sides)) return false;
    double RadiusValue = std::hypot(Points.front().X, Points.front().Y), EdgeLength = 0.0;
    bool PositiveX = false;
    for (size_t I = 0; I < Points.size(); ++I)
    {
        const Vec3& A = Points[I], B = Points[(I + 1) % Points.size()];
        const double R = std::hypot(A.X, A.Y), L = std::hypot(B.X - A.X, B.Y - A.Y);
        if (std::fabs(R - RadiusValue) > 1e-7 || L <= 1e-7 || (I > 0 && std::fabs(L - EdgeLength) > 1e-7)) return false;
        if (I == 0) EdgeLength = L;
        PositiveX = PositiveX || (std::fabs(A.X - RadiusValue) < 1e-7 && std::fabs(A.Y) < 1e-7);
    }
    return PositiveX;
}

[[nodiscard]] bool ExactSupports(const BrepBody& Body) noexcept
{
    for (const BrepFace& F : Body.Faces)
        if (F.Surface.Classification != SurfaceClassification::Plane && F.Surface.Classification != SurfaceClassification::Extrusion) return false;
    return true;
}

[[nodiscard]] bool ExactTopology(const BrepBody& Body, int Sides) noexcept
{
    const BodyReport R = Body.Validate();
    return R.Solid() && R.Hulls == 1 && R.Genus == 0 && R.OpenEdges == 0 && R.NonManifoldEdges == 0 && R.MisorientedEdges == 0 &&
           Body.Vertices.size() == static_cast<size_t>(2 * Sides) && Body.Edges.size() == static_cast<size_t>(3 * Sides) &&
           Body.Coedges.size() == static_cast<size_t>(6 * Sides) && Body.Loops.size() == static_cast<size_t>(Sides + 2) &&
           Body.Faces.size() == static_cast<size_t>(Sides + 2);
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Batch 57 · regular triangular and hexagonal prism face offsets");
    const auto TriangleDeliver = RegularPrism(3);
    const auto HexagonDeliver = RegularPrism(6);
    Panel.Expect("Both exact regular prism fixtures are constructed", TriangleDeliver && HexagonDeliver);
    if (!TriangleDeliver || !HexagonDeliver) return Panel.Conclude();

    const BrepBody Triangle = TriangleDeliver.Payload, TriangleSnapshot = Triangle;
    const BrepBody Hexagon = HexagonDeliver.Payload, HexagonSnapshot = Hexagon;
    const BodyReport TriangleBefore = Triangle.Validate(), HexagonBefore = Hexagon.Validate();
    const int TriangleTop = FaceToward(Triangle, Vec3::UnitZ()), TriangleBottom = FaceToward(Triangle, -Vec3::UnitZ()), TriangleSide = FaceToward(Triangle, Vec3::UnitX());
    const int HexagonTop = FaceToward(Hexagon, Vec3::UnitZ()), HexagonBottom = FaceToward(Hexagon, -Vec3::UnitZ()), HexagonSide = FaceToward(Hexagon, Vec3::UnitX());

    Panel.Section("Exact regular source recognition");
    Panel.Expect("Triangle source is closed V6/E9/C18/L5/F5", ExactTopology(Triangle, 3) && ExactSupports(Triangle) &&
                 ExactRegularCap(Triangle, TriangleTop, 3) && !ExactRegularCap(Triangle, TriangleBottom, 3));
    Panel.Expect("Hexagon source is closed V12/E18/C36/L8/F8", ExactTopology(Hexagon, 6) && ExactSupports(Hexagon) && ExactRegularCap(Hexagon, HexagonTop, 6));
    Panel.Expect("Each route selects distinct upper, lower, and side faces", TriangleTop != TriangleBottom && TriangleTop != TriangleSide &&
                 HexagonTop != HexagonBottom && HexagonTop != HexagonSide);

    Panel.Section("Multiple positive upper-cap distances");
    for (const double Distance : { DistanceA, DistanceB, DistanceC })
    {
        const auto TriangleResult = FaceEditSolver::OffsetExtrudedTriangularPrism(Triangle, TriangleTop, Distance);
        const auto HexagonResult = FaceEditSolver::OffsetExtrudedHexagonalPrism(Hexagon, HexagonTop, Distance);
        const double TriangleArea = 3.0 * std::sqrt(3.0) * Radius * Radius / 4.0;
        const double HexagonArea = 3.0 * std::sqrt(3.0) * Radius * Radius / 2.0;
        Panel.Expect("Triangle offset retains closed V6/E9/C18/L5/F5 topology", TriangleResult && ExactTopology(TriangleResult.Payload, 3) && ExactSupports(TriangleResult.Payload) &&
                     ExactRegularCap(TriangleResult.Payload, FaceToward(TriangleResult.Payload, Vec3::UnitZ()), 3));
        Panel.Expect("Hexagon offset retains closed V12/E18/C36/L8/F8 topology", HexagonResult && ExactTopology(HexagonResult.Payload, 6) && ExactSupports(HexagonResult.Payload) &&
                     ExactRegularCap(HexagonResult.Payload, FaceToward(HexagonResult.Payload, Vec3::UnitZ()), 6));
        if (TriangleResult) Panel.Within("Triangle volume follows exact regular-triangle area times height", std::fabs(TriangleResult.Payload.Validate().Volume - TriangleArea * (Height + Distance)) / (TriangleArea * (Height + Distance)), 1e-3);
        if (HexagonResult) Panel.Within("Hexagon volume follows exact regular-hexagon area times height", std::fabs(HexagonResult.Payload.Validate().Volume - HexagonArea * (Height + Distance)) / (HexagonArea * (Height + Distance)), 1e-3);
    }
    Panel.Expect("The public dispatcher reaches both regular-prism routes", FaceEditSolver::OffsetFace(Triangle, TriangleTop, DistanceB) &&
                 FaceEditSolver::OffsetFace(Hexagon, HexagonTop, DistanceB));
    Panel.Expect("Both separate reconstructions preserve their sources", SameSource(Triangle, TriangleSnapshot, TriangleBefore) &&
                 SameSource(Hexagon, HexagonSnapshot, HexagonBefore));

    Panel.Section("Transactional refusal boundaries");
    Panel.Expect("Only the triangular upper cap is supported", !FaceEditSolver::OffsetExtrudedTriangularPrism(Triangle, TriangleBottom, DistanceB) &&
                 !FaceEditSolver::OffsetExtrudedTriangularPrism(Triangle, TriangleSide, DistanceB));
    Panel.Expect("Only the hexagonal upper cap is supported", !FaceEditSolver::OffsetExtrudedHexagonalPrism(Hexagon, HexagonBottom, DistanceB) &&
                 !FaceEditSolver::OffsetExtrudedHexagonalPrism(Hexagon, HexagonSide, DistanceB));
    Panel.Expect("The routes refuse each other's polygon topology", !FaceEditSolver::OffsetExtrudedTriangularPrism(Hexagon, HexagonTop, DistanceB) &&
                 !FaceEditSolver::OffsetExtrudedHexagonalPrism(Triangle, TriangleTop, DistanceB));
    Panel.Expect("Zero, negative, and non-finite distances refuse for both routes", !FaceEditSolver::OffsetExtrudedTriangularPrism(Triangle, TriangleTop, 0.0) &&
                 !FaceEditSolver::OffsetExtrudedTriangularPrism(Triangle, TriangleTop, -0.1) &&
                 !FaceEditSolver::OffsetExtrudedTriangularPrism(Triangle, TriangleTop, std::numeric_limits<double>::infinity()) &&
                 !FaceEditSolver::OffsetExtrudedHexagonalPrism(Hexagon, HexagonTop, 0.0) &&
                 !FaceEditSolver::OffsetExtrudedHexagonalPrism(Hexagon, HexagonTop, -0.1) &&
                 !FaceEditSolver::OffsetExtrudedHexagonalPrism(Hexagon, HexagonTop, std::numeric_limits<double>::infinity()));
    const auto TranslatedTriangle = RegularPrism(3, Radius, Height);
    const auto TranslatedHexagon = RegularPrism(6, Radius, Height);
    const BrepBody TriangleMoved = TranslatedTriangle.Payload.Transformed(Mat4::Translation({ 2, 0, 0 }));
    const BrepBody HexagonMoved = TranslatedHexagon.Payload.Transformed(Mat4::Translation({ 2, 0, 0 }));
    Panel.Expect("Translated regular prisms refuse the origin-anchored routes", !FaceEditSolver::OffsetExtrudedTriangularPrism(TriangleMoved, FaceToward(TriangleMoved, Vec3::UnitZ()), DistanceB) &&
                 !FaceEditSolver::OffsetExtrudedHexagonalPrism(HexagonMoved, FaceToward(HexagonMoved, Vec3::UnitZ()), DistanceB));
    const auto Irregular = NonRegularTriangle();
    Panel.Expect("A non-regular triangular prism refuses", Irregular && !FaceEditSolver::OffsetExtrudedTriangularPrism(Irregular.Payload, FaceToward(Irregular.Payload, Vec3::UnitZ()), DistanceB));
    BrepBody MalformedTriangle = Triangle; MalformedTriangle.Edges.front().Coedges.push_back(MalformedTriangle.Edges.front().Coedges.front());
    BrepBody MalformedHexagon = Hexagon; MalformedHexagon.Edges.front().Coedges.push_back(MalformedHexagon.Edges.front().Coedges.front());
    Panel.Expect("Malformed regular prisms refuse without healing", !FaceEditSolver::OffsetExtrudedTriangularPrism(MalformedTriangle, TriangleTop, DistanceB) &&
                 !FaceEditSolver::OffsetExtrudedHexagonalPrism(MalformedHexagon, HexagonTop, DistanceB));
    Panel.Expect("All refusal paths preserve both valid sources", SameSource(Triangle, TriangleSnapshot, TriangleBefore) &&
                 SameSource(Hexagon, HexagonSnapshot, HexagonBefore));

    Panel.Section("Durable regular-prism contact sheet");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Batch57_TriangularAndHexagonalPrismFaceOffset.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    const auto TriangleResult = FaceEditSolver::OffsetExtrudedTriangularPrism(Triangle, TriangleTop, DistanceB);
    const auto HexagonResult = FaceEditSolver::OffsetExtrudedHexagonalPrism(Hexagon, HexagonTop, DistanceB);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1800, 900);
    const bool Added = TriangleResult && HexagonResult &&
        Host.Document().AddBody("SharpTriangle", Triangle.Transformed(Mat4::Translation({ -12, -4, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetTriangle", TriangleResult.Payload.Transformed(Mat4::Translation({ -4, -4, 0 }))).Identity > 0 &&
        Host.Document().AddBody("SharpHexagon", Hexagon.Transformed(Mat4::Translation({ 5, -4, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetHexagon", HexagonResult.Payload.Transformed(Mat4::Translation({ 13, -4, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") && Host.Execute("view orbit 25 -18") &&
        Host.Execute("view fit") && Host.Execute("render Batch57_TriangularAndHexagonalPrismFaceOffset");
    Panel.Expect("The consolidated regular-prism proof render completes", Rendered);
    Panel.Expect("The durable regular-prism proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
