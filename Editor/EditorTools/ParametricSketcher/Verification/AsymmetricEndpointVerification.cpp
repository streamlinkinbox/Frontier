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
        const BodyReport Report = Result.Payload.Validate();
        const double Exact = ScalarCriteria::Pi * 8.0 * (4.0 + 2.5 + 1.5625) / 3.0;
        Panel.Expect("result is closed and manifold", Report.Closed && Report.Manifold && Report.Oriented);
        Panel.Equal("result volume is analytic frustum volume", Report.Volume, Exact, 1e-6);
    }

    Spec.Kind = AsymmetricSupportKind::UnequalRadialCaps;
    Panel.Expect("unequal radial caps reconstruct", BlendSolver::ReconstructAsymmetricSupport(Spec));
    Spec.Kind = AsymmetricSupportKind::PartialEndpointChain;
    Panel.Expect("partial endpoint chain refuses until bounded", !BlendSolver::ReconstructAsymmetricSupport(Spec));

    EndpointSupport EqualHigh{ High.Centre, High.Normal, Low.Radius, High.EndpointAngle };
    Spec = { Low, EqualHigh, AsymmetricSupportKind::EqualRadiusAsymmetricPlanes, 0.1, 0.0 };
    Panel.Expect("equal-radius asymmetric plane specification classifies", BlendSolver::ValidateAsymmetricSpecification(Spec, Refusal));
    Panel.Expect("equal-radius asymmetric plane reconstructs", BlendSolver::ReconstructAsymmetricSupport(Spec));

    return Panel.Conclude();
}
