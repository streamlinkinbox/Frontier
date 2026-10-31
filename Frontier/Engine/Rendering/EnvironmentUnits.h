/*==========================================================================================================================================
                                                   ENVIRONMENT FRAME DECLARATION
                                     ------------------------------------------------------------------
                 Atmosphere LUT bakes, sky, clouds, fog, precipitation and flare declared as
                      ordered submission units (plan §8.1 groups; Phase 6).
==========================================================================================================================================*/

// 🧩 Environment frame declaration: the environment half of the frame as data.
//
//   AtmosphereTransmittance (LUT bake) → AtmosphereMultiple (LUT) → CelestialSky → NightStars
//     → CloudRecords → CloudShadowProject → CloudTrace → CloudPresent
//     → FogMedium → Precipitation → FlareVisibility → FlareImage
//
// LUTs are Persistent (baked once, re-baked on parameter change — never atlas-aliased); the
// surface depth is an imported read from the surface phase. The SDK phase merges this with the
// illumination declaration into one frame; today the portable layer orders it, aliases its
// transients and derives its barriers, and Exhibits/EnvironmentPipeline proves the models.

#pragma once

#include "RenderTarget.h"
#include "SubmissionOrder.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

struct EnvironmentFrameDeclaration
{
    std::vector<SubmissionUnitDeclaration> Units;
    std::vector<RenderTargetDeclaration>   Targets;
};

[[nodiscard]] EnvironmentFrameDeclaration DeclareEnvironmentFrame(uint32_t Width, uint32_t Height);

} // namespace Frontier
