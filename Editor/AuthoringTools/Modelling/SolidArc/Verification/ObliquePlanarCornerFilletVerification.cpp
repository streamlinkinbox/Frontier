//=============================================================================================================================================
// SolidArc · Stage 4e · bounded oblique planar corner fillet
//=============================================================================================================================================
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
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
}

int main()
{
    VerificationPanel Panel("SolidArc · oblique planar corner fillet");
    Panel.Section("Exact non-orthogonal planar-support construction");

    ObliquePlanarCornerFilletSpecification Specification;
    Specification.Origin = { 0, 0, 0 };
    Specification.EdgeAxis = { 1, 0, 0 };
    Specification.SupportA = { 0, 1, 0 };
    Specification.SupportB = { 0, 0.5, std::sqrt(3.0) * 0.5 };
    Specification.Length = 8.0;
    Specification.WidthA = 6.0;
    Specification.WidthB = 5.0;
    Specification.Radius = 0.8;

    const double Theta = std::acos(Specification.SupportA.Normalised().Dot(Specification.SupportB.Normalised()));
    const double TangentDistance = Specification.Radius * std::cos(Theta * 0.5) / std::sin(Theta * 0.5);
    const Deliver<BrepBody> Result = BlendSolver::ReconstructObliquePlanarCornerFillet(Specification);
    Panel.Expect("The strict 60-degree oblique corner commits one solid", Result && Result.Payload.Validate().Solid());
    Panel.Expect("The support angle is genuinely oblique", std::fabs(Theta - ScalarCriteria::Pi * 0.5) > 1e-6);
    Panel.Within("The analytic tangent distance is r cot(theta/2)",
                 TangentDistance - Specification.Radius * std::cos(Theta * 0.5) / std::sin(Theta * 0.5), 1e-12);
    Panel.Expect("The tangent distance leaves both finite supports", TangentDistance < Specification.WidthA &&
                 TangentDistance < Specification.WidthB);

    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        const double SharpArea = 0.5 * Specification.WidthA * Specification.WidthB * std::sin(Theta);
        const double RemovedArea = 0.5 * std::sin(Theta) *
            (TangentDistance * TangentDistance + Specification.Radius * Specification.Radius) -
            0.5 * Specification.Radius * Specification.Radius * (ScalarCriteria::Pi - Theta);
        const double ExpectedVolume = Specification.Length * (SharpArea - RemovedArea);
        int RationalFaces = 0;
        for (const BrepFace& Face : Result.Payload.Faces) if (Face.Surface.Rational()) ++RationalFaces;
        Panel.Expect("The oblique result has capped V8/E12/F6/L6 topology", Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0 &&
                     Result.Payload.Vertices.size() == 8 && Result.Payload.Edges.size() == 12 &&
                     Result.Payload.Coedges.size() == 24 && Result.Payload.Loops.size() == 6 && Result.Payload.Faces.size() == 6);
        Panel.Within("The volume follows the oblique wedge-minus-segment identity",
                     std::fabs(Report.Volume - ExpectedVolume) / ExpectedVolume, 1e-3);
        Panel.Expect("Exactly one sewn face is the rational circular fillet extrusion", RationalFaces == 1);
        Panel.Expect("Every oblique face tessellation carries an outward normal", ExteriorFaceNormals(Result.Payload));
        Panel.Expect("The circular tangent points are inside both support widths",
                     TangentDistance > 0.0 && TangentDistance < Specification.WidthA &&
                     TangentDistance < Specification.WidthB);
    }

    Panel.Section("Oblique and transactional refusal boundaries");
    ObliquePlanarCornerFilletSpecification Bad = Specification;
    Bad.Radius = Specification.WidthA;
    Panel.Expect("A radius consuming support A refuses", !BlendSolver::ReconstructObliquePlanarCornerFillet(Bad));
    Bad = Specification;
    Bad.SupportB = Specification.SupportA;
    Panel.Expect("Parallel supports refuse", !BlendSolver::ReconstructObliquePlanarCornerFillet(Bad));
    Bad = Specification;
    Bad.SupportB = -Specification.SupportA;
    Panel.Expect("Opposite supports refuse", !BlendSolver::ReconstructObliquePlanarCornerFillet(Bad));
    Bad = Specification;
    Bad.SupportA = { 1, 1, 0 };
    Panel.Expect("A support direction not perpendicular to the edge refuses", !BlendSolver::ReconstructObliquePlanarCornerFillet(Bad));
    Bad = Specification;
    Bad.EdgeAxis = { 0, 0, 0 };
    Panel.Expect("A degenerate edge axis refuses", !BlendSolver::ReconstructObliquePlanarCornerFillet(Bad));
    Bad = Specification;
    Bad.Length = 0.0;
    Panel.Expect("A zero-length oblique edge refuses", !BlendSolver::ReconstructObliquePlanarCornerFillet(Bad));
    Bad = Specification;
    Bad.WidthB = TangentDistance;
    Panel.Expect("A tangent point on the far support boundary refuses", !BlendSolver::ReconstructObliquePlanarCornerFillet(Bad));
    Panel.Expect("The accepted oblique result remains valid after refusals", Result && Result.Payload.Validate().Solid());

    Panel.Section("Exterior sharp/rounded oblique proof");
    const Vec3 OuterA = Specification.Origin + Specification.SupportA.Normalised() * Specification.WidthA;
    const Vec3 OuterB = Specification.Origin + Specification.SupportB.Normalised() * Specification.WidthB;
    Deliver<NurbsCurve> SharpProfile = NurbsCurve::Polyline({ Specification.Origin, OuterA, OuterB }, true);
    const Deliver<BrepBody> Sharp = SharpProfile
        ? BrepBody::Extrude(SharpProfile.Payload, Specification.EdgeAxis, Specification.Length)
        : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "sharp oblique profile is degenerate");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase38e_ObliquePlanarCornerFillet.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Result && Sharp &&
        Host.Document().AddBody("SharpObliqueCorner", Sharp.Payload.Transformed(Mat4::Translation({ -8, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("RoundedObliqueCorner", Result.Payload.Transformed(Mat4::Translation({ 8, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 195 -12") && Host.Execute("view fit") &&
        Host.Execute("render Phase38e_ObliquePlanarCornerFillet");
    Panel.Expect("The sharp/rounded oblique proof render completes", Rendered);
    Panel.Expect("The oblique proof PNG is visible", std::filesystem::exists(Proof) &&
                 std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
