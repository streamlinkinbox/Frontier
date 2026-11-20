/*============================================================================================================================================
                                                            SURFELOCCLUDERFIELD.H
============================================================================================================================================*/
// 🧩 Stratified Occluder Surfel Field from Real PolygonNetwork Faces (Zero SDF, Zero Analytical Proxies):
//    - Builds OccluderSurfelDisk set from WideBoundingTree leaf triangles (static + vehicle + character real faces)
//    - Per-frame refresh reads deformed character triangles directly (no voxel rebake, no capsule math)
//    - EvaluateSoftDiskShadow: analytic cone-vs-disk occlusion + 0.28 m WideBoundingTree contact ray
//    - Shared by SurfelFaceShadow (Mode 5), StokesContourGiSequence, and SurfelCascadeGiSequence

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/LinearColors.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       HELPERS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline float SurfelClampUnit(float X) noexcept
{
    return std::clamp(X, 0.0f, 1.0f);
}

[[nodiscard]] inline float SurfelSmoothHermite(float Edge0, float Edge1, float X) noexcept
{
    const float T = std::clamp((X - Edge0) / std::max(1.0e-6f, Edge1 - Edge0), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

[[nodiscard]] inline float SurfelSoftMin(float A, float B, float K) noexcept
{
    const float H = std::max(K - std::fabs(A - B), 0.0f) / K;
    return std::min(A, B) - H * H * K * 0.25f;
}

inline void SurfelConstructTangentPair(
    Mathematics::Axis3D Direction,
    Mathematics::Axis3D& OutTangent,
    Mathematics::Axis3D& OutBitangent) noexcept
{
    const Mathematics::Axis3D Reference = (std::fabs(Direction.Z) < 0.9f)
        ? Mathematics::Axis3D { 0.0f, 0.0f, 1.0f }
        : Mathematics::Axis3D { 1.0f, 0.0f, 0.0f };
    OutTangent = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Direction, Reference), { 1.0f, 0.0f, 0.0f });
    OutBitangent = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(OutTangent, Direction), { 0.0f, 1.0f, 0.0f });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct OccluderSurfelDisk
{
    Mathematics::Axis3D Position {};
    Mathematics::Axis3D Normal   {};
    float               RadiusM  = 0.18f;
};

struct RadiantFaceSource
{
    Mathematics::Axis3D    Position {};
    Mathematics::Axis3D    Normal   {};
    float                  AreaM2   = 0.0f;
    Mathematics::LinearRGB Radiance {};
    uint16_t               MaterialOrdinal = 0u;
};

//------------------------------------------------------------------------------------------------------------------------
//                                               SURFEL OCCLUDER FIELD
//------------------------------------------------------------------------------------------------------------------------

class SurfelOccluderField
{
public:
    std::vector<OccluderSurfelDisk> Disks {};

    // Builds stratified disks from real leaf triangles in WideBoundingTree (all faces: static + vehicle + character).
    // Grid cell size 0.28 m; disk radius clamped to [0.11, 0.34] m. No SDF volume, no analytical proxies.
    void BuildFromWideBoundingTree(
        const SpatialAcceleration::WideBoundingTree& SceneTree,
        float                                        CellSizeM = 0.28f) noexcept
    {
        Disks.clear();
        if (SceneTree.Primitives.empty())
        {
            return;
        }
        struct CellAccum
        {
            Mathematics::Axis3D PosSum {};
            Mathematics::Axis3D NrmSum {};
            float               Area   = 0.0f;
            uint32_t            Count  = 0u;
        };
        const Mathematics::Axis3D BoundsMin = SceneTree.RootBoundsMin;
        const Mathematics::Axis3D BoundsMax = SceneTree.RootBoundsMax;
        const Mathematics::Axis3D Extent = BoundsMax - BoundsMin;
        const float SafeCell = std::max(CellSizeM, 1.0e-3f);
        const int NX = std::clamp(static_cast<int>(std::ceil(Extent.X / SafeCell)), 1, 96);
        const int NY = std::clamp(static_cast<int>(std::ceil(Extent.Y / SafeCell)), 1, 96);
        const int NZ = std::clamp(static_cast<int>(std::ceil(Extent.Z / SafeCell)), 1, 48);
        std::vector<CellAccum> Grid(static_cast<std::size_t>(NX) * NY * NZ);
        for (const auto& Tri : SceneTree.Primitives)
        {
            const Mathematics::Axis3D Centroid = Tri.Corner0 + (Tri.Edge01 + Tri.Edge02) * (1.0f / 3.0f);
            const Mathematics::Axis3D Cross = Mathematics::EvaluateCross(Tri.Edge01, Tri.Edge02);
            const float Len = Mathematics::EvaluateLength(Cross);
            const float Area = 0.5f * Len;
            if (Area <= 1.0e-6f)
            {
                continue;
            }
            const Mathematics::Axis3D Normal = Cross * (1.0f / Len);
            const int IX = std::clamp(static_cast<int>(std::floor((Centroid.X - BoundsMin.X) / SafeCell)), 0, NX - 1);
            const int IY = std::clamp(static_cast<int>(std::floor((Centroid.Y - BoundsMin.Y) / SafeCell)), 0, NY - 1);
            const int IZ = std::clamp(static_cast<int>(std::floor((Centroid.Z - BoundsMin.Z) / SafeCell)), 0, NZ - 1);
            auto& Cell = Grid[(static_cast<std::size_t>(IZ) * NY + IY) * NX + IX];
            Cell.PosSum = Cell.PosSum + Centroid * Area;
            Cell.NrmSum = Cell.NrmSum + Normal * Area;
            Cell.Area  += Area;
            Cell.Count += 1u;
        }
        for (const auto& Cell : Grid)
        {
            if (Cell.Count == 0u || Cell.Area <= 1.0e-5f)
            {
                continue;
            }
            OccluderSurfelDisk Disk {};
            Disk.Position = Cell.PosSum * (1.0f / Cell.Area);
            Disk.Normal   = Mathematics::NormalizeAxis(Cell.NrmSum, { 0.0f, 0.0f, 1.0f });
            Disk.RadiusM  = std::clamp(std::sqrt(Cell.Area / Constants::PiFloat) * 1.15f, 0.11f, 0.34f);
            Disks.push_back(Disk);
        }
    }

    // Builds stratified disks from PolygonNetwork faces (triangulated via WideBoundingTree internally).
    void BuildFromPolygonNetwork(
        const GeometrySpace::PolygonNetwork& Network,
        float                                CellSizeM = 0.28f) noexcept
    {
        SpatialAcceleration::WideBoundingTree TempTree {};
        if (!TempTree.BuildFromPolygonNetwork(Network))
        {
            Disks.clear();
            return;
        }
        BuildFromWideBoundingTree(TempTree, CellSizeM);
    }

    // Extracts luminous face sources from real leaf triangles (radiance per material ordinal).
    [[nodiscard]] static std::vector<RadiantFaceSource> ExtractRadiantFaceSources(
        const SpatialAcceleration::WideBoundingTree& SceneTree,
        std::span<const Mathematics::LinearRGB>      RadianceByOrdinal,
        float                                        MinimumLuminance = 1.0f) noexcept
    {
        std::vector<RadiantFaceSource> Sources {};
        for (const auto& Tri : SceneTree.Primitives)
        {
            if (static_cast<std::size_t>(Tri.MaterialOrdinal) >= RadianceByOrdinal.size())
            {
                continue;
            }
            const Mathematics::LinearRGB Radiance = RadianceByOrdinal[Tri.MaterialOrdinal];
            const float Luminance = Mathematics::EvaluateRec709Luminance(Radiance);
            if (Luminance < MinimumLuminance)
            {
                continue;
            }
            const Mathematics::Axis3D Cross = Mathematics::EvaluateCross(Tri.Edge01, Tri.Edge02);
            const float Len = Mathematics::EvaluateLength(Cross);
            const float Area = 0.5f * Len;
            if (Area <= 1.0e-6f)
            {
                continue;
            }
            RadiantFaceSource Source {};
            Source.Position = Tri.Corner0 + (Tri.Edge01 + Tri.Edge02) * (1.0f / 3.0f);
            Source.Normal   = Cross * (1.0f / Len);
            Source.AreaM2   = Area;
            Source.Radiance = Radiance;
            Source.MaterialOrdinal = Tri.MaterialOrdinal;
            Sources.push_back(Source);
        }
        return Sources;
    }

    // Analytic cone-vs-disk soft shadow (zero 3D SDF volume). Contact BVH4 ray handles near-field darkening.
    [[nodiscard]] float EvaluateSoftDiskShadow(
        const Mathematics::Axis3D&                      ReceiverP,
        const Mathematics::Axis3D&                      ReceiverN,
        const Mathematics::Axis3D&                      SunDir,
        float                                           TanSunRadius,
        const SpatialAcceleration::WideBoundingTree*    SceneTree) const noexcept
    {
        const float NdotL = Mathematics::EvaluateDot(ReceiverN, SunDir);
        if (NdotL <= 1.0e-4f)
        {
            return 0.0f;
        }
        if (SceneTree != nullptr)
        {
            SpatialAcceleration::RaySegmentQuery ContactRay {};
            ContactRay.Origin          = ReceiverP + ReceiverN * 0.003f;
            ContactRay.UnitDirection   = SunDir;
            ContactRay.MinimumDistance = 0.002f;
            ContactRay.MaximumDistance = 0.28f;
            if (SceneTree->QueryRayOccluded(ContactRay))
            {
                return 0.0f;
            }
        }
        float MinRatio = 2.0f;
        for (const auto& Disk : Disks)
        {
            const Mathematics::Axis3D Rel = Disk.Position - ReceiverP;
            const float T = Mathematics::EvaluateDot(Rel, SunDir);
            if (T <= 0.14f || T > 9.5f)
            {
                continue;
            }
            const float PlaneDist = Mathematics::EvaluateDot(Rel, ReceiverN);
            const float PlaneFade = SurfelSmoothHermite(0.02f, 0.14f, PlaneDist);
            if (PlaneFade <= 1.0e-4f)
            {
                continue;
            }
            const Mathematics::Axis3D Perp = Rel - SunDir * T;
            const float DistToAxis = Mathematics::EvaluateLength(Perp);
            const float SignedClearance = DistToAxis - Disk.RadiusM * 0.92f;
            const float ConeR = std::max(0.020f, T * TanSunRadius);
            const float Ratio = std::clamp(SignedClearance / ConeR, -1.5f, 1.5f) * PlaneFade + 1.5f * (1.0f - PlaneFade);
            MinRatio = SurfelSoftMin(MinRatio, Ratio, 0.14f);
            if (MinRatio <= -1.0f)
            {
                return 0.0f;
            }
        }
        return SurfelSmoothHermite(-0.95f, 0.95f, MinRatio);
    }
};

// Mode 5 free function: SurfelFaceShadow (real-mesh, zero SDF). Wired into SolarShadowSequence in Phase 1.
[[nodiscard]] inline float EvaluateSurfelFaceShadow(
    const SurfelOccluderField&                    Field,
    const Mathematics::Axis3D&                    ReceiverP,
    const Mathematics::Axis3D&                    ReceiverN,
    const Mathematics::Axis3D&                    SunDir,
    float                                         TanSunRadius,
    const SpatialAcceleration::WideBoundingTree*  SceneTree) noexcept
{
    return Field.EvaluateSoftDiskShadow(ReceiverP, ReceiverN, SunDir, TanSunRadius, SceneTree);
}

} // namespace Frontier::GeometricRaster
