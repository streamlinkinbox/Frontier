//=============================================================================================================================================
// SolidArc · Stage 4k · bounded quadratic variable-radius rolling-ball-core G2 transition
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

[[nodiscard]] G2RollingBallPlanarCornerSpecification Station(
    const VariableG2RollingBallPlanarCornerSpecification& Specification, double T) noexcept
{
    G2RollingBallPlanarCornerSpecification Result;
    Result.Origin = Specification.Origin + Specification.EdgeAxis.Normalised() * (Specification.Length * T);
    Result.EdgeAxis = Specification.EdgeAxis;
    Result.SupportA = Specification.SupportA;
    Result.SupportB = Specification.SupportB;
    Result.Length = 1.0;
    Result.Width = Specification.Width;
    Result.Radius = Specification.RadiusLaw.Radius(T);
    Result.TransitionAngle = Specification.TransitionAngle;
    return Result;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · quadratic variable-radius rolling-ball-core G2 transition");
    VariableG2RollingBallPlanarCornerSpecification Specification;
    Specification.Origin = { 0, 0, 0 };
    Specification.EdgeAxis = { 1, 0, 0 };
    Specification.SupportA = { 0, 1, 0 };
    Specification.SupportB = { 0, 0, 1 };
    Specification.Length = 12.0;
    Specification.Width = 5.0;
    Specification.RadiusLaw = { 0.60, 1.10, 0.80 };
    Specification.TransitionAngle = ScalarCriteria::Pi / 8.0;

    const Deliver<BrepBody> Result = BlendSolver::ReconstructVariableG2RollingBallPlanarCorner(Specification);
    Panel.Section("Genuinely nonlinear stations and G2 section acceptance");
    Panel.Expect("The variable G2 route commits a solid", Result && Result.Payload.Validate().Solid());
    Panel.Expect("The radius law is genuinely nonlinear", Specification.RadiusLaw.Nonlinear());
    Panel.Expect("The radius law remains positive", Specification.RadiusLaw.Positive());
    Panel.Within("The start radius is exact", Specification.RadiusLaw.Radius(0.0) - 0.60, 1e-12);
    Panel.Within("The middle radius is exact", Specification.RadiusLaw.Radius(0.5) - 1.10, 1e-12);
    Panel.Within("The end radius is exact", Specification.RadiusLaw.Radius(1.0) - 0.80, 1e-12);
    Panel.Expect("The middle station differs from endpoint interpolation",
                 std::fabs(Specification.RadiusLaw.Radius(0.5) -
                           (Specification.RadiusLaw.Radius(0.0) + Specification.RadiusLaw.Radius(1.0)) * 0.5) > 1e-6);
    for (double T : { 0.0, 0.5, 1.0 })
    {
        const G2RollingBallPlanarCornerSpecification S = Station(Specification, T);
        const Deliver<G2RollingBallProfile> Profile = BlendSolver::BuildG2RollingBallProfile(S);
        std::string StationRefusal;
        Panel.Expect("Every quadratic station retains its exact rational G2 profile", Profile &&
                     BlendSolver::ValidateG2RollingBallProfile(Profile.Payload, S, StationRefusal));
        if (Profile)
        {
            Panel.Within("Station core curvature equals 1/r(t)", Profile.Payload.Pieces[1].Curvature(0.5) - 1.0 / S.Radius, 2e-7);
            Panel.Expect("Station core remains rational", Profile.Payload.Pieces[1].Rational());
        }
    }
    Panel.Expect("Every sampled tangent distance remains below the support width", [&]() noexcept
    {
        for (int I = 0; I <= 64; ++I)
            if (Specification.RadiusLaw.Radius(static_cast<double>(I) / 64.0) >= Specification.Width) return false;
        return true;
    }());

    Panel.Section("Topology, normals and integrated radius-square volume");
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        G2RollingBallPlanarCornerSpecification Unit = Station(Specification, 0.0);
        Unit.Radius = 1.0;
        const Deliver<G2RollingBallProfile> UnitProfile = BlendSolver::BuildG2RollingBallProfile(Unit);
        const double Expected = Specification.Length * Specification.Width * Specification.Width -
            BlendSolver::G2RollingBallRemovalArea(UnitProfile.Payload) *
            Specification.RadiusLaw.IntegratedSquare(Specification.Length);
        Panel.Expect("Variable station surfaces sew as V12/E18/F8/L8 topology",
                     Report.Vertices == 14 && Report.Edges == 21 && Result.Payload.Coedges.size() == 42 &&
                     Report.Faces == 9 && Report.Loops == 9 && Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        Panel.Within("The variable volume follows integrated radius-square removal",
                     std::fabs(Report.Volume - Expected) / Expected, 1e-3);
        Panel.Expect("Every variable station face carries an outward normal", OutwardNormals(Result.Payload));
        Panel.Expect("The accepted variable body remains valid after measurement", Result.Payload.Validate().Solid());
    }

    Panel.Section("Transactional variable-law and frame refusals");
    VariableG2RollingBallPlanarCornerSpecification Bad = Specification;
    Bad.RadiusLaw.Middle = (Bad.RadiusLaw.Start + Bad.RadiusLaw.End) * 0.5;
    Panel.Expect("A linear law refuses the nonlinear route", !BlendSolver::ReconstructVariableG2RollingBallPlanarCorner(Bad));
    Bad = Specification;
    Bad.RadiusLaw.Middle = 0.0;
    Panel.Expect("A non-positive middle radius refuses", !BlendSolver::ReconstructVariableG2RollingBallPlanarCorner(Bad));
    Bad = Specification;
    Bad.RadiusLaw = { 5.0, 5.2, 4.8 };
    Panel.Expect("A law consuming the support refuses", !BlendSolver::ReconstructVariableG2RollingBallPlanarCorner(Bad));
    Bad = Specification;
    Bad.SupportB = { 0, 0.5, 0.866025403784 };
    Panel.Expect("An oblique support pair refuses this perpendicular route", !BlendSolver::ReconstructVariableG2RollingBallPlanarCorner(Bad));
    Bad = Specification;
    Bad.EdgeAxis = { 0, 0, 0 };
    Panel.Expect("A degenerate edge refuses", !BlendSolver::ReconstructVariableG2RollingBallPlanarCorner(Bad));
    Bad = Specification;
    Bad.Width = 0.5;
    Panel.Expect("An insufficient support width refuses", !BlendSolver::ReconstructVariableG2RollingBallPlanarCorner(Bad));
    Bad = Specification;
    Bad.Length = 0.0;
    Panel.Expect("A zero-length edge refuses", !BlendSolver::ReconstructVariableG2RollingBallPlanarCorner(Bad));
    Panel.Expect("The accepted variable body remains transactional after refusals", Result && Result.Payload.Validate().Solid());

    Panel.Section("Distinct exterior variable-versus-constant proof");
    G2RollingBallPlanarCornerSpecification Constant;
    Constant.Origin = { 0, 0, 0 };
    Constant.EdgeAxis = Specification.EdgeAxis;
    Constant.SupportA = Specification.SupportA;
    Constant.SupportB = Specification.SupportB;
    Constant.Length = Specification.Length;
    Constant.Width = Specification.Width;
    Constant.Radius = Specification.RadiusLaw.Start;
    Constant.TransitionAngle = Specification.TransitionAngle;
    const Deliver<BrepBody> Reference = BlendSolver::ReconstructG2RollingBallPlanarCorner(Constant);
    Panel.Expect("The constant rolling-core comparison commits", Reference && Reference.Payload.Validate().Solid());
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase38k_VariableG2RollingBall.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Result && Reference &&
        Host.Document().AddBody("VariableG2Core", Result.Payload.Transformed(Mat4::Translation({ -7, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("ConstantG2Core", Reference.Payload.Transformed(Mat4::Translation({ 7, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 190 -10") && Host.Execute("view fit") &&
        Host.Execute("render Phase38k_VariableG2RollingBall");
    Panel.Expect("The variable/constant comparison proof render completes", Rendered);
    Panel.Expect("The variable G2 proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
