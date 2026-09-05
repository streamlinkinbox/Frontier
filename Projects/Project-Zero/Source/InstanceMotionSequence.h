//============================================================================================================================================
//                                                    INSTANCEMOTIONSEQUENCE.H
//============================================================================================================================================
// 🧩 D3 — a scripted, deterministic driver for per-instance transforms. Moves a chosen span of instances along an
//    analytic path so the upload path can be proven WITHOUT physics in the loop.
//
//    Why a scripted driver before Jolt: if the balls come straight from the solver and the picture is wrong, the
//    fault could be the transform maths, the upload, the descriptor lifetime, or the physics itself. Driving the
//    same path from a closed-form sine reduces that to one variable — the expected pose at time τ is known exactly,
//    so a mismatch is provably the renderer's, not the solver's. D4 swaps this for RigidBodySolver poses and
//    nothing else changes.
//
//    Motion is a vertical bob plus a yaw, per instance, phase-offset so the bodies do not move in lockstep (which
//    would hide an indexing error where every instance reads slot 0).
//
//    Engine ⇄ project seam: this lives in the PROJECT. The engine is handed finished InstanceRecord rows and is
//    told nothing about why they moved.

#pragma once

#include "../../../Engine/GeometricRaster/SceneStructure.h"

#include <cstdint>
#include <vector>

namespace Frontier {
namespace ProjectZero {

struct InstanceMotionConfiguration
{
    uint32_t FirstInstance  = 0u;      // [idx] first instance this sequence owns
    uint32_t InstanceCount  = 0u;      // [cnt] how many it drives (0 = none; the rest stay static)
    float    BobAmplitude   = 0.35f;   // [m]   vertical travel, ± this about the rest pose
    float    BobRate        = 0.60f;   // [Hz]  bob cycles per second
    float    YawRate        = 0.45f;   // [rad/s] spin about +Z
    float    PhaseStride    = 0.7f;    // [rad] phase added per instance so they desynchronise
};

class InstanceMotionSequence
{
public:
    // Captures the rest pose of every driven instance. Must be called once after the scene is resident and before
    //    the first Advance, because the motion is expressed relative to where the instance already sits.
    void Construct(const std::vector<InstanceRecord>& Instances, const InstanceMotionConfiguration& Configuration) noexcept;

    // Writes new World matrices for the driven span into Rows (which must be the full instance list). Rolls the
    //    previous World into PreviousWorld first, so motion vectors and ReSTIR reprojection stay correct.
    void AdvanceMotion(std::vector<InstanceRecord>& Rows, double Elapsed) const noexcept;

    // Closed-form expected height of a driven instance at time τ — the oracle the proof checks the renderer against.
    [[nodiscard]] float QueryExpectedHeight(uint32_t Ordinal, double Elapsed) const noexcept;

    [[nodiscard]] uint32_t QueryDrivenCount() const noexcept { return static_cast<uint32_t>(RestHeights.size()); }
    [[nodiscard]] const InstanceMotionConfiguration& QueryConfiguration() const noexcept { return Config; }

private:
    InstanceMotionConfiguration Config;
    std::vector<float>          RestHeights;   // [m] Z translation of each driven instance at capture
};

} // namespace ProjectZero
} // namespace Frontier
