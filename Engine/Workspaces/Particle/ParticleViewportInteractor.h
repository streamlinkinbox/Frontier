/*============================================================================================================================================
                                                      PARTICLEVIEWPORTINTERACTOR.H
============================================================================================================================================*/
// 🧭 Orbit/pan/zoom camera navigation, 3D force/collider handle dragging & Throw Ball interaction for ParticleViewportHost

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Workspaces/Particle/ParticleWorkspaceSession.h"

#include <cstdint>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        CLASSES
//------------------------------------------------------------------------------------------------------------------------

class ParticleViewportInteractor
{
public:
    ParticleViewportInteractor() = default;

    void AttachSession(ParticleWorkspaceSession* TargetSession) noexcept { Session = TargetSession; }

    void ProcessInput(
        Rectangle2D             CanvasBounds,
        const PaneInputReading& Input);

    [[nodiscard]] bool QueryDraggingOrbit() const noexcept { return OrbitDragging; }
    [[nodiscard]] bool QueryDraggingHandle() const noexcept { return DraggingHandleIndex >= 0; }

private:
    ParticleWorkspaceSession* Session             = nullptr;
    bool                      OrbitDragging       = false;
    float                     LastCursorX         = 0.0f;
    float                     LastCursorY         = 0.0f;
    int32_t                   DraggingHandleIndex = -1;
};

} // namespace Frontier::Workspaces
