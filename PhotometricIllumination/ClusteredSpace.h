//============================================================================================================================================
// 📦 Frontier/PhotometricIllumination/ClusteredSpace.h — Frustum-Aligned 3D Cluster Light Binning Realm
//============================================================================================================================================

#pragma once

#include "../DeviceExchange/OrientationClassifier.h"
#include <vector>
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                PHOTOMETRIC LIGHT RECORD
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) PhotometricLightRecord
{
    Vector3                 SpatialLocation;                    // [m] light world position
    Vector3                 EmissiveRadiance;                   // [lux] RGB radiant flux intensity
    float                   InfluenceRadius;                    // [m] attenuation cutoff distance
    float                   SpotInnerAngleCosine;               // [-] spot inner cone angle
    float                   SpotOuterAngleCosine;               // [-] spot outer cone angle
    uint32_t                LightCategory;                      // [category] 0=point, 1=spot, 2=directional
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    CLUSTER RECORD
//------------------------------------------------------------------------------------------------------------------------

struct ClusterRecord
{
    uint32_t                LightOffset;                        // [offset] offset into light index span
    uint32_t                LightCount;                         // [count] number of active lights in cluster
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    CLUSTERED SPACE
//------------------------------------------------------------------------------------------------------------------------

class ClusteredSpace
{
public:
    ClusteredSpace(uint32_t SlicesX = 16, uint32_t SlicesY = 16, uint32_t SlicesZ = 32) noexcept;
    ~ClusteredSpace() noexcept = default;

    ClusteredSpace(const ClusteredSpace&) = delete;
    ClusteredSpace& operator=(const ClusteredSpace&) = delete;

    void                    BinLights(const std::vector<PhotometricLightRecord>& Lights, const Matrix4x4& ViewProj) noexcept;

    [[nodiscard]] ClusterRecord QueryCluster(uint32_t x, uint32_t y, uint32_t z) const noexcept;
    [[nodiscard]] const std::vector<uint32_t>& QueryActiveLightIndices() const noexcept { return ActiveLightIndices; }

    // Single unified conversion operator for total clusters
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    uint32_t                SlicesX;                            // [slices] cluster subdivisions in X
    uint32_t                SlicesY;                            // [slices] cluster subdivisions in Y
    uint32_t                SlicesZ;                            // [slices] cluster subdivisions in Z
    std::vector<ClusterRecord> Clusters;                        // [clusters] cluster cell records
    std::vector<uint32_t>   ActiveLightIndices;                 // [indices] flattened light index list
};

template<>
inline size_t ClusteredSpace::Convert<size_t>() const noexcept
{
    return Clusters.size();
}

} // namespace Frontier
