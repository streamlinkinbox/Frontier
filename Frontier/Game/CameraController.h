#pragma once
/*==========================================================================================================================================
                                                          CAMERACONTROLLER.H
==========================================================================================================================================*/
// 🧩 Camera input is routed independently of vehicle controls. The Lobby gets a studio orbit; driveable
//    levels default to a vehicle-following chase view and can cycle to a free orbit.

#include "Frontier/Engine/Scene/DeployableUnit.h"
#include "Frontier/Engine/Session/SessionPolicy.h"
#include "Frontier/Game/SoftwarePresentation.h"

namespace Frontier
{

class CameraController
{
  public:
    void Step(const InputPacket& Packet, const InputRoutingPolicy& Policy, const DeployableUnit* Vehicle);
    [[nodiscard]] PresentationCamera Resolve(const InputRoutingPolicy& Policy, const DeployableUnit* Vehicle) const;
    [[nodiscard]] bool IsChaseView() const noexcept { return ChaseView; }

  private:
    void SynchroniseContext(const InputRoutingPolicy& Policy);
    bool ContextInitialised = false;
    bool DrivingContext = false;
    bool ChaseView = false;
    float OrbitYawDegrees = 38.0f;
    float PitchDegrees = 14.0f;
};

} // namespace Frontier
