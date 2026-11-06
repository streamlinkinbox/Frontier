/*==========================================================================================================================================
                                                         CAMERACONTROLLER.CPP
==========================================================================================================================================*/

#include "Frontier/Game/CameraController.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{
constexpr float DegreesPerRadian = 57.29577951308232f;
}

void CameraController::SynchroniseContext(const InputRoutingPolicy& Policy)
{
    const bool IsDriving = Policy.AllowDriving;
    if (ContextInitialised && DrivingContext == IsDriving)
        return;
    ContextInitialised = true;
    DrivingContext = IsDriving;
    ChaseView = IsDriving;
    OrbitYawDegrees = IsDriving ? 0.0f : 38.0f;
    PitchDegrees = 14.0f;
}

void CameraController::Step(const InputPacket& Packet, const InputRoutingPolicy& Policy, const DeployableUnit*)
{
    SynchroniseContext(Policy);
    if (Policy.AllowCameraLook)
    {
        OrbitYawDegrees += Packet.LookYawDelta * DegreesPerRadian;
        PitchDegrees = std::clamp(PitchDegrees + Packet.LookPitchDelta * DegreesPerRadian, -8.0f, 72.0f);
    }
    if (Policy.AllowCameraCycle && Packet.HasAction(InputActionCycleCamera))
        ChaseView = !ChaseView;
}

PresentationCamera CameraController::Resolve(const InputRoutingPolicy& Policy, const DeployableUnit* Vehicle) const
{
    PresentationCamera Camera{};
    Camera.PitchDegrees = PitchDegrees;
    const bool HasVehicle = Vehicle != nullptr;
    if (!Policy.AllowDriving)
    {
        Camera.FocusMetres = { 0.0f, 0.0f, 0.55f };
        Camera.YawDegrees = OrbitYawDegrees;
        Camera.DistanceMetres = 15.5f;
        return Camera;
    }

    const auto Position = HasVehicle ? Vehicle->QueryPositionMetres() : std::array<float, 3>{ 0.0f, 0.0f, 0.53f };
    const float VehicleYaw = HasVehicle ? Vehicle->QueryYawDegrees() : 0.0f;
    Camera.FocusMetres = { Position[0], Position[1], Position[2] + (ChaseView ? 0.62f : 0.40f) };
    Camera.YawDegrees = VehicleYaw + (ChaseView ? 180.0f : 38.0f) + OrbitYawDegrees;
    Camera.DistanceMetres = ChaseView ? 13.5f : 16.5f;
    return Camera;
}

} // namespace Frontier
