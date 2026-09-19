#include "Kernel/BlendSolver.h"
#include "VerificationPanel.h"
#include <string>

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

    return Panel.Conclude();
}
