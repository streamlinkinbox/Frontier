//============================================================================================================================================
//                                                       SCENESTRUCTURE.H
//============================================================================================================================================
// 🧩 Resident scene: every record the GPU keeps for a whole level — vertices, indices, instances, cull clusters,
//    materials and the luminaire alias table — in the exact std430 layouts the R2 shaders read.
//
// Layout contract (mirrored in Shaders/SceneRecords.slang — change both or neither):
//    VertexRecord     64 B  GeometryStructure.h   (pos.xyz pad | normal.xyz pad | tangent.xyzw | u v pad pad)
//    InstanceRecord  160 B  World, PreviousWorld (column-major), mesh range, material, cluster range
//    ClusterRecord    64 B  object-space bounding sphere + normal cone + triangle range   (cull unit, ≤ 128 triangles)
//    LuminaireRecord  32 B  emissive triangle + Walker alias entry for O(1) light selection
//    MaterialRecord   64 B  ContentInterchange/MaterialIndex.h (header) + MaterialSlabRecord 288 B per slab (R4a)
//    TriangleIndex    64 B  SwapchainExchange.h — flattened world-space triangles addressed by the CWBVH primitive index
//                           (R4a: carries per-vertex UVs for texture lookup; 🚧 R5 deletes it).
//    PlacementRecord / CameraRecord / PunctualLuminaireRecord — CPU-only scene-graph rows (R4a, data only, no UI).
//
// Visibility identifier (GeometricRaster/VisibilityProjection.h): 18-bit instance token << 14 | 14-bit primitive token.
//    A mesh with more than 8 192 fine triangles is split into several InstanceRecords sharing one transform, so the
//    primitive token never overflows. 0xFFFFFFFF = nothing rasterised.

#pragma once

#if defined(_MSC_VER)
    #pragma warning(disable: 4324)
#endif

#include "GeometryStructure.h"
#include "../DeviceExchange/SwapchainExchange.h"
#include "../ContentInterchange/MaterialIndex.h"
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSTANCE RECORD
//------------------------------------------------------------------------------------------------------------------------

enum InstanceFlag : uint32_t
{
    InstanceFlagDoubleSided = 1u << 0,    // material.doubleSided → normal-cone cull disabled for its clusters
    InstanceFlagEmissive    = 1u << 1,    // at least one luminaire triangle
};

struct InstanceRecord
{
    float    World[16];                 // [-]   object → world, column-major (Columns[c][r] flattened c*4+r)
    float    PreviousWorld[16];         // [-]   last frame's object → world (motion vectors)
    uint32_t VertexOffset;              // [idx] added to every index of this instance (vertexOffset of the draw)
    uint32_t FirstIndex;                // [idx] first index in the shared index buffer
    uint32_t TriangleCount;             // [cnt] triangles in this instance (≤ 8 192)
    uint32_t MaterialIndex;             // [idx] RadianceStructure slot
    uint32_t ClusterOffset;             // [idx] first ClusterRecord of this instance (clusters are contiguous per instance)
    uint32_t ClusterCount;              // [cnt]
    uint32_t Flags;                     // [bit] InstanceFlag
    uint32_t FlatTriangleOffset;        // [idx] first TriangleIndex of this instance in the flattened buffer (🚧 R3 removes)
};
static_assert(sizeof(InstanceRecord) == 160u, "InstanceRecord must be 160 bytes (std430 mirror)");

//------------------------------------------------------------------------------------------------------------------------
//                                                     CLUSTER RECORD
//------------------------------------------------------------------------------------------------------------------------

struct ClusterRecord
{
    float    CenterX, CenterY, CenterZ; // [m]   object-space bounding sphere centre
    float    Radius;                    // [m]   object-space bounding sphere radius
    float    AxisX, AxisY, AxisZ;       // [-]   object-space normal-cone axis (unit)
    float    Cutoff;                    // [-]   meshoptimizer-style cone cutoff; 1.0 = never backface-culled
    uint32_t InstanceIndex;             // [idx] owning InstanceRecord
    uint32_t FirstIndex;                // [idx] absolute first index in the shared index buffer
    uint32_t TriangleCount;             // [cnt] ≤ kClusterTriangleCapacity
    uint32_t FirstPrimitive;            // [idx] primitive token of the first triangle (within the instance)
    uint32_t CoarseFirstIndex = 0, CoarseTriangleCount = 0, CoarseFirstPrimitive = 0;
    float CoarseError = 0;              // conservative accumulated object-space endpoint displacement
};
static_assert(sizeof(ClusterRecord) == 64u, "ClusterRecord must be 64 bytes (std430 mirror)");

inline constexpr size_t kSpaceClusterPrefixBytes = 48u; // frozen CLST v1 ABI
static_assert(offsetof(ClusterRecord,CoarseFirstIndex)==kSpaceClusterPrefixBytes);

static constexpr uint32_t kClusterTriangleCapacity = 128u;
static constexpr uint32_t kInstanceTriangleCapacity = 1u << 14;   // 14-bit primitive token

//------------------------------------------------------------------------------------------------------------------------
//                                                    LUMINAIRE RECORD
//------------------------------------------------------------------------------------------------------------------------
// Walker alias table over emissive triangles, weighted by area × luminance(Le). Pick: i = floor(u1·N);
//    take i when u2 < Threshold[i], else Alias[i]. Selection pdf = Power[i] / TotalPower (stored in Probability).

struct LuminaireRecord
{
    uint32_t TriangleSlot;              // [idx] flattened TriangleIndex slot (🚧 R3: instance+primitive instead)
    uint32_t InstanceIndex;             // [idx]
    uint32_t PrimitiveIndex;            // [idx] within the instance
    uint32_t AliasSlot;                 // [idx] alias target
    float    Threshold;                 // [0..1] accept probability for this slot
    float    Area;                      // [m²]  world-space triangle area
    float    Probability;               // [-]   discrete selection pdf of THIS triangle
    float    Pad;
};
static_assert(sizeof(LuminaireRecord) == 32u, "LuminaireRecord must be 32 bytes (std430 mirror)");

//------------------------------------------------------------------------------------------------------------------------
//                                          PLACEMENT / CAMERA / PUNCTUAL LUMINAIRE RECORDS  (CPU only)
//------------------------------------------------------------------------------------------------------------------------
// The scene graph as flat rows with stable indices: each placement links to its ancestor, first descendant and next
//    peer (first-descendant / next-peer lists, so any fan-out costs two indices). Instances, cameras and punctual
//    luminaires hang off a placement by index. The outliner reads these directly later; nothing here is uploaded.

static constexpr uint32_t kPlacementNone = 0xFFFFFFFFu;

struct PlacementRecord
{
    std::string Name;
    uint32_t    Ancestor        = kPlacementNone;   // [idx] enclosing placement
    uint32_t    FirstDescendant = kPlacementNone;   // [idx]
    uint32_t    NextPeer        = kPlacementNone;   // [idx]
    float       LocalTransform[16];                 // [-]   column-major, relative to the ancestor (engine axes)
    float       WorldTransform[16];                 // [-]   column-major, absolute
    uint32_t    FirstInstance   = kPlacementNone;   // [idx] first InstanceRecord placed here (contiguous)
    uint32_t    InstanceCount   = 0u;               // [cnt]
    uint32_t    Camera          = kPlacementNone;   // [idx] CameraRecord
    uint32_t    Luminaire       = kPlacementNone;   // [idx] PunctualLuminaireRecord
    bool        Dynamic         = false;            // [-] the object moves (--animate / physics drive it)
    // Which base mesh built this placement, as an index into ConstructKind; kNoBaseMesh for anything that
    //    was not constructed from one (an import, a camera, a luminaire). The shipped editor keeps the same
    //    fact on the outliner row's icon and reads the fracture primitive back off it. Authoring state only:
    //    glTF carries no such notion, so SceneCodec neither writes nor reads it.
    uint8_t     BaseMesh        = 0xFFu;
};

constexpr uint8_t kNoBaseMesh = 0xFFu;

struct CameraRecord
{
    std::string Name;
    float       VerticalFieldOfView = 0.8f;         // [rad]
    float       AspectRatio         = 0.0f;         // [-]   0 = viewport
    float       NearPlane           = 0.05f;        // [m]
    float       FarPlane            = 0.0f;         // [m]   0 = infinite (reverse-Z)
    bool        Orthographic        = false;
    float       OrthographicHalfHeight = 1.0f;      // [m]
};

enum class PunctualLuminaireCategory : uint32_t { Directional = 0, Point = 1, Spot = 2, Rectangle = 3, Tube = 4, Strip = 5 };
enum class LuminaireDistribution : uint32_t { Uniform = 0, IesProfile = 1, AutomotiveLowBeam = 2 };

// Persistent authoring component used by imported and editor-created lights. Point, Spot and Directional are
// renderer-ready types; extended emitters remain explicit authoring components until their sampling kernels land.
struct PunctualLuminaireRecord
{
    std::string               Name;
    PunctualLuminaireCategory Category = PunctualLuminaireCategory::Point;
    LuminaireDistribution     Distribution = LuminaireDistribution::Uniform;
    float                     Colour[3] = { 1.0f, 1.0f, 1.0f };   // [-] linear Rec.709
    float                     Intensity = 1.0f;     // [cd] point/spot, [lux] directional
    float                     Range     = 0.0f;     // [m]  0 = infinite
    float                     InnerConeAngle = 0.0f;   // [rad]
    float                     OuterConeAngle = 0.7853982f;
    float                     Size[2] = { 1.0f, 1.0f }; // [m] rectangle width/height, tube/strip length/width
    // Strip-specific authoring stays on the same persistent light component. These values mirror the accepted
    // LED Strip cards rather than deriving an electrical model from generic intensity/size controls.
    float                     LumensPerMetre = 1000.0f; // [lm/m]
    float                     WattsPerMetre = 14.4f;    // [W/m]
    float                     Dimmer = 1.0f;            // [0..1]
    float                     Temperature = 4000.0f;    // [K]
    float                     EmittersPerMetre = 60.0f; // [/m]
    float                     SupplyVoltage = 24.0f;    // [V]
    // Shadow and response authoring shared by every emitter category, mirroring the accepted
    // "Shadows & response" card rather than being inferred from intensity.
    float                     ShadowSoftness = 28.0f;   // [%] penumbra width at the shadow edge
    float                     DiffuseResponse = 100.0f; // [%] matte surface contribution
    float                     SpecularResponse = 100.0f;// [%] highlight contribution
    bool                      Diffuser = true;
    bool                      DrawEmitter = true;
    bool                      Enabled = true;
    bool                      CastShadows = true;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     SCENE STRUCTURE
//------------------------------------------------------------------------------------------------------------------------

class SceneStructure
{
public:
    SceneStructure() noexcept = default;
    ~SceneStructure() noexcept = default;

    SceneStructure(const SceneStructure&) = delete;
    SceneStructure& operator=(const SceneStructure&) = delete;

    // ── Shared topology ─────────────────────────────────────────────────────────────────────────────────────────
    // Register a mesh ONCE and place it as many times as the level wants. Only what actually differs between two
    //    placements — the transform, the material, the flags — is stored per placement; the vertex span, the index
    //    span and the per-cluster LOD bake are stored once and referenced by every instance of them.
    //
    //    What this costs, measured on the material grid: 400 placements of the 68 k-triangle shader ball used to
    //    mean 400 Morton sorts, 400 copies of 34 623 vertices, and 400 × 531 PatchGeometry::LoadOrBake coarse
    //    bakes — about 1.1 GB of vertex and index storage for one mesh in 400 positions. Shared, the topology is
    //    resident once (≈ 3.6 MB) and a placement appends only its InstanceRecords and ClusterRecords.
    //
    //    When a placement must NOT share: see ForkTopology. Material already varies per placement at no cost,
    //    because MaterialIndex lives on InstanceRecord, not on the topology.
    [[nodiscard]] uint32_t  RegisterTopology(const GeometryStructure& Mesh) noexcept;

    // Place a registered topology. Returns the first InstanceRecord index, as RegisterInstance does.
    uint32_t                PlaceTopology(uint32_t Topology, const Matrix4x4& World, uint32_t MaterialIndex, uint32_t Flags) noexcept;

    // Groundwork for destruction: give one placement a PRIVATE copy of the topology it currently shares, so its
    //    geometry can be cut, split or removed without touching its siblings. The placement's InstanceRecords are
    //    repointed at the copy; everything else in the scene is untouched. Returns the new topology index.
    //    📝 Deliberately explicit. Nothing forks implicitly — a caller that mutates shared topology without asking
    //    for a fork is mutating every placement of it, and that has to be a decision rather than an accident.
    uint32_t                ForkTopology(uint32_t FirstInstance) noexcept;

    [[nodiscard]] uint32_t  QueryTopologyCount() const noexcept { return static_cast<uint32_t>(Topologies.size()); }
    // Placements sharing each topology, for diagnostics and for the sharing proof.
    [[nodiscard]] uint32_t  QueryTopologyPlacements(uint32_t Topology) const noexcept
                            { return Topology < Topologies.size() ? Topologies[Topology].Placements : 0u; }
    // InstanceRecords one placement of this topology emits — the ≤ 8 192-triangle split, so a 68 k-triangle mesh
    //    is 9 of them. A caller walking placements must step by this, not by one.
    [[nodiscard]] uint32_t  QueryTopologyPartitions(uint32_t Topology) const noexcept
                            { return Topology < Topologies.size() ? static_cast<uint32_t>(Topologies[Topology].Partitions.size()) : 0u; }

    // Append one mesh (object space) under a world transform. The mesh is split into ≤ 8 192-triangle instances and
    //    ≤ 128-triangle clusters; returns the first InstanceRecord index. Indices are into `Mesh`'s vertex span.
    //    Equivalent to PlaceTopology(RegisterTopology(Mesh), ...): meshes with identical content share topology
    //    automatically, so existing callers that register the same mesh repeatedly get the sharing for free.
    uint32_t                RegisterInstance(const GeometryStructure& Mesh, const Matrix4x4& World, uint32_t MaterialIndex, uint32_t Flags) noexcept;
    uint32_t                RegisterMaterial(const MaterialDescriptor& Material) noexcept;

    // Scene graph rows (R4a). RegisterPlacement links the new row under `Ancestor` (appended as the last peer).
    uint32_t                RegisterPlacement(std::string PlacementName, uint32_t Ancestor, const Matrix4x4& Local, const Matrix4x4& World) noexcept;
    uint32_t                RegisterCamera(const CameraRecord& Camera, uint32_t Placement) noexcept;
    uint32_t                RegisterPunctualLuminaire(const PunctualLuminaireRecord& Luminaire, uint32_t Placement) noexcept;
    void                    AttachInstances(uint32_t Placement, uint32_t FirstInstance, uint32_t InstanceCount) noexcept;
    void                    AttachCamera(uint32_t Placement, uint32_t Camera) noexcept            { if (Placement < Placements.size() && Camera < Cameras.size()) Placements[Placement].Camera = Camera; }
    void                    AttachPunctualLuminaire(uint32_t Placement, uint32_t Luminaire) noexcept { if (Placement < Placements.size() && Luminaire < PunctualLuminaires.size()) Placements[Placement].Luminaire = Luminaire; }
    void                    AssignPlacementDynamic(uint32_t Placement, bool Dynamic) noexcept { if (Placement < Placements.size()) Placements[Placement].Dynamic = Dynamic; }
    void                    AssignPlacementBaseMesh(uint32_t Placement, uint8_t Kind) noexcept { if (Placement < Placements.size()) Placements[Placement].BaseMesh = Kind; }

    // Finalise: flatten materials at `SlabLimit`, flatten world-space triangles, gather luminaires, build the alias
    //    table. `Report` receives the material fold lines.
    void                    Finalise(uint32_t SlabLimit = 1u, std::vector<std::string>* Report = nullptr) noexcept;

    // Commit a fixed-topology deformation snapshot; refresh culling and flattened world triangles together.
    void                    RefreshGeometry(const std::vector<InstanceRecord>& Rows) noexcept;

    void                    Clear() noexcept;

    [[nodiscard]] const std::vector<VertexRecord>&      QueryVertices()   const noexcept { return Vertices; }
    // 🔴 Mutating the vertex span of a SHARED topology changes every placement of it. Call ForkTopology first
    //    unless that is genuinely what you want. The span a placement owns is [VertexOffset, +VertexCount).
    [[nodiscard]] std::vector<VertexRecord>&            AccessVertices()        noexcept { return Vertices; }
    [[nodiscard]] const std::vector<uint32_t>&          QueryIndices()    const noexcept { return Indices; }
    [[nodiscard]] const std::vector<InstanceRecord>&    QueryInstances()  const noexcept { return Instances; }
    [[nodiscard]] const std::vector<ClusterRecord>&     QueryClusters()   const noexcept { return Clusters; }
    [[nodiscard]] const MaterialIndex&                  QueryMaterials()  const noexcept { return Materials; }
    [[nodiscard]] MaterialIndex&                        AccessMaterials()       noexcept { return Materials; }   // M7b: the materials page commits drafts through here
    [[nodiscard]] MaterialIndex&                        ModifyMaterials()       noexcept { return Materials; }
    [[nodiscard]] const std::vector<PlacementRecord>&   QueryPlacements() const noexcept { return Placements; }
    [[nodiscard]] std::vector<PlacementRecord>&         AccessPlacements() noexcept { return Placements; }
    [[nodiscard]] std::vector<PunctualLuminaireRecord>& AccessPunctualLuminaires() noexcept { return PunctualLuminaires; }
    [[nodiscard]] const std::vector<CameraRecord>&      QueryCameras()    const noexcept { return Cameras; }
    [[nodiscard]] const std::vector<PunctualLuminaireRecord>& QueryPunctualLuminaires() const noexcept { return PunctualLuminaires; }
    [[nodiscard]] const std::vector<LuminaireRecord>&   QueryLuminaires() const noexcept { return Luminaires; }
    [[nodiscard]] const std::vector<TriangleIndex>&     QueryFlatTriangles() const noexcept { return FlatTriangles; }
    [[nodiscard]] float                                 QueryLuminairePower() const noexcept { return TotalLuminairePower; }
    [[nodiscard]] uint32_t                              QueryTriangleCount() const noexcept { uint32_t Count = 0; for (const auto& I : Instances) Count += I.TriangleCount; return Count; }
    [[nodiscard]] const std::string&                    QueryName() const noexcept { return Name; }
    void                                                AssignName(std::string NewName) noexcept { Name = std::move(NewName); }

    // World-space bounds of everything registered (valid after Finalise).
    [[nodiscard]] Vector3   QueryBoundsMinimum() const noexcept { return BoundsMinimum; }
    [[nodiscard]] Vector3   QueryBoundsMaximum() const noexcept { return BoundsMaximum; }

    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    static ClusterRecord    ConstructCluster(const VertexRecord* MeshVertices, const uint32_t* MeshIndices, uint32_t TriangleCount, bool DoubleSided) noexcept;

    // One registered topology: the spans in Vertices / Indices that every placement of it shares, plus the cluster
    //    rows a placement copies and re-stamps. Built by RegisterTopology; never mutated afterwards.
    struct TopologyRecord
    {
        uint64_t              ContentKey    = 0u;   // [-]   hash of the mesh's indices + positions; the sharing key
        uint32_t              VertexOffset  = 0u;   // [idx] first VertexRecord — the value every instance carries
        uint32_t              VertexCount   = 0u;   // [cnt]
        uint32_t              TriangleCount = 0u;   // [cnt] fine triangles across the whole mesh
        uint32_t              Placements    = 0u;   // [cnt] instances currently sharing this topology
        // One entry per InstanceRecord a placement needs (the ≤ 8 192-triangle split), in order.
        struct Partition
        {
            uint32_t FirstIndex    = 0u;            // [idx] into Indices — shared, identical for every placement
            uint32_t TriangleCount = 0u;            // [cnt]
            uint32_t FirstCluster  = 0u;            // [idx] into ClusterTemplates
            uint32_t ClusterCount  = 0u;            // [cnt]
        };
        std::vector<Partition> Partitions;
    };

    // Cluster rows as built, with InstanceIndex left unset. A placement copies its partition's rows into Clusters
    //    and stamps the owning instance. 64 B per cluster per placement is the one duplication that remains: the
    //    row's only per-placement field is InstanceIndex, and removing it would change the device ABI the cull
    //    shader reads. For the 400-ball grid that is 13.6 MB, against 1.1 GB for duplicating the topology itself.
    std::vector<ClusterRecord>     ClusterTemplates;
    std::vector<TopologyRecord>    Topologies;
    // Which topology each InstanceRecord was placed from, parallel to Instances. It lives here and not on
    //    InstanceRecord because that struct is a 160-byte std430 mirror of GpuInstance with no spare word, and
    //    the device has no use for the back-reference — only ForkTopology does.
    std::vector<uint32_t>          InstanceTopology;

    [[nodiscard]] static uint64_t  TopologyKey(const GeometryStructure& Mesh) noexcept;

    std::vector<VertexRecord>      Vertices;
    std::vector<uint32_t>          Indices;
    std::vector<InstanceRecord>    Instances;
    std::vector<ClusterRecord>     Clusters;
    MaterialIndex                  Materials;
    std::vector<LuminaireRecord>   Luminaires;
    std::vector<PlacementRecord>   Placements;
    std::vector<CameraRecord>      Cameras;
    std::vector<PunctualLuminaireRecord> PunctualLuminaires;
    std::vector<TriangleIndex>     FlatTriangles;
    float                          TotalLuminairePower = 0.0f;
    Vector3                        BoundsMinimum;
    Vector3                        BoundsMaximum;
    std::string                    Name;
};

template<>
inline uint32_t SceneStructure::Convert<uint32_t>() const noexcept
{
    return QueryTriangleCount();
}

} // namespace Frontier
