/*==========================================================================================================================================
                                                  PERFORMANCEPARITYVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 The optimized execution path has to do the same work as the baseline, not different work that is faster; and the log that compares them has to
//    measure what it says. This holds both:
//
//      ① the log (no device): the switch is off unless asked for and is stamped in the file; the percentiles are the nearest rank; nested scopes keep
//        their parent and a stretch's own time is what is left of it after its children; counts are summed per refresh; a refresh can be left out; the
//        TOML that is written reads back with the same figures; and the A/B test's verdict is what its figures say (faster, slower, no difference, no
//        test, pictures that differ);
//      ② the atlas (no device): BuildDomainSdfAtlasRetained gives the very bits of BuildDomainSdfAtlas, in float and as the half floats that the upload
//        makes of them, over cars of every kind (none, one, several, high, low, far outside the window, not a number) and over windows, lattices and
//        course settings that change between calls, while keeping the course's distances between calls whose window has not changed;
//      ③ the device: a scripted drive (a burnout, then a drive that crosses the window's edge) is run on the baseline, on the optimized path, and on the
//        baseline again as the control; the picture the scene makes after every tick is hashed. The optimized pictures must be the baseline's, tick for
//        tick, and the control must repeat the baseline (else a difference could not be blamed on the optimization). The optimized path must also make
//        one submission a tick where the baseline makes two or three.
//
//        PerformanceParityValidation [--require-vulkan] [--only log|comparison|atlas|device] [--ticks N]
//
//    Run it from the repository root (the device half reads Experimental/VehicleHmiPrototype/Archive). On a device that is a software rasterizer it holds
//    the arithmetic and the order of the work; it says nothing about speed, and nothing here is a timing claim.

#include "Frontier/Engine/Diagnostics/PerformanceLog.h"
#include "Frontier/Engine/Diagnostics/PerformancePanel.h"
#include "Frontier/Engine/Input/InputPacket.h"
#include "Frontier/Engine/Interface/SceneViewportPresentation.h"
#include "Frontier/Engine/Rendering/HalfFloatConversion.h"
#include "Frontier/Engine/Rendering/VulkanContext.h"
#include "Frontier/Engine/Rendering/WorldSdfScene.h"
#include "Frontier/Engine/Scene/WorldSpace.h"
#include "Frontier/Engine/VolumetricDynamics/FluidPlumeBridge.h"
#include "Frontier/Engine/VolumetricDynamics/PyroVolumeSolver.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

namespace
{

using namespace Frontier;

int Assertions = 0;
int Failures   = 0;

void Verify(bool Condition, const std::string& Caption)
{
    ++Assertions;
    if (!Condition) ++Failures;
    std::printf("%s\t%s\n", Condition ? "PASS" : "FAIL", Caption.c_str());
}

std::string Spell(double Amount, int Decimals = 2)
{
    char Text[64];
    std::snprintf(Text, sizeof(Text), "%.*f", Decimals, Amount);
    return Text;
}

// [-] - a small deterministic generator, so that a failure can be repeated
struct Sequence
{
    uint64_t Seed = 88172645463325252ull;

    float Next() noexcept
    {
        Seed = Seed * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<float>((Seed >> 40) & 0xFFFFFFu) / 16777216.0f;
    }
    float Between(float Low, float High) noexcept { return Low + (High - Low) * Next(); }
};

uint64_t HashBytes(const void* Bytes, size_t Count) noexcept
{
    uint64_t       Hash = 1469598103934665603ull;
    const uint8_t* Byte = static_cast<const uint8_t*>(Bytes);
    for (size_t Index = 0u; Index < Count; ++Index)
    {
        Hash ^= Byte[Index];
        Hash *= 1099511628211ull;
    }
    return Hash;
}

//----------------------------------------------------------------------------------------------------------------------
//                                                         THE LOG
//----------------------------------------------------------------------------------------------------------------------

const toml::table* FindSeries(const toml::table& Root, const std::string& Path)
{
    const toml::array* Series = Root["series"].as_array();
    if (Series == nullptr)
        return nullptr;
    for (const toml::node& Entry : *Series)
    {
        const toml::table* Table = Entry.as_table();
        if (Table != nullptr && (*Table)["path"].value_or(std::string()) == Path)
            return Table;
    }
    return nullptr;
}

void ValidateLog()
{
    // ---- the switch
    const char* Environment = std::getenv("FRONTIER_OPTIMIZED_EXECUTION");
    if (Environment == nullptr)
        Verify(!OptimizedExecution::QueryEnabled() && std::string(OptimizedExecution::QueryModeName()) == "baseline",
               "the optimized path is off unless a run asks for it");
    else
        std::printf("INFO\tFRONTIER_OPTIMIZED_EXECUTION is set (%s): the default is not asserted\n", Environment);
    OptimizedExecution::Configure(true);
    Verify(OptimizedExecution::QueryEnabled() && std::string(OptimizedExecution::QueryModeName()) == "optimized", "a run can switch it on");
    OptimizedExecution::Configure(false);
    Verify(!OptimizedExecution::QueryEnabled() && std::string(OptimizedExecution::QueryModeName()) == "baseline", "and off again");

    // ---- nearest-rank percentiles
    std::vector<double> OneToHundred;
    for (int Sample = 1; Sample <= 100; ++Sample) OneToHundred.push_back(static_cast<double>(Sample));
    Verify(PerformanceLog::ResolveNearestRank(OneToHundred, 0.50) == 50.0 && PerformanceLog::ResolveNearestRank(OneToHundred, 0.90) == 90.0 &&
               PerformanceLog::ResolveNearestRank(OneToHundred, 0.95) == 95.0 && PerformanceLog::ResolveNearestRank(OneToHundred, 0.99) == 99.0,
           "the percentiles of 1..100 are 50, 90, 95 and 99 (nearest rank: the smallest sample with that share at or below it)");
    std::vector<double> OneToTen;
    for (int Sample = 10; Sample >= 1; --Sample) OneToTen.push_back(static_cast<double>(Sample));   // [-] - unsorted on purpose
    Verify(PerformanceLog::ResolveNearestRank(OneToTen, 0.50) == 5.0 && PerformanceLog::ResolveNearestRank(OneToTen, 0.90) == 9.0 &&
               PerformanceLog::ResolveNearestRank(OneToTen, 0.99) == 10.0 && PerformanceLog::ResolveNearestRank(OneToTen, 1.0) == 10.0 &&
               PerformanceLog::ResolveNearestRank(OneToTen, 0.0) == 1.0,
           "the percentiles of ten unsorted samples are 5, 9, 10 at 50, 90 and 99, the maximum at 100 and the minimum at 0");
    Verify(PerformanceLog::ResolveNearestRank({ 7.0 }, 0.99) == 7.0 && PerformanceLog::ResolveNearestRank({}, 0.5) == 0.0,
           "a single sample is every percentile of itself, and no samples give zero");

    // ---- a run: nested scopes, counts, GPU samples, an excluded refresh, written and read back
    const std::filesystem::path Directory = std::filesystem::temp_directory_path() / "FrontierPerformanceParity";
    std::error_code             Ignored;
    std::filesystem::remove_all(Directory, Ignored);

    PerformanceRunDescription Description{};
    Description.Label            = "parity check";
    Description.Notes            = "written by PerformanceParityValidation";
    Description.OutputDirectory  = Directory.string();
    Description.WarmupRefreshes  = 2;
    Description.PlannedRefreshes = 16;
    PerformanceLog& Log          = PerformanceLog::Instance();
    OptimizedExecution::Configure(true);
    Log.Configure(Description);
    Log.RegisterEnvironment("device", "none (parity check)");
    Log.RegisterEnvironmentNumber("whole_number", 8.0);
    Log.RegisterEnvironmentNumber("fraction", 0.25);

    // [-] - refresh r adds r+1 to the count "n", has an outer scope "outer" holding an "inner" scope, and a GPU sample of 2 ms. Refresh 6 is left out.
    for (int Refresh = 0; Refresh < 14; ++Refresh)
    {
        Log.BeginRefresh();
        {
            PerformanceScope Outer("outer");
            {
                PerformanceScope Inner("inner");
                Log.RegisterCount("n", static_cast<double>(Refresh + 1));
                Log.RegisterGpuSample("g", 2.0);
            }
        }
        if (Refresh == 6)
            Log.ExcludeOpenRefresh();
    }
    Verify(Log.QueryRefreshCount() == 14, "the log counts the refreshes it was given (" + std::to_string(Log.QueryRefreshCount()) + ")");

    // [-] - 14 refreshes opened; the last one closed is cut short by the end of the run; 2 are warm-up; refresh 6 is excluded
    const auto Statistics = Log.ComputeStatistics();
    auto       Lookup     = [&](const std::string& Path) -> const PerformanceLog::SeriesStatistics* {
        for (const auto& Entry : Statistics)
            if (Entry.Path == Path) return &Entry;
        return nullptr;
    };
    // The last refresh is still open (Finalize closes it), so 13 are closed and the last of those (12) is the one cut short: measured 2..11 but 6.
    const auto* Count = Lookup("n");
    Verify(Count != nullptr && Count->Kind == "count", "a count is a series of its own");
    if (Count != nullptr)
    {
        // closed refreshes: 0..12 (13); last closed (12) is left out; warm-up 0,1; excluded 6: measured = 2,3,4,5,7,8,9,10,11 -> n = 3,4,5,6,8,9,10,11,12
        Verify(Count->Refreshes == 9, "the warm-up, the refresh that was cut short and the refresh that was left out are not measured (" +
                                          std::to_string(Count->Refreshes) + " of 14 are)");
        Verify(Count->Total == 68.0 && std::fabs(Count->Mean - 68.0 / 9.0) < 1.0e-9, "a count is summed per refresh: total " + Spell(Count->Total, 0));
        Verify(Count->Median == 8.0 && Count->Percentile90 == 12.0 && Count->Minimum == 3.0 && Count->Maximum == 12.0,
               "the statistics of the counts are those of 3,4,5,6,8,9,10,11,12 (p50 " + Spell(Count->Median, 0) + ", p90 " + Spell(Count->Percentile90, 0) + ")");
    }
    const auto* Outer = Lookup("outer");
    const auto* Inner = Lookup("outer/inner");
    const auto* Self  = Lookup("outer.self");
    Verify(Outer != nullptr && Inner != nullptr, "a scope inside another keeps its parent in its path (outer, outer/inner)");
    Verify(Self != nullptr && Outer != nullptr && Inner != nullptr && Self->Mean <= Outer->Mean + 1.0e-9 && Outer->Mean + 1.0e-9 >= Inner->Mean,
           "a scope is at least as long as the one inside it, and its own time is what is left after its children");
    const auto* Gpu = Lookup("g");
    Verify(Gpu != nullptr && Gpu->Kind == "gpu" && Gpu->Minimum == 2.0 && Gpu->Maximum == 2.0, "a GPU sample is a series of its own, in milliseconds");

    std::string Path;
    std::string Summary;
    const bool  Written = Log.Finalize(Path, Summary);
    OptimizedExecution::Configure(false);
    Verify(Written && !Path.empty() && std::filesystem::exists(Path), "the log is written (" + Path + ")");
    Verify(Path.find("optimized") != std::string::npos, "the mode is in the file's name");
    Verify(!Log.QueryRecordingEnabled(), "recording stops when the run is finalized");
    {
        PerformanceScope After("after the run");   // [-] - must be harmless, and must not start a series
        Log.RegisterCount("after", 1.0);
    }

    toml::table Document;
    bool        Parsed = false;
    try
    {
        Document = toml::parse_file(Path);
        Parsed   = true;
    }
    catch (const toml::parse_error& Failure)
    {
        std::printf("INFO\tthe log does not read back: %s\n", std::string(Failure.description()).c_str());
    }
    Verify(Parsed, "the log is valid TOML");
    if (Parsed)
    {
        Verify(Document["run"]["mode"].value_or(std::string()) == "optimized" && Document["run"]["optimized_execution"].value_or(false) &&
                   Document["environment"]["mode_at_start"].value_or(std::string()) == "optimized" &&
                   Document["environment"]["mode_at_end"].value_or(std::string()) == "optimized",
               "the mode is stamped in the run table and in the environment, at the start and at the end");
        Verify(Document["run"]["refreshes_measured"].value_or(int64_t(-1)) == 10 && Document["run"]["refreshes_warmup"].value_or(int64_t(-1)) == 2 &&
                   Document["run"]["refreshes_excluded"].value_or(int64_t(-1)) == 1 && Document["run"]["refreshes_total"].value_or(int64_t(-1)) == 14,
               "the run table says how many refreshes were measured, warm-up and left out");
        Verify(Document["environment"]["device"].value_or(std::string()) == "none (parity check)" && Document["environment"]["whole_number"].is_integer() &&
                   Document["environment"]["whole_number"].value_or(int64_t(0)) == 8 && Document["environment"]["fraction"].is_floating_point() &&
                   Document["environment"]["logical_cores"].is_integer() && !Document["environment"]["cpu"].value_or(std::string()).empty() &&
                   !Document["environment"]["build_revision"].value_or(std::string()).empty(),
               "the environment names the device, the CPU, the revision, and writes whole numbers as integers");
        const toml::table* ReadBack = FindSeries(Document, "n");
        Verify(ReadBack != nullptr && (*ReadBack)["p50"].value_or(-1.0) == 8.0 && (*ReadBack)["total"].value_or(-1.0) == 81.0 &&
                   (*ReadBack)["refreshes"].value_or(int64_t(-1)) == 10,
               "the series read back with the same figures that were computed");
        Verify(FindSeries(Document, "outer/inner") != nullptr && FindSeries(Document, "outer.self") != nullptr && FindSeries(Document, "refresh.period") != nullptr &&
                   FindSeries(Document, "after") == nullptr && FindSeries(Document, "after the run") == nullptr,
               "the nesting, the own time and the refresh period are in the file, and nothing recorded after the end is");
        const toml::array* Windows = Document["window"].as_array();
        Verify(Windows != nullptr && !Windows->empty() && Document["summary"]["refreshes_per_second_mean"].is_floating_point(),
               "the file has a row per second and the figures that are asked for first");
    }
    std::filesystem::remove_all(Directory, Ignored);

    // ---- a run that has nothing to say does not fail
    PerformanceRunDescription Empty{};
    Empty.OutputDirectory = Directory.string();
    Log.Configure(Empty);
    std::string EmptyPath, EmptySummary;
    const bool  EmptyWritten = Log.Finalize(EmptyPath, EmptySummary);
    Verify(EmptyWritten, "a run of no refreshes is still written");
    std::filesystem::remove_all(Directory, Ignored);
}

//----------------------------------------------------------------------------------------------------------------------
//                                                     THE COMPARISON
//----------------------------------------------------------------------------------------------------------------------

// [-] - a run of the A/B test as the panel would have measured it: a mean refresh period, a drawn scene, a stepped simulation, submissions, and five pictures
PerformancePhaseResult ConstructRun(bool Optimized, double PeriodMean, double Submissions, uint64_t PictureSeed = 7u)
{
    PerformancePhaseResult Run{};
    Run.Optimized = Optimized;
    Run.Label     = Optimized ? "optimized" : "baseline";
    Run.Period.Path = "refresh.period";
    Run.Period.Kind = "refresh";
    Run.Period.Refreshes   = 100u;
    Run.Period.Active      = 100u;
    Run.Period.Mean        = PeriodMean;
    Run.Period.Median      = PeriodMean;
    Run.Period.Percentile95 = PeriodMean * 1.2;
    Run.Period.Percentile99 = PeriodMean * 1.4;
    Run.Period.Maximum      = PeriodMean * 2.0;
    auto Add = [&](const char* Path, const char* Kind, double Mean) {
        PerformanceLog::SeriesStatistics Entry{};
        Entry.Path = Path;
        Entry.Kind = Kind;
        Entry.Refreshes = 100u;
        Entry.Active    = 100u;
        Entry.Mean      = Mean;
        Run.Series.push_back(Entry);
    };
    // [-] - ui.build holds the scene (3 ms, of which 2 ms wait for the device) and the step (2 ms, no stretch inside): the CPU works 1 + 1 + 2 ms and waits 2 ms
    Add("ui.build", "cpu", PeriodMean * 0.8);
    Add("ui.build.self", "cpu_self", 1.0);
    Add("ui.build/scene.present", "cpu", 3.0);
    Add("ui.build/scene.present.self", "cpu_self", 1.0);
    Add("ui.build/scene.present/vk.submit_wait", "cpu", 2.0);
    Add("ui.build/sim.step", "cpu", 2.0);
    Add("vk.submits", "count", Submissions);
    Add("sim.ticks", "count", 1.0);
    for (uint64_t Checkpoint = 0u; Checkpoint < 5u; ++Checkpoint)
        Run.PictureHashes.push_back(PictureSeed * 1000003u + Checkpoint + 1u);
    Run.PictureWidth  = 640;
    Run.PictureHeight = 360;
    return Run;
}

void ValidateComparison()
{
    // ---- one path is clearly faster, the runs of each path agreeing to about 1 %
    {
        const std::vector<PerformancePhaseResult> Runs = { ConstructRun(false, 12.00, 3.0), ConstructRun(true, 9.60, 1.0), ConstructRun(true, 9.70, 1.0), ConstructRun(false, 12.10, 3.0) };
        const PerformanceComparison Comparison = ComparePerformancePhases(Runs, "IMMEDIATE");
        Verify(Comparison.Valid && Comparison.PicturesIdentical && Comparison.PicturesChecked == 15, "a test of four runs whose pictures agree is valid (" + std::to_string(Comparison.PicturesChecked) + " comparisons)");
        Verify(std::fabs(Comparison.PeriodChange - ((9.65 - 12.05) / 12.05)) < 1.0e-9, "the change is the optimized mean against the baseline mean (" + Spell(Comparison.PeriodChange * 100.0, 2) + " %)");
        Verify(Comparison.SpreadKnown && std::fabs(Comparison.Spread - std::max(0.10 / 12.05, 0.10 / 9.65)) < 1.0e-9,
               "the spread is how far the runs of one path differ, as a share of its mean, the larger of the two paths' (" + Spell(Comparison.Spread * 100.0, 2) + " %)");
        Verify(Comparison.Verdict.find("faster") != std::string::npos && Comparison.Verdict.find("slower") == std::string::npos, "the verdict says faster: " + Comparison.Verdict);
        bool SubmissionsRow = false;
        for (const PerformanceComparisonRow& Row : Comparison.Rows)
            if (Row.Caption == "Submissions per refresh" && Row.Baseline == 3.0 && Row.Optimized == 1.0) SubmissionsRow = true;
        Verify(SubmissionsRow, "the table has the submissions of each path");
        double Work = -1.0, Waiting = -1.0;
        for (const PerformanceComparisonRow& Row : Comparison.Rows)
        {
            if (Row.Caption == "CPU working (not waiting)") Work = Row.Baseline;
            if (Row.Caption == "CPU waiting for the device or display") Waiting = Row.Baseline;
        }
        Verify(std::fabs(Work - 4.0) < 1.0e-9 && std::fabs(Waiting - 2.0) < 1.0e-9,
               "the CPU's own work is the own time of stretches with others inside plus stretches with none, less the waits (" + Spell(Work, 3) + " ms), the waiting is the waits (" + Spell(Waiting, 3) + " ms)");
        Verify(std::any_of(Comparison.Notes.begin(), Comparison.Notes.end(), [](const std::string& Note) { return Note.find("CPU rows trade") != std::string::npos; }),
               "merged submissions bring the note that the CPU rows trade the wait between them");
        Verify(std::none_of(Comparison.Notes.begin(), Comparison.Notes.end(), [](const std::string& Note) { return Note.find("display") != std::string::npos; }), "no note about vsync when presenting immediately");
    }
    // ---- a change inside the spread is no difference
    {
        const std::vector<PerformancePhaseResult> Runs = { ConstructRun(false, 12.00, 3.0), ConstructRun(true, 11.90, 1.0), ConstructRun(true, 12.60, 1.0), ConstructRun(false, 12.70, 3.0) };
        const PerformanceComparison Comparison = ComparePerformancePhases(Runs, "IMMEDIATE");
        Verify(Comparison.Verdict.find("No clear difference") != std::string::npos, "a change inside the spread between runs is no difference: " + Comparison.Verdict);
    }
    // ---- slower
    {
        const std::vector<PerformancePhaseResult> Runs = { ConstructRun(false, 10.00, 3.0), ConstructRun(true, 11.50, 1.0), ConstructRun(true, 11.55, 1.0), ConstructRun(false, 10.05, 3.0) };
        const PerformanceComparison Comparison = ComparePerformancePhases(Runs, "MAILBOX");
        Verify(Comparison.Verdict.find("slower") != std::string::npos, "a clear loss is called slower: " + Comparison.Verdict);
    }
    // ---- one run of each path: no spread to read it against
    {
        const std::vector<PerformancePhaseResult> Runs = { ConstructRun(false, 12.00, 3.0), ConstructRun(true, 11.80, 1.0) };
        const PerformanceComparison Comparison = ComparePerformancePhases(Runs, "FIFO");
        Verify(!Comparison.SpreadKnown && Comparison.Verdict.find("No clear difference") != std::string::npos, "one run of each path cannot call a 1.7 % change: " + Comparison.Verdict);
        Verify(std::any_of(Comparison.Notes.begin(), Comparison.Notes.end(), [](const std::string& Note) { return Note.find("One run of each path") != std::string::npos; }) &&
                   std::any_of(Comparison.Notes.begin(), Comparison.Notes.end(), [](const std::string& Note) { return Note.find("display") != std::string::npos; }),
               "and says so, and that presenting in FIFO caps the refresh period");
    }
    // ---- different pictures: the timing is not to be trusted
    {
        std::vector<PerformancePhaseResult> Runs = { ConstructRun(false, 12.00, 3.0), ConstructRun(true, 9.60, 1.0), ConstructRun(true, 9.70, 1.0), ConstructRun(false, 12.10, 3.0) };
        Runs[1].PictureHashes[2] ^= 0x10u;
        const PerformanceComparison Comparison = ComparePerformancePhases(Runs, "IMMEDIATE");
        Verify(Comparison.PicturesCompared && !Comparison.PicturesIdentical && Comparison.Verdict.find("did not draw the same pictures") != std::string::npos && Comparison.BaselineRepeats,
               "one differing picture in the optimized run is reported, with the baseline repeating itself: " + Comparison.Verdict);
        Runs[3].PictureHashes[0] ^= 0x10u;
        const PerformanceComparison NotRepeating = ComparePerformancePhases(Runs, "IMMEDIATE");
        Verify(!NotRepeating.BaselineRepeats && NotRepeating.Verdict.find("did not even repeat itself") != std::string::npos, "a baseline that differs from itself is said to be not deterministic");
    }
    // ---- a viewport that changed size is not compared
    {
        std::vector<PerformancePhaseResult> Runs = { ConstructRun(false, 12.00, 3.0), ConstructRun(true, 9.60, 1.0) };
        Runs[1].PictureWidth = 800;
        Runs[1].PictureHashes[0] ^= 0x10u;
        const PerformanceComparison Comparison = ComparePerformancePhases(Runs, "IMMEDIATE");
        Verify(!Comparison.PicturesCompared && !Comparison.PicturesIdentical, "pictures of different sizes are not compared");
    }
    // ---- not a test: the scene was not drawn, or the simulation did not step
    {
        std::vector<PerformancePhaseResult> Runs = { ConstructRun(false, 12.00, 3.0), ConstructRun(true, 9.60, 1.0) };
        for (auto& Entry : Runs[1].Series)
            if (Entry.Path == "ui.build/scene.present") Entry.Active = 40u;
        const PerformanceComparison Comparison = ComparePerformancePhases(Runs, "IMMEDIATE");
        Verify(!Comparison.Valid && Comparison.Verdict.find("Not a valid test") != std::string::npos && Comparison.Verdict.find("scene was not drawn") != std::string::npos, "a run that did not draw the scene makes it no test: " + Comparison.Verdict);
        Runs = { ConstructRun(false, 12.00, 3.0), ConstructRun(true, 9.60, 1.0) };
        for (auto& Entry : Runs[0].Series)
            if (Entry.Path == "sim.ticks") Entry.Active = 10u;
        const PerformanceComparison Idle = ComparePerformancePhases(Runs, "IMMEDIATE");
        Verify(!Idle.Valid && Idle.Verdict.find("did not step") != std::string::npos, "nor one in which the simulation did not step: " + Idle.Verdict);
        const PerformanceComparison OnePath = ComparePerformancePhases({ ConstructRun(false, 12.0, 3.0) }, "IMMEDIATE");
        Verify(!OnePath.Valid && OnePath.Verdict.find("No result") != std::string::npos, "nor a test of one path only");
    }
    // ---- the report holds what it says
    {
        PerformanceTestResult Result{};
        Result.Finished    = true;
        Result.Device      = "Test device";
        Result.PresentMode = "IMMEDIATE";
        Result.Scene       = "Lobby";
        Result.Settings    = PerformanceTestSettings{};
        Result.Phases      = { ConstructRun(false, 12.00, 3.0), ConstructRun(true, 9.60, 1.0), ConstructRun(true, 9.70, 1.0), ConstructRun(false, 12.10, 3.0) };
        const PerformanceComparison Comparison = ComparePerformancePhases(Result.Phases, Result.PresentMode);
        const std::string           Report     = ConstructPerformanceReport(Result, Comparison);
        Verify(Report.find("Test device") != std::string::npos && Report.find("Verdict") != std::string::npos && Report.find("ui.build/scene.present") != std::string::npos &&
                   Report.find("baseline, optimized, optimized, baseline") != std::string::npos && Report.find("Refresh period, mean") != std::string::npos,
               "the report names the device, the order of the runs, the verdict, the table and every series");
    }
}

//----------------------------------------------------------------------------------------------------------------------
//                                                        THE ATLAS
//----------------------------------------------------------------------------------------------------------------------

struct LatticeLayout
{
    int32_t Resolution;
    int32_t TilesX;
    int32_t TilesY;
};

constexpr LatticeLayout Layouts[] = { { 16, 4, 4 }, { 32, 8, 4 }, { 48, 8, 6 }, { 64, 8, 8 } };

WorldVehicleRig ConstructCar(Sequence& Random, float Left, float Back, float Right, float Front)
{
    WorldVehicleRig Car{};
    Car.Visual.PositionMetres = { Random.Between(Left, Right), Random.Between(Back, Front), Random.Between(-0.5f, 1.0f) };
    Car.Visual.YawDegrees     = Random.Between(-540.0f, 540.0f);
    Car.VelocityMps           = { Random.Between(-15.0f, 15.0f), Random.Between(-15.0f, 15.0f), Random.Between(-2.0f, 2.0f) };
    Car.YawRateRad            = Random.Between(-3.0f, 3.0f);
    return Car;
}

struct AtlasScore
{
    int         Builds           = 0;
    int         FloatMismatches  = 0;
    int         HalfMismatches   = 0;
    int         SizeMismatches   = 0;
    int         Configurations   = 0;
    std::string FirstFailure;
};

// [-] - one build by both routes, compared in float and in half floats
void CompareAtlases(WorldSdfScene& Scene, const std::array<float, 3>& Origin, const PyroBoundsBox& Box, const LatticeLayout& Layout, bool Proving,
                    bool Obstacles, const std::vector<WorldVehicleRig>& Cars, AtlasScore& Score, const std::string& Caption)
{
    std::vector<float> Expected;
    Scene.BuildDomainSdfAtlas(Origin, Box, Layout.Resolution, Layout.TilesX, Layout.TilesY, Proving, Obstacles, Cars, Expected);

    std::vector<float>    Floats;
    std::vector<uint16_t> Halves;
    Scene.BuildDomainSdfAtlasRetained(Origin, Box, Layout.Resolution, Layout.TilesX, Layout.TilesY, Proving, Obstacles, Cars, &Floats, &Halves);
    ++Score.Builds;

    if (Floats.size() != Expected.size() || Halves.size() != Expected.size())
    {
        ++Score.SizeMismatches;
        if (Score.FirstFailure.empty()) Score.FirstFailure = Caption + ": sizes differ";
        return;
    }
    if (std::memcmp(Floats.data(), Expected.data(), Expected.size() * sizeof(float)) != 0)
    {
        ++Score.FloatMismatches;
        if (Score.FirstFailure.empty())
        {
            size_t At = 0u;
            while (At < Expected.size() && std::memcmp(&Floats[At], &Expected[At], sizeof(float)) == 0) ++At;
            Score.FirstFailure = Caption + ": float texel " + std::to_string(At) + " is " + Spell(Floats[At], 6) + ", baseline " + Spell(Expected[At], 6);
        }
    }
    bool HalvesMatch = true;
    for (size_t At = 0u; At < Expected.size() && HalvesMatch; ++At)
        HalvesMatch = (Halves[At] == ConvertFloat32ToHalf16(Expected[At]));
    if (!HalvesMatch)
    {
        ++Score.HalfMismatches;
        if (Score.FirstFailure.empty()) Score.FirstFailure = Caption + ": half-float texels differ";
    }

    // [-] - the half-float output on its own (as the optimized path asks for it) must equal what asking for both gave
    std::vector<uint16_t> HalvesAlone;
    Scene.BuildDomainSdfAtlasRetained(Origin, Box, Layout.Resolution, Layout.TilesX, Layout.TilesY, Proving, Obstacles, Cars, nullptr, &HalvesAlone);
    if (HalvesAlone != Halves)
    {
        ++Score.HalfMismatches;
        if (Score.FirstFailure.empty()) Score.FirstFailure = Caption + ": the half-float output alone differs";
    }
}

void ValidateAtlas()
{
    Sequence    Random;
    AtlasScore  Score;
    WorldSdfScene Scene;   // [-] - one scene for everything below: what it keeps from one call to the next must never make a call wrong

    // ---- ticks of a moving car on a window that stays: the course's distances are made once and kept
    {
        const LatticeLayout& Layout = Layouts[2];
        PyroBoundsBox        Box{};
        Box.BoxMin = { -19.2f, -0.60f, -19.2f };
        Box.BoxMax = { 19.2f, 10.92f, 19.2f };
        const uint64_t Before = Scene.QueryRetainedCourseRebuildCount();
        WorldVehicleRig Car{};
        Car.Visual.PositionMetres = { -12.0f, -3.0f, 0.53f };
        Car.VelocityMps           = { 9.0f, 1.0f, 0.0f };
        Car.YawRateRad            = 0.4f;
        for (int Tick = 0; Tick < 60; ++Tick)
        {
            Car.Visual.PositionMetres[0] += Car.VelocityMps[0] / 60.0f * 5.0f;
            Car.Visual.PositionMetres[1] += Car.VelocityMps[1] / 60.0f * 5.0f;
            Car.Visual.YawDegrees += 3.0f;
            CompareAtlases(Scene, { 0.0f, 0.0f, 0.0f }, Box, Layout, true, true, { Car }, Score, "tick " + std::to_string(Tick));
        }
        const uint64_t Made = Scene.QueryRetainedCourseRebuildCount() - Before;
        Verify(Made == 1u, "60 ticks of a moving car on a window that stays make the course's distances once (made " + std::to_string(Made) + " times)");
        ++Score.Configurations;
    }

    // ---- windows, lattices and course settings that change between calls, with cars of every kind
    const float Nan  = std::numeric_limits<float>::quiet_NaN();
    const float Inf  = std::numeric_limits<float>::infinity();
    for (int Round = 0; Round < 90; ++Round)
    {
        const LatticeLayout& Layout = Layouts[Round % 3];   // [-] - 64 is covered below, as it is the slow one
        const float          Width  = Random.Between(8.0f, 60.0f);
        const float          Height = Random.Between(4.0f, 14.0f);
        PyroBoundsBox        Box{};
        const float          MinX = Random.Between(-60.0f, 40.0f);
        const float          MinZ = Random.Between(-60.0f, 40.0f);
        Box.BoxMin = { MinX, -0.60f, MinZ };
        Box.BoxMax = { MinX + Width, -0.60f + Height, MinZ + Width };
        const std::array<float, 3> Origin = { (Round % 7 == 0) ? Random.Between(-3.0f, 3.0f) : 0.0f, (Round % 7 == 0) ? Random.Between(-3.0f, 3.0f) : 0.0f, 0.0f };
        const bool Proving   = (Round % 4) != 0;
        const bool Obstacles = (Round % 3) != 1;

        std::vector<WorldVehicleRig> Cars;
        const int Kinds = Round % 9;
        if (Kinds >= 1)   // one car near or in the window
            Cars.push_back(ConstructCar(Random, MinX - 3.0f, MinZ - 3.0f, MinX + Width + 3.0f, MinZ + Width + 3.0f));
        if (Kinds >= 2)   // a second, anywhere on the course
            Cars.push_back(ConstructCar(Random, -50.0f, -30.0f, 50.0f, 30.0f));
        if (Kinds == 3)   // a third on the ramp and one on the portal
        {
            WorldVehicleRig Ramp = ConstructCar(Random, 22.0f, -2.0f, 28.0f, 2.0f);
            Ramp.Visual.PositionMetres[2] = Random.Between(0.2f, 1.6f);
            Cars.push_back(Ramp);
        }
        if (Kinds == 4)   // high above the course and below it
        {
            WorldVehicleRig High = ConstructCar(Random, MinX, MinZ, MinX + Width, MinZ + Width);
            High.Visual.PositionMetres[2] = Random.Between(3.0f, 14.0f);
            WorldVehicleRig Low = ConstructCar(Random, MinX, MinZ, MinX + Width, MinZ + Width);
            Low.Visual.PositionMetres[2] = Random.Between(-6.0f, -0.5f);
            Cars.push_back(High);
            Cars.push_back(Low);
        }
        if (Kinds == 5)   // far outside the window, and a car that is not a number
        {
            WorldVehicleRig Far = ConstructCar(Random, 300.0f, 300.0f, 400.0f, 400.0f);
            WorldVehicleRig Lost = ConstructCar(Random, MinX, MinZ, MinX + Width, MinZ + Width);
            Lost.Visual.PositionMetres[Round % 3] = (Round % 2 == 0) ? Nan : Inf;
            Cars.push_back(Far);
            Cars.push_back(Lost);
        }
        if (Kinds == 6)   // a car whose yaw is not finite, and one that is huge
        {
            WorldVehicleRig Spinning = ConstructCar(Random, MinX, MinZ, MinX + Width, MinZ + Width);
            Spinning.Visual.YawDegrees = Nan;
            WorldVehicleRig Huge = ConstructCar(Random, MinX, MinZ, MinX + Width, MinZ + Width);
            Huge.Visual.YawDegrees = 1.0e7f;
            Cars.push_back(Spinning);
            Cars.push_back(Huge);
        }
        if (Kinds == 7)   // a stack of cars in one place
        {
            for (int Index = 0; Index < 5; ++Index)
                Cars.push_back(ConstructCar(Random, MinX + Width * 0.4f, MinZ + Width * 0.4f, MinX + Width * 0.6f, MinZ + Width * 0.6f));
        }
        ++Score.Configurations;

        // [-] - two or three ticks per configuration, the car moved a little between them, so that kept distances are used more than once
        for (int Tick = 0; Tick < 3; ++Tick)
        {
            CompareAtlases(Scene, Origin, Box, Layout, Proving, Obstacles, Cars, Score,
                           "round " + std::to_string(Round) + " tick " + std::to_string(Tick) + " (" + std::to_string(Cars.size()) + " cars)");
            for (WorldVehicleRig& Car : Cars)
            {
                if (std::isfinite(Car.Visual.PositionMetres[0])) Car.Visual.PositionMetres[0] += 0.35f;
                Car.Visual.YawDegrees += 2.0f;
            }
        }
    }

    // ---- the lattice that the device uses (64), a few times
    for (int Round = 0; Round < 6; ++Round)
    {
        PyroBoundsBox Box{};
        const float   MinX = -19.2f + static_cast<float>(Round) * 4.8f;
        Box.BoxMin = { MinX, -0.60f, -19.2f };
        Box.BoxMax = { MinX + 38.4f, 10.92f, 19.2f };
        std::vector<WorldVehicleRig> Cars;
        Cars.push_back(ConstructCar(Random, MinX, -19.0f, MinX + 38.4f, 19.0f));
        if (Round % 2 == 1) Cars.push_back(ConstructCar(Random, MinX, -19.0f, MinX + 38.4f, 19.0f));
        CompareAtlases(Scene, { 0.0f, 0.0f, 0.0f }, Box, Layouts[3], true, true, Cars, Score, "lattice 64 round " + std::to_string(Round));
        ++Score.Configurations;
    }

    // ---- degenerate calls do not crash and give nothing
    {
        std::vector<float>    Floats{ 1.0f };
        std::vector<uint16_t> Halves{ 1u };
        PyroBoundsBox         Box{};
        Scene.BuildDomainSdfAtlasRetained({ 0.0f, 0.0f, 0.0f }, Box, 0, 8, 8, true, true, {}, &Floats, &Halves);
        Verify(Floats.empty() && Halves.empty(), "a lattice of no voxels gives empty outputs");
    }

    Verify(Score.SizeMismatches == 0 && Score.FloatMismatches == 0, std::to_string(Score.Builds) + " atlases over " + std::to_string(Score.Configurations) +
                                                                       " configurations: the optimized float atlas is the baseline's, bit for bit (" +
                                                                       std::to_string(Score.FloatMismatches) + " differ)" +
                                                                       (Score.FirstFailure.empty() ? std::string() : " - " + Score.FirstFailure));
    Verify(Score.HalfMismatches == 0, "and its half-float atlas is what the upload makes of the baseline's (" + std::to_string(Score.HalfMismatches) + " differ)");
}

//----------------------------------------------------------------------------------------------------------------------
//                                                        THE DEVICE
//----------------------------------------------------------------------------------------------------------------------

struct DeviceRun
{
    bool                  Started = false;
    std::vector<uint64_t> SceneHashes;   // [-] - the scene's picture after each tick
    std::vector<uint64_t> Submissions;   // [-] - submissions made by one tick's step and scene
    std::vector<uint64_t> Relocations;   // [-] - the world solver's relocation count after each tick
    int                   Width  = 0;
    int                   Height = 0;
    bool                  RecordingLeftOpen = false;
};

// [-] - the same drive for every run: a burnout where the car stands, then (from tick 10) the car is put 14 m ahead (the window has to move) and drives on
DeviceRun RunScript(VulkanContext& Context, const std::filesystem::path& Archive, bool Optimized, int Ticks)
{
    DeviceRun Run;
    OptimizedExecution::Configure(Optimized);

    EverestPlumeSceneRuntime Runtime;
    std::string              Diagnostic;
    if (!Runtime.Initialize(Context, Archive, Diagnostic))
    {
        std::printf("INFO\tthe runtime did not start (%s)\n", Diagnostic.c_str());
        OptimizedExecution::Configure(false);
        return Run;
    }
    Run.Started = true;

    WorldSpace World;
    Runtime.ResetWorldSimulation(Context, World);
    Runtime.AccessTyreIntake().BurnoutLineLock = true;
    for (int Step = 0; Step < 810; ++Step)   // [-] - the tyres are brought up to heat without the solver, as WorldGridValidation does
    {
        InputPacket Packet{};
        Packet.ThrottleAxis  = 1.0f;
        Packet.BrakeAxis     = 0.85f;
        Packet.HandbrakeAxis = 1.0f;
        Packet.SteerAxis     = 0.16f;
        Runtime.AccessVehicleUnit().ApplyInputPacket(Packet, 1.0f / 60.0f);
        const auto At = Runtime.AccessVehicleUnit().QueryPositionMetres();
        Runtime.AccessTyreIntake().Advance(Runtime.AccessVehicleUnit(), 1.0f / 60.0f, WorldSdfScene::ResolveCourseHeight(At[0], At[1], true));
    }

    SceneViewportRequest Request{};
    Request.PanelWidth     = 256.0f;
    Request.PanelHeight    = 144.0f;
    Request.PixelDensity   = 1.0f;
    Request.CameraPosition = { -8.0, -6.0, 3.5 };
    Request.CameraTarget   = { 2.0, 0.0, 0.8 };

    for (int Tick = 0; Tick < Ticks; ++Tick)
    {
        if (Tick == 10)
            Runtime.AccessVehicleUnit().Place({ 14.0f, 0.0f, 0.53f }, 0.0f);

        const uint64_t Before = Context.QuerySubmissionCount();
        if (Tick < 10)
            Runtime.StepWorldSimulation(Context, World, 1.0 / 24.0, 1.0f, 0.85f, 0.16f, true, false);
        else
            Runtime.StepWorldSimulation(Context, World, 1.0 / 24.0, 1.0f, 0.0f, 0.0f, false, false);
        (void)Runtime.PresentSceneViewport(Context, World, Request);
        Run.Submissions.push_back(Context.QuerySubmissionCount() - Before);
        Run.RecordingLeftOpen = Run.RecordingLeftOpen || Context.QueryRecordingOpen();

        std::vector<uint8_t> Pixels;
        int                  Width = 0, Height = 0;
        if (Runtime.ReadSceneColor(Context, Pixels, Width, Height) && !Pixels.empty())
        {
            Run.SceneHashes.push_back(HashBytes(Pixels.data(), Pixels.size()));
            Run.Width  = Width;
            Run.Height = Height;
        }
        else
            Run.SceneHashes.push_back(0u);
        Run.Relocations.push_back(Runtime.QueryWorldGridRelocations());
    }

    Runtime.Shutdown(Context);
    OptimizedExecution::Configure(false);
    return Run;
}

void ValidateDevice(VulkanContext& Context, const std::filesystem::path& Archive, int Ticks, bool RequireVulkan)
{
    const DeviceRun Baseline  = RunScript(Context, Archive, false, Ticks);
    const DeviceRun Optimized = RunScript(Context, Archive, true, Ticks);
    const DeviceRun Control   = RunScript(Context, Archive, false, Ticks);
    Verify(Baseline.Started && Optimized.Started && Control.Started, "the scene runtime starts for each of the three runs");
    if (!(Baseline.Started && Optimized.Started && Control.Started))
        return;

    const size_t Count = static_cast<size_t>(Ticks);
    Verify(Baseline.SceneHashes.size() == Count && Optimized.SceneHashes.size() == Count && Control.SceneHashes.size() == Count &&
               Baseline.Width > 0 && Baseline.Width == Optimized.Width && Baseline.Height == Optimized.Height,
           "each run made " + std::to_string(Ticks) + " pictures of " + std::to_string(Baseline.Width) + " x " + std::to_string(Baseline.Height));

    size_t Distinct = 0u;
    {
        std::vector<uint64_t> Sorted = Baseline.SceneHashes;
        std::sort(Sorted.begin(), Sorted.end());
        Distinct = static_cast<size_t>(std::unique(Sorted.begin(), Sorted.end()) - Sorted.begin());
    }
    const bool AllRead = std::find(Baseline.SceneHashes.begin(), Baseline.SceneHashes.end(), uint64_t(0)) == Baseline.SceneHashes.end();
    Verify(AllRead && Distinct >= Count * 8u / 10u, "the baseline's pictures were all read back and change from tick to tick, so a difference would show (" +
                                                       std::to_string(Distinct) + " different of " + std::to_string(Count) + ")");

    size_t ControlDifferent = 0u, OptimizedDifferent = 0u;
    size_t FirstOptimized   = Count;
    for (size_t Tick = 0u; Tick < Count && Tick < Baseline.SceneHashes.size() && Tick < Optimized.SceneHashes.size() && Tick < Control.SceneHashes.size(); ++Tick)
    {
        if (Control.SceneHashes[Tick] != Baseline.SceneHashes[Tick]) ++ControlDifferent;
        if (Optimized.SceneHashes[Tick] != Baseline.SceneHashes[Tick])
        {
            ++OptimizedDifferent;
            FirstOptimized = std::min(FirstOptimized, Tick);
        }
    }
    // [unit] - parity can only be judged against a baseline that repeats itself. A device whose baseline does not is told (and fails the test where a
    //          device is required): what the optimized path did differently cannot be separated from what the device does differently every time.
    if (ControlDifferent == 0u)
    {
        Verify(true, "the baseline repeats itself on this device (" + std::to_string(ControlDifferent) + " of " + std::to_string(Ticks) +
                         " pictures differ between two baseline runs), so a difference below can be blamed on the optimization");
        Verify(OptimizedDifferent == 0u, "the optimized path's picture is the baseline's, tick for tick (" + std::to_string(OptimizedDifferent) + " of " +
                                             std::to_string(Ticks) + " differ" + (OptimizedDifferent > 0u ? ", the first at tick " + std::to_string(FirstOptimized) : std::string()) + ")");
    }
    else if (RequireVulkan)
    {
        Verify(false, "the baseline repeats itself on this device (" + std::to_string(ControlDifferent) + " of " + std::to_string(Ticks) + " pictures differ between two baseline runs)");
    }
    else
    {
        std::printf("INFO\tthe baseline does not repeat itself on this device (%zu of %d pictures differ between two baseline runs): parity is not judged here\n", ControlDifferent, Ticks);
    }

    Verify(Baseline.Relocations == Optimized.Relocations && !Baseline.Relocations.empty() && Baseline.Relocations.back() >= 1u,
           "the world window moved when the car was put 14 m ahead, on both paths, at the same ticks (" +
               std::to_string(Baseline.Relocations.empty() ? 0u : Baseline.Relocations.back()) + " relocations)");

    // [-] - submissions: the baseline makes one for the atlas (every second tick, and when the window moves), one for the step and one for the scene; the
    //       optimized path makes one. The first ticks allocate, on both paths, so they are left out of the comparison.
    uint64_t BaselineMost = 0u, OptimizedMost = 0u, BaselineLeast = UINT64_MAX;
    for (size_t Tick = 2u; Tick < Count; ++Tick)
    {
        BaselineMost  = std::max(BaselineMost, Baseline.Submissions[Tick]);
        BaselineLeast = std::min(BaselineLeast, Baseline.Submissions[Tick]);
        OptimizedMost = std::max(OptimizedMost, Optimized.Submissions[Tick]);
    }
    Verify(BaselineLeast >= 2u && BaselineMost <= 3u, "the baseline submits two or three times a tick (" + std::to_string(BaselineLeast) + " to " + std::to_string(BaselineMost) + ")");
    Verify(OptimizedMost == 1u, "the optimized path submits once a tick, the atlas, the step and the scene together (at most " + std::to_string(OptimizedMost) + ")");
    Verify(!Optimized.RecordingLeftOpen && !Baseline.RecordingLeftOpen, "no run leaves a recording open after a tick's scene");
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    bool        RequireVulkan = false;
    std::string Only;   // [-] log, atlas or device: the one part to run (empty: all)
    int         Ticks = 20;
    for (int Index = 1; Index < ArgumentCount; ++Index)
    {
        if (std::strcmp(Arguments[Index], "--require-vulkan") == 0) RequireVulkan = true;
        else if (std::strcmp(Arguments[Index], "--only") == 0 && Index + 1 < ArgumentCount) Only = Arguments[++Index];
        else if (std::strcmp(Arguments[Index], "--ticks") == 0 && Index + 1 < ArgumentCount) Ticks = std::max(12, std::atoi(Arguments[++Index]));
    }

    if (Only.empty() || Only == "log") ValidateLog();
    if (Only.empty() || Only == "log" || Only == "comparison") ValidateComparison();
    if (Only.empty() || Only == "atlas") ValidateAtlas();

    if (Only.empty() || Only == "device")
    {
        VulkanContext Context;
        std::string   Diagnostic;
        if (!Context.InitializeHeadless(Diagnostic))
        {
            if (RequireVulkan) Verify(false, "a Vulkan 1.1 device comes up (" + Diagnostic + ")");
            else std::printf("INFO\tno Vulkan device (%s): the device half is skipped\n", Diagnostic.c_str());
        }
        else
        {
            std::printf("INFO\tVulkan 1.1 on %s\n", Context.QueryDeviceSummary().c_str());
            const std::filesystem::path Archive = std::filesystem::path("Experimental/VehicleHmiPrototype/Archive");
            if (std::filesystem::exists(Archive / "ControlVehicleBody.facet"))
                ValidateDevice(Context, Archive, Ticks, RequireVulkan);
            else
                Verify(false, "Experimental/VehicleHmiPrototype/Archive is reachable: run from the repository root");
            Context.Shutdown();
        }
    }

    std::printf("[PerformanceParityValidation] %d assertions, %d failed\n", Assertions, Failures);
    return Failures == 0 ? 0 : 1;
}
