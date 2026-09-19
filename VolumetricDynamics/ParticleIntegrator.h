//============================================================================================================================================
// 📦 Frontier/VolumetricDynamics/ParticleIntegrator.h — GPU Compute Particle Integration with Curl Noise Turbulence
//============================================================================================================================================

#pragma once

#include "ParticleStructure.h"
#include "FluidSolver.h"
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                 PARTICLE INTEGRATOR
//------------------------------------------------------------------------------------------------------------------------

class ParticleIntegrator
{
public:
    explicit ParticleIntegrator(size_t MaximumParticleCapacity = 65536) noexcept;
    ~ParticleIntegrator() noexcept = default;

    ParticleIntegrator(const ParticleIntegrator&) = delete;
    ParticleIntegrator& operator=(const ParticleIntegrator&) = delete;

    void                    EmitParticles(const EmitterConfiguration& Emitter, uint32_t Count) noexcept;
    void                    AdvanceSimulation(float Δτ, const FluidSolver* Fluid = nullptr) noexcept;

    [[nodiscard]] size_t    QueryActiveParticleCount() const noexcept;
    [[nodiscard]] const ParticleRecord* QueryParticleSpan() const noexcept;

    // Single unified conversion operator for particle count
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    [[nodiscard]] Vector3   SampleCurlNoise(const Vector3& Position) const noexcept;

    size_t                  Capacity;                           // [count] maximum particle capacity
    std::vector<ParticleRecord> Particles;                      // [particles] contiguous particle storage
    size_t                  ActiveCount;                        // [count] currently alive particles
};

template<>
inline size_t ParticleIntegrator::Convert<size_t>() const noexcept
{
    return ActiveCount;
}

} // namespace Frontier
