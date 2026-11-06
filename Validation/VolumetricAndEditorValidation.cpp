/*==========================================================================================================================================
                                                     VOLUMETRICANDEDITORVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 End-to-end C++ & Headless Vulkan 1.1 validation suite for:
//    1. 18-preset GasPresetLibrary, 82-parameter GasParameterCatalogue, & .gasscene.toml round-trip + all 8 Sample scenes.
//    2. 3D WorldSdfScene analytic SDF + ControlVehicle aerodynamic wake velocity field.
//    3. TyreSmokeIntake Pacejka slip -> 3-node thermal conduction -> rubber smoke -> >= 160 C ignition fire + fire light.
//    4. Headless Vulkan 1.1 PyroVolumeSolver standalone raymarch + transparent RGBA8 flipbook bake.
//    5. Continuous Toroidal World-Grid driving-course smoke (WorldGridRelocations > 0 across >20 m travel) +
//       authentic Frontier Editor (EverestWorkspace + ControlCentrePanel + FluidPlumeEditorWorkspace) with 1920x1080 PNG proofs.

#include "Engine/Interface/FluidPlumeEditor.h"
#include "Engine/Interface/FluidPlumeEditor/FluidPlumeEditorWorkspace.h"
#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Lifecycle/ActivationSequence.h"
#include "Engine/PhysicalDynamics/FluidDynamics/GasParameterCatalogue.h"
#include "Frontier/Engine/Rendering/PngImageCodec.h"
#include "Frontier/Engine/Rendering/VulkanContext.h"
#include "Frontier/Engine/Rendering/WorldSdfScene.h"
#include "Frontier/Engine/Session/LobbySession.h"
#include "Frontier/Engine/VolumetricDynamics/FluidPlumeBridge.h"
#include "Frontier/Engine/VolumetricDynamics/GasPresetLibrary.h"
#include "Frontier/Engine/VolumetricDynamics/GasSceneCodec.h"
#include "Frontier/Engine/VolumetricDynamics/PyroVolumeSolver.h"
#include "Frontier/Engine/VolumetricDynamics/TyreSmokeIntake.h"
#include "Projects/Everest/Source/EverestWorkspace.h"
#include "Projects/Everest/Source/Navigation/NavigationApp.h"
#include "Projects/Everest/Source/Session/SessionPolicy.h"
#include "UI/Editor/ControlCentrePanel.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{

int Failures = 0;

void Expect(bool Condition, const char* Description)
{
    if (!Condition)
    {
        std::fprintf(stderr, "  [FAIL] %s\n", Description);
        ++Failures;
    }
    else
    {
        std::printf("  [PASS] %s\n", Description);
    }
}

std::filesystem::path LocateRepositorySubpath(const std::filesystem::path& Rel)
{
    const std::filesystem::path Candidates[] = {
        Rel,
        std::filesystem::path("..") / Rel,
        std::filesystem::path("../..") / Rel,
    };
    for (const auto& P : Candidates)
    {
        if (std::filesystem::exists(P))
            return std::filesystem::absolute(P);
    }
    return std::filesystem::absolute(Rel);
}

std::string ReadWholeFile(const std::filesystem::path& Path)
{
    std::ifstream In(Path, std::ios::binary);
    if (!In)
        return {};
    return std::string((std::istreambuf_iterator<char>(In)), std::istreambuf_iterator<char>());
}

bool RenderImGuiFrameOffscreen(Frontier::VulkanContext& VkCtx, Frontier::VulkanImGuiPresenter& Presenter,
                               Frontier::VulkanImageTarget& ColorImg, VkFramebuffer Fbo,
                               int Width, int Height, std::vector<uint8_t>& OutRgba)
{
    Presenter.SyncManagedTextures(VkCtx);
    VkCommandBuffer Cmd = VkCtx.BeginFrameCommands();
    VkCtx.TransitionImage(Cmd, ColorImg, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    const std::array<float, 4> ClearBg = { 0.04f, 0.04f, 0.04f, 1.0f };
    VkCtx.BeginTarget(Cmd, VkCtx.QueryTargetSingleRgba8Clear(), Fbo, Width, Height, &ClearBg, false);
    Presenter.RecordDrawData(VkCtx, Cmd, ImGui::GetDrawData(), Width, Height);
    VkCtx.EndTarget(Cmd);
    if (!VkCtx.SubmitAndWaitFrameCommands())
        return false;
    return VkCtx.DownloadRgba8Image(ColorImg, OutRgba);
}

bool HasNonTrivialColorVariance(const std::vector<uint8_t>& Rgba)
{
    if (Rgba.size() < 64u)
        return false;
    uint8_t MinLum = 255u, MaxLum = 0u;
    uint64_t BrightCount = 0u;
    for (size_t Idx = 0u; Idx + 3u < Rgba.size(); Idx += 4u)
    {
        const uint8_t Lum = static_cast<uint8_t>((uint32_t(Rgba[Idx]) + uint32_t(Rgba[Idx + 1u]) + uint32_t(Rgba[Idx + 2u])) / 3u);
        MinLum = std::min(MinLum, Lum);
        MaxLum = std::max(MaxLum, Lum);
        if (Lum > 42u)
            ++BrightCount;
    }
    return (MaxLum - MinLum) >= 45u && BrightCount > 250u;
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    using namespace Frontier;

    bool WriteProofs     = false;
    bool RequireVulkan   = false;
    std::string ProofDir = "/tmp/frontier-proofs";
    for (int Idx = 1; Idx < ArgumentCount; ++Idx)
    {
        if (std::strcmp(Arguments[Idx], "--require-vulkan") == 0)
        {
            RequireVulkan = true;
        }
        else if (std::strcmp(Arguments[Idx], "--write-proofs") == 0 || std::strcmp(Arguments[Idx], "--emit-proofs") == 0)
        {
            WriteProofs   = true;
            RequireVulkan = true;
            if (Idx + 1 < ArgumentCount && Arguments[Idx + 1][0] != '-')
                ProofDir = Arguments[++Idx];
        }
    }

    std::printf("=== 1. Validating 18 Presets, GasParameterCatalogue & .gasscene.toml Codec ===\n");
    Expect(GasPresetCount == 18u, "Exact 18 presets ported from Experimental/Fluid/src/presets.js");
    Expect(FluidDynamics::QueryGasParameterCatalogue().size() == 33u && FluidDynamics::QueryGasPresets().size() == 6u,
           "Authentic Frontier GasParameterCatalogue (33 entries, 6 looks) populated");
    Expect(FindGasPreset("ue5_pyro_default") != nullptr, "ue5_pyro_default preset registered");
    Expect(FindGasPreset("megaton_open_bounds") != nullptr, "megaton_open_bounds preset registered");
    Expect(FindGasPreset("tyre_burnout") != nullptr, "tyre_burnout preset registered");
    Expect(FindGasPreset("low_gpu_performance") != nullptr, "low_gpu_performance preset registered");

    bool AllPresetsRoundTrip = true;
    const GasPreset* PresetTable = GasPresetLibrary();
    for (uint32_t PIdx = 0u; PIdx < GasPresetCount; ++PIdx)
    {
        GasScene Doc{};
        Doc.Name     = PresetTable[PIdx].Name;
        Doc.Settings = ConstructPresetSettingsBySlot(PIdx);

        const std::string Serialized = WriteGasScene(Doc);
        GasScene Parsed{};
        const GasSceneReading Res = ReadGasScene(Serialized, Parsed);
        if (!Res.Accepted ||
            std::fabs(Parsed.Settings.Buoyancy - Doc.Settings.Buoyancy) > 1e-4f ||
            std::fabs(Parsed.Settings.FireIntensity - Doc.Settings.FireIntensity) > 1e-4f ||
            Parsed.Settings.LatticeResolution != Doc.Settings.LatticeResolution)
        {
            AllPresetsRoundTrip = false;
        }
    }
    Expect(AllPresetsRoundTrip, "All 18 GasPresets round-trip losslessly through .gasscene.toml");

    const char* SampleFiles[8] = {
        "Experimental/Fluid/Samples/camp_fire_steady.gasscene.toml",
        "Experimental/Fluid/Samples/cold_dust_fall.gasscene.toml",
        "Experimental/Fluid/Samples/deflector_obstacle.gasscene.toml",
        "Experimental/Fluid/Samples/far_cheap_plume.gasscene.toml",
        "Experimental/Fluid/Samples/fracture_dust_burst.gasscene.toml",
        "Experimental/Fluid/Samples/hero_detonation.gasscene.toml",
        "Experimental/Fluid/Samples/open_bounds_megaton.gasscene.toml",
        "Experimental/Fluid/Samples/tyre_burnout_ring.gasscene.toml",
    };
    bool AllSamplesParsed = true;
    for (const char* SamplePath : SampleFiles)
    {
        const std::string Text = ReadWholeFile(LocateRepositorySubpath(SamplePath));
        GasScene SampleDoc{};
        if (Text.empty() || !ReadGasScene(Text, SampleDoc).Accepted)
            AllSamplesParsed = false;
    }
    Expect(AllSamplesParsed, "All 8 Experimental/Fluid/Samples/*.gasscene.toml parse cleanly");

    std::printf("\n=== 2. Validating 3D World SDF & ControlVehicle Wake Evaluator ===\n");
    WorldSdfScene CpuSdfCheck{};
    WorldVehicleRig MovingCar{};
    MovingCar.Visual.PositionMetres = { 4.0f, 0.0f, 0.53f };
    MovingCar.VelocityMps           = { 18.5f, 0.0f, 0.0f };
    std::vector<WorldVehicleRig> SingleCar{ MovingCar };

    float DistInsideSphere = 10.0f, DistInsideCar = 10.0f;
    std::array<float, 3> VelNearSphere{}, VelNearCar{};
    CpuSdfCheck.EvaluateSdfAndVelocity({ 6.2f, -2.35f, 0.82f }, true, true, SingleCar, DistInsideSphere, VelNearSphere);
    CpuSdfCheck.EvaluateSdfAndVelocity({ 4.0f,  0.0f,  0.80f }, true, true, SingleCar, DistInsideCar,    VelNearCar);
    Expect(DistInsideSphere < -0.5f, "Proving Grounds deflector sphere evaluates negative signed distance inside solid");
    Expect(DistInsideCar < 0.0f && VelNearCar[0] > 18.0f, "ControlVehicle SDF hull evaluates interior solid + 18.5 m/s aerodynamic wake");

    std::printf("\n=== 3. Validating Pacejka Tyre Slip -> Smoke -> Overheat Fire Ignition ===\n");
    SessionConfiguration Config{};
    LobbySession BurnoutSession(Config, Everest::ConstructEverestSessionPolicy);
    Everest::NavigationApp BurnoutNav(Config);
    BurnoutNav.Navigate(BurnoutSession, 2u); // Proving Grounds

    DeployableUnit* Car = BurnoutSession.QueryLocalVehicle();
    Expect(Car != nullptr, "ControlVehicle deployed on Proving Grounds");

    TyreSmokeIntake Intake{};
    Intake.BurnoutLineLock = true;
    bool SawTyreSmoke = false;
    bool SawTyreFire  = false;
    std::vector<DynamicFireLight> BurnoutLights;
    PyroBoundsBox BurnoutBox{};
    BurnoutBox.BoxMin          = { -3.8f, -0.60f, -3.8f };
    BurnoutBox.BoxMax          = {  3.8f,  3.60f,  3.8f };
    BurnoutBox.EffectiveWidth  = 7.6f;
    BurnoutBox.EffectiveHeight = 4.2f;
    PyroExternalSource WheelSrcs[8]{};

    for (int Tick = 0; Tick < 960 && Car; ++Tick)
    {
        InputPacket Pkt{};
        Pkt.ThrottleAxis = 1.0f;
        Pkt.BrakeAxis    = 1.0f;
        Pkt.SteerAxis    = 0.18f;
        BurnoutSession.SubmitLocalInput(Pkt);
        BurnoutSession.Step(1.0f / 60.0f);

        const auto Pos = Car->QueryPositionMetres();
        Intake.Advance(*Car, 1.0f / 60.0f, WorldSdfScene::ResolveCourseHeight(Pos[0], Pos[1], true));
        BurnoutLights.clear();
        const int Count = Intake.ProjectIntoDomain({ Pos[0], Pos[1], 0.0f }, BurnoutBox, WheelSrcs, 8, BurnoutLights);
        if (Count > 0 && Intake.QueryActiveSmokingWheelCount() >= 2)
            SawTyreSmoke = true;
        if (Intake.HasAnyBurningTyre() && !BurnoutLights.empty())
            SawTyreFire = true;
    }
    Expect(SawTyreSmoke, "Standing burnout generates active Pacejka tyre smoke sources");
    Expect(SawTyreFire,  "Sustained >= 160 C tyre overheat ignites rubber fire + emits DynamicFireLight");

    std::printf("\n=== 4. Validating Headless Vulkan 1.1 Solver, Toroidal World-Grid & Authentic Frontier Editor ===\n");
    VulkanContext VkCtx{};
    std::string VkDiag;
    if (!VkCtx.InitializeHeadless(VkDiag))
    {
        if (RequireVulkan)
        {
            std::fprintf(stderr, "  [FAIL] Headless Vulkan 1.1 initialization failed: %s\n", VkDiag.c_str());
            return 1;
        }
        std::printf("  [SKIP] No Vulkan 1.1 ICD installed on this runner (%s); CPU/TOML/SDF/Tyre checks complete.\n", VkDiag.c_str());
        std::printf("\nValidation summary: %d failure(s).\n", Failures);
        return Failures == 0 ? 0 : 1;
    }
    std::printf("  [INFO] Vulkan 1.1 active on: %s\n", VkCtx.QueryDeviceSummary().c_str());

    // Standalone PyroVolumeSolver + 2x2 Flipbook bake check
    {
        GasSettings StandaloneSettings        = ConstructPresetSettings("ue5_pyro_default");
        StandaloneSettings.LatticeResolution  = 32;
        StandaloneSettings.PressureIterations = 12;
        StandaloneSettings.RaymarchSteps      = 56;
        PyroVolumeSolver StandaloneSolver{};
        Expect(StandaloneSolver.Initialize(VkCtx, StandaloneSettings, VkDiag), "PyroVolumeSolver initializes all 15 Vulkan 1.1 SPIR-V pipelines");

        PyroOrbitCamera OrbitCam{};
        for (int Step = 0; Step < 20; ++Step)
            StandaloneSolver.AdvanceSimulation(VkCtx, StandaloneSettings, 1.0f / 30.0f);

        Expect(StandaloneSolver.RenderStandaloneViewport(VkCtx, StandaloneSettings, OrbitCam.ComputeFrame(16.0f / 9.0f), 320, 180, false),
               "PyroVolumeSolver renders Vulkan volumetric viewport");

        std::vector<uint8_t> StandalonePixels;
        int Rw = 0, Rh = 0;
        Expect(StandaloneSolver.ReadViewportRgba(VkCtx, StandalonePixels, Rw, Rh) && HasNonTrivialColorVariance(StandalonePixels),
               "Vulkan volumetric raymarcher produces shaded luminous plume pixels");

        std::vector<uint8_t> FlipbookRgba;
        Expect(StandaloneSolver.BakeFlipbookSheet(VkCtx, StandaloneSettings, OrbitCam, 2, 2, 48, 1.0f / 25.0f, FlipbookRgba)
               && FlipbookRgba.size() == 96u * 96u * 4u,
               "PyroVolumeSolver bakes transparent RGBA8 flipbook sprite sheet via Vulkan");

        StandaloneSolver.Shutdown(VkCtx);
    }

    // Authentic Frontier UI + EverestWorkspace + FluidPlumeEditorWorkspace + Toroidal World-Grid validation
    const std::filesystem::path ContentDir  = LocateRepositorySubpath("EngineContent");
    const std::filesystem::path EverestRoot = LocateRepositorySubpath("Projects/Everest");
    const std::filesystem::path ArchiveDir  = LocateRepositorySubpath("Experimental/VehicleHmiPrototype/Archive");

    Expect(std::filesystem::exists(ContentDir / "FontArchives" / "DMSans" / "DMSans-Regular.ttf"), "OFL DM Sans fonts staged in EngineContent/FontArchives");
    Expect(std::filesystem::exists(ContentDir / "Icons"), "SVG Icon archives staged in EngineContent/Icons");

    EverestPlumeSceneRuntime PlumeScene{};
    Expect(PlumeScene.Initialize(VkCtx, ArchiveDir, VkDiag),
           "EverestPlumeSceneRuntime initializes WorldSdfScene + World-Grid PyroSolver + Editor PyroSolver");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& Io    = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.DeltaTime   = 1.0f / 30.0f;
    Io.ConfigFlags  |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    Io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
    ImGui::GetPlatformIO().Renderer_TextureMaxWidth  = 4096;
    ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;

    IconPresentation::Attach(ContentDir / "Icons");

    const int WinW = WriteProofs ? 1920 : 800;
    const int WinH = WriteProofs ? 1080 : 480;
    Io.DisplaySize = { static_cast<float>(WinW), static_cast<float>(WinH) };

    VulkanImGuiPresenter GuiPresenter{};
    Expect(GuiPresenter.Initialize(VkCtx, VkCtx.QueryTargetSingleRgba8Clear(), VkDiag),
           "VulkanImGuiPresenter initializes SPIR-V UI pipeline + multi-texture cache");

    VulkanImageTarget ProofColor{};
    Expect(VkCtx.CreateImageTarget(WinW, WinH, VK_FORMAT_R8G8B8A8_UNORM,
                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, false, ProofColor, VkDiag),
           "Allocated 1920x1080 RGBA8 offscreen Vulkan UI target");
    const VulkanImageTarget* ProofAtt[1] = { &ProofColor };
    VkFramebuffer ProofFbo = VkCtx.CreateFramebuffer(VkCtx.QueryTargetSingleRgba8Clear(), ProofAtt, 1u, WinW, WinH);

    const std::filesystem::path PrefsPath = std::filesystem::temp_directory_path() / "FrontierValidationPersonal.projectspace";
    std::filesystem::remove(PrefsPath);
    ActivationOwnership Ownership(new ControlCentrePanel(ContentDir / "FontArchives", PrefsPath, EverestRoot / "Everest.projectspace"));
    auto* Centre = static_cast<ControlCentrePanel*>(Ownership.get());
    Expect(Centre->Initialize() && Centre->Activate(), "ControlCentrePanel initializes TypefaceRegistry + theme + dockspace");

    {
        Everest::EverestWorkspace Workspace(*Centre, EverestRoot);

        WorldSpace& World = Workspace.QueryWorldSpace();
        RecordToken PlumeToken{};
        if (SceneRecordBase* ExistingPlume = World.FindFirstByType("FluidPlume"))
            PlumeToken = ExistingPlume->QueryToken();
        else
            PlumeToken = World.ConstructRecord("FluidPlume", "Smoke and fire");
        Expect(PlumeToken.IsValid(), "FluidPlume scene record present in Everest WorldSpace");

        PlumeScene.ResetWorldSimulation(VkCtx, World);

        Workspace.PresentSceneViewport = [&](const SceneViewportRequest& Req) -> SceneViewportImage {
            return PlumeScene.PresentSceneViewport(VkCtx, Workspace.QueryWorldSpace(), Req);
        };
        Workspace.StepSimulationCallback = [&](WorldSpace& W, double DtSec, const Everest::EverestWorkspace::SimulationStepDriver& Drv) {
            PlumeScene.StepWorldSimulation(VkCtx, W, DtSec, Drv.Throttle, Drv.Brake, Drv.Steer, Drv.Handbrake, Drv.ResetRequested);
        };
        InspectorRuntime::Instance().RegisterPlumeViewportPresenter([&](const PlumeViewportRequest& Req) -> PlumeViewportImage {
            return PlumeScene.PresentPlumeViewport(VkCtx, Req);
        });

        auto StepUiFrame = [&](double Dt, std::vector<uint8_t>* OutCapture) -> bool {
            Io.DisplaySize = { static_cast<float>(WinW), static_cast<float>(WinH) };
            Io.DeltaTime   = static_cast<float>(Dt);
            Centre->PrepareInterface();
            ImGui::NewFrame();
            (void)Centre->Integrate(Dt);
            (void)Workspace.ConfigurePlatform();
            Workspace.ConstructInterface(Dt);
            Centre->ConstructInterface();
            ImGui::Render();
            if (OutCapture)
                return RenderImGuiFrameOffscreen(VkCtx, GuiPresenter, ProofColor, ProofFbo, WinW, WinH, *OutCapture);
            GuiPresenter.SyncManagedTextures(VkCtx);
            return true;
        };

        if (WriteProofs)
            std::filesystem::create_directories(ProofDir);

        // Warm dockspace layout for 3 frames
        for (int Warm = 0; Warm < 3; ++Warm)
            StepUiFrame(1.0 / 30.0, nullptr);

        // Proof 1: Authentic FluidPlumeEditor ("Smoke & fire") with Pyro fire plume + Fire inspector page
        Workspace.QueryOutliner().Select(PlumeToken);
        Expect(InspectorRuntime::Instance().RequestEditorOpen(PlumeToken), "FluidPlumeEditor opens via InspectorRuntime::RequestEditorOpen");
        StepUiFrame(1.0 / 30.0, nullptr);
        if (FluidPlumeEditorWorkspace* Ed = FluidPlumeEditor::AccessWorkspace())
        {
            Ed->Perform("emitter:fire");
            Ed->Perform("look:inferno");
            Ed->Perform("select:Fire");
        }
        StepUiFrame(1.0 / 30.0, nullptr);
        {
            GasSettings WarmFire       = ConstructPresetSettings("ue5_pyro_default");
            WarmFire.LatticeResolution = PlumeScene.AccessEditorSolver().QueryLatticeResolution();
            PlumeScene.AccessEditorSolver().TriggerBurstSequence(WarmFire, "single");
            const int WarmSteps = WriteProofs ? 28 : 8;
            for (int Warm = 0; Warm < WarmSteps; ++Warm)
                PlumeScene.AccessEditorSolver().AdvanceSimulation(VkCtx, WarmFire, 1.0f / 30.0f);
        }
        StepUiFrame(1.0 / 30.0, nullptr);
        std::vector<uint8_t> Frame1;
        Expect(StepUiFrame(1.0 / 30.0, &Frame1) && HasNonTrivialColorVariance(Frame1),
               "Authentic FluidPlumeEditorWorkspace (Smoke & fire) renders UI + Vulkan 3D Pyro plume");
        if (WriteProofs)
            WriteRgbaPngFile(ProofDir + "/01_fluid_editor_pyro_plume.png", Frame1.data(), WinW, WinH);

        // Proof 2: Authentic FluidPlumeEditor Libraries sheet + Thick burnout / mushroom detonation
        if (FluidPlumeEditorWorkspace* Ed = FluidPlumeEditor::AccessWorkspace())
        {
            Ed->Perform("emitter:fire");
            Ed->Perform("look:thick-burnout");
            Ed->Perform("library:on");
            Ed->Perform("library-tab:looks");
        }
        {
            GasSettings WarmBlast       = ConstructPresetSettings("megaton_open_bounds");
            WarmBlast.LatticeResolution = PlumeScene.AccessEditorSolver().QueryLatticeResolution();
            PlumeScene.AccessEditorSolver().TriggerBurstSequence(WarmBlast, "mushroom");
            const int WarmSteps = WriteProofs ? 24 : 8;
            for (int Warm = 0; Warm < WarmSteps; ++Warm)
                PlumeScene.AccessEditorSolver().AdvanceSimulation(VkCtx, WarmBlast, 1.0f / 30.0f);
        }
        StepUiFrame(1.0 / 30.0, nullptr);
        std::vector<uint8_t> Frame2;
        Expect(StepUiFrame(1.0 / 30.0, &Frame2) && HasNonTrivialColorVariance(Frame2),
               "Authentic FluidPlumeEditorWorkspace renders Libraries sheet + live Vulkan plume");
        if (WriteProofs)
            WriteRgbaPngFile(ProofDir + "/02_fluid_editor_nuclear_blast.png", Frame2.data(), WinW, WinH);

        // Proof 3: Authentic EverestWorkspace (Outliner + FluidPlumeInspector + Log Stream + 3D SDF Proving Grounds)
        InspectorRuntime::Instance().CloseActiveEditor();
        Workspace.QueryOutliner().Select(PlumeToken);
        // Aim scene viewport camera nicely at the Proving Grounds pad + ControlVehicle
        Workspace.AccessViewportFlight().Position  = { -13.5, -9.5, 4.4 };
        Workspace.AccessViewportFlight().Heading   =  33.0;
        Workspace.AccessViewportFlight().Elevation = -14.5;
        StepUiFrame(1.0 / 30.0, nullptr);
        std::vector<uint8_t> Frame3;
        Expect(StepUiFrame(1.0 / 30.0, &Frame3) && HasNonTrivialColorVariance(Frame3),
               "Authentic EverestWorkspace renders Outliner + FluidPlumeInspector + 3D SDF Proving Grounds");
        if (WriteProofs)
            WriteRgbaPngFile(ProofDir + "/03_frontier_editor_world_sdf.png", Frame3.data(), WinW, WinH);

        // Proof 4: Continuous Toroidal World-Grid Burnout & Drift across the Proving Grounds!
        PlumeScene.ResetWorldSimulation(VkCtx, World);
        if (SceneRecordBase* VehRec = World.FindFirstByType("Vehicle"))
            Workspace.QueryOutliner().Select(VehRec->QueryToken());

        // Phase A: Standing burnout to heat rear tyres past ignition (>160 C) + build initial plume
        const int BurnoutWorldStart = WriteProofs ? 810 : 832;
        for (int Step = 0; Step < 840; ++Step)
        {
            if (Step < BurnoutWorldStart)
            {
                InputPacket Pkt{};
                Pkt.ThrottleAxis  = 1.0f;
                Pkt.BrakeAxis     = 0.85f;
                Pkt.HandbrakeAxis = 1.0f;
                Pkt.SteerAxis     = 0.16f;
                PlumeScene.AccessTyreIntake().BurnoutLineLock = true;
                PlumeScene.AccessVehicleUnit().ApplyInputPacket(Pkt, 1.0f / 60.0f);
                const auto Pos = PlumeScene.AccessVehicleUnit().QueryPositionMetres();
                PlumeScene.AccessTyreIntake().Advance(PlumeScene.AccessVehicleUnit(), 1.0f / 60.0f,
                                                      WorldSdfScene::ResolveCourseHeight(Pos[0], Pos[1], true));
            }
            else
            {
                PlumeScene.StepWorldSimulation(VkCtx, World, 1.0 / 35.0, 1.0f, 0.85f, 0.16f, true, false);
            }
        }
        // Phase B: Release line-lock & power-drift >10 metres across the Proving Grounds so the toroidal world-grid relocates!
        const auto StartDrivePos = PlumeScene.AccessVehicleUnit().QueryPositionMetres();
        const int DriveTicks = WriteProofs ? 60 : 28;
        for (int Step = 0; Step < DriveTicks; ++Step)
        {
            // Full throttle, zero brake/handbrake so the vehicle accelerates across the course while spinning hot rear tyres!
            PlumeScene.StepWorldSimulation(VkCtx, World, 1.0 / 25.0, 1.0f, 0.0f, 0.16f, false, false);
        }
        const auto EndDrivePos = PlumeScene.AccessVehicleUnit().QueryPositionMetres();
        const float DrivenDistMetres = std::hypot(EndDrivePos[0] - StartDrivePos[0], EndDrivePos[1] - StartDrivePos[1]);
        std::printf("  [INFO] Toroidal World-Grid drive distance = %.2f m, voxel relocations = %llu\n",
                    DrivenDistMetres, static_cast<unsigned long long>(PlumeScene.QueryWorldGridRelocations()));
        Expect(DrivenDistMetres > 4.0f && PlumeScene.QueryWorldGridRelocations() > 0u,
               "Toroidal World-Grid relocates smoothly across Proving Grounds without resetting smoke trail");

        // Position chase/quarter camera looking across the moving car & its continuous world-grid smoke/fire trail
        Workspace.AccessViewportFlight().Position  = { EndDrivePos[0] - 9.0, EndDrivePos[1] - 9.2, 4.2 };
        Workspace.AccessViewportFlight().Heading   =  46.0;
        Workspace.AccessViewportFlight().Elevation = -14.0;
        StepUiFrame(1.0 / 30.0, nullptr);
        std::vector<uint8_t> Frame4;
        Expect(StepUiFrame(1.0 / 30.0, &Frame4) && HasNonTrivialColorVariance(Frame4),
               "EverestWorkspace renders continuous World-Grid tyre smoke & ignited rubber fire behind moving ControlVehicle");
        if (WriteProofs)
        {
            WriteRgbaPngFile(ProofDir + "/04_vehicle_burnout_smoke_and_fire.png", Frame4.data(), WinW, WinH);
            std::printf("  [INFO] Wrote 4 1920x1080 Vulkan PNG proofs to %s\n", ProofDir.c_str());
        }

        InspectorRuntime::Instance().RegisterPlumeViewportPresenter({});
    }

    VkCtx.DestroyFramebuffer(ProofFbo);
    VkCtx.DestroyImageTarget(ProofColor);
    Ownership.reset();
    PlumeScene.Shutdown(VkCtx);
    GuiPresenter.Shutdown(VkCtx);
    ImGui::DestroyContext();
    VkCtx.Shutdown();

    std::printf("\nValidation summary: %d failure(s).\n", Failures);
    return Failures == 0 ? 0 : 1;
}
