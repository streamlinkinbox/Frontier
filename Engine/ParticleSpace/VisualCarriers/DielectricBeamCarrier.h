/*============================================================================================================================================
                                                        DIELECTRICBEAMCARRIER.H
============================================================================================================================================*/
// 🧩 Coherent laser/plasma beams, Tesla/static sphere arcs, spiderweb silk bonds, molecular bonds, 3D fibre tubes & strike sparks

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourTypes.h"

#include <cstdint>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class DielectricArcCategory : uint8_t
{
    SpiderwebSilk  = 0u, // [-] - Radial spoke & capture spiral silk filament
    MolecularBond  = 1u, // [-] - Covalent/LJ pair bond line
    LaserBeam      = 2u, // [-] - Coherent collimated laser / plasma beam
    TeslaSphereArc = 3u, // [-] - Dielectric breakdown arc branching around sphere
    PlasmaCageArc  = 4u, // [-] - High-voltage cage / storm-grid lattice arcs
    FibreTube      = 5u, // [-] - Illuminated 3D optical fibre tube
    StrikeSpark    = 6u, // [-] - Grounding electrical strike arc
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct DielectricBeamCarrier
{
    DielectricArcCategory             ArcCategory      = DielectricArcCategory::LaserBeam;
    float                             CoreRadiusMeters = 0.048f; // [m]  - Luminous core radius
    float                             PulseSpeedHz     = 6.0f;   // [Hz] - Travelling wave modulation speed
    float                             DewIntensity     = 0.85f;  // [0-1]- Droplet / nodal sparkle weight
    ColourSpace::OpticalBlendEquation BlendEquation    = ColourSpace::OpticalBlendEquation::Additive;
};

} // namespace Frontier::ParticleSpace
