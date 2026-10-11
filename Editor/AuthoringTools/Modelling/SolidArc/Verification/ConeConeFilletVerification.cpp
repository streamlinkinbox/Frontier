//=============================================================================================================================================
// SolidArc · Phase 36q · bounded complete cone–cone root fillet
//
// A complete coaxial cone–cone root receives one exact constant-radius circular meridian roll.
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
        if (ScalarCriteria::WithinAngularTolerance(std::fabs(SweepAngle), ScalarCriteria::TwoPi))
            Faces.push_back(std::move(Surface.Payload));
        else
            for (NurbsSurface& Patch : SplitAngular(Surface.Payload)) Faces.push_back(std::move(Patch));
    }
    Deliver<BrepBody> Result = BrepBody::Sew(Faces);
    if (!Result) return Deliver<BrepBody>::Reject(Result.Denial.Reason, Result.Denial.Detail);
    if (ScalarCriteria::WithinAngularTolerance(std::fabs(SweepAngle), ScalarCriteria::Pi) ||
        ScalarCriteria::WithinAngularTolerance(std::fabs(SweepAngle), ScalarCriteria::TwoPi)) return Result;
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


namespace
{
[[nodiscard]] int RootEdge(const BrepBody& Body) noexcept
{
    for (int Edge = 0; Edge < static_cast<int>(Body.Edges.size()); ++Edge)
    {
        const BrepEdge& Candidate = Body.Edges[Edge];
        if (!Candidate.Closed() || Candidate.Coedges.size() != 2) continue;
        int Cones = 0;
        for (int Coedge : Candidate.Coedges)
        {
            const int Face = Body.Coedges[Coedge].Face;
            if (Face >= 0 && Face < static_cast<int>(Body.Faces.size()) &&
                Body.Faces[Face].Surface.Classification == SurfaceClassification::Cone) ++Cones;
        }
        if (Cones == 2) return Edge;
    }
    return -1;
}

[[nodiscard]] double FilletRemoval(double BaseRadius, double RootRadius, double TopRadius,
                                   double LowerHeight, double UpperHeight, double Radius) noexcept
{
    const double LowerAngle = std::atan2(BaseRadius - RootRadius, LowerHeight);
    const double UpperAngle = std::atan2(RootRadius - TopRadius, UpperHeight);
    const double Det = std::sin(UpperAngle - LowerAngle);
    const double LowerSin = std::sin(LowerAngle), LowerCos = std::cos(LowerAngle);
    const double UpperSin = std::sin(UpperAngle), UpperCos = std::cos(UpperAngle);
    const double CentreR = RootRadius + Radius * (LowerSin - UpperSin) / Det;
    const double CentreZ = Radius * (UpperCos - LowerCos) / Det;
    const double LowerContactR = CentreR + Radius * LowerCos;
    const double LowerContactZ = CentreZ + Radius * LowerSin;
    const double UpperContactR = CentreR + Radius * UpperCos;
    const double UpperContactZ = CentreZ + Radius * UpperSin;
    auto Segment = [](double R0, double Z0, double R1, double Z1) noexcept
    { return (Z1 - Z0) * (R0 * R0 + R0 * R1 + R1 * R1) / 6.0; };
    auto Arc = [&](double Start, double End) noexcept
    {
        const double S0 = std::sin(Start), S1 = std::sin(End);
        const double S20 = std::sin(2.0 * Start), S21 = std::sin(2.0 * End);
        return Radius / 2.0 * (CentreR * CentreR * (S1 - S0) +
            2.0 * CentreR * Radius * ((End - Start) / 2.0 + (S21 - S20) / 4.0) +
            Radius * Radius * ((S1 - S0) - (S1 * S1 * S1 - S0 * S0 * S0) / 3.0));
    };
    return ScalarCriteria::TwoPi * std::fabs(
        Segment(LowerContactR, LowerContactZ, RootRadius, 0.0) +
        Segment(RootRadius, 0.0, UpperContactR, UpperContactZ) + Arc(UpperAngle, LowerAngle));
}

[[nodiscard]] double SourceVolume(double BaseRadius, double RootRadius, double TopRadius,
                                  double LowerHeight, double UpperHeight) noexcept
{
    return ScalarCriteria::Pi * LowerHeight *
        (BaseRadius * BaseRadius + BaseRadius * RootRadius + RootRadius * RootRadius) / 3.0 +
        ScalarCriteria::Pi * UpperHeight *
        (RootRadius * RootRadius + RootRadius * TopRadius + TopRadius * TopRadius) / 3.0;
}

[[nodiscard]] Deliver<BrepBody> Complete(Vec3 Base, Vec3 Axis, double BaseRadius, double RootRadius,
                                         double TopRadius, double LowerHeight, double UpperHeight) noexcept
{
    return SectorConeCone(Base, Axis, BaseRadius, RootRadius, TopRadius, LowerHeight, UpperHeight,
                           ScalarCriteria::TwoPi);
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 36q · complete cone–cone root fillet");
    constexpr double BaseRadius = 6.0, RootRadius = 5.0, TopRadius = 3.0;
    constexpr double LowerHeight = 8.0, UpperHeight = 4.0, Radius = 0.5;
    const Vec3 Base{ 0, 0, 0 }, Axis{ 0, 0, 1 };
    const Deliver<BrepBody> SourceResult = Complete(Base, Axis, BaseRadius, RootRadius, TopRadius, LowerHeight, UpperHeight);
    const BrepBody Source = SourceResult.Payload;
    const int Edge = SourceResult ? RootEdge(Source) : -1;
    const BodyReport Before = Source.Validate();

    Panel.Section("Canonical complete coaxial cone–cone root");
    Panel.Expect("The complete cone–cone source is a V5/E7/C14/L4/F4 solid",
                 SourceResult && Before.Solid() && Before.Hulls == 1 && Before.Genus == 0 &&
                 Source.Vertices.size() == 5 && Source.Edges.size() == 7 && Source.Coedges.size() == 14 &&
                 Source.Loops.size() == 4 && Source.Faces.size() == 4);
    Panel.Expect("The closed root is structurally identified between two cone faces", Edge >= 0);
    Deliver<std::vector<int>> Chain = Edge >= 0 ? BlendSolver::TangentChain(Source, Edge)
        : Deliver<std::vector<int>>::Reject(RefusalReason::Unsupported, "root was not found");
    Panel.Expect("The selected closed root propagates as one canonical chain", Chain && Chain.Payload.size() == 1);

    int Applied = 0;
    Deliver<BrepBody> Result = Edge >= 0 ? BlendSolver::FilletEdges(Source, { Edge }, Radius, &Applied)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "root was not found");
    Panel.Expect("The complete cone–cone fillet commits one selected root", Result && Applied == 1 && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport After = Result.Payload.Validate();
        Panel.Expect("The result is one genus-zero V4/E7/C14/L5/F5 solid",
                     After.Hulls == 1 && After.Genus == 0 && Result.Payload.Vertices.size() == 4 &&
                     Result.Payload.Edges.size() == 7 && Result.Payload.Coedges.size() == 14 &&
                     Result.Payload.Loops.size() == 5 && Result.Payload.Faces.size() == 5 &&
                     After.OpenEdges == 0 && After.NonManifoldEdges == 0 && After.MisorientedEdges == 0);
        const double Expected = SourceVolume(BaseRadius, RootRadius, TopRadius, LowerHeight, UpperHeight) -
                                FilletRemoval(BaseRadius, RootRadius, TopRadius, LowerHeight, UpperHeight, Radius);
        Panel.Within("The filleted volume follows the exact cone–cone meridian first moment",
                     std::fabs(After.Volume - Expected) / std::max(1e-9, std::fabs(Expected)), 2e-3);
        int Cones = 0, Tori = 0, Planes = 0;
        for (const BrepFace& Face : Result.Payload.Faces)
        {
            Cones += Face.Surface.Classification == SurfaceClassification::Cone;
            Tori += Face.Surface.Classification == SurfaceClassification::Torus;
            Planes += Face.Surface.Classification == SurfaceClassification::Plane;
        }
        Panel.Expect("The result retains two cone patches, one toroidal roll and two caps", Cones == 2 && Tori == 1 && Planes == 2);
        Panel.Expect("The complete cone–cone source remains immutable", SameSource(Source, Before));
    }

    Panel.Section("Transactional refusal boundaries");
    if (Edge >= 0)
    {
        Panel.Expect("Zero, negative, and consuming radii refuse transactionally",
                     !BlendSolver::FilletEdge(Source, Edge, 0.0) && !BlendSolver::FilletEdge(Source, Edge, -0.1) &&
                     !BlendSolver::FilletEdge(Source, Edge, 20.0) && SameSource(Source, Before));
    }
    const Deliver<BrepBody> Flaring = Complete(Base, Axis, 6.0, 5.0, 6.0, 8.0, 4.0);
    const int FlaringEdge = Flaring ? RootEdge(Flaring.Payload) : -1;
    Panel.Expect("Flaring cone pairs remain explicitly refused", Flaring && FlaringEdge >= 0 &&
                 !BlendSolver::FilletEdge(Flaring.Payload, FlaringEdge, Radius));
    const Deliver<BrepBody> EqualSlope = Complete(Base, Axis, 6.0, 5.0, 4.5, 8.0, 4.0);
    const int EqualEdge = EqualSlope ? RootEdge(EqualSlope.Payload) : -1;
    Panel.Expect("Equal-slope cone pairs remain explicitly refused", EqualSlope && EqualEdge >= 0 &&
                 !BlendSolver::FilletEdge(EqualSlope.Payload, EqualEdge, Radius));
    const Deliver<BrepBody> Apex = Complete(Base, Axis, 6.0, 5.0, 0.0, 8.0, 4.0);
    const int ApexEdge = Apex ? RootEdge(Apex.Payload) : -1;
    Panel.Expect("Apex cone–cone roots remain explicitly refused", !Apex || ApexEdge < 0 ||
                 !BlendSolver::FilletEdge(Apex.Payload, ApexEdge, Radius));
    auto Torus = BrepBody::Torus({ 20, 0, 0 }, Axis, 4.0, 1.0);
    Panel.Expect("An arbitrary torus edge remains explicitly refused", Torus &&
                 !BlendSolver::FilletEdge(Torus.Payload, 0, Radius));

    Panel.Section("Console transaction and visible proof");
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Added = Host.Document().AddBody("ConeConeSource", Source).Identity > 0;
    const bool ConsoleCommit = Added && Edge >= 0 &&
        Host.Execute("fillet ConeConeSource 0.5 --edges=" + std::to_string(Edge) + " --name=ConeConeFillet") &&
        Host.Document().Find("ConeConeFillet") && Host.Document().Find("ConeConeFillet")->Body.Validate().Solid();
    Panel.Expect("The console commits the bounded cone–cone fillet", ConsoleCommit);

    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase36q_ConeConeFillet.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1600, 900);
    Deliver<BrepBody> ProofResult = Edge >= 0 ? BlendSolver::FilletEdge(Source, Edge, Radius)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "proof fixture is unavailable");
    const bool Rendered = SourceResult && ProofResult &&
        ProofHost.Document().AddBody("SharpConeCone", Source.Transformed(Mat4::Translation({ -9, 0, 0 }))).Identity > 0 &&
        ProofHost.Document().AddBody("FilletedConeCone", ProofResult.Payload.Transformed(Mat4::Translation({ 9, 0, 0 }))).Identity > 0 &&
        ProofHost.Execute("view front") && ProofHost.Execute("view fit") && ProofHost.Execute("view dolly 0.7") &&
        ProofHost.Execute("render Phase36q_ConeConeFillet");
    Panel.Expect("The cone–cone source/result proof render completes", Rendered);
    Panel.Expect("The cone–cone proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 10000);
    return Panel.Conclude();
}
