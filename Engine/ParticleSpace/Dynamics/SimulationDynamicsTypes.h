/*============================================================================================================================================
                                                      SIMULATIONDYNAMICSTYPES.H
============================================================================================================================================*/
// 🧩 Modular composable physics & dynamics configuration structs for all 16 ParticleSpace simulation solvers (zero monolithic switch)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"

#include <cstdint>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ParticleSimulationFamily : uint8_t
{
    Ballistic        = 0u, // [-] - Standard ballistic spark/pyro/weather/fluid particle (HTML kind 0)
    FlutterDebris    = 1u, // [-] - Aerodynamic flutter leaf/paper/confetti/feather (HTML kind 1)
    MolecularAtoms   = 3u, // [-] - Lennard-Jones molecular gas/liquid/crystal (HTML kind 3)
    ChemicalReaction = 4u, // [-] - Reversible A + B <-> C reaction kinetics (HTML kind 4)
    SpectacleFx      = 5u, // [-] - Specialized VFX (curl, strand, flora, ferro, fracture, shield, derez, lens, fireworks)
    BoidsFlock       = 6u, // [-] - Spatial-hash cohesion/alignment/separation + predator evasion (HTML kind 6)
};

enum class CurlManifoldMode : uint8_t
{
    Off                 = 0u, // [-] - Standard advection
    PlanarWaveField     = 1u, // [-] - Planar 3D curl waves
    SphericalPlasma     = 2u, // [-] - Spherical tangent curl plasma manifold
    BokehStreamRibbons  = 3u, // [-] - Ribbon wake bokeh streams
    SurfacePourRibbons  = 4u, // [-] - Object surface pouring ribbons
};

enum class StrandDynamicsMode : uint8_t
{
    Off               = 0u, // [-] - Inactive
    CablesChains      = 1u, // [-] - Catenary cables / metallic chains
    VinesRopes        = 2u, // [-] - Hanging vines / ropes
    FlagSail          = 3u, // [-] - Woven flags & ship sails
    GrassWheat        = 4u, // [-] - Meadow grass & golden wheat
    TallReeds         = 5u, // [-] - Tall marsh reeds & cattails
    BroadFern         = 6u, // [-] - Pinnate broad fern fronds
    CanopyTree        = 7u, // [-] - Articulated canopy branches & leaves
    UnderwaterKelp    = 8u, // [-] - Buoyant underwater giant kelp
    SeaAnemone        = 9u, // [-] - Thickened swaying sea anemone tentacles
};

enum class MoGraphGridShape : uint8_t
{
    HalftoneCircles = 0u,
    UniformBoxes    = 1u,
    HeightCubes     = 2u,
    FlatTiles       = 3u,
    Triangles       = 4u,
    Hexagons        = 5u,
    AutoMixed       = 6u,
};

enum class MoGraphEffectorDriver : uint8_t
{
    FluidSimplexWave     = 0u,
    OrbitingDualEffector = 1u,
    CurlIslands          = 2u,
    ExpandingPulseRing   = 3u,
    ForceFieldBallOnly   = 4u,
    Placeable3DEffector  = 5u,
};

enum class EffectorVolumeShape : uint8_t
{
    Sphere = 0u,
    Box    = 1u,
    Torus  = 2u,
    Disc   = 3u,
    Cone   = 4u,
};

enum class TransitionDerezMode : uint8_t
{
    Off             = 0u,
    DerezCube       = 1u,
    CubeCoins       = 2u,
    MeltAway        = 3u,
    ReverseDerez    = 4u,
    VoxelExplosion  = 5u,
    CoinStack       = 6u,
    DissolveNothing = 7u,
    DissolveButterfly = 8u,
    GlitchDissolve  = 9u,
};

enum class ShieldFormCategory : uint8_t
{
    SphereDome     = 0u,
    CircleDisc     = 1u,
    TriangleAegis  = 2u,
    SquarePane     = 3u,
    HexagonWall    = 4u,
    DiamondBarrier = 5u,
};

enum class ForceChannelMask : uint32_t
{
    None      = 0u,
    Particles = 1u << 0u,
    Debris    = 1u << 1u,
    Foliage   = 1u << 2u,
    Cloth     = 1u << 3u,
    Fluid     = 1u << 4u,
    Smoke     = 1u << 5u,
    Rigid     = 1u << 6u,
    All       = 0x7Fu,
};

[[nodiscard]] constexpr ForceChannelMask operator|(ForceChannelMask Lhs, ForceChannelMask Rhs) noexcept
{
    return static_cast<ForceChannelMask>(static_cast<uint32_t>(Lhs) | static_cast<uint32_t>(Rhs));
}

[[nodiscard]] constexpr ForceChannelMask operator&(ForceChannelMask Lhs, ForceChannelMask Rhs) noexcept
{
    return static_cast<ForceChannelMask>(static_cast<uint32_t>(Lhs) & static_cast<uint32_t>(Rhs));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct BallisticDynamicsSpec
{
    Mathematics::Axis3D DirectionAxis   { 0.0f, 0.0f, 1.0f }; // [-]     - Release cone centerline (+Z Up)
    float               SpreadRadians   = 0.5f;               // [rad]   - Cone half-angle spread
    float               SpeedMin        = 1.0f;               // [m/s]   - Initial speed minimum
    float               SpeedMax        = 3.0f;               // [m/s]   - Initial speed maximum
    float               DragPerSecond   = 0.2f;               // [1/s]   - Aerodynamic linear damping
    float               GravityFactor   = 1.0f;               // [g]     - Multiplier of 9.81 m/s^2 downward (-Z)
    float               BuoyancyAccel   = 0.0f;               // [m/s^2] - Upward thermal/bubble lift (+Z)
    float               BounceRestitution = 0.0f;             // [0-1]   - Floor collision elasticity
    float               FlutterAmp      = 0.0f;               // [-]     - Lateral leaf/paper oscillation amplitude
    float               BurstIntervalSec = 0.0f;              // [s]     - Periodic burst cadence (0 = continuous rate)
    uint32_t            BurstCount      = 120u;               // [count] - Particles released per burst
    bool                BurstAtOrigin   = false;              // [-]     - True to fire bursts from rig origin
};

struct ForceFieldLinkSpec
{
    float            WindCouplingRate = 0.0f;                          // [1/s] - Relaxation rate toward ambient wind
    ForceChannelMask ReceiverChannels = ForceChannelMask::Particles;   // [mask]
    bool             LocalMagneticActive = false;                      // [-]   - Inline magnetic dipole field (magnetic-particles)
    float            LocalMagneticStrength = 3.0f;                     // [m/s^2]
    float            LocalMagneticGuide    = 4.0f;                     // [-]
    float            LocalMagneticRadius   = 6.0f;                     // [m]
};

struct CurlManifoldSpec
{
    CurlManifoldMode Mode            = CurlManifoldMode::Off;
    float            SpatialFreq     = 0.85f; // [1/m]
    float            DriveSpeed      = 2.2f;  // [m/s]
    float            EvolutionRate   = 0.24f; // [1/s]
    float            ShellRadius     = 1.75f; // [m]
};

struct StrandConstraintSpec
{
    StrandDynamicsMode Mode               = StrandDynamicsMode::Off;
    uint32_t           SubVariant         = 0u;    // [-]   - Mode variant (e.g. flag vs sail, cable vs chain)
    uint32_t           RowCount           = 28u;   // [count]
    uint32_t           SegmentCount       = 16u;   // [count] (2..20)
    float              StiffnessRate      = 1.25f; // [1/s]
    float              TearThresholdSpeed = 0.0f;  // [m/s] (0 = unbreakable)
    float              HeightMeters       = 2.4f;  // [m]
    float              SagMeters          = 0.35f; // [m]
    float              WaveSpeedHz        = 2.2f;  // [Hz]
    float              AnemoneThickness   = 1.0f;  // [-]
    uint32_t           BeadsDrapeMode     = 0u;    // [-]   - 0 off, 1 crystal beads, 2 bamboo chimes, 3 velvet drapes
    uint32_t           JellyTendrilMode   = 0u;    // [-]   - 0 off, 1 jelly blob, 2 elastic slime tendrils
    bool               CurveDebugEnabled  = false; // [-]
};

struct BotanicalSwaySpec
{
    uint32_t SeedHeadMode      = 0u;   // [-] - 0 off, 1 dandelion puff, 2 blossom branch detach
    float    DetachmentWindMin = 2.8f; // [m/s]
    float    SwirlFlutter      = 0.6f; // [-]
};

struct SpiderwebWeaveSpec
{
    uint32_t WebMode       = 0u;    // [-]     - 0 off, 1 orb-weave, 2 attic cobweb
    uint32_t SpokeCount    = 18u;   // [count]
    uint32_t RingCount     = 14u;   // [count]
    float    SpinRate      = 1.0f;  // [-]
    float    SagMeters     = 0.16f; // [m]
    float    DewHighlight  = 0.85f; // [0-1]
};

struct FerrofluidMeniscusSpec
{
    uint32_t FerroMode         = 0u;    // [-]     - 0 off, 1 petri-dish pool, 2 3D levitated crown
    uint32_t SpikeCount        = 520u;  // [count]
    float    MagnetOrbitSpeed  = 1.35f; // [rad/s]
    bool     FreeDraggableMagnet = false;
};

struct FractureFissureSpec
{
    uint32_t CrackMode         = 0u;    // [-]   - 0 off, 1 glacial ice, 2 seismic basalt quake
    float    PropagationSpeed  = 2.6f;  // [m/s]
    float    FissureHeaveMeters= 0.44f; // [m]
};

struct ShallowWaterSplashSpec
{
    uint32_t SplashMode         = 0u;    // [-]   - 0 off, 1 puddle ripple, 2 car tyre splash, 3 mud splatter
    float    VehicleSpeed       = 4.2f;  // [m/s]
    float    TrackRadiusMeters  = 3.15f; // [m]
};

struct EffectorVolumeSpec
{
    Mathematics::Axis3D CentreMeters { 0.0f, 0.0f, 1.5f };          // [m]
    EffectorVolumeShape Shape        = EffectorVolumeShape::Sphere; // [-]
    float               SizeMeters   = 0.0f;                        // [m] (0 = inactive)
};

struct EffectorLatticeSpec
{
    bool                  Active             = false;
    MoGraphGridShape      GridShape          = MoGraphGridShape::HeightCubes;
    MoGraphEffectorDriver Driver             = MoGraphEffectorDriver::FluidSimplexWave;
    uint32_t              Columns            = 24u;
    uint32_t              Rows               = 24u;
    float                 MinScale           = 0.10f; // [-]
    float                 MaxHeightMeters    = 0.95f; // [m]
    float                 FalloffContrast    = 1.85f; // [-]
    bool                  CameraBillboarded  = false;
    bool                  BlendPlaceable     = false;
    EffectorVolumeSpec    PrimaryEffector    { { 0.0f, 0.0f, 1.5f }, EffectorVolumeShape::Sphere, 1.2f };
    EffectorVolumeSpec    SecondaryEffector  { { 2.5f, 0.0f, 1.5f }, EffectorVolumeShape::Sphere, 0.0f };
};

struct FlockBoidsSpec
{
    bool  Active           = false;
    float NeighbourRadius  = 1.4f;  // [m]
    float CohesionWeight   = 1.15f; // [-]
    float AlignmentWeight  = 1.65f; // [-]
    float SeparationWeight = 2.85f; // [-]
    float TargetSpeed      = 3.4f;  // [m/s]
    float PulseHz          = 0.0f;  // [Hz]
    float PulseDepth       = 0.0f;  // [0-1]
};

struct MolecularKineticsSpec
{
    bool  Active             = false;
    float Temperature        = 0.6f;   // [kT]
    float Epsilon            = 0.15f;  // [-] - Lennard-Jones well depth
    float Sigma              = 0.08f;  // [m] - Particle collision diameter
    float LangevinDamping    = 2.0f;   // [1/s]
    float ReactionRate       = 4.0f;   // [1/s] - A + B -> C
    float DissociationRate   = 0.3f;   // [1/s] - C -> A + B
    float ReactionRadius     = 0.12f;  // [m]
    float InitialFractionA   = 0.5f;   // [0-1]
    float FixedSubstepSec    = 0.004f; // [s]
};

struct TransitionDerezSpec
{
    TransitionDerezMode Mode             = TransitionDerezMode::Off;
    float               HoldDurationSec  = 1.2f; // [s]
    float               GlitchRateHz     = 14.0f;// [Hz]
};

struct ShieldBarrierSpec
{
    bool               Active        = false;
    ShieldFormCategory Form          = ShieldFormCategory::SphereDome;
    uint32_t           MotifBitmask  = 1u;    // [mask] - 1 Hex, 2 Circles, 4 Triangles, 8 Squares, 16 Diamonds, 32 Voronoi, 64 Stars
    float              SpinRate      = 0.20f; // [rad/s]
    float              GlitchRateHz  = 14.0f; // [Hz]
};

struct PyroSpectacleSpec
{
    uint32_t FireworkMode   = 0u;   // [-] - 0 off, 1 pyro shells, 2 spectacle sky-text/shapes
    uint32_t FlareSalvoMode = 0u;   // [-] - 0 off, 1 military IR decoy V-salvo
    float    TrailLengthSec = 0.0f; // [s] - Viscous/projectile ribbon trail mode (>0 active)
    float    BeamModeActive = 0.0f; // [-] - Coherent laser/plasma beam toggle
};

struct SimulationDynamicsProfile
{
    ParticleSimulationFamily Family         = ParticleSimulationFamily::Ballistic;
    BallisticDynamicsSpec    Ballistics     {};
    ForceFieldLinkSpec       ForceLink      {};
    CurlManifoldSpec         Curl           {};
    StrandConstraintSpec     Strands        {};
    BotanicalSwaySpec        Botanical      {};
    SpiderwebWeaveSpec       Spiderweb      {};
    FerrofluidMeniscusSpec   Ferrofluid     {};
    FractureFissureSpec      Fracture       {};
    ShallowWaterSplashSpec   WaterSplash    {};
    EffectorLatticeSpec      EffectorGrid   {};
    FlockBoidsSpec           Boids          {};
    MolecularKineticsSpec    Molecular      {};
    TransitionDerezSpec      Transition     {};
    ShieldBarrierSpec        Shield         {};
    PyroSpectacleSpec        Pyro           {};
};

struct InteractiveColliderSettings
{
    Mathematics::Axis3D BallPositionMeters   { 0.0f, 0.0f, -10.0f }; // [m]   - Collider ball centre (+Z Up)
    Mathematics::Axis3D BallVelocityMps      { 0.0f, 0.0f,   0.0f }; // [m/s] - Ball velocity
    float               BallRadiusMeters     = 0.0f;                 // [m]   - <= 0 disables collider ball
    float               ImpactImpulseScale   = 1.0f;                 // [-]   - Impulse multiplier
    float               FloorElevationMeters = 0.0f;                 // [m]   - Reference floor Z plane
    bool                SignedDistanceActive = false;                // [-]   - Enable DistanceFieldSpace collision
};

} // namespace Frontier::ParticleSpace
