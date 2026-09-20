//=============================================================================================================================================
// SolidArc direct-modelling smoke/regression: the user-facing single-edge chamfer route.
// The legacy BrepBody::ChamferEdge helper is intentionally not used here; the console's BlendSolver route is the
// transactional, closed-solid operation exposed to a modeller.
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"

#include <cmath>
#include <filesystem>

using namespace Frontier;

int main()
{
    VerificationPanel Panel("SolidArc · direct modelling · single-edge chamfer");

    Panel.Section("A single selected solid edge becomes a closed bevel, while an edge loop is explicit refusal");
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 640, 480);
    Panel.Expect("A box primitive is accepted", Host.Execute("box (0,0,0) (4,3,2) --name=Block"));
    Panel.Expect("One edge chamfer commits", Host.Execute("chamfer Block 0.2 --edges=0 --name=Bevel"));
    const SceneFigure* Bevel = Host.Document().Find("Bevel");
    Panel.Expect("The source is consumed and the bevel result is a solid", Bevel != nullptr && Host.Document().Find("Block") == nullptr && Bevel->Body.Validate().Solid());
    if (Bevel)
    {
        const BodyReport R = Bevel->Body.Validate();
        Panel.Expect("The single edge adds exactly one face (V10/E15/F7)", R.Vertices == 10 && R.Edges == 15 && R.Faces == 7);
        Panel.Within("The bevel removes the expected 0.08 cubic units", std::fabs(R.Volume - 23.92), 1e-6);
    }

    ConsoleHost LoopHost(SOLIDARC_PROOF_FOLDER, 640, 480);
    Panel.Expect("A second box primitive is accepted", LoopHost.Execute("box (0,0,0) (4,3,2) --name=LoopBlock"));
    Panel.Expect("An edge-loop request refuses instead of beveling only its first member",
                 !LoopHost.Execute("chamfer LoopBlock 0.2 --edges=0,1 --name=ShouldNotExist") &&
                 LoopHost.Document().Find("LoopBlock") != nullptr && LoopHost.Document().Find("ShouldNotExist") == nullptr);

    Panel.Section("Visual proof: sharp source beside the single-edge bevel");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase34c_SingleEdgeChamfer.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1280, 800);
    const bool Rendered = ProofHost.Execute("box (-6,0,0) (-2,3,2) --name=Sharp") &&
                          ProofHost.Execute("box (2,0,0) (6,3,2) --name=Source") &&
                          ProofHost.Execute("chamfer Source 0.2 --edges=0 --name=Beveled") &&
                          ProofHost.Execute("view iso") && ProofHost.Execute("view fit") &&
                          ProofHost.Execute("render Phase34c_SingleEdgeChamfer");
    Panel.Expect("The chamfer proof render completes", Rendered);
    Panel.Expect("The chamfer proof PNG is written", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);

    return Panel.Conclude();
}
