/*============================================================================================================================================
                                                         INTERACTIVECOLLIDER.CPP
============================================================================================================================================*/
// 🧩 Interactive chrome collider ball, floor plane bounce & DistanceFieldSpace SignedDistanceVolume collision response

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/InteractiveCollider.h"

#include <algorithm>
#include <cmath>

namespace Frontier::ParticleSpace
{

void ResolveParticleCollisions(
    ParticleSlot&                                   Slot,
    const InteractiveColliderSettings&              Collider,
    float                                           BounceRestitution,
    float                                           ElapsedSeconds,
    const DistanceFieldSpace::SignedDistanceVolume* OptionalSdf) noexcept
{
    if (Slot.PositionAge[3] >= Slot.VelocityLifetime[3])
    {
        return;
    }

    // 1. Floor plane collision (+Z Up)
    const float ParticleHalfSize = 0.5f * std::max(0.0f, Slot.Attributes[0]);
    const float FloorContactZ    = Collider.FloorElevationMeters + ParticleHalfSize;
    if (Slot.PositionAge[2] < FloorContactZ)
    {
        Slot.PositionAge[2] = FloorContactZ;
        if (Slot.VelocityLifetime[2] < 0.0f)
        {
            if (BounceRestitution > 0.001f)
            {
                Slot.VelocityLifetime[2] = -Slot.VelocityLifetime[2] * BounceRestitution;
                Slot.VelocityLifetime[0] *= 0.88f;
                Slot.VelocityLifetime[1] *= 0.88f;
            }
            else
            {
                Slot.VelocityLifetime[2] = 0.0f;
                Slot.VelocityLifetime[0] *= 0.72f;
                Slot.VelocityLifetime[1] *= 0.72f;
            }
            if (Slot.PhaseTiming[1] <= 0.0f)
            {
                Slot.PhaseTiming[1] = ElapsedSeconds + 1.0f;
            }
        }
    }

    // 2. Interactive chrome ball collider
    if (Collider.BallRadiusMeters > 0.01f)
    {
        const float Dx     = Slot.PositionAge[0] - Collider.BallPositionMeters.X;
        const float Dy     = Slot.PositionAge[1] - Collider.BallPositionMeters.Y;
        const float Dz     = Slot.PositionAge[2] - Collider.BallPositionMeters.Z;
        const float MinSep = Collider.BallRadiusMeters + ParticleHalfSize;
        const float DistSq = Dx * Dx + Dy * Dy + Dz * Dz;
        if (DistSq < MinSep * MinSep)
        {
            const float Dist = std::sqrt(std::max(1.0e-6f, DistSq));
            const float Nx   = Dx / Dist;
            const float Ny   = Dy / Dist;
            const float Nz   = Dz / Dist;
            Slot.PositionAge[0] = Collider.BallPositionMeters.X + Nx * MinSep;
            Slot.PositionAge[1] = Collider.BallPositionMeters.Y + Ny * MinSep;
            Slot.PositionAge[2] = Collider.BallPositionMeters.Z + Nz * MinSep;

            const float RelVx = Slot.VelocityLifetime[0] - Collider.BallVelocityMps.X;
            const float RelVy = Slot.VelocityLifetime[1] - Collider.BallVelocityMps.Y;
            const float RelVz = Slot.VelocityLifetime[2] - Collider.BallVelocityMps.Z;
            const float VdotN = RelVx * Nx + RelVy * Ny + RelVz * Nz;
            if (VdotN < 0.0f)
            {
                const float Imp = (1.0f + std::max(0.25f, BounceRestitution)) * (-VdotN) * Collider.ImpactImpulseScale;
                Slot.VelocityLifetime[0] += Nx * Imp + Collider.BallVelocityMps.X * 0.45f;
                Slot.VelocityLifetime[1] += Ny * Imp + Collider.BallVelocityMps.Y * 0.45f;
                Slot.VelocityLifetime[2] += Nz * Imp + Collider.BallVelocityMps.Z * 0.45f;
            }
        }
    }

    // 3. Optional DistanceFieldSpace SignedDistanceVolume collision
    if (Collider.SignedDistanceActive && OptionalSdf != nullptr)
    {
        const Mathematics::Axis3D QueryPt{ Slot.PositionAge[0], Slot.PositionAge[1], Slot.PositionAge[2] };
        const float Sd = OptionalSdf->SampleSignedDistance(QueryPt);
        if (Sd < ParticleHalfSize)
        {
            const Mathematics::Axis3D Grad = OptionalSdf->EvaluateSurfaceGradient(QueryPt);
            const float Push = ParticleHalfSize - Sd;
            Slot.PositionAge[0] += Grad.X * Push;
            Slot.PositionAge[1] += Grad.Y * Push;
            Slot.PositionAge[2] += Grad.Z * Push;
            const float Vn = Slot.VelocityLifetime[0] * Grad.X + Slot.VelocityLifetime[1] * Grad.Y + Slot.VelocityLifetime[2] * Grad.Z;
            if (Vn < 0.0f)
            {
                const float Bounce = 1.0f + std::clamp(BounceRestitution, 0.0f, 0.95f);
                Slot.VelocityLifetime[0] -= Bounce * Vn * Grad.X;
                Slot.VelocityLifetime[1] -= Bounce * Vn * Grad.Y;
                Slot.VelocityLifetime[2] -= Bounce * Vn * Grad.Z;
            }
        }
    }
}

} // namespace Frontier::ParticleSpace
