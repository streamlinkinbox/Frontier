/*============================================================================================================================================
                                                             CURLMANIFOLD.H
============================================================================================================================================*/
// 🧩 Divergence-free 3D curl simplex advection, planar wave fur, spherical tangent plasma, bokeh streams & surface-pour manifolds

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/ParticleSpace/Dynamics/SimulationDynamicsTypes.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] Mathematics::Axis3D EvaluateCurlNoise3D(
    Mathematics::Axis3D PositionMeters,
    float               SpatialFrequency,
    float               ElapsedSeconds) noexcept;

void StepCurlManifoldParticle(
    ParticleSlot&           Slot,
    const CurlManifoldSpec& Spec,
    Mathematics::Axis3D     OriginMeters,
    float                   DeltaSeconds,
    float                   ElapsedSeconds) noexcept;

} // namespace Frontier::ParticleSpace
