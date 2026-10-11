//============================================================================================================================================
//                                                    DISTANCEFIELDSPACE.CPP
//============================================================================================================================================
// 📦 3D Signed Distance Field voxel representation for geometric query, ray marching, and soft shadow evaluation.

#include "DistanceFieldSpace.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <cstring>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

DistanceFieldSpace::DistanceFieldSpace(uint32_t InResolutionX,
                                       uint32_t InResolutionY,
                                       uint32_t InResolutionZ,
                                       Vector3 InBoundingMinimum,
                                       Vector3 InBoundingMaximum) noexcept
    : ResolutionX(InResolutionX)
    , ResolutionY(InResolutionY)
    , ResolutionZ(InResolutionZ)
    , BoundingMinimum(InBoundingMinimum)
    , BoundingMaximum(InBoundingMaximum)
{
    VoxelSpacing.x = (ResolutionX > 0u) ? ((BoundingMaximum.x - BoundingMinimum.x) / static_cast<float>(ResolutionX)) : 0.0f;
    VoxelSpacing.y = (ResolutionY > 0u) ? ((BoundingMaximum.y - BoundingMinimum.y) / static_cast<float>(ResolutionY)) : 0.0f;
    VoxelSpacing.z = (ResolutionZ > 0u) ? ((BoundingMaximum.z - BoundingMinimum.z) / static_cast<float>(ResolutionZ)) : 0.0f;
    DistanceSamples.resize(static_cast<size_t>(ResolutionX) * ResolutionY * ResolutionZ, 1e6f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 TRILINEAR SAMPLING
//------------------------------------------------------------------------------------------------------------------------

float DistanceFieldSpace::SampleDistance(Vector3 SpatialPosition) const noexcept
{
    if (ResolutionX == 0u || ResolutionY == 0u || ResolutionZ == 0u || DistanceSamples.empty())
        return 1e6f;

    const float SpanX = BoundingMaximum.x - BoundingMinimum.x;
    const float SpanY = BoundingMaximum.y - BoundingMinimum.y;
    const float SpanZ = BoundingMaximum.z - BoundingMinimum.z;

    if (SpanX <= 1e-6f || SpanY <= 1e-6f || SpanZ <= 1e-6f)
        return 1e6f;

    // Check distance outside bounding box to prevent clamping artifacts
    const float OutDx = std::max(0.0f, std::max(BoundingMinimum.x - SpatialPosition.x, SpatialPosition.x - BoundingMaximum.x));
    const float OutDy = std::max(0.0f, std::max(BoundingMinimum.y - SpatialPosition.y, SpatialPosition.y - BoundingMaximum.y));
    const float OutDz = std::max(0.0f, std::max(BoundingMinimum.z - SpatialPosition.z, SpatialPosition.z - BoundingMaximum.z));
    const float DistToBox = std::sqrt(OutDx * OutDx + OutDy * OutDy + OutDz * OutDz);

    // If outside the bounding box, project to box surface and add Euclidean distance outside
    if (DistToBox > 1e-4f)
    {
        const Vector3 ClampedPos = {
            std::clamp(SpatialPosition.x, BoundingMinimum.x, BoundingMaximum.x),
            std::clamp(SpatialPosition.y, BoundingMinimum.y, BoundingMaximum.y),
            std::clamp(SpatialPosition.z, BoundingMinimum.z, BoundingMaximum.z)
        };
        return DistToBox + SampleDistance(ClampedPos);
    }

    // Fractional voxel coordinates with half-cell centering
    const float CoordX = (SpatialPosition.x - BoundingMinimum.x) / SpanX * static_cast<float>(ResolutionX) - 0.5f;
    const float CoordY = (SpatialPosition.y - BoundingMinimum.y) / SpanY * static_cast<float>(ResolutionY) - 0.5f;
    const float CoordZ = (SpatialPosition.z - BoundingMinimum.z) / SpanZ * static_cast<float>(ResolutionZ) - 0.5f;

    // Integer cell index clamped to interior span
    const int32_t IndexX = std::clamp(static_cast<int32_t>(std::floor(CoordX)), 0, static_cast<int32_t>(ResolutionX) - 2);
    const int32_t IndexY = std::clamp(static_cast<int32_t>(std::floor(CoordY)), 0, static_cast<int32_t>(ResolutionY) - 2);
    const int32_t IndexZ = std::clamp(static_cast<int32_t>(std::floor(CoordZ)), 0, static_cast<int32_t>(ResolutionZ) - 2);

    // Trilinear interpolation weights
    const float FractionX = std::clamp(CoordX - static_cast<float>(IndexX), 0.0f, 1.0f);
    const float FractionY = std::clamp(CoordY - static_cast<float>(IndexY), 0.0f, 1.0f);
    const float FractionZ = std::clamp(CoordZ - static_cast<float>(IndexZ), 0.0f, 1.0f);

    const auto ReadVoxel = [this](int32_t X, int32_t Y, int32_t Z) noexcept -> float
    {
        const size_t LinearIndex = static_cast<size_t>(Z) * ResolutionY * ResolutionX +
                                   static_cast<size_t>(Y) * ResolutionX +
                                   static_cast<size_t>(X);
        return DistanceSamples[LinearIndex];
    };

    const float Sample000 = ReadVoxel(IndexX,     IndexY,     IndexZ);
    const float Sample100 = ReadVoxel(IndexX + 1, IndexY,     IndexZ);
    const float Sample010 = ReadVoxel(IndexX,     IndexY + 1, IndexZ);
    const float Sample110 = ReadVoxel(IndexX + 1, IndexY + 1, IndexZ);
    const float Sample001 = ReadVoxel(IndexX,     IndexY,     IndexZ + 1);
    const float Sample101 = ReadVoxel(IndexX + 1, IndexY,     IndexZ + 1);
    const float Sample011 = ReadVoxel(IndexX,     IndexY + 1, IndexZ + 1);
    const float Sample111 = ReadVoxel(IndexX + 1, IndexY + 1, IndexZ + 1);

    const float InterpolatedX00 = Sample000 + (Sample100 - Sample000) * FractionX;
    const float InterpolatedX10 = Sample010 + (Sample110 - Sample010) * FractionX;
    const float InterpolatedX01 = Sample001 + (Sample101 - Sample001) * FractionX;
    const float InterpolatedX11 = Sample011 + (Sample111 - Sample011) * FractionX;

    const float InterpolatedY0 = InterpolatedX00 + (InterpolatedX10 - InterpolatedX00) * FractionY;
    const float InterpolatedY1 = InterpolatedX01 + (InterpolatedX11 - InterpolatedX01) * FractionY;

    return InterpolatedY0 + (InterpolatedY1 - InterpolatedY0) * FractionZ;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 NORMAL SAMPLING
//------------------------------------------------------------------------------------------------------------------------

Vector3 DistanceFieldSpace::SampleNormal(Vector3 SpatialPosition) const noexcept
{
    const float EpsilonX = (VoxelSpacing.x > 0.0f) ? (VoxelSpacing.x * 0.5f) : 0.005f;
    const float EpsilonY = (VoxelSpacing.y > 0.0f) ? (VoxelSpacing.y * 0.5f) : 0.005f;
    const float EpsilonZ = (VoxelSpacing.z > 0.0f) ? (VoxelSpacing.z * 0.5f) : 0.005f;

    const float Dx = SampleDistance({ SpatialPosition.x + EpsilonX, SpatialPosition.y, SpatialPosition.z }) -
                     SampleDistance({ SpatialPosition.x - EpsilonX, SpatialPosition.y, SpatialPosition.z });
    const float Dy = SampleDistance({ SpatialPosition.x, SpatialPosition.y + EpsilonY, SpatialPosition.z }) -
                     SampleDistance({ SpatialPosition.x, SpatialPosition.y - EpsilonY, SpatialPosition.z });
    const float Dz = SampleDistance({ SpatialPosition.x, SpatialPosition.y, SpatialPosition.z + EpsilonZ }) -
                     SampleDistance({ SpatialPosition.x, SpatialPosition.y, SpatialPosition.z - EpsilonZ });

    const Vector3 Gradient = { Dx / (2.0f * EpsilonX), Dy / (2.0f * EpsilonY), Dz / (2.0f * EpsilonZ) };
    const float GradientLength = Gradient.Length();
    return (GradientLength > 1e-7f) ? (Gradient / GradientLength) : Vector3{ 0.0f, 0.0f, 1.0f };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    RAY MARCHING
//------------------------------------------------------------------------------------------------------------------------

DistanceFieldHitRecord DistanceFieldSpace::MarchRay(Vector3 RayOrigin,
                                                    Vector3 RayDirection,
                                                    float MinimumDistance,
                                                    float MaximumDistance,
                                                    float SurfaceThreshold,
                                                    uint32_t MaximumSteps,
                                                    float StepRelaxation) const noexcept
{
    DistanceFieldHitRecord HitRecord{};
    HitRecord.TravelDistance   = MinimumDistance;
    HitRecord.StepCount        = 0u;
    HitRecord.InstanceIdentity = 0u;
    HitRecord.HasHit           = false;

    // Ray bounding box slab intersection test
    float BoxEntryDistance = MinimumDistance;
    float BoxExitDistance  = MaximumDistance;

    const auto IntersectSlab = [&](float OriginComponent, float DirectionComponent, float MinBound, float MaxBound) noexcept -> bool
    {
        if (std::abs(DirectionComponent) > 1e-7f)
        {
            float Near = (MinBound - OriginComponent) / DirectionComponent;
            float Far  = (MaxBound - OriginComponent) / DirectionComponent;
            if (Near > Far) std::swap(Near, Far);
            BoxEntryDistance = std::max(BoxEntryDistance, Near);
            BoxExitDistance  = std::min(BoxExitDistance,  Far);
            return BoxEntryDistance <= BoxExitDistance;
        }
        return (OriginComponent >= MinBound && OriginComponent <= MaxBound);
    };

    if (!IntersectSlab(RayOrigin.x, RayDirection.x, BoundingMinimum.x, BoundingMaximum.x) ||
        !IntersectSlab(RayOrigin.y, RayDirection.y, BoundingMinimum.y, BoundingMaximum.y) ||
        !IntersectSlab(RayOrigin.z, RayDirection.z, BoundingMinimum.z, BoundingMaximum.z))
    {
        return HitRecord;
    }

    float CurrentDistance = std::max(MinimumDistance, BoxEntryDistance);

    for (uint32_t Step = 0u; Step < MaximumSteps && CurrentDistance < BoxExitDistance; ++Step)
    {
        HitRecord.StepCount++;
        const Vector3 SamplePosition = RayOrigin + RayDirection * CurrentDistance;
        const float EvaluatedDistance = SampleDistance(SamplePosition);

        if (EvaluatedDistance <= SurfaceThreshold)
        {
            HitRecord.HasHit         = true;
            HitRecord.TravelDistance = CurrentDistance;
            HitRecord.HitPosition    = SamplePosition;
            HitRecord.SurfaceNormal  = SampleNormal(SamplePosition);
            return HitRecord;
        }

        const float SafeAdvance = std::max(EvaluatedDistance * StepRelaxation, SurfaceThreshold * 0.5f);
        CurrentDistance += SafeAdvance;
    }

    return HitRecord;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SOFT SHADOWS
//------------------------------------------------------------------------------------------------------------------------

float DistanceFieldSpace::MarchSoftShadow(Vector3 ShadingPosition,
                                          Vector3 IlluminantDirection,
                                          float MinimumDistance,
                                          float MaximumDistance,
                                          float LightAngularSize,
                                          uint32_t MaximumSteps) const noexcept
{
    float ShadowPenumbra = 1.0f;
    const float PenumbraScale = 1.0f / std::max(0.01f, std::tan(LightAngularSize));

    // Ray bounding box slab intersection test with penumbra margin
    float BoxEntryDistance = MinimumDistance;
    float BoxExitDistance  = MaximumDistance;
    const float Margin     = 0.35f;

    const auto IntersectSlab = [&](float OriginComponent, float DirectionComponent, float MinBound, float MaxBound) noexcept -> bool
    {
        if (std::abs(DirectionComponent) > 1e-7f)
        {
            float Near = (MinBound - OriginComponent) / DirectionComponent;
            float Far  = (MaxBound - OriginComponent) / DirectionComponent;
            if (Near > Far) std::swap(Near, Far);
            BoxEntryDistance = std::max(BoxEntryDistance, Near);
            BoxExitDistance  = std::min(BoxExitDistance,  Far);
            return BoxEntryDistance <= BoxExitDistance;
        }
        return (OriginComponent >= MinBound && OriginComponent <= MaxBound);
    };

    if (!IntersectSlab(ShadingPosition.x, IlluminantDirection.x, BoundingMinimum.x - Margin, BoundingMaximum.x + Margin) ||
        !IntersectSlab(ShadingPosition.y, IlluminantDirection.y, BoundingMinimum.y - Margin, BoundingMaximum.y + Margin) ||
        !IntersectSlab(ShadingPosition.z, IlluminantDirection.z, BoundingMinimum.z - Margin, BoundingMaximum.z + Margin))
    {
        return 1.0f; // Shadow ray misses the object bounds entirely
    }

    float CurrentDistance = std::max(MinimumDistance, BoxEntryDistance);
    float PrevH = 1e10f;

    for (uint32_t Step = 0u; Step < MaximumSteps && CurrentDistance < BoxExitDistance; ++Step)
    {
        const Vector3 SamplePosition = ShadingPosition + IlluminantDirection * CurrentDistance;
        const float EvaluatedDistance = SampleDistance(SamplePosition);

        if (EvaluatedDistance <= 0.001f)
            return 0.0f;

        const float StepPenumbra = PenumbraScale * EvaluatedDistance / CurrentDistance;
        ShadowPenumbra = std::min(ShadowPenumbra, StepPenumbra);

        const float StepAdvance = std::max(EvaluatedDistance * 0.85f, 0.005f);
        CurrentDistance += StepAdvance;
    }

    return std::clamp(ShadowPenumbra, 0.0f, 1.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SERIALIZATION
//------------------------------------------------------------------------------------------------------------------------

bool DistanceFieldSpace::WriteToStream(std::ostream& Stream) const noexcept
{
    const uint32_t Magic = kDistanceFieldMagic;
    Stream.write(reinterpret_cast<const char*>(&Magic), sizeof(Magic));
    Stream.write(reinterpret_cast<const char*>(&ResolutionX), sizeof(ResolutionX));
    Stream.write(reinterpret_cast<const char*>(&ResolutionY), sizeof(ResolutionY));
    Stream.write(reinterpret_cast<const char*>(&ResolutionZ), sizeof(ResolutionZ));
    Stream.write(reinterpret_cast<const char*>(&BoundingMinimum), sizeof(BoundingMinimum));
    Stream.write(reinterpret_cast<const char*>(&BoundingMaximum), sizeof(BoundingMaximum));

    const size_t SampleCount = DistanceSamples.size();
    Stream.write(reinterpret_cast<const char*>(DistanceSamples.data()), SampleCount * sizeof(float));
    return Stream.good();
}

bool DistanceFieldSpace::ReadFromStream(std::istream& Stream, std::string* OutError) noexcept
{
    uint32_t Magic = 0u;
    Stream.read(reinterpret_cast<char*>(&Magic), sizeof(Magic));
    if (Magic != kDistanceFieldMagic)
    {
        if (OutError) *OutError = "Invalid SDF magic header";
        return false;
    }

    Stream.read(reinterpret_cast<char*>(&ResolutionX), sizeof(ResolutionX));
    Stream.read(reinterpret_cast<char*>(&ResolutionY), sizeof(ResolutionY));
    Stream.read(reinterpret_cast<char*>(&ResolutionZ), sizeof(ResolutionZ));
    Stream.read(reinterpret_cast<char*>(&BoundingMinimum), sizeof(BoundingMinimum));
    Stream.read(reinterpret_cast<char*>(&BoundingMaximum), sizeof(BoundingMaximum));

    VoxelSpacing.x = (ResolutionX > 0u) ? ((BoundingMaximum.x - BoundingMinimum.x) / static_cast<float>(ResolutionX)) : 0.0f;
    VoxelSpacing.y = (ResolutionY > 0u) ? ((BoundingMaximum.y - BoundingMinimum.y) / static_cast<float>(ResolutionY)) : 0.0f;
    VoxelSpacing.z = (ResolutionZ > 0u) ? ((BoundingMaximum.z - BoundingMinimum.z) / static_cast<float>(ResolutionZ)) : 0.0f;

    const size_t SampleCount = static_cast<size_t>(ResolutionX) * ResolutionY * ResolutionZ;
    DistanceSamples.resize(SampleCount);
    Stream.read(reinterpret_cast<char*>(DistanceSamples.data()), SampleCount * sizeof(float));

    if (!Stream.good())
    {
        if (OutError) *OutError = "Stream read error reading distance payload";
        return false;
    }
    return true;
}

bool DistanceFieldSpace::SaveToFile(const std::string& FilePath) const noexcept
{
    std::ofstream File(FilePath, std::ios::binary);
    if (!File.is_open()) return false;
    return WriteToStream(File);
}

bool DistanceFieldSpace::LoadFromFile(const std::string& FilePath, std::string* OutError) noexcept
{
    std::ifstream File(FilePath, std::ios::binary);
    if (!File.is_open())
    {
        if (OutError) *OutError = "Failed to open file: " + FilePath;
        return false;
    }
    return ReadFromStream(File, OutError);
}

} // namespace Frontier
