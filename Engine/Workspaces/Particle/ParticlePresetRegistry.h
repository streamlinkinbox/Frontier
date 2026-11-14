/*============================================================================================================================================
                                                        PARTICLEPRESETREGISTRY.H
============================================================================================================================================*/
// 🧩 Headless 93-preset Particle Studio registry, 14 domain categories, shading silhouettes, color drivers, and effect descriptors

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ParticleEffectCategory : uint8_t
{
    ClothRigging     = 0u,  // [-] - Ball-interactive banners, sails, curtains, chimes, drapes, cables & vines
    BotanicalFlora     = 1u,  // [-] - Interactive grass, wheat, reeds, ferns, trees, kelp, anemones & seeds
    FerrofluidFields = 2u,  // [-] - Magnetic Rosensweig liquid-obsidian spikes & levitated crowns
    FractureSeismic   = 3u,  // [-] - Glacial ice crack propagation & seismic basalt ground fissures
    MotionLattice      = 4u,  // [-] - Effector halftone grids, extruded cube lattices & surface patterns
    CurlFilaments         = 5u,  // [-] - 3D curl waves, plasma spheres, bokeh ribbons, arcs & spiderwebs
    DerezTransitions = 6u,  // [-] - Surface lattice derez, floor shatter, coin stacks & dissolves
    SpaceDistortion  = 7u,  // [-] - Black hole accretion lensing & rising heat shimmer refraction
    ShieldTech      = 8u,  // [-] - Multi-shape hex/prism shields, radar sweeps, sonar & EMP domes
    AtmosphericSky    = 9u,  // [-] - Aurora curtains, fireworks spectacles, countermeasure flares & motes
    BallisticSparks = 10u, // [-] - Shockwaves, beams, tracers, strike sparks, 3D glass shards & arcs
    PyroFluid        = 11u, // [-] - Fire, smoke, embers, steam, volumetric water mist, bubbles & foam
    WindBlown           = 12u, // [-] - Leaves, petals, seeds, starling murmurations & butterfly boids
    StormWeather    = 13u, // [-] - Rain, snow, hail, sandstorms, tornado debris, ash & sand
    Count               = 14u, // [count] - Active non-quarantined categories (Quarantine excluded)
};

enum class ParticleAuthoringDomain : uint8_t
{
    GeneralParticle    = 0u, // [-] - Standard ballistic, pyro, atmospheric, or wind-driven particles
    StrandBotanical    = 1u, // [-] - Curve-driven cloth strips, hanging chains, vines, grass, kelp & anemones
    FerrofluidFlux     = 2u, // [-] - Rosensweig magnetic dish spikes & levitated ferrofluid crowns
    FireworksSpectacle = 3u, // [-] - Multi-volley pyrotechnic patterns & sky-writing custom text
    FractureSeismic  = 4u, // [-] - Glacial ice & seismic Voronoi ground crack propagation
    MoGraphEffector    = 5u, // [-] - Effector-modulated grids & halftone dot/tile surfaces
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
    CubeShard           = 4u,  // [-] - Tumbling cuboid fragment
    CoinDisc            = 5u,  // [-] - Metallic spinning coin disc
    BubbleDome          = 6u,  // [-] - Iridescent specular bubble
    SnowflakeCrystal    = 7u,  // [-] - Crystalline hexagonal flake
    HailstonePellet     = 8u,  // [-] - Faceted ice hailstone
    BokehBead           = 9u,  // [-] - Soft circular optical bokeh bead
    ShockwaveRing       = 10u, // [-] - Expanding Mach shockwave ring
    EmpPulseDome        = 11u, // [-] - Expanding electromagnetic pulse dome
    HologramScanline    = 12u, // [-] - Segmented digital scanline ribbon
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
    SphereVolume  = 1u, // [-] - Isotropic spherical volume or surface
    DiscPlane     = 2u, // [-] - Planar circular disc source
    BoxVolume     = 3u, // [-] - Axis-aligned rectangular box extent
    SurfaceSample = 4u, // [-] - Sampled geometric surface positions
    Count         = 5u, // [count] - Total release volume shapes
};

enum class ParticleColorDriver : uint8_t
{
    ModeDefault     = 0u,  // [-] - Preset default ramp driver
    LifetimeRatio   = 1u,  // [-] - Normalized age [0..1]
    ElapsedAge      = 2u,  // [-] - Raw particle age [s]
    SpeedMagnitude  = 3u,  // [-] - Instantaneous velocity magnitude [m/s]
    VerticalSpeed   = 4u,  // [-] - Vertical velocity component [m/s]
    AzimuthAngle    = 5u,  // [-] - Angular bearing around origin [rad]
    HeadingAngle    = 6u,  // [-] - Velocity heading angle [rad]
    OriginDistance  = 7u,  // [-] - Radial distance from origin [m]
    AltitudeHeight  = 8u,  // [-] - World elevation height [m]
    NoiseField3D    = 9u,  // [-] - Procedural 3D spatial noise
    PerParticleRng  = 10u, // [-] - Deterministic per-particle hash
    SpinOrientation = 11u, // [-] - Current roll/spin angle [rad]
    CameraDistance  = 12u, // [-] - Distance to viewer camera [m]
    ParticleScale   = 13u, // [-] - Current rendered diameter [m]
    LifeDuration    = 14u, // [-] - Assigned total lifespan [s]
    Count           = 15u, // [count] - Total color-driver categories
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
    ClassicPeony     = 0u,  // [-] - Spherical chrysanthemum / peony burst
    SkyText          = 1u,  // [-] - Custom sky-writing alphanumeric text
    HeartShape       = 2u,  // [-] - Parametric 3D heart burst
    SaturnRings      = 3u,  // [-] - Central sphere with tilted orbital ring
    FivePointStar    = 4u,  // [-] - Crisp five-point pyrotechnic star
    SmileyFace       = 5u,  // [-] - Classic smiley pyrotechnic burst
    RoyalCrown       = 6u,  // [-] - Multi-spire gold crown burst
    ButterflyWings   = 7u,  // [-] - Twin swept wing bursts
    DoubleRingWillow = 8u,  // [-] - Concentric double ring & weeping willow
    GrandFinale      = 9u,  // [-] - Synchronized multi-pattern & sky-text barrage
    Count            = 10u, // [count] - Total firework patterns
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

// Headless 8-bit sRGB RGBA color stop (zero UI / PixelSpace dependency for Shipping compatibility)
struct ParticleRgba8
{
    uint8_t Red   = 255u; // [0..255] - Red channel
    uint8_t Green = 255u; // [0..255] - Green channel
    uint8_t Blue  = 255u; // [0..255] - Blue channel
    uint8_t Alpha = 255u; // [0..255] - Alpha channel

    [[nodiscard]] constexpr bool operator==(const ParticleRgba8&) const noexcept = default;
};

// Headless simulation playback clock synchronized bidirectionally with PlaybackScrubber in UI hosts
struct ParticlePlaybackClock
{
    float CurrentSeconds  = 0.0f;  // [s]   - Active playback cursor
    float DurationSeconds = 12.0f; // [s]   - Timeline duration ceiling
    float PlaybackRate    = 1.0f;  // [x]   - Time scale multiplier
    float FixedStepHz     = 60.0f; // [Hz]  - Step rate for single-step buttons
    bool  Playing         = true;  // [-]   - True when running continuously
    bool  LoopEnabled     = true;  // [-]   - Wrap back to 0 when exceeding duration

    [[nodiscard]] constexpr bool operator==(const ParticlePlaybackClock&) const noexcept = default;
};

struct ParticleCategoryDescriptor
{
    ParticleEffectCategory Category   = ParticleEffectCategory::ClothRigging; // [-] - Category enumeration code
    const char*            FullTitle  = "";                                      // [-] - Full Outliner subgroup heading
    const char*            ShortLabel = "";                                      // [-] - Compact Outliner filter label
    const char*            TagCode    = "";                                      // [-] - Uppercase compact section tag
    ParticleRgba8          AccentTint { 232u, 194u, 122u, 255u };                // [-] - Category signature color
};

struct ParticlePresetDescriptor
{
    const char*              Identifier           = "";                                          // [-]     - Stable slug identifier
    const char*              DisplayName          = "";                                          // [-]     - Human-readable preset title
    const char*              Sublabel             = "";                                          // [-]     - Secondary row summary label
    ParticleEffectCategory   Category             = ParticleEffectCategory::BallisticSparks; // [-]     - Active non-quarantined category
    ParticleAuthoringDomain  AuthoringDomain      = ParticleAuthoringDomain::GeneralParticle;    // [-]     - Specialized inspector section
    ParticleVisualSilhouette Silhouette           = ParticleVisualSilhouette::Streak;            // [-]     - Primary particle silhouette
    ParticleBlendCategory    BlendCategory        = ParticleBlendCategory::AdditiveGlow;         // [-]     - Compositing blend mode
    ParticleReleaseVolume    ReleaseVolume        = ParticleReleaseVolume::PointOrigin;          // [-]     - Spatial birth volume
    uint32_t                 Capacity             = 1024u;                                       // [count] - Allocated particle slots
    float                    BirthRatePerSec      = 60.0f;                                       // [1/s]   - Continuous birth rate
    float                    RadiusMeters         = 0.5f;                                        // [m]     - Source volume radius
    float                    SpeedMinMs           = 1.0f;                                        // [m/s]   - Minimum initial speed
    float                    SpeedMaxMs           = 3.0f;                                        // [m/s]   - Maximum initial speed
    float                    LifeMinSec           = 1.0f;                                        // [s]     - Minimum particle lifetime
    float                    LifeMaxSec           = 2.0f;                                        // [s]     - Maximum particle lifetime
    float                    SizeStartMeters      = 0.08f;                                       // [m]     - Birth visual extent
    float                    SizeEndMeters        = 0.04f;                                       // [m]     - Death visual extent
    float                    DragCoefficient      = 0.25f;                                       // [1/s]   - Aerodynamic damping
    float                    GravityMultiplier    = 1.0f;                                        // [-]     - World gravity scalar
    uint8_t                  RiggingArchitecture  = 0u;                                          // [0..9]  - Dynamic rigging/flora mode
    uint8_t                  FerrofluidVessel     = 0u;                                          // [0..2]  - 1=Dish Spikes, 2=Orb Crown
    uint8_t                  FracturePattern      = 0u;                                          // [0..2]  - 1=Glacial Ice, 2=Ground Quake
    uint8_t                  EffectorArchitecture = 0u;                                          // [0..5]  - MoGraph effector grid mode
    uint8_t                  FireworkPattern      = 0u;                                          // [0..9]  - Pyrotechnic burst pattern
    ParticleRgba8            ColorA               { 255u, 255u, 255u, 255u };                    // [-]     - Ramp start RGBA color
    ParticleRgba8            ColorB               { 255u, 180u, 84u,  255u };                    // [-]     - Ramp end/secondary RGBA color
    ParticleRgba8            ColorC               { 255u, 235u, 140u, 255u };                    // [-]     - Ramp highlight/mid RGBA color
};

// Headless authored particle effect descriptor shared across ParticleWorkspaceSession, ParticlePresetCompiler & ParticleCodec
struct ParticleEffectDescriptor
{
    uint32_t                   UniqueToken           = 0u;                                          // [id]    - Monotonic effect token
    std::string                PresetIdentifier      {};                                            // [-]     - Source non-quarantined preset slug
    std::string                Title                 {};                                            // [-]     - Editable Outliner & Inspector heading
    std::string                Sublabel              {};                                            // [-]     - Secondary row summary text
    ParticleEffectCategory     Category              = ParticleEffectCategory::BallisticSparks; // [-]     - Active domain category
    ParticleAuthoringDomain    AuthoringDomain       = ParticleAuthoringDomain::GeneralParticle;    // [-]     - Specialized Inspector section
    bool                       Visible               = true;                                        // [-]     - Visibility / active switch

    // Release Volume & Birth Kinematics
    ParticleReleaseVolume                        ReleaseVolume     = ParticleReleaseVolume::PointOrigin; // [-] - Spatial release geometry
    SimulationSpace::ForceFields::SpatialVector3 OriginMeters      { 0.0f, 0.0f, 1.2f };                 // [m] - World anchor (+Z Up)
    SimulationSpace::ForceFields::SpatialVector3 LaunchDirection   { 0.0f, 0.0f, 1.0f };                 // [-] - Nominal release axis
    float                      RadiusMeters          = 0.5f;                                        // [m]     - Source volume radius
    float                      ConeSpreadRad         = 0.52f;                                       // [rad]   - Cone dispersion half-angle
    uint32_t                   Capacity              = 1024u;                                       // [count] - Allocated particle slots
    float                      BirthRatePerSec       = 120.0f;                                      // [1/s]   - Continuous release rate
    float                      SpeedMinMs            = 1.2f;                                        // [m/s]   - Minimum launch speed
    float                      SpeedMaxMs            = 4.2f;                                        // [m/s]   - Maximum launch speed
    float                      LifeMinSec            = 1.2f;                                        // [s]     - Minimum particle lifespan
    float                      LifeMaxSec            = 2.6f;                                        // [s]     - Maximum particle lifespan
    float                      BurstIntervalSec      = 0.0f;                                        // [s]     - Periodic burst interval (0 = off)
    uint32_t                   BurstCount            = 96u;                                         // [count] - Particles per burst pulse
    bool                       BurstOriginAnchored   = true;                                        // [-]     - Release bursts at local origin

    // Shading, Silhouette & Color Ramp
    ParticleVisualSilhouette   Silhouette            = ParticleVisualSilhouette::Streak;            // [-]     - Rendered particle silhouette
    ParticleBlendCategory      BlendCategory         = ParticleBlendCategory::AdditiveGlow;         // [-]     - Optical compositing mode
    float                      SizeStartMeters       = 0.08f;                                       // [m]     - Initial diameter
    float                      SizeEndMeters         = 0.03f;                                       // [m]     - Final diameter
    float                      SizeMultiplier        = 1.0f;                                        // [x]     - Global visual scale gain
    ParticleRgba8              ColorA                { 255u, 235u, 140u, 255u };                    // [-]     - Color ramp start stop
    ParticleRgba8              ColorB                { 255u, 89u,  13u,  255u };                    // [-]     - Color ramp end stop
    ParticleRgba8              ColorC                { 255u, 255u, 180u, 255u };                    // [-]     - Color ramp highlight stop
    ParticleColorDriver        ColorDriver           = ParticleColorDriver::LifetimeRatio;          // [-]     - Color ramp sampling driver
    ParticleRampWrapCategory   RampWrap              = ParticleRampWrapCategory::Clamp;             // [-]     - Ramp coordinate wrap mode
    ParticleRampInterpCategory RampInterp            = ParticleRampInterpCategory::Smooth;          // [-]     - Stop interpolation curve
    float                      RampPhaseOffset       = 0.0f;                                        // [-1..1] - Ramp coordinate phase shift

    // Physics & Reused C++ ForceFields Coupling
    float                      GravityMultiplier     = 1.0f;                                        // [x]     - Gravitational acceleration scale
    float                      DragCoefficient       = 0.35f;                                       // [1/s]   - Aerodynamic linear damping
    float                      ForceCouplingGain     = 1.0f;                                        // [x]     - Gain applied to ForceFieldAccumulator
    float                      CurlAmplitude         = 0.45f;                                       // [m/s]   - Local curl turbulence strength
    float                      CurlFrequency         = 0.85f;                                       // [1/m]   - Local curl spatial frequency
    float                      FloorRestitution      = 0.35f;                                       // [0-1]   - Ground bounce elasticity
    float                      SpinRateRadSec        = 1.4f;                                        // [rad/s] - Angular tumble velocity
    bool                       BallInteractionEnabled = true;                                       // [-]     - Throw-ball collision interaction
    bool                       RibbonTrailsEnabled   = false;                                       // [-]     - Tapered 3D ribbon trail switch

    // Specialized Domain Parameters (Zero Quarantine Fields)
    uint8_t                    RiggingArchitecture   = 0u;                                          // [0..9]  - Rigging & flora curve architecture
    uint32_t                   StrandRows            = 18u;                                         // [count] - Strands, strips, or stipes
    uint32_t                   CurveSegments         = 12u;                                         // [count] - Resolution segments per curve
    float                      StrandHeightMeters    = 2.2f;                                        // [m]     - Strand or frond length
    float                      CatenarySagMeters     = 0.32f;                                       // [m]     - Gravity sag / frond arch
    float                      ElasticStiffness      = 1.15f;                                       // [x]     - Restoring spring stiffness
    float                      WaveSpeedHz           = 1.6f;                                        // [Hz]    - Wind wave or current speed
    float                      TearSpeedMs           = 8.5f;                                        // [m/s]   - Impact tear threshold (0 = none)
    bool                       CurveDebugEnabled     = false;                                       // [-]     - Curve segment inspection overlay

    uint8_t                    FerrofluidVessel      = 0u;                                          // [1..2]  - 1=Petri Dish, 2=Levitated Orb
    uint32_t                   FerroSpikeCount       = 192u;                                        // [count] - Magnetic Rosensweig cusps
    float                      MagnetOrbitSpeedRadS  = 1.15f;                                       // [rad/s] - Neodymium pole orbit speed
    float                      MaxSpikeHeightMeters  = 0.92f;                                       // [m]     - Peak cusp elevation
    bool                       ManualMagnetPole      = false;                                       // [-]     - Manual pole positioning switch

    FireworkPatternCategory    FireworkPattern       = FireworkPatternCategory::GrandFinale;        // [-]     - Pyrotechnic burst pattern
    std::string                FireworkSkyText       { "2026" };                                    // [-]     - Custom sky-writing firework text

    uint8_t                    FracturePattern       = 0u;                                          // [1..2]  - 1=Glacial Ice, 2=Ground Quake
    float                      FractureDepthMeters   = 0.45f;                                       // [m]     - Fissure incision depth
    float                      CrackBranchDensity    = 1.25f;                                       // [x]     - Secondary fracture branching

    uint8_t                    EffectorArchitecture  = 0u;                                          // [1..5]  - MoGraph grid layout
    float                      EffectorFalloffMeters = 2.1f;                                        // [m]     - Spatial falloff envelope radius
    float                      EffectorExtrusionMeters = 1.4f;                                      // [m]     - Height modulation amplitude

    uint8_t                    ShieldGeometry        = 0u;                                          // [0..3]  - 0=Sphere, 1=Hex, 2=Prism, 3=Cyl
    float                      ShieldRippleSpeed     = 2.4f;                                        // [Hz]    - Impact ripple propagation rate
    float                      ShieldCellDensity     = 14.0f;                                       // [1/m]   - Tessellated lattice density

    uint8_t                    BoidsCreatureVariant  = 0u;                                          // [0..2]  - 0=Birds, 1=Butterflies, 2=Fish
    float                      BoidsPerceptionMeters = 1.6f;                                        // [m]     - Neighbour perception radius
    float                      BoidsCruiseSpeedMs    = 4.2f;                                        // [m/s]   - Target cruising velocity

    float                      TransitionHoldSec     = 0.85f;                                       // [s]     - Pre-derez solid hold duration
    float                      DistortionStrength    = 1.35f;                                       // [x]     - Gravitational/thermal lens gain

    uint32_t                   DeterministicSeed     = 2026u;                                       // [-]     - Reproducible PRNG seed
    uint32_t                   BurstTriggerCounter   = 0u;                                          // [count] - Manual burst pulse counter
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

namespace ParticlePresetRegistry
{

[[nodiscard]] std::span<const ParticleCategoryDescriptor> QueryActiveCategories() noexcept;
[[nodiscard]] const ParticleCategoryDescriptor&           QueryCategoryDescriptor(ParticleEffectCategory Category) noexcept;
[[nodiscard]] std::span<const ParticlePresetDescriptor>   QueryActivePresets() noexcept;
[[nodiscard]] const ParticlePresetDescriptor*             FindPreset(std::string_view Identifier) noexcept;
[[nodiscard]] uint32_t                                    CountCategoryPresets(ParticleEffectCategory Category) noexcept;

// 📝 Verification check confirming that none of the 9 retired Quarantine slugs exist in the C++ registry
[[nodiscard]] bool VerifySlugRetired(std::string_view RetiredSlug) noexcept;

[[nodiscard]] const char* QuerySilhouetteLabel(ParticleVisualSilhouette Silhouette) noexcept;
[[nodiscard]] const char* QueryReleaseVolumeLabel(ParticleReleaseVolume Volume) noexcept;
[[nodiscard]] const char* QueryColorDriverLabel(ParticleColorDriver Driver) noexcept;
[[nodiscard]] const char* QueryRampWrapLabel(ParticleRampWrapCategory Wrap) noexcept;
[[nodiscard]] const char* QueryRampInterpLabel(ParticleRampInterpCategory Interp) noexcept;
[[nodiscard]] const char* QueryFireworkPatternLabel(FireworkPatternCategory Pattern) noexcept;
[[nodiscard]] const char* QueryRiggingArchitectureLabel(uint8_t Architecture) noexcept;

} // namespace ParticlePresetRegistry

} // namespace Frontier::Workspaces
