/*============================================================================================================================================
                                                            BOTANICALSWAY.H
============================================================================================================================================*/
// 🧩 Grass/wheat, tall reeds, pinnate ferns, canopy trees, underwater kelp, sea anemones & dandelion/blossom wind detachment

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/ParticleSpace/Dynamics/SimulationDynamicsTypes.h"
#include "Engine/ParticleSpace/Origins/StructuralOrigins.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"

#include <cstdint>
#include <span>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

void StepBotanicalFoliageStrand(
    std::span<StrandVertex>            Vertices,
    const BotanicalSwaySpec&           Botanical,
    const StrandConstraintSpec&        StrandSpec,
    const StructuralStrandAnchor&      Anchor,
    const InteractiveColliderSettings& Collider,
    Mathematics::Axis3D                AmbientWindMps,
    float                              ElapsedSeconds) noexcept;

void EvaluateBotanicalSeedDetachment(
    ParticleSlot&            SeedSlot,
    const BotanicalSwaySpec& Botanical,
    Mathematics::Axis3D      AmbientWindMps,
    float                    DeltaSeconds,
    float                    ElapsedSeconds) noexcept;

} // namespace Frontier::ParticleSpace
