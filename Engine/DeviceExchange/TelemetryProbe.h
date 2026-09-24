//============================================================================================================================================
//                                                         TELEMETRYPROBE.H
//============================================================================================================================================
// 🧩 Development/debug-only, in-RAM performance probe: exact startup phase times, every shader load and pipeline
//    creation, and one VERBOSE ROW PER FRAME (CPU section laps + the GPU stage timings). Nothing touches the disk
//    while the application runs — every record accumulates in pre-reserved RAM and is written out ONCE, when the
//    application closes (FRONTIER_PROBE_SAVE at the end of main).
//
//    ⚠️ SHIP BUILDS COMPILE THIS OUT ENTIRELY. The probe exists only when FRONTIER_DEVELOPMENT (the editor build,
//    ToolchainSequence.ps1's default) or FRONTIER_DEBUG (the Debug configuration) is defined. Without either, every
//    FRONTIER_PROBE_* macro below expands to ((void)0), the class is not declared, and TelemetryProbe.cpp compiles
//    to an empty translation unit — the production binary carries no probe code, no strings, and no calls. This is
//    the same mechanism as wrapping every call site in #ifdef FRONTIER_DEVELOPMENT by hand, folded into the macro so
//    a call site cannot forget the guard.
//
//    What it records:
//      · Phases   — explicit begin/end pairs around the startup work (scene decode, texture decode, CWBVH build,
//                   Vulkan bring-up, shading-table bake, scene upload, interface bring-up), in ms since boot.
//      · Samples  — one row per shader load (SPIR-V read + vkCreateShaderModule) and per pipeline/bring-up stage,
//                   with exact durations. The bring-up stage loop in SwapchainExchange::Bring() feeds this, so every
//                   Bring* stage gets its own row without instrumenting each function.
//      · Events   — single timestamps (boot, first frame complete, shutdown).
//      · Frames   — one row per frame: Δτ, wall clock, the CPU section laps (input/UI, celestial tick, editor +
//                   panels, simulation + spatial interface, scene push, RecordAndPresent, frame-cap wait), the GPU
//                   stage milliseconds from the device timestamp pool, cluster/triangle counts, fps and resident MiB.
//
//    Output (written by SaveReport at close):
//      · <Directory>/ProjectZero_TelemetryProbe.md         — phases, samples, events, and per-column frame statistics
//      · <Directory>/ProjectZero_TelemetryProbe_Frames.csv — the raw per-frame rows, one line per frame
//
//    The singleton shape is deliberate: the shader loaders (SwapchainExchange/VisibilityExchange/InterfaceExchange/
//    BlasBuildPipeline) have no path to a per-run object without changing their public APIs, and the probe must cost
//    nothing to reach from a leaf function. All entry points are noexcept and mutex-guarded.

#pragma once

#if defined(FRONTIER_DEVELOPMENT) || defined(FRONTIER_DEBUG)
#   define FRONTIER_TELEMETRY_PROBE 1
#else
#   define FRONTIER_TELEMETRY_PROBE 0
#endif

#if FRONTIER_TELEMETRY_PROBE

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    PROBE SECTIONS
//------------------------------------------------------------------------------------------------------------------------
// The frame loop is divided by LAPS: each FRONTIER_PROBE_LAP(Section) charges the time since the previous lap (or
//    frame begin) to that section. Ordered exactly as the loop runs, so the columns in the CSV read top-to-bottom
//    like the loop reads.

enum class ProbeSection : uint32_t
{
    InputAndUi = 0,             // poll input, control centre, notifications, configuration, telemetry overlay advance
    CelestialTick,              // sky/weather/precipitation tick
    EditorAndPanels,            // settings apply, appearance, editor feed, ImGui panel construction (Panel.Present)
    SimulationAndInterface,     // spatial interface, physics, instance motion, acceleration-structure refit
    ScenePush,                  // sky/moon/post constant pushes and the accumulation-restart compares
    RecordAndPresent,           // ⑤ cull → raster → HiZ → resolve → kernel → blit → ImGui → present (CPU-side cost)
    FrameCapWait,               // the deliberate sleep/spin when Display → Frame Cap is set
    Count
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       ROW SHAPES
//------------------------------------------------------------------------------------------------------------------------

struct ProbeFrameRow
{
    uint32_t Frame           = 0u;      // [cnt] 0-based frame ordinal
    float    WallSeconds     = 0.0f;    // [s]   since boot
    float    DeltaMs         = 0.0f;    // [ms]  the frame's Δτ as the loop measured it
    float    SectionMs[static_cast<size_t>(ProbeSection::Count)] = {};   // [ms] CPU lap per section
    float    GpuCullMs       = 0.0f;    // [ms] device timestamps — see VisibilityTelemetry
    float    GpuRasterMs     = 0.0f;
    float    GpuHiZMs        = 0.0f;
    float    GpuResolveMs    = 0.0f;
    float    GpuKernelMs     = 0.0f;
    float    GpuShadowMs     = 0.0f;
    float    GpuRestirMs     = 0.0f;
    float    GpuPostMs       = 0.0f;
    float    GpuSkyMs        = 0.0f;
    float    GpuVolumeMs     = 0.0f;
    uint32_t ClusterTotal    = 0u;      // [cnt] clusters tested
    uint32_t ClustersVisible = 0u;      // [cnt] visible after HiZ
    uint32_t TrianglesDrawn  = 0u;      // [cnt]
    float    Fps             = 0.0f;    // [1/s] 2 s rolling average
    float    ResidentMiB     = 0.0f;    // [MiB]
    uint8_t  GpuValid        = 0u;      // 0 until the query pool has a completed frame
};

struct ProbePhaseRow  { std::string Name; double BeginMs = 0.0; double EndMs = -1.0; };
struct ProbeSampleRow { std::string Kind; std::string Name; double AtMs = 0.0; double Milliseconds = 0.0; };
struct ProbeEventRow  { std::string Name; double AtMs = 0.0; };

//------------------------------------------------------------------------------------------------------------------------
//                                                     TELEMETRY PROBE
//------------------------------------------------------------------------------------------------------------------------

class TelemetryProbe
{
public:
    [[nodiscard]] static TelemetryProbe& Access() noexcept;

    void   MarkBoot() noexcept;                                        // pins the epoch; call first in main()
    double NowMilliseconds() const noexcept;                           // [ms] since boot

    void   BeginPhase(const char* Name) noexcept;
    void   EndPhase(const char* Name) noexcept;                        // closes the most recent open phase of that name
    void   RecordSample(const char* Kind, const char* Name, double Milliseconds) noexcept;
    void   MarkEvent(const char* Name) noexcept;

    void   BeginFrame(float DeltaSeconds) noexcept;
    void   Lap(ProbeSection Section) noexcept;

    // Duck-typed so this header does not depend on VisibilityExchange.h: any struct with the VisibilityTelemetry
    //    field set satisfies it, and a unit test can pass a stand-in.
    template<typename TGpu>
    void EndFrame(const TGpu& Gpu, float Fps, float ResidentMiB) noexcept
    {
        EndFrameRow(Gpu.Valid,
                    Gpu.CullMilliseconds, Gpu.RasterMilliseconds, Gpu.HiZMilliseconds, Gpu.ResolveMilliseconds,
                    Gpu.KernelMilliseconds, Gpu.ShadowMilliseconds, Gpu.RestirMilliseconds, Gpu.PostMilliseconds,
                    Gpu.SkyMilliseconds, Gpu.VolumeMilliseconds,
                    Gpu.ClusterTotal, Gpu.OcclusionPassed, Gpu.TrianglesDrawn, Fps, ResidentMiB);
    }

    // The ONLY disk write in the probe's life. Called when the application closes; writes the .md summary and the
    //    .csv frame table into Directory (created if absent).
    void SaveReport(const char* Directory) noexcept;

private:
    TelemetryProbe() noexcept;

    void EndFrameRow(bool Valid,
                     float Cull, float Raster, float HiZ, float Resolve, float Kernel,
                     float Shadow, float Restir, float Post, float Sky, float Volume,
                     uint32_t Clusters, uint32_t Visible, uint32_t Triangles,
                     float Fps, float ResidentMiB) noexcept;

    using ProbeClock = std::chrono::steady_clock;

    mutable std::mutex           Guard;
    ProbeClock::time_point       Epoch;
    ProbeClock::time_point       FrameLapMark;         // rolling mark the laps measure against
    ProbeFrameRow                PendingFrame;         // the row being filled between BeginFrame and EndFrame
    bool                         FrameOpen   = false;
    uint32_t                     FrameOrdinal = 0u;
    std::vector<ProbeFrameRow>   Frames;
    std::vector<ProbePhaseRow>   Phases;
    std::vector<ProbeSampleRow>  Samples;
    std::vector<ProbeEventRow>   Events;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      RAII HELPERS
//------------------------------------------------------------------------------------------------------------------------
// Scope timers for leaf call sites (shader loads, bring-up stages). The name is copied at construction — the sample
//    row owns its string, so a transient buffer is safe to pass.

class ProbeSampleScope
{
public:
    ProbeSampleScope(const char* Kind, const char* Name) noexcept
        : KindToken(Kind), NameToken(Name ? Name : "?"), Start(std::chrono::steady_clock::now()) {}
    ~ProbeSampleScope() noexcept
    {
        const double Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
        TelemetryProbe::Access().RecordSample(KindToken, NameToken.c_str(), Ms);
    }
    ProbeSampleScope(const ProbeSampleScope&)            = delete;
    ProbeSampleScope& operator=(const ProbeSampleScope&) = delete;
private:
    const char*                           KindToken;
    std::string                           NameToken;
    std::chrono::steady_clock::time_point Start;
};

} // namespace Frontier

//------------------------------------------------------------------------------------------------------------------------
//                                                     CALL-SITE MACROS
//------------------------------------------------------------------------------------------------------------------------

#define FRONTIER_PROBE_JOIN2(A, B) A##B
#define FRONTIER_PROBE_JOIN(A, B)  FRONTIER_PROBE_JOIN2(A, B)

#define FRONTIER_PROBE_BOOT()                    ::Frontier::TelemetryProbe::Access().MarkBoot()
#define FRONTIER_PROBE_PHASE_BEGIN(Name)         ::Frontier::TelemetryProbe::Access().BeginPhase(Name)
#define FRONTIER_PROBE_PHASE_END(Name)           ::Frontier::TelemetryProbe::Access().EndPhase(Name)
#define FRONTIER_PROBE_EVENT(Name)               ::Frontier::TelemetryProbe::Access().MarkEvent(Name)
#define FRONTIER_PROBE_SHADER_SCOPE(Name)        ::Frontier::ProbeSampleScope FRONTIER_PROBE_JOIN(FrontierProbeShader_, __LINE__)("Shader", Name)
#define FRONTIER_PROBE_STAGE_SCOPE(Name)         ::Frontier::ProbeSampleScope FRONTIER_PROBE_JOIN(FrontierProbeStage_,  __LINE__)("Stage",  Name)
#define FRONTIER_PROBE_FRAME_BEGIN(DeltaSeconds) ::Frontier::TelemetryProbe::Access().BeginFrame(DeltaSeconds)
#define FRONTIER_PROBE_LAP(Section)              ::Frontier::TelemetryProbe::Access().Lap(::Frontier::ProbeSection::Section)
#define FRONTIER_PROBE_FRAME_END(Gpu, Fps, MiB)  ::Frontier::TelemetryProbe::Access().EndFrame((Gpu), (Fps), (MiB))
#define FRONTIER_PROBE_SAVE(Directory)           ::Frontier::TelemetryProbe::Access().SaveReport(Directory)

#else // !FRONTIER_TELEMETRY_PROBE — ship build: every call site compiles to nothing

#define FRONTIER_PROBE_BOOT()                    ((void)0)
#define FRONTIER_PROBE_PHASE_BEGIN(Name)         ((void)0)
#define FRONTIER_PROBE_PHASE_END(Name)           ((void)0)
#define FRONTIER_PROBE_EVENT(Name)               ((void)0)
#define FRONTIER_PROBE_SHADER_SCOPE(Name)        ((void)0)
#define FRONTIER_PROBE_STAGE_SCOPE(Name)         ((void)0)
#define FRONTIER_PROBE_FRAME_BEGIN(DeltaSeconds) ((void)0)
#define FRONTIER_PROBE_LAP(Section)              ((void)0)
#define FRONTIER_PROBE_FRAME_END(Gpu, Fps, MiB)  ((void)0)
#define FRONTIER_PROBE_SAVE(Directory)           ((void)0)

#endif // FRONTIER_TELEMETRY_PROBE
