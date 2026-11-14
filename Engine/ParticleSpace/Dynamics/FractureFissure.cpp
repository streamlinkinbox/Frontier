/*============================================================================================================================================
                                                          FRACTUREFISSURE.CPP
============================================================================================================================================*/
// 🧩 Glacial ice & seismic Voronoi ground fracture front propagation and fissure heave solver

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/FractureFissure.h"

#include "Engine/ColourSpace/ColourRampSampler.h"

#include <algorithm>
#include <cmath>

namespace Frontier::ParticleSpace
{

void StepFractureFissureSlabs(
    std::span<ParticleSlot>    SlabSlots,
    const FractureFissureSpec& Spec,
    Mathematics::Axis3D        EpicentreMeters,
    float                      ElapsedSeconds) noexcept
{
    if (Spec.CrackMode == 0u || SlabSlots.empty())
    {
        return;
    }

    const float FrontRadius = ElapsedSeconds * Spec.PropagationSpeed;
    const uint32_t Total    = static_cast<uint32_t>(SlabSlots.size());
    const uint32_t Side     = std::max(4u, static_cast<uint32_t>(std::sqrt(static_cast<float>(Total))));

    for (uint32_t I = 0u; I < Total; ++I)
    {
        const uint32_t Col = I % Side;
        const uint32_t Row = I / Side;
        const float U      = (static_cast<float>(Col) + 0.5f) / static_cast<float>(Side) * 2.0f - 1.0f;
        const float V      = (static_cast<float>(Row) + 0.5f) / static_cast<float>(Side) * 2.0f - 1.0f;
        const float JitterX = (ColourSpace::HashDeterministicFloat(static_cast<float>(I), 11u) - 0.5f) * 0.14f;
        const float JitterY = (ColourSpace::HashDeterministicFloat(static_cast<float>(I), 23u) - 0.5f) * 0.14f;
        const float Rx      = EpicentreMeters.X + U * 3.4f + JitterX;
        const float Ry      = EpicentreMeters.Y + V * 3.4f + JitterY;
        const float R       = std::sqrt((Rx - EpicentreMeters.X) * (Rx - EpicentreMeters.X) +
                                        (Ry - EpicentreMeters.Y) * (Ry - EpicentreMeters.Y));

        const float Activated = std::clamp((FrontRadius - R) * 1.8f, 0.0f, 1.0f);
        const float Noise     = ColourSpace::SampleLatticeNoise3D(Rx * 1.4f, Ry * 1.4f, 2.3f);
        const float HeaveZ    = (Spec.CrackMode == 2u)
                                    ? Activated * Spec.FissureHeaveMeters * (Noise - 0.35f) * 2.0f
                                    : -Activated * Spec.FissureHeaveMeters * 0.45f * std::max(0.0f, Noise);

        ParticleSlot& S = SlabSlots[I];
        S.PositionAge[0]      = Rx;
        S.PositionAge[1]      = Ry;
        S.PositionAge[2]      = EpicentreMeters.Z + HeaveZ;
        S.PositionAge[3]      = Activated;
        S.VelocityLifetime[0] = 0.0f;
        S.VelocityLifetime[1] = 0.0f;
        S.VelocityLifetime[2] = HeaveZ * 0.5f;
        S.VelocityLifetime[3] = 99.0f;
        S.Attributes[0]       = 6.4f / static_cast<float>(Side);
        S.Attributes[1]       = Activated * (Noise - 0.5f) * 0.48f; // Slab tilt angle
        S.PhaseTiming[0]      = Activated;
    }
}

} // namespace Frontier::ParticleSpace
