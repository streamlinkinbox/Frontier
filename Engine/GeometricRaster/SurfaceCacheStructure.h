//============================================================================================================================================
//                                                   SURFACECACHESTRUCTURE.H
//============================================================================================================================================
// 📦 2D Surface Cache atlas storing parameterised surface irradiance, direct illuminant caching, and multi-bounce radiance.

#pragma once

#if defined(_MSC_VER)
    #pragma warning(disable: 4324)                              // Disable structure padding alignment warning under /WX
#endif

#include "GlobalDistanceFieldSpace.h"
#include <vector>
#include <cstdint>
#include <atomic>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                SURFACE CACHE TEXEL
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) SurfaceCacheTexel
{
    Vector3                 WorldPosition;                      // [m] surface point in world space
    Vector3                 SurfaceNormal;                      // [-] outward surface unit normal
    Vector3                 AlbedoColour;                       // [-] material base colour reflectancy
    Vector3                 SpecularF0;                         // [-] Fresnel reflectance at normal incidence (F0)
    Vector3                 DirectRadiance;                     // [W/m²·sr] direct illuminant contribution (diffuse + specular)
    Vector3                 IrradianceBounce;                   // [W/m²·sr] indirect multi-bounce irradiance
    float                   SurfaceRoughness = 0.35f;           // [-] surface roughness parameter
    float                   Metallic         = 0.0f;            // [-] conductor vs dielectric parameter
    float                   ClearCoatWeight  = 0.0f;            // [-] clear coat layer weight
    float                   ClearCoatRough   = 0.05f;           // [-] clear coat layer roughness
    uint32_t                CardIndex        = 0u;              // [-] owning surface card index
    bool                    IsAllocated      = false;           // [-] occupancy flag in atlas layout
    bool                    IsDirty          = true;            // [-] requires direct radiance baking
};

//------------------------------------------------------------------------------------------------------------------------
//                                                SURFACE CARD RECORD
//------------------------------------------------------------------------------------------------------------------------

struct SurfaceCard
{
    uint32_t                CardId           = 0u;              // [-] unique card identifier
    uint32_t                InstanceId       = 0u;              // [-] scene instance identity
    Vector3                 Center           = { 0.0f, 0.0f, 0.0f }; // [m] world space center
    float                   Radius           = 0.55f;           // [m] bounding radius
    Vector3                 BaseAlbedo       = { 0.85f, 0.40f, 0.20f }; // [-] base diffuse colour
    float                   Metallic         = 0.0f;            // [-] metallic parameter
    float                   Roughness        = 0.25f;           // [-] roughness parameter
    float                   ClearCoat        = 0.0f;            // [-] clear coat weight
    uint32_t                AtlasOffsetX     = 0u;              // [texels] atlas X origin
    uint32_t                AtlasOffsetY     = 0u;              // [texels] atlas Y origin
    uint32_t                TileWidth        = 256u;            // [texels] card width
    uint32_t                TileHeight       = 256u;            // [texels] card height
    bool                    IsDirty          = true;            // [-] requires re-baking
};

//------------------------------------------------------------------------------------------------------------------------
//                                            ASYNC BAKE SCHEDULER STATE
//------------------------------------------------------------------------------------------------------------------------

struct SurfaceCacheBakeState
{
    Vector3                 IlluminantDirection = { 0.5f, -0.5f, 0.7071f };
    Vector3                 IlluminantRadiance  = { 2.2f, 2.1f, 1.9f };
    float                   LightAngularSize    = 0.175f;
    uint32_t                CurrentTexelCursor  = 0u;
    uint32_t                TotalTexels         = 0u;
    uint32_t                BakedPassCounter    = 0u;
    bool                    IsBakingActive      = false;
    bool                    IsCompleted         = true;
};

//------------------------------------------------------------------------------------------------------------------------
//                                               SURFACE CACHE STRUCTURE
//------------------------------------------------------------------------------------------------------------------------

class SurfaceCacheStructure
{
public:
    SurfaceCacheStructure() noexcept = default;

    SurfaceCacheStructure(uint32_t InAtlasWidth, uint32_t InAtlasHeight) noexcept;

    // Registers a surface card for a ShaderBall instance with rich material parameters
    uint32_t RegisterShaderBallCard(uint32_t InstanceId,
                                    Vector3 Center,
                                    float Radius = 0.55f,
                                    Vector3 BaseAlbedo = { 0.85f, 0.40f, 0.20f },
                                    float Metallic = 0.0f,
                                    float Roughness = 0.25f,
                                    float ClearCoat = 0.0f) noexcept;

    // Updates a card's world position (marks texels dirty for incremental baking)
    void UpdateCardTransform(uint32_t CardId, Vector3 NewCenter) noexcept;

    // Parameterizes a single hero ShaderBall occupying the whole atlas (legacy API)
    void ParameterizeShaderBall(Vector3 Center, float Radius = 0.55f, Vector3 BaseAlbedo = { 0.85f, 0.40f, 0.20f }) noexcept;

    // Synchronous full-atlas direct lighting update using multithreaded OpenMP execution
    void BakeDirectLightingMultithreaded(Vector3 IlluminantDirection,
                                         Vector3 IlluminantRadiance,
                                         const GlobalDistanceFieldSpace& DistanceField,
                                         float LightAngularSize = 0.175f) noexcept;

    // Initiates an asynchronous / time-sliced direct lighting bake
    void QueueAsyncDirectBake(Vector3 IlluminantDirection,
                              Vector3 IlluminantRadiance,
                              float LightAngularSize = 0.175f) noexcept;

    // Steps the asynchronous background bake by a given texel budget (returns true when current pass finishes)
    bool StepAsyncDirectBake(const GlobalDistanceFieldSpace& DistanceField,
                             uint32_t TexelBudget = 16384u) noexcept;

    // Evaluates direct sun illuminant across valid surface cache texels (legacy API)
    void UpdateDirectLighting(Vector3 IlluminantDirection,
                              Vector3 IlluminantRadiance,
                              const GlobalDistanceFieldSpace& DistanceField,
                              float LightAngularSize = 0.175f) noexcept;

    // Propagates indirect irradiance by gathering radiance from other surface cache texels via distance field rays
    void PropagateIndirectIrradiance(const GlobalDistanceFieldSpace& DistanceField,
                                     uint32_t RaysPerTexel = 8u) noexcept;

    // Instant O(1) bilinear lookup of surface radiance at hit coordinates
    [[nodiscard]] Vector3 SampleRadiance(Vector3 WorldPosition,
                                         Vector3 SurfaceNormal,
                                         float TextureCoordinateU,
                                         float TextureCoordinateV) const noexcept;

    // Continuous spherical / UV projection lookup using InstanceId and world normal
    [[nodiscard]] Vector3 SampleRadianceFromWorld(Vector3 WorldPosition,
                                                  Vector3 SurfaceNormal,
                                                  uint32_t InstanceId = 0xFFFFFFFFu) const noexcept;

    // Accessors
    [[nodiscard]] uint32_t GetAtlasWidth() const noexcept { return AtlasWidth; }
    [[nodiscard]] uint32_t GetAtlasHeight() const noexcept { return AtlasHeight; }
    [[nodiscard]] size_t GetTexelCount() const noexcept { return TexelAtlas.size(); }
    [[nodiscard]] const SurfaceCacheBakeState& GetBakeState() const noexcept { return BakeState; }
    [[nodiscard]] const std::vector<SurfaceCard>& GetCards() const noexcept { return Cards; }

    [[nodiscard]] const SurfaceCacheTexel& GetTexel(uint32_t X, uint32_t Y) const noexcept
    {
        return TexelAtlas[static_cast<size_t>(Y) * AtlasWidth + X];
    }

    [[nodiscard]] SurfaceCacheTexel& GetMutableTexel(uint32_t X, uint32_t Y) noexcept
    {
        return TexelAtlas[static_cast<size_t>(Y) * AtlasWidth + X];
    }

private:
    uint32_t                       AtlasWidth       = 512u;            // [-] atlas pixel width
    uint32_t                       AtlasHeight      = 512u;            // [-] atlas pixel height
    std::vector<SurfaceCacheTexel> TexelAtlas;                         // [-] contiguous surface cache texel storage
    std::vector<SurfaceCard>       Cards;                              // [-] registered surface cards
    SurfaceCacheBakeState          BakeState;                          // [-] background baking progress & parameters
};

} // namespace Frontier
