//=============================================================================================================================================
// SolidArc · Stage 4x · bounded nonlinear variable-radius corner edge dispatch
//=============================================================================================================================================
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <limits>

using namespace Frontier;

namespace
{
[[nodiscard]] bool ExteriorFaceNormals(const BrepBody& Body) noexcept
{
    const Box3 Bounds = Body.Bounds();
    const Vec3 Centre = (Bounds.Low + Bounds.High) * 0.5;
    for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
    {
        const BrepBody::FaceTriangles Triangles = Body.TessellateFace(static_cast<int>(Face));
        if (Triangles.Positions.empty() || Triangles.Normals.size() != Triangles.Positions.size()) return false;
        Vec3 Position{ 0, 0, 0 }, Normal{ 0, 0, 0 };
        for (size_t I = 0; I < Triangles.Positions.size(); ++I)
        {
            Position += Triangles.Positions[I];
            Normal += Triangles.Normals[I];
        }
        if (Normal.Normalised().Dot(Position / static_cast<double>(Triangles.Positions.size()) - Centre) <=
            ScalarCriteria::MergeTolerance)
            return false;
    }
    return true;
}

[[nodiscard]] bool SameSource(const BrepBody& Body, const BodyReport& Before) noexcept
{
    const BodyReport After = Body.Validate();
    return After.Vertices == Before.Vertices && After.Edges == Before.Edges &&
           After.Loops == Before.Loops && After.Faces == Before.Faces &&
           After.OpenEdges == Before.OpenEdges && After.NonManifoldEdges == Before.NonManifoldEdges &&
           After.MisorientedEdges == Before.MisorientedEdges && std::fabs(After.Volume - Before.Volume) <= 1e-12 &&
           Body.Vertices.size() == 8 && Body.Edges.size() == 12 && Body.Coedges.size() == 24 &&
           Body.Loops.size() == 6 && Body.Faces.size() == 6;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · bounded nonlinear variable-radius corner edge dispatch");
    const Vec3 Origin{ 0, 0, 0 };
    const double Length = 8.0;
    const double Width = 5.0;
    const double Setback = 1.2;
    const QuadraticRadiusLaw Law{ 1.0, 1.8, 1.0 };
    constexpr int SelectedEdge = 0;
    const Deliver<BrepBody> SourceDeliver = BrepBody::Box(Origin, { Length, Width, Width });
    Panel.Expect("The canonical rectangular source is a closed solid", SourceDeliver && SourceDeliver.Payload.Validate().Solid());
    if (SourceDeliver)
        Panel.Expect("The source has canonical V8/E12/C24/L6/F6 topology", SourceDeliver.Payload.Vertices.size() == 8 &&
                     SourceDeliver.Payload.Edges.size() == 12 && SourceDeliver.Payload.Coedges.size() == 24 &&
                     SourceDeliver.Payload.Loops.size() == 6 && SourceDeliver.Payload.Faces.size() == 6);
    const BrepBody Source = SourceDeliver.Payload;
    const BodyReport Before = Source.Validate();
    const Deliver<NonlinearVariableRadiusCornerSpecification> Candidate =
        BlendSolver::ClassifyNonlinearVariableRadiusCornerEdge(Source, SelectedEdge, Setback, Law);
    Panel.Expect("One explicit edge dispatches the nonlinear variable corner", static_cast<bool>(Candidate));
    if (Candidate)
    {
        const NonlinearVariableRadiusCornerSpecification& Specification = Candidate.Payload;
        Panel.Within("The nonlinear edge origin is extracted exactly", Specification.Origin.Distance(Origin), 1e-12);
        Panel.Within("The nonlinear edge axis is extracted exactly", Specification.EdgeAxis.Distance(Vec3{ 1, 0, 0 }), 1e-12);
        Panel.Within("The nonlinear edge length is extracted exactly", Specification.Length - Length, 1e-12);
        Panel.Within("The nonlinear setback is retained exactly", Specification.Setback - Setback, 1e-12);
        Panel.Within("The nonlinear low station is retained exactly", Specification.RadiusLaw.Start - Law.Start, 1e-12);
        Panel.Within("The nonlinear middle station is retained exactly", Specification.RadiusLaw.Middle - Law.Middle, 1e-12);
        Panel.Within("The nonlinear high station is retained exactly", Specification.RadiusLaw.End - Law.End, 1e-12);
        Panel.Expect("The dispatched law is genuinely nonlinear", Specification.RadiusLaw.Nonlinear());
        const Deliver<NonlinearVariableRadiusCornerSpecification> Repeat =
            BlendSolver::ClassifyNonlinearVariableRadiusCornerEdge(Source, SelectedEdge, Setback, Law);
        Panel.Expect("Nonlinear dispatch is deterministic", Repeat &&
                     Repeat.Payload.Origin.Distance(Specification.Origin) <= 1e-12 &&
                     Repeat.Payload.EdgeAxis.Distance(Specification.EdgeAxis) <= 1e-12 &&
                     std::fabs(Repeat.Payload.RadiusLaw.Middle - Specification.RadiusLaw.Middle) <= 1e-12);
        Panel.Expect("Nonlinear dispatch leaves the source unchanged", SameSource(Source, Before));
    }

    Panel.Section("Separate nonlinear variable-radius reconstruction after edge dispatch");
    const Deliver<BrepBody> Result = Candidate
        ? BlendSolver::ReconstructNonlinearVariableRadiusCornerBlend(Candidate.Payload)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no eligible nonlinear variable edge");
    Panel.Expect("The dispatched nonlinear specification reconstructs a solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        Panel.Expect("The result retains V10/E15/C30/L7/F7 topology",
                     Result.Payload.Vertices.size() == 10 && Result.Payload.Edges.size() == 15 &&
                     Result.Payload.Coedges.size() == 30 && Result.Payload.Loops.size() == 7 &&
                     Result.Payload.Faces.size() == 7 && Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        const QuadraticRadiusLaw OuterLaw{ Law.Start + Setback, Law.Middle + Setback, Law.End + Setback };
        const double ExpectedVolume = OuterLaw.IntegratedSquare(Length) -
            (1.0 - ScalarCriteria::Pi / 4.0) * Law.IntegratedSquare(Length);
        Panel.Expect("The nonlinear volume follows the integrated quadratic identity",
                     ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume));
        Panel.Expect("The nonlinear result has outward normals", ExteriorFaceNormals(Result.Payload));
        Panel.Expect("Nonlinear dispatch remains transactional", SameSource(Source, Before));
    }

    Panel.Section("Unsupported edge, law, and source refusals");
    Panel.Expect("An unequal-width box edge refuses", [&]()
    {
        const Deliver<BrepBody> Unequal = BrepBody::Box(Origin, { Length, 6.0, Width });
        return Unequal && !BlendSolver::ClassifyNonlinearVariableRadiusCornerEdge(Unequal.Payload, SelectedEdge, Setback, Law);
    }());
    Panel.Expect("A non-corner box edge with unequal supports refuses",
                 !BlendSolver::ClassifyNonlinearVariableRadiusCornerEdge(Source, 1, Setback, Law));
    Panel.Expect("An out-of-range edge refuses",
                 !BlendSolver::ClassifyNonlinearVariableRadiusCornerEdge(Source, 99, Setback, Law));
    Panel.Expect("Linear and unequal-endpoint laws remain outside nonlinear dispatch",
                 !BlendSolver::ClassifyNonlinearVariableRadiusCornerEdge(Source, SelectedEdge, Setback, { 1.0, 1.2, 1.4 }) &&
                 !BlendSolver::ClassifyNonlinearVariableRadiusCornerEdge(Source, SelectedEdge, Setback, { 1.0, 1.8, 1.4 }));
    Panel.Expect("Zero, negative, and non-finite laws refuse",
                 !BlendSolver::ClassifyNonlinearVariableRadiusCornerEdge(Source, SelectedEdge, Setback, { 0.0, 1.0, 1.4 }) &&
                 !BlendSolver::ClassifyNonlinearVariableRadiusCornerEdge(Source, SelectedEdge, Setback, { -1.0, 1.0, 1.4 }) &&
                 !BlendSolver::ClassifyNonlinearVariableRadiusCornerEdge(Source, SelectedEdge, Setback,
                     { std::numeric_limits<double>::quiet_NaN(), 1.0, 1.4 }));
    Panel.Expect("A consuming radius-plus-setback request refuses",
                 !BlendSolver::ClassifyNonlinearVariableRadiusCornerEdge(Source, SelectedEdge, 4.0, Law));
    Panel.Expect("A non-finite setback refuses",
                 !BlendSolver::ClassifyNonlinearVariableRadiusCornerEdge(Source, SelectedEdge,
                     std::numeric_limits<double>::infinity(), Law));
    const Deliver<BrepBody> Cylinder = BrepBody::Cylinder(Origin, { 0, 0, 1 }, Width, Length);
    Panel.Expect("A cylinder remains outside nonlinear variable edge dispatch",
                 Cylinder && !BlendSolver::ClassifyNonlinearVariableRadiusCornerEdge(Cylinder.Payload, 0, Setback, Law));
    BrepBody NonManifold = Source;
    NonManifold.Edges[SelectedEdge].Coedges.push_back(NonManifold.Edges[SelectedEdge].Coedges.front());
    Panel.Expect("A malformed box refuses transactionally",
                 !BlendSolver::ClassifyNonlinearVariableRadiusCornerEdge(NonManifold, SelectedEdge, Setback, Law));
    Panel.Expect("The source remains unchanged after all nonlinear refusals", SameSource(Source, Before));

    Panel.Section("Distinct sharp/nonlinear-radius proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase38x_NonlinearVariableRadiusCornerEdgeDispatch.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("SelectedSharpNonlinearCorner", Source.Transformed(Mat4::Translation({ -12, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("DispatchedNonlinearCorner", Result.Payload.Transformed(Mat4::Translation({ 4, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 190 -10") && Host.Execute("view fit") &&
        Host.Execute("render Phase38x_NonlinearVariableRadiusCornerEdgeDispatch");
    Panel.Expect("The sharp/nonlinear-radius proof render completes", Rendered);
    Panel.Expect("The nonlinear-radius dispatch proof PNG is visible",
                 std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
