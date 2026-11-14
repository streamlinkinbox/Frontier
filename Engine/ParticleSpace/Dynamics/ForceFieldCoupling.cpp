/*============================================================================================================================================
                                                          FORCEFIELDCOUPLING.CPP
============================================================================================================================================*/
// 🧩 Implementation of SimulationSpace::ForceFields coupling, ambient wind relaxation & local magnetic dipole steering

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/ForceFieldCoupling.h"

#include <algorithm>
#include <cmath>

namespace Frontier::ParticleSpace
{

void ApplyForceFieldCoupling(
    ParticleSlot&                                              Slot,
    const ForceFieldLinkSpec&                                  LinkSpec,
    const SimulationSpace::ForceFields::ForceFieldAccumulator* Accumulator,
    Mathematics::Axis3D                                        RigOriginMeters,
    Mathematics::Axis3D                                        AmbientWindMps,
    float                                                      DeltaSeconds,
    double                                                     TimeSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f || Slot.PositionAge[3] >= Slot.VelocityLifetime[3])
    {
        return;
    }

    // 1. Ambient wind relaxation
    if (LinkSpec.WindCouplingRate > 0.0f)
    {
        const float Alpha = 1.0f - std::exp(-LinkSpec.WindCouplingRate * DeltaSeconds);
        Slot.VelocityLifetime[0] += (AmbientWindMps.X - Slot.VelocityLifetime[0]) * Alpha;
        Slot.VelocityLifetime[1] += (AmbientWindMps.Y - Slot.VelocityLifetime[1]) * Alpha;
        Slot.VelocityLifetime[2] += (AmbientWindMps.Z - Slot.VelocityLifetime[2]) * Alpha;
    }

    // 2. Full 19-model ForceFieldAccumulator sample (+Z Up)
    if (Accumulator != nullptr && Accumulator->QueryActiveSourceCount() > 0u &&
        (LinkSpec.ReceiverChannels & ForceChannelMask::Particles) != ForceChannelMask::None)
    {
        const SimulationSpace::ForceFields::SpatialVector3 Pt{
            Slot.PositionAge[0],
            Slot.PositionAge[1],
            Slot.PositionAge[2],
            0.0f,
        };
        const SimulationSpace::ForceFields::SpatialVector3 Vel{
            Slot.VelocityLifetime[0],
            Slot.VelocityLifetime[1],
            Slot.VelocityLifetime[2],
            0.0f,
        };
        const auto Sample = Accumulator->QueryForceFieldSample(Pt, Vel, TimeSeconds);

        const float AdvectGain = std::clamp(std::max(LinkSpec.WindCouplingRate, 0.8f) * DeltaSeconds, 0.0f, 1.0f);
        Slot.VelocityLifetime[0] += Sample.AdvectionVelocity.X * AdvectGain + Sample.LinearAcceleration.X * DeltaSeconds;
        Slot.VelocityLifetime[1] += Sample.AdvectionVelocity.Y * AdvectGain + Sample.LinearAcceleration.Y * DeltaSeconds;
        Slot.VelocityLifetime[2] += Sample.AdvectionVelocity.Z * AdvectGain + Sample.LinearAcceleration.Z * DeltaSeconds;

        if (Sample.DragDecayRate > 0.0f)
        {
            const float Damp = std::exp(-Sample.DragDecayRate * DeltaSeconds);
            Slot.VelocityLifetime[0] *= Damp;
            Slot.VelocityLifetime[1] *= Damp;
            Slot.VelocityLifetime[2] *= Damp;
        }
    }

    // 3. Inline magnetic dipole field (magnetic-particles preset: B(r) = (3(m·r̂)r̂ - m) / r^3 along +Z dipole moment)
    if (LinkSpec.LocalMagneticActive)
    {
        const float Rx = Slot.PositionAge[0] - RigOriginMeters.X;
        const float Ry = Slot.PositionAge[1] - RigOriginMeters.Y;
        const float Rz = Slot.PositionAge[2] - RigOriginMeters.Z;
        const float R2 = Rx * Rx + Ry * Ry + Rz * Rz;
        const float R  = std::sqrt(std::max(0.09f, R2));
        if (R < LinkSpec.LocalMagneticRadius)
        {
            const float InvR  = 1.0f / R;
            const float Ux    = Rx * InvR;
            const float Uy    = Ry * InvR;
            const float Uz    = Rz * InvR;
            // Dipole moment m = (0, 0, 1) -> B = (3*Uz*u - (0,0,1))
            const float Bx    = 3.0f * Uz * Ux;
            const float By    = 3.0f * Uz * Uy;
            const float Bz    = 3.0f * Uz * Uz - 1.0f;
            const float BLen  = std::sqrt(std::max(1.0e-6f, Bx * Bx + By * By + Bz * Bz));
            const float Fall  = LinkSpec.LocalMagneticStrength / (1.0f + R2 * 0.25f);
            const float Tx    = (Bx / BLen) * LinkSpec.LocalMagneticGuide;
            const float Ty    = (By / BLen) * LinkSpec.LocalMagneticGuide;
            const float Tz    = (Bz / BLen) * LinkSpec.LocalMagneticGuide;
            const float Alpha = std::clamp(Fall * DeltaSeconds, 0.0f, 0.75f);
            Slot.VelocityLifetime[0] += (Tx - Slot.VelocityLifetime[0]) * Alpha;
            Slot.VelocityLifetime[1] += (Ty - Slot.VelocityLifetime[1]) * Alpha;
            Slot.VelocityLifetime[2] += (Tz - Slot.VelocityLifetime[2]) * Alpha;
        }
    }
}

} // namespace Frontier::ParticleSpace
