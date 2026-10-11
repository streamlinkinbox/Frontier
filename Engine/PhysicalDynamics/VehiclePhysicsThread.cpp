//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/VehiclePhysicsThread.cpp
//============================================================================================================================================

#include "VehiclePhysicsThread.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Frontier {

using Clock = std::chrono::steady_clock;

bool VehiclePhysicsThread::Start(const VehiclePhysicsThreadConfiguration& Configuration, StepCallback OnStep) noexcept
{
    if (Running.load(std::memory_order_acquire)) return false;
    if (!OnStep)                                 return false;
    if (!(Configuration.StepHz > 0.0))           return false;

    Config   = Configuration;
    Config.MaxCatchUpSteps = std::max<uint32_t>(1u, Config.MaxCatchUpSteps);
    Callback = std::move(OnStep);

    StopRequested.store(false, std::memory_order_release);
    Running.store(true, std::memory_order_release);

    {
        std::lock_guard<std::mutex> Guard(MetricsMutex);
        Metrics = VehiclePhysicsThreadMetrics{};
        Metrics.Running = true;
    }

    Worker = std::thread([this] { Loop(); });
    return true;
}

void VehiclePhysicsThread::Stop() noexcept
{
    StopRequested.store(true, std::memory_order_release);
    if (Worker.joinable()) Worker.join();
    Running.store(false, std::memory_order_release);

    std::lock_guard<std::mutex> Guard(MetricsMutex);
    Metrics.Running = false;
}

VehiclePhysicsThreadMetrics VehiclePhysicsThread::QueryMetrics() const noexcept
{
    std::lock_guard<std::mutex> Guard(MetricsMutex);
    return Metrics;
}

void VehiclePhysicsThread::Loop() noexcept
{
    const double FixedSeconds = 1.0 / Config.StepHz;
    const auto   FixedStep    = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(FixedSeconds));
    const float  FixedDelta   = static_cast<float>(FixedSeconds);

    const auto StartTime  = Clock::now();
    auto       Previous   = StartTime;
    double     Accumulator = 0.0;             // [s] real time owed to the sim
    uint64_t   StepIndex   = 0u;

    while (!StopRequested.load(std::memory_order_acquire))
    {
        const auto   Now     = Clock::now();
        const double Elapsed = std::chrono::duration<double>(Now - Previous).count();
        Previous     = Now;
        Accumulator += Elapsed;

        // Run as many whole fixed steps as real time has accrued, capped so a stall can't spiral into an unbounded burst.
        uint32_t StepsThisWake = 0u;
        float    StepMicros     = 0.0f;
        while (Accumulator >= FixedSeconds && StepsThisWake < Config.MaxCatchUpSteps && !StopRequested.load(std::memory_order_acquire))
        {
            const auto StepStart = Clock::now();
            Callback(StepIndex, FixedDelta);
            StepMicros = std::chrono::duration<float, std::micro>(Clock::now() - StepStart).count();

            Accumulator -= FixedSeconds;
            ++StepIndex;
            ++StepsThisWake;
        }

        // Backlog beyond the cap is time we will never make up — count it and resync so we don't chase it forever.
        uint64_t Dropped = 0u;
        if (Accumulator >= FixedSeconds)
        {
            Dropped     = static_cast<uint64_t>(Accumulator / FixedSeconds);
            Accumulator = std::fmod(Accumulator, FixedSeconds);
        }

        if (StepsThisWake > 0u || Dropped > 0u)
        {
            const double RealElapsed = std::chrono::duration<double>(Now - StartTime).count();
            const double SimElapsed  = static_cast<double>(StepIndex) * FixedSeconds;

            std::lock_guard<std::mutex> Guard(MetricsMutex);
            Metrics.StepCount            = StepIndex;
            Metrics.StepsLastWake        = StepsThisWake;
            Metrics.DroppedSteps        += Dropped;
            Metrics.LastStepMicroseconds = StepMicros;
            Metrics.RealtimeRatio        = RealElapsed > 0.0 ? static_cast<float>(SimElapsed / RealElapsed) : 1.0f;
        }

        // Wait out the remainder of the current step so we hover at StepHz instead of busy-spinning the core.
        if (!Config.PinBusyWait)
        {
            const double Remaining = FixedSeconds - Accumulator;
            if (Remaining > 0.0)
            {
                // Sleep a hair short of the target to absorb OS scheduler jitter, then let the next accumulate catch it.
                const auto Nap = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(Remaining * 0.9));
                std::this_thread::sleep_for(Nap);
            }
        }
        else
        {
            std::this_thread::yield();
        }
    }
}

} // namespace Frontier
