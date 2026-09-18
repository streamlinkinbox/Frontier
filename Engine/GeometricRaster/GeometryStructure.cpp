//============================================================================================================================================
// 📦 Frontier/GeometricRaster/GeometryStructure.cpp — Polyhedral Cluster Storage Implementation
//============================================================================================================================================

#include "GeometryStructure.h"

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                GEOMETRY INGESTION
//------------------------------------------------------------------------------------------------------------------------

uint32_t GeometryStructure::RegisterCluster(PolyhedralCluster Cluster) noexcept
{
    uint32_t Index = static_cast<uint32_t>(Clusters.size());
    Clusters.push_back(Cluster);
    return Index;
}

void GeometryStructure::AppendVertices(const VertexRecord* NewVertices, size_t Count) noexcept
{
    if (NewVertices && Count > 0)
    {
        Vertices.insert(Vertices.end(), NewVertices, NewVertices + Count);
    }
}

void GeometryStructure::AppendIndices(const uint32_t* NewIndices, size_t Count) noexcept
{
    if (NewIndices && Count > 0)
    {
        Indices.insert(Indices.end(), NewIndices, NewIndices + Count);
    }
}

} // namespace Frontier
