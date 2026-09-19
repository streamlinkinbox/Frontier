//============================================================================================================================================
// 📦 Frontier/PhotometricIllumination/ClusteredSpace.cpp — Cluster Light Binning Implementation
//============================================================================================================================================

#include "ClusteredSpace.h"

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

ClusteredSpace::ClusteredSpace(uint32_t DesiredSlicesX, uint32_t DesiredSlicesY, uint32_t DesiredSlicesZ) noexcept
    : SlicesX(DesiredSlicesX)
    , SlicesY(DesiredSlicesY)
    , SlicesZ(DesiredSlicesZ)
    , Clusters(DesiredSlicesX * DesiredSlicesY * DesiredSlicesZ, ClusterRecord{ 0, 0 })
{
}

//------------------------------------------------------------------------------------------------------------------------
//                                                BINNING OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

void ClusteredSpace::BinLights(const std::vector<PhotometricLightRecord>& Lights, const Matrix4x4& ViewProj) noexcept
{
    (void)ViewProj;
    ActiveLightIndices.clear();

    for (size_t i = 0; i < Lights.size(); ++i)
    {
        ActiveLightIndices.push_back(static_cast<uint32_t>(i));
    }

    uint32_t TotalLightCount = static_cast<uint32_t>(Lights.size());
    for (auto& cluster : Clusters)
    {
        cluster.LightOffset = 0;
        cluster.LightCount = TotalLightCount;
    }
}

ClusterRecord ClusteredSpace::QueryCluster(uint32_t x, uint32_t y, uint32_t z) const noexcept
{
    if (x < SlicesX && y < SlicesY && z < SlicesZ)
    {
        return Clusters[z * (SlicesX * SlicesY) + y * SlicesX + x];
    }
    return ClusterRecord{ 0, 0 };
}

} // namespace Frontier
