//============================================================================================================================================
// 📦 Frontier/VolumetricDynamics/ParticleStructure.h — GPU Compute Particle Record and Emitter Configuration
//============================================================================================================================================

#pragma once

#include "../DeviceExchange/OrientationClassifier.h"
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                 PARTICLE RECORD
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) ParticleRecord
{
    Vector3                 SpatialLocation;                    // [m] world position coordinate
    Vector3                 LinearVelocity;                     // [m/s] ballistic velocity
    Vector4                 ColorPayload;                       // [0..1] RGBA photometric color
    float                   AgeInSeconds;                       // [s] current elapsed lifetime
    float                   LifespanInSeconds;                  // [s] maximum duration
    float                   ParticleRadius;                     // [m] rendering sphere radius
    float                   ParticleMass;                       // [kg] mass for ballistic gravity
    float                   DragCoefficient;                    // [0..1] aerodynamic air resistance

    [[nodiscard]] bool      IsAlive() const noexcept { return AgeInSeconds < LifespanInSeconds; }

    // Single unified conversion operator
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;
};

template<>
inline Vector3 ParticleRecord::Convert<Vector3>() const noexcept
{
    return SpatialLocation;
}

template<>
inline bool ParticleRecord::Convert<bool>() const noexcept
{
    return IsAlive();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                EMITTER CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

struct EmitterConfiguration
{
    Vector3                 InjectionOrigin;                    // [m] spawn position
    Vector3                 BaseVelocity;                       // [m/s] mean initial velocity
    Vector4                 InitialColor;                       // [0..1] initial RGBA color
    float                   VelocityDispersion;                 // [m/s] random velocity spread
    float                   ParticleLifespan;                   // [s] lifetime per particle
    float                   ParticleRadius;                     // [m] particle sphere radius
    float                   ParticleMass;                       // [kg] particle mass
    uint32_t                SpawnRatePerSecond;                 // [1/s] emission rate
};

} // namespace Frontier
