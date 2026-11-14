/*============================================================================================================================================
                                                         MOLECULARKINETICS.CPP
============================================================================================================================================*/
// 🧩 Lennard-Jones 12-6 potential, Langevin thermal bath & reversible A + B <-> C species exchange

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/MolecularKinetics.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Frontier::ParticleSpace
{

void StepMolecularKinetics(
    std::span<ParticleSlot>      Slots,
    const MolecularKineticsSpec& Spec,
    Mathematics::Axis3D          VesselOriginMeters,
    bool                         EnableReactions,
    float                        DeltaSeconds) noexcept
{
    if (!Spec.Active || Slots.empty() || DeltaSeconds <= 0.0f)
    {
        return;
    }

    const uint32_t Total      = static_cast<uint32_t>(Slots.size());
    const uint32_t SampleStep = std::max(1u, Total / 48u);
    const float    Sigma      = std::max(0.06f, Spec.Sigma);
    const float    CutoffSq   = (Sigma * 3.0f) * (Sigma * 3.0f);

    for (uint32_t I = 0u; I < Total; ++I)
    {
        ParticleSlot& Self = Slots[I];
        if (Self.PositionAge[3] >= Self.VelocityLifetime[3])
        {
            continue;
        }

        float ForceX = 0.0f, ForceY = 0.0f, ForceZ = 0.0f;
        for (uint32_t J = I % SampleStep; J < Total; J += SampleStep)
        {
            if (J == I)
            {
                continue;
            }
            ParticleSlot& Other = Slots[J];
            const float Dx = Self.PositionAge[0] - Other.PositionAge[0];
            const float Dy = Self.PositionAge[1] - Other.PositionAge[1];
            const float Dz = Self.PositionAge[2] - Other.PositionAge[2];
            const float R2 = std::max(0.02f * Sigma * Sigma, Dx * Dx + Dy * Dy + Dz * Dz);
            if (R2 > CutoffSq)
            {
                continue;
            }

            // Softened Lennard-Jones 12-6 radial force
            const float InvR2  = (Sigma * Sigma) / R2;
            const float InvR6  = InvR2 * InvR2 * InvR2;
            const float Scalar = std::clamp(24.0f * Spec.Epsilon * InvR2 * (2.0f * InvR6 * InvR6 - InvR6), -25.0f, 25.0f);
            ForceX += Dx * Scalar;
            ForceY += Dy * Scalar;
            ForceZ += Dz * Scalar;

            // Reversible A (0) + B (1) <-> C (2) reaction kinetics in PhaseTiming[2]
            if (EnableReactions && R2 < Spec.ReactionRadius * Spec.ReactionRadius)
            {
                const int SpeciesA = static_cast<int>(Self.PhaseTiming[2] + 0.5f);
                const int SpeciesB = static_cast<int>(Other.PhaseTiming[2] + 0.5f);
                if ((SpeciesA == 0 && SpeciesB == 1) || (SpeciesA == 1 && SpeciesB == 0))
                {
                    if (Self.Attributes[3] < Spec.ReactionRate * DeltaSeconds)
                    {
                        Self.PhaseTiming[2] = 2.0f; // Bound product C
                    }
                }
            }
        }

        // Langevin thermal target kinetic energy relaxation
        const float TargetSpeed = std::sqrt(std::max(0.05f, Spec.Temperature)) * 2.2f;
        const float CurSpeed    = std::sqrt(
            Self.VelocityLifetime[0] * Self.VelocityLifetime[0] +
            Self.VelocityLifetime[1] * Self.VelocityLifetime[1] +
            Self.VelocityLifetime[2] * Self.VelocityLifetime[2]);
        const float ThermoScale = (CurSpeed > 1.0e-4f)
                                      ? (1.0f + (TargetSpeed / CurSpeed - 1.0f) * std::min(0.45f, DeltaSeconds * Spec.LangevinDamping))
                                      : 1.0f;

        Self.VelocityLifetime[0] = (Self.VelocityLifetime[0] + ForceX * DeltaSeconds) * ThermoScale;
        Self.VelocityLifetime[1] = (Self.VelocityLifetime[1] + ForceY * DeltaSeconds) * ThermoScale;
        Self.VelocityLifetime[2] = (Self.VelocityLifetime[2] + ForceZ * DeltaSeconds) * ThermoScale;

        Self.PositionAge[0] += Self.VelocityLifetime[0] * DeltaSeconds;
        Self.PositionAge[1] += Self.VelocityLifetime[1] * DeltaSeconds;
        Self.PositionAge[2] += Self.VelocityLifetime[2] * DeltaSeconds;
        Self.PositionAge[3] += DeltaSeconds;

        // Soft containment sphere around reaction vessel
        const float Vx = Self.PositionAge[0] - VesselOriginMeters.X;
        const float Vy = Self.PositionAge[1] - VesselOriginMeters.Y;
        const float Vz = Self.PositionAge[2] - (VesselOriginMeters.Z + 1.5f);
        const float VR = std::sqrt(Vx * Vx + Vy * Vy + Vz * Vz);
        constexpr float VesselRadius = 2.6f;
        if (VR > VesselRadius)
        {
            const float Nx = Vx / VR;
            const float Ny = Vy / VR;
            const float Nz = Vz / VR;
            Self.PositionAge[0] = VesselOriginMeters.X + Nx * VesselRadius;
            Self.PositionAge[1] = VesselOriginMeters.Y + Ny * VesselRadius;
            Self.PositionAge[2] = VesselOriginMeters.Z + 1.5f + Nz * VesselRadius;
            const float VdotN = Self.VelocityLifetime[0] * Nx + Self.VelocityLifetime[1] * Ny + Self.VelocityLifetime[2] * Nz;
            if (VdotN > 0.0f)
            {
                Self.VelocityLifetime[0] -= 1.8f * VdotN * Nx;
                Self.VelocityLifetime[1] -= 1.8f * VdotN * Ny;
                Self.VelocityLifetime[2] -= 1.8f * VdotN * Nz;
            }
        }
    }
}

} // namespace Frontier::ParticleSpace
