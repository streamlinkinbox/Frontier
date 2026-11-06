/*==========================================================================================================================================
                                                       EVERESTAPPLICATION.H
==========================================================================================================================================*/
// 🧩 Project Everest C++ runtime class owning WorldSpace and the simulation main loop (matching ProjectGenerator architecture).

#pragma once

#include "Engine/Scene/WorldSpace.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Everest
{

class EverestApplication
{
  public:
    EverestApplication() = default;
    ~EverestApplication() = default;

    // 📝 LevelIdentity names one of the project's levels to open instead of its startup level (empty: the startup level).
    bool Initialize(const std::filesystem::path& ProjectSpacePath, std::string* OutDiagnostic = nullptr, const std::string& LevelIdentity = {});
    void StepFrame(float DeltaSeconds);
    int  RunMainLoop(uint64_t MaxFrames = 0);
    void Shutdown();

    [[nodiscard]] Frontier::WorldSpace&       AccessWorld() noexcept { return World; }
    [[nodiscard]] const Frontier::WorldSpace& QueryWorld() const noexcept { return World; }
    [[nodiscard]] uint64_t                    QuerySimulatedFrames() const noexcept { return SimulatedFrames; }
    [[nodiscard]] bool                        QueryRunning() const noexcept { return Running; }

  private:
    void RegisterProjectTypes();

    Frontier::WorldSpace  World;
    std::filesystem::path ActiveProjectPath;
    uint64_t              SimulatedFrames = 0;
    bool                  Running         = false;
};

} // namespace Everest
