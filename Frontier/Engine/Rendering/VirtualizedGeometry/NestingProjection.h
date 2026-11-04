/*==========================================================================================================================================
                                                          NESTINGPROJECTION.H
==========================================================================================================================================*/
// 🧩 1:1 port of the demo's GPU virtualized geometry (VirtualizedGeometry.js): every cluster of every eligible facet set becomes a
//    record the parallel cull cuts by projected error and occlusion-tests in two phases against a reversed-Z Hi-Z pyramid, cluster
//    geometry streams through fixed arena slots by page, and a visibility raster with a deferred resolve feeds the cluster views.

// 📝 Inert unless Enabled (the demo's ?hierarchy=1, here RenderPipelineConfiguration::VirtualizedGeometry); the cluster inspection
//    modes 10-14 activate it on demand (ActivateForInspection). Eligible: a facet set of ≥ 256 facets, no reserved fragment vertices,
//    no dynamic occupant in its run. Shadows, ray tracing, distance fields and cards keep the full-detail geometry.
#pragma once
#include "NestingArchive.h"
#include "NestingSelection.h"
#include "Rendering/GlobalIllumination/SceneStructure.h"
#include "Rendering/GlobalIllumination/ShaderInterchange.h"
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
namespace Frontier::VirtualizedGeometry
{
[[nodiscard]] SurfaceSoup ConvertFacetSet(const GlobalIllumination::FacetSet& Set);
// 📝 I2, the import step: the nesting of every eligible set of a scene (≥ 256 facets, no reserved fragment vertices, at least one run
//    without a dynamic occupant), built with the given options on their worker threads.
[[nodiscard]] NestingArchive ConstructSceneNestingArchive(const GlobalIllumination::SceneStructure& Scene, const NestingOptions& Options);

class NestingProjection
{
  public:
    NestingProjection(DeviceExchange& Exchange, const GlobalIllumination::ShaderInterchange& IlluminationShaders);
    void ConstructPrograms(const std::shared_ptr<DeviceRegion>& RefreshRegion, const std::shared_ptr<DeviceRegion>& OccupantRegion);
    void ConstructTargets(uint32_t Width, uint32_t Height);
    // 📝 Returns this projection when it took over (Records then tells whether any cluster exists), nullptr while inert.
    NestingProjection* Prepare(const GlobalIllumination::SceneStructure& Scene);
    void ServiceStreaming(uint32_t MaxLoads = 2);
    void RecordCullPhase1(CommandSequence& Sequence);
    void RecordHiZ(CommandSequence& Sequence, const std::shared_ptr<DeviceTextureView>& DepthView);
    void RecordCullPhase2(CommandSequence& Sequence);
    void RecordClusters(CommandSequence& Sequence, const std::shared_ptr<DeviceRegion>& Instances, uint32_t Phase);
    void RecordVisibilityResolve(CommandSequence& Sequence, const std::shared_ptr<DescriptorGroup>& RasterGroup,
                                 const std::shared_ptr<DeviceRegion>& Instances);
    void RecordInspectionView(CommandSequence& Sequence, const std::shared_ptr<DeviceTextureView>& TargetView, uint32_t Mode);
    void DismissInspectionStatus() { StatusCaption_.clear(); }
    // 📝 The demo's ActivateForDebug, synchronous: programs and targets now, records at the next Prepare.
    void ActivateForInspection();
    // 📝 I2: the stored nestings beside the scene's .facetspace. The editor never builds: a set without a current entry stays on the
    //    lossless-cluster path.
    void AssignArchive(const std::filesystem::path& Path);
    [[nodiscard]] bool OwnsOccupant(uint32_t Occupant) const { return OwnedOccupants_.contains(Occupant); }
    [[nodiscard]] bool HasRecords() const noexcept { return Records_; }
    [[nodiscard]] uint32_t RecordCount() const noexcept { return RecordCount_; }
    [[nodiscard]] const std::string& StatusCaption() const noexcept { return StatusCaption_; }
    [[nodiscard]] uint32_t PageCount() const noexcept { return static_cast<uint32_t>(Pages_.size()); }
    [[nodiscard]] uint32_t PendingPages() const noexcept { return static_cast<uint32_t>(StreamQueue_.size()); }
    [[nodiscard]] uint32_t OwnedOccupantCount() const noexcept { return static_cast<uint32_t>(OwnedOccupants_.size()); }

    bool     Enabled     = false;   // [-] the demo's ?hierarchy=1
    uint32_t StreamSlots = 0;       // [-] the demo's ?streampages (0: one slot per page)
    // 📝 I1: one call per phase and class — an indirect-count draw over compacted commands with drawIndirectCount, else one multi-draw
    //    over the records with multiDrawIndirect; with neither, or with this off, the demo's one command per record. Read at each
    //    refresh's first cull.
    bool     CompactCommands = true;
    [[nodiscard]] bool     CommandsCompacted() const noexcept { return Compacted_; }
    [[nodiscard]] uint32_t RecordedCommands() const noexcept { return RecordedCommands_; }   // [-] indirect calls of the last refresh
    [[nodiscard]] double   RecordedMilliseconds() const noexcept { return RecordedMilliseconds_; }   // [ms] host time recording them

  private:
    struct ExpandedCluster
    {
        uint32_t ClusterIdentity = 0, FirstIndex = 0, BaseVertex = 0, IndexCount = 0, VertexCount = 0;
    };
    // 📝 Expanded per-corner geometry of one set's whole nesting (BuildSet), rebuilt when the facet count changes.
    struct ExpandedSet
    {
        size_t                       FacetCount = 0;
        GeometryNesting              Nesting{};
        std::vector<ExpandedCluster> Clusters{};     // [-] the demo's Records: tier order, then cluster order
        std::vector<float>           Vertices{};     // [-] 8 per corner: position, family bits, face normal, 0
        std::vector<uint32_t>        Indices{};
        std::vector<float>           CornerPos{};    // [-] 4 per corner: position, family bits
        std::vector<float>           CornerUv{};     // [-] 4 per corner: UV, tier, 0
    };
    struct StreamingPage
    {
        uint32_t                     GlobalPage = 0;
        bool                         Pinned     = false;
        std::map<uint32_t, uint32_t> LocalFirstIndex{};   // [-] cluster identity → first index inside the page
        std::vector<float>           Vertices{}, CornerPos{}, CornerUv{};
        std::vector<uint32_t>        Indices{};
        std::vector<uint32_t>        ClusterRecords{};    // [-] the demo's DrawRecords
    };
    struct ClusterRecord
    {
        uint32_t Record = 0, Occupant = 0, Page = 0, ClassMask = 0;
    };
    using RecordMirror = std::array<uint32_t, 24>;   // [-] 96 B NestedClusterProfile

    ExpandedSet* BuildSet(uint32_t FacetSetIndex, const GlobalIllumination::FacetSet& Set);
    void RefreshGroups();
    std::shared_ptr<DescriptorGroup> CullGroupFor(const std::shared_ptr<DeviceTextureView>& HiZView);
    std::shared_ptr<DescriptorGroup> ResolveGroupFor(const std::shared_ptr<DeviceTextureView>& Slot6, const std::shared_ptr<DeviceTextureView>& Slot11,
                                                     const std::shared_ptr<DeviceTextureView>& Slot12);
    void RecordClass(CommandSequence& Sequence, const std::shared_ptr<DeviceRegion>& Commands, const std::shared_ptr<DeviceRegion>& Instances,
                     bool Transmissive);
    void RecordUnavailableView(CommandSequence& Sequence, const std::shared_ptr<DeviceTextureView>& TargetView);
    [[nodiscard]] std::string InspectionState() const;
    void ReportInspectionStatus();

    DeviceExchange&                   Exchange_;
    GlobalIllumination::ShaderInterchange Shaders_;
    const GlobalIllumination::ShaderInterchange& IlluminationShaders_;
    std::map<uint32_t, ExpandedSet>   Retained_{};
    std::optional<NestingArchive>     Archive_{};
    std::string                       ArchiveCaption_{};   // [-] why sets fell back (missing or stale archive entries)
    std::shared_ptr<DeviceRegion>     RefreshRegion_{}, OccupantRegion_{};
    std::shared_ptr<DescriptorLayout> CullLayout_{}, SeedLayout_{}, DownsampleLayout_{}, VisibilityLayout_{}, GlassBackLayout_{}, ResolveLayout_{},
                                      FallbackLayout_{};
    std::shared_ptr<DeviceProgram>    CompactedCullProgram_{}, CompactedPhase2Program_{};
    std::shared_ptr<DeviceProgram>    CullProgram_{}, Phase2Program_{}, SeedProgram_{}, DownsampleProgram_{}, VisibilityOpaqueProgram_{},
                                      VisibilityGlassProgram_{}, VisibilityGlassBackProgram_{}, ResolveOpaqueProgram_{}, ResolveRefractionProgram_{},
                                      InspectIdentitiesProgram_{}, InspectGlassProgram_{}, InspectCopyProgram_{}, InspectHiZProgram_{},
                                      InspectTiersProgram_{};
    std::shared_ptr<DeviceProgram>    FallbackUnavailable_{}, FallbackFailed_{}, FallbackNoGeometry_{};
    std::shared_ptr<DeviceTexture>    DummyHiZ_{};
    std::shared_ptr<DeviceTextureView> DummyHiZView_{}, DummyStorageView_{}, DummySampledView_{}, PreviousHiZView_{}, CurrentHiZView_{};
    std::vector<std::shared_ptr<DeviceTexture>>     HiZTextures_{};
    std::vector<std::shared_ptr<DeviceTextureView>> HiZViews_{};
    std::vector<std::shared_ptr<DescriptorGroup>>   HiZGroups_{};   // [-] seed group (depth view dependent) then one per finer→coarser mip
    std::shared_ptr<DeviceTextureView>              SeededDepthView_{};
    std::shared_ptr<DeviceTexture>    OpaqueVisTexture_{}, OpaqueBaryTexture_{}, GlassVisTexture_{}, GlassBaryTexture_{}, GlassBackVisTexture_{},
                                      GlassBackBaryTexture_{}, VisDepthTexture_{}, GlassBackDepthTexture_{}, OpaqueResolvedTexture_{},
                                      FinalResolvedTexture_{};
    std::shared_ptr<DeviceTextureView> OpaqueVisView_{}, OpaqueBaryView_{}, GlassVisView_{}, GlassBaryView_{}, GlassBackVisView_{}, GlassBackBaryView_{},
                                       VisDepthView_{}, GlassBackDepthView_{}, OpaqueResolvedView_{}, FinalResolvedView_{};
    std::shared_ptr<DescriptorGroup>  GlassBackGroup_{};
    std::map<std::pair<const void*, const void*>, std::shared_ptr<DescriptorGroup>> CullGroups_{};
    std::map<std::array<const void*, 3>, std::shared_ptr<DescriptorGroup>>          ResolveGroups_{};
    std::array<uint32_t, 2>           Extent_{0, 0};
    std::optional<std::array<uint32_t, 2>> TargetExtent_{};
    bool                              ProgramsBuilt_ = false, Records_ = false, Compacted_ = false, CountedCommands_ = false;
    bool                              CompactionOffered_ = false;
    uint32_t                          RecordedCommands_ = 0;
    double                            RecordedMilliseconds_ = 0.0;
    std::shared_ptr<DeviceRegion>     CompactedCommands_{}, CompactedCounts_{}, ClassCommands_{};
    void RecordList(CommandSequence& Sequence, const std::shared_ptr<DeviceRegion>& Instances, uint32_t List);
    std::optional<std::string>        ActivationError_{};
    std::string                       StatusCaption_{};
    std::string                       Key_{};
    std::vector<ClusterRecord>        Clusters_{};
    std::set<uint32_t>                OwnedOccupants_{};
    std::vector<StreamingPage>        Pages_{};
    std::vector<RecordMirror>         RecordMirrors_{};
    std::vector<uint32_t>             PageLocalFirstIndices_{};
    uint32_t                          RecordCount_ = 0, SlotVertexCount_ = 1, SlotIndexCount_ = 1;
    // 📝 Beyond the demo (which never rebuilds at run time): after the draw runs change (an object shown, hidden, added or
    //    removed), every page streams in the next refresh instead of two per refresh, so the takeover shows no partial cut.
    uint32_t                          BurstLoads_ = 0;
    std::unique_ptr<PageSlotAllocator> Allocator_{};
    std::vector<uint32_t>             StreamQueue_{};
    int32_t                           RefreshCount_ = 0;   // [-] the demo's FrameCount
    std::shared_ptr<DeviceRegion>     ClusterRecords_{}, VertexRegion_{}, IndexRegion_{}, CornerPosRegion_{}, CornerUvRegion_{}, Phase1Commands_{},
                                      Phase2Commands_{}, Verdicts_{};
    std::shared_ptr<DescriptorGroup>  CullGroup_{};
};
} // namespace Frontier::VirtualizedGeometry
