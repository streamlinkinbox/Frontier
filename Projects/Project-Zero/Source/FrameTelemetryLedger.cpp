//============================================================================================================================================
//                                                    FRAMETELEMETRYLEDGER.CPP
//============================================================================================================================================
// See the header. The whole TU is development-only; on a shipping build it compiles to an empty object.

#include "FrameTelemetryLedger.h"

#ifdef FRONTIER_DEVELOPMENT

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <system_error>

namespace Frontier {
namespace ProjectZero {

namespace {

// 2 million samples x 32 B = 64 MB. Large, but this is a development build and the alternative — growing the vector
//    mid-frame — would put a reallocation of tens of megabytes inside a frame and produce a spike the ledger would
//    then dutifully report as a renderer stall.
constexpr size_t kDefaultCapacity = 2u * 1000u * 1000u;

const char* CategoryName(uint16_t Category) noexcept
{
    switch (Category)
    {
        case kTelemetryStartup:  return "startup";
        case kTelemetryFrame:    return "frame";
        case kTelemetryShader:   return "shader";
        case kTelemetryContent:  return "content";
        case kTelemetryShutdown: return "shutdown";
        default:                 return "other";
    }
}

} // namespace

FrameTelemetryLedger::FrameTelemetryLedger() noexcept
    : Origin(std::chrono::steady_clock::now())
{
    Reserve(kDefaultCapacity);
}

FrameTelemetryLedger& FrameTelemetryLedger::Instance() noexcept
{
    // Function-local static: constructed on first use, which is the first scope anyone opens, and destroyed after
    //    main() returns. Thread-safe initialisation is guaranteed by the standard.
    static FrameTelemetryLedger Ledger;
    return Ledger;
}

void FrameTelemetryLedger::Reserve(size_t SampleCapacity) noexcept
{
    if (SampleCapacity == 0u) return;
    Capacity = SampleCapacity;
    // resize, not reserve: the ring writes by index and must never grow. Doing it here, once, is the entire point.
    Samples.assign(Capacity, TelemetrySample{});
    WriteCursor    = 0u;
    DroppedSamples = 0u;
    Wrapped        = false;
}

void FrameTelemetryLedger::Submit(const TelemetrySample& Sample) noexcept
{
    if (Capacity == 0u) return;
    Samples[WriteCursor] = Sample;
    WriteCursor = (WriteCursor + 1u) % Capacity;
    if (WriteCursor == 0u) Wrapped = true;
    if (Wrapped) ++DroppedSamples;
}

void FrameTelemetryLedger::MarkFirstFrame() noexcept
{
    if (StartupClosed) return;
    StartupClosed = true;
    StartupMicroseconds =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - Origin).count();
}

bool FrameTelemetryLedger::Serialise(const std::string& DirectoryPath, const std::string& Stem) noexcept
{
    std::error_code Dirs;
    std::filesystem::create_directories(DirectoryPath, Dirs);

    // The samples in chronological order. When the ring has wrapped the oldest live sample is at WriteCursor.
    std::vector<const TelemetrySample*> Ordered;
    const size_t Live = Wrapped ? Capacity : WriteCursor;
    Ordered.reserve(Live);
    for (size_t I = 0u; I < Live; ++I)
    {
        const size_t Index = Wrapped ? (WriteCursor + I) % Capacity : I;
        if (Samples[Index].Label) Ordered.push_back(&Samples[Index]);
    }

    const double TotalSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - Origin).count();

    //──────────────────────────────────────────────────────────────────────────
    // ① The summary — what anyone actually reads first
    //──────────────────────────────────────────────────────────────────────────
    struct Aggregate
    {
        uint64_t Count = 0u;
        double   Total = 0.0, Minimum = 1e30, Maximum = 0.0;
        uint32_t WorstFrame = 0u;
        uint16_t Category = 0u;
    };
    std::map<std::string, Aggregate> Totals;
    uint32_t FramesSeen = 0u;
    for (const TelemetrySample* S : Ordered)
    {
        Aggregate& A = Totals[S->Label];
        A.Count   += 1u;
        A.Total   += S->Microseconds;
        A.Category = S->Category;
        A.Minimum  = std::min(A.Minimum, S->Microseconds);
        if (S->Microseconds > A.Maximum) { A.Maximum = S->Microseconds; A.WorstFrame = S->FrameIndex; }
        FramesSeen = std::max(FramesSeen, S->FrameIndex);
    }

    const std::string SummaryPath = (std::filesystem::path(DirectoryPath) / (Stem + "_Summary.md")).string();
    {
        std::ofstream Out(SummaryPath, std::ios::trunc);
        if (!Out.is_open()) return false;

        Out << "# " << Stem << " - timing summary\n\n";
        Out << "Buffered in RAM for the whole run and written once at shutdown, so the measurement never touches\n"
               "the disk on the hot path. CPU wall times from steady_clock; for GPU stages these are SUBMISSION\n"
               "times, not device execution - the device's own timestamps are the Gpu*Ms rows in the telemetry\n"
               "report, and the two must not be confused.\n\n";
        Out << "- run length: " << TotalSeconds << " s\n";
        Out << "- startup (main to first frame): " << (StartupMicroseconds / 1000.0) << " ms\n";
        Out << "- frames: " << FrameIndex << "\n";
        Out << "- samples recorded: " << Ordered.size() << "\n";
        if (DroppedSamples)
            Out << "- **samples dropped: " << DroppedSamples << "** (the ring wrapped; raise the reserve)\n";
        Out << "\n";

        Out << "| scope | category | count | total ms | mean us | min us | max us | worst frame |\n";
        Out << "|---|---|---:|---:|---:|---:|---:|---:|\n";

        // Heaviest first: the reason anyone opens this file is to find what to fix.
        std::vector<std::pair<std::string, Aggregate>> Rows(Totals.begin(), Totals.end());
        std::sort(Rows.begin(), Rows.end(),
                  [](const auto& A, const auto& B) { return A.second.Total > B.second.Total; });

        for (const auto& [Label, A] : Rows)
        {
            Out << "| " << Label
                << " | " << CategoryName(A.Category)
                << " | " << A.Count
                << " | " << (A.Total / 1000.0)
                << " | " << (A.Total / static_cast<double>(A.Count))
                << " | " << A.Minimum
                << " | " << A.Maximum
                << " | " << A.WorstFrame
                << " |\n";
        }

        // Startup gets its own table: those costs are paid once and a per-frame mean makes them meaningless.
        //
        //    ⚠️ Ordered is CLOSE order — a scope is submitted by its destructor, so a nested child lands before the
        //    parent that contains it. Printing that verbatim reads as though the shader modules loaded before the
        //    bring-up that loaded them. Sort by OPEN order instead, which for a properly nested set is: parents
        //    before their children, and siblings in the order they ran. Open time is close time minus duration.
        Out << "\n## Startup phases (one-off, in order)\n\n";
        Out << "| phase | ms |\n|---|---:|\n";
        {
            std::vector<const TelemetrySample*> Phases;
            for (const TelemetrySample* S : Ordered)
            {
                if (S->FrameIndex != 0u) continue;
                if (S->Category != kTelemetryStartup && S->Category != kTelemetryShader &&
                    S->Category != kTelemetryContent) continue;
                Phases.push_back(S);
            }
            // Exact, not inferred: OpenOrder is a monotonic tick taken when the scope opened, so sorting by
            //    it reproduces the true chronological order — parents before the children they contain, siblings in
            //    the order they ran.
            std::sort(Phases.begin(), Phases.end(),
                      [](const TelemetrySample* A, const TelemetrySample* B) { return A->OpenOrder < B->OpenOrder; });
            for (const TelemetrySample* S : Phases)
                Out << "| " << std::string(S->Depth * 2u, ' ') << S->Label
                    << " | " << (S->Microseconds / 1000.0) << " |\n";
        }
    }

    //──────────────────────────────────────────────────────────────────────────
    // ② The verbose per-sample log — every scope, every frame
    //──────────────────────────────────────────────────────────────────────────
    const std::string VerbosePath = (std::filesystem::path(DirectoryPath) / (Stem + "_Verbose.log")).string();
    {
        std::ofstream Out(VerbosePath, std::ios::trunc);
        if (!Out.is_open()) return false;

        Out << "# frame  category  depth  microseconds  scope\n";
        Out << "# every scope opened during the run, in chronological order. Indentation is nesting depth, so a\n";
        Out << "# frame's cost can be attributed to its parts rather than read as a flat list.\n";
        for (const TelemetrySample* S : Ordered)
        {
            char Line[320];
            std::snprintf(Line, sizeof(Line), "%8u  %-8s  %2u  %12.3f  %s%s\n",
                          S->FrameIndex, CategoryName(S->Category), static_cast<unsigned>(S->Depth),
                          S->Microseconds, std::string(S->Depth * 2u, ' ').c_str(), S->Label);
            Out << Line;
        }
    }

    std::printf("[telemetry] %zu samples -> %s and %s\n", Ordered.size(), SummaryPath.c_str(), VerbosePath.c_str());
    return true;
}

} // namespace ProjectZero
} // namespace Frontier

#endif // FRONTIER_DEVELOPMENT
