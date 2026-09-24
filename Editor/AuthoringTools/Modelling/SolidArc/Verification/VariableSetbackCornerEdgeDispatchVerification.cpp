//=============================================================================================================================================
// SolidArc · Stage 4w · bounded variable support-setback corner edge dispatch
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
    VerificationPanel Panel("SolidArc · bounded variable support-setback corner edge dispatch");
    const Vec3 Origin{ 0, 0, 0 };
    const double Length = 8.0;
    const double Width = 5.0;
    const VariableRadiusLaw RadiusLaw{ 1.0, 2.0 };
    const VariableRadiusLaw SetbackLaw{ 1.5, 2.0 };
    constexpr int SelectedEdge = 0;
    const Deliver<BrepBody> SourceDeliver = BrepBody::Box(Origin, { Length, Width, Width });
    Panel.Expect("The canonical rectangular source is a closed solid", SourceDeliver && SourceDeliver.Payload.Validate().Solid());
    if (SourceDeliver)
        Panel.Expect("The source has canonical V8/E12/C24/L6/F6 topology", SourceDeliver.Payload.Vertices.size() == 8 &&
                     SourceDeliver.Payload.Edges.size() == 12 && SourceDeliver.Payload.Coedges.size() == 24 &&
                     SourceDeliver.Payload.Loops.size() == 6 && SourceDeliver.Payload.Faces.size() == 6);
    const BrepBody Source = SourceDeliver.Payload;
    const BodyReport Before = Source.Validate();
    const Deliver<VariableSetbackCornerSpecification> Candidate =
        BlendSolver::ClassifyVariableSetbackCornerEdge(Source, SelectedEdge, RadiusLaw, SetbackLaw);
    Panel.Expect("One explicit edge dispatches the variable-setback corner", static_cast<bool>(Candidate));
    if (Candidate)
    {
        const VariableSetbackCornerSpecification& Specification = Candidate.Payload;
        Panel.Within("The setback edge origin is extracted exactly", Specification.Origin.Distance(Origin), 1e-12);
        Panel.Within("The setback edge axis is extracted exactly", Specification.EdgeAxis.Distance(Vec3{ 1, 0, 0 }), 1e-12);
        Panel.Within("The setback edge length is extracted exactly", Specification.Length - Length, 1e-12);
        Panel.Within("The low radius station is retained exactly", Specification.RadiusLaw.Start - RadiusLaw.Start, 1e-12);
        Panel.Within("The high radius station is retained exactly", Specification.RadiusLaw.End - RadiusLaw.End, 1e-12);
        Panel.Within("The low setback station is retained exactly", Specification.SetbackLaw.Start - SetbackLaw.Start, 1e-12);
        Panel.Within("The high setback station is retained exactly", Specification.SetbackLaw.End - SetbackLaw.End, 1e-12);
        Panel.Within("The low support extent identity is exact",
                     Specification.RadiusLaw.Start + Specification.SetbackLaw.Start - 2.5, 1e-12);
        Panel.Within("The high support extent identity is exact",
                     Specification.RadiusLaw.End + Specification.SetbackLaw.End - 4.0, 1e-12);
        const Deliver<VariableSetbackCornerSpecification> Repeat =
            BlendSolver::ClassifyVariableSetbackCornerEdge(Source, SelectedEdge, RadiusLaw, SetbackLaw);
        Panel.Expect("Variable-setback dispatch is deterministic", Repeat &&
                     Repeat.Payload.Origin.Distance(Specification.Origin) <= 1e-12 &&
                     Repeat.Payload.EdgeAxis.Distance(Specification.EdgeAxis) <= 1e-12 &&
                     std::fabs(Repeat.Payload.Length - Specification.Length) <= 1e-12 &&
                     std::fabs(Repeat.Payload.SetbackLaw.End - Specification.SetbackLaw.End) <= 1e-12);
        Panel.Expect("Variable-setback dispatch leaves the source unchanged", SameSource(Source, Before));
    }

    Panel.Section("Separate variable-setback reconstruction after edge dispatch");
    const Deliver<BrepBody> Result = Candidate
        ? BlendSolver::ReconstructVariableSetbackCornerBlend(Candidate.Payload)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no eligible variable-setback edge");
    Panel.Expect("The dispatched variable-setback specification reconstructs a solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        Panel.Expect("The result retains V10/E15/C30/L7/F7 topology",
                     Result.Payload.Vertices.size() == 10 && Result.Payload.Edges.size() == 15 &&
                     Result.Payload.Coedges.size() == 30 && Result.Payload.Loops.size() == 7 &&
                     Result.Payload.Faces.size() == 7 && Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        const double D0 = RadiusLaw.Start + SetbackLaw.Start;
        const double D1 = RadiusLaw.End + SetbackLaw.End;
        const double ExpectedVolume = Length * (D0 * D0 + D0 * D1 + D1 * D1) / 3.0 -
            (1.0 - ScalarCriteria::Pi / 4.0) * Length *
            (RadiusLaw.Start * RadiusLaw.Start + RadiusLaw.Start * RadiusLaw.End + RadiusLaw.End * RadiusLaw.End) / 3.0;
        Panel.Expect("The variable-setback volume follows the integrated identity",
                     ScalarCriteria::WithinVolumeTolerance(Report.Volume, ExpectedVolume));
        Panel.Expect("The variable-setback result has outward normals", ExteriorFaceNormals(Result.Payload));
        Panel.Expect("Variable-setback dispatch remains transactional", SameSource(Source, Before));
    }

    Panel.Section("Unsupported edge, law, and source refusals");
    Panel.Expect("An unequal-width box edge refuses", [&]()
    {
        const Deliver<BrepBody> Unequal = BrepBody::Box(Origin, { Length, 6.0, Width });
        return Unequal && !BlendSolver::ClassifyVariableSetbackCornerEdge(Unequal.Payload, SelectedEdge, RadiusLaw, SetbackLaw);
    }());
    Panel.Expect("A non-corner box edge with unequal supports refuses",
                 !BlendSolver::ClassifyVariableSetbackCornerEdge(Source, 1, RadiusLaw, SetbackLaw));
    Panel.Expect("An out-of-range edge refuses",
                 !BlendSolver::ClassifyVariableSetbackCornerEdge(Source, 99, RadiusLaw, SetbackLaw));
    Panel.Expect("Zero, negative, and non-finite radius laws refuse",
                 !BlendSolver::ClassifyVariableSetbackCornerEdge(Source, SelectedEdge, { 0.0, 2.0 }, SetbackLaw) &&
                 !BlendSolver::ClassifyVariableSetbackCornerEdge(Source, SelectedEdge, { -1.0, 2.0 }, SetbackLaw) &&
                 !BlendSolver::ClassifyVariableSetbackCornerEdge(Source, SelectedEdge,
                     { std::numeric_limits<double>::quiet_NaN(), 2.0 }, SetbackLaw));
    Panel.Expect("Zero, negative, and non-finite setback laws refuse",
                 !BlendSolver::ClassifyVariableSetbackCornerEdge(Source, SelectedEdge, RadiusLaw, { 0.0, 2.0 }) &&
                 !BlendSolver::ClassifyVariableSetbackCornerEdge(Source, SelectedEdge, RadiusLaw, { -1.0, 2.0 }) &&
                 !BlendSolver::ClassifyVariableSetbackCornerEdge(Source, SelectedEdge, RadiusLaw,
                     { std::numeric_limits<double>::quiet_NaN(), 2.0 }));
    Panel.Expect("A consuming support extent refuses",
                 !BlendSolver::ClassifyVariableSetbackCornerEdge(Source, SelectedEdge, RadiusLaw, { 4.1, 4.1 }));
    const Deliver<BrepBody> Cylinder = BrepBody::Cylinder(Origin, { 0, 0, 1 }, Width, Length);
    Panel.Expect("A cylinder remains outside variable-setback edge dispatch",
                 Cylinder && !BlendSolver::ClassifyVariableSetbackCornerEdge(Cylinder.Payload, 0, RadiusLaw, SetbackLaw));
    BrepBody NonManifold = Source;
    NonManifold.Edges[SelectedEdge].Coedges.push_back(NonManifold.Edges[SelectedEdge].Coedges.front());
    Panel.Expect("A malformed box refuses transactionally",
                 !BlendSolver::ClassifyVariableSetbackCornerEdge(NonManifold, SelectedEdge, RadiusLaw, SetbackLaw));
    Panel.Expect("The source remains unchanged after all setback refusals", SameSource(Source, Before));

    Panel.Section("Distinct sharp/variable-setback proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase38w_VariableSetbackCornerEdgeDispatch.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("SelectedSharpSetbackCorner", Source.Transformed(Mat4::Translation({ -12, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("DispatchedVariableSetback", Result.Payload.Transformed(Mat4::Translation({ 4, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 190 -10") && Host.Execute("view fit") &&
        Host.Execute("render Phase38w_VariableSetbackCornerEdgeDispatch");
    Panel.Expect("The sharp/variable-setback proof render completes", Rendered);
    Panel.Expect("The variable-setback dispatch proof PNG is visible",
                 std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
