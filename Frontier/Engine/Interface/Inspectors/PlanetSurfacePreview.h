/*==========================================================================================================================================
                                                           PLANETSURFACEPREVIEW.H
==========================================================================================================================================*/
// 🧩 Isolated live planet previews evaluated by the production celestial surface renderer.

#pragma once

#include "Engine/Scene/EngineConfigurations.h"
#include <vector>

namespace Frontier
{
[[nodiscard]] std::vector<uint8_t> GeneratePlanetSurfacePreview(const PlanetConfiguration &Planet);
} // namespace Frontier
