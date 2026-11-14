/*============================================================================================================================================
                                                           STRANDCONSTRAINT.H
============================================================================================================================================*/
// 🧩 XPBD 20-vertex strand solver for cables, chains, vines, woven flags/sails, bead curtains, wind chimes, velvet drapes & jelly tendrils

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

void InitializeStrandVertices(
    std::span<StrandVertex>              Vertices,
    std::span<StrandVertex>              PrevVertices,
    const StructuralStrandAnchor&        Anchor,
    float                                NominalWidthMeters) noexcept;

// Returns true if the strand remains intact; false if torn by impact exceeding TearThresholdSpeed
bool StepStrandXpbd(
    std::span<StrandVertex>              Vertices,
    std::span<StrandVertex>              PrevVertices,
    const StrandConstraintSpec&          Spec,
    const StructuralStrandAnchor&        Anchor,
    const InteractiveColliderSettings&   Collider,
    Mathematics::Axis3D                  AmbientWindMps,
    float                                DeltaSeconds,
    float                                ElapsedSeconds) noexcept;

} // namespace Frontier::ParticleSpace
