//=============================================================================================================================================
// SolidArc · Phase 49 · bounded orthogonal concave-prism side draft
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
constexpr double Angle = 12.0 * ScalarCriteria::Pi / 180.0;
constexpr double Delta = std::tan(Angle) * Height;
constexpr double DraftedVolume = Height * (61.0 + 0.5 * 12.0 * Delta);
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

[[nodiscard]] bool HasVertex(const BrepBody& Body, Vec3 Point, double Tolerance = 1e-8) noexcept
{
    for (const BrepVertex& Vertex : Body.Vertices)
        if (Vertex.Point.Distance(Point) <= Tolerance) return true;
    return false;
}

[[nodiscard]] bool IsConcaveCap(const BrepBody& Body, int Face, bool Upper) noexcept
{
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return false;
    const BrepFace& Cap = Body.Faces[Face];
    if (Cap.Surface.Classification != SurfaceClassification::Plane || Cap.Loops.size() != 1) return false;
    const Vec3 N = Body.FaceNormal(Face, 0.5 * (Cap.Surface.DomainStartU() + Cap.Surface.DomainEndU()),
                                   0.5 * (Cap.Surface.DomainStartV() + Cap.Surface.DomainEndV())).Normalised();
    if ((Upper && N.Dot(Vec3::UnitZ()) < 1.0 - 1e-6) || (!Upper && N.Dot(-Vec3::UnitZ()) < 1.0 - 1e-6)) return false;
    const int Loop = Cap.Loops.front();
    if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size()) || Body.Loops[Loop].Coedges.size() != 6) return false;
    int Positive = 0, Negative = 0;
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
    for (size_t I = 0; I < Polygon.size(); ++I)
    {
        const Vec3& A = Polygon[I], B = Polygon[(I + 1) % Polygon.size()], C = Polygon[(I + 2) % Polygon.size()];
        const double Cross = (B.X - A.X) * (C.Y - B.Y) - (B.Y - A.Y) * (C.X - B.X);
        if (Cross > 0.0) ++Positive; else if (Cross < 0.0) ++Negative; else return false;
    }
    return std::min(Positive, Negative) == 1 && std::max(Positive, Negative) == 5;
}

[[nodiscard]] bool HasSupports(const BrepBody& Body) noexcept
{
    int Planes = 0, Ruled = 0;
    for (const BrepFace& F : Body.Faces)
    {
        if (F.Surface.Classification == SurfaceClassification::Plane) ++Planes;
        else if (F.Surface.Classification == SurfaceClassification::Ruled) ++Ruled;
        else return false;
    }
    return Planes == 2 && Ruled == 6;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 49 · bounded orthogonal concave-prism side draft");
    const auto SourceDeliver = ConcavePrism();
    Panel.Expect("The orthogonal L-shaped concave prism is constructed", static_cast<bool>(SourceDeliver));
    const BrepBody Source = SourceDeliver.Payload;
    const BrepBody Snapshot = Source;
    const BodyReport Before = Source.Validate();
    const int Wall = FaceToward(Source, { 0, -1, 0 });
    const int Top = FaceToward(Source, Vec3::UnitZ());
    const int Bottom = FaceToward(Source, -Vec3::UnitZ());
    Panel.Expect("The source has closed genus-zero V12/E18/C36/L8/F8 topology", SourceDeliver && Before.Solid() &&
                 Before.Hulls == 1 && Before.Genus == 0 && Source.Vertices.size() == 12 && Source.Edges.size() == 18 &&
                 Source.Coedges.size() == 36 && Source.Loops.size() == 8 && Source.Faces.size() == 8);
    Panel.Expect("The selected wall is vertical and both caps resolve", Wall >= 0 && Top >= 0 && Bottom >= 0 && Wall != Top && Wall != Bottom);
    Panel.Expect("The source caps contain the single reflex L profile", IsConcaveCap(Source, Top, true) && IsConcaveCap(Source, Bottom, false));

    Panel.Section("Exact non-convex side draft reconstruction");
    const auto Result = SourceDeliver ? FaceEditSolver::DraftExtrudedConcavePrism(Source, Wall, Angle)
                                      : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no concave source");
    Panel.Expect("The selected vertical wall produces a closed drafted solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport R = Result.Payload.Validate();
        Panel.Expect("The drafted result retains V12/E18/C36/L8/F8 topology", R.Hulls == 1 && R.Genus == 0 &&
                     Result.Payload.Vertices.size() == 12 && Result.Payload.Edges.size() == 18 && Result.Payload.Coedges.size() == 36 &&
                     Result.Payload.Loops.size() == 8 && Result.Payload.Faces.size() == 8 && R.OpenEdges == 0 &&
                     R.NonManifoldEdges == 0 && R.MisorientedEdges == 0);
        Panel.Within("The drafted volume follows the mean-section L-profile identity", std::fabs(R.Volume - DraftedVolume), 1e-3);
        Panel.Expect("The drafted caps retain one reflex L profile", IsConcaveCap(Result.Payload, FaceToward(Result.Payload, Vec3::UnitZ()), true) &&
                     IsConcaveCap(Result.Payload, FaceToward(Result.Payload, -Vec3::UnitZ()), false));
        Panel.Expect("The selected upper edge moves outward by tan(angle) times height", HasVertex(Result.Payload, { -6, -4 - Delta, Height }) &&
                     HasVertex(Result.Payload, { 6, -4 - Delta, Height }));
        Panel.Expect("The opposite upper profile remains fixed", HasVertex(Result.Payload, { -1, -1, Height }) &&
                     HasVertex(Result.Payload, { -1, 4, Height }));
        Panel.Expect("The drafted result is two planar caps and six ruled walls", HasSupports(Result.Payload));
        Panel.Expect("The separate draft reconstruction preserves the source", SameSource(Source, Snapshot, Before));
    }
    const auto Dispatch = FaceEditSolver::Draft(Source, Wall, Angle);
    Panel.Expect("The public draft dispatcher reaches the concave route", Dispatch && Dispatch.Payload.Validate().Solid());
    const auto Inward = FaceEditSolver::DraftExtrudedConcavePrism(Source, Wall, -5.0 * ScalarCriteria::Pi / 180.0);
    Panel.Expect("A bounded inward draft remains supported", Inward && Inward.Payload.Validate().Solid());
    Panel.Expect("Only vertical side walls are supported", !FaceEditSolver::DraftExtrudedConcavePrism(Source, Top, Angle) &&
                 !FaceEditSolver::DraftExtrudedConcavePrism(Source, Bottom, Angle));

    Panel.Section("Concave-prism draft refusal boundaries");
    const auto Box = BrepBody::Box({ -6, -4, 0 }, { 6, 4, Height });
    Panel.Expect("A box remains outside the distinct concave-draft API", Box && !FaceEditSolver::DraftExtrudedConcavePrism(Box.Payload, FaceToward(Box.Payload, { 0, -1, 0 }), Angle));
    const auto ConvexHex = Prism({ { -6, -3, 0 }, { 0, -6, 0 }, { 6, -3, 0 }, { 6, 3, 0 }, { 0, 6, 0 }, { -6, 3, 0 } });
    Panel.Expect("A convex six-sided prism refuses", ConvexHex && !FaceEditSolver::DraftExtrudedConcavePrism(ConvexHex.Payload, FaceToward(ConvexHex.Payload, { 0, -1, 0 }), Angle));
    const auto Pentagon = Prism({ { -5, -3, 0 }, { 5, -3, 0 }, { 6, 2, 0 }, { 0, 5, 0 }, { -6, 2, 0 } });
    Panel.Expect("A five-sided prism refuses", Pentagon && !FaceEditSolver::DraftExtrudedConcavePrism(Pentagon.Payload, FaceToward(Pentagon.Payload, { 0, -1, 0 }), Angle));
    const auto Triangle = Prism({ { -5, -3, 0 }, { 5, -3, 0 }, { 0, 4, 0 } });
    Panel.Expect("A triangular prism remains on its separate route", Triangle && !FaceEditSolver::DraftExtrudedConcavePrism(Triangle.Payload, FaceToward(Triangle.Payload, { 0, -1, 0 }), Angle));
    const auto NonOrthogonal = Prism({ { -6, -4, 0 }, { 6, -4, 0 }, { 6, -1, 0 }, { -1, -1, 0 }, { -2, 4, 0 }, { -6, 4, 0 } });
    Panel.Expect("A non-orthogonal concave prism refuses", NonOrthogonal && !FaceEditSolver::DraftExtrudedConcavePrism(NonOrthogonal.Payload, FaceToward(NonOrthogonal.Payload, { 0, -1, 0 }), Angle));
    const auto Cylinder = BrepBody::Cylinder({ 0, 0, 0 }, Vec3::UnitZ(), 5.0, Height);
    Panel.Expect("A curved cylinder refuses", Cylinder && !FaceEditSolver::DraftExtrudedConcavePrism(Cylinder.Payload, FaceToward(Cylinder.Payload, { 0, -1, 0 }), Angle));
    Panel.Expect("Zero, near-90, and non-finite angles refuse", !FaceEditSolver::DraftExtrudedConcavePrism(Source, Wall, 0.0) &&
                 !FaceEditSolver::DraftExtrudedConcavePrism(Source, Wall, ScalarCriteria::HalfPi) &&
                 !FaceEditSolver::DraftExtrudedConcavePrism(Source, Wall, std::numeric_limits<double>::infinity()));
    BrepBody Malformed = Source;
    Malformed.Edges[0].Coedges.push_back(Malformed.Edges[0].Coedges.front());
    Panel.Expect("A malformed concave prism refuses transactionally", !FaceEditSolver::DraftExtrudedConcavePrism(Malformed, Wall, Angle));
    Panel.Expect("All refusal paths preserve the source", SameSource(Source, Snapshot, Before));

    Panel.Section("Distinct concave-draft proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase49_ConcavePrismDraft.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("SharpConcavePrism", Source.Transformed(Mat4::Translation({ -10, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("DraftedConcavePrism", Result.Payload.Transformed(Mat4::Translation({ 10, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 35 -18") && Host.Execute("view fit") &&
        Host.Execute("render Phase49_ConcavePrismDraft");
    Panel.Expect("The concave-prism draft proof render completes", Rendered);
    Panel.Expect("The concave-prism draft proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
