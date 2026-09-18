//============================================================================================================================================
//                                                      GAMEEXECUTION.CPP
//============================================================================================================================================
// 🧩 Project-Zero entry point — opens the Vulkan window, makes a glTF level resident, runs the ReSTIR render loop.
//
//    Scene selection (R2): `Project-Zero.exe [--scene <file.gltf|glb|shaderball|materials|showroom|showcase>] [--scale <float>]`
//        showroom — P0 spatial-interface level, exported once from ShowroomStructure then imported like any other
//        materials — the material library level (M10): 42 swatch spheres (plastics → coat → metals → glass →
//                    subsurface → cloth/specials) plus three sign panels, exported once from MaterialSwatchStructure
//        showcase — the default level: a 6x6 grid of OpenPBR spheres (anisotropic metals, IOR glass, subsurface,
//                    coat, cloth/fuzz, thin film, haziness, EON, emission, glints) over a scattered field of boxes,
//                    cylinders and cones, lit by two area luminaires. Exported once from ShowcaseStructure.
//        default  Projects/Project-Zero/Content/Scenes/Showcase.gltf — regenerated from ShowcaseStructure when the
//                 file is missing OR was written by an older kShowcaseRevision
//                 (the Cornell box stays one --scene path away, untouched as the reference).
//        Sponza   Projects/Project-Zero/Content/Scenes/Sponza/Sponza.gltf (fetched by the build script, not committed).

#include "../../../Engine/DeviceExchange/SwapchainExchange.h"
#include "../../../Engine/DisplayPresentation/ReSTIRIntegrator.h"
#include "../../../Engine/DisplayPresentation/ShadingTableCodec.h"
#include "../../../Engine/DisplayPresentation/RenderScheduler.h"
#include "../../../Engine/DisplayPresentation/CelestialTier.h"
#include "CelestialSequence.h"
#include "../../../Engine/Editor/EditorInstance.h"
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
#include "../../../Engine/ContentInterchange/AssetResolution.h"
#include "../../../Engine/GeometricRaster/SceneStructure.h"
#include "../../../Engine/GeometricRaster/TraversalIndex.h"
#include "../../../Engine/GeometricRaster/InstanceAcceleration.h"   // D6/D7 two-level: BLASes + instance top level
#include "FlyThroughSolver.h"
#include "RayTracingSolver.h"
#include "../../../Engine/ContentInterchange/ShaderballPreview.h"
#include "../../../Engine/ContentInterchange/ShaderBallStructure.h"
#include "../../../Engine/ContentInterchange/ShowcaseStructure.h"
#include "../../../Engine/ContentInterchange/MaterialSwatchStructure.h"
#include "ShowroomStructure.h"
#include "EditorFeedSequence.h"
#include "../../../Engine/DeviceExchange/InterfaceExchange.h"
#include "../../../Engine/SpatialInterface/InterfaceSequence.h"
#include "../../../Engine/SpatialInterface/InterfacePointerProjection.h"
#include "../../../Engine/GeometricRaster/ClipProjection.h"
#include "InterfaceTrialSequence.h"
#include "InstanceMotionSequence.h"
#include "PerformanceTelemetrySequence.h"
#include "FrameTelemetryLedger.h"
#include "PhysicsInstanceSequence.h"
#include "InterfaceAudioSequence.h"
#include "../../../Engine/SpatialInterface/InterfaceScreenSequence.h"
#include "../../../Engine/SpatialInterface/InterfaceTextProjection.h"
#include "../../../Engine/SpatialInterface/InterfaceVectorCodec.h"
#include "../../../Engine/SpatialInterface/InterfaceLightProjection.h"

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

    // Showcase is the default level (the Cornell box stays one --scene path away, untouched as the reference).
    std::string ScenePath  = "Projects/Project-Zero/Content/Scenes/Showcase.gltf";
    float       SceneScale = 1.0f;
    bool        AnimateInstances = false;   // D3: --animate drives instance transforms from a scripted path
    bool        SilentAudio      = false;   // --silent: open the null audio driver (no sound card, or CI)
    for (int I = 1; I < argc; ++I)
    {
        if (std::strcmp(argv[I], "--animate") == 0) { AnimateInstances = true; continue; }
        if (std::strcmp(argv[I], "--silent")  == 0) { SilentAudio      = true; continue; }   // null audio driver
        if (I + 1 >= argc) break;
        if (std::strcmp(argv[I], "--scene") == 0) ScenePath  = argv[++I];
        if (std::strcmp(argv[I], "--scale") == 0) SceneScale = static_cast<float>(std::atof(argv[++I]));
    }
    if (ScenePath == "shaderball") ScenePath = "Projects/Project-Zero/Content/Scenes/ShaderBall.gltf";   // R4b material test level
    if (ScenePath == "materials")  ScenePath = "Projects/Project-Zero/Content/Scenes/Materials.gltf";    // M10 material library level
    if (ScenePath == "showroom")   ScenePath = "Projects/Project-Zero/Content/Scenes/Showroom.gltf";     // P0 spatial-interface level
    // The open-air scene.
    if (ScenePath == "outdoor")    ScenePath = "Projects/Project-Zero/Content/Scenes/Outdoor.gltf";
    // The showcase field (default level).
    if (ScenePath == "showcase")   ScenePath = "Projects/Project-Zero/Content/Scenes/Showcase.gltf";
    bool DropScene = false;
    if (ScenePath == "drop") { ScenePath = "Projects/Project-Zero/Content/Scenes/ShowroomDrop.gltf"; DropScene = true; }   // D4 physics level

    // Anchor every repository-relative path to the content root before anything opens a file. Levels are exported
    //    once and then imported forever, so a run whose working directory is not the repository root would otherwise
    //    export a SECOND copy of the level next to the executable and read that one — which is exactly how the
    //    renderer ended up showing a stale Showcase while the repository held a newer one.
    {
        const std::filesystem::path ContentRoot = Frontier::QueryContentRoot();
        if (!ContentRoot.empty() && !std::filesystem::path(ScenePath).is_absolute())
            ScenePath = (ContentRoot / ScenePath).string();
    }

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
            Frontier::SceneEncodeConfiguration CornellNaming{};
            CornellNaming.Name  = "CornellBox";
            CornellNaming.Spans = &Scene.QuerySpans();
            if (Frontier::SceneCodec::Encode(ScenePath, Frontier::ReSTIRIntegrator::BuildTriangleIndex(Scene),
                                             Frontier::ReSTIRIntegrator::BuildMaterialDescriptors(Scene), &Error,
                                             CornellNaming))
                std::cerr << "[Scene] Exported the Cornell box to " << ScenePath << "\n";
            else
                std::cerr << "[Scene] Cornell export failed: " << Error << "\n";
        }
        const bool IsOutdoor = ScenePath.find("Outdoor.gltf") != std::string::npos;
        if (IsOutdoor && !std::filesystem::exists(ScenePath, FsError))
        {
            std::filesystem::create_directories(std::filesystem::path(ScenePath).parent_path(), FsError);
            Frontier::ProjectZero::RayTracingSolver Open;
            Open.ConstructOutdoorScene();
            std::string Error;
            // The scene name rides the encode configuration (the file stem becomes the level name at import,
            //    and the camera branch below keys off that) — without it the outdoor scene would load with the
            //    Cornell camera, indoors-facing.
            Frontier::SceneEncodeConfiguration OutdoorNaming{};
            OutdoorNaming.Name  = "Outdoor";
            OutdoorNaming.Spans = &Open.QuerySpans();
            if (Frontier::SceneCodec::Encode(ScenePath, Frontier::ReSTIRIntegrator::BuildTriangleIndex(Open),
                                             Frontier::ReSTIRIntegrator::BuildMaterialDescriptors(Open), &Error,
                                             OutdoorNaming))
                std::cerr << "[Scene] Exported the outdoor scene to " << ScenePath << "\n";
            else
                std::cerr << "[Scene] Outdoor export failed: " << Error << "\n";
        }

        // The showcase is authored as OpenPBR slabs (ShowcaseStructure), NOT through the analytical solver. The old
        //    path went through ReSTIRIntegrator::BuildMaterialDescriptors, which pins SpecularWeight = 0 to protect the
        //    Cornell reference — so every showcase object arrived Lambertian, and the level carried no emissive
        //    triangle at all ("0 luminaires"), which is what left it with neither shadows nor indirect light.
        //
        //    kShowcaseRevision is stamped into the file name so an existing Showcase.gltf from the previous structure
        //    is not silently reused. Export-once-then-import only works if "once" can be invalidated when the level
        //    itself changes; without this the old file wins forever and the new materials never appear.
        const bool IsShowcase = ScenePath.find("Showcase.gltf") != std::string::npos;
        if (IsShowcase)
        {
            const std::filesystem::path Folder = std::filesystem::path(ScenePath).parent_path();
            ScenePath = (Folder / "Showcase.gltf").string();
            if (!std::filesystem::exists(ScenePath, FsError) || !Frontier::ShowcaseIsCurrent(ScenePath))
            {
                std::filesystem::create_directories(Folder, FsError);
                Frontier::ShowcaseStructure Showcase; Showcase.Construct();
                std::string Error;
                if (Showcase.Export(ScenePath, &Error)) std::cerr << "[Scene] Exported the showcase level to " << ScenePath << "\n";
                else                                    std::cerr << "[Scene] Showcase export failed: " << Error << "\n";
            }
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
        // M10 material library level. Same export-once-then-import discipline: the swatch level is generated headless
        //    (MaterialSwatchStructure) and every later run reads the file like any other level.
        const bool IsMaterials = ScenePath.find("Materials.gltf") != std::string::npos;
        if (IsMaterials && !std::filesystem::exists(ScenePath, FsError))
        {
            std::filesystem::create_directories(std::filesystem::path(ScenePath).parent_path(), FsError);
            std::string Error;
            Frontier::MaterialSwatchStructure Library; Library.Construct();
            if (Library.Export(ScenePath, &Error)) std::cerr << "[Scene] Exported the material library level to " << ScenePath << "\n";
            else                                   std::cerr << "[Scene] Material library export failed: " << Error << "\n";
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
    {
        FRONTIER_TELEMETRY_STARTUP("Bootstrap/ConfigurationLoad");
        if (!Configuration.Load("Projects/Project-Zero/Content/Frontier.config.toml"))
            std::cerr << "[Configuration] " << Configuration.QueryPath() << ": " << Configuration.QueryLastError() << " - using defaults\n";
    }

    Frontier::SceneStructure Level;
    Frontier::TextureIndex   Textures;
    uint32_t MaxTextureLevels = 1u;   // R6 row 3: deepest mip chain resident (F3 scene-census row; computed once below)
    // Celestial moon atlas: bindless slots, filled right after the scene registers its own textures (below) and
    //    handed to the sequence after Decode. Outer scope because registration and assignment straddle the scene
    //    block; kNoMoonSlot until filled.
    uint32_t MoonSlots[Frontier::kMoonAtlasCount];
    for (uint32_t M = 0u; M < Frontier::kMoonAtlasCount; ++M) MoonSlots[M] = 0xFFFFFFFFu;
    {
        FRONTIER_TELEMETRY_CONTENT("Bootstrap/SceneDecode");
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
        // Celestial moons — the six albedos join the shared index BEFORE Textures.Decode, so they ride the same
        //    decode/upload path as the scene and land in the bindless table the kernel samples. Colour, not
        //    data (Linear=false): they upload SRGB and the shader reads linear albedos.
        for (uint32_t M = 0u; M < Frontier::kMoonAtlasCount; ++M)
        {
            char MoonPath[128];
            std::snprintf(MoonPath, sizeof(MoonPath), "%s%s",
                          Frontier::kMoonTextureDirectory, Frontier::kMoonAtlas[M].File);
            MoonSlots[M] = Textures.RegisterPath(MoonPath, /*Linear=*/false);
        }
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
            {
                // JPEG/PNG decode plus the mip chains. Seconds on a cold cache, and worth seeing separately from
                //    the upload it precedes.
                FRONTIER_TELEMETRY_CONTENT("Bootstrap/TextureDecode");
                (void)Textures.Decode(Configuration.Query().Backend.TextureEdgeLimit, &TextureReport);
            }
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

    //──────────────────────────────────────────────────────────────────────────
    // Interface light contribution — the panel as an emitter in the room
    //──────────────────────────────────────────────────────────────────────────
    // Where the panel hangs, resolved here because the proxy must be registered before the acceleration structure
    //    is built — well before the interface itself is brought up further down.
    const bool ShowroomLevelForLight = Level.QueryName() == "Showroom" || Level.QueryName() == "ShowroomDrop";
    Frontier::PlanePlacement PanelPlacementForLight;
    if (ShowroomLevelForLight)
    {
        const Frontier::Vector3 LightAnchor = Frontier::ProjectZero::ShowroomStructure::QueryPanelOrigin();
        PanelPlacementForLight.Origin    = Frontier::PlaneOrigin{ LightAnchor.x, LightAnchor.y, LightAnchor.z };
        PanelPlacementForLight.RotationX = 1.57079633f + Frontier::ProjectZero::ShowroomStructure::QueryPanelTilt();
        PanelPlacementForLight.Scale     = 2.2f;
    }

    // Registered BEFORE the acceleration structure is built, and the scene re-finalised, because Finalise is what
    //    flattens triangles and builds the luminaire table. A proxy added after it would be geometry the light
    //    sampler never sees: drawn, but lighting nothing.
    //
    //    The radiance is measured once here from the panel's rest composition rather than per frame. A per-frame
    //    update would mean rebuilding the acceleration structure every time a lamp changed brightness, which is
    //    the 28 ms rebuild D6 measured — far too expensive for a second-order lighting effect. The panel's average
    //    colour barely moves during the trial loop, so a static proxy is the honest trade.
    Frontier::InterfaceFidelityTier PanelTier = Frontier::InterfaceFidelityTier::Low;
    {
        FRONTIER_TELEMETRY_STARTUP("Bootstrap/PanelLightProxy");
    if (ShowroomLevelForLight)
    {
        Frontier::InterfaceStructure RestFigures;
        Frontier::MotionIntegrator   RestMotion;
        Frontier::ProjectZero::InterfaceTrialSequence RestTrial;
        RestTrial.AssignPanelPlacement(PanelPlacementForLight);
        RestTrial.Construct(RestFigures, RestMotion);
        RestTrial.AdvanceTrial(RestFigures, RestMotion, 1.5, true);   // mid-loop: buttons lit, bar part filled

        Frontier::InterfaceSequence RestComposition;
        Frontier::InterfaceViewConfiguration RestView;
        RestView.EyeY = -1.70f; RestView.EyeZ = 1.45f; RestView.ForwardY = 1.0f;
        RestComposition.AssignView(RestView);
        RestComposition.Advance(RestFigures, 1.5);

        // Panel face in world space. Scale 2.2 and the trial's authored half extents give the half-axes; the
        //    showroom tilt leans the face back, so the up axis is not simply world +Z.
        const float HalfWidth  = 0.115f * 2.2f;   // [m]
        const float HalfHeight = 0.072f * 2.2f;   // [m]
        const float Tilt = Frontier::ProjectZero::ShowroomStructure::QueryPanelTilt();
        const Frontier::Vector3 Anchor = Frontier::ProjectZero::ShowroomStructure::QueryPanelOrigin();

        Frontier::PanelProxyRequest Proxy;
        Proxy.Tier    = PanelTier;
        Proxy.CentreX = Anchor.x; Proxy.CentreY = Anchor.y; Proxy.CentreZ = Anchor.z;
        Proxy.RightX  = HalfWidth; Proxy.RightY = 0.0f; Proxy.RightZ = 0.0f;
        // Local +Y after the stand-up rotation and tilt: mostly world +Z, leaning toward −Y.
        Proxy.UpX = 0.0f;
        Proxy.UpY = -HalfHeight * std::sin(Tilt);
        Proxy.UpZ =  HalfHeight * std::cos(Tilt);
        // A display that reads correctly as an overlay is far too dim as an emitter measured against a 32 nit
        //    ceiling panel; this brings it into the same range as the room's own luminaires.
        Proxy.Gain = 26.0f;

        const Frontier::PanelRadiance Radiance =
            Frontier::InterfaceLightProjection::MeasureRadiance(RestFigures, RestComposition,
                                                                4.0f * HalfWidth * HalfHeight);
        const uint32_t ProxyInstance =
            Frontier::InterfaceLightProjection::ComposeProxy(Level, Proxy, Radiance);

        if (ProxyInstance != 0xFFFFFFFFu)
        {
            Level.Finalise(64u, nullptr);   // rebuilds the flat triangles and the luminaire table with the proxy in
            char Line[224];
            std::snprintf(Line, sizeof(Line),
                          "Panel light %s: rgb (%.3f %.3f %.3f) from %u figures, %.0f%% coverage, %zu luminaires now.",
                          Frontier::InterfaceFidelityTierName(PanelTier),
                          Radiance.Red, Radiance.Green, Radiance.Blue, Radiance.Contributors,
                          Radiance.Coverage() * 100.0, Level.QueryLuminaires().size());
            Logger.RecordMessage(Frontier::DiagnosticSeverity::Information, "Interface", Line);
        }
        else
        {
            Logger.RecordMessage(Frontier::DiagnosticSeverity::Information, "Interface",
                                 std::string("Panel light ") + Frontier::InterfaceFidelityTierName(PanelTier) +
                                 ": no proxy registered (tier off, unavailable, or the panel emits nothing).");
        }
    }
    }

    // R3: Tier A acceleration structure — tinybvh binned SAH → CWBVH over the flat world-space triangles.
    // D1: built through the bottom-level entry point. The whole level is currently ONE identity-transformed
    //     instance, so object space is world space and this is bit-for-bit what Build() produced before
    //     (Scratchpad/CheckTraversalIdentity.sh is the gate). Per-instance transforms arrive in D2/D3.
    Frontier::TraversalIndex Traversal;
    {
        FRONTIER_TELEMETRY_SHADER("Bootstrap/TraversalBuild");
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

    Frontier::ProjectZero::FlyThroughSolver Camera(CameraConfig);
    {
        FRONTIER_TELEMETRY_STARTUP("Bootstrap/CameraSetup");
        // Z-up: stand 1.95 m in front of the open face (Y < 0), eye height 1 m, looking along +Y into the box.
        // Pulled back and raised for the larger room (X ±2, Y 0-4, Z 0-3) so the whole box and the roof aperture are
        //    in frame from the default position.
        Camera.AssignSpatialLocation(Frontier::Vector3{ 0.0f, -3.30f, 1.55f });
        Camera.AssignOrientationEuler(0.0f, 0.0f, 0.0f);
        if (Level.QueryName() == "ShaderBall")
        {
            // Shader ball: 5 m back from the front row, 2.6 m up, pitched down ~22° so all four rows fit at 55° FoV.
            Camera.AssignSpatialLocation(Frontier::Vector3{ 0.0f, -6.2f, 2.6f });
            Camera.AssignOrientationEuler(-22.0f * 3.14159265f / 180.0f, 0.0f, 0.0f);
        }
        else if (Level.QueryName() == "Materials")
        {
            // Material library: 5 m back from the near row at 2.6 m, pitched down ~13° — the whole 7 × 6 grid sits in
            //    frame at 55° FoV with the three sign panels on the backdrop behind it.
            Camera.AssignSpatialLocation(Frontier::Vector3{ 0.0f, -5.0f, 2.6f });
            Camera.AssignOrientationEuler(-13.0f * 3.14159265f / 180.0f, 0.0f, 0.0f);
        }
        else if (Level.QueryName() == "Outdoor")
        {
            // Standing on open ground at eye height, looking north along +Y at the casters, pitched up 8° so the
            //    horizon sits low in frame.
            Camera.AssignSpatialLocation(Frontier::Vector3{ 0.0f, -6.0f, 1.70f });
            Camera.AssignOrientationEuler(8.0f * 3.14159265f / 180.0f, 0.0f, 0.0f);
        }
        else if (Level.QueryName() == "Showcase")
        {
            // Showcase: 9.5 m south of the material grid at 5.6 m, pitched down 21° and looking straight up +Y. The
            //    elevation is what makes the six rows read AS six rows: from eye height they telescope into each other
            //    and the back rows are hidden behind the front ones.
            //
            //    ⚠️ The old framing (0, −14, 2.2) at yaw 220° pointed AWAY from the grid — it was aimed at the sunset
            //    for the lens flare, which made sense when the level was a scattered analytical field with nothing in
            //    particular to look at. Pointed at the new level it would frame empty ground, which reads as "the new
            //    scene did not load". This is the framing the CPU proof renders, so the two match shot for shot.
            Camera.AssignSpatialLocation(Frontier::Vector3{ 0.0f, -9.5f, 5.6f });
            Camera.AssignOrientationEuler(-21.0f * 3.14159265f / 180.0f, 0.0f, 0.0f);
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
    }

    //──────────────────────────────────────────────────────────────────────────
    // ReSTIR integrator — owns dispatch parameters, accumulation index
    //──────────────────────────────────────────────────────────────────────────
    Frontier::ReSTIRIntegratorConfiguration IntegratorConfig
    {
        .CandidatesPerPixel  = 8u,      // [-]  primary DI candidates per pixel
        .ExtraCandidateCount = 2u,      // [-]  extra same-pixel candidates
        .Exposure            = 1.05f,   // [-]  ACES exposure
        .AmbientStrength     = 0.015f   // [-]  ambient strength
    };

    Frontier::ReSTIRIntegrator Integrator(IntegratorConfig);
    {
        FRONTIER_TELEMETRY_STARTUP("Bootstrap/IntegratorSetup");
        // User directive 2026-09-11: no adaptive exposure in the engine build. Frame-median metering keys to the
        //    background on wide framings (small bright subject blows out) and the 0.4/2.2 s adaptation lags flash
        //    white/black on every turn. Manual holds the slider value above, so the frame is a pure function of the
        //    scene and the camera. The struct default stays Adaptive: the proofs seat their own configurations and
        //    must be untouched by this.
        Frontier::ExposureConfiguration ExposureSeed = Integrator.Exposure().QueryConfiguration();
        ExposureSeed.Mode = Frontier::ExposureModeCategory::Manual;
        ExposureSeed.ManualExposure = IntegratorConfig.Exposure;
        Integrator.Exposure().AssignConfiguration(ExposureSeed);
    }

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

    // Frontier.config.toml is read before the device comes up: [render] ray_tracing_tier decides which traversal backend
    //    the swapchain resolves (missing file = defaults = Auto).

    Frontier::SwapchainExchange Surface(SurfaceConfig);
    Surface.AssignRayTracingRequest(static_cast<Frontier::RayTracingRequestCategory>(Configuration.Query().Backend.RayTracingTier));

    bool SurfaceUp = false;
    {
        // Device, window, swapchain, every pipeline and every shader module: the single largest startup cost and,
        //    until now, an unmeasured one. The ledger records it as a phase so "why does it take so long to open"
        //    has an answer that is a number.
        FRONTIER_TELEMETRY_STARTUP("Bootstrap/SwapchainBring");
        SurfaceUp = Surface.Bring();
    }
    if (!SurfaceUp)
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
        FRONTIER_TELEMETRY_STARTUP("Bootstrap/ShadingTableBake");
        const Frontier::ShadingTableSet Tables = Frontier::ShadingTableCodec::Bake();   // R4b: GGX energy + LTC sheen LUTs
        Surface.UploadShadingTables(Tables.Energy.data(), Tables.Sheen.data(), Frontier::ShadingTableSet::kResolution);
    }
    {
        // Vertex/index/cluster/material/texture upload plus the acceleration structures — the other half of the
        //    startup bill, and the half that scales with the level.
        FRONTIER_TELEMETRY_CONTENT("Bootstrap/UploadScene");
        Surface.UploadScene(Level, Traversal, &Textures);
    }

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
    // The editor feed doubles as the scene census: the physics bridge and the scripted driver ask it where
    //    the dynamic bodies live, so no ordinal arithmetic here can drift from the file.
    Frontier::ProjectZero::EditorFeedSequence Feed;
    Frontier::RigidBodySolver                       BodySolver;
    Frontier::ProjectZero::PhysicsInstanceSequence  BodyBridge;
    bool PhysicsReady = false;

    {
        FRONTIER_TELEMETRY_STARTUP("Bootstrap/PhysicsInit");
        if (DropScene && !AnimatedInstances.empty())
    {
        Frontier::RigidBodyConfiguration SolverConfiguration;
        SolverConfiguration.FixedStepSeconds = 1.0f / 60.0f;
        uint32_t FirstBody = 0u, BodyCount = 0u;
        const bool BodiesFound = Feed.QueryAnimatedSpan(&FirstBody, &BodyCount, Level)
            && BodyCount == kDropBodyCount;
        if (BodiesFound && BodySolver.Bring(SolverConfiguration))
        {
            Frontier::ProjectZero::PhysicsInstanceConfiguration BridgeConfiguration;
            // Drop bodies are the level's only dynamic placements, so the animated span IS the body run.
            BridgeConfiguration.DropCount         = kDropBodyCount;
            BridgeConfiguration.FirstDropInstance = FirstBody;
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
                                   " rigid bodies from instance " + std::to_string(FirstBody) + "."
                                 : "Drop scene requested but the solver refused - the level renders statically.");

        if (PhysicsReady)
            Logger.RecordMessage(TraceMovingBodies ? Frontier::DiagnosticSeverity::Information
                                                   : Frontier::DiagnosticSeverity::Warning,
                                 "Physics",
                                 TraceMovingBodies
                                     ? "Traced geometry follows the bodies (acceleration structure refitted per frame)."
                                     : "Acceleration structure is not refittable - bodies will move but their shadows will not.");
        }
    }

    {
        FRONTIER_TELEMETRY_STARTUP("Bootstrap/ScriptedMotionInit");
        if (AnimateInstances && !PhysicsReady && !AnimatedInstances.empty())
    {
        // Drive the dynamic span the builders flagged, so the static scenery proves, in the same frame,
        //    that untouched rows really are untouched. A level with no flagged objects idles.
        Frontier::ProjectZero::InstanceMotionConfiguration MotionConfiguration;
        uint32_t FirstAnimated = 0u, AnimatedCount = 0u;
        if (Feed.QueryAnimatedSpan(&FirstAnimated, &AnimatedCount, Level))
        {
            MotionConfiguration.FirstInstance = FirstAnimated;
            MotionConfiguration.InstanceCount = AnimatedCount;
        }
        else
        {
            MotionConfiguration.FirstInstance = 0u;
            MotionConfiguration.InstanceCount = 0u;
        }
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
    }

    //──────────────────────────────────────────────────────────────────────────
    // D6/D7 — the two-level acceleration structure (object-space BLASes + an instance top level)
    //──────────────────────────────────────────────────────────────────────────
    // Built beside the world-space CWBVH, never instead of it. Each instance of the level becomes its own BLAS over
    //    that instance's REST-pose triangles (the flat soup exactly as the scene builder baked it — nothing is
    //    rewritten per frame, which is the difference to D5), and the top level is a thin tree over the instances'
    //    world AABBs. The frame then writes one 112 B row per moved instance and rebuilds that top level; the measured
    //    cost of moving EVERY instance is in Exhibits/Workbench/Traversal (CheckTwoLevelBvh.sh): 0.07 ms at 256
    //    instances, 0.32 ms at 1 024, 1.42 ms at 4 096 instances on the two-core proof host.
    //
    //    Uploaded only when the build succeeds, and the dispatcher's TlasInstanceCount is what makes the kernel walk it:
    //    a refusal (or a level with a single instance) leaves the single-blob path exactly as it was before D6.
    Frontier::InstanceAcceleration InstanceStructure;
    std::vector<Frontier::InstanceRow> InstanceRows;
    struct RestTransform { float World[16]; };
    std::vector<RestTransform> RestWorlds;
    bool InstancesResident = false;
    {
        FRONTIER_TELEMETRY_SHADER("Bootstrap/TwoLevelBuild");
        if (AnimatedInstances.size() > 1u)
    {
        std::vector<Frontier::MeshPrototype> Prototypes;
        Prototypes.reserve(AnimatedInstances.size());
        InstanceRows.reserve(AnimatedInstances.size());
        bool RowsValid = true;
        for (uint32_t I = 0u; I < AnimatedInstances.size(); ++I)
        {
            const Frontier::InstanceRecord& Row = AnimatedInstances[I];
            if (Row.FlatTriangleOffset + Row.TriangleCount > TracedFacets.size()) { RowsValid = false; break; }
            // One BLAS per instance. Two instances sharing a mesh could share a BLAS too, but the level's instances are
            //    distinct placements with their own material and triangle range, so the shared-mesh case is a later
            //    optimisation rather than something the structure must assume. The prototypes read the REST soup.
            Prototypes.push_back(Frontier::MeshPrototype{ TracedFacets.data() + Row.FlatTriangleOffset, Row.TriangleCount });
            RestTransform Rest{};
            std::memcpy(Rest.World, Row.World, sizeof(Rest.World));
            RestWorlds.push_back(Rest);

            // Build time IS the rest pose: World_now == World_rest, so every row starts as the exact identity and the
            //    kernel's first frame is the single-blob path's numbers, not merely close to them.
            Frontier::InstanceRow Instance{};
            for (uint32_t E = 0u; E < 16u; ++E) Instance.Transform[E] = 0.0f;
            Instance.Transform[0] = Instance.Transform[5] = Instance.Transform[10] = Instance.Transform[15] = 1.0f;
            Instance.BlasIndex     = I;
            Instance.FirstTriangle = Row.FlatTriangleOffset;
            Instance.Flags         = Row.Flags;
            InstanceRows.push_back(Instance);
        }
        if (RowsValid && InstanceStructure.Build(Prototypes, InstanceRows, false))
        {
            Surface.UploadInstanceTraversal(InstanceStructure);
            Integrator.AssignInstanceCount(static_cast<uint32_t>(InstanceRows.size()));
            InstancesResident = true;
            const Frontier::InstanceAccelerationMetrics& M = InstanceStructure.QueryMetrics();
            char Line[256];
            std::snprintf(Line, sizeof(Line), "Two-level: %u instances -> %u BLASes over %u triangles, top level %u nodes, "
                                              "shared blobs %.1f KB + %.1f KB, built in %.1f ms",
                          M.InstanceCount, M.BlasCount, M.PrimitiveCount, M.TlasNodeCount,
                          double(M.NodeBytes) / 1024.0, double(M.LeafBytes) / 1024.0, double(M.BuildMilliseconds));
            Logger.RecordMessage(Frontier::DiagnosticSeverity::Information, "Traversal", Line);
        }
        else
        {
            Logger.RecordMessage(Frontier::DiagnosticSeverity::Warning, "Traversal",
                                 "Two-level build refused - the kernel keeps the single world-space structure.");
        }
        }
    }

    //──────────────────────────────────────────────────────────────────────────
    // ImGui panel — apply theme once after context exists
    //──────────────────────────────────────────────────────────────────────────
    Frontier::RenderScheduler Panel;
    // The sky, the weather and everything that carries them. Prepared once; ticked with the frame.
    Frontier::ProjectZero::CelestialSequence Celestial;
    {
        FRONTIER_TELEMETRY_STARTUP("Bootstrap/CelestialPrepare");
        Celestial.Prepare();
    // Cloud-shadow weather by level, once at load: the 60 m showcase diorama stages the FIN3 diorama deck
    //    (visible broken shadow on its 46 m of ground), every other level the panel kilometre deck. Both
    //    instants are single-sourced in CloudShadowStaging.h; time stays frozen for accumulation parity.
    {
        const bool IsShowcaseLevel = ScenePath.find("Showcase.gltf") != std::string::npos;
        Celestial.AssignCloudShadowStaging(IsShowcaseLevel ? Frontier::kCloudShadowShowcaseDiorama
                                                           : Frontier::kCloudShadowPanelKm);
    }
    // The moon atlas arrives after Decode: slots were registered with the scene (above), and the descriptors
    //    now carry pixels the CPU raster can borrow. A missing file degrades to the index's 1x1 placeholder —
    //    a pale disc, logged at decode — never a refusal to start.
    Celestial.AssignMoonAtlas(MoonSlots, Textures);
    // Moon atlas census: which bindless slots the kernel's MoonAlong will sample, against what is resident.
    //    A slot past the resident count samples an unbound descriptor — white on most drivers — so this line
    //    next to the "Textures: N resident" line is the whole diagnosis for a textureless moon.
    {
        uint32_t Lo = 0xFFFFFFFFu, Hi = 0u;
        for (uint32_t M = 0u; M < Frontier::kMoonAtlasCount; ++M)
        {
            if (MoonSlots[M] < Lo) Lo = MoonSlots[M];
            if (MoonSlots[M] > Hi) Hi = MoonSlots[M];
        }
        char MoonLine[128];
        std::snprintf(MoonLine, sizeof(MoonLine), "%u textures resident, moon slots %u..%u%s.",
                      Textures.QueryCount(), Lo, Hi,
                      Hi < Textures.QueryCount() ? "" : " PAST THE TABLE (moons sample unbound)");
        Logger.RecordMessage(Hi < Textures.QueryCount() ? Frontier::DiagnosticSeverity::Information
                                                        : Frontier::DiagnosticSeverity::Warning,
                             "Moons", MoonLine);
    }
    // The star tables upload once, now the catalogue is loaded: cells then binned stars into binding 23.
    //    Skipped — never called — when the catalogue is empty, so the bring-up zeros stand and the packer's
    //    zero brightness keeps the kernel's star loop off. Static for the run: the sky's rotation is time, not
    //    data, and it rides the per-frame record instead.
    {
        const Frontier::StarCatalogueIndex& Stars = Celestial.Stars();
        if (!Stars.Empty())
        {
            Surface.UploadStarTables(Stars.QueryCells().data(), static_cast<uint32_t>(Stars.QueryCells().size()),
                                     Stars.QueryStars().data(), static_cast<uint32_t>(Stars.QueryStars().size()));
            char StarLine[96];
            std::snprintf(StarLine, sizeof(StarLine), "%u stars in %u cells uploaded to binding 23.",
                          static_cast<uint32_t>(Stars.QueryStars().size()),
                          static_cast<uint32_t>(Stars.QueryCells().size()));
            Logger.RecordMessage(Frontier::DiagnosticSeverity::Information, "Stars", StarLine);
        }
        else
        {
            Logger.RecordMessage(Frontier::DiagnosticSeverity::Warning, "Stars",
                                 "Catalogue empty or missing — the night sky renders starless.");
        }
        }
    }
    uint32_t CelestialFirstRow = Frontier::kNoEditorInstance;

    Panel.ApplyTheme();

    //──────────────────────────────────────────────────────────────────────────
    // Control Centre — top notch + pull-down shade (engine overlay, drawn above every ImGui window)
    //──────────────────────────────────────────────────────────────────────────
    // Typefaces: every static face under EngineContent/FontArchives, loaded once into the dynamic atlas (Vulkan backend
    //    rasterises glyphs on demand). The Fonts tab reads the registry; PixelSpace text honours the applied face.
    Frontier::TypefaceRegistry Typefaces;
    {
        FRONTIER_TELEMETRY_STARTUP("Bootstrap/TypefaceLoad");
        (void)Typefaces.Load("EngineContent/FontArchives");
    }
    Frontier::TypefaceRegistry::Install(&Typefaces);

    Frontier::ControlCentreHost ControlCentre;
    {
        FRONTIER_TELEMETRY_STARTUP("Bootstrap/ControlCentreInit");
        ControlCentre.AssignProjectName("Project-Zero");
    (void)ControlCentre.Initialize(Surface.QueryWidth(), Surface.QueryHeight());
    }

    // The hosts are seeded from the configuration loaded before bring-up; every Apply / debounced dashboard change
    //    writes the file back.
    ControlCentre.SeedSettings(Configuration.Query().Render);
    ControlCentre.AccessAppearance().Seed(Configuration.Query().Appearance);
    ControlCentre.AccessInput().Seed(Configuration.Query().Input);
    ControlCentre.AccessNotifications().Seed(Configuration.Query().Notifications);
    ControlCentre.AccessMaterials().SeedSelection(Configuration.Query().Material.Selected.c_str());
    ControlCentre.AccessMaterials().SeedPreview(Configuration.Query().Material.Preview);
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
    uint32_t AppliedMaterialsRevision  = 0u;
    uint32_t AppliedMaterialsCommit    = 0u;   // M7b: commit generation (Apply ran Finalise inside)
    uint32_t AppliedMaterialsPreview   = 0u;   // M7b: preview-toggle generation ([material] preview)
    Frontier::SkyConstantRecord  LastSky{};    // last sky bytes pushed (④d); a change restarts the accumulation
    Frontier::MoonConstantRecord LastMoons{};  // last moon bytes pushed (④e); a change restarts the accumulation
    Frontier::PostConstantRecord LastPost{};   // last post bytes pushed (④f); a change restarts the accumulation
    bool     BakeAnnounced             = false;  // "Baking Complete" = temporal accumulation reached BakeFrameCount
    constexpr uint32_t BakeFrameCount  = 256u;
    std::string LastSaveError;                   // de-duplicates the "Autosave Errors" toast

    // The tier-chosen shadow settings (technique, kernel width, map side), held so the per-frame sun refresh can
    //    re-send them with the current solar state rather than rebuilding the tier decision every frame.
    Frontier::ShadowFrameConfiguration ShadowTierFrame{};
    bool                               ShadowTierFrameValid = false;
    // The frame the sun (④d) and the moon (④e) jointly fill each tick, handed to the device once at the end of ④e.
    Frontier::ShadowFrameConfiguration ShadowFrameStaged{};

    // Push the Control Centre settings into the renderer. Called whenever the settings revision changes.
    auto ApplyControlCentreSettings = [&](const Frontier::ControlCentreSettings& S, bool Announce)
    {
        Fidelity.AssignCategory(S.Quality);
        const Frontier::FidelityCriteria Criteria = Fidelity.QueryActiveCriteria();

        // The quality tier sets the ReSTIR budget; the GI / AA tiles override the tier's own defaults.
        Integrator.AssignCandidatesPerPixel(Criteria.ReSTIRCandidateSampleCount);
        Integrator.AssignExtraCandidateCount(Criteria.ReSTIRExtraCandidateCount);
        Integrator.AssignSpatialTapCount(Criteria.ReSTIRSpatialTapCount);
        Integrator.AssignDenoiseLevelCount(Criteria.DenoiseLevelCount);
        Integrator.AssignGlobalIllumination(S.GlobalIllumination);
        Integrator.AssignAntiAliasing(S.AntiAliasing);
        Notifications.AssignEnabled(S.Notifications);

        // R10 — the GI-off shadow stage. The tier picks the technique, the kernel width and a default map side;
        //    the Control Centre's "Shadow resolution" dropdown then OVERRIDES that side and stands regardless of
        //    which tier is selected (WithShadowResolution leaves the tier's value alone only for Auto). That
        //    separation is deliberate: resolution is the setting a user is most likely to want to pin against the
        //    tier's judgement, on a machine whose memory or bandwidth the tier cannot know about.
        {
            const Frontier::FidelityCriteria ShadowCriteria = Frontier::WithShadowResolution(Criteria, S.ShadowResolution);
            Frontier::ShadowFrameConfiguration Shadow{};
            Shadow.MapSide    = ShadowCriteria.ShadowMapSide;
            Shadow.FilterTaps = ShadowCriteria.ShadowFilterTapCount;
            switch (ShadowCriteria.ShadowTechnique)
            {
                case Frontier::ShadowTechniqueCategory::HardShadowMap:
                    Shadow.Filter = Frontier::ShadowFilterCategory::Hard; break;
                case Frontier::ShadowTechniqueCategory::WidePercentageCloserFilter:
                    Shadow.Filter = Frontier::ShadowFilterCategory::Pcf;  break;
                case Frontier::ShadowTechniqueCategory::PercentageCloserSoftShadow:
                default:
                    Shadow.Filter = Frontier::ShadowFilterCategory::Pcss; break;
            }
            // The sun's own tap is NOT set here: this lambda runs only when the Control Centre's settings revision
            //    changes, and the sun moves every frame. It is refreshed next to the sky push instead (④d below),
            //    which is the one place that already knows the current solar state.
            ShadowTierFrame      = Shadow;
            ShadowTierFrameValid = true;
            Surface.AssignShadowFrame(Shadow);
        }

        // The celestial budget comes from the SAME tier, through CelestialTier — the one translation from a
        //    quality tier to celestial settings (CheckCelestialTiers forbids reading those fields by hand).
        //
        // The sample counts in it reach the GPU every frame: PackSkyRecord folds Budget.AtmosphereSamples and
        //    Budget.AtmosphereLightSamples into the record RefreshSky pushes to binding 21, so the kernel's sky
        //    integral spends what the tier granted. (A dedicated raster sky pass, SkyView.slang, still does not
        //    exist — the CPU raster reads the same budget through ApplyTo — but the kernel path is live, and both
        //    consumers read this same budget rather than a second copy of the ladder.)
        Celestial.Budget = Frontier::CelestialTier::BudgetFor(Criteria);

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

    Frontier::ProjectZero::InterfaceAudioSequence InterfaceAudio;
    bool InterfaceAudioReady = false;

    // P3/P4: the director and the second screen it switches to. Screen 0 is the live trial panel; screen 1 is a
    //    static card built from P1 text and a P4-converted icon, which exists so the director has two real
    //    screens to move between rather than being wired up against a single one and never exercised.
    Frontier::InterfaceScreenSequence InterfaceDirector;
    bool     InterfaceDirectorReady = false;
    uint32_t InterfaceScreenShown   = 0u;
    bool     ScreenKeyHeldLastFrame = false;
    constexpr uint32_t kTrialScreen = 0u;
    constexpr uint32_t kAboutScreen = 1u;

    // P2: previous-frame mouse state, so a press is detected as an edge rather than a level.
    bool PointerHeldLastFrame = false;

    {
        FRONTIER_TELEMETRY_STARTUP("Bootstrap/InterfaceBring");
        if (Interface.Bring(Surface.QueryDevice(), Surface.QueryPhysicalDevice(),
                        Surface.QueryCycleSlotCount(), Surface.QueryColourFormat(), Surface.QueryDepthFormat()))
    {
        // Place the panel in the ROOM rather than at the world origin. ShowroomStructure publishes the anchor it
        //    reserved for exactly this — above the plinth, tilted toward the eye — so the level owns where the
        //    interface hangs and the trial sequence owns what is on it. Any other level keeps the default upright
        //    placement, which is why this is conditional rather than unconditional.
        const bool ShowroomLevel = Level.QueryName() == "Showroom" || Level.QueryName() == "ShowroomDrop";

        // Shared by the trial panel and every other screen, so they all hang in the same place. Declared out here
        //    rather than inside the branch because the director's second screen needs the same placement.
        Frontier::PlanePlacement PanelPlacementForScreens;
        PanelPlacementForScreens.RotationX = 1.57079633f;

        if (ShowroomLevel)
        {
            const Frontier::Vector3 Anchor = Frontier::ProjectZero::ShowroomStructure::QueryPanelOrigin();
            Frontier::PlanePlacement PanelPlacement;
            PanelPlacement.Origin = Frontier::PlaneOrigin{ Anchor.x, Anchor.y, Anchor.z };
            // π/2 stands the panel up (local +Y → world +Z); the showroom's tilt then leans it back toward the eye.
            PanelPlacement.RotationX = 1.57079633f + Frontier::ProjectZero::ShowroomStructure::QueryPanelTilt();
            PanelPlacement.Scale     = 2.2f;   // the trial layout is authored at ~0.14 m across; this reads at 2 m
            InterfaceTrial.AssignPanelPlacement(PanelPlacement);
            PanelPlacementForScreens = PanelPlacement;
        }

        InterfaceTrial.Construct(InterfaceFigures, InterfaceMotion);

        //──────────────────────────────────────────────────────────────────────
        // P3 + P4 — a second screen, and the director that moves between them
        //──────────────────────────────────────────────────────────────────────
        // Built from the phases that came before rather than from anything new: the card is a Surface figure, its
        //     caption is P1 stroke text, and the tick is a P4-converted lucide path. All of it lands in the same
        //     batch as the trial panel, so two screens still cost one draw.
        {
            Frontier::InterfaceFigure Card;
            Card.Category     = Frontier::InterfaceCategory::Surface;
            Card.HalfWidth    = 0.090f;
            Card.HalfHeight   = 0.055f;
            Card.CornerRadius = 0.008f;
            Card.Palette      = Frontier::PaletteSlot::Housing;
            Card.Placement    = PanelPlacementForScreens;
            const uint32_t AboutRoot = InterfaceFigures.Construct(Card);

            Frontier::TextPlacement Caption;
            Caption.OriginY     =  0.014f;
            Caption.OriginZ     =  0.0020f;
            Caption.CapHeight   =  0.016f;
            Caption.StrokeWidth =  0.0016f;
            Caption.Alignment   = Frontier::TextAlignment::Centre;
            (void)Frontier::InterfaceTextProjection::Compose(InterfaceFigures, AboutRoot, "FRONTIER", Caption);

            Caption.OriginY   = -0.010f;
            Caption.CapHeight =  0.009f;
            Caption.Palette   = Frontier::PaletteSlot::MarkingMute;
            (void)Frontier::InterfaceTextProjection::Compose(InterfaceFigures, AboutRoot, "SHOWROOM P4", Caption);

            Frontier::VectorPlacement Tick;
            Tick.OriginX     =  0.060f;
            Tick.OriginY     = -0.028f;
            Tick.OriginZ     =  0.0020f;
            Tick.Extent      =  0.022f;
            Tick.StrokeWidth =  0.0018f;
            Tick.Palette     = Frontier::PaletteSlot::Confirm;
            const Frontier::VectorConversionMetrics Converted =
                Frontier::InterfaceVectorCodec::Compose(InterfaceFigures, AboutRoot, "M20 6 L9 17 L4 12", Tick);

            InterfaceDirector.Construct(kTrialScreen, { InterfaceTrial.QueryHousingOrdinal() },
                                        Frontier::TransitionConfiguration{ Frontier::TransitionCategory::Fade, 0.30f, 0.0f });
            InterfaceDirector.Construct(kAboutScreen, { AboutRoot },
                                        Frontier::TransitionConfiguration{ Frontier::TransitionCategory::Wipe, 0.35f, 0.0f });
            InterfaceDirector.Present(kTrialScreen);
            InterfaceDirectorReady = true;

            Logger.RecordMessage(Frontier::DiagnosticSeverity::Information, "Interface",
                                 "Director ready: TAB switches screens. The card carries " +
                                 std::to_string(Converted.FigureCount) + " converted vector segments.");
        }

        // Bind the panel to the audio transport: turning the progress bar changes the engine note. Failure is not
        //    fatal — a machine with no sound device still renders the scene, it just does so quietly.
        Frontier::ProjectZero::InterfaceAudioConfiguration AudioConfiguration;
        AudioConfiguration.UseNullDriver = SilentAudio;
        std::string AudioError;
        InterfaceAudioReady = InterfaceAudio.Construct(AudioConfiguration, &AudioError);
        Logger.RecordMessage(InterfaceAudioReady ? Frontier::DiagnosticSeverity::Information
                                                 : Frontier::DiagnosticSeverity::Warning,
                             "Audio",
                             InterfaceAudioReady
                                 ? "Panel bound to audio: drag the progress bar to change the engine note."
                                 : "Audio unavailable, the panel renders silently - " + AudioError);
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

    // Scene editor feed — the roster fills once from the live level; the sheet rebuilds whenever the
//    pick moves. One write-back crosses back every tick: the folder tint mirror; the orbit's home
//    seats from the fly camera below.
    //    Without FRONTIER_DEVELOPMENT the panel below ignores all of this (see the ifdef at the feed block).
    Frontier::EditorInstance   SceneInstances[Frontier::kMaxEditorInstances] = {};
    Frontier::EditorSheet    PickedSheet = {};
    bool                     SceneReady = false;
#ifdef FRONTIER_DEVELOPMENT
    Frontier::EditorReadout  EditorFooter{};
    uint32_t                 EditorViewGeneration = ~0u;
#endif
    uint32_t                 SceneRowCount = 0u;
    uint32_t                 SheetFor     = Frontier::kNoEditorInstance;
    Frontier::EditorProperty* TintMirror  = nullptr;
    uint32_t                 AppliedOrbit = 0u;

    // The performance reporter: owns the 5 s window, the prose lines and the measurement rows. See the block at the
    //    end of the loop, and PerformanceTelemetrySequence.h for why rows exist at all.
    Frontier::ProjectZero::PerformanceTelemetrySequence PerformanceTelemetry{ 5.0f };

    // Everything up to here was one-off. Close the startup phase so the report can separate "what it cost to open"
    //    from "what it costs per frame" — two numbers that a single average would blend into something meaningless.
    FRONTIER_TELEMETRY_FIRST_FRAME();

    while (!Surface.CloseRequested() && !Panel.Convert<bool>())
    {
        FRONTIER_TELEMETRY_ADVANCE_FRAME();
        FRONTIER_TELEMETRY_SCOPE("Frame");
        const auto  NowTime = Clock::now();
        float       Δτ      = std::chrono::duration_cast<Duration>(NowTime - PreviousTime).count();
        PreviousTime        = NowTime;

        // Clamp Δτ to prevent spiral-of-death on window drag or breakpoints
        if (Δτ > 0.1f) Δτ = 0.1f;

        // ① Poll input — GLFW callbacks forward into Input
        {
            FRONTIER_TELEMETRY_SCOPE("Frame/PollInput");
            Surface.PollInput(Input);
        }

        // ①b Control Centre owns the pointer while hovered / grabbed / pulled down; the camera never sees those clicks
        //    Display → UI Scale: the overlay lives in logical pixels (physical ÷ scale); the pointer is mapped the same way.
        const float    InterfaceScale = std::clamp(ControlCentre.QueryAppearance().QueryApplied().InterfaceScale / 100.0f, 0.5f, 2.0f);
        const uint32_t LogicalWidth   = std::max(1u, static_cast<uint32_t>(static_cast<float>(Surface.QueryWidth())  / InterfaceScale + 0.5f));
        const uint32_t LogicalHeight  = std::max(1u, static_cast<uint32_t>(static_cast<float>(Surface.QueryHeight()) / InterfaceScale + 0.5f));
        {
            FRONTIER_TELEMETRY_SCOPE("Frame/ControlCentre");
            ControlCentre.Resize(LogicalWidth, LogicalHeight);
            ControlCentre.AdvanceInteraction(Input, Input.QueryCursorPositionX() / InterfaceScale, Input.QueryCursorPositionY() / InterfaceScale);
            ControlCentre.AdvanceLocomotion(Δτ);
            Notifications.Advance(Δτ);
            Configuration.Advance(Δτ);
            Telemetry.RecordFrame(Δτ);
        }

        // ①a' The sky and the weather. Ticked here, beside the other per-frame advances, so the clock, the wind
        //     phase and the precipitation pool all move exactly once and in a fixed order. The camera position
        //     is what the precipitation emitter follows — it is a world-space cylinder about the viewer, with no
        //     view direction, which is what keeps rain from following where you look.
        {
            FRONTIER_TELEMETRY_SCOPE("Frame/CelestialTick");
            const Frontier::Vector3 Eye = Camera.Convert<Frontier::Vector3>();
            const float CameraWorld[3] = { Eye.x, Eye.y, Eye.z };
            Celestial.Tick(static_cast<float>(Δτ), CameraWorld, 0.0f);
        }

        // ①b' F3 debug popup: view / HiZ / alias-pick toggles persist to [render] and restart the accumulation.
        // R6 row 3: the scheduler's Alias-pick checkbox writes the integrator directly — mirror it into the popup
        //    member before edge-detecting F5 so both toggles converge on one flag.
        {
            FRONTIER_TELEMETRY_SCOPE("Frame/Diagnostics");
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
        }

        // ①c Dashboard settings → renderer (only when something changed)
        {
            FRONTIER_TELEMETRY_SCOPE("Frame/SettingsApply");
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
            FRONTIER_TELEMETRY_SCOPE("Frame/AppearanceApply");
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
            FRONTIER_TELEMETRY_SCOPE("Frame/InputApply");
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
            FRONTIER_TELEMETRY_SCOPE("Frame/NotificationsApply");
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

        // ①h Materials page → commits + [material] (M7b): the inspector snapshots the selection every frame so the
        //    F-panel summary stays fresh without opening the page; a selection change persists the name, a commit
        //    (Apply already ran Finalise inside) restarts the accumulation + toasts, a preview-toggle change persists
        //    the flag, and a preview request renders the shaderball PNG + stamps the header line. Selection alone
        //    never restarts the accumulation.
        {
            FRONTIER_TELEMETRY_SCOPE("Frame/MaterialsApply");
            Frontier::MaterialInspector& M = ControlCentre.AccessMaterials();
            M.Rebuild(&Level.AccessMaterials());
            if (M.QueryRevision() != AppliedMaterialsRevision)
            {
                const bool First = AppliedMaterialsRevision == 0u;
                AppliedMaterialsRevision = M.QueryRevision();
                if (!First)
                {
                    Configuration.Access().Material.Selected = M.QuerySelectedName();
                    if (!Configuration.Save()) std::cerr << "[Configuration] save failed: " << Configuration.QueryLastError() << "\n";
                }
            }
            if (M.QueryCommitRevision() != AppliedMaterialsCommit)
            {
                AppliedMaterialsCommit = M.QueryCommitRevision();
                Integrator.ResetAccumulation();   // committed constants change the shading - restart like any look change
                if (ControlCentre.QueryNotifications().QueryApplied().RenderFinished)
                {
                    char Body[128];
                    std::snprintf(Body, sizeof(Body), "%u material change%s applied - accumulation restarted",
                                  M.QueryLastCommitCount(), M.QueryLastCommitCount() == 1u ? "" : "s");
                    Notifications.Push("Material changes applied", Body);
                }
            }
            if (M.QueryPreviewRevision() != AppliedMaterialsPreview)
            {
                AppliedMaterialsPreview = M.QueryPreviewRevision();
                Configuration.Access().Material.Preview = M.QueryPreviewEnabled();
                if (!Configuration.Save()) std::cerr << "[Configuration] save failed: " << Configuration.QueryLastError() << "\n";
            }
            if (M.TakePreviewRequest())
            {
                Frontier::MaterialIndex& Index = Level.AccessMaterials();
                const uint32_t Id = M.QuerySelectedId();
                if (Id < Index.QueryCount())
                {
                    // Fresh derive (NOT the inspector's retained selection - it predates the commit that requested
                    //    this render, and the selection is exactly what an edit may have flipped).
                    const Frontier::MaterialDescriptor& D = Index.QueryDescriptors()[Id];
                    const uint32_t Limit = std::max(1u, Index.QueryMetrics().SlabLimit);
                    uint32_t Folded = 0u;
                    const std::vector<Frontier::MaterialSlabDescriptor> Flat =
                        Frontier::MaterialIndex::Flatten(D, Limit, &Folded, nullptr);
                    static const Frontier::MaterialSlabDescriptor kPreviewSlab{};
                    const Frontier::MaterialSlabDescriptor& S = Flat.empty() ? kPreviewSlab : Flat.front();
                    const Frontier::MaterialReflectance Sel = Frontier::MaterialIndex::DeriveReflectance(D, S);
                    std::string Safe;
                    for (char C : D.Name)
                        Safe += ((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '-' || C == '_') ? C : '_';
                    if (Safe.empty()) Safe = "material";
                    const std::string Out = "Projects/Project-Zero/Diagnostics/MaterialPreview_" + Safe + ".png";
                    std::error_code PreviewDirs;
                    std::filesystem::create_directories("Projects/Project-Zero/Diagnostics", PreviewDirs);
                    Frontier::ShaderballPreviewRequest Req;
                    Req.Material = &D; Req.Selection = Sel; Req.Size = 160; Req.Spp = 6; Req.OutPath = Out.c_str();
                    const auto T0 = std::chrono::steady_clock::now();
                    Frontier::ShaderballPreviewResult Res;
                    // The shaderball preview is an EDITOR feature: it exists to show the inspector what a material
                    //    looks like after a commit. A shipping build has no inspector, so the whole CPU path-tracer
                    //    behind it — ShaderballExhibit.cpp, which #includes MaterialEvaluation.slang as C++ through
                    //    SlangCpuShim.h — is dead weight there. Undefining FRONTIER_DEVELOPMENT now drops that TU
                    //    from the link entirely (see the build scripts), which is also what silences the C4244/C4305
                    //    double→float warnings that file emits: they come from the shader source being compiled as
                    //    host C++, not from anything the GPU build does.
                    bool Ok = false;
#ifdef FRONTIER_DEVELOPMENT
                    Ok = Frontier::RenderShaderballPreview(Req, Res);
#else
                    (void)Req;
#endif
                    const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - T0).count();
                    M.NotifyPreviewRendered(Ok, D.Name.c_str(), Ok ? Out.c_str() : "write failed", Seconds, M.QueryCommitRevision());
                    if (ControlCentre.QueryNotifications().QueryApplied().RenderFinished)
                    {
                        char Body[192];
                        if (Ok) std::snprintf(Body, sizeof(Body), "%s rendered in %.1fs (mean %.3f, %d tris)", Safe.c_str(), Seconds, Res.Mean, Res.Tris);
                        else std::snprintf(Body, sizeof(Body), "%s failed to render", Safe.c_str());
                        Notifications.Push(Ok ? "Shaderball preview rendered" : "Shaderball preview failed", Body);
                    }
                }
                else
                {
                    M.NotifyPreviewRendered(false, "", "no material selected", 0.0, M.QueryCommitRevision());
                }
            }
        }


        // ①g Alert gates: "Autosave Errors" (preference writes), "Baking Complete" (accumulation converged),
        //    "Frame-rate Drops" (2 s average under 30 fps, once per episode).
        {
            FRONTIER_TELEMETRY_SCOPE("Frame/AlertGates");
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

        // ①c Text-queue drain. No text consumer remains — the development editor reads keystrokes
        //    through ImGui itself — so the queue is drained every tick and Escape still closes the window.
        {
            FRONTIER_TELEMETRY_SCOPE("Frame/TextQueue");
            for (uint32_t I = 0u; I < Input.QueryEditKeyCount(); ++I)
                if (Input.QueryEditKey(I) == 256u) Surface.RequestClose();
            Input.ClearTextQueue();
        }

        // ② Advance camera kinematics (frozen while the Control Centre owns the pointer, or an ImGui
        //    window has captured the pointer or keyboard — a drag that started on a panel must not fly
        //    the camera, and a keystroke typed into one must not fire a shortcut).
        {
            FRONTIER_TELEMETRY_SCOPE("Frame/CameraAdvance");
            if (!ControlCentre.CoversPointer() && !Panel.QueryEditorCapturesPointer()
                && !Panel.QueryEditorCapturesKeyboard())
                Camera.AdvanceLocomotion(Input, Δτ);
            Camera.AssignAspectRatio(
                static_cast<float>(Surface.QueryWidth()) /
                static_cast<float>(Surface.QueryHeight()));
        }

        // ③ Build ImGui draw data (calls ImGui::NewFrame → ImGui::Render internally); the Control Centre records
        //    itself onto the foreground list between NewFrame and Render via the overlay hook.
        // ②c Scene editor feed: the roster fills once, the sheet follows the pick, and the folder
        //    tint mirror carries back onto the row every tick. Development only: without the define the
        //    editor records nothing, so feeding it would be dead work on a shipping build.
#ifdef FRONTIER_DEVELOPMENT
        if (!SceneReady)
        {
            SceneRowCount = Feed.FillRoster(SceneInstances, Level);
            // The celestial entities follow the scene's own rows, under their own folder. Appended rather
            //    than merged so the scene walk stays exactly what it was.
            CelestialFirstRow = SceneRowCount;
            SceneRowCount += Celestial.AppendRoster(SceneInstances, SceneRowCount, Frontier::kMaxEditorInstances);
            Frontier::ViewportOrbit Home;
            float Middle[3] = { 0.0f, 0.0f, 0.0f };
            Frontier::ProjectZero::QueryLevelCentre(Level, Middle);
            const Frontier::Vector3 At = Camera.Convert<Frontier::Vector3>();
            const float Dx = At.x - Middle[0], Dy = At.y - Middle[1], Dz = At.z - Middle[2];
            Home.Yaw      = Camera.QueryYawRadians();
            Home.Pitch    = Camera.QueryPitchRadians();
            Home.Distance = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
            if (Home.Distance < 0.5f)
                Home.Distance = 4.5f;
            Home.Target[0] = Middle[0]; Home.Target[1] = Middle[1]; Home.Target[2] = Middle[2];
            Home.Ortho = false; Home.ViewPoint = 0u; Home.Revision = 0u;
            Panel.SeatViewportOrbit(Home);
            AppliedOrbit = 0u;
            SceneReady   = true;
        }
        const uint32_t PickedNow = Panel.QueryPickedInstance();
        Frontier::ProjectZero::CelestialEntity PickedCelestial{};
        const bool CelestialPicked = CelestialFirstRow != Frontier::kNoEditorInstance
                                  && Celestial.Owns(PickedNow, CelestialFirstRow, PickedCelestial);
        if (PickedNow != SheetFor)
        {
            if (CelestialPicked)
            {
                Celestial.BuildSheet(PickedCelestial, PickedSheet);
                TintMirror = nullptr;   // celestial rows carry no folder tint to mirror back
            }
            else
            {
                TintMirror = Feed.BuildSheet(PickedNow, SceneInstances, SceneRowCount, &PickedSheet,
                                             Camera, Level, AnimatedInstances);
            }
            SheetFor   = PickedNow;
        }
        else if (CelestialPicked)
        {
            // The panel edits the sheet in place, so the write-back happens every tick the row stays picked.
            //    Read-outs are then refreshed from the state the edit just changed.
            Celestial.ApplySheet(PickedCelestial, PickedSheet);
            Celestial.BuildSheet(PickedCelestial, PickedSheet);
        }
        // The outliner's eye toggles live on the rows; carry them back so hiding a row hides the thing.
        if (CelestialFirstRow != Frontier::kNoEditorInstance)
        {
            Celestial.Enabled = SceneInstances[CelestialFirstRow].Visible;
            for (uint32_t E = 0; E < Frontier::ProjectZero::kCelestialEntityCount; ++E)
            {
                const uint32_t Row = CelestialFirstRow + 1u + E;
                if (Row < SceneRowCount) Celestial.Shown[E] = SceneInstances[Row].Visible;
            }
        }
        // ②e The metas move with the clock (sun degrees, air mass, wind rose), so the celestial rows are re-derived
        //    every tick; the footer readout is seated the same way, and the viewport panel is handed the scene
        //    image whenever a swapchain rebuild has re-created it.
        if (CelestialFirstRow != Frontier::kNoEditorInstance)
            Celestial.RefreshRoster(SceneInstances, CelestialFirstRow, SceneRowCount);
        {
            EditorFooter.Fps = Telemetry.QueryAverageFramesPerSecond();
            std::snprintf(EditorFooter.Quality, sizeof(EditorFooter.Quality), "%s", Frontier::InterfaceFidelityTierName(PanelTier));
            std::snprintf(EditorFooter.Pixels, sizeof(EditorFooter.Pixels), "%u\xc3\x97%u", Surface.QueryWidth(), Surface.QueryHeight());
            EditorFooter.SunElevation = Celestial.Frame().Sun.Elevation;
            uint32_t Seated = 0u;
            for (uint32_t S = 0u; S < Frontier::kMoonDrawCount; ++S) if (Celestial.MoonSlots[S].Visible) ++Seated;
            EditorFooter.MoonCount = Seated;
            EditorFooter.MoonCap   = static_cast<uint32_t>(Frontier::kMoonDrawCount);
            const Frontier::Vector3 Eye = Camera.Convert<Frontier::Vector3>();
            EditorFooter.Cam[0] = Eye.x; EditorFooter.Cam[1] = Eye.z; EditorFooter.Cam[2] = Eye.y;
            std::snprintf(EditorFooter.Scene, sizeof(EditorFooter.Scene), "%s", Level.QueryName().c_str());
            EditorFooter.Triangles = Level.QueryTriangleCount();
            Panel.AssignEditorReadout(&EditorFooter);
        }
        if (Surface.QueryTargetGeneration() != EditorViewGeneration)
        {
            EditorViewGeneration = Surface.QueryTargetGeneration();
            Panel.AssignEditorView(Surface.QuerySceneViewTexture(), Surface.QueryWidth(), Surface.QueryHeight());
        }
#else
        (void)SceneReady; (void)SceneRowCount; (void)SheetFor; (void)TintMirror; (void)AppliedOrbit;
#endif

        {
            FRONTIER_TELEMETRY_SCOPE("Frame/PanelPresent");
            Panel.Present(Integrator, Camera, Scene,
                      Surface.QueryWidth(), Surface.QueryHeight(),
                      SceneInstances, SceneRowCount, &PickedSheet,
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
                                                                   Textures.QueryMetrics(), MaxTextureLevels,
                                                                   ControlCentre.QueryMaterials().QuerySummaryLine());
                              ControlCentre.ConstructControlLayout(OverlaySurface);
                              Notifications.ConstructNotificationLayout(OverlaySurface, NotchLine);
                          }

                      });
        }

#ifdef FRONTIER_DEVELOPMENT
        // ②d The tint write-back: a folder tint edited in the sheet lands back on its row.
        if (TintMirror != nullptr && PickedNow < SceneRowCount)
        {
            SceneInstances[PickedNow].Tint[0] = TintMirror->ColourTint[0];
            SceneInstances[PickedNow].Tint[1] = TintMirror->ColourTint[1];
            SceneInstances[PickedNow].Tint[2] = TintMirror->ColourTint[2];
        }
        // ②f The view write-back: a fresh orbit revision poses the fly camera (the eye off the orbit's
        //    figures), so the views menu and the gizmo steer the rendered view.
        const Frontier::ViewportOrbit& Orbit = Panel.QueryViewportOrbit();
        if (Orbit.Revision != AppliedOrbit)
        {
            const float Cy = std::cos(Orbit.Yaw), Sy = std::sin(Orbit.Yaw);
            const float Cp = std::cos(Orbit.Pitch), Sp = std::sin(Orbit.Pitch);
            const float Fx = Sy * Cp, Fy = Cy * Cp, Fz = Sp;
            Camera.AssignSpatialLocation(Frontier::Vector3{ Orbit.Target[0] - Fx * Orbit.Distance,
                                                            Orbit.Target[1] - Fy * Orbit.Distance,
                                                            Orbit.Target[2] - Fz * Orbit.Distance });
            Camera.AssignOrientationEuler(Orbit.Pitch, Orbit.Yaw, 0.0f);
            AppliedOrbit = Orbit.Revision;
        }
#endif

        // ④ Build dispatch configuration from live camera + integrator state (camera motion restarts accumulation)
        //    Render scale: the kernel runs on a sub-rectangle of the storage image and the blit stretches it.
        //    Display → Resolution: Native follows the dashboard render-scale slider; a fixed preset renders at that
        //    height (window aspect preserved), never above the swapchain size, and the scale slider still multiplies it.
        const float    RenderScale  = ControlCentre.QuerySettings().RenderScale;
        const float    FixedFactor  = FixedRenderHeight > 0u ? std::min(1.0f, static_cast<float>(FixedRenderHeight) / static_cast<float>(std::max(1u, Surface.QueryHeight()))) : 1.0f;
        const uint32_t RenderWidth  = std::max(1u, static_cast<uint32_t>(static_cast<float>(Surface.QueryWidth())  * RenderScale * FixedFactor + 0.5f));
        const uint32_t RenderHeight = std::max(1u, static_cast<uint32_t>(static_cast<float>(Surface.QueryHeight()) * RenderScale * FixedFactor + 0.5f));

        Frontier::DispatchConfiguration Dispatch;
        {
            FRONTIER_TELEMETRY_SCOPE("Frame/DispatchBuild");
            // A6b — adaptive exposure. The measurement is one or two frames stale because it is read from the cycle
            //    slot the GPU has already finished with; against time constants of half a second and up that is
            //    invisible, and it is what keeps the read from stalling the CPU on the GPU.
            {
                const float Measured = Surface.QueryAverageLogLuminance();
                if (Measured > -1.0e8f) Integrator.Exposure().ObserveLuminance(Measured);
                Integrator.Exposure().Advance(Δτ);
            }

            Integrator.ObserveCamera(Camera, RenderWidth, RenderHeight);
            if (Telemetry.QueryRows().ShowScene)
            {
                char Line[96];
                std::snprintf(Line, sizeof(Line), "%s  |  %u tris  |  %u luminaire tris  |  %ux%u  |  frame %u  |  %s",
                              Level.QueryName().c_str(), Level.QueryTriangleCount(), LuminaireCount, RenderWidth, RenderHeight, Integrator.QueryAccumulationIndex(),
                              Frontier::RayTracingCapabilitySet::TierName(Surface.QueryRayTracingTier()));
                Frontier::TelemetryRowStructure Rows = Telemetry.QueryRows(); Rows.SceneLine = Line; Telemetry.AssignRows(Rows);
            }

            Dispatch = Integrator.BuildDispatch(
                    Camera,
                    RenderWidth,
                    RenderHeight,
                    AlphaMaskedMaterialCount,
                    LuminaireCount);
        }

        // ④b R2 front end: same camera, reverse-Z infinite projection; AA jitter is a per-frame Halton(2,3) offset shared
        //    by the raster and the resolve (pixel centre when AA is off).
        {
            FRONTIER_TELEMETRY_SCOPE("Frame/VisibilityFrame");
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
        {
            FRONTIER_TELEMETRY_SCOPE("Frame/InterfaceUpdate");
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

                // P2 — pointer interaction. The cursor becomes a world ray, the engine reports which figure it
                //     struck, and the trial sequence decides what that means. Done BEFORE AdvanceTrial so a press
                //     this frame is reflected in the same frame's animation rather than one frame late.
                {
                    const Frontier::Vector3 Eye     = Camera.QuerySpatialLocation();
                    const Frontier::Vector3 Forward = Camera.QueryForwardVector();
                    const Frontier::Vector3 Right   = Camera.QueryRightVector();
                    const Frontier::Vector3 Upward  = Camera.QueryUpwardVector();
                    const float EyeArray[3]     = { Eye.x, Eye.y, Eye.z };
                    const float ForwardArray[3] = { Forward.x, Forward.y, Forward.z };
                    const float RightArray[3]   = { Right.x, Right.y, Right.z };
                    const float UpArray[3]      = { Upward.x, Upward.y, Upward.z };

                    const Frontier::PointerRay Ray = Frontier::InterfacePointerProjection::ConstructViewportRay(
                        Input.QueryCursorPositionX(), Input.QueryCursorPositionY(),
                        Surface.QueryWidth(), Surface.QueryHeight(),
                        EyeArray, ForwardArray, RightArray, UpArray,
                        Dispatch.FieldOfViewTanHalf, Camera.QueryAspectRatio());

                    const Frontier::PointerContact Contact =
                        Frontier::InterfacePointerProjection::Project(InterfaceFigures, InterfaceCompose, Ray);

                    // Edge, not level: only the frame the button goes down counts as a press, so holding does not
                    //     retrigger a toggle sixty times a second.
                    const bool Held    = Input.IsMouseButtonPressed(Frontier::MouseButtonCategory::ButtonLeft);
                    const bool Pressed = Held && !PointerHeldLastFrame;
                    PointerHeldLastFrame = Held;

                    // P3: a screen that is not fully present must not be clickable. The director already clears
                    //     PointerTarget while a screen moves, but discarding the contact here as well means a
                    //     press cannot be queued during a transition and applied the instant it lands.
                    const bool ScreenSettled = !InterfaceDirectorReady ||
                                               InterfaceDirector.QueryInteractiveScreen() == kTrialScreen;
                    InterfaceTrial.ApplyPointer(InterfaceFigures, ScreenSettled ? Contact : Frontier::PointerContact{},
                                                ScreenSettled && Pressed, ScreenSettled && Held);
                }

                // P3 — TAB switches screens, on the key EDGE so holding it does not flip every frame.
                if (InterfaceDirectorReady)
                {
                    const bool Held = Input.IsKeyPressed(Frontier::VirtualKeyCategory::KeyTab);
                    if (Held && !ScreenKeyHeldLastFrame && !InterfaceDirector.IsTransitioning())
                    {
                        InterfaceScreenShown = (InterfaceScreenShown == kTrialScreen) ? kAboutScreen : kTrialScreen;
                        InterfaceDirector.Present(InterfaceScreenShown);
                    }
                    ScreenKeyHeldLastFrame = Held;

                    // Advance BEFORE the composition below, or a screen renders one frame stale.
                    InterfaceDirector.AdvanceScreens(InterfaceFigures, Δτ);
                }

                InterfaceTrial.AdvanceTrial(InterfaceFigures, InterfaceMotion, InterfaceElapsed, true);

                // Publish the panel's values as audio demand and service the device. After AdvanceTrial so the
                //     note follows the value the user just set rather than lagging it by a frame.
                if (InterfaceAudioReady) InterfaceAudio.AdvanceAudio(InterfaceTrial, Δτ);

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
        }

        // ④c D3 — advance instance transforms and refresh them in place. No reallocation and no device stall, so
        {
            FRONTIER_TELEMETRY_SCOPE("Frame/PhysicsAdvance");
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

            // D6/D7 — move the bodies for the TRACER by moving their instances, not their triangles. The rows are the
            //     same ones RefreshInstances just uploaded, so the drawn pose and the traced pose cannot disagree; the
            //     BLASes are untouched (the proof hashes them), and the top level is rebuilt from the rows.
            if (InstancesResident)
            {
                bool RowsComposed = true;
                for (size_t I = 0u; I < InstanceRows.size() && RowsComposed; ++I)
                {
                    // Relative to the bake: World_now · World_rest⁻¹. Unchanged instances take the exact-identity
                    //    branch so that a scene at rest keeps the pre-D6 numbers bit for bit — the composed matrix,
                    //    though mathematically identity, is not bitwise identity and would move grazing hits by an ulp.
                    if (std::memcmp(AnimatedInstances[I].World, RestWorlds[I].World, sizeof(RestWorlds[I].World)) == 0)
                    {
                        for (uint32_t E = 0u; E < 16u; ++E) InstanceRows[I].Transform[E] = 0.0f;
                        InstanceRows[I].Transform[0] = InstanceRows[I].Transform[5] = 1.0f;
                        InstanceRows[I].Transform[10] = InstanceRows[I].Transform[15] = 1.0f;
                    }
                    else
                    {
                        RowsComposed = Frontier::RelativeMatrix(AnimatedInstances[I].World, RestWorlds[I].World,
                                                                InstanceRows[I].Transform);
                    }
                }
                if (RowsComposed && InstanceStructure.UpdateTopLevel(InstanceRows) && Surface.RefreshInstanceTraversal(InstanceStructure))
                {
                    RefitMillisecondsPeak = std::max(RefitMillisecondsPeak, InstanceStructure.QueryMetrics().UpdateMilliseconds);
                }
                else
                {
                    // The structure refused a frame (a grown payload) — fall back to the world-space path rather than
                    //     leaving shadows behind the bodies.
                    InstancesResident = false;
                    Integrator.AssignInstanceCount(0u);
                    Logger.RecordMessage(Frontier::DiagnosticSeverity::Warning, "Traversal",
                                         "Two-level refresh refused - falling back to the world-space structure.");
                }
            }

            // D5 — the world-space fallback: rewrite the bodies' triangles and refit the whole structure. Kept as the
            //     arm a scene without a successful two-level build still uses (and the bit-identity reference for the
            //     two-level path: with D6/D7 live, this must NOT also run — it would rewrite the rest soup the BLASes
            //     were built from).
            if (TraceMovingBodies && PhysicsReady && !InstancesResident)
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

        }
        // ④d GPU sky — the kernel reads the packed record at binding 21 on every miss and every escaped bounce.
        //     Pushed every frame like the instances: 128 bytes, and the sun moves. Refusal is impossible here by
        //     construction (the size is pinned by static_assert and the device is up), so the nodiscard is cast
        //     away — there is nothing to fall back to, and the previous contents stand, which is a stale sky
        //     rather than a torn one.
        {
            FRONTIER_TELEMETRY_SCOPE("Frame/SkyRefresh");
            const Frontier::SkyConstantRecord Sky = Celestial.PackSkyRecord();
            (void)Surface.RefreshSky(&Sky, sizeof(Sky));

            // ⚠️ The sun casts a shadow, and this is where it is told to. The ReSTIR kernel has sampled the sun as a
            //    direct light since it landed (kSunLightIndex / PHatSun), but the GI-off shadow stage only ever
            //    walked the emissive MESH triangles — so with GI off an outdoor level rasterised lamp shadows and no
            //    sun shadow, and a level lit only by the sun placed zero taps and skipped the stage entirely.
            //
            //    Driven from the SAME packed record the kernel reads at binding 21, so the two paths cannot disagree
            //    about where the sun is or how bright it is. SunDirect is already hard zero below the horizon and
            //    when the sun is hidden or disabled, so "is the sun up" needs no second opinion here. Re-sent every
            //    frame because the sun moves; the tier's own fields ride along unchanged.
            //    The frame is STAGED here rather than assigned: ④e below adds the moon to the same block, and the
            //    device must see one frame carrying both bodies, not the sun's frame immediately overwritten by a
            //    moon-only one. ShadowFrameStaged is handed to the device once, at the end of ④e.
            if (ShadowTierFrameValid)
            {
                ShadowFrameStaged = ShadowTierFrame;
                const float Direct = Sky.SunDirect[0] + Sky.SunDirect[1] + Sky.SunDirect[2];
                ShadowFrameStaged.SunEnabled = Direct > 0.0f;
                for (int I = 0; I < 3; ++I) ShadowFrameStaged.SunDirection[I] = Sky.SunDirection[I];
                for (int I = 0; I < 3; ++I) ShadowFrameStaged.SunRadiance[I]  = Sky.SunDirect[I];
            }
            // A slider step on a converged frame is absorbed at 1/n — invisible until the camera restarts the
            //    history, which is why panel edits used to land only when the view moved. Compare the packed
            //    bytes and restart the accumulation the tick the sky changes, so sliders, presets, visibility
            //    toggles and the moving sun all show at once. The packer zero-fills then assigns every field,
            //    so padding is deterministic and the compare is exact; while the sun animates the history
            //    restarts every tick — noisy while moving, exactly like the camera, instead of a smeared trail.
            if (std::memcmp(&Sky, &LastSky, sizeof(Sky)) != 0)
            {
                LastSky = Sky;
                Integrator.ResetAccumulation();
            }
        }

        // ④e GPU moons — the roster reads the packed record at binding 22 on every miss and every escaped
        //     bounce, and the direct fill lights the primary hit. Pushed every frame beside the sky: 288 bytes,
        //     and Luna moves. Same no-fallback shape as the sky — the previous roster stands, which is stale
        //     moons rather than torn ones.
        {
            FRONTIER_TELEMETRY_SCOPE("Frame/MoonRefresh");
            const Frontier::MoonConstantRecord Moons = Celestial.PackMoonRecord();
            (void)Surface.RefreshMoons(&Moons, sizeof(Moons));

            // ⚠️ THE MOON LIGHTS THE SCENE, AND UNTIL NOW IT DID NOT. MoonAmbient() in MoonRecords.slang is summed
            //    by the ReSTIR kernel alone, so with GI OFF a night frame had no lunar contribution whatsoever —
            //    the moon was a lit disc painted on a sky above ground it did not illuminate, and it cast no
            //    shadow on either path. The moon is a directional light like the sun, orders of magnitude down,
            //    and it gets a tap on the same terms.
            //
            //    Sourced from the SAME packed record the kernel reads at binding 22, so the two paths cannot
            //    disagree, and using the SAME brightness × phase product MoonAmbient() uses, so a moonlit frame
            //    has the same intensity whether GI is on or off. Of up to four moons only the brightest gets a
            //    tap: four shadow-casting moons would spend the entire tap budget on the sky and leave none for
            //    the lamps, and the others still contribute through the ambient term.
            if (ShadowTierFrameValid)
            {
                const uint32_t Drawn = Moons.Control[0] < Frontier::kMoonDrawCount ? Moons.Control[0]
                                                                                   : Frontier::kMoonDrawCount;
                int   Brightest = -1;
                float BestLevel = 0.0f;
                for (uint32_t M = 0u; M < Drawn; ++M)
                {
                    // Below the horizon it lights nothing: the same max(direction.z, 0) gate as MoonAmbient.
                    const float Up = Moons.Direction[M][2];
                    if (Up <= 0.0f) continue;
                    // Phase is the REFERENCE convention here (0 = full), matching MoonAmbient's cosine exactly.
                    const float Phase = Moons.Params[M][2];
                    const float Lit   = 0.5f + 0.5f * std::cos(Phase * 2.0f * 3.14159265358979323846f);
                    const float Level = Moons.Params[M][1] * Up * Lit;
                    if (Level > BestLevel) { BestLevel = Level; Brightest = static_cast<int>(M); }
                }

                ShadowFrameStaged.MoonEnabled = Brightest >= 0 && BestLevel > 0.0f;
                if (ShadowFrameStaged.MoonEnabled)
                {
                    const uint32_t M = static_cast<uint32_t>(Brightest);
                    for (int I = 0; I < 3; ++I) ShadowFrameStaged.MoonDirection[I] = Moons.Direction[M][I];
                    // The 0.0025 scale is MoonAmbient's, kept verbatim so the direct tap and the kernel's ambient
                    //    are the same moon at the same brightness rather than two guesses that drift apart.
                    for (int I = 0; I < 3; ++I)
                        ShadowFrameStaged.MoonRadiance[I] = Moons.Tint[M][I] * BestLevel * 0.0025f;
                    // MoonParams.x is the angular RADIUS in radians — exactly what the PCSS penumbra wants.
                    ShadowFrameStaged.MoonAngularRadius = Moons.Params[M][0] > 1e-6f ? Moons.Params[M][0] : 0.00465f;
                }

                // One frame, both bodies. See ④d: the sun staged it, the moon completed it, the device gets it now.
                Surface.AssignShadowFrame(ShadowFrameStaged);
            }
            // Same shape as the sky above: a moon slider step is absorbed at 1/n on a converged frame, so the
            //    roster bytes are compared and the accumulation restarts the tick anything lands.
            if (std::memcmp(&Moons, &LastMoons, sizeof(Moons)) != 0)
            {
                LastMoons = Moons;
                Integrator.ResetAccumulation();
            }
        }

        // ④f GPU post — stars, flare and rainbow ride one 128-byte record at binding 24. The flare's occlusion
        //     is a single camera→sun ray through the CPU traversal: the header's "never per frame" guidance
        //     targets per-pixel tracing, and one ray is microseconds — the one query the flare's own spec
        //     demands (light that never entered the lens cannot bounce in it). Pushed every frame beside the
        //     sky and moons, and compared like them, so star/flare/bow sliders land the tick they move.
        {
            FRONTIER_TELEMETRY_SCOPE("Frame/PostRefresh");
            const Frontier::Vector3 Eye = Camera.QuerySpatialLocation();
            const float EyeArray[3] = { Eye.x, Eye.y, Eye.z };
            const Frontier::CelestialFrame& Frame = Celestial.Frame();
            float SunVisibility = 1.0f;
            if (Traversal.IsReady())
            {
                float HitDistance = 0.0f; uint32_t HitPrimitive = 0u;
                if (Traversal.TraceClosest(EyeArray, Frame.Sun.Direction, HitDistance, HitPrimitive))
                    SunVisibility = 0.0f;
            }
            const Frontier::Vector3 Forward = Camera.QueryForwardVector();
            const Frontier::Vector3 Right   = Camera.QueryRightVector();
            const Frontier::Vector3 Upward  = Camera.QueryUpwardVector();
            const float ForwardArray[3] = { Forward.x, Forward.y, Forward.z };
            const float RightArray[3]   = { Right.x, Right.y, Right.z };
            const float UpArray[3]      = { Upward.x, Upward.y, Upward.z };
            const Frontier::PostConstantRecord Post =
                Celestial.PackPostRecord(ForwardArray, RightArray, UpArray, Dispatch.FieldOfViewTanHalf,
                                         Camera.QueryAspectRatio(), RenderHeight, SunVisibility);
            (void)Surface.RefreshPost(&Post, sizeof(Post));
            if (std::memcmp(&Post, &LastPost, sizeof(Post)) != 0)
            {
                LastPost = Post;
                Integrator.ResetAccumulation();
            }
        }

        // ⑤ Cull → raster → HiZ → resolve → kernel, blit to swapchain, submit ImGui, present
        {
            FRONTIER_TELEMETRY_SCOPE("Frame/RecordAndPresent");
            Surface.RecordAndPresent(Dispatch);
        }

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

        //──────────────────────────────────────────────────────────────────────
        // Performance telemetry — CPU frame pacing and the GPU stage timings
        //──────────────────────────────────────────────────────────────────────
        // PerformanceTelemetrySequence owns the cadence, the prose and the measurement ROWS that land in
        //    ProjectZero_TelemetryReport. See its header: the report used to carry no performance or GPU entries at
        //    all, because everything the frame loop knew was written with RecordMessage (a sentence) and never with
        //    RecordMeasurement (a row).
        {
            FRONTIER_TELEMETRY_SCOPE("Frame/PerformanceTelemetry");
            Frontier::ProjectZero::PerformanceWorkload Workload;
            Workload.RenderWidth     = Surface.QueryWidth();
            Workload.RenderHeight    = Surface.QueryHeight();
            Workload.PresentMode     = Surface.QueryPresentModeName();
            const Frontier::ReSTIRIntegratorConfiguration& C = Integrator.QueryConfiguration();
            Workload.Candidates      = C.CandidatesPerPixel;
            Workload.ExtraCandidates = C.ExtraCandidateCount;
            Workload.SpatialTaps     = C.SpatialTapCount;
            Workload.DenoiseLevels   = C.DenoiseLevelCount;

            // Reporting is the natural moment to get the rows onto disk: a run killed mid-flight then still leaves a
            //    complete report rather than one truncated part-way through a window.
            if (PerformanceTelemetry.AdvanceFrame(Δτ, Logger, Surface.QueryVisibilityTelemetry(), Workload,
                                                  Telemetry.QueryAverageFramesPerSecond(),
                                                  Telemetry.QueryResidentMebibytes()))
            {
                Logger.FlushSink();
            }
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

    // ⚠️ THIS is the only point the verbose ledger touches the disk. Every scope timed during the run lived in a
    //    preallocated RAM ring so that measuring the renderer could not perturb it; the whole run is serialised
    //    here, once, into a summary table and a per-sample log. Development builds only — on a ship build the macro
    //    is `(false)` and none of this exists.
    (void)FRONTIER_TELEMETRY_FLUSH("Projects/Project-Zero/Diagnostics", "ProjectZero_FrameTelemetry");

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
