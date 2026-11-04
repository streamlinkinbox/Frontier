/*==========================================================================================================================================
                                                          SURFACEPROJECTION.H
==========================================================================================================================================*/
// 🧩 Primary visibility (depth, attributes, motion), lossless cluster culling and the direct radiance over it (SurfaceProjection.js).

#pragma once

#include "SceneStructure.h"
#include "ShaderInterchange.h"
#include "Rendering/VirtualizedGeometry/NestingProjection.h"

#include <map>

namespace Frontier::GlobalIllumination
{

// 📝 GeometryClusters.js: lossless spatial leaf clusters (spatial median splits, no triangle removal).
struct GeometryCluster
{
    Vector3  Minimum{};
    Vector3  Maximum{};
    uint32_t FirstIndex = 0;
    uint32_t IndexCount = 0;
};

struct GeometryClusterArchive
{
    std::vector<uint32_t>        Indices{};
    std::vector<GeometryCluster> Clusters{};
};

[[nodiscard]] GeometryClusterArchive BuildGeometryClusters(const FacetSet& Set, uint32_t Limit = 128);

// 📝 ClusteredGeometry.js: GPU frustum-tested static clusters; mutable/fragmented/dynamic geometry stays on instanced runs.
class ClusteredGeometry
{
  public:
    ClusteredGeometry(DeviceExchange& Exchange, ShaderInterchange& Shaders);

    void ConstructPrograms(const std::shared_ptr<DeviceRegion>& RefreshRegion, const std::shared_ptr<DeviceRegion>& OccupantRegion);
    void Prepare(const SceneStructure& Scene);
    void RecordCull(CommandSequence& Sequence);

    struct ClusterDraw
    {
        bool     Run      = false;
        DrawRun  RunEntry{};
        uint32_t Cluster  = 0;
        uint32_t Occupant = 0;
    };

    std::vector<ClusterDraw>          Draws{};
    uint32_t                          CandidateCount = 0;
    std::shared_ptr<DeviceRegion>     Indices{}, Instances{}, Records{}, Commands{};

  private:
    DeviceExchange&                   Exchange_;
    ShaderInterchange&                Shaders_;
    std::shared_ptr<DeviceRegion>     RefreshRegion_{}, OccupantRegion_{};
    std::shared_ptr<DescriptorLayout> Layout_{};
    std::shared_ptr<DeviceProgram>    Program_{};
    std::shared_ptr<DescriptorGroup>  Group_{};
    std::string                       Key_{};
    std::map<uint32_t, GeometryClusterArchive> Retained_{};
};

struct SurfaceTargetInputs
{
    std::shared_ptr<DeviceTextureView> ShadowLayered{};
    std::shared_ptr<DeviceSampler>     ShadowComparison{};
    std::shared_ptr<DeviceRegion>      ShadowReceiver{};
    std::shared_ptr<DeviceTextureView> SkyView{};
    std::shared_ptr<DeviceSampler>     SkySampler{};
    std::shared_ptr<DeviceRegion>      LuminaireRegion{};
    std::shared_ptr<DeviceRegion>      CloudShadowParams{};
    std::shared_ptr<DeviceTextureView> CloudShadowView{};
    std::shared_ptr<DeviceSampler>     CloudShadowSampler{};
};

class SurfaceProjection
{
  public:
    SurfaceProjection(DeviceExchange& Exchange, ShaderInterchange& Shaders);

    void ConstructPrograms(const std::shared_ptr<DeviceRegion>& RefreshRegion, const std::shared_ptr<DeviceRegion>& OccupantRegion,
                           const std::shared_ptr<DeviceRegion>& MaterialRegion, const std::shared_ptr<DescriptorLayout>& DistanceLayout,
                           const std::shared_ptr<DescriptorLayout>& SamplingLayout, const std::shared_ptr<DescriptorLayout>& RayTracingLayout,
                           const std::shared_ptr<DeviceTextureView>& TyreMarkView, const std::shared_ptr<DeviceSampler>& TyreMarkSampler,
                           const std::shared_ptr<DeviceRegion>& EffectRegion, const std::shared_ptr<DeviceRegion>& VehicleRegion);
    void ConstructTargets(uint32_t Width, uint32_t Height, const SurfaceTargetInputs& Inputs);
    void RecordSurface(CommandSequence& Sequence, const SceneStructure& Scene, const std::shared_ptr<DeviceRegion>& VertexRegion);
    void RecordDirect(CommandSequence& Sequence, const std::shared_ptr<DescriptorGroup>& DistanceGroup,
                      const std::shared_ptr<DescriptorGroup>& SamplingGroup, const std::shared_ptr<DescriptorGroup>& RayTracingGroup);
    // 📝 RefinementPenumbraFilter: the ray-traced sun's penumbra filtered across its width, into the direct radiance.
    void RecordPenumbra(CommandSequence& Sequence);

    bool                               ClusteredEnabled = true;
    std::array<uint32_t, 2>            Extent{0, 0};
    std::shared_ptr<DeviceTexture>     DepthTexture{}, AttributeTexture{}, MotionTexture{}, DirectTexture{}, PenumbraTexture{};
    std::shared_ptr<DeviceTextureView> DepthView{}, AttributeView{}, MotionView{}, DirectView{}, PenumbraView{};
    std::unique_ptr<ClusteredGeometry> Clusters{};
    // 📝 The demo's Surface.Virtualized: the cluster nesting takeover of eligible static sets (inert unless enabled).
    std::unique_ptr<VirtualizedGeometry::NestingProjection> Virtualized{};

  private:
    DeviceExchange&                   Exchange_;
    ShaderInterchange&                Shaders_;
    std::shared_ptr<DeviceRegion>     RefreshRegion_{}, MaterialRegion_{};
    std::shared_ptr<DescriptorLayout> RasterLayout_{}, DirectLayout_{}, PenumbraLayout_{};
    std::shared_ptr<DescriptorGroup>  RasterGroup_{}, DirectGroup_{}, PenumbraGroup_{};
    std::shared_ptr<DeviceProgram>    RasterProgram_{}, DirectProgram_{}, PenumbraProgram_{};
};

} // namespace Frontier::GlobalIllumination
