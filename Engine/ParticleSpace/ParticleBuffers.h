/*============================================================================================================================================
                                                            PARTICLEBUFFERS.H
============================================================================================================================================*/
// 🧩 16-byte-aligned C++20 <-> .slang GPU buffer layouts for ParticleSpace compute simulation, strand ribbons, and telemetry

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t MaxRibbonStrandVertices    = 20u;    // [count] - Maximum vertices stored per ribbon/cloth/flora strand
inline constexpr uint32_t MaxActiveForceUniformSlots = 16u;    // [count] - Compact force source array capacity per dispatch
inline constexpr uint32_t WindLatticeDimX            = 24u;    // [count] - Default 3D wind texture X resolution
inline constexpr uint32_t WindLatticeDimY            = 12u;    // [count] - Default 3D wind texture Y resolution
inline constexpr uint32_t WindLatticeDimZ            = 24u;    // [count] - Default 3D wind texture Z resolution
inline constexpr uint32_t RigUniformVec4SlotCount    = 22u;    // [count] - 22 x float4 = 352 bytes matching WGSL/Slang Sys

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

// Matches .slang ParticleSlot (exact 80-byte / 5 x float4 layout)
struct alignas(16) ParticleSlot
{
    float PositionAge[4]      { 0.0f, 0.0f, 0.0f, 0.0f }; // [m, s]   - xyz world position, w accumulated age
    float VelocityLifetime[4] { 0.0f, 0.0f, 0.0f, 0.0f }; // [m/s, s] - xyz velocity, w assigned lifespan (inactive when age >= life)
    float ColourRgba[4]       { 1.0f, 1.0f, 1.0f, 1.0f }; // [0-1+]   - Linear RGBA colour output
    float Attributes[4]       { 0.05f, 0.0f, 0.0f, 0.0f };// [-]      - x size [m], y angle [rad], z spin [rad/s], w seed or species
    float PhaseTiming[4]      { 0.0f, 0.0f, 0.0f, 0.0f }; // [s]      - x transition hold time, y floor-impact time + 1, z/w carrier extras
};
static_assert(sizeof(ParticleSlot) == 80u, "ParticleSlot must remain exactly 80 bytes for GPU parity");
static_assert(alignof(ParticleSlot) == 16u, "ParticleSlot must remain 16-byte aligned");
static_assert(offsetof(ParticleSlot, VelocityLifetime) == 16u, "ParticleSlot::VelocityLifetime offset mismatch");
static_assert(offsetof(ParticleSlot, ColourRgba) == 32u, "ParticleSlot::ColourRgba offset mismatch");
static_assert(offsetof(ParticleSlot, Attributes) == 48u, "ParticleSlot::Attributes offset mismatch");
static_assert(offsetof(ParticleSlot, PhaseTiming) == 64u, "ParticleSlot::PhaseTiming offset mismatch");

// Matches .slang StrandVertex (exact 16-byte layout)
struct alignas(16) StrandVertex
{
    float PositionRadius[4] { 0.0f, 0.0f, 0.0f, 0.0f }; // [m] - xyz world position, w strand half-width / parameter
};
static_assert(sizeof(StrandVertex) == 16u, "StrandVertex must remain exactly 16 bytes");
static_assert(alignof(StrandVertex) == 16u, "StrandVertex must remain 16-byte aligned");

// Matches .slang GlobalParticleUniform (exact 240-byte / 15 x float4 layout)
struct alignas(16) GlobalParticleUniform
{
    float ViewProjection[16]     {}; // [-]      - 4x4 row-major view-projection clip matrix
    float CameraRight[4]         {}; // [-]      - xyz camera right basis, w unused
    float CameraUp[4]            {}; // [-]      - xyz camera up basis, w unused
    float CameraPosition[4]      {}; // [m]      - xyz camera eye position, w vertical fov [rad]
    float WindDomainMin[4]       {}; // [m]      - xyz wind lattice lower corner
    float WindDomainSize[4]      {}; // [m]      - xyz wind lattice box extent
    float WindDimensions[4]      {}; // [count]  - xyz wind lattice resolution
    float TimingMetrics[4]       {}; // [s]      - x dt, y elapsed time, z time scale, w step index
    float OverlaySettings[4]     {}; // [-]      - x overlay mode, y vector arrows, z curve debug, w opacity
    float CurlSwirl[4]           {}; // [-]      - x swirl strength [m/s], y spatial freq [1/m], z anim rate, w unused
    float ColliderBallPose[4]    {}; // [m]      - xyz interactive collider ball centre, w radius [m] (<=0 inactive)
    float ColliderBallVelocity[4]{}; // [m/s]    - xyz interactive collider ball velocity, w impact impulse scale
};
static_assert(sizeof(GlobalParticleUniform) == 240u, "GlobalParticleUniform must remain exactly 240 bytes");
static_assert(alignof(GlobalParticleUniform) == 16u, "GlobalParticleUniform must remain 16-byte aligned");
static_assert(offsetof(GlobalParticleUniform, CameraRight) == 64u, "CameraRight offset mismatch");
static_assert(offsetof(GlobalParticleUniform, WindDomainMin) == 112u, "WindDomainMin offset mismatch");
static_assert(offsetof(GlobalParticleUniform, ColliderBallPose) == 208u, "ColliderBallPose offset mismatch");

// Matches .slang RigParticleUniform (exact 352-byte / 22 x float4 layout matching PE.SYS_SLOT 0..21)
struct alignas(16) RigParticleUniform
{
    float OriginRadius[4]         {}; // slot 0:  xyz origin [m], w radius [m]
    float DirectionSpread[4]      {}; // slot 1:  xyz unit direction, w spread [rad]
    float SpeedDragGravity[4]     {}; // slot 2:  x speedMin, y speedMax, z drag [1/s], w gravity [g]
    float LifetimeSize[4]         {}; // slot 3:  x lifeMin, y lifeMax, z sizeStart [m], w sizeEnd [m]
    float PrimaryColour[4]        {}; // slot 4:  linear RGBA primary (colA)
    float SecondaryColour[4]      {}; // slot 5:  linear RGBA secondary (colB)
    float MidpointColour[4]       {}; // slot 6:  linear RGBA midpoint (colC)
    float SimulationStep[4]       {}; // slot 7:  x dt, y time, z frame, w simulation kind
    float FluidResponse[4]        {}; // slot 8:  x windCoupling [1/s], y bounce, z buoyancy [m/s^2], w flutter
    float ReleaseBudget[4]        {}; // slot 9:  x capacity, y originShape, z fractionA, w releaseCountThisStep
    float BoxExtentsHead[4]       {}; // slot 10: xyz box half extents [m], w head index
    float MolecularTerms[4]       {}; // slot 11: x temperature, y epsilon/curlFreq, z sigma/curlAmp, w reactRate/curlRate
    float GridLatticeTerms[4]     {}; // slot 12: x cellSize, y gridDim, z slotsPerCell, w dissociationRate
    float ReactionDamping[4]      {}; // slot 13: x reactRadius, y damping, z shellRadius, w streakThinness
    float CarrierDispatch[4]      {}; // slot 14: x head, y seedBase, z cutoutShape, w leafSubMode
    float ScaleAndModes[4]        {}; // slot 15: x sizeScale, y curlMode, z webMode, w dynMode
    float ColourRangeMeta[4]      {}; // slot 16: x stopCount, y scalarChannel, z rangeMin, w rangeMax
    float ColourWrapMeta[4]       {}; // slot 17: x repeatCount, y wrapMode, z phaseOffset, w blendCurve
    float StrandAndBlend[4]       {}; // slot 18: x strandVertices, y headScale, z headStyle, w blendEquation
    float EffectorPrimary[4]      {}; // slot 19: xyz effector A centre [m], w size [m]
    float EffectorSecondary[4]    {}; // slot 20: xyz effector B centre [m], w size [m]
    float EffectorShapes[4]       {}; // slot 21: x effector A shape, y effector B shape, z barrierForm, w barrierMask
};
static_assert(sizeof(RigParticleUniform) == 352u, "RigParticleUniform must remain exactly 352 bytes (22 x float4)");
static_assert(alignof(RigParticleUniform) == 16u, "RigParticleUniform must remain 16-byte aligned");
static_assert(offsetof(RigParticleUniform, PrimaryColour) == 64u, "PrimaryColour offset mismatch");
static_assert(offsetof(RigParticleUniform, ColourRangeMeta) == 256u, "ColourRangeMeta offset mismatch");
static_assert(offsetof(RigParticleUniform, EffectorShapes) == 336u, "EffectorShapes offset mismatch");

// Matches .slang ParticleTelemetryCounters (exact 32-byte layout)
struct alignas(16) ParticleTelemetryCounters
{
    uint32_t AliveCount        = 0u; // [count] - Active particle slots in simulation
    uint32_t ActiveStrandCount = 0u; // [count] - Active ribbon/cloth/flora strands
    uint32_t BoundsMinFixedX   = 0u; // [mm]    - Atomic fixed-point min X bound
    uint32_t BoundsMinFixedY   = 0u; // [mm]    - Atomic fixed-point min Y bound
    uint32_t BoundsMinFixedZ   = 0u; // [mm]    - Atomic fixed-point min Z bound
    uint32_t BoundsMaxFixedX   = 0u; // [mm]    - Atomic fixed-point max X bound
    uint32_t BoundsMaxFixedY   = 0u; // [mm]    - Atomic fixed-point max Y bound
    uint32_t BoundsMaxFixedZ   = 0u; // [mm]    - Atomic fixed-point max Z bound
};
static_assert(sizeof(ParticleTelemetryCounters) == 32u, "ParticleTelemetryCounters must remain exactly 32 bytes");
static_assert(alignof(ParticleTelemetryCounters) == 16u, "ParticleTelemetryCounters must remain 16-byte aligned");

} // namespace Frontier::ParticleSpace
