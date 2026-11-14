/*============================================================================================================================================
                                                        PARTICLEWORKSPACEHOST.H
============================================================================================================================================*/
// 🧩 Dual-deployment Particle Workspace host mounting Standalone or Editor docked Outliner + 3D Viewport + Inspector + ContentBrowser

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Configuration/BuildConfiguration.h"
#include "Engine/Configuration/EngineConfiguration.h"
#include "Engine/Workspaces/Particle/ParticleInspectorPanel.h"
#include "Engine/Workspaces/Particle/ParticleOutlinerPanel.h"
#include "Engine/Workspaces/Particle/ParticleViewportHost.h"
#include "Engine/Workspaces/Particle/ParticleWorkspaceSession.h"

#if FRONTIER_EDITOR_ENABLED
#include "Engine/Editor/Explorer/ContentBrowser.h"
#endif

#include <cstdint>
#include <filesystem>
#include <string>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   PARTICLEWORKSPACEHOST
//------------------------------------------------------------------------------------------------------------------------

class ParticleWorkspaceHost
{
public:
    ParticleWorkspaceHost();

    void ConfigureLaunchPolicy(const Configuration::EngineConfiguration& Config) noexcept;

    void AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel);
#if FRONTIER_EDITOR_ENABLED
    void AttachContentBrowser(Editor::Explorer::ContentBrowser* TargetBrowser);
#endif

    [[nodiscard]] bool IntakeExternalFileDrop(
        const std::filesystem::path& DroppedPath,
        std::string&                 OutDiagnostic);

    void RegisterDockPanes(Components::HeadlessWorkspaceDockContainer& DockContainer);

    [[nodiscard]] ParticleWorkspaceSession&       AccessSession() noexcept      { return Session; }
    [[nodiscard]] const ParticleWorkspaceSession& InspectSession() const noexcept { return Session; }
    [[nodiscard]] ParticleOutlinerPanel&          AccessOutliner() noexcept     { return Outliner; }
    [[nodiscard]] ParticleViewportHost&           AccessViewportHost() noexcept { return ViewportHost; }
    [[nodiscard]] ParticleInspectorPanel&         AccessInspector() noexcept    { return Inspector; }
    [[nodiscard]] uint32_t                        QueryRegisteredTabCount() const noexcept { return RegisteredTabCount; }

private:
    Configuration::EngineConfiguration ActiveEngineConfig {};
    ParticleWorkspaceSession           Session            {};
    ParticleOutlinerPanel              Outliner           {};
    ParticleViewportHost               ViewportHost       {};
    ParticleInspectorPanel             Inspector          {};
    uint32_t                           RegisteredTabCount = 0u;
    ContentBrowserPanel*               BoundBrowserPanel  = nullptr;
#if FRONTIER_EDITOR_ENABLED
    Editor::Explorer::ContentBrowser*  BoundBrowser       = nullptr;
#endif
};

} // namespace Frontier::Workspaces
