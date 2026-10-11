//============================================================================================================================================
//                                                      DISTANCEFIELDSTRUCTURE.H
//============================================================================================================================================
// 📦 World-space triangle BVH shared by distance-volume construction and exact secondary mesh rays.

#pragma once
#include "GeometryStructure.h"
#include "../ContentInterchange/MaterialIndex.h"
#include <array>

namespace Frontier
{
class SceneStructure;
struct InstanceRecord;
struct alignas(16) DistanceFieldFacet
{
    float Alpha[4], Beta[4], Gamma[4], Normal[4], Albedo[4], Emission[4];
};
struct alignas(16) DistanceFieldBranch
{
    float Minimum[4], Maximum[4];
    uint32_t Escape, First, Count, Reserved;
};
static_assert(sizeof(DistanceFieldFacet) == 96u);
static_assert(sizeof(DistanceFieldBranch) == 48u);

/// 📦 Owns a stackless, balanced triangle BVH; preserves exact instance/primitive identities through partitioning.
class DistanceFieldStructure
{
public:
    DistanceFieldStructure();
    ~DistanceFieldStructure();
    DistanceFieldStructure(const DistanceFieldStructure&) = delete;
    DistanceFieldStructure& operator=(const DistanceFieldStructure&) = delete;
    bool Construct(const SceneStructure& Scene);
    bool Construct(const std::vector<VertexRecord>& Positions,
                   const std::vector<uint32_t>& Corners,
                   const std::vector<InstanceRecord>& Placements,
                   const std::vector<MaterialRecord>& Reflectance);
    bool RefreshInstances(const InstanceRecord* Rows, uint32_t Count);
    bool Construct(std::vector<DistanceFieldFacet> Facets);
    const std::vector<DistanceFieldFacet>& QueryFacets() const { return Facets; }
    const std::vector<DistanceFieldBranch>& QueryBranches() const { return Branches; }
    uint64_t QueryRevision() const { return Revision; }
private:
    void Linearize(uint32_t First, uint32_t Count);
    bool ProjectInstances();
    std::vector<DistanceFieldFacet> Facets;
    std::vector<DistanceFieldBranch> Branches;
    std::vector<VertexRecord> Vertices;
    std::vector<uint32_t> Indices;
    std::vector<InstanceRecord> Instances;
    std::vector<MaterialRecord> Materials;
    uint64_t Revision = 0u;
};
}
