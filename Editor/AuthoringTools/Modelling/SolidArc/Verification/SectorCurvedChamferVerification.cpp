//=============================================================================================================================================
// SolidArc · Phase 36i · bounded general-angle partial curved-root chamfer
//
// A non-reflex sector is the next partial-loop slice after the exact half-turn. Two radial endpoint caps are healed
// explicitly; arbitrary partial/branched/freeform networks remain outside the route.
//=============================================================================================================================================
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"

#include <algorithm>
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

[[nodiscard]] bool CloseSectorCaps(BrepBody& Body, Vec3 AxisStart, Vec3 AxisEnd, Vec3 RadialStart,
                                   double SweepAngle, double RadialExtent) noexcept
{
    const Vec3 Axis = (AxisEnd - AxisStart).Normalised();
    RadialStart = (RadialStart - Axis * RadialStart.Dot(Axis)).Normalised();
    const Vec3 RadialEnd = RadialStart * std::cos(SweepAngle) + Axis.Cross(RadialStart) * std::sin(SweepAngle);
    if (Axis.Length() <= ScalarCriteria::MergeTolerance || RadialStart.Length() <= ScalarCriteria::MergeTolerance ||
        RadialEnd.Length() <= ScalarCriteria::MergeTolerance || RadialExtent <= ScalarCriteria::MergeTolerance) return false;

    std::vector<int> StartEdges, EndEdges;
    for (size_t Edge = 0; Edge < Body.Edges.size(); ++Edge)
    {
        if (Body.Edges[Edge].Coedges.size() != 1) continue;
        const NurbsCurve& Curve = Body.Edges[Edge].Curve;
        const Vec3 Middle = Curve.Sample(0.5 * (Curve.DomainStart() + Curve.DomainEnd()));
        Vec3 Radial = Middle - (AxisStart + Axis * (Middle - AxisStart).Dot(Axis));
        if (Radial.Length() <= ScalarCriteria::MergeTolerance) return false;
        Radial = Radial.Normalised();
        (std::fabs(Radial.Dot(RadialStart)) >= std::fabs(Radial.Dot(RadialEnd)) ? StartEdges : EndEdges)
            .push_back(static_cast<int>(Edge));
    }
    if (StartEdges.empty() || EndEdges.empty()) return false;
    Deliver<NurbsCurve> AxisCurve = NurbsCurve::Line(AxisStart, AxisEnd);
    if (!AxisCurve) return false;
    const int AxisEdge = Body.AddEdge(AxisCurve.Payload, ScalarCriteria::MergeTolerance);
    const int StartVertex = Body.AddVertex(AxisStart, ScalarCriteria::MergeTolerance);
    const int EndVertex = Body.AddVertex(AxisEnd, ScalarCriteria::MergeTolerance);

    auto AddCap = [&](std::vector<int> Edges, Vec3 Radial) noexcept
    {
        std::vector<std::pair<int, bool>> Path;
        int Current = StartVertex;
        while (Current != EndVertex)
        {
            auto It = std::find_if(Edges.begin(), Edges.end(), [&](int Edge)
            { return Body.Edges[Edge].VertexStart == Current || Body.Edges[Edge].VertexEnd == Current; });
            if (It == Edges.end()) return false;
            const int Edge = *It;
            const bool Reversed = Body.Edges[Edge].VertexEnd == Current;
            Current = Reversed ? Body.Edges[Edge].VertexStart : Body.Edges[Edge].VertexEnd;
            Path.push_back({ Edge, Reversed });
            Edges.erase(It);
        }
        if (!Edges.empty()) return false;
        const double Pad = 0.01 * std::max(RadialExtent, AxisStart.Distance(AxisEnd)) + ScalarCriteria::MergeTolerance;
        Deliver<NurbsSurface> Plane = NurbsSurface::Plane(AxisStart - Radial * Pad - Axis * Pad,
            Radial, Axis, RadialExtent + 2.0 * Pad, AxisStart.Distance(AxisEnd) + 2.0 * Pad);
        if (!Plane) return false;
        const int Face = Body.AddFace(std::move(Plane.Payload));
        Body.Faces[Face].Natural = false;
        const int Loop = Body.AddLoop(Face, true);
        for (const auto& [Edge, Reversed] : Path) Body.AddCoedge(Edge, Reversed, Face, Loop);
        Body.AddCoedge(AxisEdge, Body.Edges[AxisEdge].VertexStart == EndVertex, Face, Loop);
        return true;
    };
    if (!AddCap(StartEdges, RadialStart) || !AddCap(EndEdges, RadialEnd)) return false;
    Body.Orient();
    return Body.Validate().Solid();
}

[[nodiscard]] Deliver<BrepBody> SectorBoss(Vec3 Base, Vec3 Axis, double OuterRadius, double ShoulderHeight,
                                            double BossRadius, double BossHeight, double SweepAngle) noexcept
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
            ? NurbsSurface::Revolution(Line.Payload, Base, Axis, SweepAngle)
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
    Deliver<BrepBody> Result = BrepBody::Sew(Faces);
    if (!Result || !CloseSectorCaps(Result.Payload, Base, BossTop, Radial, SweepAngle, OuterRadius))
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "general-angle source could not close its radial caps");
    return Result;
}

[[nodiscard]] std::vector<int> RootMembers(const BrepBody& Body, double BossRadius, double RootHeight) noexcept
{
    std::vector<int> Members;
    for (int Edge = 0; Edge < static_cast<int>(Body.Edges.size()); ++Edge)
    {
        const BrepEdge& Candidate = Body.Edges[Edge];
        if (Candidate.Closed() || Candidate.Coedges.size() != 2 || Candidate.Curve.Classification != CurveClassification::Arc) continue;
        bool Shoulder = false, Boss = false;
        for (int Coedge : Candidate.Coedges)
        {
            if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) continue;
            const int Face = Body.Coedges[Coedge].Face;
            if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) continue;
            const NurbsSurface& Surface = Body.Faces[Face].Surface;
            Shoulder = Shoulder || Surface.Classification == SurfaceClassification::Revolution;
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
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 36i · general-angle partial curved-root chamfer");
    constexpr double OuterRadius = 10.0, ShoulderHeight = 8.0, BossRadius = 5.0, BossHeight = 7.0;
    constexpr double SweepAngle = 2.0, SetBack = 0.5;
    const Vec3 Base{ 0, 0, 0 }, Axis{ 0, 0, 1 };
    const Deliver<BrepBody> SourceResult = SectorBoss(Base, Axis, OuterRadius, ShoulderHeight, BossRadius, BossHeight, SweepAngle);
    const BrepBody Source = SourceResult.Payload;
    const std::vector<int> Members = SourceResult ? RootMembers(Source, BossRadius, ShoulderHeight) : std::vector<int>{};
    const BodyReport SourceReport = Source.Validate();

    Panel.Section("General-angle partial-chain classifier");
    Panel.Expect("The sector source closes as one V14/E24/F12 solid", SourceResult && SourceReport.Solid() &&
                 SourceReport.Hulls == 1 && SourceReport.Genus == 0 && Source.Vertices.size() == 14 &&
                 Source.Edges.size() == 24 && Source.Coedges.size() == 48 && Source.Loops.size() == 12 &&
                 Source.Faces.size() == 12);
    Panel.Expect("The general-angle root has two open rational arc members", Members.size() == 2);
    Deliver<std::vector<int>> Chain = !Members.empty()
        ? BlendSolver::TangentChain(Source, Members.front())
        : Deliver<std::vector<int>>::Reject(RefusalReason::Unsupported, "sector root was not found");
    Panel.Expect("The selected member propagates through the complete sector chain", Chain && Chain.Payload.size() == 2);

    Panel.Section("Exact general-angle reconstruction");
    int Applied = 0;
    Deliver<BrepBody> Result = !Members.empty()
        ? BlendSolver::ChamferEdges(Source, { Members.front() }, SetBack, &Applied)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "sector root was not found");
    Panel.Expect("The general-angle partial chamfer commits one complete chain", Result && Result.Payload.Validate().Solid() && Applied == 1);
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        Panel.Expect("The result is one genus-zero V12/E18/F8 manifold", Report.Hulls == 1 && Report.Genus == 0 &&
                     Result.Payload.Vertices.size() == 12 && Result.Payload.Edges.size() == 18 &&
                     Result.Payload.Coedges.size() == 36 && Result.Payload.Loops.size() == 8 &&
                     Result.Payload.Faces.size() == 8 && Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 &&
                     Report.MisorientedEdges == 0);
        const double FullAdded = ScalarCriteria::Pi * SetBack *
            (BossRadius * SetBack + SetBack * SetBack / 3.0);
        const double ExpectedAdded = std::fabs(SweepAngle) / ScalarCriteria::TwoPi * FullAdded;
        Panel.Within("The sector added wedge follows its exact angular fraction",
                     std::fabs((Report.Volume - SourceReport.Volume) - ExpectedAdded) / ExpectedAdded, 2e-3);
        Panel.Expect("Both radial endpoint caps remain closed and the source remains immutable", SameSource(Source, SourceReport));
    }

    Panel.Section("Transactional refusal boundaries");
    if (!Members.empty())
    {
        Panel.Expect("Zero and consuming setbacks refuse without mutating the sector source",
                     !BlendSolver::ChamferEdge(Source, Members.front(), 0.0) &&
                     !BlendSolver::ChamferEdge(Source, Members.front(), BossHeight) &&
                     SameSource(Source, SourceReport));
    }
    auto Torus = BrepBody::Torus({ 20, 0, 0 }, Axis, 4.0, 1.0);
    Panel.Expect("An arbitrary torus edge remains an explicit refusal", Torus &&
                 !BlendSolver::ChamferEdge(Torus.Payload, 0, SetBack));

    Panel.Section("Console transaction and visible proof");
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Host.Document().AddBody("SectorSource", Source).Identity > 0;
    const bool ConsoleCommit = Added && !Members.empty() &&
        Host.Execute("chamfer SectorSource 0.5 --edges=" + std::to_string(Members.front()) + " --name=SectorChamfer") &&
        Host.Document().Find("SectorChamfer") && Host.Document().Find("SectorChamfer")->Body.Validate().Solid();
    Panel.Expect("The console commits the bounded general-angle route", ConsoleCommit);

    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase36i_GeneralSectorChamfer.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1600, 900);
    Deliver<BrepBody> ProofResult = !Members.empty()
        ? BlendSolver::ChamferEdge(Source, Members.front(), SetBack)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "proof fixture is unavailable");
    const bool Rendered = SourceResult && ProofResult &&
        ProofHost.Document().AddBody("SharpSector", Source.Transformed(Mat4::Translation({ -11, 0, 0 }))).Identity > 0 &&
        ProofHost.Document().AddBody("ChamferedSector", ProofResult.Payload.Transformed(Mat4::Translation({ 11, 0, 0 }))).Identity > 0 &&
        ProofHost.Execute("view iso") && ProofHost.Execute("view fit") && ProofHost.Execute("render Phase36i_GeneralSectorChamfer");
    Panel.Expect("The general-angle source/result proof render completes", Rendered);
    Panel.Expect("The general-angle proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
