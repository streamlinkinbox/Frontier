/*==========================================================================================================================================
                                                         PERFORMANCEPANEL.CPP
==========================================================================================================================================*/
// 🧩 The F2 performance window (see PerformancePanel.h): the optimized-execution switch, the live figures, and the A/B test with its report.

#include "Frontier/Engine/Diagnostics/PerformancePanel.h"

#include "Frontier/Engine/Rendering/VulkanContext.h"
#include "Frontier/Engine/VolumetricDynamics/FluidPlumeBridge.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Frontier
{

namespace
{

//----------------------------------------------------------------------------------------------------------------------
//                                                     THE SCRIPTED DRIVE
//----------------------------------------------------------------------------------------------------------------------

constexpr int    ScriptLapRefreshes = 720;          // [refresh] - burnout 0-149, drive 150-389, brake 390-479, circle 480-719
constexpr double ScriptStepSeconds  = 1.0 / 60.0;   // [s] - the step of every refresh of the test, whatever the machine's rate

// 📝 The inputs of one refresh of the test: a pure function of the refresh's index, so that every run meets the same drive.
PerformancePanel::SimulationStep ResolveScriptedStep(int Refresh)
{
    PerformancePanel::SimulationStep Step{};
    Step.DeltaSeconds = ScriptStepSeconds;
    const int Lap     = Refresh % ScriptLapRefreshes;
    Step.ResetRequested = (Lap == 0);
    if (Lap < 150)
    {
        // [unit] - the burnout: throttle, brake and handbrake together hold the car where it stands while the rear tyres smoke
        Step.Throttle  = 1.0f;
        Step.Brake     = 0.85f;
        Step.Steer     = 0.16f;
        Step.Handbrake = 1.0f;
    }
    else if (Lap < 390)
    {
        // [unit] - the drive off: two slow turns each way, so that the smoke is left behind and the window of the world's grid has to move
        Step.Throttle = 0.85f;
        Step.Steer    = 0.30f * std::sin(6.2831853f * static_cast<float>(Lap - 150) / 120.0f);
    }
    else if (Lap < 480)
    {
        Step.Brake = 0.90f;
    }
    else
    {
        // [unit] - the circle: throttle and lock held, the handbrake pulled in pulses of 0.75 s
        Step.Throttle  = 0.65f;
        Step.Steer     = 0.55f;
        Step.Handbrake = (((Lap - 480) / 45) % 2 == 1) ? 1.0f : 0.0f;
    }
    return Step;
}

//----------------------------------------------------------------------------------------------------------------------
//                                                         HELPERS
//----------------------------------------------------------------------------------------------------------------------

uint64_t HashBytes(const uint8_t* Bytes, size_t Count) noexcept
{
    uint64_t Hash = 1469598103934665603ull;
    for (size_t Index = 0u; Index < Count; ++Index)
    {
        Hash ^= Bytes[Index];
        Hash *= 1099511628211ull;
    }
    return Hash;
}

std::string Format(const char* Pattern, double Number)
{
    char Text[64];
    std::snprintf(Text, sizeof(Text), Pattern, Number);
    return Text;
}

std::string FormatChange(double Fraction)
{
    char Text[32];
    std::snprintf(Text, sizeof(Text), "%+.1f %%", Fraction * 100.0);
    return Text;
}

std::string ResolveLeaf(const std::string& Path)
{
    const size_t Slash = Path.rfind('/');
    return Slash == std::string::npos ? Path : Path.substr(Slash + 1u);
}

std::string ResolveUtcStamp()
{
    const std::time_t Now = std::time(nullptr);
    std::tm           Utc{};
#if defined(_WIN32)
    gmtime_s(&Utc, &Now);
#else
    gmtime_r(&Now, &Utc);
#endif
    char Text[32] = {};
    std::strftime(Text, sizeof(Text), "%Y%m%d-%H%M%S", &Utc);
    return Text;
}

// 📝 The means of the series of one kind whose last name is one of Leaves, added: a stretch that is entered from several places (a wait on the device)
//    is counted wherever it ran. Present is false when no such series was recorded at all.
double SumMeans(const std::vector<PerformanceLog::SeriesStatistics>& Series, const char* Kind, const char* const (&Leaves)[4], bool& OutPresent)
{
    double Sum  = 0.0;
    OutPresent  = false;
    for (const PerformanceLog::SeriesStatistics& Entry : Series)
    {
        if (Entry.Kind != Kind)
            continue;
        const std::string Leaf = ResolveLeaf(Entry.Path);
        for (const char* Wanted : Leaves)
        {
            if (Wanted != nullptr && Leaf == Wanted)
            {
                Sum += Entry.Mean;
                OutPresent = true;
            }
        }
    }
    return Sum;
}

const PerformanceLog::SeriesStatistics* FindByLeaf(const std::vector<PerformanceLog::SeriesStatistics>& Series, const char* Kind, const char* Leaf)
{
    for (const PerformanceLog::SeriesStatistics& Entry : Series)
    {
        if (Entry.Kind == Kind && ResolveLeaf(Entry.Path) == Leaf)
            return &Entry;
    }
    return nullptr;
}

// 📝 The stretches that wait (for the device, or for the display): a refresh's CPU time is the time it works plus the time it waits.
constexpr const char* WaitingLeaves[4] = { "vk.submit_wait", "vk.fence_wait", "vk.queue_present", "swapchain.acquire" };

bool QueryWaiting(const std::string& Leaf) noexcept
{
    for (const char* Waiting : WaitingLeaves)
    {
        if (Leaf == Waiting)
            return true;
    }
    return false;
}

// 📝 What the CPU works at in a refresh: the own time of every stretch that has stretches inside it, and the whole time of every stretch that has none
//    but does not wait. With the time it waits (and what lies outside any stretch) this is the refresh period.
double SumCpuWork(const std::vector<PerformanceLog::SeriesStatistics>& Series, bool& OutPresent)
{
    double Sum = 0.0;
    OutPresent = false;
    for (const PerformanceLog::SeriesStatistics& Entry : Series)
    {
        if (Entry.Kind == "cpu_self")
        {
            Sum += Entry.Mean;
            OutPresent = true;
        }
        else if (Entry.Kind == "cpu")
        {
            bool HasChild = false;
            for (const PerformanceLog::SeriesStatistics& Other : Series)
            {
                if (Other.Kind == "cpu" && Other.Path.size() > Entry.Path.size() && Other.Path.compare(0u, Entry.Path.size() + 1u, Entry.Path + "/") == 0)
                {
                    HasChild = true;
                    break;
                }
            }
            if (!HasChild && !QueryWaiting(ResolveLeaf(Entry.Path)))
            {
                Sum += Entry.Mean;
                OutPresent = true;
            }
        }
    }
    return Sum;
}

// 📝 One row of the table: which figure of a run it is.
enum class RowSource
{
    PeriodMean,
    PeriodP95,
    PeriodP99,
    PerSecond,
    CpuWork,
    CpuLeaves,
    GpuLeaves,
    CountLeaves,
};

struct RowDefinition
{
    const char* Caption;
    const char* Unit;
    RowSource   Source;
    const char* Leaves[4];   // [-] - the last names of the series that make the row; unused places are null
    bool        LowerIsBetter;
    bool        Indented;
};

const RowDefinition RowDefinitions[] = {
    { "Refresh period, mean",                    "ms", RowSource::PeriodMean,  { nullptr, nullptr, nullptr, nullptr },                    true,  false },
    { "Refresh period, 95th percentile",         "ms", RowSource::PeriodP95,   { nullptr, nullptr, nullptr, nullptr },                    true,  false },
    { "Refresh period, 99th percentile",         "ms", RowSource::PeriodP99,   { nullptr, nullptr, nullptr, nullptr },                    true,  false },
    { "Refreshes per second",                    "/s", RowSource::PerSecond,   { nullptr, nullptr, nullptr, nullptr },                    false, false },
    { "CPU working (not waiting)",               "ms", RowSource::CpuWork,     { nullptr, nullptr, nullptr, nullptr },                    true,  false },
    { "CPU waiting for the device or display",   "ms", RowSource::CpuLeaves,   { "vk.submit_wait", "vk.fence_wait", "vk.queue_present", "swapchain.acquire" }, true, false },
    { "Simulation step (CPU)",                   "ms", RowSource::CpuLeaves,   { "sim.step", nullptr, nullptr, nullptr },                 true,  true  },
    { "Solid atlas built",                       "ms", RowSource::CpuLeaves,   { "sim.atlas_build", nullptr, nullptr, nullptr },          true,  true  },
    { "Solid atlas uploaded",                    "ms", RowSource::CpuLeaves,   { "sim.atlas_upload", nullptr, nullptr, nullptr },         true,  true  },
    { "Solver recorded and run",                 "ms", RowSource::CpuLeaves,   { "sim.solver", nullptr, nullptr, nullptr },               true,  true  },
    { "Scene presented (CPU)",                   "ms", RowSource::CpuLeaves,   { "scene.present", nullptr, nullptr, nullptr },            true,  true  },
    { "Interface rendered and recorded",         "ms", RowSource::CpuLeaves,   { "ui.render", "ui.record", nullptr, nullptr },            true,  true  },
    { "Submissions per refresh",                 "",   RowSource::CountLeaves, { "vk.submits", nullptr, nullptr, nullptr },               true,  false },
    { "GPU: atlas copy",                         "ms", RowSource::GpuLeaves,   { "gpu.atlas_copy", nullptr, nullptr, nullptr },           true,  true  },
    { "GPU: solver",                             "ms", RowSource::GpuLeaves,   { "gpu.solver_step", nullptr, nullptr, nullptr },          true,  true  },
    { "GPU: scene",                              "ms", RowSource::GpuLeaves,   { "gpu.scene_world", nullptr, nullptr, nullptr },          true,  true  },
    { "GPU: smoke composite",                    "ms", RowSource::GpuLeaves,   { "gpu.plume_composite", nullptr, nullptr, nullptr },      true,  true  },
    { "GPU: interface",                          "ms", RowSource::GpuLeaves,   { "gpu.ui_draw", nullptr, nullptr, nullptr },              true,  true  },
};

// 📝 One figure of a run (or of the live window): false where the run recorded nothing of it.
bool ResolveRowValue(const PerformanceLog::SeriesStatistics& Period, const std::vector<PerformanceLog::SeriesStatistics>& Series, const RowDefinition& Row,
                     double& OutValue)
{
    bool Present = false;
    switch (Row.Source)
    {
        case RowSource::PeriodMean:  OutValue = Period.Mean;         return Period.Refreshes > 0u;
        case RowSource::PeriodP95:   OutValue = Period.Percentile95; return Period.Refreshes > 0u;
        case RowSource::PeriodP99:   OutValue = Period.Percentile99; return Period.Refreshes > 0u;
        case RowSource::PerSecond:   OutValue = Period.Mean > 0.0 ? 1000.0 / Period.Mean : 0.0; return Period.Refreshes > 0u;
        case RowSource::CpuWork:     OutValue = SumCpuWork(Series, Present);                    return Present;
        case RowSource::CpuLeaves:   OutValue = SumMeans(Series, "cpu", Row.Leaves, Present);   return Present;
        case RowSource::GpuLeaves:   OutValue = SumMeans(Series, "gpu", Row.Leaves, Present);   return Present;
        case RowSource::CountLeaves: OutValue = SumMeans(Series, "count", Row.Leaves, Present); return Present;
    }
    return false;
}

} // namespace

//----------------------------------------------------------------------------------------------------------------------
//                                                    THE COMPARISON
//----------------------------------------------------------------------------------------------------------------------

PerformanceComparison ComparePerformancePhases(const std::vector<PerformancePhaseResult>& Phases, const std::string& PresentMode)
{
    PerformanceComparison Out;
    std::vector<const PerformancePhaseResult*> Baseline, Optimized;
    for (const PerformancePhaseResult& Phase : Phases)
        (Phase.Optimized ? Optimized : Baseline).push_back(&Phase);

    if (Baseline.empty() || Optimized.empty())
    {
        Out.InvalidReason = "the test needs at least one run of each path";
        Out.Verdict       = "No result: " + Out.InvalidReason + ".";
        return Out;
    }

    // ---- is it a test at all: every run drew the scene and stepped the simulation on (nearly) every refresh
    Out.Valid = true;
    for (const PerformancePhaseResult& Phase : Phases)
    {
        const auto* Scene = FindByLeaf(Phase.Series, "cpu", "scene.present");
        const auto* Ticks = FindByLeaf(Phase.Series, "count", "sim.ticks");
        if (Phase.Period.Refreshes < 3u)
        {
            Out.Valid         = false;
            Out.InvalidReason = "run " + Phase.Label + " has only " + std::to_string(Phase.Period.Refreshes) + " measured refreshes";
        }
        else if (Scene == nullptr || Scene->Active * 10u < Scene->Refreshes * 9u)
        {
            Out.Valid         = false;
            Out.InvalidReason = "the scene was not drawn on most refreshes of run " + Phase.Label + " (the viewport hidden, or an editor open over it?)";
        }
        else if (Ticks == nullptr || Ticks->Active * 10u < Ticks->Refreshes * 9u)
        {
            Out.Valid         = false;
            Out.InvalidReason = "the simulation did not step on most refreshes of run " + Phase.Label;
        }
        if (!Out.Valid)
            break;
    }

    // ---- the pictures: every run against the first
    const PerformancePhaseResult& Reference = Phases.front();
    int  Mismatches        = 0;
    bool BaselineRepeats   = true;
    bool SizeChanged       = false;
    for (size_t PhaseIndex = 1u; PhaseIndex < Phases.size(); ++PhaseIndex)
    {
        const PerformancePhaseResult& Phase = Phases[PhaseIndex];
        if (Phase.PictureWidth != Reference.PictureWidth || Phase.PictureHeight != Reference.PictureHeight)
        {
            SizeChanged = true;
            continue;
        }
        for (size_t Checkpoint = 0u; Checkpoint < Phase.PictureHashes.size() && Checkpoint < Reference.PictureHashes.size(); ++Checkpoint)
        {
            if (Phase.PictureHashes[Checkpoint] == 0u || Reference.PictureHashes[Checkpoint] == 0u)
                continue;
            ++Out.PicturesChecked;
            if (Phase.PictureHashes[Checkpoint] != Reference.PictureHashes[Checkpoint])
            {
                ++Mismatches;
                if (Phase.Optimized == Reference.Optimized)
                    BaselineRepeats = false;
            }
        }
    }
    Out.PicturesCompared  = Out.PicturesChecked > 0 && !SizeChanged;
    Out.PicturesIdentical = Out.PicturesCompared && Mismatches == 0;
    Out.BaselineRepeats   = BaselineRepeats;

    // ---- the table: the mean of each figure over the runs of a path
    for (const RowDefinition& Row : RowDefinitions)
    {
        double BaselineSum = 0.0, OptimizedSum = 0.0;
        int    BaselineCount = 0, OptimizedCount = 0;
        for (const PerformancePhaseResult* Phase : Baseline)
        {
            double Value = 0.0;
            if (ResolveRowValue(Phase->Period, Phase->Series, Row, Value)) { BaselineSum += Value; ++BaselineCount; }
        }
        for (const PerformancePhaseResult* Phase : Optimized)
        {
            double Value = 0.0;
            if (ResolveRowValue(Phase->Period, Phase->Series, Row, Value)) { OptimizedSum += Value; ++OptimizedCount; }
        }
        if (BaselineCount == 0 || OptimizedCount == 0)
            continue;
        PerformanceComparisonRow Entry{};
        Entry.Caption       = Row.Caption;
        Entry.Unit          = Row.Unit;
        Entry.Baseline      = BaselineSum / BaselineCount;
        Entry.Optimized     = OptimizedSum / OptimizedCount;
        Entry.LowerIsBetter = Row.LowerIsBetter;
        Entry.Indented      = Row.Indented;
        Out.Rows.push_back(Entry);
    }

    // ---- the refresh period, and how much two runs of the same path differ
    auto MeanOfMeans = [](const std::vector<const PerformancePhaseResult*>& Runs) {
        double Sum = 0.0;
        for (const PerformancePhaseResult* Run : Runs)
            Sum += Run->Period.Mean;
        return Sum / static_cast<double>(Runs.size());
    };
    auto SpreadOf = [&](const std::vector<const PerformancePhaseResult*>& Runs, double Mean) {
        if (Runs.size() < 2u || Mean <= 0.0)
            return 0.0;
        double Low = Runs.front()->Period.Mean, High = Low;
        for (const PerformancePhaseResult* Run : Runs)
        {
            Low  = std::min(Low, Run->Period.Mean);
            High = std::max(High, Run->Period.Mean);
        }
        return (High - Low) / Mean;
    };
    const double BaselineMean  = MeanOfMeans(Baseline);
    const double OptimizedMean = MeanOfMeans(Optimized);
    Out.PeriodChange           = BaselineMean > 0.0 ? (OptimizedMean - BaselineMean) / BaselineMean : 0.0;
    Out.SpreadKnown            = Baseline.size() >= 2u || Optimized.size() >= 2u;
    Out.Spread                 = std::max(SpreadOf(Baseline, BaselineMean), SpreadOf(Optimized, OptimizedMean));

    // ---- the words
    char Line[512];
    if (!Out.Valid)
    {
        Out.Verdict = "Not a valid test: " + Out.InvalidReason + ".";
    }
    else if (Out.PicturesCompared && !Out.PicturesIdentical)
    {
        std::snprintf(Line, sizeof(Line),
                      "The two paths did not draw the same pictures (%d of %d comparisons differ%s). The timing below is not to be trusted until that is understood.",
                      Mismatches, Out.PicturesChecked, Out.BaselineRepeats ? "" : "; the baseline did not even repeat itself, so the scene is not deterministic on this machine");
        Out.Verdict = Line;
    }
    else
    {
        const double Threshold = std::max(0.02, Out.SpreadKnown ? 1.5 * Out.Spread : 0.05);
        if (std::fabs(Out.PeriodChange) <= Threshold)
        {
            if (Out.SpreadKnown)
                std::snprintf(Line, sizeof(Line),
                              "No clear difference: the mean refresh period is %.2f ms on the baseline and %.2f ms optimized (%s). Two runs of one path differ by up to "
                              "%.1f %%, so a change under %.1f %% cannot be told from noise.",
                              BaselineMean, OptimizedMean, FormatChange(Out.PeriodChange).c_str(), Out.Spread * 100.0, Threshold * 100.0);
            else
                std::snprintf(Line, sizeof(Line),
                              "No clear difference: the mean refresh period is %.2f ms on the baseline and %.2f ms optimized (%s). One run of each path cannot call a "
                              "change under %.0f %%.",
                              BaselineMean, OptimizedMean, FormatChange(Out.PeriodChange).c_str(), Threshold * 100.0);
        }
        else if (Out.PeriodChange < 0.0)
        {
            std::snprintf(Line, sizeof(Line), "Optimized execution is faster on this machine: the mean refresh period goes from %.2f ms to %.2f ms (%s); the runs of one path differ by %.1f %%.",
                          BaselineMean, OptimizedMean, FormatChange(Out.PeriodChange).c_str(), Out.Spread * 100.0);
        }
        else
        {
            std::snprintf(Line, sizeof(Line), "Optimized execution is slower on this machine: the mean refresh period goes from %.2f ms to %.2f ms (%s); the runs of one path differ by %.1f %%.",
                          BaselineMean, OptimizedMean, FormatChange(Out.PeriodChange).c_str(), Out.Spread * 100.0);
        }
        Out.Verdict = Line;
    }

    // [unit] - merging the submissions moves the wait for the device from the simulation's row to the scene's: the rows of the CPU are not independent
    double BaselineSubmissions = 0.0, OptimizedSubmissions = 0.0;
    for (const PerformanceComparisonRow& Row : Out.Rows)
    {
        if (Row.Caption == "Submissions per refresh")
        {
            BaselineSubmissions  = Row.Baseline;
            OptimizedSubmissions = Row.Optimized;
        }
    }
    if (OptimizedSubmissions > 0.0 && OptimizedSubmissions < BaselineSubmissions)
        Out.Notes.push_back("The CPU rows trade with each other: with the submissions merged the wait for the device is taken in the scene's row, not the "
                            "simulation's. The refresh period, CPU working, CPU waiting and the GPU rows are the totals.");
    if (Out.PicturesCompared && Out.PicturesIdentical)
    {
        std::snprintf(Line, sizeof(Line), "Pictures: identical at all %d comparisons across %zu runs.", Out.PicturesChecked, Phases.size());
        Out.Notes.push_back(Line);
    }
    else if (SizeChanged)
        Out.Notes.push_back("Pictures: not compared, the viewport changed size during the test.");
    else if (!Out.PicturesCompared && Out.PicturesChecked == 0)
        Out.Notes.push_back("Pictures: not compared.");
    if (PresentMode == "FIFO" || PresentMode == "FIFO_RELAXED")
        Out.Notes.push_back("Presentation waits for the display (" + PresentMode + "): the refresh period cannot go below the display's, so read the CPU and GPU rows.");
    if (!Out.SpreadKnown)
        Out.Notes.push_back("One run of each path: nothing here separates a difference from noise. Use two rounds or more.");
    if (Baseline.size() != Optimized.size())
        Out.Notes.push_back("The paths ran a different number of times.");
    return Out;
}

std::string ConstructPerformanceReport(const PerformanceTestResult& Result, const PerformanceComparison& Comparison)
{
    std::ostringstream Text;
    char               Line[512];
    Text << "Frontier performance A/B test\n";
    Text << "=============================\n";
    Text << "Device:        " << Result.Device << "\n";
    Text << "Present mode:  " << Result.PresentMode << "\n";
    Text << "Scene:         " << Result.Scene << "\n";
#if defined(FRONTIER_BUILD_REVISION)
    Text << "Build:         " << FRONTIER_BUILD_REVISION;
#else
    Text << "Build:         unknown";
#endif
#if defined(NDEBUG)
    Text << " (release)\n";
#else
    Text << " (NOT a release build: the figures are not those of one)\n";
#endif
    std::snprintf(Line, sizeof(Line), "Runs:          %zu (%d rounds), each %d warm-up and %d measured refreshes, fixed step 1/60 s, scripted drive, fixed camera\n",
                  Result.Phases.size(), Result.Settings.Rounds, Result.Settings.WarmupRefreshes, Result.Settings.MeasuredRefreshes);
    Text << Line;
    Text << "Order:         ";
    for (size_t Index = 0u; Index < Result.Phases.size(); ++Index)
        Text << (Index > 0u ? ", " : "") << (Result.Phases[Index].Optimized ? "optimized" : "baseline");
    Text << "\n\n";

    Text << "Verdict\n-------\n" << Comparison.Verdict << "\n";
    for (const std::string& Note : Comparison.Notes)
        Text << "- " << Note << "\n";
    Text << "\n";

    Text << "Means over the runs of each path\n--------------------------------\n";
    std::snprintf(Line, sizeof(Line), "%-40s %12s %12s %10s\n", "", "baseline", "optimized", "change");
    Text << Line;
    for (const PerformanceComparisonRow& Row : Comparison.Rows)
    {
        const double Change = Row.Baseline != 0.0 ? (Row.Optimized - Row.Baseline) / Row.Baseline : 0.0;
        const std::string Caption = std::string(Row.Indented ? "  " : "") + Row.Caption + (Row.Unit.empty() ? "" : " (" + Row.Unit + ")");
        std::snprintf(Line, sizeof(Line), "%-40s %12.3f %12.3f %10s\n", Caption.c_str(), Row.Baseline, Row.Optimized, FormatChange(Change).c_str());
        Text << Line;
    }
    Text << "\n";

    Text << "Every run\n---------\n";
    std::snprintf(Line, sizeof(Line), "%-10s %-10s %10s %10s %10s %10s %10s  %s\n", "run", "path", "mean ms", "p50 ms", "p95 ms", "p99 ms", "max ms", "pictures");
    Text << Line;
    for (const PerformancePhaseResult& Phase : Result.Phases)
    {
        std::string Pictures;
        for (const uint64_t Hash : Phase.PictureHashes)
        {
            char Piece[24];
            std::snprintf(Piece, sizeof(Piece), "%08llx ", static_cast<unsigned long long>(Hash & 0xFFFFFFFFull));
            Pictures += Piece;
        }
        std::snprintf(Line, sizeof(Line), "%-10s %-10s %10.3f %10.3f %10.3f %10.3f %10.3f  %s\n", Phase.Label.c_str(), Phase.Optimized ? "optimized" : "baseline",
                      Phase.Period.Mean, Phase.Period.Median, Phase.Period.Percentile95, Phase.Period.Percentile99, Phase.Period.Maximum, Pictures.c_str());
        Text << Line;
    }
    Text << "\n";

    // [unit] - every series of the runs, for whoever reads the report to dig: the mean over the runs of each path
    Text << "Every series (mean per refresh over the runs of each path)\n----------------------------------------------------------\n";
    std::vector<std::string> Paths;
    for (const PerformancePhaseResult& Phase : Result.Phases)
    {
        for (const PerformanceLog::SeriesStatistics& Entry : Phase.Series)
        {
            if (std::find(Paths.begin(), Paths.end(), Entry.Path) == Paths.end())
                Paths.push_back(Entry.Path);
        }
    }
    std::snprintf(Line, sizeof(Line), "%-62s %-8s %11s %11s %9s\n", "series", "kind", "baseline", "optimized", "change");
    Text << Line;
    for (const std::string& Path : Paths)
    {
        double      Sums[2]   = { 0.0, 0.0 };
        int         Counts[2] = { 0, 0 };
        std::string Kind;
        for (const PerformancePhaseResult& Phase : Result.Phases)
        {
            for (const PerformanceLog::SeriesStatistics& Entry : Phase.Series)
            {
                if (Entry.Path != Path)
                    continue;
                Kind = Entry.Kind;
                Sums[Phase.Optimized ? 1 : 0] += Entry.Mean;
                ++Counts[Phase.Optimized ? 1 : 0];
            }
        }
        const double Baseline  = Counts[0] > 0 ? Sums[0] / Counts[0] : 0.0;
        const double Optimized = Counts[1] > 0 ? Sums[1] / Counts[1] : 0.0;
        const std::string Change = (Counts[0] > 0 && Counts[1] > 0 && Baseline != 0.0) ? FormatChange((Optimized - Baseline) / Baseline) : std::string("-");
        std::snprintf(Line, sizeof(Line), "%-62s %-8s %11.4f %11.4f %9s\n", Path.c_str(), Kind.c_str(), Baseline, Optimized, Change.c_str());
        Text << Line;
    }
    Text << "\n";

    Text << "Files\n-----\n";
    for (const PerformancePhaseResult& Phase : Result.Phases)
        Text << Phase.Label << ": " << (Phase.LogPath.empty() ? std::string("(not written)") : Phase.LogPath) << "\n";
    return Text.str();
}

//----------------------------------------------------------------------------------------------------------------------
//                                                    CONSTRUCTION
//----------------------------------------------------------------------------------------------------------------------

PerformancePanel::PerformancePanel(VulkanContext& NewContext, EverestPlumeSceneRuntime& NewScene, WorkspaceHooks NewHooks)
    : Context(NewContext)
    , Scene(NewScene)
    , Hooks(std::move(NewHooks))
{
    UncappedChoice  = Context.QueryUncappedPresentEnabled();
    TimestampChoice = Context.QueryGpuTimestampsEnabled();
}

PerformancePanel::~PerformancePanel()
{
    if (Stage == TestStage::Running)
        AbortTest();
    if (LiveRecording)
        EndLiveRecording();
}

//----------------------------------------------------------------------------------------------------------------------
//                                                       THE LOOP
//----------------------------------------------------------------------------------------------------------------------

void PerformancePanel::ApplyRequestedMode()
{
    if (!RequestPending)
        return;
    RequestPending = false;
    if (Stage == TestStage::Running)
        return;   // [unit] - the test chooses the path of every run
    if (OptimizedExecution::QueryEnabled() == RequestedOptimized)
        return;
    OptimizedExecution::Configure(RequestedOptimized);
    // [unit] - the live figures belong to one path: a new run starts with the new one
    if (LiveRecording)
        EndLiveRecording();
}

void PerformancePanel::BeginLiveRecording()
{
    PerformanceRunDescription Description{};
    Description.Label           = "live";
    Description.OutputDirectory = Settings.OutputDirectory;
    PerformanceLog::Instance().Configure(Description);
    LiveRecording = true;
    LiveCountdown = 0;
    LiveSeries.clear();
    LivePlot.clear();
    LivePeriod = {};
}

void PerformancePanel::EndLiveRecording()
{
    PerformanceLog::Instance().Discard();
    LiveRecording = false;
}

bool PerformancePanel::RefreshLiveFigures()
{
    PerformanceLog& Log = PerformanceLog::Instance();
    if (Log.QueryClosedRefreshCount() < 8u)
        return false;
    LiveSeries = Log.ComputeStatisticsLast(300u, &LivePeriod);
    const std::vector<double> Recent = Log.QueryRecentPeriods(240u);
    LivePlot.assign(Recent.begin(), Recent.end());
    return true;
}

void PerformancePanel::BeginRefresh()
{
    ApplyRequestedMode();
    if (Stage == TestStage::Running)
    {
        AdvanceTest();
    }
    else
    {
        if (WindowOpen && !LiveRecording)
            BeginLiveRecording();
        else if (!WindowOpen && LiveRecording)
            EndLiveRecording();
        if (LiveRecording && --LiveCountdown <= 0)
            LiveCountdown = RefreshLiveFigures() ? 20 : 1;
    }
    PerformanceLog::Instance().BeginRefresh();
    if (Stage == TestStage::Running && Hooks.ConfigureTestCamera)
        Hooks.ConfigureTestCamera();
}

void PerformancePanel::EndRefresh()
{
    if (Stage != TestStage::Running || !PhaseActive || !Settings.PicturesEnabled)
        return;
    const int Checkpoint = ResolveCheckpointIndex(CurrentRefresh);
    if (Checkpoint < 0 || static_cast<size_t>(Checkpoint) >= PhaseHashes.size())
        return;
    // [unit] - the read-back is not part of what is measured
    PerformanceLog::Instance().ExcludeOpenRefresh();
    std::vector<uint8_t> Pixels;
    int                  Width = 0, Height = 0;
    if (Scene.ReadSceneColor(Context, Pixels, Width, Height) && !Pixels.empty())
    {
        PhaseHashes[static_cast<size_t>(Checkpoint)] = HashBytes(Pixels.data(), Pixels.size());
        PhasePictureWidth                           = Width;
        PhasePictureHeight                          = Height;
    }
}

void PerformancePanel::ResolveSimulationStep(SimulationStep& Step) const
{
    if (Stage != TestStage::Running || !PhaseActive)
        return;
    Step = ResolveScriptedStep(CurrentRefresh);
}

int PerformancePanel::ResolveCheckpointIndex(int Refresh) const noexcept
{
    for (size_t Index = 0u; Index < Checkpoints.size(); ++Index)
    {
        if (Checkpoints[Index] == Refresh)
            return static_cast<int>(Index);
    }
    return -1;
}

//----------------------------------------------------------------------------------------------------------------------
//                                                       THE TEST
//----------------------------------------------------------------------------------------------------------------------

void PerformancePanel::StartTest()
{
    if (Stage == TestStage::Running)
        return;

    Result                    = PerformanceTestResult{};
    Comparison                = PerformanceComparison{};
    Settings.Rounds           = std::clamp(Settings.Rounds, 1, 4);
    Settings.WarmupRefreshes  = std::clamp(Settings.WarmupRefreshes, 1, 600);
    Settings.MeasuredRefreshes = std::clamp(Settings.MeasuredRefreshes, 8, 6000);
    Result.Settings           = Settings;
    Result.Device             = Context.QueryDeviceSummary();
    Result.Scene              = Scene.IsProvingGroundsMode() ? "Proving Grounds" : "Lobby";
    Result.PresentMode        = Context.QueryPresentModeName();

    // [unit] - baseline then optimized in the first round, the other way round in the second: with two rounds each path has one early and one late run,
    //          so that a machine that warms up or throttles over the minute does not favour one of them
    Plan.clear();
    for (int Round = 0; Round < Settings.Rounds; ++Round)
    {
        const bool Flip = (Round % 2) == 1;
        Plan.push_back({ Flip, Round + 1 });
        Plan.push_back({ !Flip, Round + 1 });
    }

    Checkpoints.clear();
    static const int Shares[5] = { 15, 35, 55, 75, 95 };
    const int        Last      = Settings.WarmupRefreshes + Settings.MeasuredRefreshes - 1;
    for (const int Share : Shares)
    {
        const int At = std::clamp(Settings.WarmupRefreshes + Settings.MeasuredRefreshes * Share / 100, Settings.WarmupRefreshes, Last);
        if (Checkpoints.empty() || At > Checkpoints.back())
            Checkpoints.push_back(At);
    }

    SavedOptimized  = OptimizedExecution::QueryEnabled();
    SavedUncapped   = Context.QueryUncappedPresentEnabled();
    SavedTimestamps = Context.QueryGpuTimestampsEnabled();
    SavedSimulation = Hooks.QuerySimulationActive ? Hooks.QuerySimulationActive() : false;
    if (Settings.UncappedEnabled)
        Context.ConfigureUncappedPresent(true);
    if (Settings.GpuTimestampsEnabled)
        Context.ConfigureGpuTimestamps(true);
    if (Hooks.ConfigureSimulationActive && !SavedSimulation)
        Hooks.ConfigureSimulationActive(true);
    if (LiveRecording)
        EndLiveRecording();

    RequestPending       = false;
    SelectTestTabPending = true;
    Stage                = TestStage::Running;
    PhaseIndex      = 0;
    PhaseActive     = false;
    PhaseRefreshes  = 0;
    CurrentRefresh  = 0;
    ReportCopied    = false;
}

void PerformancePanel::AdvanceTest()
{
    if (!PhaseActive)
    {
        BeginTestPhase();
    }
    else if (PhaseRefreshes >= QueryPlannedRefreshes())
    {
        EndTestPhase(true);
        ++PhaseIndex;
        if (PhaseIndex >= static_cast<int>(Plan.size()))
        {
            FinishTest();
            return;
        }
        BeginTestPhase();
    }
    CurrentRefresh = PhaseRefreshes++;
}

void PerformancePanel::RegisterPhaseEnvironment(int PhaseNumber)
{
    PerformanceLog&                  Log   = PerformanceLog::Instance();
    const VkPhysicalDeviceProperties& Props = Context.QueryDeviceProperties();
    Log.RegisterEnvironment("device", Context.QueryDeviceSummary());
    Log.RegisterEnvironment("present_mode", Context.QueryPresentModeName());
    Log.RegisterEnvironment("gpu_timestamps", Context.QueryGpuTimestampsEnabled() ? "on" : (Context.QueryGpuTimestampsAvailable() ? "off" : "not supported"));
    Log.RegisterEnvironment("scene", Scene.IsProvingGroundsMode() ? "Proving Grounds" : "Lobby");
    Log.RegisterEnvironment("test", "A/B run " + std::to_string(PhaseNumber) + " of " + std::to_string(Plan.size()));
    Log.RegisterEnvironmentNumber("test_rounds", static_cast<double>(Settings.Rounds));
    Log.RegisterEnvironmentNumber("test_warmup_refreshes", static_cast<double>(Settings.WarmupRefreshes));
    Log.RegisterEnvironmentNumber("test_measured_refreshes", static_cast<double>(Settings.MeasuredRefreshes));
    Log.RegisterEnvironmentNumber("test_step_seconds", ScriptStepSeconds);
    Log.RegisterEnvironmentNumber("gpu_vendor_id", static_cast<double>(Props.vendorID));
    Log.RegisterEnvironmentNumber("gpu_device_id", static_cast<double>(Props.deviceID));
    Log.RegisterEnvironmentNumber("gpu_driver_version", static_cast<double>(Props.driverVersion));
    Log.RegisterEnvironmentNumber("vulkan_api_version", static_cast<double>(Props.apiVersion));
    Log.RegisterEnvironmentNumber("world_lattice_resolution", static_cast<double>(Scene.AccessWorldSolver().QueryLatticeResolution()));
    Log.RegisterEnvironmentNumber("world_pressure_iterations", static_cast<double>(Scene.AccessWorldSettings().PressureIterations));
    Log.RegisterEnvironmentNumber("world_raymarch_steps", static_cast<double>(Scene.AccessWorldSettings().RaymarchSteps));
}

void PerformancePanel::BeginTestPhase()
{
    const PhasePlan& Phase = Plan[static_cast<size_t>(PhaseIndex)];
    OptimizedExecution::Configure(Phase.Optimized);

    PerformanceRunDescription Description{};
    Description.Label            = "ab-" + std::to_string(PhaseIndex + 1) + "-of-" + std::to_string(Plan.size()) + "-" + (Phase.Optimized ? "optimized" : "baseline");
    Description.Notes            = "F2 panel A/B test, round " + std::to_string(Phase.Round) + " of " + std::to_string(Settings.Rounds);
    Description.OutputDirectory  = Settings.OutputDirectory;
    Description.BenchmarkEnabled = true;
    Description.WarmupRefreshes  = Settings.WarmupRefreshes;
    Description.PlannedRefreshes = QueryPlannedRefreshes();
    PerformanceLog::Instance().Configure(Description);
    RegisterPhaseEnvironment(PhaseIndex + 1);

    PhaseActive        = true;
    PhaseRefreshes     = 0;
    CurrentRefresh     = 0;
    PhaseHashes.assign(Checkpoints.size(), 0u);
    PhasePictureWidth  = 0;
    PhasePictureHeight = 0;
}

void PerformancePanel::EndTestPhase(bool Keep)
{
    PerformanceLog& Log = PerformanceLog::Instance();
    Log.RegisterEnvironment("present_mode", Context.QueryPresentModeName());
    Log.RegisterEnvironmentNumber("picture_width", static_cast<double>(PhasePictureWidth));
    Log.RegisterEnvironmentNumber("picture_height", static_cast<double>(PhasePictureHeight));
    for (size_t Index = 0u; Index < PhaseHashes.size(); ++Index)
    {
        char Hash[24];
        std::snprintf(Hash, sizeof(Hash), "%016llx", static_cast<unsigned long long>(PhaseHashes[Index]));
        Log.RegisterEnvironment("picture_hash_refresh_" + std::to_string(Checkpoints[Index]), Hash);
    }
    Result.PresentMode = Context.QueryPresentModeName();

    std::string Path, Summary;
    const bool  Written = Log.Finalize(Path, Summary);
    if (Keep)
    {
        const PhasePlan&       Planned = Plan[static_cast<size_t>(PhaseIndex)];
        PerformancePhaseResult Phase{};
        Phase.Label         = std::to_string(PhaseIndex + 1) + " of " + std::to_string(Plan.size());
        Phase.Optimized     = Planned.Optimized;
        Phase.Round         = Planned.Round;
        Phase.Period        = Log.ComputeRefreshPeriodStatistics();
        Phase.Series        = Log.ComputeStatistics();
        Phase.PictureHashes = PhaseHashes;
        Phase.PictureWidth  = PhasePictureWidth;
        Phase.PictureHeight = PhasePictureHeight;
        Phase.LogPath       = Written ? Path : std::string();
        Result.Phases.push_back(std::move(Phase));
    }
    PhaseActive = false;
}

void PerformancePanel::RestoreAfterTest()
{
    OptimizedExecution::Configure(SavedOptimized);
    Context.ConfigureUncappedPresent(SavedUncapped);
    Context.ConfigureGpuTimestamps(SavedTimestamps);
    if (Hooks.ConfigureSimulationActive && !SavedSimulation)
        Hooks.ConfigureSimulationActive(false);
    if (Hooks.RestoreCamera)
        Hooks.RestoreCamera();
}

void PerformancePanel::FinishTest()
{
    RestoreAfterTest();
    Result.Finished   = true;
    Comparison        = ComparePerformancePhases(Result.Phases, Result.PresentMode);
    Result.ReportText = ConstructPerformanceReport(Result, Comparison);

    std::error_code Error;
    std::filesystem::create_directories(std::filesystem::path(Settings.OutputDirectory), Error);
    const std::filesystem::path ReportPath = std::filesystem::path(Settings.OutputDirectory) / (ResolveUtcStamp() + "_ab-report.txt");
    {
        std::ofstream File(ReportPath, std::ios::binary | std::ios::trunc);
        if (File)
        {
            File << Result.ReportText;
            if (File)
                Result.ReportPath = ReportPath.string();
        }
    }
    Stage = TestStage::Finished;
}

void PerformancePanel::AbortTest()
{
    if (Stage != TestStage::Running)
        return;
    if (PhaseActive)
        EndTestPhase(false);
    RestoreAfterTest();
    Result.Aborted = true;
    Result.Failure = "Stopped before the end: no result.";
    Stage          = TestStage::Idle;
}

//----------------------------------------------------------------------------------------------------------------------
//                                                      THE WINDOW
//----------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr ImU32 InkGood    = IM_COL32(110, 205, 140, 255);
constexpr ImU32 InkWarning = IM_COL32(235, 190, 90, 255);
constexpr ImU32 InkBad     = IM_COL32(240, 110, 100, 255);
constexpr ImU32 InkMuted   = IM_COL32(150, 154, 162, 255);

// 📝 A switch: a rounded track with a knob, on in green. Returns true on the click that flips it.
bool ConstructSwitch(const char* Identifier, bool& On, float Unit)
{
    const float  Height = Unit * 1.7f;
    const float  Width  = Height * 1.9f;
    const ImVec2 Corner = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(Identifier, ImVec2(Width, Height));
    const bool Clicked = ImGui::IsItemClicked();
    if (Clicked)
        On = !On;
    ImDrawList* List  = ImGui::GetWindowDrawList();
    const ImU32 Track = On ? IM_COL32(46, 150, 92, 255) : IM_COL32(78, 80, 86, 255);
    List->AddRectFilled(Corner, ImVec2(Corner.x + Width, Corner.y + Height), ImGui::IsItemHovered() ? Track + IM_COL32(12, 12, 12, 0) : Track, Height * 0.5f);
    const float Radius = Height * 0.5f - 3.0f;
    const float KnobX  = On ? Corner.x + Width - Height * 0.5f : Corner.x + Height * 0.5f;
    List->AddCircleFilled(ImVec2(KnobX, Corner.y + Height * 0.5f), Radius, IM_COL32(244, 244, 246, 255));
    return Clicked;
}

ImU32 ResolveChangeInk(double Change, bool LowerIsBetter)
{
    if (std::fabs(Change) < 0.02)
        return InkMuted;
    const bool Better = LowerIsBetter ? (Change < 0.0) : (Change > 0.0);
    return Better ? InkGood : InkBad;
}

void ConstructMutedWrapped(const char* Text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, InkMuted);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(Text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

} // namespace

void PerformancePanel::ConstructSwitchSection(float Unit)
{
    bool On = QueryOptimizedRequested();
    if (ConstructSwitch("##optimized", On, Unit))
        RequestOptimized(On);
    ImGui::SameLine(0.0f, Unit * 0.8f);
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Optimized execution");
    ImGui::PushStyleColor(ImGuiCol_Text, On ? InkGood : InkMuted);
    ImGui::TextUnformatted(On ? "ON: merged submissions, retained course distances" : "OFF: the original path");
    ImGui::PopStyleColor();
    ImGui::EndGroup();
    ImGui::Spacing();
    ConstructMutedWrapped("Takes effect on the next refresh. The optimized path draws the same pictures (the A/B test compares them) with fewer waits on "
                          "the device and less recomputing of the solid atlas.");
}

void PerformancePanel::ConstructLiveSection(float Unit)
{
    ImGui::PushStyleColor(ImGuiCol_Text, InkMuted);
    ImGui::Text("The last 300 refreshes, on the %s path.", OptimizedExecution::QueryModeName());
    ImGui::PopStyleColor();
    if (!LiveRecording || LivePeriod.Refreshes < 8u)
    {
        ImGui::TextUnformatted("Collecting...");
        return;
    }

    const double Mean = LivePeriod.Mean;
    ImGui::PushFont(nullptr, ImGui::GetFontSize() * 1.6f);
    ImGui::Text("%.2f ms", Mean);
    ImGui::PopFont();
    ImGui::SameLine(0.0f, Unit);
    ImGui::BeginGroup();
    ImGui::Text("%.0f refreshes per second", Mean > 0.0 ? 1000.0 / Mean : 0.0);
    ImGui::PushStyleColor(ImGuiCol_Text, InkMuted);
    ImGui::Text("p95 %.2f   p99 %.2f   max %.2f ms", LivePeriod.Percentile95, LivePeriod.Percentile99, LivePeriod.Maximum);
    ImGui::PopStyleColor();
    ImGui::EndGroup();

    if (LivePlot.size() >= 2u)
    {
        float Peak = 1.0f;
        for (const float Sample : LivePlot)
            Peak = std::max(Peak, Sample);
        ImGui::PlotLines("##period", LivePlot.data(), static_cast<int>(LivePlot.size()), 0, nullptr, 0.0f, Peak * 1.1f, ImVec2(-1.0f, Unit * 3.2f));
    }

    if (ImGui::BeginTable("##live", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
    {
        ImGui::TableSetupColumn("per refresh", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("mean", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("p95", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();
        for (const RowDefinition& Row : RowDefinitions)
        {
            if (Row.Source == RowSource::PeriodMean || Row.Source == RowSource::PeriodP95 || Row.Source == RowSource::PeriodP99 || Row.Source == RowSource::PerSecond)
                continue;
            double MeanValue = 0.0;
            if (!ResolveRowValue(LivePeriod, LiveSeries, Row, MeanValue))
                continue;
            // [unit] - the 95th percentile of a sum of series is not the sum of theirs: only a row of one series shows it
            double P95 = -1.0;
            if (Row.Leaves[0] != nullptr && Row.Leaves[1] == nullptr)
            {
                const char* Kind = Row.Source == RowSource::GpuLeaves ? "gpu" : (Row.Source == RowSource::CountLeaves ? "count" : "cpu");
                if (const auto* Entry = FindByLeaf(LiveSeries, Kind, Row.Leaves[0]))
                    P95 = Entry->Percentile95;
            }
            const bool Count = Row.Source == RowSource::CountLeaves;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s%s", Row.Indented ? "   " : "", Row.Caption);
            ImGui::TableNextColumn();
            ImGui::Text(Count ? "%.2f" : "%.3f", MeanValue);
            ImGui::TableNextColumn();
            if (P95 >= 0.0)
                ImGui::Text(Count ? "%.2f" : "%.3f", P95);
        }
        ImGui::EndTable();
    }
}

void PerformancePanel::ConstructComparisonTable(float Unit)
{
    (void)Unit;
    if (!ImGui::BeginTable("##comparison", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
        return;
    ImGui::TableSetupColumn("mean over the runs", ImGuiTableColumnFlags_WidthStretch, 3.4f);
    ImGui::TableSetupColumn("baseline", ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn("optimized", ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn("change", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableHeadersRow();
    for (const PerformanceComparisonRow& Row : Comparison.Rows)
    {
        const double Change = Row.Baseline != 0.0 ? (Row.Optimized - Row.Baseline) / Row.Baseline : 0.0;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%s%s", Row.Indented ? "   " : "", Row.Caption.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", Row.Baseline);
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", Row.Optimized);
        ImGui::TableNextColumn();
        ImGui::PushStyleColor(ImGuiCol_Text, ResolveChangeInk(Change, Row.LowerIsBetter));
        ImGui::TextUnformatted(FormatChange(Change).c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndTable();
}

void PerformancePanel::ConstructTestSection(float Unit)
{
    if (Stage == TestStage::Running)
    {
        const int   Total     = static_cast<int>(Plan.size());
        const int   Planned   = QueryPlannedRefreshes();
        const float Fraction  = Total > 0 ? (static_cast<float>(PhaseIndex) + std::min(1.0f, static_cast<float>(PhaseRefreshes) / static_cast<float>(Planned))) / static_cast<float>(Total) : 0.0f;
        const bool  Optimized = PhaseIndex < Total && Plan[static_cast<size_t>(PhaseIndex)].Optimized;
        char        Caption[96];
        std::snprintf(Caption, sizeof(Caption), "run %d of %d  -  %s  -  refresh %d / %d", std::min(PhaseIndex + 1, Total), Total, Optimized ? "optimized" : "baseline", PhaseRefreshes, Planned);
        ImGui::ProgressBar(Fraction, ImVec2(-1.0f, Unit * 1.6f), Caption);
        ConstructMutedWrapped("Leave the window alone: the camera is held and the car drives itself. Changing the scene or hiding the viewport spoils the test.");
        if (ImGui::Button("Stop the test"))
            AbortTest();
        return;
    }

    const int   Runs    = Settings.Rounds * 2;
    const float Seconds = static_cast<float>(Runs * (Settings.WarmupRefreshes + Settings.MeasuredRefreshes + 1)) / 60.0f;
    if (ImGui::Button("Run the A/B test", ImVec2(Unit * 12.0f, Unit * 2.0f)))
        StartTest();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, InkMuted);
    ImGui::Text("%d runs, %s  -  %.0f s or less", Runs, Settings.Rounds >= 2 ? "baseline, optimized, optimized, baseline, ..." : "baseline, optimized", Seconds);
    ImGui::PopStyleColor();
    ConstructMutedWrapped("The same scripted drive (burnout, drive, brake, circle) at a fixed step with a fixed camera, on both paths, in the scene chosen under "
                          "Settings.");

    if (Result.Aborted)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, InkWarning);
        ImGui::TextUnformatted(Result.Failure.c_str());
        ImGui::PopStyleColor();
    }
    if (!Result.Finished)
        return;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    const double Threshold  = std::max(0.02, Comparison.SpreadKnown ? 1.5 * Comparison.Spread : 0.05);
    const ImU32  VerdictInk = !Comparison.Valid ? InkBad
                              : (Comparison.PicturesCompared && !Comparison.PicturesIdentical) ? InkBad
                              : (std::fabs(Comparison.PeriodChange) <= Threshold) ? InkWarning
                              : (Comparison.PeriodChange < 0.0 ? InkGood : InkBad);
    ImGui::PushStyleColor(ImGuiCol_Text, VerdictInk);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(Comparison.Verdict.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    for (const std::string& Note : Comparison.Notes)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, InkMuted);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::Text("- %s", Note.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    ConstructComparisonTable(Unit);
    ImGui::Spacing();
    if (ImGui::Button(ReportCopied ? "Copied" : "Copy the report"))
    {
        ImGui::SetClipboardText(Result.ReportText.c_str());
        ReportCopied = true;
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, InkMuted);
    ImGui::TextUnformatted(Result.ReportPath.empty() ? "The report could not be written to disk." : "Saved, with one TOML log per run, in:");
    ImGui::PopStyleColor();
    if (!Result.ReportPath.empty())
        ConstructMutedWrapped(std::filesystem::absolute(std::filesystem::path(Result.ReportPath)).parent_path().string().c_str());
}

void PerformancePanel::ConstructSettingsSection(float Unit)
{
    // ---- the scene the test runs in
    bool Proving = Scene.IsProvingGroundsMode();
    ImGui::TextUnformatted("Scene");
    ImGui::SameLine(Unit * 6.0f);
    if (ImGui::RadioButton("Lobby", !Proving))
        Scene.ConfigureProvingGroundsMode(false);
    ImGui::SameLine();
    if (ImGui::RadioButton("Proving Grounds", Proving))
        Scene.ConfigureProvingGroundsMode(true);

    ImGui::Spacing();
    ImGui::SetNextItemWidth(Unit * 14.0f);
    ImGui::SliderInt("Rounds", &Settings.Rounds, 1, 4);
    ImGui::SetNextItemWidth(Unit * 14.0f);
    ImGui::SliderInt("Measured refreshes", &Settings.MeasuredRefreshes, 120, 1800);
    ImGui::SetNextItemWidth(Unit * 14.0f);
    ImGui::SliderInt("Warm-up refreshes", &Settings.WarmupRefreshes, 30, 300);
    ImGui::Checkbox("Present without waiting for the display during the test", &Settings.UncappedEnabled);
    ImGui::Checkbox("GPU timestamps during the test (where the device has them)", &Settings.GpuTimestampsEnabled);
    ImGui::Checkbox("Compare the pictures of the two paths", &Settings.PicturesEnabled);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ---- the presentation and the device's timestamps, for the live figures
    bool Uncapped = Context.QueryUncappedPresentEnabled();
    if (ImGui::Checkbox("Present without waiting for the display (no vsync)", &Uncapped))
        Context.ConfigureUncappedPresent(Uncapped);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, InkMuted);
    ImGui::Text("now: %s", Context.QueryPresentModeName());
    ImGui::PopStyleColor();
    if (Context.QueryGpuTimestampsAvailable())
    {
        bool Timestamps = Context.QueryGpuTimestampsEnabled();
        if (ImGui::Checkbox("GPU timestamps", &Timestamps))
            Context.ConfigureGpuTimestamps(Timestamps);
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, InkMuted);
        ImGui::TextUnformatted("GPU timestamps: not supported by this device");
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    ConstructMutedWrapped(Context.QueryDeviceSummary().c_str());
}

void PerformancePanel::ConstructInterface()
{
    ImGuiIO& Io = ImGui::GetIO();
    // 📝 F2 opens and closes the window. (Renaming a record in the Outliner is Shift+F2.)
    if (!Io.WantTextInput && !Io.KeyShift && !Io.KeyCtrl && !Io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_F2, false))
        WindowOpen = !WindowOpen;
    if (!WindowOpen)
        return;

    const float       Unit     = ImGui::GetFontSize();
    ImGuiViewport*    Viewport = ImGui::GetMainViewport();
    const float       Width    = Unit * 40.0f;
    const ImGuiCond   Placement = GeometryResetPending ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
    GeometryResetPending        = false;
    ImGui::SetNextWindowPos(ImVec2(Viewport->WorkPos.x + Viewport->WorkSize.x - Width - Unit, Viewport->WorkPos.y + Unit * 3.0f), Placement);
    ImGui::SetNextWindowSize(ImVec2(Width, std::min(Viewport->WorkSize.y - Unit * 5.0f, Unit * 58.0f)), Placement);

    // [unit] - the window's own colours: the editor's theme leaves the boxes of a checkbox or a slider as dark as the window
    int Colours = 0;
    auto Colour = [&](ImGuiCol Slot, ImU32 Value) {
        ImGui::PushStyleColor(Slot, Value);
        ++Colours;
    };
    Colour(ImGuiCol_WindowBg, IM_COL32(23, 23, 25, 244));
    Colour(ImGuiCol_Border, IM_COL32(55, 55, 60, 255));
    Colour(ImGuiCol_FrameBg, IM_COL32(48, 48, 54, 255));
    Colour(ImGuiCol_FrameBgHovered, IM_COL32(62, 62, 70, 255));
    Colour(ImGuiCol_FrameBgActive, IM_COL32(72, 72, 82, 255));
    Colour(ImGuiCol_CheckMark, InkGood);
    Colour(ImGuiCol_SliderGrab, IM_COL32(96, 180, 126, 255));
    Colour(ImGuiCol_SliderGrabActive, InkGood);
    Colour(ImGuiCol_Button, IM_COL32(52, 56, 62, 255));
    Colour(ImGuiCol_ButtonHovered, IM_COL32(66, 72, 80, 255));
    Colour(ImGuiCol_ButtonActive, IM_COL32(80, 88, 98, 255));
    Colour(ImGuiCol_Tab, IM_COL32(34, 34, 38, 255));
    Colour(ImGuiCol_TabHovered, IM_COL32(66, 72, 80, 255));
    Colour(ImGuiCol_TabSelected, IM_COL32(52, 56, 62, 255));
    Colour(ImGuiCol_PlotLines, InkGood);
    Colour(ImGuiCol_PlotHistogram, IM_COL32(96, 180, 126, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Unit * 1.1f, Unit * 0.9f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    const bool Shown = ImGui::Begin("Performance  -  F2##FrontierPerformance", &WindowOpen,
                                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking);
    ImGui::PopStyleVar(3);   // the frame rounding stays for the content
    if (Shown)
    {
        ConstructSwitchSection(Unit);
        ImGui::Spacing();
        if (ImGui::BeginTabBar("##performance_tabs"))
        {
            if (ImGui::BeginTabItem("Live"))
            {
                ConstructLiveSection(Unit);
                ImGui::EndTabItem();
            }
            const ImGuiTabItemFlags TestFlags = SelectTestTabPending ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            SelectTestTabPending              = false;
            if (ImGui::BeginTabItem("A/B test", nullptr, TestFlags))
            {
                ConstructTestSection(Unit);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Settings"))
            {
                ConstructSettingsSection(Unit);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(Colours);
}

} // namespace Frontier
