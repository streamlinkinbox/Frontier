//=============================================================================================================================================
// SolidArc · Stage 4y · bounded nonlinear support-setback corner edge dispatch
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
    VerificationPanel Panel("SolidArc · bounded nonlinear support-setback corner edge dispatch");
    const Vec3 Origin{ 0, 0, 0 };
    const double Length = 8.0;
    const double Width = 5.0;
    const QuadraticRadiusLaw RadiusLaw{ 0.9, 0.9, 0.9 };
    const QuadraticRadiusLaw SetbackLaw{ 0.8, 1.6, 0.8 };
    constexpr int SelectedEdge = 0;
    const Deliver<BrepBody> SourceDeliver = BrepBody::Box(Origin, { Length, Width, Width });
    Panel.Expect("The canonical rectangular source is a closed solid", SourceDeliver && SourceDeliver.Payload.Validate().Solid());
    if (SourceDeliver)
        Panel.Expect("The source has canonical V8/E12/C24/L6/F6 topology", SourceDeliver.Payload.Vertices.size() == 8 &&
                     SourceDeliver.Payload.Edges.size() == 12 && SourceDeliver.Payload.Coedges.size() == 24 &&
                     SourceDeliver.Payload.Loops.size() == 6 && SourceDeliver.Payload.Faces.size() == 6);
    const BrepBody Source = SourceDeliver.Payload;
    const BodyReport Before = Source.Validate();
    const Deliver<NonlinearVariableSetbackCornerSpecification> Candidate =
        BlendSolver::ClassifyNonlinearVariableSetbackCornerEdge(Source, SelectedEdge, RadiusLaw, SetbackLaw);
    Panel.Expect("One explicit edge dispatches the nonlinear-setback corner", static_cast<bool>(Candidate));
    if (Candidate)
    {
        const NonlinearVariableSetbackCornerSpecification& Specification = Candidate.Payload;
        Panel.Within("The nonlinear-setback origin is extracted exactly", Specification.Origin.Distance(Origin), 1e-12);
        Panel.Within("The nonlinear-setback axis is extracted exactly", Specification.EdgeAxis.Distance(Vec3{ 1, 0, 0 }), 1e-12);
        Panel.Within("The nonlinear-setback edge length is extracted exactly", Specification.Length - Length, 1e-12);
        Panel.Within("The constant radius is retained exactly", Specification.RadiusLaw.Start - RadiusLaw.Start, 1e-12);
        Panel.Expect("The dispatched radius remains constant", std::fabs(Specification.RadiusLaw.SecondDerivative()) <= 1e-12 &&
                     std::fabs(Specification.RadiusLaw.Start - Specification.RadiusLaw.End) <= 1e-12);
        Panel.Within("The low setback station is retained exactly", Specification.SetbackLaw.Start - SetbackLaw.Start, 1e-12);
        Panel.Within("The middle setback station is retained exactly", Specification.SetbackLaw.Middle - SetbackLaw.Middle, 1e-12);
        Panel.Within("The high setback station is retained exactly", Specification.SetbackLaw.End - SetbackLaw.End, 1e-12);
        Panel.Expect("The dispatched setback is genuinely nonlinear", Specification.SetbackLaw.Nonlinear());
        Panel.Within("The low extent identity is exact", Specification.RadiusLaw.Radius(0.0) + Specification.SetbackLaw.Radius(0.0) - 1.7, 1e-12);
        Panel.Within("The middle extent identity is exact", Specification.RadiusLaw.Radius(0.5) + Specification.SetbackLaw.Radius(0.5) - 2.5, 1e-12);
        const Deliver<NonlinearVariableSetbackCornerSpecification> Repeat =
            BlendSolver::ClassifyNonlinearVariableSetbackCornerEdge(Source, SelectedEdge, RadiusLaw, SetbackLaw);
        Panel.Expect("Nonlinear-setback dispatch is deterministic", Repeat &&
                     Repeat.Payload.Origin.Distance(Specification.Origin) <= 1e-12 &&
                     Repeat.Payload.EdgeAxis.Distance(Specification.EdgeAxis) <= 1e-12 &&
                     std::fabs(Repeat.Payload.SetbackLaw.Middle - Specification.SetbackLaw.Middle) <= 1e-12);
        Panel.Expect("Nonlinear-setback dispatch leaves the source unchanged", SameSource(Source, Before));
    }

    Panel.Section("Separate nonlinear support-setback reconstruction after edge dispatch");
    const Deliver<BrepBody> Result = Candidate
        ? BlendSolver::ReconstructNonlinearVariableSetbackCornerBlend(Candidate.Payload)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no eligible nonlinear-setback edge");
    Panel.Expect("The dispatched nonlinear-setback specification reconstructs a solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        Panel.Expect("The result retains V10/E15/C30/L7/F7 topology",
                     Result.Payload.Vertices.size() == 10 && Result.Payload.Edges.size() == 15 &&
                     Result.Payload.Coedges.size() == 30 && Result.Payload.Loops.size() == 7 &&
                     Result.Payload.Faces.size() == 7 && Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        const QuadraticRadiusLaw OuterLaw{ 1.7, 2.5, 1.7 };
        const double ExpectedVolume = OuterLaw.IntegratedSquare(Length) -
            (1.0 - ScalarCriteria::Pi / 4.0) * RadiusLaw.IntegratedSquare(Length);
        Panel.Expect("The nonlinear-setback volume follows the integrated identity",
                     ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume));
        Panel.Expect("The nonlinear-setback result has outward normals", ExteriorFaceNormals(Result.Payload));
        Panel.Expect("Nonlinear-setback dispatch remains transactional", SameSource(Source, Before));
    }

    Panel.Section("Unsupported edge, law, and source refusals");
    Panel.Expect("An unequal-width box edge refuses", [&]()
    {
        const Deliver<BrepBody> Unequal = BrepBody::Box(Origin, { Length, 6.0, Width });
        return Unequal && !BlendSolver::ClassifyNonlinearVariableSetbackCornerEdge(Unequal.Payload, SelectedEdge, RadiusLaw, SetbackLaw);
    }());
    Panel.Expect("A non-corner box edge with unequal supports refuses",
                 !BlendSolver::ClassifyNonlinearVariableSetbackCornerEdge(Source, 1, RadiusLaw, SetbackLaw));
    Panel.Expect("An out-of-range edge refuses",
                 !BlendSolver::ClassifyNonlinearVariableSetbackCornerEdge(Source, 99, RadiusLaw, SetbackLaw));
    Panel.Expect("A variable or non-finite radius law refuses",
                 !BlendSolver::ClassifyNonlinearVariableSetbackCornerEdge(Source, SelectedEdge, { 0.9, 1.0, 0.9 }, SetbackLaw) &&
                 !BlendSolver::ClassifyNonlinearVariableSetbackCornerEdge(Source, SelectedEdge,
                     { std::numeric_limits<double>::quiet_NaN(), 0.9, 0.9 }, SetbackLaw));
    Panel.Expect("A linear, asymmetric, or non-finite setback law refuses",
                 !BlendSolver::ClassifyNonlinearVariableSetbackCornerEdge(Source, SelectedEdge, RadiusLaw, { 0.8, 0.8, 0.8 }) &&
                 !BlendSolver::ClassifyNonlinearVariableSetbackCornerEdge(Source, SelectedEdge, RadiusLaw, { 0.8, 1.6, 1.0 }) &&
                 !BlendSolver::ClassifyNonlinearVariableSetbackCornerEdge(Source, SelectedEdge, RadiusLaw,
                     { std::numeric_limits<double>::quiet_NaN(), 1.6, 0.8 }));
    Panel.Expect("A consuming nonlinear setback extent refuses",
                 !BlendSolver::ClassifyNonlinearVariableSetbackCornerEdge(Source, SelectedEdge, RadiusLaw, { 4.1, 4.8, 4.1 }));
    const Deliver<BrepBody> Cylinder = BrepBody::Cylinder(Origin, { 0, 0, 1 }, Width, Length);
    Panel.Expect("A cylinder remains outside nonlinear-setback edge dispatch",
                 Cylinder && !BlendSolver::ClassifyNonlinearVariableSetbackCornerEdge(Cylinder.Payload, 0, RadiusLaw, SetbackLaw));
    BrepBody NonManifold = Source;
    NonManifold.Edges[SelectedEdge].Coedges.push_back(NonManifold.Edges[SelectedEdge].Coedges.front());
    Panel.Expect("A malformed box refuses transactionally",
                 !BlendSolver::ClassifyNonlinearVariableSetbackCornerEdge(NonManifold, SelectedEdge, RadiusLaw, SetbackLaw));
    Panel.Expect("The source remains unchanged after all nonlinear-setback refusals", SameSource(Source, Before));

    Panel.Section("Distinct sharp/nonlinear-setback proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase38y_NonlinearVariableSetbackCornerEdgeDispatch.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("SelectedSharpNonlinearSetback", Source.Transformed(Mat4::Translation({ -12, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("DispatchedNonlinearSetback", Result.Payload.Transformed(Mat4::Translation({ 4, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 190 -10") && Host.Execute("view fit") &&
        Host.Execute("render Phase38y_NonlinearVariableSetbackCornerEdgeDispatch");
    Panel.Expect("The sharp/nonlinear-setback proof render completes", Rendered);
    Panel.Expect("The nonlinear-setback dispatch proof PNG is visible",
                 std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
