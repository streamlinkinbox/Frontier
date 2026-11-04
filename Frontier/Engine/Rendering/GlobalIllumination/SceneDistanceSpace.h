/*==========================================================================================================================================
                                                         SCENEDISTANCESPACE.H
==========================================================================================================================================*/
// 🧩 The single scene distance field: four camera-centred toroidal cascades, rewritten only where invalidated (SceneDistanceSpace.js).

#pragma once

#include "SceneStructure.h"
#include "ShaderInterchange.h"

#include <optional>

namespace Frontier::GlobalIllumination
{

struct SceneDistanceConfiguration
{
    uint32_t ClipCount   = 4;       // [-] ≤ 4
    uint32_t Resolution  = 128;     // [voxel] a multiple of 8
    double   FinestVoxel = 0.125;   // [m]
    double   BandVoxels  = 6.0;     // [voxel]
    double   HitFraction = 0.2;     // [-]
};

class SceneDistanceSpace
{
  public:
    SceneDistanceSpace(DeviceExchange& Exchange, ShaderInterchange& Shaders, const SceneDistanceConfiguration& Configuration);

    void InvalidateBounds(const std::vector<WorldBounds>& BoundsList);
    void ConstructResources(const std::shared_ptr<DeviceTextureView>& VolumeSpaceView, const std::shared_ptr<DeviceRegion>& OccupantRegion);
    void ScheduleInvalidation(const Vector3& Focus, std::vector<WorldBounds> MovedBounds);
    [[nodiscard]] std::vector<uint8_t> EncodeDistanceConstants(uint32_t DistanceOccupantCount, uint32_t RefreshIndex) const;
    void UploadDistanceList(const std::vector<uint32_t>& List);
    void RecordUnion(CommandSequence& Sequence, uint32_t DistanceOccupantCount, uint32_t RefreshIndex);
    [[nodiscard]] WorldBounds QueryClipWindow(uint32_t Level) const;

    struct ClipWindow
    {
        double                                VoxelExtent = 0.0;   // [m]
        double                                Band        = 0.0;   // [m]
        std::optional<std::array<int64_t, 3>> OriginBrick{};       // [brick]
    };

    SceneDistanceConfiguration         Configuration{};
    uint32_t                           BricksPerAxis = 0;
    std::array<ClipWindow, 4>          Clips{};
    std::vector<int32_t>               RefreshBricks{};            // [-] x, y, z, level | causes << 8 per brick
    std::array<uint32_t, 4>            RefreshLevelCounts{0, 0, 0, 0};
    uint64_t                           TotalBricks = 0;
    bool                               Frozen      = false;
    std::vector<std::shared_ptr<DeviceTexture>>     Textures{};
    std::vector<std::shared_ptr<DeviceTextureView>> Views{};
    std::shared_ptr<DeviceRegion>      BrickRegion{};
    std::shared_ptr<DeviceRegion>      EpochRegion{};
    std::shared_ptr<DeviceRegion>      UniformRegion{};
    std::shared_ptr<DeviceRegion>      ListRegion{};
    std::shared_ptr<DeviceSampler>     VolumeSampler{};
    std::shared_ptr<DeviceSampler>     CascadeSampler{};
    std::shared_ptr<DescriptorLayout>  UnionLayout{};
    std::shared_ptr<DeviceProgram>     UnionProgram{};
    std::shared_ptr<DescriptorGroup>   UnionGroup{};
    std::shared_ptr<DescriptorLayout>  MarchLayout{};
    std::shared_ptr<DescriptorGroup>   MarchGroup{};

  private:
    DeviceExchange&          Exchange_;
    ShaderInterchange&       Shaders_;
    std::vector<WorldBounds> PendingBounds_{};
};

// 📝 A moving occupant invalidates the union of where the field last drew it and where it is now (2 mm / ~0.1°).
[[nodiscard]] std::vector<WorldBounds> ResolveMovedBounds(SceneStructure& Scene, bool Frozen = false);

} // namespace Frontier::GlobalIllumination
