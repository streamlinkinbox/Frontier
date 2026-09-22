//=============================================================================================================================================
// SolidArc · Phase 36t · bounded linear variable-radius root blend
//
// This route deliberately remains the exact linear-law ruled/frustum root blend. It does not
// claim rolling-ball variable-radius fillets, partial edges, nonlinear laws, or G2 joins.
//=============================================================================================================================================
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <string>

using namespace Frontier;

int main()
{
    VerificationPanel Panel("SolidArc · Phase 36t · linear variable-radius root blend");
    Panel.Section("Explicit linear radius law and ruled surface");

    const EndpointSupport Low{ { 0, 0, 0 }, { 0, 0, 1 }, 2.5 };
    const EndpointSupport High{ { 0, 0, 8 }, { 0, 0, -1 }, 1.25 };
    AsymmetricBlendSpecification Specification;
    Specification.Low = Low;
    Specification.High = High;
    Specification.Classification = AsymmetricSupportClassification::VariableRadiusRoll;
    Specification.MinimumClearance = 0.2;
    Specification.BlendRadius = 0.35;
    Specification.RadiusLaw = { Low.Radius, High.Radius };
    std::string Refusal;

    Panel.Expect("The complete circular linear variable-radius root specification validates", BlendSolver::ValidateAsymmetricSpecification(Specification, Refusal));
    Deliver<VariableRadiusSurface> Surface = BlendSolver::BuildVariableRadiusSurface(Specification);
    Panel.Expect("The variable-radius root surface builds from the explicit law", Surface && Surface.Payload.Length == 8.0 && Surface.Payload.Law.Positive());
    if (Surface)
    {
        Panel.Within("The law reaches its low endpoint radius", std::fabs(Surface.Payload.Law.Radius(0.0) - Low.Radius), 1e-12);
        Panel.Within("The law reaches its high endpoint radius", std::fabs(Surface.Payload.Law.Radius(1.0) - High.Radius), 1e-12);
        const double Slope = Surface.Payload.Law.Slope();
        Panel.Within("The generator derivative is constant for the linear law", (Surface.Payload.TangentAlong(0.7).Length() - Surface.Payload.TangentAlong(2.2).Length()), 1e-12);
        const double MaximumCurvature = 1.0 / (High.Radius * std::sqrt(1.0 + (Slope / Surface.Payload.Length) * (Slope / Surface.Payload.Length))) + 1e-9;
        Panel.Expect("The sampled circumferential curvature stays within the declared bound", BlendSolver::ValidateVariableSurfaceCurvature(Surface.Payload, MaximumCurvature, Refusal));
        Panel.Expect("A bound below the smallest-radius curvature refuses", !BlendSolver::ValidateVariableSurfaceCurvature(Surface.Payload, MaximumCurvature - 1e-4, Refusal));
    }

    Panel.Section("Exact bounded ruled-solid reconstruction");
    Deliver<BrepBody> Result = BlendSolver::ReconstructVariableRadiusRootBlend(Specification);
    Panel.Expect("The complete linear root law reconstructs one exact positive-volume solid", Result && Result.Payload.Validate().Solid());
    if (Result)
    {
        const BodyReport Report = Result.Payload.Validate();
        const double ExpectedVolume = Specification.RadiusLaw.SweptVolume(8.0);
        Panel.Expect("The root blend retains native frustum topology V2/E3/F3", Report.Vertices == 2 && Report.Edges == 3 && Report.Faces == 3);
        Panel.Within("The root blend volume follows the swept law", std::fabs(Report.Volume - ExpectedVolume) / ExpectedVolume, 1e-3);
        Panel.Expect("The root blend has one hull and no broken edges", Report.Hulls == 1 && Report.OpenEdges == 0 && Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0);
    }

    Panel.Section("Transactional refusals for unsupported variable-radius root modes");
    AsymmetricBlendSpecification BadLaw = Specification;
    BadLaw.RadiusLaw.End = 0.0;
    Panel.Expect("A law reaching zero radius refuses", !BlendSolver::ReconstructVariableRadiusRootBlend(BadLaw));
    BadLaw = Specification;
    BadLaw.Low.EndpointAngle = ScalarCriteria::Pi;
    BadLaw.High.EndpointAngle = ScalarCriteria::Pi;
    Panel.Expect("Partial circular supports remain explicit refusal", !BlendSolver::ReconstructVariableRadiusRootBlend(BadLaw));
    BadLaw = Specification;
    BadLaw.High.Centre = { 3, 0, 8 };
    Panel.Expect("A non-axial endpoint centre refuses", !BlendSolver::ReconstructVariableRadiusRootBlend(BadLaw));
    BadLaw = Specification;
    BadLaw.Classification = AsymmetricSupportClassification::PartialEndpointChain;
    Panel.Expect("A partial endpoint-chain mode remains refused", !BlendSolver::ReconstructVariableRadiusRootBlend(BadLaw));
    BadLaw = Specification;
    BadLaw.RadiusLaw.Start = 2.0;
    BadLaw.RadiusLaw.End = 1.5;
    Panel.Expect("A law that does not match the measured support radii refuses", !BlendSolver::ReconstructVariableRadiusRootBlend(BadLaw));

    Panel.Section("Visible proof of the accepted law");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase36t_VariableRadiusRootBlend.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1600, 900);
    const bool Rendered = Result &&
        ProofHost.Document().AddBody("LinearVariableRadiusFrustum", Result.Payload.Transformed(Mat4::Translation({ 0, 0, 0 }))).Identity > 0 &&
        ProofHost.Execute("show shading flat") && ProofHost.Execute("view iso") &&
        ProofHost.Execute("view orbit 190 -10") && ProofHost.Execute("view fit") &&
        ProofHost.Execute("render Phase36t_VariableRadiusRootBlend");
    Panel.Expect("The accepted variable-radius root-blend proof render completes", Rendered);
    Panel.Expect("The variable-radius root-blend proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);

    return Panel.Conclude();
}
