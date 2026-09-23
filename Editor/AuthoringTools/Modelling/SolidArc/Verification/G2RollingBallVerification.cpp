//=============================================================================================================================================
// SolidArc · Stage 4i · bounded rolling-ball-core G2 planar corner transition
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

[[nodiscard]] double CoreRadiusResidual(const G2RollingBallProfile& Profile) noexcept
{
    const Vec3 Centre = Profile.Origin + (Profile.AxisU + Profile.AxisV) * Profile.Radius;
    double Worst = 0.0;
    for (int I = 0; I <= 16; ++I)
    {
        const Vec3 Point = Profile.Pieces[1].Sample(static_cast<double>(I) / 16.0);
        Worst = std::max(Worst, std::fabs(Point.Distance(Centre) - Profile.Radius));
    }
    return Worst;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · rolling-ball-core G2 planar corner transition");
    G2RollingBallPlanarCornerSpecification Specification;
    Specification.Origin = { 0, 0, 0 };
    Specification.EdgeAxis = { 1, 0, 0 };
    Specification.SupportA = { 0, 1, 0 };
    Specification.SupportB = { 0, 0, 1 };
    Specification.Length = 8.0;
    Specification.Width = 5.0;
    Specification.Radius = 1.5;
    Specification.TransitionAngle = ScalarCriteria::Pi / 8.0;

    const Deliver<G2RollingBallProfile> Profile = BlendSolver::BuildG2RollingBallProfile(Specification);
    Panel.Section("Curvature-matched transitions around an exact rolling-ball core");
    Panel.Expect("The bounded profile builds three exact sections", Profile && Profile.Payload.Pieces.size() == 3);
    std::string Refusal;
    Panel.Expect("The profile validates its shared boundaries and G2 joins",
                 Profile && BlendSolver::ValidateG2RollingBallProfile(Profile.Payload, Specification, Refusal));
    if (Profile)
    {
        const G2RollingBallProfile& P = Profile.Payload;
        Panel.Expect("Support transitions are quintic", P.Pieces[0].Degree == 5 && P.Pieces[2].Degree == 5);
        Panel.Expect("The middle section is an exact rational circular core", P.Pieces[1].Degree == 2 && P.Pieces[1].Rational());
        Panel.Within("The circular core radius is exact", CoreRadiusResidual(P), 1e-10);
        Panel.Within("The low support join has zero curvature", P.Pieces[0].Curvature(0.0), 1e-8);
        Panel.Within("The high support join has zero curvature", P.Pieces[2].Curvature(1.0), 1e-8);
        Panel.Within("The low transition matches rolling-ball curvature", P.Pieces[0].Curvature(1.0) - 1.0 / Specification.Radius, 2e-7);
        Panel.Within("The exact core has rolling-ball curvature", P.Pieces[1].Curvature(0.5) - 1.0 / Specification.Radius, 2e-7);
        Panel.Within("The high transition matches rolling-ball curvature", P.Pieces[2].Curvature(0.0) - 1.0 / Specification.Radius, 2e-7);
        Panel.Expect("The profile line integral removes a positive corner area",
                     BlendSolver::G2RollingBallRemovalArea(P) > 0.0);
        Panel.Expect("The support endpoint tangents are aligned", P.Pieces[0].Tangent(0.0).Dot(-P.AxisV) > 1.0 - ScalarCriteria::AngularTolerance &&
                     P.Pieces[2].Tangent(1.0).Dot(P.AxisU) > 1.0 - ScalarCriteria::AngularTolerance);
    }

    Panel.Section("Closed reconstruction, topology, normals and volume");
    const Deliver<BrepBody> Result = BlendSolver::ReconstructG2RollingBallPlanarCorner(Specification);
    Panel.Expect("The G2 rolling-core route commits a solid", Result && Result.Payload.Validate().Solid());
    if (Result && Profile)
    {
        const BodyReport Report = Result.Payload.Validate();
        const double ExpectedVolume = Specification.Length *
            (Specification.Width * Specification.Width - BlendSolver::G2RollingBallRemovalArea(Profile.Payload));
        Panel.Expect("Split transitions sew as deterministic V14/E21/F9/L9 topology",
                     Report.Vertices == 14 && Report.Edges == 21 && Result.Payload.Coedges.size() == 42 &&
                     Report.Faces == 9 && Report.Loops == 9 && Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        Panel.Within("The volume follows the profile Green line integral",
                     std::fabs(Report.Volume - ExpectedVolume) / ExpectedVolume, 1e-3);
        Panel.Expect("Every face carries an outward normal", OutwardNormals(Result.Payload));
        Panel.Expect("The accepted body remains valid after all profile queries", Result.Payload.Validate().Solid());
    }

    Panel.Section("Transactional refusal boundaries");
    G2RollingBallPlanarCornerSpecification Bad = Specification;
    Bad.Radius = 0.0;
    Panel.Expect("A zero radius refuses", !BlendSolver::ReconstructG2RollingBallPlanarCorner(Bad));
    Bad = Specification;
    Bad.Radius = Specification.Width;
    Panel.Expect("A radius consuming the support refuses", !BlendSolver::ReconstructG2RollingBallPlanarCorner(Bad));
    Bad = Specification;
    Bad.TransitionAngle = 0.0;
    Panel.Expect("A zero transition refuses", !BlendSolver::ReconstructG2RollingBallPlanarCorner(Bad));
    Bad = Specification;
    Bad.TransitionAngle = ScalarCriteria::HalfPi * 0.5;
    Panel.Expect("A transition with no circular core refuses", !BlendSolver::ReconstructG2RollingBallPlanarCorner(Bad));
    Bad = Specification;
    Bad.SupportB = Specification.SupportA;
    Panel.Expect("Non-perpendicular supports refuse", !BlendSolver::ReconstructG2RollingBallPlanarCorner(Bad));
    Bad = Specification;
    Bad.EdgeAxis = Specification.SupportA;
    Panel.Expect("A support direction consumed by the edge refuses", !BlendSolver::ReconstructG2RollingBallPlanarCorner(Bad));
    Bad = Specification;
    Bad.Length = 0.0;
    Panel.Expect("A zero-length edge refuses", !BlendSolver::ReconstructG2RollingBallPlanarCorner(Bad));
    Bad = Specification;
    Bad.Width = 1.0;
    Panel.Expect("A width smaller than the radius refuses", !BlendSolver::ReconstructG2RollingBallPlanarCorner(Bad));
    Panel.Expect("The accepted body remains transactional after refusals", Result && Result.Payload.Validate().Solid());

    Panel.Section("Distinct exterior comparison proof");
    VariableSetbackCornerSpecification Rolling;
    Rolling.Origin = { 0, 0, 0 };
    Rolling.EdgeAxis = { 1, 0, 0 };
    Rolling.Length = Specification.Length;
    Rolling.RadiusLaw = { Specification.Radius, Specification.Radius };
    Rolling.SetbackLaw = { Specification.Width - Specification.Radius,
                           Specification.Width - Specification.Radius };
    const Deliver<BrepBody> Reference = BlendSolver::ReconstructVariableSetbackCornerBlend(Rolling);
    Panel.Expect("The pure rolling reference fixture commits", Reference && Reference.Payload.Validate().Solid());
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase38i_G2RollingBall.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Result && Reference &&
        Host.Document().AddBody("G2RollingCore", Result.Payload.Transformed(Mat4::Translation({ -7, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("PureRollingReference", Reference.Payload.Transformed(Mat4::Translation({ 7, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 190 -10") && Host.Execute("view fit") &&
        Host.Execute("render Phase38i_G2RollingBall");
    Panel.Expect("The G2/core comparison proof render completes", Rendered);
    Panel.Expect("The G2/core proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
