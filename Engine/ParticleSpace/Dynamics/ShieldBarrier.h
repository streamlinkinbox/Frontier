/*============================================================================================================================================
                                                            SHIELDBARRIER.H
============================================================================================================================================*/
// 🧩 6 volumetric energy shield forms x 7 geometric tessellation motif masks + geodesic ball impact ripples

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/ParticleSpace/Dynamics/SimulationDynamicsTypes.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"

#include <cstdint>
#include <span>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] float EvaluateShieldMotifMask(
    Mathematics::Axis3D LocalUnitNormal,
    uint32_t            MotifBitmask,
    float               TileScale) noexcept;

void StepShieldBarrier(
    std::span<ParticleSlot>            Slots,
    const ShieldBarrierSpec&           Spec,
    const InteractiveColliderSettings& Collider,
    Mathematics::Axis3D                OriginMeters,
    float                              ElapsedSeconds) noexcept;

} // namespace Frontier::ParticleSpace
