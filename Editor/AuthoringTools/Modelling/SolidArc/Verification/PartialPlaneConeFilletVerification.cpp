//=============================================================================================================================================
// SolidArc · Phase 36o · bounded general-angle partial plane–cone root fillet
//
// A coaxial conical-frustum sector is the first partial mixed-support slice. It accepts only the canonical open
// sector with radial endpoint caps, reconstructs exact revolution/cone supports, and refuses arbitrary or branched
// curved networks rather than guessing a trim/sew.
//=============================================================================================================================================
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>
#include <utility>

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

[[nodiscard]] Deliver<BrepBody> SectorTaperedBoss(Vec3 Base, Vec3 Axis, double OuterRadius, double ShoulderHeight,
                                                   double FootRadius, double TopRadius, double BossHeight,
                                                   double SweepAngle) noexcept
{
    Axis = Axis.Normalised();
    const Vec3 Radial = Workplane::FromNormal(Base, Axis).AxisX;
    const Vec3 ShoulderCentre = Base + Axis * ShoulderHeight;
    const Vec3 BossTop = ShoulderCentre + Axis * BossHeight;
    const std::vector<std::pair<Vec3, Vec3>> Profile{
        { Base, Base + Radial * OuterRadius },
        { Base + Radial * OuterRadius, ShoulderCentre + Radial * OuterRadius },
        { ShoulderCentre + Radial * OuterRadius, ShoulderCentre + Radial * FootRadius },
        { ShoulderCentre + Radial * FootRadius, BossTop + Radial * TopRadius },
        { BossTop + Radial * TopRadius, BossTop }
    };
    std::vector<NurbsSurface> Faces;
    for (size_t Piece = 0; Piece < Profile.size(); ++Piece)
    {
        Deliver<NurbsCurve> Line = NurbsCurve::Line(Profile[Piece].first, Profile[Piece].second);
        Deliver<NurbsSurface> Surface = Line
            ? NurbsSurface::Revolution(Line.Payload, Base, Axis, SweepAngle)
            : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
        if (!Surface) return Deliver<BrepBody>::Reject(Surface.Denial.Reason, Surface.Denial.Detail);
        if (Piece == 1)
        {
            Surface.Payload.Classification = SurfaceClassification::Cylinder;
            Surface.Payload.Origin = Base;
            Surface.Payload.Axis = Axis;
            Surface.Payload.RadiusMajor = Surface.Payload.RadiusMinor = OuterRadius;
        }
        else if (Piece == 3)
        {
            Surface.Payload.Classification = SurfaceClassification::Cone;
            Surface.Payload.Origin = ShoulderCentre;
            Surface.Payload.Axis = Axis;
            Surface.Payload.RadiusMajor = FootRadius;
            Surface.Payload.RadiusMinor = TopRadius;
        }
        for (NurbsSurface& Patch : SplitAngular(Surface.Payload)) Faces.push_back(std::move(Patch));
    }
    Deliver<BrepBody> Result = BrepBody::Sew(Faces);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, Result.Denial.Detail);
    if (ScalarCriteria::WithinAngularTolerance(std::fabs(SweepAngle), ScalarCriteria::Pi)) return Result;
    if (!CloseSectorCaps(Result.Payload, Base, BossTop, Radial, SweepAngle, OuterRadius))
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "partial plane-cone fixture could not close its radial caps");
    return Result;
}

[[nodiscard]] std::vector<int> RootMembers(const BrepBody& Body, double FootRadius, double RootHeight) noexcept
{
    std::vector<int> Members;
    for (int Edge = 0; Edge < static_cast<int>(Body.Edges.size()); ++Edge)
    {
        const BrepEdge& Candidate = Body.Edges[Edge];
        if (Candidate.Closed() || Candidate.Coedges.size() != 2 || Candidate.Curve.Classification != CurveClassification::Arc) continue;
        bool Shoulder = false, Cone = false;
        for (int Coedge : Candidate.Coedges)
        {
            if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) continue;
            const int Face = Body.Coedges[Coedge].Face;
            if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) continue;
            const SurfaceClassification Class = Body.Faces[Face].Surface.Classification;
            Shoulder = Shoulder || Class == SurfaceClassification::Revolution;
            Cone = Cone || Class == SurfaceClassification::Cone;
        }
        const Vec3 Middle = Candidate.Curve.Sample(0.5 * (Candidate.Curve.DomainStart() + Candidate.Curve.DomainEnd()));
        const Vec3 Origin{ 0, 0, RootHeight };
        if (Shoulder && Cone && std::fabs(Middle.Z - RootHeight) < 1e-8 &&
            std::fabs(std::hypot(Middle.X, Middle.Y) - FootRadius) < 1e-8) Members.push_back(Edge);
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

[[nodiscard]] double AddedWedge(double FootRadius, double TopRadius, double BossHeight, double Radius,
                                double SweepAngle) noexcept
{
    const double HalfAngle = std::atan2(FootRadius - TopRadius, BossHeight);
    const double SinA = std::sin(HalfAngle), CosA = std::cos(HalfAngle);
    const double ContactHeight = Radius * (1.0 - SinA);
    const double SpineRadius = FootRadius + Radius * (1.0 - SinA) / CosA;
    const double ContactRadius = FootRadius - ContactHeight * std::tan(HalfAngle);
    const double Quad[4][2] = { { FootRadius, 0.0 }, { SpineRadius, 0.0 }, { SpineRadius, Radius },
                                { ContactRadius, ContactHeight } };
    double QuadMoment = 0.0;
    for (int I = 0; I < 4; ++I)
    {
        const double* P = Quad[I]; const double* Q = Quad[(I + 1) % 4];
        QuadMoment += (P[0] + Q[0]) * (P[0] * Q[1] - Q[0] * P[1]);
    }
    QuadMoment = std::fabs(QuadMoment) / 6.0;
    const double Theta0 = ScalarCriteria::Pi + HalfAngle, Theta1 = 1.5 * ScalarCriteria::Pi;
    const double SectorMoment = SpineRadius * Radius * Radius * (Theta1 - Theta0) / 2.0 +
                                Radius * Radius * Radius * (std::sin(Theta1) - std::sin(Theta0)) / 3.0;
    return std::fabs(SweepAngle) * (QuadMoment - SectorMoment);
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 36o · partial plane–cone root fillet");
    constexpr double OuterRadius = 10.0, ShoulderHeight = 8.0, FootRadius = 5.0, TopRadius = 3.0, BossHeight = 7.0;
    constexpr double SweepAngle = 2.0, Radius = 0.5;
    const Vec3 Base{ 0, 0, 0 }, Axis{ 0, 0, 1 };
    const Deliver<BrepBody> SourceResult = SectorTaperedBoss(Base, Axis, OuterRadius, ShoulderHeight,
                                                              FootRadius, TopRadius, BossHeight, SweepAngle);
    const BrepBody Source = SourceResult.Payload;
    const std::vector<int> Members = SourceResult ? RootMembers(Source, FootRadius, ShoulderHeight) : std::vector<int>{};
    const BodyReport SourceReport = Source.Validate();

    Panel.Section("Canonical coaxial conical-frustum sector");
    Panel.Expect("The sector source closes as one V14/E24/F12 solid", SourceResult && SourceReport.Solid() &&
                 SourceReport.Hulls == 1 && SourceReport.Genus == 0 && Source.Vertices.size() == 14 &&
                 Source.Edges.size() == 24 && Source.Coedges.size() == 48 && Source.Loops.size() == 12 &&
                 Source.Faces.size() == 12);
    Panel.Expect("The plane-cone root has two open rational arc members", Members.size() == 2);
    Deliver<std::vector<int>> Chain = !Members.empty()
        ? BlendSolver::TangentChain(Source, Members.front())
        : Deliver<std::vector<int>>::Reject(RefusalReason::Unsupported, "sector root was not found");
    Panel.Expect("The selected root member propagates through the complete sector chain", Chain && Chain.Payload.size() == 2);

    const Deliver<BrepBody> HalfResult = SectorTaperedBoss(Base, Axis, OuterRadius, ShoulderHeight,
                                                            FootRadius, TopRadius, BossHeight, ScalarCriteria::Pi);
    const BrepBody HalfSource = HalfResult.Payload;
    const std::vector<int> HalfMembers = HalfResult ? RootMembers(HalfSource, FootRadius, ShoulderHeight) : std::vector<int>{};
    Panel.Section("Canonical half-turn plane-cone sector");
    Panel.Expect("The half-turn source closes as one V14/E23/F11 solid", HalfResult && HalfSource.Validate().Solid() &&
                 HalfSource.Vertices.size() == 14 && HalfSource.Edges.size() == 23 && HalfSource.Coedges.size() == 46 &&
                 HalfSource.Loops.size() == 11 && HalfSource.Faces.size() == 11 && HalfMembers.size() == 2);
    Deliver<std::vector<int>> HalfChain = !HalfMembers.empty()
        ? BlendSolver::TangentChain(HalfSource, HalfMembers.front())
        : Deliver<std::vector<int>>::Reject(RefusalReason::Unsupported, "half-turn root was not found");
    Panel.Expect("The half-turn root propagates through the complete open chain", HalfChain && HalfChain.Payload.size() == 2);
    int HalfApplied = 0;
    Deliver<BrepBody> HalfFillet = !HalfMembers.empty()
        ? BlendSolver::FilletEdges(HalfSource, { HalfMembers.front() }, Radius, &HalfApplied)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "half-turn root was not found");
    const BodyReport HalfReport = HalfFillet ? HalfFillet.Payload.Validate() : BodyReport{};
    Panel.Expect("The half-turn plane-cone fillet commits exact V12/E17/F7 topology",
                 HalfFillet && HalfApplied == 1 && HalfReport.Solid() && HalfReport.Hulls == 1 && HalfReport.Genus == 0 &&
                 HalfFillet.Payload.Vertices.size() == 12 && HalfFillet.Payload.Edges.size() == 17 &&
                 HalfFillet.Payload.Coedges.size() == 34 && HalfFillet.Payload.Loops.size() == 7 &&
                 HalfFillet.Payload.Faces.size() == 7 && HalfReport.OpenEdges == 0 && HalfReport.NonManifoldEdges == 0 &&
                 HalfReport.MisorientedEdges == 0);

    Panel.Section("Exact partial mixed-support reconstruction");
    int Applied = 0;
    Deliver<BrepBody> Result = !Members.empty()
        ? BlendSolver::FilletEdges(Source, { Members.front() }, Radius, &Applied)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "sector root was not found");
    Panel.Expect("The partial plane-cone fillet commits one complete chain", Result && Result.Payload.Validate().Solid() && Applied == 1);
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        Panel.Expect("The result is one genus-zero V12/E18/F8 manifold", Report.Hulls == 1 && Report.Genus == 0 &&
                     Result.Payload.Vertices.size() == 12 && Result.Payload.Edges.size() == 18 &&
                     Result.Payload.Coedges.size() == 36 && Result.Payload.Loops.size() == 8 &&
                     Result.Payload.Faces.size() == 8 && Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 &&
                     Report.MisorientedEdges == 0);
        Panel.Within("The sector added wedge follows the exact angular square-radius integral",
                     std::fabs((Report.Volume - SourceReport.Volume) -
                               AddedWedge(FootRadius, TopRadius, BossHeight, Radius, SweepAngle)) /
                         AddedWedge(FootRadius, TopRadius, BossHeight, Radius, SweepAngle), 2e-3);
        Panel.Expect("Both radial endpoint caps remain closed and the source remains immutable", SameSource(Source, SourceReport));
    }

    Panel.Section("Transactional refusal boundaries");
    if (!Members.empty())
    {
        Panel.Expect("Zero and consuming radii refuse without mutating the sector source",
                     !BlendSolver::FilletEdge(Source, Members.front(), 0.0) &&
                     !BlendSolver::FilletEdge(Source, Members.front(), BossHeight) &&
                     SameSource(Source, SourceReport));
    }
    auto Torus = BrepBody::Torus({ 20, 0, 0 }, Axis, 4.0, 1.0);
    Panel.Expect("An arbitrary torus edge remains an explicit refusal", Torus &&
                 !BlendSolver::FilletEdge(Torus.Payload, 0, Radius));

    Panel.Section("Console transaction and visible proof");
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Host.Document().AddBody("PartialPlaneConeFilletSource", Source).Identity > 0;
    const bool ConsoleCommit = Added && !Members.empty() &&
        Host.Execute("fillet PartialPlaneConeFilletSource 0.5 --edges=" + std::to_string(Members.front()) + " --name=PartialPlaneConeFillet") &&
        Host.Document().Find("PartialPlaneConeFillet") &&
        Host.Document().Find("PartialPlaneConeFillet")->Body.Validate().Solid();
    Panel.Expect("The console commits the bounded partial plane-cone route", ConsoleCommit);

    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase36o_PartialPlaneConeFillet.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1600, 900);
    Deliver<BrepBody> ProofResult = !Members.empty()
        ? BlendSolver::FilletEdge(Source, Members.front(), Radius)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "proof fixture is unavailable");
    const bool Rendered = SourceResult && ProofResult &&
        ProofHost.Document().AddBody("SharpPartialPlaneConeFillet", Source.Transformed(Mat4::Translation({ -11, 0, 0 }))).Identity > 0 &&
        ProofHost.Document().AddBody("FilletedPartialPlaneCone", ProofResult.Payload.Transformed(Mat4::Translation({ 11, 0, 0 }))).Identity > 0 &&
        ProofHost.Execute("view top") && ProofHost.Execute("view fit") && ProofHost.Execute("render Phase36o_PartialPlaneConeFillet");
    Panel.Expect("The partial plane-cone source/result proof render completes", Rendered);
    Panel.Expect("The partial plane-cone proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
