/*============================================================================================================================================
                                                          TRANSITIONDEREZ.CPP
============================================================================================================================================*/
// 🧩 Surface lattice hold, derez wave sweep, reverse assembly, floor shatter & butterfly swarm transition

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/TransitionDerez.h"

#include <algorithm>
#include <cmath>

namespace Frontier::ParticleSpace
{

void StepTransitionDerez(
    std::span<ParticleSlot>    Slots,
    const TransitionDerezSpec& Spec,
    Mathematics::Axis3D        OriginMeters,
    float                      DeltaSeconds,
    float                      ElapsedSeconds) noexcept
{
    if (Spec.Mode == TransitionDerezMode::Off || Slots.empty() || DeltaSeconds <= 0.0f)
    {
        return;
    }

    const float Cycle     = std::fmod(ElapsedSeconds * 1.4f + 0.01f, 6.0f);
    const float HoldGate  = std::max(0.1f, Spec.HoldBaseSec + Spec.HoldDurationSec * 0.5f);
    const float WaveFront = (Cycle > HoldGate) ? ((Cycle - HoldGate) / std::max(0.25f, 6.0f - HoldGate)) * 2.4f - 1.2f : -2.0f;
    const bool  Reverse   = (Spec.AssembleMode == 1u) || (Spec.Mode == TransitionDerezMode::ReverseDerez);
    const bool  Butterfly = (Spec.AfterTransform == 2u) || (Spec.Mode == TransitionDerezMode::DissolveButterfly);
    const bool  Glitch    = (Spec.WavePattern == 2u) || (Spec.Mode == TransitionDerezMode::GlitchDissolve);

    for (std::size_t I = 0u; I < Slots.size(); ++I)
    {
        ParticleSlot& S = Slots[I];
        const float SweepCoord = (S.PositionAge[0] - OriginMeters.X) * 0.65f + (S.PositionAge[2] - OriginMeters.Z) * 0.45f;
        const bool  Triggered  = Reverse ? (SweepCoord > WaveFront) : (SweepCoord < WaveFront);

        if (!Triggered)
        {
            S.PhaseTiming[0] = 0.0f;
            continue;
        }

        S.PhaseTiming[0] = std::min(1.0f, S.PhaseTiming[0] + DeltaSeconds * 2.5f);
        if (Butterfly)
        {
            const float Flap = std::sin(ElapsedSeconds * 16.0f + S.Attributes[3] * 12.0f) * 1.4f;
            S.VelocityLifetime[0] += std::cos(S.Attributes[3] * 6.283f + ElapsedSeconds) * 2.2f * DeltaSeconds;
            S.VelocityLifetime[1] += std::sin(S.Attributes[3] * 6.283f + ElapsedSeconds) * 2.2f * DeltaSeconds;
            S.VelocityLifetime[2] += (1.2f + Flap) * DeltaSeconds;
        }
        else if (Glitch)
        {
            const float Step = std::floor((ElapsedSeconds + S.Attributes[3]) * Spec.GlitchRateHz);
            S.VelocityLifetime[0] = std::sin(Step * 13.7f) * 2.6f;
            S.VelocityLifetime[2] += 1.4f * DeltaSeconds;
        }
        else if (Spec.Mode == TransitionDerezMode::MeltAway || Spec.WavePattern == 1u)
        {
            S.VelocityLifetime[2] -= 2.4f * DeltaSeconds;
        }
        else
        {
            S.VelocityLifetime[2] += (S.Attributes[3] < Spec.BurstShare ? 2.4f : -2.2f) * DeltaSeconds;
        }

        S.PositionAge[0] += S.VelocityLifetime[0] * DeltaSeconds;
        S.PositionAge[1] += S.VelocityLifetime[1] * DeltaSeconds;
        S.PositionAge[2] += S.VelocityLifetime[2] * DeltaSeconds;
        S.PositionAge[3] += DeltaSeconds;
    }
}

} // namespace Frontier::ParticleSpace
