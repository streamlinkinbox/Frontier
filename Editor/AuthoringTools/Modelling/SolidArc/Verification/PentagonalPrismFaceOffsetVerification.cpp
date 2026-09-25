//=============================================================================================================================================
// SolidArc · Phase 41 · bounded pentagonal-prism face offset
//=============================================================================================================================================
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
constexpr double Radius = 4.5;
constexpr double Height = 7.0;
constexpr double Distance = 1.25;

[[nodiscard]] Deliver<BrepBody> PentagonPrism() noexcept
{
    std::vector<Vec3> Points;
    for (int I = 0; I < 5; ++I)
    {
        const double A = ScalarCriteria::TwoPi * static_cast<double>(I) / 5.0;
        Points.push_back({ Radius * std::cos(A), Radius * std::sin(A), 0.0 });
    }
    const Deliver<NurbsCurve> Profile = NurbsCurve::Polyline(Points, true);
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
           After.OpenEdges == Before.OpenEdges && After.NonManifoldEdges == Before.NonManifoldEdges &&
           After.MisorientedEdges == Before.MisorientedEdges && std::fabs(After.Volume - Before.Volume) <= 1e-12 &&
           Body.Vertices.size() == Snapshot.Vertices.size() && Body.Edges.size() == Snapshot.Edges.size() &&
           Body.Faces.size() == Snapshot.Faces.size();
}

[[nodiscard]] bool HasVertex(const BrepBody& Body, Vec3 Point, double Tolerance = 1e-8) noexcept
{
    for (const BrepVertex& Vertex : Body.Vertices)
        if (Vertex.Point.Distance(Point) <= Tolerance) return true;
    return false;
}

[[nodiscard]] Deliver<BrepBody> TrianglePrism() noexcept
{
    const std::vector<Vec3> Points{ { 0, 0, 0 }, { 6, 0, 0 }, { 2, 4, 0 } };
    const Deliver<NurbsCurve> Profile = NurbsCurve::Polyline(Points, true);
    return Profile ? BrepBody::Extrude(Profile.Payload, Vec3::UnitZ(), Height)
                   : Deliver<BrepBody>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
}

[[nodiscard]] Deliver<BrepBody> ConcavePentagonPrism() noexcept
{
    const std::vector<Vec3> Points{ { -5, -3, 0 }, { 5, -3, 0 }, { 0, 0, 0 }, { 5, 3, 0 }, { -5, 3, 0 } };
    const Deliver<NurbsCurve> Profile = NurbsCurve::Polyline(Points, true);
    return Profile ? BrepBody::Extrude(Profile.Payload, Vec3::UnitZ(), Height)
                   : Deliver<BrepBody>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
}

[[nodiscard]] Deliver<BrepBody> HexPrism() noexcept
{
    std::vector<Vec3> Points;
    for (int I = 0; I < 6; ++I)
    {
        const double A = ScalarCriteria::TwoPi * static_cast<double>(I) / 6.0;
        Points.push_back({ Radius * std::cos(A), Radius * std::sin(A), 0.0 });
    }
    const Deliver<NurbsCurve> Profile = NurbsCurve::Polyline(Points, true);
    return Profile ? BrepBody::Extrude(Profile.Payload, Vec3::UnitZ(), Height)
                   : Deliver<BrepBody>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 41 · bounded pentagonal-prism face offset");
    const auto SourceDeliver = PentagonPrism();
    Panel.Expect("The distinct regular pentagonal prism source is constructed", static_cast<bool>(SourceDeliver));
    const BrepBody Source = SourceDeliver.Payload;
    const BrepBody Snapshot = Source;
    const BodyReport Before = Source.Validate();
    const int Top = FaceToward(Source, Vec3::UnitZ());
    const int Bottom = FaceToward(Source, -Vec3::UnitZ());
    Panel.Expect("The source has V10/E15/C30/L7/F7 topology", SourceDeliver && Before.Solid() && Before.Hulls == 1 &&
                 Before.Genus == 0 && Source.Vertices.size() == 10 && Source.Edges.size() == 15 &&
                 Source.Coedges.size() == 30 && Source.Loops.size() == 7 && Source.Faces.size() == 7);
    Panel.Expect("The upper and lower planar caps resolve independently", Top >= 0 && Bottom >= 0 && Top != Bottom);

    Panel.Section("Exact non-box face offset reconstruction");
    const auto Offset = SourceDeliver ? FaceEditSolver::OffsetExtrudedConvexPrism(Source, Top, Distance)
                                      : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no prism source");
    Panel.Expect("The selected upper cap produces a closed offset solid", Offset && Offset.Payload.Validate().Solid());
    if (Offset)
    {
        const BodyReport R = Offset.Payload.Validate();
        const double Area = 2.5 * Radius * Radius * std::sin(ScalarCriteria::TwoPi / 5.0);
        const double Expected = Area * (Height + Distance);
        Panel.Expect("The offset retains V10/E15/C30/L7/F7 topology", R.Hulls == 1 && R.Genus == 0 &&
                     Offset.Payload.Vertices.size() == 10 && Offset.Payload.Edges.size() == 15 &&
                     Offset.Payload.Coedges.size() == 30 && Offset.Payload.Loops.size() == 7 &&
                     Offset.Payload.Faces.size() == 7 && R.OpenEdges == 0 && R.NonManifoldEdges == 0 &&
                     R.MisorientedEdges == 0);
        Panel.Within("The offset volume follows the exact pentagonal-prism volume", std::fabs(R.Volume - Expected), 1e-1);
        Panel.Expect("The original lower profile remains present", HasVertex(Offset.Payload, { Radius, 0, 0 }));
        Panel.Expect("The offset upper profile is exactly one distance above the source", HasVertex(Offset.Payload, { Radius, 0, Height + Distance }));
        Panel.Expect("The separate offset reconstruction preserves the source", SameSource(Source, Snapshot, Before));
        Panel.Expect("The offset remains analytic planar/extruded geometry", std::all_of(Offset.Payload.Faces.begin(), Offset.Payload.Faces.end(),
            [](const BrepFace& F) { return F.Surface.Classification == SurfaceClassification::Plane || F.Surface.Classification == SurfaceClassification::Extrusion; }));
    }
    const auto Dispatch = FaceEditSolver::OffsetFace(Source, Top, Distance);
    Panel.Expect("The public face-offset dispatcher reaches the pentagonal route", Dispatch && Dispatch.Payload.Validate().Solid());
    Panel.Expect("Only the upper cap is supported by this bounded route", !FaceEditSolver::OffsetExtrudedConvexPrism(Source, Bottom, Distance));

    Panel.Section("Pentagonal-prism offset refusal boundaries");
    const auto Box = BrepBody::Box({ -4, -4, 0 }, { 4, 4, Height });
    Panel.Expect("A rectangular box remains outside the distinct pentagonal API", Box && !FaceEditSolver::OffsetExtrudedConvexPrism(Box.Payload, FaceToward(Box.Payload, Vec3::UnitZ()), Distance));
    const auto Hex = HexPrism();
    Panel.Expect("A six-sided prism refuses the five-sided route", Hex && !FaceEditSolver::OffsetExtrudedConvexPrism(Hex.Payload, FaceToward(Hex.Payload, Vec3::UnitZ()), Distance));
    const auto Triangle = TrianglePrism();
    Panel.Expect("A triangular prism refuses the five-sided route", Triangle && !FaceEditSolver::OffsetExtrudedConvexPrism(Triangle.Payload, FaceToward(Triangle.Payload, Vec3::UnitZ()), Distance));
    const auto Concave = ConcavePentagonPrism();
    Panel.Expect("A concave pentagonal prism refuses", Concave && !FaceEditSolver::OffsetExtrudedConvexPrism(Concave.Payload, FaceToward(Concave.Payload, Vec3::UnitZ()), Distance));
    const auto Cylinder = BrepBody::Cylinder({ 0, 0, 0 }, Vec3::UnitZ(), Radius, Height);
    Panel.Expect("A curved cylinder refuses the polygonal-prism route", Cylinder && !FaceEditSolver::OffsetExtrudedConvexPrism(Cylinder.Payload, FaceToward(Cylinder.Payload, Vec3::UnitZ()), Distance));
    Panel.Expect("Lower, zero, negative, and non-finite offsets refuse", !FaceEditSolver::OffsetExtrudedConvexPrism(Source, Bottom, Distance) &&
                 !FaceEditSolver::OffsetExtrudedConvexPrism(Source, Top, 0.0) &&
                 !FaceEditSolver::OffsetExtrudedConvexPrism(Source, Top, -0.1) &&
                 !FaceEditSolver::OffsetExtrudedConvexPrism(Source, Top, std::numeric_limits<double>::infinity()));
    BrepBody Malformed = Source;
    Malformed.Edges[0].Coedges.push_back(Malformed.Edges[0].Coedges.front());
    Panel.Expect("A malformed pentagonal prism refuses transactionally", !FaceEditSolver::OffsetExtrudedConvexPrism(Malformed, Top, Distance));
    Panel.Expect("All refusal paths preserve the source", SameSource(Source, Snapshot, Before));

    Panel.Section("Distinct non-box face-offset proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase41_PentagonalPrismFaceOffset.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Offset &&
        Host.Document().AddBody("SharpPentagonalPrism", Source.Transformed(Mat4::Translation({ -8, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetPentagonalPrism", Offset.Payload.Transformed(Mat4::Translation({ 8, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 35 -18") && Host.Execute("view fit") &&
        Host.Execute("render Phase41_PentagonalPrismFaceOffset");
    Panel.Expect("The pentagonal-prism offset proof render completes", Rendered);
    Panel.Expect("The pentagonal-prism offset proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
