//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/RigidBodySolver.h — Multi-Threaded Rigid Body Constraint and Manifold Solver
//============================================================================================================================================

#pragma once

#include "RigidBodyStructure.h"
#include "../DeviceExchange/TaskScheduler.h"
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                  RIGID BODY SOLVER
//------------------------------------------------------------------------------------------------------------------------

class RigidBodySolver
{
public:
    explicit RigidBodySolver(TaskScheduler* InitialScheduler) noexcept;
    ~RigidBodySolver() noexcept = default;

    RigidBodySolver(const RigidBodySolver&) = delete;
    RigidBodySolver& operator=(const RigidBodySolver&) = delete;

    [[nodiscard]] bool      Initialize() noexcept;
    void                    Terminate() noexcept;

    [[nodiscard]] uint64_t  RegisterBody(const RigidBodyStructure& Body) noexcept;
    void                    UnregisterBody(uint64_t DesiredBodyIdentifier) noexcept;

    void                    AdvanceSimulation(float Δτ) noexcept;

    [[nodiscard]] const RigidBodyStructure* QueryBody(uint64_t DesiredBodyIdentifier) const noexcept;
    [[nodiscard]] size_t    QueryActiveBodyCount() const noexcept;

    // Single unified conversion operator for active body metrics
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    TaskScheduler*          Scheduler;                          // [ptr] task graph scheduler reference
    std::vector<RigidBodyStructure> Bodies;                     // [bodies] contiguous body records
    Vector3                 GravitationalAcceleration;          // [m/s²] continuous gravity vector
    bool                    InitializedCondition;               // [bool] lifecycle status
};

template<>
inline size_t RigidBodySolver::Convert<size_t>() const noexcept
{
    return QueryActiveBodyCount();
}

} // namespace Frontier
