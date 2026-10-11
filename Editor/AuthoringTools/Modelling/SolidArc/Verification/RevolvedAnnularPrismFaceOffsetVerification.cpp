//=============================================================================================================================================
// SolidArc · Phase 51 · bounded revolved-annular-prism face offset
//=============================================================================================================================================
#include "Kernel/FaceEditSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <vector>

using namespace Frontier;

namespace
{
constexpr double InnerRadius = 4.0;
constexpr double OuterRadius = 6.0;
constexpr double Height = 4.0;
constexpr double Offset = 1.5;

[[nodiscard]] Deliver<BrepBody> AnnularPrism(double Sweep = ScalarCriteria::TwoPi) noexcept
{
    const Deliver<NurbsCurve> Profile = NurbsCurve::Polyline({ { InnerRadius, 0, 0 }, { OuterRadius, 0, 0 },
                                                                 { OuterRadius, 0, Height }, { InnerRadius, 0, Height } }, true);
    return Profile ? BrepBody::Revolve(Profile.Payload, { 0, 0, 0 }, Vec3::UnitZ(), Sweep)
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
           After.Loops == Before.Loops && After.Genus == Before.Genus && After.OpenEdges == Before.OpenEdges &&
           After.NonManifoldEdges == Before.NonManifoldEdges && After.MisorientedEdges == Before.MisorientedEdges &&
           std::fabs(After.Volume - Before.Volume) <= 1e-12 && Body.Vertices.size() == Snapshot.Vertices.size() &&
           Body.Edges.size() == Snapshot.Edges.size() && Body.Coedges.size() == Snapshot.Coedges.size() &&
           Body.Loops.size() == Snapshot.Loops.size() && Body.Faces.size() == Snapshot.Faces.size();
}

[[nodiscard]] bool IsAnnularCap(const BrepBody& Body, int Face, bool Upper) noexcept
{
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return false;
    const BrepFace& Cap = Body.Faces[Face];
    if (Cap.Surface.Classification != SurfaceClassification::Revolution || Cap.Loops.size() != 1) return false;
    const Vec3 N = Body.FaceNormal(Face, 0.5 * (Cap.Surface.DomainStartU() + Cap.Surface.DomainEndU()),
                                   0.5 * (Cap.Surface.DomainStartV() + Cap.Surface.DomainEndV())).Normalised();
    if ((Upper && N.Dot(Vec3::UnitZ()) < 1.0 - 1e-6) || (!Upper && N.Dot(-Vec3::UnitZ()) < 1.0 - 1e-6)) return false;
    const int Loop = Cap.Loops.front();
    if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size()) || Body.Loops[Loop].Coedges.size() != 4) return false;
    int Circles = 0, Lines = 0;
    for (int Coedge : Body.Loops[Loop].Coedges)
    {
        if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return false;
        const int EdgeIndex = Body.Coedges[Coedge].Edge;
        if (EdgeIndex < 0 || EdgeIndex >= static_cast<int>(Body.Edges.size())) return false;
        const BrepEdge& E = Body.Edges[EdgeIndex];
        if (E.Curve.Classification == CurveClassification::Circle && E.Curve.Rational() && E.Curve.Closed()) ++Circles;
        else if (E.Curve.Classification == CurveClassification::Line && E.Curve.Degree == 1) ++Lines;
        else return false;
    }
    return Circles == 2 && Lines == 2;
}

[[nodiscard]] bool HasRadii(const BrepBody& Body, int Face, double Inner, double Outer) noexcept
{
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return false;
    const BrepFace& Cap = Body.Faces[Face];
    if (Cap.Loops.size() != 1) return false;
    const int Loop = Cap.Loops.front();
    if (Loop < 0 || Loop >= static_cast<int>(Body.Loops.size())) return false;
    std::vector<double> Radii;
    for (int Coedge : Body.Loops[Loop].Coedges)
    {
        if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) return false;
        const int EdgeIndex = Body.Coedges[Coedge].Edge;
        if (EdgeIndex < 0 || EdgeIndex >= static_cast<int>(Body.Edges.size())) return false;
        const BrepEdge& E = Body.Edges[EdgeIndex];
        if (E.Curve.Classification != CurveClassification::Circle) continue;
        const Box3 B = E.Curve.Bounds();
        Radii.push_back(0.25 * ((B.High.X - B.Low.X) + (B.High.Y - B.Low.Y)));
    }
    if (Radii.size() != 2) return false;
    std::sort(Radii.begin(), Radii.end());
    return std::fabs(Radii[0] - Inner) <= 1e-9 && std::fabs(Radii[1] - Outer) <= 1e-9;
}

[[nodiscard]] bool AllRevolved(const BrepBody& Body) noexcept
{
    return std::all_of(Body.Faces.begin(), Body.Faces.end(), [](const BrepFace& F) {
        return F.Surface.Classification == SurfaceClassification::Revolution;
    });
}

[[nodiscard]] Deliver<BrepBody> ThroughHolePrism() noexcept
{
    Workplane W;
    const auto Outer = NurbsCurve::Rectangle(W, { -6, -5 }, { 6, 5 });
    const auto Hole = NurbsCurve::Circle({ 0, 0, 0 }, Vec3::UnitZ(), 1.5);
    return Outer && Hole ? BrepBody::Extrude(std::vector<NurbsCurve>{ Outer.Payload, Hole.Payload }, Vec3::UnitZ(), Height)
                         : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "hole fixture");
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 51 · bounded revolved-annular-prism face offset");
    const auto SourceDeliver = AnnularPrism();
    Panel.Expect("The full-turn annular prism is constructed", static_cast<bool>(SourceDeliver));
    const BrepBody Source = SourceDeliver.Payload;
    const BrepBody Snapshot = Source;
    const BodyReport Before = Source.Validate();
    const int Top = FaceToward(Source, Vec3::UnitZ());
    const int Bottom = FaceToward(Source, -Vec3::UnitZ());
    const int Side = FaceToward(Source, Vec3::UnitX());
    Panel.Expect("The source has closed genus-one V4/E8/C16/L4/F4 topology", SourceDeliver && Before.Solid() &&
                 Before.Hulls == 1 && Before.Genus == 1 && Source.Vertices.size() == 4 && Source.Edges.size() == 8 &&
                 Source.Coedges.size() == 16 && Source.Loops.size() == 4 && Source.Faces.size() == 4);
    Panel.Expect("Both annular caps contain two circular rims", IsAnnularCap(Source, Top, true) && IsAnnularCap(Source, Bottom, false));
    Panel.Expect("The selected cap and side face are distinct", Top >= 0 && Bottom >= 0 && Side >= 0 && Top != Bottom && Top != Side);
    Panel.Expect("The annular prism is entirely revolved analytic geometry", AllRevolved(Source));

    Panel.Section("Exact revolved-annulus face offset reconstruction");
    const auto Result = SourceDeliver ? FaceEditSolver::OffsetRevolvedAnnularPrism(Source, Top, Offset)
                                      : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "no annular source");
    Panel.Expect("The selected upper annular cap produces a closed solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport R = Result.Payload.Validate();
        Panel.Expect("The result retains V4/E8/C16/L4/F4 genus-one topology", R.Hulls == 1 && R.Genus == 1 &&
                     Result.Payload.Vertices.size() == 4 && Result.Payload.Edges.size() == 8 &&
                     Result.Payload.Coedges.size() == 16 && Result.Payload.Loops.size() == 4 && Result.Payload.Faces.size() == 4 &&
                     R.OpenEdges == 0 && R.NonManifoldEdges == 0 && R.MisorientedEdges == 0);
        Panel.Within("The offset volume follows annular area times extended height", std::fabs(R.Volume - ScalarCriteria::Pi * (OuterRadius * OuterRadius - InnerRadius * InnerRadius) * (Height + Offset)), 2e-1);
        Panel.Expect("The result retains both annular caps", IsAnnularCap(Result.Payload, FaceToward(Result.Payload, Vec3::UnitZ()), true) &&
                     IsAnnularCap(Result.Payload, FaceToward(Result.Payload, -Vec3::UnitZ()), false));
        Panel.Expect("The exact inner and outer circular rims are preserved", HasRadii(Source, Top, InnerRadius, OuterRadius) &&
                     HasRadii(Source, Bottom, InnerRadius, OuterRadius) &&
                     HasRadii(Result.Payload, FaceToward(Result.Payload, Vec3::UnitZ()), InnerRadius, OuterRadius) &&
                     HasRadii(Result.Payload, FaceToward(Result.Payload, -Vec3::UnitZ()), InnerRadius, OuterRadius));
        Panel.Expect("The result retains four revolution supports", AllRevolved(Result.Payload));
        Panel.Expect("The separate reconstruction preserves the source", SameSource(Source, Snapshot, Before));
    }
    const auto Dispatch = FaceEditSolver::OffsetFace(Source, Top, Offset);
    Panel.Expect("The public offset dispatcher reaches the revolved-annulus route", Dispatch && Dispatch.Payload.Validate().Genus == 1);
    Panel.Expect("Only the upper annular cap is supported", !FaceEditSolver::OffsetRevolvedAnnularPrism(Source, Bottom, Offset) &&
                 !FaceEditSolver::OffsetRevolvedAnnularPrism(Source, Side, Offset));

    Panel.Section("Revolved-annulus offset refusal boundaries");
    const auto Box = BrepBody::Box({ -6, -6, 0 }, { 6, 6, Height });
    Panel.Expect("A box remains outside the revolved-annulus API", Box && !FaceEditSolver::OffsetRevolvedAnnularPrism(Box.Payload, FaceToward(Box.Payload, Vec3::UnitZ()), Offset));
    const auto Cylinder = BrepBody::Cylinder({ 0, 0, 0 }, Vec3::UnitZ(), OuterRadius, Height);
    Panel.Expect("A solid cylinder refuses", Cylinder && !FaceEditSolver::OffsetRevolvedAnnularPrism(Cylinder.Payload, FaceToward(Cylinder.Payload, Vec3::UnitZ()), Offset));
    const auto Partial = AnnularPrism(ScalarCriteria::Pi);
    Panel.Expect("A partial-turn annular revolution refuses", Partial && !FaceEditSolver::OffsetRevolvedAnnularPrism(Partial.Payload, FaceToward(Partial.Payload, Vec3::UnitZ()), Offset));
    const auto Hole = ThroughHolePrism();
    Panel.Expect("An extruded through-hole prism refuses", Hole && !FaceEditSolver::OffsetRevolvedAnnularPrism(Hole.Payload, FaceToward(Hole.Payload, Vec3::UnitZ()), Offset));
    Panel.Expect("Zero, negative, and non-finite offsets refuse", !FaceEditSolver::OffsetRevolvedAnnularPrism(Source, Top, 0.0) &&
                 !FaceEditSolver::OffsetRevolvedAnnularPrism(Source, Top, -0.1) &&
                 !FaceEditSolver::OffsetRevolvedAnnularPrism(Source, Top, std::numeric_limits<double>::infinity()));
    BrepBody Malformed = Source;
    Malformed.Edges[0].Coedges.push_back(Malformed.Edges[0].Coedges.front());
    Panel.Expect("A malformed annular prism refuses transactionally", !FaceEditSolver::OffsetRevolvedAnnularPrism(Malformed, Top, Offset));
    Panel.Expect("All refusal paths preserve the source", SameSource(Source, Snapshot, Before));

    Panel.Section("Distinct revolved-annulus proof");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) /
        "Phase51_RevolvedAnnularPrismFaceOffset.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = SourceDeliver && Result &&
        Host.Document().AddBody("SharpAnnularPrism", Source.Transformed(Mat4::Translation({ -8, 0, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetAnnularPrism", Result.Payload.Transformed(Mat4::Translation({ 8, 0, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") &&
        Host.Execute("view orbit 35 -18") && Host.Execute("view fit") &&
        Host.Execute("render Phase51_RevolvedAnnularPrismFaceOffset");
    Panel.Expect("The revolved-annulus offset proof render completes", Rendered);
    Panel.Expect("The revolved-annulus proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
