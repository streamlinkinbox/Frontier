//=============================================================================================================================================
// SolidArc · Stage 4c · bounded quadratic-radius partial-edge blend
//
// This is a distinct extension of Stage 4a: the radius is genuinely nonlinear over one explicit
// strict-interior interval. It is not arbitrary variable-radius support handling.
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
[[nodiscard]] bool NormalsAndTransitionCaps(const BrepBody& Body, Vec3 Axis, double Start, double End) noexcept
{
    int TransitionCaps = 0;
    for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
    {
        const NurbsSurface& Surface = Body.Faces[Face].Surface;
        const double U = 0.37 * (Surface.DomainEndU() - Surface.DomainStartU()) + Surface.DomainStartU();
        const double V = 0.53 * (Surface.DomainEndV() - Surface.DomainStartV()) + Surface.DomainStartV();
        const Vec3 Normal = Body.FaceNormal(static_cast<int>(Face), U, V);
        if (!std::isfinite(Normal.X) || !std::isfinite(Normal.Y) || !std::isfinite(Normal.Z) ||
            Normal.Length() <= ScalarCriteria::GeometricTolerance) return false;
        if (Surface.Classification != SurfaceClassification::Coons) continue;
        const Vec3 Point = Surface.Sample(U, V);
        const double Station = Point.X;
        const double Expected = std::fabs(Station - Start) <= ScalarCriteria::GeometricTolerance ? 1.0 :
                                (std::fabs(Station - End) <= ScalarCriteria::GeometricTolerance ? -1.0 : 0.0);
        if (Expected == 0.0 || Normal.Normalised().Dot(Axis) * Expected < 1.0 - ScalarCriteria::AngularTolerance) return false;
        ++TransitionCaps;
    }
    return TransitionCaps == 2;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · bounded quadratic-radius partial-edge blend");
    QuadraticPartialEdgeFilletSpecification Specification;
    Specification.Origin = { 0, 0, 0 };
    Specification.EdgeAxis = { 1, 0, 0 };
    Specification.Length = 12.0;
    Specification.Start = 3.0;
    Specification.End = 9.0;
    Specification.Width = 6.0;
    Specification.RadiusLaw = { 0.80, 1.50, 1.20 };

    Panel.Section("Genuinely nonlinear selected-interval reconstruction");
    const Deliver<BrepBody> Result = BlendSolver::ReconstructQuadraticPartialEdgeFillet(Specification);
    Panel.Expect("The quadratic partial-edge route commits a solid", Result && Result.Payload.Validate().Solid());
    Panel.Expect("The radius law is genuinely nonlinear", Specification.RadiusLaw.Nonlinear());
    Panel.Within("The interval-start radius is exact", Specification.RadiusLaw.Radius(0.0) - 0.80, 1e-12);
    Panel.Within("The interval-middle radius is exact", Specification.RadiusLaw.Radius(0.5) - 1.50, 1e-12);
    Panel.Within("The interval-end radius is exact", Specification.RadiusLaw.Radius(1.0) - 1.20, 1e-12);
    Panel.Expect("The interval remains strict and interior", Specification.Start > 0.0 && Specification.End < Specification.Length &&
                 Specification.End > Specification.Start);

    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        const double ExpectedVolume = Specification.Length * Specification.Width * Specification.Width -
            (1.0 - ScalarCriteria::Pi / 4.0) * Specification.RadiusLaw.IntegratedSquare(Specification.End - Specification.Start);
        Panel.Expect("The nonlinear partial blend has deterministic V34/E67/F35/L35 topology",
                     Report.Vertices == 34 && Report.Edges == 67 && Report.Faces == 35 && Report.Loops == 35 &&
                     Report.Hulls == 1 && Report.Genus == 0 && Report.OpenEdges == 0 &&
                     Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        Panel.Within("The volume follows the integrated quadratic radius square",
                     std::fabs(Report.Volume - ExpectedVolume) / ExpectedVolume, 1e-3);
        Panel.Expect("Support, blend and recessed transition normals are outward",
                     NormalsAndTransitionCaps(Result.Payload, Specification.EdgeAxis.Normalised(), Specification.Start, Specification.End));
        for (int I = 0; I <= 4; ++I)
        {
            const double T = static_cast<double>(I) / 4.0;
            const double Radius = Specification.RadiusLaw.Radius(T);
            Panel.Expect("Sampled nonlinear radius stays positive and below support width", Radius > 0.0 && Radius < Specification.Width);
        }
    }

    Panel.Section("Transactional nonlinear and partial-edge refusals");
    QuadraticPartialEdgeFilletSpecification Bad = Specification;
    Bad.RadiusLaw.Middle = (Bad.RadiusLaw.Start + Bad.RadiusLaw.End) * 0.5;
    Panel.Expect("A linear law refuses the quadratic-only route", !BlendSolver::ReconstructQuadraticPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.RadiusLaw.Middle = 0.0;
    Panel.Expect("A zero middle radius refuses", !BlendSolver::ReconstructQuadraticPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.RadiusLaw.Start = -0.1;
    Panel.Expect("A negative radius endpoint refuses", !BlendSolver::ReconstructQuadraticPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.RadiusLaw = { 5.9, 6.0, 5.8 };
    Panel.Expect("A radius consuming the planar support refuses", !BlendSolver::ReconstructQuadraticPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.Start = 0.0;
    Panel.Expect("A complete-edge start refuses this partial-only route", !BlendSolver::ReconstructQuadraticPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.End = Bad.Length;
    Panel.Expect("A complete-edge end refuses this partial-only route", !BlendSolver::ReconstructQuadraticPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.End = Bad.Start;
    Panel.Expect("An empty interval refuses", !BlendSolver::ReconstructQuadraticPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.EdgeAxis = { 0, 0, 0 };
    Panel.Expect("A degenerate edge axis refuses", !BlendSolver::ReconstructQuadraticPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.Length = 0.0;
    Panel.Expect("A zero-length parent edge refuses", !BlendSolver::ReconstructQuadraticPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.RadiusLaw.End = std::numeric_limits<double>::quiet_NaN();
    Panel.Expect("A non-finite radius law refuses", !BlendSolver::ReconstructQuadraticPartialEdgeFillet(Bad));
    Panel.Expect("The accepted nonlinear result remains valid after refusals", Result && Result.Payload.Validate().Solid());

    Panel.Section("Exterior sharp/nonlinear comparison proof");
    const Deliver<BrepBody> Sharp = BrepBody::Box({ 0, 0, 0 }, { Specification.Length, Specification.Width, Specification.Width });
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase38c_QuadraticPartialEdgeFillet.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Result && Sharp &&
        Host.Document().AddBody("SharpCorner", Sharp.Payload.Transformed(Mat4::Translation({ -7, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("QuadraticPartial", Result.Payload.Transformed(Mat4::Translation({ 7, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 195 -12") && Host.Execute("view fit") &&
        Host.Execute("render Phase38c_QuadraticPartialEdgeFillet");
    Panel.Expect("The nonlinear partial-edge exterior proof render completes", Rendered);
    Panel.Expect("The nonlinear partial-edge proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
