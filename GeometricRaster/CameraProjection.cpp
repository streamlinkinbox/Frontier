//============================================================================================================================================
// 📦 Frontier/GeometricRaster/CameraProjection.cpp — Perspective Camera View Projection Implementation
//============================================================================================================================================

#include "CameraProjection.h"

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

CameraProjection::CameraProjection() noexcept
    : SpatialLocation{ 0.0f, 0.0f, 0.0f }
    , ForwardVector{ 0.0f, 0.0f, 1.0f }
    , RightVector{ 1.0f, 0.0f, 0.0f }
    , UpwardVector{ 0.0f, 1.0f, 0.0f }
    , PitchRadians(0.0f)
    , YawRadians(0.0f)
    , RollRadians(0.0f)
    , FieldOfViewRadians(60.0f * (3.14159265359f / 180.0f))
    , AspectRatio(16.0f / 9.0f)
    , NearPlaneDistance(0.01f)
    , FarPlaneDistance(1000.0f)
{
    RecomputeDirectionalVectors();
}

CameraProjection::CameraProjection(const CameraConfiguration& Config) noexcept
    : SpatialLocation(Config.InitialLocation)
    , ForwardVector{ 0.0f, 0.0f, 1.0f }
    , RightVector{ 1.0f, 0.0f, 0.0f }
    , UpwardVector{ 0.0f, 1.0f, 0.0f }
    , PitchRadians(0.0f)
    , YawRadians(0.0f)
    , RollRadians(0.0f)
    , FieldOfViewRadians(Config.FieldOfViewDegrees * (3.14159265359f / 180.0f))
    , AspectRatio(Config.AspectRatio)
    , NearPlaneDistance(Config.NearPlaneDistance)
    , FarPlaneDistance(Config.FarPlaneDistance)
{
    RecomputeDirectionalVectors();
}

void CameraProjection::AdvanceProjection(float Δτ) noexcept
{
    (void)Δτ;
    // Extensible base: specialized subclasses override for orbital, fly-through, or kinematic camera motion
}

//------------------------------------------------------------------------------------------------------------------------
//                                                COORDINATE CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

void CameraProjection::AssignSpatialLocation(const Vector3& Location) noexcept
{
    SpatialLocation = Location;
}

void CameraProjection::AssignOrientationEuler(float InPitch, float InYaw, float InRoll) noexcept
{
    // Clamp pitch between -89 and +89 degrees to avoid gimbal lock singularity
    constexpr float MaxPitch = 89.0f * (3.14159265359f / 180.0f);
    PitchRadians = std::clamp(InPitch, -MaxPitch, MaxPitch);
    YawRadians   = InYaw;
    RollRadians  = InRoll;
    RecomputeDirectionalVectors();
}

void CameraProjection::AssignFieldOfView(float FieldOfViewDegrees) noexcept
{
    FieldOfViewRadians = FieldOfViewDegrees * (3.14159265359f / 180.0f);
}

void CameraProjection::AssignAspectRatio(float Ratio) noexcept
{
    AspectRatio = Ratio > 0.001f ? Ratio : 1.0f;
}

void CameraProjection::RecomputeDirectionalVectors() noexcept
{
    float CosPitch = std::cos(PitchRadians);
    float SinPitch = std::sin(PitchRadians);
    float CosYaw   = std::cos(YawRadians);
    float SinYaw   = std::sin(YawRadians);

    // Standard Right-Handed Coordinate System: +X Right (Red), +Y Forward (Green), +Z Up (Blue)
    ForwardVector = Vector3{ SinYaw * CosPitch, CosYaw * CosPitch, SinPitch }.Normalized();
    Vector3 WorldUp{ 0.0f, 0.0f, 1.0f }; // Strict +Z Upward Axis
    RightVector   = OrientationClassifier::CrossProduct(ForwardVector, WorldUp).Normalized();
    UpwardVector  = OrientationClassifier::CrossProduct(RightVector, ForwardVector).Normalized();
}

ViewRay CameraProjection::ConstructRay(float NormalizedU, float NormalizedV) const noexcept
{
    float HalfFovTan = std::tan(FieldOfViewRadians * 0.5f);
    float ScreenX    = (2.0f * NormalizedU - 1.0f) * HalfFovTan * AspectRatio;
    float ScreenY    = (1.0f - 2.0f * NormalizedV) * HalfFovTan;

    Vector3 RayDirection = (ForwardVector + RightVector * ScreenX + UpwardVector * ScreenY).Normalized();
    return ViewRay{ SpatialLocation, RayDirection, NearPlaneDistance, FarPlaneDistance };
}

} // namespace Frontier
