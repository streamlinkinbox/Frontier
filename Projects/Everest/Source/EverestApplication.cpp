/*==========================================================================================================================================
                                                      EVERESTAPPLICATION.CPP
==========================================================================================================================================*/
// 🧩 Project Everest C++ runtime implementation.

#include "EverestApplication.h"

#include "EverestSimulationRecords.h"

#include <chrono>

namespace Everest
{

void EverestApplication::RegisterProjectTypes()
{
    RegisterEverestSimulationTypes(World);
}

bool EverestApplication::Initialize(const std::filesystem::path& ProjectSpacePath, std::string* OutDiagnostic)
{
    ActiveProjectPath = ProjectSpacePath;
    RegisterProjectTypes();
    if (!World.LoadStartupLevelFromProject(ProjectSpacePath, OutDiagnostic))
    {
        return false;
    }
    SimulatedFrames = 0;
    Running         = true;
    return true;
}

void EverestApplication::StepFrame(float DeltaSeconds)
{
    if (!Running)
    {
        return;
    }
    World.AdvanceSimulation(DeltaSeconds);
    ++SimulatedFrames;
}

int EverestApplication::RunMainLoop(uint64_t MaxFrames)
{
    using Clock = std::chrono::steady_clock;
    Clock::time_point Previous = Clock::now();
    while (Running)
    {
        const Clock::time_point Now = Clock::now();
        float DeltaSeconds          = std::chrono::duration<float>(Now - Previous).count();
        Previous                    = Now;
        if (DeltaSeconds <= 0.0f || DeltaSeconds > 0.25f)
        {
            DeltaSeconds = 1.0f / 60.0f;
        }
        StepFrame(DeltaSeconds);
        if (MaxFrames > 0 && SimulatedFrames >= MaxFrames)
        {
            Running = false;
        }
    }
    return 0;
}

void EverestApplication::Shutdown()
{
    Running = false;
    World.Clear();
}

} // namespace Everest
