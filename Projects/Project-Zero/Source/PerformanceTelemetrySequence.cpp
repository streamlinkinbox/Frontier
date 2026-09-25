//============================================================================================================================================
//                                                PERFORMANCETELEMETRYSEQUENCE.CPP
//============================================================================================================================================
// See the header for why this exists: the report carried no performance or GPU rows because the frame loop only ever
//    wrote prose. Every quantity below is emitted twice — once as a sentence for a human reading the log, once as a
//    RecordMeasurement row so the report can be parsed, diffed and trended.

#include "PerformanceTelemetrySequence.h"

#include <algorithm>
#include <cstdio>

namespace Frontier {
namespace ProjectZero {

bool PerformanceTelemetrySequence::AdvanceFrame(float DeltaSeconds,
                                                DiagnosticMetrics&         Logger,
                                                const VisibilityTelemetry& Gpu,
                                                const PerformanceWorkload& Workload,
                                                float                      FramesPerSecond,
                                                float                      ResidentMebibytes) noexcept
{
    CelestialCpuSum[0]+=Workload.CpuCelestialTickMs;
    CelestialCpuSum[1]+=Workload.CpuSunMoonSolveMs;
    CelestialCpuSum[2]+=Workload.CpuSkyPackUploadMs;
    CelestialCpuSum[3]+=Workload.CpuWeatherPostPackUploadMs;
    SampleCount   += 1u;
    WindowSeconds += DeltaSeconds;
    PeakSeconds    = std::max(PeakSeconds, DeltaSeconds);

    if (WindowSeconds < ReportInterval || SampleCount == 0u) return false;

    const float MeanMs = 1000.0f * WindowSeconds / static_cast<float>(SampleCount);
    const float PeakMs = 1000.0f * PeakSeconds;

    char Line[512];

    //──────────────────────────────────────────────────────────────────────────
    // CPU pacing
    //──────────────────────────────────────────────────────────────────────────
    std::snprintf(Line, sizeof(Line),
                  "CPU %.2f ms/frame (%.1f fps, worst %.2f ms over %u frames), RSS %.0f MiB",
                  static_cast<double>(MeanMs), static_cast<double>(FramesPerSecond),
                  static_cast<double>(PeakMs), SampleCount, static_cast<double>(ResidentMebibytes));
    Logger.RecordMessage(DiagnosticSeverity::Information, "Performance", Line);

    Logger.RecordMeasurement("FrameTimeMeanMs",  static_cast<double>(MeanMs),            "ms");
    Logger.RecordMeasurement("FrameTimePeakMs",  static_cast<double>(PeakMs),            "ms");
    Logger.RecordMeasurement("FramesPerSecond",  static_cast<double>(FramesPerSecond),   "fps");
    Logger.RecordMeasurement("FrameSampleCount", static_cast<double>(SampleCount),       "count");
    Logger.RecordMeasurement("ResidentMemory",   static_cast<double>(ResidentMebibytes), "MiB");

    const char* CpuNames[4]={"CpuCelestialTickMeanMs","CpuSunMoonSolveMeanMs","CpuSkyPackUploadMeanMs","CpuWeatherPostPackUploadMeanMs"};
    for(unsigned I=0;I<4;++I){Logger.RecordMeasurement(CpuNames[I],CelestialCpuSum[I]/SampleCount,"ms");CelestialCpuSum[I]=0;}
    Logger.RecordMeasurement("CelestialBufferPayloadBytes",double(Workload.CelestialBufferPayloadBytes),"bytes");
    Logger.RecordMeasurement("CelestialBufferAllocationBytes",double(Workload.CelestialBufferAllocationBytes),"bytes");
    Logger.RecordMeasurement("SkySunUniformPayloadBytes",Workload.SkySunUniformBytes,"bytes");
    Logger.RecordMeasurement("CloudFogUniformPayloadBytes",Workload.CloudFogUniformBytes,"bytes");
    Logger.RecordMeasurement("PostUniformPayloadBytes",Workload.PostUniformBytes,"bytes");
    Logger.RecordMeasurement("WeatherDedicatedVolumeImageBytes",0,"bytes");
    Logger.RecordMeasurement("CloudViewStepBudget",Workload.CloudSteps,"steps");
    Logger.RecordMeasurement("LocalVolumeStepBudget",Workload.LocalSteps,"steps");
    Logger.RecordMeasurement("CloudLightTapBudget",Workload.LightTaps,"taps");
    Logger.RecordMeasurement("SunShown",Workload.SunShown,"bool");
    Logger.RecordMeasurement("SkyShown",Workload.SkyShown,"bool");
    Logger.RecordMeasurement("CloudActive",Workload.CloudActive,"bool");
    Logger.RecordMeasurement("LocalCloudActive",Workload.LocalCloudActive,"bool");
    Logger.RecordMeasurement("FogActive",Workload.FogActive,"bool");
    Logger.RecordMeasurement("GpuSeparateSkySunCloudTimersAvailable",0,"bool");
    Logger.RecordMessage(DiagnosticSeverity::Information,"CelestialTiming",
        "On the ReSTIR path sky, sun lighting, clouds and fog execute INLINE: separate timings N/A, not zero cost. "
        "Map-only sun lighting instead shares the shadow stage. "
        "GpuReSTIRMs includes geometry/materials/lighting/weather. CPU pack timings are not GPU execution. "
        "Buffer bytes cover sky+moon+post+stars only; textures, shared targets/history and shadow maps are excluded; "
        "cloud density has no dedicated volume image. Budgets are limits, not measured step counts.");

    LastMeanMs = MeanMs;
    LastPeakMs = PeakMs;

    if (Gpu.Valid)
    {
        //──────────────────────────────────────────────────────────────────────
        // GPU stage timings — the device's own timestamps
        //──────────────────────────────────────────────────────────────────────
        // One measured span, not a sum of overlapping stage intervals. The former
        // KernelMilliseconds assumption became stale when it changed to ReSTIR-only.
        // This ends before editor overlay/presentation, so it is not total board time.
        const float GpuTotal = Gpu.FrameMilliseconds;

        std::snprintf(Line, sizeof(Line),
                      "GPU %.2f ms total | cull %.2f · raster %.2f · HiZ %.2f · resolve %.2f · "
                      "ReSTIR %.2f · shadow %.2f · post %.2f · sky/sun/cloud N/A (shared stages)",
                      static_cast<double>(GpuTotal),
                      static_cast<double>(Gpu.CullMilliseconds),   static_cast<double>(Gpu.RasterMilliseconds),
                      static_cast<double>(Gpu.HiZMilliseconds),    static_cast<double>(Gpu.ResolveMilliseconds),
                      static_cast<double>(Gpu.RestirMilliseconds), static_cast<double>(Gpu.ShadowMilliseconds),
                      static_cast<double>(Gpu.PostMilliseconds));
        Logger.RecordMessage(DiagnosticSeverity::Information, "GpuTiming", Line);

        // A stage that did not run this frame reports 0 rather than being omitted, so the row set is the same
        //    shape every report and a reader can diff two runs column by column.
        Logger.RecordMeasurement("GpuFrameTotalMs", static_cast<double>(GpuTotal),                 "ms");
        Logger.RecordMeasurement("GpuCullMs",       static_cast<double>(Gpu.CullMilliseconds),     "ms");
        Logger.RecordMeasurement("GpuRasterMs",     static_cast<double>(Gpu.RasterMilliseconds),   "ms");
        Logger.RecordMeasurement("GpuHiZMs",        static_cast<double>(Gpu.HiZMilliseconds),      "ms");
        Logger.RecordMeasurement("GpuResolveMs",    static_cast<double>(Gpu.ResolveMilliseconds),  "ms");
        Logger.RecordMeasurement("GpuReSTIRMs",     static_cast<double>(Gpu.RestirMilliseconds),   "ms");
        Logger.RecordMeasurement("GpuShadowMs",     static_cast<double>(Gpu.ShadowMilliseconds),   "ms");
        Logger.RecordMeasurement("GpuPostMs",       static_cast<double>(Gpu.PostMilliseconds),     "ms");
        Logger.RecordMeasurement("GpuSkyMs",        static_cast<double>(Gpu.SkyMilliseconds),      "ms");
        Logger.RecordMeasurement("GpuVolumeMs",     static_cast<double>(Gpu.VolumeMilliseconds),   "ms");
        Logger.RecordMeasurement("GpuHistorySnapshotMs",Gpu.HistorySnapshotMilliseconds,"ms");
        for(unsigned I=0;I<5;++I){char Name[40];std::snprintf(Name,sizeof(Name),"GpuDenoiseL%uMs",I);Logger.RecordMeasurement(Name,Gpu.DenoiseLevelMilliseconds[I],"ms");}


        //──────────────────────────────────────────────────────────────────────
        // What the culling threw away
        //──────────────────────────────────────────────────────────────────────
        std::snprintf(Line, sizeof(Line),
                      "Clusters %u tested -> %u frustum, %u cone, %u visible | draws %u+%u, %u triangles",
                      Gpu.ClusterTotal, Gpu.FrustumPassed, Gpu.ConePassed, Gpu.OcclusionPassed,
                      Gpu.PhaseOneDraws, Gpu.PhaseTwoDraws, Gpu.TrianglesDrawn);
        Logger.RecordMessage(DiagnosticSeverity::Information, "Visibility", Line);

        Logger.RecordMeasurement("ClustersTested",  static_cast<double>(Gpu.ClusterTotal),    "count");
        Logger.RecordMeasurement("ClustersFrustum", static_cast<double>(Gpu.FrustumPassed),   "count");
        Logger.RecordMeasurement("ClustersCone",    static_cast<double>(Gpu.ConePassed),      "count");
        Logger.RecordMeasurement("ClustersVisible", static_cast<double>(Gpu.OcclusionPassed), "count");
        Logger.RecordMeasurement("TrianglesDrawn",  static_cast<double>(Gpu.TrianglesDrawn),  "count");
        Logger.RecordMeasurement("DrawCalls",
                                 static_cast<double>(Gpu.PhaseOneDraws + Gpu.PhaseTwoDraws),  "count");

        //──────────────────────────────────────────────────────────────────────
        // The verdict, and the workload that earned it
        //──────────────────────────────────────────────────────────────────────
        // GPU-bound and CPU-bound want opposite fixes, so the conclusion is stated rather than left to be worked
        //    out from the numbers. The common surprise on this renderer is "100 % GPU on a tiny scene", which is
        //    almost never the geometry: at 1280x720 the ReSTIR kernel dispatches ~922k threads per frame, each
        //    tracing candidate and shadow rays through a software BVH (no ray-tracing extension here, so traversal
        //    is plain compute). Triangle count barely enters into it — the cost is pixels x candidates x bounces,
        //    which is exactly why the workload rows below sit next to the timings.
        const bool  GpuBound   = GpuTotal > MeanMs * 0.85f;
        const float PixelCount = static_cast<float>(Workload.RenderWidth * Workload.RenderHeight);
        const float RestirShare = GpuTotal > 0.0f ? 100.0f * Gpu.RestirMilliseconds / GpuTotal : 0.0f;

        std::snprintf(Line, sizeof(Line),
                      "%s | %.0f kpx x (%u candidates + %u extra + %u spatial taps), %u denoise levels, "
                      "present %s | kernel %.0f%% of GPU frame",
                      GpuBound ? "GPU-BOUND" : "CPU-BOUND or presenting-limited",
                      static_cast<double>(PixelCount / 1000.0f),
                      Workload.Candidates, Workload.ExtraCandidates, Workload.SpatialTaps, Workload.DenoiseLevels,
                      Workload.PresentMode, static_cast<double>(RestirShare));
        Logger.RecordMessage(DiagnosticSeverity::Information, "Performance", Line);

        Logger.RecordMeasurement("RenderPixels",      static_cast<double>(PixelCount),               "px");
        Logger.RecordMeasurement("ReSTIRCandidates",  static_cast<double>(Workload.Candidates),      "count");
        Logger.RecordMeasurement("ReSTIRExtra",       static_cast<double>(Workload.ExtraCandidates), "count");
        Logger.RecordMeasurement("ReSTIRSpatialTaps", static_cast<double>(Workload.SpatialTaps),     "count");
        Logger.RecordMeasurement("DenoiseLevels",     static_cast<double>(Workload.DenoiseLevels),   "count");
        Logger.RecordMeasurement("GpuBound",          GpuBound ? 1.0 : 0.0,                          "bool");
        Logger.RecordMeasurement("ReSTIRShareOfFrame", static_cast<double>(RestirShare),             "percent");

        LastGpuMs    = GpuTotal;
        LastGpuBound = GpuBound;
    }
    else
    {
        Logger.RecordMessage(DiagnosticSeverity::Warning, "GpuTiming",
                             "Device timestamps unavailable - the query pool reported no completed frame.");
    }

    WindowSeconds = 0.0f;
    SampleCount   = 0u;
    PeakSeconds   = 0.0f;
    ++ReportCount;
    return true;
}

} // namespace ProjectZero
} // namespace Frontier
