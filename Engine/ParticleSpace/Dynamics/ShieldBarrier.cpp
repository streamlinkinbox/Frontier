/*============================================================================================================================================
                                                           SHIELDBARRIER.CPP
============================================================================================================================================*/
// 🧩 Volumetric energy shield shells, tessellation motif bitmasking & chrome ball impact ripple waves

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/ShieldBarrier.h"

#include <algorithm>
#include <cmath>

namespace Frontier::ParticleSpace
{

float EvaluateShieldMotifMask(
    Mathematics::Axis3D LocalUnitNormal,
    uint32_t            MotifBitmask,
    float               TileScale) noexcept
{
    const float Azimuth = std::atan2(LocalUnitNormal.Y, LocalUnitNormal.X) * TileScale;
    const float Elev    = std::asin(std::clamp(LocalUnitNormal.Z, -1.0f, 1.0f)) * TileScale;

    if ((MotifBitmask & 1u) != 0u) // Honeycomb Hex
    {
        const float H1 = std::abs(std::sin(Azimuth * 1.5f));
        const float H2 = std::abs(std::sin(Azimuth * 0.75f + Elev * 1.3f));
        return (std::min(H1, H2) < 0.18f) ? 1.0f : 0.28f;
    }
    if ((MotifBitmask & 8u) != 0u) // Squares / Grid
    {
        const float G = std::min(std::abs(std::sin(Azimuth * 2.0f)), std::abs(std::sin(Elev * 2.0f)));
        return (G < 0.16f) ? 1.0f : 0.2f;
    }
    if ((MotifBitmask & 4u) != 0u) // Triangles
    {
        const float T = std::min({ std::abs(std::sin(Azimuth + Elev)), std::abs(std::sin(Azimuth - Elev)), std::abs(std::sin(Elev * 2.0f)) });
        return (T < 0.15f) ? 1.0f : 0.25f;
    }
    return 0.45f + 0.55f * std::abs(std::sin(Azimuth * 1.4f + Elev * 2.2f));
}

void StepShieldBarrier(
    std::span<ParticleSlot>            Slots,
    const ShieldBarrierSpec&           Spec,
    const InteractiveColliderSettings& Collider,
    Mathematics::Axis3D                OriginMeters,
    float                              ElapsedSeconds) noexcept
{
    if (!Spec.Active || Slots.empty())
    {
        return;
    }

    const uint32_t Total = static_cast<uint32_t>(Slots.size());
    constexpr float GoldenAngle = 2.39996323f;
    constexpr float RadiusMeters = 2.0f;

    for (uint32_t I = 0u; I < Total; ++I)
    {
        const float T     = (static_cast<float>(I) + 0.5f) / static_cast<float>(Total);
        const float Theta = static_cast<float>(I) * GoldenAngle + ElapsedSeconds * Spec.SpinRate;
        const float Phi   = std::acos(1.0f - 2.0f * T);
        const float SinP  = std::sin(Phi);

        const float Nx = SinP * std::cos(Theta);
        const float Ny = SinP * std::sin(Theta);
        const float Nz = (Spec.Form == ShieldFormCategory::SphereDome) ? std::abs(std::cos(Phi)) : std::cos(Phi);

        if (Spec.Form == ShieldFormCategory::SquarePane || Spec.Form == ShieldFormCategory::HexagonWall)
        {
            const float U = (static_cast<float>(I % 32u) / 31.0f) * 2.0f - 1.0f;
            const float V = (static_cast<float>(I / 32u) / std::max(1.0f, static_cast<float>(Total / 32u))) * 2.0f - 1.0f;
            Slots[I].PositionAge[0]      = OriginMeters.X + U * RadiusMeters;
            Slots[I].PositionAge[1]      = OriginMeters.Y;
            Slots[I].PositionAge[2]      = OriginMeters.Z + 1.5f + V * (RadiusMeters * 0.65f);
            Slots[I].PositionAge[3]      = 0.2f;
            Slots[I].VelocityLifetime[3] = 99.0f;
            Slots[I].Attributes[0]       = 0.08f;
            Slots[I].PhaseTiming[0]      = EvaluateShieldMotifMask({ U, 0.0f, V }, Spec.MotifBitmask, 6.0f);
            continue;
        }

        const float Px = OriginMeters.X + Nx * RadiusMeters;
        const float Py = OriginMeters.Y + Ny * RadiusMeters;
        const float Pz = OriginMeters.Z + 1.5f + Nz * RadiusMeters;

        // Geodesic impact ripple from interactive chrome ball
        float ImpactWave = 0.0f;
        if (Collider.BallRadiusMeters > 0.01f)
        {
            const float Dx = Px - Collider.BallPositionMeters.X;
            const float Dy = Py - Collider.BallPositionMeters.Y;
            const float Dz = Pz - Collider.BallPositionMeters.Z;
            const float Dist = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
            if (Dist < Collider.BallRadiusMeters + 1.4f)
            {
                ImpactWave = std::sin(Dist * 10.0f - ElapsedSeconds * 14.0f) * std::exp(-Dist * 1.1f);
            }
        }

        const float Motif = EvaluateShieldMotifMask({ Nx, Ny, Nz }, Spec.MotifBitmask, 5.0f);
        ParticleSlot& S = Slots[I];
        S.PositionAge[0]      = Px + Nx * (ImpactWave * 0.14f);
        S.PositionAge[1]      = Py + Ny * (ImpactWave * 0.14f);
        S.PositionAge[2]      = Pz + Nz * (ImpactWave * 0.14f);
        S.PositionAge[3]      = 0.2f;
        S.VelocityLifetime[3] = 99.0f;
        S.Attributes[0]       = 0.08f * (0.45f + 0.75f * Motif + std::abs(ImpactWave) * 0.6f);
        S.PhaseTiming[0]      = std::clamp(Motif + std::abs(ImpactWave), 0.0f, 1.5f);
    }
}

} // namespace Frontier::ParticleSpace
