/*============================================================================================================================================
                                                           BALLISTICMOTION.CPP
============================================================================================================================================*/
// 🧩 Ballistic advection, drag, gravity (-Z), buoyancy, lateral leaf/paper flutter & local vortex attractor

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/BallisticMotion.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>

namespace Frontier::ParticleSpace
{

void StepBallisticParticle(
    ParticleSlot&                Slot,
    const BallisticDynamicsSpec& Ballistics,
    const LocalAttractorSpec&    Attractor,
    Mathematics::Axis3D          RigOriginMeters,
    float                        DeltaSeconds,
    float                        ElapsedSeconds,
    uint32_t                     SlotIndex) noexcept
{
    if (DeltaSeconds <= 0.0f || Slot.PositionAge[3] >= Slot.VelocityLifetime[3])
    {
        return;
    }

    Slot.PositionAge[3] += DeltaSeconds;
    if (Slot.PositionAge[3] >= Slot.VelocityLifetime[3])
    {
        return;
    }

    // Gravity along -Z Up (9.81 m/s^2) + upward thermal/bubble buoyancy (+Z)
    constexpr float EarthGravityMps2 = 9.81f;
    Slot.VelocityLifetime[2] += (Ballistics.BuoyancyAccel - Ballistics.GravityFactor * EarthGravityMps2) * DeltaSeconds;

    // Aerodynamic leaf/paper lateral flutter in XY plane
    if (Ballistics.FlutterAmp > 1.0e-4f)
    {
        const float Phase = ElapsedSeconds * 4.8f + Slot.Attributes[3] * Constants::TauFloat + static_cast<float>(SlotIndex) * 0.37f;
        Slot.VelocityLifetime[0] += std::cos(Phase) * Ballistics.FlutterAmp * 2.4f * DeltaSeconds;
        Slot.VelocityLifetime[1] += std::sin(Phase * 1.19f) * Ballistics.FlutterAmp * 2.4f * DeltaSeconds;
        Slot.Attributes[1]       += std::sin(Phase * 0.7f) * Ballistics.FlutterAmp * 1.8f * DeltaSeconds;
    }

    // Inline local vortex / black-hole attractor
    if (Attractor.Enabled)
    {
        const float Dx = RigOriginMeters.X - Slot.PositionAge[0];
        const float Dy = RigOriginMeters.Y - Slot.PositionAge[1];
        const float Dz = RigOriginMeters.Z - Slot.PositionAge[2];
        const float DistSq = Dx * Dx + Dy * Dy + Dz * Dz;
        const float Dist   = std::sqrt(std::max(1.0e-5f, DistSq));
        if (Dist < Attractor.SwallowRadius)
        {
            // Absorbed across event horizon
            Slot.PositionAge[3] = Slot.VelocityLifetime[3];
            return;
        }
        if (Dist < Attractor.RadiusMeters)
        {
            const float Falloff = 1.0f - (Dist / std::max(0.1f, Attractor.RadiusMeters));
            const float Pull    = Attractor.Strength * (0.35f + 0.65f * Falloff) / std::max(0.25f, Dist);
            Slot.VelocityLifetime[0] += Dx * Pull * DeltaSeconds;
            Slot.VelocityLifetime[1] += Dy * Pull * DeltaSeconds;
            Slot.VelocityLifetime[2] += Dz * Pull * DeltaSeconds;

            // Tangential accretion swirl around Z Up axis
            const float RadialXY = std::sqrt(Dx * Dx + Dy * Dy);
            if (RadialXY > 1.0e-4f)
            {
                const float Tx = -Dy / RadialXY;
                const float Ty =  Dx / RadialXY;
                const float SwirlAccel = Attractor.SwirlSpeed * (0.4f + Falloff) * DeltaSeconds;
                Slot.VelocityLifetime[0] += Tx * SwirlAccel;
                Slot.VelocityLifetime[1] += Ty * SwirlAccel;
            }
        }
    }

    // Exponential linear aerodynamic drag
    if (Ballistics.DragPerSecond > 0.0f)
    {
        const float Decay = std::exp(-Ballistics.DragPerSecond * DeltaSeconds);
        Slot.VelocityLifetime[0] *= Decay;
        Slot.VelocityLifetime[1] *= Decay;
        Slot.VelocityLifetime[2] *= Decay;
    }

    // Semi-implicit Euler position integration
    Slot.PositionAge[0] += Slot.VelocityLifetime[0] * DeltaSeconds;
    Slot.PositionAge[1] += Slot.VelocityLifetime[1] * DeltaSeconds;
    Slot.PositionAge[2] += Slot.VelocityLifetime[2] * DeltaSeconds;

    // Intrinsic spin angle update
    Slot.Attributes[1] += Slot.Attributes[2] * DeltaSeconds;
}

} // namespace Frontier::ParticleSpace
