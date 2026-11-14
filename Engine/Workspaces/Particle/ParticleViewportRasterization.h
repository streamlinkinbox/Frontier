/*============================================================================================================================================
                                                  PARTICLEVIEWPORTRASTERIZATION.H
============================================================================================================================================*/
// 🧩 Vulkan 1.3 GPU .slang particle viewport rasterizer (studio backdrop, 3D floor grid, strands/ribbons & billboards from live GPU SSBOs)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Particles/ParticleVulkanExecutor.h"
#include "Engine/Workspaces/Particle/ParticleViewportInteractor.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ParticleGpuSilhouette : uint32_t
{
    SoftDisc    = 0u, // [-] - Smooth anti-aliased sphere/droplet disc
    SparkStreak = 1u, // [-] - Velocity-aligned glowing spark streak
    GlowOrb     = 2u, // [-] - Luminous plasma / bokeh orb with hot core
    ShardQuad   = 3u, // [-] - Beveled fracture shard / MoGraph cube quad
    ShockRing   = 4u, // [-] - Hollow luminous expanding shockwave / EMP / radar ring
};

enum class ParticleGpuBlendMode : uint32_t
{
    Alpha    = 0u, // [-] - Standard straight-alpha translucency
    Additive = 1u, // [-] - Premultiplied additive optical glow
};

enum class ParticleSolidProfile : uint32_t
{
    Cube       = 0u, // [-] - Extruded MoGraph lattice / derez 3D cube
    Shard      = 1u, // [-] - Faceted crystalline ice / basalt fracture shard
    Spike      = 2u, // [-] - Octagonal metallic Rosensweig ferrofluid spike
    WingedBoid = 3u, // [-] - Articulated flapping winged boid creature
    CoinDisc   = 4u, // [-] - Tumbling hexagonal metallic coin / token
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     RENDER REQUEST
//------------------------------------------------------------------------------------------------------------------------

struct ParticleViewportRenderRequest
{
    uint32_t                  WidthPx              = 1280u;                             // [px]  - Offscreen color target width
    uint32_t                  HeightPx             = 720u;                              // [px]  - Offscreen color target height
    ParticleCameraRig         Camera               {};                                  // [-]   - +Z Up orbit camera rig
    float                     TimeSeconds          = 0.0f;                              // [s]   - Elapsed simulation time
    uint32_t                  ActiveCount          = 0u;                                // [cnt] - Active GPU particle slots
    ParticleGpuSilhouette     Silhouette           = ParticleGpuSilhouette::SoftDisc;   // [-]   - Billboard visual silhouette
    ParticleGpuBlendMode      BlendMode            = ParticleGpuBlendMode::Alpha;       // [-]   - Blending mode
    ParticleSolidProfile      SolidProfile         = ParticleSolidProfile::Cube;        // [-]   - 3D oriented solid profile
    float                     SizeScale            = 1.0f;                              // [x]   - Visual radius multiplier
    float                     EmissiveBoost        = 1.15f;                             // [x]   - Scene luminance boost
    float                     RibbonWidthMeters    = 0.03f;                             // [m]   - Minimum strand ribbon half-width
    bool                      ShowFloorGrid        = true;                              // [-]   - Render GPU 3D perspective floor grid
    bool                      RenderStrands        = false;                             // [-]   - Render GPU strand/cloth ribbons
    bool                      RenderBillboards     = true;                              // [-]   - Render GPU billboard sprites
    bool                      RenderOrientedSolids = false;                             // [-]   - Render GPU 3D quaternion-oriented solids
    bool                      TransparentBackdrop  = false;                             // [-]   - Skip opaque studio backdrop for world compositing
};

//------------------------------------------------------------------------------------------------------------------------
//                                            PARTICLEVIEWPORTRASTERIZATION
//------------------------------------------------------------------------------------------------------------------------

class ParticleViewportRasterization
{
public:
    ParticleViewportRasterization() noexcept;
    ~ParticleViewportRasterization() noexcept;

    ParticleViewportRasterization(const ParticleViewportRasterization&)            = delete;
    ParticleViewportRasterization& operator=(const ParticleViewportRasterization&) = delete;

    [[nodiscard]] bool Initialize(
        SimulationSpace::Particles::ParticleVulkanExecutor& ComputeExecutor,
        const std::filesystem::path&                        ShaderBinaryDir,
        std::string&                                        OutDiagnostic) noexcept;

    void Finalize() noexcept;

    [[nodiscard]] bool QueryReady() const noexcept;

    [[nodiscard]] bool RenderOffscreen(
        const ParticleViewportRenderRequest& Request,
        std::string&                         OutDiagnostic) noexcept;

    [[nodiscard]] std::span<const uint8_t> QueryColorRgbaPixels() const noexcept;
    [[nodiscard]] uint32_t                 QueryWidthPx() const noexcept;
    [[nodiscard]] uint32_t                 QueryHeightPx() const noexcept;

private:
    struct RasterImplementation;
    std::unique_ptr<RasterImplementation> Handle{};
};

} // namespace Frontier::Workspaces
