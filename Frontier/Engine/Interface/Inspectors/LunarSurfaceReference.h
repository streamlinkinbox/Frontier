/*==========================================================================================================================================
                                                         LUNARSURFACEREFERENCE.H
==========================================================================================================================================*/
// 🧩 Registered lunar maps and the demo's textured, isolated sphere reference.

#pragma once

#include "Engine/Scene/EngineConfigurations.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    LUNAR SURFACES
//------------------------------------------------------------------------------------------------------------------------

struct LunarSurfacePreset
{
    const char *Name;
    const char *Filename;
    float       Diameter;
    float       Tilt;
    float       Haze;
    float       Gamma;
};

inline constexpr std::array<LunarSurfacePreset, 6> LunarSurfacePresets = {{{"Luna", "MoonAlbedoSource2048.jpg", 25.0f, 6.7f, 0.0f, 1.0f},
                                                                           {"Ember", "Ember2048.jpg", 1.6f, 25.0f, 0.08f, 1.0f},
                                                                           {"Glacier", "Glacier2048.jpg", 1.2f, 3.0f, 0.05f, 1.0f},
                                                                           {"Sulfur", "Sulfur2048.jpg", 1.1f, 2.0f, 0.0f, 1.0f},
                                                                           {"Shroud", "Shroud2048.jpg", 2.4f, 27.0f, 1.0f, 1.0f},
                                                                           {"Shard", "Shard2048.jpg", 0.6f, 0.0f, 0.0f, 0.9f}}};

struct LunarSurfaceReference
{
    int                  Width  = 0;
    int                  Height = 0;
    std::vector<uint8_t> Rgb;
    std::string          Diagnostic;
};

[[nodiscard]] LunarSurfaceReference ReadLunarSurface(const std::filesystem::path &Path);
[[nodiscard]] std::vector<uint8_t>  GenerateLunarReference(const LunarSurfaceReference &Surface, const MoonInstanceConfiguration &Moon);
[[nodiscard]] const char           *ResolveLunarPhaseName(double Days) noexcept;

} // namespace Frontier
