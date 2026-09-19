//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/WorldSpace.h — Continuous 3D World Space Coordinate Domain and Scene Transitions
//============================================================================================================================================

#pragma once

#include "RigidBodySolver.h"
#include "DeformableSolver.h"
#include <string>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                 WORLD DESCRIPTOR
//------------------------------------------------------------------------------------------------------------------------

struct WorldDescriptor
{
    std::string             WorldIdentifier;                    // [token] unique world or track identifier
    std::string             DisplayName;                        // [text] display world title
    Vector3                 SpawnLocation;                      // [m] initial vehicle / player spawn point
    Vector3                 SunDirection;                       // [-] primary directional sun vector
    Vector3                 SunRadiance;                        // [lux] sun light radiant intensity
    uint32_t                TotalCheckpoints;                   // [count] track checkpoint triggers
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    WORLD SPACE
//------------------------------------------------------------------------------------------------------------------------

class WorldSpace
{
public:
    WorldSpace() noexcept;
    ~WorldSpace() noexcept = default;

    WorldSpace(const WorldSpace&) = delete;
    WorldSpace& operator=(const WorldSpace&) = delete;

    uint32_t                RegisterWorld(WorldDescriptor World) noexcept;
    [[nodiscard]] bool      TransitionToWorld(uint32_t WorldIndex, RigidBodySolver& RigidBodies, DeformableSolver& Deformables) noexcept;

    void                    AdvanceWorld(float Δτ) noexcept;

    [[nodiscard]] uint32_t  QueryActiveWorldIndex() const noexcept { return ActiveWorldIndex; }
    [[nodiscard]] const WorldDescriptor* QueryActiveWorld() const noexcept;
    [[nodiscard]] float     QueryTransitionProgress() const noexcept { return TransitionProgress; }

    // Single unified conversion operator for world count
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    std::vector<WorldDescriptor> RegisteredWorlds;              // [worlds] available track world environments
    uint32_t                ActiveWorldIndex;                   // [index] currently loaded world
    float                   TransitionProgress;                 // [0..1] world fade transition progress
    bool                    TransitioningCondition;             // [bool] true when streaming world assets
};

template<>
inline size_t WorldSpace::Convert<size_t>() const noexcept
{
    return RegisteredWorlds.size();
}

template<>
inline uint32_t WorldSpace::Convert<uint32_t>() const noexcept
{
    return ActiveWorldIndex;
}

} // namespace Frontier
