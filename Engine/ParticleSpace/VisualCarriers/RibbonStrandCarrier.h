/*============================================================================================================================================
                                                         RIBBONSTRANDCARRIER.H
============================================================================================================================================*/
// 🧩 20-vertex continuous ribbon strip carrier for silk waves, cables/chains, woven cloth, blades, pinnate ferns, kelp & smoke plumes

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

enum class RibbonProfileCategory : uint8_t
{
    VelocityWake      = 0u,  // [-] - Tapered projectile / viscous droplet wake
    SurfaceStreamPour = 1u,  // [-] - Bokeh / curl stream ribbons pouring across surfaces
    CatenaryCable     = 2u,  // [-] - Hanging flexible power cable or suspension wire
    ChainLinks        = 3u,  // [-] - Interlocked metallic chain links
    LeafyVine         = 4u,  // [-] - Botanical vine / creeping rope with foliage
    WovenCloth        = 5u,  // [-] - Solid woven banner flag, ship sail, or theater drape
    TaperedBlade      = 6u,  // [-] - Grass, wheat stalk, or tall reed blade
    PinnateFern       = 7u,  // [-] - Compound pinnate fern frond with leaflets
    BarkCylinder      = 8u,  // [-] - Tapered woody trunk / branch cylinder
    KelpBlade         = 9u,  // [-] - Undulating giant kelp frond
    AnemoneTentacle   = 10u, // [-] - Thickened luminescent sea anemone tentacle
    SmokePlume        = 11u, // [-] - Billowing countermeasure decoy flare smoke ribbon
    ViscousTrail      = 12u, // [-] - Cohesive dripping slime / viscous tendril
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct RibbonStrandCarrier
{
    RibbonProfileCategory             Profile        = RibbonProfileCategory::VelocityWake;
    uint32_t                          SegmentCount   = 16u;                                   // [count] - Ribbon segments (2..20)
    float                             WidthMeters    = 0.04f;                                 // [m]     - Nominal strip half-width
    float                             ThicknessScale = 1.0f;                                  // [-]     - Profile thickness multiplier
    bool                              CurveDebugPins = false;                                 // [-]     - Show control vertices
    ColourSpace::OpticalBlendEquation BlendEquation  = ColourSpace::OpticalBlendEquation::Additive;
};

} // namespace Frontier::ParticleSpace
