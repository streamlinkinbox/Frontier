//=============================================================================================================================================
// SolidArc · Stage 4l · bounded oblique quadratic variable-radius rolling-ball-core G2 transition
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

[[nodiscard]] ObliqueG2RollingBallPlanarCornerSpecification Station(
    const ObliqueVariableG2RollingBallPlanarCornerSpecification& Specification, double T) noexcept
{
    ObliqueG2RollingBallPlanarCornerSpecification Base;
    Base.Origin = Specification.Origin + Specification.EdgeAxis.Normalised() * (Specification.Length * T);
    Base.EdgeAxis = Specification.EdgeAxis;
    Base.SupportA = Specification.SupportA;
    Base.SupportB = Specification.SupportB;
    Base.Length = 1.0;
    Base.WidthA = Specification.WidthA;
    Base.WidthB = Specification.WidthB;
    Base.Radius = Specification.RadiusLaw.Radius(T);
    Base.TransitionAngle = Specification.TransitionAngle;
    return Base;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · oblique quadratic variable-radius rolling-ball-core G2 transition");
    ObliqueVariableG2RollingBallPlanarCornerSpecification Specification;
    Specification.Origin = { 0, 0, 0 };
    Specification.EdgeAxis = { 1, 0, 0 };
    Specification.SupportA = { 0, 1, 0 };
    Specification.SupportB = { 0, 0.5, std::sqrt(3.0) * 0.5 };
    Specification.Length = 12.0;
    Specification.WidthA = 6.0;
    Specification.WidthB = 5.5;
    Specification.RadiusLaw = { 0.45, 0.90, 0.65 };
    Specification.TransitionAngle = ScalarCriteria::Pi / 18.0;

    const Vec3 A = Specification.SupportA.Normalised();
    const Vec3 B = Specification.SupportB.Normalised();
    const double Theta = std::acos(A.Dot(B));
    const double CotHalf = std::cos(Theta * 0.5) / std::sin(Theta * 0.5);
    const Deliver<BrepBody> Result = BlendSolver::ReconstructObliqueVariableG2RollingBallPlanarCorner(Specification);

    Panel.Section("Strict oblique quadratic stations");
    Panel.Expect("The oblique variable G2 route commits a solid", Result && Result.Payload.Validate().Solid());
    Panel.Expect("The support angle remains strict and oblique", Theta > 0.0 && Theta < ScalarCriteria::Pi &&
                 std::fabs(Theta - ScalarCriteria::HalfPi) > 1e-6);
    Panel.Expect("The radius law is genuinely nonlinear and positive",
                 Specification.RadiusLaw.Nonlinear() && Specification.RadiusLaw.Positive());
    Panel.Within("The start radius is exact", Specification.RadiusLaw.Radius(0.0) - 0.45, 1e-12);
    Panel.Within("The middle radius is exact", Specification.RadiusLaw.Radius(0.5) - 0.90, 1e-12);
    Panel.Within("The end radius is exact", Specification.RadiusLaw.Radius(1.0) - 0.65, 1e-12);
    Panel.Expect("The middle station differs from endpoint interpolation",
                 std::fabs(Specification.RadiusLaw.Radius(0.5) -
                           (Specification.RadiusLaw.Radius(0.0) + Specification.RadiusLaw.Radius(1.0)) * 0.5) > 1e-6);
    Panel.Expect("Every station tangent distance r cot(theta/2) clears both supports", [&]() noexcept
    {
        for (int I = 0; I <= 64; ++I)
        {
            const double Radius = Specification.RadiusLaw.Radius(static_cast<double>(I) / 64.0);
            if (Radius * CotHalf >= Specification.WidthA || Radius * CotHalf >= Specification.WidthB) return false;
        }
        return true;
    }());
    for (double T : { 0.0, 0.5, 1.0 })
    {
        const ObliqueG2RollingBallPlanarCornerSpecification S = Station(Specification, T);
        const Deliver<ObliqueG2RollingBallProfile> Profile = BlendSolver::BuildObliqueG2RollingBallProfile(S);
        std::string StationRefusal;
        Panel.Expect("Each station retains an exact oblique G2 profile", Profile &&
                     BlendSolver::ValidateObliqueG2RollingBallProfile(Profile.Payload, S, StationRefusal));
        if (Profile)
        {
            Panel.Within("Station core curvature equals 1/r(t)", Profile.Payload.Pieces[1].Curvature(0.5) - 1.0 / S.Radius, 2e-7);
            Panel.Expect("Station core remains rational", Profile.Payload.Pieces[1].Rational());
        }
    }

    Panel.Section("Oblique topology, normals and integrated volume");
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        ObliqueG2RollingBallPlanarCornerSpecification Unit = Station(Specification, 0.0);
        Unit.Radius = 1.0;
        const Deliver<ObliqueG2RollingBallProfile> UnitProfile = BlendSolver::BuildObliqueG2RollingBallProfile(Unit);
        const double SharpArea = 0.5 * Specification.WidthA * Specification.WidthB * std::sin(Theta);
        const double Expected = Specification.Length * SharpArea -
            BlendSolver::ObliqueG2RollingBallRemovalArea(UnitProfile.Payload) *
            Specification.RadiusLaw.IntegratedSquare(Specification.Length);
        Panel.Expect("Oblique variable surfaces sew as V12/E18/F8/L8 topology",
                     Report.Vertices == 12 && Report.Edges == 18 && Result.Payload.Coedges.size() == 36 &&
                     Report.Faces == 8 && Report.Loops == 8 && Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        Panel.Within("The oblique variable volume follows integrated radius-square removal",
                     std::fabs(Report.Volume - Expected) / Expected, 1e-3);
        Panel.Expect("Every oblique variable face carries an outward normal", OutwardNormals(Result.Payload));
        Panel.Expect("The accepted oblique variable body remains valid", Result.Payload.Validate().Solid());
    }

    Panel.Section("Transactional oblique variable refusals");
    ObliqueVariableG2RollingBallPlanarCornerSpecification Bad = Specification;
    Bad.RadiusLaw.Middle = (Bad.RadiusLaw.Start + Bad.RadiusLaw.End) * 0.5;
    Panel.Expect("A linear law refuses", !BlendSolver::ReconstructObliqueVariableG2RollingBallPlanarCorner(Bad));
    Bad = Specification;
    Bad.RadiusLaw.Middle = 0.0;
    Panel.Expect("A non-positive middle radius refuses", !BlendSolver::ReconstructObliqueVariableG2RollingBallPlanarCorner(Bad));
    Bad = Specification;
    Bad.RadiusLaw = { 4.0, 4.4, 4.1 };
    Panel.Expect("A law consuming the finite oblique supports refuses", !BlendSolver::ReconstructObliqueVariableG2RollingBallPlanarCorner(Bad));
    Bad = Specification;
    Bad.SupportB = Specification.SupportA;
    Panel.Expect("Parallel supports refuse", !BlendSolver::ReconstructObliqueVariableG2RollingBallPlanarCorner(Bad));
    Bad = Specification;
    Bad.SupportB = { 1, 1, 0 };
    Panel.Expect("A support not perpendicular to the edge refuses", !BlendSolver::ReconstructObliqueVariableG2RollingBallPlanarCorner(Bad));
    Bad = Specification;
    Bad.Length = 0.0;
    Panel.Expect("A zero-length edge refuses", !BlendSolver::ReconstructObliqueVariableG2RollingBallPlanarCorner(Bad));
    Panel.Expect("The accepted oblique variable body remains transactional", Result && Result.Payload.Validate().Solid());

    Panel.Section("Distinct oblique variable-versus-constant proof");
    ObliqueG2RollingBallPlanarCornerSpecification Constant = Station(Specification, 0.0);
    const Deliver<BrepBody> Reference = BlendSolver::ReconstructObliqueG2RollingBallPlanarCorner(Constant);
    Panel.Expect("The constant oblique comparison commits", Reference && Reference.Payload.Validate().Solid());
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase38l_ObliqueVariableG2.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Result && Reference &&
        Host.Document().AddBody("ObliqueVariableG2", Result.Payload.Transformed(Mat4::Translation({ -7, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("ObliqueConstantG2", Reference.Payload.Transformed(Mat4::Translation({ 7, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 190 -10") && Host.Execute("view fit") &&
        Host.Execute("render Phase38l_ObliqueVariableG2");
    Panel.Expect("The oblique variable/constant proof render completes", Rendered);
    Panel.Expect("The oblique variable proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
