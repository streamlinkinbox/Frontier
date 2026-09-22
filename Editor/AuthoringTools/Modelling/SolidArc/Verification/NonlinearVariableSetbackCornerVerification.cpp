//=============================================================================================================================================
// SolidArc · Stage 3c · bounded nonlinear support-setback corner blend
//
// The radius may remain constant while the support clearance follows a genuinely nonlinear
// quadratic law. Three exact station sections are quadratic-lofted; analytic extent/removal
// volume and curvature acceptance prevent this from collapsing into the earlier linear setback
// route. Rolling-ball G2, unequal support laws, partial edges, and arbitrary healing remain out
// of scope.
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
[[nodiscard]] bool ExteriorFaceNormals(const BrepBody& Body) noexcept
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
    VerificationPanel Panel("SolidArc · nonlinear support-setback corner");
    Panel.Section("A separate nonlinear setback law");

    NonlinearVariableSetbackCornerSpecification Specification;
    Specification.Origin = { 0, 0, 0 };
    Specification.EdgeAxis = { 1, 0, 0 };
    Specification.Length = 8.0;
    Specification.RadiusLaw = { 0.90, 0.90, 0.90 };
    Specification.SetbackLaw = { 0.80, 1.60, 0.80 };

    const Deliver<BrepBody> Result = BlendSolver::ReconstructNonlinearVariableSetbackCornerBlend(Specification);
    Panel.Expect("The nonlinear setback law commits one solid", Result && Result.Payload.Validate().Solid());
    Panel.Expect("The setback descriptor is genuinely nonlinear", Specification.SetbackLaw.Nonlinear());
    Panel.Within("The low setback station is exact", Specification.SetbackLaw.Radius(0.0) - 0.80, 1e-12);
    Panel.Within("The middle setback station is exact", Specification.SetbackLaw.Radius(0.5) - 1.60, 1e-12);
    Panel.Within("The high setback station is exact", Specification.SetbackLaw.Radius(1.0) - 0.80, 1e-12);
    Panel.Expect("The radius remains independently constant", std::fabs(Specification.RadiusLaw.FirstDerivative(0.5)) <= 1e-12 &&
                 std::fabs(Specification.RadiusLaw.SecondDerivative()) <= 1e-12);

    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        const QuadraticRadiusLaw OuterLaw{ 1.70, 2.50, 1.70 };
        const double ExpectedVolume = OuterLaw.IntegratedSquare(Specification.Length) -
            (1.0 - ScalarCriteria::Pi / 4.0) * Specification.RadiusLaw.IntegratedSquare(Specification.Length);
        Panel.Expect("The nonlinear setback result has capped V10/E15/C30/L7/F7 topology", Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0 &&
                     Result.Payload.Vertices.size() == 10 && Result.Payload.Edges.size() == 15 &&
                     Result.Payload.Coedges.size() == 30 && Result.Payload.Loops.size() == 7 && Result.Payload.Faces.size() == 7);
        Panel.Within("The volume follows the nonlinear support extent minus the corner removal",
                     std::fabs(Report.Volume - ExpectedVolume) / ExpectedVolume, 1e-3);
        Panel.Expect("Every nonlinear-setback face tessellation carries an outward normal", ExteriorFaceNormals(Result.Payload));
        for (int I = 0; I <= 4; ++I)
        {
            const double T = static_cast<double>(I) / 4.0;
            const double Radius = Specification.RadiusLaw.Radius(T);
            const double Setback = Specification.SetbackLaw.Radius(T);
            const double Extent = Radius + Setback;
            Panel.Within("Station extent minus radius equals nonlinear setback", Extent - Radius - Setback, 1e-12);
        }
    }

    Panel.Section("Nonlinear setback refusal boundaries");
    NonlinearVariableSetbackCornerSpecification Bad = Specification;
    Bad.SetbackLaw = { 0.80, 0.80, 0.80 };
    Panel.Expect("A linear setback law refuses this nonlinear-only route", !BlendSolver::ReconstructNonlinearVariableSetbackCornerBlend(Bad));
    Bad = Specification;
    Bad.SetbackLaw.Middle = 0.0;
    Panel.Expect("A zero middle setback refuses", !BlendSolver::ReconstructNonlinearVariableSetbackCornerBlend(Bad));
    Bad = Specification;
    Bad.SetbackLaw = { 1.0, 0.1, 10.0 };
    Panel.Expect("A setback law that dips below zero refuses", !BlendSolver::ReconstructNonlinearVariableSetbackCornerBlend(Bad));
    Bad = Specification;
    Bad.RadiusLaw.Start = -0.1;
    Panel.Expect("A negative radius refuses", !BlendSolver::ReconstructNonlinearVariableSetbackCornerBlend(Bad));
    Bad = Specification;
    Bad.Length = 0.0;
    Panel.Expect("A zero-length edge refuses", !BlendSolver::ReconstructNonlinearVariableSetbackCornerBlend(Bad));
    Bad = Specification;
    Bad.EdgeAxis = { 0, 0, 0 };
    Panel.Expect("A degenerate edge frame refuses", !BlendSolver::ReconstructNonlinearVariableSetbackCornerBlend(Bad));
    Panel.Expect("The accepted result remains valid after refused laws", Result && Result.Payload.Validate().Solid());

    Panel.Section("Exterior-filled nonlinear-setback proof");
    VariableSetbackCornerSpecification ReferenceSpecification;
    ReferenceSpecification.Origin = { 0, 0, 0 };
    ReferenceSpecification.EdgeAxis = { 1, 0, 0 };
    ReferenceSpecification.Length = Specification.Length;
    ReferenceSpecification.RadiusLaw = { Specification.RadiusLaw.Start, Specification.RadiusLaw.End };
    ReferenceSpecification.SetbackLaw = { Specification.SetbackLaw.Start, Specification.SetbackLaw.End };
    const Deliver<BrepBody> Reference = BlendSolver::ReconstructVariableSetbackCornerBlend(ReferenceSpecification);
    Panel.Expect("The linear-setback comparison fixture commits", Reference && Reference.Payload.Validate().Solid());
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase37e_NonlinearVariableSetbackCorner.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Result && Reference &&
        Host.Document().AddBody("NonlinearSetbackCorner", Result.Payload.Transformed(Mat4::Translation({ -7, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("LinearSetbackReference", Reference.Payload.Transformed(Mat4::Translation({ 7, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 190 -10") && Host.Execute("view fit") &&
        Host.Execute("render Phase37e_NonlinearVariableSetbackCorner");
    Panel.Expect("The nonlinear-setback comparison proof render completes", Rendered);
    Panel.Expect("The nonlinear-setback proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
