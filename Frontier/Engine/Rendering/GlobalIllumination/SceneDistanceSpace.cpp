/*==========================================================================================================================================
                                                        SCENEDISTANCESPACE.CPP
==========================================================================================================================================*/
// 🧩 The single scene distance field: four camera-centred toroidal cascades, rewritten only where invalidated (SceneDistanceSpace.js).

#include "SceneDistanceSpace.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frontier::GlobalIllumination
{

namespace
{
constexpr int64_t BrickEdge     = 8;   // [voxel] edge of the unit of invalidation
constexpr uint8_t ScrollCause   = 1;   // [-] the cascade window newly covers the brick
constexpr uint8_t OccupantCause = 2;   // [-] a moving occupant's previous or current bounds overlap the brick

int64_t FloorToInteger(double Scalar)
{
    return static_cast<int64_t>(std::floor(Scalar));
}
} // namespace

SceneDistanceSpace::SceneDistanceSpace(DeviceExchange& Exchange, ShaderInterchange& Shaders, const SceneDistanceConfiguration& Configuration)
    : Configuration(Configuration), Exchange_(Exchange), Shaders_(Shaders)
{
    BricksPerAxis = Configuration.Resolution / static_cast<uint32_t>(BrickEdge);
    for (uint32_t Level = 0; Level < 4; ++Level)
    {
        const double VoxelExtent = Configuration.FinestVoxel * std::pow(2.0, Level);
        Clips[Level] = {VoxelExtent, Configuration.BandVoxels * VoxelExtent, std::nullopt};
    }
}

void SceneDistanceSpace::InvalidateBounds(const std::vector<WorldBounds>& BoundsList)
{
    PendingBounds_.insert(PendingBounds_.end(), BoundsList.begin(), BoundsList.end());
}

void SceneDistanceSpace::ConstructResources(const std::shared_ptr<DeviceTextureView>& VolumeSpaceView, const std::shared_ptr<DeviceRegion>& OccupantRegion)
{
    const uint32_t Resolution = Configuration.Resolution;
    for (uint32_t Level = 0; Level < 4; ++Level)
    {
        TextureSpecification Specification;
        Specification.Label         = "Scene distance cascade " + std::to_string(Level);
        Specification.Width         = Resolution;
        Specification.Height        = Resolution;
        Specification.DepthOrLayers = Resolution;
        Specification.Dimension     = TextureDimension::Volumetric;
        Specification.Format        = TextureFormat::Rgba16Float;
        Specification.Usage         = TextureUsage::Storage | TextureUsage::Sampled;
        Textures.push_back(Exchange_.ConstructTexture(Specification));
        Views.push_back(Exchange_.ConstructView(Textures.back()));
    }
    const uint32_t BrickSlots = 4u * BricksPerAxis * BricksPerAxis * BricksPerAxis;
    BrickRegion   = Exchange_.ConstructRegion(static_cast<VkDeviceSize>(BrickSlots) * 16, "Invalidated bricks");
    EpochRegion   = Exchange_.ConstructRegion(static_cast<VkDeviceSize>(BrickSlots) * 4, "Brick stamps");
    UniformRegion = Exchange_.ConstructRegion(160, "Distance constants");
    ListRegion    = Exchange_.ConstructRegion(static_cast<VkDeviceSize>(IdentityCeiling) * 4, "Distance list");
    const std::vector<uint32_t> Unstamped(BrickSlots, 0xFFFFFFFFu);
    Exchange_.WriteRegion(EpochRegion, 0, Unstamped.data(), Unstamped.size() * 4);

    SamplerSpecification Clamped;
    Clamped.MagFilter = Clamped.MinFilter = FilterMode::Linear;
    VolumeSampler = Exchange_.ConstructSampler(Clamped);
    SamplerSpecification Repeating = Clamped;
    Repeating.AddressU = Repeating.AddressV = Repeating.AddressW = AddressMode::Repeat;
    CascadeSampler = Exchange_.ConstructSampler(Repeating);

    const std::vector<DescriptorSlot> UnionSlots = {
        {DescriptorCode::Uniform}, {DescriptorCode::StorageRead}, {DescriptorCode::StorageRead}, {DescriptorCode::Texture3D}, {DescriptorCode::Sampler},
        {DescriptorCode::StorageTexture3D, TextureFormat::Rgba16Float}, {DescriptorCode::StorageTexture3D, TextureFormat::Rgba16Float},
        {DescriptorCode::StorageTexture3D, TextureFormat::Rgba16Float}, {DescriptorCode::StorageTexture3D, TextureFormat::Rgba16Float},
        {DescriptorCode::Storage}, {DescriptorCode::StorageRead}};
    UnionLayout  = Exchange_.ConstructDescriptorLayout(UnionSlots, "Distance union");
    UnionProgram = Shaders_.ConstructComputeProgram("DistanceUnion", "UniteDistance", {UnionLayout});
    UnionGroup   = Exchange_.ConstructDescriptorGroup(UnionLayout, {UniformRegion, BrickRegion, OccupantRegion, VolumeSpaceView, VolumeSampler,
                                                                    Views[0], Views[1], Views[2], Views[3], EpochRegion, ListRegion}, "Distance union");
    const std::vector<DescriptorSlot> MarchSlots = {{DescriptorCode::Uniform}, {DescriptorCode::Texture3D}, {DescriptorCode::Texture3D},
                                                    {DescriptorCode::Texture3D}, {DescriptorCode::Texture3D}, {DescriptorCode::Sampler},
                                                    {DescriptorCode::StorageRead}};
    MarchLayout = Exchange_.ConstructDescriptorLayout(MarchSlots, "Distance march");
    MarchGroup  = Exchange_.ConstructDescriptorGroup(MarchLayout, {UniformRegion, Views[0], Views[1], Views[2], Views[3], CascadeSampler, EpochRegion},
                                                     "Distance march");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   INVALIDATION SCHEDULE
//------------------------------------------------------------------------------------------------------------------------

void SceneDistanceSpace::ScheduleInvalidation(const Vector3& Focus, std::vector<WorldBounds> MovedBounds)
{
    RefreshBricks.clear();
    RefreshLevelCounts = {0, 0, 0, 0};
    if (Frozen) return;
    if (!PendingBounds_.empty())
    {
        MovedBounds.insert(MovedBounds.end(), PendingBounds_.begin(), PendingBounds_.end());
        PendingBounds_.clear();
    }
    const int64_t Edge = BricksPerAxis;
    for (uint32_t Level = 0; Level < Configuration.ClipCount; ++Level)
    {
        ClipWindow&  Clip       = Clips[Level];
        const double BrickWorld = static_cast<double>(BrickEdge) * Clip.VoxelExtent;
        const std::array<int64_t, 3> Desired = {FloorToInteger(Focus[0] / BrickWorld) - Edge / 2,
                                                FloorToInteger(Focus[1] / BrickWorld) - Edge / 2,
                                                FloorToInteger(Focus[2] / BrickWorld) - Edge / 4};
        std::vector<uint8_t> Flags(static_cast<size_t>(Edge * Edge * Edge), 0u);
        const auto Previous = Clip.OriginBrick;
        const bool Moved    = !Previous || Desired != *Previous;
        if (Moved)
        {
            for (int64_t Z = 0; Z < Edge; ++Z)
            {
                for (int64_t Y = 0; Y < Edge; ++Y)
                {
                    for (int64_t X = 0; X < Edge; ++X)
                    {
                        const std::array<int64_t, 3> World = {Desired[0] + X, Desired[1] + Y, Desired[2] + Z};
                        bool Covered = Previous.has_value();
                        for (int Axis = 0; Axis < 3 && Covered; ++Axis)
                        {
                            Covered = World[Axis] >= (*Previous)[Axis] && World[Axis] < (*Previous)[Axis] + Edge;
                        }
                        if (!Covered) Flags[static_cast<size_t>((Z * Edge + Y) * Edge + X)] |= ScrollCause;
                    }
                }
            }
            Clip.OriginBrick = Desired;
        }
        for (const WorldBounds& Bounds : MovedBounds)
        {
            std::array<int64_t, 3> Low{}, High{};
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                Low[Axis]  = FloorToInteger((Bounds.Minimum[Axis] - Clip.Band) / BrickWorld) - Desired[Axis];
                High[Axis] = FloorToInteger((Bounds.Maximum[Axis] + Clip.Band) / BrickWorld) - Desired[Axis];
            }
            for (int64_t Z = std::max<int64_t>(0, Low[2]); Z <= std::min(Edge - 1, High[2]); ++Z)
                for (int64_t Y = std::max<int64_t>(0, Low[1]); Y <= std::min(Edge - 1, High[1]); ++Y)
                    for (int64_t X = std::max<int64_t>(0, Low[0]); X <= std::min(Edge - 1, High[0]); ++X)
                        Flags[static_cast<size_t>((Z * Edge + Y) * Edge + X)] |= OccupantCause;
        }
        for (size_t Slot = 0; Slot < Flags.size(); ++Slot)
        {
            if (!Flags[Slot]) continue;
            const int64_t X = static_cast<int64_t>(Slot) % Edge;
            const int64_t Y = (static_cast<int64_t>(Slot) / Edge) % Edge;
            const int64_t Z = static_cast<int64_t>(Slot) / (Edge * Edge);
            RefreshBricks.push_back(static_cast<int32_t>(Desired[0] + X));
            RefreshBricks.push_back(static_cast<int32_t>(Desired[1] + Y));
            RefreshBricks.push_back(static_cast<int32_t>(Desired[2] + Z));
            RefreshBricks.push_back(static_cast<int32_t>(Level | (static_cast<uint32_t>(Flags[Slot]) << 8)));
            RefreshLevelCounts[Level]++;
        }
    }
    TotalBricks += RefreshBricks.size() / 4;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   RECORDING
//------------------------------------------------------------------------------------------------------------------------

std::vector<uint8_t> SceneDistanceSpace::EncodeDistanceConstants(uint32_t DistanceOccupantCount, uint32_t RefreshIndex) const
{
    std::vector<uint8_t> Bytes(160, 0u);
    auto StoreSigned   = [&Bytes](size_t Index, int32_t Value) { std::memcpy(Bytes.data() + Index * 4, &Value, 4); };
    auto StoreUnsigned = [&Bytes](size_t Index, uint32_t Value) { std::memcpy(Bytes.data() + Index * 4, &Value, 4); };
    auto StoreFloat    = [&Bytes](size_t Index, double Value) { const float Single = static_cast<float>(Value); std::memcpy(Bytes.data() + Index * 4, &Single, 4); };
    const uint32_t Resolution = Configuration.Resolution;
    for (uint32_t Level = 0; Level < 4; ++Level)
    {
        const ClipWindow& Clip   = Clips[Level];
        const size_t      Offset = Level * 8;
        const std::array<int64_t, 3> Origin = Clip.OriginBrick ? *Clip.OriginBrick : std::array<int64_t, 3>{0, 0, 0};
        StoreSigned(Offset + 0, static_cast<int32_t>(Origin[0] * BrickEdge));
        StoreSigned(Offset + 1, static_cast<int32_t>(Origin[1] * BrickEdge));
        StoreSigned(Offset + 2, static_cast<int32_t>(Origin[2] * BrickEdge));
        StoreUnsigned(Offset + 3, Resolution);
        StoreFloat(Offset + 4, Clip.VoxelExtent);
        StoreFloat(Offset + 5, Clip.Band);
        StoreFloat(Offset + 6, 1.0 / (Resolution * Clip.VoxelExtent));
    }
    StoreUnsigned(32, Configuration.ClipCount);
    StoreUnsigned(33, DistanceOccupantCount);
    StoreFloat(34, 0.0);
    StoreFloat(35, Configuration.HitFraction);
    StoreUnsigned(36, static_cast<uint32_t>(RefreshBricks.size() / 4));
    StoreUnsigned(37, RefreshIndex);
    StoreUnsigned(38, BricksPerAxis);
    return Bytes;
}

void SceneDistanceSpace::UploadDistanceList(const std::vector<uint32_t>& List)
{
    if (!List.empty()) Exchange_.WriteRegion(ListRegion, 0, List.data(), List.size() * 4);
}

void SceneDistanceSpace::RecordUnion(CommandSequence& Sequence, uint32_t DistanceOccupantCount, uint32_t RefreshIndex)
{
    const std::vector<uint8_t> Constants = EncodeDistanceConstants(DistanceOccupantCount, RefreshIndex);
    Exchange_.WriteRegion(UniformRegion, 0, Constants.data(), Constants.size());
    const uint32_t BrickCount = static_cast<uint32_t>(RefreshBricks.size() / 4);
    if (BrickCount == 0) return;
    Exchange_.WriteRegion(BrickRegion, 0, RefreshBricks.data(), RefreshBricks.size() * 4);
    Sequence.BindProgram(UnionProgram);
    Sequence.BindGroup(0, UnionGroup);
    Sequence.Dispatch(std::min(BrickCount, 65535u), (BrickCount + 65534u) / 65535u);
}

WorldBounds SceneDistanceSpace::QueryClipWindow(uint32_t Level) const
{
    const ClipWindow& Clip       = Clips[Level];
    const double      BrickWorld = static_cast<double>(BrickEdge) * Clip.VoxelExtent;
    const std::array<int64_t, 3> Origin = Clip.OriginBrick ? *Clip.OriginBrick : std::array<int64_t, 3>{0, 0, 0};
    WorldBounds Window;
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        Window.Minimum[Axis] = static_cast<double>(Origin[Axis]) * BrickWorld;
        Window.Maximum[Axis] = Window.Minimum[Axis] + BricksPerAxis * BrickWorld;
    }
    return Window;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   MOVED BOUNDS
//------------------------------------------------------------------------------------------------------------------------

std::vector<WorldBounds> ResolveMovedBounds(SceneStructure& Scene, bool Frozen)
{
    constexpr double TranslationTolerance = 0.002;   // [m]
    constexpr double RotationTolerance    = 0.002;   // [-] largest change of any rotation-matrix element
    std::vector<WorldBounds> Moved;
    for (uint32_t Index = 0; Index < Scene.Occupants.size(); ++Index)
    {
        OccupantEntry& Occupant = Scene.Occupants[Index];
        if (!(Occupant.Participation & OccupantParticipation::Dynamic) || !(Occupant.Participation & OccupantParticipation::Distance)) continue;
        if (!Occupant.UnionTransform) Occupant.UnionTransform = Occupant.Current;
        if (Frozen) continue;
        double Rotation = 0.0;
        for (int Element : {0, 1, 2, 4, 5, 6, 8, 9, 10})
        {
            Rotation = std::max(Rotation, std::fabs(Occupant.Current[Element] - (*Occupant.UnionTransform)[Element]));
        }
        const double Translation = ComputeHypot(Occupant.Current[12] - (*Occupant.UnionTransform)[12],
                                                Occupant.Current[13] - (*Occupant.UnionTransform)[13],
                                                Occupant.Current[14] - (*Occupant.UnionTransform)[14]);
        if (Rotation < RotationTolerance && Translation < TranslationTolerance) continue;
        const FacetSet&   Set     = Scene.FacetSets[Occupant.FacetSet];
        const WorldBounds Current = Scene.QueryWorldBounds(Index);
        const WorldBounds Before  = TransformBounds(*Occupant.UnionTransform, Occupant.LocalMinimum ? *Occupant.LocalMinimum : Set.LocalMinimum,
                                                    Occupant.LocalMaximum ? *Occupant.LocalMaximum : Set.LocalMaximum);
        WorldBounds Union;
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            Union.Minimum[Axis] = std::min(Current.Minimum[Axis], Before.Minimum[Axis]);
            Union.Maximum[Axis] = std::max(Current.Maximum[Axis], Before.Maximum[Axis]);
        }
        Moved.push_back(Union);
        Occupant.UnionTransform = Occupant.Current;
    }
    return Moved;
}

} // namespace Frontier::GlobalIllumination
