/*============================================================================================================================================
                                                    PARTICLEVIEWPORTINTERACTOR.H
============================================================================================================================================*/
// 🧩 Right-handed +Z-Up 3D orbit, pan, and dolly camera rig for ParticleViewportHost

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Mathematics/AxisCoordinates.h"

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParticleCameraRig
{
    Mathematics::Axis3D TargetMeters       { 0.0f, 0.0f, 1.35f }; // [m]   - Orbit look-at focal anchor (+Z Up)
    float               YawRadians         = -0.72f;              // [rad] - Azimuth angle around +Z axis
    float               PitchRadians       = 0.34f;               // [rad] - Elevation angle above XY floor
    float               DistanceMeters     = 9.8f;                // [m]   - Orbit radius from focal anchor
    float               VerticalFovRadians = 0.78539816f;         // [rad] - Vertical perspective FOV (45 deg)
};

//------------------------------------------------------------------------------------------------------------------------
//                                               PARTICLEVIEWPORTINTERACTOR
//------------------------------------------------------------------------------------------------------------------------

class ParticleViewportInteractor
{
public:
    ParticleViewportInteractor() noexcept = default;

    void ResetCamera() noexcept;
    void ConfigureCamera(const ParticleCameraRig& Rig) noexcept;
    void FocusPoint(Mathematics::Axis3D TargetMeters, float DistanceMeters) noexcept;

    [[nodiscard]] bool ApplyPointerInput(
        Rectangle2D             ViewportBounds,
        const PaneInputReading& Input) noexcept;

    [[nodiscard]] Mathematics::Axis3D QueryCameraPosition() const noexcept;
    [[nodiscard]] const ParticleCameraRig& InspectCameraRig() const noexcept { return CameraRig; }
    [[nodiscard]] ParticleCameraRig&       AccessCameraRig() noexcept        { return CameraRig; }

    [[nodiscard]] bool ProjectWorldPoint(
        Mathematics::Axis3D WorldPoint,
        Rectangle2D         ViewportBounds,
        Point2D&            OutScreenPoint,
        float&              OutDepthMeters) const noexcept;

private:
    ParticleCameraRig CameraRig       {};
    bool              OrbitDragActive = false;
    bool              PanDragActive   = false;
    Point2D           LastPointerPos  { 0.0f, 0.0f };
};

} // namespace Frontier::Workspaces
