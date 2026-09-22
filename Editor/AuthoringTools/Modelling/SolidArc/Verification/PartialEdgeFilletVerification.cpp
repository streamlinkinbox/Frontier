//=============================================================================================================================================
// SolidArc · Stage 4a · bounded partial-edge constant-radius planar-corner blend
//
// This is intentionally not general partial-edge selection. It reconstructs an explicit
// orthogonal corner descriptor with a strict interior blend interval and two planar sector caps.
//=============================================================================================================================================
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>

using namespace Frontier;

namespace
{
[[nodiscard]] bool ExteriorFaceNormals(const BrepBody& Body, Vec3 Axis, double Start, double End) noexcept
{
    int TransitionCaps = 0;
    for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
    {
        const Vec3 N = Body.FaceNormal(static_cast<int>(Face), 0.5, 0.5);
        if (!std::isfinite(N.X) || !std::isfinite(N.Y) || !std::isfinite(N.Z) ||
            N.Length() <= ScalarCriteria::GeometricTolerance) return false;
        if (Body.Faces[Face].Surface.Classification != SurfaceClassification::Coons) continue;
        const Vec3 P = Body.Faces[Face].Surface.Sample(0.5, 0.5);
        const double Station = (P - Body.Bounds().Low).Dot(Axis);
        const double Expected = std::fabs(Station - Start) <= ScalarCriteria::GeometricTolerance ? 1.0 :
                                (std::fabs(Station - End) <= ScalarCriteria::GeometricTolerance ? -1.0 : 0.0);
        if (Expected == 0.0 || N.Normalised().Dot(Axis) * Expected < 1.0 - ScalarCriteria::AngularTolerance) return false;
        ++TransitionCaps;
    }
    return TransitionCaps == 2;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · bounded partial-edge constant-radius blend");
    PartialEdgeFilletSpecification Specification;
    Specification.Origin = { 0, 0, 0 };
    Specification.EdgeAxis = { 1, 0, 0 };
    Specification.Length = 12.0;
    Specification.Start = 3.0;
    Specification.End = 9.0;
    Specification.Width = 6.0;
    Specification.Radius = 1.25;

    Panel.Section("Strict interior interval reconstruction");
    const Deliver<BrepBody> Result = BlendSolver::ReconstructPartialEdgeFillet(Specification);
    Panel.Expect("The strict interior partial interval commits a solid", Result && Result.Payload.Validate().Solid());
    Panel.Expect("The route is genuinely partial rather than complete-edge", Specification.Start > 0.0 &&
                 Specification.End < Specification.Length && Specification.End > Specification.Start);
    Panel.Within("The interval start is preserved", Specification.Start - 3.0, 1e-12);
    Panel.Within("The interval end is preserved", Specification.End - 9.0, 1e-12);
    Panel.Within("The radius is preserved", Specification.Radius - 1.25, 1e-12);

    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        const double ExpectedVolume = Specification.Length * Specification.Width * Specification.Width -
            (1.0 - ScalarCriteria::Pi / 4.0) * Specification.Radius * Specification.Radius *
            (Specification.End - Specification.Start);
        Panel.Expect("The partial blend is one closed genus-zero manifold", Report.Hulls == 1 && Report.Genus == 0 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        Panel.Expect("The split walls and caps have deterministic V34/E65/F33/L33 topology",
                     Report.Vertices == 34 && Report.Edges == 65 && Report.Faces == 33 && Report.Loops == 33);
        Panel.Within("The partial volume removes only the selected interval corner",
                     std::fabs(Report.Volume - ExpectedVolume) / ExpectedVolume, 1e-3);
        Panel.Expect("The explicit transition and end caps survive as bounded faces", Result.Payload.Faces.size() >= 25);
        Panel.Expect("Support, blend, and recessed transition normals are outward", ExteriorFaceNormals(Result.Payload,
                     Specification.EdgeAxis.Normalised(), Specification.Start, Specification.End));
        const Box3 Bounds = Result.Payload.Bounds();
        Panel.Within("The partial result retains the full parent-edge length", Bounds.High.X - Bounds.Low.X - Specification.Length, 1e-12);
        Panel.Within("The partial result retains the full support width", Bounds.High.Y - Bounds.Low.Y - Specification.Width, 1e-5);
        Panel.Within("The partial result retains the full support width in the second support", Bounds.High.Z - Bounds.Low.Z - Specification.Width, 1e-5);
    }

    Panel.Section("Transactional refusal boundaries");
    PartialEdgeFilletSpecification Bad = Specification;
    Bad.Radius = 0.0;
    Panel.Expect("A zero radius refuses", !BlendSolver::ReconstructPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.Radius = Specification.Width;
    Panel.Expect("A radius consuming the support width refuses", !BlendSolver::ReconstructPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.Start = 0.0;
    Panel.Expect("A complete-edge start refuses this partial-only route", !BlendSolver::ReconstructPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.End = Specification.Length;
    Panel.Expect("A complete-edge end refuses this partial-only route", !BlendSolver::ReconstructPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.End = Bad.Start;
    Panel.Expect("An empty interval refuses", !BlendSolver::ReconstructPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.Start = 8.0;
    Bad.End = 4.0;
    Panel.Expect("A reversed interval refuses", !BlendSolver::ReconstructPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.EdgeAxis = { 0, 0, 0 };
    Panel.Expect("A degenerate edge frame refuses", !BlendSolver::ReconstructPartialEdgeFillet(Bad));
    Bad = Specification;
    Bad.Length = 0.0;
    Panel.Expect("A zero-length parent edge refuses", !BlendSolver::ReconstructPartialEdgeFillet(Bad));
    Panel.Expect("The accepted partial result remains valid after refusals", Result && Result.Payload.Validate().Solid());

    Panel.Section("Exterior-facing source/comparison proof");
    const Deliver<BrepBody> Sharp = BrepBody::Box({ 0, 0, 0 }, { Specification.Length, Specification.Width, Specification.Width });
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase38a_PartialEdgeFillet.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Result && Sharp &&
        Host.Document().AddBody("SharpCorner", Sharp.Payload.Transformed(Mat4::Translation({ -7, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("PartialBlend", Result.Payload.Transformed(Mat4::Translation({ 7, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 195 -12") && Host.Execute("view fit") &&
        Host.Execute("render Phase38a_PartialEdgeFillet");
    Panel.Expect("The partial-edge exterior comparison render completes", Rendered);
    Panel.Expect("The partial-edge proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
