/*============================================================================================================================================
                                                         ORIENTEDSOLIDCARRIER.H
============================================================================================================================================*/
// 🧩 True 3D instanced solid carrier for cubes, metallic coins, glass shards, 8-fragment floor shatter derez & MoGraph prisms

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

enum class SolidGeometryCategory : uint8_t
{
    TumblingCube         = 0u, // [-] - 3D spinning voxel / derez cube
    MetallicCoin         = 1u, // [-] - 3D cylindrical rimmed coin
    GlassShardCrystal    = 2u, // [-] - Sharp refractive 3D glass prism shard
    ShatterFragment      = 3u, // [-] - 8-fragment floor-impact shatter derez
    MoGraphExtrudedPrism = 4u, // [-] - Height-extruded MoGraph box / hexagon / triangle / tile
    VoronoiFractureSlab  = 5u, // [-] - Cracked ice / seismic basalt Voronoi slab
    FerrofluidSpikeCone  = 6u, // [-] - Magnetic Rosensweig meniscus spike cone
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct OrientedSolidCarrier
{
    SolidGeometryCategory             GeometryKind  = SolidGeometryCategory::TumblingCube;
    float                             ExtrudeGain   = 1.0f;                                            // [-] - Height / scale multiplier
    float                             MetallicRough = 0.22f;                                           // [0-1] - Surface micro-roughness
    ColourSpace::OpticalBlendEquation BlendEquation = ColourSpace::OpticalBlendEquation::PremultipliedAlpha;
};

} // namespace Frontier::ParticleSpace
