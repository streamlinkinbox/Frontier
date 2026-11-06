#pragma once
/*==========================================================================================================================================
                                                          PERFORMANCELOG.H
==========================================================================================================================================*/
// 🧩 Timing log for comparing two runs of the same build, with and without the optimized execution path, on the machine that will be judged:
//
//      OptimizedExecution   one runtime switch, off unless a run asks for it, stamped in every log;
//      PerformanceScope     a named stretch of CPU time, nested stretches keep their parent: the log is a tree, not a flat list;
//      PerformanceLog       one run: the refreshes, the stretches of each, GPU timestamps and counters, written as TOML with the machine described.
//
//    Nothing here changes what is drawn or solved. Recording is off until Configure() is called, and then a scope costs two clock reads; with it off a
//    scope costs one branch. The log is TOML, readable by any TOML reader; the F2 panel (PerformancePanel) writes it and reads it back as figures. Not
//    thread safe: every call comes from the thread that runs the window's refresh loop.

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

//----------------------------------------------------------------------------------------------------------------------
//                                           OPTIMIZED EXECUTION SWITCH
//----------------------------------------------------------------------------------------------------------------------

// 📝 Off unless FRONTIER_OPTIMIZED_EXECUTION is 1/true/on in the environment or a caller configures it. The optimized path produces the same atlas, the
//    same solver textures and the same pictures as the baseline (PerformanceParityValidation holds that, bit for bit); it only does less work, or does
//    it with fewer waits for the device.
namespace OptimizedExecution
{

[[nodiscard]] bool        QueryEnabled() noexcept;
void                      Configure(bool Enabled) noexcept;
[[nodiscard]] const char* QueryModeName() noexcept;   // "baseline" or "optimized"

} // namespace OptimizedExecution

//----------------------------------------------------------------------------------------------------------------------
//                                                  RUN DESCRIPTION
//----------------------------------------------------------------------------------------------------------------------

struct PerformanceRunDescription
{
    std::string Label;                              // [-] - chosen by the person who runs it ("laptop-run-1")
    std::string Notes;                              // [-] - free text kept in the log
    std::string OutputDirectory = "PerformanceLogs";
    bool        BenchmarkEnabled    = false;        // [-] - the scripted drive, not someone at the keyboard
    int         WarmupRefreshes     = 0;            // [refresh] - at the start of the run, kept in the log but left out of the statistics
    int         PlannedRefreshes    = 0;            // [refresh] - 0 when the run ends when the window closes
    int         RetainedRefreshLimit = 36000;       // [refresh] - the most recent refreshes kept in memory (10 minutes at 60 per second); 0: all
};

//----------------------------------------------------------------------------------------------------------------------
//                                                  PERFORMANCE LOG
//----------------------------------------------------------------------------------------------------------------------

class PerformanceLog
{
  public:
    static PerformanceLog& Instance() noexcept;

    // Starts a run (and forgets any earlier one). Recording is enabled from here until Finalize().
    void Configure(const PerformanceRunDescription& Description);

    // The machine and the settings of the run, written to the log header: the device, the window, the present mode, ...
    void RegisterEnvironment(const std::string& Key, const std::string& Text);
    void RegisterEnvironmentNumber(const std::string& Key, double Number);

    [[nodiscard]] bool QueryRecordingEnabled() const noexcept { return RecordingEnabled; }
    [[nodiscard]] int  QueryRefreshCount() const noexcept { return RefreshesOpened; }
    [[nodiscard]] size_t QueryClosedRefreshCount() const noexcept { return RefreshPeriods.size(); }

    // Stops recording and forgets the run without writing it (the live view of the panel, when it closes).
    void Discard() noexcept;

    // The start of a refresh: closes the one before it and opens the next. Call once at the top of the window's loop.
    void BeginRefresh();

    // The refresh that is open is left out of the statistics (it stays in the log's count of refreshes): for the one in which the run did something
    // that is not part of what is measured, such as reading a picture back to compare it.
    void ExcludeOpenRefresh() noexcept { OpenRefreshExcluded = true; }

    // GPU time between two timestamps of one submission, and event counts (a count is summed over a refresh).
    void RegisterGpuSample(const char* Name, double Milliseconds);
    void RegisterCount(const char* Name, double Amount);

    // Ends the run: closes the open refresh, computes the statistics and writes <OutputDirectory>/<UTC stamp>_<mode>_<label>.toml
    // (the directory is made). The summary is a few lines of text for the console. Returns false if the file could not be written.
    bool Finalize(std::string& OutPath, std::string& OutSummary);

    // Used by PerformanceScope.
    void EnterScope(const char* Name);
    void LeaveScope() noexcept;

    // Exposed so that the tests can read the statistics without going through the file.
    struct SeriesStatistics
    {
        std::string Path;               // [-] - "refresh.ui_build/sim.step/vk.submit_wait"
        std::string Kind;               // [-] - "cpu", "cpu_self", "gpu" or "count"
        size_t      Refreshes   = 0;    // [refresh] - measured refreshes
        size_t      Active      = 0;    // [refresh] - of them, those in which the series was seen
        double      Total       = 0.0;  // [ms or count]
        double      Mean        = 0.0;  // [ms or count] - over all measured refreshes
        double      MeanActive  = 0.0;  // [ms or count] - over the refreshes in which it was seen
        double      Minimum     = 0.0;
        double      Median      = 0.0;
        double      Percentile90 = 0.0;
        double      Percentile95 = 0.0;
        double      Percentile99 = 0.0;
        double      Maximum     = 0.0;
        double      Deviation   = 0.0;  // [ms or count] - population standard deviation
    };
    [[nodiscard]] std::vector<SeriesStatistics> ComputeStatistics() const;
    [[nodiscard]] SeriesStatistics              ComputeRefreshPeriodStatistics() const;

    // The same figures over the most recent Count closed refreshes (those left out are skipped; the warm-up is not): what the live view shows.
    [[nodiscard]] std::vector<SeriesStatistics> ComputeStatisticsLast(size_t Count, SeriesStatistics* OutPeriod) const;
    [[nodiscard]] std::vector<double>           QueryRecentPeriods(size_t Count) const;   // [ms] - oldest first

    // The value of a statistic of a sorted copy: the smallest sample such that Fraction of the samples are at or below it (nearest rank).
    [[nodiscard]] static double ResolveNearestRank(std::vector<double> Samples, double Fraction);

  private:
    PerformanceLog() = default;

    struct Series
    {
        std::string         Leaf;
        std::string         Path;
        const char*         Kind        = "cpu";   // [-] - "cpu", "gpu" or "count"
        int                 Parent      = -1;      // [-] - index of the enclosing series, or -1
        std::vector<int>    Children{};
        std::vector<double> PerRefresh{};          // [ms or count] - one sum per closed refresh
    };

    struct OpenScope
    {
        int                                   SeriesIndex = -1;
        std::chrono::steady_clock::time_point Start{};
    };

    [[nodiscard]] int  ResolveSeries(int Parent, const char* Leaf, const char* Kind);
    void               CloseRefresh(std::chrono::steady_clock::time_point End);
    void               AddSample(int SeriesIndex, double Amount) noexcept;
    [[nodiscard]] std::vector<double> ResolveSelfSamples(int SeriesIndex, const std::vector<size_t>& Refreshes) const;
    [[nodiscard]] std::vector<size_t> ResolveMeasuredRefreshes() const;
    [[nodiscard]] std::vector<SeriesStatistics> ComputeStatisticsOver(const std::vector<size_t>& Refreshes) const;
    [[nodiscard]] SeriesStatistics              ComputePeriodStatisticsOver(const std::vector<size_t>& Refreshes) const;
    void                                        DropOldestRefreshes(size_t Count);

    bool                                  RecordingEnabled = false;
    PerformanceRunDescription             Description{};
    std::vector<std::pair<std::string, std::string>> EnvironmentText{};
    std::vector<std::pair<std::string, double>>      EnvironmentNumbers{};

    std::vector<Series>                   AllSeries{};
    std::vector<int>                      Roots{};            // [-] - the series that have no parent
    std::vector<double>                   OpenSums{};
    std::vector<OpenScope>                ScopeStack{};

    std::vector<std::chrono::steady_clock::time_point> RefreshStarts{};   // [-] - one per opened refresh
    std::vector<double>                                 RefreshPeriods{};  // [ms] - one per closed refresh (start to next start)
    std::vector<char>                                   RefreshExcluded{};  // [-] - one per closed refresh: 1 if it is left out of the statistics
    int                                                 RefreshesOpened = 0;
    bool                                                RefreshOpen     = false;
    bool                                                OpenRefreshExcluded = false;
    std::chrono::steady_clock::time_point               RunStart{};
    std::string                                         StartedUtc{};
};

//----------------------------------------------------------------------------------------------------------------------
//                                                  PERFORMANCE SCOPE
//----------------------------------------------------------------------------------------------------------------------

// 📝 Name must outlive the run (a string literal). Scopes nest; the nesting is the structure of the log.
class PerformanceScope
{
  public:
    explicit PerformanceScope(const char* Name)
    {
        PerformanceLog& Log = PerformanceLog::Instance();
        if (Log.QueryRecordingEnabled())
        {
            Log.EnterScope(Name);
            Entered = true;
        }
    }
    ~PerformanceScope()
    {
        if (Entered)
            PerformanceLog::Instance().LeaveScope();
    }

    PerformanceScope(const PerformanceScope&)            = delete;
    PerformanceScope& operator=(const PerformanceScope&) = delete;

  private:
    bool Entered = false;
};

} // namespace Frontier
