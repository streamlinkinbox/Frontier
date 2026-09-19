//============================================================================================================================================
// 📦 Frontier/DisplayPresentation/EngineExecution.cpp — Frontier Engine Standalone Execution Entry Point
//============================================================================================================================================

#include "FrontierHost.h"
#include "../DeviceExchange/DiagnosticMetrics.h"
#include <iostream>

int main(int ArgumentCount, char** ArgumentValues)
{
    (void)ArgumentCount;
    (void)ArgumentValues;

    std::cout << "[Frontier] Bootstrapping Frontier Engine Subsystems...\n";

    // Initialize Flexible Telemetry Logger (.md format)
    Frontier::DiagnosticConfiguration DiagnosticConfig{};
    DiagnosticConfig.DestinationFolder          = "Diagnostics";
    DiagnosticConfig.OutputFileStem             = "EngineTelemetryReport";
    DiagnosticConfig.FileExtension              = ".md";
    DiagnosticConfig.TimestampPrefixEnabled     = true;
    DiagnosticConfig.ConsoleEchoEnabled         = false;
    DiagnosticConfig.MarkdownTableFormatEnabled = true;

    Frontier::DiagnosticMetrics Logger(DiagnosticConfig);
    if (Logger.InitializeSink())
    {
        Logger.RecordMessage(Frontier::DiagnosticSeverity::Information, "Bootstrap", "Diagnostic sink initialized at " + Logger.QueryResolvedFilePath());
    }

    Frontier::FrontierHost HostInstance;
    if (!HostInstance.Bootstrap(1920, 1080))
    {
        Logger.RecordMessage(Frontier::DiagnosticSeverity::Fatal, "Bootstrap", "Host bootstrap failed!");
        std::cerr << "[Frontier Error] Host bootstrap failed!\n";
        return 1;
    }

    Logger.RecordMessage(Frontier::DiagnosticSeverity::Information, "Bootstrap", "Frontier Host bootstrap successful.");
    std::cout << "[Frontier] Initialization successful. Executing 10 simulation test cycles...\n";

    for (int cycle = 1; cycle <= 10; ++cycle)
    {
        HostInstance.StepOnce(1.0f / 60.0f);
        const auto& TelemetryData = HostInstance.QueryScheduler()->QueryTelemetry();
        
        Logger.RecordMeasurement("InterpolationAlpha", TelemetryData.InterpolationAlpha, "-");
        Logger.RecordMeasurement("ActiveParticles", static_cast<double>(TelemetryData.ActiveParticles), "count");
        Logger.RecordMeasurement("ActiveDeformableParticles", static_cast<double>(TelemetryData.ActiveDeformableParticles), "count");
        Logger.RecordMeasurement("ByteSpaceOccupiedBytes", static_cast<double>(TelemetryData.ByteSpaceOccupiedBytes), "B");

        std::cout << "  Cycle " << TelemetryData.CycleIndex
                  << " | α: " << TelemetryData.InterpolationAlpha
                  << " | Particles: " << TelemetryData.ActiveParticles
                  << " | Softbody Particles: " << TelemetryData.ActiveDeformableParticles
                  << " | ByteSpace Usage: " << TelemetryData.ByteSpaceOccupiedBytes << " B\n";
    }

    Logger.RecordMessage(Frontier::DiagnosticSeverity::Information, "Shutdown", "10 simulation cycles completed successfully.");
    std::cout << "[Frontier] Cycles complete. Shutting down gracefully...\n";
    HostInstance.Shutdown();
    Logger.RecordMessage(Frontier::DiagnosticSeverity::Information, "Shutdown", "Host shutdown completed.");
    Logger.TerminateSink();

    std::cout << "[Frontier] Engine terminated successfully. Telemetry written to " << Logger.QueryResolvedFilePath() << "\n";
    return 0;
}
