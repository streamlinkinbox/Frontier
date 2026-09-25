//=============================================================================================================================================
// SolidArc · Phase 47 · bounded orthogonal concave-prism face offset
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
constexpr double Height = 6.0;
constexpr double Offset = 1.5;
constexpr double ProfileArea = 61.0;
const std::vector<Vec3> LProfile{
    { -6, -4, 0 }, { 6, -4, 0 }, { 6, -1, 0 }, { -1, -1, 0 }, { -1, 4, 0 }, { -6, 4, 0 }
};

[[nodiscard]] Deliver<BrepBody> Prism(const std::vector<Vec3>& Points) noexcept
{
    const Deliver<NurbsCurve> Profile = NurbsCurve::Polyline(Points, true);
    return Profile ? BrepBody::Extrude(Profile.Payload, Vec3::UnitZ(), Height)
                   : Deliver<BrepBody>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
}

[[nodiscard]] Deliver<BrepBody> ConcavePrism() noexcept { return Prism(LProfile); }

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
           After.Loops == Before.Loops && After.Genus == Before.Genus && After.OpenEdges == Before.OpenEdges &&
           After.NonManifoldEdges == Before.NonManifoldEdges && After.MisorientedEdges == Before.MisorientedEdges &&
           std::fabs(After.Volume - Before.Volume) <= 1e-12 && Body.Vertices.size() == Snapshot.Vertices.size() &&
           Body.Edges.size() == Snapshot.Edges.size() && Body.Coedges.size() == Snapshot.Coedges.size() &&
           Body.Loops.size() == Snapshot.Loops.size() && Body.Faces.size() == Snapshot.Faces.size();
}

[[nodiscard]] bool IsOrthogonalConcaveCap(const BrepBody& Body, int Face) noexcept
{
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return false;
    const BrepFace& Cap = Body.Faces[Face];
    if (Cap.Surface.Classification != SurfaceClassification::Plane || Cap.Loops.size() != 1) return false;
    const Vec3 N = Body.FaceNormal(Face, 0.5 * (Cap.Surface.DomainStartU() + Cap.Surface.DomainEndU()),
                                   0.5 * (Cap.Surface.DomainStartV() + Cap.Surface.DomainEndV())).Normalised();
    if (N.Dot(Vec3::UnitZ()) < 1.0 - 1e-6) return false;
    const int Loop = Cap.Loops.front();
    if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size()) || Body.Loops[Loop].Coedges.size() != 6) return false;
    std::vector<Vec3> Polygon;
    for (int Coedge : Body.Loops[Loop].Coedges)
    {
        if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return false;
        const BrepCoedge& C = Body.Coedges[Coedge];
        if (C.Edge < 0 || C.Edge >= static_cast<int>(Body.Edges.size())) return false;
        const BrepEdge& E = Body.Edges[C.Edge];
        if (E.Curve.Classification != CurveClassification::Line || E.VertexStart < 0 || E.VertexEnd < 0) return false;
        const int Start = C.Reversed ? E.VertexEnd : E.VertexStart;
        Polygon.push_back(Body.Vertices[Start].Point);
    }
    int Positive = 0, Negative = 0;
    for (size_t I = 0; I < Polygon.size(); ++I)
    {
        const Vec3& A = Polygon[I];
        const Vec3& B = Polygon[(I + 1) % Polygon.size()];
        const Vec3& C = Polygon[(I + 2) % Polygon.size()];
        const double DX = B.X - A.X, DY = B.Y - A.Y;
        if ((std::fabs(DX) <= 1e-7) == (std::fabs(DY) <= 1e-7)) return false;
        const double Cross = DX * (C.Y - B.Y) - DY * (C.X - B.X);
        if (Cross > 0.0) ++Positive; else if (Cross < 0.0) ++Negative; else return false;
    }
    return std::min(Positive, Negative) == 1 && std::max(Positive, Negative) == 5;
}

[[nodiscard]] bool HasVertex(const BrepBody& Body, Vec3 Point, double Tolerance = 1e-8) noexcept
{
    for (const BrepVertex& Vertex : Body.Vertices)
        if (Vertex.Point.Distance(Point) <= Tolerance) return true;
    return false;
}

[[nodiscard]] bool AllAnalyticPrismFaces(const BrepBody& Body) noexcept
{
    return std::all_of(Body.Faces.begin(), Body.Faces.end(), [](const BrepFace& F) {
        return F.Surface.Classification == SurfaceClassification::Plane || F.Surface.Classification == SurfaceClassification::Extrusion;
    });
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 47 · bounded orthogonal concave-prism face offset");
    const auto SourceDeliver = ConcavePrism();
    Panel.Expect("The orthogonal L-shaped concave prism is constructed", static_cast<bool>(SourceDeliver));
    const BrepBody Source = SourceDeliver.Payload;
    const BrepBody Snapshot = Source;
    const BodyReport Before = Source.Validate();
    const int Top = FaceToward(Source, Vec3::UnitZ());
    const int Bottom = FaceToward(Source, -Vec3::UnitZ());
    Panel.Expect("The source has closed genus-zero V12/E18/C36/L8/F8 topology", SourceDeliver && Before.Solid() &&
                 Before.Hulls == 1 && Before.Genus == 0 && Source.Vertices.size() == 12 && Source.Edges.size() == 18 &&
                 Source.Coedges.size() == 36 && Source.Loops.size() == 8 && Source.Faces.size() == 8);
    Panel.Expect("The upper cap is one six-edge orthogonal concave loop", IsOrthogonalConcaveCap(Source, Top));
    Panel.Expect("The upper and lower cap selections are distinct", Top >= 0 && Bottom >= 0 && Top != Bottom);

    Panel.Section("Exact non-convex face offset reconstruction");
    const auto Result = SourceDeliver ? FaceEditSolver::OffsetExtrudedConcavePrism(Source, Top, Offset)
                                      : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no concave source");
    Panel.Expect("The selected upper cap produces a closed concave offset solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport R = Result.Payload.Validate();
        Panel.Expect("The result retains V12/E18/C36/L8/F8 genus-zero topology", R.Hulls == 1 && R.Genus == 0 &&
                     Result.Payload.Vertices.size() == 12 && Result.Payload.Edges.size() == 18 &&
                     Result.Payload.Coedges.size() == 36 && Result.Payload.Loops.size() == 8 && Result.Payload.Faces.size() == 8 &&
                     R.OpenEdges == 0 && R.NonManifoldEdges == 0 && R.MisorientedEdges == 0);
        Panel.Within("The offset volume follows L-profile area times extended height", std::fabs(R.Volume - ProfileArea * (Height + Offset)), 1e-9);
        Panel.Expect("The result retains the concave upper profile", IsOrthogonalConcaveCap(Result.Payload, FaceToward(Result.Payload, Vec3::UnitZ())));
        Panel.Expect("The lower L-profile is unchanged and the cap rises by the offset", HasVertex(Result.Payload, { -6, -4, 0 }) &&
                     HasVertex(Result.Payload, { -6, -4, Height + Offset }));
        Panel.Expect("The result remains planar/extrusion analytic geometry", AllAnalyticPrismFaces(Result.Payload));
        Panel.Expect("The separate reconstruction preserves the source", SameSource(Source, Snapshot, Before));
    }
    const auto Dispatch = FaceEditSolver::OffsetFace(Source, Top, Offset);
    Panel.Expect("The public face-offset dispatcher reaches the concave route", Dispatch && Dispatch.Payload.Validate().Solid());
    Panel.Expect("Only the upper cap is supported", !FaceEditSolver::OffsetExtrudedConcavePrism(Source, Bottom, Offset));

    Panel.Section("Concave-prism offset refusal boundaries");
    const auto Box = BrepBody::Box({ -6, -4, 0 }, { 6, 4, Height });
    Panel.Expect("A box refuses the distinct concave-prism API", Box && !FaceEditSolver::OffsetExtrudedConcavePrism(Box.Payload, FaceToward(Box.Payload, Vec3::UnitZ()), Offset));
    const auto ConvexHex = Prism({ { -6, -3, 0 }, { 0, -6, 0 }, { 6, -3, 0 }, { 6, 3, 0 }, { 0, 6, 0 }, { -6, 3, 0 } });
    Panel.Expect("A convex six-sided prism refuses", ConvexHex && !FaceEditSolver::OffsetExtrudedConcavePrism(ConvexHex.Payload, FaceToward(ConvexHex.Payload, Vec3::UnitZ()), Offset));
    const auto Pentagon = Prism({ { -5, -3, 0 }, { 5, -3, 0 }, { 6, 2, 0 }, { 0, 5, 0 }, { -6, 2, 0 } });
    Panel.Expect("A five-sided prism refuses", Pentagon && !FaceEditSolver::OffsetExtrudedConcavePrism(Pentagon.Payload, FaceToward(Pentagon.Payload, Vec3::UnitZ()), Offset));
    const auto Triangle = Prism({ { -5, -3, 0 }, { 5, -3, 0 }, { 0, 4, 0 } });
    Panel.Expect("A triangular prism refuses", Triangle && !FaceEditSolver::OffsetExtrudedConcavePrism(Triangle.Payload, FaceToward(Triangle.Payload, Vec3::UnitZ()), Offset));
    const auto NonOrthogonal = Prism({ { -6, -4, 0 }, { 6, -4, 0 }, { 6, -1, 0 }, { -1, -1, 0 }, { -2, 4, 0 }, { -6, 4, 0 } });
    Panel.Expect("A non-orthogonal concave hexagon refuses", NonOrthogonal && !FaceEditSolver::OffsetExtrudedConcavePrism(NonOrthogonal.Payload, FaceToward(NonOrthogonal.Payload, Vec3::UnitZ()), Offset));
    const auto Cylinder = BrepBody::Cylinder({ 0, 0, 0 }, Vec3::UnitZ(), 5.0, Height);
    Panel.Expect("A curved cylinder refuses", Cylinder && !FaceEditSolver::OffsetExtrudedConcavePrism(Cylinder.Payload, FaceToward(Cylinder.Payload, Vec3::UnitZ()), Offset));
    Panel.Expect("Lower, zero, negative, and non-finite offsets refuse", !FaceEditSolver::OffsetExtrudedConcavePrism(Source, Bottom, Offset) &&
                 !FaceEditSolver::OffsetExtrudedConcavePrism(Source, Top, 0.0) &&
                 !FaceEditSolver::OffsetExtrudedConcavePrism(Source, Top, -0.1) &&
                 !FaceEditSolver::OffsetExtrudedConcavePrism(Source, Top, std::numeric_limits<double>::infinity()));
    BrepBody Malformed = Source;
    Malformed.Edges[0].Coedges.push_back(Malformed.Edges[0].Coedges.front());
    Panel.Expect("A malformed concave prism refuses transactionally", !FaceEditSolver::OffsetExtrudedConcavePrism(Malformed, Top, Offset));
    Panel.Expect("All refusal paths preserve the source", SameSource(Source, Snapshot, Before));

    Panel.Section("Distinct concave-profile face-offset proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase47_ConcavePrismFaceOffset.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("SharpConcavePrism", Source.Transformed(Mat4::Translation({ -10, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetConcavePrism", Result.Payload.Transformed(Mat4::Translation({ 10, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 35 -18") && Host.Execute("view fit") &&
        Host.Execute("render Phase47_ConcavePrismFaceOffset");
    Panel.Expect("The concave-prism offset proof render completes", Rendered);
    Panel.Expect("The concave-prism offset proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
