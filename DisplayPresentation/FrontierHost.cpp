//============================================================================================================================================
// 📦 Frontier/DisplayPresentation/FrontierHost.cpp — Engine Bootstrap Host Implementation
//============================================================================================================================================

#include "FrontierHost.h"

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

FrontierHost::FrontierHost() noexcept
    : RunningCondition(false)
{
}

FrontierHost::~FrontierHost() noexcept
{
    Shutdown();
}

bool FrontierHost::Bootstrap(uint32_t Width, uint32_t Height) noexcept
{
    CycleConfiguration config{};
    config.FixedTimeStepΔτ            = 1.0f / 60.0f;           // 60 Hz physics
    config.MaximumAccumulatorSeconds  = 0.1f;                   // 100ms cap
    config.ViewportWidth              = Width;
    config.ViewportHeight             = Height;

    SchedulerUnit = std::make_unique<CycleScheduler>(config);
    if (!SchedulerUnit->Initialize())
    {
        return false;
    }

    RunningCondition = true;
    return true;
}

void FrontierHost::Shutdown() noexcept
{
    if (RunningCondition)
    {
        if (SchedulerUnit)
        {
            SchedulerUnit->Terminate();
            SchedulerUnit.reset();
        }
        RunningCondition = false;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                STEP EXECUTION
//------------------------------------------------------------------------------------------------------------------------

void FrontierHost::StepOnce(float DeltaSeconds) noexcept
{
    if (RunningCondition && SchedulerUnit)
    {
        SchedulerUnit->ExecuteCycle(DeltaSeconds);
    }
}

} // namespace Frontier
