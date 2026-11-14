/*============================================================================================================================================
                                                          FERROFLUIDMENISCUS.H
============================================================================================================================================*/
// 🧩 Magnetic Rosensweig instability spike meniscus pool & 3D levitated ferrofluid crown tracking orbiting/draggable magnet

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

[[nodiscard]] Mathematics::Axis3D EvaluateFerroMagnetPosition(
    const FerrofluidMeniscusSpec& Spec,
    Mathematics::Axis3D           ManualEffectorMeters,
    Mathematics::Axis3D           OriginMeters,
    float                         ElapsedSeconds) noexcept;

void StepFerrofluidSpikes(
    std::span<ParticleSlot>       Slots,
    const FerrofluidMeniscusSpec& Spec,
    Mathematics::Axis3D           OriginMeters,
    Mathematics::Axis3D           MagnetMeters,
    float                         ElapsedSeconds) noexcept;

} // namespace Frontier::ParticleSpace
