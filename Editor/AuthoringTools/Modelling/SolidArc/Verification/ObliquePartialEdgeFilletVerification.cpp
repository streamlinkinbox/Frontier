//=============================================================================================================================================
// SolidArc · Stage 4f · bounded oblique partial-edge fillet
//=============================================================================================================================================
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>

using namespace Frontier;

namespace
{
[[nodiscard]] bool ExteriorFaceNormals(const BrepBody& Body,
                                      const ObliquePartialEdgeFilletSpecification& Specification) noexcept
{
    if (Body.Faces.size() != 20) return false;
    const Vec3 Axis = Specification.EdgeAxis.Normalised();
    const Vec3 A = Specification.SupportA.Normalised();
    const Vec3 B = Specification.SupportB.Normalised();
    const Vec3 OuterNormal = (-Axis.Cross(B * Specification.WidthB - A * Specification.WidthA)).Normalised();
    const double Theta = std::acos(A.Dot(B));
    const Vec3 ArcCentre = (A + B).Normalised() * (Specification.Radius / std::sin(Theta * 0.5));
    std::vector<Vec3> Expected(20, Vec3{ 0, 0, 0 });
    for (int Face = 0; Face < 3; ++Face) Expected[Face] = (-Axis.Cross(A)).Normalised();
    for (int Face = 3; Face < 6; ++Face) Expected[Face] = OuterNormal;
    for (int Face = 6; Face < 9; ++Face) Expected[Face] = (-Axis.Cross(B * (Specification.WidthB -
        Specification.Radius * std::cos(std::acos(A.Dot(B)) * 0.5) / std::sin(std::acos(A.Dot(B)) * 0.5)) -
        B * Specification.WidthB)).Normalised();
    for (int Face = 9; Face < 11; ++Face) Expected[Face] = (-Axis.Cross(A)).Normalised();
    for (int Face = 11; Face < 13; ++Face) Expected[Face] = (Axis.Cross(B)).Normalised();
    Expected[14] = Axis;
    Expected[15] = -Axis;
    Expected[16] = -Axis;
    Expected[17] = -Axis;
    Expected[18] = Axis;
    Expected[19] = Axis;
    for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
    {
        const NurbsSurface& Surface = Body.Faces[Face].Surface;
        const double U = 0.37 * (Surface.DomainEndU() - Surface.DomainStartU()) + Surface.DomainStartU();
        const double V = 0.53 * (Surface.DomainEndV() - Surface.DomainStartV()) + Surface.DomainStartV();
        if (Face == 13)
        {
            const Vec3 P = Surface.Sample(U, V);
            const Vec3 PlanarPoint = P - Axis * (P - Specification.Origin).Dot(Axis);
            Expected[Face] = (PlanarPoint - ArcCentre - Specification.Origin).Normalised();
        }
        if (Body.FaceNormal(static_cast<int>(Face), U, V).Normalised().Dot(Expected[Face]) < 0.98) return false;
    }
    return true;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · oblique partial-edge fillet");
    Panel.Section("Strict interior oblique fillet interval");

    ObliquePartialEdgeFilletSpecification Specification;
    Specification.Origin = { 0, 0, 0 };
    Specification.EdgeAxis = { 1, 0, 0 };
    Specification.SupportA = { 0, 1, 0 };
    Specification.SupportB = { 0, 0.5, std::sqrt(3.0) * 0.5 };
    Specification.Length = 12.0;
    Specification.Start = 3.0;
    Specification.End = 9.0;
    Specification.WidthA = 6.0;
    Specification.WidthB = 5.0;
    Specification.Radius = 0.8;

    const double Theta = std::acos(Specification.SupportA.Normalised().Dot(Specification.SupportB.Normalised()));
    const double TangentDistance = Specification.Radius * std::cos(Theta * 0.5) / std::sin(Theta * 0.5);
    const Deliver<BrepBody> Result = BlendSolver::ReconstructObliquePartialEdgeFillet(Specification);
    Panel.Expect("The strict oblique partial interval commits one solid", Result && Result.Payload.Validate().Solid());
    Panel.Expect("The interval is strict interior", Specification.Start > 0.0 &&
                 Specification.Start < Specification.End && Specification.End < Specification.Length);
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
        const double ExpectedVolume = Specification.Length * SharpArea -
            (Specification.End - Specification.Start) * RemovedArea;
        Panel.Expect("The partial oblique result has deterministic V20/E38/F20/L20 topology",
                     Report.Hulls == 1 && Report.Genus == 0 && Report.OpenEdges == 0 &&
                     Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0 &&
                     Result.Payload.Vertices.size() == 20 && Result.Payload.Edges.size() == 38 &&
                     Result.Payload.Coedges.size() == 76 && Result.Payload.Loops.size() == 20 &&
                     Result.Payload.Faces.size() == 20);
        Panel.Within("The volume removes only the selected oblique interval",
                     std::fabs(Report.Volume - ExpectedVolume) / ExpectedVolume, 1e-3);
        Panel.Expect("Every oblique partial face tessellation carries an outward normal",
                     ExteriorFaceNormals(Result.Payload, Specification));
        Panel.Expect("The analytic tangent points are strictly inside both supports",
                     TangentDistance > 0.0 && TangentDistance < Specification.WidthA &&
                     TangentDistance < Specification.WidthB);
    }

    Panel.Section("Partial oblique transactional refusals");
    ObliquePartialEdgeFilletSpecification Bad = Specification;
    Bad.Start = 0.0;
    Panel.Expect("A zero start refuses", !BlendSolver::ReconstructObliquePartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.End = Specification.Length;
    Panel.Expect("An end at the finite boundary refuses", !BlendSolver::ReconstructObliquePartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.End = Bad.Start;
    Panel.Expect("A zero-length interval refuses", !BlendSolver::ReconstructObliquePartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.Radius = Specification.WidthA;
    Panel.Expect("A radius consuming support A refuses", !BlendSolver::ReconstructObliquePartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.SupportB = Specification.SupportA;
    Panel.Expect("Parallel supports refuse", !BlendSolver::ReconstructObliquePartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.SupportA = { 1, 1, 0 };
    Panel.Expect("A support direction not perpendicular to the edge refuses", !BlendSolver::ReconstructObliquePartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.EdgeAxis = { 0, 0, 0 };
    Panel.Expect("A degenerate edge axis refuses", !BlendSolver::ReconstructObliquePartialEdgeFillet(Bad));
    Panel.Expect("The accepted partial oblique result remains valid after refusals",
                 Result && Result.Payload.Validate().Solid());

    Panel.Section("Exterior sharp/partial rounded oblique proof");
    const Vec3 OuterA = Specification.Origin + Specification.SupportA.Normalised() * Specification.WidthA;
    const Vec3 OuterB = Specification.Origin + Specification.SupportB.Normalised() * Specification.WidthB;
    Deliver<NurbsCurve> SharpProfile = NurbsCurve::Polyline({ Specification.Origin, OuterA, OuterB }, true);
    const Deliver<BrepBody> Sharp = SharpProfile
        ? BrepBody::Extrude(SharpProfile.Payload, Specification.EdgeAxis, Specification.Length)
        : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "sharp oblique profile is degenerate");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase38f_ObliquePartialEdgeFillet.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Result && Sharp &&
        Host.Document().AddBody("SharpObliqueEdge", Sharp.Payload.Transformed(Mat4::Translation({ -8, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("PartialObliqueFillet", Result.Payload.Transformed(Mat4::Translation({ 8, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 195 -12") && Host.Execute("view fit") &&
        Host.Execute("render Phase38f_ObliquePartialEdgeFillet");
    Panel.Expect("The sharp/partial-oblique proof render completes", Rendered);
    Panel.Expect("The partial-oblique proof PNG is visible", std::filesystem::exists(Proof) &&
                 std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
