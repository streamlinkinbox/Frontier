//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/SpatialStructure.h — Continuous 3D World Transform, Bounds, and Spatial Element Topology
//============================================================================================================================================

#pragma once

#if defined(_MSC_VER)
    #pragma warning(disable: 4324)                              // Disable structure padding alignment warning under /WX
#endif

#include "../DeviceExchange/OrientationClassifier.h"
#include <cstdint>
#include <string>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                SPATIAL STRUCTURE
//------------------------------------------------------------------------------------------------------------------------

struct SpatialStructure
{
    Vector3                 SpatialLocation;                    // [m] world coordinate position
    Quaternion              AngularOrientation;                 // [rad] rotational attitude
    Vector3                 ScaleDimensions;                    // [-] non-uniform 3D scale factors
    Vector3                 LinearVelocity;                     // [m/s] translational speed vector
    Vector3                 AngularVelocity;                    // [rad/s] rotational velocity vector
    uint64_t                SpatialIdentifier;                  // [token] unique spatial tracker ID
    BoundingExtent          LocalBoundingBox;                   // [m] object-space axis-aligned extents
    uint32_t                LayerCategoryIndex;                 // [index] collision/rendering layer
    bool                    VisibleCondition;                   // [bool] rendering visibility status
    bool                    ActiveCondition;                    // [bool] simulation activity status

    constexpr SpatialStructure() noexcept
        : SpatialLocation{ 0.0f, 0.0f, 0.0f }
        , AngularOrientation{ 0.0f, 0.0f, 0.0f, 1.0f }
        , ScaleDimensions{ 1.0f, 1.0f, 1.0f }
        , LinearVelocity{ 0.0f, 0.0f, 0.0f }
        , AngularVelocity{ 0.0f, 0.0f, 0.0f }
        , SpatialIdentifier(0)
        , LocalBoundingBox{ Vector3{ -0.5f, -0.5f, -0.5f }, Vector3{ 0.5f, 0.5f, 0.5f } }
        , LayerCategoryIndex(0)
        , VisibleCondition(true)
        , ActiveCondition(true)
    {
    }

    [[nodiscard]] Vector3 TransformPoint(const Vector3& LocalPoint) const noexcept
    {
        Vector3 ScaledPoint = LocalPoint * ScaleDimensions;
        Vector3 RotatedPoint = OrientationClassifier::RotateVector(ScaledPoint, AngularOrientation);
        return RotatedPoint + SpatialLocation;
    }

    [[nodiscard]] Vector3 TransformDirection(const Vector3& LocalDirection) const noexcept
    {
        return OrientationClassifier::RotateVector(LocalDirection, AngularOrientation);
    }

    // Single unified conversion operator for location
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;
};

template<>
inline Vector3 SpatialStructure::Convert<Vector3>() const noexcept
{
    return SpatialLocation;
}

template<>
inline uint64_t SpatialStructure::Convert<uint64_t>() const noexcept
{
    return SpatialIdentifier;
}

} // namespace Frontier
