/*============================================================================================================================================
                                                            PYROSPECTACLE.H
============================================================================================================================================*/
// 🧩 Multi-stage firework shells + SkyText bursts, military IR decoy flare V-salvos & coherent beam arc filaments

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/ParticleSpace/Dynamics/SimulationDynamicsTypes.h"
#include "Engine/ParticleSpace/Origins/FireworkPatternSampler.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"

#include <span>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

void StepPyroFireworkStar(
    ParticleSlot&            Slot,
    const PyroSpectacleSpec& Spec,
    float                    DeltaSeconds,
    float                    ElapsedSeconds) noexcept;

void StepFlareSalvoEjection(
    std::span<ParticleSlot>  Slots,
    const PyroSpectacleSpec& Spec,
    Mathematics::Axis3D      AircraftOriginMeters,
    float                    DeltaSeconds,
    float                    ElapsedSeconds) noexcept;

void StepBeamFilament(
    std::span<ParticleSlot>  Slots,
    const PyroSpectacleSpec& Spec,
    Mathematics::Axis3D      SourceMeters,
    Mathematics::Axis3D      TargetMeters,
    float                    ElapsedSeconds) noexcept;

} // namespace Frontier::ParticleSpace
