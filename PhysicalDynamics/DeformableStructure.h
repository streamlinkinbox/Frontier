//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/DeformableStructure.h — Extended Position-Based Dynamics Particle and Constraint Topology
//============================================================================================================================================

#pragma once

#include "../DeviceExchange/OrientationClassifier.h"
#include <cstdint>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                 DEFORMABLE PARTICLE
//------------------------------------------------------------------------------------------------------------------------

struct DeformableParticle
{
    Vector3                 SpatialLocation;                    // [m] current spatial coordinate x
    Vector3                 PredictedLocation;                  // [m] predicted spatial coordinate x~
    Vector3                 PreviousLocation;                   // [m] previous spatial coordinate x_prev
    Vector3                 Velocity;                           // [m/s] linear velocity v
    float                   InverseMass;                        // [1/kg] inverse mass w = 1/m (0 for fixed anchor)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 DISTANCE CONSTRAINT
//------------------------------------------------------------------------------------------------------------------------

struct DistanceConstraint
{
    uint32_t                ParticleIndexA;                     // [index] first particle index
    uint32_t                ParticleIndexB;                     // [index] second particle index
    float                   RestLength;                         // [m] rest edge distance L₀
    float                   Compliance;                         // [m/N] XPBD compliance α
    float                   LagrangeMultiplier;                 // [N] accumulated multiplier λ
};

//------------------------------------------------------------------------------------------------------------------------
//                                               TETRAHEDRAL CONSTRAINT
//------------------------------------------------------------------------------------------------------------------------

struct TetrahedralConstraint
{
    uint32_t                ParticleIndices[4];                 // [indices] tetrahedral vertex indices
    float                   RestVolume;                         // [m³] rest volume V₀
    float                   Compliance;                         // [m⁴/N] volumetric compliance α
    float                   LagrangeMultiplier;                 // [N] accumulated multiplier λ
};

//------------------------------------------------------------------------------------------------------------------------
//                                               DEFORMABLE STRUCTURE
//------------------------------------------------------------------------------------------------------------------------

struct DeformableStructure
{
    uint64_t                StructureIdentifier;                // [token] unique softbody identifier
    std::vector<DeformableParticle> Particles;                  // [particles] vertex positions and masses
    std::vector<DistanceConstraint> Edges;                      // [constraints] distance constraints
    std::vector<TetrahedralConstraint> Tetrahedra;              // [constraints] volumetric constraints

    // Single unified conversion operator
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;
};

template<>
inline size_t DeformableStructure::Convert<size_t>() const noexcept
{
    return Particles.size();
}

} // namespace Frontier
