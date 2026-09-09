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
    constexpr float TwoPi    = 2.0f * 3.14159265359f;
    PitchRadians = std::clamp(InPitch, -MaxPitch, MaxPitch);
    // Wrap yaw into (−π, π] so it never grows unbounded (precision loss, unreadable telemetry).
    YawRadians   = InYaw - TwoPi * std::floor((InYaw + 3.14159265359f) / TwoPi);
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

    // Right-handed, +Z up (CLAUDE.md §7): yaw 0 / pitch 0 looks along +Y (north); positive yaw turns
    //    toward +X (east, clockwise seen from above); positive pitch looks up toward +Z.
    //    Right = Forward × WorldUp, Up = Right × Forward. These three vectors are the "View" basis consumed by
    //    Engine/Shaders/RayGeneration.slang, which performs the single World → Vulkan-image mapping
    //    (Vulkan image y grows downward; the shader maps the top pixel row to +Up — no further flip anywhere).
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
