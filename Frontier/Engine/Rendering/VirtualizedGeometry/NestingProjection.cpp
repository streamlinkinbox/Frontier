/*==========================================================================================================================================
                                                         NESTINGPROJECTION.CPP
==========================================================================================================================================*/
// 🧩 VirtualizedGeometry.js statement for statement: programs, targets, expanded set geometry, cluster records, page streaming,
//    the two cull phases around the Hi-Z pyramid, cluster commands, the visibility resolve and the cluster inspection views.
#include "NestingProjection.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>
#include <utility>
namespace Frontier::VirtualizedGeometry
{
using namespace Frontier::GlobalIllumination;
namespace
{
constexpr uint32_t RecordWords  = 24;   // [-] 96 B NestedClusterProfile (VirtualizedGeometryCull.slang)
constexpr uint32_t CommandBytes = 20;   // [B] ClusterCommand
constexpr uint32_t ResidentFlag = 8;    // [-]

[[nodiscard]] Point3 FaceNormal(const std::vector<double>& Positions, uint32_t A, uint32_t B, uint32_t C) noexcept
{
    const Point3 AB = {Positions[B * 3] - Positions[A * 3], Positions[B * 3 + 1] - Positions[A * 3 + 1], Positions[B * 3 + 2] - Positions[A * 3 + 2]};
    const Point3 AC = {Positions[C * 3] - Positions[A * 3], Positions[C * 3 + 1] - Positions[A * 3 + 1], Positions[C * 3 + 2] - Positions[A * 3 + 2]};
    const Point3 N  = {AB[1] * AC[2] - AB[2] * AC[1], AB[2] * AC[0] - AB[0] * AC[2], AB[0] * AC[1] - AB[1] * AC[0]};
    double L = JsHypot(N[0], N[1], N[2]);
    if (L == 0 || std::isnan(L)) L = 1;
    return {N[0] / L, N[1] / L, N[2] / L};
}
// 📝 FamilyFloat: the family's uint32 bits carried in a float32 lane.
[[nodiscard]] float FamilyFloat(uint32_t Family) noexcept { return std::bit_cast<float>(Family); }
[[nodiscard]] uint32_t FloatWord(double Value) noexcept { return std::bit_cast<uint32_t>(static_cast<float>(Value)); }

} // namespace

SurfaceSoup ConvertFacetSet(const FacetSet& Set)
{
    SurfaceSoup Soup;
    Soup.Facets.reserve(Set.Facets.size());
    for (const FacetEntry& Entry : Set.Facets)
    {
        SoupFacet Facet;
        Facet.Corners        = Entry.Corners;
        Facet.CarriesNormals = Entry.NormalsAuthored;
        Facet.Normals        = Entry.Normals;
        Facet.Family         = Entry.Family;
        Soup.Facets.push_back(Facet);
    }
    return Soup;
}

NestingArchive ConstructSceneNestingArchive(const SceneStructure& Scene, const NestingOptions& Options)
{
    NestingArchive Archive;
    Archive.Profile = NestingProfileBits(Options);
    std::vector<bool> Eligible(Scene.FacetSets.size(), false);
    for (const DrawRun& Run : Scene.DrawRuns)
    {
        bool Static = true;
        for (uint32_t Index = 0; Static && Index < Run.Count; ++Index)
            Static = (Scene.Occupants[Run.First + Index].Participation & OccupantParticipation::Dynamic) == 0;
        if (Static) Eligible[Run.FacetSet] = true;
    }
    for (uint32_t SetIndex = 0; SetIndex < Scene.FacetSets.size(); ++SetIndex)
    {
        const FacetSet& Set = Scene.FacetSets[SetIndex];
        if (!Eligible[SetIndex] || Set.Facets.size() < 256 || Set.VertexCapacity) continue;
        const SurfaceSoup Soup = ConvertFacetSet(Set);
        NestingArchiveEntry Entry;
        Entry.FacetSet   = SetIndex;
        Entry.FacetCount = static_cast<uint32_t>(Set.Facets.size());
        Entry.Digest     = DigestSurfaceSoup(Soup);
        Entry.Nesting    = ConstructGeometryNesting(Soup, Options);
        Archive.Entries.push_back(std::move(Entry));
    }
    return Archive;
}

NestingProjection::NestingProjection(DeviceExchange& Exchange, const ShaderInterchange& IlluminationShaders)
    : Exchange_(Exchange), Shaders_(Exchange, IlluminationShaders.SpirvFolder().parent_path() / "VirtualizedGeometry"),
      IlluminationShaders_(IlluminationShaders)
{
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PROGRAMS AND TARGETS
//------------------------------------------------------------------------------------------------------------------------

void NestingProjection::ConstructPrograms(const std::shared_ptr<DeviceRegion>& RefreshRegion, const std::shared_ptr<DeviceRegion>& OccupantRegion)
{
    // 📝 Refresh and occupant regions are kept even while inert so an inspection mode can activate the projection on demand.
    RefreshRegion_  = RefreshRegion;
    OccupantRegion_ = OccupantRegion;
    if (!Enabled) return;
    Shaders_.ConfigureConstants(IlluminationShaders_.Constants());
    using D = DescriptorCode;
    const std::vector<DescriptorSlot> CullSlots = {{D::Uniform}, {D::StorageRead}, {D::StorageRead}, {D::Storage}, {D::Storage}, {D::Storage},
                                                   {D::Texture2DUnfiltered}, {D::Storage}, {D::Storage}, {D::Storage}};
    CountedCommands_   = Exchange_.Capabilities().DrawIndirectCount;
    CompactionOffered_ = CountedCommands_ || Exchange_.Capabilities().MultiDrawIndirect;
    CullLayout_ = Exchange_.ConstructDescriptorLayout(CullSlots, "Virtualized geometry culling");
    const std::vector<DescriptorSlot> SeedSlots = {{D::TextureDepth2D}, {D::StorageTexture2D, TextureFormat::R32Float}};
    SeedLayout_ = Exchange_.ConstructDescriptorLayout(SeedSlots, "Hi-Z seed");
    const std::vector<DescriptorSlot> DownsampleSlots = {{D::Texture2DUnfiltered}, {D::StorageTexture2D, TextureFormat::R32Float}};
    DownsampleLayout_ = Exchange_.ConstructDescriptorLayout(DownsampleSlots, "Hi-Z downsample");
    auto ConstructDummy = [&](const char* Label, TextureFormat Format, uint32_t Usage) {
        TextureSpecification Specification;
        Specification.Label  = Label;
        Specification.Format = Format;
        Specification.Usage  = Usage;
        return Exchange_.ConstructTexture(Specification);
    };
    DummyHiZ_         = ConstructDummy("Hi-Z dummy", TextureFormat::R32Float, TextureUsage::Sampled | TextureUsage::Storage);
    DummyHiZView_     = Exchange_.ConstructView(DummyHiZ_);
    PreviousHiZView_  = DummyHiZView_;
    // 📝 Resolve-group slot fillers: a texture may occupy only one slot per group, so the slot an entry does not use gets a distinct
    //    1x1 dummy of matching kind (the demo's Dawn usage-scope rule, kept for parity).
    DummyStorageView_ = Exchange_.ConstructView(ConstructDummy("Resolve dummy (storage slot)", TextureFormat::Rgba16Float,
                                                               TextureUsage::Storage | TextureUsage::Sampled));
    DummySampledView_ = Exchange_.ConstructView(ConstructDummy("Resolve dummy (sampled slot)", TextureFormat::Rgba16Float, TextureUsage::Sampled));
    CullProgram_       = Shaders_.ConstructComputeProgram("VirtualizedGeometryCull", "SelectAndCullPhase1", {CullLayout_});
    Phase2Program_     = Shaders_.ConstructComputeProgram("VirtualizedGeometryCull", "CullPhase2Disoccluded", {CullLayout_});
    if (CompactionOffered_)
    {
        CompactedCullProgram_   = Shaders_.ConstructComputeProgram("VirtualizedGeometryCull", "SelectAndCullPhase1Compacted", {CullLayout_});
        CompactedPhase2Program_ = Shaders_.ConstructComputeProgram("VirtualizedGeometryCull", "CullPhase2DisoccludedCompacted", {CullLayout_});
    }
    SeedProgram_       = Shaders_.ConstructComputeProgram("HiZPyramid", "SeedHiZFromDepth", {SeedLayout_});
    DownsampleProgram_ = Shaders_.ConstructComputeProgram("HiZDownsample", "DownsampleHiZMip", {DownsampleLayout_});

    // Visibility raster (opaque and glass programs share one vertex entry; the glass back peel samples the front glass depth) and the
    // two deferred resolve entries.
    const std::vector<DescriptorSlot> VisibilitySlots = {{D::Uniform}, {D::StorageRead}, {D::Texture2D}, {D::Sampler}, {D::Uniform}, {D::StorageRead}};
    VisibilityLayout_ = Exchange_.ConstructDescriptorLayout(VisibilitySlots, "Visibility raster");
    const std::vector<DescriptorSlot> GlassBackSlots = {{D::TextureDepth2D}};
    GlassBackLayout_ = Exchange_.ConstructDescriptorLayout(GlassBackSlots, "Glass back depth");
    RasterProgramSpecification Visibility;
    Visibility.VertexEntry   = "VisibilityVertex";
    Visibility.VertexLayouts = {{VertexStride, false, {{0, 0, VertexFormat::Float32x3}, {1, 12, VertexFormat::Uint32}, {2, 16, VertexFormat::Float32x3},
                                                       {3, 28, VertexFormat::Uint32}}},
                                {4, true, {{4, 0, VertexFormat::Uint32}}}};
    Visibility.CullMode      = VK_CULL_MODE_NONE;
    Visibility.DepthEnabled  = true;
    Visibility.DepthCompare  = CompareFunction::Greater;
    Visibility.Targets       = {{TextureFormat::Rg32Uint}, {TextureFormat::Rg16Float}, {TextureFormat::Rg32Uint}, {TextureFormat::Rg16Float}};
    Visibility.Layouts       = {VisibilityLayout_};
    RasterProgramSpecification Opaque = Visibility;
    Opaque.FragmentEntry = "VisibilityFragment";
    Opaque.DepthWrite    = true;
    VisibilityOpaqueProgram_ = Shaders_.ConstructRasterProgram("VisibilityRaster", Opaque);
    RasterProgramSpecification Glass = Visibility;
    Glass.Label         = "VisibilityRaster:VisibilityGlassFragment";
    Glass.FragmentEntry = "VisibilityGlassFragment";
    Glass.DepthWrite    = false;
    VisibilityGlassProgram_ = Shaders_.ConstructRasterProgram("VisibilityRaster", Glass);
    RasterProgramSpecification GlassBack = Visibility;
    GlassBack.Label         = "VisibilityRaster:VisibilityGlassBackFragment";
    GlassBack.FragmentEntry = "VisibilityGlassBackFragment";
    GlassBack.DepthWrite    = true;
    GlassBack.Targets       = {{TextureFormat::Rg32Uint}, {TextureFormat::Rg16Float}};
    GlassBack.Layouts       = {VisibilityLayout_, GlassBackLayout_};
    VisibilityGlassBackProgram_ = Shaders_.ConstructRasterProgram("VisibilityRaster", GlassBack);
    const std::vector<DescriptorSlot> ResolveSlots = {{D::Uniform}, {D::StorageRead}, {D::StorageRead}, {D::StorageRead}, {D::TextureUint2D}, {D::Texture2D},
                                                      {D::StorageTexture2D, TextureFormat::Rgba16Float}, {D::TextureUint2D}, {D::Texture2D},
                                                      {D::TextureUint2D}, {D::Texture2D}, {D::StorageTexture2D, TextureFormat::Rgba16Float},
                                                      {D::Texture2D}, {D::Texture2DUnfiltered}};
    ResolveLayout_ = Exchange_.ConstructDescriptorLayout(ResolveSlots, "Material resolve");
    ResolveOpaqueProgram_     = Shaders_.ConstructComputeProgram("MaterialResolve", "ResolveOpaqueVisibility", {ResolveLayout_});
    ResolveRefractionProgram_ = Shaders_.ConstructComputeProgram("MaterialResolve", "ResolveRefractionGather", {ResolveLayout_});
    InspectIdentitiesProgram_ = Shaders_.ConstructComputeProgram("MaterialResolve", "DebugVisibilityIds", {ResolveLayout_});
    InspectGlassProgram_      = Shaders_.ConstructComputeProgram("MaterialResolve", "DebugGlassLayers", {ResolveLayout_});
    InspectCopyProgram_       = Shaders_.ConstructComputeProgram("MaterialResolve", "DebugCopyResolved", {ResolveLayout_});
    InspectHiZProgram_        = Shaders_.ConstructComputeProgram("MaterialResolve", "DebugHiZView", {ResolveLayout_});
    InspectTiersProgram_      = Shaders_.ConstructComputeProgram("MaterialResolve", "DebugLodLevels", {ResolveLayout_});
    ProgramsBuilt_ = true;
}

// 📝 Inspection state for the fallback views: "activating" (magenta) → "ready" | "failed" (red) | "no-geometry" (orange).
std::string NestingProjection::InspectionState() const
{
    if (ActivationError_) return "failed";
    if (ProgramsBuilt_ && OpaqueVisTexture_ && !Records_) return "no-geometry";
    if (ProgramsBuilt_ && OpaqueVisTexture_ && Records_) return "ready";
    return "activating";
}

void NestingProjection::ReportInspectionStatus()
{
    const std::string State = InspectionState();
    StatusCaption_ = State == "failed"        ? "Cluster inspection: activation failed — " + *ActivationError_
                     : State == "no-geometry" ? "Cluster inspection: no eligible facet set in scene (needs >= 256 facets, static, non-breakable occupants" +
                                                    (ArchiveCaption_.empty() ? std::string(")") : "; " + ArchiveCaption_ + ")")
                     : State == "ready"       ? std::string()
                                              : "Cluster inspection: activating";
}

void NestingProjection::ActivateForInspection()
{
    if (ProgramsBuilt_ && OpaqueVisTexture_) return;
    Enabled = true;
    ActivationError_.reset();
    try
    {
        if (!FallbackLayout_)
        {
            using D = DescriptorCode;
            Shaders_.ConfigureConstants(IlluminationShaders_.Constants());
            const std::vector<DescriptorSlot> FallbackSlots = {{D::StorageTexture2D, TextureFormat::Rgba16Float}};
            FallbackLayout_     = Exchange_.ConstructDescriptorLayout(FallbackSlots, "Cluster inspection fallback");
            FallbackUnavailable_ = Shaders_.ConstructComputeProgram("ClusterDebugFallback", "DebugUnavailable", {FallbackLayout_});
            FallbackFailed_      = Shaders_.ConstructComputeProgram("ClusterDebugFallback", "DebugFailed", {FallbackLayout_});
            FallbackNoGeometry_  = Shaders_.ConstructComputeProgram("ClusterDebugFallback", "DebugNoGeometry", {FallbackLayout_});
        }
        if (!ProgramsBuilt_) ConstructPrograms(RefreshRegion_, OccupantRegion_);
        if (TargetExtent_ && !OpaqueVisTexture_) ConstructTargets((*TargetExtent_)[0], (*TargetExtent_)[1]);
    }
    catch (const std::exception& Error)
    {
        ActivationError_ = Error.what();
    }
    ReportInspectionStatus();
}

void NestingProjection::RecordUnavailableView(CommandSequence& Sequence, const std::shared_ptr<DeviceTextureView>& TargetView)
{
    ReportInspectionStatus();
    const std::string State = InspectionState();
    const std::shared_ptr<DeviceProgram>& Program = State == "failed" ? FallbackFailed_ : State == "no-geometry" ? FallbackNoGeometry_ : FallbackUnavailable_;
    if (!Program || !TargetView) return;
    const auto Group = Exchange_.ConstructDescriptorGroup(FallbackLayout_, {TargetView}, "Cluster inspection (" + State + ")");
    Sequence.BindProgram(Program);
    Sequence.BindGroup(0, Group);
    const std::array<uint32_t, 2> Size = TargetExtent_.value_or(std::array<uint32_t, 2>{1920, 1080});
    Sequence.Dispatch((Size[0] + 7) / 8, (Size[1] + 7) / 8);
}

void NestingProjection::ConstructTargets(uint32_t Width, uint32_t Height)
{
    TargetExtent_ = std::array<uint32_t, 2>{Width, Height};   // remembered while inert so an inspection mode can size targets later
    if (!Enabled) return;
    auto Construct = [&](const std::string& Label, uint32_t W, uint32_t H, TextureFormat Format, uint32_t Usage) {
        TextureSpecification Specification;
        Specification.Label  = Label;
        Specification.Width  = W;
        Specification.Height = H;
        Specification.Format = Format;
        Specification.Usage  = Usage;
        return Exchange_.ConstructTexture(Specification);
    };
    HiZTextures_.clear();
    HiZViews_.clear();
    HiZGroups_.clear();
    SeededDepthView_.reset();
    uint32_t W = Width, H = Height;
    while (true)
    {
        HiZTextures_.push_back(Construct("Hi-Z mip " + std::to_string(HiZTextures_.size()), W, H, TextureFormat::R32Float,
                                         TextureUsage::Sampled | TextureUsage::Storage));
        HiZViews_.push_back(Exchange_.ConstructView(HiZTextures_.back()));
        if (W == 1 && H == 1) break;
        W = std::max<uint32_t>(1, W >> 1);
        H = std::max<uint32_t>(1, H >> 1);
    }
    PreviousHiZView_ = DummyHiZView_;
    CurrentHiZView_.reset();
    Extent_ = {Width, Height};
    // Visibility identities and barycentrics per layer, plus the resolved colour outputs.
    const uint32_t VisUsage = TextureUsage::RenderAttachment | TextureUsage::Sampled;
    OpaqueVisTexture_      = Construct("Opaque visibility", Width, Height, TextureFormat::Rg32Uint, VisUsage);
    OpaqueBaryTexture_     = Construct("Opaque barycentrics", Width, Height, TextureFormat::Rg16Float, VisUsage);
    GlassVisTexture_       = Construct("Glass visibility", Width, Height, TextureFormat::Rg32Uint, VisUsage);
    GlassBaryTexture_      = Construct("Glass barycentrics", Width, Height, TextureFormat::Rg16Float, VisUsage);
    GlassBackVisTexture_   = Construct("Glass back visibility", Width, Height, TextureFormat::Rg32Uint, VisUsage);
    GlassBackBaryTexture_  = Construct("Glass back barycentrics", Width, Height, TextureFormat::Rg16Float, VisUsage);
    VisDepthTexture_       = Construct("Visibility depth", Width, Height, TextureFormat::Depth32Float, VisUsage);
    GlassBackDepthTexture_ = Construct("Glass back depth", Width, Height, TextureFormat::Depth32Float, TextureUsage::RenderAttachment);
    const uint32_t ResolveUsage = TextureUsage::Storage | TextureUsage::Sampled;
    OpaqueResolvedTexture_ = Construct("Opaque resolved", Width, Height, TextureFormat::Rgba16Float, ResolveUsage);
    FinalResolvedTexture_  = Construct("Final resolved", Width, Height, TextureFormat::Rgba16Float, ResolveUsage);
    OpaqueVisView_      = Exchange_.ConstructView(OpaqueVisTexture_);
    OpaqueBaryView_     = Exchange_.ConstructView(OpaqueBaryTexture_);
    GlassVisView_       = Exchange_.ConstructView(GlassVisTexture_);
    GlassBaryView_      = Exchange_.ConstructView(GlassBaryTexture_);
    GlassBackVisView_   = Exchange_.ConstructView(GlassBackVisTexture_);
    GlassBackBaryView_  = Exchange_.ConstructView(GlassBackBaryTexture_);
    VisDepthView_       = Exchange_.ConstructView(VisDepthTexture_);
    GlassBackDepthView_ = Exchange_.ConstructView(GlassBackDepthTexture_);
    OpaqueResolvedView_ = Exchange_.ConstructView(OpaqueResolvedTexture_);
    FinalResolvedView_  = Exchange_.ConstructView(FinalResolvedTexture_);
    GlassBackGroup_     = Exchange_.ConstructDescriptorGroup(GlassBackLayout_, {VisDepthView_}, "Glass back depth");
    CullGroups_.clear();
    ResolveGroups_.clear();
    CullGroup_.reset();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SET GEOMETRY AND RECORDS
//------------------------------------------------------------------------------------------------------------------------

void NestingProjection::AssignArchive(const std::filesystem::path& Path)
{
    std::string Diagnostic;
    Archive_ = ReadNestingArchive(Path, &Diagnostic);
    ArchiveCaption_ = Archive_ ? std::string() : Diagnostic;
    if (Archive_ && Archive_->Profile != NestingProfileBits(RefinedNestingOptions()))
    {
        ArchiveCaption_ = "the nesting archive was built with another refinement profile; rerun NestingArchiveExport";
        Archive_.reset();
    }
    Retained_.clear();
    Key_.clear();
}

// 📝 The demo's BuildSet, with the nesting loaded from the stored archive (I2) instead of built here.
NestingProjection::ExpandedSet* NestingProjection::BuildSet(uint32_t FacetSetIndex, const FacetSet& Set)
{
    auto Hit = Retained_.find(FacetSetIndex);
    if (Hit != Retained_.end() && Hit->second.FacetCount == Set.Facets.size()) return &Hit->second;
    const NestingArchiveEntry* Stored = nullptr;
    if (Archive_)
        for (const NestingArchiveEntry& Entry : Archive_->Entries)
            if (Entry.FacetSet == FacetSetIndex && Entry.FacetCount == Set.Facets.size())
            {
                Stored = &Entry;
                break;
            }
    if (!Stored || Stored->Digest != DigestSurfaceSoup(ConvertFacetSet(Set)))
    {
        if (ArchiveCaption_.empty())
            ArchiveCaption_ = "facet set " + std::to_string(FacetSetIndex) + " has no current stored nesting; rerun NestingArchiveExport";
        return nullptr;
    }
    ExpandedSet Built;
    Built.FacetCount = Set.Facets.size();
    Built.Nesting    = Stored->Nesting;
    for (const NestingTier& Tier : Built.Nesting.Tiers)
        for (uint32_t ClusterIdentity : Tier.Clusters)
        {
            const NestedCluster& Node = Built.Nesting.Clusters[ClusterIdentity];
            const uint32_t BaseVertex = static_cast<uint32_t>(Built.Vertices.size() / 8), FirstIndex = static_cast<uint32_t>(Built.Indices.size());
            for (uint32_t Triangle : Node.Triangles)
            {
                const std::array<uint32_t, 3> Corner = {Tier.Triangles[Triangle * 3], Tier.Triangles[Triangle * 3 + 1], Tier.Triangles[Triangle * 3 + 2]};
                const Point3 Normal   = FaceNormal(Tier.Positions, Corner[0], Corner[1], Corner[2]);
                const float  FamFloat = FamilyFloat(Tier.Families[Triangle]);
                for (uint32_t C = 0; C < 3; ++C)
                {
                    Built.Indices.push_back(static_cast<uint32_t>(Built.Vertices.size() / 8));
                    const Point3 P = Tier.ReadPosition(Corner[C]);
                    for (double Value : {P[0], P[1], P[2]}) Built.Vertices.push_back(static_cast<float>(Value));
                    Built.Vertices.push_back(FamFloat);
                    for (double Value : {Normal[0], Normal[1], Normal[2]}) Built.Vertices.push_back(static_cast<float>(Value));
                    Built.Vertices.push_back(0.0f);
                    for (double Value : {P[0], P[1], P[2]}) Built.CornerPos.push_back(static_cast<float>(Value));
                    Built.CornerPos.push_back(FamFloat);
                    Built.CornerUv.push_back(static_cast<float>(Tier.CornerUvs[static_cast<size_t>(Triangle) * 6 + C * 2]));
                    Built.CornerUv.push_back(static_cast<float>(Tier.CornerUvs[static_cast<size_t>(Triangle) * 6 + C * 2 + 1]));
                    Built.CornerUv.push_back(static_cast<float>(Node.Tier));
                    Built.CornerUv.push_back(0.0f);
                }
            }
            Built.Clusters.push_back({ClusterIdentity, FirstIndex, BaseVertex, Node.TriangleCount * 3, Node.TriangleCount * 3});
        }
    return &Retained_.insert_or_assign(FacetSetIndex, std::move(Built)).first->second;
}

NestingProjection* NestingProjection::Prepare(const SceneStructure& Scene)
{
    // 📝 Enabling at run time (the levelspace key) builds the programs the demo builds at start-up behind ?hierarchy=1.
    if (Enabled && !ProgramsBuilt_ && !ActivationError_) ActivateForInspection();
    if (!Enabled || !ProgramsBuilt_) return nullptr;
    std::string Key;
    for (const DrawRun& Run : Scene.DrawRuns)
        Key += (Key.empty() ? "" : "|") + std::to_string(Run.FacetSet) + ":" + std::to_string(Run.First) + ":" + std::to_string(Run.Count);
    if (Key == Key_ && Records_) return this;
    const bool Rebuilt = !Key_.empty();
    Key_ = Key;
    Clusters_.clear();
    OwnedOccupants_.clear();
    Pages_.clear();
    RecordMirrors_.clear();
    PageLocalFirstIndices_.clear();
    CullGroups_.clear();
    ResolveGroups_.clear();
    CullGroup_.reset();
    Records_ = false;

    for (const DrawRun& Run : Scene.DrawRuns)
    {
        const FacetSet& Set = Scene.FacetSets[Run.FacetSet];
        bool Eligible = Set.Facets.size() >= 256 && !Set.VertexCapacity;
        for (uint32_t Index = 0; Eligible && Index < Run.Count; ++Index)
            Eligible = (Scene.Occupants[Run.First + Index].Participation & OccupantParticipation::Dynamic) == 0;
        if (!Eligible) continue;
        ExpandedSet* Expanded = BuildSet(Run.FacetSet, Set);
        if (!Expanded) continue;
        ExpandedSet& Built = *Expanded;
        const std::vector<NestingPage> SetPages = PackNestingPages(Built.Nesting);
        std::map<uint32_t, const ExpandedCluster*> RecordByCluster;
        for (const ExpandedCluster& Record : Built.Clusters) RecordByCluster[Record.ClusterIdentity] = &Record;
        std::vector<uint32_t> GlobalPageOf(Built.Nesting.Clusters.size(), 0);
        // Per page payload: the concatenated expanded vertices, indices and corner data of the page's clusters.
        for (const NestingPage& Page : SetPages)
        {
            StreamingPage Streaming;
            Streaming.GlobalPage = static_cast<uint32_t>(Pages_.size());
            Streaming.Pinned     = Page.Pinned;
            for (uint32_t ClusterIdentity : Page.Clusters)
            {
                const ExpandedCluster& Record = *RecordByCluster.at(ClusterIdentity);
                const uint32_t LocalFirstIndex = static_cast<uint32_t>(Streaming.Indices.size());
                const uint32_t LocalBaseVertex = static_cast<uint32_t>(Streaming.Vertices.size() / 8);
                for (uint32_t I = 0; I < Record.VertexCount; ++I)
                {
                    const size_t Source = static_cast<size_t>(Record.BaseVertex + I);
                    Streaming.Vertices.insert(Streaming.Vertices.end(), Built.Vertices.begin() + Source * 8, Built.Vertices.begin() + Source * 8 + 8);
                    Streaming.CornerPos.insert(Streaming.CornerPos.end(), Built.CornerPos.begin() + Source * 4, Built.CornerPos.begin() + Source * 4 + 4);
                    Streaming.CornerUv.insert(Streaming.CornerUv.end(), Built.CornerUv.begin() + Source * 4, Built.CornerUv.begin() + Source * 4 + 4);
                }
                for (uint32_t I = 0; I < Record.IndexCount; ++I)
                    Streaming.Indices.push_back(Built.Indices[Record.FirstIndex + I] - Record.BaseVertex + LocalBaseVertex);
                Streaming.LocalFirstIndex[ClusterIdentity] = LocalFirstIndex;
            }
            for (uint32_t ClusterIdentity : Page.Clusters) GlobalPageOf[ClusterIdentity] = Streaming.GlobalPage;
            Pages_.push_back(std::move(Streaming));
        }
        for (uint32_t O = Run.First; O < Run.First + Run.Count; ++O)
        {
            OwnedOccupants_.insert(O);
            for (const ExpandedCluster& Record : Built.Clusters)
            {
                const NestedCluster& Node = Built.Nesting.Clusters[Record.ClusterIdentity];
                StreamingPage& Page = Pages_[GlobalPageOf[Record.ClusterIdentity]];
                RecordMirror Words{};
                for (int Axis = 0; Axis < 3; ++Axis) Words[Axis] = FloatWord(Node.TightMinimum[Axis]);
                Words[3] = O;
                for (int Axis = 0; Axis < 3; ++Axis) Words[4 + Axis] = FloatWord(Node.TightMaximum[Axis]);
                Words[7] = Page.LocalFirstIndex.at(Record.ClusterIdentity);   // slot offset added on load
                for (int Axis = 0; Axis < 3; ++Axis) Words[8 + Axis] = FloatWord(Node.Sphere.Center[Axis]);
                Words[11] = FloatWord(Node.Sphere.Radius);
                for (int Axis = 0; Axis < 3; ++Axis) Words[12 + Axis] = FloatWord(Node.CoarserSphere.Center[Axis]);
                Words[15] = FloatWord(Node.CoarserSphere.Radius);
                for (int Axis = 0; Axis < 3; ++Axis) Words[16 + Axis] = FloatWord(Node.Cone.Axis[Axis]);
                Words[19] = FloatWord(Node.Cone.Cutoff);
                Words[20] = FloatWord(Node.Error);
                Words[21] = FloatWord(std::isfinite(Node.CoarserError) ? Node.CoarserError : 1e30);
                Words[22] = Record.IndexCount;
                Words[23] = Node.ClassMask | (Node.Cone.Cutoff > -1 ? 4u : 0u) | (Node.Tier << 8);   // resident bit set on load
                const uint32_t Index = static_cast<uint32_t>(Clusters_.size());
                Clusters_.push_back({Index, O, Page.GlobalPage, Node.ClassMask});
                PageLocalFirstIndices_.push_back(Page.LocalFirstIndex.at(Record.ClusterIdentity));
                Page.ClusterRecords.push_back(Index);
                RecordMirrors_.push_back(Words);
            }
        }
    }
    RecordCount_ = static_cast<uint32_t>(Clusters_.size());
    if (!RecordCount_) return this;

    // Arena slots: every page streams through SlotCount slots sized to the largest page payload.
    SlotVertexCount_ = 1;
    SlotIndexCount_  = 1;
    for (const StreamingPage& Page : Pages_)
    {
        SlotVertexCount_ = std::max<uint32_t>(SlotVertexCount_, static_cast<uint32_t>(Page.Vertices.size() / 8));
        SlotIndexCount_  = std::max<uint32_t>(SlotIndexCount_, static_cast<uint32_t>(Page.Indices.size()));
    }
    const uint32_t PageCount = static_cast<uint32_t>(Pages_.size());
    const uint32_t SlotCount = std::max<uint32_t>(1, std::min<uint32_t>(StreamSlots > 0 ? StreamSlots : PageCount, PageCount));
    Allocator_ = std::make_unique<PageSlotAllocator>(SlotCount);
    for (const StreamingPage& Page : Pages_)
        if (Page.Pinned) Allocator_->Pin(Page.GlobalPage);
    StreamQueue_.clear();
    for (const StreamingPage& Page : Pages_) StreamQueue_.push_back(Page.GlobalPage);
    BurstLoads_ = Rebuilt ? PageCount : 0u;

    auto Make = [&](VkDeviceSize Bytes, const std::string& Label) { return Exchange_.ConstructRegion(std::max<VkDeviceSize>(4, Bytes), Label); };
    ClusterRecords_ = Make(static_cast<VkDeviceSize>(RecordCount_) * RecordWords * 4, "Nesting cluster records");
    Exchange_.WriteRegion(ClusterRecords_, 0, RecordMirrors_.data(), static_cast<VkDeviceSize>(RecordCount_) * RecordWords * 4);
    VertexRegion_    = Make(static_cast<VkDeviceSize>(SlotVertexCount_) * SlotCount * 32, "Nesting arena vertices");
    IndexRegion_     = Make(static_cast<VkDeviceSize>(SlotIndexCount_) * SlotCount * 4, "Nesting arena indices");
    CornerPosRegion_ = Make(static_cast<VkDeviceSize>(SlotVertexCount_) * SlotCount * 16, "Nesting corner positions");
    CornerUvRegion_  = Make(static_cast<VkDeviceSize>(SlotVertexCount_) * SlotCount * 16, "Nesting corner UVs");
    Phase1Commands_  = Make(static_cast<VkDeviceSize>(RecordCount_) * CommandBytes, "Nesting phase-1 commands");
    Phase2Commands_  = Make(static_cast<VkDeviceSize>(RecordCount_) * CommandBytes, "Nesting phase-2 commands");
    Verdicts_        = Make(static_cast<VkDeviceSize>(RecordCount_) * 4, "Nesting cluster verdicts");
    CompactedCommands_ = Make(static_cast<VkDeviceSize>(RecordCount_) * 4 * CommandBytes, "Nesting compacted commands");
    CompactedCounts_   = Make(16, "Nesting compacted counts");
    ClassCommands_     = Make(static_cast<VkDeviceSize>(RecordCount_) * 4 * CommandBytes, "Nesting class commands");
    Records_ = true;
    RefreshGroups();
    return this;
}

// 📝 Streams up to MaxLoads queued pages into arena slots, patching cluster records (first index and resident bit) and clearing the
//    records of any evicted page so the cut falls back to resident coarser clusters.
void NestingProjection::ServiceStreaming(uint32_t MaxLoads)
{
    if (!Records_ || StreamQueue_.empty()) return;
    uint32_t Loads = 0;
    auto Upload = [&](uint32_t Record) {
        Exchange_.WriteRegion(ClusterRecords_, static_cast<VkDeviceSize>(Record) * RecordWords * 4, RecordMirrors_[Record].data(), RecordWords * 4);
    };
    while (Loads < MaxLoads && !StreamQueue_.empty())
    {
        const uint32_t PageIndex = StreamQueue_.front();
        if (Allocator_->IsResident(PageIndex))
        {
            StreamQueue_.erase(StreamQueue_.begin());
            continue;
        }
        const PageSlotAllocator::Placement Result = Allocator_->Ensure(PageIndex, RefreshCount_);
        if (Result.Slot < 0) break;   // every slot pinned: wait for eviction pressure to change
        StreamQueue_.erase(StreamQueue_.begin());
        const StreamingPage& Page = Pages_[PageIndex];
        if (Result.EvictedPage && *Result.EvictedPage >= 0)
            for (uint32_t Record : Pages_[*Result.EvictedPage].ClusterRecords)
            {
                RecordMirrors_[Record][23] &= ~ResidentFlag;
                Upload(Record);
            }
        const uint32_t VertexBase = static_cast<uint32_t>(Result.Slot) * SlotVertexCount_, IndexBase = static_cast<uint32_t>(Result.Slot) * SlotIndexCount_;
        Exchange_.WriteRegion(VertexRegion_, static_cast<VkDeviceSize>(VertexBase) * 32, Page.Vertices.data(), Page.Vertices.size() * 4);
        Exchange_.WriteRegion(CornerPosRegion_, static_cast<VkDeviceSize>(VertexBase) * 16, Page.CornerPos.data(), Page.CornerPos.size() * 4);
        Exchange_.WriteRegion(CornerUvRegion_, static_cast<VkDeviceSize>(VertexBase) * 16, Page.CornerUv.data(), Page.CornerUv.size() * 4);
        std::vector<uint32_t> Adjusted(Page.Indices.size());
        for (size_t I = 0; I < Page.Indices.size(); ++I) Adjusted[I] = Page.Indices[I] + VertexBase;
        if (!Adjusted.empty()) Exchange_.WriteRegion(IndexRegion_, static_cast<VkDeviceSize>(IndexBase) * 4, Adjusted.data(), Adjusted.size() * 4);
        for (uint32_t Record : Page.ClusterRecords)
        {
            // The first index was stored page-local at build time; add the slot's index base and set resident.
            RecordMirrors_[Record][7] = IndexBase + PageLocalFirstIndices_[Record];
            RecordMirrors_[Record][23] |= ResidentFlag;
            Upload(Record);
        }
        ++Loads;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     CULL, HI-Z AND CLUSTER COMMANDS
//------------------------------------------------------------------------------------------------------------------------

std::shared_ptr<DescriptorGroup> NestingProjection::CullGroupFor(const std::shared_ptr<DeviceTextureView>& HiZView)
{
    const std::pair<const void*, const void*> Key = {ClusterRecords_.get(), HiZView.get()};
    auto Hit = CullGroups_.find(Key);
    if (Hit != CullGroups_.end()) return Hit->second;
    auto Group = Exchange_.ConstructDescriptorGroup(CullLayout_, {RefreshRegion_, OccupantRegion_, ClusterRecords_, Phase1Commands_, Phase2Commands_, Verdicts_, HiZView,
                                                                  CompactedCommands_, CompactedCounts_, ClassCommands_},
                                                    "Virtualized geometry culling");
    return CullGroups_.emplace(Key, Group).first->second;
}

void NestingProjection::RefreshGroups()
{
    // 📝 An incomplete group means the cull keeps the previous refresh's group for now.
    if (!Records_ || !ClusterRecords_ || !PreviousHiZView_) return;
    CullGroup_ = CullGroupFor(PreviousHiZView_);
}

void NestingProjection::RecordCullPhase1(CommandSequence& Sequence)
{
    if (!Records_) return;
    ServiceStreaming(BurstLoads_ > 0 ? std::exchange(BurstLoads_, 0u) : 2u);
    RefreshGroups();
    if (!CullGroup_) return;
    RecordedCommands_     = 0;
    RecordedMilliseconds_ = 0.0;
    Compacted_            = CompactCommands && CompactionOffered_;
    if (Compacted_ && CountedCommands_)
    {
        const std::array<uint32_t, 4> Zeros{};
        Exchange_.WriteRegion(CompactedCounts_, 0, Zeros.data(), sizeof(Zeros));
    }
    Sequence.BindProgram(Compacted_ ? CompactedCullProgram_ : CullProgram_);
    Sequence.BindGroup(0, CullGroup_);
    Sequence.Dispatch((RecordCount_ + 63) / 64);
}

void NestingProjection::RecordHiZ(CommandSequence& Sequence, const std::shared_ptr<DeviceTextureView>& DepthView)
{
    if (!Records_ || HiZViews_.empty()) return;
    if (SeededDepthView_ != DepthView || HiZGroups_.empty())
    {
        HiZGroups_.clear();
        HiZGroups_.push_back(Exchange_.ConstructDescriptorGroup(SeedLayout_, {DepthView, HiZViews_[0]}, "Hi-Z seed"));
        for (size_t Mip = 1; Mip < HiZViews_.size(); ++Mip)
            HiZGroups_.push_back(Exchange_.ConstructDescriptorGroup(DownsampleLayout_, {HiZViews_[Mip - 1], HiZViews_[Mip]}, "Hi-Z downsample " + std::to_string(Mip)));
        SeededDepthView_ = DepthView;
    }
    Sequence.BindProgram(SeedProgram_);
    Sequence.BindGroup(0, HiZGroups_[0]);
    Sequence.Dispatch((Extent_[0] + 7) / 8, (Extent_[1] + 7) / 8);
    for (size_t Mip = 1; Mip < HiZViews_.size(); ++Mip)
    {
        const uint32_t W = std::max<uint32_t>(1, Extent_[0] >> Mip), H = std::max<uint32_t>(1, Extent_[1] >> Mip);
        Sequence.BindProgram(DownsampleProgram_);
        Sequence.BindGroup(0, HiZGroups_[Mip]);
        Sequence.Dispatch((W + 7) / 8, (H + 7) / 8);
    }
    CurrentHiZView_ = HiZViews_[0];
}

void NestingProjection::RecordCullPhase2(CommandSequence& Sequence)
{
    if (!Records_) return;
    const std::shared_ptr<DeviceTextureView>& HiZView = CurrentHiZView_ ? CurrentHiZView_ : PreviousHiZView_;
    Sequence.BindProgram(Compacted_ ? CompactedPhase2Program_ : Phase2Program_);
    Sequence.BindGroup(0, CullGroupFor(HiZView));
    Sequence.Dispatch((RecordCount_ + 63) / 64);
    PreviousHiZView_ = HiZView;
    ++RefreshCount_;
}

void NestingProjection::RecordClusters(CommandSequence& Sequence, const std::shared_ptr<DeviceRegion>& Instances, uint32_t Phase)
{
    if (!Records_) return;
    const auto Start = std::chrono::steady_clock::now();
    const std::shared_ptr<DeviceRegion>& Commands = Phase == 2 ? Phase2Commands_ : Phase1Commands_;
    Sequence.BindVertexRegion(0, VertexRegion_);
    Sequence.BindIndexRegion(IndexRegion_);
    if (Compacted_)
    {
        // 📝 I1: both classes of this phase in two calls; the occupant arrives as FirstInstance over the identity instance stream.
        for (uint32_t List : {Phase == 2 ? 2u : 0u, Phase == 2 ? 3u : 1u}) RecordList(Sequence, Instances, List);
    }
    else
    {
        RecordedCommands_ += RecordCount_;
        for (const ClusterRecord& Cluster : Clusters_)
        {
            Sequence.BindVertexRegion(1, Instances, static_cast<VkDeviceSize>(Cluster.Occupant) * 4);
            Sequence.DrawIndexedIndirect(Commands, static_cast<VkDeviceSize>(Cluster.Record) * CommandBytes);
        }
    }
    RecordedMilliseconds_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     VISIBILITY RESOLVE AND INSPECTION
//------------------------------------------------------------------------------------------------------------------------

void NestingProjection::RecordClass(CommandSequence& Sequence, const std::shared_ptr<DeviceRegion>& Commands, const std::shared_ptr<DeviceRegion>& Instances,
                                    bool Transmissive)
{
    const auto Start = std::chrono::steady_clock::now();
    Sequence.BindVertexRegion(0, VertexRegion_);
    Sequence.BindIndexRegion(IndexRegion_);
    if (Compacted_)
        RecordList(Sequence, Instances, (Commands == Phase2Commands_ ? 2u : 0u) + (Transmissive ? 1u : 0u));
    else
        for (const ClusterRecord& Cluster : Clusters_)
        {
            if (((Cluster.ClassMask & 2u) != 0) != Transmissive) continue;
            ++RecordedCommands_;
            Sequence.BindVertexRegion(1, Instances, static_cast<VkDeviceSize>(Cluster.Occupant) * 4);
            Sequence.DrawIndexedIndirect(Commands, static_cast<VkDeviceSize>(Cluster.Record) * CommandBytes);
        }
    RecordedMilliseconds_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
}

void NestingProjection::RecordList(CommandSequence& Sequence, const std::shared_ptr<DeviceRegion>& Instances, uint32_t List)
{
    Sequence.BindVertexRegion(1, Instances, 0);
    const VkDeviceSize Offset = static_cast<VkDeviceSize>(List) * RecordCount_ * CommandBytes;
    if (CountedCommands_)
        Sequence.DrawIndexedIndirectCount(CompactedCommands_, Offset, CompactedCounts_, List * 4, RecordCount_, CommandBytes);
    else
        Sequence.DrawIndexedIndirectSeries(ClassCommands_, Offset, RecordCount_, CommandBytes);
    ++RecordedCommands_;
}

std::shared_ptr<DescriptorGroup> NestingProjection::ResolveGroupFor(const std::shared_ptr<DeviceTextureView>& Slot6, const std::shared_ptr<DeviceTextureView>& Slot11,
                                                                    const std::shared_ptr<DeviceTextureView>& Slot12)
{
    const std::shared_ptr<DeviceTextureView>& HiZView = CurrentHiZView_ ? CurrentHiZView_ : PreviousHiZView_;
    const std::array<const void*, 3> Key = {Slot6.get(), Slot11.get(), Slot12.get()};
    auto Hit = ResolveGroups_.find(Key);
    if (Hit != ResolveGroups_.end() && Hit->second->Resources[13].View == HiZView && Hit->second->Resources[2].Region == CornerPosRegion_) return Hit->second;
    auto Group = Exchange_.ConstructDescriptorGroup(ResolveLayout_, {RefreshRegion_, OccupantRegion_, CornerPosRegion_, CornerUvRegion_, OpaqueVisView_, OpaqueBaryView_,
                                                                     Slot6, GlassVisView_, GlassBaryView_, GlassBackVisView_, GlassBackBaryView_, Slot11, Slot12,
                                                                     HiZView},
                                                    "Material resolve");
    return ResolveGroups_.insert_or_assign(Key, Group).first->second;
}

// 📝 Visibility raster and deferred resolve over both phases' commands: opaque records write identity and depth, transmissive
//    records depth-test without writing, a peel captures the glass back layer, then two compute entries resolve material and
//    refraction into the opaque and final resolved textures (read by the cluster inspection views).
void NestingProjection::RecordVisibilityResolve(CommandSequence& Sequence, const std::shared_ptr<DescriptorGroup>& RasterGroup,
                                                const std::shared_ptr<DeviceRegion>& Instances)
{
    if (!Records_ || !OpaqueVisTexture_) return;
    const std::array<ColorAttachment, 4> Layers = {{{OpaqueVisView_}, {OpaqueBaryView_}, {GlassVisView_}, {GlassBaryView_}}};
    const DepthAttachment Depth{VisDepthView_, LoadOperation::Clear, 0.0f};
    Sequence.BeginRaster(Layers, &Depth, "Visibility raster (opaque + glass front)");
    Sequence.BindProgram(VisibilityOpaqueProgram_);
    Sequence.BindGroup(0, RasterGroup);
    RecordClass(Sequence, Phase1Commands_, Instances, false);
    RecordClass(Sequence, Phase2Commands_, Instances, false);
    Sequence.BindProgram(VisibilityGlassProgram_);
    Sequence.BindGroup(0, RasterGroup);
    RecordClass(Sequence, Phase1Commands_, Instances, true);
    RecordClass(Sequence, Phase2Commands_, Instances, true);
    Sequence.EndRaster();
    const std::array<ColorAttachment, 2> BackLayers = {{{GlassBackVisView_}, {GlassBackBaryView_}}};
    const DepthAttachment BackDepth{GlassBackDepthView_, LoadOperation::Clear, 0.0f};
    Sequence.BeginRaster(BackLayers, &BackDepth, "Visibility raster (glass back peel)");
    Sequence.BindProgram(VisibilityGlassBackProgram_);
    Sequence.BindGroup(0, RasterGroup);
    Sequence.BindGroup(1, GlassBackGroup_);
    RecordClass(Sequence, Phase1Commands_, Instances, true);
    RecordClass(Sequence, Phase2Commands_, Instances, true);
    Sequence.EndRaster();
    // 📝 Two groups, identical except slots 6 and 12: the opaque resolve writes slot 6 and must not also see it sampled; the refraction
    //    gather reads slot 12 and must not also see it writable. Slot 11 (final resolved) is only ever written.
    const std::array<std::pair<std::shared_ptr<DeviceProgram>, std::shared_ptr<DescriptorGroup>>, 2> Entries = {
        {{ResolveOpaqueProgram_, ResolveGroupFor(OpaqueResolvedView_, FinalResolvedView_, DummySampledView_)},
         {ResolveRefractionProgram_, ResolveGroupFor(DummyStorageView_, FinalResolvedView_, OpaqueResolvedView_)}}};
    for (const auto& [Program, Group] : Entries)
    {
        Sequence.BindProgram(Program);
        Sequence.BindGroup(0, Group);
        Sequence.Dispatch((Extent_[0] + 7) / 8, (Extent_[1] + 7) / 8);
    }
}

// 📝 Inspection modes 10-14 (RenderInspectionModes): the chosen cluster image goes into the display inspection target, which the
//    recombination presents un-tonemapped. Mode 11 copies the deferred resolve output itself.
void NestingProjection::RecordInspectionView(CommandSequence& Sequence, const std::shared_ptr<DeviceTextureView>& TargetView, uint32_t Mode)
{
    if (!TargetView) return;
    if (ProgramsBuilt_ && !OpaqueVisTexture_ && TargetExtent_) ConstructTargets((*TargetExtent_)[0], (*TargetExtent_)[1]);
    if (!ProgramsBuilt_ || !OpaqueVisTexture_)
    {
        ActivateForInspection();
        RecordUnavailableView(Sequence, TargetView);
        return;
    }
    if (!Records_)
    {
        RecordUnavailableView(Sequence, TargetView);
        return;
    }
    ReportInspectionStatus();   // ready: clears the status caption
    const std::shared_ptr<DeviceProgram>& Program = Mode == 10   ? InspectIdentitiesProgram_
                                                    : Mode == 13 ? InspectGlassProgram_
                                                    : Mode == 12 ? InspectHiZProgram_
                                                    : Mode == 14 ? InspectTiersProgram_
                                                                 : InspectCopyProgram_;
    if (!Program)
    {
        RecordUnavailableView(Sequence, TargetView);
        return;
    }
    // 📝 Slot 6 carries the storage dummy: the inspection entries write slot 11 (the inspection target) and read slot 12.
    const auto Group = ResolveGroupFor(DummyStorageView_, TargetView, Mode == 11 ? FinalResolvedView_ : OpaqueResolvedView_);
    Sequence.BindProgram(Program);
    Sequence.BindGroup(0, Group);
    Sequence.Dispatch((Extent_[0] + 7) / 8, (Extent_[1] + 7) / 8);
}
} // namespace Frontier::VirtualizedGeometry
