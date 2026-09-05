//============================================================================================================================================
//                                                       SCENESTRUCTURE.H
//============================================================================================================================================
// 🧩 Resident scene: every record the GPU keeps for a whole level — vertices, indices, instances, cull clusters,
//    materials and the luminaire alias table — in the exact std430 layouts the R2 shaders read.
//
// Layout contract (mirrored in Shaders/SceneRecords.slang — change both or neither):
//    VertexRecord     64 B  GeometryStructure.h   (pos.xyz pad | normal.xyz pad | tangent.xyzw | u v pad pad)
//    InstanceRecord  160 B  World, PreviousWorld (column-major), mesh range, material, cluster range
//    ClusterRecord    48 B  object-space bounding sphere + normal cone + triangle range   (cull unit, ≤ 128 triangles)
//    LuminaireRecord  32 B  emissive triangle + Walker alias entry for O(1) light selection
//    MaterialRecord   64 B  ContentInterchange/MaterialIndex.h (header) + MaterialSlabRecord 288 B per slab (R4a)
//    TriangleIndex    64 B  SwapchainExchange.h — flattened world-space triangles addressed by the CWBVH primitive index
//                           (R4a: carries per-vertex UVs for texture lookup; 🚧 R5 deletes it).
//    PlacementRecord / CameraRecord / PunctualLuminaireRecord — CPU-only scene-graph rows (R4a, data only, no UI).
//
// Visibility identifier (GeometricRaster/VisibilityProjection.h): 18-bit instance token << 14 | 14-bit primitive token.
//    A mesh with more than 16 384 triangles is split into several InstanceRecords sharing one transform, so the
//    primitive token never overflows. 0xFFFFFFFF = nothing rasterised.

#pragma once

#if defined(_MSC_VER)
    #pragma warning(disable: 4324)
#endif

#include "GeometryStructure.h"
#include "../DeviceExchange/SwapchainExchange.h"
#include "../ContentInterchange/MaterialIndex.h"
#include <cstdint>
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
    uint32_t TriangleCount;             // [cnt] triangles in this instance (≤ 16 384)
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
};
static_assert(sizeof(ClusterRecord) == 48u, "ClusterRecord must be 48 bytes (std430 mirror)");

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
};

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

enum class PunctualLuminaireCategory : uint32_t { Directional = 0, Point = 1, Spot = 2 };

struct PunctualLuminaireRecord                      // 🚧 stored only in R4a; the kernel does not light from these yet
{
    std::string               Name;
    PunctualLuminaireCategory Category = PunctualLuminaireCategory::Point;
    float                     Colour[3] = { 1.0f, 1.0f, 1.0f };   // [-] linear Rec.709
    float                     Intensity = 1.0f;     // [cd] point/spot, [lux] directional (KHR_lights_punctual)
    float                     Range     = 0.0f;     // [m]  0 = infinite
    float                     InnerConeAngle = 0.0f;   // [rad]
    float                     OuterConeAngle = 0.7853982f;
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

    // Append one mesh (object space) under a world transform. The mesh is split into ≤ 16 384-triangle instances and
    //    ≤ 128-triangle clusters; returns the first InstanceRecord index. Indices are into `Mesh`'s vertex span.
    uint32_t                RegisterInstance(const GeometryStructure& Mesh, const Matrix4x4& World, uint32_t MaterialIndex, uint32_t Flags) noexcept;
    uint32_t                RegisterMaterial(const MaterialDescriptor& Material) noexcept;

    // Scene graph rows (R4a). RegisterPlacement links the new row under `Ancestor` (appended as the last peer).
    uint32_t                RegisterPlacement(std::string PlacementName, uint32_t Ancestor, const Matrix4x4& Local, const Matrix4x4& World) noexcept;
    uint32_t                RegisterCamera(const CameraRecord& Camera, uint32_t Placement) noexcept;
    uint32_t                RegisterPunctualLuminaire(const PunctualLuminaireRecord& Luminaire, uint32_t Placement) noexcept;
    void                    AttachInstances(uint32_t Placement, uint32_t FirstInstance, uint32_t InstanceCount) noexcept;
    void                    AttachCamera(uint32_t Placement, uint32_t Camera) noexcept            { if (Placement < Placements.size() && Camera < Cameras.size()) Placements[Placement].Camera = Camera; }
    void                    AttachPunctualLuminaire(uint32_t Placement, uint32_t Luminaire) noexcept { if (Placement < Placements.size() && Luminaire < PunctualLuminaires.size()) Placements[Placement].Luminaire = Luminaire; }

    // Finalise: flatten materials at `SlabLimit`, flatten world-space triangles, gather luminaires, build the alias
    //    table. `Report` receives the material fold lines.
    void                    Finalise(uint32_t SlabLimit = 1u, std::vector<std::string>* Report = nullptr) noexcept;

    void                    Clear() noexcept;

    [[nodiscard]] const std::vector<VertexRecord>&      QueryVertices()   const noexcept { return Vertices; }
    [[nodiscard]] const std::vector<uint32_t>&          QueryIndices()    const noexcept { return Indices; }
    [[nodiscard]] const std::vector<InstanceRecord>&    QueryInstances()  const noexcept { return Instances; }
    [[nodiscard]] const std::vector<ClusterRecord>&     QueryClusters()   const noexcept { return Clusters; }
    [[nodiscard]] const MaterialIndex&                  QueryMaterials()  const noexcept { return Materials; }
    [[nodiscard]] MaterialIndex&                        ModifyMaterials()       noexcept { return Materials; }
    [[nodiscard]] const std::vector<PlacementRecord>&   QueryPlacements() const noexcept { return Placements; }
    [[nodiscard]] const std::vector<CameraRecord>&      QueryCameras()    const noexcept { return Cameras; }
    [[nodiscard]] const std::vector<PunctualLuminaireRecord>& QueryPunctualLuminaires() const noexcept { return PunctualLuminaires; }
    [[nodiscard]] const std::vector<LuminaireRecord>&   QueryLuminaires() const noexcept { return Luminaires; }
    [[nodiscard]] const std::vector<TriangleIndex>&     QueryFlatTriangles() const noexcept { return FlatTriangles; }
    [[nodiscard]] float                                 QueryLuminairePower() const noexcept { return TotalLuminairePower; }
    [[nodiscard]] uint32_t                              QueryTriangleCount() const noexcept { return static_cast<uint32_t>(Indices.size() / 3u); }
    [[nodiscard]] const std::string&                    QueryName() const noexcept { return Name; }
    void                                                AssignName(std::string NewName) noexcept { Name = std::move(NewName); }

    // World-space bounds of everything registered (valid after Finalise).
    [[nodiscard]] Vector3   QueryBoundsMinimum() const noexcept { return BoundsMinimum; }
    [[nodiscard]] Vector3   QueryBoundsMaximum() const noexcept { return BoundsMaximum; }

    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    static ClusterRecord    ConstructCluster(const VertexRecord* MeshVertices, const uint32_t* MeshIndices, uint32_t TriangleCount, bool DoubleSided) noexcept;

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
