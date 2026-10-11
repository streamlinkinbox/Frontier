//=============================================================================================================================================
// SolidArc · Stage 4o · bounded eligible-edge dispatch for variable-radius G2 corner reconstruction
//=============================================================================================================================================
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>

using namespace Frontier;

namespace
{
[[nodiscard]] bool SameSource(const BrepBody& Body, const BodyReport& Before) noexcept
{
    const BodyReport After = Body.Validate();
    return After.Solid() == Before.Solid() && After.Hulls == Before.Hulls &&
           After.Genus == Before.Genus && std::fabs(After.Volume - Before.Volume) <= 1e-9 &&
           Body.Vertices.size() == 8 && Body.Edges.size() == 12 && Body.Coedges.size() == 24 &&
           Body.Loops.size() == 6 && Body.Faces.size() == 6;
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
    VerificationPanel Panel("SolidArc · bounded eligible variable G2 edge dispatch");
    const Deliver<BrepBody> SourceDeliver = BrepBody::Box({ 0, 0, 0 }, { 4, 4, 4 });
    Panel.Expect("The variable-dispatch source is a closed cube", SourceDeliver && SourceDeliver.Payload.Validate().Solid());
    const BrepBody Source = SourceDeliver.Payload;
    const BodyReport Before = Source.Validate();
    constexpr int SelectedEdge = 0;
    const QuadraticRadiusLaw RadiusLaw{ 0.25, 0.35, 0.50 };
    const double TransitionAngle = ScalarCriteria::Pi / 8.0;
    const Deliver<VariableG2RollingBallPlanarCornerSpecification> Candidate =
        BlendSolver::ClassifyVariableG2RollingBallEdge(Source, SelectedEdge, RadiusLaw, TransitionAngle);
    Panel.Expect("One explicit cube edge dispatches to variable G2", static_cast<bool>(Candidate));
    if (Candidate)
    {
        const VariableG2RollingBallPlanarCornerSpecification& Specification = Candidate.Payload;
        Panel.Within("The variable-dispatch edge length is extracted exactly", Specification.Length - 4.0, 1e-12);
        Panel.Within("The variable-dispatch support width is extracted exactly", Specification.Width - 4.0, 1e-12);
        Panel.Within("The start radius is retained", Specification.RadiusLaw.Start - RadiusLaw.Start, 1e-12);
        Panel.Within("The middle radius is retained", Specification.RadiusLaw.Middle - RadiusLaw.Middle, 1e-12);
        Panel.Within("The end radius is retained", Specification.RadiusLaw.End - RadiusLaw.End, 1e-12);
        Panel.Expect("The dispatched law is genuinely nonlinear and positive",
                     Specification.RadiusLaw.Positive() && Specification.RadiusLaw.Nonlinear());
        Panel.Expect("The dispatched support frame is orthogonal",
                     std::fabs(Specification.EdgeAxis.Dot(Specification.SupportA)) < 1e-12 &&
                     std::fabs(Specification.EdgeAxis.Dot(Specification.SupportB)) < 1e-12 &&
                     std::fabs(Specification.SupportA.Dot(Specification.SupportB)) < 1e-12);
        const Deliver<VariableG2RollingBallPlanarCornerSpecification> Repeat =
            BlendSolver::ClassifyVariableG2RollingBallEdge(Source, SelectedEdge, RadiusLaw, TransitionAngle);
        Panel.Expect("Variable dispatch orientation is deterministic", Repeat &&
                     Repeat.Payload.SupportA.Distance(Specification.SupportA) <= 1e-12 &&
                     Repeat.Payload.SupportB.Distance(Specification.SupportB) <= 1e-12);
        Panel.Expect("Variable dispatch leaves the source unchanged", SameSource(Source, Before));
    }

    Panel.Section("Separate variable G2 reconstruction after dispatch");
    const Deliver<BrepBody> Result = Candidate
        ? BlendSolver::ReconstructVariableG2RollingBallPlanarCorner(Candidate.Payload)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no eligible variable G2 edge");
    Panel.Expect("The dispatched variable specification reconstructs a G2 solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        Panel.Expect("The dispatched variable result retains V14/E21/F9/L9 topology",
                     Result.Payload.Vertices.size() == 14 && Result.Payload.Edges.size() == 21 &&
                     Result.Payload.Coedges.size() == 42 && Result.Payload.Loops.size() == 9 &&
                     Result.Payload.Faces.size() == 9 && Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        Panel.Expect("The dispatched variable result has outward normals", OutwardNormals(Result.Payload));
        Panel.Expect("Variable dispatch remains transactional", SameSource(Source, Before));
    }

    Panel.Section("Unsupported selections and variable-law refusals");
    Panel.Expect("A constant law remains outside variable dispatch",
                 !BlendSolver::ClassifyVariableG2RollingBallEdge(Source, SelectedEdge,
                                                                  { 0.25, 0.375, 0.50 }, TransitionAngle));
    Panel.Expect("A non-positive law refuses",
                 !BlendSolver::ClassifyVariableG2RollingBallEdge(Source, SelectedEdge,
                                                                  { 0.25, -0.1, 0.50 }, TransitionAngle));
    Panel.Expect("A law whose end station consumes the support refuses",
                 !BlendSolver::ClassifyVariableG2RollingBallEdge(Source, SelectedEdge,
                                                                  { 0.25, 0.35, 4.0 }, TransitionAngle));
    Panel.Expect("An invalid transition refuses",
                 !BlendSolver::ClassifyVariableG2RollingBallEdge(Source, SelectedEdge, RadiusLaw, 0.0));
    const Deliver<NurbsCurve> ObliqueProfile =
        NurbsCurve::Polyline({ { 0, 0, 0 }, { 0, 6, 0 }, { 0, 2.5, 4.3301270189 } }, true);
    const Deliver<BrepBody> ObliqueSource = ObliqueProfile
        ? BrepBody::Extrude(ObliqueProfile.Payload, { 1, 0, 0 }, 8.0)
        : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "oblique source is degenerate");
    Panel.Expect("An oblique edge remains in the dedicated oblique dispatch boundary",
                 ObliqueSource && !BlendSolver::ClassifyVariableG2RollingBallEdge(ObliqueSource.Payload, 3,
                                                                                    RadiusLaw, TransitionAngle));
    const Deliver<BrepBody> UnequalBox = BrepBody::Box({ 0, 0, 0 }, { 4, 5, 6 });
    bool UnequalRefused = static_cast<bool>(UnequalBox);
    if (UnequalBox)
        for (int Edge = 0; Edge < static_cast<int>(UnequalBox.Payload.Edges.size()); ++Edge)
            UnequalRefused = UnequalRefused &&
                !BlendSolver::ClassifyVariableG2RollingBallEdge(UnequalBox.Payload, Edge, RadiusLaw, TransitionAngle);
    Panel.Expect("Unequal rectangular support widths remain outside orthogonal variable dispatch", UnequalRefused);
    const Deliver<BrepBody> Cylinder = BrepBody::Cylinder({ 0, 0, 0 }, { 0, 0, 1 }, 2.0, 4.0);
    Panel.Expect("A curved cylinder edge remains explicitly unsupported",
                 Cylinder && !BlendSolver::ClassifyVariableG2RollingBallEdge(Cylinder.Payload, 0,
                                                                                RadiusLaw, TransitionAngle));
    BrepBody NonManifold = Source;
    NonManifold.Edges[SelectedEdge].Coedges.push_back(NonManifold.Edges[SelectedEdge].Coedges.front());
    Panel.Expect("A non-manifold selected edge remains explicitly unsupported",
                 !BlendSolver::ClassifyVariableG2RollingBallEdge(NonManifold, SelectedEdge,
                                                                  RadiusLaw, TransitionAngle));
    Panel.Expect("The source remains unchanged after all variable refusals", SameSource(Source, Before));

    Panel.Section("Distinct sharp/variable-dispatch proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase38o_EligibleVariableG2EdgeDispatch.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("EligibleVariableSource", Source.Transformed(Mat4::Translation({ -8, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("DispatchedVariableG2", Result.Payload.Transformed(Mat4::Translation({ 8, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 190 -10") && Host.Execute("view fit") &&
        Host.Execute("render Phase38o_EligibleVariableG2EdgeDispatch");
    Panel.Expect("The sharp/variable-dispatch proof render completes", Rendered);
    Panel.Expect("The sharp/variable-dispatch proof PNG is visible",
                 std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
