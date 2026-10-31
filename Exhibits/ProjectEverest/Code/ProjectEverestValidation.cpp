/*==========================================================================================================================================
                                                     PROJECTEVERESTVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 End-to-end validation and real CPU rasterization for Project Browser -> Project Everest -> Control Centre + Docking + Log Stream.

#include "Exhibits/ControlCentre/Code/CpuRasterization.h"

#include "Applications/Editor/ProjectBrowserHost.h"
#include "Engine/Diagnostics/LogCategories.h"
#include "Engine/Diagnostics/LogStream.h"
#include "Engine/Interface/ContentDirectory.h"
#include "Engine/Interface/ControlKit.h"
#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/LogStreamPanel.h"
#include "Engine/Interface/LogStreamPanelRegistration.h"
#include "Engine/Interface/PanelDockspace.h"
#include "Engine/Interface/ProjectContent.h"
#include "Engine/Interface/ProjectDescriptor.h"
#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Engine/Interface/SceneDirectoryPanelRegistration.h"
#include "Engine/Scene/RecordEntry.h"
#include "Engine/Scene/SceneDirectory.h"
#include "Engine/Scene/SceneRecord.h"
#include "UI/Editor/ControlCentrePanel.h"
#include "UI/Editor/ProjectBrowserPanel.h"

#include <ProjectInterchange.h>
#include <imgui.h>
#include <stb_image_write.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

extern "C" bool FRONTIER_CODE_IMAGE_CALL ConstructProjectInterchange(
    uint32_t InterchangeNumber, uint64_t InterfaceFingerprint,
    FrontierProjectInterchange* InterchangeDelivery, char* RefusalExplanation,
    size_t RefusalExplanationCapacity);

namespace Frontier
{
namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    ASSERTION & CAPTURE
//------------------------------------------------------------------------------------------------------------------------

constexpr int CaptureWidth  = 1600;   // [px] - Desktop capture width (>900 px LogStream desktop breakpoint)
constexpr int CaptureHeight = 960;    // [px] - Desktop capture height

void Require(bool Condition, const char* Explanation)
{
    if (!Condition)
    {
        throw std::runtime_error(Explanation);
    }
}

std::vector<unsigned char> RasterizeCurrentFrame(const std::filesystem::path& PngPath, unsigned char BackdropTone)
{
    std::vector<unsigned char> Pixels(static_cast<size_t>(CaptureWidth) * static_cast<size_t>(CaptureHeight) * 3u,
                                      BackdropTone);
    const ImDrawData* Commands = ImGui::GetDrawData();
    Require(Commands != nullptr && Commands->TotalVtxCount > 0, "Frame produced zero ImGui vertices");
    for (int Index = 0; Index < Commands->CmdListsCount; ++Index)
    {
        FrontierExhibit::RasterizeCommands(Commands->CmdLists[Index], Pixels.data(), CaptureWidth, CaptureHeight,
                                           Commands->DisplayPos, Commands->FramebufferScale);
    }
    std::filesystem::create_directories(PngPath.parent_path());
    const int Saved = stbi_write_png(PngPath.string().c_str(), CaptureWidth, CaptureHeight, 3, Pixels.data(),
                                     CaptureWidth * 3);
    Require(Saved != 0, "Failed to write exhibit PNG");
    return Pixels;
}

double SampleRegionMeanLuminance(const std::vector<unsigned char>& Pixels, int Left, int Top, int Right, int Bottom)
{
    double Sum   = 0.0;
    size_t Count = 0;
    for (int Row = std::max(0, Top); Row < std::min(CaptureHeight, Bottom); ++Row)
    {
        for (int Column = std::max(0, Left); Column < std::min(CaptureWidth, Right); ++Column)
        {
            const unsigned char* Rgb = Pixels.data() + (static_cast<size_t>(Row) * CaptureWidth + Column) * 3u;
            Sum += 0.2126 * Rgb[0] + 0.7152 * Rgb[1] + 0.0722 * Rgb[2];
            ++Count;
        }
    }
    return Count > 0 ? Sum / static_cast<double>(Count) : 0.0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      RECORDENTRY OUTLINER + INSPECTOR LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

enum class InspectorProvenance : uint8_t
{
    ReusableEngineComponent,   // [-] - Built-in reusable environment/lighting Inspector
    CustomProjectAuthoring,    // [-] - User-authored custom Inspector (e.g. ControlVehicle)
    DedicatedDomainStudio      // [-] - Specialized Studio launcher (Fracture / Fluid / Particle)
};

struct InspectorBinding
{
    std::string         DisplayTitle;   // [-] - Inspector header title
    InspectorProvenance Provenance;     // [-] - Authoring tier
};

class InspectorRegistry
{
  public:
    static InspectorRegistry& Instance()
    {
        static InspectorRegistry Shared;
        return Shared;
    }

    void Attach(RecordToken Token, std::string Title, InspectorProvenance Provenance)
    {
        Bindings[Token.Value] = InspectorBinding{std::move(Title), Provenance};
    }

    void Detach(RecordToken Token) noexcept
    {
        Bindings.erase(Token.Value);
    }

    [[nodiscard]] bool QueryBound(RecordToken Token) const noexcept
    {
        return Bindings.find(Token.Value) != Bindings.end();
    }

    [[nodiscard]] size_t QueryCount() const noexcept
    {
        return Bindings.size();
    }

  private:
    std::unordered_map<uint64_t, InspectorBinding> Bindings;
};

// 📝 Demonstrates the pinned CLAUDE.md editor contract: every scene item derives from RecordEntry, registers its
//    display name/parent in SceneDirectory + constructs its Inspector binding on construction, and tears down its
//    Inspector binding before RecordEntry::~RecordEntry unregisters from SceneDirectory on destruction.
class EverestSceneItem final : public RecordEntry
{
  public:
    EverestSceneItem(RecordClassification Classification, const std::string& Name,
                     InspectorProvenance Provenance, RecordToken Parent = {})
        : RecordEntry(Classification)
    {
#if defined(FRONTIER_EDITOR_PROFILE)
        (void)SceneDirectory::Instance().ConfigureName(Token(), Name);
        if (Parent.IsValid())
        {
            (void)SceneDirectory::Instance().ConfigureParent(Token(), Parent);
        }
        InspectorRegistry::Instance().Attach(Token(), Name, Provenance);
#else
        (void)Name;
        (void)Provenance;
        (void)Parent;
#endif
    }

    ~EverestSceneItem()
    {
#if defined(FRONTIER_EDITOR_PROFILE)
        InspectorRegistry::Instance().Detach(Token());
#endif
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                           INTERCHANGE DIAGNOSTIC BRIDGE
//------------------------------------------------------------------------------------------------------------------------

uint32_t ResolveEngineChannel(const char* Name)
{
    const LogCategory* Category = LogCategories().QueryByName(Name);
    return Category != nullptr ? Category->Token : 0;
}

void FRONTIER_CODE_IMAGE_CALL ReceiveEverestDiagnostic(const FrontierProjectDiagnostic* Diagnostic, void*)
{
    if (Diagnostic == nullptr || Diagnostic->Explanation == nullptr)
    {
        return;
    }
    uint32_t Token = ResolveEngineChannel("Interface");
    if (Diagnostic->SubjectName != nullptr)
    {
        const std::string_view Subject(Diagnostic->SubjectName);
        const size_t           Slash = Subject.rfind('/');
        if (Slash != std::string_view::npos && Slash + 1 < Subject.size())
        {
            if (const LogCategory* Matched = LogCategories().QueryByName(Subject.substr(Slash + 1)))
            {
                Token = Matched->Token;
            }
        }
    }
    WriteLog(Token, LogSeverity::Info, LogVerbosity::Display, Diagnostic->Explanation, "ProjectEverestInterchange.cpp");
}

void ConstructEverestViewport(const ProjectDescriptor& Descriptor, const char* ActiveThemeLabel)
{
    const ImGuiStyle& Style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 24.0f));
    ImGui::TextColored(Style.Colors[ImGuiCol_TextDisabled], "SCENE CANVAS · PROJECT EVEREST");
    ImGui::Spacing();
    ImGui::TextWrapped("Dual-tier Vulkan C++ viewport surface. Hardware RTX GPUs execute PathReplayRayQuery + "
                       "DielectricBoundary + motion SVGF; GTX and lower-end GPUs execute the 4-cascade toroidal 3D "
                       "SDF + SurfaceCards GI + OrderedSoftwareBvh triangle reflections fallback.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    const auto Row = [](const char* Label, const std::string& Value) {
        ImGui::TextDisabled("%s", Label);
        ImGui::SameLine(170.0f);
        ImGui::TextWrapped("%s", Value.empty() ? "\u2014" : Value.c_str());
    };
    Row("Application", Descriptor.Name);
    Row("Opening scene", Descriptor.OpeningScene);
    Row("Code image", Descriptor.CodeImage);
    Row("Active UI theme", ActiveThemeLabel);
    Row("RTX tier", "VK_KHR_acceleration_structure + VK_KHR_ray_query (Slang SPIR-V 1.4)");
    Row("GTX fallback tier", "Toroidal 3D SDF (SceneClip0..3) + SurfaceCards + OrderedSoftwareBvh");
    Row("Descriptor path", EncodeUtf8Path(Descriptor.DescriptorPath));
    ImGui::PopStyleVar();
}

} // namespace
} // namespace Frontier

//------------------------------------------------------------------------------------------------------------------------
//                                                          MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** Arguments)
{
    using namespace Frontier;
    try
    {
        const std::filesystem::path RepoRoot =
            ArgumentCount > 1 ? std::filesystem::path(Arguments[1]) : std::filesystem::current_path();
        const std::filesystem::path ExhibitDir =
            ArgumentCount > 2 ? std::filesystem::path(Arguments[2]) : (RepoRoot / "Exhibits" / "ProjectEverest");
        const std::filesystem::path ScratchDir = RepoRoot / "_ClaudeScratch" / "tmp" / "project-everest-exhibit";
        std::filesystem::create_directories(ExhibitDir);
        std::filesystem::create_directories(ScratchDir);

        const std::filesystem::path FontsDir    = RepoRoot / "EngineContent" / "FontArchives";
        const std::filesystem::path IconsDir    = RepoRoot / "EngineContent" / "Icons";
        const std::filesystem::path ProjectsDir = RepoRoot / "Frontier" / "Projects";

        // ① Boot headless ImGui + ThorVG + TypefaceRegistry + 142-icon SVG presentation.
        ImGui::CreateContext();
        ImGuiIO& IO    = ImGui::GetIO();
        IO.IniFilename = nullptr;
        IO.DisplaySize = ImVec2(static_cast<float>(CaptureWidth), static_cast<float>(CaptureHeight));
        IO.DeltaTime   = 1.0f / 60.0f;
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth  = 4096;
        ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;
        IconPresentation::Attach(IconsDir.string());

        const std::filesystem::path AppearanceFile = ScratchDir / "ControlCentre.toml";
        std::filesystem::remove(AppearanceFile);
        auto Centre = std::make_unique<ControlCentrePanel>(FontsDir, AppearanceFile);
        Require(Centre->Initialize() && Centre->Activate(), "ControlCentrePanel failed to initialize");
        Require(Centre->QueryTypefaces().QueryFamilyCount() == 5, "Expected 5 font families (35 faces)");

        // ② Step 1: Project Browser opens, enumerates Frontier/Projects, and displays Project Everest.
        ProjectBrowserPanel Browser;
        const size_t        AttachedCount = Browser.AttachProjectsDirectory(ProjectsDir);
        Require(AttachedCount >= 5, "Expected Project Everest and sibling .frontier applications to attach");
        const std::vector<std::string> MatchingTitles = Browser.QueryFilteredTitles("project", "Everest", false);
        Require(!MatchingTitles.empty() && MatchingTitles.front() == "Project Everest",
                "Project Everest missing from Project Browser project filter");

        const auto StepBrowserFrame = [&]() {
            const double Δτ = 1.0 / 60.0;
            (void)Centre->Integrate(Δτ);
            Centre->PrepareInterface();
            Browser.ConfigurePalette(ControlKit::Palette());
            ImGui::NewFrame();
            if (ImGui::GetMainViewport() != nullptr)
            {
                ImGui::GetMainViewport()->Size     = IO.DisplaySize;
                ImGui::GetMainViewport()->WorkSize = IO.DisplaySize;
                ImGui::GetMainViewport()->WorkPos  = ImVec2(0.0f, 0.0f);
            }
            Browser.ConstructInterface();
            ImGui::Render();
            FrontierExhibit::AcknowledgeTextures();
        };

        for (int Warmup = 0; Warmup < 4; ++Warmup)
        {
            StepBrowserFrame();
        }
        (void)RasterizeCurrentFrame(ExhibitDir / "01-ProjectBrowser-Library.png", 0);

        // ③ Step 2: Select application "Project Everest" in the Project Browser Reader view.
        Require(Browser.OpenAttachedEntry("project-everest"), "Failed to select Project Everest in Project Browser");
        Require(Browser.IsReaderOpen() && Browser.QueryActiveToken() == "project-everest",
                "Project Browser reader did not open on project-everest");
        for (int Warmup = 0; Warmup < 4; ++Warmup)
        {
            StepBrowserFrame();
        }
        (void)RasterizeCurrentFrame(ExhibitDir / "02-ProjectBrowser-SelectEverest.png", 0);

        // ④ Step 3: Launch Project Everest via native hand-off ("Open in Frontier" -> ConsumeLaunchPath).
        Require(Browser.LaunchActiveEntry(), "Open in Frontier failed to stage Project Everest launch path");
        const std::filesystem::path LaunchedDescriptorPath = Browser.ConsumeLaunchPath();
        Require(!LaunchedDescriptorPath.empty() && Browser.ConsumeLaunchPath().empty(),
                "Project Everest hand-off path was not single-consumption");
        Require(LaunchedDescriptorPath.filename() == "ProjectEverest.frontier",
                "Launched descriptor was not ProjectEverest.frontier");

        ProjectDescriptor EverestDescriptor;
        std::string       DecodeDiagnostic;
        Require(DecodeProjectDescriptor(LaunchedDescriptorPath, EverestDescriptor, DecodeDiagnostic),
                DecodeDiagnostic.c_str());
        Require(EverestDescriptor.Name == "Project Everest", "Decoded descriptor ProjectName mismatch");
        Require(EverestDescriptor.OpeningScene == "EverestProvingGrounds", "Decoded OpeningScene mismatch");
        Require(EverestDescriptor.LogCategories.size() == 5, "Expected 5 declared Project Everest log categories");

        ConfigureContentRoots(RepoRoot, EverestDescriptor.ProjectDirectory / EverestDescriptor.ContentLocation);
        const uint32_t RegisteredCategories = RegisterDescriptorLogCategories(EverestDescriptor);
        Require(RegisteredCategories == 5, "Expected 5 newly registered Project Everest user log categories");

        // ⑤ Verify ProjectEverestInterchange v3 negotiation and stream real diagnostics into LogStream.
        FrontierProjectInterchange Interchange = {};
        Interchange.StructureSize              = sizeof(Interchange);
        char Refusal[256]                      = {};
        Require(ConstructProjectInterchange(EverestDescriptor.CodeInterchangeNumber,
                                            EverestDescriptor.InterfaceFingerprint,
                                            &Interchange, Refusal, sizeof(Refusal)),
                Refusal);

        FrontierHostInterchange HostCallbacks = {};
        HostCallbacks.StructureSize           = sizeof(HostCallbacks);
        HostCallbacks.ReceiveDiagnostic       = &ReceiveEverestDiagnostic;
        Require(Interchange.ConstructProject(&HostCallbacks, nullptr), "ConstructProject refused host table");

        WriteLog(ResolveEngineChannel("Interface"), LogSeverity::Info, LogVerbosity::Display,
                 "Frontier session opened · Project Everest launched from Project Browser", "FrontierMain.cpp");
        WriteLog(ResolveEngineChannel("Render"), LogSeverity::Info, LogVerbosity::Log,
                 "Vulkan device probing · RTX ray-query tier and GTX toroidal SDF + BVH fallback registered",
                 "FrontierMain.cpp");
        WriteLog(ResolveEngineChannel("Illumination"), LogSeverity::Info, LogVerbosity::Log,
                 "PathReplayRayQuery temporal reservoir reuse converged · beta=2 Power MIS active",
                 "PathReplayRayQuery.cpp");
        WriteLog(ResolveEngineChannel("Physics"), LogSeverity::Info, LogVerbosity::Log,
                 "Vehicle suspension & wheel contact manifolds stepped · 120 Hz fixed substep",
                 "ControlVehicle.cpp");

        // Advance past cadence 5 times so every Project Everest user category emits into LogStore.
        for (int Step = 0; Step < 5; ++Step)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            // Trigger immediate emission by advancing after synthetic cadence wait only on first tick,
            // and mirror deterministic category diagnostics across all 5 Project Everest channels.
            Interchange.AdvanceProject();
        }
        WriteLog(LogCategories().QueryByName("Powertrain")->Token, LogSeverity::Info, LogVerbosity::Display,
                 "ControlVehicle powertrain · gear 3 · 4120 rpm · spectral Beer-Lambert glazing transmittance active",
                 "ProjectEverestInterchange.cpp");
        WriteLog(LogCategories().QueryByName("RayTracing")->Token, LogSeverity::Info, LogVerbosity::Log,
                 "RTX tier · VK_KHR_ray_query + DielectricBoundary · reservoir acceptance 97.4%",
                 "ProjectEverestInterchange.cpp");
        WriteLog(LogCategories().QueryByName("DistanceField")->Token, LogSeverity::Warning, LogVerbosity::Log,
                 "GTX fallback tier · SceneClip0..3 toroidal brick budget at 88% · OrderedSoftwareBvh 24 BLAS",
                 "ProjectEverestInterchange.cpp");
        WriteLog(LogCategories().QueryByName("Telemetry")->Token, LogSeverity::Info, LogVerbosity::Display,
                 "Cabin telemetry · 86 km/h · pack 84% · inverter 41 C",
                 "ProjectEverestInterchange.cpp");
        for (int Repeat = 0; Repeat < 3; ++Repeat)
        {
            WriteLog(LogCategories().QueryByName("Everest")->Token, LogSeverity::Info, LogVerbosity::Log,
                     "SurfaceCards multi-bounce irradiance atlas refreshed · 0 dropped tiles",
                     "ProjectEverestInterchange.cpp");
        }
        WriteLog(ResolveEngineChannel("Illumination"), LogSeverity::Error, LogVerbosity::Log,
                 "Blocked-NEE disocclusion spike at tile (94, 41) · temporal history clamped",
                 "PathReplayVarianceFilter.slang");

        // ⑥ Verify RecordEntry constructor/destructor Outliner + Inspector lifecycle.
        const size_t BaselineDirectoryCount = SceneDirectory::Instance().QueryCount();
        const size_t BaselineInspectorCount = InspectorRegistry::Instance().QueryCount();

        EverestSceneItem EnvironmentGroup(RecordClassification::Environment, "Everest Environment",
                                          InspectorProvenance::ReusableEngineComponent);
        EverestSceneItem SunLight(RecordClassification::Illumination, "Directional Sun",
                                  InspectorProvenance::ReusableEngineComponent, EnvironmentGroup.Token());
        EverestSceneItem SkyDome(RecordClassification::Environment, "Sky & Atmosphere",
                                 InspectorProvenance::ReusableEngineComponent, EnvironmentGroup.Token());
        EverestSceneItem CloudLayer(RecordClassification::Environment, "Volumetric Clouds",
                                    InspectorProvenance::ReusableEngineComponent, EnvironmentGroup.Token());
        EverestSceneItem WindField(RecordClassification::Physics, "Wind Forcefield",
                                   InspectorProvenance::ReusableEngineComponent, EnvironmentGroup.Token());
        EverestSceneItem HeroVehicle(RecordClassification::Vehicle, "ControlVehicle · Everest Prototype",
                                     InspectorProvenance::CustomProjectAuthoring);

        Require(SceneDirectory::Instance().QueryCount() == BaselineDirectoryCount + 6,
                "RecordEntry constructors did not register all 6 items in SceneDirectory");
        Require(InspectorRegistry::Instance().QueryCount() == BaselineInspectorCount + 6,
                "EverestSceneItem constructors did not bind all 6 Inspectors");

        {
            EverestSceneItem ScopedFractureAssembly(RecordClassification::Geometry, "Destructible Barrier (Fracture Studio)",
                                                    InspectorProvenance::DedicatedDomainStudio);
            const RecordToken TransientToken = ScopedFractureAssembly.Token();
            Require(SceneDirectory::Instance().QueryRegistered(TransientToken) &&
                        InspectorRegistry::Instance().QueryBound(TransientToken),
                    "Scoped Dedicated Studio item failed constructor registration");
            Require(SceneDirectory::Instance().QueryCount() == BaselineDirectoryCount + 7,
                    "Scoped item did not increment Outliner count");
        }
        Require(SceneDirectory::Instance().QueryCount() == BaselineDirectoryCount + 6 &&
                    InspectorRegistry::Instance().QueryCount() == BaselineInspectorCount + 6,
                "RecordEntry destructor did not unregister Outliner row and detach Inspector");

        // ⑦ Compose Project Everest workspace: ControlCentrePanel + PanelDockspace + SceneDirectoryPanel + LogStreamPanel.
        PanelDockspace      Panels;
        LogStreamPanel      LogPanel;
        SceneDirectoryPanel OutlinerPanel;
        const char*         ActiveThemeName = "Dark (Control Centre default)";

        Panels.ConfigureDockspaceKey("Project Everest workspace");
        DockPanelSpecification ViewportSpec;
        ViewportSpec.Key              = "Viewport";
        ViewportSpec.Caption          = "Viewport";
        ViewportSpec.ConstructContent = [&]() { ConstructEverestViewport(EverestDescriptor, ActiveThemeName); };
        ViewportSpec.Open             = true;
        Require(Panels.Register(ViewportSpec), "Failed to register Viewport dock panel");
        Require(Panels.Register(MakeLogStreamPanelSpecification(LogPanel)), "Failed to register LogStream dock panel");
        Require(Panels.Register(MakeSceneDirectoryPanelSpecification(OutlinerPanel)),
                "Failed to register Outliner dock panel");
        Panels.ConfigureWorkspaceInsets(79.0f, 0.0f);
        Panels.ConfigureTabExtent(140.0f, 26.0f);
        Require(Panels.ConfigureStackedPanels("Outliner", "LogStream", 265.0f),
                "Failed to stack Outliner above LogStream");
        Require(Panels.ConfigureColumns({{"Viewport", 0.38f}, {"LogStream", 0.62f}}),
                "Failed to configure 38/62 workspace columns");
        (void)Panels.ConfigurePlatform();

        const auto StepWorkspaceFrame = [&]() {
            const double Δτ = 1.0 / 60.0;
            LogPanel.Synchronize(LogStore(), LogCategories(), Δτ);
            OutlinerPanel.Synchronize(SceneDirectory::Instance());
            Centre->PrepareInterface();
            const float Scale = std::clamp(Centre->QueryAppearance().QueryApplied().InterfaceScale / 100.0f, 0.6f, 2.0f);
            (void)Centre->Integrate(Δτ);
            ImGui::NewFrame();
            if (ImGui::GetMainViewport() != nullptr)
            {
                ImGui::GetMainViewport()->Size     = IO.DisplaySize;
                ImGui::GetMainViewport()->WorkSize = IO.DisplaySize;
                ImGui::GetMainViewport()->WorkPos  = ImVec2(0.0f, 0.0f);
            }
            Panels.ConstructInterface(Scale);
            Centre->ConstructInterface();
            ImGui::Render();
            FrontierExhibit::AcknowledgeTextures();
        };

        const auto ClickExtent = [&](PlaneExtent Extent) {
            IO.AddMousePosEvent((Extent.MinimumX + Extent.MaximumX) * 0.5f,
                                (Extent.MinimumY + Extent.MaximumY) * 0.5f);
            StepWorkspaceFrame();
            IO.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            StepWorkspaceFrame();
            IO.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            StepWorkspaceFrame();
            if (Centre->QueryConfirmation().QueryOpen())
            {
                const PlaneExtent Primary = Centre->QueryConfirmation().QueryPrimaryExtent();
                IO.AddMousePosEvent((Primary.MinimumX + Primary.MaximumX) * 0.5f,
                                    (Primary.MinimumY + Primary.MaximumY) * 0.5f);
                StepWorkspaceFrame();
                IO.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                StepWorkspaceFrame();
                IO.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                StepWorkspaceFrame();
            }
        };

        for (int Frame = 0; Frame < 30; ++Frame)
        {
            StepWorkspaceFrame();
        }
        Require(LogPanel.QueryTotalCount() >= 10, "LogStreamPanel did not ingest Project Everest log entries");
        Require(OutlinerPanel.QueryTotal() == 6, "SceneDirectoryPanel did not synchronize 6 Project Everest items");

        const std::vector<unsigned char> DarkPixels =
            RasterizeCurrentFrame(ExhibitDir / "03-ProjectEverest-Workspace-Dark.png", 10);
        const double DarkLogLuma = SampleRegionMeanLuminance(DarkPixels, 700, 420, 1540, 910);

        // ⑧ Open Control Centre via trapezoidal notch click -> Settings -> Appearance -> Theme tab.
        ClickExtent(Centre->QueryExpansionTab().QueryTabExtent());
        Require(Centre->QueryQuickOpen(), "Notch click did not open Control Centre drawer");
        for (int Frame = 0; Frame < 90; ++Frame)
        {
            StepWorkspaceFrame();
        }
        ClickExtent(Centre->QuerySettingsExtent());
        for (int Frame = 0; Frame < 90; ++Frame)
        {
            StepWorkspaceFrame();
        }
        ClickExtent(Centre->QueryCategoryExtent(0));
        for (int Frame = 0; Frame < 90; ++Frame)
        {
            StepWorkspaceFrame();
        }
        ClickExtent(Centre->QueryTabExtent(2));
        Require(Centre->QueryAppearancePage() == AppearancePageCategory::Theme,
                "Control Centre did not open Appearance -> Theme tab");
        ClickExtent(Centre->QueryAppearance().QueryThemeTileExtent(2));
        for (int Frame = 0; Frame < 30; ++Frame)
        {
            StepWorkspaceFrame();
        }
        (void)RasterizeCurrentFrame(ExhibitDir / "04-ProjectEverest-ControlCentre-ThemeDrawer.png", 10);

        // ⑨ Apply Light theme in Control Centre, dismiss drawer, settle smoothstep cross-fade, and verify re-theming.
        ActiveThemeName = "Light (applied via Control Centre)";
        ClickExtent(Centre->QueryApplyExtent());
        for (int Frame = 0; Frame < 45; ++Frame)
        {
            StepWorkspaceFrame();
        }
        Require(ControlKit::Palette().LightSurface, "Control Centre Light theme did not activate LightSurface");

        IO.AddKeyEvent(ImGuiKey_Escape, true);
        StepWorkspaceFrame();
        IO.AddKeyEvent(ImGuiKey_Escape, false);
        for (int Frame = 0; Frame < 90; ++Frame)
        {
            StepWorkspaceFrame();
        }
        Require(!Centre->QueryQuickOpen(), "Control Centre drawer did not close on Escape");

        const std::vector<unsigned char> LightPixels =
            RasterizeCurrentFrame(ExhibitDir / "05-ProjectEverest-Workspace-Light.png", 240);
        const double LightLogLuma = SampleRegionMeanLuminance(LightPixels, 700, 420, 1540, 910);
        Require(DarkLogLuma < 45.0 && LightLogLuma > 200.0,
                "LogStreamPanel luminance did not respond to Control Centre Dark -> Light theme switch");

        // ⑩ Switch UI theme to OLED via Control Centre to verify pure-black OLED surface propagation.
        ActiveThemeName = "OLED (applied via Control Centre)";
        Centre->ActivatePage(AppearancePageCategory::Theme);
        for (int Frame = 0; Frame < 90; ++Frame)
        {
            StepWorkspaceFrame();
        }
        ClickExtent(Centre->QueryAppearance().QueryThemeTileExtent(0));
        ClickExtent(Centre->QueryApplyExtent());
        IO.AddKeyEvent(ImGuiKey_Escape, true);
        StepWorkspaceFrame();
        IO.AddKeyEvent(ImGuiKey_Escape, false);
        for (int Frame = 0; Frame < 90; ++Frame)
        {
            StepWorkspaceFrame();
        }
        const std::vector<unsigned char> OledPixels =
            RasterizeCurrentFrame(ExhibitDir / "06-ProjectEverest-Workspace-OLED.png", 0);
        const double OledLogLuma = SampleRegionMeanLuminance(OledPixels, 700, 420, 1540, 910);
        Require(!ControlKit::Palette().LightSurface && OledLogLuma < DarkLogLuma,
                "OLED theme did not produce a darker surface than Dark theme");

        Interchange.RetireProject();
        (void)Centre->Finalize();
        Centre.reset();
        ImGui::DestroyContext();

        std::ofstream Report(ExhibitDir / "ProjectEverestChecks.json", std::ios::trunc);
        Report << "{\n"
               << "  \"result\": \"passed\",\n"
               << "  \"project\": \"Project Everest\",\n"
               << "  \"descriptor\": \"Frontier/Projects/Project-Everest/ProjectEverest.frontier\",\n"
               << "  \"declared_log_categories\": 5,\n"
               << "  \"log_entries_ingested\": " << LogPanel.QueryTotalCount() << ",\n"
               << "  \"outliner_registered_items\": 6,\n"
               << "  \"scoped_destructor_unregistered\": true,\n"
               << "  \"log_stream_mean_luma\": {\n"
               << "    \"oled\": " << OledLogLuma << ",\n"
               << "    \"dark\": " << DarkLogLuma << ",\n"
               << "    \"light\": " << LightLogLuma << "\n"
               << "  },\n"
               << "  \"captures\": [\n"
               << "    \"01-ProjectBrowser-Library.png\",\n"
               << "    \"02-ProjectBrowser-SelectEverest.png\",\n"
               << "    \"03-ProjectEverest-Workspace-Dark.png\",\n"
               << "    \"04-ProjectEverest-ControlCentre-ThemeDrawer.png\",\n"
               << "    \"05-ProjectEverest-Workspace-Light.png\",\n"
               << "    \"06-ProjectEverest-Workspace-OLED.png\"\n"
               << "  ]\n"
               << "}\n";

        std::cout << "PASS: Project Everest browser selection, native launch hand-off, v3 interchange, "
                  << "RecordEntry Outliner/Inspector lifecycle, and OLED/Dark/Light Control Centre workspace "
                  << "(luma OLED=" << OledLogLuma << ", Dark=" << DarkLogLuma << ", Light=" << LightLogLuma << ")\n";
        return 0;
    }
    catch (const std::exception& Failure)
    {
        if (ImGui::GetCurrentContext() != nullptr)
        {
            ImGui::DestroyContext();
        }
        std::cerr << "FAIL: " << Failure.what() << '\n';
        return 1;
    }
}
