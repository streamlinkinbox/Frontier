//=============================================================================================================================================
// SolidArc · Stage 4m · bounded eligible-edge dispatch for G2 corner reconstruction
//=============================================================================================================================================
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <string>

using namespace Frontier;

namespace
{
[[nodiscard]] bool SameSource(const BrepBody& Body, const BodyReport& Before) noexcept
{
    const BodyReport After = Body.Validate();
    return After.Solid() == Before.Solid() && After.Hulls == Before.Hulls &&
           After.Genus == Before.Genus && std::fabs(After.Volume - Before.Volume) <= 1e-9 &&
           Body.Vertices.size() == 8 && Body.Edges.size() == 12 && Body.Faces.size() == 6;
}

[[nodiscard]] bool OutwardNormals(const BrepBody& Body) noexcept
{
    const Box3 Bounds = Body.Bounds();
    const Vec3 Centre = (Bounds.Low + Bounds.High) * 0.5;
    for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
    {
        const BrepBody::FaceTriangles T = Body.TessellateFace(static_cast<int>(Face));
        if (T.Positions.empty() || T.Normals.size() != T.Positions.size()) return false;
        Vec3 Position{ 0, 0, 0 }, Normal{ 0, 0, 0 };
        for (size_t I = 0; I < T.Positions.size(); ++I)
        {
            Position += T.Positions[I];
            Normal += T.Normals[I];
        }
        Position = Position / static_cast<double>(T.Positions.size());
        if (Normal.Normalised().Dot(Position - Centre) <= ScalarCriteria::MergeTolerance) return false;
    }
    return true;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · bounded eligible G2 edge dispatch");
    const Deliver<BrepBody> SourceDeliver = BrepBody::Box({ 0, 0, 0 }, { 4, 4, 4 });
    Panel.Expect("The canonical dispatch source is a closed cube", SourceDeliver && SourceDeliver.Payload.Validate().Solid());
    const BrepBody Source = SourceDeliver.Payload;
    const BodyReport Before = Source.Validate();
    int EligibleEdge = -1;
    G2RollingBallPlanarCornerSpecification Specification;
    for (int Edge = 0; Edge < static_cast<int>(Source.Edges.size()); ++Edge)
    {
        const Deliver<G2RollingBallPlanarCornerSpecification> Candidate =
            BlendSolver::ClassifyG2RollingBallEdge(Source, Edge, 0.5, ScalarCriteria::Pi / 8.0);
        if (Candidate) { EligibleEdge = Edge; Specification = Candidate.Payload; break; }
    }
    Panel.Expect("One explicit cube edge dispatches to a bounded G2 specification", EligibleEdge >= 0);
    if (EligibleEdge >= 0)
    {
        Panel.Within("The selected edge length is extracted exactly", Specification.Length - 4.0, 1e-12);
        Panel.Within("The selected rectangular support width is extracted exactly", Specification.Width - 4.0, 1e-12);
        Panel.Within("The selected radius is retained", Specification.Radius - 0.5, 1e-12);
        Panel.Expect("The dispatched support frame is perpendicular", std::fabs(Specification.EdgeAxis.Dot(Specification.SupportA)) < 1e-12 &&
                     std::fabs(Specification.EdgeAxis.Dot(Specification.SupportB)) < 1e-12 &&
                     std::fabs(Specification.SupportA.Dot(Specification.SupportB)) < 1e-12);
        Panel.Expect("Dispatch leaves the source unchanged", SameSource(Source, Before));
    }

    Panel.Section("Explicit reconstruction after bounded dispatch");
    const Deliver<BrepBody> Result = EligibleEdge >= 0
        ? BlendSolver::ReconstructG2RollingBallPlanarCorner(Specification)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no eligible edge");
    Panel.Expect("The dispatched specification reconstructs a G2 solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        Panel.Expect("The dispatched result retains V14/E21/F9/L9 topology",
                     Result.Payload.Vertices.size() == 14 && Result.Payload.Edges.size() == 21 &&
                     Result.Payload.Coedges.size() == 42 && Result.Payload.Faces.size() == 9 &&
                     Result.Payload.Loops.size() == 9 && Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        Panel.Expect("The dispatched result has outward normals", OutwardNormals(Result.Payload));
        Panel.Expect("Dispatch remains transactional", SameSource(Source, Before));
    }

    Panel.Section("Unsupported selection and feasibility refusals");
    Panel.Expect("An out-of-range edge refuses", !BlendSolver::ClassifyG2RollingBallEdge(Source, 999, 0.5, ScalarCriteria::Pi / 8.0));
    Panel.Expect("A radius consuming the selected supports refuses",
                 EligibleEdge < 0 || !BlendSolver::ClassifyG2RollingBallEdge(Source, EligibleEdge, 4.0, ScalarCriteria::Pi / 8.0));
    Panel.Expect("A zero transition refuses",
                 EligibleEdge < 0 || !BlendSolver::ClassifyG2RollingBallEdge(Source, EligibleEdge, 0.5, 0.0));
    const Deliver<BrepBody> Cylinder = BrepBody::Cylinder({ 0, 0, 0 }, { 0, 0, 1 }, 2.0, 4.0);
    Panel.Expect("A curved cylinder edge remains explicitly unsupported",
                 Cylinder && !BlendSolver::ClassifyG2RollingBallEdge(Cylinder.Payload, 0, 0.5, ScalarCriteria::Pi / 8.0));
    const Deliver<BrepBody> UnequalBox = BrepBody::Box({ 0, 0, 0 }, { 4, 5, 6 });
    bool UnequalRefused = false;
    if (UnequalBox)
        for (int Edge = 0; Edge < static_cast<int>(UnequalBox.Payload.Edges.size()); ++Edge)
            if (!BlendSolver::ClassifyG2RollingBallEdge(UnequalBox.Payload, Edge, 0.5, ScalarCriteria::Pi / 8.0))
            { UnequalRefused = true; break; }
    Panel.Expect("Unequal rectangular support widths remain outside dispatch", UnequalRefused);
    Mat4 ObliqueTransform;
    ObliqueTransform.Place(0, 0, 1.0); ObliqueTransform.Place(1, 0, 0.0); ObliqueTransform.Place(2, 0, 0.0);
    ObliqueTransform.Place(0, 1, 0.0); ObliqueTransform.Place(1, 1, 1.0); ObliqueTransform.Place(2, 1, 0.0);
    ObliqueTransform.Place(0, 2, 0.0); ObliqueTransform.Place(1, 2, 0.5); ObliqueTransform.Place(2, 2, std::sqrt(3.0) * 0.5);
    const BrepBody ObliqueBox = Source.Transformed(ObliqueTransform);
    bool ObliqueRefused = ObliqueBox.Validate().Solid();
    for (int Edge = 0; Edge < static_cast<int>(ObliqueBox.Edges.size()); ++Edge)
        ObliqueRefused = ObliqueRefused && !BlendSolver::ClassifyG2RollingBallEdge(ObliqueBox, Edge, 0.5, ScalarCriteria::Pi / 8.0);
    Panel.Expect("Oblique parallelogram supports remain outside rectangular orthogonal dispatch", ObliqueRefused);
    BrepBody NonManifold = Source;
    NonManifold.Edges[0].Coedges.push_back(NonManifold.Edges[0].Coedges.front());
    Panel.Expect("A non-manifold selected edge remains explicitly unsupported",
                 !BlendSolver::ClassifyG2RollingBallEdge(NonManifold, 0, 0.5, ScalarCriteria::Pi / 8.0));
    Panel.Expect("The source remains unchanged after all refusals", SameSource(Source, Before));

    Panel.Section("Distinct source/dispatch exterior proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase38m_EligibleG2EdgeDispatch.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("SelectedCubeSource", Source.Transformed(Mat4::Translation({ -7, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("DispatchedG2Result", Result.Payload.Transformed(Mat4::Translation({ 7, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 190 -10") && Host.Execute("view fit") &&
        Host.Execute("render Phase38m_EligibleG2EdgeDispatch");
    Panel.Expect("The source/dispatch proof render completes", Rendered);
    Panel.Expect("The source/dispatch proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
