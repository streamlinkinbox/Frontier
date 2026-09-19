//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/LocomotionSolver.h — Kinematic Character Locomotion and Ground Surface Tracking
//============================================================================================================================================

#pragma once

#include "../DeviceExchange/OrientationClassifier.h"
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                 LOCOMOTION CAPSULE
//------------------------------------------------------------------------------------------------------------------------

struct LocomotionCapsule
{
    Vector3                 SpatialLocation;                    // [m] world position of capsule bottom
    Vector3                 DesiredVelocity;                    // [m/s] requested horizontal velocity
    Vector3                 LinearVelocity;                     // [m/s] actual evaluated velocity
    float                   CapsuleRadius;                      // [m] cylinder radius
    float                   CapsuleHalfHeight;                  // [m] half-height of cylinder portion
    float                   MaximumSlopeAngle;                  // [rad] maximum walkable slope angle
    float                   StepClimbHeight;                    // [m] maximum step height
    bool                    GroundedCondition;                  // [bool] true when touching walkable ground
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 LOCOMOTION SOLVER
//------------------------------------------------------------------------------------------------------------------------

class LocomotionSolver
{
public:
    LocomotionSolver() noexcept;
    ~LocomotionSolver() noexcept = default;

    void                    AdvanceLocomotion(LocomotionCapsule& Capsule, float Δτ) noexcept;

    // Single unified conversion operator
    template<typename TargetType>
    [[nodiscard]] TargetType Convert(const LocomotionCapsule& Capsule) const noexcept;
};

template<>
inline Vector3 LocomotionSolver::Convert<Vector3>(const LocomotionCapsule& Capsule) const noexcept
{
    return Capsule.SpatialLocation;
}

template<>
inline bool LocomotionSolver::Convert<bool>(const LocomotionCapsule& Capsule) const noexcept
{
    return Capsule.GroundedCondition;
}

} // namespace Frontier
