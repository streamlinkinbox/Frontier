//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/WorldSpace.cpp — Continuous 3D World Space Coordinate Domain and Scene Transitions
//============================================================================================================================================

#include "WorldSpace.h"
#include <algorithm>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

WorldSpace::WorldSpace() noexcept
    : ActiveWorldIndex(0)
    , TransitionProgress(1.0f)
    , TransitioningCondition(false)
{
}

//------------------------------------------------------------------------------------------------------------------------
//                                                WORLD REGISTRATION
//------------------------------------------------------------------------------------------------------------------------

uint32_t WorldSpace::RegisterWorld(WorldDescriptor World) noexcept
{
    uint32_t Index = static_cast<uint32_t>(RegisteredWorlds.size());
    RegisteredWorlds.push_back(std::move(World));
    return Index;
}

bool WorldSpace::TransitionToWorld(uint32_t WorldIndex, RigidBodySolver& RigidBodies, DeformableSolver& Deformables) noexcept
{
    if (WorldIndex >= RegisteredWorlds.size())
    {
        return false;
    }

    ActiveWorldIndex       = WorldIndex;
    TransitionProgress     = 0.0f;
    TransitioningCondition = true;

    (void)RigidBodies;
    (void)Deformables;

    return true;
}

void WorldSpace::AdvanceWorld(float Δτ) noexcept
{
    if (TransitioningCondition)
    {
        TransitionProgress += Δτ * 2.0f; // 0.5s transition
        if (TransitionProgress >= 1.0f)
        {
            TransitionProgress     = 1.0f;
            TransitioningCondition = false;
        }
    }
}

const WorldDescriptor* WorldSpace::QueryActiveWorld() const noexcept
{
    if (ActiveWorldIndex < RegisteredWorlds.size())
    {
        return &RegisteredWorlds[ActiveWorldIndex];
    }
    return nullptr;
}

} // namespace Frontier
