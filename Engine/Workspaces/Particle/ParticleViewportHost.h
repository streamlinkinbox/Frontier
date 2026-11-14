/*============================================================================================================================================
                                                         PARTICLEVIEWPORTHOST.H
============================================================================================================================================*/
// 🧩 Particle Workspace central 3D viewport hosting live ParticleSimulation preview, ForceFields gizmos, top HUD & PlaybackScrubber transport

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/PlaybackScrubber.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Workspaces/Particle/ParticleViewportInteractor.h"
#include "Engine/Workspaces/Particle/ParticleViewportRasterization.h"
#include "Engine/Workspaces/Particle/ParticleWorkspaceSession.h"

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PARTICLEVIEWPORTHOST
//------------------------------------------------------------------------------------------------------------------------

class ParticleViewportHost
{
public:
    ParticleViewportHost() noexcept = default;
    ~ParticleViewportHost() noexcept;

    ParticleViewportHost(const ParticleViewportHost&)            = delete;
    ParticleViewportHost& operator=(const ParticleViewportHost&) = delete;

    void AttachSession(ParticleWorkspaceSession* TargetSession) noexcept;
    void AttachGpuRasterizer(ParticleViewportRasterization* TargetRasterizer) noexcept { GpuRasterizer = TargetRasterizer; }

    [[nodiscard]] bool QueryGpuRasterizerReady() const noexcept
    {
        return GpuRasterizer != nullptr && GpuRasterizer->QueryReady();
    }

    [[nodiscard]] ParticleViewportInteractor&       AccessInteractor() noexcept        { return Interactor; }
    [[nodiscard]] const ParticleViewportInteractor& InspectInteractor() const noexcept { return Interactor; }

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  Δt);

    void PresentInImGuiPane(double Δt, float ScaleFactor = 1.0f);

private:
    void RenderFloorGrid(PixelSpace& Surface, Rectangle2D CanvasBounds) const noexcept;
    void RenderForceFieldGizmos(PixelSpace& Surface, Rectangle2D CanvasBounds) const noexcept;
    void RenderParticleSimulations(PixelSpace& Surface, Rectangle2D CanvasBounds) const noexcept;
    void RenderTopHudBar(PixelSpace& Surface, Rectangle2D Bounds, const PaneInputReading& Input) noexcept;
    void RenderBottomScrubber(PixelSpace& Surface, Rectangle2D Bounds, const PaneInputReading& Input, double Δt) noexcept;

    ParticleWorkspaceSession*                 Session       = nullptr;
    ParticleViewportRasterization*            GpuRasterizer = nullptr;
    ParticleViewportInteractor                Interactor    {};
    PlaybackScrubber::PlaybackClockParameters ScrubberClock {};
};

} // namespace Frontier::Workspaces
