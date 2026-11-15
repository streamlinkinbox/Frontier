/*============================================================================================================================================
                                                     PARTICLEVIEWPORTINTERACTOR.CPP
============================================================================================================================================*/
// 🧭 Implementation of ParticleViewportInteractor supporting orbit/pan/zoom, Throw Ball clicks & 3D handle dragging

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particle/ParticleViewportInteractor.h"

#include <algorithm>
#include <cmath>

namespace Frontier::Workspaces
{

void ParticleViewportInteractor::ProcessInput(
    Rectangle2D             CanvasBounds,
    const PaneInputReading& Input)
{
    if (Session == nullptr)
    {
        return;
    }

    const ControlPointer& Ptr = Input.PrimaryPointer;
    const bool Inside = CanvasBounds.Contains({ Ptr.X, Ptr.Y });
    if (Inside && std::abs(Input.WheelDeltaY) > 1.0e-3f)
    {
        Session->CameraDistanceMeters = std::clamp(
            Session->CameraDistanceMeters - Input.WheelDeltaY * 0.45f,
            2.2f,
            24.0f);
    }

    if (!Ptr.Down)
    {
        OrbitDragging       = false;
        DraggingHandleIndex = -1;
    }

    if (Inside && Ptr.Pressed)
    {
        if (Session->ActiveTool == ParticleViewportTool::ThrowBall)
        {
            Session->ThrowInteractiveBall();
            return;
        }

        // Check proximity to active ForceSource handles
        if (Session->HandlesVisible)
        {
            const auto& Forces = Session->InspectDocument().ForceSources;
            for (std::size_t Idx = 0u; Idx < Forces.size(); ++Idx)
            {
                const float Sx = CanvasBounds.Center().X + Forces[Idx].CentreMeters.X * 68.0f;
                const float Sy = CanvasBounds.Y + CanvasBounds.Height * 0.62f - Forces[Idx].CentreMeters.Z * 68.0f;
                const float Dx = Ptr.X - Sx;
                const float Dy = Ptr.Y - Sy;
                if (Dx * Dx + Dy * Dy <= 196.0f)
                {
                    DraggingHandleIndex = static_cast<int32_t>(Idx);
                    Session->SelectForceSource(Idx);
                    LastCursorX = Ptr.X;
                    LastCursorY = Ptr.Y;
                    return;
                }
            }
        }

        OrbitDragging = true;
        LastCursorX   = Ptr.X;
        LastCursorY   = Ptr.Y;
    }

    if (DraggingHandleIndex >= 0 && Ptr.Down)
    {
        const float Dx = Ptr.X - LastCursorX;
        const float Dy = Ptr.Y - LastCursorY;
        auto& Forces = Session->AccessDocument().ForceSources;
        if (static_cast<std::size_t>(DraggingHandleIndex) < Forces.size())
        {
            Forces[static_cast<std::size_t>(DraggingHandleIndex)].CentreMeters.X += Dx / 68.0f;
            Forces[static_cast<std::size_t>(DraggingHandleIndex)].CentreMeters.Z -= Dy / 68.0f;
        }
        LastCursorX = Ptr.X;
        LastCursorY = Ptr.Y;
    }
    else if (OrbitDragging && Ptr.Down)
    {
        const float Dx = Ptr.X - LastCursorX;
        const float Dy = Ptr.Y - LastCursorY;
        Session->CameraYawRad   += Dx * 0.008f;
        Session->CameraPitchRad  = std::clamp(Session->CameraPitchRad + Dy * 0.006f, -0.45f, 1.35f);
        LastCursorX              = Ptr.X;
        LastCursorY              = Ptr.Y;
    }
}

} // namespace Frontier::Workspaces
