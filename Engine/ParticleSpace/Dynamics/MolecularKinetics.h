/*============================================================================================================================================
                                                          MOLECULARKINETICS.H
============================================================================================================================================*/
// 🧩 Lennard-Jones short-range molecular force, Langevin thermostat & reversible A + B <-> C reaction kinetics

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

void StepMolecularKinetics(
    std::span<ParticleSlot>      Slots,
    const MolecularKineticsSpec& Spec,
    Mathematics::Axis3D          VesselOriginMeters,
    bool                         EnableReactions,
    float                        DeltaSeconds) noexcept;

} // namespace Frontier::ParticleSpace
