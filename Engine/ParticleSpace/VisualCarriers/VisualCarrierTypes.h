/*============================================================================================================================================
                                                          VISUALCARRIERTYPES.H
============================================================================================================================================*/
// 🧩 Unified VisualCarrierCategory & std::variant<...> covering all 7 modular ParticleSpace render-geometry carriers

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/VisualCarriers/BackingSurfaceCarrier.h"
#include "Engine/ParticleSpace/VisualCarriers/DielectricBeamCarrier.h"
#include "Engine/ParticleSpace/VisualCarriers/OrientedSolidCarrier.h"
#include "Engine/ParticleSpace/VisualCarriers/RefractionLensCarrier.h"
#include "Engine/ParticleSpace/VisualCarriers/RibbonStrandCarrier.h"
#include "Engine/ParticleSpace/VisualCarriers/SpriteBillboardCarrier.h"
#include "Engine/ParticleSpace/VisualCarriers/WingedCreatureCarrier.h"

#include <cstdint>
#include <string_view>
#include <variant>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class VisualCarrierCategory : uint8_t
{
    SpriteBillboard = 0u, // [-] - Procedural 2D/3D billboard cutouts (0..19)
    RibbonStrand    = 1u, // [-] - 20-vertex continuous ribbon & cloth/flora strips
    OrientedSolid   = 2u, // [-] - True 3D solid instances & MoGraph prisms
    WingedCreature  = 3u, // [-] - Articulated 3D flapping wings & swimming fins
    DielectricBeam  = 4u, // [-] - Arcs, beams, spiderweb/molecular bonds & tubes
    RefractionLens  = 5u, // [-] - Gravitational lens & thermal shimmer distortion
    BackingSurface  = 6u, // [-] - Backing surface + vehicle/collider geometry
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

using VisualCarrierSpec = std::variant<
    SpriteBillboardCarrier,
    RibbonStrandCarrier,
    OrientedSolidCarrier,
    WingedCreatureCarrier,
    DielectricBeamCarrier,
    RefractionLensCarrier,
    BackingSurfaceCarrier>;

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] constexpr VisualCarrierCategory QueryVisualCarrierCategory(const VisualCarrierSpec& Carrier) noexcept
{
    return static_cast<VisualCarrierCategory>(Carrier.index());
}

[[nodiscard]] constexpr std::string_view QueryVisualCarrierCategoryToken(VisualCarrierCategory Category) noexcept
{
    switch (Category)
    {
    case VisualCarrierCategory::SpriteBillboard: return "SpriteBillboard";
    case VisualCarrierCategory::RibbonStrand:    return "RibbonStrand";
    case VisualCarrierCategory::OrientedSolid:   return "OrientedSolid";
    case VisualCarrierCategory::WingedCreature:  return "WingedCreature";
    case VisualCarrierCategory::DielectricBeam:  return "DielectricBeam";
    case VisualCarrierCategory::RefractionLens:  return "RefractionLens";
    case VisualCarrierCategory::BackingSurface:  return "BackingSurface";
    default:                                     return "SpriteBillboard";
    }
}

[[nodiscard]] constexpr std::string_view QuerySpriteCutoutLabel(SpriteCutoutCategory Shape) noexcept
{
    switch (Shape)
    {
    case SpriteCutoutCategory::Streak:               return "Streak";
    case SpriteCutoutCategory::ShadedSphere:         return "Sphere";
    case SpriteCutoutCategory::BotanicalLeaf:        return "Leaf";
    case SpriteCutoutCategory::SoftGlow:             return "Soft Glow";
    case SpriteCutoutCategory::CubeProjection:       return "Cube";
    case SpriteCutoutCategory::MetallicCoin:         return "Coin";
    case SpriteCutoutCategory::IridescentBubble:     return "Bubble";
    case SpriteCutoutCategory::DendriticSnowflake:   return "Snowflake";
    case SpriteCutoutCategory::CrystallineHailstone: return "Hailstone";
    case SpriteCutoutCategory::BokehDisc:            return "Bokeh";
    case SpriteCutoutCategory::ShockwaveRing:        return "Shockwave";
    case SpriteCutoutCategory::PulseDome:            return "Pulse Dome";
    case SpriteCutoutCategory::HologramGlitch:       return "Hologram";
    case SpriteCutoutCategory::WingedSilhouette:     return "Winged";
    case SpriteCutoutCategory::ViscousDroplet:       return "Droplet";
    case SpriteCutoutCategory::RadarSonarSweep:      return "Radar Sweep";
    case SpriteCutoutCategory::EnergyBarrierShield:  return "Shield";
    case SpriteCutoutCategory::AuroraCurtain:        return "Aurora";
    case SpriteCutoutCategory::GlassShardCrystal:    return "Glass Shard";
    case SpriteCutoutCategory::VolumetricWaterMist:  return "Water Mist";
    default:                                         return "Streak";
    }
}

} // namespace Frontier::ParticleSpace
