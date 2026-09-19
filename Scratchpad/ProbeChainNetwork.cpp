//============================================================================================================================================
// 📦 Scratchpad/ProbeChainNetwork.cpp — measures the bounded finite-support chain network before its checks are fixed
//============================================================================================================================================
// Diagnostics only: prints topology, exact swept volume and refusals for a three-support chain and its failure fixtures,
//    so Verification/AsymmetricEndpointVerification.cpp can assert exact counts instead of ranges.
#include "Kernel/BlendSolver.h"
#include <cstdio>

using namespace Frontier;

static void Show(const char* What, const Deliver<BrepBody>& Built)
{
    if (!Built) { std::printf("  %-34s refused: %s — %s\n", What, Refusal::Describe(Built.Denial.Reason), Built.Denial.Detail); return; }
    const BodyReport R = Built.Payload.Validate();
    const BodyReport T = Built.Payload.Validate(ScalarCriteria::VerificationChord, ScalarCriteria::VerificationCap);
    std::printf("  %-34s V%-3d E%-3d C%-3d L%-3d F%-3d χ%-3d hulls%-2d genus%-2d vol %.9f (refined %.9f)\n",
                What, R.Vertices, R.Edges, (int)Built.Payload.Coedges.size(), R.Loops, R.Faces, R.EulerCharacteristic, R.Hulls, R.Genus,
                R.Volume, T.Volume);
}

int main()
{
    const AsymmetricEndpointChain Chain{ {
        { { 0, 0, 0 },  { 0, 0, 1 },  2.0,  ScalarCriteria::HalfPi },
        { { 0, 0, 8 },  { 0, 0, -1 }, 1.25, ScalarCriteria::HalfPi },
        { { 0, 0, 16 }, { 0, 0, -1 }, 0.75, ScalarCriteria::HalfPi } } };

    std::string Refusal;
    std::printf("\n  chain classifies: %d  %s\n", int(BlendSolver::ValidateAsymmetricEndpointChain(Chain, 0.1, Refusal)), Refusal.c_str());
    Show("three-support chain", BlendSolver::ReconstructAsymmetricChain(Chain, 0.1));

    AsymmetricEndpointChain Folded = Chain;  Folded.Supports[2].Centre = { 0, 0, 4 };
    Show("folding chain", BlendSolver::ReconstructAsymmetricChain(Folded, 0.1));
    AsymmetricEndpointChain Oblique = Chain; Oblique.Supports[2].Normal = { 0.5, 0, -0.8660254037844386 };
    Show("oblique support", BlendSolver::ReconstructAsymmetricChain(Oblique, 0.1));
    AsymmetricEndpointChain Offset = Chain;  Offset.Supports[2].Centre = { 0.75, 0, 16 };
    Show("support off the axis", BlendSolver::ReconstructAsymmetricChain(Offset, 0.1));
    AsymmetricEndpointChain Crowded = Chain; Crowded.Supports[2].Centre = { 0, 0, 8.4 };
    Show("crowded chain", BlendSolver::ReconstructAsymmetricChain(Crowded, 0.1));
    AsymmetricEndpointChain Tilted = Chain;
    for (EndpointSupport& S : Tilted.Supports) S.Normal = { 0.5, 0.0, 0.8660254037844386 };
    Show("supports oblique to the axis", BlendSolver::ReconstructAsymmetricChain(Tilted, 0.1));

    const AsymmetricEndpointChain Long{ {
        { { 0, 0, 0 },  { 0, 0, 1 },  2.0,  ScalarCriteria::HalfPi },
        { { 0, 0, 8 },  { 0, 0, -1 }, 1.25, ScalarCriteria::HalfPi },
        { { 0, 0, 16 }, { 0, 0, -1 }, 0.75, ScalarCriteria::HalfPi },
        { { 0, 0, 22 }, { 0, 0, -1 }, 0.4,  ScalarCriteria::HalfPi } } };
    Show("four-support chain", BlendSolver::ReconstructAsymmetricChain(Long, 0.1));

    const Quat Turn = Quat::AxisAngle({ 1, -2, 0.5 }, 0.64577);
    const Vec3 Shift{ 3.5, -1.25, 7.75 };
    AsymmetricEndpointChain Spun = Chain;
    for (EndpointSupport& S : Spun.Supports) { S.Centre = Turn.Rotate(S.Centre) + Shift; S.Normal = Turn.Rotate(S.Normal); }
    Show("rigid-transformed chain", BlendSolver::ReconstructAsymmetricChain(Spun, 0.1));

    AsymmetricEndpointChain Even = Chain;    Even.Supports[1].Radius = 2.0;
    Show("equal-radius neighbour", BlendSolver::ReconstructAsymmetricChain(Even, 0.1));
    AsymmetricEndpointChain Degenerate = Chain; Degenerate.Supports[2].Radius = 0.0;
    Show("vanishing support radius", BlendSolver::ReconstructAsymmetricChain(Degenerate, 0.1));
    return 0;
}
