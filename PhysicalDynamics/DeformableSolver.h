//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/DeformableSolver.h — Extended Position-Based Dynamics (XPBD) Softbody and Cloth Solver
//============================================================================================================================================

#pragma once

#include "DeformableStructure.h"
#include "../DeviceExchange/TaskScheduler.h"
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                 DEFORMABLE SOLVER
//------------------------------------------------------------------------------------------------------------------------

class DeformableSolver
{
public:
    explicit DeformableSolver(TaskScheduler* InitialScheduler) noexcept;
    ~DeformableSolver() noexcept = default;

    DeformableSolver(const DeformableSolver&) = delete;
    DeformableSolver& operator=(const DeformableSolver&) = delete;

    [[nodiscard]] bool      Initialize() noexcept;
    void                    Terminate() noexcept;

    [[nodiscard]] uint64_t  RegisterStructure(DeformableStructure Structure) noexcept;
    void                    UnregisterStructure(uint64_t DesiredStructureIdentifier) noexcept;

    void                    AdvanceSimulation(float Δτ, uint32_t SubstepCount = 4) noexcept;

    [[nodiscard]] const DeformableStructure* QueryStructure(uint64_t DesiredStructureIdentifier) const noexcept;
    [[nodiscard]] size_t    QueryActiveParticleCount() const noexcept;

    // Single unified conversion operator for metrics
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    void                    SolveSubstep(float SubstepΔτ) noexcept;
    void                    ProjectDistanceConstraints(DeformableStructure& Structure, float SubstepΔτ) noexcept;
    void                    ProjectTetrahedralConstraints(DeformableStructure& Structure, float SubstepΔτ) noexcept;

    TaskScheduler*          Scheduler;                          // [ptr] task scheduler reference
    std::vector<DeformableStructure> Structures;                // [structures] active softbodies
    Vector3                 GravitationalAcceleration;          // [m/s²] gravity vector
    bool                    InitializedCondition;               // [bool] status
};

template<>
inline size_t DeformableSolver::Convert<size_t>() const noexcept
{
    return QueryActiveParticleCount();
}

} // namespace Frontier
