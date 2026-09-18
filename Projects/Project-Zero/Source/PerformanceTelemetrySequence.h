//============================================================================================================================================
//                                                 PERFORMANCETELEMETRYSEQUENCE.H
//============================================================================================================================================
// 🧩 The frame loop's performance reporter: CPU pacing, real GPU stage timings and the workload that produced them,
//    written to ProjectZero_TelemetryReport as MEASUREMENT ROWS on a wall-clock cadence.
//
//    Why rows and not log lines. The report used to contain no performance or GPU entries at all. The frame loop was
//    not silent — it logged frame times, per-stage timings and cluster counts — but every one of them went through
//    RecordMessage, which writes a SENTENCE. RecordMeasurement is the call that writes
//    "Measurement: <token> = <value> [<unit>]", and nothing in the application ever called it; only the offline
//    CpuReferenceMain did. The numbers existed and scrolled past as prose, so nothing could parse, diff or trend
//    them. Both forms are emitted here: the sentence for someone reading the log, the row for everything else.
//
//    Why the GPU numbers are trustworthy. They are device timestamps, not host-side clocks. VisibilityExchange
//    writes them from its Vulkan query pool around each stage and scales by timestampPeriod, so "ReSTIR 6.2 ms" is
//    the dispatch as the GPU measured it, not what the submission looked like from the CPU. They read as invalid
//    until the pool has a completed frame to report, which is why the telemetry is gated on Valid — a stage that
//    did not run this frame contributes zero rather than garbage.
//
//    Why a wall-clock cadence rather than every N frames. The cost of reporting then does not scale with frame rate,
//    and a run that stalls still produces rows — a frame-count cadence goes quiet exactly when the timings would be
//    most interesting.
//
//    Engine ⇄ project seam: this lives in the PROJECT. It reads the engine's telemetry and writes the project's
//    report; the engine is told nothing about either.

#pragma once

#include "../../../Engine/DeviceExchange/DiagnosticMetrics.h"
#include "../../../Engine/DeviceExchange/VisibilityExchange.h"

#include <cstdint>

namespace Frontier {
namespace ProjectZero {

// What the sequence needs from the rest of the frame that it cannot read off VisibilityTelemetry: the swapchain's
//    size and present mode, and the integrator's sample budget. Passed in rather than depended on, so this file
//    needs no include of SwapchainExchange or the integrator and can be unit-tested without a device.
struct PerformanceWorkload
{
    uint32_t    RenderWidth      = 0u;        // [px]
    uint32_t    RenderHeight     = 0u;        // [px]
    uint32_t    Candidates       = 0u;        // [cnt] ReSTIR candidates per pixel
    uint32_t    ExtraCandidates  = 0u;        // [cnt]
    uint32_t    SpatialTaps      = 0u;        // [cnt]
    uint32_t    DenoiseLevels    = 0u;        // [cnt] à-trous levels
    const char* PresentMode      = "";        // [-]   resolved VkPresentModeKHR name, for the prose line
};

class PerformanceTelemetrySequence
{
public:
    // ReportSeconds is the wall-clock cadence. 5 s is the shipped value: long enough that the mean is stable and the
    //    report does not become a wall of numbers, short enough to see a regression inside a short run.
    explicit PerformanceTelemetrySequence(float ReportSeconds = 5.0f) noexcept
        : ReportInterval(ReportSeconds) {}

    // Call once per frame with the frame's duration. Accumulates, and when the window closes writes one report and
    //    resets. Returns true on the frames that actually reported, so a caller can flush the sink in step.
    bool AdvanceFrame(float DeltaSeconds,
                      DiagnosticMetrics&          Logger,
                      const VisibilityTelemetry&  Gpu,
                      const PerformanceWorkload&  Workload,
                      float                       FramesPerSecond,
                      float                       ResidentMebibytes) noexcept;

    // The last window's figures, for anything that wants them without waiting for the log (the editor footer, a
    //    soak harness). Zero until the first report lands.
    [[nodiscard]] float    QueryMeanMilliseconds() const noexcept { return LastMeanMs; }
    [[nodiscard]] float    QueryPeakMilliseconds() const noexcept { return LastPeakMs; }
    [[nodiscard]] float    QueryGpuMilliseconds()  const noexcept { return LastGpuMs;  }
    [[nodiscard]] bool     QueryGpuBound()         const noexcept { return LastGpuBound; }
    [[nodiscard]] uint32_t QueryReportCount()      const noexcept { return ReportCount; }

private:
    float    ReportInterval   = 5.0f;   // [s]
    float    WindowSeconds    = 0.0f;   // [s]   accumulated since the last report
    float    PeakSeconds      = 0.0f;   // [s]   worst single frame in the window
    uint32_t SampleCount      = 0u;     // [cnt] frames in the window

    float    LastMeanMs       = 0.0f;
    float    LastPeakMs       = 0.0f;
    float    LastGpuMs        = 0.0f;
    bool     LastGpuBound     = false;
    uint32_t ReportCount      = 0u;
};

} // namespace ProjectZero
} // namespace Frontier
