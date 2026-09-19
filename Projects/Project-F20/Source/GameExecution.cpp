//============================================================================================================================================
// 📦 Project-F20/Source/GameExecution.cpp — Project-F20 Standalone Executable Game Entry Point
//============================================================================================================================================

#include "GameHost.h"
#include "../../../DeviceExchange/DiagnosticMetrics.h"
#include <iostream>

int main(int ArgumentCount, char** ArgumentValues)
{
    (void)ArgumentCount;
    (void)ArgumentValues;

    std::cout << "================================================================================\n";
    std::cout << "                        PROJECT-F20 — VIRTUAL HYPERGRID                         \n";
    std::cout << "================================================================================\n";
    std::cout << "[Project-F20] Initializing standalone racing game project...\n";

    // Initialize Telemetry Logger with custom .log format
    Frontier::DiagnosticConfiguration RaceLogConfig{};
    RaceLogConfig.DestinationFolder          = "Logs/Project-F20";
    RaceLogConfig.OutputFileStem             = "RaceSessionLog";
    RaceLogConfig.FileExtension              = ".log";
    RaceLogConfig.TimestampPrefixEnabled     = true;
    RaceLogConfig.ConsoleEchoEnabled         = false;
    RaceLogConfig.MarkdownTableFormatEnabled = false;

    Frontier::DiagnosticMetrics RaceLogger(RaceLogConfig);
    if (RaceLogger.InitializeSink())
    {
        RaceLogger.RecordMessage(Frontier::DiagnosticSeverity::Information, "GameLifecycle", "Launching Project-F20 session...");
    }

    Frontier::ProjectF20::GameHost Game;
    if (!Game.LaunchGame(1920, 1080))
    {
        RaceLogger.RecordMessage(Frontier::DiagnosticSeverity::Fatal, "GameLifecycle", "Game launch failed!");
        std::cerr << "[Project-F20 Error] Failed to launch game!\n";
        return 1;
    }

    RaceLogger.RecordMessage(Frontier::DiagnosticSeverity::Information, "GameLifecycle", "Game launched successfully. Starting race simulation.");
    bool TestModeOnly = false;
    for (int i = 1; i < ArgumentCount; ++i)
    {
        std::string_view Arg(ArgumentValues[i]);
        if (Arg == "--test" || Arg == "--benchmark" || Arg == "--headless")
        {
            TestModeOnly = true;
        }
    }

    if (TestModeOnly)
    {
        std::cout << "[Project-F20] Running test suite (10 deterministic simulation cycles)...\n";
        for (int tick = 1; tick <= 10; ++tick)
        {
            Game.StepGameCycle(1.0f / 60.0f);
            const auto& Telemetry = Game.QueryVehicleTelemetry();
            
            RaceLogger.RecordMeasurement("SpeedKph", Telemetry.SpeedKilometersPerHour, "km/h");
            RaceLogger.RecordMeasurement("EngineRpm", Telemetry.EngineRevolutionsPerMinute, "RPM");

            std::cout << "  Tick " << tick
                      << " | Speed: " << static_cast<int>(Telemetry.SpeedKilometersPerHour) << " km/h"
                      << " | RPM: " << static_cast<int>(Telemetry.EngineRevolutionsPerMinute)
                      << " | Gear: " << Telemetry.CurrentGear
                      << " | Pos: (" << Telemetry.SpatialLocation.x << ", " << Telemetry.SpatialLocation.z << ")\n";
        }
    }
    else
    {
        std::cout << "[Project-F20] Entering interactive game execution loop (WASD / Inputs / Notch / ESC to quit)...\n";
        int cycleCount = 0;
        while (Game.IsRunning() && !Game.ShouldClose())
        {
            Game.StepGameCycle(1.0f / 60.0f);
            ++cycleCount;

            // In virtual / headless fallback environment, run test cycle window
            if (cycleCount >= 120 && Game.QueryWindow()->QueryNativeWindowToken() == reinterpret_cast<void*>(0xDEADBEEFULL))
            {
                std::cout << "[Project-F20] Ran 120 interactive cycles on headless display buffer. Session completed.\n";
                break;
            }
        }
    }

    RaceLogger.RecordMessage(Frontier::DiagnosticSeverity::Information, "GameLifecycle", "Race session completed successfully.");
    std::cout << "[Project-F20] Race complete. Terminating game cleanly...\n";
    Game.TerminateGame();
    RaceLogger.TerminateSink();

    std::cout << "[Project-F20] Terminated successfully. Race telemetry written to " << RaceLogger.QueryResolvedFilePath() << "\n";
    return 0;
}
