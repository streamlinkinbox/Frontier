/*============================================================================================================================================
                                                          PARTICLETINTDRIVER.H
============================================================================================================================================*/
// 🧩 Particle-only scalar channel extraction (all 15 HTML COLOR_BY_MODES) routed directly into Engine/ColourSpace/ColourRampSampler

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourRampSampler.h"
#include "Engine/ColourSpace/ColourSourceTypes.h"

#include <cstdint>
#include <string_view>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

// Matches HTML COLOR_BY_MODES codes 0..14 verbatim
enum class ParticleScalarChannel : uint8_t
{
    ModeDefault             = 0u,  // [-]   - Rig-specific default progression coordinate
    NormalizedLifetime      = 1u,  // [0-1] - ParticleAgeSeconds / AssignedLifespanSeconds
    ParticleAgeSeconds      = 2u,  // [s]   - Elapsed age since release
    VelocitySpeed           = 3u,  // [m/s] - Magnitude of particle velocity vector
    VerticalVelocity        = 4u,  // [m/s] - Vertical velocity component along world Up axis
    OriginAzimuthAngle      = 5u,  // [0-1] - Normalized azimuth angle around release origin
    VelocityHeadingAngle    = 6u,  // [0-1] - Normalized horizontal velocity heading angle
    DistanceFromOrigin      = 7u,  // [m]   - Euclidean distance from release origin
    WorldElevation          = 8u,  // [m]   - World vertical elevation coordinate
    SpatialNoise3D          = 9u,  // [0-1] - 3D spatial lattice noise sampled at particle position
    PerParticleRandomSeed   = 10u, // [0-1] - Deterministic hash derived from particle seed
    SpinOrientationAngle    = 11u, // [0-1] - Normalized spin orientation angle mod 2*pi
    DistanceToCamera        = 12u, // [m]   - Distance between particle and active camera eye
    ParticleSizeMeters      = 13u, // [m]   - Current particle diameter/radius in metres
    AssignedLifespanSeconds = 14u, // [s]   - Total assigned lifespan of particle
    Count                   = 15u, // [count] - Total particle tint driver channels
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParticleChannelSampleInput
{
    float ModeDefaultCoordinate = 0.0f; // [0-1] - Solver-supplied default progression
    float PositionX             = 0.0f; // [m]   - Particle world X (Right-Hand +Z Up)
    float PositionY             = 0.0f; // [m]   - Particle world Y (Right-Hand +Z Up)
    float PositionZ             = 0.0f; // [m]   - Particle world Z (Right-Hand +Z Up)
    float OriginX               = 0.0f; // [m]   - Release origin X
    float OriginY               = 0.0f; // [m]   - Release origin Y
    float OriginZ               = 0.0f; // [m]   - Release origin Z
    float VelocityX             = 0.0f; // [m/s] - Particle velocity X
    float VelocityY             = 0.0f; // [m/s] - Particle velocity Y
    float VelocityZ             = 0.0f; // [m/s] - Particle velocity Z
    float CameraX               = 0.0f; // [m]   - Camera eye X
    float CameraY               = 0.0f; // [m]   - Camera eye Y
    float CameraZ               = 0.0f; // [m]   - Camera eye Z
    float AgeSeconds            = 0.0f; // [s]   - Elapsed particle age
    float LifespanSeconds       = 1.0f; // [s]   - Assigned particle lifespan
    float SizeMeters            = 0.05f;// [m]   - Particle size
    float OrientationRadians    = 0.0f; // [rad] - Spin angle
    float RandomSeed            = 0.0f; // [-]   - Per-particle deterministic seed
    float ElapsedSeconds        = 0.0f; // [s]   - Global simulation elapsed clock
    float NoiseFrequency        = 0.85f;// [1/m] - 3D noise spatial frequency
    bool  WorldIsYUp            = false;// [-]   - True when caller supplies Y-Up coordinates (HTML parity)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::string_view      QueryParticleScalarChannelToken(ParticleScalarChannel Channel) noexcept;
[[nodiscard]] std::string_view      QueryParticleScalarChannelLabel(ParticleScalarChannel Channel) noexcept;
[[nodiscard]] ParticleScalarChannel ParseParticleScalarChannelToken(std::string_view Token) noexcept;

[[nodiscard]] float ExtractParticleDriverScalar(
    ParticleScalarChannel             Channel,
    const ParticleChannelSampleInput& Input) noexcept;

[[nodiscard]] ColourSpace::LinearRgba EvaluateParticleTint(
    const ColourSpace::ColourSourceProfile& Profile,
    ParticleScalarChannel                   Channel,
    const ParticleChannelSampleInput&       Input) noexcept;

} // namespace Frontier::ParticleSpace
