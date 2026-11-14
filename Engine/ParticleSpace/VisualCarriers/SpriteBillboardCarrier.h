/*============================================================================================================================================
                                                        SPRITEBILLBOARDCARRIER.H
============================================================================================================================================*/
// 🧩 Procedural camera/velocity-aligned billboard carrier supporting all 20 HTML SpriteCutout shapes (0..19) & bokeh head styles

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

// Exact 1:1 parity with HTML ParticleEditor cutout shape IDs 0..19
enum class SpriteCutoutCategory : uint8_t
{
    Streak              = 0u,  // [-] - Velocity-aligned streak / silk filament
    ShadedSphere        = 1u,  // [-] - Soft-shaded sphere disc
    BotanicalLeaf       = 2u,  // [-] - Leaf / paper / petal / feather silhouette
    SoftGlow            = 3u,  // [-] - Radial incandescent soft glow disc
    CubeProjection      = 4u,  // [-] - Projected isometric cube cutout
    MetallicCoin        = 5u,  // [-] - Spinning metallic rimmed coin
    IridescentBubble    = 6u,  // [-] - Fresnel iridescent soap bubble
    DendriticSnowflake  = 7u,  // [-] - Six-arm dendritic crystal snowflake
    CrystallineHailstone= 8u,  // [-] - Faceted icy hailstone
    BokehDisc           = 9u,  // [-] - Optical depth-of-field bokeh aperture disc
    ShockwaveRing       = 10u, // [-] - Expanding radial pressure ring
    PulseDome           = 11u, // [-] - Spherical pulse shell / EMP dome
    HologramGlitch      = 12u, // [-] - Scanline / matrix glitch fragment
    WingedSilhouette    = 13u, // [-] - Winged silhouette fallback
    ViscousDroplet      = 14u, // [-] - Surface-tension specular droplet
    RadarSonarSweep     = 15u, // [-] - Rotating radar / acoustic sonar sweep disc
    EnergyBarrierShield = 16u, // [-] - Layered geometric barrier shield motif
    AuroraCurtain       = 17u, // [-] - Vertical polar aurora ribbon curtain
    GlassShardCrystal   = 18u, // [-] - Angular refractive shard cutout
    VolumetricWaterMist = 19u, // [-] - Soft billowy mist / steam / smoke puff
    Count               = 20u, // [count] - Total procedural cutouts
};

enum class BokehApertureCategory : uint8_t
{
    Mixed      = 0u, // [-] - Per-particle mixed optical lens profiles
    SharpCore  = 1u, // [-] - Pinpoint razor focus core
    CreamyDisc = 2u, // [-] - Smooth Gaussian-like out-of-focus disc
    RimRing    = 3u, // [-] - Spherical-aberration bright rim ring
};

enum class LeafSubMode : uint8_t
{
    Leaf    = 0u, // [-] - Organic leaf silhouette
    Paper   = 1u, // [-] - Rectangular paper / confetti sheet
    Petal   = 2u, // [-] - Floral petal silhouette
    Feather = 3u, // [-] - Tapered plume feather
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SpriteBillboardCarrier
{
    SpriteCutoutCategory            CutoutShape    = SpriteCutoutCategory::Streak;          // [-]   - 0..19 procedural cutout
    LeafSubMode                     LeafStyle      = LeafSubMode::Leaf;                     // [-]   - Sub-variant when CutoutShape == BotanicalLeaf
    BokehApertureCategory           BokehAperture  = BokehApertureCategory::Mixed;          // [-]   - Head optical lens style (0..3)
    float                           HeadScale      = 1.0f;                                  // [-]   - Bokeh / head radius scale
    float                           StreakThinness = 1.0f;                                  // [-]   - Cross-section thinness (<1 = ultra-fine silk)
    ColourSpace::OpticalBlendEquation BlendEquation = ColourSpace::OpticalBlendEquation::Additive; // [-] - Blend equation
};

} // namespace Frontier::ParticleSpace
