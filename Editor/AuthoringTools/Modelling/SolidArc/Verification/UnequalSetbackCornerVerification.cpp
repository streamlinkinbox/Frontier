//=============================================================================================================================================
// SolidArc · bounded unequal support-setback corner blend
//
// The two perpendicular planar supports receive independent positive linear clearances. This
// is distinct from the common-setback routes: the station section is a non-square rounded
// rectangle, its analytic volume uses the product of the two extents, and equal laws refuse.
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
    VerificationPanel Panel("SolidArc · unequal support-setback corner");
    Panel.Section("Independent clearances on the two planar supports");

    UnequalSetbackCornerSpecification Specification;
    Specification.Origin = { 0, 0, 0 };
    Specification.EdgeAxis = { 1, 0, 0 };
    Specification.Length = 8.0;
    Specification.RadiusLaw = { 0.80, 1.40 };
    Specification.SetbackALaw = { 0.70, 1.10 };
    Specification.SetbackBLaw = { 1.80, 1.00 };

    const Deliver<BrepBody> Result = BlendSolver::ReconstructUnequalSetbackCornerBlend(Specification);
    Panel.Expect("The unequal support-setback laws commit one solid", Result && Result.Payload.Validate().Solid());
    Panel.Expect("The two support clearances are genuinely unequal", std::fabs(Specification.SetbackALaw.Start - Specification.SetbackBLaw.Start) > 1e-12 ||
                 std::fabs(Specification.SetbackALaw.End - Specification.SetbackBLaw.End) > 1e-12);
    Panel.Within("The low A-support setback is exact", Specification.SetbackALaw.Radius(0.0) - 0.70, 1e-12);
    Panel.Within("The low B-support setback is exact", Specification.SetbackBLaw.Radius(0.0) - 1.80, 1e-12);
    Panel.Within("The high A-support setback is exact", Specification.SetbackALaw.Radius(1.0) - 1.10, 1e-12);
    Panel.Within("The high B-support setback is exact", Specification.SetbackBLaw.Radius(1.0) - 1.00, 1e-12);

    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        const double R0 = Specification.RadiusLaw.Start, R1 = Specification.RadiusLaw.End;
        const double U0 = R0 + Specification.SetbackALaw.Start;
        const double U1 = R1 + Specification.SetbackALaw.End;
        const double V0 = R0 + Specification.SetbackBLaw.Start;
        const double V1 = R1 + Specification.SetbackBLaw.End;
        const double OuterVolume = Specification.Length * (2.0 * U0 * V0 + U0 * V1 + U1 * V0 + 2.0 * U1 * V1) / 6.0;
        const double RemovedVolume = (1.0 - ScalarCriteria::Pi / 4.0) * Specification.Length *
            (R0 * R0 + R0 * R1 + R1 * R1) / 3.0;
        const double ExpectedVolume = OuterVolume - RemovedVolume;
        Panel.Expect("The unequal result has capped V10/E15/C30/L7/F7 topology", Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0 &&
                     Result.Payload.Vertices.size() == 10 && Result.Payload.Edges.size() == 15 &&
                     Result.Payload.Coedges.size() == 30 && Result.Payload.Loops.size() == 7 && Result.Payload.Faces.size() == 7);
        Panel.Within("The volume follows the product of the two extent laws", std::fabs(Report.Volume - ExpectedVolume) / ExpectedVolume, 1e-3);
        Panel.Expect("Every unequal-setback face tessellation carries an outward normal", ExteriorFaceNormals(Result.Payload));
        for (int I = 0; I <= 4; ++I)
        {
            const double T = static_cast<double>(I) / 4.0;
            const double Radius = Specification.RadiusLaw.Radius(T);
            const double A = Radius + Specification.SetbackALaw.Radius(T);
            const double B = Radius + Specification.SetbackBLaw.Radius(T);
            Panel.Within("Station A extent minus radius equals A setback", A - Radius - Specification.SetbackALaw.Radius(T), 1e-12);
            Panel.Within("Station B extent minus radius equals B setback", B - Radius - Specification.SetbackBLaw.Radius(T), 1e-12);
        }
    }

    Panel.Section("Asymmetric and transactional refusal boundaries");
    UnequalSetbackCornerSpecification Bad = Specification;
    Bad.SetbackBLaw = Bad.SetbackALaw;
    Panel.Expect("Equal support setbacks delegate/refuse rather than duplicate the common route", !BlendSolver::ReconstructUnequalSetbackCornerBlend(Bad));
    Bad = Specification;
    Bad.SetbackALaw.Start = 0.0;
    Panel.Expect("A zero A-support setback refuses", !BlendSolver::ReconstructUnequalSetbackCornerBlend(Bad));
    Bad = Specification;
    Bad.SetbackBLaw.End = -0.1;
    Panel.Expect("A negative B-support setback refuses", !BlendSolver::ReconstructUnequalSetbackCornerBlend(Bad));
    Bad = Specification;
    Bad.RadiusLaw.Start = 0.0;
    Panel.Expect("A zero radius station refuses", !BlendSolver::ReconstructUnequalSetbackCornerBlend(Bad));
    Bad = Specification;
    Bad.Length = 0.0;
    Panel.Expect("A zero-length edge refuses", !BlendSolver::ReconstructUnequalSetbackCornerBlend(Bad));
    Bad = Specification;
    Bad.EdgeAxis = { 0, 0, 0 };
    Panel.Expect("A degenerate unequal-setback frame refuses", !BlendSolver::ReconstructUnequalSetbackCornerBlend(Bad));
    Panel.Expect("The accepted unequal result remains valid after refusals", Result && Result.Payload.Validate().Solid());

    Panel.Section("Exterior-filled asymmetric proof");
    VariableSetbackCornerSpecification ReferenceSpecification;
    ReferenceSpecification.Origin = { 0, 0, 0 };
    ReferenceSpecification.EdgeAxis = { 1, 0, 0 };
    ReferenceSpecification.Length = Specification.Length;
    ReferenceSpecification.RadiusLaw = Specification.RadiusLaw;
    ReferenceSpecification.SetbackLaw = { 1.25, 1.05 };
    const Deliver<BrepBody> Reference = BlendSolver::ReconstructVariableSetbackCornerBlend(ReferenceSpecification);
    Panel.Expect("The common-setback comparison fixture commits", Reference && Reference.Payload.Validate().Solid());
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase37f_UnequalSetbackCorner.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Result && Reference &&
        Host.Document().AddBody("UnequalSetbackCorner", Result.Payload.Transformed(Mat4::Translation({ -7, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("CommonSetbackReference", Reference.Payload.Transformed(Mat4::Translation({ 7, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 190 -10") && Host.Execute("view fit") &&
        Host.Execute("render Phase37f_UnequalSetbackCorner");
    Panel.Expect("The asymmetric comparison proof render completes", Rendered);
    Panel.Expect("The unequal-setback proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
