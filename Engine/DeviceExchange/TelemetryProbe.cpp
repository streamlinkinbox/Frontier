//============================================================================================================================================
//                                                        TELEMETRYPROBE.CPP
//============================================================================================================================================
// 🧩 The development/debug-only probe's storage and its single save-at-close write. See TelemetryProbe.h for the
//    contract; the essential property implemented here is that NOTHING touches the disk until SaveReport — every
//    frame row, phase, sample and event lives in pre-reserved vectors, and the reserves are sized so a normal run
//    never reallocates mid-frame (100 000 frames ≈ 28 minutes at 60 fps ≈ ~10 MiB of rows).
//
//    Ship builds: FRONTIER_TELEMETRY_PROBE is 0, and this file compiles to an empty translation unit.

#include "TelemetryProbe.h"

#if FRONTIER_TELEMETRY_PROBE

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                        CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

static constexpr size_t kFrameReserve  = 100'000u;   // ~28 min at 60 fps before the vector regrows
static constexpr size_t kPhaseReserve  = 256u;
static constexpr size_t kSampleReserve = 1'024u;
static constexpr size_t kEventReserve  = 64u;

static const char* const kSectionNames[static_cast<size_t>(ProbeSection::Count)] =
{
    "InputAndUi", "CelestialTick", "EditorAndPanels", "SimulationAndInterface",
    "ScenePush", "RecordAndPresent", "FrameCapWait",
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

TelemetryProbe& TelemetryProbe::Access() noexcept
{
    static TelemetryProbe Instance;
    return Instance;
}

TelemetryProbe::TelemetryProbe() noexcept
{
    Epoch        = ProbeClock::now();
    FrameLapMark = Epoch;
    Frames.reserve(kFrameReserve);
    Phases.reserve(kPhaseReserve);
    Samples.reserve(kSampleReserve);
    Events.reserve(kEventReserve);
}

void TelemetryProbe::MarkBoot() noexcept
{
    std::lock_guard<std::mutex> Lock(Guard);
    Epoch        = ProbeClock::now();
    FrameLapMark = Epoch;
    Events.push_back({ "Boot", 0.0 });
}

double TelemetryProbe::NowMilliseconds() const noexcept
{
    return std::chrono::duration<double, std::milli>(ProbeClock::now() - Epoch).count();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  PHASES / SAMPLES / EVENTS
//------------------------------------------------------------------------------------------------------------------------

void TelemetryProbe::BeginPhase(const char* Name) noexcept
{
    const double At = NowMilliseconds();
    std::lock_guard<std::mutex> Lock(Guard);
    Phases.push_back({ Name ? Name : "?", At, -1.0 });
}

void TelemetryProbe::EndPhase(const char* Name) noexcept
{
    const double At = NowMilliseconds();
    std::lock_guard<std::mutex> Lock(Guard);
    // Close the most recent OPEN phase of this name (phases nest; the innermost closes first).
    for (auto It = Phases.rbegin(); It != Phases.rend(); ++It)
    {
        if (It->EndMs < 0.0 && It->Name == (Name ? Name : "?")) { It->EndMs = At; return; }
    }
    // An End without a Begin still leaves a trace rather than vanishing.
    Phases.push_back({ std::string(Name ? Name : "?") + " (end without begin)", At, At });
}

void TelemetryProbe::RecordSample(const char* Kind, const char* Name, double Milliseconds) noexcept
{
    const double At = NowMilliseconds();
    std::lock_guard<std::mutex> Lock(Guard);
    Samples.push_back({ Kind ? Kind : "?", Name ? Name : "?", At, Milliseconds });
}

void TelemetryProbe::MarkEvent(const char* Name) noexcept
{
    const double At = NowMilliseconds();
    std::lock_guard<std::mutex> Lock(Guard);
    Events.push_back({ Name ? Name : "?", At });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        FRAMES
//------------------------------------------------------------------------------------------------------------------------

void TelemetryProbe::BeginFrame(float DeltaSeconds) noexcept
{
    std::lock_guard<std::mutex> Lock(Guard);
    PendingFrame              = ProbeFrameRow{};
    PendingFrame.Frame        = FrameOrdinal;
    PendingFrame.WallSeconds  = static_cast<float>(NowMilliseconds() / 1000.0);
    PendingFrame.DeltaMs      = DeltaSeconds * 1000.0f;
    FrameLapMark              = ProbeClock::now();
    FrameOpen                 = true;
}

void TelemetryProbe::Lap(ProbeSection Section) noexcept
{
    const ProbeClock::time_point Now = ProbeClock::now();
    std::lock_guard<std::mutex> Lock(Guard);
    if (!FrameOpen) return;
    const float Ms = static_cast<float>(std::chrono::duration<double, std::milli>(Now - FrameLapMark).count());
    PendingFrame.SectionMs[static_cast<size_t>(Section)] += Ms;   // += so a section lapped twice accumulates
    FrameLapMark = Now;
}

void TelemetryProbe::EndFrameRow(bool Valid,
                                 float Cull, float Raster, float HiZ, float Resolve, float Kernel,
                                 float Shadow, float Restir, float Post, float Sky, float Volume,
                                 uint32_t Clusters, uint32_t Visible, uint32_t Triangles,
                                 float Fps, float ResidentMiB) noexcept
{
    std::lock_guard<std::mutex> Lock(Guard);
    if (!FrameOpen) return;
    PendingFrame.GpuValid        = Valid ? 1u : 0u;
    PendingFrame.GpuCullMs       = Valid ? Cull    : 0.0f;
    PendingFrame.GpuRasterMs     = Valid ? Raster  : 0.0f;
    PendingFrame.GpuHiZMs        = Valid ? HiZ     : 0.0f;
    PendingFrame.GpuResolveMs    = Valid ? Resolve : 0.0f;
    PendingFrame.GpuKernelMs     = Valid ? Kernel  : 0.0f;
    PendingFrame.GpuShadowMs     = Valid ? Shadow  : 0.0f;
    PendingFrame.GpuRestirMs     = Valid ? Restir  : 0.0f;
    PendingFrame.GpuPostMs       = Valid ? Post    : 0.0f;
    PendingFrame.GpuSkyMs        = Valid ? Sky     : 0.0f;
    PendingFrame.GpuVolumeMs     = Valid ? Volume  : 0.0f;
    PendingFrame.ClusterTotal    = Clusters;
    PendingFrame.ClustersVisible = Visible;
    PendingFrame.TrianglesDrawn  = Triangles;
    PendingFrame.Fps             = Fps;
    PendingFrame.ResidentMiB     = ResidentMiB;
    Frames.push_back(PendingFrame);
    FrameOpen = false;
    ++FrameOrdinal;
    if (FrameOrdinal == 1u) Events.push_back({ "FirstFrameComplete", NowMilliseconds() });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SAVE AT CLOSE
//------------------------------------------------------------------------------------------------------------------------

namespace {

struct ColumnStat { double Sum = 0.0, Peak = 0.0; };

void Accumulate(ColumnStat& S, double V) noexcept { S.Sum += V; S.Peak = std::max(S.Peak, V); }

} // namespace

void TelemetryProbe::SaveReport(const char* Directory) noexcept
{
    std::lock_guard<std::mutex> Lock(Guard);

    std::error_code FsError;
    const std::filesystem::path Root = Directory && Directory[0] ? Directory : "Diagnostics";
    std::filesystem::create_directories(Root, FsError);

    //── the raw frame table — CSV, one line per frame ─────────────────────────────────────────────────────────────
    {
        std::ofstream Csv(Root / "ProjectZero_TelemetryProbe_Frames.csv", std::ios::trunc);
        if (Csv.is_open())
        {
            Csv << "Frame,WallSeconds,DeltaMs";
            for (const char* Name : kSectionNames) Csv << ",Cpu" << Name << "Ms";
            Csv << ",GpuValid,GpuCullMs,GpuRasterMs,GpuHiZMs,GpuResolveMs,GpuKernelMs,GpuShadowMs,GpuRestirMs,"
                   "GpuPostMs,GpuSkyMs,GpuVolumeMs,ClusterTotal,ClustersVisible,TrianglesDrawn,Fps,ResidentMiB\n";
            char Line[640];
            for (const ProbeFrameRow& R : Frames)
            {
                int N = std::snprintf(Line, sizeof(Line), "%u,%.4f,%.4f",
                                      R.Frame, static_cast<double>(R.WallSeconds), static_cast<double>(R.DeltaMs));
                for (float S : R.SectionMs)
                    N += std::snprintf(Line + N, sizeof(Line) - static_cast<size_t>(N), ",%.4f", static_cast<double>(S));
                std::snprintf(Line + N, sizeof(Line) - static_cast<size_t>(N),
                              ",%u,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%u,%u,%u,%.2f,%.1f",
                              R.GpuValid,
                              static_cast<double>(R.GpuCullMs), static_cast<double>(R.GpuRasterMs),
                              static_cast<double>(R.GpuHiZMs), static_cast<double>(R.GpuResolveMs),
                              static_cast<double>(R.GpuKernelMs), static_cast<double>(R.GpuShadowMs),
                              static_cast<double>(R.GpuRestirMs), static_cast<double>(R.GpuPostMs),
                              static_cast<double>(R.GpuSkyMs), static_cast<double>(R.GpuVolumeMs),
                              R.ClusterTotal, R.ClustersVisible, R.TrianglesDrawn,
                              static_cast<double>(R.Fps), static_cast<double>(R.ResidentMiB));
                Csv << Line << '\n';
            }
        }
    }

    //── the human summary — markdown ──────────────────────────────────────────────────────────────────────────────
    std::ofstream Md(Root / "ProjectZero_TelemetryProbe.md", std::ios::trunc);
    if (!Md.is_open()) return;
    char Line[512];

    Md << "# Project-Zero Telemetry Probe\n\n"
       << "Development/debug-only probe (compiled out of ship builds). All records were held in RAM and written "
          "once, at application close.\n\n";

    Md << "## Events\n\n| Event | At [ms] |\n|---|---:|\n";
    for (const ProbeEventRow& E : Events)
    {
        std::snprintf(Line, sizeof(Line), "| %s | %.3f |", E.Name.c_str(), E.AtMs);
        Md << Line << '\n';
    }

    Md << "\n## Startup Phases\n\n| Phase | Begin [ms] | End [ms] | Duration [ms] |\n|---|---:|---:|---:|\n";
    for (const ProbePhaseRow& P : Phases)
    {
        if (P.EndMs >= 0.0)
            std::snprintf(Line, sizeof(Line), "| %s | %.3f | %.3f | %.3f |", P.Name.c_str(), P.BeginMs, P.EndMs, P.EndMs - P.BeginMs);
        else
            std::snprintf(Line, sizeof(Line), "| %s | %.3f | (never closed) | - |", P.Name.c_str(), P.BeginMs);
        Md << Line << '\n';
    }

    Md << "\n## Shader Loads and Bring-up Stages\n\n| Kind | Name | At [ms] | Duration [ms] |\n|---|---|---:|---:|\n";
    for (const ProbeSampleRow& S : Samples)
    {
        std::snprintf(Line, sizeof(Line), "| %s | %s | %.3f | %.3f |", S.Kind.c_str(), S.Name.c_str(), S.AtMs, S.Milliseconds);
        Md << Line << '\n';
    }

    //── per-column frame statistics ───────────────────────────────────────────────────────────────────────────────
    Md << "\n## Frames\n\n";
    std::snprintf(Line, sizeof(Line), "%zu frames recorded. Full per-frame rows: ProjectZero_TelemetryProbe_Frames.csv\n",
                  Frames.size());
    Md << Line;

    if (!Frames.empty())
    {
        const double Count = static_cast<double>(Frames.size());
        ColumnStat Delta, Sections[static_cast<size_t>(ProbeSection::Count)];
        ColumnStat Cull, Raster, HiZ, Resolve, Kernel, Shadow, Restir, Post, Sky, Volume;
        size_t GpuRows = 0u;
        for (const ProbeFrameRow& R : Frames)
        {
            Accumulate(Delta, R.DeltaMs);
            for (size_t I = 0u; I < static_cast<size_t>(ProbeSection::Count); ++I)
                Accumulate(Sections[I], R.SectionMs[I]);
            if (R.GpuValid)
            {
                ++GpuRows;
                Accumulate(Cull, R.GpuCullMs);     Accumulate(Raster, R.GpuRasterMs);
                Accumulate(HiZ, R.GpuHiZMs);       Accumulate(Resolve, R.GpuResolveMs);
                Accumulate(Kernel, R.GpuKernelMs); Accumulate(Shadow, R.GpuShadowMs);
                Accumulate(Restir, R.GpuRestirMs); Accumulate(Post, R.GpuPostMs);
                Accumulate(Sky, R.GpuSkyMs);       Accumulate(Volume, R.GpuVolumeMs);
            }
        }

        Md << "\n| Column | Mean [ms] | Peak [ms] |\n|---|---:|---:|\n";
        std::snprintf(Line, sizeof(Line), "| Frame Δτ | %.4f | %.4f |", Delta.Sum / Count, Delta.Peak);
        Md << Line << '\n';
        for (size_t I = 0u; I < static_cast<size_t>(ProbeSection::Count); ++I)
        {
            std::snprintf(Line, sizeof(Line), "| CPU %s | %.4f | %.4f |", kSectionNames[I], Sections[I].Sum / Count, Sections[I].Peak);
            Md << Line << '\n';
        }
        if (GpuRows > 0u)
        {
            const double G = static_cast<double>(GpuRows);
            const struct { const char* Name; const ColumnStat* S; } GpuColumns[] =
            {
                { "GPU Cull",    &Cull    }, { "GPU Raster", &Raster }, { "GPU HiZ",    &HiZ    },
                { "GPU Resolve", &Resolve }, { "GPU Kernel", &Kernel }, { "GPU Shadow", &Shadow },
                { "GPU ReSTIR",  &Restir  }, { "GPU Post",   &Post   }, { "GPU Sky",    &Sky    },
                { "GPU Volume",  &Volume  },
            };
            for (const auto& C : GpuColumns)
            {
                std::snprintf(Line, sizeof(Line), "| %s | %.4f | %.4f |", C.Name, C.S->Sum / G, C.S->Peak);
                Md << Line << '\n';
            }
            std::snprintf(Line, sizeof(Line), "\n%zu of %zu frames carried valid device timestamps.\n", GpuRows, Frames.size());
            Md << Line;
        }
        else
        {
            Md << "\nNo frame carried valid device timestamps (the query pool never completed a frame).\n";
        }
    }
}

} // namespace Frontier

#endif // FRONTIER_TELEMETRY_PROBE
