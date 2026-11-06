#pragma once
/*==========================================================================================================================================
                                                            PYROVOLUMESOLVER.H
==========================================================================================================================================*/
// 🧩 1:1 C++ Vulkan 1.1 3D Eulerian Voxel Lattice Pyro Solver & Volumetric Raymarcher.
//    Ported 1:1 from Experimental/Fluid/src/engine-webgl2.js, camera.js, and FlipbookSequence.js,
//    with 3D SDF world solid/wake coupling and multi-source tyre smoke & fire injection. Zero OpenGL dependencies.

#include "Frontier/Engine/Rendering/VulkanContext.h"
#include "Frontier/Engine/VolumetricDynamics/GasPresetLibrary.h"
#include "Frontier/Engine/VolumetricDynamics/PyroCamera.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

//----------------------------------------------------------------------------------------------------------------------
//                                               DOMAIN & SOURCE RECORDS
//----------------------------------------------------------------------------------------------------------------------

struct PyroBoundsBox
{
    std::array<float, 3> BoxMin          = { -0.925f, -0.60f, -0.925f };   // [m]
    std::array<float, 3> BoxMax          = {  0.925f,  1.50f,  0.925f };   // [m]
    std::array<float, 3> DomainScale     = {  1.85f,   1.75f,  1.85f  };   // [-]
    float                EffectiveWidth  = 1.85f;                          // [m]
    float                EffectiveHeight = 2.10f;                          // [m]
    float                SurgeMultiplier = 1.0f;                           // [-]
};

struct PyroExplosionOptions
{
    std::array<float, 3> CenterUvw        = { 0.5f, 0.18f, 0.5f };
    std::array<float, 3> DirectionalVel   = { 0.0f, 0.0f, 0.0f };
    float                RadiusOverride   = -1.0f;
    float                StrengthOverride = -1.0f;
    float                TempOverride     = -1.0f;
    float                FuelOverride     = -1.0f;
    float                SmokeOverride    = -1.0f;
    int32_t              LobesOverride    = -1;
    int                  ShrapnelOverride = -1;
    float                DelaySeconds     = 0.0f;
};

struct PyroExternalSource
{
    std::array<float, 3> PositionUvw      = { 0.5f, 0.1f, 0.5f };   // [-] - normalised [0..1]^3 inside domain
    float                NormalizedRadius = 0.08f;                  // [-]
    std::array<float, 3> VelocityPyro     = { 0.0f, 1.0f, 0.0f };   // [m/s] - Pyro Y-up velocity
    float                Swirl            = 0.0f;                   // [-]
    float                SmokeRate        = 0.0f;                   // [1/s]
    float                Temperature      = 0.0f;                   // [K]
    float                FuelRate         = 0.0f;                   // [1/s]
    float                SootRate         = 0.0f;                   // [1/s]
};

//----------------------------------------------------------------------------------------------------------------------
//                                            VULKAN GPU PYRO VOLUME SOLVER
//----------------------------------------------------------------------------------------------------------------------

class PyroVolumeSolver
{
  public:
    PyroVolumeSolver() = default;
    ~PyroVolumeSolver() = default;

    PyroVolumeSolver(const PyroVolumeSolver&)            = delete;
    PyroVolumeSolver& operator=(const PyroVolumeSolver&) = delete;

    bool Initialize(VulkanContext& Ctx, const GasSettings& InitialSettings, std::string& OutDiagnostic);
    void Shutdown(VulkanContext& Ctx) noexcept;

    bool EnsureLatticeResolution(VulkanContext& Ctx, int32_t RequestedResolution);
    void ClearLattice(VulkanContext& Ctx);

    void UploadWorldSdfAtlas(VulkanContext& Ctx, const float* RgbaTexels, size_t FloatCount);
    void DisableWorldSdf() noexcept { WorldSdfEnabled = false; }

    void AssignExternalSources(const PyroExternalSource* Sources, int Count) noexcept;
    void AssignBrush(const std::array<float, 3>& PosUvw, const std::array<float, 3>& VelPyro, bool Active) noexcept;

    void TriggerExplosion(const GasSettings& Settings, const PyroExplosionOptions& Options = {});
    void TriggerBurstSequence(const GasSettings& Settings, const char* PatternName, const std::array<float, 3>* CustomCenter = nullptr);

    void AdvanceSimulation(VulkanContext& Ctx, GasSettings& Settings, float RawDtSeconds);

    bool RenderStandaloneViewport(VulkanContext& Ctx, const GasSettings& Settings, const PyroCameraFrame& Cam,
                                  int Width, int Height, bool TransparentExport = false);

    // Records premultiplied Beer-Lambert + blackbody volume composite + embers directly into an active World MRT RenderPass!
    void RecordCompositeIntoWorldTarget(VulkanContext& Ctx, VkCommandBuffer Cmd, const GasSettings& Settings,
                                        const PyroCameraFrame& Cam, int TargetWidth, int TargetHeight,
                                        VulkanImageTarget& SceneLinearDepthImage);

    // Ensures internal volume atlases are in SHADER_READ_ONLY_OPTIMAL before beginning a World MRT RenderPass
    void PrepareAtlasesForWorldComposite(VulkanContext& Ctx, VkCommandBuffer Cmd);

    bool ReadViewportRgba(VulkanContext& Ctx, std::vector<uint8_t>& OutTopDownRgba, int& OutWidth, int& OutHeight);

    bool BakeFlipbookSheet(VulkanContext& Ctx, GasSettings Settings, const PyroOrbitCamera& Cam,
                           int Columns, int Rows, int TilePixels, float FrameStepSeconds,
                           std::vector<uint8_t>& OutTopDownRgba);

    [[nodiscard]] PyroBoundsBox ComputeBoundsBox(const GasSettings& Settings) const noexcept;
    [[nodiscard]] uint64_t QueryViewportUiToken() const noexcept { return ViewportImage.UiToken; }
    [[nodiscard]] int QueryViewportWidth() const noexcept { return ViewportImage.Width; }
    [[nodiscard]] int QueryViewportHeight() const noexcept { return ViewportImage.Height; }
    [[nodiscard]] int32_t QueryLatticeResolution() const noexcept { return LatticeRes; }
    [[nodiscard]] int32_t QueryTilesX() const noexcept { return TilesX; }
    [[nodiscard]] int32_t QueryTilesY() const noexcept { return TilesY; }
    [[nodiscard]] int32_t QueryAtlasWidth() const noexcept { return AtlasWidth; }
    [[nodiscard]] int32_t QueryAtlasHeight() const noexcept { return AtlasHeight; }
    [[nodiscard]] float QuerySimulationSeconds() const noexcept { return SimulationTime; }
    [[nodiscard]] uint32_t QueryStepCount() const noexcept { return StepCounter; }

  private:
    struct BallisticProjectile
    {
        std::array<float, 3> Pos     = { 0.5f, 0.2f, 0.5f };
        std::array<float, 3> Vel     = { 0.0f, 1.0f, 0.0f };
        float                Age     = 0.0f;
        float                MaxLife = 1.8f;
        float                Radius  = 0.06f;
    };

    struct PendingBlastRecord
    {
        std::array<float, 3> Center         = { 0.5f, 0.18f, 0.5f };
        std::array<float, 3> DirectionalVel = { 0.0f, 0.0f, 0.0f };
        float                Radius         = 0.18f;
        float                Strength       = 9.0f;
        float                Temperature    = 5.5f;
        float                Fuel           = 4.5f;
        float                Smoke          = 3.5f;
        int32_t              Lobes          = 5;
        float                Seed           = 1.0f;
        float                Delay          = 0.0f;
        bool                 SpawnShrapnel  = false;
    };

    bool AllocateLatticeTargets(VulkanContext& Ctx, int32_t NewRes, std::string& OutDiagnostic);
    void DestroyLatticeTargets(VulkanContext& Ctx) noexcept;
    bool EnsureViewportTarget(VulkanContext& Ctx, int Width, int Height);

    VkPipeline                     PipeClearSingle      = VK_NULL_HANDLE;
    VkPipeline                     PipeClearDual        = VK_NULL_HANDLE;
    VkPipeline                     PipeSplat            = VK_NULL_HANDLE;
    VkPipeline                     PipeAdvect           = VK_NULL_HANDLE;
    VkPipeline                     PipeMacCormack       = VK_NULL_HANDLE;
    VkPipeline                     PipeCurl             = VK_NULL_HANDLE;
    VkPipeline                     PipeVorticity        = VK_NULL_HANDLE;
    VkPipeline                     PipeBuoyancy         = VK_NULL_HANDLE;
    VkPipeline                     PipeDivergence       = VK_NULL_HANDLE;
    VkPipeline                     PipeJacobi           = VK_NULL_HANDLE;
    VkPipeline                     PipeGradient         = VK_NULL_HANDLE;
    VkPipeline                     PipeRaymarchStandalone= VK_NULL_HANDLE;
    VkPipeline                     PipeRaymarchWorldMrt = VK_NULL_HANDLE;
    VkPipeline                     PipeEmbersStandalone = VK_NULL_HANDLE;
    VkPipeline                     PipeEmbersWorldMrt   = VK_NULL_HANDLE;

    VulkanBufferResource           EmberSeedBuffer{};

    int32_t                        LatticeRes           = 0;
    int32_t                        TilesX               = 0;
    int32_t                        TilesY               = 0;
    int32_t                        AtlasWidth           = 0;
    int32_t                        AtlasHeight          = 0;

    VulkanImageTarget              VelImg[3]{};         // 0, 1 ping-pong + 2 scratch for MacCormack
    VulkanImageTarget              ThermoImg[3]{};      // 0, 1 ping-pong + 2 scratch for MacCormack
    VulkanImageTarget              CurlImg{};
    VulkanImageTarget              DivImg{};
    VulkanImageTarget              PresImg[2]{};        // 0, 1 ping-pong pressure + irradiance
    VulkanImageTarget              WorldSdfImg{};

    VkFramebuffer                  FboDual[3]           = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkFramebuffer                  FboVelSingle[2]      = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkFramebuffer                  FboCurl              = VK_NULL_HANDLE;
    VkFramebuffer                  FboDiv               = VK_NULL_HANDLE;
    VkFramebuffer                  FboPres[2]           = { VK_NULL_HANDLE, VK_NULL_HANDLE };

    int                            ReadSlotIdx          = 0;
    int                            ReadPresIdx          = 0;

    VulkanImageTarget              ViewportImage{};
    VkFramebuffer                  ViewportFboClear     = VK_NULL_HANDLE;
    VkFramebuffer                  ViewportFboLoad      = VK_NULL_HANDLE;

    bool                           WorldSdfEnabled      = false;
    std::array<PyroExternalSource, 8> ExternalSources{};
    int                            ExternalSourceCount  = 0;

    bool                           BrushActive          = false;
    std::array<float, 3>           BrushPosUvw          = { 0.5f, 0.25f, 0.5f };
    std::array<float, 3>           BrushVelPyro         = { 0.0f, 0.0f, 0.0f };

    std::vector<PendingBlastRecord>  PendingBlasts{};
    std::vector<BallisticProjectile> Projectiles{};

    float                          DynamicSurge         = 1.0f;
    float                          SimulationTime       = 0.0f;
    uint32_t                       StepCounter          = 0u;
    uint32_t                       BlastSeedCounter     = 1u;
    bool                           Initialized          = false;
};

} // namespace Frontier
