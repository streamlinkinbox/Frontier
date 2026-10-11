//=============================================================================================================================================
// SolidArc · Phase 45 · bounded triangular-prism side draft
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
constexpr double Height = 6.0;
constexpr double Angle = ScalarCriteria::Pi * 12.0 / 180.0;
const std::vector<Vec3> TriangleProfile{ { -4, -2, 0 }, { 5, -2, 0 }, { 0, 4, 0 } };

[[nodiscard]] Deliver<BrepBody> TrianglePrism() noexcept
{
    const Deliver<NurbsCurve> Curve = NurbsCurve::Polyline(TriangleProfile, true);
    return Curve ? BrepBody::Extrude(Curve.Payload, Vec3::UnitZ(), Height)
                 : Deliver<BrepBody>::Reject(Curve.Denial.Reason, Curve.Denial.Detail);
}

[[nodiscard]] Deliver<BrepBody> PentagonPrism() noexcept
{
    const std::vector<Vec3> Points{ { 4.5, 0, 0 }, { 1.391, 4.28, 0 }, { -3.641, 2.645, 0 }, { -3.641, -2.645, 0 }, { 1.391, -4.28, 0 } };
    const Deliver<NurbsCurve> Curve = NurbsCurve::Polyline(Points, true);
    return Curve ? BrepBody::Extrude(Curve.Payload, Vec3::UnitZ(), Height)
                 : Deliver<BrepBody>::Reject(Curve.Denial.Reason, Curve.Denial.Detail);
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
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 45 · bounded triangular-prism side draft");
    const auto SourceDeliver = TrianglePrism();
    Panel.Expect("The triangular prism source is constructed", static_cast<bool>(SourceDeliver));
    const BrepBody Source = SourceDeliver.Payload;
    const BrepBody Snapshot = Source;
    const BodyReport Before = Source.Validate();
    const int Side = FaceToward(Source, { 0, -1, 0 });
    const int Top = FaceToward(Source, Vec3::UnitZ());
    Panel.Expect("The source has V6/E9/C18/L5/F5 topology", SourceDeliver && Before.Solid() && Before.Hulls == 1 &&
                 Before.Genus == 0 && Source.Vertices.size() == 6 && Source.Edges.size() == 9 &&
                 Source.Coedges.size() == 18 && Source.Loops.size() == 5 && Source.Faces.size() == 5);
    Panel.Expect("The selected wall and upper cap resolve independently", Side >= 0 && Top >= 0 && Side != Top &&
                 Source.Faces[Side].Surface.Classification == SurfaceClassification::Extrusion &&
                 Source.Faces[Side].Loops.size() == 1 && Source.Loops[Source.Faces[Side].Loops.front()].Coedges.size() == 4);

    Panel.Section("Exact non-box draft reconstruction");
    const auto Result = SourceDeliver ? FaceEditSolver::DraftExtrudedTriangularPrism(Source, Side, Angle)
                                      : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no triangular prism source");
    Panel.Expect("The selected wall produces a closed drafted solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport R = Result.Payload.Validate();
        const Vec3 Normal = Source.FaceNormal(Side, 0.5 * (Source.Faces[Side].Surface.DomainStartU() + Source.Faces[Side].Surface.DomainEndU()),
                                              0.5 * (Source.Faces[Side].Surface.DomainStartV() + Source.Faces[Side].Surface.DomainEndV())).Normalised();
        const Vec3 Outward{ Normal.X, Normal.Y, 0.0 };
        const double Delta = std::tan(Angle) * Height;
        const double SourceArea = 0.5 * 9.0 * 6.0;
        const double DraftedArea = 0.5 * 9.0 * (6.0 + Delta);
        Panel.Expect("The draft retains V6/E9/C18/L5/F5 topology", R.Hulls == 1 && R.Genus == 0 &&
                     Result.Payload.Vertices.size() == 6 && Result.Payload.Edges.size() == 9 &&
                     Result.Payload.Coedges.size() == 18 && Result.Payload.Loops.size() == 5 &&
                     Result.Payload.Faces.size() == 5 && R.OpenEdges == 0 && R.NonManifoldEdges == 0 &&
                     R.MisorientedEdges == 0);
        Panel.Within("The drafted solid volume follows the mean-section triangle identity", std::fabs(R.Volume - Height * (SourceArea + DraftedArea) / 2.0), 1e-3);
        Panel.Expect("The lower profile remains unchanged", HasVertex(Result.Payload, TriangleProfile[0]) && HasVertex(Result.Payload, TriangleProfile[1]) && HasVertex(Result.Payload, TriangleProfile[2]));
        Panel.Expect("The selected upper edge moves by tan(angle) times height", HasVertex(Result.Payload, TriangleProfile[0] + Vec3::UnitZ() * Height + Outward * Delta) &&
                     HasVertex(Result.Payload, TriangleProfile[1] + Vec3::UnitZ() * Height + Outward * Delta));
        Panel.Expect("The opposite upper vertex remains fixed", HasVertex(Result.Payload, TriangleProfile[2] + Vec3::UnitZ() * Height));
        int Planes = 0, Ruled = 0;
        for (const BrepFace& F : Result.Payload.Faces)
        {
            if (F.Surface.Classification == SurfaceClassification::Plane) ++Planes;
            if (F.Surface.Classification == SurfaceClassification::Ruled) ++Ruled;
        }
        Panel.Expect("The result has two planar caps and three ruled drafted walls", Planes == 2 && Ruled == 3);
        Panel.Expect("The separate draft preserves the source", SameSource(Source, Snapshot, Before));
    }
    const auto Dispatch = FaceEditSolver::Draft(Source, Side, Angle);
    Panel.Expect("The public draft dispatcher reaches the triangular route", Dispatch && Dispatch.Payload.Validate().Solid());
    const auto Inward = FaceEditSolver::DraftExtrudedTriangularPrism(Source, Side, -0.12);
    Panel.Expect("A finite inward draft remains supported while non-collapsing", Inward && Inward.Payload.Validate().Solid());

    Panel.Section("Triangular-prism draft refusal boundaries");
    const auto Box = BrepBody::Box({ -4, -3, 0 }, { 4, 3, Height });
    Panel.Expect("A rectangular box remains on its separate canonical draft route", Box && !FaceEditSolver::DraftExtrudedTriangularPrism(Box.Payload, FaceToward(Box.Payload, Vec3::UnitY()), Angle));
    const auto Pentagon = PentagonPrism();
    Panel.Expect("A pentagonal prism refuses the triangular draft route", Pentagon && !FaceEditSolver::DraftExtrudedTriangularPrism(Pentagon.Payload, FaceToward(Pentagon.Payload, Vec3::UnitY()), Angle));
    const auto Cylinder = BrepBody::Cylinder({ 0, 0, 0 }, Vec3::UnitZ(), 3.0, Height);
    Panel.Expect("A curved cylinder refuses the triangular draft route", Cylinder && !FaceEditSolver::DraftExtrudedTriangularPrism(Cylinder.Payload, FaceToward(Cylinder.Payload, Vec3::UnitY()), Angle));
    Panel.Expect("A cap selection refuses", !FaceEditSolver::DraftExtrudedTriangularPrism(Source, Top, Angle));
    Panel.Expect("Zero, near-90, and non-finite angles refuse", !FaceEditSolver::DraftExtrudedTriangularPrism(Source, Side, 0.0) &&
                 !FaceEditSolver::DraftExtrudedTriangularPrism(Source, Side, ScalarCriteria::HalfPi) &&
                 !FaceEditSolver::DraftExtrudedTriangularPrism(Source, Side, std::numeric_limits<double>::infinity()) &&
                 !FaceEditSolver::DraftExtrudedTriangularPrism(Source, Side, -0.8));
    BrepBody Malformed = Source;
    Malformed.Edges[0].Coedges.push_back(Malformed.Edges[0].Coedges.front());
    Panel.Expect("A malformed triangular prism refuses transactionally", !FaceEditSolver::DraftExtrudedTriangularPrism(Malformed, Side, Angle));
    Panel.Expect("All refusal paths preserve the source", SameSource(Source, Snapshot, Before));

    Panel.Section("Distinct non-box draft proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase45_TriangularPrismDraft.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("SharpTrianglePrism", Source.Transformed(Mat4::Translation({ -8, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("DraftedTrianglePrism", Result.Payload.Transformed(Mat4::Translation({ 8, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 35 -18") && Host.Execute("view fit") &&
        Host.Execute("render Phase45_TriangularPrismDraft");
    Panel.Expect("The triangular-prism draft proof render completes", Rendered);
    Panel.Expect("The triangular-prism draft proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
