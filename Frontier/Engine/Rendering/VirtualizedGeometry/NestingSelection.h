/*==========================================================================================================================================
                                                          NESTINGSELECTION.H
==========================================================================================================================================*/
// 🧩 1:1 port of the demo's virtualized geometry runtime (GeometryHierarchyRuntime.js): fixed-size page packing and LRU residency,
//    the projected-error cut with refraction deviation for glass, frustum and normal-cone culling, two-phase Hi-Z occlusion and
//    indirect draw compaction separating opaque and transmissive visibility classes, plus the GPU page slot allocator.

// 📝 Matrices are column-major float64 like the demo's Float64Array(16). An absent LocalToWorld (nullptr) is the identity.
//    Demo vocabulary: HierarchyResidencyManager → NestingResidency, SelectHierarchyCut → SelectNestingCut, SelfErrorPx /
//    ParentErrorPx → OwnErrorPx / CoarserErrorPx, ExecuteHierarchyRuntimeFrame → ExecuteNestingRefresh, Level → Tier.
#pragma once
#include "NestingConstruction.h"
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace Frontier::VirtualizedGeometry
{
using ColumnMatrix = std::array<double, 16>;

[[nodiscard]] ColumnMatrix IdentityMatrix() noexcept;
[[nodiscard]] ColumnMatrix MultiplyMatrices(const ColumnMatrix& A, const ColumnMatrix& B) noexcept;
[[nodiscard]] Point3 TransformWorldPoint(const ColumnMatrix* Matrix, const Point3& Point) noexcept;
[[nodiscard]] Point3 TransformWorldDirection(const ColumnMatrix* Matrix, const Point3& Vector) noexcept;
[[nodiscard]] std::array<double, 4> TransformClipPoint(const ColumnMatrix& Matrix, const Point3& Point) noexcept;
[[nodiscard]] double ExtractMaxScale(const ColumnMatrix* Matrix) noexcept;

struct NestingCamera
{
    std::optional<ColumnMatrix> ViewProjection{};
    std::optional<ColumnMatrix> PreviousViewProjection{};
    Point3                      Eye{0, -10, 4};               // [m]
    Point3                      Target{0, 0, 0};              // [m]
    Point3                      Up{0, 0, 1};                  // [-]
    double                      FovY         = 3.141592653589793 / 3;   // [rad]
    double                      ScreenWidth  = 720;           // [px]
    double                      ScreenHeight = 540;           // [px]
    double                      Near         = 0.1;           // [m]
    double                      Far          = 1000.0;        // [m]
    std::optional<double>       ProjectionScale{};            // [px] focal length in pixels, derived from FovY when absent
    std::optional<double>       PixelErrorThreshold{};        // [px]
};
// 📝 Reversed-Z perspective (depth 1 at Near, 0 at Far) matching SurfaceProjection; the camera's own matrix when it has one.
[[nodiscard]] ColumnMatrix ConstructViewProjection(const NestingCamera& Camera);

//------------------------------------------------------------------------------------------------------------------------
//                                              FIXED-SIZE PAGES & LRU RESIDENCY
//------------------------------------------------------------------------------------------------------------------------

struct NestingPage
{
    uint32_t              PageIndex = 0;
    bool                  Pinned    = false;
    uint32_t              ByteSize  = 0;     // [B]
    std::vector<uint32_t> Clusters{};
    std::vector<uint32_t> Tiers{};
    std::vector<uint8_t>  BinaryPayload{};   // 32 B header ('1MGP' magic), then a 24 B header per cluster slice
};
// 📝 Clusters pack coarsest tier first into fixed-size pages; the coarsest tier's pages are pinned. Sets NestedCluster::PageIndex.
[[nodiscard]] std::vector<NestingPage> PackNestingPages(GeometryNesting& Nesting, uint32_t PageByteLimit = 65536);

class NestingResidency
{
  public:
    NestingResidency(const std::vector<NestingPage>& Pages, std::optional<double> MaxResidentBytes = std::nullopt, bool InitiallyResidentAll = false);
    [[nodiscard]] bool IsPageResident(int32_t PageIndex) const;
    void               TouchPage(int32_t PageIndex, int32_t FrameIndex = 0);
    void               RequestPage(int32_t PageIndex, double PriorityErrorPx);
    uint32_t           ServiceStreamingQueue(int32_t FrameIndex = 0, uint32_t MaxPagesToLoad = 2);
    struct StreamingRequest
    {
        int32_t PageIndex = 0;
        double  Priority  = 0.0;
    };
    const std::vector<NestingPage>& Pages;
    double                          MaxResidentBytes = 0.0;
    std::vector<uint32_t>           ResidentPages{};     // [-] insertion order, as the demo's Set
    std::vector<int32_t>            LastUsedFrame{};
    double                          ResidentBytes = 0.0;
    std::vector<StreamingRequest>   StreamingQueue{};
    uint32_t                        Evictions = 0, Loads = 0;
  private:
    [[nodiscard]] bool Holds(uint32_t PageIndex) const;
};

//------------------------------------------------------------------------------------------------------------------------
//                                              PROJECTED-ERROR CUT
//------------------------------------------------------------------------------------------------------------------------

struct DepthPyramid;
struct RefreshOptions
{
    std::optional<double> PixelErrorThreshold{};    // [px]
    NestingResidency*     Residency = nullptr;
    int32_t               FrameIndex = 0;
    std::optional<double> IndexOfRefraction{};      // [-] 1.52 when absent
    bool                  BackfaceCull = true;
    bool                  StandardDepth = false;    // [-] DepthMode "standard"; reversed-Z otherwise
    const DepthPyramid*   PreviousDepthPyramid = nullptr;
};

[[nodiscard]] double ComputeProjectionScale(const NestingCamera& Camera);
[[nodiscard]] double EvaluateProjectedSphereError(const NestingSphere& Sphere, double ObjectError, const NestingCamera& Camera,
                                                  const ColumnMatrix* LocalToWorld = nullptr, uint32_t ClassMask = 1,
                                                  const NestingCone* Cone = nullptr, const RefreshOptions& Options = {});
struct ClusterCutMetric
{
    double OwnErrorPx     = 0.0;   // [px]
    double CoarserErrorPx = 0.0;   // [px]
};
[[nodiscard]] ClusterCutMetric EvaluateClusterProjectedError(const NestedCluster& Node, const NestingCamera& Camera,
                                                             const ColumnMatrix* LocalToWorld = nullptr, const RefreshOptions& Options = {});
struct NestingCut
{
    std::vector<uint32_t>         CutClusterIdentities{};
    uint32_t                      CutTriangles    = 0;
    uint32_t                      SourceTriangles = 0;
    std::vector<int32_t>          TierHistogram{};
    std::vector<ClusterCutMetric> Metrics{};   // [-] per cluster identity
};
// 📝 Per-cluster cut (own error <= threshold < coarser error), pruned to an antichain coarsest first, with non-resident clusters
//    promoted to their coarser group when a residency is attached.
[[nodiscard]] NestingCut SelectNestingCut(const GeometryNesting& Nesting, const NestingCamera& Camera, const ColumnMatrix* LocalToWorld = nullptr,
                                          const RefreshOptions& Options = {});

//------------------------------------------------------------------------------------------------------------------------
//                                              CULLING, HI-Z AND DEPTH RASTER
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool TestClusterFrustum(const NestedCluster& Node, const ColumnMatrix* LocalToWorld, const ColumnMatrix& ViewProjection);
[[nodiscard]] bool TestClusterNormalCone(const NestedCluster& Node, const ColumnMatrix* LocalToWorld, const Point3& CameraEye,
                                         const RefreshOptions& Options = {});

struct DepthPyramid
{
    struct MipLevel
    {
        uint32_t           Width = 0, Height = 0;
        std::vector<float> Depth{};
    };
    uint32_t              Width = 0, Height = 0;
    bool                  StandardDepth = false;
    std::vector<MipLevel> Mips{};
};
// 📝 Conservative pyramid: reversed-Z keeps the minimum of each 2x2 footprint (3 taps along an odd edge), standard depth the maximum.
[[nodiscard]] DepthPyramid ConstructDepthPyramid(const std::vector<float>& BaseDepth, uint32_t Width, uint32_t Height, bool StandardDepth = false);
struct ClusterScreenRect
{
    bool   CrossesNearPlane = false, Offscreen = false;
    double MinX = 0, MaxX = 0, MinY = 0, MaxY = 0, ClosestDepth = 0;
};
[[nodiscard]] ClusterScreenRect ProjectClusterScreenRect(const NestedCluster& Node, const ColumnMatrix* LocalToWorld, const ColumnMatrix& ViewProjection,
                                                         double Width, double Height, bool StandardDepth = false);
[[nodiscard]] bool TestClusterHiZ(const NestedCluster& Node, const ColumnMatrix* LocalToWorld, const ColumnMatrix& ViewProjection,
                                  const DepthPyramid* Pyramid);

struct RefreshInstance
{
    const GeometryNesting*      Nesting = nullptr;
    std::optional<ColumnMatrix> LocalToWorld{};
    std::optional<ColumnMatrix> PreviousLocalToWorld{};
    std::optional<uint32_t>     Occupant{};
    NestingResidency*           Residency = nullptr;
};
struct RefreshCluster
{
    uint32_t               InstanceIndex = 0, Occupant = 0;
    const GeometryNesting* Nesting = nullptr;
    const NestedCluster*   Node    = nullptr;
    const ColumnMatrix*    LocalToWorld = nullptr;
    const ColumnMatrix*    PreviousLocalToWorld = nullptr;
    ClusterCutMetric       Metric{};
    uint32_t               Phase = 0;
};
// 📝 Software depth raster of opaque cut clusters (transmissive-only clusters never write depth).
void RasterizeClustersToDepth(const std::vector<RefreshCluster>& Clusters, std::vector<float>& DepthBuffer, uint32_t Width, uint32_t Height,
                              const ColumnMatrix& ViewProjection, bool StandardDepth = false);

struct ClusterDrawCommand
{
    uint32_t IndexCount = 0, InstanceCount = 1, FirstIndex = 0, BaseVertex = 0, FirstInstance = 0;
    uint32_t Occupant = 0, InstanceIndex = 0, ClusterIdentity = 0, Tier = 0, Phase = 0, ClassMask = 0;
    double   OwnErrorPx = 0.0;
};
struct NestingRefresh
{
    ColumnMatrix                    ViewProjection{};
    std::vector<float>              DepthBuffer{};
    DepthPyramid                    CurrentDepthPyramid{};
    std::vector<RefreshCluster>     CutCandidates{}, Phase1Visible{}, Phase2Disoccluded{}, OccludedCulled{}, FrustumCulled{}, ConeCulled{},
                                    VisibleClusters{};
    std::vector<ClusterDrawCommand> OpaqueDraws{}, TransmissiveDraws{};
    struct Statistics
    {
        uint32_t SourceTriangles = 0, CutTriangles = 0, VisibleTriangles = 0, CutClusterCount = 0, Phase1VisibleCount = 0,
                 Phase2DisoccludedCount = 0, OccludedCulledCount = 0, FrustumCulledCount = 0, ConeCulledCount = 0, OpaqueDrawCount = 0,
                 TransmissiveDrawCount = 0;
    } Stats{};
};
// 📝 One refresh: cut per instance, frustum and cone culling, phase 1 against the previous pyramid, depth raster, phase 2 against the
//    current pyramid, and compaction into opaque and transmissive draw streams. Instances must outlive the result.
[[nodiscard]] NestingRefresh ExecuteNestingRefresh(const std::vector<RefreshInstance>& Instances, const NestingCamera& Camera,
                                                   const RefreshOptions& Options = {});

//------------------------------------------------------------------------------------------------------------------------
//                                              GPU PAGE SLOT ALLOCATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Fixed GPU arena slots the pages stream through: pinned pages never evict, others evict the least recently used page.
class PageSlotAllocator
{
  public:
    explicit PageSlotAllocator(uint32_t SlotCount);
    void Pin(uint32_t PageIndex) { PinnedPages_.insert(PageIndex); }
    [[nodiscard]] bool IsResident(uint32_t PageIndex) const;
    [[nodiscard]] std::optional<uint32_t> SlotOf(uint32_t PageIndex) const;
    void Touch(uint32_t PageIndex, int32_t FrameIndex);
    struct Placement
    {
        int32_t                Slot = -1;
        std::optional<int32_t> EvictedPage{};   // [-] absent when a free slot was used or the page was resident; -1 when all are pinned
    };
    [[nodiscard]] Placement Ensure(uint32_t PageIndex, int32_t FrameIndex);
    void Evict(uint32_t PageIndex);
    uint32_t SlotCount = 0;
  private:
    std::vector<std::pair<uint32_t, uint32_t>> PageToSlot_{};   // [-] insertion order, as the demo's Map
    std::unordered_map<uint32_t, uint32_t>     SlotToPage_{};
    std::unordered_map<uint32_t, int32_t>      LastUsedFrame_{};
    std::unordered_set<uint32_t>               PinnedPages_{};
};
} // namespace Frontier::VirtualizedGeometry
