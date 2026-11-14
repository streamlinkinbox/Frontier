/*============================================================================================================================================
                                                             FLOCKBOIDS.H
============================================================================================================================================*/
// 🧩 Spatial-hash Boids flocking (cohesion, alignment, separation), synchronized firefly bioluminescence & predator ball evasion

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

void StepFlockBoids(
    std::span<ParticleSlot>            Slots,
    const FlockBoidsSpec&              Spec,
    const InteractiveColliderSettings& Collider,
    Mathematics::Axis3D                RoostOriginMeters,
    float                              DeltaSeconds,
    float                              ElapsedSeconds) noexcept;

} // namespace Frontier::ParticleSpace
