//=============================================================================================================================================
// SolidArc Phase 34c/34d: transactional planar edge-set chamfers.
// Connected selections are solved as one convex half-space reconstruction when the source is a convex planar solid;
// this is the route that gives arbitrary dihedral angles and real endpoint mitres instead of sequential cutter seams.
#include "Console/ConsoleHost.h"
#include "Kernel/BlendSolver.h"
#include "VerificationPanel.h"

#include <cmath>
#include <filesystem>
#include <string>

using namespace Frontier;

int main()
{
    VerificationPanel Panel("SolidArc · planar edge-set chamfer · arbitrary dihedral and loop transaction");

    Panel.Section("Connected planar selections commit together and preserve the source on failure");
    auto Box = BrepBody::Box({ 0, 0, 0 }, { 4, 3, 2 });
    Panel.Expect("The box fixture is a closed solid", Box && Box.Payload.Validate().Solid());
    const double BoxVolume = Box ? Box.Payload.Validate().Volume : 0.0;

    int Applied = 0;
    auto Adjacent = BlendSolver::ChamferEdges(Box.Payload, { 0, 1 }, 0.2, &Applied);
    Panel.Expect("Two adjacent planar edges commit as one mitred transaction", Adjacent && Applied == 2);
    if (Adjacent)
    {
        BodyReport R = Adjacent.Payload.Validate();
        Panel.Expect("The adjacent result is closed, manifold and oriented", R.Solid());
        Panel.Expect("The adjacent result has two new chamfer planes (V11/E17/F8)", R.Vertices == 11 && R.Edges == 17 && R.Faces == 8);
        Panel.Expect("The adjacent loop removes material without over-cutting the two wedges",
                     R.Volume < BoxVolume && R.Volume > BoxVolume - 2.0 * BlendSolver::ChamferRemoval(
                         [&]() { EdgeCornerFrame F; std::string Why; (void)BlendSolver::Frame(Box.Payload, 0, F, Why); return F; }(), 0.2) - 1e-3);
    }
    Panel.Expect("The source remains untouched after the committed transaction",
                 Box.Payload.Validate().Solid() && std::fabs(Box.Payload.Validate().Volume - BoxVolume) < 1e-9);

    Applied = 0;
    auto Loop = BlendSolver::ChamferEdges(Box.Payload, { 0, 1, 2, 3 }, 0.2, &Applied);
    Panel.Expect("A four-edge planar loop commits in one operation", Loop && Applied == 4);
    if (Loop)
    {
        BodyReport R = Loop.Payload.Validate();
        Panel.Expect("The four-edge loop is a single closed solid", R.Solid());
        Panel.Expect("The loop has four chamfer planes (V12/E20/F10)", R.Vertices == 12 && R.Edges == 20 && R.Faces == 10);
    }

    Panel.Section("Arbitrary dihedral and self-intersection feasibility");
    auto Profile = NurbsCurve::Polyline({ { 0, 0, 0 }, { 4, 0, 0 }, { 1, 3, 0 }, { 0, 0, 0 } }, true);
    auto Prism = Profile ? BrepBody::Extrude(Profile.Payload, { 0, 0, 1 }, 4.0)
                         : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "triangle profile did not build");
    Panel.Expect("The triangular prism fixture is a closed solid", Prism && Prism.Payload.Validate().Solid());
    EdgeCornerFrame Dihedral;
    std::string Why;
    const bool Framed = Prism && BlendSolver::Frame(Prism.Payload, 1, Dihedral, Why);
    Panel.Expect("A non-right planar edge is accepted", Framed && std::fabs(Dihedral.Dihedral - ScalarCriteria::Pi * 0.25) < 1e-6);
    Applied = 0;
    auto Oblique = Prism ? BlendSolver::ChamferEdges(Prism.Payload, { 1 }, 0.2, &Applied)
                         : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "missing prism");
    Panel.Expect("The arbitrary-dihedral chamfer commits", Oblique && Applied == 1);
    if (Oblique)
    {
        BodyReport R = Oblique.Payload.Validate();
        Panel.Expect("The arbitrary-dihedral result is a closed solid", R.Solid());
        Panel.Within("The arbitrary-dihedral volume follows the analytic wedge", std::fabs(R.Volume -
            (Prism.Payload.Validate().Volume - BlendSolver::ChamferRemoval(Dihedral, 0.2))), 3e-3);
    }

    auto Refused = BlendSolver::ChamferEdges(Box.Payload, { 0 }, 2.0, &Applied);
    Panel.Expect("An over-large setback refuses before self-intersection", !Refused && Applied == 0);
    Panel.Expect("A refused setback leaves the source unchanged", Box.Payload.Validate().Solid() &&
                 std::fabs(Box.Payload.Validate().Volume - BoxVolume) < 1e-9);

    Panel.Section("Visual proof: sharp, adjacent-edge and four-edge-loop results");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase34d_ChamferLoop.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 800);
    const bool Rendered = Host.Execute("box (-9,0,0) (-5,3,2) --name=Sharp") &&
                          Host.Execute("box (-2,0,0) (2,3,2) --name=AdjacentSource") &&
                          Host.Execute("chamfer AdjacentSource 0.2 --edges=0,1 --name=Adjacent") &&
                          Host.Execute("box (5,0,0) (9,3,2) --name=LoopSource") &&
                          Host.Execute("chamfer LoopSource 0.2 --edges=0,1,2,3 --name=Loop") &&
                          Host.Execute("view iso") && Host.Execute("view fit") &&
                          Host.Execute("render Phase34d_ChamferLoop");
    Panel.Expect("The edge-loop proof render completes", Rendered);
    Panel.Expect("The edge-loop proof PNG is written", std::filesystem::exists(Proof) &&
                 std::filesystem::file_size(Proof, Error) > 100000);

    return Panel.Conclude();
}
