//============================================================================================================================================
//                                                     DISTANCEFIELDSTRUCTURE.CPP
//============================================================================================================================================
// 📦 Projects resident scene triangles into a balanced world-space BVH for GPU distance and ray queries.

#include "DistanceFieldStructure.h"
#include "SceneStructure.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace Frontier
{
DistanceFieldStructure::DistanceFieldStructure() = default;
DistanceFieldStructure::~DistanceFieldStructure() = default;

bool DistanceFieldStructure::Construct(const SceneStructure& Scene)
{
    return Construct(Scene.QueryVertices(), Scene.QueryIndices(), Scene.QueryInstances(), Scene.QueryMaterials().QueryRecords());
}

bool DistanceFieldStructure::Construct(const std::vector<VertexRecord>& Positions,
                                      const std::vector<uint32_t>& Corners,
                                      const std::vector<InstanceRecord>& Placements,
                                      const std::vector<MaterialRecord>& Reflectance)
{
    Vertices = Positions; Indices = Corners; Instances = Placements; Materials = Reflectance;
    return ProjectInstances();
}

bool DistanceFieldStructure::RefreshInstances(const InstanceRecord* Rows, uint32_t Count)
{
    if (!Rows || Count != Instances.size()) return false;
    bool Changed = false;
    for (uint32_t Index = 0u; Index < Count; ++Index)
    {
        if (std::memcmp(Rows[Index].World, Instances[Index].World, sizeof(Rows[Index].World)) != 0 ||
            Rows[Index].MaterialIndex != Instances[Index].MaterialIndex)
            Changed = true;
    }
    if (!Changed) return true;
    Instances.assign(Rows, Rows + Count);
    return ProjectInstances();
}

bool DistanceFieldStructure::ProjectInstances()
{
    Facets.clear(); Branches.clear(); ++Revision;
    std::vector<DistanceFieldFacet> Projected;
    for (uint32_t InstanceIndex = 0u; InstanceIndex < Instances.size(); ++InstanceIndex)
    {
        const auto& Instance = Instances[InstanceIndex];
        if (Instance.MaterialIndex >= Materials.size()) return false;
        const auto& Material = Materials[Instance.MaterialIndex];
        for (uint32_t Primitive = 0u; Primitive < Instance.TriangleCount; ++Primitive)
        {
            DistanceFieldFacet Facet{};
            float* Corners[] = { Facet.Alpha, Facet.Beta, Facet.Gamma };
            for (uint32_t Corner = 0u; Corner < 3u; ++Corner)
            {
                const size_t Index = size_t(Instance.FirstIndex) + Primitive * 3u + Corner;
                if (Index >= Indices.size() || size_t(Indices[Index]) + Instance.VertexOffset >= Vertices.size()) return false;
                const auto& Position = Vertices[Indices[Index] + Instance.VertexOffset].SpatialLocation;
                for (uint32_t Axis = 0u; Axis < 3u; ++Axis)
                    Corners[Corner][Axis] = Instance.World[Axis] * Position.x + Instance.World[4u + Axis] * Position.y +
                                           Instance.World[8u + Axis] * Position.z + Instance.World[12u + Axis];
            }
            std::memcpy(&Facet.Alpha[3], &InstanceIndex, sizeof(uint32_t));
            std::memcpy(&Facet.Beta[3], &Primitive, sizeof(uint32_t));
            Facet.Albedo[0] = Material.AlbedoR * (1.0f - Material.Metalness);
            Facet.Albedo[1] = Material.AlbedoG * (1.0f - Material.Metalness);
            Facet.Albedo[2] = Material.AlbedoB * (1.0f - Material.Metalness);
            Facet.Emission[0] = Material.EmissiveR;
            Facet.Emission[1] = Material.EmissiveG;
            Facet.Emission[2] = Material.EmissiveB;
            Projected.push_back(Facet);
        }
    }
    return Construct(std::move(Projected));
}

bool DistanceFieldStructure::Construct(std::vector<DistanceFieldFacet> Input)
{
    Facets.clear();
    Branches.clear();
    ++Revision;
    for (auto Facet : Input)
    {
        bool Finite = true;
        for (uint32_t Axis = 0u; Axis < 3u; ++Axis)
            Finite &= std::isfinite(Facet.Alpha[Axis]) && std::isfinite(Facet.Beta[Axis]) && std::isfinite(Facet.Gamma[Axis]);
        for (uint32_t Axis = 0u; Axis < 3u; ++Axis)
            Finite &= std::isfinite(Facet.Albedo[Axis]) && std::isfinite(Facet.Emission[Axis]);
        if (!Finite) return false;
        const float Alpha[3] = { Facet.Beta[0]-Facet.Alpha[0], Facet.Beta[1]-Facet.Alpha[1], Facet.Beta[2]-Facet.Alpha[2] };
        const float Beta[3] = { Facet.Gamma[0]-Facet.Alpha[0], Facet.Gamma[1]-Facet.Alpha[1], Facet.Gamma[2]-Facet.Alpha[2] };
        Facet.Normal[0] = Alpha[1]*Beta[2]-Alpha[2]*Beta[1];
        Facet.Normal[1] = Alpha[2]*Beta[0]-Alpha[0]*Beta[2];
        Facet.Normal[2] = Alpha[0]*Beta[1]-Alpha[1]*Beta[0];
        const float Length = std::sqrt(Facet.Normal[0]*Facet.Normal[0]+Facet.Normal[1]*Facet.Normal[1]+Facet.Normal[2]*Facet.Normal[2]);
        if (!std::isfinite(Length)) return false;
        if (Length < 1.0e-10f) continue;
        for (uint32_t Axis = 0u; Axis < 3u; ++Axis) Facet.Normal[Axis] /= Length;
        Facets.push_back(Facet);
    }
    if (Facets.empty()) return false;
    Branches.reserve(Facets.size());
    Linearize(0u, static_cast<uint32_t>(Facets.size()));
    return true;
}

void DistanceFieldStructure::Linearize(uint32_t First, uint32_t Count)
{
    DistanceFieldBranch Branch{};
    for (uint32_t Axis = 0u; Axis < 3u; ++Axis)
    {
        Branch.Minimum[Axis] = std::numeric_limits<float>::max();
        Branch.Maximum[Axis] = -std::numeric_limits<float>::max();
        for (uint32_t Index = First; Index < First + Count; ++Index)
        {
            const auto& Facet = Facets[Index];
            Branch.Minimum[Axis] = std::min({Branch.Minimum[Axis], Facet.Alpha[Axis], Facet.Beta[Axis], Facet.Gamma[Axis]});
            Branch.Maximum[Axis] = std::max({Branch.Maximum[Axis], Facet.Alpha[Axis], Facet.Beta[Axis], Facet.Gamma[Axis]});
        }
    }
    const uint32_t Slot = static_cast<uint32_t>(Branches.size());
    Branches.push_back(Branch);
    if (Count <= 4u)
    {
        Branches[Slot].First = First;
        Branches[Slot].Count = Count;
    }
    else
    {
        uint32_t Axis = 0u;
        for (uint32_t Candidate = 1u; Candidate < 3u; ++Candidate)
            if (Branch.Maximum[Candidate]-Branch.Minimum[Candidate] > Branch.Maximum[Axis]-Branch.Minimum[Axis]) Axis = Candidate;
        const uint32_t Half = Count / 2u;
        std::nth_element(Facets.begin()+First, Facets.begin()+First+Half, Facets.begin()+First+Count,
            [Axis](const auto& Alpha, const auto& Beta)
            {
                return Alpha.Alpha[Axis]+Alpha.Beta[Axis]+Alpha.Gamma[Axis] < Beta.Alpha[Axis]+Beta.Beta[Axis]+Beta.Gamma[Axis];
            });
        Linearize(First, Half);
        Linearize(First+Half, Count-Half);
    }
    Branches[Slot].Escape = static_cast<uint32_t>(Branches.size());
}
}
