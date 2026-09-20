//=============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Verification/MultiEdgeFilletVerification.cpp — Phase 32c intentional sets
//=============================================================================================================================================
// Multi-edge filleting is transactional: curved members of one propagated chain deduplicate to one operation, while
// independent vertex-disjoint seeds are resolved by sampled edge identity and applied in deterministic geometric order.
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <string>
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

[[nodiscard]] Deliver<BrepBody> SplitSteppedBoss(Vec3 Base, Vec3 Axis, double OuterRadius, double ShoulderHeight,
                                                  double BossRadius, double BossHeight, int Segments) noexcept
{
    Axis = Axis.Normalised();
    Workplane Frame = Workplane::FromNormal(Base, Axis);
    Vec3 ShoulderCentre = Base + Axis * ShoulderHeight;
    Deliver<NurbsSurface> Outer = NurbsSurface::Cylinder(Base, Axis, OuterRadius, ShoulderHeight);
    Deliver<NurbsCurve> ShoulderLine = NurbsCurve::Line(
        ShoulderCentre + Frame.AxisX * OuterRadius, ShoulderCentre + Frame.AxisX * BossRadius);
    Deliver<NurbsSurface> Shoulder = ShoulderLine
        ? NurbsSurface::Revolution(ShoulderLine.Payload, Base, Axis, ScalarCriteria::TwoPi)
        : Deliver<NurbsSurface>::Reject(ShoulderLine.Denial.Reason, ShoulderLine.Denial.Detail);
    Deliver<NurbsSurface> Boss = NurbsSurface::Cylinder(ShoulderCentre, Axis, BossRadius, BossHeight);
    if (!Outer || !Shoulder || !Boss)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "split boss support is degenerate");
    std::vector<NurbsSurface> Faces;
    for (const NurbsSurface& Surface : { Outer.Payload, Shoulder.Payload, Boss.Payload })
    {
        std::vector<NurbsSurface> Patches = AngularPatches(Surface, Segments);
        Faces.insert(Faces.end(), std::make_move_iterator(Patches.begin()), std::make_move_iterator(Patches.end()));
    }
    return BrepBody::Sew(Faces);
}

[[nodiscard]] bool CircularFrame(const NurbsCurve& Curve, Vec3& Centre, Vec3& Normal, double& Radius) noexcept
{
    if ((Curve.Classification != CurveClassification::Arc && Curve.Classification != CurveClassification::Circle) ||
        Curve.Degree != 2 || !Curve.Rational()) return false;
    double T0 = Curve.DomainStart(), T1 = Curve.DomainEnd();
    double TM = Curve.Closed() ? T0 + (T1 - T0) * 0.25 : 0.5 * (T0 + T1);
    double TE = Curve.Closed() ? T0 + (T1 - T0) * 0.5 : T1;
    Vec3 P0 = Curve.Sample(T0), P1 = Curve.Sample(TM), P2 = Curve.Sample(TE);
    Vec3 U = P1 - P0, V = P2 - P0, Cross = U.Cross(V);
    double Denominator = 2.0 * Cross.LengthSquared();
    if (Denominator <= ScalarCriteria::KernelTolerance) return false;
    Centre = P0 + (Cross.Cross(U) * V.LengthSquared() + V.Cross(Cross) * U.LengthSquared()) / Denominator;
    Radius = Centre.Distance(P0); Normal = Cross.Normalised();
    return Radius > ScalarCriteria::MergeTolerance;
}

[[nodiscard]] std::vector<int> CircleEdges(const BrepBody& Body, Vec3 Centre, Vec3 Axis, double Radius) noexcept
{
    std::vector<int> Edges;
    Axis = Axis.Normalised();
    for (size_t Edge = 0; Edge < Body.Edges.size(); ++Edge)
    {
        Vec3 CandidateCentre, Normal; double CandidateRadius = 0.0;
        if (CircularFrame(Body.Edges[Edge].Curve, CandidateCentre, Normal, CandidateRadius) &&
            CandidateCentre.Distance(Centre) < 1e-8 && std::fabs(CandidateRadius - Radius) < 1e-8 &&
            std::fabs(Normal.Dot(Axis)) > 1.0 - 1e-8) Edges.push_back(static_cast<int>(Edge));
    }
    return Edges;
}

[[nodiscard]] int RationalRollFaces(const BrepBody& Body) noexcept
{
    int Count = 0;
    for (const BrepFace& Face : Body.Faces)
        if (Face.Surface.Classification == SurfaceClassification::Extrusion && Face.Surface.Rational()) ++Count;
    return Count;
}

[[nodiscard]] bool CanonicalBossRoll(const BrepBody& Body, Vec3 Origin, Vec3 Axis,
                                     double MajorRadius, double MinorRadius) noexcept
{
    for (const BrepFace& Face : Body.Faces)
    {
        const NurbsSurface& Surface = Face.Surface;
        if (Surface.Classification != SurfaceClassification::Torus) continue;
        return Body.Validate().Solid() && Body.Vertices.size() == 5 && Body.Edges.size() == 9 && Body.Faces.size() == 6 &&
            Surface.Origin.Distance(Origin) < 1e-9 && Surface.Axis.Normalised().Dot(Axis.Normalised()) > 1.0 - 1e-12 &&
            std::fabs(Surface.RadiusMajor - MajorRadius) < 1e-10 && std::fabs(Surface.RadiusMinor - MinorRadius) < 1e-10;
    }
    return false;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 32c · Multi-Edge Fillet Verification — deterministic and transactional sets");
    Deliver<BrepBody> BoxResult = BrepBody::Box({ 0, 0, 0 }, { 20, 16, 12 });
    BrepBody Box = BoxResult.Payload;
    BodyReport OriginalReport = Box.Validate();
    EdgeCornerFrame FrontFrame, BackFrame; std::string Why;
    bool FrontOkay = BlendSolver::Frame(Box, 0, FrontFrame, Why);
    bool BackOkay = BlendSolver::Frame(Box, 2, BackFrame, Why);

    Panel.Section("Independent seed-set composition");
    Panel.Expect("The source box is a one-hull V8/E12/F6 solid",
                 BoxResult && OriginalReport.Solid() && OriginalReport.Hulls == 1 &&
                 Box.Vertices.size() == 8 && Box.Edges.size() == 12 && Box.Faces.size() == 6);
    Panel.Expect("Both opposite bottom edges expose exact planar corner frames", FrontOkay && BackOkay);
    int AppliedPair = -1;
    Deliver<BrepBody> Pair = BlendSolver::FilletEdges(Box, { 0, 2 }, 1.0, &AppliedPair);
    Panel.Expect("Two vertex-disjoint seeds commit as one valid solid transaction", Pair && Pair.Payload.Validate().Solid());
    Panel.Expect("The transaction reports two applied independent chains", AppliedPair == 2);
    Panel.Expect("The twin roll has exact V12/E18/C36/L8/F8 topology",
                 Pair && Pair.Payload.Vertices.size() == 12 && Pair.Payload.Edges.size() == 18 &&
                 Pair.Payload.Coedges.size() == 36 && Pair.Payload.Loops.size() == 8 && Pair.Payload.Faces.size() == 8);
    Panel.Expect("Exactly two rational rolling-cylinder faces replace the selected corners",
                 Pair && RationalRollFaces(Pair.Payload) == 2);
    const double ExpectedPairVolume = OriginalReport.Volume -
        BlendSolver::FilletRemoval(FrontFrame, 1.0) - BlendSolver::FilletRemoval(BackFrame, 1.0);
    Panel.Expect("Both convex rolls remove material", Pair && Pair.Payload.Validate().Volume < OriginalReport.Volume);
    Panel.Within("Twin-fillet volume follows the summed closed form",
                 Pair ? std::fabs(Pair.Payload.Validate().Volume - ExpectedPairVolume) : ScalarCriteria::Infinity, 0.05);

    int AppliedReverse = -1;
    Deliver<BrepBody> Reverse = BlendSolver::FilletEdges(Box, { 2, 0 }, 1.0, &AppliedReverse);
    Panel.Expect("Reversing input order still applies two chains", Reverse && AppliedReverse == 2);
    Panel.Within("Deterministic geometric ordering makes both input orders volume-identical",
                 Pair && Reverse ? std::fabs(Pair.Payload.Validate().Volume - Reverse.Payload.Validate().Volume)
                                 : ScalarCriteria::Infinity, 1e-9);
    Panel.Expect("The transactional API leaves its source body unchanged",
                 Box.Validate().Solid() && Box.Vertices.size() == 8 && Box.Edges.size() == 12 &&
                 std::fabs(Box.Validate().Volume - OriginalReport.Volume) < 1e-12);

    Panel.Section("Propagated-chain seed deduplication");
    const Vec3 Base{ 0, 0, 0 }, Axis{ 0, 0, 1 };
    constexpr double OuterRadius = 10.0, ShoulderHeight = 8.0, BossRadius = 5.0, BossHeight = 7.0, Radius = 2.0;
    Deliver<BrepBody> Split2 = SplitSteppedBoss(Base, Axis, OuterRadius, ShoulderHeight, BossRadius, BossHeight, 2);
    std::vector<int> Root2 = Split2
        ? CircleEdges(Split2.Payload, Base + Axis * ShoulderHeight, Axis, BossRadius) : std::vector<int>{};
    Panel.Expect("The representation-split boss exposes two selectable members", Root2.size() == 2);
    int AppliedRoot = -1;
    Deliver<BrepBody> RootSet = Root2.size() == 2
        ? BlendSolver::FilletEdges(Split2.Payload, { Root2[0], Root2[1], Root2[0] }, Radius, &AppliedRoot)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "split root not found");
    Panel.Expect("Repeated members of one propagated chain deduplicate to one operation", RootSet && AppliedRoot == 1);
    Panel.Expect("The deduplicated set commits the canonical exact quarter-torus",
                 RootSet && CanonicalBossRoll(RootSet.Payload, Base + Axis * (ShoulderHeight + Radius),
                                              Axis, BossRadius + Radius, Radius));
    int AppliedSingle = -1;
    Deliver<BrepBody> SingleRoot = !Root2.empty()
        ? BlendSolver::FilletEdges(Split2.Payload, { Root2.front() }, Radius, &AppliedSingle)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "split root not found");
    Panel.Within("One seed and all duplicate chain members produce the same volume",
                 RootSet && SingleRoot ? std::fabs(RootSet.Payload.Validate().Volume - SingleRoot.Payload.Validate().Volume)
                                       : ScalarCriteria::Infinity, 1e-9);

    Deliver<BrepBody> Split4 = SplitSteppedBoss(Base, Axis, OuterRadius, ShoulderHeight, BossRadius, BossHeight, 4);
    std::vector<int> Root4 = Split4
        ? CircleEdges(Split4.Payload, Base + Axis * ShoulderHeight, Axis, BossRadius) : std::vector<int>{};
    int AppliedFour = -1;
    Deliver<BrepBody> FourSet = Root4.size() == 4
        ? BlendSolver::FilletEdges(Split4.Payload, { Root4[3], Root4[1], Root4[0], Root4[2] }, Radius, &AppliedFour)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "four-member root not found");
    Panel.Expect("Four explicitly selected members still commit one propagated chain", FourSet && AppliedFour == 1);
    Panel.Expect("Four-member deduplication heals to exact canonical topology",
                 FourSet && CanonicalBossRoll(FourSet.Payload, Base + Axis * (ShoulderHeight + Radius),
                                              Axis, BossRadius + Radius, Radius));

    Panel.Section("All-or-nothing refusal boundary");
    int RefusedCount = 99;
    Panel.Expect("An empty seed set refuses and reports no applied chain",
                 !BlendSolver::FilletEdges(Box, {}, 1.0, &RefusedCount) && RefusedCount == 0);
    RefusedCount = 99;
    Panel.Expect("A non-positive common radius refuses before construction",
                 !BlendSolver::FilletEdges(Box, { 0, 2 }, 0.0, &RefusedCount) && RefusedCount == 0);
    RefusedCount = 99;
    Panel.Expect("One out-of-range member rejects the complete transaction",
                 !BlendSolver::FilletEdges(Box, { 0, 999, 2 }, 1.0, &RefusedCount) && RefusedCount == 0);
    RefusedCount = 99;
    Deliver<BrepBody> CornerSet = BlendSolver::FilletEdges(Box, { 0, 1 }, 1.0, &RefusedCount);
    Panel.Expect("Two chains sharing a corner vertex refuse before either roll",
                 !CornerSet && RefusedCount == 0 &&
                 std::string(CornerSet.Denial.Detail) ==
                     "multi-edge fillet chains share a vertex (corner resolution is not supported)");
    RefusedCount = 99;
    Panel.Expect("A radius that cannot fit every independent edge rejects the set",
                 !BlendSolver::FilletEdges(Box, { 0, 2 }, 20.0, &RefusedCount) && RefusedCount == 0);
    std::vector<int> Outer2 = Split2
        ? CircleEdges(Split2.Payload, Base + Axis * ShoulderHeight, Axis, OuterRadius) : std::vector<int>{};
    RefusedCount = 99;
    Panel.Expect("Mixing one valid root with one unsupported outer chain commits neither",
                 !Root2.empty() && !Outer2.empty() &&
                 !BlendSolver::FilletEdges(Split2.Payload, { Root2.front(), Outer2.front() }, Radius, &RefusedCount) &&
                 RefusedCount == 0 && Split2.Payload.Validate().Solid());

    Panel.Section("Console transaction and visual proof");
#ifndef SOLIDARC_PROOF_FOLDER
#error SOLIDARC_PROOF_FOLDER must be supplied by the build
#endif
    ConsoleHost CommandHost(SOLIDARC_PROOF_FOLDER, 1280, 800);
    bool CommandAccepted = CommandHost.Execute("box (0,0,0) (20,16,12) --name=MultiBox") &&
        CommandHost.Execute("fillet MultiBox 1 --edges=0,2 --name=TwinRoll");
    const SceneFigure* CommandResult = CommandHost.Document().Find("TwinRoll");
    Panel.Expect("The C++ console commits a two-seed transaction", CommandAccepted);
    Panel.Expect("The console result contains both exact rolling faces",
                 CommandResult && CommandResult->Classification == FigureClassification::Body &&
                 CommandResult->Body.Validate().Solid() && RationalRollFaces(CommandResult->Body) == 2);

    ConsoleHost RefusalHost(SOLIDARC_PROOF_FOLDER, 1280, 800);
    bool BuiltRefusalSource = RefusalHost.Execute("box (0,0,0) (20,16,12) --name=CornerBox");
    bool CornerAccepted = RefusalHost.Execute("fillet CornerBox 1 --edges=0,1 --name=MustNotExist");
    const SceneFigure* Preserved = RefusalHost.Document().Find("CornerBox");
    Panel.Expect("A refused console corner set preserves the original scene transactionally",
                 BuiltRefusalSource && !CornerAccepted && !RefusalHost.Document().Find("MustNotExist") &&
                 Preserved && Preserved->Body.Edges.size() == 12);

    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase32c_MultiEdgeFillet.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1280, 800);
    auto Reset = [&]() { return ProofHost.Execute("reset") && ProofHost.Execute("gizmo off"); };
    auto Add = [&](const char* Name, BrepBody Body, uint8_t Matcap) -> bool
    {
        SceneFigure& Figure = ProofHost.Document().AddBody(Name, std::move(Body)); Figure.Matcap = Matcap; return true;
    };
    bool Rendered =
        Reset() && Add("SharpBox", Box.Transformed(Mat4::Translation({ -24, 0, 0 })), 0) &&
        Add("TwinRoll", Pair.Payload.Transformed(Mat4::Translation({ 8, 0, 0 })), 2) &&
        ProofHost.Execute("view iso") && ProofHost.Execute("view fit") && ProofHost.Execute("render sheet 0") &&
        Reset() && Add("TwinRollClose", Pair.Payload, 2) && ProofHost.Execute("view iso") &&
        ProofHost.Execute("view fit") && ProofHost.Execute("view dolly 0.75") && ProofHost.Execute("render sheet 1") &&
        Reset() && Add("AllRootSeeds", Split4.Payload, 6) && ProofHost.Execute("view iso") &&
        ProofHost.Execute("view fit") && ProofHost.Execute("render sheet 2") &&
        Reset() && Add("OneCommittedChain", FourSet.Payload, 3) && ProofHost.Execute("view iso") &&
        ProofHost.Execute("view fit") && ProofHost.Execute("render sheet 3") &&
        ProofHost.Execute("render sheet finalize Phase32c_MultiEdgeFillet");
    Panel.Expect("C++ multi-edge proof commands complete without refusal", Rendered);
    Panel.Expect("C++ multi-edge proof is written and non-trivial",
                 std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);

    return Panel.Conclude();
}
