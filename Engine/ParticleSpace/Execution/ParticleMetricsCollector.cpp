/*============================================================================================================================================
                                                       PARTICLEMETRICSCOLLECTOR.CPP
============================================================================================================================================*/
// 🧩 Decodes fixed-point millimeter telemetry bounds and tracks step latency EMA

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Execution/ParticleMetricsCollector.h"

#include <algorithm>

namespace Frontier::ParticleSpace::Execution
{

namespace
{

[[nodiscard]] float DecodeMm(uint32_t EncodedMm) noexcept
{
    return static_cast<float>((static_cast<double>(EncodedMm) - 1.0e6) * 0.001);
}

} // namespace

void ParticleMetricsCollector::Reset() noexcept
{
    LatestSnapshot = ParticleMetricsSnapshot{};
}

void ParticleMetricsCollector::IngestCounters(
    const ParticleTelemetryCounters& Counters,
    double                           StepDurationMs,
    bool                             FromGpuCompute) noexcept
{
    LatestSnapshot.AliveParticles   = Counters.AliveCount;
    LatestSnapshot.ActiveStrands    = Counters.ActiveStrandCount;
    LatestSnapshot.LastStepMs       = std::max(0.0, StepDurationMs);
    LatestSnapshot.GpuComputeActive = FromGpuCompute;
    ++LatestSnapshot.CompletedSteps;

    if (LatestSnapshot.CompletedSteps == 1ull)
    {
        LatestSnapshot.SmoothedStepMs = LatestSnapshot.LastStepMs;
    }
    else
    {
        LatestSnapshot.SmoothedStepMs = LatestSnapshot.SmoothedStepMs * 0.85 + LatestSnapshot.LastStepMs * 0.15;
    }

    if (Counters.AliveCount > 0u)
    {
        LatestSnapshot.BoundsMinMeters = {
            DecodeMm(Counters.BoundsMinFixedX),
            DecodeMm(Counters.BoundsMinFixedY),
            DecodeMm(Counters.BoundsMinFixedZ),
        };
        LatestSnapshot.BoundsMaxMeters = {
            DecodeMm(Counters.BoundsMaxFixedX),
            DecodeMm(Counters.BoundsMaxFixedY),
            DecodeMm(Counters.BoundsMaxFixedZ),
        };
    }
    else
    {
        LatestSnapshot.BoundsMinMeters = {};
        LatestSnapshot.BoundsMaxMeters = {};
    }
}

ParticleMetricsSnapshot ParticleMetricsCollector::QuerySnapshot() const noexcept
{
    return LatestSnapshot;
}

} // namespace Frontier::ParticleSpace::Execution
