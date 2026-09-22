//=============================================================================================================================================
// SolidArc · Stage 3e · bounded nonlinear unequal support-setback corner
//=============================================================================================================================================
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <array>
#include <cmath>
#include <filesystem>

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

[[nodiscard]] double IntegratedProduct(const QuadraticRadiusLaw& A,
                                       const QuadraticRadiusLaw& B,
                                       double Length) noexcept
{
    const std::array<double, 3> CA{ A.Start, -3.0 * A.Start + 4.0 * A.Middle - A.End,
                                    2.0 * A.Start - 4.0 * A.Middle + 2.0 * A.End };
    const std::array<double, 3> CB{ B.Start, -3.0 * B.Start + 4.0 * B.Middle - B.End,
                                    2.0 * B.Start - 4.0 * B.Middle + 2.0 * B.End };
    double Integral = 0.0;
    for (int I = 0; I <= 2; ++I) for (int J = 0; J <= 2; ++J)
        Integral += CA[I] * CB[J] * Length / static_cast<double>(I + J + 1);
    return Integral;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · nonlinear unequal support-setback corner");
    Panel.Section("Two independent genuinely nonlinear support setbacks");

    NonlinearUnequalSetbackCornerSpecification Specification;
    Specification.Origin = { 0, 0, 0 };
    Specification.EdgeAxis = { 1, 0, 0 };
    Specification.Length = 8.0;
    Specification.RadiusLaw = { 0.80, 1.20, 1.40 };
    Specification.SetbackALaw = { 0.70, 1.10, 1.30 };
    Specification.SetbackBLaw = { 1.80, 1.00, 1.40 };

    const Deliver<BrepBody> Result = BlendSolver::ReconstructNonlinearUnequalSetbackCornerBlend(Specification);
    Panel.Expect("The nonlinear unequal laws commit one solid", Result && Result.Payload.Validate().Solid());
    Panel.Expect("The A-support setback is genuinely nonlinear", Specification.SetbackALaw.Nonlinear());
    Panel.Expect("The B-support setback is genuinely nonlinear", Specification.SetbackBLaw.Nonlinear());
    Panel.Expect("The independent support setbacks are not equal", std::fabs(Specification.SetbackALaw.Start - Specification.SetbackBLaw.Start) > 1e-12 ||
                 std::fabs(Specification.SetbackALaw.Middle - Specification.SetbackBLaw.Middle) > 1e-12 ||
                 std::fabs(Specification.SetbackALaw.End - Specification.SetbackBLaw.End) > 1e-12);
    Panel.Expect("The radius law remains positive", Specification.RadiusLaw.Positive());

    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        const QuadraticRadiusLaw OuterA{
            Specification.RadiusLaw.Start + Specification.SetbackALaw.Start,
            Specification.RadiusLaw.Middle + Specification.SetbackALaw.Middle,
            Specification.RadiusLaw.End + Specification.SetbackALaw.End };
        const QuadraticRadiusLaw OuterB{
            Specification.RadiusLaw.Start + Specification.SetbackBLaw.Start,
            Specification.RadiusLaw.Middle + Specification.SetbackBLaw.Middle,
            Specification.RadiusLaw.End + Specification.SetbackBLaw.End };
        const double ExpectedVolume = IntegratedProduct(OuterA, OuterB, Specification.Length) -
            (1.0 - ScalarCriteria::Pi / 4.0) * Specification.RadiusLaw.IntegratedSquare(Specification.Length);
        Panel.Expect("The nonlinear unequal result has capped V10/E15/C30/L7/F7 topology",
                     Report.Hulls == 1 && Report.Genus == 0 && Report.OpenEdges == 0 &&
                     Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0 &&
                     Result.Payload.Vertices.size() == 10 && Result.Payload.Edges.size() == 15 &&
                     Result.Payload.Coedges.size() == 30 && Result.Payload.Loops.size() == 7 &&
                     Result.Payload.Faces.size() == 7);
        Panel.Within("The volume follows the exact product-of-quadratic-extents identity",
                     std::fabs(Report.Volume - ExpectedVolume) / ExpectedVolume, 1e-3);
        Panel.Expect("Every nonlinear unequal face carries an outward normal", ExteriorFaceNormals(Result.Payload));
        for (int I = 0; I <= 4; ++I)
        {
            const double T = static_cast<double>(I) / 4.0;
            const double Radius = Specification.RadiusLaw.Radius(T);
            const double A = Radius + Specification.SetbackALaw.Radius(T);
            const double B = Radius + Specification.SetbackBLaw.Radius(T);
            Panel.Within("Station A extent minus radius equals A setback",
                         A - Radius - Specification.SetbackALaw.Radius(T), 1e-12);
            Panel.Within("Station B extent minus radius equals B setback",
                         B - Radius - Specification.SetbackBLaw.Radius(T), 1e-12);
            Panel.Expect("Every sampled nonlinear station leaves positive corner width", A > Radius && B > Radius);
        }
    }

    Panel.Section("Nonlinear unequal transactional refusals");
    NonlinearUnequalSetbackCornerSpecification Bad = Specification;
    Bad.SetbackALaw = { 0.70, 0.90, 1.10 };
    Panel.Expect("A linear A-support setback refuses", !BlendSolver::ReconstructNonlinearUnequalSetbackCornerBlend(Bad));
    Bad = Specification;
    Bad.SetbackBLaw = Bad.SetbackALaw;
    Panel.Expect("Equal nonlinear support setbacks refuse as a duplicate common route", !BlendSolver::ReconstructNonlinearUnequalSetbackCornerBlend(Bad));
    Bad = Specification;
    Bad.SetbackALaw.Middle = -0.1;
    Panel.Expect("A negative nonlinear A-support station refuses", !BlendSolver::ReconstructNonlinearUnequalSetbackCornerBlend(Bad));
    Bad = Specification;
    Bad.SetbackBLaw.End = 0.0;
    Panel.Expect("A zero nonlinear B-support station refuses", !BlendSolver::ReconstructNonlinearUnequalSetbackCornerBlend(Bad));
    Bad = Specification;
    Bad.RadiusLaw.Start = 0.0;
    Panel.Expect("A zero radius station refuses", !BlendSolver::ReconstructNonlinearUnequalSetbackCornerBlend(Bad));
    Bad = Specification;
    Bad.Length = 0.0;
    Panel.Expect("A zero-length edge refuses", !BlendSolver::ReconstructNonlinearUnequalSetbackCornerBlend(Bad));
    Bad = Specification;
    Bad.EdgeAxis = { 0, 0, 0 };
    Panel.Expect("A degenerate nonlinear unequal frame refuses", !BlendSolver::ReconstructNonlinearUnequalSetbackCornerBlend(Bad));
    Panel.Expect("The accepted nonlinear unequal result remains valid after refusals", Result && Result.Payload.Validate().Solid());

    Panel.Section("Exterior-filled nonlinear asymmetric proof");
    UnequalSetbackCornerSpecification ReferenceSpecification;
    ReferenceSpecification.Origin = { 0, 0, 0 };
    ReferenceSpecification.EdgeAxis = { 1, 0, 0 };
    ReferenceSpecification.Length = Specification.Length;
    ReferenceSpecification.RadiusLaw = { 0.80, 1.40 };
    ReferenceSpecification.SetbackALaw = { 0.70, 1.10 };
    ReferenceSpecification.SetbackBLaw = { 1.80, 1.00 };
    const Deliver<BrepBody> Reference = BlendSolver::ReconstructUnequalSetbackCornerBlend(ReferenceSpecification);
    Panel.Expect("The independent linear comparison fixture commits", Reference && Reference.Payload.Validate().Solid());
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase37g_NonlinearUnequalSetbackCorner.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Result && Reference &&
        Host.Document().AddBody("NonlinearUnequalSetback", Result.Payload.Transformed(Mat4::Translation({ -7, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("LinearUnequalReference", Reference.Payload.Transformed(Mat4::Translation({ 7, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 190 -10") && Host.Execute("view fit") &&
        Host.Execute("render Phase37g_NonlinearUnequalSetbackCorner");
    Panel.Expect("The nonlinear asymmetric comparison proof render completes", Rendered);
    Panel.Expect("The nonlinear unequal proof PNG is visible", std::filesystem::exists(Proof) &&
                 std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
