//=============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Verification/PlaneCylinderFilletVerification.cpp — Phase 31 smooth-support fillet
//=============================================================================================================================================
// The first non-cap smooth-support pair is a circular cylindrical boss leaving a planar annular shoulder. Its exact
// rolling-ball patch is a rational quarter torus which adds the root wedge and meets both retained supports at G1.
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{
[[nodiscard]] Deliver<BrepBody> SteppedBoss(Vec3 Base, Vec3 Axis, double OuterRadius, double ShoulderHeight,
                                            double BossRadius, double BossHeight) noexcept
{
    Axis = Axis.Normalised();
    Workplane Frame = Workplane::FromNormal(Base, Axis);
    Vec3 ShoulderCentre = Base + Axis * ShoulderHeight;
    Deliver<NurbsSurface> Outer = NurbsSurface::Cylinder(Base, Axis, OuterRadius, ShoulderHeight);
    Deliver<NurbsCurve> ShoulderLine = NurbsCurve::Line(
        ShoulderCentre + Frame.AxisX * OuterRadius,
        ShoulderCentre + Frame.AxisX * BossRadius);
    Deliver<NurbsSurface> Shoulder = ShoulderLine
        ? NurbsSurface::Revolution(ShoulderLine.Payload, Base, Axis, ScalarCriteria::TwoPi)
        : Deliver<NurbsSurface>::Reject(ShoulderLine.Denial.Reason, ShoulderLine.Denial.Detail);
    Deliver<NurbsSurface> Boss = NurbsSurface::Cylinder(ShoulderCentre, Axis, BossRadius, BossHeight);
    if (!Outer || !Shoulder || !Boss)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "stepped-boss support is degenerate");
    return BrepBody::Sew({ Outer.Payload, Shoulder.Payload, Boss.Payload });
}

[[nodiscard]] bool CircleFrame(const NurbsCurve& Curve, Vec3& Centre, Vec3& Normal, double& Radius) noexcept
{
    if (Curve.Classification != CurveClassification::Circle || Curve.Degree != 2 ||
        !Curve.Rational() || !Curve.Closed()) return false;
    double T0 = Curve.DomainStart(), Span = Curve.DomainEnd() - T0;
    Vec3 P0 = Curve.Sample(T0), P1 = Curve.Sample(T0 + Span * 0.25), P2 = Curve.Sample(T0 + Span * 0.5);
    Vec3 U = P1 - P0, V = P2 - P0, Cross = U.Cross(V);
    double Denominator = 2.0 * Cross.LengthSquared();
    if (Span <= ScalarCriteria::ParametricEpsilon || Denominator <= ScalarCriteria::KernelTolerance) return false;
    Centre = P0 + (Cross.Cross(U) * V.LengthSquared() + V.Cross(Cross) * U.LengthSquared()) / Denominator;
    Radius = Centre.Distance(P0); Normal = Cross.Normalised();
    return Radius > ScalarCriteria::MergeTolerance;
}

[[nodiscard]] int CircularEdgeAt(const BrepBody& Body, Vec3 Centre, Vec3 Axis, double Radius) noexcept
{
    Axis = Axis.Normalised();
    for (size_t Edge = 0; Edge < Body.Edges.size(); ++Edge)
    {
        Vec3 CandidateCentre, Normal; double CandidateRadius = 0.0;
        if (CircleFrame(Body.Edges[Edge].Curve, CandidateCentre, Normal, CandidateRadius) &&
            CandidateCentre.Distance(Centre) < 1e-8 && std::fabs(CandidateRadius - Radius) < 1e-8 &&
            std::fabs(Normal.Dot(Axis)) > 1.0 - 1e-8) return static_cast<int>(Edge);
    }
    return -1;
}

[[nodiscard]] int TorusFace(const BrepBody& Body) noexcept
{
    for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
        if (Body.Faces[Face].Surface.Classification == SurfaceClassification::Torus) return static_cast<int>(Face);
    return -1;
}

struct FilletInspection
{
    bool Topology = false;
    bool AnalyticIdentity = false;
    bool Supports = false;
    double TorusResidual = ScalarCriteria::Infinity;
    double ShoulderTangentBreak = ScalarCriteria::Infinity;
    double CylinderTangentBreak = ScalarCriteria::Infinity;
};

[[nodiscard]] FilletInspection InspectFillet(const BrepBody& Body, Vec3 Base, Vec3 Axis,
                                             double OuterRadius, double ShoulderHeight,
                                             double BossRadius, double BossHeight, double Radius) noexcept
{
    FilletInspection Result;
    Axis = Axis.Normalised();
    BodyReport Report = Body.Validate();
    Result.Topology = Report.Solid() && Body.Vertices.size() == 5 && Body.Edges.size() == 9 &&
        Body.Coedges.size() == 18 && Body.Loops.size() == 6 && Body.Faces.size() == 6;
    int RollFace = TorusFace(Body);
    if (RollFace < 0) return Result;

    const NurbsSurface& Roll = Body.Faces[RollFace].Surface;
    Vec3 ExpectedOrigin = Base + Axis * (ShoulderHeight + Radius);
    Result.AnalyticIdentity = Roll.Origin.Distance(ExpectedOrigin) < 1e-9 &&
        Roll.Axis.Normalised().Dot(Axis) > 1.0 - 1e-12 &&
        std::fabs(Roll.RadiusMajor - (BossRadius + Radius)) < 1e-10 &&
        std::fabs(Roll.RadiusMinor - Radius) < 1e-10;

    Result.TorusResidual = 0.0;
    Result.ShoulderTangentBreak = 0.0;
    Result.CylinderTangentBreak = 0.0;
    double U0 = Roll.DomainStartU(), U1 = Roll.DomainEndU();
    double V0 = Roll.DomainStartV(), V1 = Roll.DomainEndV();
    for (int I = 0; I < 9; ++I)
    {
        double U = U0 + (U1 - U0) * (static_cast<double>(I) / 8.0);
        for (int J = 0; J < 5; ++J)
        {
            double V = V0 + (V1 - V0) * (static_cast<double>(J) / 4.0);
            Vec3 Point = Roll.Sample(U, V);
            double Along = (Point - ExpectedOrigin).Dot(Axis);
            Vec3 Radial = Point - (ExpectedOrigin + Axis * Along);
            double Q = Radial.Length() - (BossRadius + Radius);
            Result.TorusResidual = std::max(Result.TorusResidual, std::fabs(Q * Q + Along * Along - Radius * Radius));
        }
        Vec3 ShoulderNormal = Body.FaceNormal(RollFace, U, V0).Normalised();
        Result.ShoulderTangentBreak = std::max(Result.ShoulderTangentBreak, 1.0 - ShoulderNormal.Dot(Axis));
        Vec3 BossPoint = Roll.Sample(U, V1);
        double Along = (BossPoint - ExpectedOrigin).Dot(Axis);
        Vec3 BossRadial = (BossPoint - (ExpectedOrigin + Axis * Along)).Normalised();
        Vec3 BossNormal = Body.FaceNormal(RollFace, U, V1).Normalised();
        Result.CylinderTangentBreak = std::max(Result.CylinderTangentBreak, 1.0 - BossNormal.Dot(BossRadial));
    }

    int Cylinders = 0, Caps = 0, Shoulder = 0;
    bool OuterOkay = false, BossOkay = false, ShoulderOkay = false;
    for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
    {
        const NurbsSurface& Surface = Body.Faces[Face].Surface;
        if (Surface.Classification == SurfaceClassification::Cylinder)
        {
            ++Cylinders;
            double SU0 = Surface.DomainStartU(), SU1 = Surface.DomainEndU();
            double SV0 = Surface.DomainStartV(), SV1 = Surface.DomainEndV();
            Vec3 C0 = (Surface.Sample(SU0, SV0) + Surface.Sample(0.5 * (SU0 + SU1), SV0)) * 0.5;
            Vec3 C1 = (Surface.Sample(SU0, SV1) + Surface.Sample(0.5 * (SU0 + SU1), SV1)) * 0.5;
            double T0 = (C0 - Base).Dot(Axis), T1 = (C1 - Base).Dot(Axis);
            double Low = std::min(T0, T1), High = std::max(T0, T1);
            if (std::fabs(Surface.RadiusMajor - OuterRadius) < 1e-9)
                OuterOkay = std::fabs(Low) < 1e-8 && std::fabs(High - ShoulderHeight) < 1e-8;
            if (std::fabs(Surface.RadiusMajor - BossRadius) < 1e-9)
                BossOkay = std::fabs(Low - (ShoulderHeight + Radius)) < 1e-8 &&
                    std::fabs(High - (ShoulderHeight + BossHeight)) < 1e-8;
        }
        else if (Surface.Classification == SurfaceClassification::Plane) ++Caps;
        else if (static_cast<int>(Face) != RollFace)
        {
            ++Shoulder;
            double SU0 = Surface.DomainStartU(), SU1 = Surface.DomainEndU();
            double SV0 = Surface.DomainStartV(), SV1 = Surface.DomainEndV();
            double MinRadius = ScalarCriteria::Infinity, MaxRadius = 0.0, PlaneError = 0.0;
            Vec3 ShoulderCentre = Base + Axis * ShoulderHeight;
            for (int I = 0; I < 5; ++I)
                for (int J = 0; J < 3; ++J)
                {
                    Vec3 Point = Surface.Sample(SU0 + (SU1 - SU0) * (static_cast<double>(I) / 4.0),
                                                SV0 + (SV1 - SV0) * (static_cast<double>(J) / 2.0));
                    double Along = (Point - ShoulderCentre).Dot(Axis);
                    double R = (Point - (ShoulderCentre + Axis * Along)).Length();
                    MinRadius = std::min(MinRadius, R); MaxRadius = std::max(MaxRadius, R);
                    PlaneError = std::max(PlaneError, std::fabs(Along));
                }
            ShoulderOkay = PlaneError < 1e-9 && std::fabs(MinRadius - (BossRadius + Radius)) < 1e-8 &&
                std::fabs(MaxRadius - OuterRadius) < 1e-8;
        }
    }
    Result.Supports = Cylinders == 2 && Caps == 2 && Shoulder == 1 && OuterOkay && BossOkay && ShoulderOkay;
    return Result;
}

[[nodiscard]] double AnalyticFilletVolume(double OuterRadius, double ShoulderHeight,
                                          double BossRadius, double BossHeight, double Radius) noexcept
{
    const double Original = ScalarCriteria::Pi *
        (OuterRadius * OuterRadius * ShoulderHeight + BossRadius * BossRadius * BossHeight);
    const double Added = 2.0 * ScalarCriteria::Pi * Radius * Radius *
        (BossRadius * (1.0 - ScalarCriteria::Pi / 4.0) +
         Radius * (5.0 / 6.0 - ScalarCriteria::Pi / 4.0));
    return Original + Added;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 31 · Plane–Cylinder Fillet Verification — exact boss-root rolling ball");
    const Vec3 Base{ 0, 0, 0 }, Axis{ 0, 0, 1 };
    constexpr double OuterRadius = 10.0, ShoulderHeight = 8.0, BossRadius = 5.0, BossHeight = 7.0, Radius = 2.0;
    Deliver<BrepBody> SourceResult = SteppedBoss(Base, Axis, OuterRadius, ShoulderHeight, BossRadius, BossHeight);
    const BrepBody Source = SourceResult.Payload;
    const Vec3 RootCentre = Base + Axis * ShoulderHeight;
    const int RootEdge = CircularEdgeAt(Source, RootCentre, Axis, BossRadius);

    Panel.Section("Bounded smooth-support classifier");
    Panel.Expect("The exact stepped solid is a closed V4/E7/F5 B-rep",
                 SourceResult && Source.Validate().Solid() && Source.Vertices.size() == 4 &&
                 Source.Edges.size() == 7 && Source.Faces.size() == 5);
    Panel.Expect("The circular boss root is found from its geometry", RootEdge >= 0);

    Deliver<BrepBody> Filleted = BlendSolver::FilletEdge(Source, RootEdge, Radius);
    Panel.Section("Exact rational rolling-ball result");
    Panel.Expect("The plane–cylinder root fillet returns a closed manifold solid", Filleted && Filleted.Payload.Validate().Solid());
    FilletInspection Inspection = Filleted
        ? InspectFillet(Filleted.Payload, Base, Axis, OuterRadius, ShoulderHeight, BossRadius, BossHeight, Radius)
        : FilletInspection{};
    Panel.Expect("The result has the exact V5/E9/C18/L6/F6 topology", Inspection.Topology);
    Panel.Expect("The roll retains exact partial-torus analytic identity", Inspection.AnalyticIdentity);
    Panel.Within("Quarter-torus implicit residual", Inspection.TorusResidual, 1e-9);
    Panel.Within("G1 break at the planar shoulder contact", Inspection.ShoulderTangentBreak, 1e-10);
    Panel.Within("G1 break at the cylindrical boss contact", Inspection.CylinderTangentBreak, 1e-10);
    Panel.Expect("Outer wall, annular shoulder, boss wall, and both caps retain exact extents", Inspection.Supports);
    Panel.Expect("A concave boss-root roll adds material", Filleted && Filleted.Payload.Validate().Volume > Source.Validate().Volume);
    const double ExpectedVolume = AnalyticFilletVolume(OuterRadius, ShoulderHeight, BossRadius, BossHeight, Radius);
    Panel.Within("Tessellated volume follows the analytic rolling-ball addition",
                 Filleted ? std::fabs(Filleted.Payload.Validate().Volume - ExpectedVolume) : ScalarCriteria::Infinity,
                 ExpectedVolume * 1e-3);
    Panel.Expect("The sharp root edge is replaced by two distinct tangent contact circles",
                 Filleted && CircularEdgeAt(Filleted.Payload, RootCentre, Axis, BossRadius) < 0 &&
                 CircularEdgeAt(Filleted.Payload, RootCentre, Axis, BossRadius + Radius) >= 0 &&
                 CircularEdgeAt(Filleted.Payload, RootCentre + Axis * Radius, Axis, BossRadius) >= 0);

    Panel.Section("World axis and construction direction are not special cases");
    const Vec3 ObliqueBase{ 3, -4, 2 }, ObliqueAxis{ 2, -1, 4 }, ObliqueUnit = ObliqueAxis.Normalised();
    Deliver<BrepBody> ObliqueSource = SteppedBoss(ObliqueBase, ObliqueAxis, 8.0, 6.0, 3.0, 5.0);
    int ObliqueRoot = ObliqueSource
        ? CircularEdgeAt(ObliqueSource.Payload, ObliqueBase + ObliqueUnit * 6.0, ObliqueUnit, 3.0) : -1;
    Deliver<BrepBody> ObliqueFillet = ObliqueRoot >= 0
        ? BlendSolver::FilletEdge(ObliqueSource.Payload, ObliqueRoot, 1.25)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "oblique root not found");
    Panel.Expect("A non-unit oblique construction axis exposes the same support pair", ObliqueRoot >= 0);
    Panel.Expect("The oblique root produces an exact solid quarter-torus",
                 ObliqueFillet && InspectFillet(ObliqueFillet.Payload, ObliqueBase, ObliqueUnit,
                                                8.0, 6.0, 3.0, 5.0, 1.25).AnalyticIdentity);

    const Vec3 ReversedBase{ 0, 0, 15 }, ReversedAxis{ 0, 0, -1 };
    Deliver<BrepBody> ReversedSource = SteppedBoss(ReversedBase, ReversedAxis, 9.0, 5.0, 4.0, 6.0);
    int ReversedRoot = ReversedSource
        ? CircularEdgeAt(ReversedSource.Payload, ReversedBase + ReversedAxis * 5.0, ReversedAxis, 4.0) : -1;
    Deliver<BrepBody> ReversedFillet = ReversedRoot >= 0
        ? BlendSolver::FilletEdge(ReversedSource.Payload, ReversedRoot, 1.5)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "reversed root not found");
    FilletInspection ReversedInspection = ReversedFillet
        ? InspectFillet(ReversedFillet.Payload, ReversedBase, ReversedAxis, 9.0, 5.0, 4.0, 6.0, 1.5)
        : FilletInspection{};
    Panel.Expect("Reversing the world direction retains exact supports and G1 contacts",
                 ReversedInspection.Topology && ReversedInspection.AnalyticIdentity && ReversedInspection.Supports &&
                 ReversedInspection.ShoulderTangentBreak < 1e-10 && ReversedInspection.CylinderTangentBreak < 1e-10);

    Panel.Section("Feasibility and unsupported selections refuse cleanly");
    Panel.Expect("Zero radius refuses", !BlendSolver::FilletEdge(Source, RootEdge, 0.0));
    Panel.Expect("A radius consuming the boss height refuses", !BlendSolver::FilletEdge(Source, RootEdge, BossHeight));
    Panel.Expect("A radius consuming the complete shoulder width refuses",
                 !BlendSolver::FilletEdge(Source, RootEdge, OuterRadius - BossRadius));
    const int OuterRoot = CircularEdgeAt(Source, RootCentre, Axis, OuterRadius);
    Panel.Expect("The opposite shoulder rim is not misclassified as a boss root",
                 OuterRoot >= 0 && !BlendSolver::FilletEdge(Source, OuterRoot, Radius));
    Deliver<BrepBody> NativeCylinder = BrepBody::Cylinder(Base, Axis, 6.0, 12.0);
    int NativeTop = NativeCylinder ? CircularEdgeAt(NativeCylinder.Payload, { 0, 0, 12 }, Axis, 6.0) : -1;
    Panel.Expect("The established native-cylinder cap fillet remains available",
                 NativeTop >= 0 && BlendSolver::FilletEdge(NativeCylinder.Payload, NativeTop, 1.0));

    Panel.Section("C++ console path and visual proof");
#ifndef SOLIDARC_PROOF_FOLDER
#error SOLIDARC_PROOF_FOLDER must be supplied by the build
#endif
    ConsoleHost CommandHost(SOLIDARC_PROOF_FOLDER, 1280, 800);
    SceneFigure& CommandSource = CommandHost.Document().AddBody("Stepped", Source);
    (void)CommandSource;
    const std::string FilletCommand = "fillet Stepped 2 --edges=" + std::to_string(RootEdge) + " --name=Rounded";
    bool CommandAccepted = CommandHost.Execute(FilletCommand);
    const SceneFigure* CommandResult = CommandHost.Document().Find("Rounded");
    Panel.Expect("The C++ console accepts the plane–cylinder support fillet", CommandAccepted);
    Panel.Expect("The console commits the exact kernel result",
                 CommandResult && CommandResult->Classification == FigureClassification::Body &&
                 InspectFillet(CommandResult->Body, Base, Axis, OuterRadius, ShoulderHeight,
                               BossRadius, BossHeight, Radius).AnalyticIdentity);

    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase31_PlaneCylinderFillet.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1280, 800);
    auto Reset = [&]() { return ProofHost.Execute("reset") && ProofHost.Execute("gizmo off"); };
    auto Add = [&](const char* Name, BrepBody Body, uint8_t Matcap) -> bool
    {
        SceneFigure& Figure = ProofHost.Document().AddBody(Name, std::move(Body));
        Figure.Matcap = Matcap;
        return true;
    };
    Deliver<BrepBody> SmallRoll = BlendSolver::FilletEdge(Source, RootEdge, 0.75);
    bool Rendered =
        Reset() && Add("Sharp", SteppedBoss({ -13, 0, 0 }, Axis, OuterRadius, ShoulderHeight, BossRadius, BossHeight).Payload, 0) &&
        Add("Rolled", BlendSolver::FilletEdge(SteppedBoss({ 13, 0, 0 }, Axis, OuterRadius, ShoulderHeight, BossRadius, BossHeight).Payload, RootEdge, Radius).Payload, 2) &&
        ProofHost.Execute("view iso") && ProofHost.Execute("view fit") && ProofHost.Execute("render sheet 0") &&
        Reset() && Add("R2", Filleted.Payload, 2) && ProofHost.Execute("view front") && ProofHost.Execute("view fit") &&
        ProofHost.Execute("view dolly 0.72") && ProofHost.Execute("render sheet 1") &&
        Reset() && Add("Oblique", ObliqueFillet.Payload, 6) && ProofHost.Execute("view iso") && ProofHost.Execute("view fit") &&
        ProofHost.Execute("render sheet 2") &&
        Reset() && SmallRoll && Add("R0.75", SmallRoll.Payload, 3) && Add("R2", Filleted.Payload.Transformed(Mat4::Translation({ 24, 0, 0 })), 2) &&
        ProofHost.Execute("view iso") && ProofHost.Execute("view fit") && ProofHost.Execute("render sheet 3") &&
        ProofHost.Execute("render sheet finalize Phase31_PlaneCylinderFillet");
    Panel.Expect("C++ proof commands complete without refusal", Rendered);
    Panel.Expect("C++ close-up proof is written and non-trivial",
                 std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);

    return Panel.Conclude();
}
