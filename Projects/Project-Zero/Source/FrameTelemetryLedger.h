//============================================================================================================================================
//                                                     FRAMETELEMETRYLEDGER.H
//============================================================================================================================================
// 🧩 Verbose, per-frame, RAM-resident timing. Records every scope you mark, every frame, into a preallocated ring in
//    memory and writes NOTHING until the application closes.
//
//    Why nothing is written during the run. A verbose log that touches the disk every frame measures the disk. At
//    300 fps a single fprintf per scope is enough to change the number it is reporting, and the first thing anyone
//    does with that log is conclude the renderer is slow. So the ledger stores fixed-size binary samples in a
//    preallocated buffer — no allocation, no formatting, no I/O on the hot path — and serialises once at shutdown.
//
//    Why it is development-only. Every macro below compiles to nothing unless FRONTIER_DEVELOPMENT is defined, which
//    is the same switch the editor already uses (ToolchainSequence.ps1 -Development, CMake FRONTIER_ZERO_DEVELOPMENT).
//    A shipping build carries no ledger, no ring, no timer objects and no call sites — not a disabled branch, no
//    code at all. Verify with Tools/Build/CheckTelemetryCompileOut.sh, which compiles a TU both ways and asserts the
//    symbols vanish.
//
//    What it measures.
//      · STARTUP phases — every one-off cost between main() and the first frame, individually: device bring-up,
//        each shader module, pipeline creation, scene load, texture decode, BVH build, acceleration structures.
//        These are the numbers that answer "why does it take 9 seconds to open" and they are invisible to any
//        per-frame counter.
//      · PER-FRAME scopes — anything wrapped in FRONTIER_TELEMETRY_SCOPE, nested arbitrarily. Parent/child
//        relationships are preserved by depth, so the report can attribute a frame to its parts rather than listing
//        unrelated totals.
//
//    How the timing is taken. steady_clock around the scope, closed by the destructor, so an early return or a
//    thrown exception still closes the sample. Resolution is the platform's steady clock — sub-microsecond on
//    Windows (QPC) and Linux (CLOCK_MONOTONIC). These are CPU wall times: for GPU work they measure the SUBMISSION,
//    not the execution, which is what the device timestamp pool in VisibilityExchange is for. Both appear in the
//    report and the distinction is stated there, because conflating them is how a frame gets blamed on the wrong
//    side of the bus.

#pragma once

#include <cstdint>

// The whole facility is development-only. Everything below is either compiled or compiled away as a unit.
#ifdef FRONTIER_DEVELOPMENT

#include <chrono>
#include <string>
#include <vector>

namespace Frontier {
namespace ProjectZero {

// A closed timing sample. Deliberately POD and small: the ring holds millions of these and nothing about writing one
//    may allocate, lock or format. 32 bytes.
struct TelemetrySample
{
    const char* Label        = nullptr;   // [-]   a string LITERAL, never owned — copying a std::string here would allocate on the hot path
    double      Microseconds = 0.0;       // [µs]  wall time the scope was open
    uint32_t    FrameIndex   = 0u;        // [idx] which frame it belongs to (0 during startup)
    uint16_t    Depth        = 0u;        // [-]   nesting level, so the report can indent and attribute
    uint16_t    Category     = 0u;        // [-]   TelemetryCategory
    uint64_t    OpenOrder    = 0u;        // [-]   monotonic tick taken when the scope OPENED. Samples are submitted
                                          //       on CLOSE, so a child lands before its parent; this is what lets the
                                          //       report print startup phases in the order they actually began.
};

enum TelemetryCategory : uint16_t
{
    kTelemetryStartup = 0u,   // one-off, before the first frame
    kTelemetryFrame   = 1u,   // per-frame scope
    kTelemetryShader  = 2u,   // shader module load / pipeline creation
    kTelemetryContent = 3u,   // scene, textures, BVH
    kTelemetryShutdown= 4u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE LEDGER
//------------------------------------------------------------------------------------------------------------------------

class FrameTelemetryLedger
{
public:
    // Reserve up front. The default holds ~2 million samples (64 MB); at 40 scopes a frame that is ~50 000 frames,
    //    or 14 minutes at 60 fps, before the ring wraps. Wrapping is counted and reported rather than hidden.
    static FrameTelemetryLedger& Instance() noexcept;

    void Reserve(size_t SampleCapacity) noexcept;

    // Called by the scope timer's destructor. Never allocates: if the ring is full it overwrites the oldest sample
    //    and increments DroppedSamples, because losing the START of a long run is far less bad than either
    //    allocating mid-frame or losing the end, which is where the interesting frames usually are.
    void Submit(const TelemetrySample& Sample) noexcept;

    // The frame counter every per-frame sample is stamped with. Advanced once per frame by the host.
    void AdvanceFrame() noexcept { ++FrameIndex; }
    [[nodiscard]] uint32_t QueryFrameIndex() const noexcept { return FrameIndex; }

    // Depth tracking, so nested scopes indent correctly in the report.
    uint16_t PushDepth() noexcept { return CurrentDepth++; }
    [[nodiscard]] uint64_t NextOpenOrder() noexcept { return OpenCounter++; }
    void     PopDepth()  noexcept { if (CurrentDepth) --CurrentDepth; }

    // Mark the end of startup: everything submitted before this is a one-off cost, everything after is per-frame.
    void MarkFirstFrame() noexcept;

    // Write everything to disk. Called ONCE, at shutdown. Produces two files: a verbose per-sample log and a
    //    summary that aggregates by label (count, total, mean, min, max, and the worst single occurrence).
    [[nodiscard]] bool Serialise(const std::string& DirectoryPath, const std::string& Stem) noexcept;

    [[nodiscard]] size_t QuerySampleCount()  const noexcept { return Samples.size(); }
    [[nodiscard]] size_t QueryDroppedCount() const noexcept { return DroppedSamples; }

private:
    FrameTelemetryLedger() noexcept;

    std::vector<TelemetrySample> Samples;
    size_t   Capacity        = 0u;
    size_t   WriteCursor     = 0u;
    size_t   DroppedSamples  = 0u;
    bool     Wrapped         = false;
    uint32_t FrameIndex      = 0u;
    uint16_t CurrentDepth    = 0u;
    uint64_t OpenCounter     = 0u;
    double   StartupMicroseconds = 0.0;
    bool     StartupClosed   = false;
    std::chrono::steady_clock::time_point Origin;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE SCOPE TIMER
//------------------------------------------------------------------------------------------------------------------------

// RAII. Opens on construction, closes and submits on destruction — so an early return, a break or a throw still
//    produces a correctly closed sample rather than a missing one.
class TelemetryScope
{
public:
    TelemetryScope(const char* Label, TelemetryCategory Category) noexcept
        : ScopeLabel(Label), ScopeCategory(Category)
        , ScopeDepth(FrameTelemetryLedger::Instance().PushDepth())
        , ScopeOrder(FrameTelemetryLedger::Instance().NextOpenOrder())
        , Opened(std::chrono::steady_clock::now())
    {}

    ~TelemetryScope() noexcept
    {
        const auto Closed = std::chrono::steady_clock::now();
        FrameTelemetryLedger& Ledger = FrameTelemetryLedger::Instance();
        Ledger.PopDepth();
        TelemetrySample Sample;
        Sample.Label        = ScopeLabel;
        Sample.Microseconds = std::chrono::duration<double, std::micro>(Closed - Opened).count();
        Sample.FrameIndex   = Ledger.QueryFrameIndex();
        Sample.Depth        = ScopeDepth;
        Sample.Category     = static_cast<uint16_t>(ScopeCategory);
        Sample.OpenOrder    = ScopeOrder;
        Ledger.Submit(Sample);
    }

    TelemetryScope(const TelemetryScope&)            = delete;
    TelemetryScope& operator=(const TelemetryScope&) = delete;

private:
    const char*       ScopeLabel;
    TelemetryCategory ScopeCategory;
    uint16_t          ScopeDepth;
    uint64_t          ScopeOrder;
    std::chrono::steady_clock::time_point Opened;
};

} // namespace ProjectZero
} // namespace Frontier

//------------------------------------------------------------------------------------------------------------------------
//                                                         THE MACROS
//------------------------------------------------------------------------------------------------------------------------
// Label must be a string LITERAL — the sample stores the pointer, it does not copy. That is what keeps Submit free
//    of allocation, and a literal's lifetime is the program's.

#define FRONTIER_TELEMETRY_CONCAT_(A, B) A##B
#define FRONTIER_TELEMETRY_CONCAT(A, B)  FRONTIER_TELEMETRY_CONCAT_(A, B)

#define FRONTIER_TELEMETRY_SCOPE(Label) \
    ::Frontier::ProjectZero::TelemetryScope FRONTIER_TELEMETRY_CONCAT(TelemetryScope_, __LINE__) \
        (Label, ::Frontier::ProjectZero::kTelemetryFrame)

#define FRONTIER_TELEMETRY_SCOPE_CATEGORY(Label, Cat) \
    ::Frontier::ProjectZero::TelemetryScope FRONTIER_TELEMETRY_CONCAT(TelemetryScope_, __LINE__) \
        (Label, ::Frontier::ProjectZero::Cat)

#define FRONTIER_TELEMETRY_STARTUP(Label)  FRONTIER_TELEMETRY_SCOPE_CATEGORY(Label, kTelemetryStartup)
#define FRONTIER_TELEMETRY_SHADER(Label)   FRONTIER_TELEMETRY_SCOPE_CATEGORY(Label, kTelemetryShader)
#define FRONTIER_TELEMETRY_CONTENT(Label)  FRONTIER_TELEMETRY_SCOPE_CATEGORY(Label, kTelemetryContent)

#define FRONTIER_TELEMETRY_ADVANCE_FRAME() ::Frontier::ProjectZero::FrameTelemetryLedger::Instance().AdvanceFrame()
#define FRONTIER_TELEMETRY_FIRST_FRAME()   ::Frontier::ProjectZero::FrameTelemetryLedger::Instance().MarkFirstFrame()
#define FRONTIER_TELEMETRY_RESERVE(N)      ::Frontier::ProjectZero::FrameTelemetryLedger::Instance().Reserve(N)
#define FRONTIER_TELEMETRY_FLUSH(Dir, Stem) \
    ::Frontier::ProjectZero::FrameTelemetryLedger::Instance().Serialise(Dir, Stem)

#else // !FRONTIER_DEVELOPMENT

//    A shipping build gets NOTHING: no ledger, no ring, no timers, no call sites. Each macro expands to a statement
//    that compiles away entirely, and the (void) forms keep an unused-variable warning from appearing where a label
//    was the only use of something.
#define FRONTIER_TELEMETRY_SCOPE(Label)                   do { } while (false)
#define FRONTIER_TELEMETRY_SCOPE_CATEGORY(Label, Cat)     do { } while (false)
#define FRONTIER_TELEMETRY_STARTUP(Label)                 do { } while (false)
#define FRONTIER_TELEMETRY_SHADER(Label)                  do { } while (false)
#define FRONTIER_TELEMETRY_CONTENT(Label)                 do { } while (false)
#define FRONTIER_TELEMETRY_ADVANCE_FRAME()                do { } while (false)
#define FRONTIER_TELEMETRY_FIRST_FRAME()                  do { } while (false)
#define FRONTIER_TELEMETRY_RESERVE(N)                     do { } while (false)
#define FRONTIER_TELEMETRY_FLUSH(Dir, Stem)               (false)

#endif // FRONTIER_DEVELOPMENT
