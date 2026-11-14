/*============================================================================================================================================
                                                           EFFECTORLATTICE.H
============================================================================================================================================*/
// 🧩 MoGraph 7 lattice shapes x 6 effector drivers + twin placeable 3D effectors (Sphere, Box, Torus, Disc, Cone)

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

[[nodiscard]] float EvaluateEffectorInfluence(
    Mathematics::Axis3D SamplePoint,
    EffectorVolumeSpec  Volume,
    float               FalloffContrast) noexcept;

void StepEffectorLattice(
    std::span<ParticleSlot>    Slots,
    const EffectorLatticeSpec& Spec,
    Mathematics::Axis3D        OriginMeters,
    float                      ElapsedSeconds) noexcept;

} // namespace Frontier::ParticleSpace
