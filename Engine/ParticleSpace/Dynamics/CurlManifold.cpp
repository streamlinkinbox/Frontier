/*============================================================================================================================================
                                                            CURLMANIFOLD.CPP
============================================================================================================================================*/
// 🧩 Divergence-free 3D curl noise evaluation & spherical/planar/ribbon/pour curl manifold advection

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/CurlManifold.h"

#include "Engine/ColourSpace/ColourRampSampler.h"

#include <algorithm>
#include <cmath>

namespace Frontier::ParticleSpace
{

namespace
{

[[nodiscard]] float SamplePotentialField(float X, float Y, float Z, float Offset) noexcept
{
    return ColourSpace::SampleLatticeNoise3D(X + Offset, Y - Offset * 0.73f, Z + Offset * 1.37f);
}

} // namespace

Mathematics::Axis3D EvaluateCurlNoise3D(
    Mathematics::Axis3D PositionMeters,
    float               SpatialFrequency,
    float               ElapsedSeconds) noexcept
{
    const float F  = std::max(0.05f, SpatialFrequency);
    const float Px = PositionMeters.X * F;
    const float Py = PositionMeters.Y * F;
    const float Pz = PositionMeters.Z * F + ElapsedSeconds;
    constexpr float Eps = 0.04f;
    constexpr float Inv2Eps = 1.0f / (2.0f * Eps);

    // Vector potential psi = (psi_x, psi_y, psi_z) -> curl(psi) is strictly divergence-free
    const float dPsiZ_dY = (SamplePotentialField(Px, Py + Eps, Pz, 17.3f) - SamplePotentialField(Px, Py - Eps, Pz, 17.3f)) * Inv2Eps;
    const float dPsiY_dZ = (SamplePotentialField(Px, Py, Pz + Eps, 31.7f) - SamplePotentialField(Px, Py, Pz - Eps, 31.7f)) * Inv2Eps;

    const float dPsiX_dZ = (SamplePotentialField(Px, Py, Pz + Eps, 5.1f) - SamplePotentialField(Px, Py, Pz - Eps, 5.1f)) * Inv2Eps;
    const float dPsiZ_dX = (SamplePotentialField(Px + Eps, Py, Pz, 17.3f) - SamplePotentialField(Px - Eps, Py, Pz, 17.3f)) * Inv2Eps;

    const float dPsiY_dX = (SamplePotentialField(Px + Eps, Py, Pz, 31.7f) - SamplePotentialField(Px - Eps, Py, Pz, 31.7f)) * Inv2Eps;
    const float dPsiX_dY = (SamplePotentialField(Px, Py + Eps, Pz, 5.1f) - SamplePotentialField(Px, Py - Eps, Pz, 5.1f)) * Inv2Eps;

    return {
        dPsiZ_dY - dPsiY_dZ,
        dPsiX_dZ - dPsiZ_dX,
        dPsiY_dX - dPsiX_dY,
    };
}

void StepCurlManifoldParticle(
    ParticleSlot&           Slot,
    const CurlManifoldSpec& Spec,
    Mathematics::Axis3D     OriginMeters,
    float                   DeltaSeconds,
    float                   ElapsedSeconds) noexcept
{
    if (Spec.Mode == CurlManifoldMode::Off || DeltaSeconds <= 0.0f)
    {
        return;
    }

    const Mathematics::Axis3D CurlVec = EvaluateCurlNoise3D(
        { Slot.PositionAge[0], Slot.PositionAge[1], Slot.PositionAge[2] },
        Spec.SpatialFreq,
        ElapsedSeconds * Spec.EvolutionRate);

    float Vx = CurlVec.X * Spec.DriveSpeed;
    float Vy = CurlVec.Y * Spec.DriveSpeed;
    float Vz = CurlVec.Z * Spec.DriveSpeed;

    if (Spec.Mode == CurlManifoldMode::SphericalPlasma || Spec.Mode == CurlManifoldMode::SurfacePourRibbons)
    {
        const float Dx   = Slot.PositionAge[0] - OriginMeters.X;
        const float Dy   = Slot.PositionAge[1] - OriginMeters.Y;
        const float Dz   = Slot.PositionAge[2] - OriginMeters.Z;
        const float Dist = std::sqrt(std::max(1.0e-5f, Dx * Dx + Dy * Dy + Dz * Dz));
        const float Nx   = Dx / Dist;
        const float Ny   = Dy / Dist;
        const float Nz   = Dz / Dist;

        // Project velocity onto tangent plane of spherical shell and gently restore radial shell distance
        const float VdotN = Vx * Nx + Vy * Ny + Vz * Nz;
        Vx -= VdotN * Nx;
        Vy -= VdotN * Ny;
        Vz -= VdotN * Nz;

        const float TargetR = std::max(0.25f, Spec.ShellRadius);
        const float RadialRestore = (TargetR - Dist) * 3.5f;
        Vx += Nx * RadialRestore;
        Vy += Ny * RadialRestore;
        Vz += Nz * RadialRestore;
    }

    Slot.VelocityLifetime[0] = Vx;
    Slot.VelocityLifetime[1] = Vy;
    Slot.VelocityLifetime[2] = Vz;

    Slot.PositionAge[0] += Vx * DeltaSeconds;
    Slot.PositionAge[1] += Vy * DeltaSeconds;
    Slot.PositionAge[2] += Vz * DeltaSeconds;
    Slot.PositionAge[3] += DeltaSeconds;
}

} // namespace Frontier::ParticleSpace
