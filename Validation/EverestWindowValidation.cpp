/*==========================================================================================================================================
                                                       EVERESTWINDOWVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 What Everest's window shows when it starts, and the F2 window that measures it:
//
//      ① the levels (no device): the project starts in the Lobby, a level of environment records with no geometry and no force sources and with ONE
//        vehicle (a Vehicle record with its four tyres, possessed, at the middle of the studio), and the Proving Grounds level is still the project's,
//        opened by its identity; an identity the project does not have is refused;
//      ② the Lobby's view (no device): the hero framing of the HTML Lobby (Experimental/VehicleHmiPrototype/Lobby.html). The numbers are read from that
//        demo's own source (the end of its entrance dolly, its orbit camera's target and field of view), so that the eye, the point looked at, the pivot
//        and the field of view are the demo's, and a change to the demo shows up here;
//      ③ the window (device): the real editor on the Lobby shows the one vehicle large (the picture is not the empty studio of a far view), and the
//        simulation is not running;
//      ④ F2: opens and closes the performance window, and Rename on a record is Shift+F2; the switch changes the optimized-execution path at the next
//        refresh; the live figures appear; an A/B test of four runs goes through the real loop (the workspace's step callback, the fixed camera, the
//        scripted drive) and ends with a report whose pictures are the same for both paths and whose TOML logs name the path each one ran.
//
//        EverestWindowValidation [--require-vulkan] [--write-pictures FOLDER]
//
//    Run it from the repository root. On a software device it holds what is shown and what is measured of; nothing here is a timing claim.

#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Lifecycle/ActivationSequence.h"
#include "Frontier/Engine/Diagnostics/PerformanceLog.h"
#include "Frontier/Engine/Diagnostics/PerformancePanel.h"
#include "Frontier/Engine/Rendering/PngImageCodec.h"
#include "Frontier/Engine/Rendering/VulkanContext.h"
#include "Frontier/Engine/VolumetricDynamics/FluidPlumeBridge.h"
#include "Projects/Everest/Source/EverestApplication.h"
#include "Projects/Everest/Source/EverestWorkspace.h"
#include "UI/Editor/ControlCentrePanel.h"

#include <toml++/toml.hpp>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace
{

using namespace Frontier;

int Assertions = 0;
int Failures   = 0;

void Verify(bool Condition, const std::string& Caption)
{
    ++Assertions;
    if (!Condition) ++Failures;
    std::printf("%s\t%s\n", Condition ? "PASS" : "FAIL", Caption.c_str());
}

std::string Spell(double Amount, int Decimals = 2)
{
    char Text[64];
    std::snprintf(Text, sizeof(Text), "%.*f", Decimals, Amount);
    return Text;
}

std::filesystem::path LocateRepositorySubpath(const std::filesystem::path& Rel)
{
    const std::filesystem::path Candidates[] = { Rel, std::filesystem::path("..") / Rel, std::filesystem::path("../..") / Rel };
    for (const auto& Candidate : Candidates)
    {
        if (std::filesystem::exists(Candidate))
            return std::filesystem::absolute(Candidate);
    }
    return std::filesystem::absolute(Rel);
}

size_t CountType(const WorldSpace& World, const char* Type)
{
    return World.FindAllByType(Type).size();
}

bool RenderImGuiFrameOffscreen(VulkanContext& VkCtx, VulkanImGuiPresenter& Presenter, VulkanImageTarget& ColorImg, VkFramebuffer Fbo, int Width, int Height,
                               std::vector<uint8_t>& OutRgba)
{
    Presenter.SyncManagedTextures(VkCtx);
    VkCommandBuffer            Cmd     = VkCtx.BeginFrameCommands();
    VkCtx.TransitionImage(Cmd, ColorImg, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    const std::array<float, 4> ClearBg = { 0.04f, 0.04f, 0.04f, 1.0f };
    VkCtx.BeginTarget(Cmd, VkCtx.QueryTargetSingleRgba8Clear(), Fbo, Width, Height, &ClearBg, false);
    Presenter.RecordDrawData(VkCtx, Cmd, ImGui::GetDrawData(), Width, Height);
    VkCtx.EndTarget(Cmd);
    if (!VkCtx.SubmitAndWaitFrameCommands())
        return false;
    return VkCtx.DownloadRgba8Image(ColorImg, OutRgba);
}

// [-] - the share of pixels whose neighbour to the right differs by 6 grey levels or more: the studio's gradient has almost none, a car in it has many
double ComputeEdgeFraction(const std::vector<uint8_t>& Rgba, int Width, int Height)
{
    if (Width < 2 || Height < 1 || Rgba.size() < static_cast<size_t>(Width) * static_cast<size_t>(Height) * 4u)
        return 0.0;
    size_t Edges = 0u;
    for (int Row = 0; Row < Height; ++Row)
    {
        for (int Column = 0; Column + 1 < Width; ++Column)
        {
            const size_t At   = (static_cast<size_t>(Row) * static_cast<size_t>(Width) + static_cast<size_t>(Column)) * 4u;
            const int    Here = (Rgba[At] + Rgba[At + 1] + Rgba[At + 2]) / 3;
            const int    Next = (Rgba[At + 4] + Rgba[At + 5] + Rgba[At + 6]) / 3;
            if (std::abs(Next - Here) >= 6)
                ++Edges;
        }
    }
    return static_cast<double>(Edges) / (static_cast<double>(Width) * static_cast<double>(Height));
}

//----------------------------------------------------------------------------------------------------------------------
//                                                        THE LEVELS
//----------------------------------------------------------------------------------------------------------------------

void ValidateLevels(const std::filesystem::path& EverestRoot)
{
    const std::filesystem::path Project = EverestRoot / "Everest.projectspace";

    Everest::EverestApplication Started;
    std::string                 Diagnostic;
    const bool                  Loaded = Started.Initialize(Project, &Diagnostic);
    Verify(Loaded, "the project starts (" + Diagnostic + ")");
    const WorldSpace& Lobby = Started.QueryWorld();
    Verify(Lobby.QueryLoadedLevelPath().filename() == "Lobby.levelspace", "the project's startup level is the Lobby (" + Lobby.QueryLoadedLevelPath().filename().string() + ")");
    Verify(CountType(Lobby, "Geometry") == 0 && CountType(Lobby, "ForceSource") == 0,
           "the Lobby holds no geometry and no force sources (" + std::to_string(CountType(Lobby, "Geometry")) + " and " + std::to_string(CountType(Lobby, "ForceSource")) + ")");
    Verify(CountType(Lobby, "RenderPipeline") == 1 && CountType(Lobby, "Solar") == 1 && Lobby.QueryRecordCount() == 21u,
           "it keeps the environment: the Render settings, the Sun, and 21 records in all with the vehicle (" + std::to_string(Lobby.QueryRecordCount()) + ")");
    const auto  Vehicles     = Lobby.FindAllByType("Vehicle");
    const auto* LobbyVehicle = Vehicles.empty() ? nullptr : dynamic_cast<const VehicleRecord*>(Vehicles.front());
    Verify(CountType(Lobby, "Vehicle") == 1u && CountType(Lobby, "Tyre") == 4u && LobbyVehicle != nullptr,
           "the Lobby has exactly one vehicle, with its four tyres (" + std::to_string(CountType(Lobby, "Vehicle")) + " and " + std::to_string(CountType(Lobby, "Tyre")) + ")");
    if (LobbyVehicle != nullptr)
    {
        const auto& Configuration = LobbyVehicle->QueryConfiguration();
        Verify(Configuration.Visible && Configuration.Physics && Configuration.Possessed && Configuration.Spatial.PositionMetres[0] == 0.0f &&
                   Configuration.Spatial.PositionMetres[1] == 0.0f && Configuration.Spatial.OrientationDegrees[2] == 0.0f,
               "it is visible, drivable and possessed, at the middle of the studio and facing +X");
        size_t Fitted = 0u;
        for (const SceneRecordBase* Entry : Lobby.FindAllByType("Tyre"))
            if (Entry->QueryParentToken() == LobbyVehicle->QueryToken())
                ++Fitted;
        Verify(Fitted == 4u, "and the four tyres are its own (" + std::to_string(Fitted) + " of 4 under it)");
    }

    Everest::EverestApplication Proving;
    const bool                  ProvingLoaded = Proving.Initialize(Project, &Diagnostic, "default-level");
    Verify(ProvingLoaded && Proving.QueryWorld().QueryLoadedLevelPath().filename() == "Default.levelspace" && CountType(Proving.QueryWorld(), "Geometry") == 37u,
           "the Proving Grounds level is still the project's, opened by its identity (" + std::to_string(CountType(Proving.QueryWorld(), "Geometry")) + " geometry records)");

    Everest::EverestApplication Missing;
    std::string                 MissingDiagnostic;
    const bool                  MissingLoaded = Missing.Initialize(Project, &MissingDiagnostic, "no-such-level");
    Verify(!MissingLoaded && MissingDiagnostic.find("no-such-level") != std::string::npos,
           "a level the project does not have is refused, and the diagnostic names it (" + MissingDiagnostic + ")");

    // [unit] - the project file itself: the Lobby first, startup_level naming it
    toml::table Document;
    try
    {
        Document = toml::parse_file(Project.string());
    }
    catch (const toml::parse_error&)
    {
    }
    Verify(Document["engine"]["startup_level"].value_or(std::string()) == "lobby" && Document["levels"].as_array() != nullptr && Document["levels"].as_array()->size() == 2u,
           "Everest.projectspace names the Lobby as its startup level, and lists two levels");
}

// [unit] - the whole of a text file; empty when it is not there
std::string ReadText(const std::filesystem::path& Path)
{
    std::ifstream  In(Path, std::ios::binary);
    std::ostringstream Text;
    Text << In.rdbuf();
    return Text.str();
}

void ValidateLobbyView(const std::filesystem::path& HtmlSource)
{
    // [unit] - the demo's own numbers: LobbyEntry.To in IlluminationHost.js, the target and field of view of VehicleOrbitCamera.Resolve, the centre of mass of the car
    const std::string Host  = ReadText(HtmlSource / "IlluminationHost.js");
    const std::string Orbit = ReadText(HtmlSource / "VehicleOrbitCamera.js");
    const std::string Spec  = ReadText(HtmlSource / "VehicleSpecification.js");
    std::smatch       Entry, Raise, Zoom, Centre;
    const bool        Found = std::regex_search(Host, Entry, std::regex(R"(To:\s*\{\s*Yaw:\s*([0-9.]+),\s*Pitch:\s*([0-9.]+),\s*Distance:\s*([0-9.]+)\s*\})")) &&
                       std::regex_search(Orbit, Raise, std::regex(R"(Target\s*=\s*\[Position\[0\],\s*Position\[1\],\s*Position\[2\]\s*\+\s*([0-9.]+)\])")) &&
                       std::regex_search(Orbit, Zoom, std::regex(R"(Fov:\s*([0-9.]+)\s*\})")) &&
                       std::regex_search(Spec, Centre, std::regex(R"(CoMHeight:\s*([0-9.]+))"));
    Verify(Found, "the HTML Lobby's hero framing is found in its source (" + HtmlSource.string() + ")");
    if (!Found)
        return;
    const double Yaw = std::stod(Entry[1]), Pitch = std::stod(Entry[2]), Out = std::stod(Entry[3]);
    const double Lift = std::stod(Raise[1]), Fov = std::stod(Zoom[1]), Com = std::stod(Centre[1]);
    // [unit] - VehicleOrbitCamera.Resolve for a car at the origin whose centre of mass is Com above the floor
    const std::array<double, 3> Target = { 0.0, 0.0, Com + Lift };
    const double                Reach  = Out * std::cos(Pitch);
    const std::array<double, 3> Eye    = { Target[0] + Reach * std::cos(Yaw), Target[1] + Reach * std::sin(Yaw), Target[2] + Out * std::sin(Pitch) };

    const Everest::SceneViewportFlight View = Everest::SceneViewportFlight::ConstructLobbyView();
    const std::array<double, 3>        Gaze = View.ResolveTarget();
    const double EyeOff    = std::sqrt((View.Position[0] - Eye[0]) * (View.Position[0] - Eye[0]) + (View.Position[1] - Eye[1]) * (View.Position[1] - Eye[1]) +
                                       (View.Position[2] - Eye[2]) * (View.Position[2] - Eye[2]));
    const double TargetOff = std::sqrt((Gaze[0] - Target[0]) * (Gaze[0] - Target[0]) + (Gaze[1] - Target[1]) * (Gaze[1] - Target[1]) + (Gaze[2] - Target[2]) * (Gaze[2] - Target[2]));
    Verify(EyeOff < 1.0e-9, "the Lobby's eye is where the HTML Lobby puts it: yaw " + Spell(Yaw, 2) + " rad, pitch " + Spell(Pitch, 2) + " rad, " + Spell(Out, 1) + " m out (" +
                                Spell(EyeOff, 12) + " m apart)");
    Verify(TargetOff < 1.0e-9 && std::fabs(View.PivotDistance - Out) < 1.0e-9,
           "it looks at the same point, the car's centre of mass (" + Spell(Com, 2) + " m) raised " + Spell(Lift, 2) + " m, and pivots on it (" + Spell(TargetOff, 12) + " m apart)");
    Verify(std::fabs(View.ResolveFieldOfView() - Fov * 3.14159265358979323846 / 180.0) < 1.0e-9, "with the same " + Spell(Fov, 0) + " degrees of vertical field of view");
    Verify(std::fabs(View.Elevation + Pitch * 180.0 / 3.14159265358979323846) < 1.0e-9 && View.Position[0] > 0.0 && View.Position[1] > 0.0,
           "from the car's front and left, above the floor (elevation " + Spell(View.Elevation, 2) + " degrees)");
    // [unit] - the vehicle: the demo's host builds its scene from its own Archive folder, and that folder holds the three ControlVehicle archives the window draws
    const std::filesystem::path Archive = HtmlSource.parent_path() / "Archive";
    Verify(Host.find("ConstructScene(\"Archive\"") != std::string::npos && std::filesystem::exists(Archive / "ControlVehicleBody.facet") &&
               std::filesystem::exists(Archive / "ControlVehicleTyre.facet") && std::filesystem::exists(Archive / "ControlVehicleRim.facet"),
           "the vehicle is the HTML Lobby's: its host builds the scene from its Archive folder, which holds the Body, Tyre and Rim archives the window draws");
    const Everest::SceneViewportFlight Far = Everest::SceneViewportFlight{};
    Verify(std::fabs(Far.Position[0] - View.Position[0]) > 20.0, "the default view of the Proving Grounds is the far one, and is left as it was");
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    bool                  RequireVulkan = false;
    std::filesystem::path PictureDirectory;
    for (int Index = 1; Index < ArgumentCount; ++Index)
    {
        if (std::strcmp(Arguments[Index], "--require-vulkan") == 0) RequireVulkan = true;
        else if (std::strcmp(Arguments[Index], "--write-pictures") == 0 && Index + 1 < ArgumentCount)
        {
            PictureDirectory = Arguments[++Index];
            RequireVulkan    = true;
            std::filesystem::create_directories(PictureDirectory);
        }
    }

    const std::filesystem::path EverestRoot = LocateRepositorySubpath("Projects/Everest");
    ValidateLevels(EverestRoot);
    ValidateLobbyView(LocateRepositorySubpath("Experimental/VehicleHmiPrototype/Source"));

    VulkanContext VkCtx;
    std::string   Diagnostic;
    if (!VkCtx.InitializeHeadless(Diagnostic))
    {
        if (RequireVulkan) Verify(false, "a Vulkan 1.1 device comes up (" + Diagnostic + ")");
        else std::printf("INFO\tno Vulkan device (%s): the window's half is skipped\n", Diagnostic.c_str());
        std::printf("[EverestWindowValidation] %d assertions, %d failed\n", Assertions, Failures);
        return Failures == 0 ? 0 : 1;
    }
    std::printf("INFO\tVulkan 1.1 on %s\n", VkCtx.QueryDeviceSummary().c_str());

    const std::filesystem::path ContentDir = LocateRepositorySubpath("EngineContent");
    const std::filesystem::path ArchiveDir = LocateRepositorySubpath("Experimental/VehicleHmiPrototype/Archive");
    if (!std::filesystem::exists(ArchiveDir / "ControlVehicleBody.facet") || !std::filesystem::exists(ContentDir / "FontArchives"))
    {
        Verify(false, "the vehicle archive and the fonts are reachable: run from the repository root");
        std::printf("[EverestWindowValidation] %d assertions, %d failed\n", Assertions, Failures);
        return 1;
    }

    EverestPlumeSceneRuntime PlumeScene{};
    Verify(PlumeScene.Initialize(VkCtx, ArchiveDir, Diagnostic), "the scene runtime starts (" + Diagnostic + ")");
    PlumeScene.ConfigureProvingGroundsMode(false);   // [unit] - as the window starts: the Lobby's studio

    // [unit] - the window is 480 x 270 for everything that is measured (a software device pays for every pixel); a picture, when asked for, is 960 x 540 for a
    //          few refreshes and the window goes back
    const bool Pictures = !PictureDirectory.empty();
    const int  CaptureW = Pictures ? 960 : 480;
    const int  CaptureH = Pictures ? 540 : 270;
    int        WinW     = 480;
    int        WinH     = 270;

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
    Io.DisplaySize = { static_cast<float>(WinW), static_cast<float>(WinH) };

    VulkanImGuiPresenter GuiPresenter{};
    Verify(GuiPresenter.Initialize(VkCtx, VkCtx.QueryTargetSingleRgba8Clear(), Diagnostic), "the interface presenter starts");
    VulkanImageTarget ProofColor{};
    Verify(VkCtx.CreateImageTarget(CaptureW, CaptureH, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, false, ProofColor, Diagnostic), "the offscreen target is made");
    const VulkanImageTarget* ProofAttachments[1] = { &ProofColor };
    VkFramebuffer            ProofFbo            = VkCtx.CreateFramebuffer(VkCtx.QueryTargetSingleRgba8Clear(), ProofAttachments, 1u, CaptureW, CaptureH);

    const std::filesystem::path PreferencesPath = std::filesystem::temp_directory_path() / "FrontierEverestWindowPersonal.projectspace";
    std::filesystem::remove(PreferencesPath);
    ActivationOwnership Ownership(new ControlCentrePanel(ContentDir / "FontArchives", PreferencesPath, EverestRoot / "Everest.projectspace"));
    auto*               Centre = static_cast<ControlCentrePanel*>(Ownership.get());
    Verify(Centre->Initialize() && Centre->Activate(), "the control centre starts");

    {
        // [unit] - the window as EverestGlfwMain builds it: the project's own startup level, the Lobby's view, the step callback through the panel
        Everest::EverestWorkspace Workspace(*Centre, EverestRoot);
        WorldSpace&               World = Workspace.QueryWorldSpace();
        Verify(Workspace.QueryActiveLevelPath().filename() == "Lobby.levelspace" && World.QueryRecordCount() >= 21u,
               "the editor opens the Lobby (" + Workspace.QueryActiveLevelPath().filename().string() + ", " + std::to_string(World.QueryRecordCount()) + " records)");
        Workspace.AccessViewportFlight() = Everest::SceneViewportFlight::ConstructLobbyView();
        PlumeScene.ResetWorldSimulation(VkCtx, World);

        Everest::SceneViewportFlight TestFlight{};
        Everest::SceneViewportFlight SavedFlight{};
        bool                         CameraHeld = false;
        PerformancePanel::WorkspaceHooks Hooks{};
        Hooks.QuerySimulationActive     = [&]() { return Workspace.QuerySimulationActive(); };
        Hooks.ConfigureSimulationActive = [&](bool Active) { Workspace.ConfigureSimulationActive(Active); };
        Hooks.ConfigureTestCamera       = [&]() {
            if (!CameraHeld)
            {
                SavedFlight = Workspace.AccessViewportFlight();
                CameraHeld  = true;
            }
            Workspace.ConfigureViewportCameraHeld(true);   // [unit] - the Lobby's vehicle has a chase camera: it must not take the view from the test
            Workspace.AccessViewportFlight() = TestFlight;
        };
        Hooks.RestoreCamera = [&]() {
            if (CameraHeld)
                Workspace.AccessViewportFlight() = SavedFlight;
            CameraHeld = false;
            Workspace.ConfigureViewportCameraHeld(false);
        };
        PerformancePanel Panel(VkCtx, PlumeScene, Hooks);

        Workspace.PresentSceneViewport = [&](const SceneViewportRequest& Req) -> SceneViewportImage {
            return PlumeScene.PresentSceneViewport(VkCtx, Workspace.QueryWorldSpace(), Req);
        };
        Workspace.StepSimulationCallback = [&](WorldSpace& W, double DtSec, const Everest::EverestWorkspace::SimulationStepDriver& Drv) {
            PerformancePanel::SimulationStep Step{ DtSec, Drv.Throttle, Drv.Brake, Drv.Steer, Drv.Handbrake, Drv.ResetRequested };
            Panel.ResolveSimulationStep(Step);
            PlumeScene.StepWorldSimulation(VkCtx, W, Step.DeltaSeconds, Step.Throttle, Step.Brake, Step.Steer, Step.Handbrake > 0.5f, Step.ResetRequested);
        };
        InspectorRuntime::Instance().RegisterPlumeViewportPresenter([&](const PlumeViewportRequest& Req) -> PlumeViewportImage { return PlumeScene.PresentPlumeViewport(VkCtx, Req); });
        InspectorRuntime::Instance().RegisterFluidViewportPresenter([&](const FluidViewportRequest& Req) -> FluidViewportImage { return PlumeScene.PresentFluidViewport(VkCtx, Req); });

        // [unit] - one pass of the window's loop, in the order EverestGlfwMain runs it
        auto Refresh = [&](std::vector<uint8_t>* OutPicture) {
            Panel.BeginRefresh();
            const double Dt = Panel.QueryTestRunning() ? 1.0 / 60.0 : 1.0 / 30.0;
            Io.DisplaySize  = { static_cast<float>(WinW), static_cast<float>(WinH) };
            Io.DeltaTime    = static_cast<float>(Dt);
            {
                PerformanceScope BuildScope("ui.build");
                Centre->PrepareInterface();
                ImGui::NewFrame();
                (void)Centre->Integrate(Dt);
                (void)Workspace.ConfigurePlatform();
                Workspace.ConstructInterface(Dt);
                Centre->ConstructInterface();
                Panel.ConstructInterface();
            }
            ImGui::Render();
            if (OutPicture != nullptr)
                (void)RenderImGuiFrameOffscreen(VkCtx, GuiPresenter, ProofColor, ProofFbo, CaptureW, CaptureH, *OutPicture);
            else
                GuiPresenter.SyncManagedTextures(VkCtx);
            Panel.EndRefresh();
        };
        auto Press = [&](ImGuiKey Key, bool Shift) {
            if (Shift) Io.AddKeyEvent(ImGuiMod_Shift, true);
            Io.AddKeyEvent(Key, true);
            Refresh(nullptr);
            Io.AddKeyEvent(Key, false);
            if (Shift) Io.AddKeyEvent(ImGuiMod_Shift, false);
            Refresh(nullptr);
        };
        // [unit] - a picture of the window: it is made at the capture size (a few refreshes to settle the layout and the scene), then the window goes back
        auto Capture = [&](const char* Name) {
            if (!Pictures)
                return;
            WinW = CaptureW;
            WinH = CaptureH;
            Panel.ResetGeometry();
            Centre->RequestLayoutReset();   // [unit] - the docks lay themselves out again for the new size
            for (int Settle = 0; Settle < 3; ++Settle)
                Refresh(nullptr);
            std::vector<uint8_t> Window;
            Refresh(&Window);
            if (!Window.empty())
                WriteRgbaPngFile((PictureDirectory / Name).string(), Window.data(), CaptureW, CaptureH);
            WinW = 480;
            WinH = 270;
            Centre->RequestLayoutReset();
            for (int Settle = 0; Settle < 3; ++Settle)
                Refresh(nullptr);
        };

        for (int Warm = 0; Warm < 8; ++Warm)
            Refresh(nullptr);

        // ---- ③ the Lobby on screen
        {
            std::vector<uint8_t> Scene;
            int                  Width = 0, Height = 0;
            const bool           Read     = PlumeScene.ReadSceneColor(VkCtx, Scene, Width, Height);
            const double         Edges    = Read ? ComputeEdgeFraction(Scene, Width, Height) : 0.0;
            Verify(Read && Edges >= 0.004, "the scene viewport shows the vehicle large: " + Spell(Edges * 100.0, 2) + " % of its pixels are an edge (the far view of the studio has 0.03 %)");
            Verify(!Workspace.QuerySimulationActive() && !PlumeScene.IsProvingGroundsMode(), "the simulation is not running, and the scene is the Lobby's studio");
            Capture("lobby-start.png");
        }

        // ---- ④ F2
        Verify(!Panel.QueryOpen(), "the performance window is closed when the editor starts");
        const RecordToken Sun = World.FindFirstByType("Solar") != nullptr ? World.FindFirstByType("Solar")->QueryToken() : RecordToken{};
        Workspace.QueryOutliner().Select(Sun);
        Press(ImGuiKey_F2, false);
        Verify(Panel.QueryOpen(), "F2 opens the performance window");
        Verify(!Workspace.QueryOutliner().QueryRenaming().IsValid(), "and does not rename the selected record");
        Press(ImGuiKey_F2, false);
        Verify(!Panel.QueryOpen(), "F2 again closes it");
        Press(ImGuiKey_F2, false);
        Verify(Panel.QueryOpen(), "and opens it again");

        // the switch: asked for in the window, applied at the start of the next refresh
        Verify(!OptimizedExecution::QueryEnabled() || std::getenv("FRONTIER_OPTIMIZED_EXECUTION") != nullptr, "the optimized path is off when the editor starts");
        OptimizedExecution::Configure(false);
        Panel.RequestOptimized(true);
        Verify(!OptimizedExecution::QueryEnabled(), "the switch asked for does not act in the middle of a refresh");
        Refresh(nullptr);
        Verify(OptimizedExecution::QueryEnabled(), "it acts at the start of the next one");
        for (int Live = 0; Live < 40 && Panel.QueryLivePeriod().Refreshes < 8u; ++Live)
            Refresh(nullptr);
        Verify(Panel.QueryLiveRecording() && Panel.QueryLivePeriod().Refreshes >= 8u && Panel.QueryLivePeriod().Mean > 0.0,
               "the live figures appear with the window open (" + std::to_string(Panel.QueryLivePeriod().Refreshes) + " refreshes, mean " + Spell(Panel.QueryLivePeriod().Mean, 1) + " ms)");
        Panel.RequestOptimized(false);
        Refresh(nullptr);
        Verify(!OptimizedExecution::QueryEnabled() && Panel.QueryLiveRecording() && Panel.QueryLivePeriod().Refreshes == 0u,
               "turned off again, and the live figures start afresh for the other path");

        // ---- the A/B test through the loop
        PerformanceTestSettings Settings{};
        Settings.Rounds            = 2;
        Settings.WarmupRefreshes   = 3;
        Settings.MeasuredRefreshes = 10;
        Settings.OutputDirectory   = (std::filesystem::temp_directory_path() / "FrontierEverestWindowLogs").string();
        std::error_code Ignored;
        std::filesystem::remove_all(Settings.OutputDirectory, Ignored);
        Panel.ConfigureSettings(Settings);
        const Everest::SceneViewportFlight BeforeTest = Workspace.AccessViewportFlight();
        const bool                         ModeBefore = OptimizedExecution::QueryEnabled();
        Panel.StartTest();
        Verify(Panel.QueryTestRunning() && Workspace.QuerySimulationActive(), "the test starts, and switches the simulation on");
        int Passes = 0;
        for (; Passes < 400 && Panel.QueryTestRunning(); ++Passes)
        {
            if (Passes == 20)
            {
                // [unit] - mid-test the held camera is the test's, not the person's
                const auto& Held = Workspace.AccessViewportFlight();
                Verify(std::fabs(Held.Position[0] - TestFlight.Position[0]) < 1.0e-9 && std::fabs(Held.Position[0] - BeforeTest.Position[0]) > 20.0, "while the test runs the camera is held at the test's place");
            }
            Refresh(nullptr);
        }
        const PerformanceTestResult&     Result     = Panel.QueryResult();
        const PerformanceComparison&     Comparison = Panel.QueryComparison();
        Verify(!Panel.QueryTestRunning() && Result.Finished && Result.Phases.size() == 4u, "the test ends by itself with four runs (" + std::to_string(Result.Phases.size()) + " in " + std::to_string(Passes) + " refreshes)");
        Verify(OptimizedExecution::QueryEnabled() == ModeBefore && !Workspace.QuerySimulationActive(), "it puts the switch and the simulation back as they were");
        const auto& After = Workspace.AccessViewportFlight();
        Verify(std::fabs(After.Position[0] - BeforeTest.Position[0]) < 1.0e-9 && std::fabs(After.Heading - BeforeTest.Heading) < 1.0e-9, "and the camera where the person had it");

        bool OrderKnown = Result.Phases.size() == 4u && !Result.Phases[0].Optimized && Result.Phases[1].Optimized && Result.Phases[2].Optimized && !Result.Phases[3].Optimized;
        Verify(OrderKnown, "the order is baseline, optimized, optimized, baseline");
        Verify(Comparison.Valid, "every run drew the scene and stepped the simulation (" + (Comparison.Valid ? std::string("valid") : Comparison.InvalidReason) + ")");
        // [unit] - as in PerformanceParityValidation: pictures are only judged against a baseline that repeats itself
        if (Comparison.PicturesCompared && !Comparison.PicturesIdentical && !Comparison.BaselineRepeats && !RequireVulkan)
            std::printf("INFO\tthe baseline does not repeat itself on this device: the pictures of the paths are not judged here\n");
        else
            Verify(Comparison.PicturesCompared && Comparison.PicturesIdentical && Comparison.PicturesChecked >= 12,
                   "the pictures of the four runs are the same at every checkpoint (" + std::to_string(Comparison.PicturesChecked) + " comparisons, " + (Comparison.PicturesIdentical ? "identical" : "DIFFERENT") + ")");
        double BaselineSubmissions = 0.0, OptimizedSubmissions = 0.0;
        for (const PerformanceComparisonRow& Row : Comparison.Rows)
        {
            if (Row.Caption == "Submissions per refresh")
            {
                BaselineSubmissions  = Row.Baseline;
                OptimizedSubmissions = Row.Optimized;
            }
        }
        Verify(BaselineSubmissions >= 2.0 && OptimizedSubmissions > 0.0 && OptimizedSubmissions < BaselineSubmissions,
               "the optimized path submits to the device less often (" + Spell(OptimizedSubmissions, 2) + " a refresh against " + Spell(BaselineSubmissions, 2) + ")");
        // the CPU's time is its work and its waiting: the two rows add up to the refresh period (to a tenth: the gaps between stretches are not in either)
        for (int Path = 0; Path < 2; ++Path)
        {
            double Period = 0.0, Work = 0.0, Waiting = 0.0;
            for (const PerformanceComparisonRow& Row : Comparison.Rows)
            {
                const double Value = Path == 0 ? Row.Baseline : Row.Optimized;
                if (Row.Caption == "Refresh period, mean") Period = Value;
                else if (Row.Caption == "CPU working (not waiting)") Work = Value;
                else if (Row.Caption == "CPU waiting for the device or display") Waiting = Value;
            }
            Verify(Period > 0.0 && std::fabs(Work + Waiting - Period) <= 0.10 * Period,
                   std::string(Path == 0 ? "baseline" : "optimized") + ": the CPU works " + Spell(Work, 1) + " ms and waits " + Spell(Waiting, 1) + " ms of a " + Spell(Period, 1) + " ms refresh");
        }
        Verify(!Comparison.Verdict.empty() && Result.ReportText.find("Verdict") != std::string::npos && Result.ReportText.find("Every series") != std::string::npos && !Result.ReportPath.empty() &&
                   std::filesystem::exists(Result.ReportPath),
               "the report has its verdict and its series, and is on disk");

        // each run's log names the path it ran and the scene
        size_t Named = 0u;
        for (const PerformancePhaseResult& Phase : Result.Phases)
        {
            try
            {
                const toml::table Log = toml::parse_file(Phase.LogPath);
                const std::string Mode = Log["run"]["mode"].value_or(std::string());
                if (Mode == (Phase.Optimized ? "optimized" : "baseline") && Log["environment"]["scene"].value_or(std::string()) == "Lobby" &&
                    Log["run"]["benchmark"].value_or(false) && Log["run"]["refreshes_measured"].value_or(int64_t(0)) >= 4)
                    ++Named;
            }
            catch (const toml::parse_error&)
            {
            }
        }
        Verify(Named == Result.Phases.size() && !Result.Phases.empty(), "each of the runs' TOML logs names its path, the Lobby, and its measured refreshes (" + std::to_string(Named) + " of " + std::to_string(Result.Phases.size()) + ")");

        // the window with the result, and a picture of it
        Refresh(nullptr);
        Capture("performance-window.png");
        if (!Result.ReportText.empty())
            std::printf("--- report ---\n%s--- end of report ---\n", Result.ReportText.c_str());

        // ---- Rename keeps its key: Shift+F2 (last, as a rename takes the keyboard)
        const bool OpenBefore = Panel.QueryOpen();
        Workspace.QueryOutliner().Select(Sun);
        Press(ImGuiKey_F2, true);
        Verify(Panel.QueryOpen() == OpenBefore && Workspace.QueryOutliner().QueryRenaming().IsValid(), "Shift+F2 renames the selected record and leaves the performance window as it was");

        InspectorRuntime::Instance().RegisterPlumeViewportPresenter({});
        InspectorRuntime::Instance().RegisterFluidViewportPresenter({});
        std::filesystem::remove_all(Settings.OutputDirectory, Ignored);
    }

    VkCtx.WaitIdle();
    Ownership.reset();
    PlumeScene.Shutdown(VkCtx);
    GuiPresenter.Shutdown(VkCtx);
    ImGui::DestroyContext();
    VkCtx.Shutdown();

    std::printf("[EverestWindowValidation] %d assertions, %d failed\n", Assertions, Failures);
    return Failures == 0 ? 0 : 1;
}
