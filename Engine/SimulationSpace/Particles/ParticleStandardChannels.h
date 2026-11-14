/*============================================================================================================================================
                                                        PARTICLESTANDARDCHANNELS.H
============================================================================================================================================*/
// 🧩 Built-in 16B-aligned GPU-ready modular particle channels and ChannelTag<T> declarations (+Z Up Right-Hand world space)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Particles/ParticleChannelBank.h"

#include <cstdint>

namespace Frontier::SimulationSpace::Particles
{

//------------------------------------------------------------------------------------------------------------------------
//                                              BUILT-IN MODULAR CHANNEL ATTRIBUTES
//------------------------------------------------------------------------------------------------------------------------

// Core translation kinematics (32 B = 2x float4)
struct alignas(16) KinematicsChannel
{
    float PosX        = 0.0f; // [m]    - Right-Hand +X Forward
    float PosY        = 0.0f; // [m]    - Right-Hand +Y Left
    float PosZ        = 0.0f; // [m]    - Right-Hand +Z Up
    float InverseMass = 1.0f; // [1/kg] - 0 = pinned / infinite mass

    float VelX        = 0.0f; // [m/s]  - Linear velocity X
    float VelY        = 0.0f; // [m/s]  - Linear velocity Y
    float VelZ        = 0.0f; // [m/s]  - Linear velocity Z
    float DragScale   = 1.0f; // [x]    - Per-particle aerodynamic drag multiplier
};

// Lifetime & deterministic per-slot seed (16 B = 1x float4)
struct alignas(16) LifespanChannel
{
    float    AgeSeconds     = 0.0f; // [s]    - Elapsed age since release
    float    MaxLifeSeconds = 2.0f; // [s]    - Expiry lifespan (<= 0 = immortal)
    uint32_t SeedUint32     = 1u;   // [-]    - Deterministic slot PRNG seed
    uint32_t LivenessMask   = 1u;   // [bits] - Bit 0 = active, Bit 1 = settled, Bit 2 = detached
};

// Linear RGBA color, visual radius, roll & spin (32 B = 2x float4)
struct alignas(16) AppearanceChannel
{
    float ColorR        = 1.0f; // [0-1]   - Linear red
    float ColorG        = 1.0f; // [0-1]   - Linear green
    float ColorB        = 1.0f; // [0-1]   - Linear blue
    float ColorA        = 1.0f; // [0-1]   - Linear opacity

    float RadiusMeters  = 0.05f; // [m]     - Visual diameter / radius
    float RollRadians   = 0.0f;  // [rad]   - In-plane roll orientation
    float SpinRadPerSec = 0.0f;  // [rad/s] - Roll angular velocity
    float ColorRampT    = 0.0f;  // [0-1]   - Evaluated color ramp sample coordinate
};

// Full 3D quaternion orientation & angular velocity (32 B = 2x float4)
struct alignas(16) OrientationChannel
{
    float QuatX         = 0.0f; // [-]     - Unit quaternion X
    float QuatY         = 0.0f; // [-]     - Unit quaternion Y
    float QuatZ         = 0.0f; // [-]     - Unit quaternion Z
    float QuatW         = 1.0f; // [-]     - Unit quaternion W

    float AngVelX       = 0.0f; // [rad/s] - Angular velocity around X
    float AngVelY       = 0.0f; // [rad/s] - Angular velocity around Y
    float AngVelZ       = 0.0f; // [rad/s] - Angular velocity around Z
    float InertiaScalar = 1.0f; // [x]     - Rotational inertia factor
};

// XPBD / Verlet previous position & pinned rest anchor (32 B = 2x float4)
struct alignas(16) StrandAnchorChannel
{
    float PrevPosX         = 0.0f; // [m]   - Previous-step position X (Verlet)
    float PrevPosY         = 0.0f; // [m]   - Previous-step position Y (Verlet)
    float PrevPosZ         = 0.0f; // [m]   - Previous-step position Z (Verlet)
    float RestLengthMeters = 0.1f; // [m]   - Distance constraint rest length

    float AnchorX          = 0.0f; // [m]   - Rest / root anchor X
    float AnchorY          = 0.0f; // [m]   - Rest / root anchor Y
    float AnchorZ          = 0.0f; // [m]   - Rest / root anchor Z
    float PinWeight        = 0.0f; // [0-1] - 1.0 = rigidly anchored, 0.0 = free
};

// Strand, cloth grid, web & tree branch link indexing (16 B = 1x uint4)
struct alignas(16) StrandConnectivityChannel
{
    uint32_t StrandIndex    = 0u;          // [idx] - Strand / strip / limb index
    uint32_t SegmentIndex   = 0u;          // [idx] - Position along strand (0 = root)
    uint32_t LinkPrevSlot   = 0xFFFFFFFFu; // [idx] - Upstream slot index (invalid = root)
    uint32_t LinkCrossSlot  = 0xFFFFFFFFu; // [idx] - Lateral cloth / web neighbor slot (or 0xFFFFFFFFu)
};

// Boids murmuration & articulated wingbeat channel (32 B = 2x float4)
struct alignas(16) FlockSteeringChannel
{
    float    HeadingX       = 1.0f; // [-]   - Unit forward heading X
    float    HeadingY       = 0.0f; // [-]   - Unit forward heading Y
    float    HeadingZ       = 0.0f; // [-]   - Unit forward heading Z
    float    WingPhaseRad   = 0.0f; // [rad] - Articulated wing / tail flap phase

    float    SteerX         = 0.0f; // [m/s2]- Accumulated steering acceleration X
    float    SteerY         = 0.0f; // [m/s2]- Accumulated steering acceleration Y
    float    SteerZ         = 0.0f; // [m/s2]- Accumulated steering acceleration Z
    uint32_t SpeciesVariant = 0u;   // [idx] - 0=Birds, 1=Butterflies, 2=Fish
};

// Rosensweig magnetic ferrofluid dish & levitated crown channel (32 B = 2x float4)
struct alignas(16) MagneticCuspChannel
{
    float DishAnchorX      = 0.0f; // [m] - Equilibrium fluid surface anchor X
    float DishAnchorY      = 0.0f; // [m] - Equilibrium fluid surface anchor Y
    float DishAnchorZ      = 0.0f; // [m] - Equilibrium fluid surface anchor Z
    float CuspHeightMeters = 0.0f; // [m] - Current Rosensweig peak elevation

    float NormalX          = 0.0f; // [-] - Rest surface outward normal X
    float NormalY          = 0.0f; // [-] - Rest surface outward normal Y
    float NormalZ          = 1.0f; // [-] - Rest surface outward normal Z
    float Susceptibility   = 1.0f; // [x] - Local magnetic susceptibility multiplier
};

// Multi-tier Hanabi fireworks & sky-writing stroke channel (32 B = 2x float4)
struct alignas(16) PyroVolleyChannel
{
    uint32_t TierIndex         = 0u;   // [idx] - 0=Ascent rocket, 1=Primary star, 2=Secondary crackle
    float    DetonationTimeSec = 1.4f; // [s]   - Scheduled burst timestamp
    uint32_t SubBurstCount     = 24u;  // [cnt] - Secondary stars released at detonation
    uint32_t PatternCode       = 0u;   // [idx] - Firework pattern code

    float    TargetOffsetX     = 0.0f; // [m]   - Sky-writing stroke target offset X
    float    TargetOffsetY     = 0.0f; // [m]   - Sky-writing stroke target offset Y
    float    TargetOffsetZ     = 0.0f; // [m]   - Sky-writing stroke target offset Z
    float    TrailGain         = 1.0f; // [x]   - Luminous wake persistence
};

// Glacial ice & seismic basalt fracture propagation channel (32 B = 2x float4)
struct alignas(16) FractureStressChannel
{
    float    CellOriginX       = 0.0f; // [m]   - Intact fracture cell anchor X
    float    CellOriginY       = 0.0f; // [m]   - Intact fracture cell anchor Y
    float    CellOriginZ       = 0.0f; // [m]   - Intact fracture cell anchor Z
    float    CrackDepthMeters  = 0.0f; // [m]   - Current incision depth

    float    AccumulatedStress = 0.0f; // [0-1] - Normalized fracture stress progress
    uint32_t BranchId          = 0u;   // [idx] - Fracture propagation branch identifier
    uint32_t DetachedBit       = 0u;   // [0|1] - 1 once shard detaches into ballistic fall
    float    ShardScale        = 1.0f; // [x]   - Fragment scale factor
};

// MoGraph effector lattice & surface derez transition channel (32 B = 2x float4)
struct alignas(16) LatticeGridChannel
{
    float    RestGridX       = 0.0f; // [m]   - Rest lattice coordinate X
    float    RestGridY       = 0.0f; // [m]   - Rest lattice coordinate Y
    float    RestGridZ       = 0.0f; // [m]   - Rest lattice coordinate Z
    float    ExtrusionMeters = 0.0f; // [m]   - Current effector height extrusion

    uint32_t GridCoordX      = 0u;   // [idx] - Integer grid column X
    uint32_t GridCoordY      = 0u;   // [idx] - Integer grid row Y
    uint32_t GridCoordZ      = 0u;   // [idx] - Integer grid layer Z
    float    WavePhase       = 0.0f; // [rad] - Travelling wave / derez phase
};

static_assert(sizeof(KinematicsChannel)         == 32u, "KinematicsChannel must remain 32 bytes.");
static_assert(sizeof(LifespanChannel)           == 16u, "LifespanChannel must remain 16 bytes.");
static_assert(sizeof(AppearanceChannel)         == 32u, "AppearanceChannel must remain 32 bytes.");
static_assert(sizeof(OrientationChannel)        == 32u, "OrientationChannel must remain 32 bytes.");
static_assert(sizeof(StrandAnchorChannel)       == 32u, "StrandAnchorChannel must remain 32 bytes.");
static_assert(sizeof(StrandConnectivityChannel) == 16u, "StrandConnectivityChannel must remain 16 bytes.");
static_assert(sizeof(FlockSteeringChannel)      == 32u, "FlockSteeringChannel must remain 32 bytes.");
static_assert(sizeof(MagneticCuspChannel)       == 32u, "MagneticCuspChannel must remain 32 bytes.");
static_assert(sizeof(PyroVolleyChannel)         == 32u, "PyroVolleyChannel must remain 32 bytes.");
static_assert(sizeof(FractureStressChannel)     == 32u, "FractureStressChannel must remain 32 bytes.");
static_assert(sizeof(LatticeGridChannel)        == 32u, "LatticeGridChannel must remain 32 bytes.");

//------------------------------------------------------------------------------------------------------------------------
//                                                STANDARD CHANNEL TAGS
//------------------------------------------------------------------------------------------------------------------------

namespace StandardChannels
{
inline constexpr ChannelTag<KinematicsChannel> Kinematics{
    HashChannelSignature("Frontier::Particles::Kinematics"), "Kinematics", ChannelResidency::CpuAndGpu
};
inline constexpr ChannelTag<LifespanChannel> Lifespan{
    HashChannelSignature("Frontier::Particles::Lifespan"), "Lifespan", ChannelResidency::CpuAndGpu
};
inline constexpr ChannelTag<AppearanceChannel> Appearance{
    HashChannelSignature("Frontier::Particles::Appearance"), "Appearance", ChannelResidency::CpuAndGpu
};
inline constexpr ChannelTag<OrientationChannel> Orientation{
    HashChannelSignature("Frontier::Particles::Orientation"), "Orientation", ChannelResidency::CpuAndGpu
};
inline constexpr ChannelTag<StrandAnchorChannel> StrandAnchor{
    HashChannelSignature("Frontier::Particles::StrandAnchor"), "StrandAnchor", ChannelResidency::CpuAndGpu
};
inline constexpr ChannelTag<StrandConnectivityChannel> StrandConnectivity{
    HashChannelSignature("Frontier::Particles::StrandConnectivity"), "StrandConnectivity", ChannelResidency::CpuAndGpu
};
inline constexpr ChannelTag<FlockSteeringChannel> FlockSteering{
    HashChannelSignature("Frontier::Particles::FlockSteering"), "FlockSteering", ChannelResidency::CpuAndGpu
};
inline constexpr ChannelTag<MagneticCuspChannel> MagneticCusp{
    HashChannelSignature("Frontier::Particles::MagneticCusp"), "MagneticCusp", ChannelResidency::CpuAndGpu
};
inline constexpr ChannelTag<PyroVolleyChannel> PyroVolley{
    HashChannelSignature("Frontier::Particles::PyroVolley"), "PyroVolley", ChannelResidency::CpuAndGpu
};
inline constexpr ChannelTag<FractureStressChannel> FractureStress{
    HashChannelSignature("Frontier::Particles::FractureStress"), "FractureStress", ChannelResidency::CpuAndGpu
};
inline constexpr ChannelTag<LatticeGridChannel> LatticeGrid{
    HashChannelSignature("Frontier::Particles::LatticeGrid"), "LatticeGrid", ChannelResidency::CpuAndGpu
};
} // namespace StandardChannels

} // namespace Frontier::SimulationSpace::Particles
