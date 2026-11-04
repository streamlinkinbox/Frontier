/*==========================================================================================================================================
                                                       EVERESTAPPLICATION.H
==========================================================================================================================================*/
// 🧩 Project Everest C++ runtime class owning SceneWorld and the simulation main loop (matching ProjectGenerator architecture).

#pragma once

#include "Engine/Scene/SceneWorld.h"

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

    bool Initialize(const std::filesystem::path& ProjectSpacePath, std::string* OutDiagnostic = nullptr);
    void StepFrame(float DeltaSeconds);
    int  RunMainLoop(uint64_t MaxFrames = 0);
    void Shutdown();

    [[nodiscard]] Frontier::SceneWorld&       AccessWorld() noexcept { return World; }
    [[nodiscard]] const Frontier::SceneWorld& QueryWorld() const noexcept { return World; }
    [[nodiscard]] uint64_t                    QuerySimulatedFrames() const noexcept { return SimulatedFrames; }
    [[nodiscard]] bool                        QueryRunning() const noexcept { return Running; }

  private:
    void RegisterProjectTypes();

    Frontier::SceneWorld  World;
    std::filesystem::path ActiveProjectPath;
    uint64_t              SimulatedFrames = 0;
    bool                  Running         = false;
};

} // namespace Everest
