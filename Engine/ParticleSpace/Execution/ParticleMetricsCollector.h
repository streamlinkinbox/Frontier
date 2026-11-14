/*============================================================================================================================================
                                                        PARTICLEMETRICSCOLLECTOR.H
============================================================================================================================================*/
// 🧩 Decodes GPU/CPU ParticleTelemetryCounters, world AABB bounds, step latency EMA & active carrier statistics

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"

#include <cstdint>

namespace Frontier::ParticleSpace::Execution
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParticleMetricsSnapshot
{
    uint32_t            AliveParticles     = 0u;    // [count] - Live particle count
    uint32_t            ActiveStrands      = 0u;    // [count] - Active strand / web bond count
    Mathematics::Axis3D BoundsMinMeters    {};      // [m]     - World AABB min (+Z Up)
    Mathematics::Axis3D BoundsMaxMeters    {};      // [m]     - World AABB max (+Z Up)
    double              LastStepMs         = 0.0;   // [ms]    - Latest simulation + render duration
    double              SmoothedStepMs     = 0.0;   // [ms]    - Exponentially smoothed step time
    uint64_t            CompletedSteps     = 0ull;  // [count] - Total executed simulation steps
    bool                GpuComputeActive   = false; // [-]     - True when Vulkan GPU compute populated telemetry
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        CLASSES
//------------------------------------------------------------------------------------------------------------------------

class ParticleMetricsCollector
{
public:
    void Reset() noexcept;

    void IngestCounters(
        const ParticleTelemetryCounters& Counters,
        double                           StepDurationMs,
        bool                             FromGpuCompute) noexcept;

    [[nodiscard]] ParticleMetricsSnapshot QuerySnapshot() const noexcept;

private:
    ParticleMetricsSnapshot LatestSnapshot {};
};

} // namespace Frontier::ParticleSpace::Execution
