/*============================================================================================================================================
                                                        BACKINGSURFACECARRIER.H
============================================================================================================================================*/
// 🧩 Backing geometry appearance carrier for all 17 surface presets, imported PolygonNetworks, collider ball & vehicle hull

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/ParticleSpace/Origins/ReleaseOriginTypes.h"

#include <cstdint>
#include <string>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class BackingFinishCategory : uint8_t
{
    ObsidianDark        = 0u, // [-] - Deep matte-specular obsidian finish
    BrushedChrome       = 1u, // [-] - Anisotropic metallic chrome finish
    GhostWireframe      = 2u, // [-] - Translucent edge-wireframe silhouette
    ParticlesOnlyHidden = 3u, // [-] - Backing geometry suppressed (particles only)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct BackingSurfaceCarrier
{
    BackingFinishCategory FinishStyle      = BackingFinishCategory::ObsidianDark;      // [-]   - Shading finish
    SurfacePresetCategory SurfacePreset    = SurfacePresetCategory::FloorMatrixXZ;     // [-]   - 0..16 surface selector
    SurfaceSampleMode     SampleMode       = SurfaceSampleMode::EvenSurface;           // [-]   - Anchor sampling mode
    Mathematics::Axis3D   EulerDegrees     { 0.0f, 0.0f, 0.0f };                       // [deg] - Surface Euler orientation
    std::string           CustomAssetPath  {};                                         // [-]   - Optional external .obj/.stl path
    bool                  VehicleHullMode  = false;                                    // [-]   - Draw figure-8 rally vehicle hull
};

} // namespace Frontier::ParticleSpace
