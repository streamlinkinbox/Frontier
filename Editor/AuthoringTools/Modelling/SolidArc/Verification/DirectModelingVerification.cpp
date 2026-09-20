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

    Panel.Section("A single edge and a connected planar edge loop become closed bevels transactionally");
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
    Panel.Expect("A connected edge-loop request commits as one result",
                 LoopHost.Execute("chamfer LoopBlock 0.2 --edges=0,1 --name=LoopBevel") &&
                 LoopHost.Document().Find("LoopBlock") == nullptr &&
                 LoopHost.Document().Find("LoopBevel") != nullptr &&
                 LoopHost.Document().Find("LoopBevel")->Body.Validate().Solid());
    if (const SceneFigure* Loop = LoopHost.Document().Find("LoopBevel"))
    {
        const BodyReport R = Loop->Body.Validate();
        Panel.Expect("The console loop has two chamfer planes (V11/E17/F8)", R.Vertices == 11 && R.Edges == 17 && R.Faces == 8);
    }

    ConsoleHost RefusalHost(SOLIDARC_PROOF_FOLDER, 640, 480);
    Panel.Expect("A self-intersecting setback refuses transactionally",
                 RefusalHost.Execute("box (0,0,0) (4,3,2) --name=TooLarge") &&
                 !RefusalHost.Execute("chamfer TooLarge 2.0 --edges=0 --name=MustNotExist") &&
                 RefusalHost.Document().Find("TooLarge") != nullptr &&
                 RefusalHost.Document().Find("MustNotExist") == nullptr);

    Panel.Section("Visual proof: sharp source beside single-edge and edge-loop bevels");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase34c_SingleEdgeChamfer.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1280, 800);
    const bool Rendered = ProofHost.Execute("box (-9,0,0) (-5,3,2) --name=Sharp") &&
                          ProofHost.Execute("box (-2,0,0) (2,3,2) --name=Source") &&
                          ProofHost.Execute("chamfer Source 0.2 --edges=0 --name=Beveled") &&
                          ProofHost.Execute("box (5,0,0) (9,3,2) --name=LoopSource") &&
                          ProofHost.Execute("chamfer LoopSource 0.2 --edges=0,1 --name=LoopBeveled") &&
                          ProofHost.Execute("view iso") && ProofHost.Execute("view fit") &&
                          ProofHost.Execute("render Phase34c_SingleEdgeChamfer");
    Panel.Expect("The chamfer proof render completes", Rendered);
    Panel.Expect("The chamfer proof PNG is written", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);

    return Panel.Conclude();
}
