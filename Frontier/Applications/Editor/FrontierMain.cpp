/*==========================================================================================================================================
                                                             APPLICATION ENTRY
                                     ------------------------------------------------------------------
                             The project browser opens first and closes behind the project window it launches.
==========================================================================================================================================*/

// 🧩 Application entry: the project browser opens first and closes behind the project window it launches.

#include "CommandLine.h"
#include "ProjectBrowserHost.h"

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Diagnostics/LogStream.h"
#include "Engine/Interface/ProjectContent.h"
#include "Engine/Interface/LogStreamPanel.h"
#include "Engine/Interface/ProjectDescriptor.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <system_error>

namespace Frontier
{
namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::filesystem::path ExecutableDirectory(const char* ArgumentZero)
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
    return Candidates[3];   // [path] - Repository layout default, reported in enumeration diagnostics when missing
}

uint32_t EngineToken(const char* Name)
{
    const LogCategory* Category = LogCategories().QueryByName(Name);
    return Category != nullptr ? Category->Token : 0;
}

// Opens everything a project session needs before any window exists. Returns false with a printed diagnostic.
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
    WriteLog(EngineToken("Interface"), LogSeverity::Info, LogVerbosity::Display, Line, "FrontierMain.cpp");

    const std::filesystem::path EngineContent = QueryEngineContentDirectory();
    std::snprintf(Line, sizeof(Line), "Content roots configured · project %s · engine %s",
                  OutDescriptor.ContentLocation.c_str(),
                  EngineContent.empty() ? "(none staged)" : EncodeUtf8Path(EngineContent).c_str());
    WriteLog(EngineToken("Content"), LogSeverity::Info, LogVerbosity::Log, Line, "FrontierMain.cpp");

    std::snprintf(Line, sizeof(Line), "Log categories registered · %u declared by project · %u total",
                  Declared, LogCategories().QueryCount());
    WriteLog(EngineToken("Interface"), LogSeverity::Info, LogVerbosity::Log, Line, "FrontierMain.cpp");
    return true;
}

int RunHeadlessSession(const std::filesystem::path& DescriptorPath, const std::filesystem::path& FromExecutable)
{
    ProjectDescriptor Descriptor;
    if (!PrepareProjectSession(DescriptorPath, FromExecutable, Descriptor))
    {
        return 1;
    }

    WriteLog(EngineToken("Interface"), LogSeverity::Info, LogVerbosity::Display,
             "Headless diagnostic session · no scene renderer invoked", "FrontierMain.cpp");

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
                Panel.QueryVisible().size(),
                LogCategories().QueryCount(),
                static_cast<unsigned long long>(Panel.QueryDroppedCount()));
    std::printf("  session log: %s\n",
                EncodeUtf8Path(Descriptor.ProjectDirectory / "Saved" / "Logs" / "Frontier.log").c_str());
    return 0;
}

#if !defined(FRONTIER_WITH_INTERFACE_SDK)
int RunBrowserListing(const std::filesystem::path& FromExecutable)
{
    const std::filesystem::path    Projects    = ResolveProjectsDirectory(FromExecutable);
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
#endif // !FRONTIER_WITH_INTERFACE_SDK

} // namespace
} // namespace Frontier

//------------------------------------------------------------------------------------------------------------------------
//                                                 WINDOWED SESSION (SDK)
//------------------------------------------------------------------------------------------------------------------------

// 🚧 The windowed path requires the interface SDK (production Interface/WindowsApplication, pinned ImGui/GLFW):
//    define FRONTIER_WITH_INTERFACE_SDK and link the host/presentation units. Its shared contracts are based on
//    SultanAladin/Frontier `arena/11f31835-frontier` @71e4a17ed543325061b6e5f392476f5c9fd26699. The project
//    window composes the production ControlCentrePanel over a host-owned PanelDockspace, with no project-summary
//    header. This entry point is compile-checked by the FrontierNativeEntry object target; an OS window has not
//    been launched in this environment.
#if defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/LogStreamPanelRegistration.h"
#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Engine/Interface/SceneDirectoryPanelRegistration.h"
#include "Engine/Interface/SceneInspectorPanel.h"
#include "Engine/Interface/IconPresentation.h"
#include "Engine/Scene/EnvironmentRecords.h"
#include "Engine/Scene/SceneDirectory.h"
#include "Engine/Scene/SceneRecord.h"
#include "Engine/Interface/WindowsApplication.h"
#include "UI/Editor/ControlCentrePanel.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <fstream>
#include <memory>

namespace Frontier
{
namespace
{

struct BrowserSession
{
    GLFWwindow*               Window = nullptr;
    ProjectBrowserEnumeration Listing;
    std::filesystem::path     Chosen;
};

struct WorkspaceSession
{
    GLFWwindow*                        Window = nullptr;
    std::unique_ptr<ControlCentrePanel> Centre;          // Notch tab + appearance + fonts (TextureStudio pattern)
    PanelDockspace                     Panels;           // Outliner | (Viewport over Log Stream) | Inspector
    LogStreamPanel                     LogPanel;
    SceneDirectoryPanel                OutlinerPanel;
    // 📝 Ported environment records: construction registers them in the Outliner; in the editor
    //    profile each also builds its own Inspector description (Sun/sky/clouds/fog/wind scene).
    SunRecord                          Sun;
    AtmosphereRecord                   Atmosphere;
    CloudRecord                        Clouds;
    FogRecord                          Fog;
    WindRecord                         Wind;
    std::vector<std::unique_ptr<SceneRecord>> SpawnedRecords;   // Editor-spawned records live with the session
    std::string                        ProjectName;
    std::string                        ProjectLocation;  // [utf8]
    std::filesystem::path              LayoutPath;
    float                              InterfaceScale = 1.0f;
    bool                               Prepared        = false;
};

// The Viewport is an intentional placeholder for this UI-first slice. No project scene, render graph,
// GPU resource, or renderer is constructed here; this panel makes no rendering claim.
void ConstructViewportPanel(const WorkspaceSession& Session)
{
    const ImGuiStyle& Style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28, 24));
    ImGui::TextColored(Style.Colors[ImGuiCol_TextDisabled], "SCENE CANVAS");
    ImGui::Spacing();
    ImGui::TextWrapped("This project has no opening scene. The VehicleHMI viewport and ReSTIR renderer are not "
                       "connected in this UI port; the canvas remains an honest empty workspace until the native "
                       "scene and renderer work is reviewed.");
    ImGui::Separator();
    const auto Row = [](const char* Label, const std::string& Value) {
        ImGui::TextDisabled("%s", Label);
        ImGui::SameLine(150);
        ImGui::TextWrapped("%s", Value.empty() ? "\u2014" : Value.c_str());
    };
    Row("Project", Session.ProjectName);
    Row("Location", Session.ProjectLocation);
    ImGui::PopStyleVar();
}

int RunBrowserWindow(const std::filesystem::path& FromExecutable, std::filesystem::path& ChosenDescriptor)
{
    BrowserSession Session;
    Session.Listing = EnumerateProjects(ResolveProjectsDirectory(FromExecutable));

    ApplicationWindowCallbacks Callbacks;
    Callbacks.Title      = "Frontier · Project Browser";
    Callbacks.Initialize = [&Session](GLFWwindow* Window) { Session.Window = Window; };
    Callbacks.ConstructInterface = [&Session](bool) {
        const ImVec2 Display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(Display);
        if (ImGui::Begin("Project Browser", nullptr, ImGuiWindowFlags_NoDecoration))
        {
            ImGui::TextUnformatted("Open a project");
            ImGui::Separator();
            for (const ProjectBrowserEntry& Entry : Session.Listing.Entries)
            {
                if (ImGui::Button(Entry.Descriptor.Name.c_str(), ImVec2(420.0f, 0.0f)))
                {
                    Session.Chosen = Entry.Descriptor.DescriptorPath;
                    // 📝 The browser window closes behind the project window it launches.
                    glfwSetWindowShouldClose(Session.Window, GLFW_TRUE);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%s", EncodeUtf8Path(Entry.Descriptor.ProjectDirectory).c_str());
            }
            for (const std::string& Diagnostic : Session.Listing.Diagnostics)
            {
                ImGui::TextDisabled("skipped: %s", Diagnostic.c_str());
            }
        }
        ImGui::End();
    };

    const int Result = RunWindowsApplication(Callbacks);
    ChosenDescriptor = Session.Chosen;
    return Result;
}

int RunProjectWindow(const ProjectDescriptor& Descriptor, const std::filesystem::path& FromExecutable)
{
    (void)FromExecutable;

    WorkspaceSession Session;
    Session.ProjectName     = Descriptor.Name;
    Session.ProjectLocation = EncodeUtf8Path(Descriptor.ProjectDirectory);
    Session.LayoutPath      = Descriptor.ProjectDirectory / "Saved" / "WorkspaceLayout.toml";

    ApplicationWindowCallbacks Callbacks;
    Callbacks.Title            = "Frontier · " + Descriptor.Name;
    Callbacks.Initialize       = [&Session](GLFWwindow* Window) { Session.Window = Window; };
    Callbacks.InitializeVulkan = [](VulkanPresentation&) {
        WriteLog(LogCategories().QueryByName("Render") != nullptr ? LogCategories().QueryByName("Render")->Token : 0,
                 LogSeverity::Info, LogVerbosity::Log, "Vulkan presentation initialising", "FrontierMain.cpp");
    };
    Callbacks.ConfigurePlatform = [&Session]() { Session.Panels.ConfigurePlatform(); };
    Callbacks.RecoverWindows    = [&Session]() { Session.Panels.RecoverPlatformWindows(); };
    Callbacks.PrepareInterface  = [&Session, &Descriptor]() {
        if (Session.Prepared)
        {
            return;
        }
        Session.Prepared = true;

        Session.Centre = std::make_unique<ControlCentrePanel>(
            "EngineContent/FontArchives", Descriptor.ProjectDirectory / "Saved" / "ControlCentre.toml",
            Descriptor.ProjectDirectory);
        (void)Session.Centre->Initialize();
        (void)Session.Centre->Activate();
        IconPresentation::AttachVehicleHmiOutliner();

        Session.Panels.ConfigureDockspaceKey("Frontier project panels");
        DockPanelSpecification ViewportPanel;
        ViewportPanel.Key              = "Viewport";
        ViewportPanel.Caption          = "Viewport";
        ViewportPanel.ConstructContent = [&Session]() { ConstructViewportPanel(Session); };
        ViewportPanel.Open             = true;
        (void)Session.Panels.Register(ViewportPanel);
        (void)Session.Panels.Register(MakeLogStreamPanelSpecification(Session.LogPanel));
        (void)Session.Panels.Register(MakeSceneDirectoryPanelSpecification(Session.OutlinerPanel));
        DockPanelSpecification InspectorPanel;
        InspectorPanel.Key              = "Inspector";
        InspectorPanel.Caption          = "Inspector";
        InspectorPanel.ConstructContent = [&Session]() { ConstructSceneInspectorInterface(Session.OutlinerPanel); };
        InspectorPanel.Open             = true;
        (void)Session.Panels.Register(InspectorPanel);
        (void)SceneDirectory::Instance().ConfigureName(Session.Sun.Token(), "Sun");
        (void)SceneDirectory::Instance().ConfigureName(Session.Atmosphere.Token(), "Atmosphere");
        (void)SceneDirectory::Instance().ConfigureName(Session.Clouds.Token(), "Clouds");
        (void)SceneDirectory::Instance().ConfigureName(Session.Fog.Token(), "Fog");
        (void)SceneDirectory::Instance().ConfigureName(Session.Wind.Token(), "Wind");
        Session.OutlinerPanel.ConfigureWorkspaceContext(
            Descriptor.Name, Descriptor.OpeningScene.empty() ? "No opening scene · canvas idle"
                                                             : "Scene loader disconnected · canvas idle");
        Session.OutlinerPanel.Synchronize(SceneDirectory::Instance());
        (void)Session.OutlinerPanel.SelectByStableId("sun");
        Session.Panels.ConfigureWorkspaceInsets(79.0f, 0.0f);   // [px] - Top band for the notch tab (TextureStudio)
        Session.Panels.ConfigureTabExtent(140.0f, 26.0f);       // [px]
        // 📝 The viewport and Log Stream share the middle column; Outliner and Inspector flank it.
        const bool StackConfigured = Session.Panels.ConfigureStackedPanels("Viewport", "LogStream", 560.0f);
        const bool ColumnsConfigured = Session.Panels.ConfigureColumns(
            {{"Outliner", 0.2f}, {"LogStream", 0.56f}, {"Inspector", 0.24f}});
        if (!StackConfigured || !ColumnsConfigured)
        {
            WriteLog(EngineToken("Interface"), LogSeverity::Error, LogVerbosity::Log,
                     "Project workspace default layout was rejected", "FrontierMain.cpp");
        }

        std::error_code Error;
        if (std::filesystem::is_regular_file(Session.LayoutPath, Error))
        {
            try
            {
                const toml::table Saved = toml::parse_file(EncodeUtf8Path(Session.LayoutPath));
                std::string       Diagnostic;
                Session.Panels.ConfigureLayout(Saved, Diagnostic);
            }
            catch (const toml::parse_error&)
            {
                Session.Panels.ResetLayout();
            }
        }
        // 🚧 Project code-image loading (LoadLibrary + ConstructProjectInterchange negotiation + per-frame
        //    AdvanceProject) lands with the host SDK header; the descriptor's declared categories are already
        //    registered by PrepareProjectSession either way.
    };
    Callbacks.ConstructInterface = [&Session](bool) {
        if (!Session.Centre)
        {
            return;
        }
        const ImGuiIO& InputExchange = ImGui::GetIO();
        const double   Δτ            = InputExchange.DeltaTime;   // [s]

        Session.LogPanel.Synchronize(LogStore(), LogCategories(), Δτ);

        // Service Outliner spawn requests: the panel asks, the host creates the record (it then appears
        //    in the directory by construction). Spawned placeholders live for the session.
        while (const std::optional<RecordClassification> Spawn = Session.OutlinerPanel.TakeSpawnRequest())
        {
            auto Record = std::make_unique<SceneRecord>(*Spawn);
            (void)SceneDirectory::Instance().ConfigureName(
                Record->Token(), std::string(RecordClassificationName(*Spawn)) + " (spawned)");
            Session.SpawnedRecords.push_back(std::move(Record));
        }
        Session.OutlinerPanel.Synchronize(SceneDirectory::Instance());

        Session.Centre->PrepareInterface();
        const float InterfacePercent = Session.Centre->QueryAppearance().QueryApplied().InterfaceScale;   // [%]
        Session.InterfaceScale       = std::clamp(InterfacePercent / 100.0f, 0.6f, 2.0f);
        (void)Session.Centre->Integrate(Δτ);
        Session.Panels.ConstructInterface(Session.InterfaceScale);
        Session.Centre->ConstructInterface();   // Trapezoidal notch tab + drawer, drawn over the panels
    };
    Callbacks.Finalize = [&Session]() {
        std::error_code Error;
        std::filesystem::create_directories(Session.LayoutPath.parent_path(), Error);
        std::ofstream Out(Session.LayoutPath, std::ios::trunc);
        if (Out.is_open())
        {
            Out << Session.Panels.EncodeLayout();
        }
    };

    return RunWindowsApplication(Callbacks);
}

} // namespace
} // namespace Frontier

#endif // FRONTIER_WITH_INTERFACE_SDK

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** Arguments)
{
    using namespace Frontier;

    const LaunchRequest         Request   = DecodeCommandLine(ArgumentCount, Arguments);
    const std::filesystem::path FromExecutable = ExecutableDirectory(ArgumentCount > 0 ? Arguments[0] : nullptr);

    switch (Request.Mode)
    {
        case LaunchMode::Help:
            std::printf("%s", LaunchUsageText());
            return 0;

        case LaunchMode::Invalid:
            std::fprintf(stderr, "Frontier: %s\n%s", Request.Diagnostic.c_str(), LaunchUsageText());
            return 2;

        case LaunchMode::Headless:
            return RunHeadlessSession(Request.ProjectDescriptor, FromExecutable);

        case LaunchMode::Project:
        {
#if defined(FRONTIER_WITH_INTERFACE_SDK)
            ProjectDescriptor Descriptor;
            if (!PrepareProjectSession(Request.ProjectDescriptor, FromExecutable, Descriptor))
            {
                return 1;
            }
            return RunProjectWindow(Descriptor, FromExecutable);
#else
            std::printf("Frontier: built without the interface SDK; running the headless diagnostic session instead.\n");
            return RunHeadlessSession(Request.ProjectDescriptor, FromExecutable);
#endif
        }

        case LaunchMode::Browser:
        {
#if defined(FRONTIER_WITH_INTERFACE_SDK)
            std::filesystem::path Chosen;
            const int             BrowserResult = RunBrowserWindow(FromExecutable, Chosen);
            if (BrowserResult != 0 || Chosen.empty())
            {
                return BrowserResult;
            }
            ProjectDescriptor Descriptor;
            if (!PrepareProjectSession(Chosen, FromExecutable, Descriptor))
            {
                return 1;
            }
            return RunProjectWindow(Descriptor, FromExecutable);
#else
            return RunBrowserListing(FromExecutable);
#endif
        }
    }
    return 0;
}
