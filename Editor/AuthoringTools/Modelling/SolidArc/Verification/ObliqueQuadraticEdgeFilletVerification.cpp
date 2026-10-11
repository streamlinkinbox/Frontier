//=============================================================================================================================================
// SolidArc · Stage 4h · bounded oblique quadratic full-edge fillet
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
                                            const ObliqueQuadraticEdgeFilletSpecification& Specification) noexcept
{
    if (Body.Faces.size() != 6) return false;
    const Vec3 Axis = Specification.EdgeAxis.Normalised();
    const Vec3 A = Specification.SupportA.Normalised();
    const Vec3 B = Specification.SupportB.Normalised();
    const double Theta = std::acos(A.Dot(B));
    const double HalfTheta = Theta * 0.5;
    const Vec3 OuterNormal = (-Axis.Cross(B * Specification.WidthB - A * Specification.WidthA)).Normalised();
    const Vec3 SupportANormal = (-Axis.Cross(A)).Normalised();
    const Vec3 SupportBNormal = (Axis.Cross(B)).Normalised();
    const Vec3 Expected[6] = { SupportANormal, OuterNormal, SupportBNormal,
                               Vec3{}, -Axis, Axis };
    for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
    {
        const NurbsSurface& Surface = Body.Faces[Face].Surface;
        const double U = 0.37 * (Surface.DomainEndU() - Surface.DomainStartU()) + Surface.DomainStartU();
        const double V = 0.53 * (Surface.DomainEndV() - Surface.DomainStartV()) + Surface.DomainStartV();
        const Vec3 Normal = Body.FaceNormal(static_cast<int>(Face), U, V).Normalised();
        if (!std::isfinite(Normal.X) || !std::isfinite(Normal.Y) || !std::isfinite(Normal.Z) ||
            Normal.Length() <= ScalarCriteria::GeometricTolerance) return false;
        if (Face == 3)
        {
            const Vec3 Point = Surface.Sample(U, V);
            const Vec3 PlanarPoint = Point - Axis * (Point - Specification.Origin).Dot(Axis);
            const double T = ScalarCriteria::Clamp((Point - Specification.Origin).Dot(Axis) /
                                                    Specification.Length, 0.0, 1.0);
            const Vec3 Centre = Specification.Origin + (A + B).Normalised() *
                (Specification.RadiusLaw.Radius(T) / std::sin(HalfTheta));
            if (Normal.Dot((PlanarPoint - Centre).Normalised()) < 0.97 || !Surface.Rational()) return false;
        }
        else if (Normal.Dot(Expected[Face]) < 0.97) return false;
    }
    return true;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · oblique quadratic full-edge fillet");
    ObliqueQuadraticEdgeFilletSpecification Specification;
    Specification.Origin = { 0, 0, 0 };
    Specification.EdgeAxis = { 1, 0, 0 };
    Specification.SupportA = { 0, 1, 0 };
    Specification.SupportB = { 0, 0.5, std::sqrt(3.0) * 0.5 };
    Specification.Length = 12.0;
    Specification.WidthA = 6.0;
    Specification.WidthB = 5.0;
    Specification.RadiusLaw = { 0.50, 0.90, 0.70 };

    const Vec3 A = Specification.SupportA.Normalised();
    const Vec3 B = Specification.SupportB.Normalised();
    const double Theta = std::acos(A.Dot(B));
    const double CotHalf = std::cos(Theta * 0.5) / std::sin(Theta * 0.5);
    const Deliver<BrepBody> Result = BlendSolver::ReconstructObliqueQuadraticEdgeFillet(Specification);

    Panel.Section("Genuinely nonlinear complete-edge reconstruction");
    Panel.Expect("The oblique quadratic full-edge route commits a solid", Result && Result.Payload.Validate().Solid());
    Panel.Expect("The support angle is genuinely oblique", std::fabs(Theta - ScalarCriteria::Pi * 0.5) > 1e-6);
    Panel.Expect("The radius law is genuinely nonlinear", Specification.RadiusLaw.Nonlinear());
    Panel.Within("The start radius is exact", Specification.RadiusLaw.Radius(0.0) - 0.50, 1e-12);
    Panel.Within("The middle radius is exact", Specification.RadiusLaw.Radius(0.5) - 0.90, 1e-12);
    Panel.Within("The end radius is exact", Specification.RadiusLaw.Radius(1.0) - 0.70, 1e-12);
    Panel.Within("The start tangent distance is r cot(theta/2)",
                 Specification.RadiusLaw.Start * CotHalf - 0.50 * CotHalf, 1e-12);
    Panel.Within("The middle tangent distance is r cot(theta/2)",
                 Specification.RadiusLaw.Middle * CotHalf - 0.90 * CotHalf, 1e-12);
    Panel.Within("The end tangent distance is r cot(theta/2)",
                 Specification.RadiusLaw.End * CotHalf - 0.70 * CotHalf, 1e-12);
    Panel.Expect("Every sampled tangent point leaves both finite supports",
                 Specification.RadiusLaw.Start * CotHalf < Specification.WidthA &&
                 Specification.RadiusLaw.Middle * CotHalf < Specification.WidthA &&
                 Specification.RadiusLaw.End * CotHalf < Specification.WidthA &&
                 Specification.RadiusLaw.Start * CotHalf < Specification.WidthB &&
                 Specification.RadiusLaw.Middle * CotHalf < Specification.WidthB &&
                 Specification.RadiusLaw.End * CotHalf < Specification.WidthB);

    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        const double SharpArea = 0.5 * Specification.WidthA * Specification.WidthB * std::sin(Theta);
        const double RemovedCoefficient = 0.5 * std::sin(Theta) * (CotHalf * CotHalf + 1.0) -
            0.5 * (ScalarCriteria::Pi - Theta);
        const double ExpectedVolume = Specification.Length * SharpArea - RemovedCoefficient *
            Specification.RadiusLaw.IntegratedSquare(Specification.Length);
        Panel.Expect("The oblique quadratic result has deterministic V8/E12/F6/L6 topology",
                     Report.Vertices == 8 && Report.Edges == 12 && Report.Faces == 6 && Report.Loops == 6 &&
                     Report.Hulls == 1 && Report.Genus == 0 && Report.OpenEdges == 0 &&
                     Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        Panel.Within("The volume follows the integrated oblique removed-corner area",
                     std::fabs(Report.Volume - ExpectedVolume) / ExpectedVolume, 1e-3);
        Panel.Expect("Support, cap and variable-band normals are outward",
                     BoundaryNormalsAndArcFit(Result.Payload, Specification));
        Panel.Expect("The complete variable oblique fillet band is rational", Result.Payload.Faces[3].Surface.Rational());
    }

    Panel.Section("Transactional nonlinear oblique refusals");
    ObliqueQuadraticEdgeFilletSpecification Bad = Specification;
    Bad.RadiusLaw.Middle = (Bad.RadiusLaw.Start + Bad.RadiusLaw.End) * 0.5;
    Panel.Expect("A linear law refuses the quadratic-only route", !BlendSolver::ReconstructObliqueQuadraticEdgeFillet(Bad));
    Bad = Specification;
    Bad.RadiusLaw.Middle = 0.0;
    Panel.Expect("A zero middle radius refuses", !BlendSolver::ReconstructObliqueQuadraticEdgeFillet(Bad));
    Bad = Specification;
    Bad.RadiusLaw.Start = -0.1;
    Panel.Expect("A negative radius endpoint refuses", !BlendSolver::ReconstructObliqueQuadraticEdgeFillet(Bad));
    Bad = Specification;
    Bad.RadiusLaw = { 5.0, 5.2, 4.8 };
    Panel.Expect("A radius consuming the oblique supports refuses", !BlendSolver::ReconstructObliqueQuadraticEdgeFillet(Bad));
    Bad = Specification;
    Bad.SupportB = Specification.SupportA;
    Panel.Expect("Parallel supports refuse", !BlendSolver::ReconstructObliqueQuadraticEdgeFillet(Bad));
    Bad = Specification;
    Bad.SupportA = { 1, 1, 0 };
    Panel.Expect("A support direction not perpendicular to the edge refuses", !BlendSolver::ReconstructObliqueQuadraticEdgeFillet(Bad));
    Bad = Specification;
    Bad.EdgeAxis = { 0, 0, 0 };
    Panel.Expect("A degenerate edge axis refuses", !BlendSolver::ReconstructObliqueQuadraticEdgeFillet(Bad));
    Bad = Specification;
    Bad.RadiusLaw.End = std::numeric_limits<double>::quiet_NaN();
    Panel.Expect("A non-finite radius law refuses", !BlendSolver::ReconstructObliqueQuadraticEdgeFillet(Bad));
    Panel.Expect("The accepted nonlinear result remains valid after refusals", Result && Result.Payload.Validate().Solid());

    Panel.Section("Exterior sharp/oblique-quadratic proof");
    const Vec3 OuterA = Specification.Origin + A * Specification.WidthA;
    const Vec3 OuterB = Specification.Origin + B * Specification.WidthB;
    Deliver<NurbsCurve> SharpProfile = NurbsCurve::Polyline({ Specification.Origin, OuterA, OuterB }, true);
    const Deliver<BrepBody> Sharp = SharpProfile
        ? BrepBody::Extrude(SharpProfile.Payload, Specification.EdgeAxis, Specification.Length)
        : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "sharp oblique profile is degenerate");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase38h_ObliqueQuadraticEdgeFillet.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Result && Sharp &&
        Host.Document().AddBody("SharpObliqueEdge", Sharp.Payload.Transformed(Mat4::Translation({ -8, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("ObliqueQuadraticEdge", Result.Payload.Transformed(Mat4::Translation({ 8, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 195 -12") && Host.Execute("view fit") &&
        Host.Execute("render Phase38h_ObliqueQuadraticEdgeFillet");
    Panel.Expect("The sharp/oblique-quadratic proof render completes", Rendered);
    Panel.Expect("The oblique-quadratic proof PNG is visible", std::filesystem::exists(Proof) &&
                 std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
