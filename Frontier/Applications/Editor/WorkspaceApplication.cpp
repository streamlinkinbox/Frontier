/*==========================================================================================================================================
                                                        WORKSPACE APPLICATION
                                     ------------------------------------------------------------------
                 Editor host lifecycle (Initialize -> Loop -> Finalize) transitioning Project Browser <-> Workspace.
==========================================================================================================================================*/

// 🧩 Editor host lifecycle (Initialize -> Loop -> Finalize) transitioning Project Browser <-> Workspace in one loop.

#include "WorkspaceApplication.h"
#include "ProjectBrowserHost.h"

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Diagnostics/LogStream.h"
#include "Engine/Interface/LogStreamPanel.h"
#include "Engine/Interface/ProjectContent.h"

#include <cstdio>
#include <cstring>
#include <system_error>

namespace Frontier
{
namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

std::filesystem::path ResolveExecutableFolder(const char* ArgumentZero)
{
    std::error_code Error;
    if (ArgumentZero == nullptr || ArgumentZero[0] == '\0')
    {
        return std::filesystem::current_path(Error);
    }
    const std::filesystem::path Absolute = std::filesystem::absolute(ArgumentZero, Error);
    return Absolute.has_parent_path() ? Absolute.parent_path() : std::filesystem::current_path(Error);
}

std::filesystem::path ResolveProjectsDirectory(const std::filesystem::path& FromExecutable)
{
    std::error_code             Error;
    const std::filesystem::path Working = std::filesystem::current_path(Error);
    const std::filesystem::path Candidates[] = {
        FromExecutable / "Projects",
        FromExecutable / ".." / "Projects",
        FromExecutable / ".." / ".." / "Frontier" / "Projects",
        Working / "Frontier" / "Projects",
        Working / "Projects",
    };
    for (const std::filesystem::path& Candidate : Candidates)
    {
        if (std::filesystem::is_directory(Candidate, Error))
        {
            return Candidate;
        }
    }
    return Candidates[3];
}

uint32_t QueryEngineLogToken(const char* CategoryName)
{
    const LogCategory* Category = LogCategories().QueryByName(CategoryName);
    return Category != nullptr ? Category->Token : 0;
}

bool PrepareProjectSession(const std::filesystem::path& DescriptorPath,
                           const std::filesystem::path& FromExecutable,
                           ProjectDescriptor&           OutDescriptor)
{
    std::string Diagnostic;
    if (!DecodeProjectDescriptor(DescriptorPath, OutDescriptor, Diagnostic))
    {
        std::fprintf(stderr, "Frontier: cannot open project: %s\n", Diagnostic.c_str());
        return false;
    }

    ConfigureContentRoots(FromExecutable, OutDescriptor.ProjectDirectory / OutDescriptor.ContentLocation);
    ConfigureLogFile(OutDescriptor.ProjectDirectory / "Saved" / "Logs" / "Frontier.log");

    const uint32_t Declared = RegisterDescriptorLogCategories(OutDescriptor);

    char Line[512] = {};
    std::snprintf(Line, sizeof(Line), "Frontier session opened · %s", OutDescriptor.Name.c_str());
    WriteLog(QueryEngineLogToken("Interface"), LogSeverity::Info, LogVerbosity::Display, Line,
             "WorkspaceApplication.cpp");

    const std::filesystem::path EngineContent = QueryEngineContentDirectory();
    std::snprintf(Line, sizeof(Line), "Content roots configured · project %s · engine %s",
                  OutDescriptor.ContentLocation.c_str(),
                  EngineContent.empty() ? "(none staged)" : EncodeUtf8Path(EngineContent).c_str());
    WriteLog(QueryEngineLogToken("Content"), LogSeverity::Info, LogVerbosity::Log, Line, "WorkspaceApplication.cpp");

    std::snprintf(Line, sizeof(Line), "Log categories registered · %u declared by project · %u total", Declared,
                  LogCategories().QueryCount());
    WriteLog(QueryEngineLogToken("Interface"), LogSeverity::Info, LogVerbosity::Log, Line, "WorkspaceApplication.cpp");
    return true;
}

void RecordHeadlessDiagnosticTraffic(const ProjectDescriptor& Descriptor)
{
    WriteLog(QueryEngineLogToken("Interface"), LogSeverity::Info, LogVerbosity::Display,
             "Headless diagnostic session · synthetic traffic follows", "WorkspaceApplication.cpp");

    uint32_t Random = 2166136261u;
    auto     Next   = [&Random]() {
        Random = Random * 1664525u + 1013904223u;
        return (Random >> 16) & 0x7fffu;
    };

    char Line[256] = {};
    for (uint32_t Index = 0; Index < 36; ++Index)
    {
        switch (Index % 6)
        {
            case 0:
                std::snprintf(Line, sizeof(Line), "Pipeline cache hit · SurfaceRasterization (0.%02u ms)",
                              20 + Next() % 70);
                WriteLog(QueryEngineLogToken("Render"), LogSeverity::Info, LogVerbosity::Log, Line,
                         "WorkspaceApplication.cpp");
                break;
            case 1:
                std::snprintf(Line, sizeof(Line), "Temporal resampling converged · reservoirs valid %u.%u%%",
                              96 + Next() % 4, Next() % 10);
                WriteLog(QueryEngineLogToken("Illumination"), LogSeverity::Info, LogVerbosity::Log, Line,
                         "WorkspaceApplication.cpp");
                break;
            case 2:
                std::snprintf(Line, sizeof(Line), "Jolt world stepped · %u bodies", 900 + Next() % 700);
                WriteLog(QueryEngineLogToken("Physics"), LogSeverity::Info, LogVerbosity::Verbose, Line,
                         "WorkspaceApplication.cpp");
                break;
            case 3:
                std::snprintf(Line, sizeof(Line), "Powertrain · gear %u · %u rpm", 2 + Next() % 5,
                              2800 + Next() % 3600);
                WriteLog(QueryEngineLogToken("Vehicle"), LogSeverity::Info, LogVerbosity::Display, Line,
                         "WorkspaceApplication.cpp");
                break;
            case 4:
                std::snprintf(Line, sizeof(Line), "Cluster cull · %u%% rejected by HiZ", 30 + Next() % 28);
                WriteLog(QueryEngineLogToken("Geometry"), LogSeverity::Info, LogVerbosity::Log, Line,
                         "WorkspaceApplication.cpp");
                break;
            default:
                std::snprintf(Line, sizeof(Line), "Sky transmittance sampled · sun elevation %u deg", Next() % 62);
                WriteLog(QueryEngineLogToken("Environment"), LogSeverity::Info, LogVerbosity::Verbose, Line,
                         "WorkspaceApplication.cpp");
                break;
        }
    }

    for (const ProjectLogCategory& Declared : Descriptor.LogCategories)
    {
        const LogCategory* Category = LogCategories().QueryByName(Declared.Name);
        if (Category == nullptr)
        {
            continue;
        }
        for (uint32_t Repeat = 0; Repeat < 5; ++Repeat)
        {
            WriteLog(Category->Token, LogSeverity::Info, LogVerbosity::Log,
                     "Leaf cluster detached · 22 leaves · gust 5.8 m/s", "WorkspaceApplication.cpp");
        }
        WriteLog(Category->Token, LogSeverity::Warning, LogVerbosity::Log,
                 "Leaf instance budget 91% · oldest litter recycled", "WorkspaceApplication.cpp");
    }

    WriteLog(QueryEngineLogToken("Illumination"), LogSeverity::Error, LogVerbosity::Log,
             "NaN in reservoir stream at tile (141, 63) · reservoir reset for tile", "WorkspaceApplication.cpp");
}

int ExecuteHeadlessSession(const std::filesystem::path& DescriptorPath, const std::filesystem::path& FromExecutable)
{
    ProjectDescriptor Descriptor;
    if (!PrepareProjectSession(DescriptorPath, FromExecutable, Descriptor))
    {
        return 1;
    }

    RecordHeadlessDiagnosticTraffic(Descriptor);

    LogStreamPanel Panel;
    for (uint32_t Step = 0; Step < 16; ++Step)
    {
        Panel.Synchronize(LogStore(), LogCategories(), 0.25);
    }

    std::printf("Frontier headless · %s\n", Descriptor.Name.c_str());
    std::printf("  entries %llu · warnings %llu · errors %llu · visible %zu · categories %u · dropped %llu\n",
                static_cast<unsigned long long>(Panel.QueryTotalCount()),
                static_cast<unsigned long long>(Panel.QuerySeverityCount(LogSeverity::Warning)),
                static_cast<unsigned long long>(Panel.QuerySeverityCount(LogSeverity::Error) +
                                                Panel.QuerySeverityCount(LogSeverity::Fatal)),
                Panel.QueryVisible().size(), LogCategories().QueryCount(),
                static_cast<unsigned long long>(Panel.QueryDroppedCount()));
    std::printf("  session log: %s\n",
                EncodeUtf8Path(Descriptor.ProjectDirectory / "Saved" / "Logs" / "Frontier.log").c_str());
    return 0;
}

#if !defined(FRONTIER_WITH_INTERFACE_SDK)
int ExecuteBrowserListing(const std::filesystem::path& FromExecutable)
{
    const std::filesystem::path     Projects    = ResolveProjectsDirectory(FromExecutable);
    const ProjectBrowserEnumeration Enumeration = EnumerateProjects(Projects);

    std::printf("Frontier project browser (headless listing) · %s\n", EncodeUtf8Path(Projects).c_str());
    for (const ProjectBrowserEntry& Entry : Enumeration.Entries)
    {
        std::printf("  %s · %s\n", Entry.Descriptor.Name.c_str(),
                    EncodeUtf8Path(Entry.Descriptor.DescriptorPath).c_str());
    }
    for (const std::string& Diagnostic : Enumeration.Diagnostics)
    {
        std::printf("  skipped: %s\n", Diagnostic.c_str());
    }
    if (Enumeration.Entries.empty())
    {
        std::printf("  (no projects decoded)\n");
    }
    return 0;
}
#endif

} // namespace
} // namespace Frontier

//========================================================================================================================
//                                            UNIFIED SDK WINDOWED LOOP
//========================================================================================================================

#if defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/DomainStudioPanels.h"
#include "Engine/Interface/LogStreamPanelRegistration.h"
#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Engine/Interface/SceneDirectoryPanelRegistration.h"
#include "Engine/Interface/SceneInspectorPanel.h"
#include "Engine/Interface/SceneInspectorPanelRegistration.h"
#include "Engine/Interface/StackRailControls.h"
#include "Engine/Interface/WindowsApplication.h"
#include "Engine/Rendering/DualTierRenderer.h"
#include "Engine/Scene/EnvironmentRecords.h"
#include "Engine/Scene/SceneDirectory.h"
#include "Engine/Scene/SceneRecord.h"
#include "Projects/Project-Everest/Source/ProjectEverestScene.h"
#include "UI/Editor/ControlCentrePanel.h"
#include "UI/Editor/ProjectBrowserPanel.h"

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>

namespace Frontier
{
namespace
{

// Reusable global environment bundle instantiated automatically for any non-Everest project so every Frontier
// project gets global Folders + Sun + Lens Flare + Atmosphere + Clouds + Wind out of the box.
struct StandardProjectEnvironment
{
    FolderRecord     EnvironmentFolder{"Environment", "ENVIRONMENT", 0xFF4FD18Fu};
    FolderRecord     ForcefieldFolder{"Forcefield", "FORCEFIELD", 0xFFACD8C7u};
    AtmosphereRecord Atmosphere;
    CloudRecord      Clouds;
    FogRecord        Fog;
    SunRecord        Sun;
    MoonRecord       Moon;
    StarsRecord      Stars;
    LensFlareRecord  LensFlare;
    WeatherRecord    Weather;
    WindRecord       Wind;

    StandardProjectEnvironment()
    {
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
        SceneDirectory& Directory = SceneDirectory::Instance();
        (void)Directory.ConfigureParent(Atmosphere.Token(), EnvironmentFolder.Token());
        (void)Directory.ConfigureParent(Clouds.Token(), EnvironmentFolder.Token());
        (void)Directory.ConfigureParent(Fog.Token(), EnvironmentFolder.Token());
        (void)Directory.ConfigureParent(Sun.Token(), EnvironmentFolder.Token());
        (void)Directory.ConfigureParent(Moon.Token(), EnvironmentFolder.Token());
        (void)Directory.ConfigureParent(Stars.Token(), EnvironmentFolder.Token());
        (void)Directory.ConfigureParent(LensFlare.Token(), EnvironmentFolder.Token());
        (void)Directory.ConfigureParent(Weather.Token(), EnvironmentFolder.Token());
        (void)Directory.ConfigureParent(Wind.Token(), ForcefieldFolder.Token());
#endif
    }
};

struct ActiveWorkspaceSession
{
    PanelDockspace                              Panels;
    LogStreamPanel                              LogPanel;
    DomainStudioPanel                           StudioPanel;
    SceneDirectoryPanel                         OutlinerPanel;
    SceneInspectorPanel                         InspectorPanel;
    std::unique_ptr<DualTierProvingGroundsRenderer> EverestRenderer;
    std::unique_ptr<ProjectEverestScene>        EverestScene;
    std::unique_ptr<StandardProjectEnvironment> StandardScene;
    std::vector<std::unique_ptr<FolderRecord>>  SpawnedFolders;
    std::vector<std::unique_ptr<SceneRecord>>   SpawnedRecords;
    ProjectDescriptor                           Descriptor;
    std::filesystem::path                       LayoutPath;
    double                                      CameraYawDegrees     = -36.0;
    double                                      CameraPitchDegrees   = 16.5;
    double                                      CameraDistanceMetres = 5.85;
    EverestViewportInputs                       CachedViewportInputs{};
    std::vector<SpectrumRgb>                    CachedViewportGrid;
    DualTierFrameTelemetry                      CachedTelemetry{};
    bool                                        ViewportCacheValid   = false;
    float                                       InterfaceScale       = 1.0f;
    bool                                        ShowDomainStudio     = false;
    bool                                        ReturnRequested      = false;
};

bool DecodeLaunchedProjectSelection(const std::filesystem::path& Chosen,
                                    const std::filesystem::path& FromExecutable,
                                    ProjectDescriptor&           OutDescriptor)
{
    if (Chosen.extension() == ".projectspace")
    {
        std::error_code Error;
        for (const std::filesystem::directory_entry& Candidate :
             std::filesystem::directory_iterator(Chosen.parent_path(), Error))
        {
            if (Candidate.is_regular_file(Error) && Candidate.path().extension() == ".frontier")
            {
                return PrepareProjectSession(Candidate.path(), FromExecutable, OutDescriptor);
            }
        }
        ProjectSpaceDocument Container;
        std::string          Diagnostic;
        if (!ProjectSpaceCodec::Decode(Chosen, Container, Diagnostic))
        {
            std::fprintf(stderr, "Frontier: cannot open projectspace: %s\n", Diagnostic.c_str());
            return false;
        }
        OutDescriptor                  = ProjectDescriptor{};
        OutDescriptor.Name             = Container.Sections["project"]["name"].value_or(std::string{"Untitled"});
        OutDescriptor.OpeningScene     = Container.Sections["engine"]["startup_level"].value_or(std::string{});
        OutDescriptor.DescriptorPath   = std::filesystem::absolute(Chosen, Error);
        OutDescriptor.ProjectDirectory = OutDescriptor.DescriptorPath.parent_path();
        ConfigureContentRoots(FromExecutable, OutDescriptor.ProjectDirectory / OutDescriptor.ContentLocation);
        ConfigureLogFile(OutDescriptor.ProjectDirectory / "Saved" / "Logs" / "Frontier.log");
        return true;
    }
    return PrepareProjectSession(Chosen, FromExecutable, OutDescriptor);
}

void ConstructWorkspaceViewport(ActiveWorkspaceSession& Session)
{
    const KitFaces Faces    = ResolveKitFaces();
    const KitTheme Theme    = ResolveKitTheme();
    ImDrawList*    Ink      = ImGui::GetWindowDrawList();
    const ImVec2   WinPos   = ImGui::GetWindowPos();
    const ImVec2   WinSize  = ImGui::GetWindowSize();
    const float    Left     = WinPos.x + 14.0f;
    const float    Top      = std::max(WinPos.y + 30.0f, ImGui::GetCursorScreenPos().y + 4.0f);
    const float    Width    = std::max(120.0f, WinSize.x - 28.0f);
    const float    Height   = std::max(120.0f, WinPos.y + WinSize.y - Top - 12.0f);

    Ink->AddRectFilled(ImVec2(WinPos.x, Top - 6.0f), ImVec2(WinPos.x + WinSize.x, WinPos.y + WinSize.y), Theme.PanelBg);

    // Top viewport bar: "< Browser", project title, GPU tier pill.
    bool BackHover = false;
    if (KitTarget("##ViewportBackBrowser", ImVec2(Left, Top), ImVec2(88.0f, 26.0f), BackHover,
                  "Return to Project Browser"))
    {
        Session.ReturnRequested = true;
    }
    KitPillSurface(Ink, ImVec2(Left, Top), ImVec2(Left + 88.0f, Top + 26.0f),
                   BackHover ? Theme.Surface3 : Theme.Surface2, Theme.Line, 999.0f);
    KitDrawText(Ink, Faces.Medium, 11.5f, ImVec2(Left + 14.0f, Top + 6.5f),
                BackHover ? Theme.Text : Theme.Muted, "< Browser");

    KitDrawText(Ink, Faces.SemiBold, 14.0f, ImVec2(Left + 100.0f, Top + 5.5f), Theme.Text,
                Session.Descriptor.Name.c_str());

    if (Session.EverestScene != nullptr)
    {
        const bool  RtxActive = (Session.EverestScene->Illumination.TierSelectionIndex == 0);
        const char* TierLabel = RtxActive ? "RTX RayQuery + ReSTIR" : "GTX SDF + BVH";
        const float TierW     = KitTextWidth(Faces.Medium, 10.8f, TierLabel) + 24.0f;
        const float TierX     = Left + Width - TierW;
        bool        TierHover = false;
        if (KitTarget("##ViewportTierToggle", ImVec2(TierX, Top), ImVec2(TierW, 26.0f), TierHover,
                      "Click to toggle RTX Raytracing vs GTX SDF + BVH Fallback"))
        {
            Session.EverestScene->Illumination.TierSelectionIndex = RtxActive ? 1 : 0;
        }
        const ImU32 TierAccent = RtxActive ? KitInkHex(0x2fe6a7) : KitInkHex(0x63a8ff);
        KitPillSurface(Ink, ImVec2(TierX, Top), ImVec2(TierX + TierW, Top + 26.0f),
                       KitInkWithAlpha(TierAccent, 0.16f), KitInkWithAlpha(TierAccent, 0.55f), 999.0f);
        Ink->AddCircleFilled(ImVec2(TierX + 11.0f, Top + 13.0f), 3.4f, TierAccent);
        KitDrawText(Ink, Faces.Medium, 10.8f, ImVec2(TierX + 19.0f, Top + 7.0f), Theme.Text, TierLabel);

        // Domain Studio Drawer Toggle Pill
        const float StudioW = 74.0f;
        const float StudioX = TierX - StudioW - 6.0f;
        bool        StHover = false;
        if (KitTarget("##ViewportStudioToggle", ImVec2(StudioX, Top), ImVec2(StudioW, 26.0f), StHover,
                      "Toggle Bottom Dock between Log Stream and Tier-3 Domain Studio"))
        {
            Session.ShowDomainStudio = !Session.ShowDomainStudio;
        }
        KitPillSurface(Ink, ImVec2(StudioX, Top), ImVec2(StudioX + StudioW, Top + 26.0f),
                       Session.ShowDomainStudio ? KitInkRgba(242, 166, 94, 48) : Theme.Surface2,
                       Session.ShowDomainStudio ? KitInkHex(0xf2a65e) : Theme.LineSoft, 999.0f);
        KitDrawText(Ink, Faces.Medium, 10.8f, ImVec2(StudioX + 14.0f, Top + 7.0f),
                    Session.ShowDomainStudio ? Theme.Text : Theme.Muted, "Studios");

        // Compact AOV Strip (Final, GI, Refl, SDF, Glass, Nrm)
        static constexpr std::array<const char*, 6> AovShortLabels{"Final", "GI", "Refl", "SDF", "Glass", "Nrm"};
        float AovCursor = StudioX - 8.0f;
        for (int Aov = 5; Aov >= 0; --Aov)
        {
            const float PW = KitTextWidth(Faces.Medium, 10.2f, AovShortLabels[static_cast<size_t>(Aov)]) + 14.0f;
            AovCursor -= PW;
            if (AovCursor > Left + 225.0f)
            {
                const bool Sel = (Session.EverestScene->Illumination.InspectionAovIndex == Aov);
                bool       Hov = false;
                char       Id[32] = {};
                std::snprintf(Id, sizeof(Id), "##VpAov%d", Aov);
                if (KitTarget(Id, ImVec2(AovCursor, Top + 1.0f), ImVec2(PW, 24.0f), Hov))
                {
                    Session.EverestScene->Illumination.InspectionAovIndex = Aov;
                }
                KitPillSurface(Ink, ImVec2(AovCursor, Top + 1.0f), ImVec2(AovCursor + PW, Top + 25.0f),
                               Sel ? KitInkWithAlpha(TierAccent, 0.22f) : Theme.Surface2,
                               Sel ? TierAccent : Theme.LineSoft, 6.0f);
                KitDrawText(Ink, Faces.Medium, 10.2f, ImVec2(AovCursor + 7.0f, Top + 7.0f),
                            Sel ? Theme.Text : Theme.Muted, AovShortLabels[static_cast<size_t>(Aov)]);
            }
            AovCursor -= 4.0f;
        }
    }

    // Live 3D Dual-Tier Proving Grounds Viewport (or Standard Environment Sky Canvas for non-Everest projects).
    const ImVec2 CanvasMin(Left, Top + 34.0f);
    const ImVec2 CanvasMax(Left + Width, Top + Height);
    KitPillSurface(Ink, CanvasMin, CanvasMax, KitInkHex(0x090d11), Theme.Line, 12.0f);

    if (Session.EverestScene != nullptr && Session.EverestRenderer != nullptr)
    {
        const float  CWidth  = std::max(32.0f, CanvasMax.x - CanvasMin.x - 6.0f);
        const float  CHeight = std::max(32.0f, CanvasMax.y - CanvasMin.y - 6.0f);
        const double Aspect  = static_cast<double>(CWidth / CHeight);

        // Orbit drag interaction inside 3D viewport!
        ImGui::SetCursorScreenPos(ImVec2(CanvasMin.x + 3.0f, CanvasMin.y + 3.0f));
        ImGui::InvisibleButton("##Everest3DOrbitSurface", ImVec2(CWidth, CHeight - 38.0f));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            const ImVec2 Delta         = ImGui::GetIO().MouseDelta;
            Session.CameraYawDegrees  += static_cast<double>(Delta.x) * 0.35;
            Session.CameraPitchDegrees = std::clamp(Session.CameraPitchDegrees + static_cast<double>(Delta.y) * 0.25,
                                                    3.0, 78.0);
        }

        const EverestViewportInputs Inputs = Session.EverestScene->BuildViewportInputs(
            Session.CameraYawDegrees, Session.CameraPitchDegrees, Session.CameraDistanceMetres);

        constexpr uint32_t GridCols = 280u;
        constexpr uint32_t GridRows = 156u;
        if (!Session.ViewportCacheValid ||
            std::memcmp(&Inputs, &Session.CachedViewportInputs, sizeof(EverestViewportInputs)) != 0 ||
            Session.CachedViewportGrid.size() != static_cast<size_t>((GridCols + 1u) * (GridRows + 1u)))
        {
            Session.EverestRenderer->RenderFrame(GridCols + 1u, GridRows + 1u, Inputs,
                                                 Session.CachedViewportGrid, Session.CachedTelemetry);
            Session.CachedViewportInputs = Inputs;
            Session.ViewportCacheValid   = true;
        }
        const std::vector<SpectrumRgb>& LinearGrid = Session.CachedViewportGrid;
        const DualTierFrameTelemetry&   Telemetry  = Session.CachedTelemetry;

        // Couple optical LensFlare overlay when FinalLit AOV is active and Sun projects onto sensor!
        double SunU = 0.24, SunV = 0.22;
        const SpatialVector FarSunPt{Inputs.SunDirection.X * 120.0, Inputs.SunDirection.Y * 120.0,
                                     Inputs.SunDirection.Z * 120.0};
        const bool SunInFront = Session.EverestRenderer->ProjectWorldPoint(FarSunPt, Inputs, Aspect, SunU, SunV);
        const LensFlareRecord& Flare = Session.EverestScene->LensFlare;

        auto ToSrgbByte = [](double Linear) -> int {
            const double Pos    = std::max(0.0, Linear);
            // ACES-inspired filmic shoulder so metallic car highlights & sky roll off cleanly
            const double Mapped = (Pos * (2.51 * Pos + 0.03)) / (Pos * (2.43 * Pos + 0.59) + 0.14);
            return std::clamp(static_cast<int>(std::pow(std::clamp(Mapped, 0.0, 1.0), 1.0 / 2.2) * 255.0 + 0.5), 0, 255);
        };

        std::vector<ImU32> PackedVertices(LinearGrid.size());
        for (uint32_t Row = 0; Row <= GridRows; ++Row)
        {
            const double V = static_cast<double>(Row) / static_cast<double>(GridRows);
            for (uint32_t Col = 0; Col <= GridCols; ++Col)
            {
                const double U   = static_cast<double>(Col) / static_cast<double>(GridCols);
                SpectrumRgb  Px  = LinearGrid[static_cast<size_t>(Row) * (GridCols + 1u) + Col];
                if (Inputs.InspectionMode == ViewportInspectionMode::FinalLit && Flare.Enabled &&
                    Inputs.SunEnabled && SunInFront && SunU > -0.15 && SunU < 1.15 && SunV > -0.15 && SunV < 1.15)
                {
                    const SpectrumRgb Opt = Flare.EvaluateOpticalSampleAtSource(
                        U, V, std::clamp(SunU, 0.04, 0.96), std::clamp(SunV, 0.04, 0.96), Aspect);
                    Px.R += Opt.R;
                    Px.G += Opt.G;
                    Px.B += Opt.B;
                }
                PackedVertices[static_cast<size_t>(Row) * (GridCols + 1u) + Col] =
                    KitInkRgba(ToSrgbByte(Px.R), ToSrgbByte(Px.G), ToSrgbByte(Px.B), 255);
            }
        }

        const float CellW = CWidth / static_cast<float>(GridCols);
        const float CellH = CHeight / static_cast<float>(GridRows);
        for (uint32_t Row = 0; Row < GridRows; ++Row)
        {
            for (uint32_t Col = 0; Col < GridCols; ++Col)
            {
                const ImVec2 PMin(CanvasMin.x + 3.0f + static_cast<float>(Col) * CellW,
                                  CanvasMin.y + 3.0f + static_cast<float>(Row) * CellH);
                const ImVec2 PMax(PMin.x + CellW + 0.4f, PMin.y + CellH + 0.4f);
                const size_t VertexOffset = static_cast<size_t>(Row) * (GridCols + 1u) + Col;
                Ink->AddRectFilledMultiColor(PMin, PMax, PackedVertices[VertexOffset],
                                             PackedVertices[VertexOffset + 1u],
                                             PackedVertices[VertexOffset + GridCols + 2u],
                                             PackedVertices[VertexOffset + GridCols + 1u]);
            }
        }

        // 3D Collision X-Ray Wireframe Overlay (chassis hull + 4 wheel contact hubs)!
        if (Inputs.CollisionOverlayEnabled)
        {
            auto ProjectScreen = [&](SpatialVector Pt, ImVec2& OutPx) -> bool {
                double U = 0.0, V = 0.0;
                if (!Session.EverestRenderer->ProjectWorldPoint(Pt, Inputs, Aspect, U, V))
                {
                    return false;
                }
                OutPx = ImVec2(CanvasMin.x + 3.0f + static_cast<float>(U) * CWidth,
                               CanvasMin.y + 3.0f + static_cast<float>(V) * CHeight);
                return U >= 0.0 && U <= 1.0 && V >= 0.0 && V <= 1.0;
            };
            const std::array<SpatialVector, 8> HullCorners{{
                {-0.96, -2.22, 0.20}, {+0.96, -2.22, 0.20}, {+0.96, +2.22, 0.20}, {-0.96, +2.22, 0.20},
                {-0.96, -2.22, 1.24}, {+0.96, -2.22, 1.24}, {+0.96, +2.22, 1.24}, {-0.96, +2.22, 1.24},
            }};
            constexpr int Edges[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
            for (const auto& Edge : Edges)
            {
                ImVec2 A{}, B{};
                if (ProjectScreen(HullCorners[Edge[0]], A) && ProjectScreen(HullCorners[Edge[1]], B))
                {
                    Ink->AddLine(A, B, KitInkHex(0xff6b8b), 1.6f);
                }
            }
            for (SpatialVector WC : {SpatialVector{-0.96, +1.38, 0.35}, SpatialVector{+0.96, +1.38, 0.35},
                                     SpatialVector{-0.96, -1.38, 0.35}, SpatialVector{+0.96, -1.38, 0.35}})
            {
                ImVec2 Hub{};
                if (ProjectScreen(WC, Hub))
                {
                    Ink->AddCircle(Hub, 14.0f, KitInkHex(0x2fe6a7), 20, 1.8f);
                }
            }
        }

        // Bottom 3D Render Telemetry Bar!
        char HudLine[220] = {};
        std::snprintf(HudLine, sizeof(HudLine),
                      "%s  \xC2\xB7  %u tris / %u SAH nodes  \xC2\xB7  4 SDF Cascades  \xC2\xB7  %u Card texels  \xC2\xB7  GI %.2f  \xC2\xB7  Refl %.0f%%",
                      Inputs.ExecutionMode == DualTierExecutionMode::HardwareRayQueryRtx ? "RTX TIER" : "GTX FALLBACK",
                      Telemetry.TriangleCount, Telemetry.BvhEnclosureCount, Telemetry.SurfaceCardTexels,
                      Telemetry.MeanIndirectLuminance, Telemetry.ReflectionHitFraction * 100.0);
        KitPillSurface(Ink, ImVec2(CanvasMin.x + 12.0f, CanvasMax.y - 34.0f),
                       ImVec2(CanvasMax.x - 12.0f, CanvasMax.y - 10.0f), KitInkRgba(10, 14, 18, 208),
                       KitInkRgba(255, 255, 255, 32), 999.0f);
        KitDrawText(Ink, Faces.Medium, 10.8f, ImVec2(CanvasMin.x + 22.0f, CanvasMax.y - 27.5f), KitInkHex(0xeef3f0),
                    HudLine);
        return;
    }

    const SunRecord*        Sun        = Session.EverestScene ? &Session.EverestScene->Sun
                                                              : (Session.StandardScene ? &Session.StandardScene->Sun : nullptr);
    const AtmosphereRecord* Atmosphere = Session.EverestScene
                                             ? &Session.EverestScene->Atmosphere
                                             : (Session.StandardScene ? &Session.StandardScene->Atmosphere : nullptr);
    const CloudRecord*      Clouds     = Session.EverestScene
                                             ? &Session.EverestScene->Clouds
                                             : (Session.StandardScene ? &Session.StandardScene->Clouds : nullptr);
    const LensFlareRecord*  Flare      = Session.EverestScene
                                             ? &Session.EverestScene->LensFlare
                                             : (Session.StandardScene ? &Session.StandardScene->LensFlare : nullptr);
    const WindRecord*       Wind       = Session.EverestScene
                                             ? &Session.EverestScene->Wind
                                             : (Session.StandardScene ? &Session.StandardScene->Wind : nullptr);

    if (Sun != nullptr && Atmosphere != nullptr)
    {
        const float         CWidth     = CanvasMax.x - CanvasMin.x - 6.0f;
        const float         CHeight    = CanvasMax.y - CanvasMin.y - 6.0f;
        const SpatialVector SunDir     = Sun->QuerySunDirection();
        const SpatialVector ObsKm{0.0, 0.0, Atmosphere->Parameters.GroundRadiusKm + 0.1};
        const double        Scale      = (Sun->Enabled ? Sun->SolarIlluminance : 0.0) * Atmosphere->SkyBrightnessScale;
        const SpectrumRgb   SolarTint  = SunRecord::EvaluateBlackbodyRgb(Sun->TemperatureKelvin);
        const SpectrumRgb   SunIrr{Scale * SolarTint.R, Scale * SolarTint.G, Scale * SolarTint.B};
        constexpr int       Cols       = 112;
        constexpr int       Rows       = 64;
        const float         CellW      = CWidth / static_cast<float>(Cols);
        const float         CellH      = CHeight / static_cast<float>(Rows);
        const double        Aspect     = static_cast<double>(CWidth / std::max(1.0f, CHeight));
        const double        SunScreenU = std::clamp(0.5 + (Sun->AzimuthDegrees - 145.0) / 180.0, 0.08, 0.92);
        const double        SunScreenV = std::clamp(0.68 - (Sun->ElevationDegrees / 90.0) * 0.58, 0.06, 0.92);

        auto ToByte = [](double Linear) -> int {
            const double Mapped = std::max(0.0, Linear) / (1.0 + std::max(0.0, Linear));
            return std::clamp(static_cast<int>(std::pow(Mapped, 1.0 / 2.2) * 255.0 + 0.5), 0, 255);
        };

        std::vector<ImU32> SkyVertexGrid(static_cast<size_t>((Cols + 1) * (Rows + 1)));
        for (int Row = 0; Row <= Rows; ++Row)
        {
            const double V        = static_cast<double>(Row) / static_cast<double>(Rows);
            const double ViewElev = (0.68 - V) * 1.45;
            for (int Col = 0; Col <= Cols; ++Col)
            {
                const double U      = static_cast<double>(Col) / static_cast<double>(Cols);
                SpectrumRgb  Colour = {0.015, 0.020, 0.024};

                if (ViewElev >= 0.0)
                {
                    if (Atmosphere->Enabled)
                    {
                        const double        ViewAz = (U - 0.5) * 2.2 + (Sun->AzimuthDegrees * 3.14159265358979323846 / 180.0);
                        const SpatialVector ViewDir{std::cos(ViewElev) * std::cos(ViewAz),
                                                    std::cos(ViewElev) * std::sin(ViewAz), std::sin(std::max(0.008, ViewElev))};
                        Colour = SingleScattering(Atmosphere->Parameters, ObsKm, ViewDir, SunDir, SunIrr,
                                                  Atmosphere->UseBakedLookup ? &Atmosphere->LookupTable : nullptr, 10);
                    }
                    if (Clouds != nullptr && Clouds->Enabled && ViewElev > 0.03)
                    {
                        const double CloudD = Clouds->SampleTopDownDensity(U, V * 1.4);
                        const double Mix    = std::clamp(CloudD * 0.78 * std::min(1.0, (ViewElev - 0.03) * 8.0), 0.0, 0.88);
                        const double Lit    = 0.25 + 0.75 * std::clamp((Sun->ElevationDegrees + 8.0) / 48.0, 0.05, 1.0);
                        Colour.R            = Colour.R * (1.0 - Mix) + Lit * SolarTint.R * Mix;
                        Colour.G            = Colour.G * (1.0 - Mix) + Lit * SolarTint.G * Mix;
                        Colour.B            = Colour.B * (1.0 - Mix) + Lit * SolarTint.B * Mix;
                    }
                    if (Sun->Enabled && Sun->DiscEnabled && Sun->ElevationDegrees > -2.0)
                    {
                        const double Dx   = (U - SunScreenU) * Aspect;
                        const double Dy   = (V - SunScreenV);
                        const double Dist = std::sqrt(Dx * Dx + Dy * Dy);
                        const double Disc = std::exp(-Dist * Dist / 0.00045);
                        const double Glow = std::exp(-Dist * 11.0) * 0.45 * Sun->OpticalGlow;
                        Colour.R += SolarTint.R * (Disc * 4.2 + Glow);
                        Colour.G += SolarTint.G * (Disc * 4.0 + Glow * 0.92);
                        Colour.B += SolarTint.B * (Disc * 3.6 + Glow * 0.80);
                    }
                }
                else
                {
                    const double DepthFade = std::clamp((V - 0.68) / 0.32, 0.0, 1.0);
                    const double Ambient   = 0.03 + 0.14 * std::clamp((Sun->ElevationDegrees + 6.0) / 50.0, 0.0, 1.0);
                    Colour = SpectrumRgb{Ambient * (0.55 + 0.35 * DepthFade), Ambient * (0.60 + 0.35 * DepthFade),
                                         Ambient * (0.62 + 0.32 * DepthFade)};
                }

                if (Flare != nullptr && Flare->Enabled && Sun->Enabled && Sun->ElevationDegrees > -2.0)
                {
                    const SpectrumRgb FlareRgb =
                        Flare->EvaluateOpticalSampleAtSource(U, V, SunScreenU, SunScreenV, Aspect);
                    Colour.R += FlareRgb.R;
                    Colour.G += FlareRgb.G;
                    Colour.B += FlareRgb.B;
                }

                SkyVertexGrid[static_cast<size_t>(Row * (Cols + 1) + Col)] =
                    KitInkRgba(ToByte(Colour.R), ToByte(Colour.G), ToByte(Colour.B), 255);
            }
        }

        for (int Row = 0; Row < Rows; ++Row)
        {
            const bool CrossesBoundary = ((static_cast<double>(Row) / Rows) < 0.68 &&
                                          (static_cast<double>(Row + 1) / Rows) > 0.68);
            for (int Col = 0; Col < Cols; ++Col)
            {
                const ImVec2 PMin(CanvasMin.x + 3.0f + static_cast<float>(Col) * CellW,
                                  CanvasMin.y + 3.0f + static_cast<float>(Row) * CellH);
                const ImVec2 PMax(PMin.x + CellW, PMin.y + CellH);
                const ImU32  C00 = SkyVertexGrid[static_cast<size_t>(Row * (Cols + 1) + Col)];
                const ImU32  C10 = SkyVertexGrid[static_cast<size_t>(Row * (Cols + 1) + Col + 1)];
                const ImU32  C11 = CrossesBoundary ? C10 : SkyVertexGrid[static_cast<size_t>((Row + 1) * (Cols + 1) + Col + 1)];
                const ImU32  C01 = CrossesBoundary ? C00 : SkyVertexGrid[static_cast<size_t>((Row + 1) * (Cols + 1) + Col)];
                Ink->AddRectFilledMultiColor(PMin, PMax, C00, C10, C11, C01);
            }
        }

        // Bottom HUD telemetry bar inside Viewport.
        char HudLine[192] = {};
        std::snprintf(HudLine, sizeof(HudLine),
                      "Sun %.1f\xC2\xB0 / %.0f K  \xC2\xB7  Clouds %d%% (%s)  \xC2\xB7  Wind %.1f m/s FROM %.0f\xC2\xB0  \xC2\xB7  Flare %s",
                      Sun->ElevationDegrees, Sun->TemperatureKelvin,
                      Clouds ? static_cast<int>(std::lround(Clouds->Coverage * 100.0)) : 0,
                      Clouds ? Clouds->QueryFormationName() : "Off", Wind ? Wind->ReferenceSpeedMps : 0.0,
                      Wind ? Wind->QueryFromBearingDegrees() : 0.0,
                      (Flare && Flare->Enabled) ? "Active" : "Bypassed");
        KitPillSurface(Ink, ImVec2(CanvasMin.x + 12.0f, CanvasMax.y - 34.0f),
                       ImVec2(CanvasMax.x - 12.0f, CanvasMax.y - 10.0f), KitInkRgba(10, 14, 18, 196),
                       KitInkRgba(255, 255, 255, 28), 999.0f);
        KitDrawText(Ink, Faces.Medium, 11.0f, ImVec2(CanvasMin.x + 24.0f, CanvasMax.y - 27.5f), KitInkHex(0xeef3f0),
                    HudLine);
    }
}

int ExecuteUnifiedWindowLoop(const std::filesystem::path& FromExecutable,
                             const ProjectDescriptor*     PreloadedProject)
{
    GLFWwindow*                             WindowHandle = nullptr;
    std::unique_ptr<ControlCentrePanel>     Centre;
    ProjectBrowserPanel                     Browser;
    std::unique_ptr<ActiveWorkspaceSession> ActiveSession;
    const std::filesystem::path             ProjectsDirectory = ResolveProjectsDirectory(FromExecutable);
    bool                                    BrowserPrepared   = false;

    (void)Browser.AttachProjectsDirectory(ProjectsDirectory);

    auto ActivateWorkspaceForDescriptor = [&](const ProjectDescriptor& Descriptor) {
        if (Centre)
        {
            (void)Centre->Finalize();
            Centre.reset();
        }
        ActiveSession.reset();

        Centre = std::make_unique<ControlCentrePanel>(
            "EngineContent/FontArchives", Descriptor.ProjectDirectory / "Saved" / "ControlCentre.toml");
        (void)Centre->Initialize();
        (void)Centre->Activate();

        ActiveSession             = std::make_unique<ActiveWorkspaceSession>();
        ActiveSession->Descriptor = Descriptor;
        ActiveSession->LayoutPath = Descriptor.ProjectDirectory / "Saved" / "WorkspaceLayout.toml";

        if (Descriptor.Name.find("Everest") != std::string::npos ||
            EncodeUtf8Path(Descriptor.ProjectDirectory).find("Everest") != std::string::npos)
        {
            ActiveSession->EverestRenderer = std::make_unique<DualTierProvingGroundsRenderer>();
            ActiveSession->EverestScene    = std::make_unique<ProjectEverestScene>();
            ActiveSession->EverestScene->BindDomainStudio(ActiveSession->StudioPanel);
        }
        else
        {
            ActiveSession->StandardScene = std::make_unique<StandardProjectEnvironment>();
        }

        ActiveSession->Panels.ConfigureDockspaceKey("Frontier project panels");
        DockPanelSpecification ViewportSpec;
        ViewportSpec.Key              = "Viewport";
        ViewportSpec.Caption          = "Viewport";
        ViewportSpec.ConstructContent = [&ActiveSession]() {
            if (ActiveSession)
            {
                ConstructWorkspaceViewport(*ActiveSession);
            }
        };
        ViewportSpec.Open = true;
        (void)ActiveSession->Panels.Register(ViewportSpec);

        DockPanelSpecification BottomDrawerSpec = MakeLogStreamPanelSpecification(ActiveSession->LogPanel);
        BottomDrawerSpec.ConstructContent = [&ActiveSession]() {
            if (!ActiveSession)
            {
                return;
            }
            if (ActiveSession->StudioPanel.ConsumeFocusRequest())
            {
                ActiveSession->ShowDomainStudio = true;
            }
            if (ActiveSession->ShowDomainStudio)
            {
                ActiveSession->StudioPanel.ConstructInterface();
            }
            else
            {
                ActiveSession->LogPanel.ConstructInterface();
            }
        };
        (void)ActiveSession->Panels.Register(BottomDrawerSpec);
        (void)ActiveSession->Panels.Register(MakeSceneDirectoryPanelSpecification(ActiveSession->OutlinerPanel));
        (void)ActiveSession->Panels.Register(
            MakeSceneInspectorPanelSpecification(ActiveSession->InspectorPanel, ActiveSession->OutlinerPanel));

        ActiveSession->Panels.ConfigureWorkspaceInsets(79.0f, 0.0f);
        ActiveSession->Panels.ConfigureTabExtent(132.0f, 26.0f);
        (void)ActiveSession->Panels.ConfigureStackedPanels("Viewport", "LogStream", 460.0f);
        (void)ActiveSession->Panels.ConfigureColumns(
            {{"Outliner", 0.24f}, {"LogStream", 0.46f}, {"Inspector", 0.30f}});

#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
        ActiveSession->OutlinerPanel.Synchronize(SceneDirectory::Instance());
        const RecordToken DefaultToken = SceneDirectory::Instance().FindTokenByIdentifier("sun");
        if (DefaultToken.IsValid())
        {
            ActiveSession->OutlinerPanel.Select(DefaultToken);
            ActiveSession->OutlinerPanel.Synchronize(SceneDirectory::Instance());
        }
#endif

        if (WindowHandle != nullptr)
        {
            glfwSetWindowTitle(WindowHandle, ("Frontier · " + Descriptor.Name).c_str());
        }
    };

    ApplicationWindowCallbacks Callbacks;
    Callbacks.Title = PreloadedProject != nullptr ? ("Frontier · " + PreloadedProject->Name)
                                                  : std::string("Frontier · Project Browser");
    Callbacks.Initialize = [&WindowHandle](GLFWwindow* CreatedWindow) { WindowHandle = CreatedWindow; };
    Callbacks.InitializeVulkan = [](VulkanPresentation&) {
        WriteLog(QueryEngineLogToken("Render"), LogSeverity::Info, LogVerbosity::Log,
                 "Vulkan presentation initialising", "WorkspaceApplication.cpp");
    };
    Callbacks.ConfigurePlatform = [&]() {
        if (!ActiveSession)
        {
            const std::filesystem::path Picked = Browser.ConsumeLaunchPath();
            if (!Picked.empty())
            {
                ProjectDescriptor Decoded;
                if (DecodeLaunchedProjectSelection(Picked, FromExecutable, Decoded))
                {
                    ActivateWorkspaceForDescriptor(Decoded);
                }
            }
        }
        else if (ActiveSession->ReturnRequested)
        {
            ActiveSession.reset();
            if (Centre)
            {
                (void)Centre->Finalize();
                Centre.reset();
            }
            Centre = std::make_unique<ControlCentrePanel>(
                "EngineContent/FontArchives", ProjectsDirectory / "Saved" / "BrowserAppearance.toml");
            (void)Centre->Initialize();
            (void)Centre->Activate();
            Browser.ResetSelection();
            if (WindowHandle != nullptr)
            {
                glfwSetWindowTitle(WindowHandle, "Frontier · Project Browser");
            }
        }
        if (ActiveSession)
        {
            (void)ActiveSession->Panels.ConfigurePlatform();
        }
    };
    Callbacks.RecoverWindows = [&]() {
        if (ActiveSession)
        {
            ActiveSession->Panels.RecoverPlatformWindows();
        }
    };
    Callbacks.PrepareInterface = [&]() {
        if (!BrowserPrepared)
        {
            BrowserPrepared = true;
            Browser.ConfigurePreferences(ProjectsDirectory / "Saved" / "ProjectBrowserPrefs.toml");
            if (PreloadedProject != nullptr)
            {
                ActivateWorkspaceForDescriptor(*PreloadedProject);
            }
            else
            {
                Centre = std::make_unique<ControlCentrePanel>(
                    "EngineContent/FontArchives", ProjectsDirectory / "Saved" / "BrowserAppearance.toml");
                (void)Centre->Initialize();
                (void)Centre->Activate();
            }
        }
        if (Centre)
        {
            Centre->PrepareInterface();
        }
    };
    Callbacks.ConstructInterface = [&](bool PrimaryWindowVisible) {
        if (ActiveSession && Centre)
        {
            const double ElapsedSeconds = ImGui::GetIO().DeltaTime;
            ActiveSession->LogPanel.Synchronize(LogStore(), LogCategories(), ElapsedSeconds);
            if (ActiveSession->EverestScene)
            {
                ActiveSession->EverestScene->ServiceSpawnRequest(ActiveSession->OutlinerPanel);
            }
            else
            {
                while (const std::optional<RecordClassification> Spawn =
                           ActiveSession->OutlinerPanel.TakeSpawnRequest())
                {
                    if (*Spawn == RecordClassification::Folder)
                    {
                        ActiveSession->SpawnedFolders.push_back(
                            std::make_unique<FolderRecord>("New Folder", "GROUP", 0xFFA3D97Au));
                    }
                    else
                    {
                        ActiveSession->SpawnedRecords.push_back(std::make_unique<SceneRecord>(*Spawn));
                    }
                }
            }
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
            ActiveSession->OutlinerPanel.Synchronize(SceneDirectory::Instance());
#endif
            const float ScalePercent      = Centre->QueryAppearance().QueryApplied().InterfaceScale;
            ActiveSession->InterfaceScale = std::clamp(ScalePercent / 100.0f, 0.6f, 2.0f);
            (void)Centre->Integrate(ElapsedSeconds);
            ActiveSession->Panels.ConstructInterface(ActiveSession->InterfaceScale);
            if (PrimaryWindowVisible)
            {
                Centre->ConstructInterface();
            }
        }
        else if (PrimaryWindowVisible)
        {
            Browser.ConfigurePalette(ControlKit::Palette());
            Browser.ConstructInterface();
        }
    };
    Callbacks.Finalize = [&]() {
        if (ActiveSession)
        {
            std::error_code Error;
            std::filesystem::create_directories(ActiveSession->LayoutPath.parent_path(), Error);
            std::ofstream Out(ActiveSession->LayoutPath, std::ios::trunc);
            if (Out.is_open())
            {
                Out << ActiveSession->Panels.EncodeLayout();
            }
            ActiveSession.reset();
        }
        if (Centre)
        {
            (void)Centre->Finalize();
            Centre.reset();
        }
    };

    return RunWindowsApplication(Callbacks);
}

} // namespace
} // namespace Frontier

#endif // FRONTIER_WITH_INTERFACE_SDK

//========================================================================================================================
//                                             WORKSPACE APPLICATION LIFECYCLE
//========================================================================================================================

namespace Frontier
{

bool WorkspaceApplication::Initialize(int ArgumentCount, const char* const* Arguments)
{
    Request          = DecodeCommandLine(ArgumentCount, Arguments);
    ExecutableFolder = ResolveExecutableFolder(ArgumentCount > 0 ? Arguments[0] : nullptr);

    if (Request.Mode == LaunchMode::Help)
    {
        std::printf("%s", LaunchUsageText());
        ImmediateExitCode = 0;
        ImmediateExit     = true;
        return true;
    }

    if (Request.Mode == LaunchMode::Invalid)
    {
        std::fprintf(stderr, "Frontier: %s\n%s", Request.Diagnostic.c_str(), LaunchUsageText());
        ImmediateExitCode = 2;
        ImmediateExit     = true;
        return false;
    }

#if defined(FRONTIER_WITH_INTERFACE_SDK)
    if (Request.Mode == LaunchMode::Project)
    {
        if (!PrepareProjectSession(Request.ProjectDescriptor, ExecutableFolder, InitialProject))
        {
            ImmediateExitCode = 1;
            ImmediateExit     = true;
            return false;
        }
        InitialProjectPrepared = true;
    }
#endif
    return true;
}

int WorkspaceApplication::Loop()
{
    if (ImmediateExit)
    {
        return ImmediateExitCode;
    }

    if (Request.Mode == LaunchMode::Headless)
    {
        return ExecuteHeadlessSession(Request.ProjectDescriptor, ExecutableFolder);
    }

#if defined(FRONTIER_WITH_INTERFACE_SDK)
    return ExecuteUnifiedWindowLoop(ExecutableFolder, InitialProjectPrepared ? &InitialProject : nullptr);
#else
    if (Request.Mode == LaunchMode::Project)
    {
        std::printf("Frontier: built without the interface SDK; running the headless diagnostic session instead.\n");
        return ExecuteHeadlessSession(Request.ProjectDescriptor, ExecutableFolder);
    }
    return ExecuteBrowserListing(ExecutableFolder);
#endif
}

void WorkspaceApplication::Finalize()
{
    InitialProjectPrepared = false;
}

int ExecuteWorkspaceApplication(int ArgumentCount, const char* const* Arguments)
{
    WorkspaceApplication Application;
    (void)Application.Initialize(ArgumentCount, Arguments);
    const int ExitStatus = Application.Loop();
    Application.Finalize();
    return ExitStatus;
}

} // namespace Frontier

//------------------------------------------------------------------------------------------------------------------------
//                                                 STANDARD C++ LINKER ENTRY
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** Arguments)
{
    return Frontier::ExecuteWorkspaceApplication(ArgumentCount, Arguments);
}
