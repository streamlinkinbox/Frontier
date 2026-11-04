/*==========================================================================================================================================
                                                       OCCUPANTDISTANCESPACE.CPP
==========================================================================================================================================*/
// 🧩 Per-facet-set signed distance volumes stacked in one 3D texel space, transferred on the device (OccupantDistanceSpace.js).

#include "OccupantDistanceSpace.h"

#include "BoundingStructure.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frontier::GlobalIllumination
{

namespace
{
constexpr uint32_t MaximumVolumeTexels = 128;     // [texel] longest volume axis
constexpr uint32_t MinimumVolumeTexels = 8;       // [texel] shortest volume axis
constexpr uint32_t SignRayCount        = 32;      // [-]     rays per voxel for the back-face sign heuristic
constexpr uint32_t SlabVoxelBudget     = 65536;   // [voxel] voxels per submission
constexpr uint32_t PoolStride          = FragmentPool.MaxTriangles * 3 + FragmentPool.MaxPlanes;   // [vec4] per job

// 📝 IEEE 754 binary16, round to nearest even; finite inputs only (distances), overflow saturates to ±65504.
uint16_t PackHalf(float Scalar) noexcept
{
    uint32_t Bits = 0;
    std::memcpy(&Bits, &Scalar, 4);
    const uint32_t Sign     = (Bits >> 16) & 0x8000u;
    const int32_t  Exponent = static_cast<int32_t>((Bits >> 23) & 0xFFu) - 127 + 15;
    uint32_t       Mantissa = Bits & 0x7FFFFFu;
    if (Exponent <= 0)
    {
        if (Exponent < -10) return static_cast<uint16_t>(Sign);
        Mantissa |= 0x800000u;
        const uint32_t Shift = static_cast<uint32_t>(14 - Exponent);
        uint32_t       Half  = Mantissa >> Shift;
        const uint32_t Rest  = Mantissa & ((1u << Shift) - 1u);
        const uint32_t Mid   = 1u << (Shift - 1u);
        if (Rest > Mid || (Rest == Mid && (Half & 1u))) ++Half;
        return static_cast<uint16_t>(Sign | Half);
    }
    if (Exponent >= 31) return static_cast<uint16_t>(Sign | 0x7BFFu);
    uint32_t Half = Sign | (static_cast<uint32_t>(Exponent) << 10) | (Mantissa >> 13);
    const uint32_t Rest = Mantissa & 0x1FFFu;
    if (Rest > 0x1000u || (Rest == 0x1000u && (Half & 1u))) ++Half;
    return static_cast<uint16_t>(std::min<uint32_t>(Half, Sign | 0x7BFFu));
}
} // namespace

OccupantDistanceSpace::OccupantDistanceSpace(DeviceExchange& Exchange, ShaderInterchange& Shaders)
    : Exchange_(Exchange), Shaders_(Shaders)
{
}

bool OccupantDistanceSpace::ConstructVolumes(SceneStructure& Scene, double VoxelScale, std::string* OutDiagnostic)
{
    // 📝 A JS Set keeps first-insertion order; the volumes follow the occupants' first use of each facet set.
    std::vector<uint32_t> Required;
    for (const OccupantEntry& Occupant : Scene.Occupants)
    {
        if ((Occupant.Participation & OccupantParticipation::Distance) &&
            std::find(Required.begin(), Required.end(), Occupant.FacetSet) == Required.end())
        {
            Required.push_back(Occupant.FacetSet);
        }
    }
    uint32_t Width = 0, Height = 0, Depth = 0;
    for (uint32_t SetIndex : Required)
    {
        FacetSet& Set   = Scene.FacetSets[SetIndex];
        if (Set.BakedVolume)
        {
            // 📝 A baked engine shape keeps the layout it was baked for at every quality grade.
            OccupantVolume Volume;
            Volume.FacetSet    = SetIndex;
            Volume.VoxelExtent = Set.BakedVolume->VoxelExtent;
            Volume.Resolution  = Set.BakedVolume->Resolution;
            for (int Axis = 0; Axis < 3; ++Axis) Volume.Minimum[Axis] = Set.BakedVolume->Minimum[Axis];
            Volume.TexelOrigin = {0, 0, Depth};
            Set.VolumeIndex    = static_cast<int32_t>(Volumes.size());
            Volumes.push_back(Volume);
            Width  = std::max(Width, Volume.Resolution[0]);
            Height = std::max(Height, Volume.Resolution[1]);
            Depth += Volume.Resolution[2] + 1;
            continue;
        }
        double    Voxel = Set.DistanceVoxel * VoxelScale;
        Vector3   Extent{};
        for (int Axis = 0; Axis < 3; ++Axis) Extent[Axis] = Set.LocalMaximum[Axis] - Set.LocalMinimum[Axis];
        auto Margin = [&Voxel]() { return std::max(4.0 * Voxel, 0.12); };
        double Longest = -INFINITY;
        for (int Axis = 0; Axis < 3; ++Axis) Longest = std::max(Longest, Extent[Axis] + 2.0 * Margin());
        if (Longest / Voxel > MaximumVolumeTexels) Voxel = Longest / MaximumVolumeTexels;
        OccupantVolume Volume;
        Volume.FacetSet    = SetIndex;
        Volume.VoxelExtent = Voxel;
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            const double Cells = std::ceil((Extent[Axis] + 2.0 * Margin()) / Voxel);
            Volume.Resolution[Axis] = static_cast<uint32_t>(std::max<double>(MinimumVolumeTexels, std::min<double>(MaximumVolumeTexels, Cells)));
        }
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            const double Centre = 0.5 * (Set.LocalMinimum[Axis] + Set.LocalMaximum[Axis]);
            Volume.Minimum[Axis] = Centre - 0.5 * Volume.Resolution[Axis] * Voxel;
        }
        Volume.TexelOrigin = {0, 0, Depth};
        Set.VolumeIndex    = static_cast<int32_t>(Volumes.size());
        Volumes.push_back(Volume);
        Width  = std::max(Width, Volume.Resolution[0]);
        Height = std::max(Height, Volume.Resolution[1]);
        Depth += Volume.Resolution[2] + 1;
    }
    // 📝 The fragment pool, layer after layer of Edge-cubes below the last volume.
    const uint32_t Edge = FragmentPool.Edge;
    Width  = std::max(Width, Edge);
    Height = std::max(Height, Edge);
    const uint32_t PerRow    = Width / Edge;
    const uint32_t PerColumn = Height / Edge;
    const uint32_t Layers    = (FragmentPool.Slots + PerRow * PerColumn - 1) / (PerRow * PerColumn);
    for (uint32_t Slot = 0; Slot < FragmentPool.Slots; ++Slot)
    {
        const uint32_t Layer  = Slot / (PerRow * PerColumn);
        const uint32_t Within = Slot % (PerRow * PerColumn);
        PoolOrigins_.push_back({(Within % PerRow) * Edge, (Within / PerRow) * Edge, Depth + Layer * (Edge + 1)});
        PoolOwners_.push_back(-1);
        PoolFree_.push_back(FragmentPool.Slots - 1 - Slot);
    }
    Depth += Layers * (Edge + 1);
    if (Depth > Exchange_.Limits().MaximumTexture3D)
    {
        if (OutDiagnostic) *OutDiagnostic = "distance volumes need " + std::to_string(Depth) + " slices; the adapter allows " +
                                            std::to_string(Exchange_.Limits().MaximumTexture3D);
        return false;
    }
    SpaceExtent = {Width, Height, std::max(Depth, 1u)};

    TextureSpecification Specification;
    Specification.Label         = "Occupant distance space";
    Specification.Width         = SpaceExtent[0];
    Specification.Height        = SpaceExtent[1];
    Specification.DepthOrLayers = SpaceExtent[2];
    Specification.Dimension     = TextureDimension::Volumetric;
    Specification.Format        = TextureFormat::Rgba16Float;
    Specification.Usage         = TextureUsage::Storage | TextureUsage::Sampled | TextureUsage::CopyTarget;
    Texture     = Exchange_.ConstructTexture(Specification);
    TextureView = Exchange_.ConstructView(Texture);

    const std::array<DescriptorSlot, 4> Slots = {{{DescriptorCode::Uniform}, {DescriptorCode::StorageRead}, {DescriptorCode::StorageRead},
                                                  {DescriptorCode::StorageTexture3D, TextureFormat::Rgba16Float}}};
    auto Layout  = Exchange_.ConstructDescriptorLayout(Slots, "Distance transfer");
    auto Program = Shaders_.ConstructComputeProgram("DistanceTransfer", "TransferDistance", {Layout});

    for (size_t VolumeIndex = 0; VolumeIndex < Volumes.size(); ++VolumeIndex)
    {
        const OccupantVolume& Volume = Volumes[VolumeIndex];
        const FacetSet&    Set = Scene.FacetSets[Volume.FacetSet];
        if (Set.BakedVolume)
        {
            // 📝 The baked texels as the transfer writes them: (signed distance, 0, 0, 1).
            const auto&           Distances = Set.BakedVolume->Distances;
            std::vector<uint16_t> Texels(Distances.size() * 4, 0u);
            for (size_t Index = 0; Index < Distances.size(); ++Index)
            {
                Texels[Index * 4]     = PackHalf(Distances[Index]);
                Texels[Index * 4 + 3] = 0x3C00u;
            }
            Exchange_.WriteTexture(Texture, 0, Texels.data(), Texels.size() * sizeof(uint16_t), Volume.Resolution[0], Volume.Resolution[1],
                                   Volume.Resolution[2], Volume.TexelOrigin[0], Volume.TexelOrigin[1], Volume.TexelOrigin[2]);
            continue;
        }
        std::vector<float> Corners(Set.Facets.size() * 9);
        for (size_t Index = 0; Index < Set.Facets.size(); ++Index)
        {
            for (int Corner = 0; Corner < 3; ++Corner)
            {
                for (int Axis = 0; Axis < 3; ++Axis)
                {
                    Corners[Index * 9 + Corner * 3 + Axis] = static_cast<float>(Set.Facets[Index].Corners[Corner][Axis]);
                }
            }
        }
        const BoundingStructureArchive Structure = ConstructBoundingStructure(Corners);
        auto EntryRegion = Exchange_.ConstructRegionFrom(Structure.EntryBytes.data(), Structure.EntryBytes.size(), Set.Name + " entries");
        auto FacetRegion = Exchange_.ConstructRegionFrom(Structure.FacetCorners.data(), Structure.FacetCorners.size() * sizeof(float),
                                                         Set.Name + " facets");
        const uint32_t SlicePlane = Volume.Resolution[0] * Volume.Resolution[1];
        const uint32_t SlabSlices = std::max(1u, SlabVoxelBudget / SlicePlane);
        for (uint32_t Slab = 0; Slab < Volume.Resolution[2]; Slab += SlabSlices)
        {
            const uint32_t SlabExtent = std::min(SlabSlices, Volume.Resolution[2] - Slab);
            uint8_t  Bytes[64] = {};
            float    Scalars[16] = {};
            uint32_t Integers[16] = {};
            for (int Axis = 0; Axis < 3; ++Axis) Scalars[Axis] = static_cast<float>(Volume.Minimum[Axis]);
            Scalars[3] = static_cast<float>(Volume.VoxelExtent);
            std::memcpy(Integers, Scalars, sizeof(Scalars));
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                Integers[4 + Axis] = Volume.TexelOrigin[Axis];
                Integers[8 + Axis] = Volume.Resolution[Axis];
            }
            Integers[7]  = Structure.EntryCount;
            Integers[11] = Structure.FacetCount;
            Integers[12] = Slab;
            Integers[13] = SlabExtent;
            Integers[14] = SignRayCount;
            std::memcpy(Bytes, Integers, sizeof(Bytes));
            auto Uniform = Exchange_.ConstructRegionFrom(Bytes, sizeof(Bytes), "Transfer constants");
            auto Group   = Exchange_.ConstructDescriptorGroup(Layout, {Uniform, EntryRegion, FacetRegion, TextureView}, "Distance transfer");
            Exchange_.LabelSubmissions("bring-up: distance volume " + std::to_string(VolumeIndex + 1) + " of " + std::to_string(Volumes.size()) + " ('" +
                                       Set.Name + "', " + std::to_string(Structure.FacetCount) + " facets, " + std::to_string(Volume.Resolution[0]) + "x" +
                                       std::to_string(Volume.Resolution[1]) + "x" + std::to_string(Volume.Resolution[2]) + "), slices " +
                                       std::to_string(Slab) + "-" + std::to_string(Slab + SlabExtent - 1));
            CommandSequence Sequence = Exchange_.BeginSequence();
            Sequence.BindProgram(Program);
            Sequence.BindGroup(0, Group);
            Sequence.Dispatch((Volume.Resolution[0] + 3) / 4, (Volume.Resolution[1] + 3) / 4, (SlabExtent + 3) / 4);
            if (!Exchange_.Submit(Sequence, OutDiagnostic)) return false;
        }
    }
    return true;
}

const OccupantVolume* OccupantDistanceSpace::QueryVolume(const SceneStructure& Scene, uint32_t FacetSetIndex) const
{
    const int32_t Index = Scene.FacetSets[FacetSetIndex].VolumeIndex;
    return Index >= 0 ? &Volumes[static_cast<size_t>(Index)] : nullptr;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   FRAGMENT POOL
//------------------------------------------------------------------------------------------------------------------------

void OccupantDistanceSpace::ConstructFragmentProgram()
{
    const std::array<DescriptorSlot, 3> Slots = {{{DescriptorCode::StorageRead}, {DescriptorCode::StorageRead},
                                                  {DescriptorCode::StorageTexture3D, TextureFormat::Rgba16Float}}};
    FragmentLayout_  = Exchange_.ConstructDescriptorLayout(Slots, "Fragment distance");
    FragmentProgram_ = Shaders_.ConstructComputeProgram("FragmentDistance", "ComputeFragmentDistance", {FragmentLayout_});
    JobRegion_       = Exchange_.ConstructRegion(FragmentPool.JobsPerRefresh * 32, "Fragment jobs");
    GeometryRegion_  = Exchange_.ConstructRegion(static_cast<VkDeviceSize>(FragmentPool.JobsPerRefresh) * PoolStride * 16, "Fragment geometry");
    FragmentGroup_   = Exchange_.ConstructDescriptorGroup(FragmentLayout_, {JobRegion_, GeometryRegion_, TextureView}, "Fragment distance");
}

std::optional<OccupantVolume> OccupantDistanceSpace::AllocateFragment(uint32_t Owner, const FragmentGeometry& Fragment,
                                                                      const std::function<void(uint32_t)>& Evict)
{
    if (!FragmentProgram_ || Fragment.Triangles.size() > FragmentPool.MaxTriangles || Fragment.Planes.size() > FragmentPool.MaxPlanes)
    {
        return std::nullopt;
    }
    if (PoolFree_.empty())
    {
        if (PoolOrder_.empty()) return std::nullopt;
        const uint32_t Oldest = PoolOrder_.front();
        PoolOrder_.pop_front();
        const int64_t Previous = PoolOwners_[Oldest];
        PoolOwners_[Oldest]    = -1;
        PoolQueue_.erase(std::remove_if(PoolQueue_.begin(), PoolQueue_.end(), [Oldest](const FragmentJob& Job) { return Job.Slot == Oldest; }),
                         PoolQueue_.end());
        PoolFree_.push_back(Oldest);
        if (Previous >= 0 && Evict) Evict(static_cast<uint32_t>(Previous));
    }
    const uint32_t Slot = PoolFree_.back();
    PoolFree_.pop_back();
    const uint32_t Edge = FragmentPool.Edge;
    double Largest = 0.02;
    for (int Axis = 0; Axis < 3; ++Axis) Largest = std::max(Largest, Fragment.Maximum[Axis] - Fragment.Minimum[Axis]);
    const double   Voxel = Largest / (Edge - 4);
    OccupantVolume Volume;
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        const double Centre = 0.5 * (Fragment.Minimum[Axis] + Fragment.Maximum[Axis]);
        Volume.Minimum[Axis] = Centre - 0.5 * Edge * Voxel;
    }
    Volume.VoxelExtent = Voxel;
    Volume.TexelOrigin = PoolOrigins_[Slot];
    Volume.Resolution  = {Edge, Edge, Edge};
    PoolOwners_[Slot]  = Owner;
    PoolOrder_.push_back(Slot);
    PoolQueue_.push_back({Slot, Volume, Fragment});
    return Volume;
}

void OccupantDistanceSpace::ReleaseFragment(const OccupantVolume& Volume, uint32_t Slot)
{
    (void)Volume;
    if (PoolOwners_[Slot] < 0) return;
    PoolOwners_[Slot] = -1;
    PoolOrder_.erase(std::remove(PoolOrder_.begin(), PoolOrder_.end(), Slot), PoolOrder_.end());
    PoolQueue_.erase(std::remove_if(PoolQueue_.begin(), PoolQueue_.end(), [Slot](const FragmentJob& Job) { return Job.Slot == Slot; }),
                     PoolQueue_.end());
    PoolFree_.push_back(Slot);
}

uint32_t OccupantDistanceSpace::RecordFragments(CommandSequence& Sequence)
{
    if (!FragmentProgram_ || PoolQueue_.empty()) return 0;
    const size_t Count = std::min<size_t>(FragmentPool.JobsPerRefresh, PoolQueue_.size());
    std::vector<FragmentJob> Jobs(PoolQueue_.begin(), PoolQueue_.begin() + static_cast<std::ptrdiff_t>(Count));
    PoolQueue_.erase(PoolQueue_.begin(), PoolQueue_.begin() + static_cast<std::ptrdiff_t>(Count));
    std::vector<uint32_t> JobWords(Jobs.size() * 8, 0u);
    std::vector<float>    Geometry(Jobs.size() * PoolStride * 4, 0.0f);
    for (size_t Index = 0; Index < Jobs.size(); ++Index)
    {
        const FragmentJob& Job = Jobs[Index];
        for (int Axis = 0; Axis < 3; ++Axis) JobWords[Index * 8 + Axis] = Job.Volume.TexelOrigin[Axis];
        JobWords[Index * 8 + 3] = static_cast<uint32_t>(Job.Fragment.Triangles.size()) | (static_cast<uint32_t>(Job.Fragment.Planes.size()) << 16);
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            const float Scalar = static_cast<float>(Job.Volume.Minimum[Axis]);
            std::memcpy(&JobWords[Index * 8 + 4 + Axis], &Scalar, 4);
        }
        const float Voxel = static_cast<float>(Job.Volume.VoxelExtent);
        std::memcpy(&JobWords[Index * 8 + 7], &Voxel, 4);
        const size_t Base = Index * PoolStride * 4;
        for (size_t Triangle = 0; Triangle < Job.Fragment.Triangles.size(); ++Triangle)
        {
            for (int Corner = 0; Corner < 3; ++Corner)
            {
                for (int Axis = 0; Axis < 3; ++Axis)
                {
                    Geometry[Base + (Triangle * 3 + Corner) * 4 + Axis] = static_cast<float>(Job.Fragment.Triangles[Triangle][Corner][Axis]);
                }
            }
        }
        for (size_t Plane = 0; Plane < Job.Fragment.Planes.size(); ++Plane)
        {
            for (int Component = 0; Component < 4; ++Component)
            {
                Geometry[Base + (FragmentPool.MaxTriangles * 3 + Plane) * 4 + Component] = static_cast<float>(Job.Fragment.Planes[Plane][Component]);
            }
        }
    }
    Exchange_.WriteRegion(JobRegion_, 0, JobWords.data(), JobWords.size() * 4);
    Exchange_.WriteRegion(GeometryRegion_, 0, Geometry.data(), Geometry.size() * 4);
    Sequence.BindProgram(FragmentProgram_);
    Sequence.BindGroup(0, FragmentGroup_);
    Sequence.Dispatch(static_cast<uint32_t>(Jobs.size()));
    return static_cast<uint32_t>(Jobs.size());
}

} // namespace Frontier::GlobalIllumination
