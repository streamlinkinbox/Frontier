/*==========================================================================================================================================
                                                           ATMOSPHEREBAKEIMAGES.H
==========================================================================================================================================*/
// 🧩 Display mapping and reprojection of actual GPU LUT readbacks; never recomputes a substitute sky.

#pragma once

#include "Engine/Scene/EnvironmentDiagnostics.h"
#include <vector>

namespace Frontier
{

[[nodiscard]] std::vector<uint8_t> ResolveLookupPixels(const std::vector<float> &LinearRgba, bool Transmission);
[[nodiscard]] std::vector<uint8_t> ProjectAtmosphereLookup(const AtmosphereBakeReadout &Bake, int View);

} // namespace Frontier
