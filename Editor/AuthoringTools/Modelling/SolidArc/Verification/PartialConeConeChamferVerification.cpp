//=============================================================================================================================================
// SolidArc · Phase 36k · bounded partial cone–cone root chamfer
//
// A coaxial cone/cylinder sector is the first broader mixed curved-root chamfer after the plane-supported slices.
// Only the canonical open non-reflex chain is accepted; endpoint caps are healed explicitly and general curved
// intersection/trim/sew is still refused.
//=============================================================================================================================================
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
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

[[nodiscard]] Deliver<BrepBody> SectorConeCone(Vec3 Base, Vec3 Axis, double BaseRadius, double RootRadius,
                                                    double TopRadius, double LowerHeight, double UpperHeight,
                                                    double SweepAngle) noexcept
{
    Axis = Axis.Normalised();
    const Vec3 Radial = Workplane::FromNormal(Base, Axis).AxisX;
    const Vec3 Root = Base + Axis * LowerHeight;
    const Vec3 Top = Root + Axis * UpperHeight;
    const std::vector<std::pair<Vec3, Vec3>> Profile{
        { Base, Base + Radial * BaseRadius },
        { Base + Radial * BaseRadius, Root + Radial * RootRadius },
        { Root + Radial * RootRadius, Top + Radial * TopRadius },
        { Top + Radial * TopRadius, Top }
    };
    std::vector<NurbsSurface> Faces;
    for (size_t Piece = 0; Piece < Profile.size(); ++Piece)
    {
        Deliver<NurbsCurve> Line = NurbsCurve::Line(Profile[Piece].first, Profile[Piece].second);
        Deliver<NurbsSurface> Surface = Line
            ? NurbsSurface::Revolution(Line.Payload, Base, Axis, SweepAngle)
            : Deliver<NurbsSurface>::Reject(Line.Denial.Reason, Line.Denial.Detail);
        if (!Surface) return Deliver<BrepBody>::Reject(Surface.Denial.Reason, Surface.Denial.Detail);
        if (Piece == 1 || Piece == 2)
        {
            Surface.Payload.Classification = SurfaceClassification::Cone;
            Surface.Payload.Origin = Piece == 1 ? Base : Root; Surface.Payload.Axis = Axis;
            Surface.Payload.RadiusMajor = Piece == 1 ? BaseRadius : RootRadius;
            Surface.Payload.RadiusMinor = Piece == 1 ? RootRadius : TopRadius;
        }
        for (NurbsSurface& Patch : SplitAngular(Surface.Payload)) Faces.push_back(std::move(Patch));
    }
    Deliver<BrepBody> Result = BrepBody::Sew(Faces);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, Result.Denial.Detail);
    if (ScalarCriteria::WithinAngularTolerance(std::fabs(SweepAngle), ScalarCriteria::Pi)) return Result;
    if (!CloseSectorCaps(Result.Payload, Base, Top, Radial, SweepAngle, std::max({ BaseRadius, RootRadius, TopRadius })))
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "partial cone-cone fixture could not close its radial caps");
    return Result;
}

[[nodiscard]] std::vector<int> RootMembers(const BrepBody& Body, double RootRadius, double RootHeight) noexcept
{
    std::vector<int> Members;
    for (int Edge = 0; Edge < static_cast<int>(Body.Edges.size()); ++Edge)
    {
        const BrepEdge& Candidate = Body.Edges[Edge];
        if (Candidate.Closed() || Candidate.Coedges.size() != 2 || Candidate.Curve.Classification != CurveClassification::Arc) continue;
        int Cones = 0;
        for (int Coedge : Candidate.Coedges)
        {
            if (Coedge < 0 || Coedge >= static_cast<int>(Body.Coedges.size())) continue;
            const int Face = Body.Coedges[Coedge].Face;
            if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) continue;
            Cones += Body.Faces[Face].Surface.Classification == SurfaceClassification::Cone;
        }
        const Vec3 Mid = Candidate.Curve.Sample(0.5 * (Candidate.Curve.DomainStart() + Candidate.Curve.DomainEnd()));
        if (Cones == 2 && std::fabs(Mid.Z - RootHeight) < 1e-8 &&
            std::fabs(std::hypot(Mid.X, Mid.Y) - RootRadius) < 1e-8) Members.push_back(Edge);
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

[[nodiscard]] double AddedWedge(double BaseRadius, double RootRadius, double TopRadius,
                                double LowerHeight, double UpperHeight, double SetBack, double SweepAngle) noexcept
{
    const double LowerSlant = std::hypot(LowerHeight, BaseRadius - RootRadius);
    const double UpperSlant = std::hypot(UpperHeight, TopRadius - RootRadius);
    const double LowerAxial = SetBack * LowerHeight / LowerSlant;
    const double UpperAxial = SetBack * UpperHeight / UpperSlant;
    const double LowerContact = RootRadius + (BaseRadius - RootRadius) * LowerAxial / LowerHeight;
    const double UpperContact = RootRadius + (TopRadius - RootRadius) * UpperAxial / UpperHeight;
    const double Cross = (RootRadius - LowerContact) * (LowerAxial + UpperAxial) -
                         LowerAxial * (UpperContact - LowerContact);
    const double Area = -0.5 * Cross;
    const double CentroidRadius = (LowerContact + RootRadius + UpperContact) / 3.0;
    const double Full = ScalarCriteria::TwoPi * Area * CentroidRadius;
    return std::fabs(SweepAngle) / ScalarCriteria::TwoPi * Full;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 36l · partial cone–cone root chamfer");
    constexpr double BaseRadius = 10.0, RootRadius = 5.0, TopRadius = 3.0, LowerHeight = 8.0, UpperHeight = 7.0;
    constexpr double SweepAngle = 2.0, SetBack = 0.5;
    const Vec3 Base{ 0, 0, 0 }, Axis{ 0, 0, 1 };
    const Deliver<BrepBody> SourceResult = SectorConeCone(Base, Axis, BaseRadius, RootRadius, TopRadius,
                                                               LowerHeight, UpperHeight, SweepAngle);
    const BrepBody Source = SourceResult.Payload;
    const std::vector<int> Members = SourceResult ? RootMembers(Source, RootRadius, LowerHeight) : std::vector<int>{};
    const BodyReport SourceReport = Source.Validate();
    Panel.Section("Canonical coaxial cone–cone sector");
    Panel.Expect("The sector source closes as one V11/E19/F10 solid", SourceResult && SourceReport.Solid() &&
                 SourceReport.Hulls == 1 && SourceReport.Genus == 0 && Source.Vertices.size() == 11 &&
                 Source.Edges.size() == 19 && Source.Coedges.size() == 38 && Source.Loops.size() == 10 &&
                 Source.Faces.size() == 10);
    Panel.Expect("The cone–cone root has two open rational arc members", Members.size() == 2);
    Deliver<std::vector<int>> Chain = !Members.empty()
        ? BlendSolver::TangentChain(Source, Members.front())
        : Deliver<std::vector<int>>::Reject(RefusalReason::Unsupported, "cone-cone root was not found");
    Panel.Expect("The selected root member propagates through the complete sector chain", Chain && Chain.Payload.size() == 2);

    const Deliver<BrepBody> HalfResult = SectorConeCone(Base, Axis, BaseRadius, RootRadius, TopRadius,
                                                             LowerHeight, UpperHeight, ScalarCriteria::Pi);
    const BrepBody HalfSource = HalfResult.Payload;
    const std::vector<int> HalfMembers = HalfResult ? RootMembers(HalfSource, RootRadius, LowerHeight) : std::vector<int>{};
    Panel.Expect("The half-turn source closes as one V11/E18/F9 solid", HalfResult && HalfSource.Validate().Solid() &&
                 HalfSource.Vertices.size() == 11 && HalfSource.Edges.size() == 18 && HalfSource.Coedges.size() == 36 &&
                 HalfSource.Loops.size() == 9 && HalfSource.Faces.size() == 9 && HalfMembers.size() == 2);
    int HalfApplied = 0;
    Deliver<BrepBody> HalfChamfer = !HalfMembers.empty()
        ? BlendSolver::ChamferEdges(HalfSource, { HalfMembers.front() }, SetBack, &HalfApplied)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "half-turn root was not found");
    const BodyReport HalfReport = HalfChamfer ? HalfChamfer.Payload.Validate() : BodyReport{};
    Panel.Expect("The half-turn cone–cone chamfer commits exact V10/E14/F6 topology",
                 HalfChamfer && HalfApplied == 1 && HalfReport.Solid() && HalfReport.Hulls == 1 && HalfReport.Genus == 0 &&
                 HalfChamfer.Payload.Vertices.size() == 10 && HalfChamfer.Payload.Edges.size() == 14 &&
                 HalfChamfer.Payload.Coedges.size() == 28 && HalfChamfer.Payload.Loops.size() == 6 &&
                 HalfChamfer.Payload.Faces.size() == 6 && HalfReport.OpenEdges == 0 && HalfReport.NonManifoldEdges == 0 &&
                 HalfReport.MisorientedEdges == 0);

    Panel.Section("Exact mixed-support reconstruction");
    int Applied = 0;
    Deliver<BrepBody> Result = !Members.empty()
        ? BlendSolver::ChamferEdges(Source, { Members.front() }, SetBack, &Applied)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "cone-cone root was not found");
    Panel.Expect("The partial cone–cone chamfer commits one complete chain", Result && Result.Payload.Validate().Solid() && Applied == 1);
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        Panel.Expect("The result is one genus-zero V10/E15/F7 manifold", Report.Hulls == 1 && Report.Genus == 0 &&
                     Result.Payload.Vertices.size() == 10 && Result.Payload.Edges.size() == 15 &&
                     Result.Payload.Coedges.size() == 30 && Result.Payload.Loops.size() == 7 && Result.Payload.Faces.size() == 7 &&
                     Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
        const double Expected = AddedWedge(BaseRadius, RootRadius, TopRadius, LowerHeight, UpperHeight, SetBack, SweepAngle);
        Panel.Within("The sector wedge follows the exact cone–cone meridian integral",
                     std::fabs((Report.Volume - SourceReport.Volume) - Expected) / std::max(1e-9, std::fabs(Expected)), 2e-3);
        Panel.Expect("The radial endpoint caps remain closed and the source remains immutable", SameSource(Source, SourceReport));
    }

    Panel.Section("Transactional refusal boundaries");
    if (!Members.empty())
        Panel.Expect("Zero and consuming setbacks refuse without mutating the source",
                     !BlendSolver::ChamferEdge(Source, Members.front(), 0.0) &&
                     !BlendSolver::ChamferEdge(Source, Members.front(), std::max(LowerHeight, UpperHeight)) &&
                     SameSource(Source, SourceReport));
    auto Torus = BrepBody::Torus({ 20, 0, 0 }, Axis, 4.0, 1.0);
    Panel.Expect("An arbitrary torus edge remains an explicit refusal", Torus &&
                 !BlendSolver::ChamferEdge(Torus.Payload, 0, SetBack));

    Panel.Section("Console transaction and visible proof");
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Host.Document().AddBody("PartialConeConeSource", Source).Identity > 0;
    const bool ConsoleCommit = Added && !Members.empty() &&
        Host.Execute("chamfer PartialConeConeSource 0.5 --edges=" + std::to_string(Members.front()) + " --name=PartialConeConeChamfer") &&
        Host.Document().Find("PartialConeConeChamfer") &&
        Host.Document().Find("PartialConeConeChamfer")->Body.Validate().Solid();
    Panel.Expect("The console commits the bounded cone–cone route", ConsoleCommit);

    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase36l_PartialConeConeChamfer.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1600, 900);
    Deliver<BrepBody> ProofResult = !Members.empty()
        ? BlendSolver::ChamferEdge(Source, Members.front(), SetBack)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "proof fixture is unavailable");
    const bool Rendered = SourceResult && ProofResult &&
        ProofHost.Document().AddBody("SharpPartialConeCone", Source.Transformed(Mat4::Translation({ -11, 0, 0 }))).Identity > 0 &&
        ProofHost.Document().AddBody("ChamferedPartialConeCone", ProofResult.Payload.Transformed(Mat4::Translation({ 11, 0, 0 }))).Identity > 0 &&
        ProofHost.Execute("view top") && ProofHost.Execute("view fit") && ProofHost.Execute("render Phase36l_PartialConeConeChamfer");
    Panel.Expect("The cone–cone source/result proof render completes", Rendered);
    Panel.Expect("The cone–cone proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
