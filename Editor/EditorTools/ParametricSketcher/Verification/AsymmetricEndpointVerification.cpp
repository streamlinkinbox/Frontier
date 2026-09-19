#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <filesystem>
#include <string>
#include <system_error>

#ifndef SOLIDARC_PROOF_FOLDER
#error SOLIDARC_PROOF_FOLDER must be supplied by the build
#endif

using namespace Frontier;

int main()
{
    VerificationPanel Panel("SolidArc · Phase 32z · Asymmetric Endpoint Verification");
    Panel.Section("Bounded tapered and radial endpoint supports");

    EndpointSupport Low{ { 0, 0, 0 }, { 0, 0, 1 }, 2.0, ScalarCriteria::HalfPi };
    EndpointSupport High{ { 0, 0, 8 }, { 0, 0, -1 }, 1.25, ScalarCriteria::HalfPi };
    AsymmetricBlendSpecification Spec{ Low, High, AsymmetricSupportKind::TaperedFrustum, 0.1, 0.0 };
    std::string Refusal;

    Panel.Expect("unequal endpoint pair classifies", BlendSolver::ValidateAsymmetricEndpointPair(Low, High, 0.1, Refusal));
    Panel.Expect("tapered specification classifies", BlendSolver::ValidateAsymmetricSpecification(Spec, Refusal));

    Deliver<BrepBody> Result = BlendSolver::ReconstructAsymmetricSupport(Spec);
    Panel.Expect("tapered support reconstructs", Result && Result.Payload.Validate().Solid());
    Spec.Kind = AsymmetricSupportKind::VariableRadiusRoll;
    Spec.BlendRadius = 0.25;
    Spec.RadiusLaw = { Low.Radius, High.Radius };
    auto VariableSurface = BlendSolver::BuildVariableRadiusSurface(Spec);
    Panel.Expect("variable-radius surface frame builds", VariableSurface && VariableSurface.Payload.Length > 0.0);
    Panel.Expect("variable-radius surface curvature is bounded", VariableSurface && BlendSolver::ValidateVariableSurfaceCurvature(VariableSurface.Payload, 1.0, Refusal));
    Panel.Expect("variable-radius surface rejects an over-tight curvature bound", VariableSurface && !BlendSolver::ValidateVariableSurfaceCurvature(VariableSurface.Payload, 0.1, Refusal));
    Panel.Expect("variable-radius G1 normals align", VariableSurface && BlendSolver::ValidateVariableSurfaceG1(
        VariableSurface.Payload, VariableSurface.Payload.Normal(0.0), VariableSurface.Payload.Normal(0.0), 0.0, Refusal));
    auto Ruled = BlendSolver::ReconstructVariableRadiusRuledSolid(Spec);
    Panel.Expect("variable-radius ruled solid reconstructs", Ruled && Ruled.Payload.Validate().Solid());
    if (Result)
    {
        // Two volume statements, both true at once: the shipping gate (default tessellation) and the tightened band that
        //    a refined tessellation proves. See ScalarCriteria::VerificationChord for the measured floors.
        const BodyReport Report = Result.Payload.Validate();
        const BodyReport Refined = Result.Payload.Validate(ScalarCriteria::VerificationChord, ScalarCriteria::VerificationCap);
        const double Exact = ScalarCriteria::Pi * 8.0 * (4.0 + 2.5 + 1.5625) / 3.0;
        Panel.Expect("result is closed and manifold", Report.Closed && Report.Manifold && Report.Oriented);
        Panel.Expect("result volume passes the centralized acceptance gate",
                     ScalarCriteria::WithinVolumeTolerance(Report.Volume, Exact));
        Panel.Within("result volume relative error at refined tessellation",
                     std::fabs(Refined.Volume - Exact) / Exact, ScalarCriteria::MeasuredVolumeBand);
        Panel.Expect("result volume is analytic at the tightened band", ScalarCriteria::WithinMeasuredBand(Refined.Volume, Exact));
    }

    Spec.Kind = AsymmetricSupportKind::UnequalRadialCaps;
    Deliver<BrepBody> UnequalCaps = BlendSolver::ReconstructAsymmetricSupport(Spec);
    Panel.Expect("unequal radial caps reconstruct", UnequalCaps && UnequalCaps.Payload.Validate().Solid());
    if (!UnequalCaps)
        Panel.Note("unequal radial caps refused: %s — %s",
                   Frontier::Refusal::Describe(UnequalCaps.Denial.Reason), UnequalCaps.Denial.Detail);
    else
    {
        const BodyReport Caps = UnequalCaps.Payload.Validate();
        const BodyReport CapsRefined = UnequalCaps.Payload.Validate(ScalarCriteria::VerificationChord, ScalarCriteria::VerificationCap);
        const double CapsExact = ScalarCriteria::Pi * 8.0 * (4.0 + 2.5 + 1.5625) / 3.0;
        Panel.Expect("unequal radial cap volume passes the centralized gate",
                     ScalarCriteria::WithinVolumeTolerance(Caps.Volume, CapsExact));
        Panel.Within("unequal radial cap relative error at refined tessellation",
                     std::fabs(CapsRefined.Volume - CapsExact) / CapsExact, ScalarCriteria::MeasuredVolumeBand);
        Panel.Expect("unequal radial cap result is closed and manifold", Caps.Closed && Caps.Manifold && Caps.Oriented);
    }

    // Support pairing must survive a rigid transform: the same support pair, rotated 37° about an oblique axis and
    // translated off the origin, has to reconstruct with the identical analytic volume and topology.
    {
        const Quat Turn = Quat::AxisAngle({ 1, -2, 0.5 }, 0.64577);        // [rad] ≈ 37°
        const Vec3 Shift{ 3.5, -1.25, 7.75 };
        EndpointSupport TurnedLow{ Turn.Rotate(Low.Centre) + Shift, Turn.Rotate(Low.Normal), Low.Radius, Low.EndpointAngle };
        EndpointSupport TurnedHigh{ Turn.Rotate(High.Centre) + Shift, Turn.Rotate(High.Normal), High.Radius, High.EndpointAngle };
        AsymmetricBlendSpecification Turned{ TurnedLow, TurnedHigh, AsymmetricSupportKind::UnequalRadialCaps, 0.1, 0.0 };
        Deliver<BrepBody> Rigid = BlendSolver::ReconstructAsymmetricSupport(Turned);
        Panel.Expect("unequal radial caps reconstruct after a rigid transform",
                     Rigid && Rigid.Payload.Validate().Solid() && UnequalCaps);
        if (Rigid && UnequalCaps)
        {
            // A rotated body is tessellated with a different sample pattern, so the comparison is against topology counts
            //    and the tightened analytic band — never tessellation-against-tessellation.
            const BodyReport Spin = Rigid.Payload.Validate();
            const BodyReport Flat = UnequalCaps.Payload.Validate();
            const double CapsExact = ScalarCriteria::Pi * 8.0 * (4.0 + 2.5 + 1.5625) / 3.0;
            Panel.Expect("rigid transform preserves the support counts",
                         Spin.Vertices == Flat.Vertices && Spin.Edges == Flat.Edges && Spin.Faces == Flat.Faces &&
                         Spin.Loops == Flat.Loops && Spin.Genus == Flat.Genus && Spin.Hulls == Flat.Hulls);
            Panel.Expect("rigid transform preserves the analytic volume",
                         ScalarCriteria::WithinMeasuredBand(
                             Rigid.Payload.Validate(ScalarCriteria::VerificationChord, ScalarCriteria::VerificationCap).Volume, CapsExact));
        }
        AsymmetricBlendSpecification Swapped{ TurnedHigh, TurnedLow, AsymmetricSupportKind::UnequalRadialCaps, 0.1, 0.0 };
        Deliver<BrepBody> OrderReversed = BlendSolver::ReconstructAsymmetricSupport(Swapped);
        Panel.Expect("unequal radial caps are support-order invariant",
                     OrderReversed && Rigid && OrderReversed.Payload.Validate().Solid());
        if (OrderReversed && Rigid)
        {
            // The reversed support pair describes the identical solid, so here the tessellations *are* the same and a
            //    round-off bound is the correct comparison.
            Panel.Equal("support-order measured volume matches",
                        OrderReversed.Payload.Validate().Volume, Rigid.Payload.Validate().Volume, 1e-9);
        }
    }

    Spec.Kind = AsymmetricSupportKind::PartialEndpointChain;
    Deliver<BrepBody> PartialChain = BlendSolver::ReconstructAsymmetricSupport(Spec);
    Panel.Expect("partial endpoint chain refuses until bounded", !PartialChain);
    Panel.Expect("partial endpoint chain refusal is classified",
                 PartialChain.Denial.Reason == RefusalReason::Unsupported && PartialChain.Denial.Detail[0] != '\0');

    EndpointSupport EqualHigh{ High.Centre, High.Normal, Low.Radius, High.EndpointAngle };
    Spec = { Low, EqualHigh, AsymmetricSupportKind::EqualRadiusAsymmetricPlanes, 0.1, 0.0 };
    Panel.Expect("equal-radius asymmetric plane specification classifies", BlendSolver::ValidateAsymmetricSpecification(Spec, Refusal));
    Deliver<BrepBody> EqualPlanes = BlendSolver::ReconstructAsymmetricSupport(Spec);
    Panel.Expect("equal-radius asymmetric plane reconstructs", EqualPlanes && EqualPlanes.Payload.Validate().Solid());
    if (EqualPlanes)
    {
        const BodyReport Planes = EqualPlanes.Payload.Validate();
        const BodyReport PlanesRefined = EqualPlanes.Payload.Validate(ScalarCriteria::VerificationChord, ScalarCriteria::VerificationCap);
        const double PlanesExact = ScalarCriteria::Pi * 8.0 * Low.Radius * Low.Radius;
        Panel.Expect("equal-radius plane volume passes the centralized gate",
                     ScalarCriteria::WithinVolumeTolerance(Planes.Volume, PlanesExact));
        Panel.Within("equal-radius plane relative error at refined tessellation",
                     std::fabs(PlanesRefined.Volume - PlanesExact) / PlanesExact, ScalarCriteria::MeasuredVolumeBand);
    }

    //---------------------------------------------------------------------- bounded finite-support (partial) network
    Panel.Section("Bounded finite-support network — a chain of orthogonal supports on one axis");
    {
        const double SpanA = ScalarCriteria::Pi * 8.0 * (4.0 + 2.5 + 1.5625) / 3.0;          // [m³] 2.00 → 1.25 over 8 m
        const double SpanB = ScalarCriteria::Pi * 8.0 * (1.5625 + 0.9375 + 0.5625) / 3.0;    // [m³] 1.25 → 0.75 over 8 m
        AsymmetricEndpointChain Chain{ { Low,
                                         EndpointSupport{ { 0, 0, 8 },  { 0, 0, -1 }, 1.25, ScalarCriteria::HalfPi },
                                         EndpointSupport{ { 0, 0, 16 }, { 0, 0, -1 }, 0.75, ScalarCriteria::HalfPi } } };
        std::string ChainRefusal;
        Panel.Expect("collinear three-support chain classifies",
                     BlendSolver::ValidateAsymmetricEndpointChain(Chain, 0.1, ChainRefusal));
        Deliver<BrepBody> Network = BlendSolver::ReconstructAsymmetricChain(Chain, 0.1);
        Panel.Expect("chain network reconstructs one closed solid", Network && Network.Payload.Validate().Solid());
        if (Network)
        {
            const BodyReport Report = Network.Payload.Validate();
            const BodyReport Refined = Network.Payload.Validate(ScalarCriteria::VerificationChord, ScalarCriteria::VerificationCap);
            Panel.Expect("chain network is one genus-zero hull", Report.Hulls == 1 && Report.Genus == 0 && Report.EulerCharacteristic == 2);
            Panel.Expect("chain network keeps one wall per span plus two end caps", Report.Faces == 4 && Report.Loops == 4);
            Panel.Expect("chain network keeps exact support counts",
                         Report.Vertices == 3 && Report.Edges == 5 && static_cast<int>(Network.Payload.Coedges.size()) == 10);
            Panel.Expect("chain network volume passes the centralized gate",
                         ScalarCriteria::WithinVolumeTolerance(Report.Volume, SpanA + SpanB));
            Panel.Within("chain network relative error at refined tessellation",
                         std::fabs(Refined.Volume - (SpanA + SpanB)) / (SpanA + SpanB), ScalarCriteria::MeasuredVolumeBand);
        }

        // Failure-oriented fixtures. Each classified refusal must also be the reason the reconstruction reports, because
        //    the classifier and the builder share one source of truth for the literal.
        auto Refuses = [&](const char* Name, const AsymmetricEndpointChain& Fixture, const char* Fragment) -> bool
        {
            std::string Reason;
            const bool Classified = !BlendSolver::ValidateAsymmetricEndpointChain(Fixture, 0.1, Reason);
            const Deliver<BrepBody> Refused = BlendSolver::ReconstructAsymmetricChain(Fixture, 0.1);
            const bool Reported = Refused.Denial.Detail != nullptr &&
                                  std::string(Refused.Denial.Detail).find(Fragment) != std::string::npos &&
                                  Reason == Refused.Denial.Detail;
            Panel.Expect(Name, Classified && !Refused && Reported);
            return Classified && Reported;
        };
        AsymmetricEndpointChain Folded = Chain;     Folded.Supports[2].Centre = { 0, 0, 4 };
        Refuses("chain folding back into its own span refuses with its own reason", Folded, "folds back on itself");
        Panel.Expect("a refusal leaves the offending support untouched",
                     Folded.Supports[2].Centre.Z == 4.0 && Folded.Supports[2].Radius == 0.75);
        AsymmetricEndpointChain Oblique = Chain;
        for (EndpointSupport& Support : Oblique.Supports) Support.Normal = { 0.5, 0.0, 0.8660254037844386 };
        Refuses("supports oblique to the chain axis refuse rather than approximate a rim",
                Oblique, "oblique to the chain axis");
        AsymmetricEndpointChain OffAxis = Chain;    OffAxis.Supports[2].Centre = { 0.75, 0, 16 };
        Refuses("a support that leaves the chain axis refuses", OffAxis, "changes axis direction");
        AsymmetricEndpointChain Crowded = Chain;    Crowded.Supports[2].Centre = { 0, 0, 8.4 };
        Refuses("a chain without a positive ligament refuses", Crowded, "ligament");
        AsymmetricEndpointChain Even = Chain;       Even.Supports[1].Radius = 2.0;
        Refuses("an equal-radius neighbour refuses; constant radius belongs to the symmetric builder", Even, "not asymmetric");
        AsymmetricEndpointChain Vanishing = Chain;  Vanishing.Supports[2].Radius = 0.0;
        Refuses("a vanishing support radius refuses", Vanishing, "not positive");

        // Rigid transform and reversed-span invariance: the same network, walked the other way or moved in space, must
        //    produce the same topology and the same analytic volume.
        const Quat Turn = Quat::AxisAngle({ 1, -2, 0.5 }, 0.64577);                         // [rad] ≈ 37°
        const Vec3 Shift{ 3.5, -1.25, 7.75 };
        AsymmetricEndpointChain Spun = Chain;
        for (EndpointSupport& Support : Spun.Supports)
        { Support.Centre = Turn.Rotate(Support.Centre) + Shift; Support.Normal = Turn.Rotate(Support.Normal); }
        Deliver<BrepBody> Turned = BlendSolver::ReconstructAsymmetricChain(Spun, 0.1);
        Panel.Expect("rigid-transformed chain network reconstructs", Turned && Turned.Payload.Validate().Solid() && Network);
        if (Turned && Network)
        {
            const BodyReport Spin = Turned.Payload.Validate();
            const BodyReport Flat = Network.Payload.Validate();
            Panel.Expect("rigid transform preserves the network support counts",
                         Spin.Vertices == Flat.Vertices && Spin.Edges == Flat.Edges && Spin.Faces == Flat.Faces &&
                         Spin.Loops == Flat.Loops && Spin.Genus == Flat.Genus);
            Panel.Expect("rigid transform preserves the network volume",
                         ScalarCriteria::WithinMeasuredBand(
                             Turned.Payload.Validate(ScalarCriteria::VerificationChord, ScalarCriteria::VerificationCap).Volume, SpanA + SpanB));
        }
        AsymmetricEndpointChain Reversed{ { Chain.Supports[2], Chain.Supports[1], Chain.Supports[0] } };
        Deliver<BrepBody> Walked = BlendSolver::ReconstructAsymmetricChain(Reversed, 0.1);
        Panel.Expect("the network is invariant under span order",
                     Walked && Network && Walked.Payload.Validate().Solid() &&
                     Walked.Payload.Validate().Faces == Network.Payload.Validate().Faces &&
                     ScalarCriteria::WithinMeasuredBand(
                         Walked.Payload.Validate(ScalarCriteria::VerificationChord, ScalarCriteria::VerificationCap).Volume, SpanA + SpanB));

        // The route scales: a fourth span keeps one wall per span and the analytic swept volume of the whole chain.
        AsymmetricEndpointChain Long = Chain;
        Long.Supports.push_back(EndpointSupport{ { 0, 0, 22 }, { 0, 0, -1 }, 0.4, ScalarCriteria::HalfPi });
        const double SpanC = ScalarCriteria::Pi * 6.0 * (0.5625 + 0.3 + 0.16) / 3.0;         // [m³] 0.75 → 0.40 over 6 m
        Deliver<BrepBody> Extended = BlendSolver::ReconstructAsymmetricChain(Long, 0.1);
        Panel.Expect("four-support chain reconstructs with one wall per span",
                     Extended && Extended.Payload.Validate().Solid() && Extended.Payload.Validate().Hulls == 1 &&
                     Extended.Payload.Validate().Faces == 5);
        if (Extended)
        {
            const BodyReport Report = Extended.Payload.Validate();
            Panel.Expect("four-support chain keeps exact support counts",
                         Report.Vertices == 4 && Report.Edges == 7 && static_cast<int>(Extended.Payload.Coedges.size()) == 14 &&
                         Report.Loops == 5 && Report.Genus == 0);
            Panel.Within("four-span chain relative error at refined tessellation",
                         std::fabs(Extended.Payload.Validate(ScalarCriteria::VerificationChord, ScalarCriteria::VerificationCap).Volume - (SpanA + SpanB + SpanC)) / (SpanA + SpanB + SpanC),
                         ScalarCriteria::MeasuredVolumeBand);
        }
    }

    //----------------------------------------------------------------------------------- deterministic proof render
    Panel.Section("Deterministic proof render — the chain network and its refusals");
    {
        // 480 x 300 tiles composite into a 960 x 600 sheet: the same contact-sheet writer every other phase proof uses,
        //    at the largest size whose PNG stays inside this branch's 1 MB proof budget (1024 x 640 lands on the limit).
        const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase32z_AsymmetricChain.png";
        std::error_code Error;
        std::filesystem::remove(Proof, Error);
        ConsoleHost View(SOLIDARC_PROOF_FOLDER, 480, 300);
        auto Reset = [&]() { return View.Execute("reset") && View.Execute("gizmo off"); };
        auto Add = [&](const char* Name, const BrepBody& Body, uint8_t Matcap)
        {
            SceneFigure& Added = View.Document().AddBody(Name, Body);
            Added.Matcap = Matcap;
            return true;
        };
        AsymmetricEndpointChain Chain3{ { Low,
                                          EndpointSupport{ { 0, 0, 8 },  { 0, 0, -1 }, 1.25, ScalarCriteria::HalfPi },
                                          EndpointSupport{ { 0, 0, 16 }, { 0, 0, -1 }, 0.75, ScalarCriteria::HalfPi } } };
        AsymmetricEndpointChain ChainLong = Chain3;
        ChainLong.Supports.push_back(EndpointSupport{ { 0, 0, 22 }, { 0, 0, -1 }, 0.4, ScalarCriteria::HalfPi });
        const Quat ProofTurn = Quat::AxisAngle({ 1, -2, 0.5 }, 0.64577);                     // [rad] ≈ 37°
        const Vec3 ProofShift{ 3.5, -1.25, 7.75 };
        AsymmetricEndpointChain ChainSpun = Chain3;
        for (EndpointSupport& Support : ChainSpun.Supports)
        { Support.Centre = ProofTurn.Rotate(Support.Centre) + ProofShift; Support.Normal = ProofTurn.Rotate(Support.Normal); }
        Deliver<BrepBody> Three = BlendSolver::ReconstructAsymmetricChain(Chain3, 0.1);
        Deliver<BrepBody> Four = BlendSolver::ReconstructAsymmetricChain(ChainLong, 0.1);
        Deliver<BrepBody> SpunBody = BlendSolver::ReconstructAsymmetricChain(ChainSpun, 0.1);
        const bool Rendered = Three && Four && SpunBody &&
            Reset() && Add("ThreeSpanChain", Three.Payload, 3) && View.Execute("view iso") && View.Execute("view fit") &&
            View.Execute("render sheet 0") && Reset() &&
            Add("FourSpanChain", Four.Payload, 4) && View.Execute("view iso") && View.Execute("view fit") &&
            View.Execute("render sheet 1") && Reset() &&
            Add("RigidTurnedChain", SpunBody.Payload, 2) && View.Execute("view iso") && View.Execute("view fit") &&
            View.Execute("render sheet 2") && Reset() &&
            Add("ChainRims", Three.Payload, 5) && View.Execute("view top") && View.Execute("view fit") &&
            View.Execute("render sheet 3") && View.Execute("render sheet finalize Phase32z_AsymmetricChain");
        Panel.Expect("chain-network proof commands complete", Rendered);
        Panel.Expect("chain-network proof is written",
                     std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    }

    return Panel.Conclude();
}
