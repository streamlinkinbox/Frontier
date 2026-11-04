/*==========================================================================================================================================
                                                             OPTICALREFERENCE.H
==========================================================================================================================================*/
// 🧩 The VehicleHMI optical reference, its four presets and portable linear-HDR PFM encoding.

#pragma once

#include "Engine/Scene/EngineConfigurations.h"

#include <array>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    OPTICAL REFERENCE
//------------------------------------------------------------------------------------------------------------------------

void                                ConfigureOpticalPreset(OpticalFlareConfiguration &Parameters, uint32_t Preset);
[[nodiscard]] uint32_t              ResolveFlareRayPairs(const OpticalFlareConfiguration &Parameters) noexcept;
[[nodiscard]] std::array<double, 2> EvaluateFlareGhost(const OpticalFlareConfiguration &Parameters, uint32_t Ghost) noexcept;
[[nodiscard]] std::array<double, 3> EvaluateOpticalReference(
    const OpticalFlareConfiguration &Parameters, double U, double V, double Aspect, double Visibility = 1.0) noexcept;
[[nodiscard]] std::vector<float> GenerateOpticalReference(const OpticalFlareConfiguration &Parameters, int Width = 640, int Height = 280);
[[nodiscard]] bool               EncodeLinearPfm(
                  const std::filesystem::path &Destination, std::span<const float> Radiance, int Width, int Height, std::string &Diagnostic);

} // namespace Frontier
