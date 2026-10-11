//=============================================================================================================================================
// SolidArc · Stage 4j · bounded oblique rolling-ball-core G2 transition
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

[[nodiscard]] double CoreRadiusResidual(const ObliqueG2RollingBallProfile& Profile) noexcept
{
    double Worst = 0.0;
    for (int I = 0; I <= 16; ++I)
        Worst = std::max(Worst, std::fabs(Profile.Pieces[1].Sample(static_cast<double>(I) / 16.0).Distance(Profile.Centre) - Profile.Radius));
    return Worst;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · oblique rolling-ball-core G2 transition");
    ObliqueG2RollingBallPlanarCornerSpecification Specification;
    Specification.Origin = { 0, 0, 0 };
    Specification.EdgeAxis = { 1, 0, 0 };
    Specification.SupportA = { 0, 1, 0 };
    Specification.SupportB = { 0, 0.5, std::sqrt(3.0) * 0.5 };
    Specification.Length = 10.0;
    Specification.WidthA = 6.0;
    Specification.WidthB = 5.5;
    Specification.Radius = 0.75;
    Specification.TransitionAngle = ScalarCriteria::Pi / 18.0;

    const Vec3 A = Specification.SupportA.Normalised();
    const Vec3 B = Specification.SupportB.Normalised();
    const double Theta = std::acos(A.Dot(B));
    const double CotHalf = std::cos(Theta * 0.5) / std::sin(Theta * 0.5);
    const double TangentDistance = Specification.Radius * CotHalf;
    const Deliver<ObliqueG2RollingBallProfile> Profile = BlendSolver::BuildObliqueG2RollingBallProfile(Specification);

    Panel.Section("Strict oblique frame and exact circular core");
    Panel.Expect("The oblique profile builds three exact sections", Profile && Profile.Payload.Pieces.size() == 3);
    std::string Refusal;
    Panel.Expect("The profile validates the oblique G2 joins", Profile &&
                 BlendSolver::ValidateObliqueG2RollingBallProfile(Profile.Payload, Specification, Refusal));
    Panel.Expect("The support angle is strict and non-orthogonal", Theta > 0.0 && Theta < ScalarCriteria::Pi &&
                 std::fabs(Theta - ScalarCriteria::HalfPi) > 1e-6);
    Panel.Expect("The tangent distance is r cot(theta/2)", TangentDistance < Specification.WidthA && TangentDistance < Specification.WidthB);
    if (Profile)
    {
        const ObliqueG2RollingBallProfile& P = Profile.Payload;
        Panel.Expect("The two support transitions are quintic", P.Pieces[0].Degree == 5 && P.Pieces[2].Degree == 5);
        Panel.Expect("The oblique middle section is an exact rational core", P.Pieces[1].Degree == 2 && P.Pieces[1].Rational());
        Panel.Within("The oblique circular-core radius is exact", CoreRadiusResidual(P), 1e-10);
        Panel.Within("The support-A join has zero curvature", P.Pieces[0].Curvature(0.0), 1e-8);
        Panel.Within("The support-B join has zero curvature", P.Pieces[2].Curvature(1.0), 1e-8);
        Panel.Within("The first transition matches 1/r", P.Pieces[0].Curvature(1.0) - 1.0 / Specification.Radius, 2e-7);
        Panel.Within("The exact oblique core has 1/r curvature", P.Pieces[1].Curvature(0.5) - 1.0 / Specification.Radius, 2e-7);
        Panel.Within("The second transition matches 1/r", P.Pieces[2].Curvature(0.0) - 1.0 / Specification.Radius, 2e-7);
        Panel.Expect("The oblique profile removes a positive corner area", BlendSolver::ObliqueG2RollingBallRemovalArea(P) > 0.0);
    }

    Panel.Section("Closed oblique reconstruction and integrated volume");
    const Deliver<BrepBody> Result = BlendSolver::ReconstructObliqueG2RollingBallPlanarCorner(Specification);
    Panel.Expect("The oblique G2 route commits a solid", Result && Result.Payload.Validate().Solid());
    if (Result && Profile)
    {
        const BodyReport Report = Result.Payload.Validate();
        const double SharpArea = 0.5 * Specification.WidthA * Specification.WidthB * std::sin(Theta);
        const double ExpectedVolume = Specification.Length *
            (SharpArea - BlendSolver::ObliqueG2RollingBallRemovalArea(Profile.Payload));
        Panel.Expect("Split oblique transitions sew as V12/E18/F8/L8 topology",
                     Report.Vertices == 12 && Report.Edges == 18 && Result.Payload.Coedges.size() == 36 &&
                     Report.Faces == 8 && Report.Loops == 8 && Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        Panel.Within("The oblique volume follows the profile line integral",
                     std::fabs(Report.Volume - ExpectedVolume) / ExpectedVolume, 1e-3);
        Panel.Expect("Every oblique face carries an outward normal", OutwardNormals(Result.Payload));
        Panel.Expect("The oblique result remains valid after profile inspection", Result.Payload.Validate().Solid());
    }

    Panel.Section("Transactional oblique refusal boundaries");
    ObliqueG2RollingBallPlanarCornerSpecification Bad = Specification;
    Bad.SupportB = Specification.SupportA;
    Panel.Expect("Parallel supports refuse", !BlendSolver::ReconstructObliqueG2RollingBallPlanarCorner(Bad));
    Bad = Specification;
    Bad.EdgeAxis = Specification.SupportA;
    Panel.Expect("A support direction consumed by the edge refuses", !BlendSolver::ReconstructObliqueG2RollingBallPlanarCorner(Bad));
    Bad = Specification;
    Bad.SupportB = { 1, 1, 0 };
    Panel.Expect("A support not perpendicular to the edge refuses", !BlendSolver::ReconstructObliqueG2RollingBallPlanarCorner(Bad));
    Bad = Specification;
    Bad.TransitionAngle = (ScalarCriteria::Pi - Theta) * 0.5;
    Panel.Expect("A transition consuming the oblique core refuses", !BlendSolver::ReconstructObliqueG2RollingBallPlanarCorner(Bad));
    Bad = Specification;
    Bad.Radius = 0.0;
    Panel.Expect("A zero radius refuses", !BlendSolver::ReconstructObliqueG2RollingBallPlanarCorner(Bad));
    Bad = Specification;
    Bad.WidthA = TangentDistance;
    Panel.Expect("A tangent distance consuming support A refuses", !BlendSolver::ReconstructObliqueG2RollingBallPlanarCorner(Bad));
    Bad = Specification;
    Bad.Length = 0.0;
    Panel.Expect("A zero-length edge refuses", !BlendSolver::ReconstructObliqueG2RollingBallPlanarCorner(Bad));
    Panel.Expect("The accepted oblique result remains transactional", Result && Result.Payload.Validate().Solid());

    Panel.Section("Distinct exterior sharp-versus-oblique proof");
    ObliquePlanarCornerFilletSpecification Sharp;
    Sharp.Origin = { 0, 0, 0 };
    Sharp.EdgeAxis = Specification.EdgeAxis;
    Sharp.SupportA = Specification.SupportA;
    Sharp.SupportB = Specification.SupportB;
    Sharp.Length = Specification.Length;
    Sharp.WidthA = Specification.WidthA;
    Sharp.WidthB = Specification.WidthB;
    Sharp.Radius = Specification.Radius;
    const Deliver<BrepBody> Reference = BlendSolver::ReconstructObliquePlanarCornerFillet(Sharp);
    Panel.Expect("The pure oblique rolling reference commits", Reference && Reference.Payload.Validate().Solid());
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase38j_ObliqueG2RollingBall.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Result && Reference &&
        Host.Document().AddBody("ObliqueG2Core", Result.Payload.Transformed(Mat4::Translation({ -7, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("ObliquePureRolling", Reference.Payload.Transformed(Mat4::Translation({ 7, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 190 -10") && Host.Execute("view fit") &&
        Host.Execute("render Phase38j_ObliqueG2RollingBall");
    Panel.Expect("The oblique G2 comparison proof render completes", Rendered);
    Panel.Expect("The oblique G2 proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
