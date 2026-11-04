/*==========================================================================================================================================
                                                      CELESTIALWORKSPACEVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Selected-target cameras, clean scene entries, panel-local chrome, distinct planet cards and real celestial viewport captures.

#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Interface/Inspectors/PlanetSurfacePreview.h"
#include "EverestWorkspace.h"
#include "EverestSceneViewport.h"
#include "Exhibits/ControlCentre/Code/CpuRasterization.h"
#include <imgui_internal.h>
#include <stb_image_write.h>
#include <toml++/toml.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>

namespace
{
using namespace Frontier;
std::ofstream Report;
int Checks = 0;
void Require(bool Success, const std::string &Message)
{
    ++Checks;
    Report << (Success ? "PASS" : "FAIL") << '\t' << Message << '\n';
    Report.flush();
    if (!Success)
        throw std::runtime_error(Message);
}
void ValidateCameras()
{
    using namespace Everest;
    SceneViewportFlight Flight;
    SceneViewportNavigation Navigation;
    SceneCameraSubject Target;
    Target.Available = true;
    Target.Position = {20, 15, 8};
    Target.Radius = 4;
    ConfigureSceneCamera(Flight, Navigation, SceneCameraMode::Orbit, Target, true);
    const double Radius = Flight.PivotDistance;
    SceneViewportPointer Pointer;
    Pointer.Hovered = Pointer.Active = Pointer.MiddleDown = true;
    Pointer.MotionPixels = {25, 10};
    const auto Original = Flight.Position;
    IntegrateSceneCamera(Flight, Navigation, Pointer, Target, .016);
    Require(Flight.Position != Original, "Orbit moves around selected target");
    Require(std::abs(std::hypot(Flight.Position[0] - Target.Position[0], Flight.Position[1] - Target.Position[1],
                                Flight.Position[2] - Target.Position[2]) -
                     Radius) < 1e-6,
            "Orbit retains selected-target distance");
    ConfigureSceneCamera(Flight, Navigation, SceneCameraMode::Chase, Target);
    const auto Before = Flight.Position;
    Target.Position[0] += 5;
    Target.Position[2] += 2;
    IntegrateSceneCamera(Flight, Navigation, {}, Target, .016);
    Require(std::abs(Flight.Position[0] - Before[0] - 5) < 1e-8 && std::abs(Flight.Position[2] - Before[2] - 2) < 1e-8,
            "Chase follows moving selected target");
    const std::array<std::array<double, 3>, 6> Expected = {{{0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}}};
    for (int Index = 0; Index < 6; ++Index)
    {
        ConfigureSceneCamera(Flight, Navigation, SceneCameraMode(int(SceneCameraMode::Top) + Index), Target, true);
        const auto Direction = Flight.ResolveForward();
        double Dot = 0, Up = 0;
        for (size_t Axis = 0; Axis < 3; ++Axis)
        {
            Dot += Direction[Axis] * Expected[Index][Axis];
            Up += Direction[Axis] * Navigation.Up[Axis];
        }
        Require(Dot > .999999 && std::abs(Up) < 1e-6, "Fixed-axis camera has valid direction/up basis");
    }
    ConfigureSceneCamera(Flight, Navigation, SceneCameraMode::Free, Target);
    const auto Scene = Flight.Position;
    ConfigureSceneCamera(Flight, Navigation, SceneCameraMode::Space, {});
    Require(Flight.Position[2] >= 150000, "Space navigation starts outside the atmosphere");
    ConfigureSceneCamera(Flight, Navigation, SceneCameraMode::Free, {});
    Require(Flight.Position == Scene, "Leaving space navigation restores the scene position");
}
} // namespace

int main(int Count, char **Arguments)
{
    using namespace Frontier;
    try
    {
        if (Count != 5)
            throw std::runtime_error("Expected EngineContent, project, shader root, output");
        const auto Content = std::filesystem::absolute(Arguments[1]), Project = std::filesystem::absolute(Arguments[2]);
        const auto Shaders = std::filesystem::absolute(Arguments[3]), Output = std::filesystem::absolute(Arguments[4]);
        std::filesystem::create_directories(Output);
        Report.open(Output / "WorkspaceChecks.tsv");
        ValidateCameras();
        ImGui::CreateContext();
        auto &Io = ImGui::GetIO();
        Io.IniFilename = nullptr;
        Io.DisplaySize = {1440, 900};
        Io.DeltaTime = 1.0f / 30;
        Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
        Io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth = ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;
        IconPresentation::Attach(Content / "Icons");
        {
            const auto Preferences = std::filesystem::current_path() / "_ClaudeScratch/tmp/CelestialPreferences.projectspace";
            ActivationOwnership Ownership(new ControlCentrePanel(Content / "FontArchives", Preferences, Project / "Everest.projectspace"));
            auto &Centre = *static_cast<ControlCentrePanel *>(Ownership.get());
            Require(Centre.Initialize() && Centre.Activate(), "Native UI initialized");
            Everest::EverestWorkspace Workspace(Centre, Project);
            auto &World = Workspace.QuerySceneWorld();
            Require(World.QueryRecordCount() == 67 && World.FindAllByType("Geometry").size() == 37 && World.FindAllByType("ForceSource").size() == 14,
                    "Clean default retains only rendered environment, the one Render settings record, the 37 scene objects and the 14 forces");
            for (const char *Type : {"SpatialNode", "RigidBody", "ThermalBody", "ChemicalVessel", "FluidPlume", "Fracture", "Particles",
                                     "HarmonicOscillator", "Lightning", "AerialFog"})
                Require(World.FindFirstByType(Type) == nullptr, std::string("No unused default ") + Type);
            auto &Pipeline = dynamic_cast<RenderPipelineRecord *>(World.FindFirstByType("RenderPipeline"))->AccessConfiguration();
            Pipeline.HardwareRayQueryTier = false;
            Pipeline.QualityTier = 0;
            Pipeline.GlobalIllumination = false;
            Pipeline.TemporalAntiAliasing = false;
            auto &Sun = dynamic_cast<SolarRecord *>(World.FindFirstByType("Solar"))->AccessConfiguration();
            Sun.Enabled = false;
            Sun.DayCycle = Sun.TimeLinked = false;
            dynamic_cast<AtmosphereRecord *>(World.FindFirstByType("Atmosphere"))->AccessConfiguration().Enabled = false;
            dynamic_cast<StarRecord *>(World.FindFirstByType("Stars"))->AccessConfiguration().Enabled = false;
            for (const auto *Entry : World.FindAllByType("Clouds"))
                dynamic_cast<CloudRecord *>(World.FindRecord(Entry->QueryToken()))->AccessConfiguration().Enabled = false;
            for (const auto *Entry : World.FindAllByType("Fog"))
                dynamic_cast<FogRecord *>(World.FindRecord(Entry->QueryToken()))->AccessConfiguration().Enabled = false;
            auto *MoonEntry = dynamic_cast<MoonRecord *>(World.FindFirstByType("Moons"));
            for (auto &Moon : MoonEntry->AccessConfiguration().Instances)
                Moon.Visible = false;
            auto &Moon = MoonEntry->AccessConfiguration().Instances[0];
            Moon.Visible = true;
            Moon.FollowSky = false;
            Moon.AngularDiameterDegrees = 12;
            Moon.PhaseDays = 14.765f;
            Moon.AzimuthDegrees = -90;
            Moon.ElevationDegrees = 30;
            for (const auto *Entry : World.FindAllByType("Planet"))
                dynamic_cast<PlanetRecord *>(World.FindRecord(Entry->QueryToken()))->AccessConfiguration().Enabled = false;
            Everest::EverestSceneViewport Bridge(Project, Shaders, nullptr);
            constexpr uint64_t ViewTag = 0xCE1E571A;
            std::vector<uint8_t> Pixels;
            std::array<uint32_t, 2> Extent{};
            Workspace.PresentSceneViewport = [&](const Everest::SceneViewportRequest &Request)
            {
                auto Small = Request;
                Small.PanelWidth = 480;
                Small.PanelHeight = 300;
                Small.PixelDensity = 1;
                const auto Result = Bridge.Render(Small, World, Workspace.QueryElapsedSeconds());
                Everest::SceneViewportImage Image;
                Image.Status = Result.Status;
                Image.Readout = Result.Readout;
                Require(Result.View != VK_NULL_HANDLE, "Native viewport: " + Result.Status);
                Require(Bridge.AccessViewport()->ReadPresented(Pixels), "Native presented pixels read back");
                Extent = Result.Extent;
                Image.Texture = ViewTag;
                return Image;
            };
            auto Tick = [&]
            {
                Workspace.IntegrateSimulation(.05);
                Centre.PrepareInterface();
                ImGui::NewFrame();
                (void)Centre.Integrate(1.0 / 30);
                (void)Workspace.ConfigurePlatform();
                Workspace.ConstructInterface(1.0 / 30);
                Centre.ConstructInterface();
                ImGui::Render();
                FrontierExhibit::AcknowledgeTextures();
            };
            auto Capture = [&](const std::filesystem::path &Name)
            {
                const auto *Commands = ImGui::GetDrawData();
                std::vector<uint8_t> Rgb(1440 * 900 * 3, 14);
                const std::function<FrontierExhibit::Image(ImTextureID)> Images = [&](ImTextureID Texture) {
                    return Texture == ViewTag ? FrontierExhibit::Image{Pixels.data(), int(Extent[0]), int(Extent[1]), 4}
                                              : FrontierExhibit::Image{};
                };
                for (const auto *List : Commands->CmdLists)
                    FrontierExhibit::RasterizeCommands(List, Rgb.data(), 1440, 900, Commands->DisplayPos, Commands->FramebufferScale,
                                                       nullptr, &Images);
                Require(stbi_write_png((Output / Name).string().c_str(), 1440, 900, 3, Rgb.data(), 1440 * 3), "Workspace image saved");
            };
            Workspace.QueryOutliner().Select(MoonEntry->QueryToken());
            InspectorRuntime::Instance().RequestSkyFocus(-90, 30, 12);
            Tick();
            Tick();
            Tick();
            Capture("Moon-Native.png");
            Require(Workspace.QueryOutliner().QueryTotal() == 67,
                    "Outliner lists the fifteen rendered scene entries, the one Render settings component, the 37 scene objects and "
                    "the 14 localized forces under the Force Field");
            Require(Centre.QueryQuickActionCount() == 3, "Render settings remain available from the Control Centre too");
            int RenderSettingsRows = 0;
            for (const auto &Row : Workspace.QueryOutliner().QueryRows())
            {
                Require(Row.Name != "Simulation Origin" && Row.Name != "Diagnostics" && Row.Name != "Geometry clusters" && Row.Name != "Surface cache" &&
                            Row.Name != "BVH & distance field",
                        "No dummy or absorbed renderer records in Outliner");
                RenderSettingsRows += Row.Name == "Render settings" ? 1 : 0;
            }
            Require(RenderSettingsRows == 1, "The renderer is one Render settings component in the Outliner");
            Io.AddKeyEvent(ImGuiKey_V, true);
            Tick();
            Io.AddKeyEvent(ImGuiKey_V, false);
            Tick();
            Require(Workspace.AccessViewportOverlays().Fullscreen, "V enables fullscreen viewport");
            auto *Full = ImGui::FindWindowByName("##ViewportFullscreen");
            Require(Full && Full->Size.x >= 1439 && Full->Size.y >= 899, "Fullscreen viewport occupies the entire application client area");
            Capture("Viewport-Fullscreen.png");
            Io.AddKeyEvent(ImGuiKey_Escape, true);
            Tick();
            Io.AddKeyEvent(ImGuiKey_Escape, false);
            Tick();
            Require(!Workspace.AccessViewportOverlays().Fullscreen, "Escape restores the docked workspace");
            for (const auto *Entry : World.FindAllByType("Planet"))
            {
                auto *PlanetEntry = dynamic_cast<PlanetRecord *>(World.FindRecord(Entry->QueryToken()));
                auto &Planet = PlanetEntry->AccessConfiguration();
                MoonEntry->AccessConfiguration().Enabled = false;
                Sun.Enabled = true;
                Sun.ElevationDegrees = 50;
                Sun.AzimuthDegrees = 90;
                Planet.Enabled = true;
                Planet.FollowSky = false;
                Planet.AzimuthDegrees = -90;
                Planet.ElevationDegrees = 30;
                Planet.ApparentDiameterDegrees = 10;
                Workspace.QueryOutliner().Select(Entry->QueryToken());
                InspectorRuntime::Instance().RequestSkyFocus(-90, 30, Planet.RingSystem ? 24 : 10);
                Tick();
                Tick();
                Capture(std::string(PlanetBodyNames[Planet.Body]) + "-Inspector.png");
                const auto Before = GeneratePlanetSurfacePreview(Planet);
                Planet.SpinDegrees += 70;
                Planet.Brightness *= .65f;
                const auto After = GeneratePlanetSurfacePreview(Planet);
                Require(Before != After, std::string(PlanetBodyNames[Planet.Body]) + " live preview responds to edits");
                toml::table Saved;
                PlanetEntry->SerializeConfiguration(Saved);
                Planet.SpinDegrees = 0;
                PlanetEntry->DeserializeConfiguration(Saved);
                Require(Planet.SpinDegrees == 70, "Planet orientation persists through codec");
                Planet.Enabled = false;
            }
            dynamic_cast<AtmosphereRecord *>(World.FindFirstByType("Atmosphere"))->AccessConfiguration().Enabled = true;
            dynamic_cast<StarRecord*>(World.FindFirstByType("Stars"))->AccessConfiguration().Enabled=true;
            for(const auto* Entry:World.FindAllByType("Planet"))
            {
                auto& Planet=dynamic_cast<PlanetRecord*>(World.FindRecord(Entry->QueryToken()))->AccessConfiguration();
                if(Planet.Body==3) {Planet.Enabled=true;Planet.ElevationDegrees=15;}
            }
            Everest::ConfigureSceneCamera(Workspace.AccessViewportFlight(), Workspace.AccessViewportNavigation(),
                                          Everest::SceneCameraMode::Space, {});
            Tick();
            Tick();
            Capture("Space-View.png");
            Require(Workspace.AccessViewportFlight().Position[2] >= 150000, "Space camera is live in the production viewport");
            Everest::ConfigureSceneCamera(Workspace.AccessViewportFlight(), Workspace.AccessViewportNavigation(),
                                          Everest::SceneCameraMode::Free, {});
            // Footer callbacks have independent body children. None is a workspace-wide status window.
            Require(ImGui::FindWindowByName("##EverestStatusStrip") == nullptr, "No shared workspace footer");
            int Bodies = 0;
            for (auto *Window : GImGui->Windows)
                if (std::string(Window->Name).find("##PanelContent") != std::string::npos)
                    ++Bodies;
            Require(Bodies >= 4, "All four panels reserve independent footer space");
            Workspace.PresentSceneViewport = nullptr;
        }
        ImGui::DestroyContext();
        Report << "SUMMARY\t" << Checks << " checks passed\n";
        std::cout << Checks << " workspace checks passed\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << Failure.what() << '\n';
        return 1;
    }
}
