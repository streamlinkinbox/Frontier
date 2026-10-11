//=============================================================================================================================================
// SolidArc Phase 34e: bounded face rotation and uniform scale tweaks on fixed topology.
#include "Console/ConsoleHost.h"
#include "Kernel/TweakSolver.h"
#include "VerificationPanel.h"

#include <cmath>
#include <filesystem>
#include <string>

using namespace Frontier;

int main()
{
    VerificationPanel Panel("SolidArc · face rotation and scale tweaks · fixed topology");

    Panel.Section("Uniform face scale keeps topology and preserves a closed solid");
    auto Box = BrepBody::Box({ 0, 0, 0 }, { 4, 3, 2 });
    const double OriginalVolume = Box ? Box.Payload.Validate().Volume : 0.0;
    auto Scaled = Box ? TweakSolver::ScaleFace(Box.Payload, 1, 0.5)
                      : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "box fixture failed");
    Panel.Expect("The top face scale commits", static_cast<bool>(Scaled));
    if (Scaled)
    {
        const BodyReport R = Scaled.Payload.Validate();
        Panel.Expect("The scaled result is closed, manifold and oriented", R.Solid());
        Panel.Expect("Scaling keeps fixed topology (V8/E12/F6)", R.Vertices == 8 && R.Edges == 12 && R.Faces == 6);
        Panel.Equal("The tapered box volume is 14", R.Volume, 14.0, 1e-9);
    }
    Panel.Expect("The source remains untouched", Box && Box.Payload.Validate().Solid() &&
                 std::fabs(Box.Payload.Validate().Volume - OriginalVolume) < 1e-9);

    Panel.Section("Rotation accepts valid planar transforms and refuses silent warps");
    auto RefusedRotation = Box ? TweakSolver::RotateFace(Box.Payload, 1, { 0, 0, 1 }, ScalarCriteria::Radians(12.0))
                               : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "box fixture failed");
    Panel.Expect("An in-plane rotation refuses the adjacent non-planar side quads by default", !RefusedRotation);
    Panel.Expect("The refused rotation leaves the source unchanged", Box && Box.Payload.Validate().Solid() &&
                 std::fabs(Box.Payload.Validate().Volume - OriginalVolume) < 1e-9);

    auto Rotated = Box ? TweakSolver::RotateFace(Box.Payload, 1, { 0, 0, 1 }, ScalarCriteria::Radians(12.0), true)
                       : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "box fixture failed");
    Panel.Expect("The same rotation commits with explicit warp permission", static_cast<bool>(Rotated));
    if (Rotated)
    {
        const BodyReport R = Rotated.Payload.Validate();
        Panel.Expect("The rotated result remains a closed solid", R.Solid());
        Panel.Expect("Rotation keeps fixed topology (V8/E12/F6)", R.Vertices == 8 && R.Edges == 12 && R.Faces == 6);
        Panel.Equal("The explicitly warped rotation has a positive numerical volume", R.Volume, 23.8252, 1e-3);
        Panel.Note("The bilinear side-wall warp is intentionally not volume-preserving; the solid and topology remain validated.");
    }

    auto Curved = BrepBody::Cylinder({ 0, 0, 0 }, { 0, 0, 1 }, 1.0, 2.0);
    auto CurvedRefusal = Curved ? TweakSolver::ScaleFace(Curved.Payload, 0, 0.5)
                                : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cylinder fixture failed");
    Panel.Expect("A curved-rim face scale refuses instead of approximating the circle", !CurvedRefusal);

    Panel.Section("Console commit and visible proof");
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1600, 800);
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase34e_TransformTweaks.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    const bool Commands = Host.Execute("box (-9,0,0) (-5,3,2) --name=Sharp") &&
                          Host.Execute("box (-2,0,0) (2,3,2) --name=ScaleSource") &&
                          Host.Execute("scale ScaleSource 0.5 --face=1 --name=Scaled") &&
                          Host.Execute("box (5,0,0) (9,3,2) --name=RotateSource") &&
                          Host.Execute("rotate RotateSource 12 --face=1 --axis=(0,0,1) --warp --name=Rotated") &&
                          Host.Execute("view iso") && Host.Execute("view fit") &&
                          Host.Execute("render Phase34e_TransformTweaks");
    Panel.Expect("The rotate/scale console commands commit", Commands);
    Panel.Expect("The transform-tweak proof PNG is written", std::filesystem::exists(Proof) &&
                 std::filesystem::file_size(Proof, Error) > 100000);

    return Panel.Conclude();
}
