/*============================================================================================================================================
                                                          INTERACTIVECOLLIDER.H
============================================================================================================================================*/
// 🧩 Interactive chrome collider ball push/impulse, floor plane restitution & optional DistanceFieldSpace SignedDistanceVolume collision

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/DistanceFieldSpace/SignedDistanceVolume.h"
#include "Engine/ParticleSpace/Dynamics/SimulationDynamicsTypes.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

void ResolveParticleCollisions(
    ParticleSlot&                                   Slot,
    const InteractiveColliderSettings&              Collider,
    float                                           BounceRestitution,
    float                                           ElapsedSeconds,
    const DistanceFieldSpace::SignedDistanceVolume* OptionalSdf = nullptr) noexcept;

} // namespace Frontier::ParticleSpace
