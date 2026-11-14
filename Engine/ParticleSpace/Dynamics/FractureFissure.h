/*============================================================================================================================================
                                                           FRACTUREFISSURE.H
============================================================================================================================================*/
// 🧩 Glacial ice crack propagation & seismic Voronoi basalt ground fracture heave solver

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

void StepFractureFissureSlabs(
    std::span<ParticleSlot>    SlabSlots,
    const FractureFissureSpec& Spec,
    Mathematics::Axis3D        EpicentreMeters,
    float                      ElapsedSeconds) noexcept;

} // namespace Frontier::ParticleSpace
