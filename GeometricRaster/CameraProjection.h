//============================================================================================================================================
// 📦 Frontier/GeometricRaster/CameraProjection.h — Extensible 3D Perspective View Projection and Ray Generation
//============================================================================================================================================

#pragma once

#if defined(_MSC_VER)
    #pragma warning(disable: 4324)                              // Disable structure padding alignment warning under /WX
#endif

#include "../DeviceExchange/OrientationClassifier.h"
#include <cmath>
#include <algorithm>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    VIEW RAY
//------------------------------------------------------------------------------------------------------------------------

struct ViewRay
{
    Vector3                 OriginLocation;                     // [m] ray emitter coordinate
    Vector3                 UnitDirection;                      // [-] normalized directional vector
    float                   NearClippingDistance;               // [m] near boundary distance
    float                   FarClippingDistance;                // [m] far boundary distance
};

//------------------------------------------------------------------------------------------------------------------------
//                                               CAMERA CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

struct CameraConfiguration
{
    Vector3                 InitialLocation;                    // [m] starting camera position
    float                   FieldOfViewDegrees;                 // [deg] vertical field of view
    float                   AspectRatio;                        // [-] width / height ratio
    float                   NearPlaneDistance;                  // [m] near clipping plane
    float                   FarPlaneDistance;                   // [m] far clipping plane
};

//------------------------------------------------------------------------------------------------------------------------
//                                                CAMERA PROJECTION
//------------------------------------------------------------------------------------------------------------------------

class CameraProjection
{
public:
    CameraProjection() noexcept;
    explicit CameraProjection(const CameraConfiguration& Config) noexcept;
    virtual ~CameraProjection() noexcept = default;

    CameraProjection(const CameraProjection&) = default;
    CameraProjection& operator=(const CameraProjection&) = default;

    virtual void            AdvanceProjection(float Δτ) noexcept;

    void                    AssignSpatialLocation(const Vector3& Location) noexcept;
    void                    AssignOrientationEuler(float PitchRadians, float YawRadians, float RollRadians) noexcept;
    void                    AssignFieldOfView(float FieldOfViewDegrees) noexcept;
    void                    AssignAspectRatio(float Ratio) noexcept;

    [[nodiscard]] ViewRay   ConstructRay(float NormalizedU, float NormalizedV) const noexcept;

    [[nodiscard]] const Vector3&    QuerySpatialLocation() const noexcept { return SpatialLocation; }
    [[nodiscard]] const Vector3&    QueryForwardVector() const noexcept { return ForwardVector; }
    [[nodiscard]] const Vector3&    QueryRightVector() const noexcept { return RightVector; }
    [[nodiscard]] const Vector3&    QueryUpwardVector() const noexcept { return UpwardVector; }
    [[nodiscard]] float             QueryFieldOfViewRadians() const noexcept { return FieldOfViewRadians; }
    [[nodiscard]] float             QueryAspectRatio() const noexcept { return AspectRatio; }
    [[nodiscard]] float             QueryPitchRadians() const noexcept { return PitchRadians; }
    [[nodiscard]] float             QueryYawRadians() const noexcept { return YawRadians; }

    // Single unified conversion operator for spatial location
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

protected:
    void                    RecomputeDirectionalVectors() noexcept;

    Vector3                 SpatialLocation;                    // [m] camera viewpoint coordinate
    Vector3                 ForwardVector;                      // [-] unit forward vector
    Vector3                 RightVector;                        // [-] unit right vector
    Vector3                 UpwardVector;                       // [-] unit upward vector
    float                   PitchRadians;                       // [rad] vertical pitch angle
    float                   YawRadians;                         // [rad] horizontal yaw angle
    float                   RollRadians;                        // [rad] roll angle
    float                   FieldOfViewRadians;                 // [rad] vertical perspective angle
    float                   AspectRatio;                        // [-] horizontal / vertical ratio
    float                   NearPlaneDistance;                  // [m] near frustum distance
    float                   FarPlaneDistance;                   // [m] far frustum distance
};

template<>
inline Vector3 CameraProjection::Convert<Vector3>() const noexcept
{
    return SpatialLocation;
}

template<>
inline float CameraProjection::Convert<float>() const noexcept
{
    return FieldOfViewRadians;
}

} // namespace Frontier
