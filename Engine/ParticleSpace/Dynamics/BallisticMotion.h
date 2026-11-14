/*============================================================================================================================================
                                                            BALLISTICMOTION.H
============================================================================================================================================*/
// 🧩 Modular ballistic advection, aerodynamic drag, gravity (-Z), thermal buoyancy, lateral flutter & local vortex attractor

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/ParticleSpace/Dynamics/SimulationDynamicsTypes.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"

#include <cstdint>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

void StepBallisticParticle(
    ParticleSlot&                Slot,
    const BallisticDynamicsSpec& Ballistics,
    const LocalAttractorSpec&    Attractor,
    Mathematics::Axis3D          RigOriginMeters,
    float                        DeltaSeconds,
    float                        ElapsedSeconds,
    uint32_t                     SlotIndex) noexcept;

} // namespace Frontier::ParticleSpace
