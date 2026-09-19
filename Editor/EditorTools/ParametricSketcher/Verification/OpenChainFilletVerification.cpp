//=============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Verification/OpenChainFilletVerification.cpp — Phase 32b finite chain ends
//=============================================================================================================================================
// The first bounded open chain is a semicircular plane–cylinder boss root. Its two ends terminate on one planar
// diameter face; internal angular representation seams heal while the two physical endpoint meridians remain exact.
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

[[nodiscard]] Deliver<BrepBody> SemicircularBoss(Vec3 Base, Vec3 Axis, double OuterRadius, double ShoulderHeight,
                                                  double BossRadius, double BossHeight, int AngularSegments,
                                                  double SweepSign = 1.0) noexcept
{
    if (AngularSegments <= 0 || (AngularSegments & (AngularSegments - 1)) != 0 || SweepSign == 0.0)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "invalid semicircular support segmentation");
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
            ? NurbsSurface::Revolution(Line.Payload, Base, Axis, SweepSign * ScalarCriteria::Pi)
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

[[nodiscard]] int ChainEndpointCount(const BrepBody& Body, const std::vector<int>& Chain) noexcept
{
    std::vector<int> Degrees(Body.Vertices.size(), 0);
    for (int Edge : Chain)
    {
        if (Edge < 0 || Edge >= static_cast<int>(Body.Edges.size())) return -1;
        const BrepEdge& Candidate = Body.Edges[Edge];
        if (Candidate.VertexStart < 0 || Candidate.VertexEnd < 0) return -1;
        ++Degrees[Candidate.VertexStart]; ++Degrees[Candidate.VertexEnd];
    }
    return static_cast<int>(std::count(Degrees.begin(), Degrees.end(), 1));
}

[[nodiscard]] int TorusFace(const BrepBody& Body) noexcept
{
    for (size_t Face = 0; Face < Body.Faces.size(); ++Face)
        if (Body.Faces[Face].Surface.Classification == SurfaceClassification::Torus) return static_cast<int>(Face);
    return -1;
}

struct OpenRollInspection
{
    bool Topology = false;
    bool Analytic = false;
    bool FiniteEnds = false;
    bool Supports = false;
    double Residual = ScalarCriteria::Infinity;
    double PlaneG1 = ScalarCriteria::Infinity;
    double CylinderG1 = ScalarCriteria::Infinity;
};

[[nodiscard]] OpenRollInspection Inspect(const BrepBody& Body, Vec3 Base, Vec3 Axis,
                                         double OuterRadius, double ShoulderHeight,
                                         double BossRadius, double BossHeight, double Radius) noexcept
{
    OpenRollInspection Result;
    Axis = Axis.Normalised();
    BodyReport Report = Body.Validate();
    Result.Topology = Report.Solid() && Report.Hulls == 1 && Report.Genus == 0 &&
        Body.Vertices.size() == 12 && Body.Edges.size() == 17 &&
        Body.Coedges.size() == 34 && Body.Loops.size() == 7 && Body.Faces.size() == 7;
    int RollFace = TorusFace(Body);
    if (RollFace < 0) return Result;
    const NurbsSurface& Roll = Body.Faces[RollFace].Surface;
    Vec3 Origin = Base + Axis * (ShoulderHeight + Radius);
    Result.Analytic = !Roll.ClosedU() && Roll.Origin.Distance(Origin) < 1e-9 &&
        Roll.Axis.Normalised().Dot(Axis) > 1.0 - 1e-12 &&
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

    int Cylinders = 0, DiameterCaps = 0, TorusBoundaryEdges = 0;
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
        if (Surface.Classification == SurfaceClassification::Plane)
        {
            Vec3 Normal = Body.FaceNormal(static_cast<int>(Face),
                0.5 * (Surface.DomainStartU() + Surface.DomainEndU()),
                0.5 * (Surface.DomainStartV() + Surface.DomainEndV())).Normalised();
            if (std::fabs(Normal.Dot(Axis)) < 1e-8) ++DiameterCaps;
        }
    }
    for (int Loop : Body.Faces[RollFace].Loops) TorusBoundaryEdges += static_cast<int>(Body.Loops[Loop].Coedges.size());
    std::vector<int> LowerContact = CircleEdges(Body, Base + Axis * ShoulderHeight, Axis, BossRadius + Radius);
    std::vector<int> UpperContact = CircleEdges(Body, Base + Axis * (ShoulderHeight + Radius), Axis, BossRadius);
    Result.FiniteEnds = TorusBoundaryEdges == 4 && LowerContact.size() == 1 && UpperContact.size() == 1 &&
        !Body.Edges[LowerContact.front()].Closed() && !Body.Edges[UpperContact.front()].Closed();
    Result.Supports = Cylinders == 2 && DiameterCaps == 1 && Outer && Boss;
    (void)BossHeight;
    return Result;
}

[[nodiscard]] double ExactSemicircleVolume(double OuterRadius, double ShoulderHeight,
                                           double BossRadius, double BossHeight, double Radius) noexcept
{
    double FullOriginal = ScalarCriteria::Pi *
        (OuterRadius * OuterRadius * ShoulderHeight + BossRadius * BossRadius * BossHeight);
    double FullAdded = 2.0 * ScalarCriteria::Pi * Radius * Radius *
        (BossRadius * (1.0 - ScalarCriteria::Pi / 4.0) +
         Radius * (5.0 / 6.0 - ScalarCriteria::Pi / 4.0));
    return 0.5 * (FullOriginal + FullAdded);
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 32b · Open-Chain Fillet Verification — exact finite semicircle endpoints");
    const Vec3 Base{ 0, 0, 0 }, Axis{ 0, 0, 1 };
    constexpr double OuterRadius = 10.0, ShoulderHeight = 8.0, BossRadius = 5.0, BossHeight = 7.0, Radius = 2.0;
    const Vec3 RootCentre = Base + Axis * ShoulderHeight;

    Deliver<BrepBody> Split2 = SemicircularBoss(Base, Axis, OuterRadius, ShoulderHeight, BossRadius, BossHeight, 2);
    std::vector<int> Root2 = Split2 ? CircleEdges(Split2.Payload, RootCentre, Axis, BossRadius) : std::vector<int>{};
    Deliver<std::vector<int>> Chain2 = !Root2.empty()
        ? BlendSolver::TangentChain(Split2.Payload, Root2.front())
        : Deliver<std::vector<int>>::Reject(RefusalReason::Unsupported, "open root not found");

    Panel.Section("Finite G1 chain and endpoint classification");
    BodyReport Split2Report = Split2 ? Split2.Payload.Validate() : BodyReport{};
    Panel.Expect("The two-patch semicircle is a one-hull V14/E23/C46/L11/F11 solid",
                 Split2 && Split2Report.Solid() && Split2Report.Hulls == 1 && Split2Report.Genus == 0 &&
                 Split2.Payload.Vertices.size() == 14 && Split2.Payload.Edges.size() == 23 &&
                 Split2.Payload.Coedges.size() == 46 && Split2.Payload.Loops.size() == 11 && Split2.Payload.Faces.size() == 11);
    Panel.Expect("The finite boss root consists of two open rational quarter arcs",
                 Root2.size() == 2 && !Split2.Payload.Edges[Root2[0]].Closed() && !Split2.Payload.Edges[Root2[1]].Closed());
    Panel.Expect("One quarter-arc seed propagates through both open chain members", Chain2 && Chain2.Payload.size() == 2);
    Panel.Expect("The propagated semicircle has exactly two physical endpoints",
                 Chain2 && ChainEndpointCount(Split2.Payload, Chain2.Payload) == 2);
    Deliver<std::vector<int>> OtherChain = Root2.size() == 2
        ? BlendSolver::TangentChain(Split2.Payload, Root2.back())
        : Deliver<std::vector<int>>::Reject(RefusalReason::Unsupported, "second member not found");
    Panel.Expect("Either internal member discovers the same finite chain", OtherChain && OtherChain.Payload.size() == 2);

    Panel.Section("Exact open-chain roll and end treatment");
    Deliver<BrepBody> Filleted2 = !Root2.empty()
        ? BlendSolver::FilletEdge(Split2.Payload, Root2.front(), Radius)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "open root not found");
    Panel.Expect("Selecting one member rolls the complete finite chain", Filleted2 && Filleted2.Payload.Validate().Solid());
    OpenRollInspection Inspection2 = Filleted2
        ? Inspect(Filleted2.Payload, Base, Axis, OuterRadius, ShoulderHeight, BossRadius, BossHeight, Radius)
        : OpenRollInspection{};
    Panel.Expect("Internal seams heal to one-hull V12/E17/C34/L7/F7 endpoint topology", Inspection2.Topology);
    Panel.Expect("The finite rolling surface retains exact partial-torus identity", Inspection2.Analytic);
    Panel.Within("Open-chain torus implicit residual", Inspection2.Residual, 1e-9);
    Panel.Within("G1 break at the finite planar contact", Inspection2.PlaneG1, 1e-10);
    Panel.Within("G1 break at the finite cylindrical contact", Inspection2.CylinderG1, 1e-10);
    Panel.Expect("Two torus meridians remain at the physical endpoints", Inspection2.FiniteEnds);
    Panel.Expect("Exact outer/boss supports and one planar diameter cap remain", Inspection2.Supports);
    Panel.Expect("The concave finite roll adds material", Filleted2 && Filleted2.Payload.Validate().Volume > Split2.Payload.Validate().Volume);
    const double ExpectedVolume = ExactSemicircleVolume(OuterRadius, ShoulderHeight, BossRadius, BossHeight, Radius);
    Panel.Within("Semicircle volume follows one half of the exact full rolling-ball value",
                 Filleted2 ? std::fabs(Filleted2.Payload.Validate().Volume - ExpectedVolume) : ScalarCriteria::Infinity,
                 ExpectedVolume * 1e-3);
    Deliver<BrepBody> OtherResult = Root2.size() == 2
        ? BlendSolver::FilletEdge(Split2.Payload, Root2.back(), Radius)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "second member not found");
    Panel.Within("Either chain member produces the same finite solid",
                 Filleted2 && OtherResult
                     ? std::fabs(Filleted2.Payload.Validate().Volume - OtherResult.Payload.Validate().Volume)
                     : ScalarCriteria::Infinity, 1e-9);

    Panel.Section("Longer chains, transformed axes, and sweep direction");
    Deliver<BrepBody> Split4 = SemicircularBoss(Base, Axis, OuterRadius, ShoulderHeight, BossRadius, BossHeight, 4);
    std::vector<int> Root4 = Split4 ? CircleEdges(Split4.Payload, RootCentre, Axis, BossRadius) : std::vector<int>{};
    Deliver<std::vector<int>> Chain4 = !Root4.empty()
        ? BlendSolver::TangentChain(Split4.Payload, Root4[1])
        : Deliver<std::vector<int>>::Reject(RefusalReason::Unsupported, "four-member open root not found");
    Panel.Expect("The four-patch semicircle is a one-hull V22/E41/C82/L21/F21 solid",
                 Split4 && Split4.Payload.Validate().Solid() && Split4.Payload.Validate().Hulls == 1 &&
                 Split4.Payload.Vertices.size() == 22 && Split4.Payload.Edges.size() == 41 &&
                 Split4.Payload.Coedges.size() == 82 && Split4.Payload.Loops.size() == 21 && Split4.Payload.Faces.size() == 21);
    Panel.Expect("An internal eighth-turn seed propagates to all four members and two ends",
                 Chain4 && Chain4.Payload.size() == 4 && ChainEndpointCount(Split4.Payload, Chain4.Payload) == 2);
    Deliver<BrepBody> Filleted4 = !Root4.empty()
        ? BlendSolver::FilletEdge(Split4.Payload, Root4[1], Radius)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "four-member open root not found");
    Panel.Expect("The four-member finite chain heals to the same canonical endpoint solid",
                 Filleted4 && Inspect(Filleted4.Payload, Base, Axis, OuterRadius, ShoulderHeight,
                                      BossRadius, BossHeight, Radius).Topology);

    const Vec3 ObliqueBase{ 3, -4, 2 }, ObliqueAxis{ 2, -1, 4 }, ObliqueUnit = ObliqueAxis.Normalised();
    Deliver<BrepBody> Oblique = SemicircularBoss(ObliqueBase, ObliqueAxis, 8.0, 6.0, 3.0, 5.0, 2);
    std::vector<int> ObliqueRoot = Oblique
        ? CircleEdges(Oblique.Payload, ObliqueBase + ObliqueUnit * 6.0, ObliqueUnit, 3.0) : std::vector<int>{};
    Deliver<BrepBody> ObliqueFillet = !ObliqueRoot.empty()
        ? BlendSolver::FilletEdge(Oblique.Payload, ObliqueRoot.front(), 1.25)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "oblique open root not found");
    Panel.Expect("A non-unit oblique axis retains exact finite endpoint topology",
                 ObliqueFillet && Inspect(ObliqueFillet.Payload, ObliqueBase, ObliqueUnit,
                                          8.0, 6.0, 3.0, 5.0, 1.25).Topology);

    Deliver<BrepBody> Negative = SemicircularBoss(Base, Axis, 9.0, 6.0, 4.0, 5.0, 2, -1.0);
    std::vector<int> NegativeRoot = Negative
        ? CircleEdges(Negative.Payload, Base + Axis * 6.0, Axis, 4.0) : std::vector<int>{};
    Deliver<BrepBody> NegativeFillet = !NegativeRoot.empty()
        ? BlendSolver::FilletEdge(Negative.Payload, NegativeRoot.front(), 1.5)
        : Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "negative-sweep root not found");
    Panel.Expect("Reversing the angular sweep preserves the chosen half and exact torus",
                 NegativeFillet && Inspect(NegativeFillet.Payload, Base, Axis,
                                            9.0, 6.0, 4.0, 5.0, 1.5).Analytic);

    Panel.Section("Unsupported endpoint arrangements refuse cleanly");
    std::vector<int> Outer2 = Split2 ? CircleEdges(Split2.Payload, RootCentre, Axis, OuterRadius) : std::vector<int>{};
    Panel.Expect("The outer semicircular chain is not misclassified as a boss root",
                 !Outer2.empty() && !BlendSolver::FilletEdge(Split2.Payload, Outer2.front(), Radius));
    Panel.Expect("A finite-chain radius consuming the boss height refuses",
                 !BlendSolver::FilletEdge(Split2.Payload, Root2.front(), BossHeight));
    Panel.Expect("A finite-chain radius consuming the shoulder refuses",
                 !BlendSolver::FilletEdge(Split2.Payload, Root2.front(), OuterRadius - BossRadius));
    BrepBody Unclassified = Split2.Payload;
    Unclassified.Edges[Root2.front()].Curve.Classification = CurveClassification::Freeform;
    Panel.Expect("One non-analytic chain member prevents a partial roll",
                 !BlendSolver::FilletEdge(Unclassified, Root2.front(), Radius));
    Deliver<BrepBody> FullCylinder = BrepBody::Cylinder(Base, Axis, 6.0, 12.0);
    std::vector<int> CylinderTop = FullCylinder ? CircleEdges(FullCylinder.Payload, Base + Axis * 12.0, Axis, 6.0) : std::vector<int>{};
    Panel.Expect("The established closed native-cylinder route remains available",
                 !CylinderTop.empty() && BlendSolver::FilletEdge(FullCylinder.Payload, CylinderTop.front(), 1.0));

    Panel.Section("Console commit and deterministic C++ proof");
#ifndef SOLIDARC_PROOF_FOLDER
#error SOLIDARC_PROOF_FOLDER must be supplied by the build
#endif
    ConsoleHost CommandHost(SOLIDARC_PROOF_FOLDER, 1280, 800);
    SceneFigure& CommandSource = CommandHost.Document().AddBody("OpenBoss", Split2.Payload);
    (void)CommandSource;
    const std::string Command = "fillet OpenBoss 2 --edges=" + std::to_string(Root2.front()) + " --name=OpenRoll";
    bool CommandAccepted = CommandHost.Execute(Command);
    const SceneFigure* CommandResult = CommandHost.Document().Find("OpenRoll");
    Panel.Expect("The C++ console accepts one internal member of the finite chain", CommandAccepted);
    Panel.Expect("The console commits the exact endpoint-aware kernel result",
                 CommandResult && CommandResult->Classification == FigureClassification::Body &&
                 Inspect(CommandResult->Body, Base, Axis, OuterRadius, ShoulderHeight,
                         BossRadius, BossHeight, Radius).Topology);

    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase32b_OpenChainFillet.png";
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
        Reset() && Add("OpenSource", SemicircularBoss({ -13, 0, 0 }, Axis, OuterRadius, ShoulderHeight,
                                                       BossRadius, BossHeight, 2).Payload, 0) &&
        Add("OpenRoll", Filleted2.Payload.Transformed(Mat4::Translation({ 13, 0, 0 })), 2) &&
        ProofHost.Execute("view iso") && ProofHost.Execute("view fit") && ProofHost.Execute("render sheet 0") &&
        Reset() && Add("EndpointSection", Filleted2.Payload, 2) && ProofHost.Execute("view front") &&
        ProofHost.Execute("view fit") && ProofHost.Execute("view dolly 0.72") && ProofHost.Execute("render sheet 1") &&
        Reset() && Add("FourMemberOpenSource", Split4.Payload, 6) && ProofHost.Execute("view iso") &&
        ProofHost.Execute("view fit") && ProofHost.Execute("render sheet 2") &&
        Reset() && Add("ObliqueOpenRoll", ObliqueFillet.Payload, 3) && ProofHost.Execute("view iso") &&
        ProofHost.Execute("view fit") && ProofHost.Execute("render sheet 3") &&
        ProofHost.Execute("render sheet finalize Phase32b_OpenChainFillet");
    Panel.Expect("C++ finite-chain proof commands complete without refusal", Rendered);
    Panel.Expect("C++ finite-chain proof is written and non-trivial",
                 std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);

    return Panel.Conclude();
}
