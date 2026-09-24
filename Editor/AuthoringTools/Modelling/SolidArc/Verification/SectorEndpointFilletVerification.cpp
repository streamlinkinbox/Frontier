//=============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Verification/SectorEndpointFilletVerification.cpp — Phase 32d radial ends
//=============================================================================================================================================
// General-angle plane–cylinder root chains terminate on two radial caps sharing the rotation-axis edge. Internal angular
// representation seams heal, while both exact rolling-surface meridians remain as physical endpoint boundaries.
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
[[nodiscard]] std::vector<NurbsSurface> AngularPatches(const NurbsSurface& Surface, int Count) noexcept
{
    std::vector<NurbsSurface> Patches{ Surface };
    while (static_cast<int>(Patches.size()) < Count)
    {
        std::vector<NurbsSurface> Split;
        for (const NurbsSurface& Patch : Patches)
        {
            auto Halves = Patch.SplitU(0.5 * (Patch.DomainStartU() + Patch.DomainEndU()));
            Split.push_back(std::move(Halves.first)); Split.push_back(std::move(Halves.second));
        }
        Patches = std::move(Split);
    }
    return Patches;
}

[[nodiscard]] bool CapRadialEnds(BrepBody& Body, Vec3 AxisStart, Vec3 AxisEnd, Vec3 RadialStart,
                                 double SweepAngle, double RadialExtent) noexcept
{
    Vec3 Axis = (AxisEnd - AxisStart).Normalised();
    RadialStart = (RadialStart - Axis * RadialStart.Dot(Axis)).Normalised();
    Vec3 RadialEnd = RadialStart * std::cos(SweepAngle) + Axis.Cross(RadialStart) * std::sin(SweepAngle);
    std::vector<int> StartEdges, EndEdges;
    for (size_t Edge = 0; Edge < Body.Edges.size(); ++Edge)
    {
        if (Body.Edges[Edge].Coedges.size() != 1) continue;
        const NurbsCurve& Curve = Body.Edges[Edge].Curve;
        Vec3 Middle = Curve.Sample(0.5 * (Curve.DomainStart() + Curve.DomainEnd()));
        Vec3 Radial = Middle - (AxisStart + Axis * (Middle - AxisStart).Dot(Axis));
        if (Radial.Length() <= ScalarCriteria::MergeTolerance) return false;
        Radial = Radial.Normalised();
        (std::fabs(Radial.Dot(RadialStart)) >= std::fabs(Radial.Dot(RadialEnd))
            ? StartEdges : EndEdges).push_back(static_cast<int>(Edge));
    }
    if (StartEdges.empty() || EndEdges.empty()) return false;
    Deliver<NurbsCurve> AxisCurve = NurbsCurve::Line(AxisStart, AxisEnd);
    if (!AxisCurve) return false;
    int AxisEdge = Body.AddEdge(AxisCurve.Payload, ScalarCriteria::MergeTolerance);
    int StartVertex = Body.AddVertex(AxisStart, ScalarCriteria::MergeTolerance);
    int EndVertex = Body.AddVertex(AxisEnd, ScalarCriteria::MergeTolerance);
    auto AddCap = [&](std::vector<int> Edges, Vec3 Radial) noexcept
    {
        std::vector<std::pair<int, bool>> Path;
        int Current = StartVertex;
        while (Current != EndVertex)
        {
            auto It = std::find_if(Edges.begin(), Edges.end(), [&](int Edge)
            { return Body.Edges[Edge].VertexStart == Current || Body.Edges[Edge].VertexEnd == Current; });
            if (It == Edges.end()) return false;
            int Edge = *It;
            bool Reversed = Body.Edges[Edge].VertexEnd == Current;
            Current = Reversed ? Body.Edges[Edge].VertexStart : Body.Edges[Edge].VertexEnd;
            Path.push_back({ Edge, Reversed }); Edges.erase(It);
        }
        if (!Edges.empty()) return false;
        double Pad = 0.01 * std::max(RadialExtent, AxisStart.Distance(AxisEnd)) + ScalarCriteria::MergeTolerance;
        Deliver<NurbsSurface> Plane = NurbsSurface::Plane(AxisStart - Radial * Pad - Axis * Pad,
            Radial, Axis, RadialExtent + 2.0 * Pad, AxisStart.Distance(AxisEnd) + 2.0 * Pad);
        if (!Plane) return false;
        int Face = Body.AddFace(std::move(Plane.Payload)); Body.Faces[Face].Natural = false;
        int Loop = Body.AddLoop(Face, true);
        for (const auto& [Edge, Reversed] : Path) Body.AddCoedge(Edge, Reversed, Face, Loop);
        Body.AddCoedge(AxisEdge, Body.Edges[AxisEdge].VertexStart == EndVertex, Face, Loop);
        return true;
    };
    if (!AddCap(StartEdges, RadialStart) || !AddCap(EndEdges, RadialEnd)) return false;
    Body.Orient();
    return Body.Validate().Solid();
}

[[nodiscard]] Deliver<BrepBody> SectorBoss(Vec3 Base, Vec3 Axis, double OuterRadius, double ShoulderHeight,
                                            double BossRadius, double BossHeight, double SweepAngle,
                                            int AngularSegments) noexcept
{
    if (AngularSegments <= 0 || (AngularSegments & (AngularSegments - 1)) != 0 ||
        std::fabs(SweepAngle) <= 1e-6 || std::fabs(SweepAngle) >= ScalarCriteria::TwoPi - 1e-6)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "invalid sector support sweep or segmentation");
    Axis = Axis.Normalised();
    Vec3 Radial = Workplane::FromNormal(Base, Axis).AxisX;
    Vec3 ShoulderCentre = Base + Axis * ShoulderHeight;
    Vec3 BossTop = ShoulderCentre + Axis * BossHeight;
    std::vector<std::pair<Vec3, Vec3>> Profile{
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
        std::vector<NurbsSurface> Patches = AngularPatches(Surface.Payload, AngularSegments);
        Faces.insert(Faces.end(), std::make_move_iterator(Patches.begin()), std::make_move_iterator(Patches.end()));
    }
    Deliver<BrepBody> Result = BrepBody::Sew(Faces);
    if (!Result || !CapRadialEnds(Result.Payload, Base, BossTop, Radial, SweepAngle, OuterRadius))
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "sector support could not close its radial caps");
    return Result;
}

[[nodiscard]] bool CircularFrame(const NurbsCurve& Curve, Vec3& Centre, Vec3& Normal, double& Radius) noexcept
{
    if ((Curve.Classification != CurveClassification::Arc && Curve.Classification != CurveClassification::Circle) ||
        Curve.Degree != 2 || !Curve.Rational()) return false;
    double T0 = Curve.DomainStart(), T1 = Curve.DomainEnd();
    Vec3 P0 = Curve.Sample(T0), P1 = Curve.Sample(0.5 * (T0 + T1)), P2 = Curve.Sample(T1);
    Vec3 U = P1 - P0, V = P2 - P0, Cross = U.Cross(V);
    double Denominator = 2.0 * Cross.LengthSquared();
    if (Denominator <= ScalarCriteria::KernelTolerance) return false;
    Centre = P0 + (Cross.Cross(U) * V.LengthSquared() + V.Cross(Cross) * U.LengthSquared()) / Denominator;
    Radius = Centre.Distance(P0); Normal = Cross.Normalised();
    return Radius > ScalarCriteria::MergeTolerance;
}

[[nodiscard]] std::vector<int> CircleEdges(const BrepBody& Body, Vec3 Centre, Vec3 Axis, double Radius) noexcept
{
    std::vector<int> Edges; Axis = Axis.Normalised();
    for (size_t Edge = 0; Edge < Body.Edges.size(); ++Edge)
    {
        Vec3 CandidateCentre, Normal; double CandidateRadius = 0.0;
        if (CircularFrame(Body.Edges[Edge].Curve, CandidateCentre, Normal, CandidateRadius) &&
            CandidateCentre.Distance(Centre) < 1e-8 && std::fabs(CandidateRadius - Radius) < 1e-8 &&
            std::fabs(Normal.Dot(Axis)) > 1.0 - 1e-8) Edges.push_back(static_cast<int>(Edge));
    }
    return Edges;
}

[[nodiscard]] int EndpointCount(const BrepBody& Body, const std::vector<int>& Chain) noexcept
{
    std::vector<int> Degree(Body.Vertices.size(), 0);
    for (int Edge : Chain) { ++Degree[Body.Edges[Edge].VertexStart]; ++Degree[Body.Edges[Edge].VertexEnd]; }
    return static_cast<int>(std::count(Degree.begin(), Degree.end(), 1));
}

struct Inspection
{
    bool Topology = false, Analytic = false, EndCaps = false, Supports = false;
    double Residual = ScalarCriteria::Infinity, PlaneG1 = ScalarCriteria::Infinity;
    double CylinderG1 = ScalarCriteria::Infinity, SpanError = ScalarCriteria::Infinity;
};

[[nodiscard]] Inspection Inspect(const BrepBody& Body, Vec3 Base, Vec3 Axis, double OuterRadius,
                                 double ShoulderHeight, double BossRadius, double BossHeight,
                                 double Radius, double SweepAngle) noexcept
{
    Inspection Result; Axis = Axis.Normalised();
    BodyReport Report = Body.Validate();
    Result.Topology = Report.Solid() && Report.Hulls == 1 && Report.Genus == 0 &&
        Body.Vertices.size() == 12 && Body.Edges.size() == 18 && Body.Coedges.size() == 36 &&
        Body.Loops.size() == 8 && Body.Faces.size() == 8;
    int RollFace = -1;
    for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
        if (Body.Faces[Face].Surface.Classification == SurfaceClassification::Torus) RollFace = static_cast<int>(Face);
    if (RollFace < 0) return Result;
    const NurbsSurface& Roll = Body.Faces[RollFace].Surface;
    Vec3 Origin = Base + Axis * (ShoulderHeight + Radius);
    Result.Analytic = !Roll.ClosedU() && Roll.Origin.Distance(Origin) < 1e-9 &&
        Roll.Axis.Normalised().Dot(Axis) > 1.0 - 1e-12 &&
        std::fabs(Roll.RadiusMajor - (BossRadius + Radius)) < 1e-10 &&
        std::fabs(Roll.RadiusMinor - Radius) < 1e-10;
    Result.Residual = 0.0; Result.PlaneG1 = 0.0; Result.CylinderG1 = 0.0;
    double U0 = Roll.DomainStartU(), U1 = Roll.DomainEndU(), V0 = Roll.DomainStartV(), V1 = Roll.DomainEndV();
    auto RadialAt = [&](double U)
    {
        Vec3 Point = Roll.Sample(U, 0.5 * (V0 + V1));
        return (Point - (Origin + Axis * (Point - Origin).Dot(Axis))).Normalised();
    };
    Vec3 R0 = RadialAt(U0), RM = RadialAt(0.5 * (U0 + U1)), R1 = RadialAt(U1);
    auto Turn = [&](Vec3 A, Vec3 B)
    { return std::atan2(Axis.Dot(A.Cross(B)), ScalarCriteria::Clamp(A.Dot(B), -1.0, 1.0)); };
    Result.SpanError = std::fabs(std::fabs(Turn(R0, RM) + Turn(RM, R1)) - std::fabs(SweepAngle));
    for (int I = 0; I < 9; ++I)
    {
        double U = U0 + (U1 - U0) * (static_cast<double>(I) / 8.0);
        for (int J = 0; J < 5; ++J)
        {
            Vec3 Point = Roll.Sample(U, V0 + (V1 - V0) * (static_cast<double>(J) / 4.0));
            double Along = (Point - Origin).Dot(Axis);
            Vec3 Radial = Point - (Origin + Axis * Along);
            double Q = Radial.Length() - (BossRadius + Radius);
            Result.Residual = std::max(Result.Residual, std::fabs(Q * Q + Along * Along - Radius * Radius));
        }
        Result.PlaneG1 = std::max(Result.PlaneG1,
            1.0 - Body.FaceNormal(RollFace, U, V0).Normalised().Dot(Axis));
        Vec3 Point = Roll.Sample(U, V1);
        Vec3 Radial = (Point - (Origin + Axis * (Point - Origin).Dot(Axis))).Normalised();
        Result.CylinderG1 = std::max(Result.CylinderG1,
            1.0 - Body.FaceNormal(RollFace, U, V1).Normalised().Dot(Radial));
    }
    int Cylinders = 0, RadialCaps = 0, AxisEdges = 0, TorusEdges = 0, RadiusArcs = 0;
    bool Outer = false, Boss = false;
    Vec3 BossTop = Base + Axis * (ShoulderHeight + BossHeight);
    for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
    {
        const NurbsSurface& Surface = Body.Faces[Face].Surface;
        if (Surface.Classification == SurfaceClassification::Cylinder)
        {
            ++Cylinders;
            if (std::fabs(Surface.RadiusMajor - OuterRadius) < 1e-9) Outer = true;
            if (std::fabs(Surface.RadiusMajor - BossRadius) < 1e-9) Boss = true;
        }
        if (Surface.Classification == SurfaceClassification::Plane)
        {
            Vec3 Normal = Body.FaceNormal(static_cast<int>(Face),
                0.5 * (Surface.DomainStartU() + Surface.DomainEndU()),
                0.5 * (Surface.DomainStartV() + Surface.DomainEndV())).Normalised();
            if (std::fabs(Normal.Dot(Axis)) < 1e-8) ++RadialCaps;
        }
    }
    for (size_t Edge = 0; Edge < Body.Edges.size(); ++Edge)
    {
        const BrepEdge& Candidate = Body.Edges[Edge];
        Vec3 Start = Body.Vertices[Candidate.VertexStart].Point, End = Body.Vertices[Candidate.VertexEnd].Point;
        if ((Start.Distance(Base) < 1e-8 && End.Distance(BossTop) < 1e-8) ||
            (End.Distance(Base) < 1e-8 && Start.Distance(BossTop) < 1e-8)) ++AxisEdges;
        Vec3 Centre, Normal; double ArcRadius = 0.0;
        if (CircularFrame(Candidate.Curve, Centre, Normal, ArcRadius) && std::fabs(ArcRadius - Radius) < 1e-8) ++RadiusArcs;
    }
    for (int Loop : Body.Faces[RollFace].Loops) TorusEdges += static_cast<int>(Body.Loops[Loop].Coedges.size());
    Result.EndCaps = RadialCaps == 2 && AxisEdges == 1 && TorusEdges == 4 && RadiusArcs == 2;
    Result.Supports = Cylinders == 2 && Outer && Boss;
    return Result;
}

[[nodiscard]] double ExactSectorVolume(double OuterRadius, double ShoulderHeight, double BossRadius,
                                       double BossHeight, double Radius, double SweepAngle) noexcept
{
    double FullOriginal = ScalarCriteria::Pi *
        (OuterRadius * OuterRadius * ShoulderHeight + BossRadius * BossRadius * BossHeight);
    double FullAdded = 2.0 * ScalarCriteria::Pi * Radius * Radius *
        (BossRadius * (1.0 - ScalarCriteria::Pi / 4.0) +
         Radius * (5.0 / 6.0 - ScalarCriteria::Pi / 4.0));
    return std::fabs(SweepAngle) / ScalarCriteria::TwoPi * (FullOriginal + FullAdded);
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 32d · Sector Endpoint Fillet Verification — general radial end pairs");
    const Vec3 Base{ 0, 0, 0 }, Axis{ 0, 0, 1 };
    constexpr double OuterRadius = 10.0, ShoulderHeight = 8.0, BossRadius = 5.0, BossHeight = 7.0, Radius = 2.0;
    constexpr double Quarter = ScalarCriteria::Pi * 0.5;

    Panel.Section("Quarter-sector chain and exact radial endpoint reconstruction");
    Deliver<BrepBody> Split2 = SectorBoss(Base, Axis, OuterRadius, ShoulderHeight, BossRadius, BossHeight, Quarter, 2);
    BodyReport SourceReport = Split2 ? Split2.Payload.Validate() : BodyReport{};
    Panel.Expect("The two-patch quarter source is a one-hull genus-zero solid",
                 Split2 && SourceReport.Solid() && SourceReport.Hulls == 1 && SourceReport.Genus == 0);
    Panel.Expect("The quarter source has exact V14/E24/C48/L12/F12 split topology",
                 Split2 && Split2.Payload.Vertices.size() == 14 && Split2.Payload.Edges.size() == 24 &&
                 Split2.Payload.Coedges.size() == 48 && Split2.Payload.Loops.size() == 12 && Split2.Payload.Faces.size() == 12);
    std::vector<int> Root2 = Split2
        ? CircleEdges(Split2.Payload, Base + Axis * ShoulderHeight, Axis, BossRadius) : std::vector<int>{};
    Panel.Expect("The selected quarter root propagates through two members and has two endpoints",
                 Root2.size() == 2 && BlendSolver::TangentChain(Split2.Payload, Root2.front()) &&
                 EndpointCount(Split2.Payload, Root2) == 2);
    Deliver<BrepBody> QuarterRoll = Root2.empty()
        ? Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "quarter root not found")
        : BlendSolver::FilletEdge(Split2.Payload, Root2.back(), Radius);
    Inspection QuarterCheck = QuarterRoll
        ? Inspect(QuarterRoll.Payload, Base, Axis, OuterRadius, ShoulderHeight, BossRadius, BossHeight, Radius, Quarter)
        : Inspection{};
    Panel.Expect("Either representation member commits the quarter-sector roll", static_cast<bool>(QuarterRoll));
    Panel.Expect("The healed general-angle result has exact V12/E18/C36/L8/F8 topology", QuarterCheck.Topology);
    Panel.Expect("The rolling face retains exact analytic partial-torus identity", QuarterCheck.Analytic);
    Panel.Within("Quarter-torus implicit residual", QuarterCheck.Residual, 1e-9);
    Panel.Within("Quarter-sector shoulder G1 break", QuarterCheck.PlaneG1, 1e-10);
    Panel.Within("Quarter-sector boss G1 break", QuarterCheck.CylinderG1, 1e-10);
    Panel.Within("The retained rolling surface spans exactly 90 degrees", QuarterCheck.SpanError, 1e-10);
    Panel.Expect("Two radial caps, one axis edge, and two exact R2 meridians remain", QuarterCheck.EndCaps);
    Panel.Expect("Both exact cylindrical supports remain after trimming", QuarterCheck.Supports);
    double ExpectedQuarter = ExactSectorVolume(OuterRadius, ShoulderHeight, BossRadius, BossHeight, Radius, Quarter);
    Panel.Expect("The concave quarter-sector roll adds material", QuarterRoll && QuarterRoll.Payload.Validate().Volume > SourceReport.Volume);
    Panel.Within("Quarter-sector volume follows its angular fraction of the exact full roll",
                 QuarterRoll ? std::fabs(QuarterRoll.Payload.Validate().Volume - ExpectedQuarter) : ScalarCriteria::Infinity,
                 ExpectedQuarter * 1e-3);
    Panel.Expect("Filleting leaves the representation-split source unchanged",
                 Split2 && Split2.Payload.Vertices.size() == 14 && Split2.Payload.Edges.size() == 24 &&
                 std::fabs(Split2.Payload.Validate().Volume - SourceReport.Volume) < 1e-12);

    Panel.Section("Split, sweep-direction, and angular-span invariance");
    Deliver<BrepBody> Split4 = SectorBoss(Base, Axis, OuterRadius, ShoulderHeight, BossRadius, BossHeight, Quarter, 4);
    std::vector<int> Root4 = Split4
        ? CircleEdges(Split4.Payload, Base + Axis * ShoulderHeight, Axis, BossRadius) : std::vector<int>{};
    Deliver<BrepBody> FourRoll = Root4.size() == 4
        ? BlendSolver::FilletEdge(Split4.Payload, Root4[2], Radius)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "four-member quarter root not found");
    Panel.Expect("A four-member quarter source propagates and heals to the same canonical topology",
                 Root4.size() == 4 && FourRoll && Inspect(FourRoll.Payload, Base, Axis, OuterRadius,
                    ShoulderHeight, BossRadius, BossHeight, Radius, Quarter).Topology);
    Panel.Within("Two/four-member quarter results are volume-identical",
                 QuarterRoll && FourRoll
                    ? std::fabs(QuarterRoll.Payload.Validate().Volume - FourRoll.Payload.Validate().Volume)
                    : ScalarCriteria::Infinity, 1e-9);

    Deliver<BrepBody> NegativeSource = SectorBoss(Base, Axis, OuterRadius, ShoulderHeight, BossRadius, BossHeight, -Quarter, 2);
    std::vector<int> NegativeRoot = NegativeSource
        ? CircleEdges(NegativeSource.Payload, Base + Axis * ShoulderHeight, Axis, BossRadius) : std::vector<int>{};
    Deliver<BrepBody> NegativeRoll = NegativeRoot.empty()
        ? Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "negative quarter root not found")
        : BlendSolver::FilletEdge(NegativeSource.Payload, NegativeRoot.front(), Radius);
    Inspection NegativeCheck = NegativeRoll
        ? Inspect(NegativeRoll.Payload, Base, Axis, OuterRadius, ShoulderHeight, BossRadius, BossHeight, Radius, -Quarter)
        : Inspection{};
    Box3 PositiveBounds = QuarterRoll ? QuarterRoll.Payload.Bounds() : Box3{};
    Box3 NegativeBounds = NegativeRoll ? NegativeRoll.Payload.Bounds() : Box3{};
    Panel.Expect("A negative quarter sweep retains the opposite radial sector",
                 NegativeRoll && NegativeCheck.Topology && NegativeCheck.EndCaps &&
                 PositiveBounds.Low.Y > -1e-8 && NegativeBounds.High.Y < 1e-8 && NegativeBounds.Low.Y < -9.9);
    Panel.Within("Positive and negative quarter sectors have equal volume",
                 QuarterRoll && NegativeRoll
                    ? std::fabs(QuarterRoll.Payload.Validate().Volume - NegativeRoll.Payload.Validate().Volume)
                    : ScalarCriteria::Infinity, 1e-9);

    constexpr double OneTwenty = 2.0 * ScalarCriteria::Pi / 3.0;
    Deliver<BrepBody> OneTwentySource = SectorBoss(Base, Axis, OuterRadius, ShoulderHeight, BossRadius, BossHeight, OneTwenty, 2);
    std::vector<int> OneTwentyRoot = OneTwentySource
        ? CircleEdges(OneTwentySource.Payload, Base + Axis * ShoulderHeight, Axis, BossRadius) : std::vector<int>{};
    Deliver<BrepBody> OneTwentyRoll = OneTwentyRoot.empty()
        ? Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "120-degree root not found")
        : BlendSolver::FilletEdge(OneTwentySource.Payload, OneTwentyRoot.front(), Radius);
    Inspection OneTwentyCheck = OneTwentyRoll
        ? Inspect(OneTwentyRoll.Payload, Base, Axis, OuterRadius, ShoulderHeight, BossRadius, BossHeight, Radius, OneTwenty)
        : Inspection{};
    Panel.Expect("A 120-degree chain preserves exact radial endpoints and torus span",
                 OneTwentyRoll && OneTwentyCheck.Topology && OneTwentyCheck.EndCaps && OneTwentyCheck.SpanError < 1e-10);
    Panel.Within("120-degree volume follows the exact angular fraction",
                 OneTwentyRoll ? std::fabs(OneTwentyRoll.Payload.Validate().Volume -
                    ExactSectorVolume(OuterRadius, ShoulderHeight, BossRadius, BossHeight, Radius, OneTwenty))
                                : ScalarCriteria::Infinity,
                 ExactSectorVolume(OuterRadius, ShoulderHeight, BossRadius, BossHeight, Radius, OneTwenty) * 1e-3);

    constexpr double Reflex = 1.5 * ScalarCriteria::Pi;
    Deliver<BrepBody> ReflexSource = SectorBoss(Base, Axis, OuterRadius, ShoulderHeight, BossRadius, BossHeight, Reflex, 4);
    std::vector<int> ReflexRoot = ReflexSource
        ? CircleEdges(ReflexSource.Payload, Base + Axis * ShoulderHeight, Axis, BossRadius) : std::vector<int>{};
    Deliver<BrepBody> ReflexRoll = ReflexRoot.empty()
        ? Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "270-degree root not found")
        : BlendSolver::FilletEdge(ReflexSource.Payload, ReflexRoot.back(), Radius);
    Inspection ReflexCheck = ReflexRoll
        ? Inspect(ReflexRoll.Payload, Base, Axis, OuterRadius, ShoulderHeight, BossRadius, BossHeight, Radius, Reflex)
        : Inspection{};
    Panel.Expect("A 270-degree reflex sector retains the major angular side", ReflexRoll && ReflexCheck.Topology && ReflexCheck.SpanError < 1e-10);
    Panel.Within("270-degree volume follows the exact angular fraction",
                 ReflexRoll ? std::fabs(ReflexRoll.Payload.Validate().Volume -
                    ExactSectorVolume(OuterRadius, ShoulderHeight, BossRadius, BossHeight, Radius, Reflex))
                            : ScalarCriteria::Infinity,
                 ExactSectorVolume(OuterRadius, ShoulderHeight, BossRadius, BossHeight, Radius, Reflex) * 1e-3);

    Panel.Section("Transformed axes, transactions, and bounded refusal");
    Vec3 TiltBase{ 2, -3, 4 }, TiltAxis{ 1, 2, 3 }; TiltAxis = TiltAxis.Normalised();
    Deliver<BrepBody> TiltSource = SectorBoss(TiltBase, TiltAxis, 9.0, 6.0, 4.0, 5.0, OneTwenty, 2);
    std::vector<int> TiltRoot = TiltSource
        ? CircleEdges(TiltSource.Payload, TiltBase + TiltAxis * 6.0, TiltAxis, 4.0) : std::vector<int>{};
    Deliver<BrepBody> TiltRoll = TiltRoot.empty()
        ? Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "tilted root not found")
        : BlendSolver::FilletEdge(TiltSource.Payload, TiltRoot.front(), 1.25);
    Inspection TiltCheck = TiltRoll ? Inspect(TiltRoll.Payload, TiltBase, TiltAxis, 9.0, 6.0, 4.0, 5.0, 1.25, OneTwenty) : Inspection{};
    Panel.Expect("Shifted oblique general-angle supports retain exact topology and identity",
                 TiltRoll && TiltCheck.Topology && TiltCheck.Analytic && TiltCheck.EndCaps);

    int Applied = -1;
    Deliver<BrepBody> Transaction = Root4.size() == 4
        ? BlendSolver::FilletEdges(Split4.Payload, { Root4[3], Root4[0], Root4[3], Root4[2], Root4[1] }, Radius, &Applied)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "transaction root not found");
    Panel.Expect("A repeated all-member sector selection commits one deduplicated chain", Transaction && Applied == 1);
    Panel.Within("Transactional and direct sector routes are volume-identical",
                 Transaction && FourRoll
                    ? std::fabs(Transaction.Payload.Validate().Volume - FourRoll.Payload.Validate().Volume)
                    : ScalarCriteria::Infinity, 1e-9);

    BrepBody BadCap = Split2.Payload;
    bool MovedCap = false;
    for (size_t Face = 0; Face < BadCap.Faces.size(); ++Face)
    {
        NurbsSurface& Surface = BadCap.Faces[Face].Surface;
        if (Surface.Classification != SurfaceClassification::Plane) continue;
        Vec3 Normal = BadCap.FaceNormal(static_cast<int>(Face),
            0.5 * (Surface.DomainStartU() + Surface.DomainEndU()),
            0.5 * (Surface.DomainStartV() + Surface.DomainEndV())).Normalised();
        if (std::fabs(Normal.Dot(Axis)) < 1e-8)
        {
            Surface = Surface.Transformed(Mat4::Translation(Normal * 0.1));
            MovedCap = true; break;
        }
    }
    Panel.Expect("A geometrically displaced radial cap remains a topological solid fixture", MovedCap && BadCap.Validate().Solid());
    Panel.Expect("A radial cap not lying on its chain endpoint plane refuses without approximation",
                 !Root2.empty() && !BlendSolver::FilletEdge(BadCap, Root2.front(), Radius));
    Panel.Expect("A radius consuming the finite boss still refuses",
                 !Root2.empty() && !BlendSolver::FilletEdge(Split2.Payload, Root2.front(), BossHeight));

    Panel.Section("Console path and visual proof");
#ifndef SOLIDARC_PROOF_FOLDER
#error SOLIDARC_PROOF_FOLDER must be supplied by the build
#endif
    ConsoleHost CommandHost(SOLIDARC_PROOF_FOLDER, 1280, 800);
    SceneFigure& SourceFigure = CommandHost.Document().AddBody("QuarterBoss", Split2.Payload); SourceFigure.Matcap = 6;
    bool CommandAccepted = !Root2.empty() && CommandHost.Execute(
        "fillet QuarterBoss 2 --edges=" + std::to_string(Root2.front()) + " --name=QuarterRoll");
    const SceneFigure* CommandResult = CommandHost.Document().Find("QuarterRoll");
    Panel.Expect("The C++ console accepts a general-angle chain seed", CommandAccepted);
    Panel.Expect("The console commits the exact radial-end topology",
                 CommandResult && Inspect(CommandResult->Body, Base, Axis, OuterRadius, ShoulderHeight,
                                          BossRadius, BossHeight, Radius, Quarter).Topology);

    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase32d_SectorEndpointFillet.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1280, 800);
    auto Reset = [&]() { return ProofHost.Execute("reset") && ProofHost.Execute("gizmo off"); };
    auto Add = [&](const char* Name, BrepBody Body, uint8_t Matcap)
    { SceneFigure& Figure = ProofHost.Document().AddBody(Name, std::move(Body)); Figure.Matcap = Matcap; return true; };
    bool Rendered =
        Reset() && Add("QuarterSource", Split2.Payload.Transformed(Mat4::Translation({ -14, 0, 0 })), 6) &&
        Add("QuarterRoll", QuarterRoll.Payload.Transformed(Mat4::Translation({ 8, 0, 0 })), 3) &&
        ProofHost.Execute("view iso") && ProofHost.Execute("view fit") && ProofHost.Execute("render sheet 0") &&
        Reset() && Add("QuarterEndpoints", QuarterRoll.Payload, 3) && ProofHost.Execute("view top") &&
        ProofHost.Execute("view fit") && ProofHost.Execute("view dolly 0.75") && ProofHost.Execute("render sheet 1") &&
        Reset() && Add("OneTwenty", OneTwentyRoll.Payload, 2) && ProofHost.Execute("view iso") &&
        ProofHost.Execute("view fit") && ProofHost.Execute("render sheet 2") &&
        Reset() && Add("ReflexSector", ReflexRoll.Payload, 5) && ProofHost.Execute("view iso") &&
        ProofHost.Execute("view fit") && ProofHost.Execute("render sheet 3") &&
        ProofHost.Execute("render sheet finalize Phase32d_SectorEndpointFillet");
    Panel.Expect("C++ sector-endpoint proof commands complete without refusal", Rendered);
    Panel.Expect("C++ sector-endpoint proof is written and non-trivial",
                 std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);

    return Panel.Conclude();
}
