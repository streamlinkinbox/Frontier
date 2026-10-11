//============================================================================================================================================
//                                                    DISTANCEFIELDSPACE.H
//============================================================================================================================================
// 📦 3D Signed Distance Field voxel representation for geometric query, ray marching, and soft shadow evaluation.

#pragma once

#if defined(_MSC_VER)
    #pragma warning(disable: 4324)                              // Disable structure padding alignment warning under /WX
#endif

#include "../DeviceExchange/OrientationClassifier.h"
#include <vector>
#include <string>
#include <cstdint>
#include <iosfwd>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                RAY MARCH HIT RECORD
//------------------------------------------------------------------------------------------------------------------------

struct DistanceFieldHitRecord
{
    Vector3                 HitPosition;                        // [m] world or object space hit position
    Vector3                 SurfaceNormal;                      // [-] normalized surface gradient normal
    float                   TravelDistance;                     // [m] ray parametric distance to hit
    uint32_t                StepCount;                          // [-] sphere tracing steps evaluated
    uint32_t                InstanceIdentity;                   // [-] identity of hit placement instance
    bool                    HasHit;                             // [-] true when ray intersected surface threshold
};

//------------------------------------------------------------------------------------------------------------------------
//                                                DISTANCE FIELD SPACE
//------------------------------------------------------------------------------------------------------------------------

class DistanceFieldSpace
{
public:
    static constexpr uint32_t kDistanceFieldMagic = 0x31464453u; // 'SDF1' little-endian

    DistanceFieldSpace() noexcept = default;

    DistanceFieldSpace(uint32_t InResolutionX,
                       uint32_t InResolutionY,
                       uint32_t InResolutionZ,
                       Vector3 InBoundingMinimum,
                       Vector3 InBoundingMaximum) noexcept;

    // Trilinear continuous sampling of the distance field at spatial coordinate
    [[nodiscard]] float SampleDistance(Vector3 SpatialPosition) const noexcept;

    // Gradient normal derived by central differences at spatial coordinate
    [[nodiscard]] Vector3 SampleNormal(Vector3 SpatialPosition) const noexcept;

    // Sphere-tracing ray march through the voxel field
    [[nodiscard]] DistanceFieldHitRecord MarchRay(Vector3 RayOrigin,
                                                  Vector3 RayDirection,
                                                  float MinimumDistance,
                                                  float MaximumDistance,
                                                  float SurfaceThreshold = 0.002f,
                                                  uint32_t MaximumSteps  = 96u,
                                                  float StepRelaxation   = 0.85f) const noexcept;

    // Soft shadow penumbra factor evaluated by sphere tracing toward illuminant direction
    [[nodiscard]] float MarchSoftShadow(Vector3 ShadingPosition,
                                        Vector3 IlluminantDirection,
                                        float MinimumDistance,
                                        float MaximumDistance,
                                        float LightAngularSize = 0.175f,
                                        uint32_t MaximumSteps  = 32u) const noexcept;

    // File serialization
    [[nodiscard]] bool SaveToFile(const std::string& FilePath) const noexcept;
    [[nodiscard]] bool LoadFromFile(const std::string& FilePath, std::string* OutError = nullptr) noexcept;

    // Stream serialization
    [[nodiscard]] bool WriteToStream(std::ostream& Stream) const noexcept;
    [[nodiscard]] bool ReadFromStream(std::istream& Stream, std::string* OutError = nullptr) noexcept;

    // Accessors
    [[nodiscard]] uint32_t GetResolutionX() const noexcept { return ResolutionX; }
    [[nodiscard]] uint32_t GetResolutionY() const noexcept { return ResolutionY; }
    [[nodiscard]] uint32_t GetResolutionZ() const noexcept { return ResolutionZ; }
    [[nodiscard]] Vector3 GetBoundingMinimum() const noexcept { return BoundingMinimum; }
    [[nodiscard]] Vector3 GetBoundingMaximum() const noexcept { return BoundingMaximum; }
    [[nodiscard]] Vector3 GetVoxelSpacing() const noexcept { return VoxelSpacing; }
    [[nodiscard]] size_t GetSampleCount() const noexcept { return DistanceSamples.size(); }
    [[nodiscard]] const float* GetSampleData() const noexcept { return DistanceSamples.data(); }
    [[nodiscard]] float* GetMutableSampleData() noexcept { return DistanceSamples.data(); }

    [[nodiscard]] float GetVoxelSample(uint32_t X, uint32_t Y, uint32_t Z) const noexcept
    {
        return DistanceSamples[static_cast<size_t>(Z) * ResolutionY * ResolutionX +
                               static_cast<size_t>(Y) * ResolutionX + X];
    }

    void SetVoxelSample(uint32_t X, uint32_t Y, uint32_t Z, float DistanceValue) noexcept
    {
        DistanceSamples[static_cast<size_t>(Z) * ResolutionY * ResolutionX +
                        static_cast<size_t>(Y) * ResolutionX + X] = DistanceValue;
    }

private:
    uint32_t                ResolutionX       = 0u;             // [-] voxel grid resolution x
    uint32_t                ResolutionY       = 0u;             // [-] voxel grid resolution y
    uint32_t                ResolutionZ       = 0u;             // [-] voxel grid resolution z
    Vector3                 BoundingMinimum   = { 0.0f, 0.0f, 0.0f }; // [m] minimum spatial bound
    Vector3                 BoundingMaximum   = { 0.0f, 0.0f, 0.0f }; // [m] maximum spatial bound
    Vector3                 VoxelSpacing      = { 0.0f, 0.0f, 0.0f }; // [m] extent per voxel cell
    std::vector<float>      DistanceSamples;                    // [m] contiguous voxel distance payload
};

} // namespace Frontier
