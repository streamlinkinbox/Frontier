/*============================================================================================================================================
                                                        WINGEDCREATURECARRIER.H
============================================================================================================================================*/
// 🧩 Articulated 3D hinged dual-wing creature carrier (butterflies, birds, starling murmurations, bioluminescent fish)

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

enum class WingedSpeciesCategory : uint8_t
{
    Butterfly          = 0u, // [-] - Broad iridescent hinged dual-wing butterfly
    Songbird           = 1u, // [-] - Swept-wing flock bird
    Starling           = 2u, // [-] - Agile dark murmuration starling
    BioluminescentFish = 3u, // [-] - Lateral-undulating schooling fish
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct WingedCreatureCarrier
{
    WingedSpeciesCategory             Species         = WingedSpeciesCategory::Butterfly;
    float                             FlapFrequencyHz = 5.5f;  // [Hz] - Wingbeat / tail-fin oscillation frequency
    float                             WingspanMeters  = 0.22f; // [m]  - Characteristic span scale
    ColourSpace::OpticalBlendEquation BlendEquation   = ColourSpace::OpticalBlendEquation::PremultipliedAlpha;
};

} // namespace Frontier::ParticleSpace
