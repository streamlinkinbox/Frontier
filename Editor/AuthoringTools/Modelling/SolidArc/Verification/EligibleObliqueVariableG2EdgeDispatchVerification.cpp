//=============================================================================================================================================
// SolidArc · Stage 4p · bounded eligible-edge dispatch for oblique variable-radius G2 reconstruction
//=============================================================================================================================================
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <algorithm>
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
           Body.Vertices.size() == 6 && Body.Edges.size() == 9 && Body.Coedges.size() == 18 &&
           Body.Loops.size() == 5 && Body.Faces.size() == 5;
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
    VerificationPanel Panel("SolidArc · bounded eligible oblique variable G2 edge dispatch");
    const Deliver<NurbsCurve> Profile = NurbsCurve::Polyline({ { 0, 0, 0 }, { 0, 6, 0 }, { 0, 2.5, 4.3301270189 } }, true);
    const Deliver<BrepBody> SourceDeliver = Profile
        ? BrepBody::Extrude(Profile.Payload, { 1, 0, 0 }, 8.0)
        : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "oblique triangular profile is degenerate");
    Panel.Expect("The oblique variable source is a closed solid", SourceDeliver && SourceDeliver.Payload.Validate().Solid());
    const BrepBody Source = SourceDeliver.Payload;
    const BodyReport Before = Source.Validate();
    constexpr int SelectedEdge = 3; // the prism's 8-unit edge at the 60-degree support corner
    const QuadraticRadiusLaw RadiusLaw{ 0.25, 0.35, 0.50 };
    const double TransitionAngle = ScalarCriteria::Pi / 12.0;
    const Deliver<ObliqueVariableG2RollingBallPlanarCornerSpecification> Candidate =
        BlendSolver::ClassifyObliqueVariableG2RollingBallEdge(Source, SelectedEdge, RadiusLaw, TransitionAngle);
    Panel.Expect("One explicit oblique edge dispatches to variable G2", static_cast<bool>(Candidate));
    if (Candidate)
    {
        const ObliqueVariableG2RollingBallPlanarCornerSpecification& Specification = Candidate.Payload;
        const double Angle = std::acos(ScalarCriteria::Clamp(Specification.SupportA.Dot(Specification.SupportB), -1.0, 1.0));
        Panel.Within("The oblique variable edge length is extracted exactly", Specification.Length - 8.0, 1e-12);
        Panel.Within("The first oblique support width is extracted exactly", std::min(Specification.WidthA, Specification.WidthB) - 5.0, 1e-10);
        Panel.Within("The second oblique support width is extracted exactly", std::max(Specification.WidthA, Specification.WidthB) - 6.0, 1e-10);
        Panel.Within("The oblique support angle is exactly 60 degrees", Angle - ScalarCriteria::Pi / 3.0, 1e-9);
        Panel.Within("The start radius is retained", Specification.RadiusLaw.Start - RadiusLaw.Start, 1e-12);
        Panel.Within("The middle radius is retained", Specification.RadiusLaw.Middle - RadiusLaw.Middle, 1e-12);
        Panel.Within("The end radius is retained", Specification.RadiusLaw.End - RadiusLaw.End, 1e-12);
        Panel.Expect("The oblique variable law is nonlinear and positive",
                     Specification.RadiusLaw.Positive() && Specification.RadiusLaw.Nonlinear());
        Panel.Expect("The dispatched oblique frame is orthogonal to the edge",
                     std::fabs(Specification.EdgeAxis.Dot(Specification.SupportA)) < 1e-12 &&
                     std::fabs(Specification.EdgeAxis.Dot(Specification.SupportB)) < 1e-12);
        const Deliver<ObliqueVariableG2RollingBallPlanarCornerSpecification> Repeat =
            BlendSolver::ClassifyObliqueVariableG2RollingBallEdge(Source, SelectedEdge, RadiusLaw, TransitionAngle);
        Panel.Expect("Oblique variable orientation is deterministic", Repeat &&
                     Repeat.Payload.SupportA.Distance(Specification.SupportA) <= 1e-12 &&
                     Repeat.Payload.SupportB.Distance(Specification.SupportB) <= 1e-12 &&
                     std::fabs(Repeat.Payload.WidthA - Specification.WidthA) <= 1e-12 &&
                     std::fabs(Repeat.Payload.WidthB - Specification.WidthB) <= 1e-12);
        Panel.Expect("Oblique variable dispatch leaves the source unchanged", SameSource(Source, Before));
    }

    Panel.Section("Separate oblique variable G2 reconstruction after dispatch");
    const Deliver<BrepBody> Result = Candidate
        ? BlendSolver::ReconstructObliqueVariableG2RollingBallPlanarCorner(Candidate.Payload)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no eligible oblique variable G2 edge");
    Panel.Expect("The dispatched oblique variable specification reconstructs a G2 solid",
                 Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        Panel.Expect("The dispatched oblique variable result retains V12/E18/F8/L8 topology",
                     Result.Payload.Vertices.size() == 12 && Result.Payload.Edges.size() == 18 &&
                     Result.Payload.Coedges.size() == 36 && Result.Payload.Loops.size() == 8 &&
                     Result.Payload.Faces.size() == 8 && Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        Panel.Expect("The dispatched oblique variable result has outward normals", OutwardNormals(Result.Payload));
        Panel.Expect("Oblique variable dispatch remains transactional", SameSource(Source, Before));
    }

    Panel.Section("Unsupported selections and oblique variable-law refusals");
    Panel.Expect("A constant law remains outside oblique variable dispatch",
                 !BlendSolver::ClassifyObliqueVariableG2RollingBallEdge(Source, SelectedEdge,
                                                                         { 0.25, 0.375, 0.50 }, TransitionAngle));
    Panel.Expect("A non-positive law refuses",
                 !BlendSolver::ClassifyObliqueVariableG2RollingBallEdge(Source, SelectedEdge,
                                                                         { 0.25, -0.1, 0.50 }, TransitionAngle));
    Panel.Expect("A law whose end station consumes the supports refuses",
                 !BlendSolver::ClassifyObliqueVariableG2RollingBallEdge(Source, SelectedEdge,
                                                                         { 0.25, 0.35, 4.0 }, TransitionAngle));
    Panel.Expect("An invalid transition refuses",
                 !BlendSolver::ClassifyObliqueVariableG2RollingBallEdge(Source, SelectedEdge, RadiusLaw, 0.0));
    const Deliver<BrepBody> Orthogonal = BrepBody::Box({ 0, 0, 0 }, { 4, 4, 4 });
    Panel.Expect("An orthogonal edge remains in the dedicated orthogonal variable dispatch",
                 Orthogonal && !BlendSolver::ClassifyObliqueVariableG2RollingBallEdge(Orthogonal.Payload, 0,
                                                                                         RadiusLaw, TransitionAngle));
    Panel.Expect("An out-of-range edge refuses",
                 !BlendSolver::ClassifyObliqueVariableG2RollingBallEdge(Source, 999, RadiusLaw, TransitionAngle));
    const Deliver<BrepBody> Cylinder = BrepBody::Cylinder({ 0, 0, 0 }, { 0, 0, 1 }, 2.0, 4.0);
    Panel.Expect("A curved cylinder edge remains explicitly unsupported",
                 Cylinder && !BlendSolver::ClassifyObliqueVariableG2RollingBallEdge(Cylinder.Payload, 0,
                                                                                       RadiusLaw, TransitionAngle));
    BrepBody NonManifold = Source;
    NonManifold.Edges[SelectedEdge].Coedges.push_back(NonManifold.Edges[SelectedEdge].Coedges.front());
    Panel.Expect("A non-manifold selected edge remains explicitly unsupported",
                 !BlendSolver::ClassifyObliqueVariableG2RollingBallEdge(NonManifold, SelectedEdge,
                                                                         RadiusLaw, TransitionAngle));
    Panel.Expect("The source remains unchanged after all oblique variable refusals", SameSource(Source, Before));

    Panel.Section("Distinct sharp/oblique-variable dispatch proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase38p_EligibleObliqueVariableG2EdgeDispatch.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("EligibleObliqueVariableSource", Source.Transformed(Mat4::Translation({ -9, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("DispatchedObliqueVariableG2", Result.Payload.Transformed(Mat4::Translation({ 9, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 195 -12") && Host.Execute("view fit") &&
        Host.Execute("render Phase38p_EligibleObliqueVariableG2EdgeDispatch");
    Panel.Expect("The sharp/oblique-variable proof render completes", Rendered);
    Panel.Expect("The sharp/oblique-variable proof PNG is visible",
                 std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
