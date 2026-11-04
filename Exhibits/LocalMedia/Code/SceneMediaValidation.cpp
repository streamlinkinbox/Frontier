/*==========================================================================================================================================
                                                          SCENEMEDIAVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Real Everest records, viewport, bake acknowledgement and native UI captures; validates frozen inputs, receiver motion and 3D vectors.

#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Interface/Inspectors/AtmosphereBakeImages.h"
#include "Engine/Scene/EnvironmentDiagnostics.h"
#include "EverestSceneViewport.h"
#include "EverestWorkspace.h"
#include "Exhibits/ControlCentre/Code/CpuRasterization.h"
#include "UI/Editor/ControlCentrePanel.h"

#include <imgui.h>
#include <stb_image_write.h>
#include <toml++/toml.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
using namespace Frontier;
constexpr int      CaptureWidth  = 1440;
constexpr int      CaptureHeight = 900;
constexpr uint64_t SceneTexture  = 0x10CA1F09;
std::ofstream      Report;
int                Checks = 0;

void Require(bool Condition, const std::string &Name)
{
    Report << (Condition ? "PASS" : "FAIL") << '\t' << Name << '\n';
    Report.flush();
    ++Checks;
    if (!Condition) throw std::runtime_error(Name);
}

std::array<double, 3> Offset(const EnvironmentRenderReadout &Readout, uint64_t Token)
{
    for (const auto &Receiver : Readout.ReceiverMotion)
        if (Receiver.Token == Token) return Receiver.DisplacementMetres;
    throw std::runtime_error("Missing receiver displacement");
}

double LookupDifference(const std::vector<float> &First, const std::vector<float> &Second)
{
    Require(First.size() == Second.size() && !First.empty(), "GPU lookup extents agree");
    double Sum = 0;
    for (size_t Channel = 0; Channel < First.size(); ++Channel)
        Sum += std::abs(double(First[Channel]) - Second[Channel]);
    return Sum / First.size();
}

void WriteLookup(const std::filesystem::path &Path, const std::vector<float> &Pixels, int Width, int Height, bool Transmission)
{
    const auto Rgba = ResolveLookupPixels(Pixels, Transmission);
    Require(Rgba.size() == size_t(Width) * Height * 4, "Decoded GPU lookup size");
    Require(stbi_write_png(Path.string().c_str(), Width, Height, 4, Rgba.data(), Width * 4), "GPU lookup PNG written");
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    EDITOR AND GPU VERIFICATION
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char **Arguments)
{
    using namespace Frontier;
    try
    {
        Require(ArgumentCount == 5, "Expected EngineContent, project, shader root and output");
        const auto Content = std::filesystem::absolute(Arguments[1]);
        const auto Project = std::filesystem::absolute(Arguments[2]);
        const auto Shaders = std::filesystem::absolute(Arguments[3]);
        const auto Output  = std::filesystem::absolute(Arguments[4]);
        std::filesystem::create_directories(Output);
        const auto Scratch = std::filesystem::current_path() / "_ClaudeScratch/tmp/SceneMediaValidation";
        std::filesystem::create_directories(Scratch);
        Report.open(Output / "Checks.tsv");
        ImGui::CreateContext();
        auto &Io       = ImGui::GetIO();
        Io.DisplaySize = {CaptureWidth, CaptureHeight};
        Io.DeltaTime   = 1.0f / 30;
        Io.IniFilename = nullptr;
        Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
        Io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth = ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;
        IconPresentation::Attach(Content / "Icons");
        {
            ActivationOwnership Ownership(
                new ControlCentrePanel(Content / "FontArchives", Scratch / "Personal.projectspace", Project / "Everest.projectspace"));
            auto &Centre = *static_cast<ControlCentrePanel *>(Ownership.get());
            Require(Centre.Initialize() && Centre.Activate(), "Control Centre initialized");
            Everest::EverestWorkspace Workspace(Centre, Project);
            auto                     &World = Workspace.QuerySceneWorld();
            Require(World.QueryRecordCount() == 53, "Default level includes Force Field, local media and the 37 scene objects");
            auto *FieldEntry      = dynamic_cast<ForceFieldRecord *>(World.FindFirstByType("ForceField"));
            auto *WindEntry       = dynamic_cast<WindRecord *>(World.FindFirstByType("Wind"));
            auto *AtmosphereEntry = dynamic_cast<AtmosphereRecord *>(World.FindFirstByType("Atmosphere"));
            Require(FieldEntry && WindEntry && AtmosphereEntry, "Field, Wind and Atmosphere records exist");
            auto &Field               = FieldEntry->AccessConfiguration();
            auto &Wind                = WindEntry->AccessConfiguration();
            auto &Atmosphere          = AtmosphereEntry->AccessConfiguration();
            Field.Visualize           = true;
            Field.IncludeTurbulence   = false;
            Field.GridResolution      = 3;
            Wind.DirectionDegrees     = 90;
            Wind.SpeedMetresPerSecond = 4;
            Wind.GustStrength = Wind.CurlNoiseTurbulence = Wind.ShearPerKilometre = Wind.VeerDegreesPerKilometre = 0;
            auto &Pipeline       = dynamic_cast<RenderPipelineRecord *>(World.FindFirstByType("RenderPipeline"))->AccessConfiguration();
            Pipeline.QualityTier = 0;
            Pipeline.HardwareRayQueryTier = false;
            Pipeline.TemporalAntiAliasing = false;
            Pipeline.GlobalIllumination   = false;
            auto &Sun                     = dynamic_cast<SolarRecord *>(World.FindFirstByType("Solar"))->AccessConfiguration();
            Sun.TimeLinked = Sun.DayCycle = false;
            Sun.ElevationDegrees          = 30;
            CloudRecord *LocalCloud       = nullptr;
            FogRecord   *LocalFog         = nullptr;
            for (const auto *Entry : World.FindAllByType("Clouds"))
            {
                auto *Cloud = dynamic_cast<CloudRecord *>(World.FindRecord(Entry->QueryToken()));
                if (Cloud->QueryConfiguration().LocalVolume)
                    LocalCloud = Cloud;
                else
                {
                    Cloud->AccessConfiguration().Enabled           = false;
                    Cloud->AccessConfiguration().ResolutionPercent = 50;
                }
            }
            for (const auto *Entry : World.FindAllByType("Fog"))
            {
                auto *Fog = dynamic_cast<FogRecord *>(World.FindRecord(Entry->QueryToken()));
                if (Fog->QueryConfiguration().LocalVolume)
                    LocalFog = Fog;
                else
                    Fog->AccessConfiguration().Enabled = false;
            }
            Require(LocalCloud && LocalFog, "Both local media records exist");
            auto &Cloud   = LocalCloud->AccessConfiguration();
            auto &Fog     = LocalFog->AccessConfiguration();
            Cloud.Enabled = Fog.Enabled = true;
            Cloud.Coverage              = .95f;
            Cloud.ExtinctionDensity     = 1.5f;
            Cloud.LocalFeatureMetres    = 6;
            Cloud.EdgeErosion           = .05f;
            Cloud.LocalSamples          = 32;
            Fog.LocalDensity            = .12f;
            Fog.NoiseScaleMetres        = 3;
            Fog.Heterogeneity           = 1;
            Fog.LocalSamples            = 32;
            Fog.IndependentWindMps      = 0;
            const auto Forward          = Workspace.AccessViewportFlight().ResolveForward();
            const auto Eye              = Workspace.AccessViewportFlight().Position;
            for (size_t Axis = 0; Axis < 3; ++Axis)
            {
                Fog.CentreMetres[Axis]        = float(Eye[Axis] + Forward[Axis] * 30);
                Cloud.CentreMetres[Axis]      = float(Eye[Axis] + Forward[Axis] * 45);
                Fog.HalfExtentsMetres[Axis]   = Axis == 2 ? 5 : 10;
                Cloud.HalfExtentsMetres[Axis] = Axis == 2 ? 6 : 8;
                Field.CentreMetres[Axis]      = Fog.CentreMetres[Axis];
                Field.HalfExtentsMetres[Axis] = Axis == 2 ? 5 : 10;
            }
            Cloud.CentreMetres[2] = 14;
            const auto Snapshot   = Everest::ResolveSceneEnvironment(World, 0);
            Require(Snapshot.LocalClouds.size() == 1 && Snapshot.LocalFogVolumes.size() == 1,
                    "Viewport snapshot includes both local record sets");
            const auto ExtraCloud = World.ConstructRecord("LocalCloud", "Additional cloud");
            const auto ExtraFog   = World.ConstructRecord("LocalFog", "Additional fog");
            Require(dynamic_cast<CloudRecord *>(World.FindRecord(ExtraCloud))->QueryConfiguration().LocalVolume &&
                        dynamic_cast<FogRecord *>(World.FindRecord(ExtraFog))->QueryConfiguration().LocalVolume,
                    "Create menu entries construct bounded media");
            World.DestroyRecord(ExtraCloud);
            World.DestroyRecord(ExtraFog);
            Everest::EverestSceneViewport                   Bridge(Project, Shaders, nullptr);
            std::vector<uint8_t>                            ViewportPixels;
            std::array<uint32_t, 2>                         ViewportExtent{};
            std::shared_ptr<const EnvironmentRenderReadout> Latest;
            Workspace.PresentSceneViewport = [&](const Everest::SceneViewportRequest &Request)
            {
                auto Small                           = Request;
                Small.PanelWidth                     = 320;
                Small.PanelHeight                    = 200;
                Small.PixelDensity                   = 1;
                const auto                  Rendered = Bridge.Render(Small, World, Workspace.QueryElapsedSeconds());
                Everest::SceneViewportImage Image;
                Image.Status  = Rendered.Status;
                Image.Readout = Rendered.Readout;
                Require(Rendered.View != VK_NULL_HANDLE, "Native viewport renders: " + Rendered.Status);
                Require(Bridge.AccessViewport()->ReadPresented(ViewportPixels), "Native scene readback");
                ViewportExtent = Rendered.Extent;
                Image.Texture  = SceneTexture;
                Latest         = Rendered.Readout.Environment;
                return Image;
            };
            auto Tick = [&]
            {
                Workspace.IntegrateSimulation(.25);
                Centre.PrepareInterface();
                ImGui::NewFrame();
                (void)Centre.Integrate(1.0 / 30);
                (void)Workspace.ConfigurePlatform();
                Workspace.ConstructInterface(1.0 / 30);
                Centre.ConstructInterface();
                ImGui::Render();
                FrontierExhibit::AcknowledgeTextures();
            };
            auto Capture = [&](const char *Name)
            {
                const auto                                              *Commands = ImGui::GetDrawData();
                std::vector<uint8_t>                                     Pixels(CaptureWidth * CaptureHeight * 3, 14);
                const std::function<FrontierExhibit::Image(ImTextureID)> Texture = [&](ImTextureID Id)
                {
                    return Id == SceneTexture
                               ? FrontierExhibit::Image{ViewportPixels.data(), int(ViewportExtent[0]), int(ViewportExtent[1]), 4}
                               : FrontierExhibit::Image{};
                };
                for (const auto *List : Commands->CmdLists)
                    FrontierExhibit::RasterizeCommands(List, Pixels.data(), CaptureWidth, CaptureHeight, Commands->DisplayPos,
                                                       Commands->FramebufferScale, nullptr, &Texture);
                Require(stbi_write_png((Output / Name).string().c_str(), CaptureWidth, CaptureHeight, 3, Pixels.data(), CaptureWidth * 3),
                        "Native workspace capture");
            };
            Workspace.QueryOutliner().Select(FieldEntry->QueryToken());
            Tick();
            Tick();
            Tick();
            Require(Latest && Latest->Ready, "Environment diagnostics are live");
            Require(Latest->LocalClouds == 1 && Latest->LocalFogVolumes == 1, "Both local media reach native renderer");
            Require(Latest->Vectors.size() == 27, "World-vector sample lattice generated");
            Require(std::abs(Latest->WindAtCamera[0] - 4) < 1e-6 && std::abs(Latest->WindAtCamera[1]) < 1e-6,
                    "World +X wind maps to native Y-up solver correctly");
            Capture("ForceField.png");
            Workspace.QueryOutliner().Select(LocalFog->QueryToken());
            Tick();
            Tick();
            Capture("LocalFog.png");
            Workspace.QueryOutliner().Select(LocalCloud->QueryToken());
            Tick();
            Tick();
            Capture("LocalCloud.png");
            const auto Moving = Offset(*Latest, LocalFog->QueryToken().Value);
            Wind.Enabled      = false;
            Tick();
            Require(Offset(*Latest, LocalFog->QueryToken().Value) == Moving,
                    "Disabling Wind freezes accumulated fog displacement without jumping");
            Require(std::hypot(Latest->WindAtCamera[0], Latest->WindAtCamera[1], Latest->WindAtCamera[2]) < 1e-6,
                    "Disabled wind has zero evaluated velocity");
            Wind.Enabled   = true;
            Fog.FollowWind = false;
            Tick();
            Require(Offset(*Latest, LocalFog->QueryToken().Value) == Moving, "Unlinked fog with zero independent wind stays frozen");
            Fog.FollowWind = true;
            Tick();
            Require(Offset(*Latest, LocalFog->QueryToken().Value)[0] > Moving[0],
                    "Following fog resumes motion from retained displacement");
            Workspace.QueryOutliner().Select(AtmosphereEntry->QueryToken());
            auto *Inspector = InspectorRuntime::Instance().FindInspector(AtmosphereEntry->QueryToken());
            Inspector->ConfigureSection(3);
            const auto Original = Latest->AtmosphereBake;
            Require(Original && Original->Ready, "Initial real GPU LUT readback ready");
            Atmosphere.RayleighScattering = 2;
            Atmosphere.BakeRequested++;
            Atmosphere.UseBaked = true;
            Tick();
            Tick();
            Require(Atmosphere.BakedAvailable && Atmosphere.BakeAcknowledged == Atmosphere.BakeRequested,
                    "Workspace acknowledges successful GPU bake request");
            Require(Atmosphere.Baked.RayleighScattering == 2 && Latest->AtmosphereBake->Inputs.RayleighScattering == 2,
                    "Baked settings reflect actual inputs");
            Require(LookupDifference(Original->Transmittance, Latest->AtmosphereBake->Transmittance) > 1e-5,
                    "Bake regenerates changed GPU transmittance pixels");
            const auto Frozen             = Latest->AtmosphereBake;
            Atmosphere.RayleighScattering = 5;
            Tick();
            Tick();
            Require(Latest->AtmosphereBake->Inputs.RayleighScattering == 2, "Authored edits do not silently replace frozen baked inputs");
            Require(LookupDifference(Frozen->Transmittance, Latest->AtmosphereBake->Transmittance) == 0, "Frozen GPU lookup is unchanged");
            Capture("Baking-StaleAuthoring.png");
            Atmosphere.BakeRequested++;
            Tick();
            Tick();
            Require(Atmosphere.Baked.RayleighScattering == 5 && Atmosphere.BakeAcknowledged == Atmosphere.BakeRequested,
                    "Explicit rebake replaces snapshot");
            Capture("Baking-Ready.png");
            WriteLookup(Output / "Transmittance.png", Latest->AtmosphereBake->Transmittance, 256, 64, true);
            WriteLookup(Output / "MultipleScattering.png", Latest->AtmosphereBake->MultipleScattering, 32, 32, false);
            WriteLookup(Output / "SkyView.png", Latest->AtmosphereBake->SkyView, 192, 108, false);
            Atmosphere.UseBaked           = false;
            Atmosphere.RayleighScattering = 1.5f;
            Tick();
            Require(Latest->AtmosphereBake->Inputs.RayleighScattering == 1.5f, "Live mode resumes authored optics");
            auto Wrong = *Latest->AtmosphereBake;
            Wrong.SourceRecord++;
            Require(!ConfirmAtmosphereBake(Atmosphere, AtmosphereEntry->QueryToken().Value, Wrong),
                    "Foreign/stale record bake cannot be acknowledged");
            Atmosphere.MultiScatteringLut = false;
            Tick();
            double Multiple = 0;
            for (size_t Index = 0; Index < Latest->AtmosphereBake->MultipleScattering.size(); ++Index)
                if (Index % 4 != 3) Multiple += std::abs(Latest->AtmosphereBake->MultipleScattering[Index]);
            Require(Multiple == 0, "Multiple-scattering switch changes real GPU lookup to zero transfer");
            toml::table Before;
            AtmosphereEntry->SerializeConfiguration(Before);
            Require(Before["baked"].is_table(), "Baked optical snapshot serializes as TOML");
            Require(World.SaveLevelSpace(Scratch / "Baked.levelspace"), "Baked level saved");
            SceneWorld Reloaded;
            Require(Reloaded.LoadLevelSpace(Scratch / "Baked.levelspace"), "Baked level reloaded");
            auto *ReloadedAir = dynamic_cast<AtmosphereRecord *>(Reloaded.FindFirstByType("Atmosphere"));
            Require(ReloadedAir && ReloadedAir->QueryConfiguration().BakedAvailable &&
                        ReloadedAir->QueryConfiguration().Baked.RayleighScattering == 5,
                    "Frozen optical snapshot survives level reload");
            Workspace.PresentSceneViewport = nullptr;
            std::cout << Checks << " scene-media integration checks passed\n";
        }
        ImGui::DestroyContext();
        Report << "SUMMARY\t" << Checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception &Error)
    {
        std::cerr << Error.what() << '\n';
        return 1;
    }
}
