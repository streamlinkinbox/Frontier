/*==========================================================================================================================================
                                                         CLOUDDENSITYREFERENCE.H
==========================================================================================================================================*/
// 🧩 CPU transcription of CloudField.js and CloudSettings.js for the inspector's density atlas, not the sky renderer.

#pragma once

#include "Engine/Scene/EngineConfigurations.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CLOUD DENSITY REFERENCE
//------------------------------------------------------------------------------------------------------------------------

class CloudDensityReference
{
  public:
    [[nodiscard]] static double Evaluate(const CloudConfiguration &Cloud, const std::array<double, 3> &Position);
    static void ConstructImages(const CloudConfiguration &Cloud, double Aspect, std::vector<uint8_t> &Top, std::vector<uint8_t> &Side);
    static constexpr int Width  = 192;
    static constexpr int Height = 96;
};

} // namespace Frontier
