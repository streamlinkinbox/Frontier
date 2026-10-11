//============================================================================================================================================
// 📦 Projects/Project-Drive/Source/ChaseCameraSolver.cpp — chase camera that trails the vehicle (implementation)
//============================================================================================================================================
#include "ChaseCameraSolver.h"
#include <cmath>
#include <algorithm>

namespace Frontier {
namespace Drive {

ChaseCameraSolver::ChaseCameraSolver() noexcept
    : CameraProjection(), ActiveConfiguration{} {}

ChaseCameraSolver::ChaseCameraSolver(const ChaseCameraConfiguration& InitialConfiguration) noexcept
    : CameraProjection(), ActiveConfiguration(InitialConfiguration) {}

// Yaw/pitch that make ForwardVector point from the eye at AimPoint. Matches CameraProjection's convention:
//    ForwardVector = { sinYaw·cosPitch, cosYaw·cosPitch, sinPitch } in a +Z-up world (forward default +Y).
void ChaseCameraSolver::OrientToward(const Vector3& AimPoint) noexcept
{
    Vector3 d = AimPoint - SpatialLocation;
    const float len = std::sqrt(d.x*d.x + d.y*d.y + d.z*d.z);
    if (len < 1e-5f) return;
    d = d / len;
    const float pitch = std::asin(std::clamp(d.z, -1.0f, 1.0f));
    const float yaw   = std::atan2(d.x, d.y);
    AssignOrientationEuler(pitch, yaw, 0.0f);   // rebuilds Forward/Right/Up
}

void ChaseCameraSolver::SnapTo(const Vector3& TargetPosition, const Vector3& TargetForward) noexcept
{
    const Vector3 idealEye{
        TargetPosition.x - TargetForward.x * ActiveConfiguration.FollowDistance,
        TargetPosition.y - TargetForward.y * ActiveConfiguration.FollowDistance,
        TargetPosition.z - TargetForward.z * ActiveConfiguration.FollowDistance + ActiveConfiguration.FollowHeight };
    AssignSpatialLocation(idealEye);
    Vector3 aim = TargetPosition; aim.z += ActiveConfiguration.LookAtHeight;
    OrientToward(aim);
    AssignFieldOfView(ActiveConfiguration.BaseFieldOfView);
    Seeded = true;
}

void ChaseCameraSolver::AdvanceChase(const Vector3& TargetPosition, const Vector3& TargetForward,
                                     float SpeedMetresPerSecond, float DeltaSeconds) noexcept
{
    if (!Seeded) { SnapTo(TargetPosition, TargetForward); return; }
    if (DeltaSeconds <= 0.0f) return;

    // Ideal eye: behind the car along its forward, lifted by FollowHeight.
    const Vector3 idealEye{
        TargetPosition.x - TargetForward.x * ActiveConfiguration.FollowDistance,
        TargetPosition.y - TargetForward.y * ActiveConfiguration.FollowDistance,
        TargetPosition.z - TargetForward.z * ActiveConfiguration.FollowDistance + ActiveConfiguration.FollowHeight };

    // Critically-damped exponential follow (frame-rate independent).
    const float a = 1.0f - std::exp(-ActiveConfiguration.PositionSpring * DeltaSeconds);
    const Vector3 eye{
        SpatialLocation.x + (idealEye.x - SpatialLocation.x) * a,
        SpatialLocation.y + (idealEye.y - SpatialLocation.y) * a,
        SpatialLocation.z + (idealEye.z - SpatialLocation.z) * a };
    AssignSpatialLocation(eye);

    Vector3 aim = TargetPosition; aim.z += ActiveConfiguration.LookAtHeight;
    OrientToward(aim);

    // Speed-reactive FOV.
    const float k = std::clamp(SpeedMetresPerSecond / std::max(1.0f, ActiveConfiguration.SpeedForFullFov), 0.0f, 1.0f);
    AssignFieldOfView(ActiveConfiguration.BaseFieldOfView + ActiveConfiguration.SpeedFieldOfView * k);
}

void ChaseCameraSolver::AdvanceProjection(float DeltaSeconds) noexcept { (void)DeltaSeconds; }

} // namespace Drive
} // namespace Frontier
