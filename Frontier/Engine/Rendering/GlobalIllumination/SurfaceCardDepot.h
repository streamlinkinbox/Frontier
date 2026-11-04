/*==========================================================================================================================================
                                                          SURFACECARDDEPOT.H
==========================================================================================================================================*/
// 🧩 Oriented surface cards per occupant in one texel space: captured once, relit by a scheduled tile budget (SurfaceCardDepot.js).

#pragma once

#include "SceneStructure.h"
#include "ShaderInterchange.h"

namespace Frontier::GlobalIllumination
{

struct SurfaceCardConfiguration
{
    uint32_t Extent       = 2048;   // [texel] card texel space edge
    double   DensityScale = 1.0;    // [-]
};

struct SurfaceCard
{
    Vector3                 LocalMinimum{};   // [m]
    Vector3                 LocalMaximum{};   // [m]
    uint32_t                Axis     = 0;     // [-] 0 +X, 1 −X, 2 +Y, 3 −Y, 4 +Z, 5 −Z
    uint32_t                Occupant = 0;
    std::array<uint32_t, 2> TexelExtent{};    // [texel]
    std::array<uint32_t, 2> TexelOrigin{};    // [texel]
    std::array<uint32_t, 2> TexelAllotment{}; // [texel] the rectangle packed for it (a reassigned card may use less of it)
};

// 📝 One 8×8 relighting tile: card index and (TileY << 16) | TileX.
using CardTile = std::array<uint32_t, 2>;

class SurfaceCardDepot
{
  public:
    SurfaceCardDepot(DeviceExchange& Exchange, ShaderInterchange& Shaders, const SurfaceCardConfiguration& Configuration);

    void AllocateCards(SceneStructure& Scene);
    void RebuildTileLists(const SceneStructure& Scene);
    void WarmOccupants(const SceneStructure& Scene, const std::vector<uint32_t>& Indices, uint32_t Refreshes = 6);
    bool PackCards();
    [[nodiscard]] std::vector<uint8_t> EncodeCardProfiles() const;

    void ConstructResources(const std::shared_ptr<DeviceRegion>& RefreshRegion, const std::shared_ptr<DeviceRegion>& OccupantRegion,
                            const std::shared_ptr<DeviceRegion>& MaterialRegion, const std::shared_ptr<DescriptorLayout>& DistanceLayout,
                            const std::shared_ptr<DeviceRegion>& LuminaireRegion);
    std::shared_ptr<DescriptorGroup> ConstructSamplingGroup(const std::shared_ptr<DeviceRegion>& OccupantRegion,
                                                            const std::shared_ptr<DeviceTextureView>& SkyView,
                                                            const std::shared_ptr<DeviceSampler>& SkySampler,
                                                            const std::shared_ptr<DeviceTextureView>& MoonAtlas,
                                                            const std::shared_ptr<DeviceTextureView>& Catalogue);
    void CaptureCards(const SceneStructure& Scene, const std::shared_ptr<DeviceRegion>& VertexRegion);

    // 📝 Beyond the demo. A reserve occupant that took another shape gets cards for its new bounds, in its own rectangles when they
    //    fit and else in the rows the packing left free (or clamped to its own when those run out); the indices of its cards come back.
    //    RecaptureCards captures only the given cards again, clearing their rectangles and their radiosity first.
    std::vector<uint32_t> ReassignOccupantCards(SceneStructure& Scene, uint32_t Occupant);
    void RecaptureCards(const SceneStructure& Scene, const std::shared_ptr<DeviceRegion>& VertexRegion,
                        const std::vector<uint32_t>& CardIndices);
    void InvalidateLighting();
    void ScheduleTiles(uint32_t TileBudget);
    void RecordLighting(CommandSequence& Sequence, const std::shared_ptr<DescriptorGroup>& DistanceGroup,
                        const std::shared_ptr<DescriptorGroup>& SamplingGroup, bool RadiosityEnabled);

    SurfaceCardConfiguration Configuration{};
    std::vector<SurfaceCard> Cards{};
    std::vector<CardTile>    DynamicTiles{};
    std::vector<CardTile>    StaticTiles{};
    std::vector<std::vector<CardTile>> OccupantTiles{};
    std::vector<CardTile>    PriorityTiles{};
    uint32_t                 PriorityRefreshes = 0;
    uint32_t                 Cursor            = 0;
    uint32_t                 WarmRefreshes     = 6;
    uint32_t                 ScheduledTiles    = 0;
    uint64_t                 OccupiedTexels    = 0;
    uint64_t                 TileTotal         = 0;
    double                   DensityScaleUsed  = 1.0;
    uint32_t                 GroundFirstCard   = 0;
    uint32_t                 GroundStripCard   = 0;
    uint32_t                 SiteStride        = 0;

    std::shared_ptr<DeviceTexture>     AlbedoTexture{}, NormalTexture{}, IncandescenceTexture{}, DepthTexture{}, DirectTexture{}, FinalTexture{};
    std::shared_ptr<DeviceTextureView> AlbedoView{}, NormalView{}, IncandescenceView{}, DepthView{}, DirectView{}, FinalView{};
    std::shared_ptr<DeviceRegion>      CardRegion{}, SiteRegion{}, TileRegion{}, WorkRegion{};
    std::shared_ptr<DescriptorLayout>  DirectLayout{}, RadiosityLayout{}, FinalLayout{}, SamplingLayout{}, CaptureLayout{};
    std::shared_ptr<DescriptorGroup>   DirectGroup{}, RadiosityGroup{}, FinalGroup{}, CaptureGroup{};
    std::shared_ptr<DeviceProgram>     DirectProgram{}, RadiosityProgram{}, FinalProgram{}, CaptureProgram{};

  private:
    [[nodiscard]] bool AllotRuntimeRectangle(uint32_t Width, uint32_t Height, std::array<uint32_t, 2>& Origin);
    void               EncodeCard(const SurfaceCard& Card, uint8_t* Target) const;

    DeviceExchange&    Exchange_;
    ShaderInterchange& Shaders_;
    uint32_t           RuntimeShelfX_      = 0;   // [texel] the free rows below the packing, filled shelf by shelf at run time
    uint32_t           RuntimeShelfY_      = 0;   // [texel]
    uint32_t           RuntimeShelfHeight_ = 0;   // [texel]
    uint64_t           TileCapacity_       = 0;   // [-] tiles the tile region holds
    std::shared_ptr<DeviceTexture>     CaptureDepth_{};
    std::shared_ptr<DeviceTextureView> CaptureDepthView_{};
};

} // namespace Frontier::GlobalIllumination
