//=============================================================================================================================================
// SolidArc · Stage 3a · bounded nonlinear variable-radius corner surface and fillet
//
// A quadratic Bernstein radius law is reconstructed through three exact station sections with
// a quadratic loft in the edge direction. This is not a linear ruled approximation. Curvature
// is measured from the law's first and second derivatives before the solid is sewn. G2 across
// the rolling/support junction, partial edges, apexes, and arbitrary healing remain outside this
// deliberately bounded slice.
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
    VerificationPanel Panel("SolidArc · nonlinear variable-radius corner · quadratic law");
    Panel.Section("A true quadratic radius law and its accepted derivatives");

    NonlinearVariableRadiusCornerSpecification Specification;
    Specification.Origin = { 0, 0, 0 };
    Specification.EdgeAxis = { 1, 0, 0 };
    Specification.Length = 8.0;
    Specification.Setback = 1.20;
    Specification.RadiusLaw = { 0.75, 1.65, 0.75 };

    const Deliver<QuadraticVariableRadiusSurface> Surface = BlendSolver::BuildQuadraticVariableRadiusSurface(Specification);
    Panel.Expect("The quadratic radius surface builds explicitly", Surface && Surface.Payload.Length == Specification.Length);
    Panel.Expect("The accepted law is genuinely nonlinear", Specification.RadiusLaw.Nonlinear() &&
                 std::fabs(Specification.RadiusLaw.SecondDerivative()) > ScalarCriteria::GeometricTolerance);
    Panel.Within("The nonlinear law reaches its low endpoint", Specification.RadiusLaw.Radius(0.0) - 0.75, 1e-12);
    Panel.Within("The nonlinear law reaches its high endpoint", Specification.RadiusLaw.Radius(1.0) - 0.75, 1e-12);
    Panel.Within("The nonlinear law reaches its independent middle station", Specification.RadiusLaw.Radius(0.5) - 1.65, 1e-12);
    Panel.Within("The middle derivative is zero by symmetry", Specification.RadiusLaw.FirstDerivative(0.5), 1e-12);

    std::string Refusal;
    double MaximumCircumferential = 0.0;
    double MaximumMeridional = 0.0;
    if (Surface)
    {
        for (int I = 0; I <= 32; ++I)
        {
            const double T = static_cast<double>(I) / 32.0;
            MaximumCircumferential = std::max(MaximumCircumferential, Surface.Payload.CircumferentialCurvature(T));
            MaximumMeridional = std::max(MaximumMeridional, std::fabs(Surface.Payload.MeridionalCurvature(T)));
        }
        Panel.Expect("The sampled nonlinear circumferential and meridional curvature is accepted",
                     BlendSolver::ValidateQuadraticSurfaceCurvature(Surface.Payload, MaximumCircumferential + 1e-9,
                                                                      MaximumMeridional + 1e-9, Refusal));
        Panel.Expect("A bound below circumferential curvature refuses",
                     !BlendSolver::ValidateQuadraticSurfaceCurvature(Surface.Payload, MaximumCircumferential - 1e-4,
                                                                      MaximumMeridional + 1e-9, Refusal));
        Panel.Expect("A bound below meridional curvature refuses",
                     !BlendSolver::ValidateQuadraticSurfaceCurvature(Surface.Payload, MaximumCircumferential + 1e-9,
                                                                      std::max(0.0, MaximumMeridional - 1e-4), Refusal));
    }

    Panel.Section("Quadratic loft reconstruction and analytic volume");
    const Deliver<BrepBody> Result = BlendSolver::ReconstructNonlinearVariableRadiusCornerBlend(Specification);
    Panel.Expect("The nonlinear law reconstructs one positive-volume solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        const QuadraticRadiusLaw OuterLaw{ Specification.RadiusLaw.Start + Specification.Setback,
                                           Specification.RadiusLaw.Middle + Specification.Setback,
                                           Specification.RadiusLaw.End + Specification.Setback };
        const double ExpectedVolume = OuterLaw.IntegratedSquare(Specification.Length) -
            (1.0 - ScalarCriteria::Pi / 4.0) * Specification.RadiusLaw.IntegratedSquare(Specification.Length);
        Panel.Expect("The quadratic loft retains capped V10/E15/C30/L7/F7 topology", Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0 &&
                     Result.Payload.Vertices.size() == 10 && Result.Payload.Edges.size() == 15 &&
                     Result.Payload.Coedges.size() == 30 && Result.Payload.Loops.size() == 7 && Result.Payload.Faces.size() == 7);
        Panel.Within("The nonlinear volume follows the integrated quadratic law",
                     std::fabs(Report.Volume - ExpectedVolume) / ExpectedVolume, 1e-3);
        Panel.Expect("Every nonlinear face tessellation carries an outward normal", ExteriorFaceNormals(Result.Payload));
    }

    Panel.Section("Linear, degenerate, and unsupported law boundaries");
    NonlinearVariableRadiusCornerSpecification Bad = Specification;
    Bad.RadiusLaw.Middle = (Bad.RadiusLaw.Start + Bad.RadiusLaw.End) * 0.5;
    Panel.Expect("A quadratic descriptor that is actually linear refuses", !BlendSolver::BuildQuadraticVariableRadiusSurface(Bad));
    Bad = Specification;
    Bad.RadiusLaw.Middle = 0.0;
    Panel.Expect("A zero quadratic control radius refuses", !BlendSolver::ReconstructNonlinearVariableRadiusCornerBlend(Bad));
    Bad = Specification;
    Bad.RadiusLaw = { 1.0, 0.1, 10.0 };
    Panel.Expect("A law that dips below zero between positive stations refuses", !BlendSolver::ReconstructNonlinearVariableRadiusCornerBlend(Bad));
    Bad = Specification;
    Bad.RadiusLaw.Start = -0.1;
    Panel.Expect("A negative endpoint radius refuses", !BlendSolver::ReconstructNonlinearVariableRadiusCornerBlend(Bad));
    Bad = Specification;
    Bad.Setback = 0.0;
    Panel.Expect("A zero support setback refuses", !BlendSolver::ReconstructNonlinearVariableRadiusCornerBlend(Bad));
    Bad = Specification;
    Bad.Length = 0.0;
    Panel.Expect("A zero-length edge refuses", !BlendSolver::ReconstructNonlinearVariableRadiusCornerBlend(Bad));
    Bad = Specification;
    Bad.EdgeAxis = { 0, 0, 0 };
    Panel.Expect("A degenerate nonlinear frame refuses", !BlendSolver::ReconstructNonlinearVariableRadiusCornerBlend(Bad));
    Panel.Expect("The accepted nonlinear result remains valid after refusals", Result && Result.Payload.Validate().Solid());

    Panel.Section("Exterior-filled nonlinear proof");
    VariableSetbackCornerSpecification ReferenceSpecification;
    ReferenceSpecification.Origin = { 0, 0, 0 };
    ReferenceSpecification.EdgeAxis = { 1, 0, 0 };
    ReferenceSpecification.Length = Specification.Length;
    ReferenceSpecification.RadiusLaw = { Specification.RadiusLaw.Start, Specification.RadiusLaw.End };
    ReferenceSpecification.SetbackLaw = { Specification.Setback, Specification.Setback };
    const Deliver<BrepBody> Reference = BlendSolver::ReconstructVariableSetbackCornerBlend(ReferenceSpecification);
    Panel.Expect("The linear comparison fixture commits", Reference && Reference.Payload.Validate().Solid());
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase37c_NonlinearVariableRadiusCorner.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Result && Reference &&
        Host.Document().AddBody("NonlinearRadiusCorner", Result.Payload.Transformed(Mat4::Translation({ -7, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("LinearRadiusReference", Reference.Payload.Transformed(Mat4::Translation({ 7, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 190 -10") && Host.Execute("view fit") &&
        Host.Execute("render Phase37c_NonlinearVariableRadiusCorner");
    Panel.Expect("The nonlinear comparison proof render completes", Rendered);
    Panel.Expect("The nonlinear proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
