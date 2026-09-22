//=============================================================================================================================================
// SolidArc · Stage 4g · bounded oblique quadratic partial-edge fillet
//=============================================================================================================================================
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <limits>

using namespace Frontier;

namespace
{
[[nodiscard]] bool BoundaryNormalsAndArcFit(const BrepBody& Body,
                                            const ObliqueQuadraticPartialEdgeFilletSpecification& Specification) noexcept
{
    if (Body.Faces.size() != 20) return false;
    const Vec3 Axis = Specification.EdgeAxis.Normalised();
    const Vec3 A = Specification.SupportA.Normalised();
    const Vec3 B = Specification.SupportB.Normalised();
    const double Theta = std::acos(A.Dot(B));
    const double HalfTheta = Theta * 0.5;
    const Vec3 OuterNormal = (-Axis.Cross(B * Specification.WidthB - A * Specification.WidthA)).Normalised();
    const Vec3 SupportANormal = (-Axis.Cross(A)).Normalised();
    const Vec3 SupportBNormal = (Axis.Cross(B)).Normalised();
    std::vector<Vec3> Expected(20, Vec3{ 0, 0, 0 });
    Expected[0] = SupportANormal;
    Expected[1] = SupportANormal;
    Expected[2] = OuterNormal;
    Expected[3] = SupportBNormal;
    Expected[4] = SupportBNormal;
    Expected[5] = SupportANormal;
    Expected[6] = OuterNormal;
    Expected[7] = SupportBNormal;
    Expected[9] = SupportANormal;
    Expected[10] = SupportANormal;
    Expected[11] = OuterNormal;
    Expected[12] = SupportBNormal;
    Expected[13] = SupportBNormal;
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
        const Vec3 Normal = Body.FaceNormal(static_cast<int>(Face), U, V).Normalised();
        if (!std::isfinite(Normal.X) || !std::isfinite(Normal.Y) || !std::isfinite(Normal.Z) ||
            Normal.Length() <= ScalarCriteria::GeometricTolerance) return false;
        if (Face == 8)
        {
            const Vec3 Point = Surface.Sample(U, V);
            const Vec3 PlanarPoint = Point - Axis * (Point - Specification.Origin).Dot(Axis);
            const double Along = (Point - Specification.Origin).Dot(Axis);
            const double T = ScalarCriteria::Clamp((Along - Specification.Start) /
                                                    (Specification.End - Specification.Start), 0.0, 1.0);
            const Vec3 Centre = Specification.Origin +
                (A + B).Normalised() * (Specification.RadiusLaw.Radius(T) / std::sin(HalfTheta));
            if (Normal.Dot((PlanarPoint - Centre).Normalised()) < 0.97) return false;
            if (!Surface.Rational()) return false;
        }
        else if (Normal.Dot(Expected[Face]) < 0.97) return false;
    }
    return true;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · oblique quadratic partial-edge fillet");
    ObliqueQuadraticPartialEdgeFilletSpecification Specification;
    Specification.Origin = { 0, 0, 0 };
    Specification.EdgeAxis = { 1, 0, 0 };
    Specification.SupportA = { 0, 1, 0 };
    Specification.SupportB = { 0, 0.5, std::sqrt(3.0) * 0.5 };
    Specification.Length = 12.0;
    Specification.Start = 3.0;
    Specification.End = 9.0;
    Specification.WidthA = 6.0;
    Specification.WidthB = 5.0;
    Specification.RadiusLaw = { 0.50, 0.90, 0.70 };

    const Vec3 A = Specification.SupportA.Normalised();
    const Vec3 B = Specification.SupportB.Normalised();
    const double Theta = std::acos(A.Dot(B));
    const double CotHalf = std::cos(Theta * 0.5) / std::sin(Theta * 0.5);
    const double TangentStart = Specification.RadiusLaw.Start * CotHalf;
    const double TangentMiddle = Specification.RadiusLaw.Middle * CotHalf;
    const double TangentEnd = Specification.RadiusLaw.End * CotHalf;
    const Deliver<BrepBody> Result = BlendSolver::ReconstructObliqueQuadraticPartialEdgeFillet(Specification);

    Panel.Section("Genuinely nonlinear oblique selected-interval reconstruction");
    Panel.Expect("The oblique quadratic partial route commits a solid", Result && Result.Payload.Validate().Solid());
    Panel.Expect("The support angle is genuinely oblique", std::fabs(Theta - ScalarCriteria::Pi * 0.5) > 1e-6);
    Panel.Expect("The radius law is genuinely nonlinear", Specification.RadiusLaw.Nonlinear());
    Panel.Expect("The interval is strict interior", Specification.Start > 0.0 &&
                 Specification.Start < Specification.End && Specification.End < Specification.Length);
    Panel.Within("The start radius is exact", Specification.RadiusLaw.Radius(0.0) - 0.50, 1e-12);
    Panel.Within("The middle radius is exact", Specification.RadiusLaw.Radius(0.5) - 0.90, 1e-12);
    Panel.Within("The end radius is exact", Specification.RadiusLaw.Radius(1.0) - 0.70, 1e-12);
    Panel.Within("The start tangent distance is r cot(theta/2)", TangentStart - 0.50 * CotHalf, 1e-12);
    Panel.Within("The middle tangent distance is r cot(theta/2)", TangentMiddle - 0.90 * CotHalf, 1e-12);
    Panel.Within("The end tangent distance is r cot(theta/2)", TangentEnd - 0.70 * CotHalf, 1e-12);
    Panel.Expect("Every sampled tangent point leaves both finite supports",
                 TangentStart < Specification.WidthA && TangentStart < Specification.WidthB &&
                 TangentMiddle < Specification.WidthA && TangentMiddle < Specification.WidthB &&
                 TangentEnd < Specification.WidthA && TangentEnd < Specification.WidthB);

    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        const double SharpArea = 0.5 * Specification.WidthA * Specification.WidthB * std::sin(Theta);
        const double RemovedCoefficient = 0.5 * std::sin(Theta) * (CotHalf * CotHalf + 1.0) -
            0.5 * (ScalarCriteria::Pi - Theta);
        const double ExpectedVolume = Specification.Length * SharpArea - RemovedCoefficient *
            Specification.RadiusLaw.IntegratedSquare(Specification.End - Specification.Start);
        Panel.Expect("The oblique quadratic result has deterministic V20/E38/F20/L20 topology",
                     Report.Vertices == 20 && Report.Edges == 38 && Report.Faces == 20 && Report.Loops == 20 &&
                     Report.Hulls == 1 && Report.Genus == 0 && Report.OpenEdges == 0 &&
                     Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        Panel.Within("The volume follows the integrated oblique removed-corner area",
                     std::fabs(Report.Volume - ExpectedVolume) / ExpectedVolume, 1e-3);
        Panel.Expect("Support, transition, cap and variable-band normals are outward",
                     BoundaryNormalsAndArcFit(Result.Payload, Specification));
        Panel.Expect("The variable oblique fillet band is rational", Result.Payload.Faces[8].Surface.Rational());
    }

    Panel.Section("Transactional nonlinear oblique refusals");
    ObliqueQuadraticPartialEdgeFilletSpecification Bad = Specification;
    Bad.RadiusLaw.Middle = (Bad.RadiusLaw.Start + Bad.RadiusLaw.End) * 0.5;
    Panel.Expect("A linear law refuses the quadratic-only route", !BlendSolver::ReconstructObliqueQuadraticPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.RadiusLaw.Middle = 0.0;
    Panel.Expect("A zero middle radius refuses", !BlendSolver::ReconstructObliqueQuadraticPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.RadiusLaw.Start = -0.1;
    Panel.Expect("A negative radius endpoint refuses", !BlendSolver::ReconstructObliqueQuadraticPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.RadiusLaw = { 5.0, 5.2, 4.8 };
    Panel.Expect("A radius consuming the oblique supports refuses", !BlendSolver::ReconstructObliqueQuadraticPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.Start = 0.0;
    Panel.Expect("A complete-edge start refuses", !BlendSolver::ReconstructObliqueQuadraticPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.End = Bad.Length;
    Panel.Expect("A complete-edge end refuses", !BlendSolver::ReconstructObliqueQuadraticPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.End = Bad.Start;
    Panel.Expect("An empty interval refuses", !BlendSolver::ReconstructObliqueQuadraticPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.SupportB = Specification.SupportA;
    Panel.Expect("Parallel supports refuse", !BlendSolver::ReconstructObliqueQuadraticPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.SupportA = { 1, 1, 0 };
    Panel.Expect("A support direction not perpendicular to the edge refuses", !BlendSolver::ReconstructObliqueQuadraticPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.EdgeAxis = { 0, 0, 0 };
    Panel.Expect("A degenerate edge axis refuses", !BlendSolver::ReconstructObliqueQuadraticPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.RadiusLaw.End = std::numeric_limits<double>::quiet_NaN();
    Panel.Expect("A non-finite radius law refuses", !BlendSolver::ReconstructObliqueQuadraticPartialEdgeFillet(Bad));
    Panel.Expect("The accepted nonlinear result remains valid after refusals", Result && Result.Payload.Validate().Solid());

    Panel.Section("Exterior sharp/oblique-quadratic proof");
    const Vec3 OuterA = Specification.Origin + A * Specification.WidthA;
    const Vec3 OuterB = Specification.Origin + B * Specification.WidthB;
    Deliver<NurbsCurve> SharpProfile = NurbsCurve::Polyline({ Specification.Origin, OuterA, OuterB }, true);
    const Deliver<BrepBody> Sharp = SharpProfile
        ? BrepBody::Extrude(SharpProfile.Payload, Specification.EdgeAxis, Specification.Length)
        : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "sharp oblique profile is degenerate");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase38g_ObliqueQuadraticPartialEdgeFillet.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Result && Sharp &&
        Host.Document().AddBody("SharpObliqueEdge", Sharp.Payload.Transformed(Mat4::Translation({ -8, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("ObliqueQuadraticPartial", Result.Payload.Transformed(Mat4::Translation({ 8, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 195 -12") && Host.Execute("view fit") &&
        Host.Execute("render Phase38g_ObliqueQuadraticPartialEdgeFillet");
    Panel.Expect("The sharp/oblique-quadratic proof render completes", Rendered);
    Panel.Expect("The oblique-quadratic proof PNG is visible", std::filesystem::exists(Proof) &&
                 std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
