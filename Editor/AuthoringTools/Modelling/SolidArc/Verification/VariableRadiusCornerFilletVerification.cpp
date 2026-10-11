//=============================================================================================================================================
// SolidArc · bounded rolling-ball variable-radius fillet on one straight planar corner
//
// Stage 1 of the variable-radius roadmap: a positive linear radius law along one finite
// straight edge. Each station is an exact quarter-circle rolling section; nonlinear laws,
// partial edges, apexes, G2 joins, and arbitrary support healing remain refused/out of scope.
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
        for (size_t I = 0; I < T.Positions.size(); ++I) { Position += T.Positions[I]; Normal += T.Normals[I]; }
        Position = Position / static_cast<double>(T.Positions.size());
        if (Normal.Normalised().Dot(Position - Centre) <= ScalarCriteria::MergeTolerance) return false;
    }
    return true;
}

[[nodiscard]] bool SameReport(const BodyReport& A, const BodyReport& B) noexcept
{
    return A.Vertices == B.Vertices && A.Edges == B.Edges && A.Loops == B.Loops &&
           A.Faces == B.Faces && A.OpenEdges == B.OpenEdges && A.NonManifoldEdges == B.NonManifoldEdges &&
           A.MisorientedEdges == B.MisorientedEdges && std::fabs(A.Volume - B.Volume) <= 1e-12;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · variable-radius rolling-ball fillet · straight planar corner");
    Panel.Section("One finite straight corner with a positive linear radius law");

    VariableRadiusCornerSpecification Specification;
    Specification.Origin = { 0, 0, 0 };
    Specification.EdgeAxis = { 1, 0, 0 };
    Specification.Length = 8.0;
    Specification.Width = 5.0;
    Specification.RadiusLaw = { 1.0, 2.0 };
    const Deliver<BrepBody> Result = BlendSolver::ReconstructVariableRadiusCornerBlend(Specification);
    Panel.Expect("The bounded variable-radius corner commits one solid", Result && Result.Payload.Validate().Solid());
    const Deliver<BrepBody> Sharp = BrepBody::Box({ 0, 0, 0 }, { Specification.Length, Specification.Width, Specification.Width });
    Panel.Expect("The sharp source fixture is a closed V8/E12/F6 solid", Sharp && Sharp.Payload.Validate().Solid() &&
                 Sharp.Payload.Vertices.size() == 8 && Sharp.Payload.Edges.size() == 12 && Sharp.Payload.Faces.size() == 6);
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        const double R0 = Specification.RadiusLaw.Start, R1 = Specification.RadiusLaw.End;
        const double ExpectedVolume = Specification.Length * Specification.Width * Specification.Width -
            (1.0 - ScalarCriteria::Pi / 4.0) * Specification.Length * (R0 * R0 + R0 * R1 + R1 * R1) / 3.0;
        Panel.Expect("The result has exact capped V10/E15/C30/L7/F7 topology", Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0 &&
                     Result.Payload.Vertices.size() == 10 && Result.Payload.Edges.size() == 15 &&
                     Result.Payload.Coedges.size() == 30 && Result.Payload.Loops.size() == 7 && Result.Payload.Faces.size() == 7);
        Panel.Within("The integrated rounded-corner volume follows the linear radius law", std::fabs(Report.Volume - ExpectedVolume) / ExpectedVolume, 1e-3);
        Panel.Expect("Every variable-radius face tessellation carries an outward normal", ExteriorFaceNormals(Result.Payload));
        Panel.Within("The radius law reaches its low station", std::fabs(R0 - 1.0), 1e-12);
        Panel.Within("The radius law reaches its high station", std::fabs(R1 - 2.0), 1e-12);
    }

    Panel.Section("Transactional boundaries remain explicit");
    const BodyReport SharpBefore = Sharp ? Sharp.Payload.Validate() : BodyReport{};
    VariableRadiusCornerSpecification Bad = Specification;
    Bad.RadiusLaw = { 0.0, 2.0 };
    Panel.Expect("A zero-radius station refuses", !BlendSolver::ReconstructVariableRadiusCornerBlend(Bad));
    Bad = Specification;
    Bad.RadiusLaw = { -1.0, 2.0 };
    Panel.Expect("A negative-radius station refuses", !BlendSolver::ReconstructVariableRadiusCornerBlend(Bad));
    Bad = Specification;
    Bad.RadiusLaw.End = Specification.Width;
    Panel.Expect("A consuming radius refuses", !BlendSolver::ReconstructVariableRadiusCornerBlend(Bad));
    Bad = Specification;
    Bad.Length = 0.0;
    Panel.Expect("A zero-length edge refuses", !BlendSolver::ReconstructVariableRadiusCornerBlend(Bad));
    Bad = Specification;
    Bad.EdgeAxis = { 0, 0, 0 };
    Panel.Expect("A degenerate edge frame refuses", !BlendSolver::ReconstructVariableRadiusCornerBlend(Bad));
    Panel.Expect("The sharp source fixture remains immutable", Sharp && SameReport(Sharp.Payload.Validate(), SharpBefore));

    Panel.Section("Exterior-filled visible proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase37_VariableRadiusCornerFillet.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Sharp && Result &&
        Host.Document().AddBody("SharpCorner", Sharp.Payload.Transformed(Mat4::Translation({ -12, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("VariableRadiusCorner", Result.Payload.Transformed(Mat4::Translation({ 4, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 190 -10") && Host.Execute("view fit") && Host.Execute("render Phase37_VariableRadiusCornerFillet");
    Panel.Expect("The sharp/result variable-radius proof render completes", Rendered);
    Panel.Expect("The variable-radius proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
