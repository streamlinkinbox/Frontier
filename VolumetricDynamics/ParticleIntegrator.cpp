//============================================================================================================================================
// 📦 Frontier/VolumetricDynamics/ParticleIntegrator.cpp — Particle Integration Implementation
//============================================================================================================================================

#include "ParticleIntegrator.h"
#include <cmath>
#include <cstdlib>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

ParticleIntegrator::ParticleIntegrator(size_t MaximumParticleCapacity) noexcept
    : Capacity(MaximumParticleCapacity)
    , Particles(MaximumParticleCapacity)
    , ActiveCount(0)
{
}

//------------------------------------------------------------------------------------------------------------------------
//                                                EMISSION OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

void ParticleIntegrator::EmitParticles(const EmitterConfiguration& Emitter, uint32_t Count) noexcept
{
    for (uint32_t i = 0; i < Count; ++i)
    {
        if (ActiveCount >= Capacity)
        {
            break;
        }

        // Random jitter
        float rx = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * 2.0f - 1.0f) * Emitter.VelocityDispersion;
        float ry = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * 2.0f - 1.0f) * Emitter.VelocityDispersion;
        float rz = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * 2.0f - 1.0f) * Emitter.VelocityDispersion;

        ParticleRecord& p = Particles[ActiveCount++];
        p.SpatialLocation     = Emitter.InjectionOrigin;
        p.LinearVelocity      = Emitter.BaseVelocity + Vector3{ rx, ry, rz };
        p.ColorPayload        = Emitter.InitialColor;
        p.AgeInSeconds        = 0.0f;
        p.LifespanInSeconds   = Emitter.ParticleLifespan;
        p.ParticleRadius      = Emitter.ParticleRadius;
        p.ParticleMass        = Emitter.ParticleMass;
        p.DragCoefficient     = 0.05f;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                              INTEGRATION OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

Vector3 ParticleIntegrator::SampleCurlNoise(const Vector3& Position) const noexcept
{
    // Analytic incompressible divergence-free curl potential field
    float sx = std::sin(Position.y * 2.0f);
    float sy = std::cos(Position.z * 2.0f);
    float sz = std::sin(Position.x * 2.0f);
    return Vector3{ sy - sz, sz - sx, sx - sy } * 0.5f;
}

void ParticleIntegrator::AdvanceSimulation(float Δτ, const FluidSolver* Fluid) noexcept
{
    if (Δτ <= 0.0f || ActiveCount == 0)
    {
        return;
    }

    size_t AliveCursor = 0;
    for (size_t i = 0; i < ActiveCount; ++i)
    {
        auto& p = Particles[i];
        p.AgeInSeconds += Δτ;

        if (p.IsAlive())
        {
            // Curl noise turbulence
            Vector3 Curl = SampleCurlNoise(p.SpatialLocation);

            // Fluid advection coupling
            Vector3 FluidVel{ 0.0f, 0.0f, 0.0f };
            if (Fluid)
            {
                int32_t fx = static_cast<int32_t>(p.SpatialLocation.x);
                int32_t fy = static_cast<int32_t>(p.SpatialLocation.y);
                int32_t fz = static_cast<int32_t>(p.SpatialLocation.z);
                if (fx >= 0 && fy >= 0 && fz >= 0)
                {
                    FluidVel = Fluid->QueryVelocity(static_cast<uint32_t>(fx), static_cast<uint32_t>(fy), static_cast<uint32_t>(fz));
                }
            }

            // Ballistic equation: v += (g + curl + fluid_force) * Δτ
            Vector3 Gravity{ 0.0f, -9.81f, 0.0f };
            p.LinearVelocity += (Gravity + Curl * 2.0f + FluidVel * 5.0f) * Δτ;
            p.LinearVelocity *= (1.0f - p.DragCoefficient * Δτ);

            p.SpatialLocation += p.LinearVelocity * Δτ;

            // Compact live particles
            if (AliveCursor != i)
            {
                Particles[AliveCursor] = p;
            }
            ++AliveCursor;
        }
    }
    ActiveCount = AliveCursor;
}

size_t ParticleIntegrator::QueryActiveParticleCount() const noexcept
{
    return ActiveCount;
}

const ParticleRecord* ParticleIntegrator::QueryParticleSpan() const noexcept
{
    return Particles.data();
}

} // namespace Frontier
