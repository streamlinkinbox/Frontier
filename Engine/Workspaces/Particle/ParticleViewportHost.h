/*============================================================================================================================================
                                                          PARTICLEVIEWPORTHOST.H
============================================================================================================================================*/
// 🖥️ Central ParticleWorkspace 3D viewport canvas (toolbar, live telemetry strip, tool rail, Vulkan RGBA8 surface, SimulationPlaybackControls & SimulationTimeRuler)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/ViewportProjectionPane.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Workspaces/Particle/ParticleViewportInteractor.h"
#include "Engine/Workspaces/Particle/ParticleWorkspaceSession.h"

#include <imgui.h>

#include <cstdint>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        CLASSES
//------------------------------------------------------------------------------------------------------------------------

class ParticleViewportHost
{
public:
    ParticleViewportHost() = default;
    ~ParticleViewportHost() noexcept;

    ParticleViewportHost(const ParticleViewportHost&)            = delete;
    ParticleViewportHost& operator=(const ParticleViewportHost&) = delete;

    void AttachSession(ParticleWorkspaceSession* TargetSession) noexcept;

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  DeltaSeconds);

    void PresentInImGuiPane(double DeltaSeconds, float ScaleFactor = 1.0f);

    [[nodiscard]] ParticleViewportInteractor& AccessInteractor() noexcept { return Interactor; }

private:
    void SyncViewportTexture();

    ParticleWorkspaceSession* Session             = nullptr;
    ParticleViewportInteractor Interactor         {};
    ImTextureData             ViewportTexture     {};
    uint32_t                  TextureWidth        = 0u;
    uint32_t                  TextureHeight       = 0u;
    bool                      ProjectionMenuOpen  = false;
    bool                      OverlayMenuOpen     = false;
};

} // namespace Frontier::Workspaces
