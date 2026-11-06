/*==========================================================================================================================================
                                                            EVERESTGLFWMAIN.CPP
==========================================================================================================================================*/
// 🧩 GLFW + Vulkan 1.1 interactive front-end for Project Everest:
//    Hosts the authentic Frontier Editor (EverestWorkspace + ControlCentrePanel + Outliner + Inspector + Log Stream + Construct),
//    the authentic Smoke & fire editor (FluidPlumeEditorWorkspace), the 3D GPU SDF Lobby and Proving Grounds, ControlVehicle driving,
//    and continuous toroidal world-grid Pacejka tyre smoke & fire. Zero OpenGL dependencies.
//
//      EverestWindow [--archive DIR] [--proving-grounds] [--fluid-editor] [--smoke-editor] [--preset ID] [--frames N] [--capture PATH.png]
//                    [--optimized | --baseline] [--uncapped] [--perf-window] [--perf-test]
//
//      (none)           opens the project's startup level, the Lobby: the studio with one vehicle
//      --proving-grounds  opens the Proving Grounds level instead
//      --fluid-editor   opens the Fluid editor (Experimental/Fluid's page: 85 settings, 18 presets) on a Fluid Volume;
//                       --preset ID is one of the 18 presets (for example tactical_ordnance)
//      --smoke-editor   opens the Smoke & fire editor on the tyres' plume; --preset ID is one of its looks (for example inferno)
//
//    F2 opens the performance window (PerformancePanel): the optimized-execution switch, live figures and the A/B test. The switch is off at the
//    start unless --optimized (or FRONTIER_OPTIMIZED_EXECUTION=1) turns it on; --baseline says off. --uncapped presents without waiting for the
//    display; --perf-window opens the window at the start; --perf-test also runs the A/B test once the window is up.

#include "Engine/Interface/FluidEditor/FluidEditorWorkspace.h"
#include "Engine/Interface/FluidPlumeEditor.h"
#include "Engine/Interface/FluidPlumeEditor/FluidPlumeEditorWorkspace.h"
#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/FluidVolumeEditor.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Lifecycle/ActivationSequence.h"
#include "Frontier/Engine/Diagnostics/PerformanceLog.h"
#include "Frontier/Engine/Diagnostics/PerformancePanel.h"
#include "Frontier/Engine/Rendering/PngImageCodec.h"
#include "Frontier/Engine/Rendering/VulkanContext.h"
#include "Frontier/Engine/VolumetricDynamics/FluidPlumeBridge.h"
#include "Projects/Everest/Source/EverestWorkspace.h"
#include "UI/Editor/ControlCentrePanel.h"

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

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

std::filesystem::path LocateFacetArchiveDir(const std::string& OverrideDir)
{
    if (!OverrideDir.empty() && std::filesystem::exists(std::filesystem::path(OverrideDir) / "ControlVehicleBody.facet"))
        return std::filesystem::absolute(OverrideDir);

    const char* Candidates[] = {
        "Experimental/VehicleHmiPrototype/Archive",
        "Archive",
        "../Experimental/VehicleHmiPrototype/Archive",
        "../../Experimental/VehicleHmiPrototype/Archive",
    };
    for (const char* Dir : Candidates)
    {
        if (std::filesystem::exists(std::filesystem::path(Dir) / "ControlVehicleBody.facet"))
            return std::filesystem::absolute(Dir);
    }
    return std::filesystem::absolute("Experimental/VehicleHmiPrototype/Archive");
}

bool CreateGlfwWindowSurfaceCallback(VkInstance Instance, void* UserData, VkSurfaceKHR& OutSurface)
{
    auto* Window = static_cast<GLFWwindow*>(UserData);
    return glfwCreateWindowSurface(Instance, Window, nullptr, &OutSurface) == VK_SUCCESS;
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    using namespace Frontier;

    std::string ArchiveDirectory = "";
    std::string InitialPresetId  = "";
    std::string CapturePngPath   = "";
    bool StartInProvingGrounds   = false;
    bool StartInFluidEditor      = false;
    bool StartInSmokeEditor      = false;
    bool StartUncapped           = false;
    bool StartPerformanceWindow  = false;
    bool StartPerformanceTest    = false;
    int MaxFrames                = 0;

    for (int Index = 1; Index < ArgumentCount; ++Index)
    {
        if (std::strcmp(Arguments[Index], "--archive") == 0 && Index + 1 < ArgumentCount)
            ArchiveDirectory = Arguments[++Index];
        else if (std::strcmp(Arguments[Index], "--preset") == 0 && Index + 1 < ArgumentCount)
            InitialPresetId = Arguments[++Index];
        else if (std::strcmp(Arguments[Index], "--capture") == 0 && Index + 1 < ArgumentCount)
            CapturePngPath = Arguments[++Index];
        else if (std::strcmp(Arguments[Index], "--frames") == 0 && Index + 1 < ArgumentCount)
            MaxFrames = std::atoi(Arguments[++Index]);
        else if (std::strcmp(Arguments[Index], "--proving-grounds") == 0)
            StartInProvingGrounds = true;
        else if (std::strcmp(Arguments[Index], "--lobby") == 0)
            StartInProvingGrounds = false;
        else if (std::strcmp(Arguments[Index], "--optimized") == 0)
            OptimizedExecution::Configure(true);
        else if (std::strcmp(Arguments[Index], "--baseline") == 0)
            OptimizedExecution::Configure(false);
        else if (std::strcmp(Arguments[Index], "--uncapped") == 0)
            StartUncapped = true;
        else if (std::strcmp(Arguments[Index], "--perf-window") == 0)
            StartPerformanceWindow = true;
        else if (std::strcmp(Arguments[Index], "--perf-test") == 0)
            StartPerformanceWindow = StartPerformanceTest = true;
        else if (std::strcmp(Arguments[Index], "--fluid-editor") == 0)
            StartInFluidEditor = true;
        else if (std::strcmp(Arguments[Index], "--smoke-editor") == 0)
            StartInSmokeEditor = true;
    }

    if (!glfwInit())
    {
        std::fprintf(stderr, "EverestWindow: glfwInit failed.\n");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* Window = glfwCreateWindow(1600, 920, "Frontier Editor — Project Everest (Vulkan 1.1)", nullptr, nullptr);
    if (!Window)
    {
        std::fprintf(stderr, "EverestWindow: glfwCreateWindow failed.\n");
        glfwTerminate();
        return 1;
    }

    uint32_t GlfwExtCount = 0u;
    const char** GlfwExts = glfwGetRequiredInstanceExtensions(&GlfwExtCount);
    std::vector<const char*> ReqExts;
    for (uint32_t Idx = 0u; Idx < GlfwExtCount; ++Idx)
        ReqExts.push_back(GlfwExts[Idx]);

    VulkanContext VkCtx{};
    std::string Diag;
    int InitFbW = 1600, InitFbH = 920;
    glfwGetFramebufferSize(Window, &InitFbW, &InitFbH);
    if (!VkCtx.InitializeWithSurface(ReqExts.data(), static_cast<uint32_t>(ReqExts.size()),
                                     CreateGlfwWindowSurfaceCallback, Window, InitFbW, InitFbH, Diag))
    {
        std::fprintf(stderr, "EverestWindow: Vulkan 1.1 initialization failed: %s\n", Diag.c_str());
        glfwDestroyWindow(Window);
        glfwTerminate();
        return 1;
    }

    EverestPlumeSceneRuntime PlumeScene{};
    if (!PlumeScene.Initialize(VkCtx, LocateFacetArchiveDir(ArchiveDirectory), Diag))
    {
        std::fprintf(stderr, "EverestWindow: PlumeScene initialization failed: %s\n", Diag.c_str());
        VkCtx.Shutdown();
        glfwDestroyWindow(Window);
        glfwTerminate();
        return 1;
    }
    PlumeScene.ConfigureProvingGroundsMode(StartInProvingGrounds);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& Io = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.ConfigFlags  |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    Io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
    ImGui::GetPlatformIO().Renderer_TextureMaxWidth  = 4096;
    ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;

    const std::filesystem::path ContentDir  = LocateRepositorySubpath("EngineContent");
    const std::filesystem::path EverestRoot = LocateRepositorySubpath("Projects/Everest");
    IconPresentation::Attach(ContentDir / "Icons");

    ImGui_ImplGlfw_InitForVulkan(Window, true);

    VulkanImGuiPresenter GuiPresenter{};
    if (!GuiPresenter.Initialize(VkCtx, VkCtx.QueryTargetSwapchainClear(), Diag))
    {
        std::fprintf(stderr, "EverestWindow: VulkanImGuiPresenter failed: %s\n", Diag.c_str());
        PlumeScene.Shutdown(VkCtx);
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        VkCtx.Shutdown();
        glfwDestroyWindow(Window);
        glfwTerminate();
        return 1;
    }

    const std::filesystem::path PreferencesPath = std::filesystem::temp_directory_path() / "FrontierEverestPersonal.projectspace";
    ActivationOwnership Ownership(new ControlCentrePanel(ContentDir / "FontArchives", PreferencesPath, EverestRoot / "Everest.projectspace"));
    auto* Centre = static_cast<ControlCentrePanel*>(Ownership.get());
    (void)Centre->Initialize();
    (void)Centre->Activate();

    // [unit] - the project starts in the Lobby (its startup_level); --proving-grounds opens the level of the Proving Grounds instead
    Everest::EverestWorkspace Workspace(*Centre, EverestRoot, StartInProvingGrounds ? std::string("default-level") : std::string());
    WorldSpace& World = Workspace.QueryWorldSpace();
    // [unit] - the Lobby opens on its one vehicle, in the hero framing of the HTML Lobby (the Proving Grounds keep the view of the whole course)
    if (!StartInProvingGrounds)
        Workspace.AccessViewportFlight() = Everest::SceneViewportFlight::ConstructLobbyView();
    if (StartUncapped)
        VkCtx.ConfigureUncappedPresent(true);

    RecordToken PlumeToken{};
    if (SceneRecordBase* ExistingPlume = World.FindFirstByType("FluidPlume"))
        PlumeToken = ExistingPlume->QueryToken();
    else
        PlumeToken = World.ConstructRecord("FluidPlume", "Fluid Plume");

    // The Fluid editor opens from a Fluid Volume (Outliner > Solvers); the level carries none, so the window makes one the way Construct does.
    RecordToken FluidToken{};
    if (SceneRecordBase* ExistingFluid = World.FindFirstByType("FluidVolume"))
        FluidToken = ExistingFluid->QueryToken();
    else
        FluidToken = World.ConstructRecord("FluidVolume", "Fluid Volume");

    PlumeScene.ResetWorldSimulation(VkCtx, World);

    Workspace.PresentSceneViewport = [&](const SceneViewportRequest& Req) -> SceneViewportImage {
        return PlumeScene.PresentSceneViewport(VkCtx, Workspace.QueryWorldSpace(), Req);
    };
    // [unit] - the F2 window: while its A/B test runs, the test's scripted drive and fixed step replace what the player asked for
    Everest::SceneViewportFlight TestFlight{};
    Everest::SceneViewportFlight SavedFlight{};
    bool                         CameraHeld = false;
    PerformancePanel::WorkspaceHooks PanelHooks{};
    PanelHooks.QuerySimulationActive    = [&]() { return Workspace.QuerySimulationActive(); };
    PanelHooks.ConfigureSimulationActive = [&](bool Active) { Workspace.ConfigureSimulationActive(Active); };
    PanelHooks.ConfigureTestCamera      = [&]() {
        if (!CameraHeld)
        {
            SavedFlight = Workspace.AccessViewportFlight();
            CameraHeld  = true;
        }
        Workspace.ConfigureViewportCameraHeld(true);   // [unit] - the Lobby's vehicle has a chase camera: it must not take the view from the test
        Workspace.AccessViewportFlight() = TestFlight;
    };
    PanelHooks.RestoreCamera = [&]() {
        if (CameraHeld)
            Workspace.AccessViewportFlight() = SavedFlight;
        CameraHeld = false;
        Workspace.ConfigureViewportCameraHeld(false);
    };
    PerformancePanel Panel(VkCtx, PlumeScene, PanelHooks);
    Panel.ConfigureOpen(StartPerformanceWindow);

    Workspace.StepSimulationCallback = [&](WorldSpace& W, double DtSec, const Everest::EverestWorkspace::SimulationStepDriver& Drv) {
        PerformancePanel::SimulationStep Step{ DtSec, Drv.Throttle, Drv.Brake, Drv.Steer, Drv.Handbrake, Drv.ResetRequested };
        Panel.ResolveSimulationStep(Step);
        PlumeScene.StepWorldSimulation(VkCtx, W, Step.DeltaSeconds, Step.Throttle, Step.Brake, Step.Steer, Step.Handbrake > 0.5f, Step.ResetRequested);
    };
    InspectorRuntime::Instance().RegisterPlumeViewportPresenter([&](const PlumeViewportRequest& Req) -> PlumeViewportImage {
        return PlumeScene.PresentPlumeViewport(VkCtx, Req);
    });
    InspectorRuntime::Instance().RegisterFluidViewportPresenter([&](const FluidViewportRequest& Req) -> FluidViewportImage {
        return PlumeScene.PresentFluidViewport(VkCtx, Req);
    });

    if (StartInFluidEditor && FluidToken.IsValid())
    {
        Workspace.QueryOutliner().Select(FluidToken);
        InspectorRuntime::Instance().RequestEditorOpen(FluidToken);
        if (FluidEditorWorkspace* Ed = FluidVolumeEditor::AccessWorkspace())
        {
            if (!InitialPresetId.empty())
                (void)Ed->Apply("preset:" + InitialPresetId);
        }
    }
    else if (StartInSmokeEditor && PlumeToken.IsValid())
    {
        Workspace.QueryOutliner().Select(PlumeToken);
        InspectorRuntime::Instance().RequestEditorOpen(PlumeToken);
        if (FluidPlumeEditorWorkspace* Ed = FluidPlumeEditor::AccessWorkspace())
        {
            Ed->Perform("emitter:fire");
            if (!InitialPresetId.empty())
                Ed->Perform("look:" + InitialPresetId);
        }
    }
    else if (PlumeToken.IsValid())
    {
        Workspace.QueryOutliner().Select(PlumeToken);
    }

    double LastTime  = glfwGetTime();
    int FrameCounter = 0;

    while (!glfwWindowShouldClose(Window))
    {
        // [unit] - a refresh is one pass of this loop; the performance log opens it here and closes it at the next pass
        Panel.BeginRefresh();
        if (StartPerformanceTest && FrameCounter == 90)
            Panel.StartTest();
        {
            PerformanceScope PollScope("window.poll_events");
            glfwPollEvents();
        }
        const double Now = glfwGetTime();
        // [unit] - the A/B test runs at the fixed step of its drive, so that both paths meet the same interface timeline too
        const double Dt  = Panel.QueryTestRunning() ? 1.0 / 60.0 : std::clamp(Now - LastTime, 1.0 / 240.0, 0.05);
        LastTime         = Now;

        int WinW = 1, WinH = 1, FbW = 1, FbH = 1;
        glfwGetWindowSize(Window, &WinW, &WinH);
        glfwGetFramebufferSize(Window, &FbW, &FbH);
        if (FbW <= 0 || FbH <= 0)
            continue;

        {
            // [unit] - the interface holds the simulation step and the scene's presentation, so those two nest under this one in the log
            PerformanceScope BuildScope("ui.build");
            Centre->PrepareInterface();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            (void)Centre->Integrate(Dt);
            (void)Workspace.ConfigurePlatform();
            Workspace.ConstructInterface(Dt);
            Centre->ConstructInterface();
            Panel.ConstructInterface();
        }

        {
            PerformanceScope RenderScope("ui.render");
            ImGui::Render();
            GuiPresenter.SyncManagedTextures(VkCtx);
        }

        VkCommandBuffer SwapCmd = VK_NULL_HANDLE;
        VkFramebuffer SwapFbo   = VK_NULL_HANDLE;
        VkExtent2D SwapExtent   = { 0u, 0u };
        bool       SwapBegun    = false;
        {
            PerformanceScope AcquireScope("swapchain.acquire");
            SwapBegun = VkCtx.BeginSwapchainFrame(FbW, FbH, SwapCmd, SwapFbo, SwapExtent);
        }
        if (SwapBegun)
        {
            const int ExtW = static_cast<int>(SwapExtent.width);
            const int ExtH = static_cast<int>(SwapExtent.height);
            {
                PerformanceScope RecordScope("ui.record");
                const std::array<float, 4> ClearCol = { 0.04f, 0.04f, 0.04f, 1.0f };
                VkCtx.BeginTarget(SwapCmd, VkCtx.QueryTargetSwapchainClear(), SwapFbo, ExtW, ExtH, &ClearCol, false);
                GuiPresenter.RecordDrawData(VkCtx, SwapCmd, ImGui::GetDrawData(), ExtW, ExtH);
                VkCtx.EndTarget(SwapCmd);
            }
            {
                PerformanceScope PresentScope("swapchain.present");
                VkCtx.EndAndPresentSwapchainFrame(FbW, FbH);
            }
        }
        Panel.EndRefresh();

        if (!CapturePngPath.empty() && MaxFrames > 0 && FrameCounter + 1 >= MaxFrames)
        {
            VulkanImageTarget OffscreenCap{};
            std::string CapDiag;
            if (VkCtx.CreateImageTarget(FbW, FbH, VK_FORMAT_R8G8B8A8_UNORM,
                                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, false, OffscreenCap, CapDiag))
            {
                VulkanImGuiPresenter CapPresenter{};
                if (CapPresenter.Initialize(VkCtx, VkCtx.QueryTargetSingleRgba8Clear(), CapDiag))
                {
                    CapPresenter.SyncManagedTextures(VkCtx);
                    const VulkanImageTarget* Att[1] = { &OffscreenCap };
                    VkFramebuffer CapFbo = VkCtx.CreateFramebuffer(VkCtx.QueryTargetSingleRgba8Clear(), Att, 1u, FbW, FbH);
                    VkCommandBuffer CapCmd = VkCtx.BeginFrameCommands();
                    VkCtx.TransitionImage(CapCmd, OffscreenCap, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                    const std::array<float, 4> ClearCol = { 0.04f, 0.04f, 0.04f, 1.0f };
                    VkCtx.BeginTarget(CapCmd, VkCtx.QueryTargetSingleRgba8Clear(), CapFbo, FbW, FbH, &ClearCol, false);
                    CapPresenter.RecordDrawData(VkCtx, CapCmd, ImGui::GetDrawData(), FbW, FbH);
                    VkCtx.EndTarget(CapCmd);
                    VkCtx.SubmitAndWaitFrameCommands();

                    std::vector<uint8_t> Pixels;
                    if (VkCtx.DownloadRgba8Image(OffscreenCap, Pixels))
                        WriteRgbaPngFile(CapturePngPath, Pixels.data(), FbW, FbH);
                    VkCtx.DestroyFramebuffer(CapFbo);
                    CapPresenter.Shutdown(VkCtx);
                }
                VkCtx.DestroyImageTarget(OffscreenCap);
            }
        }

        ++FrameCounter;
        if (MaxFrames > 0 && FrameCounter >= MaxFrames)
            break;
    }

    Panel.AbortTest();
    PerformanceLog::Instance().Discard();
    VkCtx.WaitIdle();
    InspectorRuntime::Instance().RegisterPlumeViewportPresenter({});
    InspectorRuntime::Instance().RegisterFluidViewportPresenter({});
    Ownership.reset();
    PlumeScene.Shutdown(VkCtx);
    GuiPresenter.Shutdown(VkCtx);
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    VkCtx.Shutdown();
    glfwDestroyWindow(Window);
    glfwTerminate();
    return 0;
}
