/*==========================================================================================================================================
                                                 BOUNDING VOLUME ATLAS
                                     ------------------------------------------------------------------
             16-bin Surface-Area-Heuristic (SAH) software bounding-volume acceleration atlas over
             FacetField or ScreenSpaceClusterRefinement cuts, supporting GTX-first GPU compute ray
             queries (closest opaque hit, two-interface dielectric glass refraction peel, and Beer-Lambert
             glass-attenuated shadow transmittance) with zero hardware RTX raytracing dependency.
==========================================================================================================================================*/
// 🧩 16-bin SAH software bounding-volume builder, CPU ray oracle, and float4 GPU upload packer.

#pragma once

#include "Engine/Geometry/GeometryProcessing/ScreenSpaceClusterRefinement.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier
{

struct EnclosureCell
{
    std::array<float, 3> LowerBounds     = {0.0f, 0.0f, 0.0f};
    uint32_t             LeftOrFirst     = 0u; // Interior: LeftCell index; Leaf: FirstTriangle index
    std::array<float, 3> UpperBounds     = {0.0f, 0.0f, 0.0f};
    uint32_t             RightOrCount    = 0u; // Interior: RightCell index | 0x80000000u; Leaf: TriangleCount (< 0x80000000u)
};

struct PackedRayTriangle
{
    std::array<float, 3> Vertex0          = {};
    float                Uv0U             = 0.0f;
    std::array<float, 3> Edge1            = {}; // Vertex1 - Vertex0
    float                Uv0V             = 0.0f;
    std::array<float, 3> Edge2            = {}; // Vertex2 - Vertex0
    uint32_t             ClassificationId = 0u;
    std::array<float, 2> Uv1              = {};
    std::array<float, 2> Uv2              = {};
    bool                 TransmissiveGlass = false;
    float                RefractiveIndex   = 1.52f;
    std::array<float, 3> AbsorptionPerM    = {0.12f, 0.04f, 0.06f};
};

struct SoftwareRayHit
{
    bool                 Hit               = false;
    float                DistanceMeters    = 1.0e30f;
    uint32_t             TriangleIndex     = UINT32_MAX;
    uint32_t             ClassificationId  = 0u;
    std::array<float, 3> WorldPosition     = {};
    std::array<float, 3> GeometricNormal   = {0.0f, 1.0f, 0.0f};
    std::array<float, 2> TextureCoord      = {};
};

class BoundingVolumeAtlas
{
  public:
    bool BuildFromFacetField(const FacetField&                    SourceFacets,
                             const ScreenSpaceClusterRefinement*  MaterialSource = nullptr);

    bool BuildFromRefinementCut(const ScreenSpaceClusterRefinement& Refinement,
                                const ClusterRuntimeFrameReport&    CutReport);

    // CPU reference ray query (opaque triangles; optionally includes transmissive glass)
    [[nodiscard]] SoftwareRayHit TraceClosestRay(std::array<float, 3> OriginMeters,
                                                 std::array<float, 3> DirectionUnit,
                                                 float                MinDistanceMeters = 1.0e-3f,
                                                 float                MaxDistanceMeters = 1.0e5f,
                                                 bool                 IncludeGlass      = false) const noexcept;

    // CPU reference shadow transmittance: {0,0,0} when blocked by opaque geometry, Beer-Lambert tinted through glass
    [[nodiscard]] std::array<float, 3> TraceShadowTransmittance(std::array<float, 3> OriginMeters,
                                                                std::array<float, 3> DirectionUnit,
                                                                float                MaxDistanceMeters) const noexcept;

    // Packs EnclosureCells (2 x float4 per cell) and PackedRayTriangles (5 x float4 per triangle) for Slang GPU search.
    void PackForGpu(std::vector<std::array<float, 4>>& OutCellFloat4Table,
                    std::vector<std::array<float, 4>>& OutTriangleFloat4Table) const;

    [[nodiscard]] uint32_t CellCount() const noexcept { return static_cast<uint32_t>(Cells_.size()); }
    [[nodiscard]] uint32_t TriangleCount() const noexcept { return static_cast<uint32_t>(Triangles_.size()); }
    [[nodiscard]] bool     Empty() const noexcept { return Cells_.empty() || Triangles_.empty(); }

  private:
    uint32_t BuildRecursive(std::vector<uint32_t>&                 Indices,
                             std::vector<std::array<float, 3>>&     Centroids,
                            std::vector<std::array<float, 3>>&     TriBoundsMin,
                            std::vector<std::array<float, 3>>&     TriBoundsMax,
                            uint32_t                               Begin,
                            uint32_t                               Count);

    std::vector<EnclosureCell>     Cells_{};
    std::vector<PackedRayTriangle> Triangles_{};
};

} // namespace Frontier
