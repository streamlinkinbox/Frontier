//============================================================================================================================================
//                                                   PERFORMANCETELEMETRYGATE.CPP
//============================================================================================================================================
// Drives the REAL PerformanceTelemetrySequence — the one Project-Zero runs — and reads back the report it produced.
//
//    This does not inspect source code. It constructs the shipped sequence, feeds it synthetic frames and a synthetic
//    VisibilityTelemetry block, lets it write to a DiagnosticMetrics sink configured exactly as GameExecution
//    configures ProjectZero_TelemetryReport, and then parses the resulting file. If the report would come out of the
//    application without performance or GPU rows, it comes out of this gate that way too.
//
//    The defect being locked down: the report carried no performance or GPU entries, because everything the frame
//    loop knew was written with RecordMessage (which emits a sentence) and never with RecordMeasurement (which emits
//    a "Measurement: <token> = <value> [<unit>]" row). The numbers were there, in prose, unparseable.
//
//    usage: bash Tools/Build/CheckPerformanceTelemetry.sh

#include "Projects/Project-Zero/Source/PerformanceTelemetrySequence.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace Frontier;
using namespace Frontier::ProjectZero;

namespace {

int Failures = 0;

void Check(bool Condition, const std::string& Message)
{
    std::printf("  %s  %s\n", Condition ? "PASS" : "FAIL", Message.c_str());
    if (!Condition) ++Failures;
}

// Pull every "Measurement: <token> = <value> [<unit>]" row out of the report the sequence just wrote.
std::map<std::string, std::pair<double, std::string>> ParseRows(const std::string& Path)
{
    std::map<std::string, std::pair<double, std::string>> Rows;
    std::ifstream In(Path);
    std::string   Line;
    while (std::getline(In, Line))
    {
        const size_t Marker = Line.find("Measurement: ");
        if (Marker == std::string::npos) continue;
        const size_t Equals = Line.find(" = ", Marker);
        const size_t Open   = Line.find(" [", Equals);
        const size_t Close  = Line.find(']', Open);
        if (Equals == std::string::npos || Open == std::string::npos || Close == std::string::npos) continue;

        const std::string Token = Line.substr(Marker + 13, Equals - (Marker + 13));
        const std::string Value = Line.substr(Equals + 3, Open - (Equals + 3));
        const std::string Unit  = Line.substr(Open + 2, Close - (Open + 2));
        Rows[Token] = { std::strtod(Value.c_str(), nullptr), Unit };
    }
    return Rows;
}

// A plausible GPU frame: every stage non-zero so a dropped row cannot hide behind a legitimate zero.
VisibilityTelemetry SyntheticGpuFrame()
{
    VisibilityTelemetry G{};
    G.Valid               = true;
    G.CullMilliseconds    = 0.42f;
    G.RasterMilliseconds  = 1.80f;
    G.HiZMilliseconds     = 0.31f;
    G.ResolveMilliseconds = 0.95f;
    G.KernelMilliseconds  = 11.20f;
    G.RestirMilliseconds  = 11.20f;
    G.ShadowMilliseconds  = 2.60f;
    G.PostMilliseconds    = 0.70f;
    G.SkyMilliseconds     = 0.50f;
    G.VolumeMilliseconds  = 0.40f;
    G.ClusterTotal        = 9000u;
    G.FrustumPassed       = 5200u;
    G.ConePassed          = 3100u;
    G.OcclusionPassed     = 1400u;
    G.PhaseOneDraws       = 120u;
    G.PhaseTwoDraws       = 35u;
    G.TrianglesDrawn      = 480000u;
    return G;
}

PerformanceWorkload SyntheticWorkload()
{
    PerformanceWorkload W;
    W.RenderWidth     = 1280u;
    W.RenderHeight    = 720u;
    W.Candidates      = 8u;
    W.ExtraCandidates = 3u;
    W.SpatialTaps     = 3u;
    W.DenoiseLevels   = 5u;
    W.PresentMode     = "MAILBOX";
    return W;
}

// The sink, configured the way GameExecution configures the real report.
DiagnosticConfiguration ReportConfiguration(const std::string& Stem)
{
    DiagnosticConfiguration Config{};
    Config.DestinationFolder          = ".";
    Config.OutputFileStem             = Stem;
    Config.FileExtension              = ".md";
    Config.TimestampPrefixEnabled     = false;
    Config.ConsoleEchoEnabled         = false;
    Config.MarkdownTableFormatEnabled = true;
    return Config;
}

} // namespace

int main()
{
    std::printf("================================================================================\n");
    std::printf("   PERFORMANCE TELEMETRY GATE — running the sequence Project-Zero runs\n");
    std::printf("================================================================================\n");

    const std::string Stem = "PerformanceTelemetryGate_Report";
    const std::string Path = "./" + Stem + ".md";
    std::remove(Path.c_str());

    // ── ① the cadence ───────────────────────────────────────────────────────────────────────────────────────────
    //    A 1 s window fed 16 ms frames must stay silent until the window closes and then report exactly once.
    uint32_t Reports = 0u;
    {
        PerformanceTelemetrySequence Sequence(1.0f);
        DiagnosticMetrics Logger(ReportConfiguration(Stem));
        if (!Logger.InitializeSink()) { std::printf("  FAIL  the sink would not open\n"); return 1; }

        const VisibilityTelemetry Gpu      = SyntheticGpuFrame();
        const PerformanceWorkload Workload = SyntheticWorkload();

        for (int Frame = 0; Frame < 40; ++Frame)                      // 0.64 s — short of the 1 s window
            if (Sequence.AdvanceFrame(0.016f, Logger, Gpu, Workload, 62.5f, 512.0f)) ++Reports;
        Logger.FlushSink();
        Logger.TerminateSink();

        Check(Reports == 0u, "the sequence stays silent until its wall-clock window closes");
        Check(Sequence.QueryReportCount() == 0u, "40 frames at 16 ms (0.64 s) is short of the window — nothing reported");
    }

    // ── ② a full window produces the report ─────────────────────────────────────────────────────────────────────
    std::remove(Path.c_str());
    {
        PerformanceTelemetrySequence Sequence(1.0f);
        DiagnosticMetrics Logger(ReportConfiguration(Stem));
        if (!Logger.InitializeSink()) { std::printf("  FAIL  the sink would not open\n"); return 1; }

        const VisibilityTelemetry Gpu      = SyntheticGpuFrame();
        const PerformanceWorkload Workload = SyntheticWorkload();

        uint32_t Emitted = 0u;
        // A window closes on the frame that takes the accumulator past 1 s: 1.0 / 0.016 = 62.5, so frame 63.
        //    Two windows therefore need 126 frames; 125 gives one report and is the off-by-one worth not shipping.
        for (int Frame = 0; Frame < 126; ++Frame)
            if (Sequence.AdvanceFrame(0.016f, Logger, Gpu, Workload, 62.5f, 512.0f)) ++Emitted;

        Logger.FlushSink();
        Logger.TerminateSink();

        Check(Emitted == 2u, "two full windows produce two reports (the cadence is wall-clock, not per frame)");
        Check(Sequence.QueryReportCount() == 2u, "the sequence counts its own reports");
        Check(Sequence.QueryMeanMilliseconds() > 15.0f && Sequence.QueryMeanMilliseconds() < 17.0f,
              "the reported mean frame time is the 16 ms it was fed");
        Check(Sequence.QueryGpuBound(), "18.9 ms of GPU against a 16 ms frame is correctly called GPU-BOUND");
    }

    // ── ③ the rows are actually in the file ─────────────────────────────────────────────────────────────────────
    const auto Rows = ParseRows(Path);
    std::printf("\n%zu measurement row(s) parsed from %s\n", Rows.size(), Path.c_str());

    struct Expected { const char* Token; const char* Unit; double Value; };
    const Expected Required[] = {
        // CPU
        { "FrameTimeMeanMs",   "ms",      16.0   },
        { "FrameTimePeakMs",   "ms",      16.0   },
        { "FramesPerSecond",   "fps",     62.5   },
        { "FrameSampleCount",  "count",    0.0   },   // value varies with the window; presence and unit are the point
        { "ResidentMemory",    "MiB",    512.0   },
        // GPU — the device timestamp pool
        { "GpuFrameTotalMs",   "ms",      18.18  },   // cull+raster+HiZ+resolve+KERNEL+shadow+sky+volume;
                                                       //    Kernel already includes ReSTIR and Post, so they are
                                                       //    NOT added again (that bug inflates the total by ~60 %)
        { "GpuCullMs",         "ms",       0.42  },
        { "GpuRasterMs",       "ms",       1.80  },
        { "GpuHiZMs",          "ms",       0.31  },
        { "GpuResolveMs",      "ms",       0.95  },
        { "GpuReSTIRMs",       "ms",      11.20  },
        { "GpuShadowMs",       "ms",       2.60  },
        { "GpuPostMs",         "ms",       0.70  },
        { "GpuSkyMs",          "ms",       0.50  },
        { "GpuVolumeMs",       "ms",       0.40  },
        // Visibility
        { "ClustersTested",    "count", 9000.0   },
        { "ClustersFrustum",   "count", 5200.0   },
        { "ClustersCone",      "count", 3100.0   },
        { "ClustersVisible",   "count", 1400.0   },
        { "TrianglesDrawn",    "count", 480000.0 },
        { "DrawCalls",         "count",  155.0   },
        // Workload — what makes the milliseconds mean something
        { "RenderPixels",      "px",    921600.0 },
        { "ReSTIRCandidates",  "count",    8.0   },
        { "ReSTIRExtra",       "count",    3.0   },
        { "ReSTIRSpatialTaps", "count",    3.0   },
        { "DenoiseLevels",     "count",    5.0   },
        { "GpuBound",          "bool",     1.0   },
        { "ReSTIRShareOfFrame","percent",  0.0   },   // checked for range below, not equality
    };

    for (const Expected& E : Required)
    {
        const auto It = Rows.find(E.Token);
        if (It == Rows.end()) { Check(false, std::string("the report contains a ") + E.Token + " row"); continue; }

        char Message[256];
        std::snprintf(Message, sizeof(Message), "%s = %g [%s]", E.Token, It->second.first, It->second.second.c_str());
        const bool UnitOk = It->second.second == E.Unit;
        // Values that are pinned by the synthetic input are checked exactly (to a tolerance); the two marked 0.0
        //    above vary legitimately, so only presence and unit matter for them.
        const bool ValueOk = E.Value == 0.0 ? true
                                            : (It->second.first > E.Value * 0.98 && It->second.first < E.Value * 1.02);
        Check(UnitOk && ValueOk, Message);
    }

    // The two that are range-checked rather than pinned.
    {
        const auto Share = Rows.find("ReSTIRShareOfFrame");
        Check(Share != Rows.end() && Share->second.first > 58.0 && Share->second.first < 65.0,
              "ReSTIRShareOfFrame is ~62 % — 11.2 of 18.18 ms, the number that explains a pegged GPU");
        const auto Samples = Rows.find("FrameSampleCount");
        Check(Samples != Rows.end() && Samples->second.first > 50.0,
              "FrameSampleCount reports the frames actually in the window");
    }

    // ── ④ the regression itself ─────────────────────────────────────────────────────────────────────────────────
    //    Prose is not a row. The report must contain BOTH, and the rows are what was missing.
    {
        std::ifstream In(Path);
        std::ostringstream Buffer; Buffer << In.rdbuf();
        const std::string Text = Buffer.str();
        Check(Text.find("CPU 16.00 ms/frame") != std::string::npos,
              "the human-readable prose line is still written (it was never the problem)");
        Check(Text.find("GPU-BOUND") != std::string::npos,
              "the bound verdict is stated in prose as well as in the GpuBound row");
        Check(Rows.size() >= 28u,
              "the report carries the full measurement set — this is the row count that used to be zero");
    }

    std::remove(Path.c_str());
    std::printf(Failures ? "\nRED — %d check(s) failed\n"
                         : "\nGREEN — Project-Zero's telemetry sequence writes real performance and GPU rows\n", Failures);
    return Failures ? 1 : 0;
}
