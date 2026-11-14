/*============================================================================================================================================
                                                       PARTICLEWORKSPACEHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleWorkspaceHost registering Outliner, 3D Viewport & Inspector panes + .ParticleSpace/.VectorFieldSpace drops

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particle/ParticleWorkspaceHost.h"

#include <algorithm>
#include <cctype>
#include <imgui.h>

namespace Frontier::Workspaces
{

namespace
{

[[nodiscard]] std::string NormalizeLowerExtension(const std::filesystem::path& FilePath)
{
    std::string Ext = FilePath.extension().string();
    std::transform(
        Ext.begin(),
        Ext.end(),
        Ext.begin(),
        [](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });
    return Ext;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                   PARTICLEWORKSPACEHOST
//------------------------------------------------------------------------------------------------------------------------

ParticleWorkspaceHost::ParticleWorkspaceHost()
{
    Outliner.AttachSession(&Session);
    ViewportHost.AttachSession(&Session);
    Inspector.AttachSession(&Session);
    (void)InitializeLiveGpuSuite();
}

bool ParticleWorkspaceHost::InitializeLiveGpuSuite(const std::filesystem::path& RepoRootOverride)
{
    std::filesystem::path Root = RepoRootOverride;
    if (Root.empty())
    {
#ifdef FRONTIER_REPO_ROOT_DIR
        Root = std::filesystem::path(FRONTIER_REPO_ROOT_DIR);
#else
        Root = std::filesystem::current_path();
#endif
    }

    const std::filesystem::path ShaderDir = Root / "_Build/CompiledParticleShaders";
    const std::filesystem::path IcdDir    = Root / "ExternalPackages/VulkanCompute/SwiftShader";
    if (!std::filesystem::exists(ShaderDir / "ParticleAdvectCompute.spv"))
    {
        return false;
    }

    if (!Session.InitializeVulkanCompute(ShaderDir, IcdDir, GpuInitDiagnostic))
    {
        return false;
    }

    auto* Executor = Session.AccessVulkanExecutor();
    if (Executor == nullptr || !GpuRasterizer.Initialize(*Executor, ShaderDir, GpuInitDiagnostic))
    {
        return false;
    }

    ViewportHost.AttachGpuRasterizer(&GpuRasterizer);
    return true;
}

void ParticleWorkspaceHost::PresentImGuiWorkspaceLayout(
    Rectangle2D FrameBounds,
    double      DeltaSeconds,
    float       ScaleFactor)
{
    const float SafeScale   = std::max(0.25f, ScaleFactor);
    const float OutlinerW   = std::clamp(FrameBounds.Width * 0.21f, 240.0f, 340.0f);
    const float InspectorW  = std::clamp(FrameBounds.Width * 0.24f, 290.0f, 400.0f);
    const float ViewportW   = std::max(240.0f, FrameBounds.Width - OutlinerW - InspectorW);

    const ImGuiWindowFlags PaneWindowStyle =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    ImGui::SetNextWindowPos(ImVec2(FrameBounds.X, FrameBounds.Y));
    ImGui::SetNextWindowSize(ImVec2(OutlinerW, FrameBounds.Height));
    if (ImGui::Begin("##ParticleStudioOutlinerPane", nullptr, PaneWindowStyle))
    {
        Outliner.PresentInImGuiPane(DeltaSeconds, SafeScale);
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(FrameBounds.X + OutlinerW, FrameBounds.Y));
    ImGui::SetNextWindowSize(ImVec2(ViewportW, FrameBounds.Height));
    if (ImGui::Begin("##ParticleStudioViewportPane", nullptr, PaneWindowStyle))
    {
        ViewportHost.PresentInImGuiPane(DeltaSeconds, SafeScale);
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(FrameBounds.X + OutlinerW + ViewportW, FrameBounds.Y));
    ImGui::SetNextWindowSize(ImVec2(InspectorW, FrameBounds.Height));
    if (ImGui::Begin("##ParticleStudioInspectorPane", nullptr, PaneWindowStyle))
    {
        Inspector.PresentInImGuiPane(DeltaSeconds, SafeScale);
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
}

void ParticleWorkspaceHost::ConfigureLaunchPolicy(const Configuration::EngineConfiguration& Config) noexcept
{
    ActiveEngineConfig = Config;
}

void ParticleWorkspaceHost::AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel)
{
    BoundBrowserPanel = TargetPanel;
}

#if FRONTIER_EDITOR_ENABLED
void ParticleWorkspaceHost::AttachContentBrowser(Editor::Explorer::ContentBrowser* TargetBrowser)
{
    BoundBrowser = TargetBrowser;
    AttachContentBrowserPanel(TargetBrowser != nullptr ? &TargetBrowser->AccessPanel() : nullptr);
}
#endif

bool ParticleWorkspaceHost::IntakeExternalFileDrop(
    const std::filesystem::path& DroppedPath,
    std::string&                 OutDiagnostic)
{
    const std::string Ext = NormalizeLowerExtension(DroppedPath);
    if (Ext == ".particlespace")
    {
        return Session.LoadParticleSpace(DroppedPath, OutDiagnostic);
    }
    if (Ext == ".vectorfieldspace")
    {
        return Session.ImportVectorFieldAsset(DroppedPath, OutDiagnostic);
    }

    OutDiagnostic = "Unsupported Particle Workspace drop extension: " + Ext;
    return false;
}

void ParticleWorkspaceHost::RegisterDockPanes(Components::HeadlessWorkspaceDockContainer& DockContainer)
{
    using Components::DockPlacementDirection;
    using Components::PaneDirectiveMask;
    using Components::WorkspaceDockPaneDescriptor;

    RegisteredTabCount = 0u;

    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Outliner",
        DockPlacementDirection::SplitLeft,
        0.21f,
        PaneDirectiveMask::StandardPaneDirectives,
        [this]() { Outliner.PresentInImGuiPane(1.0 / 60.0, 1.0f); },
    });
    ++RegisteredTabCount;

    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Particle Viewport",
        DockPlacementDirection::CenterTabGroup,
        0.55f,
        PaneDirectiveMask::StandardPaneDirectives | PaneDirectiveMask::AssignCentralViewport,
        [this]() { ViewportHost.PresentInImGuiPane(1.0 / 60.0, 1.0f); },
    });
    ++RegisteredTabCount;

    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Inspector",
        DockPlacementDirection::SplitRight,
        0.24f,
        PaneDirectiveMask::StandardPaneDirectives,
        [this]() { Inspector.PresentInImGuiPane(1.0 / 60.0, 1.0f); },
    });
    ++RegisteredTabCount;
}

} // namespace Frontier::Workspaces
