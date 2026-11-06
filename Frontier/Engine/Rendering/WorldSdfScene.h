#pragma once
/*==========================================================================================================================================
                                                             WORLDSDFSCENE.H
==========================================================================================================================================*/
// 🧩 3D Signed-Distance World & Vulkan 1.1 GPU Renderer for Project Everest / Frontier Editor:
//    1. Evaluates exact 3D SDF + moving-vehicle velocity field for two-way Pyro fluid collision & car wake deflection.
//    2. Raymarches the 3D SDF Proving Grounds (pad, kerbs, ramp, SDF sphere, pillars, portal arch, cones) or Lobby studio
//       with SDF soft shadows, ambient occlusion, and up to 8 dynamic fire lights.
//    3. Hardware-rasterizes ControlVehicle (.facet Body, Tyre, Rim) with steered/spinning wheels, thermal glow, and
//       exact linear ray-distance output (RGBA16F) for depth-accurate volumetric smoke & fire compositing. Zero OpenGL.

#include "Frontier/Engine/Rendering/VulkanContext.h"
#include "Frontier/Engine/Scene/FacetArchive.h"
#include "Frontier/Engine/VolumetricDynamics/PyroVolumeSolver.h"
#include "Frontier/Engine/VolumetricDynamics/TyreSmokeIntake.h"
#include "Frontier/Game/SoftwarePresentation.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

struct WorldVehicleRig
{
    PresentationVehicle  Visual{};
    std::array<float, 3> VelocityMps    = { 0.0f, 0.0f, 0.0f };
    float                YawRateRad     = 0.0f;
    std::array<float, 4> WheelSteerRad  = { 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<float, 4> WheelSpinRad   = { 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<float, 4> WheelHeatRatio = { 0.0f, 0.0f, 0.0f, 0.0f }; // [0..1] thermal glow
};

struct WorldEnvironmentLight
{
    float SunElevationDeg  = 42.0f;
    float SunAzimuthDeg    = 35.0f;
    float SunIntensity     = 2.2f;
    float AmbientIntensity = 0.38f;
    float Exposure         = 1.15f;
};

struct WorldCameraView
{
    std::array<float, 3> Eye        = { -8.0f, -4.0f, 2.8f };   // [m] - Frontier Z-up eye
    std::array<float, 3> Forward    = {  0.9f,  0.4f, -0.15f }; // [-] - unit forward
    std::array<float, 3> Right      = {  0.4f, -0.9f,  0.0f };  // [-] - unit right
    std::array<float, 3> Up         = {  0.1f,  0.0f,  0.98f }; // [-] - unit up
    float                FovRadians = 0.959931f;                // [rad] - 55 deg vertical FOV
    float                Aspect     = 1.777778f;                // [-]
};

WorldCameraView BuildWorldCameraFromPresentation(const PresentationCamera& Orbit, float Aspect) noexcept;
PyroCameraBasis TransformWorldCameraToPyroDomain(const WorldCameraView& WorldCam,
                                                 const std::array<float, 3>& DomainOriginWorld) noexcept;

class WorldSdfScene
{
  public:
    WorldSdfScene() = default;
    ~WorldSdfScene() = default;

    WorldSdfScene(const WorldSdfScene&)            = delete;
    WorldSdfScene& operator=(const WorldSdfScene&) = delete;

    bool Initialize(VulkanContext& Ctx, const FacetArchiveData* Body, const FacetArchiveData* Tyre,
                    const FacetArchiveData* Rim, std::string& OutDiagnostic);
    void Shutdown(VulkanContext& Ctx) noexcept;

    [[nodiscard]] static float ResolveCourseHeight(float WorldX, float WorldY, bool ProvingGrounds) noexcept;

    void EvaluateSdfAndVelocity(const std::array<float, 3>& WorldPoint, bool ProvingGrounds, bool ObstaclesEnabled,
                                const std::vector<WorldVehicleRig>& Vehicles,
                                float& OutSignedDistance, std::array<float, 3>& OutSolidVelocityWorld) const noexcept;

    void BuildDomainSdfAtlas(const std::array<float, 3>& DomainOriginWorld, const PyroBoundsBox& Bounds,
                             int32_t LatticeRes, int32_t TilesX, int32_t TilesY,
                             bool ProvingGrounds, bool ObstaclesEnabled,
                             const std::vector<WorldVehicleRig>& Vehicles,
                             std::vector<float>& OutAtlasRgba) const;

    // [unit] - the same atlas as BuildDomainSdfAtlas, bit for bit (PerformanceParityValidation compares them), for the optimized execution path. The distance
    //          to the course (ground, kerbs, ramp, obstacles) does not change while the window, the lattice and the course settings stay as they are,
    //          so it is kept between calls; each call then looks only at the voxels that a car can reach. Either output may be null; the half-float
    //          output is what UploadRgba32fImage would have made of the float one (ConvertFloat32ToHalf16) and is what the upload takes.
    void BuildDomainSdfAtlasRetained(const std::array<float, 3>& DomainOriginWorld, const PyroBoundsBox& Bounds,
                                     int32_t LatticeRes, int32_t TilesX, int32_t TilesY,
                                     bool ProvingGrounds, bool ObstaclesEnabled,
                                     const std::vector<WorldVehicleRig>& Vehicles,
                                     std::vector<float>* OutAtlasRgba, std::vector<uint16_t>* OutAtlasHalf);

    // [count] - how many times the retained course distances have been computed (a window move or a lattice change makes one)
    [[nodiscard]] uint64_t QueryRetainedCourseRebuildCount() const noexcept { return RetainedRebuildCount; }

    bool BeginWorldFrame(VulkanContext& Ctx, int Width, int Height, bool ProvingGrounds, bool ObstaclesEnabled,
                         const WorldCameraView& Cam, const WorldEnvironmentLight& Env,
                         const std::vector<WorldVehicleRig>& Vehicles,
                         const std::vector<DynamicFireLight>& FireLights);

    void CompositeDomainIntoWorld(VulkanContext& Ctx, PyroVolumeSolver& Solver, const GasSettings& Settings,
                                  const std::array<float, 3>& DomainOriginWorld, const WorldCameraView& Cam);

    void EndWorldFrame(VulkanContext& Ctx);

    bool ReadWorldRgba(VulkanContext& Ctx, std::vector<uint8_t>& OutTopDownRgba, int& OutWidth, int& OutHeight);

    [[nodiscard]] uint64_t QueryColorUiToken() const noexcept { return SceneColorImage.UiToken; }
    [[nodiscard]] int QueryWidth() const noexcept { return SceneColorImage.Width; }
    [[nodiscard]] int QueryHeight() const noexcept { return SceneColorImage.Height; }

  private:
    struct VulkanFacetMesh
    {
        VulkanBufferResource VertexBuffer{};
        uint32_t             VertexCount = 0u;
        float                BoundsMinX  = -2.0f;
        float                BoundsMaxX  =  2.0f;
    };

    bool UploadFacetMesh(VulkanContext& Ctx, const FacetArchiveData& Archive, float ZRebase, float YSign,
                         VulkanFacetMesh& OutMesh, std::string& OutDiagnostic);
    void DestroyFacetMesh(VulkanContext& Ctx, VulkanFacetMesh& Mesh) noexcept;
    bool EnsureSceneTargets(VulkanContext& Ctx, int Width, int Height);

    struct RetainedCourseKey
    {
        std::array<float, 3> Origin{};
        std::array<float, 3> BoxMin{};
        std::array<float, 3> BoxMax{};
        int32_t              LatticeRes       = 0;
        int32_t              TilesX           = 0;
        int32_t              TilesY           = 0;
        bool                 ProvingGrounds   = false;
        bool                 ObstaclesEnabled = false;
        [[nodiscard]] bool operator==(const RetainedCourseKey&) const noexcept = default;
    };
    void RefreshRetainedCourse(const RetainedCourseKey& Key);

    RetainedCourseKey     RetainedKey{};
    bool                  RetainedReady        = false;
    std::vector<float>    RetainedDistances{};     // [m] - course distance of every voxel, in voxel order (x fastest, then y, then z)
    std::vector<uint16_t> RetainedHalfStatic{};    // [-] - the atlas with no car in it, as half floats, in atlas order
    uint64_t              RetainedRebuildCount = 0u;

    VkPipeline        PipeWorldSdfRaymarch = VK_NULL_HANDLE;
    VkPipeline        PipeVehicleOpaque    = VK_NULL_HANDLE;
    VkPipeline        PipeVehicleGhost     = VK_NULL_HANDLE;

    VulkanFacetMesh   BodyMesh{};
    VulkanFacetMesh   TyreMeshLeft{};
    VulkanFacetMesh   TyreMeshRight{};
    VulkanFacetMesh   RimMeshLeft{};
    VulkanFacetMesh   RimMeshRight{};

    VulkanImageTarget SceneColorImage{};
    VulkanImageTarget SceneDepthImage{};
    VulkanImageTarget HardwareDepthImage{};
    VkFramebuffer     WorldMrtFbo          = VK_NULL_HANDLE;
    VkFramebuffer     SceneColorLoadFbo    = VK_NULL_HANDLE;
    VkCommandBuffer   ActiveCmd            = VK_NULL_HANDLE;
    bool              Initialized          = false;
};

} // namespace Frontier
