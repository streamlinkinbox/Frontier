//=============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Verification/TangentChainFilletVerification.cpp — Phase 32a closed blend chains
//=============================================================================================================================================
// A selected arc on a representation-split boss root propagates around the complete G1 circular chain. The exact
// plane–cylinder rolling-ball reconstruction heals those artificial angular seams instead of blending one patch only.
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
        Split.reserve(Patches.size() * 2);
        for (const NurbsSurface& Patch : Patches)
        {
            auto Halves = Patch.SplitU(0.5 * (Patch.DomainStartU() + Patch.DomainEndU()));
            Split.push_back(std::move(Halves.first));
            Split.push_back(std::move(Halves.second));
        }
        Patches = std::move(Split);
    }
    return Patches;
}

[[nodiscard]] Deliver<BrepBody> SteppedBoss(Vec3 Base, Vec3 Axis, double OuterRadius, double ShoulderHeight,
                                            double BossRadius, double BossHeight, int AngularSegments) noexcept
{
    if (AngularSegments <= 0 || (AngularSegments & (AngularSegments - 1)) != 0)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "angular segment count must be a positive power of two");
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

    std::vector<NurbsSurface> Faces;
    for (const NurbsSurface& Surface : { Outer.Payload, Shoulder.Payload, Boss.Payload })
    {
        std::vector<NurbsSurface> Patches = AngularPatches(Surface, AngularSegments);
        Faces.insert(Faces.end(), std::make_move_iterator(Patches.begin()), std::make_move_iterator(Patches.end()));
    }
    return BrepBody::Sew(Faces);
}

[[nodiscard]] bool CircularFrame(const NurbsCurve& Curve, Vec3& Centre, Vec3& Normal, double& Radius) noexcept
{
    if ((Curve.Classification != CurveClassification::Arc && Curve.Classification != CurveClassification::Circle) ||
        Curve.Degree != 2 || !Curve.Rational()) return false;
    double T0 = Curve.DomainStart(), T1 = Curve.DomainEnd();
    double Tm = Curve.Closed() ? T0 + (T1 - T0) * 0.25 : 0.5 * (T0 + T1);
    double Te = Curve.Closed() ? T0 + (T1 - T0) * 0.5 : T1;
    Vec3 P0 = Curve.Sample(T0), P1 = Curve.Sample(Tm), P2 = Curve.Sample(Te);
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

[[nodiscard]] int TorusFace(const BrepBody& Body) noexcept
{
    for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
        if (Body.Faces[Face].Surface.Classification == SurfaceClassification::Torus) return static_cast<int>(Face);
    return -1;
}

struct RollInspection
{
    bool Topology = false;
    bool Analytic = false;
    bool Supports = false;
    double Residual = ScalarCriteria::Infinity;
    double PlaneG1 = ScalarCriteria::Infinity;
    double CylinderG1 = ScalarCriteria::Infinity;
};

[[nodiscard]] RollInspection Inspect(const BrepBody& Body, Vec3 Base, Vec3 Axis,
                                     double OuterRadius, double ShoulderHeight,
                                     double BossRadius, double BossHeight, double Radius) noexcept
{
    RollInspection Result;
    Axis = Axis.Normalised();
    BodyReport Report = Body.Validate();
    Result.Topology = Report.Solid() && Body.Vertices.size() == 5 && Body.Edges.size() == 9 &&
        Body.Coedges.size() == 18 && Body.Loops.size() == 6 && Body.Faces.size() == 6;
    int RollFace = TorusFace(Body);
    if (RollFace < 0) return Result;
    const NurbsSurface& Roll = Body.Faces[RollFace].Surface;
    Vec3 Origin = Base + Axis * (ShoulderHeight + Radius);
    Result.Analytic = Roll.Origin.Distance(Origin) < 1e-9 && Roll.Axis.Normalised().Dot(Axis) > 1.0 - 1e-12 &&
        std::fabs(Roll.RadiusMajor - (BossRadius + Radius)) < 1e-10 &&
        std::fabs(Roll.RadiusMinor - Radius) < 1e-10;
    Result.Residual = 0.0; Result.PlaneG1 = 0.0; Result.CylinderG1 = 0.0;
    double U0 = Roll.DomainStartU(), U1 = Roll.DomainEndU();
    double V0 = Roll.DomainStartV(), V1 = Roll.DomainEndV();
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
        Vec3 PlaneNormal = Body.FaceNormal(RollFace, U, V0).Normalised();
        Result.PlaneG1 = std::max(Result.PlaneG1, 1.0 - PlaneNormal.Dot(Axis));
        Vec3 BossPoint = Roll.Sample(U, V1);
        double Along = (BossPoint - Origin).Dot(Axis);
        Vec3 Radial = (BossPoint - (Origin + Axis * Along)).Normalised();
        Vec3 BossNormal = Body.FaceNormal(RollFace, U, V1).Normalised();
        Result.CylinderG1 = std::max(Result.CylinderG1, 1.0 - BossNormal.Dot(Radial));
    }

    int Cylinders = 0, Planes = 0, Shoulder = 0;
    bool Outer = false, Boss = false;
    for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
    {
        const NurbsSurface& Surface = Body.Faces[Face].Surface;
        if (Surface.Classification == SurfaceClassification::Cylinder)
        {
            ++Cylinders;
            if (std::fabs(Surface.RadiusMajor - OuterRadius) < 1e-9) Outer = true;
            if (std::fabs(Surface.RadiusMajor - BossRadius) < 1e-9) Boss = true;
        }
        else if (Surface.Classification == SurfaceClassification::Plane) ++Planes;
        else if (static_cast<int>(Face) != RollFace) ++Shoulder;
    }
    Result.Supports = Cylinders == 2 && Planes == 2 && Shoulder == 1 && Outer && Boss &&
        CircleEdges(Body, Base + Axis * ShoulderHeight, Axis, BossRadius + Radius).size() == 1 &&
        CircleEdges(Body, Base + Axis * (ShoulderHeight + Radius), Axis, BossRadius).size() == 1;
    (void)BossHeight;
    return Result;
}

[[nodiscard]] double ExactVolume(double OuterRadius, double ShoulderHeight,
                                 double BossRadius, double BossHeight, double Radius) noexcept
{
    double Original = ScalarCriteria::Pi *
        (OuterRadius * OuterRadius * ShoulderHeight + BossRadius * BossRadius * BossHeight);
    double Added = 2.0 * ScalarCriteria::Pi * Radius * Radius *
        (BossRadius * (1.0 - ScalarCriteria::Pi / 4.0) +
         Radius * (5.0 / 6.0 - ScalarCriteria::Pi / 4.0));
    return Original + Added;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 32a · Tangent-Chain Fillet Verification — representation seams heal exactly");
    const Vec3 Base{ 0, 0, 0 }, Axis{ 0, 0, 1 };
    constexpr double OuterRadius = 10.0, ShoulderHeight = 8.0, BossRadius = 5.0, BossHeight = 7.0, Radius = 2.0;
    const Vec3 RootCentre = Base + Axis * ShoulderHeight;

    Deliver<BrepBody> Split2 = SteppedBoss(Base, Axis, OuterRadius, ShoulderHeight, BossRadius, BossHeight, 2);
    std::vector<int> Root2 = Split2 ? CircleEdges(Split2.Payload, RootCentre, Axis, BossRadius) : std::vector<int>{};
    Deliver<std::vector<int>> Chain2 = !Root2.empty()
        ? BlendSolver::TangentChain(Split2.Payload, Root2.front())
        : Deliver<std::vector<int>>::Reject(RefusalReason::Unsupported, "root arc not found");

    Panel.Section("G1 chain discovery");
    Panel.Expect("The two-patch source is a closed V8/E14/C28/L8/F8 solid",
                 Split2 && Split2.Payload.Validate().Solid() && Split2.Payload.Vertices.size() == 8 &&
                 Split2.Payload.Edges.size() == 14 && Split2.Payload.Coedges.size() == 28 &&
                 Split2.Payload.Loops.size() == 8 && Split2.Payload.Faces.size() == 8);
    Panel.Expect("The circular boss root is represented by two open rational arcs",
                 Root2.size() == 2 && !Split2.Payload.Edges[Root2[0]].Closed() && !Split2.Payload.Edges[Root2[1]].Closed());
    Panel.Expect("One selected arc propagates to the complete two-edge tangent chain",
                 Chain2 && Chain2.Payload.size() == 2 &&
                 std::all_of(Root2.begin(), Root2.end(), [&](int Edge)
                 { return std::find(Chain2.Payload.begin(), Chain2.Payload.end(), Edge) != Chain2.Payload.end(); }));
    Deliver<std::vector<int>> ChainFromOther = Root2.size() == 2
        ? BlendSolver::TangentChain(Split2.Payload, Root2.back())
        : Deliver<std::vector<int>>::Reject(RefusalReason::Unsupported, "second root arc not found");
    Panel.Expect("Propagation is independent of which chain member is selected",
                 ChainFromOther && ChainFromOther.Payload.size() == Chain2.Payload.size());

    Deliver<BrepBody> Unsplit = SteppedBoss(Base, Axis, OuterRadius, ShoulderHeight, BossRadius, BossHeight, 1);
    std::vector<int> UnsplitRoot = Unsplit ? CircleEdges(Unsplit.Payload, RootCentre, Axis, BossRadius) : std::vector<int>{};
    Deliver<std::vector<int>> ClosedChain = !UnsplitRoot.empty()
        ? BlendSolver::TangentChain(Unsplit.Payload, UnsplitRoot.front())
        : Deliver<std::vector<int>>::Reject(RefusalReason::Unsupported, "closed root not found");
    Panel.Expect("An unsplit closed circular edge remains a singleton chain",
                 ClosedChain && ClosedChain.Payload.size() == 1 && Unsplit.Payload.Edges[ClosedChain.Payload.front()].Closed());

    Panel.Section("Exact all-chain reconstruction");
    Deliver<BrepBody> Filleted2 = !Root2.empty()
        ? BlendSolver::FilletEdge(Split2.Payload, Root2.front(), Radius)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "root arc not found");
    Panel.Expect("Selecting one arc fillets the complete circular support chain", Filleted2 && Filleted2.Payload.Validate().Solid());
    RollInspection Inspection2 = Filleted2
        ? Inspect(Filleted2.Payload, Base, Axis, OuterRadius, ShoulderHeight, BossRadius, BossHeight, Radius)
        : RollInspection{};
    Panel.Expect("The healed result has canonical V5/E9/C18/L6/F6 topology", Inspection2.Topology);
    Panel.Expect("The chain roll retains exact partial-torus analytic identity", Inspection2.Analytic);
    Panel.Within("Chain torus implicit residual", Inspection2.Residual, 1e-9);
    Panel.Within("G1 break at the planar chain contact", Inspection2.PlaneG1, 1e-10);
    Panel.Within("G1 break at the cylindrical chain contact", Inspection2.CylinderG1, 1e-10);
    Panel.Expect("Artificial angular seams heal into exact retained supports and contact circles", Inspection2.Supports);
    const double ExpectedVolume = ExactVolume(OuterRadius, ShoulderHeight, BossRadius, BossHeight, Radius);
    Panel.Within("Healed-chain volume follows the exact rolling-ball value",
                 Filleted2 ? std::fabs(Filleted2.Payload.Validate().Volume - ExpectedVolume) : ScalarCriteria::Infinity,
                 ExpectedVolume * 1e-3);
    Deliver<BrepBody> OtherResult = Root2.size() == 2
        ? BlendSolver::FilletEdge(Split2.Payload, Root2.back(), Radius)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "second root arc not found");
    Panel.Within("Either member produces the same solid volume",
                 Filleted2 && OtherResult
                     ? std::fabs(Filleted2.Payload.Validate().Volume - OtherResult.Payload.Validate().Volume)
                     : ScalarCriteria::Infinity, 1e-9);

    Panel.Section("Longer chains and transformed axes");
    Deliver<BrepBody> Split4 = SteppedBoss(Base, Axis, OuterRadius, ShoulderHeight, BossRadius, BossHeight, 4);
    std::vector<int> Root4 = Split4 ? CircleEdges(Split4.Payload, RootCentre, Axis, BossRadius) : std::vector<int>{};
    Deliver<std::vector<int>> Chain4 = !Root4.empty()
        ? BlendSolver::TangentChain(Split4.Payload, Root4[1])
        : Deliver<std::vector<int>>::Reject(RefusalReason::Unsupported, "four-patch root not found");
    Panel.Expect("The four-patch source is a closed V16/E28/F14 solid",
                 Split4 && Split4.Payload.Validate().Solid() && Split4.Payload.Vertices.size() == 16 &&
                 Split4.Payload.Edges.size() == 28 && Split4.Payload.Faces.size() == 14);
    Panel.Expect("A quarter-arc seed propagates around all four chain members", Chain4 && Chain4.Payload.size() == 4);
    Deliver<BrepBody> Filleted4 = !Root4.empty()
        ? BlendSolver::FilletEdge(Split4.Payload, Root4[1], Radius)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "four-patch root not found");
    Panel.Expect("The four-member chain heals to the same exact canonical result",
                 Filleted4 && Inspect(Filleted4.Payload, Base, Axis, OuterRadius, ShoulderHeight,
                                      BossRadius, BossHeight, Radius).Analytic);

    const Vec3 ObliqueBase{ 3, -4, 2 }, ObliqueAxis{ 2, -1, 4 }, ObliqueUnit = ObliqueAxis.Normalised();
    Deliver<BrepBody> Oblique = SteppedBoss(ObliqueBase, ObliqueAxis, 8.0, 6.0, 3.0, 5.0, 4);
    std::vector<int> ObliqueRoot = Oblique
        ? CircleEdges(Oblique.Payload, ObliqueBase + ObliqueUnit * 6.0, ObliqueUnit, 3.0) : std::vector<int>{};
    Deliver<std::vector<int>> ObliqueChain = !ObliqueRoot.empty()
        ? BlendSolver::TangentChain(Oblique.Payload, ObliqueRoot.front())
        : Deliver<std::vector<int>>::Reject(RefusalReason::Unsupported, "oblique root not found");
    Deliver<BrepBody> ObliqueFillet = !ObliqueRoot.empty()
        ? BlendSolver::FilletEdge(Oblique.Payload, ObliqueRoot.front(), 1.25)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "oblique root not found");
    Panel.Expect("A non-unit oblique axis preserves the four-member tangent chain", ObliqueChain && ObliqueChain.Payload.size() == 4);
    Panel.Expect("The oblique chain produces the exact transformed torus",
                 ObliqueFillet && Inspect(ObliqueFillet.Payload, ObliqueBase, ObliqueUnit,
                                          8.0, 6.0, 3.0, 5.0, 1.25).Analytic);

    Panel.Section("Boundaries refuse instead of partially rolling");
    std::vector<int> Outer2 = Split2 ? CircleEdges(Split2.Payload, RootCentre, Axis, OuterRadius) : std::vector<int>{};
    Deliver<std::vector<int>> OuterChain = !Outer2.empty()
        ? BlendSolver::TangentChain(Split2.Payload, Outer2.front())
        : Deliver<std::vector<int>>::Reject(RefusalReason::Unsupported, "outer chain not found");
    Panel.Expect("The opposite shoulder rim is a tangent chain but not a supported boss root",
                 OuterChain && OuterChain.Payload.size() == 2 &&
                 !BlendSolver::FilletEdge(Split2.Payload, Outer2.front(), Radius));
    Panel.Expect("A chain radius consuming the boss height refuses",
                 !BlendSolver::FilletEdge(Split2.Payload, Root2.front(), BossHeight));
    Panel.Expect("A chain radius consuming the shoulder refuses",
                 !BlendSolver::FilletEdge(Split2.Payload, Root2.front(), OuterRadius - BossRadius));
    BrepBody Unclassified = Split2.Payload;
    Unclassified.Edges[Root2.front()].Curve.Classification = CurveClassification::Freeform;
    Panel.Expect("A non-analytic member prevents partial chain reconstruction",
                 !BlendSolver::FilletEdge(Unclassified, Root2.front(), Radius));
    Panel.Expect("An out-of-range chain seed refuses explicitly", !BlendSolver::TangentChain(Split2.Payload, 999));

    Panel.Section("Console commit and deterministic C++ proof");
#ifndef SOLIDARC_PROOF_FOLDER
#error SOLIDARC_PROOF_FOLDER must be supplied by the build
#endif
    ConsoleHost CommandHost(SOLIDARC_PROOF_FOLDER, 1280, 800);
    SceneFigure& CommandSource = CommandHost.Document().AddBody("SplitBoss", Split2.Payload);
    (void)CommandSource;
    const std::string Command = "fillet SplitBoss 2 --edges=" + std::to_string(Root2.front()) + " --name=ChainRoll";
    bool CommandAccepted = CommandHost.Execute(Command);
    const SceneFigure* CommandResult = CommandHost.Document().Find("ChainRoll");
    Panel.Expect("The C++ console accepts one member of a split support chain", CommandAccepted);
    Panel.Expect("The console commits the exact healed-chain kernel result",
                 CommandResult && CommandResult->Classification == FigureClassification::Body &&
                 Inspect(CommandResult->Body, Base, Axis, OuterRadius, ShoulderHeight,
                         BossRadius, BossHeight, Radius).Analytic);

    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase32a_TangentChainFillet.png";
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
    bool Rendered =
        Reset() && Add("TwoArcSource", SteppedBoss({ -13, 0, 0 }, Axis, OuterRadius, ShoulderHeight,
                                                    BossRadius, BossHeight, 2).Payload, 0) &&
        Add("HealedRoll", Filleted2.Payload.Transformed(Mat4::Translation({ 13, 0, 0 })), 2) &&
        ProofHost.Execute("view iso") && ProofHost.Execute("view fit") && ProofHost.Execute("render sheet 0") &&
        Reset() && Add("TwoArcRoll", Filleted2.Payload, 2) && ProofHost.Execute("view front") &&
        ProofHost.Execute("view fit") && ProofHost.Execute("view dolly 0.72") && ProofHost.Execute("render sheet 1") &&
        Reset() && Add("FourArcSource", Split4.Payload, 6) && ProofHost.Execute("view iso") &&
        ProofHost.Execute("view fit") && ProofHost.Execute("render sheet 2") &&
        Reset() && Add("ObliqueChainRoll", ObliqueFillet.Payload, 3) && ProofHost.Execute("view iso") &&
        ProofHost.Execute("view fit") && ProofHost.Execute("render sheet 3") &&
        ProofHost.Execute("render sheet finalize Phase32a_TangentChainFillet");
    Panel.Expect("C++ chain proof commands complete without refusal", Rendered);
    Panel.Expect("C++ chain proof is written and non-trivial",
                 std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);

    return Panel.Conclude();
}
