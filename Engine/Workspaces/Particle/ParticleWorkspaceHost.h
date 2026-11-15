/*============================================================================================================================================
                                                         PARTICLEWORKSPACEHOST.H
============================================================================================================================================*/
// 🧩 Dual-deployment ParticleWorkspace host mounting StandaloneWorkspace (FrontierParticle) or FeatureSet docked panes + ContentBrowser integration

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Configuration/BuildConfiguration.h"
#include "Engine/Configuration/EngineConfiguration.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
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
//                                                        CLASSES
//------------------------------------------------------------------------------------------------------------------------

class ParticleWorkspaceHost
{
public:
    ParticleWorkspaceHost();

    void ConfigureLaunchPolicy(const Configuration::EngineConfiguration& Config) noexcept;
    [[nodiscard]] bool AttachVulkanInterchange(
        GeometricRaster::VulkanInterchange* BoundDevice,
        const std::filesystem::path&        ShaderDirectory = {});

    void AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel);
#if FRONTIER_EDITOR_ENABLED
    void AttachContentBrowser(Editor::Explorer::ContentBrowser* TargetBrowser);
#endif

    [[nodiscard]] bool IntakeExternalFileDrop(
        const std::filesystem::path& DroppedPath,
        std::string&                 OutDiagnostic);

    void RegisterDockPanes(Components::HeadlessWorkspaceDockContainer& DockContainer);
    void RenderStudioTopStrip(PixelSpace& Surface, Rectangle2D Bounds, const ControlPointer& Pointer);
    void RenderOverlayDrawers(PixelSpace& Surface, Rectangle2D ViewportBounds, const ControlPointer& Pointer);

    [[nodiscard]] ParticleWorkspaceSession&       AccessSession() noexcept { return Session; }
    [[nodiscard]] const ParticleWorkspaceSession& InspectSession() const noexcept { return Session; }
    [[nodiscard]] ParticleOutlinerPanel&          AccessOutliner() noexcept { return Outliner; }
    [[nodiscard]] ParticleViewportHost&           AccessViewportHost() noexcept { return ViewportHost; }
    [[nodiscard]] ParticleInspectorPanel&         AccessInspector() noexcept { return Inspector; }
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
