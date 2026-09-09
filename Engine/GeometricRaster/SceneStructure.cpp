//============================================================================================================================================
//                                                      SCENESTRUCTURE.CPP
//============================================================================================================================================
// 🧩 Resident scene assembly — instance splitting, Morton-ordered cluster building, luminaire alias table.

#include "SceneStructure.h"
#include "ClipProjection.h"
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

uint32_t SceneStructure::RegisterInstance(const GeometryStructure& Mesh, const Matrix4x4& World, uint32_t MaterialIndex, uint32_t Flags) noexcept
{
    const std::vector<VertexRecord>& MeshVertices = Mesh.QueryVertices();
    const std::vector<uint32_t>&     MeshIndices  = Mesh.QueryIndices();
    const uint32_t TriangleTotal = static_cast<uint32_t>(MeshIndices.size() / 3u);
    const uint32_t FirstInstance = static_cast<uint32_t>(Instances.size());
    if (TriangleTotal == 0u || MeshVertices.empty()) return FirstInstance;

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

    // ② Copy the vertex span once; every instance of this registration shares it via VertexOffset.
    const uint32_t VertexOffset = static_cast<uint32_t>(Vertices.size());
    Vertices.insert(Vertices.end(), MeshVertices.begin(), MeshVertices.end());

    const bool DoubleSided = (Flags & InstanceFlagDoubleSided) != 0u;

    // ③ Emit ≤ 16 384-triangle instances, each made of ≤ 128-triangle clusters.
    for (uint32_t InstanceStart = 0u; InstanceStart < TriangleTotal; InstanceStart += kInstanceTriangleCapacity)
    {
        const uint32_t InstanceTriangles = std::min(kInstanceTriangleCapacity, TriangleTotal - InstanceStart);
        const uint32_t InstanceIndex     = static_cast<uint32_t>(Instances.size());

        InstanceRecord Instance{};
        std::memcpy(Instance.World, &World.Columns[0][0], sizeof(Instance.World));
        std::memcpy(Instance.PreviousWorld, &World.Columns[0][0], sizeof(Instance.PreviousWorld));
        Instance.VertexOffset  = VertexOffset;
        Instance.FirstIndex    = static_cast<uint32_t>(Indices.size());
        Instance.TriangleCount = InstanceTriangles;
        Instance.MaterialIndex = MaterialIndex;
        Instance.ClusterOffset = static_cast<uint32_t>(Clusters.size());
        Instance.Flags         = Flags;

        std::vector<uint32_t> LocalIndices;
        LocalIndices.reserve(kClusterTriangleCapacity * 3u);

        for (uint32_t ClusterStart = 0u; ClusterStart < InstanceTriangles; ClusterStart += kClusterTriangleCapacity)
        {
            const uint32_t ClusterTriangles = std::min(kClusterTriangleCapacity, InstanceTriangles - ClusterStart);
            LocalIndices.clear();
            for (uint32_t T = 0u; T < ClusterTriangles; ++T)
            {
                const uint32_t Source = Order[InstanceStart + ClusterStart + T].second;
                LocalIndices.push_back(MeshIndices[Source * 3u + 0u]);
                LocalIndices.push_back(MeshIndices[Source * 3u + 1u]);
                LocalIndices.push_back(MeshIndices[Source * 3u + 2u]);
            }

            ClusterRecord Cluster = ConstructCluster(MeshVertices.data(), LocalIndices.data(), ClusterTriangles, DoubleSided);
            Cluster.InstanceIndex  = InstanceIndex;
            Cluster.FirstIndex     = static_cast<uint32_t>(Indices.size());
            Cluster.FirstPrimitive = ClusterStart;
            Clusters.push_back(Cluster);

            Indices.insert(Indices.end(), LocalIndices.begin(), LocalIndices.end());
        }

        Instance.ClusterCount = static_cast<uint32_t>(Clusters.size()) - Instance.ClusterOffset;
        Instances.push_back(Instance);
    }
    return FirstInstance;
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
    Materials.Clear(); Luminaires.clear(); FlatTriangles.clear();
    Placements.clear(); Cameras.clear(); PunctualLuminaires.clear();
    TotalLuminairePower = 0.0f;
}

} // namespace Frontier
