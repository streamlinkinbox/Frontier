/*==========================================================================================================================================
                                                        OCCUPANTDISTANCESPACE.H
==========================================================================================================================================*/
// 🧩 Per-facet-set signed distance volumes stacked in one 3D texel space, transferred on the device (OccupantDistanceSpace.js).

#pragma once

#include "SceneStructure.h"
#include "ShaderInterchange.h"

#include <deque>
#include <functional>
#include <optional>

namespace Frontier::GlobalIllumination
{

// 📝 The fragment pool: cubes of Edge³ texels below the stacked volumes, each holding one live fragment's volume.
struct FragmentPoolSpecification
{
    uint32_t Edge         = 12;    // [texel] per axis, two of them margin on each side
    uint32_t Slots        = 256;   // [-]
    uint32_t MaxTriangles = 48;    // [-]
    uint32_t MaxPlanes    = 40;    // [-]
    uint32_t JobsPerRefresh = 48;  // [-] volumes computed per refresh at most
};
inline constexpr FragmentPoolSpecification FragmentPool{};

struct FragmentGeometry
{
    std::vector<std::array<Vector3, 3>> Triangles{};
    std::vector<std::array<double, 4>>  Planes{};
    Vector3                             Minimum{};
    Vector3                             Maximum{};
};

class OccupantDistanceSpace
{
  public:
    OccupantDistanceSpace(DeviceExchange& Exchange, ShaderInterchange& Shaders);

    // ① choose each volume's voxel and resolution; ② stack them along z with one-texel gaps; ③ transfer slab by slab.
    bool ConstructVolumes(SceneStructure& Scene, double VoxelScale, std::string* OutDiagnostic = nullptr);
    [[nodiscard]] const OccupantVolume* QueryVolume(const SceneStructure& Scene, uint32_t FacetSetIndex) const;

    void ConstructFragmentProgram();
    std::optional<OccupantVolume> AllocateFragment(uint32_t Owner, const FragmentGeometry& Fragment,
                                                   const std::function<void(uint32_t)>& Evict);
    void     ReleaseFragment(const OccupantVolume& Volume, uint32_t Slot);
    uint32_t RecordFragments(CommandSequence& Sequence);

    std::vector<OccupantVolume>        Volumes{};
    std::shared_ptr<DeviceTexture>     Texture{};
    std::shared_ptr<DeviceTextureView> TextureView{};
    std::array<uint32_t, 3>            SpaceExtent{0, 0, 0};

  private:
    struct FragmentJob
    {
        uint32_t         Slot = 0;
        OccupantVolume   Volume{};
        FragmentGeometry Fragment{};
    };

    DeviceExchange&                   Exchange_;
    ShaderInterchange&                Shaders_;
    std::vector<std::array<uint32_t, 3>> PoolOrigins_{};
    std::vector<int64_t>              PoolOwners_{};
    std::vector<uint32_t>             PoolFree_{};
    std::deque<FragmentJob>           PoolQueue_{};
    std::deque<uint32_t>              PoolOrder_{};
    std::shared_ptr<DescriptorLayout> FragmentLayout_{};
    std::shared_ptr<DeviceProgram>    FragmentProgram_{};
    std::shared_ptr<DeviceRegion>     JobRegion_{};
    std::shared_ptr<DeviceRegion>     GeometryRegion_{};
    std::shared_ptr<DescriptorGroup>  FragmentGroup_{};
};

} // namespace Frontier::GlobalIllumination
