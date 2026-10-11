//=============================================================================================================================================
// SolidArc · Phase 40 · bounded extruded convex-prism shell
//=============================================================================================================================================
#include "Kernel/FaceEditSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <limits>
#include <vector>

using namespace Frontier;

namespace
{
constexpr double Radius = 5.0;
constexpr double Height = 8.0;
constexpr double Thickness = 0.65;

[[nodiscard]] Deliver<BrepBody> HexPrism() noexcept
{
    std::vector<Vec3> Points;
    for (int I = 0; I < 6; ++I)
    {
        const double A = ScalarCriteria::TwoPi * static_cast<double>(I) / 6.0;
        Points.push_back({ Radius * std::cos(A), Radius * std::sin(A), 0.0 });
    }
    const Deliver<NurbsCurve> Profile = NurbsCurve::Polyline(Points, true);
    return Profile ? BrepBody::Extrude(Profile.Payload, Vec3::UnitZ(), Height)
                   : Deliver<BrepBody>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
}

[[nodiscard]] int FaceToward(const BrepBody& Body, Vec3 Direction) noexcept
{
    int Best = -1; double BestDot = -2.0;
    for (int I = 0; I < static_cast<int>(Body.Faces.size()); ++I)
    {
        const BrepFace& F = Body.Faces[I];
        const Vec3 N = Body.FaceNormal(I, 0.5 * (F.Surface.DomainStartU() + F.Surface.DomainEndU()),
                                       0.5 * (F.Surface.DomainStartV() + F.Surface.DomainEndV())).Normalised();
        const double D = N.Dot(Direction.Normalised());
        if (D > BestDot) { BestDot = D; Best = I; }
    }
    return Best;
}

[[nodiscard]] bool SameSource(const BrepBody& Body, const BrepBody& Snapshot, const BodyReport& Before) noexcept
{
    const BodyReport After = Body.Validate();
    return After.Vertices == Before.Vertices && After.Edges == Before.Edges && After.Faces == Before.Faces &&
           After.OpenEdges == Before.OpenEdges && After.NonManifoldEdges == Before.NonManifoldEdges &&
           After.MisorientedEdges == Before.MisorientedEdges && std::fabs(After.Volume - Before.Volume) <= 1e-12 &&
           Body.Vertices.size() == Snapshot.Vertices.size() && Body.Edges.size() == Snapshot.Edges.size() &&
           Body.Faces.size() == Snapshot.Faces.size();
}

[[nodiscard]] Deliver<BrepBody> TrianglePrism() noexcept
{
    const std::vector<Vec3> Points{ { 0, 0, 0 }, { 6, 0, 0 }, { 2, 4, 0 } };
    const Deliver<NurbsCurve> Profile = NurbsCurve::Polyline(Points, true);
    return Profile ? BrepBody::Extrude(Profile.Payload, Vec3::UnitZ(), Height)
                   : Deliver<BrepBody>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
}

[[nodiscard]] Deliver<BrepBody> ConcaveHexPrism() noexcept
{
    const std::vector<Vec3> Points{ { -4, -3, 0 }, { 4, -3, 0 }, { 1, 0, 0 }, { 4, 3, 0 }, { -4, 3, 0 }, { -1, 0, 0 } };
    const Deliver<NurbsCurve> Profile = NurbsCurve::Polyline(Points, true);
    return Profile ? BrepBody::Extrude(Profile.Payload, Vec3::UnitZ(), Height)
                   : Deliver<BrepBody>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 40 · bounded extruded convex-prism shell");
    const auto SourceDeliver = HexPrism();
    Panel.Expect("The distinct regular hexagonal prism source is constructed", static_cast<bool>(SourceDeliver));
    const BrepBody Source = SourceDeliver.Payload;
    const BrepBody Snapshot = Source;
    const BodyReport Before = Source.Validate();
    const int Top = FaceToward(Source, Vec3::UnitZ());
    const int Bottom = FaceToward(Source, -Vec3::UnitZ());
    Panel.Expect("The source has V12/E18/C36/L8/F8 topology", SourceDeliver && Before.Solid() && Before.Hulls == 1 &&
                 Before.Genus == 0 && Source.Vertices.size() == 12 && Source.Edges.size() == 18 &&
                 Source.Coedges.size() == 36 && Source.Loops.size() == 8 && Source.Faces.size() == 8);
    Panel.Expect("The upper and lower planar caps resolve independently", Top >= 0 && Bottom >= 0 && Top != Bottom);

    Panel.Section("Exact non-box shell reconstruction");
    const auto Shell = SourceDeliver ? FaceEditSolver::ShellExtrudedConvexPrism(Source, Top, Thickness)
                                     : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no prism source");
    Panel.Expect("The selected cap produces a closed shell solid", Shell && Shell.Payload.Validate().Solid());
    if (Shell)
    {
        const BodyReport R = Shell.Payload.Validate();
        Panel.Expect("The shell has V24/E42/C84/L20/F20 topology", R.Hulls == 1 && R.Genus == 0 &&
                     Shell.Payload.Vertices.size() == 24 && Shell.Payload.Edges.size() == 42 &&
                     Shell.Payload.Coedges.size() == 84 && Shell.Payload.Loops.size() == 20 &&
                     Shell.Payload.Faces.size() == 20 && R.OpenEdges == 0 && R.NonManifoldEdges == 0 &&
                     R.MisorientedEdges == 0);
        const double OuterArea = 3.0 * std::sqrt(3.0) * Radius * Radius / 2.0;
        const double InnerRadius = Radius - Thickness / std::cos(ScalarCriteria::Pi / 6.0);
        const double InnerArea = 3.0 * std::sqrt(3.0) * InnerRadius * InnerRadius / 2.0;
        const double Expected = OuterArea * Height - InnerArea * (Height - Thickness);
        Panel.Within("The shell volume follows outer prism minus inner cavity", std::fabs(R.Volume - Expected), 1e-1);
        int Planes = 0, Extrusions = 0, Rims = 0;
        for (const BrepFace& F : Shell.Payload.Faces)
        {
            if (F.Surface.Classification == SurfaceClassification::Plane) ++Planes;
            else if (F.Surface.Classification == SurfaceClassification::Extrusion) ++Extrusions;
            else if (F.Surface.Classification == SurfaceClassification::Ruled) ++Rims;
        }
        Panel.Expect("The shell retains two caps, twelve walls, and six top-rim faces", Planes == 2 && Extrusions == 12 && Rims == 6);
        Panel.Expect("The separate shell reconstruction preserves the source", SameSource(Source, Snapshot, Before));
    }
    const auto DispatchShell = FaceEditSolver::Shell(Source, Top, Thickness);
    Panel.Expect("The public shell dispatch reaches the non-box route", DispatchShell && DispatchShell.Payload.Validate().Solid());
    Panel.Expect("The upper cap is the only supported opening selection", !FaceEditSolver::ShellExtrudedConvexPrism(Source, Bottom, Thickness));

    Panel.Section("Non-box shell refusal boundaries");
    const auto Box = BrepBody::Box({ -4, -4, 0 }, { 4, 4, Height });
    Panel.Expect("A rectangular box remains on the separate box route", Box && !FaceEditSolver::ShellExtrudedConvexPrism(Box.Payload, FaceToward(Box.Payload, Vec3::UnitZ()), Thickness));
    const auto Triangle = TrianglePrism();
    Panel.Expect("A triangular prism refuses the six-sided route", Triangle && !FaceEditSolver::ShellExtrudedConvexPrism(Triangle.Payload, FaceToward(Triangle.Payload, Vec3::UnitZ()), Thickness));
    const auto Concave = ConcaveHexPrism();
    Panel.Expect("A concave six-edge prism refuses", Concave && !FaceEditSolver::ShellExtrudedConvexPrism(Concave.Payload, FaceToward(Concave.Payload, Vec3::UnitZ()), Thickness));
    const auto Cylinder = BrepBody::Cylinder({ 0, 0, 0 }, Vec3::UnitZ(), Radius, Height);
    Panel.Expect("A curved cylinder refuses the prism shell route", Cylinder && !FaceEditSolver::ShellExtrudedConvexPrism(Cylinder.Payload, FaceToward(Cylinder.Payload, Vec3::UnitZ()), Thickness));
    Panel.Expect("Zero, negative, non-finite, and consuming thicknesses refuse",
                 !FaceEditSolver::ShellExtrudedConvexPrism(Source, Top, 0.0) &&
                 !FaceEditSolver::ShellExtrudedConvexPrism(Source, Top, -0.1) &&
                 !FaceEditSolver::ShellExtrudedConvexPrism(Source, Top, std::numeric_limits<double>::infinity()) &&
                 !FaceEditSolver::ShellExtrudedConvexPrism(Source, Top, Height * 0.5));
    BrepBody Malformed = Source;
    Malformed.Edges[0].Coedges.push_back(Malformed.Edges[0].Coedges.front());
    Panel.Expect("A malformed prism refuses transactionally", !FaceEditSolver::ShellExtrudedConvexPrism(Malformed, Top, Thickness));
    Panel.Expect("All refusal paths preserve the source", SameSource(Source, Snapshot, Before));

    Panel.Section("Distinct non-box shell proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase40_ExtrudedConvexPrismShell.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Shell &&
        Host.Document().AddBody("SharpHexPrism", Source.Transformed(Mat4::Translation({ -8, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("ShelledHexPrism", Shell.Payload.Transformed(Mat4::Translation({ 8, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 35 -18") && Host.Execute("view fit") &&
        Host.Execute("render Phase40_ExtrudedConvexPrismShell");
    Panel.Expect("The non-box shell proof render completes", Rendered);
    Panel.Expect("The non-box shell proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
