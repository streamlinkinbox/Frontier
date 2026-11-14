/*============================================================================================================================================
                                                            SPIDERWEBWEAVE.H
============================================================================================================================================*/
// 🧩 Real-time orb-weaver spiderweb extrusion, tangled cobwebs, wind-flex catenary sag & dew-drop bond links

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/ParticleSpace/Dynamics/SimulationDynamicsTypes.h"
#include "Engine/ParticleSpace/Origins/StructuralOrigins.h"

#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SpiderwebSilkLattice
{
    std::vector<Mathematics::Axis3D> RestPositions     {};     // [m]   - Initial anchor positions
    std::vector<Mathematics::Axis3D> LivePositions     {};     // [m]   - Wind-flexed & ball-deflected positions
    std::vector<StructuralBondLink>  Bonds             {};     // [-]   - Radial spoke & capture spiral bonds
    float                            ExtrusionProgress = 0.0f; // [0-1] - Spinneret progressive build ratio
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] SpiderwebSilkLattice InitializeSpiderwebLattice(
    const SpiderwebOriginSpec& OriginSpec);

void StepSpiderwebWeave(
    SpiderwebSilkLattice&              WebLattice,
    const SpiderwebWeaveSpec&          WeaveSpec,
    const InteractiveColliderSettings& Collider,
    Mathematics::Axis3D                AmbientWindMps,
    float                              DeltaSeconds,
    float                              ElapsedSeconds) noexcept;

} // namespace Frontier::ParticleSpace
