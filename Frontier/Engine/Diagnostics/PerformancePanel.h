#pragma once
/*==========================================================================================================================================
                                                          PERFORMANCEPANEL.H
==========================================================================================================================================*/
// 🧩 The F2 performance window of the Everest editor: one build, one switch.
//
//      Optimized execution   a switch that is off until it is turned on: the original path, or the one that makes the same pictures with less work
//                            (see PerformanceLog.h; PerformanceParityValidation holds that the pictures are the same);
//      Live                  what the last 300 refreshes cost, with the optimized path on or off, so that the switch can be tried by eye;
//      A/B test              a scripted drive (a burnout, a drive, a brake, a circle; fixed step; fixed camera) run four times, baseline, optimized,
//                            optimized, baseline, so that the two paths meet the same machine at the same temperature. It reports both, the
//                            spread between rounds, and whether the scene was drawn the same by both paths; and writes every run as a TOML log
//                            and the report as text, to PerformanceLogs/.
//
//    The panel owns no device work of its own: it switches OptimizedExecution, the GPU timestamps and the present mode, and asks the host for a
//    scripted drive and a fixed camera while a test runs. Everything is called from the thread of the window's refresh loop.

#include "Frontier/Engine/Diagnostics/PerformanceLog.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Frontier
{

class VulkanContext;
class EverestPlumeSceneRuntime;

//----------------------------------------------------------------------------------------------------------------------
//                                                    TEST DESCRIPTION
//----------------------------------------------------------------------------------------------------------------------

struct PerformanceTestSettings
{
    int         Rounds                = 2;      // [-] - baseline and optimized each run once per round; two rounds run baseline, optimized, optimized, baseline
    int         WarmupRefreshes       = 120;    // [refresh] - at the start of every run, left out of its figures
    int         MeasuredRefreshes     = 600;    // [refresh] - of every run (a drive of 720 refreshes in all is one lap of the script)
    bool        UncappedEnabled       = true;   // [-] - present without waiting for the display, so that a faster run shows in the refresh period
    bool        GpuTimestampsEnabled  = true;   // [-] - where the device has them
    bool        PicturesEnabled       = true;   // [-] - read the scene back at five refreshes of every run and compare the runs
    std::string OutputDirectory       = "PerformanceLogs";
};

// 📝 One run of the test: its figures, the pictures it made, the file it was written to.
struct PerformancePhaseResult
{
    std::string                                   Label{};            // [-] - "1 of 4"
    bool                                          Optimized = false;
    int                                           Round     = 0;
    PerformanceLog::SeriesStatistics              Period{};
    std::vector<PerformanceLog::SeriesStatistics> Series{};
    std::vector<uint64_t>                         PictureHashes{};    // [-] - one per checkpoint, 0 where the picture could not be read
    int                                           PictureWidth  = 0;  // [px]
    int                                           PictureHeight = 0;  // [px]
    std::string                                   LogPath{};
};

struct PerformanceTestResult
{
    bool                                Finished = false;
    bool                                Aborted  = false;
    std::string                         Failure{};            // [-] - why no result exists
    std::string                         PresentMode{};
    std::string                         Device{};
    std::string                         Scene{};
    PerformanceTestSettings             Settings{};
    std::vector<PerformancePhaseResult> Phases{};
    std::string                         ReportPath{};
    std::string                         ReportText{};
};

// 📝 The two paths side by side: the rows of the table and the words under it.
struct PerformanceComparisonRow
{
    std::string Caption{};
    std::string Unit{};
    double      Baseline       = 0.0;
    double      Optimized      = 0.0;
    bool        LowerIsBetter  = true;
    bool        Indented       = false;
};

struct PerformanceComparison
{
    std::vector<PerformanceComparisonRow> Rows{};
    bool                                  Valid             = false;   // [-] - every run drew the scene and stepped the simulation on every refresh
    std::string                           InvalidReason{};
    bool                                  PicturesCompared  = false;
    bool                                  PicturesIdentical = false;   // [-] - every run made the pictures of the first run, at every checkpoint
    bool                                  BaselineRepeats   = false;   // [-] - the baseline runs made the same pictures as each other
    int                                   PicturesChecked   = 0;       // [-] - checkpoints compared
    double                                PeriodChange      = 0.0;     // [-] - optimized against baseline, mean refresh period; negative: shorter
    double                                Spread            = 0.0;     // [-] - the largest difference between the rounds of one path, as a share of its mean
    bool                                  SpreadKnown       = false;   // [-] - two rounds or more
    std::string                           Verdict{};
    std::vector<std::string>              Notes{};
};

// 📝 Pure functions (no device, no window): what the panel shows, and what PerformanceParityValidation holds to account.
[[nodiscard]] PerformanceComparison ComparePerformancePhases(const std::vector<PerformancePhaseResult>& Phases, const std::string& PresentMode);
[[nodiscard]] std::string           ConstructPerformanceReport(const PerformanceTestResult& Result, const PerformanceComparison& Comparison);

//----------------------------------------------------------------------------------------------------------------------
//                                                    PERFORMANCE PANEL
//----------------------------------------------------------------------------------------------------------------------

class PerformancePanel
{
  public:
    // 📝 What the panel needs of the workspace: the simulation's switch and the viewport camera. Without them the test cannot run.
    struct WorkspaceHooks
    {
        std::function<bool()>     QuerySimulationActive{};
        std::function<void(bool)> ConfigureSimulationActive{};
        std::function<void()>     ConfigureTestCamera{};     // [-] - puts the viewport camera where the test looks, every refresh while it runs
        std::function<void()>     RestoreCamera{};           // [-] - puts it back where the person had it, when the test is over
    };

    // 📝 One step of the simulation as the host will take it. While a test runs the test's drive replaces what the player or the interface asked for.
    struct SimulationStep
    {
        double DeltaSeconds   = 0.0;
        float  Throttle       = 0.0f;
        float  Brake          = 0.0f;
        float  Steer          = 0.0f;
        float  Handbrake      = 0.0f;
        bool   ResetRequested = false;
    };

    PerformancePanel(VulkanContext& Context, EverestPlumeSceneRuntime& Scene, WorkspaceHooks Hooks);
    ~PerformancePanel();

    PerformancePanel(const PerformancePanel&)            = delete;
    PerformancePanel& operator=(const PerformancePanel&) = delete;

    void               ConfigureOpen(bool Open) noexcept { WindowOpen = Open; }
    // 📝 The window keeps the place and size the person gave it; this puts it back at the top right of the next refresh's display, at its full height.
    void               ResetGeometry() noexcept { GeometryResetPending = true; }
    [[nodiscard]] bool QueryOpen() const noexcept { return WindowOpen; }
    void               ConfigureSettings(const PerformanceTestSettings& NewSettings) { Settings = NewSettings; }
    [[nodiscard]] const PerformanceTestSettings& QuerySettings() const noexcept { return Settings; }

    // 📝 The three calls of one pass of the window's loop: first thing; with the interface being built; and after the pass was presented.
    void BeginRefresh();
    void ConstructInterface();
    void EndRefresh();

    // 📝 The step callback calls this with the step it was given; the answer is the step to take.
    void ResolveSimulationStep(SimulationStep& Step) const;

    // 📝 The test: started from the window or by a caller; it ends by itself, or is stopped.
    void               StartTest();
    void               AbortTest();
    [[nodiscard]] bool QueryTestRunning() const noexcept { return Stage == TestStage::Running; }
    [[nodiscard]] const PerformanceTestResult& QueryResult() const noexcept { return Result; }
    [[nodiscard]] const PerformanceComparison& QueryComparison() const noexcept { return Comparison; }
    // 📝 The live figures of the refresh period (empty until the window has been open for a few refreshes) and the path they were taken on.
    [[nodiscard]] const PerformanceLog::SeriesStatistics& QueryLivePeriod() const noexcept { return LivePeriod; }
    [[nodiscard]] bool                                    QueryLiveRecording() const noexcept { return LiveRecording; }

    // 📝 The switch, applied at the start of the next refresh (never in the middle of one).
    void               RequestOptimized(bool Enabled) noexcept { RequestedOptimized = Enabled; RequestPending = true; }
    [[nodiscard]] bool QueryOptimizedRequested() const noexcept { return RequestPending ? RequestedOptimized : OptimizedExecution::QueryEnabled(); }

  private:
    enum class TestStage
    {
        Idle,
        Running,
        Finished,
    };

    struct PhasePlan
    {
        bool Optimized = false;
        int  Round     = 0;
    };

    void ApplyRequestedMode();
    void BeginLiveRecording();
    void EndLiveRecording();
    bool RefreshLiveFigures();   // [-] - false while there are too few refreshes to say anything

    void AdvanceTest();
    void BeginTestPhase();
    void EndTestPhase(bool Keep);
    void FinishTest();
    void RestoreAfterTest();
    void RegisterPhaseEnvironment(int PhaseNumber);
    [[nodiscard]] int  ResolveCheckpointIndex(int Refresh) const noexcept;   // [-] - the checkpoint a refresh is, or -1
    [[nodiscard]] int  QueryPlannedRefreshes() const noexcept { return Settings.WarmupRefreshes + Settings.MeasuredRefreshes + 1; }

    void ConstructSwitchSection(float Unit);
    void ConstructLiveSection(float Unit);
    void ConstructTestSection(float Unit);
    void ConstructSettingsSection(float Unit);
    void ConstructComparisonTable(float Unit);

    VulkanContext&            Context;
    EverestPlumeSceneRuntime& Scene;
    WorkspaceHooks            Hooks;

    bool                      WindowOpen         = false;
    PerformanceTestSettings   Settings{};
    bool                      RequestedOptimized = false;
    bool                      RequestPending     = false;

    // Live view
    bool                                          LiveRecording = false;
    int                                           LiveCountdown = 0;
    PerformanceLog::SeriesStatistics              LivePeriod{};
    std::vector<PerformanceLog::SeriesStatistics> LiveSeries{};
    std::vector<float>                            LivePlot{};

    // Test
    TestStage                 Stage               = TestStage::Idle;
    PerformanceTestResult     Result{};
    PerformanceComparison     Comparison{};
    std::vector<PhasePlan>    Plan{};
    int                       PhaseIndex          = 0;
    bool                      PhaseActive         = false;
    int                       PhaseRefreshes      = 0;        // [-] - refreshes begun in the phase
    int                       CurrentRefresh      = 0;        // [-] - index of the refresh in progress, within the phase
    std::vector<int>          Checkpoints{};
    std::vector<uint64_t>     PhaseHashes{};
    int                       PhasePictureWidth   = 0;
    int                       PhasePictureHeight  = 0;
    bool                      SavedOptimized      = false;
    bool                      SavedSimulation     = false;
    bool                      SavedUncapped       = false;
    bool                      SavedTimestamps     = false;
    bool                      UncappedChoice      = false;    // [-] - the live checkbox
    bool                      TimestampChoice     = false;    // [-] - the live checkbox
    bool                      ReportCopied        = false;
    bool                      GeometryResetPending = false;
    bool                      SelectTestTabPending = false;
    bool                      ShowSeries          = false;
};

} // namespace Frontier
