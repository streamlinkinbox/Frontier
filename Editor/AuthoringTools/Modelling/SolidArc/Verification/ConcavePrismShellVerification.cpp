//=============================================================================================================================================
// SolidArc · Phase 48 · bounded orthogonal concave-prism shell
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
constexpr double Height = 6.0;
constexpr double Thickness = 0.75;
constexpr double OuterArea = 61.0;
constexpr double InnerArea = 33.25;
constexpr double ExpectedVolume = OuterArea * Height - InnerArea * (Height - Thickness);
const std::vector<Vec3> LProfile{
    { -6, -4, 0 }, { 6, -4, 0 }, { 6, -1, 0 }, { -1, -1, 0 }, { -1, 4, 0 }, { -6, 4, 0 }
};

[[nodiscard]] Deliver<BrepBody> Prism(const std::vector<Vec3>& Points) noexcept
{
    const Deliver<NurbsCurve> Profile = NurbsCurve::Polyline(Points, true);
    return Profile ? BrepBody::Extrude(Profile.Payload, Vec3::UnitZ(), Height)
                   : Deliver<BrepBody>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
}

[[nodiscard]] Deliver<BrepBody> ConcavePrism() noexcept { return Prism(LProfile); }

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

[[nodiscard]] int PlanarFaceToward(const BrepBody& Body, Vec3 Direction) noexcept
{
    int Best = -1; double BestDot = -2.0;
    for (int I = 0; I < static_cast<int>(Body.Faces.size()); ++I)
    {
        const BrepFace& F = Body.Faces[I];
        if (F.Surface.Classification != SurfaceClassification::Plane) continue;
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
           After.Loops == Before.Loops && After.Genus == Before.Genus && After.OpenEdges == Before.OpenEdges &&
           After.NonManifoldEdges == Before.NonManifoldEdges && After.MisorientedEdges == Before.MisorientedEdges &&
           std::fabs(After.Volume - Before.Volume) <= 1e-12 && Body.Vertices.size() == Snapshot.Vertices.size() &&
           Body.Edges.size() == Snapshot.Edges.size() && Body.Coedges.size() == Snapshot.Coedges.size() &&
           Body.Loops.size() == Snapshot.Loops.size() && Body.Faces.size() == Snapshot.Faces.size();
}

[[nodiscard]] bool HasVertex(const BrepBody& Body, Vec3 Point, double Tolerance = 1e-8) noexcept
{
    for (const BrepVertex& Vertex : Body.Vertices)
        if (Vertex.Point.Distance(Point) <= Tolerance) return true;
    return false;
}

[[nodiscard]] bool IsConcaveCap(const BrepBody& Body, int Face, bool Upper) noexcept
{
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return false;
    const BrepFace& Cap = Body.Faces[Face];
    if (Cap.Surface.Classification != SurfaceClassification::Plane || Cap.Loops.size() != 1) return false;
    const Vec3 N = Body.FaceNormal(Face, 0.5 * (Cap.Surface.DomainStartU() + Cap.Surface.DomainEndU()),
                                   0.5 * (Cap.Surface.DomainStartV() + Cap.Surface.DomainEndV())).Normalised();
    if ((Upper && N.Dot(Vec3::UnitZ()) < 1.0 - 1e-6) || (!Upper && N.Dot(-Vec3::UnitZ()) < 1.0 - 1e-6)) return false;
    const int Loop = Cap.Loops.front();
    if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size()) || Body.Loops[Loop].Coedges.size() != 6) return false;
    int Positive = 0, Negative = 0;
    std::vector<Vec3> Polygon;
    for (int Coedge : Body.Loops[Loop].Coedges)
    {
        if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return false;
        const BrepCoedge& C = Body.Coedges[Coedge];
        if (C.Edge < 0 || C.Edge >= static_cast<int>(Body.Edges.size())) return false;
        const BrepEdge& E = Body.Edges[C.Edge];
        if (E.Curve.Classification != CurveClassification::Line || E.VertexStart < 0 || E.VertexEnd < 0) return false;
        const int Start = C.Reversed ? E.VertexEnd : E.VertexStart;
        Polygon.push_back(Body.Vertices[Start].Point);
    }
    for (size_t I = 0; I < Polygon.size(); ++I)
    {
        const Vec3& A = Polygon[I], B = Polygon[(I + 1) % Polygon.size()], C = Polygon[(I + 2) % Polygon.size()];
        const double Cross = (B.X - A.X) * (C.Y - B.Y) - (B.Y - A.Y) * (C.X - B.X);
        if (Cross > 0.0) ++Positive; else if (Cross < 0.0) ++Negative; else return false;
    }
    return std::min(Positive, Negative) == 1 && std::max(Positive, Negative) == 5;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 48 · bounded orthogonal concave-prism shell");
    const auto SourceDeliver = ConcavePrism();
    Panel.Expect("The orthogonal L-shaped concave prism is constructed", static_cast<bool>(SourceDeliver));
    const BrepBody Source = SourceDeliver.Payload;
    const BrepBody Snapshot = Source;
    const BodyReport Before = Source.Validate();
    const int Top = FaceToward(Source, Vec3::UnitZ());
    const int Bottom = FaceToward(Source, -Vec3::UnitZ());
    Panel.Expect("The source has closed genus-zero V12/E18/C36/L8/F8 topology", SourceDeliver && Before.Solid() &&
                 Before.Hulls == 1 && Before.Genus == 0 && Source.Vertices.size() == 12 && Source.Edges.size() == 18 &&
                 Source.Coedges.size() == 36 && Source.Loops.size() == 8 && Source.Faces.size() == 8);
    Panel.Expect("The upper cap is the six-edge concave loop", IsConcaveCap(Source, Top, true));
    Panel.Expect("The upper and lower cap selections are distinct", Top >= 0 && Bottom >= 0 && Top != Bottom);

    Panel.Section("Exact non-convex shell reconstruction");
    const auto Shell = SourceDeliver ? FaceEditSolver::ShellExtrudedConcavePrism(Source, Top, Thickness)
                                     : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no concave source");
    Panel.Expect("The selected cap produces a closed concave shell solid", Shell && Shell.Payload.Validate().Solid());
    if (Shell)
    {
        const BodyReport R = Shell.Payload.Validate();
        Panel.Expect("The shell has V24/E42/C84/L20/F20 topology", R.Hulls == 1 && R.Genus == 0 &&
                     Shell.Payload.Vertices.size() == 24 && Shell.Payload.Edges.size() == 42 &&
                     Shell.Payload.Coedges.size() == 84 && Shell.Payload.Loops.size() == 20 && Shell.Payload.Faces.size() == 20 &&
                     R.OpenEdges == 0 && R.NonManifoldEdges == 0 && R.MisorientedEdges == 0);
        Panel.Within("The shell volume follows the outer L prism minus its inset cavity", std::fabs(R.Volume - ExpectedVolume), 1e-9);
        int Planes = 0, Extrusions = 0, Rims = 0;
        for (const BrepFace& F : Shell.Payload.Faces)
        {
            if (F.Surface.Classification == SurfaceClassification::Plane) ++Planes;
            else if (F.Surface.Classification == SurfaceClassification::Extrusion) ++Extrusions;
            else if (F.Surface.Classification == SurfaceClassification::Ruled) ++Rims;
        }
        Panel.Expect("The shell retains two caps, twelve walls, and six top-rim faces", Planes == 2 && Extrusions == 12 && Rims == 6);
        Panel.Expect("The concave outer bottom and inset floor remain identifiable", IsConcaveCap(Shell.Payload, PlanarFaceToward(Shell.Payload, Vec3::UnitZ()), true) &&
                     IsConcaveCap(Shell.Payload, PlanarFaceToward(Shell.Payload, -Vec3::UnitZ()), false));
        Panel.Expect("The inset floor reaches the expected L-profile corner", HasVertex(Shell.Payload, { -5.25, -3.25, Thickness }));
        Panel.Expect("The separate shell reconstruction preserves the source", SameSource(Source, Snapshot, Before));
    }
    const auto DispatchShell = FaceEditSolver::Shell(Source, Top, Thickness);
    Panel.Expect("The public shell dispatcher reaches the concave route", DispatchShell && DispatchShell.Payload.Validate().Solid());
    Panel.Expect("Only the upper cap is supported", !FaceEditSolver::ShellExtrudedConcavePrism(Source, Bottom, Thickness));

    Panel.Section("Concave-prism shell refusal boundaries");
    const auto Box = BrepBody::Box({ -6, -4, 0 }, { 6, 4, Height });
    Panel.Expect("A box remains on the separate box-shell route", Box && !FaceEditSolver::ShellExtrudedConcavePrism(Box.Payload, FaceToward(Box.Payload, Vec3::UnitZ()), Thickness));
    const auto ConvexHex = Prism({ { -6, -3, 0 }, { 0, -6, 0 }, { 6, -3, 0 }, { 6, 3, 0 }, { 0, 6, 0 }, { -6, 3, 0 } });
    Panel.Expect("A convex six-sided prism refuses", ConvexHex && !FaceEditSolver::ShellExtrudedConcavePrism(ConvexHex.Payload, FaceToward(ConvexHex.Payload, Vec3::UnitZ()), Thickness));
    const auto Pentagon = Prism({ { -5, -3, 0 }, { 5, -3, 0 }, { 6, 2, 0 }, { 0, 5, 0 }, { -6, 2, 0 } });
    Panel.Expect("A five-sided prism refuses", Pentagon && !FaceEditSolver::ShellExtrudedConcavePrism(Pentagon.Payload, FaceToward(Pentagon.Payload, Vec3::UnitZ()), Thickness));
    const auto NonOrthogonal = Prism({ { -6, -4, 0 }, { 6, -4, 0 }, { 6, -1, 0 }, { -1, -1, 0 }, { -2, 4, 0 }, { -6, 4, 0 } });
    Panel.Expect("A non-orthogonal concave prism refuses", NonOrthogonal && !FaceEditSolver::ShellExtrudedConcavePrism(NonOrthogonal.Payload, FaceToward(NonOrthogonal.Payload, Vec3::UnitZ()), Thickness));
    const auto Cylinder = BrepBody::Cylinder({ 0, 0, 0 }, Vec3::UnitZ(), 5.0, Height);
    Panel.Expect("A curved cylinder refuses", Cylinder && !FaceEditSolver::ShellExtrudedConcavePrism(Cylinder.Payload, FaceToward(Cylinder.Payload, Vec3::UnitZ()), Thickness));
    Panel.Expect("Lower, zero, negative, non-finite, and over-thick shells refuse", !FaceEditSolver::ShellExtrudedConcavePrism(Source, Bottom, Thickness) &&
                 !FaceEditSolver::ShellExtrudedConcavePrism(Source, Top, 0.0) &&
                 !FaceEditSolver::ShellExtrudedConcavePrism(Source, Top, -0.1) &&
                 !FaceEditSolver::ShellExtrudedConcavePrism(Source, Top, std::numeric_limits<double>::infinity()) &&
                 !FaceEditSolver::ShellExtrudedConcavePrism(Source, Top, Height * 0.5));
    BrepBody Malformed = Source;
    Malformed.Edges[0].Coedges.push_back(Malformed.Edges[0].Coedges.front());
    Panel.Expect("A malformed concave prism refuses transactionally", !FaceEditSolver::ShellExtrudedConcavePrism(Malformed, Top, Thickness));
    Panel.Expect("All refusal paths preserve the source", SameSource(Source, Snapshot, Before));

    Panel.Section("Distinct concave-shell proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase48_ConcavePrismShell.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Shell &&
        Host.Document().AddBody("SharpConcavePrism", Source.Transformed(Mat4::Translation({ -10, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("ShelledConcavePrism", Shell.Payload.Transformed(Mat4::Translation({ 10, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 35 -18") && Host.Execute("view fit") &&
        Host.Execute("render Phase48_ConcavePrismShell");
    Panel.Expect("The concave-prism shell proof render completes", Rendered);
    Panel.Expect("The concave-prism shell proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
