//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/DeformableSolver.cpp — XPBD Softbody and Cloth Solver Implementation
//============================================================================================================================================

#include "DeformableSolver.h"
#include <algorithm>
#include <cmath>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

DeformableSolver::DeformableSolver(TaskScheduler* InitialScheduler) noexcept
    : Scheduler(InitialScheduler)
    , GravitationalAcceleration(0.0f, -9.81f, 0.0f)
    , InitializedCondition(false)
{
}

bool DeformableSolver::Initialize() noexcept
{
    Structures.reserve(128);
    InitializedCondition = true;
    return true;
}

void DeformableSolver::Terminate() noexcept
{
    Structures.clear();
    InitializedCondition = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              STRUCTURE REGISTRATION
//------------------------------------------------------------------------------------------------------------------------

uint64_t DeformableSolver::RegisterStructure(DeformableStructure Structure) noexcept
{
    uint64_t Identifier = Structure.StructureIdentifier;
    Structures.push_back(std::move(Structure));
    return Identifier;
}

void DeformableSolver::UnregisterStructure(uint64_t DesiredStructureIdentifier) noexcept
{
    auto iterator = std::remove_if(Structures.begin(), Structures.end(),
        [DesiredStructureIdentifier](const DeformableStructure& s) { return s.StructureIdentifier == DesiredStructureIdentifier; });
    Structures.erase(iterator, Structures.end());
}

//------------------------------------------------------------------------------------------------------------------------
//                                              XPBD SUBSTEP INTEGRATION
//------------------------------------------------------------------------------------------------------------------------

void DeformableSolver::AdvanceSimulation(float Δτ, uint32_t SubstepCount) noexcept
{
    if (Structures.empty() || Δτ <= 0.0f || SubstepCount == 0)
    {
        return;
    }

    float SubstepΔτ = Δτ / static_cast<float>(SubstepCount);
    for (uint32_t Step = 0; Step < SubstepCount; ++Step)
    {
        SolveSubstep(SubstepΔτ);
    }
}

void DeformableSolver::SolveSubstep(float SubstepΔτ) noexcept
{
    for (auto& Structure : Structures)
    {
        // 1. Predict positions: x~ = x + v * Δτ + g * Δτ²
        for (auto& Particle : Structure.Particles)
        {
            if (Particle.InverseMass <= 0.0f)
            {
                Particle.PredictedLocation = Particle.SpatialLocation;
                continue;
            }

            Particle.PreviousLocation = Particle.SpatialLocation;
            Particle.Velocity += GravitationalAcceleration * SubstepΔτ;
            Particle.PredictedLocation = Particle.SpatialLocation + Particle.Velocity * SubstepΔτ;
        }

        // Reset multipliers
        for (auto& Edge : Structure.Edges)
        {
            Edge.LagrangeMultiplier = 0.0f;
        }
        for (auto& Tet : Structure.Tetrahedra)
        {
            Tet.LagrangeMultiplier = 0.0f;
        }

        // 2. Project constraints
        ProjectDistanceConstraints(Structure, SubstepΔτ);
        ProjectTetrahedralConstraints(Structure, SubstepΔτ);

        // 3. Ground collision and velocity update: v = (x~ - x_prev) / Δτ
        for (auto& Particle : Structure.Particles)
        {
            if (Particle.InverseMass <= 0.0f)
            {
                continue;
            }

            if (Particle.PredictedLocation.y < 0.0f)
            {
                Particle.PredictedLocation.y = 0.0f;
            }

            Particle.Velocity = (Particle.PredictedLocation - Particle.PreviousLocation) / SubstepΔτ;
            Particle.SpatialLocation = Particle.PredictedLocation;
        }
    }
}

void DeformableSolver::ProjectDistanceConstraints(DeformableStructure& Structure, float SubstepΔτ) noexcept
{
    float SubstepΔτSq = SubstepΔτ * SubstepΔτ;

    for (auto& Edge : Structure.Edges)
    {
        auto& p1 = Structure.Particles[Edge.ParticleIndexA];
        auto& p2 = Structure.Particles[Edge.ParticleIndexB];

        float w1 = p1.InverseMass;
        float w2 = p2.InverseMass;
        float wSum = w1 + w2;
        if (wSum <= 1e-7f)
        {
            continue;
        }

        Vector3 Delta = p1.PredictedLocation - p2.PredictedLocation;
        float CurrentDist = Delta.Length();
        if (CurrentDist <= 1e-7f)
        {
            continue;
        }

        float ConstraintValue = CurrentDist - Edge.RestLength;
        float AlphaTilde = Edge.Compliance / SubstepΔτSq;

        float DeltaLambda = (-ConstraintValue - AlphaTilde * Edge.LagrangeMultiplier) / (wSum + AlphaTilde);
        Edge.LagrangeMultiplier += DeltaLambda;

        Vector3 Normal = Delta / CurrentDist;
        p1.PredictedLocation += Normal * (w1 * DeltaLambda);
        p2.PredictedLocation -= Normal * (w2 * DeltaLambda);
    }
}

void DeformableSolver::ProjectTetrahedralConstraints(DeformableStructure& Structure, float SubstepΔτ) noexcept
{
    float SubstepΔτSq = SubstepΔτ * SubstepΔτ;

    for (auto& Tet : Structure.Tetrahedra)
    {
        auto& p1 = Structure.Particles[Tet.ParticleIndices[0]];
        auto& p2 = Structure.Particles[Tet.ParticleIndices[1]];
        auto& p3 = Structure.Particles[Tet.ParticleIndices[2]];
        auto& p4 = Structure.Particles[Tet.ParticleIndices[3]];

        Vector3 d1 = p2.PredictedLocation - p1.PredictedLocation;
        Vector3 d2 = p3.PredictedLocation - p1.PredictedLocation;
        Vector3 d3 = p4.PredictedLocation - p1.PredictedLocation;

        Vector3 Cross23 = OrientationClassifier::CrossProduct(d2, d3);
        float Volume6 = OrientationClassifier::DotProduct(d1, Cross23);
        float CurrentVolume = Volume6 / 6.0f;

        float ConstraintValue = CurrentVolume - Tet.RestVolume;
        float AlphaTilde = Tet.Compliance / SubstepΔτSq;

        Vector3 Grad1 = OrientationClassifier::CrossProduct(p4.PredictedLocation - p2.PredictedLocation, p3.PredictedLocation - p2.PredictedLocation) / 6.0f;
        Vector3 Grad2 = OrientationClassifier::CrossProduct(d2, d3) / 6.0f;
        Vector3 Grad3 = OrientationClassifier::CrossProduct(d3, d1) / 6.0f;
        Vector3 Grad4 = OrientationClassifier::CrossProduct(d1, d2) / 6.0f;

        float Denominator = p1.InverseMass * Grad1.LengthSquared()
                          + p2.InverseMass * Grad2.LengthSquared()
                          + p3.InverseMass * Grad3.LengthSquared()
                          + p4.InverseMass * Grad4.LengthSquared()
                          + AlphaTilde;

        if (Denominator <= 1e-7f)
        {
            continue;
        }

        float DeltaLambda = (-ConstraintValue - AlphaTilde * Tet.LagrangeMultiplier) / Denominator;
        Tet.LagrangeMultiplier += DeltaLambda;

        p1.PredictedLocation += Grad1 * (p1.InverseMass * DeltaLambda);
        p2.PredictedLocation += Grad2 * (p2.InverseMass * DeltaLambda);
        p3.PredictedLocation += Grad3 * (p3.InverseMass * DeltaLambda);
        p4.PredictedLocation += Grad4 * (p4.InverseMass * DeltaLambda);
    }
}

const DeformableStructure* DeformableSolver::QueryStructure(uint64_t DesiredStructureIdentifier) const noexcept
{
    for (const auto& Structure : Structures)
    {
        if (Structure.StructureIdentifier == DesiredStructureIdentifier)
        {
            return &Structure;
        }
    }
    return nullptr;
}

size_t DeformableSolver::QueryActiveParticleCount() const noexcept
{
    size_t Total = 0;
    for (const auto& Structure : Structures)
    {
        Total += Structure.Particles.size();
    }
    return Total;
}

} // namespace Frontier
