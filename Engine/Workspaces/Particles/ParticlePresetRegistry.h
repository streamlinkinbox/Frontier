/*============================================================================================================================================
                                                        PARTICLEPRESETREGISTRY.H
============================================================================================================================================*/
// 🧩 Active non-quarantined 93-preset Particle Studio registry, 14 domain categories, shading silhouettes, and color-driver descriptors

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/PixelSpace.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ParticleEffectCategory : uint8_t
{
    ClothAndRigging     = 0u,  // [-] - Ball-interactive flags, sails, curtains, chimes, drapes, cables & vines
    FoliageAndFlora     = 1u,  // [-] - Interactive grass, wheat, reeds, ferns, trees, kelp, anemones & seeds
    FerrofluidAndFields = 2u,  // [-] - Magnetic Rosensweig liquid-obsidian spikes & levitated crowns
    FractureAndCracks   = 3u,  // [-] - Glacial ice crack propagation & seismic basalt ground fissures
    MotionAndGrids      = 4u,  // [-] - Effector halftone grids, extruded cube matrices & surface lattices
    SilkAndCurl         = 5u,  // [-] - 3D curl waves, plasma spheres, bokeh ribbons, arcs & spiderwebs
    TransitionsAndDerez = 6u,  // [-] - Surface lattice derez, floor shatter, coin stacks & dissolves
    SpaceAndDistortion  = 7u,  // [-] - Black hole accretion lensing & rising heat shimmer refraction
    ShieldsAndTech      = 8u,  // [-] - Multi-shape hex/prism shields, radar sweeps, sonar & EMP domes
    AtmosphereAndSky    = 9u,  // [-] - Aurora curtains, fireworks spectacles, countermeasure flares & motes
    BallisticsAndSparks = 10u, // [-] - Shockwaves, beams, tracers, strike sparks, 3D glass shards & arcs
    PyroAndWater        = 11u, // [-] - Fire, smoke, embers, steam, volumetric water mist, bubbles & foam
    WindBlown           = 12u, // [-] - Leaves, petals, seeds, starling murmurations & butterfly boids
    StormsAndWeather    = 13u, // [-] - Rain, snow, hail, sandstorms, tornado debris, ash & sand
    Count               = 14u, // [count] - Active non-quarantined categories (Quarantine excluded)
};

enum class ParticleAuthoringDomain : uint8_t
{
    GeneralParticle    = 0u, // [-] - Standard ballistic, pyro, atmospheric, or wind-driven particles
    RiggingAndFlora    = 1u, // [-] - Curve-driven cloth strips, hanging chains, vines, grass, kelp & anemones
    FerrofluidFlux     = 2u, // [-] - Rosensweig magnetic dish spikes & levitated ferrofluid crowns
    FireworksSpectacle = 3u, // [-] - Multi-shell pyrotechnic patterns & sky-writing custom text
    FractureAndCracks  = 4u, // [-] - Glacial ice & seismic Voronoi ground crack propagation
    MoGraphEffector    = 5u, // [-] - Effector-modulated matrix lattices & halftone dot/tile surfaces
    EnergyShield       = 6u, // [-] - Multi-shape protective barriers with impact ripples
    BoidsFlock         = 7u, // [-] - Flocking murmurations, butterfly swarms & fish schools
    TransitionDerez    = 8u, // [-] - Solid-to-fragment derez, shatter & dissolve transitions
    SpaceDistortion    = 9u, // [-] - Relativistic accretion lens & thermal refraction shimmer
};

enum class ParticleVisualSilhouette : uint8_t
{
    Streak              = 0u,  // [-] - Velocity-aligned glowing streak
    Sphere              = 1u,  // [-] - Shaded spherical grain or orb
    LeafOrPaper         = 2u,  // [-] - Tumbling botanical leaf or paper sheet
    SoftGlow            = 3u,  // [-] - Radial soft-edge incandescent puff
    ChipCube            = 4u,  // [-] - Tumbling cuboid fragment
    CoinDisc            = 5u,  // [-] - Metallic spinning coin disc
    BubbleShell         = 6u,  // [-] - Iridescent specular bubble
    SnowflakeCrystal    = 7u,  // [-] - Crystalline hexagonal flake
    HailstonePellet     = 8u,  // [-] - Faceted ice hailstone
    BokehBead           = 9u,  // [-] - Soft circular optical bokeh bead
    ShockwaveRing       = 10u, // [-] - Expanding Mach shockwave ring
    EmpPulseDome        = 11u, // [-] - Expanding electromagnetic pulse shell
    HologramScanline    = 12u, // [-] - Segmented digital scanline slice
    WingedBoid          = 13u, // [-] - Articulated bird, butterfly, or fish silhouette
    ViscousDroplet      = 14u, // [-] - Stretched viscous drip filament
    RadarOrSonarSweep   = 15u, // [-] - Tactical sweep arc or acoustic sonar ping
    EnergyShieldDome    = 16u, // [-] - Tessellated hex/prism shield barrier
    AuroraCurtain       = 17u, // [-] - Shimmering vertical geomagnetic curtain
    GlassShard          = 18u, // [-] - Multi-faceted refractive 3D glass shard
    VolumetricWaterMist = 19u, // [-] - Layered scattering water mist puff
    Count               = 20u, // [count] - Total visual silhouettes
};

enum class ParticleBlendCategory : uint8_t
{
    AdditiveGlow       = 0u, // [-] - Optical additive luminescence
    PremultipliedAlpha = 1u, // [-] - Premultiplied alpha compositing
};

enum class ParticleReleaseVolume : uint8_t
{
    PointOrigin   = 0u, // [-] - Single spatial coordinate source
    SphereVolume  = 1u, // [-] - Isotropic spherical volume or shell
    DiscPlane     = 2u, // [-] - Planar circular disc source
    BoxVolume     = 3u, // [-] - Axis-aligned rectangular box extent
    SurfaceSample = 4u, // [-] - Sampled geometric surface positions
    Count         = 5u, // [count] - Total release volume shapes
};

enum class ParticleColorByCategory : uint8_t
{
    ModeDefault    = 0u,  // [-] - Preset default ramp driver
    LifetimeRatio  = 1u,  // [-] - Normalized age [0..1]
    ElapsedAge     = 2u,  // [-] - Raw particle age [s]
    SpeedMagnitude = 3u,  // [-] - Instantaneous velocity magnitude [m/s]
    VerticalSpeed  = 4u,  // [-] - Vertical velocity component [m/s]
    AzimuthAngle   = 5u,  // [-] - Angular bearing around origin [rad]
    HeadingAngle   = 6u,  // [-] - Velocity heading angle [rad]
    OriginDistance = 7u,  // [-] - Radial distance from origin [m]
    AltitudeHeight = 8u,  // [-] - World elevation height [m]
    NoiseField3D   = 9u,  // [-] - Procedural 3D spatial noise
    PerParticleRng = 10u, // [-] - Deterministic per-particle hash
    SpinOrientation = 11u,// [-] - Current roll/spin angle [rad]
    CameraDistance = 12u, // [-] - Distance to viewer camera [m]
    ParticleScale  = 13u, // [-] - Current rendered diameter [m]
    LifeDuration   = 14u, // [-] - Assigned total lifespan [s]
    Count          = 15u, // [count] - Total color-driver categories
};

enum class ParticleRampWrapCategory : uint8_t
{
    Clamp  = 0u, // [-] - Clamp coordinates into [0, 1]
    Repeat = 1u, // [-] - Periodic wrap modulo 1
    Mirror = 2u, // [-] - Triangle mirror reflection
};

enum class ParticleRampInterpCategory : uint8_t
{
    Linear  = 0u, // [-] - Piecewise linear stop interpolation
    Smooth  = 1u, // [-] - Hermite smoothstep interpolation
    Stepped = 2u, // [-] - Quantized hard-band steps
};

enum class FireworkPatternCategory : uint8_t
{
    ClassicPeony    = 0u, // [-] - Spherical chrysanthemum / peony burst
    SkyText         = 1u, // [-] - Custom sky-writing alphanumeric text
    HeartShape      = 2u, // [-] - Parametric 3D heart shell
    SaturnRings     = 3u, // [-] - Core sphere with tilted orbital ring
    FivePointStar   = 4u, // [-] - Crisp five-point pyrotechnic star
    SmileyFace      = 5u, // [-] - Classic smiley pyrotechnic shell
    RoyalCrown      = 6u, // [-] - Multi-spire gold crown shell
    ButterflyWings  = 7u, // [-] - Twin swept wing bursts
    DoubleRingWillow = 8u,// [-] - Concentric double ring & weeping willow
    GrandFinale     = 9u, // [-] - Synchronized multi-pattern & sky-text barrage
    Count           = 10u,// [count] - Total firework patterns
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParticleCategoryDescriptor
{
    ParticleEffectCategory Category   = ParticleEffectCategory::ClothAndRigging; // [-] - Category enumeration code
    const char*            FullTitle  = "";                                      // [-] - Full Outliner subgroup heading
    const char*            ShortChip  = "";                                      // [-] - Compact Outliner filter label
    const char*            TagCode    = "";                                      // [-] - Uppercase compact section tag
    ColorRgba              AccentTint { 232u, 194u, 122u, 255u };                // [-] - Category signature color
};

struct ParticlePresetDescriptor
{
    const char*              Identifier         = "";                                          // [-]       - Stable slug identifier
    const char*              DisplayName        = "";                                          // [-]       - Human-readable preset title
    const char*              Sublabel           = "";                                          // [-]       - Secondary row summary label
    ParticleEffectCategory   Category           = ParticleEffectCategory::BallisticsAndSparks; // [-]       - Active non-quarantined category
    ParticleAuthoringDomain  AuthoringDomain    = ParticleAuthoringDomain::GeneralParticle;    // [-]       - Specialized inspector section
    ParticleVisualSilhouette Silhouette         = ParticleVisualSilhouette::Streak;            // [-]       - Primary particle silhouette
    ParticleBlendCategory    BlendCategory      = ParticleBlendCategory::AdditiveGlow;         // [-]       - Compositing blend law
    ParticleReleaseVolume    ReleaseVolume      = ParticleReleaseVolume::PointOrigin;          // [-]       - Spatial birth volume
    uint32_t                 Capacity           = 1024u;                                       // [count]   - Allocated particle slots
    float                    BirthRatePerSec    = 60.0f;                                       // [1/s]     - Continuous birth rate
    float                    RadiusMeters       = 0.5f;                                        // [m]       - Source volume radius
    float                    SpeedMinMs         = 1.0f;                                        // [m/s]     - Minimum initial speed
    float                    SpeedMaxMs         = 3.0f;                                        // [m/s]     - Maximum initial speed
    float                    LifeMinSec         = 1.0f;                                        // [s]       - Minimum particle lifetime
    float                    LifeMaxSec         = 2.0f;                                        // [s]       - Maximum particle lifetime
    float                    SizeStartMeters    = 0.08f;                                       // [m]       - Birth visual extent
    float                    SizeEndMeters      = 0.04f;                                       // [m]       - Death visual extent
    float                    DragCoefficient    = 0.25f;                                       // [1/s]     - Aerodynamic damping
    float                    GravityMultiplier  = 1.0f;                                        // [-]       - World gravity scalar
    uint8_t                  RiggingArchitecture = 0u;                                         // [0..9]    - Dynamic rigging/flora mode
    uint8_t                  FerrofluidVessel   = 0u;                                          // [0..2]    - 1=Dish Spikes, 2=Orb Crown
    uint8_t                  FracturePattern    = 0u;                                          // [0..2]    - 1=Glacial Ice, 2=Ground Quake
    uint8_t                  EffectorArchitecture = 0u;                                        // [0..5]    - MoGraph effector grid mode
    uint8_t                  FireworkPattern    = 0u;                                          // [0..9]    - Pyrotechnic shell pattern
    ColorRgba                ColorA             { 255u, 255u, 255u, 255u };                    // [-]       - Ramp start RGBA color
    ColorRgba                ColorB             { 255u, 180u, 84u,  255u };                    // [-]       - Ramp end/secondary RGBA color
    ColorRgba                ColorC             { 255u, 235u, 140u, 255u };                    // [-]       - Ramp highlight/mid RGBA color
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

namespace ParticlePresetRegistry
{

[[nodiscard]] std::span<const ParticleCategoryDescriptor> QueryActiveCategories() noexcept;
[[nodiscard]] const ParticleCategoryDescriptor&           QueryCategoryDescriptor(ParticleEffectCategory Category) noexcept;
[[nodiscard]] std::span<const ParticlePresetDescriptor>   QueryActivePresets() noexcept;
[[nodiscard]] const ParticlePresetDescriptor*             FindPresetByIdentifier(std::string_view Identifier) noexcept;
[[nodiscard]] uint32_t                                    CountPresetsInCategory(ParticleEffectCategory Category) noexcept;

// 📝 Verification check confirming that none of the 9 retired Quarantine slugs exist in the C++ registry
[[nodiscard]] bool VerifySlugExcludedFromRegistry(std::string_view RetiredSlug) noexcept;

[[nodiscard]] const char* QuerySilhouetteLabel(ParticleVisualSilhouette Silhouette) noexcept;
[[nodiscard]] const char* QueryReleaseVolumeLabel(ParticleReleaseVolume Volume) noexcept;
[[nodiscard]] const char* QueryColorByLabel(ParticleColorByCategory ColorBy) noexcept;
[[nodiscard]] const char* QueryRampWrapLabel(ParticleRampWrapCategory Wrap) noexcept;
[[nodiscard]] const char* QueryRampInterpLabel(ParticleRampInterpCategory Interp) noexcept;
[[nodiscard]] const char* QueryFireworkPatternLabel(FireworkPatternCategory Pattern) noexcept;
[[nodiscard]] const char* QueryRiggingArchitectureLabel(uint8_t Architecture) noexcept;

} // namespace ParticlePresetRegistry

} // namespace Frontier::Workspaces
