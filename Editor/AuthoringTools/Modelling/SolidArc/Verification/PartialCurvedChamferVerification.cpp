//=============================================================================================================================================
// SolidArc · Phase 36h · bounded partial curved-root chamfer
//
// First partial-loop chamfer slice: a complete physical half-turn shared by a planar shoulder and cylindrical boss.
// Either open root arc resolves the same two-member chain. General-angle sectors, arbitrary partial arcs, and
// freeform/multi-root networks remain explicit refusals.
//=============================================================================================================================================
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"

#include <cmath>
#include <filesystem>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

using namespace Frontier;

namespace
{
[[nodiscard]] std::vector<NurbsSurface> SplitAngular(const NurbsSurface& Surface) noexcept
{
    auto Halves = Surface.SplitU(0.5 * (Surface.DomainStartU() + Surface.DomainEndU()));
    return { std::move(Halves.first), std::move(Halves.second) };
}

[[nodiscard]] Deliver<BrepBody> SemicircularBoss(Vec3 Base, Vec3 Axis, double OuterRadius, double ShoulderHeight,
                                                  double BossRadius, double BossHeight) noexcept
{
    Axis = Axis.Normalised();
    const Vec3 Radial = Workplane::FromNormal(Base, Axis).AxisX;
    const Vec3 ShoulderCentre = Base + Axis * ShoulderHeight;
    const Vec3 BossTop = ShoulderCentre + Axis * BossHeight;
    const std::vector<std::pair<Vec3, Vec3>> Profile{
        { Base, Base + Radial * OuterRadius },
        { Base + Radial * OuterRadius, ShoulderCentre + Radial * OuterRadius },
        { ShoulderCentre + Radial * OuterRadius, ShoulderCentre + Radial * BossRadius },
        { ShoulderCentre + Radial * BossRadius, BossTop + Radial * BossRadius },
        { BossTop + Radial * BossRadius, BossTop }
    };
    std::vector<NurbsSurface> Faces;
    for (size_t Piece = 0; Piece < Profile.size(); ++Piece)
    {
        Deliver<NurbsCurve> Line = NurbsCurve::Line(Profile[Piece].first, Profile[Piece].second);
        Deliver<NurbsSurface> Surface = Line
            ? NurbsSurface::Revolution(Line.Payload, Base, Axis, ScalarCriteria::Pi)
            : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
        if (!Surface) return Deliver<BrepBody>::Reject(Surface.Denial.Reason, Surface.Denial.Detail);
        if (Piece == 1 || Piece == 3)
        {
            Surface.Payload.Classification = SurfaceClassification::Cylinder;
            Surface.Payload.Origin = Piece == 1 ? Base : ShoulderCentre;
            Surface.Payload.Axis = Axis;
            Surface.Payload.RadiusMajor = Surface.Payload.RadiusMinor = Piece == 1 ? OuterRadius : BossRadius;
        }
        for (NurbsSurface& Patch : SplitAngular(Surface.Payload)) Faces.push_back(std::move(Patch));
    }
    return BrepBody::Sew(Faces);
}

[[nodiscard]] std::vector<int> RootMembers(const BrepBody& Body, double BossRadius, double RootHeight) noexcept
{
    std::vector<int> Members;
    for (int Edge = 0; Edge < static_cast<int>(Body.Edges.size()); ++Edge)
    {
        const BrepEdge& Candidate = Body.Edges[Edge];
        if (Candidate.Closed() || Candidate.Coedges.size() != 2 ||
            Candidate.Curve.Classification != CurveClassification::Arc) continue;
        bool Shoulder = false, Boss = false;
        for (int Coedge : Candidate.Coedges)
        {
            if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) continue;
            const int Face = Body.Coedges[Coedge].Face;
            if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) continue;
            const NurbsSurface& Surface = Body.Faces[Face].Surface;
            Shoulder = Shoulder || Surface.Classification == SurfaceClassification::Revolution;
            // The outer shoulder rim also joins a revolution to a cylinder. The boss-radius measurement separates
            // that edge from the actual shoulder/boss root and from the top cap's revolved endpoint face.
            Boss = Boss || (Surface.Classification == SurfaceClassification::Cylinder &&
                            std::fabs(Surface.RadiusMajor - BossRadius) < 1e-9);
        }
        const Vec3 Midpoint = Candidate.Curve.Sample(0.5 * (Candidate.Curve.DomainStart() + Candidate.Curve.DomainEnd()));
        if (Shoulder && Boss && std::fabs(Midpoint.Z - RootHeight) < 1e-8) Members.push_back(Edge);
    }
    return Members;
}

[[nodiscard]] bool SameSource(const BrepBody& Body, const BodyReport& Before) noexcept
{
    const BodyReport After = Body.Validate();
    return After.Vertices == Before.Vertices && After.Edges == Before.Edges && After.Faces == Before.Faces &&
           After.OpenEdges == Before.OpenEdges && After.NonManifoldEdges == Before.NonManifoldEdges &&
           After.MisorientedEdges == Before.MisorientedEdges && std::fabs(After.Volume - Before.Volume) < 1e-9;
}

[[nodiscard]] bool ExactSupportSet(const BrepBody& Body, double BossRadius, double SetBack) noexcept
{
    int Cylinders = 0, Cones = 0, Revolutions = 0, Planes = 0;
    bool Band = false;
    for (const BrepFace& Face : Body.Faces)
    {
        const NurbsSurface& Surface = Face.Surface;
        if (Surface.Classification == SurfaceClassification::Cylinder) ++Cylinders;
        else if (Surface.Classification == SurfaceClassification::Cone)
        {
            ++Cones;
            Band = Band || ((std::fabs(Surface.RadiusMajor - (BossRadius + SetBack)) < 1e-9 &&
                             std::fabs(Surface.RadiusMinor - BossRadius) < 1e-9) ||
                            (std::fabs(Surface.RadiusMinor - (BossRadius + SetBack)) < 1e-9 &&
                             std::fabs(Surface.RadiusMajor - BossRadius) < 1e-9));
        }
        else if (Surface.Classification == SurfaceClassification::Revolution) ++Revolutions;
        else if (Surface.Classification == SurfaceClassification::Plane) ++Planes;
    }
    return Cylinders == 2 && Cones == 1 && Revolutions == 3 && Planes == 1 && Band;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 36h · partial curved-root chamfer — exact half-turn chain");
    constexpr double OuterRadius = 10.0, ShoulderHeight = 8.0, BossRadius = 5.0, BossHeight = 7.0, SetBack = 0.5;
    const Vec3 Base{ 0, 0, 0 }, Axis{ 0, 0, 1 };
    const Deliver<BrepBody> SourceResult = SemicircularBoss(Base, Axis, OuterRadius, ShoulderHeight, BossRadius, BossHeight);
    const BrepBody Source = SourceResult.Payload;
    const std::vector<int> Members = SourceResult ? RootMembers(Source, BossRadius, ShoulderHeight) : std::vector<int>{};

    Panel.Section("Partial circular-chain classifier");
    const BodyReport SourceReport = Source.Validate();
    Panel.Expect("The half-turn source is a one-hull V14/E23/F11 solid", SourceResult && SourceReport.Solid() &&
                 SourceReport.Hulls == 1 && SourceReport.Genus == 0 && Source.Vertices.size() == 14 &&
                 Source.Edges.size() == 23 && Source.Coedges.size() == 46 && Source.Loops.size() == 11 &&
                 Source.Faces.size() == 11);
    Panel.Expect("The selected curved root is two open rational arc members", Members.size() == 2);
    Deliver<std::vector<int>> Chain = !Members.empty()
        ? BlendSolver::TangentChain(Source, Members.front())
        : Deliver<std::vector<int>>::Reject(RefusalReason::Unsupported, "half-turn root was not found");
    Panel.Expect("One root member propagates to the complete two-member half-turn chain",
                 Chain && Chain.Payload.size() == 2);

    Panel.Section("Exact partial-loop reconstruction");
    int Applied = 0;
    Deliver<BrepBody> Result = !Members.empty()
        ? BlendSolver::ChamferEdges(Source, { Members.front() }, SetBack, &Applied)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "half-turn root was not found");
    Panel.Expect("Selecting one arc commits the complete half-turn chamfer", Result && Result.Payload.Validate().Solid() && Applied == 1);
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        Panel.Expect("The result is one genus-zero V12/E17/F7 manifold", Report.Hulls == 1 && Report.Genus == 0 &&
                     Result.Payload.Vertices.size() == 12 && Result.Payload.Edges.size() == 17 &&
                     Result.Payload.Coedges.size() == 34 && Result.Payload.Loops.size() == 7 &&
                     Result.Payload.Faces.size() == 7 && Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 &&
                     Report.MisorientedEdges == 0);
        const double FullAdded = ScalarCriteria::Pi * SetBack *
            (BossRadius * SetBack + SetBack * SetBack / 3.0);
        Panel.Within("The half-turn added wedge is exactly one-half of the circular result",
                     std::fabs((Report.Volume - SourceReport.Volume) - 0.5 * FullAdded) / (0.5 * FullAdded), 2e-3);
        Panel.Expect("The partial result retains two cylinders, a conical band, endpoint surfaces and one cap",
                     ExactSupportSet(Result.Payload, BossRadius, SetBack));
        Panel.Expect("The half-turn source remains immutable", SameSource(Source, SourceReport));
    }
    if (Members.size() == 2)
    {
        Deliver<BrepBody> OtherResult = BlendSolver::ChamferEdge(Source, Members.back(), SetBack);
        Panel.Expect("Selecting the other arc member resolves the same complete chain", OtherResult && OtherResult.Payload.Validate().Solid() &&
                     OtherResult.Payload.Vertices.size() == 12 && OtherResult.Payload.Edges.size() == 17);
    }

    Panel.Section("Refusal boundaries");
    if (!Members.empty())
    {
        Panel.Expect("Zero and consuming setbacks refuse transactionally",
                     !BlendSolver::ChamferEdge(Source, Members.front(), 0.0) &&
                     !BlendSolver::ChamferEdge(Source, Members.front(), BossHeight) && SameSource(Source, SourceReport));
    }
    auto Torus = BrepBody::Torus({ 20, 0, 0 }, Axis, 4.0, 1.0);
    Panel.Expect("An arbitrary torus edge remains outside the partial curved route", Torus &&
                 !BlendSolver::ChamferEdge(Torus.Payload, 0, SetBack));

    Panel.Section("Console transaction and visible proof");
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Host.Document().AddBody("HalfTurnSource", Source).Identity > 0;
    const bool ConsoleCommit = Added && !Members.empty() &&
        Host.Execute("chamfer HalfTurnSource 0.5 --edges=" + std::to_string(Members.front()) + " --name=HalfTurnChamfer") &&
        Host.Document().Find("HalfTurnChamfer") && Host.Document().Find("HalfTurnChamfer")->Body.Validate().Solid();
    Panel.Expect("The console commits the partial curved-root route", ConsoleCommit);

    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase36h_PartialCurvedChamfer.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1600, 900);
    Deliver<BrepBody> ProofResult = !Members.empty()
        ? BlendSolver::ChamferEdge(Source, Members.front(), SetBack)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "proof fixture is unavailable");
    const bool Rendered = SourceResult && ProofResult &&
        ProofHost.Document().AddBody("SharpHalfTurn", Source.Transformed(Mat4::Translation({ -11, 0, 0 }))).Identity > 0 &&
        ProofHost.Document().AddBody("ChamferedHalfTurn", ProofResult.Payload.Transformed(Mat4::Translation({ 11, 0, 0 }))).Identity > 0 &&
        ProofHost.Execute("view iso") && ProofHost.Execute("view fit") && ProofHost.Execute("render Phase36h_PartialCurvedChamfer");
    Panel.Expect("The partial curved source/result proof render completes", Rendered);
    Panel.Expect("The partial curved chamfer proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
