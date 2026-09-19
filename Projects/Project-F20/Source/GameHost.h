//============================================================================================================================================
// 📦 Project-F20/Source/GameHost.h — Standalone Racing Game Lifecycle Host and Runtime Coordinator
//============================================================================================================================================

#pragma once

#include "../../../DisplayPresentation/FrontierHost.h"
#include "../../../DisplayPresentation/ControlCentrePanel.h"
#include "../../../DeviceExchange/WindowExchange.h"
#include "../../../DeviceExchange/InputExchange.h"
#include "../../../DisplayPresentation/FidelityClassifier.h"
#include "../../../PhysicalDynamics/WorldSpace.h"
#include "VehicleSolver.h"
#include "TrackSequence.h"
#include <memory>

namespace Frontier::ProjectF20 {

//------------------------------------------------------------------------------------------------------------------------
//                                                     GAME HOST
//------------------------------------------------------------------------------------------------------------------------

class GameHost
{
public:
    GameHost() noexcept;
    ~GameHost() noexcept;

    GameHost(const GameHost&) = delete;
    GameHost& operator=(const GameHost&) = delete;

    [[nodiscard]] bool      LaunchGame(uint32_t Width = 1920, uint32_t Height = 1080) noexcept;
    void                    TerminateGame() noexcept;

    void                    StepGameCycle(float DeltaSeconds) noexcept;

    [[nodiscard]] bool      IsRunning() const noexcept { return RunningCondition; }
    [[nodiscard]] bool      ShouldClose() const noexcept { return Window ? Window->ShouldClose() : true; }
    [[nodiscard]] const VehicleTelemetry& QueryVehicleTelemetry() const noexcept { return Vehicle->QueryTelemetry(); }
    [[nodiscard]] WindowExchange*         QueryWindow() noexcept { return Window.get(); }
    [[nodiscard]] InputExchange*          QueryInput() noexcept { return Input.get(); }

#if defined(FRONTIER_DEVELOPMENT)
    [[nodiscard]] ControlCentrePanel*     QueryControlCentre() noexcept { return ControlCentre.get(); }
#endif

    // Single unified conversion operator for vehicle speed
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    std::unique_ptr<Frontier::FrontierHost>        EngineHost;  // [host] engine bootstrap host
    std::unique_ptr<Frontier::WindowExchange>      Window;      // [window] native OS display window
    std::unique_ptr<Frontier::InputExchange>       Input;       // [input] keyboard/gamepad polling
    std::unique_ptr<Frontier::FidelityClassifier>  Fidelity;    // [fidelity] graphics quality profile
    std::unique_ptr<Frontier::WorldSpace>          World;       // [world] continuous 3D world space and scene transitions
    std::unique_ptr<VehicleSolver>                 Vehicle;     // [vehicle] vehicle dynamics solver
    std::unique_ptr<TrackSequence>                 Track;       // [track] checkpoint and lap tracking
#if defined(FRONTIER_DEVELOPMENT)
    std::unique_ptr<Frontier::ControlCentrePanel>  ControlCentre; // [control] top notch and editor preferences overlay
#endif
    bool                                           RunningCondition; // [bool] game lifecycle status
};

template<>
inline float GameHost::Convert<float>() const noexcept
{
    return Vehicle ? Vehicle->Convert<float>() : 0.0f;
}

} // namespace Frontier::ProjectF20
