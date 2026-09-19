//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/RigidBodyStructure.h — Rigid Body Physical Parameters and Bounding Topology
//============================================================================================================================================

#pragma once

#if defined(_MSC_VER)
    #pragma warning(disable: 4324)                              // Disable structure padding alignment warning under /WX
#endif

#include "../DeviceExchange/OrientationClassifier.h"
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                 COLLISION CATEGORY
//------------------------------------------------------------------------------------------------------------------------

enum class CollisionCategory : uint32_t
{
    StaticTerrain                       = 1 << 0,               // Non-moving environmental geometry
    DynamicBody                         = 1 << 1,               // Active moving physics body
    KinematicBody                       = 1 << 2,               // Script-driven kinematic geometry
    SensorVolume                        = 1 << 3,               // Non-solid trigger detection
    DebrisParticle                      = 1 << 4                // Non-colliding cosmetic debris
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 RIGID BODY STRUCTURE
//------------------------------------------------------------------------------------------------------------------------

struct RigidBodyStructure
{
    Vector3                 SpatialLocation;                    // [m] position in continuous world space
    Quaternion              AngularOrientation;                 // [rad] rotational orientation
    Vector3                 LinearMomentum;                     // [m/s] linear velocity
    Vector3                 AngularMomentum;                    // [rad/s] rotational angular velocity
    uint64_t                BodyIdentifier;                     // [token] unique body tracking identifier
    float                   InertialMass;                       // [kg] mass (0.0 represents static infinite mass)
    float                   FrictionCoefficient;                // [0..1] Coulomb surface friction
    float                   RestitutionCoefficient;             // [0..1] Newtonian restitution coefficient
    float                   LinearDamping;                      // [0..1] linear drag factor
    float                   AngularDamping;                     // [0..1] angular drag factor
    CollisionCategory       CategoryMask;                       // [mask] collision participation mask
    bool                    MotionActive;                       // [bool] sleeping or active condition

    // Single unified conversion operator
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;
};

template<>
inline Vector3 RigidBodyStructure::Convert<Vector3>() const noexcept
{
    return SpatialLocation;
}

template<>
inline float RigidBodyStructure::Convert<float>() const noexcept
{
    return InertialMass;
}

} // namespace Frontier
