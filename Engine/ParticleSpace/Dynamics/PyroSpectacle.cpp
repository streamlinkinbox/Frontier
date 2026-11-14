/*============================================================================================================================================
                                                           PYROSPECTACLE.CPP
============================================================================================================================================*/
// 🧩 Firework star air drag/gravity/glitter, IR flare V-salvo dispenser & coherent plasma/arc beam filaments

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/PyroSpectacle.h"

#include "Engine/ColourSpace/ColourRampSampler.h"

#include <algorithm>
#include <cmath>

namespace Frontier::ParticleSpace
{

void StepPyroFireworkStar(
    ParticleSlot&            Slot,
    const PyroSpectacleSpec& Spec,
    float                    DeltaSeconds,
    float                    ElapsedSeconds) noexcept
{
    if (Spec.FireworkMode == 0u || DeltaSeconds <= 0.0f || Slot.PositionAge[3] >= Slot.VelocityLifetime[3])
    {
        return;
    }

    // Air drag + gentle firework star gravity (-Z Up)
    const float AirDrag = std::exp(-1.95f * DeltaSeconds);
    Slot.VelocityLifetime[0] *= AirDrag;
    Slot.VelocityLifetime[1] *= AirDrag;
    Slot.VelocityLifetime[2]  = Slot.VelocityLifetime[2] * AirDrag - 3.8f * DeltaSeconds;

    Slot.PositionAge[0] += Slot.VelocityLifetime[0] * DeltaSeconds;
    Slot.PositionAge[1] += Slot.VelocityLifetime[1] * DeltaSeconds;
    Slot.PositionAge[2] += Slot.VelocityLifetime[2] * DeltaSeconds;
    Slot.PositionAge[3] += DeltaSeconds;

    // Crossette / crackle secondary glitter strobe in PhaseTiming[0]
    const float Glitter = ColourSpace::HashDeterministicFloat(Slot.Attributes[3], static_cast<uint32_t>(ElapsedSeconds * 24.0f));
    Slot.PhaseTiming[0] = (Glitter > 0.72f) ? 1.5f : 0.7f;
}

void StepFlareSalvoEjection(
    std::span<ParticleSlot>  Slots,
    const PyroSpectacleSpec& Spec,
    Mathematics::Axis3D      AircraftOriginMeters,
    float                    DeltaSeconds,
    float                    ElapsedSeconds) noexcept
{
    if (Spec.FlareSalvoMode == 0u || Slots.empty() || DeltaSeconds <= 0.0f)
    {
        return;
    }

    constexpr float EjectionSpeed = 6.5f;
    for (std::size_t I = 0u; I < Slots.size(); ++I)
    {
        ParticleSlot& S = Slots[I];
        S.PositionAge[3] += DeltaSeconds;
        if (S.PositionAge[3] >= S.VelocityLifetime[3])
        {
            const float Side = (I % 2u == 0u) ? 1.0f : -1.0f;
            const float Seed = ColourSpace::HashDeterministicFloat(static_cast<float>(I), static_cast<uint32_t>(ElapsedSeconds * 30.0f) + 3u);
            S.PositionAge[0]      = AircraftOriginMeters.X + Side * 0.4f;
            S.PositionAge[1]      = AircraftOriginMeters.Y;
            S.PositionAge[2]      = AircraftOriginMeters.Z + 1.8f;
            S.PositionAge[3]      = 0.0f;
            S.VelocityLifetime[0] = Side * (EjectionSpeed * 0.75f + 2.0f * Seed);
            S.VelocityLifetime[1] = -EjectionSpeed * 0.45f;
            S.VelocityLifetime[2] = EjectionSpeed * 0.35f + 1.5f * Seed;
            S.VelocityLifetime[3] = 2.8f + 1.4f * Seed;
            continue;
        }

        const float Drag = std::exp(-1.15f * DeltaSeconds);
        S.VelocityLifetime[0] *= Drag;
        S.VelocityLifetime[1] *= Drag;
        S.VelocityLifetime[2]  = S.VelocityLifetime[2] * Drag - 5.2f * DeltaSeconds;
        S.PositionAge[0]      += S.VelocityLifetime[0] * DeltaSeconds;
        S.PositionAge[1]      += S.VelocityLifetime[1] * DeltaSeconds;
        S.PositionAge[2]      += S.VelocityLifetime[2] * DeltaSeconds;
    }
}

void StepBeamFilament(
    std::span<ParticleSlot>  Slots,
    const PyroSpectacleSpec& Spec,
    Mathematics::Axis3D      SourceMeters,
    Mathematics::Axis3D      TargetMeters,
    float                    ElapsedSeconds) noexcept
{
    if (Spec.BeamModeActive <= 0.0f || Slots.empty())
    {
        return;
    }

    const uint32_t Count = static_cast<uint32_t>(Slots.size());
    const float    Dx    = TargetMeters.X - SourceMeters.X;
    const float    Dy    = TargetMeters.Y - SourceMeters.Y;
    const float    Dz    = TargetMeters.Z - SourceMeters.Z;

    for (uint32_t I = 0u; I < Count; ++I)
    {
        const float T     = static_cast<float>(I) / static_cast<float>(std::max(1u, Count - 1u));
        const float Pinch = std::sin(T * 3.14159265f);
        const float Jag   = 0.14f * Pinch * (ColourSpace::SampleLatticeNoise3D(T * 8.0f, ElapsedSeconds * 9.0f, 1.5f) - 0.5f);
        const float JagZ  = 0.14f * Pinch * (ColourSpace::SampleLatticeNoise3D(T * 8.0f, ElapsedSeconds * 9.0f, 11.5f) - 0.5f);

        ParticleSlot& S = Slots[I];
        S.PositionAge[0]      = SourceMeters.X + Dx * T + Jag;
        S.PositionAge[1]      = SourceMeters.Y + Dy * T;
        S.PositionAge[2]      = SourceMeters.Z + Dz * T + JagZ;
        S.PositionAge[3]      = 0.25f;
        S.VelocityLifetime[3] = 99.0f;
        S.Attributes[0]       = 0.06f * (0.4f + 0.8f * Pinch);
        S.PhaseTiming[0]      = 1.0f;
    }
}

} // namespace Frontier::ParticleSpace
