//============================================================================================================================================
// 📦 Project-Zero/Source/GameExecution.cpp — Project-Zero Standalone ReSTIR Photometric Test Ground Entry Point
//============================================================================================================================================

#include "RendererHost.h"
#include "FlyThroughSolver.h"
#include "../../../DeviceExchange/DiagnosticMetrics.h"
#include "../../../DeviceExchange/InputExchange.h"
#include <iostream>
#include <chrono>
#include <cstdlib>

int main(int ArgumentCount, char** ArgumentValues)
{
    (void)ArgumentCount;
    (void)ArgumentValues;

    std::cout << "================================================================================\n";
    std::cout << "                 PROJECT-ZERO — RESTIR PHOTOMETRIC TEST GROUND                  \n";
    std::cout << "================================================================================\n";
    std::cout << "[Project-Zero] Initializing analytical triangle test scene, UE camera, and ReSTIR pipeline...\n";

    // Initialize Telemetry Logger with markdown table format
    Frontier::DiagnosticConfiguration ReportConfig{};
    ReportConfig.DestinationFolder          = "Diagnostics";
    ReportConfig.OutputFileStem             = "ProjectZero_TelemetryReport";
    ReportConfig.FileExtension              = ".md";
    ReportConfig.TimestampPrefixEnabled     = true;
    ReportConfig.ConsoleEchoEnabled         = false;
    ReportConfig.MarkdownTableFormatEnabled = true;

    Frontier::DiagnosticMetrics ReportLogger(ReportConfig);
    if (ReportLogger.InitializeSink())
    {
        ReportLogger.RecordMessage(Frontier::DiagnosticSeverity::Information, "Bootstrap", "Project-Zero ReSTIR test ground initialized.");
    }

    constexpr uint32_t ViewportWidth  = 640;
    constexpr uint32_t ViewportHeight = 480;

    // Initialize Unreal Engine style Fly-Through Camera
    Frontier::ProjectZero::FlyThroughConfiguration CameraConfig{
        2.5f,                   // [m/s] base speed
        3.0f,                   // [x] boost when holding Shift
        0.0025f,                // [rad/px] mouse sensitivity
        0.5f,                   // [m/s] scroll increment
        12.0f                   // damping
    };
    Frontier::ProjectZero::FlyThroughSolver Camera(CameraConfig);
    Camera.AssignSpatialLocation(Frontier::Vector3{ 0.0f, 1.0f, -1.95f });
    Camera.AssignOrientationEuler(0.0f, 0.0f, 0.0f);
    Camera.AssignFieldOfView(55.0f);
    Camera.AssignAspectRatio(static_cast<float>(ViewportWidth) / static_cast<float>(ViewportHeight));

    // Simulate Unreal Engine Viewport Input Interaction (WASD + Q/E + RMB + Scroll)
    Frontier::InputExchange Input;
    Input.AssignKeyState(Frontier::VirtualKeyCategory::KeyW, true);          // Hold W to fly forward
    Input.AssignKeyState(Frontier::VirtualKeyCategory::KeyLeftShift, true);  // Hold Shift for speed boost
    Input.AssignMouseButton(Frontier::MouseButtonCategory::ButtonRight, true); // Hold RMB for look steering
    Input.AssignCursorDelta(15.0f, -5.0f);                                   // Slight yaw right, pitch up
    Input.AssignMouseScroll(2.0f);                                           // Scroll up to increase flight speed

    // Advance camera kinematics through 5 simulation ticks
    std::cout << "[Project-Zero] Simulating Unreal-style Fly-Through Camera Navigation (WASD, Q/E, Shift, Scroll, RMB)...\n";
    for (int tick = 1; tick <= 5; ++tick)
    {
        Camera.AdvanceLocomotion(Input, 1.0f / 60.0f);
        const auto& Loc = Camera.QuerySpatialLocation();
        std::cout << "  Tick " << tick
                  << " | Cam Pos: (" << Loc.x << ", " << Loc.y << ", " << Loc.z << ")"
                  << " | Speed: " << Camera.QueryFlightSpeed() << " m/s"
                  << " | Pitch: " << (Camera.QueryPitchRadians() * 180.0f / 3.14159f) << " deg"
                  << " | Yaw: " << (Camera.QueryYawRadians() * 180.0f / 3.14159f) << " deg\n";
    }

    std::cout << "[Project-Zero] Viewport: " << ViewportWidth << "x" << ViewportHeight << " pixels.\n";
    std::cout << "[Project-Zero] Scene: Cornell Box with analytical triangle geometry & emissive ceiling luminaire.\n";
    std::cout << "[Project-Zero] Executing ReSTIR DI + ReSTIR GI from navigated camera viewpoint...\n";

    auto StartTime = std::chrono::high_resolution_clock::now();

    Frontier::ProjectZero::RendererHost Renderer(ViewportWidth, ViewportHeight);
    Renderer.RenderReSTIRFrame(Camera, 2); // 2 spatial resampling passes

    auto EndTime = std::chrono::high_resolution_clock::now();
    double DurationMs = std::chrono::duration<double, std::milli>(EndTime - StartTime).count();

    std::cout << "[Project-Zero] ReSTIR render completed in " << DurationMs << " ms.\n";

    std::string PpmPath = "Diagnostics/ProjectZero_ReSTIR_GI.ppm";
    std::string PngPath = "Diagnostics/ProjectZero_ReSTIR_GI.png";

    if (Renderer.ExportPpmImage(PpmPath))
    {
        std::cout << "[Project-Zero] Exported raw PPM image to: " << PpmPath << "\n";
        ReportLogger.RecordMessage(Frontier::DiagnosticSeverity::Information, "Renderer", "Exported PPM image to " + PpmPath);

        // Convert PPM to PNG
        std::string ConvertCmd = "python3 Tools/PpmToPng.py " + PpmPath + " " + PngPath + " > /dev/null 2>&1";
        int Result = std::system(ConvertCmd.c_str());
        if (Result == 0)
        {
            std::cout << "[Project-Zero] Converted to PNG image: " << PngPath << "\n";
            ReportLogger.RecordMessage(Frontier::DiagnosticSeverity::Information, "Renderer", "Converted to PNG at " + PngPath);
        }
    }
    else
    {
        std::cerr << "[Project-Zero Error] Failed to export PPM image!\n";
        ReportLogger.RecordMessage(Frontier::DiagnosticSeverity::Fatal, "Renderer", "Failed to export PPM image.");
    }

    ReportLogger.RecordMeasurement("ViewportWidth", ViewportWidth, "px");
    ReportLogger.RecordMeasurement("ViewportHeight", ViewportHeight, "px");
    ReportLogger.RecordMeasurement("TotalPixels", ViewportWidth * ViewportHeight, "px");
    ReportLogger.RecordMeasurement("RenderDurationMs", DurationMs, "ms");
    ReportLogger.RecordMeasurement("SpatialResamplingPasses", 2, "count");
    ReportLogger.RecordMeasurement("CameraFlightSpeed", Camera.QueryFlightSpeed(), "m/s");

    ReportLogger.RecordMessage(Frontier::DiagnosticSeverity::Information, "Shutdown", "Project-Zero test ground completed successfully.");
    ReportLogger.TerminateSink();

    std::cout << "[Project-Zero] Test complete. Telemetry report emitted to " << ReportLogger.QueryResolvedFilePath() << "\n";
    return 0;
}
