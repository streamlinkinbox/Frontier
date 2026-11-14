/*============================================================================================================================================
                                                        GPUTEXTUREANDSDFBAKER.H
============================================================================================================================================*/
// 🧩 Pure-GPU Vulkan Texture & 3D Volume SDF Baking engine with dual GTX BVH4 compute and RTX hardware VK_KHR_ray_query execution

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Baking/BakingAccelerationStructure.h"
#include "Engine/GeometricRaster/Baking/StudyGeometryBuilder.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Frontier::GeometricRaster::Baking
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      SPECIFICATIONS
//------------------------------------------------------------------------------------------------------------------------

struct SurfaceBakeExecutionSpec
{
    uint32_t ResolutionPx            = 512u;
    uint32_t AoSampleCount           = 12u;
    float    FrontCageMeters         = 0.14f;
    float    RearCageMeters          = 0.14f;
    float    AoMaxRayDistanceMeters  = 0.45f;
    float    TangentSignY            = 1.0f;
    uint32_t EdgePaddingPx           = 6u;
    uint32_t ActiveTargetBitmask     = 0xFFFFFFFFu;
    bool     PreferComputeBvh4       = false;
};

struct VolumeBakeExecutionSpec
{
    uint32_t GridDim                 = 36u;
    bool     SignedInteriorDistance  = true;
    float    BoundsPaddingFraction   = 0.16f;
    uint32_t SectionAxis             = 0u;
    float    SectionDepthNormalized  = 0.50f;
    uint32_t SectionSizePx           = 512u;
    bool     PreferComputeBvh4       = false;
};

struct BakeCompletionSummary
{
    bool                    SurfaceSucceeded = false;
    bool                    VolumeSucceeded  = false;
    double                  SurfaceElapsedMs = 0.0;
    double                  VolumeElapsedMs  = 0.0;
    BakeAccelerationBackend ExecutedBackend  = BakeAccelerationBackend::GtxComputeBvh4;
    std::string             DiagnosticText   {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  GPU BAKER ENGINE
//------------------------------------------------------------------------------------------------------------------------

class GpuTextureAndSdfBaker
{
public:
    static constexpr uint32_t SurfaceTargetCount = 17u; // Channels 1..17 mapped to slots 0..16

    GpuTextureAndSdfBaker() = default;
    ~GpuTextureAndSdfBaker();

    GpuTextureAndSdfBaker(const GpuTextureAndSdfBaker&)            = delete;
    GpuTextureAndSdfBaker& operator=(const GpuTextureAndSdfBaker&) = delete;

    [[nodiscard]] bool InitializeBaker(
        VulkanInterchange*           BoundDevice,
        const std::filesystem::path& ShaderDirectory = {});

    void TeardownBaker() noexcept;

    [[nodiscard]] bool UploadStudyGeometry(const PreparedStudyPair& Study);

    [[nodiscard]] BakeCompletionSummary ExecuteSurfaceBake(const SurfaceBakeExecutionSpec& Spec);
    [[nodiscard]] BakeCompletionSummary ExecuteVolumeSdfBake(const VolumeBakeExecutionSpec& Spec);

    [[nodiscard]] bool UpdateSdfSectionPreview(
        uint32_t SectionAxis,
        float    SectionDepthNormalized,
        bool     SignedInteriorDistance,
        uint32_t SectionSizePx = 512u);

    [[nodiscard]] bool ExportSurfaceChannelPng(
        uint32_t                     ChannelSlot,
        const std::filesystem::path& OutputPngPath) const;

    [[nodiscard]] bool ExportSdfSectionPng(const std::filesystem::path& OutputPngPath) const;
    [[nodiscard]] bool ExportVolumeFvol(const std::filesystem::path& OutputFvolPath) const;

    [[nodiscard]] bool Ready() const noexcept { return Initialized; }
    [[nodiscard]] bool QueryStudyUploaded() const noexcept { return Accel.Ready(); }
    [[nodiscard]] bool QueryHardwareRayQueryAvailable() const noexcept { return Accel.QueryHardwareAccelReady(); }
    [[nodiscard]] bool QueryRtxShadersLoaded() const noexcept { return RtxProgramsLoaded; }
    [[nodiscard]] BakeAccelerationBackend QueryActiveBackend(bool PreferComputeBvh4 = false) const noexcept
    {
        if (!PreferComputeBvh4 && Accel.QueryHardwareAccelReady() && RtxProgramsLoaded)
        {
            return BakeAccelerationBackend::RtxHardwareRayQuery;
        }
        return BakeAccelerationBackend::GtxComputeBvh4;
    }

    [[nodiscard]] uint32_t QuerySurfaceResolutionPx() const noexcept { return AllocatedSurfaceResPx; }
    [[nodiscard]] uint32_t QueryVolumeGridDim() const noexcept { return AllocatedVolumeDim; }
    [[nodiscard]] const PreparedStudyPair& InspectActiveStudy() const noexcept { return ActiveStudy; }
    [[nodiscard]] const BakingAccelerationStructure& InspectAccel() const noexcept { return Accel; }

    [[nodiscard]] VkImageView AccessBakedChannelArrayView() const noexcept
    {
        if (!SurfaceTexturesReady || AllocatedSurfaceResPx == 0u)
        {
            return DefaultArrayView;
        }
        return (ActiveChannelArrayIndex == 0u) ? ChannelArrayViewA : ChannelArrayViewB;
    }
    [[nodiscard]] VkSampler AccessLinearSampler() const noexcept { return LinearClampSampler; }

    [[nodiscard]] const std::vector<uint8_t>& InspectChannelRgba8(uint32_t ChannelSlot) const noexcept
    {
        static const std::vector<uint8_t> Empty {};
        return (ChannelSlot < SurfaceTargetCount) ? CachedSurfaceChannelsRgba8[ChannelSlot] : Empty;
    }
    [[nodiscard]] const std::vector<uint8_t>& InspectSdfSectionRgba8() const noexcept { return CachedSectionRgba8; }
    [[nodiscard]] uint32_t QuerySdfSectionSizePx() const noexcept { return AllocatedSectionPx; }
    [[nodiscard]] const std::vector<float>& InspectVolumeDistances() const noexcept { return CachedVolumeDistances; }

private:
    struct ArrayImageBundle
    {
        VkImage        ImageHandle  = VK_NULL_HANDLE;
        VkDeviceMemory MemoryHandle = VK_NULL_HANDLE;
        VkImageView    ViewHandle   = VK_NULL_HANDLE;
        uint32_t       WidthPx      = 0u;
        uint32_t       HeightPx     = 0u;
        uint32_t       LayerCount   = 0u;
    };

    [[nodiscard]] bool CreateShaderPrograms(const std::filesystem::path& ShaderDir);
    void               DestroyShaderPrograms() noexcept;
    [[nodiscard]] bool EnsureSurfaceResources(uint32_t ResolutionPx);
    void               ReleaseSurfaceResources() noexcept;
    [[nodiscard]] bool EnsureVolumeResources(uint32_t GridDim, uint32_t SectionSizePx);
    void               ReleaseVolumeResources() noexcept;
    [[nodiscard]] bool AllocateArrayImage(uint32_t ResPx, uint32_t Layers, ArrayImageBundle& OutBundle) const;
    void               ReleaseArrayImage(ArrayImageBundle& Bundle) const noexcept;
    void               TransitionImageLayout(
        VkCommandBuffer      Cmd,
        VkImage              ImageHandle,
        VkImageLayout        OldLayout,
        VkImageLayout        NewLayout,
        uint32_t             LayerCount,
        VkPipelineStageFlags SrcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VkPipelineStageFlags DstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT) const noexcept;

    VulkanInterchange*          DeviceOwner {};
    PreparedStudyPair           ActiveStudy {};
    BakingAccelerationStructure Accel       {};
    bool                        Initialized = false;
    bool                        RtxProgramsLoaded = false;

    // Vulkan synchronization & descriptor allocation
    VkDescriptorPool            DescriptorRegistry      = VK_NULL_HANDLE;
    VkSampler                   LinearClampSampler      = VK_NULL_HANDLE;

    // Graphics program: Low-Mesh UV G-buffer rasterizer
    VkRenderPass                UvGbufferRenderSequence = VK_NULL_HANDLE;
    VkFramebuffer               UvGbufferTarget         = VK_NULL_HANDLE;
    VkDescriptorSetLayout       UvRasterSetLayout       = VK_NULL_HANDLE;
    VkPipelineLayout            UvRasterProgramLayout   = VK_NULL_HANDLE;
    VkPipeline                  UvRasterProgram         = VK_NULL_HANDLE;
    VkDescriptorSet             UvRasterDescriptorSet   = VK_NULL_HANDLE;

    // Compute program: High-to-Low surface transfer + AO/Bent/Thickness (GTX & RTX variants)
    VkDescriptorSetLayout       SurfaceTransferGtxSetLayout     = VK_NULL_HANDLE;
    VkPipelineLayout            SurfaceTransferGtxProgramLayout = VK_NULL_HANDLE;
    VkPipeline                  SurfaceTransferGtxProgram       = VK_NULL_HANDLE;
    VkDescriptorSet             SurfaceTransferGtxDescriptorSet = VK_NULL_HANDLE;

    VkDescriptorSetLayout       SurfaceTransferRtxSetLayout     = VK_NULL_HANDLE;
    VkPipelineLayout            SurfaceTransferRtxProgramLayout = VK_NULL_HANDLE;
    VkPipeline                  SurfaceTransferRtxProgram       = VK_NULL_HANDLE;
    VkDescriptorSet             SurfaceTransferRtxDescriptorSet = VK_NULL_HANDLE;

    // Compute program: Unquantized channel resolve (all 17 channels)
    VkDescriptorSetLayout       ChannelResolveSetLayout       = VK_NULL_HANDLE;
    VkPipelineLayout            ChannelResolveProgramLayout   = VK_NULL_HANDLE;
    VkPipeline                  ChannelResolveProgram         = VK_NULL_HANDLE;
    VkDescriptorSet             ChannelResolveDescriptorSet   = VK_NULL_HANDLE;

    // Compute program: Ping-pong UV gutter dilation
    VkDescriptorSetLayout       DilationSetLayout             = VK_NULL_HANDLE;
    VkPipelineLayout            DilationProgramLayout         = VK_NULL_HANDLE;
    VkPipeline                  DilationProgram               = VK_NULL_HANDLE;
    VkDescriptorSet             DilationSetAtoB               = VK_NULL_HANDLE;
    VkDescriptorSet             DilationSetBtoA               = VK_NULL_HANDLE;

    // Compute program: 3D Volume SDF (GTX & RTX variants) + 2D Section extractor
    VkDescriptorSetLayout       VolumeSdfGtxSetLayout         = VK_NULL_HANDLE;
    VkPipelineLayout            VolumeSdfGtxProgramLayout     = VK_NULL_HANDLE;
    VkPipeline                  VolumeSdfGtxProgram           = VK_NULL_HANDLE;
    VkDescriptorSet             VolumeSdfGtxDescriptorSet     = VK_NULL_HANDLE;

    VkDescriptorSetLayout       VolumeSdfRtxSetLayout         = VK_NULL_HANDLE;
    VkPipelineLayout            VolumeSdfRtxProgramLayout     = VK_NULL_HANDLE;
    VkPipeline                  VolumeSdfRtxProgram           = VK_NULL_HANDLE;
    VkDescriptorSet             VolumeSdfRtxDescriptorSet     = VK_NULL_HANDLE;

    VkDescriptorSetLayout       SectionExtractSetLayout       = VK_NULL_HANDLE;
    VkPipelineLayout            SectionExtractProgramLayout   = VK_NULL_HANDLE;
    VkPipeline                  SectionExtractProgram         = VK_NULL_HANDLE;
    VkDescriptorSet             SectionExtractDescriptorSet   = VK_NULL_HANDLE;

    // Surface uniform & G-buffer attachments
    DeviceBufferResource        SurfaceUniformBuffer    {};
    DeviceImageResource         LowPosMaskImage         {};
    DeviceImageResource         LowNormPartImage        {};
    DeviceImageResource         LowTangIslandImage      {};
    DeviceImageResource         LowBitanWireImage       {};
    DeviceImageResource         HighPosHeightImage      {};
    DeviceImageResource         HighNormPartImage       {};
    DeviceImageResource         OcclusionStatsImage     {};
    DeviceImageResource         BentNormalWorldImage    {};
    ArrayImageBundle            ChannelArrayA           {};
    ArrayImageBundle            ChannelArrayB           {};
    ArrayImageBundle            DefaultArrayBundle      {};
    VkImageView                 DefaultArrayView        = VK_NULL_HANDLE;
    VkImageView                 ChannelArrayViewA       = VK_NULL_HANDLE;
    VkImageView                 ChannelArrayViewB       = VK_NULL_HANDLE;
    DeviceBufferResource        ChannelReadbackBuffer   {};

    // Volume SDF buffers & 2D section output
    DeviceBufferResource        VolumeUniformBuffer     {};
    DeviceBufferResource        VolumeDistanceBuffer    {};
    DeviceImageResource         VolumeTexture3D         {};
    DeviceImageResource         SectionRgba8Image       {};
    DeviceBufferResource        SectionReadbackBuffer   {};

    uint32_t                    AllocatedSurfaceResPx   = 0u;
    uint32_t                    ActiveChannelArrayIndex = 0u;
    bool                        SurfaceTexturesReady    = false;
    uint32_t                    AllocatedVolumeDim      = 0u;
    uint32_t                    AllocatedSectionPx      = 0u;
    GpuVolumeBakeConfig         LastVolumeConfig        {};

    std::array<std::vector<uint8_t>, SurfaceTargetCount> CachedSurfaceChannelsRgba8 {};
    std::vector<uint8_t>                                 CachedSectionRgba8         {};
    std::vector<float>                                   CachedVolumeDistances      {};
};

} // namespace Frontier::GeometricRaster::Baking
