/*============================================================================================================================================
                                                            FLOCKBOIDS.CPP
============================================================================================================================================*/
// 🧩 Boids steering (cohesion, alignment, separation), predator ball evasion & roost tether

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/FlockBoids.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Frontier::ParticleSpace
{

void StepFlockBoids(
    std::span<ParticleSlot>            Slots,
    const FlockBoidsSpec&              Spec,
    const InteractiveColliderSettings& Collider,
    Mathematics::Axis3D                RoostOriginMeters,
    float                              DeltaSeconds,
    float                              ElapsedSeconds) noexcept
{
    if (!Spec.Active || Slots.empty() || DeltaSeconds <= 0.0f)
    {
        return;
    }

    const uint32_t Total       = static_cast<uint32_t>(Slots.size());
    const float    PercSq      = Spec.NeighbourRadius * Spec.NeighbourRadius;
    const float    SepSq       = (Spec.NeighbourRadius * 0.45f) * (Spec.NeighbourRadius * 0.45f);
    const uint32_t SampleStep  = std::max(1u, Total / 48u); // Bounded O(N * 48) neighbourhood sampling for fast deterministic CPU/GPU parity

    for (uint32_t I = 0u; I < Total; ++I)
    {
        ParticleSlot& Self = Slots[I];
        if (Self.PositionAge[3] >= Self.VelocityLifetime[3])
        {
            continue;
        }

        float CohX = 0.0f, CohY = 0.0f, CohZ = 0.0f;
        float AliX = 0.0f, AliY = 0.0f, AliZ = 0.0f;
        float SepX = 0.0f, SepY = 0.0f, SepZ = 0.0f;
        uint32_t Neighbours = 0u;

        for (uint32_t J = I % SampleStep; J < Total; J += SampleStep)
        {
            if (J == I)
            {
                continue;
            }
            const ParticleSlot& Other = Slots[J];
            const float Dx = Other.PositionAge[0] - Self.PositionAge[0];
            const float Dy = Other.PositionAge[1] - Self.PositionAge[1];
            const float Dz = Other.PositionAge[2] - Self.PositionAge[2];
            const float D2 = Dx * Dx + Dy * Dy + Dz * Dz;
            if (D2 > PercSq || D2 < 1.0e-7f)
            {
                continue;
            }

            CohX += Other.PositionAge[0];
            CohY += Other.PositionAge[1];
            CohZ += Other.PositionAge[2];
            AliX += Other.VelocityLifetime[0];
            AliY += Other.VelocityLifetime[1];
            AliZ += Other.VelocityLifetime[2];
            ++Neighbours;

            if (D2 < SepSq)
            {
                const float InvD = 1.0f / std::sqrt(D2);
                SepX -= Dx * InvD;
                SepY -= Dy * InvD;
                SepZ -= Dz * InvD;
            }
        }

        float AccelX = 0.0f, AccelY = 0.0f, AccelZ = 0.0f;
        if (Neighbours > 0u)
        {
            const float InvN = 1.0f / static_cast<float>(Neighbours);
            AccelX += ((CohX * InvN - Self.PositionAge[0]) * Spec.CohesionWeight)
                    + ((AliX * InvN - Self.VelocityLifetime[0]) * Spec.AlignmentWeight)
                    + (SepX * Spec.SeparationWeight);
            AccelY += ((CohY * InvN - Self.PositionAge[1]) * Spec.CohesionWeight)
                    + ((AliY * InvN - Self.VelocityLifetime[1]) * Spec.AlignmentWeight)
                    + (SepY * Spec.SeparationWeight);
            AccelZ += ((CohZ * InvN - Self.PositionAge[2]) * Spec.CohesionWeight)
                    + ((AliZ * InvN - Self.VelocityLifetime[2]) * Spec.AlignmentWeight)
                    + (SepZ * Spec.SeparationWeight);
        }

        // Gentle roost tether keeps murmuration within visible bounds
        AccelX += (RoostOriginMeters.X - Self.PositionAge[0]) * 0.45f;
        AccelY += (RoostOriginMeters.Y - Self.PositionAge[1]) * 0.45f;
        AccelZ += (RoostOriginMeters.Z + 2.0f - Self.PositionAge[2]) * 0.45f;

        // Predator chrome ball evasion
        if (Collider.BallRadiusMeters > 0.01f)
        {
            const float Dx = Self.PositionAge[0] - Collider.BallPositionMeters.X;
            const float Dy = Self.PositionAge[1] - Collider.BallPositionMeters.Y;
            const float Dz = Self.PositionAge[2] - Collider.BallPositionMeters.Z;
            const float EvadeR = Collider.BallRadiusMeters + 1.4f;
            const float D2 = Dx * Dx + Dy * Dy + Dz * Dz;
            if (D2 < EvadeR * EvadeR)
            {
                const float D = std::sqrt(std::max(1.0e-5f, D2));
                const float Force = 14.0f * (1.0f - D / EvadeR);
                AccelX += (Dx / D) * Force;
                AccelY += (Dy / D) * Force;
                AccelZ += (Dz / D) * Force;
            }
        }

        Self.VelocityLifetime[0] += AccelX * DeltaSeconds;
        Self.VelocityLifetime[1] += AccelY * DeltaSeconds;
        Self.VelocityLifetime[2] += AccelZ * DeltaSeconds;

        const float Speed = std::sqrt(
            Self.VelocityLifetime[0] * Self.VelocityLifetime[0] +
            Self.VelocityLifetime[1] * Self.VelocityLifetime[1] +
            Self.VelocityLifetime[2] * Self.VelocityLifetime[2]);
        const float MaxSpd = std::max(0.5f, Spec.TargetSpeed);
        const float MinSpd = MaxSpd * 0.35f;
        if (Speed > MaxSpd)
        {
            const float Scale = MaxSpd / Speed;
            Self.VelocityLifetime[0] *= Scale;
            Self.VelocityLifetime[1] *= Scale;
            Self.VelocityLifetime[2] *= Scale;
        }
        else if (Speed < MinSpd && Speed > 1.0e-4f)
        {
            const float Scale = MinSpd / Speed;
            Self.VelocityLifetime[0] *= Scale;
            Self.VelocityLifetime[1] *= Scale;
            Self.VelocityLifetime[2] *= Scale;
        }

        Self.PositionAge[0] += Self.VelocityLifetime[0] * DeltaSeconds;
        Self.PositionAge[1] += Self.VelocityLifetime[1] * DeltaSeconds;
        Self.PositionAge[2] += Self.VelocityLifetime[2] * DeltaSeconds;
        Self.PositionAge[3] += DeltaSeconds;

        // Synchronized firefly bioluminescent pulse encoded in PhaseTiming[0]
        if (Spec.PulseHz > 0.0f)
        {
            const float Phase = ElapsedSeconds * Spec.PulseHz * 6.283185f + Self.Attributes[3] * 1.4f;
            Self.PhaseTiming[0] = (1.0f - Spec.PulseDepth) + Spec.PulseDepth * std::pow(0.5f + 0.5f * std::sin(Phase), 3.0f);
        }
    }
}

} // namespace Frontier::ParticleSpace
