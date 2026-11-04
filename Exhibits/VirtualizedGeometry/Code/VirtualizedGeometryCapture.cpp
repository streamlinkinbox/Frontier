/*==========================================================================================================================================
                                                    VIRTUALIZEDGEOMETRYCAPTURE.CPP
==========================================================================================================================================*/
// 🧩 The GPU virtualized geometry in the editor's own scene viewport, offscreen: the proving ground without and with the cluster
//    takeover, I1's one call per phase and class against the demo's one command per record on the same pages, the five cluster
//    inspection modes, the takeover's records and pages, and the refresh times every way.

// 📝 Usage: VirtualizedGeometryCapture <GlobalIllumination shaders> <Environment shaders> <ProvingGround> <CelestialMaps> <out>
//    [quality] [width] [height] [refreshes] [stream slots]. The VirtualizedGeometry archives sit beside the GlobalIllumination ones.
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
    double               MeanSeconds = 0.0, SlowestSeconds = 0.0, RecordedMilliseconds = 0.0;
};

double MeanAbsoluteDifference(const std::vector<uint8_t>& A, const std::vector<uint8_t>& B)
{
    double Sum = 0.0;
    for (size_t Index = 0; Index < A.size() && Index < B.size(); ++Index)
        if (Index % 4 != 3) Sum += std::abs(static_cast<int>(A[Index]) - static_cast<int>(B[Index]));
    return Sum / std::max<size_t>(1, A.size() / 4 * 3) / 255.0;
}

uint32_t CountDifferingPixels(const std::vector<uint8_t>& A, const std::vector<uint8_t>& B)
{
    uint32_t Count = 0;
    for (size_t Index = 0; Index + 3 < A.size() && Index + 3 < B.size(); Index += 4)
        if (A[Index] != B[Index] || A[Index + 1] != B[Index + 1] || A[Index + 2] != B[Index + 2]) ++Count;
    return Count;
}

uint32_t CountDistinctColours(const std::vector<uint8_t>& Rgba)
{
    std::vector<uint32_t> Colours;
    for (size_t Index = 0; Index + 3 < Rgba.size(); Index += 4 * 7)
        Colours.push_back((Rgba[Index] >> 3) << 10 | (Rgba[Index + 1] >> 3) << 5 | (Rgba[Index + 2] >> 3));
    std::sort(Colours.begin(), Colours.end());
    return static_cast<uint32_t>(std::unique(Colours.begin(), Colours.end()) - Colours.begin());
}
} // namespace

int main(int ArgumentCount, char** Arguments)
{
    if (ArgumentCount < 6)
    {
        std::printf("usage: VirtualizedGeometryCapture <GlobalIllumination shaders> <Environment shaders> <ProvingGround> <CelestialMaps> <out> "
                    "[quality] [width] [height] [refreshes] [stream slots]\n");
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
    const uint32_t Width       = ArgumentCount > 7 ? static_cast<uint32_t>(std::atoi(Arguments[7])) : 480u;
    const uint32_t Height      = ArgumentCount > 8 ? static_cast<uint32_t>(std::atoi(Arguments[8])) : 270u;
    const uint32_t Refreshes   = ArgumentCount > 9 ? static_cast<uint32_t>(std::atoi(Arguments[9])) : 8u;
    const uint32_t StreamSlots = ArgumentCount > 10 ? static_cast<uint32_t>(std::atoi(Arguments[10])) : 0u;
    std::filesystem::create_directories(OutFolder);
    Configuration.Journal = [](const std::string& Line) { std::printf("  %s\n", Line.c_str()); };

    SceneViewport Viewport;
    std::string   Diagnostic;
    if (!Viewport.Construct(nullptr, Configuration, {Width, Height}, &Diagnostic))
    {
        std::printf("bring-up: %s\n", Diagnostic.c_str());
        return 2;
    }
    IlluminationHost& Host = *Viewport.AccessHost();
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
            if (Host.Surface && Host.Surface->Virtualized) Result.RecordedMilliseconds += Host.Surface->Virtualized->RecordedMilliseconds() / Count;
            Result.SlowestSeconds = std::max(Result.SlowestSeconds, Seconds);
        }
        if (!Viewport.ReadPresented(Result.Rgba)) ReportCheck(false, Name + " readback");
        const std::string Path = (OutFolder / (Name + ".png")).string();
        stbi_write_png(Path.c_str(), static_cast<int>(Width), static_cast<int>(Height), 4, Result.Rgba.data(), static_cast<int>(Width * 4));
        std::printf("%s: %u refreshes, mean %.3f s, slowest %.3f s → %s\n", Name.c_str(), Count, Result.MeanSeconds, Result.SlowestSeconds, Path.c_str());
        return Result;
    };

    // ① the lossless-cluster path, as the editor renders today
    ViewportRendering Baseline;
    const Capture Reference = Run(Baseline, Refreshes, "Baseline");

    // ② the takeover: pages stream two per refresh, so run until every page is resident
    ViewportRendering Virtualized;
    Virtualized.VirtualizedGeometry    = true;
    Virtualized.VirtualizedStreamSlots = StreamSlots;
    Capture Streaming = Run(Virtualized, 1, "VirtualizedFirstRefresh");
    VirtualizedGeometry::NestingProjection* Projection = Host.Surface ? Host.Surface->Virtualized.get() : nullptr;
    ReportCheck(Projection != nullptr && Projection->HasRecords(), "the takeover found eligible facet sets",
                Projection ? Projection->StatusCaption() : "no projection");
    if (!Projection || !Projection->HasRecords()) return 1;
    std::printf("takeover: %u cluster records over %u occupants, %u pages (%u pending after the first refresh)\n", Projection->RecordCount(),
                Projection->OwnedOccupantCount(), Projection->PageCount(), Projection->PendingPages());
    uint32_t StreamingRefreshes = 1;
    while (Projection->PendingPages() > 0 && StreamingRefreshes < 600)
    {
        Run(Virtualized, 1, "VirtualizedStreaming");
        ++StreamingRefreshes;
    }
    std::printf("every page resident after %u refreshes\n", StreamingRefreshes);
    const Capture Settled = Run(Virtualized, Refreshes, "Virtualized");
    const uint32_t SettledCommands = Projection->RecordedCommands();
    const bool     Compacted       = Projection->CommandsCompacted();
    const double Difference = MeanAbsoluteDifference(Reference.Rgba, Settled.Rgba);
    std::printf("virtualized vs baseline: mean absolute difference %.4f; refresh %.3f s vs %.3f s (%.2fx)\n", Difference, Settled.MeanSeconds,
                Reference.MeanSeconds, Settled.MeanSeconds / std::max(1e-9, Reference.MeanSeconds));
    ReportCheck(Difference < 0.05, "the virtualized surface matches the lossless one within 5 % mean difference", std::to_string(Difference));

    // ③ I1 before and after on the same resident pages and camera, against the demo's one indirect command per cluster record. The
    //    lit image is temporal and stochastic (reservoir seeds, jitter, cloud drift), so equality is checked on the cluster identity
    //    view without jitter; the lit images are compared against the noise floor of two I1 refreshes apart.
    ViewportRendering Identities = Virtualized;
    Identities.Inspection           = 10;
    Identities.TemporalAntiAliasing = false;
    const Capture IdentitiesCompacted = Run(Identities, 2, "IdentitiesI1");
    Projection->CompactCommands = false;
    const Capture IdentitiesPerRecord = Run(Identities, 2, "IdentitiesPerRecord");
    const Capture PerRecord = Run(Virtualized, Refreshes, "VirtualizedPerRecord");
    const uint32_t PerRecordCommands = Projection->RecordedCommands();
    Projection->CompactCommands = true;
    const Capture Again = Run(Virtualized, 2, "VirtualizedAgain");
    std::printf("cluster commands, demo: %u indirect calls per refresh, %.3f ms recording them, refresh %.3f s\n", PerRecordCommands,
                PerRecord.RecordedMilliseconds, PerRecord.MeanSeconds);
    std::printf("cluster commands, I1:   %u indirect calls per refresh, %.3f ms recording them, refresh %.3f s (%s)\n", SettledCommands,
                Settled.RecordedMilliseconds, Settled.MeanSeconds,
                Compacted ? "one call per phase and class" : "the device offers neither drawIndirectCount nor multiDrawIndirect");
    const uint32_t IdentityPixels = CountDifferingPixels(IdentitiesCompacted.Rgba, IdentitiesPerRecord.Rgba);
    const double   LitDifference  = MeanAbsoluteDifference(PerRecord.Rgba, Settled.Rgba);
    const double   NoiseFloor     = MeanAbsoluteDifference(Again.Rgba, Settled.Rgba);
    std::printf("I1 vs demo commands: %u of %u cluster identity pixels differ; lit images %.6f apart, I1 against itself later %.6f\n",
                IdentityPixels, Width * Height, LitDifference, NoiseFloor);
    ReportCheck(!Compacted || SettledCommands < PerRecordCommands, "I1 records fewer indirect calls than one per cluster record",
                std::to_string(SettledCommands) + " vs " + std::to_string(PerRecordCommands));
    ReportCheck(IdentityPixels == 0, "I1 draws exactly the clusters of the per-record commands", std::to_string(IdentityPixels) + " pixels differ");

    // ③ the five cluster inspection modes
    const char* const Names[] = {"ClusterIdentities", "ClusterResolve", "ClusterHiZ", "ClusterGlassLayers", "ClusterTiers"};
    for (uint32_t Mode = 10; Mode <= 14; ++Mode)
    {
        ViewportRendering Inspection = Virtualized;
        Inspection.Inspection = Mode;
        const Capture View = Run(Inspection, 2, Names[Mode - 10]);
        const uint32_t Colours = CountDistinctColours(View.Rgba);
        ReportCheck(Colours > 8 && Projection->StatusCaption().empty(), std::string(Names[Mode - 10]) + " shows real cluster data",
                    std::to_string(Colours) + " distinct colours" + (Projection->StatusCaption().empty() ? "" : ", " + Projection->StatusCaption()));
    }
    std::printf("VirtualizedGeometryCapture: %d failing checks\n", Failures);
    return Failures == 0 ? 0 : 1;
}
