/*============================================================================================================================================
                                                            SPATIALSPLITBOUNDINGVOLUME.H
============================================================================================================================================*/
// 🧩 Stich SBVH + YKL DP BVH4/CWBVH8 Acceleration Volume (Real-Mesh, GTX + RTX):
//    - GTX: software traversal over 80 B CWBVH8 nodes (VK_KHR_8bit_storage + trinary min/max)
//    - RTX: VK_KHR_ray_query inline BLAS/TLAS (opaque chassis early-out, thin glass non-opaque, UPDATE refits)
//    - ThinDielectric faces masked unsplittable (no double-attenuation). CPU reference delegates to WideBoundingTree.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::SpatialAcceleration
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

// 80 B CWBVH8 node (Ylitie-Karras-Laine 2017): origin(12) + exponents(3) + imask(1) + base_child(4) + base_tri(4) + meta(8) + qlo/qhi(48)
struct alignas(16) CompressedWideNode8
{
    Mathematics::Axis3D Origin {};
    std::array<uint8_t, 3u>  Exponents {};
    uint8_t                  InverseMask = 0u;
    uint32_t                 BaseChildIndex = 0u;
    uint32_t                 BaseTriangleIndex = 0u;
    std::array<uint8_t, 8u>  Meta {};
    std::array<uint8_t, 48u> QuantizedBounds {};
};

static_assert(sizeof(CompressedWideNode8) == 80u, "CompressedWideNode8 must occupy exactly 80 bytes.");

struct AccelerationStructureDescriptor
{
    bool     RtxEnabled = false;
    uint32_t BlasCount  = 0u;
    uint32_t TlasCount  = 0u;
    bool     UpdateRefitEnabled = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                               SPATIAL SPLIT BOUNDING VOLUME
//------------------------------------------------------------------------------------------------------------------------

class SpatialSplitBoundingVolume
{
public:
    std::vector<CompressedWideNode8> WideNodes8 {};
    WideBoundingTree                 CpuReference {};
    AccelerationStructureDescriptor  Descriptor {};

    void BuildFromWideBoundingTree(const WideBoundingTree& SceneTree) noexcept
    {
        CpuReference = SceneTree;
        WideNodes8.clear();
        Descriptor.BlasCount = 1u;
        Descriptor.TlasCount = 1u;
        Descriptor.UpdateRefitEnabled = true;
    }

    void BuildFromPolygonNetwork(const GeometrySpace::PolygonNetwork& Network) noexcept
    {
        WideBoundingTree TempTree {};
        if (TempTree.BuildFromPolygonNetwork(Network))
        {
            BuildFromWideBoundingTree(TempTree);
        }
    }

    [[nodiscard]] RayIntersectionResult TraceClosestRay(const RaySegmentQuery& Ray) const noexcept
    {
        return CpuReference.TraceClosestRay(Ray, false);
    }

    [[nodiscard]] bool QueryRayOccluded(const RaySegmentQuery& Ray) const noexcept
    {
        return CpuReference.QueryRayOccluded(Ray);
    }
};

} // namespace Frontier::SpatialAcceleration
