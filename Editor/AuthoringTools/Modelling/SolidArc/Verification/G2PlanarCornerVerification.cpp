//=============================================================================================================================================
// SolidArc · Stage 3b · bounded G2 planar corner transition
//
// This is a separate profile family from rolling-ball fillets. A quintic Bezier corner has
// support-aligned tangents and zero endpoint curvature, so its extruded patch joins the two
// planar support faces with measured G2 curvature. It is intentionally not described as a
// circular rolling-ball solution.
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

[[nodiscard]] double EndpointCurvature(const NurbsCurve& Curve, double T) noexcept
{
    Vec3 Derivatives[3];
    Curve.Derivatives(T, 2, Derivatives);
    const double Speed = Derivatives[1].Length();
    return Speed > ScalarCriteria::GeometricTolerance
        ? Derivatives[1].Cross(Derivatives[2]).Length() / (Speed * Speed * Speed)
        : ScalarCriteria::Infinity;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · G2 planar corner transition");
    Panel.Section("Quintic profile is G2 to both planar supports");

    G2PlanarCornerSpecification Specification;
    Specification.Origin = { 0, 0, 0 };
    Specification.EdgeAxis = { 1, 0, 0 };
    Specification.Length = 8.0;
    Specification.Width = 5.0;
    Specification.Radius = 1.5;
    Specification.HandleFraction = 1.0 / 3.0;

    const Deliver<NurbsCurve> Profile = BlendSolver::BuildG2CornerProfile(Specification);
    Panel.Expect("The G2 corner profile builds as a quintic", Profile && Profile.Payload.Degree == 5 && Profile.Payload.PoleCount() == 6);
    std::string Refusal;
    Panel.Expect("The profile has support-aligned tangents and zero endpoint curvature",
                 Profile && BlendSolver::ValidateG2CornerProfile(Profile.Payload, Specification, Refusal));
    if (Profile)
    {
        Panel.Within("The low support join has zero curvature", EndpointCurvature(Profile.Payload, 0.0), 1e-10);
        Panel.Within("The high support join has zero curvature", EndpointCurvature(Profile.Payload, 1.0), 1e-10);
        Panel.Expect("The interior transition has measurable positive curvature", EndpointCurvature(Profile.Payload, 0.5) > 0.0);
        Panel.Expect("The G2 removed-area identity is positive", BlendSolver::G2CornerRemovalArea(Specification.Radius,
                     Specification.HandleFraction) > 0.0);
    }

    Panel.Section("G2 profile reconstruction and analytic volume");
    const Deliver<BrepBody> Result = BlendSolver::ReconstructG2PlanarCorner(Specification);
    Panel.Expect("The G2 profile commits one positive-volume solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        const double ExpectedVolume = Specification.Length *
            (Specification.Width * Specification.Width - BlendSolver::G2CornerRemovalArea(Specification.Radius,
                                                                                           Specification.HandleFraction));
        Panel.Expect("The G2 result has capped V10/E15/C30/L7/F7 topology", Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0 &&
                     Result.Payload.Vertices.size() == 10 && Result.Payload.Edges.size() == 15 &&
                     Result.Payload.Coedges.size() == 30 && Result.Payload.Loops.size() == 7 && Result.Payload.Faces.size() == 7);
        Panel.Within("The G2 volume follows the quintic profile area", std::fabs(Report.Volume - ExpectedVolume) / ExpectedVolume, 1e-3);
        Panel.Expect("Every G2 face tessellation carries an outward normal", ExteriorFaceNormals(Result.Payload));
    }

    Panel.Section("G2-specific refusal boundaries");
    G2PlanarCornerSpecification Bad = Specification;
    Bad.HandleFraction = 0.5;
    Panel.Expect("A degenerate half-radius handle refuses", !BlendSolver::BuildG2CornerProfile(Bad));
    Bad = Specification;
    Bad.HandleFraction = 0.0;
    Panel.Expect("A zero handle refuses", !BlendSolver::BuildG2CornerProfile(Bad));
    Bad = Specification;
    Bad.Radius = Specification.Width;
    Panel.Expect("A radius consuming the support width refuses", !BlendSolver::ReconstructG2PlanarCorner(Bad));
    Bad = Specification;
    Bad.Length = 0.0;
    Panel.Expect("A zero-length G2 edge refuses", !BlendSolver::ReconstructG2PlanarCorner(Bad));
    Bad = Specification;
    Bad.EdgeAxis = { 0, 0, 0 };
    Panel.Expect("A degenerate G2 frame refuses", !BlendSolver::ReconstructG2PlanarCorner(Bad));
    Panel.Expect("The accepted G2 result remains valid after refusals", Result && Result.Payload.Validate().Solid());

    Panel.Section("Exterior-filled comparison proof");
    VariableSetbackCornerSpecification Rolling;
    Rolling.Origin = { 0, 0, 0 };
    Rolling.EdgeAxis = { 1, 0, 0 };
    Rolling.Length = Specification.Length;
    Rolling.RadiusLaw = { Specification.Radius, Specification.Radius };
    Rolling.SetbackLaw = { Specification.Width - Specification.Radius, Specification.Width - Specification.Radius };
    const Deliver<BrepBody> Reference = BlendSolver::ReconstructVariableSetbackCornerBlend(Rolling);
    Panel.Expect("The circular rolling comparison fixture commits", Reference && Reference.Payload.Validate().Solid());
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase37d_G2PlanarCorner.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Result && Reference &&
        Host.Document().AddBody("G2PlanarCorner", Result.Payload.Transformed(Mat4::Translation({ -7, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("RollingReference", Reference.Payload.Transformed(Mat4::Translation({ 7, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 190 -10") && Host.Execute("view fit") &&
        Host.Execute("render Phase37d_G2PlanarCorner");
    Panel.Expect("The G2/rolling comparison proof render completes", Rendered);
    Panel.Expect("The G2 proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
