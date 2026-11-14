/*============================================================================================================================================
                                                       FIREWORKPATTERNSAMPLER.H
============================================================================================================================================*/
// 🧩 All 10 FireworkShape pyrotechnic shell & New Year SkyText/Object pattern samplers (matching FireworkPatterns.js verbatim)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/CoordinateSpace.h"
#include "Engine/ParticleSpace/Origins/ReleaseOriginTypes.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct FireworkStarPoint
{
    float    OffsetX    = 0.0f; // [m]   - Normalized pattern offset X
    float    OffsetY    = 0.0f; // [m]   - Normalized pattern offset Y
    float    OffsetZ    = 0.0f; // [m]   - Normalized pattern offset Z
    float    Red        = 1.0f; // [0-1] - Star linear red
    float    Green      = 1.0f; // [0-1] - Star linear green
    float    Blue       = 1.0f; // [0-1] - Star linear blue
    uint32_t RoleCode   = 1u;   // [-]   - 1 primary pattern star, 2 core/halo sparkle
    float    DelaySec   = 0.0f; // [s]   - Sub-shell ignition delay
};

struct FireworkShellSample
{
    FireworkShapeCategory          ResolvedShape = FireworkShapeCategory::Bouquet; // [-] - Resolved active shape (unwraps GrandFinale)
    std::string                    ShellCaption  {};                               // [-] - Human-readable shell summary
    bool                           WillowTrails  = false;                          // [-] - Long drooping gold/emerald trails
    bool                           StrobeTwinkle = true;                           // [-] - Terminal strobe shimmer
    std::vector<FireworkStarPoint> Points        {};                               // [-] - Sampled pattern stars
    std::vector<ParticleSlot>      PackedSnap    {};                               // [-] - Packed GPU ParticleSlot snap buffer
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] FireworkShellSample SampleFireworkShell(
    FireworkShapeCategory             Shape,
    std::string_view                  SkyText,
    uint32_t                          StarCount,
    int32_t                           ShellIndex  = 0,
    Mathematics::WorldCoordinateBasis TargetBasis = Mathematics::WorldCoordinateBasis::RightHandedZUp);

} // namespace Frontier::ParticleSpace
