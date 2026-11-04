/*==========================================================================================================================================
                                                           RAYTRACINGSPACE.H
==========================================================================================================================================*/
// 🧩 Two-level ray-tracing acceleration structure: per-refresh TLAS over active occupants + binned-SAH BLAS (RayTracingSpace.js).

#pragma once

#include "SceneStructure.h"
#include "ShaderInterchange.h"

namespace Frontier::GlobalIllumination
{

constexpr uint32_t TlasEntryCapacity        = 4608;          // [-] reserved entries [0 .. 4607] at the head for the TLAS
constexpr uint32_t DynamicBlasEntryCapacity = 512;           // [-] reserved BLAS leaf entries for live fragments
constexpr uint32_t DynamicTriangleCapacity  = 8192;          // [-] reserved triangles for live fragments
constexpr uint32_t TlasLeafBit              = 0x80000000u;   // [-] high bit of Count marking a TLAS leaf
constexpr uint32_t EmptyTlasSentinel        = 0x7FFFFFFFu;   // [-] LeftOrFirst when the TLAS holds no occupant

[[nodiscard]] uint32_t PackOctahedralHost(const Vector3& Normal) noexcept;

struct FacetBlas
{
    int64_t              RootEntry     = -1;
    std::vector<uint8_t> EntryBytes{};
    uint32_t             EntryCount    = 0;
    std::vector<uint8_t> TriangleBytes{};
    uint32_t             TriangleCount = 0;
};

[[nodiscard]] FacetBlas ConstructFacetBlas(const std::vector<FacetEntry>& Facets, uint32_t GlobalEntryOffset, uint32_t GlobalTriangleOffset);

class RayTracingSpace
{
  public:
    explicit RayTracingSpace(DeviceExchange& Exchange);

    void BuildHostHierarchy(const SceneStructure& Scene);
    void ConstructResources(const SceneStructure& Scene, const std::shared_ptr<DeviceRegion>& MaterialRegion,
                            const std::shared_ptr<DeviceRegion>& LuminaireRegion);
    void AttachHmiTextures(const std::shared_ptr<DeviceTextureView>& TabletView, const std::shared_ptr<DeviceTextureView>& ClusterView);
    void RefreshTlas(SceneStructure& Scene, const std::array<bool, 4>* ShreddedWheels = nullptr);
    void UploadTlas();

    std::vector<int64_t>               FacetBlasRoots{};
    std::vector<int64_t>               OccupantBlasRoot{};
    uint32_t                           StaticEntryCount  = 0;
    uint32_t                           StaticTriCount    = 0;
    uint32_t                           DynamicEntryOffset = 0;
    uint32_t                           DynamicTriOffset  = 0;
    uint32_t                           TotalEntryCapacity = 0;
    uint32_t                           TotalTriCapacity  = 0;
    uint32_t                           ActiveInstances   = 0;
    uint32_t                           UsedTlasEntries   = 0;
    std::vector<uint8_t>               AllEntryBytes{};
    std::vector<uint8_t>               AllTriangleBytes{};
    std::shared_ptr<DeviceRegion>      EntryRegion{};
    std::shared_ptr<DeviceRegion>      TriangleRegion{};
    std::shared_ptr<DeviceRegion>      MaterialRegion{};
    std::shared_ptr<DeviceRegion>      LuminaireRegion{};
    std::shared_ptr<DeviceTexture>     DefaultHmiTexture{};
    std::shared_ptr<DescriptorLayout>  MarchLayout{};
    std::shared_ptr<DescriptorGroup>   MarchGroup{};

  private:
    DeviceExchange& Exchange_;
};

} // namespace Frontier::GlobalIllumination
