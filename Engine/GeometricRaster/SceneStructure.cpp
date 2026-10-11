//============================================================================================================================================
//                                                      SCENESTRUCTURE.CPP
//============================================================================================================================================
// 🧩 Resident scene assembly — instance splitting, Morton-ordered cluster building, luminaire alias table.

#include "SceneStructure.h"
#include "ClipProjection.h"
#include "PatchGeometry.h"
#include "VertexIdentity.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                     LOCAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace {

// 30-bit Morton code from a [0,1]³ position — used only to order triangles so that consecutive 128-triangle chunks are
//    spatially compact (tight spheres → the frustum / HiZ cull actually rejects things).
uint32_t ExpandBits(uint32_t V) noexcept
{
    V = (V * 0x00010001u) & 0xFF0000FFu;
    V = (V * 0x00000101u) & 0x0F00F00Fu;
    V = (V * 0x00000011u) & 0xC30C30C3u;
    V = (V * 0x00000005u) & 0x49249249u;
    return V;
}

uint32_t MortonCode(float X, float Y, float Z) noexcept
{
    const auto Q = [](float F) { return static_cast<uint32_t>(std::clamp(F * 1023.0f, 0.0f, 1023.0f)); };
    return (ExpandBits(Q(X)) << 2) | (ExpandBits(Q(Y)) << 1) | ExpandBits(Q(Z));
}

float Luminance(float R, float G, float B) noexcept { return 0.2126f * R + 0.7152f * G + 0.0722f * B; }

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    CLUSTER CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------
// Bounding sphere: AABB centre + farthest vertex. Normal cone (meshoptimizer convention): axis = normalised sum of
//    triangle normals; cutoff = sqrt(1 − minDot²) where minDot = min dot(axis, n_i). A cluster with any normal more than
//    ~84° from the axis (minDot ≤ 0.1) or a double-sided material gets cutoff 1.0 = never cone-culled.

ClusterRecord SceneStructure::ConstructCluster(const VertexRecord* MeshVertices, const uint32_t* MeshIndices, uint32_t TriangleCount, bool DoubleSided) noexcept
{
    Vector3 Minimum{  std::numeric_limits<float>::max(),  std::numeric_limits<float>::max(),  std::numeric_limits<float>::max() };
    Vector3 Maximum{ -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() };
    Vector3 AxisSum;

    for (uint32_t T = 0u; T < TriangleCount; ++T)
    {
        const Vector3& A = MeshVertices[MeshIndices[T * 3u + 0u]].SpatialLocation;
        const Vector3& B = MeshVertices[MeshIndices[T * 3u + 1u]].SpatialLocation;
        const Vector3& C = MeshVertices[MeshIndices[T * 3u + 2u]].SpatialLocation;
        for (const Vector3* P : { &A, &B, &C })
        {
            Minimum = Vector3{ std::min(Minimum.x, P->x), std::min(Minimum.y, P->y), std::min(Minimum.z, P->z) };
            Maximum = Vector3{ std::max(Maximum.x, P->x), std::max(Maximum.y, P->y), std::max(Maximum.z, P->z) };
        }
        const Vector3 N = OrientationClassifier::CrossProduct(B - A, C - A);
        if (N.LengthSquared() > 0.0f) AxisSum += N.Normalized();
    }

    const Vector3 Center = (Minimum + Maximum) * 0.5f;
    float RadiusSquared = 0.0f;
    for (uint32_t I = 0u; I < TriangleCount * 3u; ++I)
        RadiusSquared = std::max(RadiusSquared, (MeshVertices[MeshIndices[I]].SpatialLocation - Center).LengthSquared());

    Vector3 Axis{ 0.0f, 0.0f, 1.0f };
    float   Cutoff = 1.0f;
    if (!DoubleSided && AxisSum.LengthSquared() > 1e-12f)
    {
        Axis = AxisSum.Normalized();
        float MinimumDot = 1.0f;
        for (uint32_t T = 0u; T < TriangleCount; ++T)
        {
            const Vector3& A = MeshVertices[MeshIndices[T * 3u + 0u]].SpatialLocation;
            const Vector3& B = MeshVertices[MeshIndices[T * 3u + 1u]].SpatialLocation;
            const Vector3& C = MeshVertices[MeshIndices[T * 3u + 2u]].SpatialLocation;
            const Vector3 N = OrientationClassifier::CrossProduct(B - A, C - A);
            if (N.LengthSquared() > 0.0f) MinimumDot = std::min(MinimumDot, OrientationClassifier::DotProduct(Axis, N.Normalized()));
        }
        Cutoff = MinimumDot <= 0.1f ? 1.0f : std::sqrt(std::max(0.0f, 1.0f - MinimumDot * MinimumDot));
    }

    ClusterRecord Record{};
    Record.CenterX = Center.x; Record.CenterY = Center.y; Record.CenterZ = Center.z;
    Record.Radius  = std::sqrt(RadiusSquared);
    Record.AxisX = Axis.x; Record.AxisY = Axis.y; Record.AxisZ = Axis.z;
    Record.Cutoff  = Cutoff;
    Record.TriangleCount = TriangleCount;
    return Record;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    REGISTER INSTANCE
//------------------------------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------------------------------
//                                                   SHARED TOPOLOGY
//------------------------------------------------------------------------------------------------------------------------

uint64_t SceneStructure::TopologyKey(const GeometryStructure& Mesh) noexcept
{
    // FNV-1a over the index stream and the vertex positions. Positions alone would collide between two meshes
    //    that share a point cloud but not a winding, and indices alone would collide between two meshes that
    //    share a winding over different points; together they decide topology for sharing purposes. Normals,
    //    tangents and UVs are deliberately NOT in the key — they ride along in the shared vertex span, so two
    //    meshes that agree on positions and winding but disagree on UVs would wrongly share. Callers that build
    //    such a pair must call RegisterTopology separately; MixInto below folds the UVs in to prevent it.
    constexpr uint64_t kOffset = 1469598103934665603ull, kPrime = 1099511628211ull;
    uint64_t Hash = kOffset;
    auto MixInto = [&Hash](uint32_t Word) noexcept
    {
        for (uint32_t Byte = 0u; Byte < 4u; ++Byte) { Hash ^= (Word >> (Byte * 8u)) & 0xFFu; Hash *= kPrime; }
    };
    auto MixFloat = [&MixInto](float Value) noexcept
    {
        uint32_t Bits = 0u; std::memcpy(&Bits, &Value, sizeof(Bits));
        if (Bits == 0x80000000u) Bits = 0u;                     // −0 and +0 are the same point
        MixInto(Bits);
    };
    const std::vector<VertexRecord>& MeshVertices = Mesh.QueryVertices();
    const std::vector<uint32_t>&     MeshIndices  = Mesh.QueryIndices();
    MixInto(static_cast<uint32_t>(MeshVertices.size()));
    MixInto(static_cast<uint32_t>(MeshIndices.size()));
    for (uint32_t Index : MeshIndices) MixInto(Index);
    for (const VertexRecord& V : MeshVertices)
    {
        MixFloat(V.SpatialLocation.x); MixFloat(V.SpatialLocation.y); MixFloat(V.SpatialLocation.z);
        MixFloat(V.TextureCoordinateU); MixFloat(V.TextureCoordinateV);
    }
    return Hash;
}

uint32_t SceneStructure::RegisterTopology(const GeometryStructure& Mesh) noexcept
{
    const std::vector<VertexRecord>& MeshVertices = Mesh.QueryVertices();
    const std::vector<uint32_t>&     MeshIndices  = Mesh.QueryIndices();
    const uint32_t TriangleTotal = static_cast<uint32_t>(MeshIndices.size() / 3u);

    // An empty mesh still earns a record, so a caller holding the handle can place it harmlessly.
    const uint64_t Key = TriangleTotal == 0u || MeshVertices.empty() ? 0u : TopologyKey(Mesh);
    if (Key != 0u)
        for (uint32_t Existing = 0u; Existing < Topologies.size(); ++Existing)
            if (Topologies[Existing].ContentKey == Key) return Existing;   // already resident — share it

    const uint32_t Handle = static_cast<uint32_t>(Topologies.size());
    Topologies.emplace_back();
    TopologyRecord& Topology = Topologies.back();
    Topology.ContentKey    = Key;
    Topology.VertexOffset  = static_cast<uint32_t>(Vertices.size());
    Topology.VertexCount   = static_cast<uint32_t>(MeshVertices.size());
    Topology.TriangleCount = TriangleTotal;
    if (TriangleTotal == 0u || MeshVertices.empty()) return Handle;

    const auto Canonical = CanonicalVertexIndices(MeshVertices);

    // ① Morton order the triangles by centroid inside the mesh's own bounds.
    Vector3 Minimum{  std::numeric_limits<float>::max(),  std::numeric_limits<float>::max(),  std::numeric_limits<float>::max() };
    Vector3 Maximum{ -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() };
    for (const VertexRecord& V : MeshVertices)
    {
        Minimum = Vector3{ std::min(Minimum.x, V.SpatialLocation.x), std::min(Minimum.y, V.SpatialLocation.y), std::min(Minimum.z, V.SpatialLocation.z) };
        Maximum = Vector3{ std::max(Maximum.x, V.SpatialLocation.x), std::max(Maximum.y, V.SpatialLocation.y), std::max(Maximum.z, V.SpatialLocation.z) };
    }
    const Vector3 Span = Maximum - Minimum;
    const Vector3 InverseSpan{ Span.x > 0.0f ? 1.0f / Span.x : 0.0f, Span.y > 0.0f ? 1.0f / Span.y : 0.0f, Span.z > 0.0f ? 1.0f / Span.z : 0.0f };

    std::vector<std::pair<uint32_t, uint32_t>> Order(TriangleTotal);   // (morton, triangle)
    for (uint32_t T = 0u; T < TriangleTotal; ++T)
    {
        const Vector3 Centroid = (MeshVertices[MeshIndices[T * 3u]].SpatialLocation + MeshVertices[MeshIndices[T * 3u + 1u]].SpatialLocation
                                + MeshVertices[MeshIndices[T * 3u + 2u]].SpatialLocation) / 3.0f;
        const Vector3 U = (Centroid - Minimum) * InverseSpan;
        Order[T] = { MortonCode(U.x, U.y, U.z), T };
    }
    std::stable_sort(Order.begin(), Order.end(), [](const auto& A, const auto& B) { return A.first < B.first; });

    // ② The vertex span, copied ONCE. Every placement of this topology carries this same VertexOffset.
    Vertices.insert(Vertices.end(), MeshVertices.begin(), MeshVertices.end());

    // Reserve half of the primitive-token range for alternate indices.
    constexpr uint32_t PatchInstanceCapacity = kInstanceTriangleCapacity / 2u;

    // ③ Partition into ≤ 8 192-fine-triangle spans, each made of ≤ 128-triangle clusters. Indices and the coarse
    //    LOD bake land in the shared arrays; the cluster rows land in ClusterTemplates with no owning instance.
    for (uint32_t PartitionStart = 0u; PartitionStart < TriangleTotal; PartitionStart += PatchInstanceCapacity)
    {
        const uint32_t PartitionTriangles = std::min(PatchInstanceCapacity, TriangleTotal - PartitionStart);
        TopologyRecord::Partition Partition{};
        Partition.FirstIndex    = static_cast<uint32_t>(Indices.size());
        Partition.TriangleCount = PartitionTriangles;
        Partition.FirstCluster  = static_cast<uint32_t>(ClusterTemplates.size());

        std::vector<uint32_t> LocalIndices;
        LocalIndices.reserve(kClusterTriangleCapacity * 3u);

        for (uint32_t ClusterStart = 0u; ClusterStart < PartitionTriangles; ClusterStart += kClusterTriangleCapacity)
        {
            const uint32_t ClusterTriangles = std::min(kClusterTriangleCapacity, PartitionTriangles - ClusterStart);
            LocalIndices.clear();
            for (uint32_t T = 0u; T < ClusterTriangles; ++T)
            {
                const uint32_t Source = Order[PartitionStart + ClusterStart + T].second;
                LocalIndices.push_back(Canonical[MeshIndices[Source * 3u + 0u]]);
                LocalIndices.push_back(Canonical[MeshIndices[Source * 3u + 1u]]);
                LocalIndices.push_back(Canonical[MeshIndices[Source * 3u + 2u]]);
            }

            ClusterRecord Cluster = ConstructCluster(MeshVertices.data(), LocalIndices.data(), ClusterTriangles, false); // keep cone for LOD; culling still checks instance flags
            Cluster.InstanceIndex  = 0u;   // stamped by PlaceTopology — a template owns no instance
            Cluster.FirstIndex     = static_cast<uint32_t>(Indices.size());
            Cluster.FirstPrimitive = ClusterStart;
            ClusterTemplates.push_back(Cluster);

            Indices.insert(Indices.end(), LocalIndices.begin(), LocalIndices.end());
        }

        Partition.ClusterCount = static_cast<uint32_t>(ClusterTemplates.size()) - Partition.FirstCluster;
        // Append only AFTER all original indices: BVH, luminaires and shadow draws keep the original ranges.
        for (uint32_t C = Partition.FirstCluster; C < ClusterTemplates.size(); ++C)
        {
            auto& Patch = ClusterTemplates[C];
            std::vector<uint32_t> Fine(Indices.begin()+Patch.FirstIndex, Indices.begin()+Patch.FirstIndex+Patch.TriangleCount*3u);
            auto Coarse = PatchGeometry::LoadOrBake(MeshVertices, Fine);
            if (Coarse.Indices.size() >= Fine.size()) continue;
            Patch.CoarseFirstIndex = static_cast<uint32_t>(Indices.size());
            Patch.CoarseFirstPrimitive = (Patch.CoarseFirstIndex-Partition.FirstIndex)/3u;
            Patch.CoarseTriangleCount = static_cast<uint32_t>(Coarse.Indices.size()/3u);
            Patch.CoarseError = Coarse.Error;
            Indices.insert(Indices.end(),Coarse.Indices.begin(),Coarse.Indices.end());
        }
        Topologies[Handle].Partitions.push_back(Partition);
    }
    return Handle;
}

uint32_t SceneStructure::PlaceTopology(uint32_t Topology, const Matrix4x4& World, uint32_t MaterialIndex, uint32_t Flags) noexcept
{
    const uint32_t FirstInstance = static_cast<uint32_t>(Instances.size());
    if (Topology >= Topologies.size()) return FirstInstance;
    TopologyRecord& Shared = Topologies[Topology];
    if (Shared.Partitions.empty()) return FirstInstance;

    for (const TopologyRecord::Partition& Partition : Shared.Partitions)
    {
        const uint32_t InstanceIndex = static_cast<uint32_t>(Instances.size());

        InstanceRecord Instance{};
        std::memcpy(Instance.World, &World.Columns[0][0], sizeof(Instance.World));
        std::memcpy(Instance.PreviousWorld, &World.Columns[0][0], sizeof(Instance.PreviousWorld));
        Instance.VertexOffset  = Shared.VertexOffset;      // ← shared
        Instance.FirstIndex    = Partition.FirstIndex;     // ← shared
        Instance.TriangleCount = Partition.TriangleCount;
        Instance.MaterialIndex = MaterialIndex;            // ← the thing that actually differs
        Instance.ClusterOffset = static_cast<uint32_t>(Clusters.size());
        Instance.ClusterCount  = Partition.ClusterCount;
        Instance.Flags         = Flags;

        for (uint32_t C = 0u; C < Partition.ClusterCount; ++C)
        {
            ClusterRecord Cluster = ClusterTemplates[Partition.FirstCluster + C];
            Cluster.InstanceIndex = InstanceIndex;
            Clusters.push_back(Cluster);
        }
        Instances.push_back(Instance);
        InstanceTopology.push_back(Topology);
    }
    ++Shared.Placements;
    return FirstInstance;
}

uint32_t SceneStructure::ForkTopology(uint32_t FirstInstance) noexcept
{
    if (FirstInstance >= Instances.size()) return 0u;
    const uint32_t Source = FirstInstance < InstanceTopology.size() ? InstanceTopology[FirstInstance] : 0u;
    if (Source >= Topologies.size()) return Source;

    // A private copy of the vertex span, the index span and the cluster templates. The copy is byte-identical,
    //    so nothing renders differently the instant it is made — it only becomes different when the caller cuts
    //    it. ContentKey is cleared so the copy never gets shared back out by RegisterTopology.
    const TopologyRecord Origin = Topologies[Source];          // by value: Topologies may reallocate below
    const uint32_t Handle = static_cast<uint32_t>(Topologies.size());
    Topologies.emplace_back();
    TopologyRecord& Fork = Topologies.back();
    Fork.ContentKey    = 0u;
    Fork.TriangleCount = Origin.TriangleCount;
    Fork.VertexCount   = Origin.VertexCount;
    Fork.VertexOffset  = static_cast<uint32_t>(Vertices.size());
    const std::vector<VertexRecord> CopiedVertices(Vertices.begin() + Origin.VertexOffset,
                                                   Vertices.begin() + Origin.VertexOffset + Origin.VertexCount);
    Vertices.insert(Vertices.end(), CopiedVertices.begin(), CopiedVertices.end());

    for (const TopologyRecord::Partition& Origin2 : Origin.Partitions)
    {
        // The partition's index range runs from its first fine index to the end of its coarse appendix. The
        //    coarse rows are appended after the fine ones, so the span's end is the furthest cluster end.
        uint32_t SpanEnd = Origin2.FirstIndex + Origin2.TriangleCount * 3u;
        for (uint32_t C = 0u; C < Origin2.ClusterCount; ++C)
        {
            const ClusterRecord& T = ClusterTemplates[Origin2.FirstCluster + C];
            SpanEnd = std::max(SpanEnd, T.FirstIndex + T.TriangleCount * 3u);
            if (T.CoarseTriangleCount > 0u) SpanEnd = std::max(SpanEnd, T.CoarseFirstIndex + T.CoarseTriangleCount * 3u);
        }
        TopologyRecord::Partition Copy{};
        Copy.FirstIndex    = static_cast<uint32_t>(Indices.size());
        Copy.TriangleCount = Origin2.TriangleCount;
        Copy.FirstCluster  = static_cast<uint32_t>(ClusterTemplates.size());
        Copy.ClusterCount  = Origin2.ClusterCount;
        const int32_t IndexShift  = static_cast<int32_t>(Copy.FirstIndex) - static_cast<int32_t>(Origin2.FirstIndex);
        const std::vector<uint32_t> CopiedIndices(Indices.begin() + Origin2.FirstIndex, Indices.begin() + SpanEnd);
        Indices.insert(Indices.end(), CopiedIndices.begin(), CopiedIndices.end());
        for (uint32_t C = 0u; C < Origin2.ClusterCount; ++C)
        {
            ClusterRecord T = ClusterTemplates[Origin2.FirstCluster + C];
            T.FirstIndex = static_cast<uint32_t>(static_cast<int32_t>(T.FirstIndex) + IndexShift);
            if (T.CoarseTriangleCount > 0u)
                T.CoarseFirstIndex = static_cast<uint32_t>(static_cast<int32_t>(T.CoarseFirstIndex) + IndexShift);
            ClusterTemplates.push_back(T);
        }
        Fork.Partitions.push_back(Copy);
    }

    // Repoint the placement. Its InstanceRecords are contiguous and one per partition, in order.
    for (uint32_t P = 0u; P < Fork.Partitions.size() && FirstInstance + P < Instances.size(); ++P)
    {
        InstanceRecord& Instance = Instances[FirstInstance + P];
        InstanceTopology[FirstInstance + P] = Handle;
        Instance.VertexOffset = Fork.VertexOffset;
        Instance.FirstIndex   = Fork.Partitions[P].FirstIndex;
        for (uint32_t C = 0u; C < Instance.ClusterCount; ++C)
        {
            ClusterRecord& Live = Clusters[Instance.ClusterOffset + C];
            const ClusterRecord& T = ClusterTemplates[Fork.Partitions[P].FirstCluster + C];
            const uint32_t Owner = Live.InstanceIndex;
            Live = T;
            Live.InstanceIndex = Owner;
        }
    }
    Fork.Placements = 1u;
    if (Topologies[Source].Placements > 0u) --Topologies[Source].Placements;
    return Handle;
}

uint32_t SceneStructure::RegisterInstance(const GeometryStructure& Mesh, const Matrix4x4& World, uint32_t MaterialIndex, uint32_t Flags) noexcept
{
    return PlaceTopology(RegisterTopology(Mesh), World, MaterialIndex, Flags);
}

uint32_t SceneStructure::RegisterMaterial(const MaterialDescriptor& Material) noexcept
{
    return Materials.Register(Material);
}

uint32_t SceneStructure::RegisterPlacement(std::string PlacementName, uint32_t Ancestor, const Matrix4x4& Local, const Matrix4x4& World) noexcept
{
    PlacementRecord P;
    P.Name = std::move(PlacementName);
    P.Ancestor = Ancestor < Placements.size() ? Ancestor : kPlacementNone;
    for (int C = 0; C < 4; ++C) for (int R = 0; R < 4; ++R) { P.LocalTransform[C * 4 + R] = Local.Columns[C][R]; P.WorldTransform[C * 4 + R] = World.Columns[C][R]; }
    const uint32_t Index    = static_cast<uint32_t>(Placements.size());
    const uint32_t Ancestor2 = P.Ancestor;
    Placements.push_back(std::move(P));
    if (Ancestor2 != kPlacementNone)
    {
        uint32_t* Link = &Placements[Ancestor2].FirstDescendant;
        while (*Link != kPlacementNone) Link = &Placements[*Link].NextPeer;
        *Link = Index;
    }
    return Index;
}

uint32_t SceneStructure::RegisterCamera(const CameraRecord& Camera, uint32_t Placement) noexcept
{
    Cameras.push_back(Camera);
    const uint32_t Index = static_cast<uint32_t>(Cameras.size() - 1u);
    if (Placement < Placements.size()) Placements[Placement].Camera = Index;
    return Index;
}

uint32_t SceneStructure::RegisterPunctualLuminaire(const PunctualLuminaireRecord& Luminaire, uint32_t Placement) noexcept
{
    PunctualLuminaires.push_back(Luminaire);
    const uint32_t Index = static_cast<uint32_t>(PunctualLuminaires.size() - 1u);
    if (Placement < Placements.size()) Placements[Placement].Luminaire = Index;
    return Index;
}

void SceneStructure::AttachInstances(uint32_t Placement, uint32_t FirstInstance, uint32_t InstanceCount) noexcept
{
    if (Placement >= Placements.size()) return;
    PlacementRecord& P = Placements[Placement];
    if (P.InstanceCount == 0u) { P.FirstInstance = FirstInstance; P.InstanceCount = InstanceCount; }
    else P.InstanceCount = FirstInstance + InstanceCount - P.FirstInstance;   // contiguous by construction (one codec pass)
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        FINALISE
//------------------------------------------------------------------------------------------------------------------------

void SceneStructure::RefreshGeometry(const std::vector<InstanceRecord>& Rows) noexcept
{
    if (Rows.size() != Instances.size()) return;
    Instances = Rows;
    for (ClusterRecord& Cluster : Clusters)
    {
        const InstanceRecord& Instance = Instances[Cluster.InstanceIndex];
        const ClusterRecord Bounds = ConstructCluster(Vertices.data() + Instance.VertexOffset,
                                                      Indices.data() + Cluster.FirstIndex, Cluster.TriangleCount, true);
        Cluster.CenterX = Bounds.CenterX; Cluster.CenterY = Bounds.CenterY; Cluster.CenterZ = Bounds.CenterZ;
        Cluster.Radius = Bounds.Radius;
        Cluster.Cutoff = 1.0f;
        // Authored simplification error and normal cones are not valid under arbitrary deformation.
        Cluster.CoarseTriangleCount = 0u;
        Cluster.CoarseError = 0.0f;
    }
    Finalise(std::max(1u, Materials.QueryMetrics().SlabLimit));
}

void SceneStructure::Finalise(uint32_t SlabLimit, std::vector<std::string>* Report) noexcept
{
    if (Materials.QueryCount() == 0u) Materials.Register(MaterialDescriptor{});   // never leave the kernel without a slot
    Materials.Finalise(SlabLimit, Report);
    const std::vector<MaterialRecord>& Records = Materials.QueryRecords();

    FlatTriangles.clear();
    Luminaires.clear();
    TotalLuminairePower = 0.0f;
    BoundsMinimum = Vector3{  std::numeric_limits<float>::max(),  std::numeric_limits<float>::max(),  std::numeric_limits<float>::max() };
    BoundsMaximum = Vector3{ -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() };

    FlatTriangles.reserve(Indices.size() / 3u);
    std::vector<float> Power;

    for (uint32_t InstanceIndex = 0u; InstanceIndex < Instances.size(); ++InstanceIndex)
    {
        InstanceRecord& Instance = Instances[InstanceIndex];
        Instance.FlatTriangleOffset = static_cast<uint32_t>(FlatTriangles.size());
        const Matrix4x4 World = ProjectionFromColumns(Instance.World);
        const MaterialRecord& Material = Records[std::min<size_t>(Instance.MaterialIndex, Records.size() - 1u)];
        const float Radiance = Luminance(Material.EmissiveR, Material.EmissiveG, Material.EmissiveB);
        if (Radiance > 0.0f) Instance.Flags |= InstanceFlagEmissive;

        for (uint32_t T = 0u; T < Instance.TriangleCount; ++T)
        {
            const uint32_t I0 = Indices[Instance.FirstIndex + T * 3u + 0u] + Instance.VertexOffset;
            const uint32_t I1 = Indices[Instance.FirstIndex + T * 3u + 1u] + Instance.VertexOffset;
            const uint32_t I2 = Indices[Instance.FirstIndex + T * 3u + 2u] + Instance.VertexOffset;
            const Vector3 A = TransformPoint(World, Vertices[I0].SpatialLocation);
            const Vector3 B = TransformPoint(World, Vertices[I1].SpatialLocation);
            const Vector3 C = TransformPoint(World, Vertices[I2].SpatialLocation);
            for (const Vector3* P : { &A, &B, &C })
            {
                BoundsMinimum = Vector3{ std::min(BoundsMinimum.x, P->x), std::min(BoundsMinimum.y, P->y), std::min(BoundsMinimum.z, P->z) };
                BoundsMaximum = Vector3{ std::max(BoundsMaximum.x, P->x), std::max(BoundsMaximum.y, P->y), std::max(BoundsMaximum.z, P->z) };
            }
            const Vector3 Cross  = OrientationClassifier::CrossProduct(B - A, C - A);
            const float   Area   = 0.5f * Cross.Length();
            const Vector3 Normal = Area > 0.0f ? Cross / (2.0f * Area) : Vector3{ 0.0f, 0.0f, 1.0f };

            TriangleIndex Flat{};
            Flat.VertexAlphaX = A.x; Flat.VertexAlphaY = A.y; Flat.VertexAlphaZ = A.z;
            Flat.VertexBetaX  = B.x; Flat.VertexBetaY  = B.y; Flat.VertexBetaZ  = B.z;
            Flat.VertexGammaX = C.x; Flat.VertexGammaY = C.y; Flat.VertexGammaZ = C.z;
            // R4a: the normal payload becomes the three vertex UVs (the kernel derives the face normal from the edges).
            Flat.TextureAlphaU = Vertices[I0].TextureCoordinateU; Flat.TextureAlphaV = Vertices[I0].TextureCoordinateV;
            Flat.TextureBetaU  = Vertices[I1].TextureCoordinateU; Flat.TextureBetaV  = Vertices[I1].TextureCoordinateV;
            Flat.TextureGammaU = Vertices[I2].TextureCoordinateU; Flat.TextureGammaV = Vertices[I2].TextureCoordinateV;
            (void)Normal;
            const uint32_t MaterialSlot = Instance.MaterialIndex;
            const uint32_t TriangleSlot = static_cast<uint32_t>(FlatTriangles.size());
            std::memcpy(&Flat.MaterialSlot, &MaterialSlot, sizeof(uint32_t));
            FlatTriangles.push_back(Flat);

            if (Radiance > 0.0f && Area > 0.0f)
            {
                LuminaireRecord L{};
                L.TriangleSlot   = TriangleSlot;
                L.InstanceIndex  = InstanceIndex;
                L.PrimitiveIndex = T;
                L.Area           = Area;
                Luminaires.push_back(L);
                Power.push_back(Area * Radiance);
                TotalLuminairePower += Area * Radiance;
            }
        }
    }

    // Walker alias table (Vose's O(N) construction).
    const uint32_t N = static_cast<uint32_t>(Luminaires.size());
    if (N == 0u || TotalLuminairePower <= 0.0f) return;

    std::vector<float>    Scaled(N);
    std::vector<uint32_t> Small, Large;
    for (uint32_t I = 0u; I < N; ++I)
    {
        Luminaires[I].Probability = Power[I] / TotalLuminairePower;
        Scaled[I] = Luminaires[I].Probability * static_cast<float>(N);
        (Scaled[I] < 1.0f ? Small : Large).push_back(I);
        Luminaires[I].AliasSlot = I;
        Luminaires[I].Threshold = 1.0f;
    }
    while (!Small.empty() && !Large.empty())
    {
        const uint32_t S = Small.back(); Small.pop_back();
        const uint32_t G = Large.back(); Large.pop_back();
        Luminaires[S].Threshold = Scaled[S];
        Luminaires[S].AliasSlot = G;
        Scaled[G] = (Scaled[G] + Scaled[S]) - 1.0f;
        (Scaled[G] < 1.0f ? Small : Large).push_back(G);
    }
    for (uint32_t I : Large) { Luminaires[I].Threshold = 1.0f; Luminaires[I].AliasSlot = I; }
    for (uint32_t I : Small) { Luminaires[I].Threshold = 1.0f; Luminaires[I].AliasSlot = I; }
}

void SceneStructure::Clear() noexcept
{
    Vertices.clear(); Indices.clear(); Instances.clear(); Clusters.clear();
    Topologies.clear(); ClusterTemplates.clear(); InstanceTopology.clear();
    Materials.Clear(); Luminaires.clear(); FlatTriangles.clear();
    Placements.clear(); Cameras.clear(); PunctualLuminaires.clear();
    TotalLuminairePower = 0.0f;
}

} // namespace Frontier
