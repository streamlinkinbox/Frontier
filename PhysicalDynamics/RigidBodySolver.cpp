//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/RigidBodySolver.cpp — Rigid Body Dynamics Solver Implementation
//============================================================================================================================================

#include "RigidBodySolver.h"
#include <algorithm>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

RigidBodySolver::RigidBodySolver(TaskScheduler* InitialScheduler) noexcept
    : Scheduler(InitialScheduler)
    , GravitationalAcceleration(0.0f, -9.81f, 0.0f)
    , InitializedCondition(false)
{
}

bool RigidBodySolver::Initialize() noexcept
{
    Bodies.reserve(4096);
    InitializedCondition = true;
    return true;
}

void RigidBodySolver::Terminate() noexcept
{
    Bodies.clear();
    InitializedCondition = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                BODY REGISTRATION
//------------------------------------------------------------------------------------------------------------------------

uint64_t RigidBodySolver::RegisterBody(const RigidBodyStructure& Body) noexcept
{
    Bodies.push_back(Body);
    return Body.BodyIdentifier;
}

void RigidBodySolver::UnregisterBody(uint64_t DesiredBodyIdentifier) noexcept
{
    auto iterator = std::remove_if(Bodies.begin(), Bodies.end(),
        [DesiredBodyIdentifier](const RigidBodyStructure& b) { return b.BodyIdentifier == DesiredBodyIdentifier; });
    Bodies.erase(iterator, Bodies.end());
}

//------------------------------------------------------------------------------------------------------------------------
//                                              SIMULATION INTEGRATION
//------------------------------------------------------------------------------------------------------------------------

void RigidBodySolver::AdvanceSimulation(float Δτ) noexcept
{
    if (Bodies.empty() || Δτ <= 0.0f)
    {
        return;
    }

    auto IntegrateChunk = [this, Δτ](uint32_t Start, uint32_t End)
    {
        for (uint32_t Index = Start; Index < End; ++Index)
        {
            auto& Body = Bodies[Index];
            if (!Body.MotionActive || Body.InertialMass <= 0.0f)
            {
                continue;
            }

            // Newtonian integration: v += g * Δτ
            Body.LinearMomentum += GravitationalAcceleration * Δτ;
            Body.LinearMomentum *= (1.0f - Body.LinearDamping * Δτ);

            // Position update: p += v * Δτ
            Body.SpatialLocation += Body.LinearMomentum * Δτ;

            // Ground floor constraint (y >= 0)
            if (Body.SpatialLocation.y < 0.0f)
            {
                Body.SpatialLocation.y = 0.0f;
                Body.LinearMomentum.y = -Body.LinearMomentum.y * Body.RestitutionCoefficient;
                Body.LinearMomentum.x *= (1.0f - Body.FrictionCoefficient);
                Body.LinearMomentum.z *= (1.0f - Body.FrictionCoefficient);
            }
        }
    };

    if (Scheduler && Bodies.size() > 64)
    {
        Scheduler->DispatchParallel(static_cast<uint32_t>(Bodies.size()), 64, IntegrateChunk);
    }
    else
    {
        IntegrateChunk(0, static_cast<uint32_t>(Bodies.size()));
    }
}

const RigidBodyStructure* RigidBodySolver::QueryBody(uint64_t DesiredBodyIdentifier) const noexcept
{
    for (const auto& Body : Bodies)
    {
        if (Body.BodyIdentifier == DesiredBodyIdentifier)
        {
            return &Body;
        }
    }
    return nullptr;
}

size_t RigidBodySolver::QueryActiveBodyCount() const noexcept
{
    return Bodies.size();
}

} // namespace Frontier
