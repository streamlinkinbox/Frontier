//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/LocomotionSolver.cpp — Kinematic Locomotion Implementation
//============================================================================================================================================

#include "LocomotionSolver.h"

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

LocomotionSolver::LocomotionSolver() noexcept
{
}

//------------------------------------------------------------------------------------------------------------------------
//                                              LOCOMOTION STEPPING
//------------------------------------------------------------------------------------------------------------------------

void LocomotionSolver::AdvanceLocomotion(LocomotionCapsule& Capsule, float Δτ) noexcept
{
    if (Δτ <= 0.0f)
    {
        return;
    }

    // Apply horizontal velocity smoothing
    Capsule.LinearVelocity.x = Capsule.DesiredVelocity.x;
    Capsule.LinearVelocity.z = Capsule.DesiredVelocity.z;

    // Gravity when airborne
    if (!Capsule.GroundedCondition)
    {
        Capsule.LinearVelocity.y -= 9.81f * Δτ;
    }

    Capsule.SpatialLocation += Capsule.LinearVelocity * Δτ;

    // Ground ray intersection against flat ground plane y = 0
    if (Capsule.SpatialLocation.y <= 0.0f)
    {
        Capsule.SpatialLocation.y = 0.0f;
        Capsule.LinearVelocity.y = 0.0f;
        Capsule.GroundedCondition = true;
    }
    else
    {
        Capsule.GroundedCondition = false;
    }
}

} // namespace Frontier
