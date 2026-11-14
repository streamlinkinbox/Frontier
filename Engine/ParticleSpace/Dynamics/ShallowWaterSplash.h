/*============================================================================================================================================
                                                          SHALLOWWATERSPLASH.H
============================================================================================================================================*/
// 🧩 Puddle ripple rings, crown splash, figure-8 rally vehicle kinematics & tyre mud splatter

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
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct RallyVehiclePose
{
    Mathematics::Axis3D PositionMeters { 0.0f, 0.0f, 0.35f }; // [m]   - Vehicle hull centre (+Z Up)
    Mathematics::Axis3D ForwardAxis    { 1.0f, 0.0f, 0.0f  }; // [-]   - Unit forward heading
    float               SpeedMps       = 4.2f;                // [m/s] - Tangential loop speed
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] RallyVehiclePose EvaluateRallyVehiclePose(
    const ShallowWaterSplashSpec& Spec,
    float                         ElapsedSeconds) noexcept;

void StepShallowWaterSplash(
    std::span<ParticleSlot>       Slots,
    const ShallowWaterSplashSpec& Spec,
    float                         DeltaSeconds,
    float                         ElapsedSeconds) noexcept;

} // namespace Frontier::ParticleSpace
