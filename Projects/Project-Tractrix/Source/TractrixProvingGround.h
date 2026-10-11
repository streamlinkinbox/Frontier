//============================================================================================================================================
// 📦 Projects/Project-Tractrix/Source/TractrixProvingGround.h — procedural Jolt-heightfield proving ground (Phase 3)
//============================================================================================================================================
//
//    The Phase-3 drivable scene needs ground. The user locked terrain to a **Jolt heightfield**, so this generates one
//    procedurally: a flat spawn pad that eases into gentle rolling hills, sized as a power-of-two-ish grid Jolt accepts
//    (SampleCount a non-zero multiple of 8). It does two things:
//
//        • `Create(solver)` builds the actual `HeightfieldDescription` and hands it to `RigidBodySolver::CreateHeightfieldBody`
//          so every OTHER rigid body (props, the chassis if it tips over) collides with the terrain in Jolt;
//        • `Sample(p, …)` is an analytic bilinear query over the SAME height grid, used as the soft tyre's `GroundQuery` so
//          the XPBD tyre nodes contact the heightfield DIRECTLY (the user's Phase-3 requirement) — cheaply and exactly,
//          without casting a ray per node per substep.
//
//    Height-grid → world mapping matches HeightfieldDescription: sample [row,col] lands at
//        world.x = Origin.x + col·Spacing,  world.y = Origin.y − row·Spacing,  world.z = Origin.z + sample·HeightScale.
//    (col advances toward +X, row advances toward −Y — the +Z-up wrap the seam applies.)

#pragma once

#include "../../../Engine/PhysicalDynamics/RigidBodySolver.h"
#include "../../../Engine/PhysicalDynamics/Vehicle/XPBDSoftTyre.h"   // Frontier::Vehicle::Vec3

#include <cmath>
#include <cstdint>
#include <vector>

namespace Frontier::Tractrix {

class TractrixProvingGround
{
public:
    uint32_t          SampleCount = 128u;                 // per side; MUST be a multiple of 8 (Jolt block constraint)
    float             Spacing     = 1.5f;                 // [m] world step between adjacent samples (both axes)
    float             HeightScale = 1.0f;                 // [-] multiplies each sample into world-Z
    Vector3           Origin      { -96.0f, 96.0f, 0.0f };// world position of sample [row=0, col=0]
    float             Friction    = 0.95f;
    RigidBodyIdentity Body        = InvalidRigidBody;

    // Terrain shape parameters (metres).
    float PadRadius   = 10.0f;    // flat spawn pad radius around the world origin
    float HillAmp     = 1.25f;    // rolling-hill amplitude away from the pad
    float HillFreqX   = 0.055f;   // spatial frequency of the hills
    float HillFreqY   = 0.043f;

    // Fill `heights_` with a flat pad blended into rolling hills. Call before Create()/Sample().
    void Generate() noexcept
    {
        heights_.assign(static_cast<size_t>(SampleCount) * SampleCount, 0.0f);
        for (uint32_t row = 0; row < SampleCount; ++row)
            for (uint32_t col = 0; col < SampleCount; ++col)
            {
                const float x = Origin.x + col * Spacing;
                const float y = Origin.y - row * Spacing;
                heights_[static_cast<size_t>(row) * SampleCount + col] = HeightForWorld(x, y);
            }
    }

    // Create the static Jolt heightfield body from the generated grid.
    [[nodiscard]] RigidBodyIdentity Create(RigidBodySolver& solver) noexcept
    {
        if (heights_.empty()) Generate();
        HeightfieldDescription D;
        D.Name        = "Tractrix.ProvingGround";
        D.Samples     = heights_.data();
        D.SampleCount = SampleCount;
        D.Origin      = Origin;
        D.SpacingX    = Spacing;
        D.SpacingY    = Spacing;
        D.HeightScale = HeightScale;
        D.Friction    = Friction;
        Body = solver.CreateHeightfieldBody(D);
        return Body;
    }

    // Analytic surface point + normal at a world point. Matches XPBDSoftTyre::GroundQuery's signature, which
    //    is now a SURFACE query rather than a height (see XPBDSoftTyre.h for why). A heightfield is the
    //    trivial case of it: the nearest point is straight down the column, so only the return shape changed.
    [[nodiscard]] bool Sample(const Frontier::Vehicle::Vec3& p, Frontier::Vehicle::Vec3& outPoint,
                              Frontier::Vehicle::Vec3& outNormal) const noexcept
    {
        // Sample the analytic field directly (identical to the grid the Jolt body was built from between samples,
        //    up to the bilinear/continuous difference). This is what the tyre nodes contact.
        const float h  = HeightForWorld(p.x, p.y);
        const float e  = 0.35f;                                   // finite-difference step for the surface normal
        const float hx = (HeightForWorld(p.x + e, p.y) - HeightForWorld(p.x - e, p.y)) / (2.0f * e);
        const float hy = (HeightForWorld(p.x, p.y + e) - HeightForWorld(p.x, p.y - e)) / (2.0f * e);
        outPoint  = Frontier::Vehicle::Vec3{p.x, p.y, Origin.z + h * HeightScale};
        outNormal = Frontier::Vehicle::Vec3{-hx, -hy, 1.0f}.Normalized();
        return true;
    }

    // A flat spot to spawn the car (world origin sits on the pad).
    [[nodiscard]] Vector3 SpawnPosition(float chassisRideHeight = 0.90f) const noexcept
    {
        return Vector3{0.0f, 0.0f, Origin.z + chassisRideHeight};
    }

    [[nodiscard]] const std::vector<float>& Heights() const noexcept { return heights_; }

private:
    // The continuous terrain function: flat inside PadRadius, smoothly ramping to rolling hills outside.
    [[nodiscard]] float HeightForWorld(float x, float y) const noexcept
    {
        const float r     = std::sqrt(x * x + y * y);
        const float blend = SmoothStep(PadRadius, PadRadius + 12.0f, r);   // 0 on the pad → 1 in the hills
        const float hills = HillAmp * (std::sin(HillFreqX * x) * std::cos(HillFreqY * y)
                                     + 0.4f * std::sin(0.021f * x + 0.017f * y));
        return blend * hills;
    }
    [[nodiscard]] static float SmoothStep(float a, float b, float v) noexcept
    {
        if (b <= a) return v >= b ? 1.0f : 0.0f;
        float t = (v - a) / (b - a);
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        return t * t * (3.0f - 2.0f * t);
    }

    std::vector<float> heights_;
};

} // namespace Frontier::Tractrix
