//============================================================================================================================================
//                                                             GEOMETRYSEQUENCE.H
//============================================================================================================================================
// 📦 Host-owned fixed-topology snapshot publication. No project pointers survive the synchronous C callback.

#pragma once
#include "../ProjectInterchange/CodeInterchange.h"
#include "../GeometricRaster/SceneStructure.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <array>
#include <map>

namespace Frontier::HostRuntime
{

class GeometrySequence
{
public:
    void Construct(SceneStructure& Scene, const CodeInterchange& Project)
    {
        Subjects.clear();
        if (!Project.HasGeometry()) return;
        for (const auto& Placement : Scene.QueryPlacements())
        {
            FrontierProjectGeometryReading Reading{sizeof(Reading), 1u, Placement.Name.c_str(), nullptr, nullptr, 0u, 0u, nullptr};
            if (Project.ProjectGeometry(Reading) != 1u) continue;
            for (uint32_t Offset = 0u; Offset < Placement.InstanceCount;)
            {
                const uint32_t Slot = Placement.FirstInstance + Offset;
                const uint32_t Topology = Scene.ForkTopology(Slot);
                const uint32_t Count = std::max(1u, Scene.QueryTopologyPartitions(Topology));
                for (uint32_t Partition = 0u; Partition < Count && Offset + Partition < Placement.InstanceCount; ++Partition)
                    Subjects.push_back({Placement.Name, Slot + Partition, 0u});
                Offset += Count;
            }
        }
    }

    void CaptureRest(const SceneStructure& Scene, const std::vector<InstanceRecord>& Rows)
    {
        if (Subjects.empty()) return;
        RestVertices = Scene.QueryVertices();
        RestIndices = Scene.QueryIndices();
        RestInstances = Rows;
        for (auto& Subject : Subjects) Subject.Revision = 0u;
    }

    bool Advance(SceneStructure& Scene, std::vector<InstanceRecord>& Rows, const CodeInterchange& Project,
                 bool& Changed, std::string& Refusal)
    {
        Changed = false;
        if (Subjects.empty() || RestVertices.empty()) return true;
        if (Scene.QueryVertices().size() != RestVertices.size() || Rows.size() != RestInstances.size() ||
            Scene.QueryIndices() != RestIndices)
        {
            Refusal = "project geometry requires unchanged topology during playback";
            return false;
        }
        // Work on a complete candidate. A refused/invalid callback cannot leave half of a wheel committed.
        auto Candidate = Scene.QueryVertices();
        auto CandidateRows = Rows;
        auto CandidateSubjects = Subjects;
        const auto& Indices = Scene.QueryIndices();
        for (auto& Subject : CandidateSubjects)
        {
            FrontierProjectGeometryReading Reading{sizeof(Reading), 1u, Subject.Name.c_str(), nullptr, nullptr, 0u, 0u, nullptr};
            const uint32_t Status = Project.ProjectGeometry(Reading);
            if (Status == 2u) { Refusal = "project refused geometry negotiation"; return false; }
            if (Status == 0u || Reading.Revision == 0u) continue;
            if (Reading.Revision == Subject.Revision) continue;
            const auto& Rest = RestInstances[Subject.Instance];
            const auto& Instance = CandidateRows[Subject.Instance];
            Reading.MaterialName = Scene.QueryMaterials().QueryDescriptors()[Instance.MaterialIndex].Name.c_str();
            std::set<uint32_t> Used;
            for (uint32_t Index = 0u; Index < Instance.TriangleCount * 3u; ++Index)
                Used.insert(Instance.VertexOffset + Indices[Instance.FirstIndex + Index]);
            std::vector<float> RestPoints, CurrentPoints;
            RestPoints.reserve(Used.size() * 3u);
            for (uint32_t Index : Used)
            {
                const Vector3 Local = RestVertices[Index].SpatialLocation;
                const float* World = Rest.World;
                RestPoints.insert(RestPoints.end(), {World[0] * Local.x + World[4] * Local.y + World[8] * Local.z + World[12],
                    World[1] * Local.x + World[5] * Local.y + World[9] * Local.z + World[13],
                    World[2] * Local.x + World[6] * Local.y + World[10] * Local.z + World[14]});
            }
            CurrentPoints.resize(RestPoints.size());
            Reading.RestPositions = RestPoints.data(); Reading.CurrentPositions = CurrentPoints.data();
            Reading.VertexCount = static_cast<uint32_t>(Used.size());
            const uint64_t ExpectedRevision = Reading.Revision;
            if (Project.ProjectGeometry(Reading) != 1u || Reading.Revision != ExpectedRevision)
            { Refusal = "project geometry snapshot changed or was refused during publication"; return false; }
            for (float Value : CurrentPoints)
                if (!std::isfinite(Value)) { Refusal = "project returned nonfinite geometry"; return false; }
            size_t Offset = 0u;
            for (uint32_t Index : Used)
            {
                Candidate[Index].SpatialLocation = {CurrentPoints[Offset], CurrentPoints[Offset + 1u], CurrentPoints[Offset + 2u]};
                Candidate[Index].NormalDirection = {};
                Offset += 3u;
            }
            auto NormalKey = [&](uint32_t Index)
            {
                const auto& RestVertex = RestVertices[Index];
                return std::array<float, 6>{RestVertex.SpatialLocation.x, RestVertex.SpatialLocation.y, RestVertex.SpatialLocation.z,
                                            RestVertex.NormalDirection.x, RestVertex.NormalDirection.y, RestVertex.NormalDirection.z};
            };
            std::map<std::array<float, 6>, Vector3> SmoothNormals;
            for (uint32_t Triangle = 0u; Triangle < Instance.TriangleCount; ++Triangle)
            {
                const uint32_t First = Instance.FirstIndex + Triangle * 3u;
                auto& A = Candidate[Instance.VertexOffset + Indices[First]];
                auto& B = Candidate[Instance.VertexOffset + Indices[First + 1u]];
                auto& C = Candidate[Instance.VertexOffset + Indices[First + 2u]];
                const Vector3 Normal = OrientationClassifier::CrossProduct(B.SpatialLocation - A.SpatialLocation,
                                                                           C.SpatialLocation - A.SpatialLocation);
                if (!std::isfinite(Normal.LengthSquared()) || Normal.LengthSquared() < 1.0e-20f)
                { Refusal = "project returned a degenerate geometry triangle"; return false; }
                // Preserve authored smoothing across duplicated glTF corners, but never weld a hard-normal seam.
                for (uint32_t Corner = 0u; Corner < 3u; ++Corner)
                    SmoothNormals[NormalKey(Instance.VertexOffset + Indices[First + Corner])] += Normal;
            }
            for (uint32_t Index : Used)
            {
                auto& Vertex = Candidate[Index];
                Vertex.NormalDirection = SmoothNormals[NormalKey(Index)].Normalized();
                const Vector3 Reference = std::abs(Vertex.NormalDirection.z) < 0.9f ? Vector3{0, 0, 1} : Vector3{0, 1, 0};
                const Vector3 Tangent = OrientationClassifier::CrossProduct(Reference, Vertex.NormalDirection).Normalized();
                Vertex.TangentDirection = {Tangent.x, Tangent.y, Tangent.z, 1.0f};
            }
            auto& Live = CandidateRows[Subject.Instance];
            std::fill_n(Live.World, 16u, 0.0f);
            Live.World[0] = Live.World[5] = Live.World[10] = Live.World[15] = 1.0f;
            std::copy_n(Live.World, 16u, Live.PreviousWorld); // deformation resets history; no fictitious rigid motion
            Subject.Revision = ExpectedRevision;
            Changed = true;
        }
        if (Changed)
        {
            Scene.AccessVertices() = std::move(Candidate);
            Rows = std::move(CandidateRows);
            Subjects = std::move(CandidateSubjects);
        }
        return true;
    }

    bool Restore(SceneStructure& Scene)
    {
        if (RestVertices.empty() || Subjects.empty() || Scene.QueryVertices().size() != RestVertices.size() ||
            Scene.QueryIndices() != RestIndices) return false;
        Scene.AccessVertices() = RestVertices;
        RestVertices.clear(); RestInstances.clear(); RestIndices.clear();
        for (auto& Subject : Subjects) Subject.Revision = 0u;
        return true;
    }
    [[nodiscard]] bool Active() const noexcept { return !Subjects.empty(); }

private:
    struct SubjectRecord { std::string Name; uint32_t Instance; uint64_t Revision; };
    std::vector<SubjectRecord> Subjects;
    std::vector<VertexRecord> RestVertices;
    std::vector<uint32_t> RestIndices;
    std::vector<InstanceRecord> RestInstances;
};

} // namespace Frontier::HostRuntime
