/*============================================================================================================================================
                                                           BOTANICALSWAY.CPP
============================================================================================================================================*/
// 🧩 Botanical wind-wave stadium sway, canopy/kelp/anemone flexing & dandelion/blossom detachment dynamics

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/BotanicalSway.h"

#include <algorithm>
#include <cmath>

namespace Frontier::ParticleSpace
{

void StepBotanicalFoliageStrand(
    std::span<StrandVertex>            Vertices,
    const BotanicalSwaySpec&           Botanical,
    const StrandConstraintSpec&        StrandSpec,
    const StructuralStrandAnchor&      Anchor,
    const InteractiveColliderSettings& Collider,
    Mathematics::Axis3D                AmbientWindMps,
    float                              ElapsedSeconds) noexcept
{
    const uint32_t Count = std::min<uint32_t>(
        static_cast<uint32_t>(Vertices.size()),
        std::max(2u, Anchor.VertexCount));
    if (Count < 2u)
    {
        return;
    }

    const float WindSpeed = std::sqrt(AmbientWindMps.X * AmbientWindMps.X + AmbientWindMps.Y * AmbientWindMps.Y);
    const float WindDirX  = (WindSpeed > 1.0e-3f) ? (AmbientWindMps.X / WindSpeed) : 1.0f;
    const float WindDirY  = (WindSpeed > 1.0e-3f) ? (AmbientWindMps.Y / WindSpeed) : 0.2f;

    for (uint32_t I = 0u; I < Count; ++I)
    {
        const float T = static_cast<float>(I) / static_cast<float>(Count - 1u);
        const float Dist = static_cast<float>(I) * Anchor.RestSegmentLength;

        float Px = Anchor.RootPosition.X + Anchor.TipDirection.X * Dist;
        float Py = Anchor.RootPosition.Y + Anchor.TipDirection.Y * Dist;
        float Pz = Anchor.RootPosition.Z + Anchor.TipDirection.Z * Dist;

        // Stadium wind gust wave + species arch curvature
        const float GustPhase = ElapsedSeconds * StrandSpec.WaveSpeedHz * 2.2f - (Px * 0.65f + Py * 0.45f) + static_cast<float>(Anchor.StrandIndex) * 0.17f;
        const float Bend      = T * T * (0.25f + 0.14f * WindSpeed + 0.18f * std::sin(GustPhase) * (0.5f + Botanical.SwirlFlutter));
        Px += WindDirX * Bend;
        Py += WindDirY * Bend;
        Pz -= T * T * StrandSpec.SagMeters * 0.35f;

        // Interactive ball pushes flora aside cleanly
        if (I > 0u && Collider.BallRadiusMeters > 0.01f)
        {
            const float Dx = Px - Collider.BallPositionMeters.X;
            const float Dy = Py - Collider.BallPositionMeters.Y;
            const float Dz = Pz - Collider.BallPositionMeters.Z;
            const float MinR = Collider.BallRadiusMeters + 0.08f;
            const float D2 = Dx * Dx + Dy * Dy + Dz * Dz;
            if (D2 < MinR * MinR)
            {
                const float D = std::sqrt(std::max(1.0e-6f, D2));
                Px = Collider.BallPositionMeters.X + (Dx / D) * MinR;
                Py = Collider.BallPositionMeters.Y + (Dy / D) * MinR;
                Pz = Collider.BallPositionMeters.Z + (Dz / D) * MinR;
            }
        }

        Vertices[I].PositionRadius[0] = Px;
        Vertices[I].PositionRadius[1] = Py;
        Vertices[I].PositionRadius[2] = Pz;
        Vertices[I].PositionRadius[3] = (StrandSpec.Mode == StrandDynamicsMode::SeaAnemone)
                                            ? (0.045f * StrandSpec.AnemoneThickness * (1.0f - 0.55f * T))
                                            : (0.025f * (1.0f - 0.75f * T));
    }
}

void EvaluateBotanicalSeedDetachment(
    ParticleSlot&            SeedSlot,
    const BotanicalSwaySpec& Botanical,
    Mathematics::Axis3D      AmbientWindMps,
    float                    DeltaSeconds,
    float                    ElapsedSeconds) noexcept
{
    if (Botanical.SeedHeadMode == 0u || DeltaSeconds <= 0.0f)
    {
        return;
    }

    const float WindSpeed = std::sqrt(AmbientWindMps.X * AmbientWindMps.X + AmbientWindMps.Y * AmbientWindMps.Y);
    const float Threshold = Botanical.DetachmentWindMin * (0.65f + 0.55f * SeedSlot.Attributes[3]);
    if (SeedSlot.PhaseTiming[0] <= 0.0f && WindSpeed >= Threshold)
    {
        // Detached: begin airborne seed flight
        SeedSlot.PhaseTiming[0] = 1.0f;
        SeedSlot.VelocityLifetime[0] = AmbientWindMps.X * 0.85f;
        SeedSlot.VelocityLifetime[1] = AmbientWindMps.Y * 0.85f;
        SeedSlot.VelocityLifetime[2] = 0.65f + 0.5f * SeedSlot.Attributes[3];
    }

    if (SeedSlot.PhaseTiming[0] > 0.0f)
    {
        const float Flutter = std::sin(ElapsedSeconds * 5.2f + SeedSlot.Attributes[3] * 19.0f) * Botanical.SwirlFlutter;
        SeedSlot.VelocityLifetime[0] += (AmbientWindMps.X - SeedSlot.VelocityLifetime[0]) * 1.4f * DeltaSeconds;
        SeedSlot.VelocityLifetime[1] += (AmbientWindMps.Y - SeedSlot.VelocityLifetime[1]) * 1.4f * DeltaSeconds + Flutter * DeltaSeconds;
        SeedSlot.VelocityLifetime[2] += (0.35f - 0.55f * SeedSlot.VelocityLifetime[2]) * DeltaSeconds;

        SeedSlot.PositionAge[0] += SeedSlot.VelocityLifetime[0] * DeltaSeconds;
        SeedSlot.PositionAge[1] += SeedSlot.VelocityLifetime[1] * DeltaSeconds;
        SeedSlot.PositionAge[2] += SeedSlot.VelocityLifetime[2] * DeltaSeconds;
        SeedSlot.PositionAge[3] += DeltaSeconds;
    }
}

} // namespace Frontier::ParticleSpace
