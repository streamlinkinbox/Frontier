//============================================================================================================================================
// 📦 Project-Zero/Source/CpuReferenceMain.cpp — Headless CPU Reference Entry Point (sandbox stand-in)
//============================================================================================================================================
// Renders the showcase scene with the CPU ReSTIR path tracer and exports Diagnostics/ProjectZero_Showcase.ppm.
// This binary exists only as the no-GPU stand-in: the product renderer is the Vulkan + Slang stack built by
// Build/ToolchainSequence.ps1 (GameExecution.cpp). No window, no worker thread — one frame, then exit.

#include "RendererHost.h"
#include "../../../Engine/GeometricRaster/CameraProjection.h"
#include "../../../Engine/DeviceExchange/DiagnosticMetrics.h"
#include "../../../Engine/DisplayPresentation/CloudShadowStaging.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

int main(int ArgumentCount, char** ArgumentValues)
{
    double SunHour = 17.93;
    double LensYaw = 220.0;
    double LensPitch = -2.0;
    uint32_t ViewportWidth = 640;
    uint32_t ViewportHeight = 480;
    uint32_t BounceCount = 8;
    uint32_t SpatialPasses = 2;
    bool FlareEnabled = true;
    float FlareVariety = 0.0f;
    bool CloudShadows = true;
    float CloudTime = Frontier::kCloudShadowShowcaseDiorama.TimeSeconds;
    uint32_t CloudType = 2u;
    float CloudCoverage = Frontier::kCloudShadowShowcaseDiorama.Coverage;
    float CloudScale = Frontier::kCloudShadowShowcaseDiorama.Scale;
    float CloudBase = 250.0f;
    float CloudThickness = 200.0f;
    float CloudDensity = Frontier::kCloudShadowShowcaseDiorama.Density;
    Frontier::ProjectZero::SkyFogIntegrator::FogScenario FogChoice = Frontier::ProjectZero::SkyFogIntegrator::FogScenario::Morning;
    for (int i = 1; i < ArgumentCount; ++i)
    {
        std::string Arg = ArgumentValues[i];
        if (Arg == "--sun" && i + 1 < ArgumentCount)
        {
            SunHour = std::atof(ArgumentValues[++i]);
        }
        else if (Arg == "--yaw" && i + 1 < ArgumentCount)
        {
            LensYaw = std::atof(ArgumentValues[++i]);
        }
        else if (Arg == "--pitch" && i + 1 < ArgumentCount)
        {
            LensPitch = std::atof(ArgumentValues[++i]);
        }
        else if (Arg == "--width" && i + 1 < ArgumentCount)
        {
            ViewportWidth = static_cast<uint32_t>(std::atoi(ArgumentValues[++i]));
        }
        else if (Arg == "--height" && i + 1 < ArgumentCount)
        {
            ViewportHeight = static_cast<uint32_t>(std::atoi(ArgumentValues[++i]));
        }
        else if (Arg == "--bounce" && i + 1 < ArgumentCount)
        {
            BounceCount = static_cast<uint32_t>(std::atoi(ArgumentValues[++i]));
        }
        else if (Arg == "--passes" && i + 1 < ArgumentCount)
        {
            SpatialPasses = static_cast<uint32_t>(std::atoi(ArgumentValues[++i]));
        }
        else if (Arg == "--flare" && i + 1 < ArgumentCount)
        {
            FlareEnabled = std::atoi(ArgumentValues[++i]) != 0;
        }
        else if (Arg == "--flarevar" && i + 1 < ArgumentCount)
        {
            FlareVariety = static_cast<float>(std::atof(ArgumentValues[++i]));
        }
        else if (Arg == "--fog" && i + 1 < ArgumentCount)
        {
            std::string Name = ArgumentValues[++i];
            if (Name == "clear")
            {
                FogChoice = Frontier::ProjectZero::SkyFogIntegrator::FogScenario::Clear;
            }
            else if (Name == "backlit")
            {
                FogChoice = Frontier::ProjectZero::SkyFogIntegrator::FogScenario::Backlit;
            }
        }
        else if (Arg == "--cloudshadow" && i + 1 < ArgumentCount)
        {
            CloudShadows = std::atoi(ArgumentValues[++i]) != 0;
        }
        else if (Arg == "--cloudtime" && i + 1 < ArgumentCount)
        {
            CloudTime = static_cast<float>(std::atof(ArgumentValues[++i]));
        }
        else if (Arg == "--cloudtype" && i + 1 < ArgumentCount)
        {
            std::string Name = ArgumentValues[++i];
            if (Name == "stratus") CloudType = 0u;
            else if (Name == "stratocumulus") CloudType = 1u;
            else if (Name == "cumulus") CloudType = 2u;
            else if (Name == "cumulonimbus") CloudType = 3u;
            else if (Name == "altostratus") CloudType = 4u;
            else if (Name == "cirrus") CloudType = 5u;
        }
        else if (Arg == "--cloudcover" && i + 1 < ArgumentCount)
        {
            CloudCoverage = static_cast<float>(std::atof(ArgumentValues[++i]));
        }
        else if (Arg == "--cloudscale" && i + 1 < ArgumentCount)
        {
            CloudScale = static_cast<float>(std::atof(ArgumentValues[++i]));
        }
        else if (Arg == "--cloudbase" && i + 1 < ArgumentCount)
        {
            CloudBase = static_cast<float>(std::atof(ArgumentValues[++i]));
        }
        else if (Arg == "--cloudthick" && i + 1 < ArgumentCount)
        {
            CloudThickness = static_cast<float>(std::atof(ArgumentValues[++i]));
        }
        else if (Arg == "--clouddensity" && i + 1 < ArgumentCount)
        {
            CloudDensity = static_cast<float>(std::atof(ArgumentValues[++i]));
        }
        else if (Arg == "--help")
        {
            std::cout << "usage: Project-Zero [--sun H] [--yaw D] [--pitch D] [--fog clear|morning|backlit]\n"
                      << "                      [--width W] [--height H] [--bounce N] [--passes N] [--flare 0|1]\n"
                      << "                      [--flarevar 0|1|2|3] (cinematic/anamorphic/starburst/halo)\n"
                      << "                      [--cloudshadow 0|1] [--cloudtime S] [--cloudcover F]\n"
                      << "                      [--cloudscale F] [--cloudbase M] [--cloudthick M] [--clouddensity F]\n"
                      << "                      [--cloudtype stratus|stratocumulus|cumulus|cumulonimbus|altostratus|cirrus]\n"
                      << "                      (headless-only: this binary always renders and exits)\n"
                      << "       defaults face the sunset (yaw 220, pitch -2); night moon view: --sun 18.3 --yaw 0 --pitch 2\n";
            return 0;
        }
    }
    if (ViewportWidth < 8)
    {
        ViewportWidth = 8;
    }
    if (ViewportHeight < 8)
    {
        ViewportHeight = 8;
    }

    std::cout << "================================================================================\n";
    std::cout << "                 PROJECT-ZERO — RESTIR PHOTOMETRIC TEST GROUND                  \n";
    std::cout << "================================================================================\n";
    std::cout << "[Project-Zero] Showcase: soil plain, one hundred analytical shapes, sunset sky,\n";
    std::cout << "[Project-Zero] moon, stars, clouds, and ground mist through ReSTIR DI + GI.\n";

    Frontier::DiagnosticConfiguration ReportConfig{};
    ReportConfig.DestinationFolder          = "Diagnostics";
    ReportConfig.OutputFileStem             = "ProjectZero_TelemetryReport";
    ReportConfig.FileExtension              = ".md";
    ReportConfig.TimestampPrefixEnabled     = true;
    ReportConfig.ConsoleEchoEnabled         = true;
    ReportConfig.MarkdownTableFormatEnabled = true;

    Frontier::DiagnosticMetrics ReportLogger(ReportConfig);
    if (!ReportLogger.InitializeSink())
    {
        std::cerr << "[Project-Zero] Telemetry sink could not be opened; continuing with console output only.\n";
    }
    ReportLogger.RecordMessage(Frontier::DiagnosticSeverity::Information, "Bootstrap", "Project-Zero showcase starting.");

    constexpr float Deg2Rad = 3.14159265359f / 180.0f;
    Frontier::CameraProjection Camera;
    Camera.AssignSpatialLocation(Frontier::Vector3{ 0.0f, -14.0f, 2.2f });
    Camera.AssignOrientationEuler(static_cast<float>(LensPitch) * Deg2Rad, static_cast<float>(LensYaw) * Deg2Rad, 0.0f);
    Camera.AssignFieldOfView(60.0f);
    Camera.AssignAspectRatio(static_cast<float>(ViewportWidth) / static_cast<float>(ViewportHeight));

    std::cout << "[Project-Zero] Viewport: " << ViewportWidth << "x" << ViewportHeight << " pixels.\n";
    std::cout << "[Project-Zero] Sun hour " << SunHour << ", yaw " << LensYaw << " deg, pitch " << LensPitch << " deg.\n";

    std::error_code DirCode;
    std::filesystem::create_directories("Diagnostics", DirCode);
    std::string PpmPath = "Diagnostics/ProjectZero_Showcase.ppm";
    std::string PngPath = "Diagnostics/ProjectZero_Showcase.png";

    auto RenderOnce = [&](Frontier::ProjectZero::RendererHost& ActiveRenderer, const char* Label) -> double
    {
        auto StartTime = std::chrono::high_resolution_clock::now();
        ActiveRenderer.RenderShowcaseFrame(Camera, SunHour, FogChoice, SpatialPasses, BounceCount, FlareEnabled, FlareVariety,
                                            CloudShadows, CloudTime, CloudType, CloudCoverage,
                                            CloudScale, CloudBase, CloudThickness, CloudDensity);
        auto EndTime = std::chrono::high_resolution_clock::now();
        double DurationMs = std::chrono::duration<double, std::milli>(EndTime - StartTime).count();
        std::cout << "[Project-Zero] " << Label << " completed in " << DurationMs << " ms.\n";
        return DurationMs;
    };
    auto ExportArtifacts = [&](Frontier::ProjectZero::RendererHost& ActiveRenderer) -> bool
    {
        if (!ActiveRenderer.ExportShowcaseImage(PpmPath))
        {
            std::cerr << "[Project-Zero Error] Failed to export PPM image!\n";
            return false;
        }
        std::cout << "[Project-Zero] Exported raw PPM image to: " << PpmPath << "\n";
#if !defined(_WIN32)
        std::string ConvertPy3 = "python3 ../../Tools/PpmToPng.py " + PpmPath + " " + PngPath + " > /dev/null 2>&1";
        std::string ConvertPy = "python ../../Tools/PpmToPng.py " + PpmPath + " " + PngPath + " > /dev/null 2>&1";
        (void)std::system(ConvertPy3.c_str());
        if (!std::filesystem::exists(PngPath))
        {
            (void)std::system(ConvertPy.c_str());
        }
        if (std::filesystem::exists(PngPath))
        {
            std::cout << "[Project-Zero] Converted to PNG image: " << PngPath << "\n";
        }
#else
        (void)PngPath;
#endif
        return true;
    };

    Frontier::ProjectZero::RendererHost HeadlessRenderer(ViewportWidth, ViewportHeight);
    double DurationMs = RenderOnce(HeadlessRenderer, "Showcase render");
    ReportLogger.RecordMeasurement("ViewportWidth", static_cast<double>(ViewportWidth), "px");
    ReportLogger.RecordMeasurement("ViewportHeight", static_cast<double>(ViewportHeight), "px");
    ReportLogger.RecordMeasurement("RenderDurationMs", DurationMs, "ms");
    ReportLogger.RecordMeasurement("SpatialResamplingPasses", static_cast<double>(SpatialPasses), "count");
    if (!ExportArtifacts(HeadlessRenderer))
    {
        return 1;
    }
    ReportLogger.RecordMessage(Frontier::DiagnosticSeverity::Information, "Shutdown", "Project-Zero showcase completed.");
    ReportLogger.TerminateSink();
    return 0;
}
