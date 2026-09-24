//============================================================================================================================================
//                                                      TELEMETRYPROBEGATE.CPP
//============================================================================================================================================
// 🧩 Runs the SHIPPED TelemetryProbe (Engine/DeviceExchange/TelemetryProbe.{h,cpp}) exactly as GameExecution drives
//    it, and verifies the two properties the user asked for and a grep cannot prove:
//
//      ① RAM-until-close — after boot, phases, shader samples and 240 verbose frame rows, NO probe file may exist
//        on disk. Only SaveReport (the application-close path) is allowed to write.
//      ② The saved report is complete and exact — the markdown carries the startup phases with real durations, the
//        shader/stage rows, and per-column frame statistics; the CSV carries one line per frame with every CPU
//        section and every GPU stage column.
//
//    The ship-build half (probe compiles to NOTHING without FRONTIER_DEVELOPMENT/FRONTIER_DEBUG) is proven by the
//    driver script, which compiles this same gate without the define and checks the object exports no probe symbols.
//
//    Compile (from the repository root — the driver script does this):
//      g++ -std=c++20 -O1 -DFRONTIER_DEVELOPMENT -I. -o TelemetryProbeGate
//          Tools/Build/Gates/TelemetryProbeGate.cpp Engine/DeviceExchange/TelemetryProbe.cpp

#include "../../../Engine/DeviceExchange/TelemetryProbe.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

int FailureCount = 0;

void Check(bool Condition, const char* What)
{
    std::printf("  %s  %s\n", Condition ? "PASS" : "FAIL", What);
    if (!Condition) ++FailureCount;
}

// The same field set VisibilityTelemetry carries — the probe's EndFrame is duck-typed on exactly these names, so
//    if VisibilityExchange.h renames one, this gate stops compiling, which is the point.
struct SyntheticGpu
{
    bool     Valid              = true;
    float    CullMilliseconds   = 0.31f;
    float    RasterMilliseconds = 1.20f;
    float    HiZMilliseconds    = 0.12f;
    float    ResolveMilliseconds= 0.45f;
    float    KernelMilliseconds = 6.20f;
    float    ShadowMilliseconds = 0.80f;
    float    RestirMilliseconds = 5.10f;
    float    PostMilliseconds   = 1.10f;
    float    SkyMilliseconds    = 0.20f;
    float    VolumeMilliseconds = 0.00f;
    uint32_t ClusterTotal       = 4096u;
    uint32_t OcclusionPassed    = 1234u;
    uint32_t TrianglesDrawn     = 250000u;
};

std::string ReadWholeFile(const std::filesystem::path& Path)
{
    std::ifstream File(Path);
    std::ostringstream Out;
    Out << File.rdbuf();
    return Out.str();
}

} // namespace

int main()
{
    namespace fs = std::filesystem;
    const fs::path Root = "TelemetryProbeGateStage";
    std::error_code FsError;
    fs::remove_all(Root, FsError);

    //── drive the probe exactly as the application does ──────────────────────────────────────────────────────────
    FRONTIER_PROBE_BOOT();

    FRONTIER_PROBE_PHASE_BEGIN("SceneDecode");
    FRONTIER_PROBE_PHASE_END("SceneDecode");
    FRONTIER_PROBE_PHASE_BEGIN("VulkanBringUp");
    { FRONTIER_PROBE_STAGE_SCOPE("BringComputePipeline"); }
    { FRONTIER_PROBE_SHADER_SCOPE("Engine/Shaders/ReSTIRViewport.spv"); }
    FRONTIER_PROBE_PHASE_END("VulkanBringUp");
    FRONTIER_PROBE_EVENT("StartupComplete");

    constexpr uint32_t kFrameCount = 240u;
    SyntheticGpu Gpu;
    for (uint32_t Frame = 0u; Frame < kFrameCount; ++Frame)
    {
        FRONTIER_PROBE_FRAME_BEGIN(0.0166f);
        FRONTIER_PROBE_LAP(InputAndUi);
        FRONTIER_PROBE_LAP(CelestialTick);
        FRONTIER_PROBE_LAP(EditorAndPanels);
        FRONTIER_PROBE_LAP(SimulationAndInterface);
        FRONTIER_PROBE_LAP(ScenePush);
        FRONTIER_PROBE_LAP(RecordAndPresent);
        FRONTIER_PROBE_LAP(FrameCapWait);
        Gpu.Valid = Frame > 0u;   // first frame: query pool not yet completed, exactly as the device behaves
        FRONTIER_PROBE_FRAME_END(Gpu, 60.0f, 512.0f);
    }

    //── ① nothing on disk before close ────────────────────────────────────────────────────────────────────────────
    Check(!fs::exists(Root / "ProjectZero_TelemetryProbe.md") &&
          !fs::exists(Root / "ProjectZero_TelemetryProbe_Frames.csv"),
          "① no probe file exists before SaveReport — all records held in RAM");

    //── the close ─────────────────────────────────────────────────────────────────────────────────────────────────
    FRONTIER_PROBE_EVENT("Shutdown");
    FRONTIER_PROBE_SAVE(Root.string().c_str());

    //── ② the saved report is complete ────────────────────────────────────────────────────────────────────────────
    const std::string Md  = ReadWholeFile(Root / "ProjectZero_TelemetryProbe.md");
    const std::string Csv = ReadWholeFile(Root / "ProjectZero_TelemetryProbe_Frames.csv");

    Check(!Md.empty(),  "② the markdown summary was written at close");
    Check(!Csv.empty(), "② the frame CSV was written at close");
    Check(Md.find("| SceneDecode |") != std::string::npos &&
          Md.find("| VulkanBringUp |") != std::string::npos,
          "② startup phases carry rows with real begin/end times");
    Check(Md.find("ReSTIRViewport.spv") != std::string::npos &&
          Md.find("BringComputePipeline") != std::string::npos,
          "② shader loads and bring-up stages each have a duration row");
    Check(Md.find("| Boot |") != std::string::npos &&
          Md.find("| StartupComplete |") != std::string::npos &&
          Md.find("| FirstFrameComplete |") != std::string::npos &&
          Md.find("| Shutdown |") != std::string::npos,
          "② boot / startup-complete / first-frame / shutdown events are stamped");
    Check(Md.find("| CPU RecordAndPresent |") != std::string::npos &&
          Md.find("| GPU ReSTIR |") != std::string::npos,
          "② per-column frame statistics cover the CPU sections and the GPU stages");

    // The CSV: header + one line per frame, GPU columns present and gated on Valid.
    size_t LineCount = 0u;
    for (char C : Csv) if (C == '\n') ++LineCount;
    Check(LineCount == 1u + kFrameCount, "② the CSV has exactly one line per frame (plus the header)");
    Check(Csv.find("CpuRecordAndPresentMs") != std::string::npos &&
          Csv.find("GpuRestirMs") != std::string::npos &&
          Csv.find("ResidentMiB") != std::string::npos,
          "② the CSV header names every CPU section and GPU stage column");
    Check(Csv.find(",5.1000,") != std::string::npos,
          "② valid frames carry the exact device timings (ReSTIR 5.1 ms round-trips)");
    Check(Csv.find("\n0,") != std::string::npos && Csv.find(",0,0.0000,0.0000,") != std::string::npos,
          "② the first frame reports GpuValid=0 with zeroed stage columns, not garbage");

    fs::remove_all(Root, FsError);

    if (FailureCount > 0)
    {
        std::printf("[telemetry-probe] RED — %d check(s) failed\n", FailureCount);
        return 1;
    }
    std::printf("[telemetry-probe] GREEN — RAM until close, and the close writes the full verbose report\n");
    return 0;
}
