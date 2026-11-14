/*============================================================================================================================================
                                                           FORCEFIELDCOUPLING.H
============================================================================================================================================*/
// 🧩 Couples SimulationSpace::ForceFields (19 ForceModel definitions) + ambient wind + inline magnetic dipole to ParticleSlots

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/ParticleSpace/Dynamics/SimulationDynamicsTypes.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

void ApplyForceFieldCoupling(
    ParticleSlot&                                          Slot,
    const ForceFieldLinkSpec&                              LinkSpec,
    const SimulationSpace::ForceFields::ForceFieldAccumulator* Accumulator,
    Mathematics::Axis3D                                    RigOriginMeters,
    Mathematics::Axis3D                                    AmbientWindMps,
    float                                                  DeltaSeconds,
    double                                                 TimeSeconds) noexcept;

} // namespace Frontier::ParticleSpace
