/*============================================================================================================================================
                                                           TRANSITIONDEREZ.H
============================================================================================================================================*/
// 🧩 Surface lattice hold, sweep/melt/glitch wave derez, reverse assembly, coin stack, 8-fragment floor shatter & butterfly morph

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/ParticleSpace/Dynamics/SimulationDynamicsTypes.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"

#include <span>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

void StepTransitionDerez(
    std::span<ParticleSlot>       Slots,
    const TransitionDerezSpec&    Spec,
    Mathematics::Axis3D           OriginMeters,
    float                         DeltaSeconds,
    float                         ElapsedSeconds) noexcept;

} // namespace Frontier::ParticleSpace
