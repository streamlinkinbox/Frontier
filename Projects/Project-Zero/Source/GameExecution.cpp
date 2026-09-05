//============================================================================================================================================
//                                                      GAMEEXECUTION.CPP
//============================================================================================================================================
// 🧩 Project-Zero entry point — opens the Vulkan window, makes a glTF level resident, runs the ReSTIR render loop.
//
//    Scene selection (R2): `Project-Zero.exe [--scene <file.gltf|glb|shaderball|showroom>] [--scale <float>]`
//        showroom — P0 spatial-interface level, exported once from ShowroomStructure then imported like any other
//        default  Projects/Project-Zero/Content/Scenes/CornellBox.gltf — regenerated from RayTracingSolver when missing,
//                 so the reference image is unchanged; the CPU solver stays only as that generator.
//        Sponza   Projects/Project-Zero/Content/Scenes/Sponza/Sponza.gltf (fetched by the build script, not committed).

#include "../../../Engine/DeviceExchange/SwapchainExchange.h"
#include "../../../Engine/DisplayPresentation/ReSTIRIntegrator.h"
#include "../../../Engine/DisplayPresentation/ShadingTableCodec.h"
#include "../../../Engine/DisplayPresentation/RenderScheduler.h"
#include "../../../Engine/DeviceExchange/DiagnosticMetrics.h"
#include "../../../Engine/DisplayPresentation/ControlCentreHost.h"
#include "../../../Engine/DisplayPresentation/PixelSpace.h"
#include "../../../Engine/DisplayPresentation/FidelityClassifier.h"
#include "../../../Engine/DisplayPresentation/NotificationQueue.h"
#include "../../../Engine/DisplayPresentation/TelemetryMetrics.h"
#include "../../../Engine/DisplayPresentation/TypefaceRegistry.h"
#include "../../../Engine/DisplayPresentation/ConfigurationRegistry.h"
#include "../../../Engine/DisplayPresentation/DiagnosticInspector.h"
#include "../../../Engine/ContentInterchange/ContentCodec.h"
#include "../../../Engine/GeometricRaster/SceneStructure.h"
#include "../../../Engine/GeometricRaster/TraversalIndex.h"
#include "FlyThroughSolver.h"
#include "RayTracingSolver.h"
#include "../../../Engine/ContentInterchange/ShaderBallStructure.h"
#include "ShowroomStructure.h"
#include "../../../Engine/DeviceExchange/InterfaceExchange.h"
#include "../../../Engine/SpatialInterface/InterfaceSequence.h"
#include "../../../Engine/GeometricRaster/ClipProjection.h"
#include "InterfaceTrialSequence.h"
#include "InstanceMotionSequence.h"
#include "PhysicsInstanceSequence.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <string>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>

int main(int argc, char** argv)
{
    // D4: how many rigid bodies the --scene drop level contains. Fixed so the exported glTF and the solver agree
    //    on instance ordinals without either having to inspect the other.
    constexpr uint32_t kDropBodyCount = 12u;

    std::string ScenePath  = "Projects/Project-Zero/Content/Scenes/CornellBox.gltf";
    float       SceneScale = 1.0f;
    bool        AnimateInstances = false;   // D3: --animate drives instance transforms from a scripted path
    for (int I = 1; I < argc; ++I)
    {
        if (std::strcmp(argv[I], "--animate") == 0) { AnimateInstances = true; continue; }
        if (I + 1 >= argc) break;
        if (std::strcmp(argv[I], "--scene") == 0) ScenePath  = argv[++I];
        if (std::strcmp(argv[I], "--scale") == 0) SceneScale = static_cast<float>(std::atof(argv[++I]));
    }
    if (ScenePath == "shaderball") ScenePath = "Projects/Project-Zero/Content/Scenes/ShaderBall.gltf";   // R4b material test level
    if (ScenePath == "showroom")   ScenePath = "Projects/Project-Zero/Content/Scenes/Showroom.gltf";     // P0 spatial-interface level
    bool DropScene = false;
    if (ScenePath == "drop") { ScenePath = "Projects/Project-Zero/Content/Scenes/ShowroomDrop.gltf"; DropScene = true; }   // D4 physics level

    //──────────────────────────────────────────────────────────────────────────
    // Telemetry sink
    //──────────────────────────────────────────────────────────────────────────
    Frontier::DiagnosticConfiguration DiagnosticConfig{};
    DiagnosticConfig.DestinationFolder          = "Diagnostics";
    DiagnosticConfig.OutputFileStem             = "ProjectZero_TelemetryReport";
    DiagnosticConfig.FileExtension              = ".md";
    DiagnosticConfig.TimestampPrefixEnabled     = true;
    DiagnosticConfig.ConsoleEchoEnabled         = true;    // 💡 mirror telemetry into the console so a failed bring-up is visible
    DiagnosticConfig.MarkdownTableFormatEnabled = true;

    Frontier::DiagnosticMetrics Logger(DiagnosticConfig);
    if (!Logger.InitializeSink())
        std::cerr << "[Project-Zero] Telemetry sink could not be opened; continuing with console output only.\n";
    Logger.RecordMessage(Frontier::DiagnosticSeverity::Information,
                         "Bootstrap", "Project-Zero windowed ReSTIR renderer starting.");

    //──────────────────────────────────────────────────────────────────────────
    // Scene — glTF level made resident (R2). The Cornell box is exported once from the analytical solver so the
    //    reference image goes through the same import path as any other level.
    //──────────────────────────────────────────────────────────────────────────
    Frontier::ProjectZero::RayTracingSolver Scene;   // CPU reference geometry (Cornell exporter + ImGui scene section)
    {
        std::error_code FsError;
        const bool IsCornell = ScenePath.find("CornellBox.gltf") != std::string::npos;
        if (IsCornell && !std::filesystem::exists(ScenePath, FsError))
        {
            std::filesystem::create_directories(std::filesystem::path(ScenePath).parent_path(), FsError);
            std::string Error;
            if (Frontier::SceneCodec::Encode(ScenePath, Frontier::ReSTIRIntegrator::BuildTriangleIndex(Scene),
                                             Frontier::ReSTIRIntegrator::BuildMaterialDescriptors(Scene), &Error))
                std::cerr << "[Scene] Exported the Cornell box to " << ScenePath << "\n";
            else
                std::cerr << "[Scene] Cornell export failed: " << Error << "\n";
        }
        const bool IsShaderBall = ScenePath.find("ShaderBall.gltf") != std::string::npos;
        if (IsShaderBall && !std::filesystem::exists(ScenePath, FsError))
        {
            std::filesystem::create_directories(std::filesystem::path(ScenePath).parent_path(), FsError);
            std::string Error;
            Frontier::ShaderBallStructure ShaderBall; ShaderBall.Construct();
            if (ShaderBall.Export(ScenePath, &Error)) std::cerr << "[Scene] Exported the shader-ball level to " << ScenePath << "\n";
            else                                     std::cerr << "[Scene] Shader-ball export failed: " << Error << "\n";
        }
        // P0 spatial-interface level. Same export-once-then-import discipline: the Cornell box stays the untouched
        //    bit-identity reference, and the showroom is a separate file the renderer only ever sees as glTF.
        const bool IsShowroom = ScenePath.find("Showroom.gltf") != std::string::npos || DropScene;
        if (IsShowroom && !std::filesystem::exists(ScenePath, FsError))
        {
            std::filesystem::create_directories(std::filesystem::path(ScenePath).parent_path(), FsError);
            std::string Error;
            Frontier::ProjectZero::ShowroomStructure Showroom; Showroom.Construct(DropScene ? kDropBodyCount : 0u);
            if (Showroom.Export(ScenePath, &Error)) std::cerr << "[Scene] Exported the showroom level to " << ScenePath << "\n";
            else                                    std::cerr << "[Scene] Showroom export failed: " << Error << "\n";
        }
    }

    Frontier::ConfigurationRegistry Configuration;
    if (!Configuration.Load("Projects/Project-Zero/Content/Slate.config.toml"))
        std::cerr << "[Configuration] " << Configuration.QueryPath() << ": " << Configuration.QueryLastError() << " - using defaults\n";

    Frontier::SceneStructure Level;
    Frontier::TextureIndex   Textures;
    uint32_t MaxTextureLevels = 1u;   // R6 row 3: deepest mip chain resident (F3 scene-census row; computed once below)
    {
        Frontier::SceneDecodeConfiguration Decode;
        Decode.UniformScale = SceneScale;
        Decode.SlabLimit    = Configuration.Query().Backend.SlabLimit;
        std::string Error;
        if (!Frontier::ContentCodec::Decode(ScenePath, Level, &Textures, Decode, &Error))   // .gltf/.glb/.fbx/.obj by extension
        {
            Logger.RecordMessage(Frontier::DiagnosticSeverity::Fatal, "Scene", ("Cannot import " + ScenePath + ": " + Error).c_str());
            Logger.TerminateSink();
            std::cerr << "\nProject-Zero could not import the scene. Press Enter to close this console.\n";
            std::cin.get();
            return 1;
        }
        if (!Error.empty()) std::cerr << "[Scene] " << Error << "\n";
        Level.AssignName(std::filesystem::path(ScenePath).stem().string());
        const Frontier::Vector3 Lo = Level.QueryBoundsMinimum(), Hi = Level.QueryBoundsMaximum();
        char Line[256];
        std::snprintf(Line, sizeof(Line), "%s: %u triangles, %zu instances, %zu clusters, %zu materials, %zu luminaires, bounds [%.2f %.2f %.2f]..[%.2f %.2f %.2f] m",
                      Level.QueryName().c_str(), Level.QueryTriangleCount(), Level.QueryInstances().size(), Level.QueryClusters().size(),
                      (size_t)Level.QueryMaterials().QueryCount(), Level.QueryLuminaires().size(), Lo.x, Lo.y, Lo.z, Hi.x, Hi.y, Hi.z);
        Logger.RecordMessage(Frontier::DiagnosticSeverity::Information, "Scene", Line);
        {
            const Frontier::MaterialIndexMetrics& M = Level.QueryMaterials().QueryMetrics();
            std::vector<std::string> TextureReport;
            (void)Textures.Decode(Configuration.Query().Backend.TextureEdgeLimit, &TextureReport);
            for (const Frontier::TextureDescriptor& T : Textures.QueryTextures())
                MaxTextureLevels = std::max(MaxTextureLevels, T.LevelCount);   // R6 row 3: LOD census for the F3 popup
            for (const std::string& L : TextureReport) Logger.RecordMessage(Frontier::DiagnosticSeverity::Information, "Textures", L.c_str());
            std::snprintf(Line, sizeof(Line), "Materials: %u descriptors -> %u records, %u slabs (limit %u, %u folded), %zu placements, %zu cameras, %zu punctual lights",
                          M.DescriptorCount, M.DescriptorCount, M.SlabCount, M.SlabLimit, M.FoldedCount, Level.QueryPlacements().size(), Level.QueryCameras().size(), Level.QueryPunctualLuminaires().size());
            Logger.RecordMessage(Frontier::DiagnosticSeverity::Information, "Materials", Line);
        }
    }
    const uint32_t LuminaireCount = static_cast<uint32_t>(Level.QueryLuminaires().size());
    uint32_t AlphaMaskedMaterialCount = 0u;   // R4b: > 0 switches shadow rays to the alpha-mask-aware walk
    for (const Frontier::MaterialRecord& R : Level.QueryMaterials().QueryRecords())
        if (R.Flags & Frontier::MaterialFlagAlphaMask) ++AlphaMaskedMaterialCount;

    // R3: Tier A acceleration structure — tinybvh binned SAH → CWBVH over the flat world-space triangles.
    // D1: built through the bottom-level entry point. The whole level is currently ONE identity-transformed
    //     instance, so object space is world space and this is bit-for-bit what Build() produced before
    //     (Scratchpad/CheckTraversalIdentity.sh is the gate). Per-instance transforms arrive in D2/D3.
    Frontier::TraversalIndex Traversal;
    {
        // SBVH; ~2× build time for ~10 % fewer steps. The drop level opts OUT: spatial splits cut triangles,
        //    which makes the tree unrefittable, and movable geometry is worth more here than the traversal gain.
        const bool HighQuality = !DropScene && Level.QueryTriangleCount() <= 2'000'000u;
        Traversal.BuildBottomLevel(Level.QueryFlatTriangles(), HighQuality);
        const Frontier::TraversalMetrics& M = Traversal.QueryMetrics();
        char Line[256];
        std::snprintf(Line, sizeof(Line), "CWBVH: %u triangles → %u nodes, %.1f KB nodes + %.1f KB leaves (%.1f B/tri), SAH %.2f, built in %.1f ms (%s)",
                      M.TriangleCount, M.NodeCount, M.NodeByteCount / 1024.0, M.LeafByteCount / 1024.0,
                      double(M.NodeByteCount + M.LeafByteCount) / std::max(1u, M.TriangleCount), M.SahCost, M.BuildMilliseconds,
                      M.HighQuality ? "spatial splits" : "binned SAH");
        Logger.RecordMessage(Frontier::DiagnosticSeverity::Information, "Traversal", Line);
    }

    //──────────────────────────────────────────────────────────────────────────
    // Camera — Unreal-style fly-through, right-handed +Z up
    //──────────────────────────────────────────────────────────────────────────
    Frontier::ProjectZero::FlyThroughConfiguration CameraConfig
    {
        2.5f,       // [m/s]    base flight speed
        3.0f,       // [-]      Shift boost multiplier
        0.00125f,   // [rad/px] mouse sensitivity (≈ 0.07°/px)
        0.5f,       // [m/s]    scroll speed increment
        12.0f       // [-]      acceleration damping
    };

    // Z-up: stand 1.95 m in front of the open face (Y < 0), eye height 1 m, looking along +Y into the box.
    Frontier::ProjectZero::FlyThroughSolver Camera(CameraConfig);
    Camera.AssignSpatialLocation(Frontier::Vector3{ 0.0f, -1.95f, 1.0f });
    Camera.AssignOrientationEuler(0.0f, 0.0f, 0.0f);
    if (Level.QueryName() == "ShaderBall")
    {
        // Shader ball: 5 m back from the front row, 2.6 m up, pitched down ~22° so all four rows fit at 55° FoV.
        Camera.AssignSpatialLocation(Frontier::Vector3{ 0.0f, -6.2f, 2.6f });
        Camera.AssignOrientationEuler(-22.0f * 3.14159265f / 180.0f, 0.0f, 0.0f);
    }
    else if (Level.QueryName() == "Showroom" || Level.QueryName() == "ShowroomDrop")
    {
        // Showroom: stand just outside the open −Y face at eye height, looking along +Y. This frames the panel
        //    anchor (0, 1.55, 1.32) dead centre with the chrome sphere directly beneath it, so the panel and its
        //    reflection are both in shot the moment the level opens.
        Camera.AssignSpatialLocation(Frontier::Vector3{ 0.0f, -1.70f, 1.45f });
        Camera.AssignOrientationEuler(0.0f, 0.0f, 0.0f);
    }
    else if (Level.QueryName() != "CornellBox")
    {
        // Other levels: start at the centre of the bounds at ~eye height, looking along +Y; flight speed scales with the level.
        const Frontier::Vector3 Lo = Level.QueryBoundsMinimum(), Hi = Level.QueryBoundsMaximum();
        Camera.AssignSpatialLocation(Frontier::Vector3{ (Lo.x + Hi.x) * 0.5f, (Lo.y + Hi.y) * 0.5f, Lo.z + std::min(1.7f, (Hi.z - Lo.z) * 0.5f) });
        CameraConfig.BaseFlightSpeed = std::max(2.5f, (Hi - Lo).Length() * 0.15f);
        Camera.AssignConfiguration(CameraConfig);
    }
    Camera.AssignFieldOfView(55.0f);
    Camera.AssignAspectRatio(1280.0f / 720.0f);

    //──────────────────────────────────────────────────────────────────────────
    // ReSTIR integrator — owns dispatch parameters, accumulation index
    //──────────────────────────────────────────────────────────────────────────
    Frontier::ReSTIRIntegratorConfiguration IntegratorConfig
    {
        8u,         // [-]  candidates per pixel
        2u,         // [-]  extra same-pixel candidates
        1.05f,      // [-]  ACES exposure
        0.015f      // [-]  ambient strength
    };

    Frontier::ReSTIRIntegrator Integrator(IntegratorConfig);

    //──────────────────────────────────────────────────────────────────────────
    // Swapchain exchange — GLFW window + Vulkan surface + compute pipeline
    //──────────────────────────────────────────────────────────────────────────
    Frontier::SwapchainConfiguration SurfaceConfig
    {
        1280u,
        720u,
        "Project-Zero  |  ReSTIR GI  |  Frontier Engine",
        true        // validation layers — set true for debugging
    };

    // Slate.config.toml is read before the device comes up: [render] ray_tracing_tier decides which traversal backend
    //    the swapchain resolves (missing file = defaults = Auto).

    Frontier::SwapchainExchange Surface(SurfaceConfig);
    Surface.AssignRayTracingRequest(static_cast<Frontier::RayTracingRequestCategory>(Configuration.Query().Backend.RayTracingTier));

    if (!Surface.Bring())
    {
        Logger.RecordMessage(Frontier::DiagnosticSeverity::Fatal,
                             "Bootstrap", "SwapchainExchange bring-up failed - see the [SwapchainExchange] lines above for the failing stage.");
        Logger.TerminateSink();
        std::cerr << "\nProject-Zero could not open its window. Press Enter to close this console.\n";
        std::cin.get();
        return 1;
    }

    Logger.RecordMessage(Frontier::DiagnosticSeverity::Information,
                         "Bootstrap", "Window and Vulkan swapchain ready.");

    {
        const Frontier::ShadingTableSet Tables = Frontier::ShadingTableCodec::Bake();   // R4b: GGX energy + LTC sheen LUTs
        Surface.UploadShadingTables(Tables.Energy.data(), Tables.Sheen.data(), Frontier::ShadingTableSet::kResolution);
    }
    Surface.UploadScene(Level, Traversal, &Textures);

    //──────────────────────────────────────────────────────────────────────────
    // D3 — scripted instance motion (--animate), proving the transform path before physics
    //──────────────────────────────────────────────────────────────────────────
    // Off by default: with no flag the instance rows are never rewritten and the renderer behaves exactly as it
    //    did, which keeps the Cornell box a valid bit-identity reference. D4 replaces the scripted driver with
    //    RigidBodySolver poses and the upload below does not change.
    std::vector<Frontier::InstanceRecord> AnimatedInstances = Level.QueryInstances();

    // D5: a mutable copy of the flat world-space triangles. The acceleration structure is refitted over these, so
    //    the bodies' traced positions follow their drawn positions. Off unless the level actually has bodies, and
    //    disabled at run time if the refit ever refuses, so a failure degrades to static shadows rather than a crash.
    std::vector<Frontier::TriangleIndex> TracedFacets = Level.QueryFlatTriangles();
    bool  TraceMovingBodies      = false;
    float RefitMillisecondsPeak  = 0.0f;   // [ms]
    Frontier::ProjectZero::InstanceMotionSequence InstanceMotion;
    bool   InstanceMotionReady = false;
    double InstanceMotionElapsed = 0.0;   // [s]

    // D4 — real rigid bodies. Takes precedence over the scripted driver: --scene drop replaces the analytic path
    //    with Jolt poses through exactly the same RefreshInstances upload, which is why D3 was worth proving first.
    Frontier::RigidBodySolver                       BodySolver;
    Frontier::ProjectZero::PhysicsInstanceSequence  BodyBridge;
    bool PhysicsReady = false;

    if (DropScene && !AnimatedInstances.empty())
    {
        Frontier::RigidBodyConfiguration SolverConfiguration;
        SolverConfiguration.FixedStepSeconds = 1.0f / 60.0f;
        if (BodySolver.Bring(SolverConfiguration))
        {
            Frontier::ProjectZero::PhysicsInstanceConfiguration BridgeConfiguration;
            // The exporter appends drop bodies after the static scenery, so they occupy the trailing instances.
            BridgeConfiguration.DropCount         = kDropBodyCount;
            BridgeConfiguration.FirstDropInstance = static_cast<uint32_t>(AnimatedInstances.size()) - kDropBodyCount;
            BridgeConfiguration.BodyRadius        = Frontier::ProjectZero::ShowroomStructure::QueryDropRadius();
            PhysicsReady = BodyBridge.Construct(BodySolver, BridgeConfiguration);
            // Refit needs a binned-SAH tree; a spatial-split (HighQuality) build cuts triangles and cannot be
            //    refitted, so the drop level knowingly trades a little traversal speed for movable geometry.
            TraceMovingBodies = PhysicsReady && Traversal.IsRefittable();
        }
        Logger.RecordMessage(PhysicsReady ? Frontier::DiagnosticSeverity::Information
                                          : Frontier::DiagnosticSeverity::Warning,
                             "Physics",
                             PhysicsReady
                                 ? "Drop scene live: " + std::to_string(BodyBridge.QueryBodyCount()) +
                                   " rigid bodies from instance " +
                                   std::to_string(static_cast<uint32_t>(AnimatedInstances.size()) - kDropBodyCount) + "."
                                 : "Drop scene requested but the solver refused - the level renders statically.");

        if (PhysicsReady)
            Logger.RecordMessage(TraceMovingBodies ? Frontier::DiagnosticSeverity::Information
                                                   : Frontier::DiagnosticSeverity::Warning,
                                 "Physics",
                                 TraceMovingBodies
                                     ? "Traced geometry follows the bodies (acceleration structure refitted per frame)."
                                     : "Acceleration structure is not refittable - bodies will move but their shadows will not.");
    }

    if (AnimateInstances && !PhysicsReady && !AnimatedInstances.empty())
    {
        // Drive the trailing half of the instance list so the static front half proves, in the same frame, that
        //    untouched rows really are untouched.
        Frontier::ProjectZero::InstanceMotionConfiguration MotionConfiguration;
        MotionConfiguration.FirstInstance = static_cast<uint32_t>(AnimatedInstances.size()) / 2u;
        MotionConfiguration.InstanceCount = static_cast<uint32_t>(AnimatedInstances.size()) - MotionConfiguration.FirstInstance;
        InstanceMotion.Construct(AnimatedInstances, MotionConfiguration);
        InstanceMotionReady = InstanceMotion.QueryDrivenCount() > 0u;

        Logger.RecordMessage(InstanceMotionReady ? Frontier::DiagnosticSeverity::Information
                                                 : Frontier::DiagnosticSeverity::Warning,
                             "Instances",
                             InstanceMotionReady
                                 ? "Scripted instance motion on: " + std::to_string(InstanceMotion.QueryDrivenCount()) +
                                   " of " + std::to_string(AnimatedInstances.size()) + " instances animated."
                                 : "Scripted instance motion requested but no instances could be driven.");
    }

    //──────────────────────────────────────────────────────────────────────────
    // ImGui panel — apply theme once after context exists
    //──────────────────────────────────────────────────────────────────────────
    Frontier::RenderScheduler Panel;
    Panel.ApplyTheme();

    //──────────────────────────────────────────────────────────────────────────
    // Control Centre — top notch + pull-down shade (engine overlay, drawn above every ImGui window)
    //──────────────────────────────────────────────────────────────────────────
    // Typefaces: every static face under EngineContent/FontArchives, loaded once into the dynamic atlas (Vulkan backend
    //    rasterises glyphs on demand). The Fonts tab reads the registry; PixelSpace text honours the applied face.
    Frontier::TypefaceRegistry Typefaces;
    (void)Typefaces.Load("EngineContent/FontArchives");
    Frontier::TypefaceRegistry::Install(&Typefaces);

    Frontier::ControlCentreHost ControlCentre;
    ControlCentre.AssignProjectName("Project-Zero");
    (void)ControlCentre.Initialize(Surface.QueryWidth(), Surface.QueryHeight());

    // The hosts are seeded from the configuration loaded before bring-up; every Apply / debounced dashboard change
    //    writes the file back.
    ControlCentre.SeedSettings(Configuration.Query().Render);
    ControlCentre.AccessAppearance().Seed(Configuration.Query().Appearance);
    ControlCentre.AccessInput().Seed(Configuration.Query().Input);
    ControlCentre.AccessNotifications().Seed(Configuration.Query().Notifications);
    Frontier::PixelSpace OverlaySurface;

    // R2 debug popup (F3) — seeded from [render] debug_view / occlusion_culling / alias_pick.
    Frontier::DiagnosticInspector Diagnostics;
    Diagnostics.Seed(static_cast<Frontier::DebugViewCategory>(Configuration.Query().Backend.DebugView), Configuration.Query().Backend.OcclusionCulling,
                     Configuration.Query().Backend.AliasPick);
    Integrator.AssignAliasPick(Configuration.Query().Backend.AliasPick);   // R6 row 3: persisted F5 state applies from the first frame

    // Dashboard-driven engine services: quality ladder, toasts, frame telemetry
    Frontier::FidelityClassifier Fidelity;
    Frontier::NotificationQueue  Notifications;
    Frontier::TelemetryMetrics   Telemetry;
    {
        // R1: announce the resolved ray-tracing backend once; a downgrade from an explicit request is an Info toast.
        const Frontier::RayTracingRequestCategory Req = Surface.QueryRayTracingRequest();
        const Frontier::RayTracingTierCategory    Use = Surface.QueryRayTracingTier();
        const bool Downgraded = Req != Frontier::RayTracingRequestCategory::Auto && static_cast<uint32_t>(Use) + 1u < static_cast<uint32_t>(Req);
        if (Downgraded)
        {
            char Body[128];
            std::snprintf(Body, sizeof(Body), "%s requested, device supports %s", Frontier::RayTracingCapabilitySet::RequestName(Req), Frontier::RayTracingCapabilitySet::TierName(Use));
            Notifications.Push("Ray-tracing tier downgraded", Body);
        }
    }
    uint32_t AppliedSettingsRevision = ~0u;   // forces the first application
    float    SettingsQuietSeconds    = 0.0f;  // [s] since the last change; the toast waits for the slider to rest
    bool     SettingsToastPending    = false;
    uint32_t AppliedAppearanceRevision = 0u;
    bool     AppearanceEverApplied     = false;
    float    FrameCapSeconds           = 0.0f;   // [s] 0 = unlimited (Display → Frame Cap)
    uint32_t FixedRenderHeight         = 0u;     // [px] 0 = native  (Display → Resolution)   // AppearanceInspector::Apply bumps its own revision
    uint32_t AppliedInputRevision      = 0u;
    uint32_t AppliedNotifyRevision     = 0u;
    bool     BakeAnnounced             = false;  // "Baking Complete" = temporal accumulation reached BakeFrameCount
    constexpr uint32_t BakeFrameCount  = 256u;
    std::string LastSaveError;                   // de-duplicates the "Autosave Errors" toast

    // Push the Control Centre settings into the renderer. Called whenever the settings revision changes.
    auto ApplyControlCentreSettings = [&](const Frontier::ControlCentreSettings& S, bool Announce)
    {
        Fidelity.AssignCategory(S.Quality);
        const Frontier::FidelityCriteria Criteria = Fidelity.QueryActiveCriteria();

        // The quality tier sets the ReSTIR budget; the GI / AA tiles override the tier's own defaults.
        Integrator.AssignCandidatesPerPixel(Criteria.ReSTIRCandidateSampleCount);
        Integrator.AssignExtraCandidateCount(Criteria.ReSTIRExtraCandidateCount);
        Integrator.AssignGlobalIllumination(S.GlobalIllumination);
        Integrator.AssignAntiAliasing(S.AntiAliasing);
        Notifications.AssignEnabled(S.Notifications);

        if (Announce)
        {
            char Body[96];
            std::snprintf(Body, sizeof(Body), "%s  |  %u candidates, %u extra, GI %s, AA %s, scale %d%%",
                          Frontier::FidelityLabel(S.Quality), Criteria.ReSTIRCandidateSampleCount,
                          Criteria.ReSTIRExtraCandidateCount, S.GlobalIllumination ? "on" : "off",
                          S.AntiAliasing ? "on" : "off", static_cast<int>(S.RenderScale * 100.0f + 0.5f));
            if (ControlCentre.QueryNotifications().QueryApplied().RenderFinished) Notifications.Push("Render settings applied", Body);
        }
    };

    Camera.AssignAspectRatio(
        static_cast<float>(Surface.QueryWidth()) /
        static_cast<float>(Surface.QueryHeight()));

    Logger.RecordMessage(Frontier::DiagnosticSeverity::Information,
                         "Bootstrap", "Entering render loop.");

    //──────────────────────────────────────────────────────────────────────────
    // Spatial interface — the world-space panel, composited over the resolved scene
    //──────────────────────────────────────────────────────────────────────────
    // The engine owns the draw (InterfaceExchange) and the shapes (InterfaceStructure); this project owns what the
    //    figures MEAN — the trial sequence composes them and normalises every value before writing it. The overlay
    //    callback is the only place the two meet, and it hands the engine nothing but a command buffer.
    Frontier::InterfaceExchange      Interface;
    Frontier::InterfaceStructure     InterfaceFigures;
    Frontier::InterfaceSequence      InterfaceCompose;
    Frontier::MotionIntegrator       InterfaceMotion;
    Frontier::ProjectZero::InterfaceTrialSequence InterfaceTrial;
    bool     InterfaceReady        = false;
    uint32_t InterfaceGeneration   = 0xFFFFFFFFu;   // forces the first Resize
    double   InterfaceElapsed      = 0.0;           // [s]

    // Filled once per frame just before RecordAndPresent; the overlay callback reads it during recording.
    Frontier::InterfaceViewClip InterfaceViewOfFrame{};

    if (Interface.Bring(Surface.QueryDevice(), Surface.QueryPhysicalDevice(),
                        Surface.QueryCycleSlotCount(), Surface.QueryColourFormat(), Surface.QueryDepthFormat()))
    {
        // Place the panel in the ROOM rather than at the world origin. ShowroomStructure publishes the anchor it
        //    reserved for exactly this — above the plinth, tilted toward the eye — so the level owns where the
        //    interface hangs and the trial sequence owns what is on it. Any other level keeps the default upright
        //    placement, which is why this is conditional rather than unconditional.
        const bool ShowroomLevel = Level.QueryName() == "Showroom" || Level.QueryName() == "ShowroomDrop";
        if (ShowroomLevel)
        {
            const Frontier::Vector3 Anchor = Frontier::ProjectZero::ShowroomStructure::QueryPanelOrigin();
            Frontier::PlanePlacement PanelPlacement;
            PanelPlacement.Origin = Frontier::PlaneOrigin{ Anchor.x, Anchor.y, Anchor.z };
            // π/2 stands the panel up (local +Y → world +Z); the showroom's tilt then leans it back toward the eye.
            PanelPlacement.RotationX = 1.57079633f + Frontier::ProjectZero::ShowroomStructure::QueryPanelTilt();
            PanelPlacement.Scale     = 2.2f;   // the trial layout is authored at ~0.14 m across; this reads at 2 m
            InterfaceTrial.AssignPanelPlacement(PanelPlacement);
        }

        InterfaceTrial.Construct(InterfaceFigures, InterfaceMotion);
        InterfaceReady = true;
        Logger.RecordMessage(Frontier::DiagnosticSeverity::Information, "Interface",
                             "Spatial interface ready: " + std::to_string(InterfaceTrial.QueryFigureCount()) +
                             " figures, depth test " + (Interface.IsDepthTested() ? "on" : "off") + ".");
    }
    else
    {
        Logger.RecordMessage(Frontier::DiagnosticSeverity::Warning, "Interface",
                             "Spatial interface unavailable - the scene renders without the panel.");
    }

    // Recorded after the scene resolves and before the blit, so the panel is part of the presented image.
    Surface.AssignOverlaySequence([&](void* Command, uint32_t CycleSlot) noexcept
    {
        if (!InterfaceReady) return;
        Interface.RecordInterface(Command, CycleSlot, InterfaceViewOfFrame);
    });

    //──────────────────────────────────────────────────────────────────────────
    // Input exchange — filled each frame by GLFW callbacks
    //──────────────────────────────────────────────────────────────────────────
    Frontier::InputExchange Input;

    //──────────────────────────────────────────────────────────────────────────
    // Render loop
    //──────────────────────────────────────────────────────────────────────────
    using Clock    = std::chrono::high_resolution_clock;
    using Duration = std::chrono::duration<float>;

    auto PreviousTime = Clock::now();

    while (!Surface.CloseRequested() && !Panel.Convert<bool>())
    {
        const auto  NowTime = Clock::now();
        float       Δτ      = std::chrono::duration_cast<Duration>(NowTime - PreviousTime).count();
        PreviousTime        = NowTime;

        // Clamp Δτ to prevent spiral-of-death on window drag or breakpoints
        if (Δτ > 0.1f) Δτ = 0.1f;

        // ① Poll input — GLFW callbacks forward into Input
        Surface.PollInput(Input);

        // ①b Control Centre owns the pointer while hovered / grabbed / pulled down; the camera never sees those clicks
        //    Display → UI Scale: the overlay lives in logical pixels (physical ÷ scale); the pointer is mapped the same way.
        const float    InterfaceScale = std::clamp(ControlCentre.QueryAppearance().QueryApplied().InterfaceScale / 100.0f, 0.5f, 2.0f);
        const uint32_t LogicalWidth   = std::max(1u, static_cast<uint32_t>(static_cast<float>(Surface.QueryWidth())  / InterfaceScale + 0.5f));
        const uint32_t LogicalHeight  = std::max(1u, static_cast<uint32_t>(static_cast<float>(Surface.QueryHeight()) / InterfaceScale + 0.5f));
        ControlCentre.Resize(LogicalWidth, LogicalHeight);
        ControlCentre.AdvanceInteraction(Input, Input.QueryCursorPositionX() / InterfaceScale, Input.QueryCursorPositionY() / InterfaceScale);
        ControlCentre.AdvanceLocomotion(Δτ);
        Notifications.Advance(Δτ);
        Configuration.Advance(Δτ);
        Telemetry.RecordFrame(Δτ);

        // ①b' F3 debug popup: view / HiZ / alias-pick toggles persist to [render] and restart the accumulation.
        // R6 row 3: the scheduler's Alias-pick checkbox writes the integrator directly — mirror it into the popup
        //    member before edge-detecting F5 so both toggles converge on one flag.
        Diagnostics.AssignAliasPick(Integrator.QueryConfiguration().AliasPick);
        if (Diagnostics.AdvanceInteraction(Input))
        {
            Configuration.Access().Backend.DebugView        = static_cast<Frontier::DebugViewSelection>(Diagnostics.QueryView());
            Configuration.Access().Backend.OcclusionCulling = Diagnostics.QueryOcclusion();
            Configuration.Access().Backend.AliasPick        = Diagnostics.QueryAliasPick();
            Configuration.MarkDirty();
            Integrator.AssignAliasPick(Diagnostics.QueryAliasPick());   // R6 row 3: F5 flips the kernel's pick live
            Integrator.ResetAccumulation();
        }

        // ①c Dashboard settings → renderer (only when something changed)
        {
            const Frontier::ControlCentreSettings& S = ControlCentre.QuerySettings();
            if (S.Revision != AppliedSettingsRevision)
            {
                const bool First = AppliedSettingsRevision == ~0u;
                ApplyControlCentreSettings(S, false);      // renderer follows every tick (live slider)
                AppliedSettingsRevision = S.Revision;
                if (!First) { Configuration.Access().Render = S; Configuration.MarkDirty(); }   // debounced write, one per gesture
                SettingsQuietSeconds = 0.0f;
                SettingsToastPending = !First;
            }
            else if (SettingsToastPending)
            {
                SettingsQuietSeconds += Δτ;
                if (SettingsQuietSeconds >= 0.4f)          // one toast per gesture, not per drag tick
                {
                    ApplyControlCentreSettings(S, true);
                    SettingsToastPending = false;
                }
            }
        }

        // ①d Appearance page → Apply (explicit, dialogue-confirmed when leaving dirty). Display settings are consumed
        //    here: V-Sync → swapchain present mode, fullscreen → GLFW monitor switch, frame cap → loop pacing below,
        //    resolution → render-target size (step ④).
        {
            const Frontier::AppearanceInspector& A = ControlCentre.QueryAppearance();
            if (A.QueryRevision() != AppliedAppearanceRevision)
            {
                const Frontier::AppearanceSettings& P = A.QueryApplied();
                AppliedAppearanceRevision = A.QueryRevision();
                const bool FirstAppearance = !AppearanceEverApplied;
                AppearanceEverApplied = true;
                if (!FirstAppearance)   // start-up seed: apply silently, nothing to persist or announce
                {
                    Configuration.Access().Appearance = P;
                    if (!Configuration.Save()) std::cerr << "[Configuration] save failed: " << Configuration.QueryLastError() << "\n";
                }
                Surface.AssignPresentPacing(P.VerticalSync == Frontier::VerticalSyncCategory::Off      ? Frontier::PresentPacingCategory::VerticalSyncOff
                                          : P.VerticalSync == Frontier::VerticalSyncCategory::Adaptive ? Frontier::PresentPacingCategory::VerticalSyncAdaptive
                                                                                                        : Frontier::PresentPacingCategory::VerticalSyncOn);
                Surface.AssignFullscreen(P.Fullscreen);
                FrameCapSeconds = P.FrameCap == Frontier::FrameCapCategory::Cap60  ? 1.0f / 60.0f
                                : P.FrameCap == Frontier::FrameCapCategory::Cap120 ? 1.0f / 120.0f
                                : P.FrameCap == Frontier::FrameCapCategory::Cap144 ? 1.0f / 144.0f : 0.0f;
                FixedRenderHeight = P.Resolution == Frontier::RenderResolutionCategory::Quad1440 ? 1440u
                                  : P.Resolution == Frontier::RenderResolutionCategory::Full1080 ? 1080u
                                  : P.Resolution == Frontier::RenderResolutionCategory::Half720  ? 720u : 0u;
                char Body[128];
                const Frontier::TypefaceFamily* Fam = Typefaces.QueryFamily(P.FontFamily);
                std::snprintf(Body, sizeof(Body), "%s  |  %s  |  UI %d%%  |  radius %dpx  |  V-Sync %s%s",
                              Frontier::AppearanceInspector::QueryThemeName(P.Theme), Fam ? Fam->Name.c_str() : "default face", static_cast<int>(P.InterfaceScale),
                              static_cast<int>(P.CornerRadius),
                              P.VerticalSync == Frontier::VerticalSyncCategory::Off ? "off" : P.VerticalSync == Frontier::VerticalSyncCategory::On ? "on" : "adaptive",
                              P.Fullscreen ? "  |  fullscreen" : "");
                if (!FirstAppearance && ControlCentre.QueryNotifications().QueryApplied().RenderFinished) Notifications.Push("Appearance applied", Body);
            }
        }

        // ①e Input page → Save keybindings: sensitivity % → rad/px (50 % = base 0.00125, linear 0.25 × … 2 ×) and
        //    Invert Y-Axis into the fly-through configuration. Profile / shortcut fields are persisted but not yet
        //    consumed by the solver (flagged in the step report).
        {
            const Frontier::InputInspector& I = ControlCentre.QueryInput();
            if (I.QueryRevision() != AppliedInputRevision)
            {
                const bool First = AppliedInputRevision == 0u;
                AppliedInputRevision = I.QueryRevision();
                const Frontier::InputPreferences& P = I.QueryApplied();
                Frontier::ProjectZero::FlyThroughConfiguration C = Camera.QueryConfiguration();
                C.MouseSensitivity = 0.00125f * (0.25f + (P.MouseSensitivity / 100.0f) * 1.75f);
                C.InvertPitch      = P.InvertPitch;
                Camera.AssignConfiguration(C);
                if (!First)
                {
                    Configuration.Access().Input = P;
                    if (!Configuration.Save()) std::cerr << "[Configuration] save failed: " << Configuration.QueryLastError() << "\n";
                    char Body[96];
                    std::snprintf(Body, sizeof(Body), "%s  |  sensitivity %d%%  |  Y-axis %s",
                                  Frontier::InputInspector::QueryProfileName(P.Profile), static_cast<int>(P.MouseSensitivity), P.InvertPitch ? "inverted" : "normal");
                    if (ControlCentre.QueryNotifications().QueryApplied().RenderFinished) Notifications.Push("Keybindings saved", Body);
                }
            }
        }

        // ①f Notifications page → Save Preferences: overlay rows, toast dwell, alert gates.
        {
            const Frontier::NotificationInspector& N = ControlCentre.QueryNotifications();
            if (N.QueryRevision() != AppliedNotifyRevision)
            {
                const bool First = AppliedNotifyRevision == 0u;
                AppliedNotifyRevision = N.QueryRevision();
                const Frontier::NotificationPreferences& P = N.QueryApplied();
                Notifications.AssignHoldSeconds(P.HoldSeconds);
                Frontier::TelemetryRowStructure Rows = Telemetry.QueryRows();
                Rows.ShowMemory = P.ShowMemoryUsage;
                Rows.ShowScene  = P.ShowSceneMetadata;
                Telemetry.AssignRows(Rows);
                if (!First)
                {
                    Configuration.Access().Notifications = P;
                    if (!Configuration.Save()) std::cerr << "[Configuration] save failed: " << Configuration.QueryLastError() << "\n";
                    if (P.RenderFinished) Notifications.Push("Notification preferences saved");
                }
            }
        }

        // ①g Alert gates: "Autosave Errors" (preference writes), "Baking Complete" (accumulation converged),
        //    "Frame-rate Drops" (2 s average under 30 fps, once per episode).
        {
            const Frontier::NotificationPreferences& P = ControlCentre.QueryNotifications().QueryApplied();
            if (P.AutosaveErrors && !Configuration.QueryLastError().empty() && Configuration.QueryLastError() != LastSaveError)
            {
                LastSaveError = Configuration.QueryLastError();
                Notifications.Push("Configuration could not be saved", LastSaveError);
            }
            if (Integrator.QueryAccumulationIndex() < BakeFrameCount) BakeAnnounced = false;
            else if (!BakeAnnounced)
            {
                BakeAnnounced = true;
                if (P.BakingComplete) { char Body[64]; std::snprintf(Body, sizeof(Body), "%u frames accumulated", BakeFrameCount); Notifications.Push("Baking complete", Body); }
            }
            if (Telemetry.ConsumeFrameRateDrop(30.0f) && P.FrameRateDrops)
            {
                char Body[64]; std::snprintf(Body, sizeof(Body), "%.0f fps average over the last 2 s", static_cast<double>(Telemetry.QueryAverageFramesPerSecond()));
                Notifications.Push("Frame-rate drop", Body);
            }
        }

        // ② Advance camera kinematics (frozen while the overlay owns the pointer)
        if (!ControlCentre.CoversPointer())
            Camera.AdvanceLocomotion(Input, Δτ);
        Camera.AssignAspectRatio(
            static_cast<float>(Surface.QueryWidth()) /
            static_cast<float>(Surface.QueryHeight()));

        // ③ Build ImGui draw data (calls ImGui::NewFrame → ImGui::Render internally); the Control Centre records
        //    itself onto the foreground list between NewFrame and Render via the overlay hook.
        Panel.Present(Integrator, Camera, Scene,
                      Surface.QueryWidth(), Surface.QueryHeight(),
                      [&]()
                      {
                          if (OverlaySurface.Begin(Frontier::SurfaceLayer::Above,
                                                   static_cast<float>(Surface.QueryWidth()),
                                                   static_cast<float>(Surface.QueryHeight()),
                                                   InterfaceScale))
                          {
                              // Scene overlays hang from the closed notch line; the pulled-down sheet covers the FPS
                              //    readout, while toasts are drawn after the shade so a settings change is acknowledged
                              //    on top of the dashboard that caused it.
                              const float NotchLine = ControlCentre.QueryHandleHeight();
                              if (ControlCentre.QuerySettings().FrameRateOverlay)
                                  Telemetry.ConstructTelemetryLayout(OverlaySurface, NotchLine);
                              Diagnostics.ConstructInspectorLayout(OverlaySurface, NotchLine, static_cast<float>(LogicalWidth),
                                                                   Surface.QueryVisibilityTelemetry(), Surface.QueryClusterCount(), Surface.QueryDrawIndirectCount(),
                                                                   Integrator.QueryConfiguration(), Level.QueryMaterials().QueryMetrics(),
                                                                   Textures.QueryMetrics(), MaxTextureLevels);
                              ControlCentre.ConstructControlLayout(OverlaySurface);
                              Notifications.ConstructNotificationLayout(OverlaySurface, NotchLine);
                          }
                      });

        // ④ Build dispatch configuration from live camera + integrator state (camera motion restarts accumulation)
        //    Render scale: the kernel runs on a sub-rectangle of the storage image and the blit stretches it.
        //    Display → Resolution: Native follows the dashboard render-scale slider; a fixed preset renders at that
        //    height (window aspect preserved), never above the swapchain size, and the scale slider still multiplies it.
        const float    RenderScale  = ControlCentre.QuerySettings().RenderScale;
        const float    FixedFactor  = FixedRenderHeight > 0u ? std::min(1.0f, static_cast<float>(FixedRenderHeight) / static_cast<float>(std::max(1u, Surface.QueryHeight()))) : 1.0f;
        const uint32_t RenderWidth  = std::max(1u, static_cast<uint32_t>(static_cast<float>(Surface.QueryWidth())  * RenderScale * FixedFactor + 0.5f));
        const uint32_t RenderHeight = std::max(1u, static_cast<uint32_t>(static_cast<float>(Surface.QueryHeight()) * RenderScale * FixedFactor + 0.5f));
        Integrator.ObserveCamera(Camera, RenderWidth, RenderHeight);
        if (Telemetry.QueryRows().ShowScene)
        {
            char Line[96];
            std::snprintf(Line, sizeof(Line), "%s  |  %u tris  |  %u luminaire tris  |  %ux%u  |  frame %u  |  %s",
                          Level.QueryName().c_str(), Level.QueryTriangleCount(), LuminaireCount, RenderWidth, RenderHeight, Integrator.QueryAccumulationIndex(),
                          Frontier::RayTracingCapabilitySet::TierName(Surface.QueryRayTracingTier()));
            Frontier::TelemetryRowStructure Rows = Telemetry.QueryRows(); Rows.SceneLine = Line; Telemetry.AssignRows(Rows);
        }
        const Frontier::DispatchConfiguration Dispatch = Integrator.BuildDispatch(
            Camera,
            RenderWidth,
            RenderHeight,
            AlphaMaskedMaterialCount,
            LuminaireCount);

        // ④b R2 front end: same camera, reverse-Z infinite projection; AA jitter is a per-frame Halton(2,3) offset shared
        //    by the raster and the resolve (pixel centre when AA is off).
        {
            Frontier::VisibilityFrameConfiguration Frame{};
            Frame.Camera.Origin             = Camera.QuerySpatialLocation();
            Frame.Camera.Forward            = Camera.QueryForwardVector();
            Frame.Camera.Right              = Camera.QueryRightVector();
            Frame.Camera.Up                 = Camera.QueryUpwardVector();
            Frame.Camera.TanHalfFieldOfView = Dispatch.FieldOfViewTanHalf;
            Frame.Camera.AspectRatio        = Camera.QueryAspectRatio();
            Frame.Camera.NearDistance       = Camera.QueryNearPlaneDistance();
            Frame.RenderWidth               = RenderWidth;
            Frame.RenderHeight              = RenderHeight;
            const auto Halton = [](uint32_t Index, uint32_t Base) { float F = 1.0f, R = 0.0f; for (Index += 1u; Index > 0u; Index /= Base) { F /= static_cast<float>(Base); R += F * static_cast<float>(Index % Base); } return R; };
            const bool Jittered = Integrator.QueryConfiguration().AntiAliasing;
            Frame.JitterX          = Jittered ? Halton(Integrator.QueryAccumulationIndex(), 2u) : 0.5f;
            Frame.JitterY          = Jittered ? Halton(Integrator.QueryAccumulationIndex(), 3u) : 0.5f;
            Frame.FrameIndex       = Integrator.QueryAccumulationIndex();
            Frame.DebugView        = Diagnostics.QueryView();
            Frame.OcclusionCulling = Diagnostics.QueryOcclusion();
            Frame.ConeCulling      = false;   // the kernel shades both faces; cone culling would remove back-facing walls seen from outside
            Surface.AssignVisibilityFrame(Frame);
        }

        // ④b Spatial interface — animate the figures, re-bind on a swapchain rebuild, publish this frame's view.
        if (InterfaceReady)
        {
            // Every image view the interface renders into is destroyed by a swapchain rebuild, so re-Resize whenever
            //    the generation moves. Comparing generations (rather than extents) also catches a rebuild that keeps
            //    the same size, e.g. a present-pacing change.
            const uint32_t Generation = Surface.QueryTargetGeneration();
            if (Generation != InterfaceGeneration)
            {
                if (Interface.Resize(RenderWidth, RenderHeight, Surface.QueryColourView(), Surface.QueryDepthView()))
                {
                    InterfaceGeneration = Generation;
                }
                else
                {
                    InterfaceReady = false;
                    Logger.RecordMessage(Frontier::DiagnosticSeverity::Warning, "Interface",
                                         "Interface Resize failed after a swapchain rebuild - panel disabled.");
                }
            }

            if (InterfaceReady)
            {
                InterfaceElapsed += static_cast<double>(Δτ);
                InterfaceTrial.AdvanceTrial(InterfaceFigures, InterfaceMotion, InterfaceElapsed, true);

                // The panel is world-space: it uses the same view→clip the visibility raster builds, so the figures
                //    sit in the room and reproject exactly like geometry rather than floating in screen space.
                // Same camera the visibility raster uses, rebuilt here because Frame is scoped to the block below.
                Frontier::CameraClipConfiguration PanelCamera;
                PanelCamera.Origin             = Camera.QuerySpatialLocation();
                PanelCamera.Forward            = Camera.QueryForwardVector();
                PanelCamera.Right              = Camera.QueryRightVector();
                PanelCamera.Up                 = Camera.QueryUpwardVector();
                PanelCamera.TanHalfFieldOfView = Dispatch.FieldOfViewTanHalf;
                PanelCamera.AspectRatio        = Camera.QueryAspectRatio();
                PanelCamera.NearDistance       = Camera.QueryNearPlaneDistance();

                const Frontier::Matrix4x4 ViewClip = Frontier::ConstructViewClipProjection(PanelCamera);
                for (int Column = 0; Column < 4; ++Column)
                    for (int Row = 0; Row < 4; ++Row)
                        InterfaceViewOfFrame.ViewClip[Column * 4 + Row] = ViewClip.Columns[Column][Row];

                const Frontier::Vector3 Eye     = Camera.QuerySpatialLocation();
                const Frontier::Vector3 Forward = Camera.QueryForwardVector();

                // Depth ordering needs the eye and forward axis; the full transform travels in the raster constants.
                Frontier::InterfaceViewConfiguration ComposeView;
                ComposeView.EyeX = Eye.x;         ComposeView.EyeY = Eye.y;         ComposeView.EyeZ = Eye.z;
                ComposeView.ForwardX = Forward.x; ComposeView.ForwardY = Forward.y; ComposeView.ForwardZ = Forward.z;
                InterfaceCompose.AssignView(ComposeView);
                InterfaceCompose.Advance(InterfaceFigures, InterfaceElapsed);

                InterfaceViewOfFrame.EyeX = Eye.x;
                InterfaceViewOfFrame.EyeY = Eye.y;
                InterfaceViewOfFrame.EyeZ = Eye.z;
                InterfaceViewOfFrame.RenderWidth  = RenderWidth;
                InterfaceViewOfFrame.RenderHeight = RenderHeight;

                Interface.UploadInstances(InterfaceCompose.QueryInstances(),
                                          InterfaceCompose.QueryInstanceCount(),
                                          Surface.QueryCycleSlot());
            }
        }

        // ④c D3 — advance instance transforms and refresh them in place. No reallocation and no device stall, so
        //     unlike UploadScene this is safe every frame; the VkBuffer handle is unchanged so descriptors stand.
        if (PhysicsReady)
        {
            BodyBridge.AdvancePhysics(BodySolver, AnimatedInstances, Δτ);
            if (!Surface.RefreshInstances(AnimatedInstances.data(), static_cast<uint32_t>(AnimatedInstances.size())))
            {
                PhysicsReady = false;
                Logger.RecordMessage(Frontier::DiagnosticSeverity::Warning, "Physics",
                                     "RefreshInstances refused the row set - physics disabled.");
            }

            // D5 — move the traced geometry too. Without this the bodies are DRAWN in their new places while
            //     their shadows and reflections stay where the structure was built, which reads as the bodies
            //     floating free of their own shadows.
            if (TraceMovingBodies && PhysicsReady)
            {
                BodyBridge.RefreshBodyFacets(TracedFacets, AnimatedInstances);
                if (Traversal.RefitBottomLevel(TracedFacets) && Surface.RefreshTraversal(Traversal, TracedFacets))
                {
                    RefitMillisecondsPeak = std::max(RefitMillisecondsPeak, Traversal.QueryRefitMilliseconds());
                }
                else
                {
                    TraceMovingBodies = false;
                    Logger.RecordMessage(Frontier::DiagnosticSeverity::Warning, "Physics",
                                         "Acceleration-structure refit refused - shadows will not follow the bodies.");
                }
            }
        }
        else if (InstanceMotionReady)
        {
            InstanceMotionElapsed += static_cast<double>(Δτ);
            InstanceMotion.AdvanceMotion(AnimatedInstances, InstanceMotionElapsed);
            if (!Surface.RefreshInstances(AnimatedInstances.data(), static_cast<uint32_t>(AnimatedInstances.size())))
            {
                InstanceMotionReady = false;   // count no longer matches the resident scene — stop rather than tear
                Logger.RecordMessage(Frontier::DiagnosticSeverity::Warning, "Instances",
                                     "RefreshInstances refused the row set - scripted motion disabled.");
            }
        }

        // ⑤ Cull → raster → HiZ → resolve → kernel, blit to swapchain, submit ImGui, present
        Surface.RecordAndPresent(Dispatch);

        Integrator.IncrementAccumulationIndex();

        // Display → Frame Cap: sleep out the remainder of the frame budget (coarse sleep, then spin the last ~1 ms so
        //    the cap holds on Windows' 1 ms timer granularity). Unlimited = 0 → no pacing.
        if (FrameCapSeconds > 0.0f)
        {
            const auto Deadline = NowTime + std::chrono::duration_cast<Clock::duration>(Duration(FrameCapSeconds));
            const auto Coarse   = Deadline - std::chrono::milliseconds(1);
            if (Clock::now() < Coarse) std::this_thread::sleep_until(Coarse);
            while (Clock::now() < Deadline) { }
        }

        // Keep the on-disk telemetry current even if the process is killed mid-run.
        if ((Integrator.QueryAccumulationIndex() & 63u) == 0u) Logger.FlushSink();
    }

    //──────────────────────────────────────────────────────────────────────────
    // Shutdown
    //──────────────────────────────────────────────────────────────────────────
    Surface.Retire();

    Logger.RecordMessage(Frontier::DiagnosticSeverity::Information,
                         "Shutdown", "Render loop exited cleanly.");

    if (RefitMillisecondsPeak > 0.0f)
    {
        char RefitLine[160];
        std::snprintf(RefitLine, sizeof(RefitLine),
                      "Acceleration-structure refit peaked at %.2f ms/frame (%.0f%% of a 16.7 ms budget).",
                      static_cast<double>(RefitMillisecondsPeak),
                      100.0 * static_cast<double>(RefitMillisecondsPeak) / 16.7);
        Logger.RecordMessage(RefitMillisecondsPeak > 8.0f ? Frontier::DiagnosticSeverity::Warning
                                                          : Frontier::DiagnosticSeverity::Information,
                             "Physics", RefitLine);
    }
    Logger.TerminateSink();

    return 0;
}
