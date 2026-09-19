//============================================================================================================================================
// 📦 Project-F20/Source/GameHost.cpp — Racing Game Host Implementation
//============================================================================================================================================

#include "GameHost.h"

namespace Frontier::ProjectF20 {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

GameHost::GameHost() noexcept
    : RunningCondition(false)
{
}

GameHost::~GameHost() noexcept
{
    TerminateGame();
}

bool GameHost::LaunchGame(uint32_t Width, uint32_t Height) noexcept
{
    EngineHost = std::make_unique<Frontier::FrontierHost>();
    if (!EngineHost->Bootstrap(Width, Height))
    {
        return false;
    }

    Window = std::make_unique<Frontier::WindowExchange>();
    Frontier::WindowConfiguration windowConfig{ Width, Height, "Project-F20 — Virtual Hypergrid", false, true };
    if (!Window->OpenDisplayWindow(windowConfig))
    {
        return false;
    }

    Input    = std::make_unique<Frontier::InputExchange>();
    Fidelity = std::make_unique<Frontier::FidelityClassifier>();
    Fidelity->AssignCategory(Frontier::FidelityCategory::UltraFidelity);

#if defined(FRONTIER_DEVELOPMENT)
    ControlCentre = std::make_unique<Frontier::ControlCentrePanel>();
#endif

    World    = std::make_unique<Frontier::WorldSpace>();
    Frontier::WorldDescriptor circuitAlpha{
        "track_01_neon_hypergrid",
        "Neon Hypergrid Circuit Alpha",
        Frontier::Vector3{ 0.0f, 0.5f, 0.0f },
        Frontier::Vector3{ 0.6f, 0.7f, 0.3f }.Normalized(),
        Frontier::Vector3{ 12.0f, 11.5f, 10.0f },
        8
    };
    World->RegisterWorld(circuitAlpha);

    VehicleConfiguration carConfig{
        1150.0f,                // Chassis mass 1150 kg
        850.0f,                 // 850 Nm peak engine torque
        4500.0f,                // 4500 Nm brake torque
        0.58f,                  // 33 degrees steering lock
        2.8f,                   // High aero downforce
        0.28f,                  // Suspension rest length
        65000.0f,               // 65 kN/m spring stiffness
        4500.0f,                // 4.5 kN*s/m damper rate
        1.45f                   // 1.45 peak lateral grip coefficient
    };
    Vehicle = std::make_unique<VehicleSolver>(carConfig);

    Track = std::make_unique<TrackSequence>();
    Track->AppendWaypoint(Frontier::Vector3{ 0.0f, 0.0f, 100.0f }, 25.0f);
    Track->AppendWaypoint(Frontier::Vector3{ 150.0f, 0.0f, 250.0f }, 25.0f);
    Track->AppendWaypoint(Frontier::Vector3{ 300.0f, 0.0f, 100.0f }, 25.0f);
    Track->AppendWaypoint(Frontier::Vector3{ 0.0f, 0.0f, 0.0f }, 25.0f);

    RunningCondition = true;
    return true;
}

void GameHost::TerminateGame() noexcept
{
    if (RunningCondition)
    {
        if (Window)
        {
            Window->CloseDisplayWindow();
        }
        if (EngineHost)
        {
            EngineHost->Shutdown();
        }
        RunningCondition = false;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                GAME CYCLE STEPPING
//------------------------------------------------------------------------------------------------------------------------

void GameHost::StepGameCycle(float DeltaSeconds) noexcept
{
    if (!RunningCondition)
    {
        return;
    }

    Window->PollEvents(Input.get());
    if (Window->ShouldClose() || Input->IsKeyPressed(Frontier::VirtualKeyCategory::KeyEscape))
    {
        RunningCondition = false;
        return;
    }

    Input->PollInputDevices();

#if defined(FRONTIER_DEVELOPMENT)
    if (ControlCentre)
    {
        ControlCentre->AdvanceInteraction(*Input, Input->QueryCursorPositionX(), Input->QueryCursorPositionY());
        ControlCentre->AdvanceLocomotion(DeltaSeconds);
    }
#endif

    // Vehicle controls (steering, throttle, braking)
    float Steer = 0.0f;
    float Throttle = 0.85f;
    float Brake = 0.0f;

    if (Input->IsKeyPressed(Frontier::VirtualKeyCategory::KeyA)) Steer -= 1.0f;
    if (Input->IsKeyPressed(Frontier::VirtualKeyCategory::KeyD)) Steer += 1.0f;
    if (Input->IsKeyPressed(Frontier::VirtualKeyCategory::KeyW)) Throttle = 1.0f;
    if (Input->IsKeyPressed(Frontier::VirtualKeyCategory::KeyS)) Brake = 1.0f;

    Vehicle->AdvanceVehicle(Steer, Throttle, Brake, DeltaSeconds);
    Track->AdvanceTracking(Vehicle->QueryTelemetry().SpatialLocation, DeltaSeconds);
    World->AdvanceWorld(DeltaSeconds);

    // Step engine 14-phase cycle
    EngineHost->StepOnce(DeltaSeconds);
}

} // namespace Frontier::ProjectF20
