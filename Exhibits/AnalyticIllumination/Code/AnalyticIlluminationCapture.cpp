/*==========================================================================================================================================
                                                    ANALYTICILLUMINATIONCAPTURE.CPP
==========================================================================================================================================*/
// 🧩 The analytic illumination in the editor's own scene viewport, offscreen: the proving ground through the global illumination and
//    through the analytic renderer from the same camera, the sun's shadow and sky response, the hand-back to the global illumination,
//    and the refresh times either way.

// 📝 Usage: AnalyticIlluminationCapture <GlobalIllumination shaders> <Environment shaders> <ProvingGround> <CelestialMaps> <out>
//    [quality] [width] [height] [refreshes]. The AnalyticIllumination archives sit beside the GlobalIllumination ones.
#include "Frontier/Engine/Rendering/SceneViewport/SceneViewport.h"
#include "stb_image_write.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
using namespace Frontier;
using namespace Frontier::GlobalIllumination;

namespace
{
int Failures = 0;
void ReportCheck(bool Passed, const std::string& Name, const std::string& Detail = "")
{
    std::printf("%s %s%s%s\n", Passed ? "[PASS]" : "[FAIL]", Name.c_str(), Detail.empty() ? "" : " — ", Detail.c_str());
    if (!Passed) ++Failures;
}

struct Capture
{
    std::vector<uint8_t> Rgba{};
    double               MeanSeconds = 0.0, SlowestSeconds = 0.0;
    bool                 Refreshed = false;
};

double MeanAbsoluteDifference(const std::vector<uint8_t>& A, const std::vector<uint8_t>& B)
{
    double Sum = 0.0;
    for (size_t Index = 0; Index < A.size() && Index < B.size(); ++Index)
        if (Index % 4 != 3) Sum += std::abs(static_cast<int>(A[Index]) - static_cast<int>(B[Index]));
    return Sum / std::max<size_t>(1, A.size() / 4 * 3) / 255.0;
}

uint32_t CountDistinctColours(const std::vector<uint8_t>& Rgba)
{
    std::vector<uint32_t> Colours;
    for (size_t Index = 0; Index + 3 < Rgba.size(); Index += 4 * 7)
        Colours.push_back((Rgba[Index] >> 3) << 10 | (Rgba[Index + 1] >> 3) << 5 | (Rgba[Index + 2] >> 3));
    std::sort(Colours.begin(), Colours.end());
    return static_cast<uint32_t>(std::unique(Colours.begin(), Colours.end()) - Colours.begin());
}

// 📝 Mean of one channel over a band of rows, 0-1.
double BandMean(const std::vector<uint8_t>& Rgba, uint32_t Width, uint32_t FirstRow, uint32_t RowCount, uint32_t Channel)
{
    double Sum = 0.0;
    for (uint32_t Row = FirstRow; Row < FirstRow + RowCount; ++Row)
        for (uint32_t Column = 0; Column < Width; ++Column) Sum += Rgba[(static_cast<size_t>(Row) * Width + Column) * 4 + Channel];
    return Sum / std::max(1.0, static_cast<double>(RowCount) * Width) / 255.0;
}
} // namespace

int main(int ArgumentCount, char** Arguments)
{
    if (ArgumentCount < 6)
    {
        std::printf("usage: AnalyticIlluminationCapture <GlobalIllumination shaders> <Environment shaders> <ProvingGround> <CelestialMaps> <out> "
                    "[quality] [width] [height] [refreshes]\n");
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SceneViewportConfiguration Configuration;
    Configuration.GlobalIlluminationShaders = Arguments[1];
    Configuration.EnvironmentShaders        = Arguments[2];
    Configuration.SceneArchive              = Arguments[3];
    Configuration.CelestialMaps             = Arguments[4];
    const std::filesystem::path OutFolder   = Arguments[5];
    Configuration.Quality                   = ArgumentCount > 6 ? Arguments[6] : "low";
    const uint32_t Width     = ArgumentCount > 7 ? static_cast<uint32_t>(std::atoi(Arguments[7])) : 480u;
    const uint32_t Height    = ArgumentCount > 8 ? static_cast<uint32_t>(std::atoi(Arguments[8])) : 270u;
    const uint32_t Refreshes = ArgumentCount > 9 ? static_cast<uint32_t>(std::atoi(Arguments[9])) : 6u;
    std::filesystem::create_directories(OutFolder);
    Configuration.Journal = [](const std::string& Line) { std::printf("  %s\n", Line.c_str()); };

    SceneViewport Viewport;
    std::string   Diagnostic;
    if (!Viewport.Construct(nullptr, Configuration, {Width, Height}, &Diagnostic))
    {
        std::printf("bring-up: %s\n", Diagnostic.c_str());
        return 2;
    }
    std::printf("device: %s\n", Viewport.QueryAdapter().c_str());

    ViewportCamera Camera;
    Camera.Position = {-70.0, -46.0, 13.0};
    Camera.Target   = {-5.0, 10.0, 2.0};
    SceneEnvironment Environment;
    Environment.Solar.ElevationDegrees   = 38.0f;
    Environment.Solar.AzimuthDegrees     = 132.0f;
    Environment.Solar.IlluminanceKilolux = 98.0f;
    Environment.Clouds.Coverage          = 0.42f;

    uint32_t RefreshClock = 0;
    auto Run = [&](const ViewportRendering& Rendering, uint32_t Count, const std::string& Name) {
        Capture Result;
        for (uint32_t Refresh = 0; Refresh < Count; ++Refresh)
        {
            Environment.ElapsedSeconds = RefreshClock++ / 30.0;
            const auto Start = std::chrono::steady_clock::now();
            if (!Viewport.Refresh(Camera, Environment, Rendering, 1.0 / 30.0, {Width, Height}, &Diagnostic))
            {
                ReportCheck(false, Name + " refresh " + std::to_string(Refresh), Diagnostic);
                return Result;
            }
            const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();
            Result.MeanSeconds += Seconds / Count;
            Result.SlowestSeconds = std::max(Result.SlowestSeconds, Seconds);
        }
        if (!Viewport.ReadPresented(Result.Rgba)) ReportCheck(false, Name + " readback");
        const std::string Path = (OutFolder / (Name + ".png")).string();
        stbi_write_png(Path.c_str(), static_cast<int>(Width), static_cast<int>(Height), 4, Result.Rgba.data(), static_cast<int>(Width * 4));
        std::printf("%s: %u refreshes, mean %.3f s, slowest %.3f s → %s\n", Name.c_str(), Count, Result.MeanSeconds, Result.SlowestSeconds, Path.c_str());
        Result.Refreshed = Result.Rgba.size() == static_cast<size_t>(Width) * Height * 4;
        return Result;
    };

    // ① the global illumination, as the editor renders by default
    ViewportRendering Global;
    const Capture Reference = Run(Global, Refreshes, "GlobalIllumination");

    // ② the analytic illumination of the same scene, camera and sun (the first refresh builds its programs)
    ViewportRendering Analytic;
    Analytic.AnalyticIllumination = true;
    const auto BuildStart = std::chrono::steady_clock::now();
    const Capture First   = Run(Analytic, 1, "AnalyticFirstRefresh");
    std::printf("analytic bring-up and first refresh: %.3f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - BuildStart).count());
    ReportCheck(First.Refreshed, "the analytic illumination builds on the global illumination's scene");
    if (!First.Refreshed) return 1;
    const Capture Settled = Run(Analytic, Refreshes, "AnalyticIllumination");
    const uint32_t Colours = CountDistinctColours(Settled.Rgba);
    ReportCheck(Colours > 64, "the analytic image is lit, shaded geometry rather than a flat fill", std::to_string(Colours) + " distinct colours");
    const double SkyBlue = BandMean(Settled.Rgba, Width, 0, Height / 12, 2), SkyRed = BandMean(Settled.Rgba, Width, 0, Height / 12, 0);
    ReportCheck(SkyBlue > SkyRed, "the sky band at the top is blue over red", std::to_string(SkyBlue) + " vs " + std::to_string(SkyRed));
    std::printf("analytic vs global: mean absolute difference %.4f; refresh %.3f s vs %.3f s (%.1fx faster)\n",
                MeanAbsoluteDifference(Reference.Rgba, Settled.Rgba), Settled.MeanSeconds, Reference.MeanSeconds,
                Reference.MeanSeconds / std::max(1e-9, Settled.MeanSeconds));
    ReportCheck(Settled.MeanSeconds < Reference.MeanSeconds, "the analytic refresh is cheaper than the global illumination's");

    // ③ the sun drives it: a low western sun and no sun at all both change the image
    Environment.Solar.ElevationDegrees = 9.0f;
    Environment.Solar.AzimuthDegrees   = 262.0f;
    const Capture LowSun = Run(Analytic, 2, "AnalyticLowSun");
    const double LowSunChange = MeanAbsoluteDifference(Settled.Rgba, LowSun.Rgba);
    ReportCheck(LowSunChange > 0.01, "a low western sun relights the scene", std::to_string(LowSunChange));
    Environment.Solar.Enabled = false;
    const Capture NoSun = Run(Analytic, 2, "AnalyticWithoutSun");
    const double Fill = (BandMean(NoSun.Rgba, Width, Height / 2, Height / 2, 0) + BandMean(NoSun.Rgba, Width, Height / 2, Height / 2, 1) +
                         BandMean(NoSun.Rgba, Width, Height / 2, Height / 2, 2)) / 3.0;
    const double Lit = (BandMean(Settled.Rgba, Width, Height / 2, Height / 2, 0) + BandMean(Settled.Rgba, Width, Height / 2, Height / 2, 1) +
                        BandMean(Settled.Rgba, Width, Height / 2, Height / 2, 2)) / 3.0;
    ReportCheck(Fill < Lit && Fill > 0.0, "without the sun the hemisphere fill alone lights the lower half, dimmer than with it",
                std::to_string(Fill) + " vs " + std::to_string(Lit));
    Environment.Solar.Enabled          = true;
    Environment.Solar.ElevationDegrees = 38.0f;
    Environment.Solar.AzimuthDegrees   = 132.0f;

    // ④ the global illumination takes the presented image back
    const Capture Returned = Run(Global, 2, "GlobalIlluminationReturned");
    ReportCheck(Returned.Refreshed && MeanAbsoluteDifference(Returned.Rgba, Settled.Rgba) > 0.005,
                "the global illumination resumes after the analytic illumination rests");
    std::printf("AnalyticIlluminationCapture: %d failing checks\n", Failures);
    return Failures == 0 ? 0 : 1;
}
