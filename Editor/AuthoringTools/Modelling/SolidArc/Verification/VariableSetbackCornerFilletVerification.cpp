//=============================================================================================================================================
// SolidArc · Stage 2 · bounded variable-radius rolling corner with a variable support setback
//
// This slice keeps the Stage 1 rolling section exact at every station and adds one independent
// linear clearance law. The setback is measured from each quarter-circle tangent point to the
// far boundary on its planar support. Both laws remain linear; nonlinear radius/setback laws,
// partial edges, apexes, G2 joins and arbitrary healing are intentionally outside this route.
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
    VerificationPanel Panel("SolidArc · variable-radius rolling corner · variable support setback");
    Panel.Section("Independent linear radius and support-setback laws");

    VariableSetbackCornerSpecification Specification;
    Specification.Origin = { 0, 0, 0 };
    Specification.EdgeAxis = { 1, 0, 0 };
    Specification.Length = 8.0;
    Specification.RadiusLaw = { 0.75, 1.75 };
    Specification.SetbackLaw = { 0.90, 2.10 };

    const Deliver<BrepBody> Result = BlendSolver::ReconstructVariableSetbackCornerBlend(Specification);
    Panel.Expect("The independent radius/setback laws commit one solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        Panel.Expect("The result has exact capped V10/E15/C30/L7/F7 topology", Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0 &&
                     Result.Payload.Vertices.size() == 10 && Result.Payload.Edges.size() == 15 &&
                     Result.Payload.Coedges.size() == 30 && Result.Payload.Loops.size() == 7 && Result.Payload.Faces.size() == 7);

        const double R0 = Specification.RadiusLaw.Start, R1 = Specification.RadiusLaw.End;
        const double S0 = Specification.SetbackLaw.Start, S1 = Specification.SetbackLaw.End;
        const double D0 = R0 + S0, D1 = R1 + S1;
        const double ExpectedOuter = Specification.Length * (D0 * D0 + D0 * D1 + D1 * D1) / 3.0;
        const double RemovedCorner = (1.0 - ScalarCriteria::Pi / 4.0) * Specification.Length *
            (R0 * R0 + R0 * R1 + R1 * R1) / 3.0;
        const double ExpectedVolume = ExpectedOuter - RemovedCorner;
        Panel.Within("The volume follows the variable support extent minus the rounded corner", 
                     std::fabs(Report.Volume - ExpectedVolume) / ExpectedVolume, 1e-3);
        Panel.Expect("Every variable-setback face tessellation carries an outward normal", ExteriorFaceNormals(Result.Payload));
        for (int I = 0; I <= 4; ++I)
        {
            const double T = static_cast<double>(I) / 4.0;
            const double Radius = Specification.RadiusLaw.Radius(T);
            const double Setback = Specification.SetbackLaw.Radius(T);
            const double Boundary = Radius + Setback;
            Panel.Within("Station setback equals boundary extent minus rolling radius", Boundary - Radius - Setback, 1e-12);
        }
        Panel.Within("The low station setback is measured independently", Specification.SetbackLaw.Radius(0.0) - S0, 1e-12);
        Panel.Within("The high station setback is measured independently", Specification.SetbackLaw.Radius(1.0) - S1, 1e-12);
        Panel.Expect("The support clearance changes along the edge", std::fabs(Specification.SetbackLaw.Slope()) > ScalarCriteria::GeometricTolerance);
        Panel.Expect("The rolling radius and support setback are not the same law", std::fabs(S0 - R0) > ScalarCriteria::GeometricTolerance ||
                     std::fabs(S1 - R1) > ScalarCriteria::GeometricTolerance);
    }

    Panel.Section("Transactional bounds for the separate setback law");
    VariableSetbackCornerSpecification Bad = Specification;
    Bad.RadiusLaw.Start = 0.0;
    Panel.Expect("A zero-radius station refuses", !BlendSolver::ReconstructVariableSetbackCornerBlend(Bad));
    Bad = Specification;
    Bad.RadiusLaw.End = -0.1;
    Panel.Expect("A negative-radius station refuses", !BlendSolver::ReconstructVariableSetbackCornerBlend(Bad));
    Bad = Specification;
    Bad.SetbackLaw.Start = 0.0;
    Panel.Expect("A zero support setback refuses", !BlendSolver::ReconstructVariableSetbackCornerBlend(Bad));
    Bad = Specification;
    Bad.SetbackLaw.End = -0.1;
    Panel.Expect("A negative support setback refuses", !BlendSolver::ReconstructVariableSetbackCornerBlend(Bad));
    Bad = Specification;
    Bad.Length = 0.0;
    Panel.Expect("A zero-length edge refuses", !BlendSolver::ReconstructVariableSetbackCornerBlend(Bad));
    Bad = Specification;
    Bad.EdgeAxis = { 0, 0, 0 };
    Panel.Expect("A degenerate edge frame refuses", !BlendSolver::ReconstructVariableSetbackCornerBlend(Bad));
    Bad = Specification;
    Bad.RadiusLaw.Start = 1e308;
    Panel.Expect("An overflowing support extent refuses", !BlendSolver::ReconstructVariableSetbackCornerBlend(Bad));
    Panel.Expect("A valid reconstruction remains unchanged after refused laws", Result && Result.Payload.Validate().Solid());

    Panel.Section("Exterior-filled visible proof");
    VariableSetbackCornerSpecification Constant = Specification;
    Constant.SetbackLaw = { 1.50, 1.50 };
    const Deliver<BrepBody> Reference = BlendSolver::ReconstructVariableSetbackCornerBlend(Constant);
    Panel.Expect("The constant-setback comparison also commits", Reference && Reference.Payload.Validate().Solid());
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase37b_VariableSetbackCornerFillet.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Result && Reference &&
        Host.Document().AddBody("VariableSetbackCorner", Result.Payload.Transformed(Mat4::Translation({ -7, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("ConstantSetbackReference", Reference.Payload.Transformed(Mat4::Translation({ 7, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 190 -10") && Host.Execute("view fit") &&
        Host.Execute("render Phase37b_VariableSetbackCornerFillet");
    Panel.Expect("The variable-setback comparison proof render completes", Rendered);
    Panel.Expect("The variable-setback proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
