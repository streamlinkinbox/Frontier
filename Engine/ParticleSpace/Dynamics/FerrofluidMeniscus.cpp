/*============================================================================================================================================
                                                         FERROFLUIDMENISCUS.CPP
============================================================================================================================================*/
// 🧩 Rosensweig magnetic ferrofluid spike heightfield & 3D levitated crown solver

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/FerrofluidMeniscus.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>

namespace Frontier::ParticleSpace
{

Mathematics::Axis3D EvaluateFerroMagnetPosition(
    const FerrofluidMeniscusSpec& Spec,
    Mathematics::Axis3D           ManualEffectorMeters,
    Mathematics::Axis3D           OriginMeters,
    float                         ElapsedSeconds) noexcept
{
    if (Spec.FreeDraggableMagnet)
    {
        return ManualEffectorMeters;
    }
    const float Angle = ElapsedSeconds * Spec.MagnetOrbitSpeed;
    const float OrbitR = (Spec.FerroMode == 2u) ? 1.45f : 1.15f;
    return {
        OriginMeters.X + std::cos(Angle) * OrbitR,
        OriginMeters.Y + std::sin(Angle) * OrbitR,
        OriginMeters.Z + ((Spec.FerroMode == 2u) ? 1.65f : 1.1f) + 0.25f * std::sin(Angle * 1.7f),
    };
}

void StepFerrofluidSpikes(
    std::span<ParticleSlot>       Slots,
    const FerrofluidMeniscusSpec& Spec,
    Mathematics::Axis3D           OriginMeters,
    Mathematics::Axis3D           MagnetMeters,
    float                         ElapsedSeconds) noexcept
{
    if (Spec.FerroMode == 0u || Slots.empty())
    {
        return;
    }

    const uint32_t ActiveSpikes = std::min<uint32_t>(static_cast<uint32_t>(Slots.size()), std::max(16u, Spec.SpikeCount));
    constexpr float GoldenAngle = 2.39996323f;

    for (uint32_t I = 0u; I < ActiveSpikes; ++I)
    {
        const float T     = (static_cast<float>(I) + 0.5f) / static_cast<float>(ActiveSpikes);
        const float Theta = static_cast<float>(I) * GoldenAngle;

        float Px = OriginMeters.X;
        float Py = OriginMeters.Y;
        float Pz = OriginMeters.Z;
        float Nx = 0.0f, Ny = 0.0f, Nz = 1.0f;

        if (Spec.FerroMode == 2u)
        {
            // 3D spherical levitated ferrofluid orb crown (Fibonacci sphere)
            const float Phi  = std::acos(1.0f - 2.0f * T);
            const float SinP = std::sin(Phi);
            Nx = SinP * std::cos(Theta);
            Ny = SinP * std::sin(Theta);
            Nz = std::cos(Phi);
            const float OrbR = 1.05f;
            Px = OriginMeters.X + Nx * OrbR;
            Py = OriginMeters.Y + Ny * OrbR;
            Pz = OriginMeters.Z + 1.35f + Nz * OrbR;
        }
        else
        {
            // Petri-dish flat ferrofluid pool (Vogel spiral)
            const float R = 1.65f * std::sqrt(T);
            Px = OriginMeters.X + std::cos(Theta) * R;
            Py = OriginMeters.Y + std::sin(Theta) * R;
            Pz = OriginMeters.Z + 0.08f;
        }

        const float Dx     = MagnetMeters.X - Px;
        const float Dy     = MagnetMeters.Y - Py;
        const float Dz     = MagnetMeters.Z - Pz;
        const float Dist   = std::sqrt(std::max(0.05f, Dx * Dx + Dy * Dy + Dz * Dz));
        const float MagPull = std::exp(-Dist * 0.85f);
        const float Ripple  = 0.5f + 0.5f * std::sin(Dist * 14.0f - ElapsedSeconds * 6.5f + static_cast<float>(I) * 0.13f);
        const float SpikeH  = (0.04f + 0.42f * MagPull * (0.45f + 0.55f * Ripple));

        // Spike stretches toward magnet
        const float Ux = (Dx / Dist) * 0.55f + Nx * 0.45f;
        const float Uy = (Dy / Dist) * 0.55f + Ny * 0.45f;
        const float Uz = (Dz / Dist) * 0.55f + Nz * 0.45f;

        ParticleSlot& S = Slots[I];
        S.PositionAge[0]      = Px + Ux * (SpikeH * 0.35f);
        S.PositionAge[1]      = Py + Uy * (SpikeH * 0.35f);
        S.PositionAge[2]      = Pz + Uz * (SpikeH * 0.35f);
        S.PositionAge[3]      = 0.1f;
        S.VelocityLifetime[0] = Ux * SpikeH * 3.0f;
        S.VelocityLifetime[1] = Uy * SpikeH * 3.0f;
        S.VelocityLifetime[2] = Uz * SpikeH * 3.0f;
        S.VelocityLifetime[3] = 99.0f;
        S.Attributes[0]       = 0.045f + SpikeH * 0.18f;
        S.Attributes[2]       = MagPull;
    }
}

} // namespace Frontier::ParticleSpace
