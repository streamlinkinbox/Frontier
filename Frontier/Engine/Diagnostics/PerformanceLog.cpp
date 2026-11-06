/*==========================================================================================================================================
                                                         PERFORMANCELOG.CPP
==========================================================================================================================================*/
// 🧩 Implements the optimized-execution switch and the timing log (see PerformanceLog.h). The log keeps one sum per series per refresh, so the
//    statistics are exact (nearest rank over every measured refresh), not binned.

#include "Frontier/Engine/Diagnostics/PerformanceLog.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <thread>
#include <utility>

#if defined(_MSC_VER)
    #include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
    #include <cpuid.h>
#endif

namespace Frontier
{

namespace
{

//----------------------------------------------------------------------------------------------------------------------
//                                                ENVIRONMENT QUERIES
//----------------------------------------------------------------------------------------------------------------------

std::string ResolveEnvironmentText(const char* Name)
{
#if defined(_MSC_VER)
    char*  Text = nullptr;
    size_t Size = 0u;
    if (_dupenv_s(&Text, &Size, Name) != 0 || Text == nullptr)
        return {};
    std::string Out(Text);
    std::free(Text);
    return Out;
#else
    const char* Text = std::getenv(Name);
    return Text != nullptr ? std::string(Text) : std::string();
#endif
}

bool ResolveOptimizedFromEnvironment()
{
    std::string Text = ResolveEnvironmentText("FRONTIER_OPTIMIZED_EXECUTION");
    std::transform(Text.begin(), Text.end(), Text.begin(), [](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });
    return Text == "1" || Text == "true" || Text == "on" || Text == "yes";
}

std::atomic<bool>& AccessOptimizedSwitch() noexcept
{
    static std::atomic<bool> Enabled{ ResolveOptimizedFromEnvironment() };
    return Enabled;
}

std::string ResolveCpuBrand()
{
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    int Regs[4] = { 0, 0, 0, 0 };
    __cpuid(Regs, static_cast<int>(0x80000000u));
    if (static_cast<unsigned>(Regs[0]) < 0x80000004u)
        return "unknown";
    char Brand[49] = {};
    for (unsigned Leaf = 0u; Leaf < 3u; ++Leaf)
    {
        __cpuid(Regs, static_cast<int>(0x80000002u + Leaf));
        std::memcpy(Brand + Leaf * 16u, Regs, 16u);
    }
    return std::string(Brand);
#elif defined(__x86_64__) || defined(__i386__)
    unsigned Eax = 0u, Ebx = 0u, Ecx = 0u, Edx = 0u;
    if (__get_cpuid(0x80000000u, &Eax, &Ebx, &Ecx, &Edx) == 0 || Eax < 0x80000004u)
        return "unknown";
    char Brand[49] = {};
    for (unsigned Leaf = 0u; Leaf < 3u; ++Leaf)
    {
        __get_cpuid(0x80000002u + Leaf, &Eax, &Ebx, &Ecx, &Edx);
        const unsigned Regs[4] = { Eax, Ebx, Ecx, Edx };
        std::memcpy(Brand + Leaf * 16u, Regs, 16u);
    }
    return std::string(Brand);
#else
    return "unknown";
#endif
}

std::string ResolveCompilerText()
{
#if defined(_MSC_VER) && !defined(__clang__)
    return "MSVC " + std::to_string(_MSC_VER);
#elif defined(__clang__)
    return std::string("Clang ") + __clang_version__;
#elif defined(__GNUC__)
    return std::string("GCC ") + __VERSION__;
#else
    return "unknown";
#endif
}

const char* ResolveOperatingSystemText() noexcept
{
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "unknown";
#endif
}

std::string ResolveUtcText(const char* Pattern)
{
    const std::time_t Now = std::time(nullptr);
    std::tm           Utc{};
#if defined(_WIN32)
    gmtime_s(&Utc, &Now);
#else
    gmtime_r(&Now, &Utc);
#endif
    char Text[48] = {};
    std::strftime(Text, sizeof(Text), Pattern, &Utc);
    return std::string(Text);
}

std::string NormalizeFileLabel(const std::string& Label)
{
    std::string Out;
    for (const char Ch : Label)
    {
        const bool Keep = (Ch >= 'a' && Ch <= 'z') || (Ch >= 'A' && Ch <= 'Z') || (Ch >= '0' && Ch <= '9') || Ch == '-' || Ch == '_';
        Out.push_back(Keep ? Ch : '-');
    }
    return Out.empty() ? std::string("run") : Out;
}

// [ms] - three decimals: a microsecond, which is finer than the clock the numbers come from
double RoundMicroseconds(double Milliseconds) noexcept
{
    return std::round(Milliseconds * 1000.0) / 1000.0;
}

double RoundCount(double Amount) noexcept
{
    return std::round(Amount * 1000.0) / 1000.0;
}

} // namespace

//----------------------------------------------------------------------------------------------------------------------
//                                           OPTIMIZED EXECUTION SWITCH
//----------------------------------------------------------------------------------------------------------------------

namespace OptimizedExecution
{

bool QueryEnabled() noexcept
{
    return AccessOptimizedSwitch().load(std::memory_order_relaxed);
}

void Configure(bool Enabled) noexcept
{
    AccessOptimizedSwitch().store(Enabled, std::memory_order_relaxed);
}

const char* QueryModeName() noexcept
{
    return QueryEnabled() ? "optimized" : "baseline";
}

} // namespace OptimizedExecution

//----------------------------------------------------------------------------------------------------------------------
//                                                  RUN CONTROL
//----------------------------------------------------------------------------------------------------------------------

PerformanceLog& PerformanceLog::Instance() noexcept
{
    static PerformanceLog Log;
    return Log;
}

void PerformanceLog::Configure(const PerformanceRunDescription& NewDescription)
{
    Description = NewDescription;
    EnvironmentText.clear();
    EnvironmentNumbers.clear();
    AllSeries.clear();
    Roots.clear();
    OpenSums.clear();
    ScopeStack.clear();
    RefreshStarts.clear();
    RefreshPeriods.clear();
    RefreshExcluded.clear();
    RefreshesOpened     = 0;
    RefreshOpen         = false;
    OpenRefreshExcluded = false;
    RunStart            = std::chrono::steady_clock::now();
    StartedUtc      = ResolveUtcText("%Y-%m-%dT%H:%M:%SZ");
    RecordingEnabled = true;

    RegisterEnvironment("mode_at_start", OptimizedExecution::QueryModeName());
    RegisterEnvironment("cpu", ResolveCpuBrand());
    RegisterEnvironmentNumber("logical_cores", static_cast<double>(std::thread::hardware_concurrency()));
    RegisterEnvironment("operating_system", ResolveOperatingSystemText());
    RegisterEnvironment("compiler", ResolveCompilerText());
#if defined(FRONTIER_BUILD_REVISION)
    RegisterEnvironment("build_revision", FRONTIER_BUILD_REVISION);
#else
    RegisterEnvironment("build_revision", "unknown");
#endif
#if defined(NDEBUG)
    RegisterEnvironment("optimization_level", "release (NDEBUG defined)");
#else
    RegisterEnvironment("optimization_level", "debug (NDEBUG not defined)");
#endif
}

void PerformanceLog::RegisterEnvironment(const std::string& Key, const std::string& Text)
{
    for (auto& Entry : EnvironmentText)
    {
        if (Entry.first == Key)
        {
            Entry.second = Text;
            return;
        }
    }
    EnvironmentText.emplace_back(Key, Text);
}

void PerformanceLog::RegisterEnvironmentNumber(const std::string& Key, double Number)
{
    for (auto& Entry : EnvironmentNumbers)
    {
        if (Entry.first == Key)
        {
            Entry.second = Number;
            return;
        }
    }
    EnvironmentNumbers.emplace_back(Key, Number);
}

//----------------------------------------------------------------------------------------------------------------------
//                                              REFRESHES AND SAMPLES
//----------------------------------------------------------------------------------------------------------------------

int PerformanceLog::ResolveSeries(int Parent, const char* Leaf, const char* Kind)
{
    const std::vector<int>& Candidates = (Parent < 0) ? Roots : AllSeries[static_cast<size_t>(Parent)].Children;
    for (const int Index : Candidates)
    {
        const Series& Entry = AllSeries[static_cast<size_t>(Index)];
        if (Entry.Kind == Kind || std::strcmp(Entry.Kind, Kind) == 0)
        {
            if (std::strcmp(Entry.Leaf.c_str(), Leaf) == 0)
                return Index;
        }
    }

    Series Fresh{};
    Fresh.Leaf   = Leaf;
    Fresh.Kind   = Kind;
    Fresh.Parent = Parent;
    Fresh.Path   = (Parent < 0) ? std::string(Leaf) : AllSeries[static_cast<size_t>(Parent)].Path + "/" + Leaf;
    Fresh.PerRefresh.assign(RefreshPeriods.size(), 0.0);

    const int Index = static_cast<int>(AllSeries.size());
    AllSeries.push_back(std::move(Fresh));
    OpenSums.push_back(0.0);
    if (Parent < 0)
        Roots.push_back(Index);
    else
        AllSeries[static_cast<size_t>(Parent)].Children.push_back(Index);
    return Index;
}

void PerformanceLog::AddSample(int SeriesIndex, double Amount) noexcept
{
    if (RefreshOpen && SeriesIndex >= 0)
        OpenSums[static_cast<size_t>(SeriesIndex)] += Amount;
}

void PerformanceLog::CloseRefresh(std::chrono::steady_clock::time_point End)
{
    // [unit] - the period of a refresh is the time from its start to the start of the next, so it includes the wait for the display
    RefreshPeriods.push_back(std::chrono::duration<double, std::milli>(End - RefreshStarts.back()).count());
    RefreshExcluded.push_back(OpenRefreshExcluded ? 1 : 0);
    for (size_t Index = 0u; Index < AllSeries.size(); ++Index)
    {
        AllSeries[Index].PerRefresh.push_back(OpenSums[Index]);
        OpenSums[Index] = 0.0;
    }
    RefreshOpen         = false;
    OpenRefreshExcluded = false;

    // [unit] - a long session keeps its most recent refreshes: a quarter of the limit goes at once, so that the cost of dropping is not paid every refresh
    const size_t Limit = static_cast<size_t>(std::max(0, Description.RetainedRefreshLimit));
    if (Limit > 0u && RefreshPeriods.size() > Limit)
        DropOldestRefreshes(std::max<size_t>(1u, Limit / 4u));
}

void PerformanceLog::DropOldestRefreshes(size_t Count)
{
    Count = std::min(Count, RefreshPeriods.size());
    if (Count == 0u)
        return;
    const auto Offset = static_cast<std::ptrdiff_t>(Count);
    RefreshPeriods.erase(RefreshPeriods.begin(), RefreshPeriods.begin() + Offset);
    RefreshExcluded.erase(RefreshExcluded.begin(), RefreshExcluded.begin() + Offset);
    RefreshStarts.erase(RefreshStarts.begin(), RefreshStarts.begin() + std::min(Offset, static_cast<std::ptrdiff_t>(RefreshStarts.size())));
    for (Series& Entry : AllSeries)
        Entry.PerRefresh.erase(Entry.PerRefresh.begin(), Entry.PerRefresh.begin() + std::min(Offset, static_cast<std::ptrdiff_t>(Entry.PerRefresh.size())));
    Description.WarmupRefreshes = std::max(0, Description.WarmupRefreshes - static_cast<int>(Count));
}

void PerformanceLog::Discard() noexcept
{
    RecordingEnabled = false;
    ScopeStack.clear();
    RefreshOpen      = false;
}

void PerformanceLog::BeginRefresh()
{
    if (!RecordingEnabled)
        return;
    const auto Now = std::chrono::steady_clock::now();
    if (RefreshOpen)
        CloseRefresh(Now);
    RefreshStarts.push_back(Now);
    ++RefreshesOpened;
    RefreshOpen = true;
}

void PerformanceLog::RegisterGpuSample(const char* Name, double Milliseconds)
{
    if (!RecordingEnabled)
        return;
    AddSample(ResolveSeries(-1, Name, "gpu"), Milliseconds);
}

void PerformanceLog::RegisterCount(const char* Name, double Amount)
{
    if (!RecordingEnabled)
        return;
    AddSample(ResolveSeries(-1, Name, "count"), Amount);
}

void PerformanceLog::EnterScope(const char* Name)
{
    const int Parent = ScopeStack.empty() ? -1 : ScopeStack.back().SeriesIndex;
    OpenScope Scope{};
    Scope.SeriesIndex = ResolveSeries(Parent, Name, "cpu");
    Scope.Start       = std::chrono::steady_clock::now();
    ScopeStack.push_back(Scope);
}

void PerformanceLog::LeaveScope() noexcept
{
    if (ScopeStack.empty())
        return;
    const auto      Now     = std::chrono::steady_clock::now();
    const OpenScope Scope   = ScopeStack.back();
    ScopeStack.pop_back();
    AddSample(Scope.SeriesIndex, std::chrono::duration<double, std::milli>(Now - Scope.Start).count());
}

//----------------------------------------------------------------------------------------------------------------------
//                                                    STATISTICS
//----------------------------------------------------------------------------------------------------------------------

double PerformanceLog::ResolveNearestRank(std::vector<double> Samples, double Fraction)
{
    if (Samples.empty())
        return 0.0;
    std::sort(Samples.begin(), Samples.end());
    const double Count = static_cast<double>(Samples.size());
    // [unit] - nearest rank: the smallest sample with at least Fraction of the samples at or below it
    size_t Rank = static_cast<size_t>(std::ceil(std::clamp(Fraction, 0.0, 1.0) * Count - 1.0e-9));
    Rank        = std::clamp<size_t>(Rank, 1u, Samples.size());
    return Samples[Rank - 1u];
}

namespace
{

PerformanceLog::SeriesStatistics SummarizeSamples(const std::string& Path, const char* Kind, const std::vector<double>& Samples)
{
    PerformanceLog::SeriesStatistics Out{};
    Out.Path      = Path;
    Out.Kind      = Kind;
    Out.Refreshes = Samples.size();
    if (Samples.empty())
        return Out;

    double Sum    = 0.0;
    double Lowest = Samples.front();
    double Highest = Samples.front();
    size_t Active = 0u;
    for (const double Sample : Samples)
    {
        Sum += Sample;
        Lowest  = std::min(Lowest, Sample);
        Highest = std::max(Highest, Sample);
        if (Sample > 0.0)
            ++Active;
    }
    const double Count = static_cast<double>(Samples.size());
    Out.Total          = Sum;
    Out.Mean           = Sum / Count;
    Out.Active         = Active;
    Out.MeanActive     = Active > 0u ? Sum / static_cast<double>(Active) : 0.0;
    Out.Minimum        = Lowest;
    Out.Maximum        = Highest;
    double SquareSum   = 0.0;
    for (const double Sample : Samples)
        SquareSum += (Sample - Out.Mean) * (Sample - Out.Mean);
    Out.Deviation      = std::sqrt(SquareSum / Count);

    std::vector<double> Sorted = Samples;
    std::sort(Sorted.begin(), Sorted.end());
    auto Rank = [&](double Fraction)
    {
        size_t Position = static_cast<size_t>(std::ceil(Fraction * Count - 1.0e-9));
        Position        = std::clamp<size_t>(Position, 1u, Sorted.size());
        return Sorted[Position - 1u];
    };
    Out.Median       = Rank(0.50);
    Out.Percentile90 = Rank(0.90);
    Out.Percentile95 = Rank(0.95);
    Out.Percentile99 = Rank(0.99);
    return Out;
}

} // namespace

std::vector<size_t> PerformanceLog::ResolveMeasuredRefreshes() const
{
    // [unit] - the refreshes that count: after the warm-up, before the last one closed (it was cut short by the end of the run), and not left out
    const size_t        Closed = RefreshPeriods.size();
    const size_t        Last   = Closed > 0u ? Closed - 1u : 0u;
    const size_t        First  = std::min(static_cast<size_t>(std::max(0, Description.WarmupRefreshes)), Last > 0u ? Last - 1u : 0u);
    std::vector<size_t> Out;
    Out.reserve(Last > First ? Last - First : 0u);
    for (size_t Refresh = First; Refresh < Last; ++Refresh)
    {
        if (Refresh >= RefreshExcluded.size() || RefreshExcluded[Refresh] == 0)
            Out.push_back(Refresh);
    }
    return Out;
}

std::vector<double> PerformanceLog::ResolveSelfSamples(int SeriesIndex, const std::vector<size_t>& Refreshes) const
{
    const Series&       Parent = AllSeries[static_cast<size_t>(SeriesIndex)];
    std::vector<double> Out;
    Out.reserve(Refreshes.size());
    for (const size_t Refresh : Refreshes)
    {
        double Remaining = Parent.PerRefresh[Refresh];
        for (const int Child : Parent.Children)
            Remaining -= AllSeries[static_cast<size_t>(Child)].PerRefresh[Refresh];
        Out.push_back(std::max(0.0, Remaining));
    }
    return Out;
}

PerformanceLog::SeriesStatistics PerformanceLog::ComputePeriodStatisticsOver(const std::vector<size_t>& Refreshes) const
{
    std::vector<double> Samples;
    Samples.reserve(Refreshes.size());
    for (const size_t Refresh : Refreshes)
        Samples.push_back(RefreshPeriods[Refresh]);
    return SummarizeSamples("refresh.period", "refresh", Samples);
}

std::vector<PerformanceLog::SeriesStatistics> PerformanceLog::ComputeStatisticsOver(const std::vector<size_t>& Refreshes) const
{
    std::vector<SeriesStatistics> Out;
    const size_t                  Closed = RefreshPeriods.size();

    for (size_t Index = 0u; Index < AllSeries.size(); ++Index)
    {
        const Series& Entry = AllSeries[Index];
        if (Entry.PerRefresh.size() < Closed)
            continue;
        std::vector<double> Samples;
        Samples.reserve(Refreshes.size());
        for (const size_t Refresh : Refreshes)
            Samples.push_back(Entry.PerRefresh[Refresh]);
        Out.push_back(SummarizeSamples(Entry.Path, Entry.Kind, Samples));
        if (std::strcmp(Entry.Kind, "cpu") == 0 && !Entry.Children.empty())
            Out.push_back(SummarizeSamples(Entry.Path + ".self", "cpu_self", ResolveSelfSamples(static_cast<int>(Index), Refreshes)));
    }
    return Out;
}

PerformanceLog::SeriesStatistics PerformanceLog::ComputeRefreshPeriodStatistics() const
{
    return ComputePeriodStatisticsOver(ResolveMeasuredRefreshes());
}

std::vector<PerformanceLog::SeriesStatistics> PerformanceLog::ComputeStatistics() const
{
    return ComputeStatisticsOver(ResolveMeasuredRefreshes());
}

std::vector<PerformanceLog::SeriesStatistics> PerformanceLog::ComputeStatisticsLast(size_t Count, SeriesStatistics* OutPeriod) const
{
    const size_t        Closed = RefreshPeriods.size();
    const size_t        First  = Closed > Count ? Closed - Count : 0u;
    std::vector<size_t> Picked;
    Picked.reserve(Closed - First);
    for (size_t Refresh = First; Refresh < Closed; ++Refresh)
    {
        if (Refresh >= RefreshExcluded.size() || RefreshExcluded[Refresh] == 0)
            Picked.push_back(Refresh);
    }
    if (OutPeriod != nullptr)
        *OutPeriod = ComputePeriodStatisticsOver(Picked);
    return ComputeStatisticsOver(Picked);
}

std::vector<double> PerformanceLog::QueryRecentPeriods(size_t Count) const
{
    const size_t Closed = RefreshPeriods.size();
    const size_t First  = Closed > Count ? Closed - Count : 0u;
    return std::vector<double>(RefreshPeriods.begin() + static_cast<std::ptrdiff_t>(First), RefreshPeriods.end());
}

//----------------------------------------------------------------------------------------------------------------------
//                                                   FINALIZATION
//----------------------------------------------------------------------------------------------------------------------

namespace
{

toml::table ConstructStatisticsTable(const PerformanceLog::SeriesStatistics& Stat)
{
    const bool IsCount = (Stat.Kind == "count");
    auto       Round   = [&](double Amount) { return IsCount ? RoundCount(Amount) : RoundMicroseconds(Amount); };
    toml::table Table;
    Table.insert("path", Stat.Path);
    Table.insert("kind", Stat.Kind);
    Table.insert("refreshes", static_cast<int64_t>(Stat.Refreshes));
    Table.insert("active", static_cast<int64_t>(Stat.Active));
    Table.insert("total", Round(Stat.Total));
    Table.insert("mean", Round(Stat.Mean));
    Table.insert("mean_active", Round(Stat.MeanActive));
    Table.insert("min", Round(Stat.Minimum));
    Table.insert("p50", Round(Stat.Median));
    Table.insert("p90", Round(Stat.Percentile90));
    Table.insert("p95", Round(Stat.Percentile95));
    Table.insert("p99", Round(Stat.Percentile99));
    Table.insert("max", Round(Stat.Maximum));
    Table.insert("stddev", Round(Stat.Deviation));
    return Table;
}

} // namespace

bool PerformanceLog::Finalize(std::string& OutPath, std::string& OutSummary)
{
    OutPath.clear();
    OutSummary.clear();
    if (!RecordingEnabled)
        return false;

    if (RefreshOpen)
        CloseRefresh(std::chrono::steady_clock::now());
    ScopeStack.clear();
    RecordingEnabled = false;

    const size_t              Closed   = RefreshPeriods.size();
    const size_t              Last     = Closed > 0u ? Closed - 1u : 0u;
    const size_t              First    = std::min(static_cast<size_t>(std::max(0, Description.WarmupRefreshes)), Last > 0u ? Last - 1u : 0u);
    const std::vector<size_t> Measured = ResolveMeasuredRefreshes();
    const double              RunSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - RunStart).count();

    const SeriesStatistics              Period = ComputeRefreshPeriodStatistics();
    const std::vector<SeriesStatistics> AllStatistics = ComputeStatistics();

    toml::table Root;

    toml::table RunTable;
    RunTable.insert("schema", static_cast<int64_t>(1));
    RunTable.insert("label", Description.Label);
    RunTable.insert("notes", Description.Notes);
    RunTable.insert("mode", std::string(OptimizedExecution::QueryModeName()));
    RunTable.insert("optimized_execution", OptimizedExecution::QueryEnabled());
    RunTable.insert("benchmark", Description.BenchmarkEnabled);
    RunTable.insert("started_utc", StartedUtc);
    RunTable.insert("duration_seconds", RoundMicroseconds(RunSeconds));
    RunTable.insert("refreshes_total", static_cast<int64_t>(RefreshesOpened));
    RunTable.insert("refreshes_warmup", static_cast<int64_t>(First));
    RunTable.insert("refreshes_measured", static_cast<int64_t>(Measured.size()));
    RunTable.insert("refreshes_excluded", static_cast<int64_t>(Last > First ? (Last - First) - Measured.size() : 0u));
    RunTable.insert("refreshes_planned", static_cast<int64_t>(Description.PlannedRefreshes));
    Root.insert("run", std::move(RunTable));

    toml::table EnvironmentTable;
    for (const auto& Entry : EnvironmentText)
        EnvironmentTable.insert_or_assign(Entry.first, Entry.second);
    for (const auto& Entry : EnvironmentNumbers)
    {
        // [unit] - a whole number is written as one ("logical_cores = 8"), not as 8.0
        if (std::isfinite(Entry.second) && Entry.second == std::floor(Entry.second) && std::fabs(Entry.second) < 9.0e15)
            EnvironmentTable.insert_or_assign(Entry.first, static_cast<int64_t>(Entry.second));
        else
            EnvironmentTable.insert_or_assign(Entry.first, Entry.second);
    }
    EnvironmentTable.insert_or_assign("mode_at_end", std::string(OptimizedExecution::QueryModeName()));
    Root.insert("environment", std::move(EnvironmentTable));

    // [unit] - the figures people ask for first; the full table of every series follows
    const double MeanPeriod = Period.Mean;
    toml::table  SummaryTable;
    SummaryTable.insert("period_mean_ms", RoundMicroseconds(MeanPeriod));
    SummaryTable.insert("period_p50_ms", RoundMicroseconds(Period.Median));
    SummaryTable.insert("period_p95_ms", RoundMicroseconds(Period.Percentile95));
    SummaryTable.insert("period_p99_ms", RoundMicroseconds(Period.Percentile99));
    SummaryTable.insert("period_max_ms", RoundMicroseconds(Period.Maximum));
    SummaryTable.insert("refreshes_per_second_mean", MeanPeriod > 0.0 ? RoundMicroseconds(1000.0 / MeanPeriod) : 0.0);
    SummaryTable.insert("refreshes_per_second_1_percent_low", Period.Percentile99 > 0.0 ? RoundMicroseconds(1000.0 / Period.Percentile99) : 0.0);
    Root.insert("summary", std::move(SummaryTable));

    toml::array SeriesArray;
    SeriesArray.push_back(ConstructStatisticsTable(Period));
    for (const SeriesStatistics& Stat : AllStatistics)
        SeriesArray.push_back(ConstructStatisticsTable(Stat));
    Root.insert("series", std::move(SeriesArray));

    // [unit] - one row per second of the measured part of the run: a hitch or a slow drift shows up here and not in the totals
    toml::array WindowArray;
    if (!Measured.empty() && RefreshStarts.size() >= Last)
    {
        const auto Origin = RefreshStarts[Measured.front()];
        size_t     Cursor = 0u;
        int        Second = 0;
        while (Cursor < Measured.size())
        {
            std::vector<double> Periods;
            const auto          WindowEnd = Origin + std::chrono::seconds(Second + 1);
            while (Cursor < Measured.size() && RefreshStarts[Measured[Cursor]] < WindowEnd)
            {
                Periods.push_back(RefreshPeriods[Measured[Cursor]]);
                ++Cursor;
            }
            if (!Periods.empty())
            {
                double Sum = 0.0, Peak = 0.0;
                for (const double Sample : Periods)
                {
                    Sum += Sample;
                    Peak = std::max(Peak, Sample);
                }
                toml::table Window;
                Window.insert("second", static_cast<int64_t>(Second));
                Window.insert("refreshes", static_cast<int64_t>(Periods.size()));
                Window.insert("mean_ms", RoundMicroseconds(Sum / static_cast<double>(Periods.size())));
                Window.insert("p95_ms", RoundMicroseconds(ResolveNearestRank(Periods, 0.95)));
                Window.insert("max_ms", RoundMicroseconds(Peak));
                Window.insert("refreshes_per_second", Sum > 0.0 ? RoundMicroseconds(1000.0 * static_cast<double>(Periods.size()) / Sum) : 0.0);
                WindowArray.push_back(std::move(Window));
            }
            ++Second;
        }
    }
    Root.insert("window", std::move(WindowArray));

    const std::string FileName = ResolveUtcText("%Y%m%d-%H%M%S") + "_" + OptimizedExecution::QueryModeName() + "_" +
                                 NormalizeFileLabel(Description.Label) + ".toml";
    std::error_code Error;
    std::filesystem::create_directories(Description.OutputDirectory.empty() ? std::filesystem::path(".") : std::filesystem::path(Description.OutputDirectory), Error);
    const std::filesystem::path Path = std::filesystem::path(Description.OutputDirectory) / FileName;
    {
        std::ofstream File(Path, std::ios::binary | std::ios::trunc);
        if (!File)
        {
            OutSummary = "The performance log could not be written to " + Path.string();
            return false;
        }
        File << "# Frontier performance log (TOML).\n"
             << "# Times are milliseconds of wall-clock time; a refresh is one pass of the window's loop.\n"
             << toml::toml_formatter{ Root } << "\n";
        if (!File)
        {
            OutSummary = "The performance log could not be written to " + Path.string();
            return false;
        }
    }
    OutPath = Path.string();

    char Line[256];
    std::ostringstream Text;
    std::snprintf(Line, sizeof(Line), "Performance run '%s' (%s): %zu refreshes measured after %zu of warm-up, %.1f s in all\n",
                  Description.Label.c_str(), OptimizedExecution::QueryModeName(), Measured.size(), First, RunSeconds);
    Text << Line;
    std::snprintf(Line, sizeof(Line), "  refresh period: mean %.3f ms (%.1f /s), p50 %.3f, p95 %.3f, p99 %.3f, max %.3f ms\n",
                  Period.Mean, Period.Mean > 0.0 ? 1000.0 / Period.Mean : 0.0, Period.Median, Period.Percentile95, Period.Percentile99, Period.Maximum);
    Text << Line;
    for (const SeriesStatistics& Stat : AllStatistics)
    {
        if (Stat.Kind == "cpu" && Stat.Path.find('/') == std::string::npos)
        {
            std::snprintf(Line, sizeof(Line), "  %-34s mean %.3f ms, p95 %.3f ms\n", Stat.Path.c_str(), Stat.Mean, Stat.Percentile95);
            Text << Line;
        }
    }
    Text << "  written to " << OutPath << "\n";
    OutSummary = Text.str();
    return true;
}

} // namespace Frontier
