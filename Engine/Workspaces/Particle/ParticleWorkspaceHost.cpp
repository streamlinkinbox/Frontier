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
