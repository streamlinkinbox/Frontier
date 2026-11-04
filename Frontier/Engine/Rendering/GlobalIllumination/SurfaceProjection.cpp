/*==========================================================================================================================================
                                                         SURFACEPROJECTION.CPP
==========================================================================================================================================*/
// 🧩 Primary visibility (depth, attributes, motion), lossless cluster culling and the direct radiance over it (SurfaceProjection.js).

#include "SurfaceProjection.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace Frontier::GlobalIllumination
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   GEOMETRY CLUSTERS
//------------------------------------------------------------------------------------------------------------------------

GeometryClusterArchive BuildGeometryClusters(const FacetSet& Set, uint32_t Limit)
{
    struct FacetCentre
    {
        uint32_t Index = 0;
        Vector3  Centre{};
    };
    std::vector<FacetCentre> Order(Set.Facets.size());
    for (uint32_t Index = 0; Index < Set.Facets.size(); ++Index)
    {
        Order[Index].Index = Index;
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            // 📝 Corners.reduce((S, P) => S + P[A], 0) / 3: left fold from zero.
            double Sum = 0.0;
            for (const Vector3& Corner : Set.Facets[Index].Corners) Sum = Sum + Corner[Axis];
            Order[Index].Centre[Axis] = Sum / 3.0;
        }
    }
    std::vector<std::vector<FacetCentre>> Leaves;
    std::function<void(std::vector<FacetCentre>)> Split = [&](std::vector<FacetCentre> Members) {
        if (Members.size() <= Limit)
        {
            Leaves.push_back(std::move(Members));
            return;
        }
        Vector3 Minimum{INFINITY, INFINITY, INFINITY}, Maximum{-INFINITY, -INFINITY, -INFINITY};
        for (const FacetCentre& Entry : Members)
        {
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                Minimum[Axis] = std::min(Minimum[Axis], Entry.Centre[Axis]);
                Maximum[Axis] = std::max(Maximum[Axis], Entry.Centre[Axis]);
            }
        }
        int Axis = 0;
        for (int Candidate = 1; Candidate < 3; ++Candidate)
        {
            if (Maximum[Candidate] - Minimum[Candidate] > Maximum[Axis] - Minimum[Axis]) Axis = Candidate;
        }
        std::sort(Members.begin(), Members.end(), [Axis](const FacetCentre& Left, const FacetCentre& Right) {
            if (Left.Centre[Axis] != Right.Centre[Axis]) return Left.Centre[Axis] < Right.Centre[Axis];
            return Left.Index < Right.Index;
        });
        const size_t Middle = Members.size() >> 1;
        Split(std::vector<FacetCentre>(Members.begin(), Members.begin() + static_cast<std::ptrdiff_t>(Middle)));
        Split(std::vector<FacetCentre>(Members.begin() + static_cast<std::ptrdiff_t>(Middle), Members.end()));
    };
    if (!Order.empty()) Split(Order);
    GeometryClusterArchive Archive;
    for (const auto& Members : Leaves)
    {
        GeometryCluster Cluster;
        Cluster.Minimum    = {INFINITY, INFINITY, INFINITY};
        Cluster.Maximum    = {-INFINITY, -INFINITY, -INFINITY};
        Cluster.FirstIndex = static_cast<uint32_t>(Archive.Indices.size());
        for (const FacetCentre& Entry : Members)
        {
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
            {
                Archive.Indices.push_back(Set.FirstVertex + Entry.Index * 3 + Corner);
                const Vector3& Point = Set.Facets[Entry.Index].Corners[Corner];
                for (int Axis = 0; Axis < 3; ++Axis)
                {
                    Cluster.Minimum[Axis] = std::min(Cluster.Minimum[Axis], Point[Axis]);
                    Cluster.Maximum[Axis] = std::max(Cluster.Maximum[Axis], Point[Axis]);
                }
            }
        }
        Cluster.IndexCount = static_cast<uint32_t>(Members.size() * 3);
        Archive.Clusters.push_back(Cluster);
    }
    return Archive;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   CLUSTERED GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

ClusteredGeometry::ClusteredGeometry(DeviceExchange& Exchange, ShaderInterchange& Shaders) : Exchange_(Exchange), Shaders_(Shaders)
{
}

void ClusteredGeometry::ConstructPrograms(const std::shared_ptr<DeviceRegion>& RefreshRegion, const std::shared_ptr<DeviceRegion>& OccupantRegion)
{
    RefreshRegion_  = RefreshRegion;
    OccupantRegion_ = OccupantRegion;
    const std::vector<DescriptorSlot> Slots = {{DescriptorCode::Uniform}, {DescriptorCode::StorageRead}, {DescriptorCode::StorageRead},
                                               {DescriptorCode::Storage}};
    Layout_  = Exchange_.ConstructDescriptorLayout(Slots, "Geometry cluster culling");
    Program_ = Shaders_.ConstructComputeProgram("GeometryClusterCull", "CullGeometryClusters", {Layout_});
}

void ClusteredGeometry::Prepare(const SceneStructure& Scene)
{
    std::string Key;
    for (const DrawRun& Run : Scene.DrawRuns) Key += std::to_string(Run.FacetSet) + ":" + std::to_string(Run.First) + ":" + std::to_string(Run.Count) + "|";
    if (Key == Key_) return;
    Key_ = Key;
    Draws.clear();
    std::vector<uint32_t> IndexList;
    std::vector<std::array<uint32_t, 12>> RecordList;
    std::map<uint32_t, uint32_t> Offsets;
    for (const DrawRun& Run : Scene.DrawRuns)
    {
        const FacetSet& Set = Scene.FacetSets[Run.FacetSet];
        bool Eligible = Set.Facets.size() >= 256 && Set.VertexCapacity == 0;
        for (uint32_t Index = 0; Index < Run.Count && Eligible; ++Index)
        {
            if (Scene.Occupants[Run.First + Index].Participation & OccupantParticipation::Dynamic) Eligible = false;
        }
        if (!Eligible)
        {
            Draws.push_back({true, Run, 0, 0});
            continue;
        }
        auto Built = Retained_.find(Run.FacetSet);
        if (Built == Retained_.end()) Built = Retained_.emplace(Run.FacetSet, BuildGeometryClusters(Set, 128)).first;
        auto Offset = Offsets.find(Run.FacetSet);
        if (Offset == Offsets.end())
        {
            Offset = Offsets.emplace(Run.FacetSet, static_cast<uint32_t>(IndexList.size())).first;
            IndexList.insert(IndexList.end(), Built->second.Indices.begin(), Built->second.Indices.end());
        }
        for (uint32_t Occupant = Run.First; Occupant < Run.First + Run.Count; ++Occupant)
        {
            for (const GeometryCluster& Cluster : Built->second.Clusters)
            {
                const uint32_t Identity = static_cast<uint32_t>(RecordList.size());
                std::array<uint32_t, 12> Record{};
                float Scalars[8] = {static_cast<float>(Cluster.Minimum[0]), static_cast<float>(Cluster.Minimum[1]), static_cast<float>(Cluster.Minimum[2]), 0.0f,
                                    static_cast<float>(Cluster.Maximum[0]), static_cast<float>(Cluster.Maximum[1]), static_cast<float>(Cluster.Maximum[2]), 0.0f};
                std::memcpy(Record.data(), Scalars, sizeof(Scalars));
                Record[3] = Occupant;
                Record[7] = Cluster.FirstIndex + Offset->second;
                Record[8] = Cluster.IndexCount;
                RecordList.push_back(Record);
                Draws.push_back({false, {}, Identity, Occupant});
            }
        }
    }
    CandidateCount = static_cast<uint32_t>(RecordList.size());
    auto Make = [this](const void* Bytes, size_t Size, const std::string& Label) {
        auto Region = Exchange_.ConstructRegion(std::max<size_t>(4, Size), Label);
        if (Size > 0) Exchange_.WriteRegion(Region, 0, Bytes, Size);
        return Region;
    };
    Indices = Make(IndexList.data(), IndexList.size() * 4, "Cluster triangle indices");
    std::vector<uint32_t> Identities(Scene.Occupants.size());
    for (uint32_t Index = 0; Index < Identities.size(); ++Index) Identities[Index] = Index;
    Instances = Make(Identities.data(), Identities.size() * 4, "Cluster instance identities");
    if (RecordList.empty()) return;
    Records  = Make(RecordList.data(), RecordList.size() * 48, "Geometry cluster bounds");
    Commands = Exchange_.ConstructRegion(static_cast<VkDeviceSize>(RecordList.size()) * 20, "GPU cluster draws");
    Group_   = Exchange_.ConstructDescriptorGroup(Layout_, {RefreshRegion_, OccupantRegion_, Records, Commands}, "Geometry cluster culling");
}

void ClusteredGeometry::RecordCull(CommandSequence& Sequence)
{
    if (CandidateCount == 0) return;
    Sequence.BindProgram(Program_);
    Sequence.BindGroup(0, Group_);
    Sequence.Dispatch((CandidateCount + 63) / 64);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   SURFACE PROJECTION
//------------------------------------------------------------------------------------------------------------------------

SurfaceProjection::SurfaceProjection(DeviceExchange& Exchange, ShaderInterchange& Shaders) : Exchange_(Exchange), Shaders_(Shaders)
{
}

void SurfaceProjection::ConstructPrograms(const std::shared_ptr<DeviceRegion>& RefreshRegion, const std::shared_ptr<DeviceRegion>& OccupantRegion,
                                          const std::shared_ptr<DeviceRegion>& MaterialRegion, const std::shared_ptr<DescriptorLayout>& DistanceLayout,
                                          const std::shared_ptr<DescriptorLayout>& SamplingLayout, const std::shared_ptr<DescriptorLayout>& RayTracingLayout,
                                          const std::shared_ptr<DeviceTextureView>& TyreMarkView, const std::shared_ptr<DeviceSampler>& TyreMarkSampler,
                                          const std::shared_ptr<DeviceRegion>& EffectRegion, const std::shared_ptr<DeviceRegion>& VehicleRegion)
{
    RefreshRegion_  = RefreshRegion;
    MaterialRegion_ = MaterialRegion;
    using D = DescriptorCode;
    const std::vector<DescriptorSlot> RasterSlots = {{D::Uniform}, {D::StorageRead}, {D::Texture2D}, {D::Sampler}, {D::Uniform}, {D::StorageRead}};
    RasterLayout_ = Exchange_.ConstructDescriptorLayout(RasterSlots, "Surface raster");
    RasterGroup_  = Exchange_.ConstructDescriptorGroup(RasterLayout_, {RefreshRegion, OccupantRegion, TyreMarkView, TyreMarkSampler, EffectRegion, VehicleRegion},
                                                       "Surface raster");
    RasterProgramSpecification Raster;
    Raster.VertexEntry   = "SurfaceVertex";
    Raster.FragmentEntry = "SurfaceFragment";
    Raster.VertexLayouts = {{VertexStride, false, {{0, 0, VertexFormat::Float32x3}, {1, 12, VertexFormat::Uint32}, {2, 16, VertexFormat::Float32x3},
                                                   {3, 28, VertexFormat::Uint32}}},
                            {4, true, {{4, 0, VertexFormat::Uint32}}}};
    Raster.DepthEnabled  = true;
    Raster.DepthWrite    = true;
    Raster.DepthCompare  = CompareFunction::Greater;
    Raster.Targets       = {{TextureFormat::Rgba16Float}, {TextureFormat::Rg16Float}};
    Raster.Layouts       = {RasterLayout_};
    RasterProgram_ = Shaders_.ConstructRasterProgram("SurfaceRaster", Raster);
    Clusters = std::make_unique<ClusteredGeometry>(Exchange_, Shaders_);
    Clusters->ConstructPrograms(RefreshRegion, OccupantRegion);
    if (!Virtualized) Virtualized = std::make_unique<VirtualizedGeometry::NestingProjection>(Exchange_, Shaders_);
    Virtualized->ConstructPrograms(RefreshRegion, OccupantRegion);
    const std::vector<DescriptorSlot> DirectSlots = {{D::Uniform}, {D::StorageRead}, {D::Texture2D}, {D::TextureDepth2D}, {D::TextureDepth2DLayered},
                                                     {D::SamplerComparison}, {D::Uniform}, {D::Texture2D}, {D::Sampler},
                                                     {D::StorageTexture2D, TextureFormat::Rgba16Float}, {D::StorageRead}, {D::Uniform}, {D::Texture2D}, {D::Sampler},
                                                     {D::StorageTexture2D, TextureFormat::Rgba16Float}};
    DirectLayout_  = Exchange_.ConstructDescriptorLayout(DirectSlots, "Direct radiance");
    DirectProgram_ = Shaders_.ConstructComputeProgram("DirectRadiance", "IntegrateDirectRadiance", {DirectLayout_, DistanceLayout, SamplingLayout, RayTracingLayout});
    const std::vector<DescriptorSlot> PenumbraSlots = {{D::Uniform}, {D::Texture2D}, {D::TextureDepth2D}, {D::Texture2D},
                                                       {D::StorageTexture2D, TextureFormat::Rgba16Float}};
    PenumbraLayout_  = Exchange_.ConstructDescriptorLayout(PenumbraSlots, "Penumbra resolve");
    PenumbraProgram_ = Shaders_.ConstructComputeProgram("PenumbraResolve", "ResolvePenumbra", {PenumbraLayout_});
}

void SurfaceProjection::ConstructTargets(uint32_t Width, uint32_t Height, const SurfaceTargetInputs& Inputs)
{
    Extent = {Width, Height};
    auto Construct = [&](const std::string& Label, TextureFormat Format, uint32_t Usage) {
        TextureSpecification Specification;
        Specification.Label  = Label;
        Specification.Width  = Width;
        Specification.Height = Height;
        Specification.Format = Format;
        Specification.Usage  = Usage;
        return Exchange_.ConstructTexture(Specification);
    };
    const uint32_t Attachment = TextureUsage::RenderAttachment | TextureUsage::Sampled;
    DepthTexture     = Construct("Surface depth", TextureFormat::Depth32Float, Attachment);
    AttributeTexture = Construct("Surface attributes", TextureFormat::Rgba16Float, Attachment);
    MotionTexture    = Construct("Surface motion", TextureFormat::Rg16Float, Attachment);
    DirectTexture    = Construct("Direct radiance", TextureFormat::Rgba16Float, TextureUsage::Storage | TextureUsage::Sampled);
    PenumbraTexture  = Construct("Penumbra signal", TextureFormat::Rgba16Float, TextureUsage::Storage | TextureUsage::Sampled);
    DepthView     = Exchange_.ConstructView(DepthTexture);
    AttributeView = Exchange_.ConstructView(AttributeTexture);
    MotionView    = Exchange_.ConstructView(MotionTexture);
    DirectView    = Exchange_.ConstructView(DirectTexture);
    PenumbraView  = Exchange_.ConstructView(PenumbraTexture);
    if (Virtualized) Virtualized->ConstructTargets(Width, Height);
    DirectGroup_ = Exchange_.ConstructDescriptorGroup(DirectLayout_, {RefreshRegion_, MaterialRegion_, AttributeView, DepthView, Inputs.ShadowLayered,
                                                                      Inputs.ShadowComparison, Inputs.ShadowReceiver, Inputs.SkyView, Inputs.SkySampler,
                                                                      DirectView, Inputs.LuminaireRegion, Inputs.CloudShadowParams, Inputs.CloudShadowView,
                                                                      Inputs.CloudShadowSampler, PenumbraView}, "Direct radiance");
    PenumbraGroup_ = Exchange_.ConstructDescriptorGroup(PenumbraLayout_, {RefreshRegion_, AttributeView, DepthView, PenumbraView, DirectView}, "Penumbra resolve");
}

void SurfaceProjection::RecordSurface(CommandSequence& Sequence, const SceneStructure& Scene, const std::shared_ptr<DeviceRegion>& VertexRegion)
{
    Clusters->Prepare(Scene);
    // 📝 The virtualized takeover must never take the refresh down with it: any failure inside Prepare (descriptor groups, the nesting
    //    build, …) disables it and the lossless-cluster path renders the refresh instead.
    VirtualizedGeometry::NestingProjection* Takeover = nullptr;
    try
    {
        Takeover = Virtualized ? Virtualized->Prepare(Scene) : nullptr;
    }
    catch (const std::exception& Error)
    {
        Exchange_.RecordFault(std::string("virtualized geometry prepare failed; virtualized path disabled: ") + Error.what());
        if (Virtualized) Virtualized->Enabled = false;
    }
    const bool VirtualActive = Takeover != nullptr && Takeover->HasRecords();
    if (ClusteredEnabled) Clusters->RecordCull(Sequence);
    if (VirtualActive) Virtualized->RecordCullPhase1(Sequence);
    auto BeginRaster = [&](const std::string& Label, bool Clear) {
        const LoadOperation Load = Clear ? LoadOperation::Clear : LoadOperation::Load;
        const std::array<ColorAttachment, 2> Colors = {{{AttributeView, Load, {0, 0, 0, 0}}, {MotionView, Load, {0, 0, 0, 0}}}};
        const DepthAttachment Depth{DepthView, Load, 0.0f};
        Sequence.BeginRaster(Colors, &Depth, Label);
    };
    // 📝 The lossless-cluster fallback runs and clusters plus, for the virtualized path, one phase of cluster commands.
    auto RecordRuns = [&](uint32_t Phase) {
        Sequence.BindProgram(RasterProgram_);
        Sequence.BindGroup(0, RasterGroup_);
        Sequence.BindVertexRegion(0, VertexRegion);
        if (Phase == 2)
        {
            if (VirtualActive) Virtualized->RecordClusters(Sequence, Clusters->Instances, 2);
            return;
        }
        if (ClusteredEnabled)
        {
            Sequence.BindIndexRegion(Clusters->Indices);
            for (const ClusteredGeometry::ClusterDraw& Draw : Clusters->Draws)
            {
                if (Draw.Run)
                {
                    Sequence.BindVertexRegion(0, VertexRegion);
                    Sequence.BindVertexRegion(1, Clusters->Instances);
                    const FacetSet& Set = Scene.FacetSets[Draw.RunEntry.FacetSet];
                    Sequence.Draw(Set.VertexCount, Draw.RunEntry.Count, Set.FirstVertex, Draw.RunEntry.First);
                }
                else if (!VirtualActive || !Virtualized->OwnsOccupant(Draw.Occupant))
                {
                    Sequence.BindVertexRegion(0, VertexRegion);
                    Sequence.BindVertexRegion(1, Clusters->Instances, static_cast<VkDeviceSize>(Draw.Occupant) * 4);
                    Sequence.DrawIndexedIndirect(Clusters->Commands, static_cast<VkDeviceSize>(Draw.Cluster) * 20);
                }
            }
        }
        else
        {
            Sequence.BindVertexRegion(1, Clusters->Instances);
            for (const DrawRun& Run : Scene.DrawRuns)
            {
                const FacetSet& Set = Scene.FacetSets[Run.FacetSet];
                Sequence.Draw(Set.VertexCount, Run.Count, Set.FirstVertex, Run.First);
            }
        }
        if (VirtualActive && Phase) Virtualized->RecordClusters(Sequence, Clusters->Instances, Phase);
    };
    if (!VirtualActive)
    {
        BeginRaster("Surface projection", true);
        RecordRuns(0);
        Sequence.EndRaster();
        return;
    }
    // 📝 Two-phase occlusion: phase-1 cut clusters (tested against the previous refresh's Hi-Z), rebuild the pyramid from their
    //    depth, then phase 2 adds clusters that were occluded last refresh but are visible now.
    BeginRaster("Surface projection (virtualized phase 1)", true);
    RecordRuns(1);
    Sequence.EndRaster();
    Virtualized->RecordHiZ(Sequence, DepthView);
    Virtualized->RecordCullPhase2(Sequence);
    BeginRaster("Surface projection (virtualized phase 2)", false);
    RecordRuns(2);
    Sequence.EndRaster();
    // 📝 Visibility raster, deferred material resolve and two-layer refraction gather from the same indirect commands.
    Virtualized->RecordVisibilityResolve(Sequence, RasterGroup_, Clusters->Instances);
}

void SurfaceProjection::RecordDirect(CommandSequence& Sequence, const std::shared_ptr<DescriptorGroup>& DistanceGroup,
                                     const std::shared_ptr<DescriptorGroup>& SamplingGroup, const std::shared_ptr<DescriptorGroup>& RayTracingGroup)
{
    Sequence.BindProgram(DirectProgram_);
    Sequence.BindGroup(0, DirectGroup_);
    Sequence.BindGroup(1, DistanceGroup);
    Sequence.BindGroup(2, SamplingGroup);
    Sequence.BindGroup(3, RayTracingGroup);
    Sequence.Dispatch((Extent[0] + 7) / 8, (Extent[1] + 7) / 8);
}

void SurfaceProjection::RecordPenumbra(CommandSequence& Sequence)
{
    Sequence.BindProgram(PenumbraProgram_);
    Sequence.BindGroup(0, PenumbraGroup_);
    Sequence.Dispatch((Extent[0] + 7) / 8, (Extent[1] + 7) / 8);
}

} // namespace Frontier::GlobalIllumination
