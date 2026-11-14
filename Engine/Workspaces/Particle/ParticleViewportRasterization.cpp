/*============================================================================================================================================
                                                 PARTICLEVIEWPORTRASTERIZATION.CPP
============================================================================================================================================*/
// 🧩 Vulkan 1.3 GPU .slang particle viewport rasterizer (studio backdrop, 3D floor grid, strands/ribbons & billboards from live GPU SSBOs)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particle/ParticleViewportRasterization.h"

#if defined(FRONTIER_WITH_VULKAN) && (FRONTIER_WITH_VULKAN != 0)
    #include <volk.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>

namespace Frontier::Workspaces
{

#if !defined(FRONTIER_WITH_VULKAN) || (FRONTIER_WITH_VULKAN == 0)

struct ParticleViewportRasterization::RasterImplementation
{
};

ParticleViewportRasterization::ParticleViewportRasterization() noexcept  = default;
ParticleViewportRasterization::~ParticleViewportRasterization() noexcept = default;

bool ParticleViewportRasterization::Initialize(
    SimulationSpace::Particles::ParticleVulkanExecutor&,
    const std::filesystem::path&,
    std::string& OutDiagnostic) noexcept
{
    OutDiagnostic = "Vulkan graphics rasterization disabled in this headless build profile.";
    return false;
}

void                     ParticleViewportRasterization::Finalize() noexcept {}
bool                     ParticleViewportRasterization::QueryReady() const noexcept { return false; }
std::span<const uint8_t> ParticleViewportRasterization::QueryColorRgbaPixels() const noexcept { return {}; }
uint32_t                 ParticleViewportRasterization::QueryWidthPx() const noexcept { return 0u; }
uint32_t                 ParticleViewportRasterization::QueryHeightPx() const noexcept { return 0u; }

bool ParticleViewportRasterization::RenderOffscreen(const ParticleViewportRenderRequest&, std::string& OutDiagnostic) noexcept
{
    OutDiagnostic = "Vulkan graphics rasterization disabled in this headless build profile.";
    return false;
}

#else // FRONTIER_WITH_VULKAN

namespace
{

// Byte-exact mirror of GpuParticleViewportUniforms (128 B = 8x float4)
struct alignas(16) ViewportGpuUniformBlock
{
    std::array<float, 16u>   ViewProjection  {}; // Row-major Reverse-Z +Z Up World -> Clip
    std::array<float, 4u>    CameraPosTime   {}; // xyz = CameraWorld [m], w = TimeSeconds [s]
    std::array<float, 4u>    CameraRightScale{}; // xyz = CameraRight, w = SizeScale
    std::array<float, 4u>    CameraUpBoost   {}; // xyz = CameraUp, w = EmissiveBoost
    std::array<uint32_t, 4u> ViewportControl {}; // ActiveCount, Silhouette, BlendMode, RibbonWidthBits
};

static_assert(sizeof(ViewportGpuUniformBlock) == 128u, "ViewportGpuUniformBlock must remain exactly 128 bytes.");

[[nodiscard]] bool ReadSpirvWords(const std::filesystem::path& SourcePath, std::vector<uint32_t>& OutWords)
{
    std::ifstream Reader(SourcePath, std::ios::binary | std::ios::ate);
    if (!Reader.is_open())
    {
        return false;
    }
    const std::streamsize ByteSize = Reader.tellg();
    if (ByteSize <= 0 || (ByteSize % 4) != 0)
    {
        return false;
    }
    Reader.seekg(0, std::ios::beg);
    OutWords.resize(static_cast<std::size_t>(ByteSize / 4));
    Reader.read(reinterpret_cast<char*>(OutWords.data()), ByteSize);
    return Reader.good();
}

void AssembleReverseZCameraUniforms(
    const ParticleCameraRig&             Cam,
    float                                Aspect,
    const ParticleViewportRenderRequest& Req,
    ViewportGpuUniformBlock&             OutUbo) noexcept
{
    const float CosP = std::cos(Cam.PitchRadians);
    const float SinP = std::sin(Cam.PitchRadians);
    const float CosY = std::cos(Cam.YawRadians);
    const float SinY = std::sin(Cam.YawRadians);
    const float Dist = std::max(0.5f, Cam.DistanceMeters);

    const float CamX = Cam.TargetMeters.X + Dist * CosP * CosY;
    const float CamY = Cam.TargetMeters.Y + Dist * CosP * SinY;
    const float CamZ = Cam.TargetMeters.Z + Dist * SinP;

    float FwdX = Cam.TargetMeters.X - CamX;
    float FwdY = Cam.TargetMeters.Y - CamY;
    float FwdZ = Cam.TargetMeters.Z - CamZ;
    const float FwdLen = std::sqrt(FwdX * FwdX + FwdY * FwdY + FwdZ * FwdZ);
    if (FwdLen > 1.0e-6f)
    {
        FwdX /= FwdLen;
        FwdY /= FwdLen;
        FwdZ /= FwdLen;
    }
    else
    {
        FwdX = 1.0f;
        FwdY = 0.0f;
        FwdZ = 0.0f;
    }

    // Right = normalize(cross(Forward, +Z))
    float RightX = FwdY;
    float RightY = -FwdX;
    float RightZ = 0.0f;
    const float RightLen = std::sqrt(RightX * RightX + RightY * RightY);
    if (RightLen > 1.0e-5f)
    {
        RightX /= RightLen;
        RightY /= RightLen;
    }
    else
    {
        RightX = 0.0f;
        RightY = -1.0f;
        RightZ = 0.0f;
    }

    // CamUp = cross(Right, Forward)
    float UpX = RightY * FwdZ - RightZ * FwdY;
    float UpY = RightZ * FwdX - RightX * FwdZ;
    float UpZ = RightX * FwdY - RightY * FwdX;
    const float UpLen = std::sqrt(UpX * UpX + UpY * UpY + UpZ * UpZ);
    if (UpLen > 1.0e-6f)
    {
        UpX /= UpLen;
        UpY /= UpLen;
        UpZ /= UpLen;
    }

    const float SafeAspect = std::max(0.1f, Aspect);
    const float HalfFov    = std::clamp(Cam.VerticalFovRadians, 0.25f, 1.95f) * 0.5f;
    const float TanHalf    = std::tan(HalfFov);
    const float Sx         = 1.0f / (SafeAspect * TanHalf);
    const float Sy         = 1.0f / TanHalf;
    const float NearZ      = 0.1f;

    const float DotCamRight = CamX * RightX + CamY * RightY + CamZ * RightZ;
    const float DotCamUp    = CamX * UpX + CamY * UpY + CamZ * UpZ;
    const float DotCamFwd   = CamX * FwdX + CamY * FwdY + CamZ * FwdZ;

    auto& M = OutUbo.ViewProjection;
    // Row 0: ClipX
    M[0]  = Sx * RightX;
    M[1]  = Sx * RightY;
    M[2]  = Sx * RightZ;
    M[3]  = -Sx * DotCamRight;
    // Row 1: ClipY (Vulkan NDC -Y points UP)
    M[4]  = -Sy * UpX;
    M[5]  = -Sy * UpY;
    M[6]  = -Sy * UpZ;
    M[7]  = Sy * DotCamUp;
    // Row 2: ClipZ (Reverse-Z infinite far: NearZ / Vz -> [1..0])
    M[8]  = 0.0f;
    M[9]  = 0.0f;
    M[10] = 0.0f;
    M[11] = NearZ;
    // Row 3: ClipW = Vz
    M[12] = FwdX;
    M[13] = FwdY;
    M[14] = FwdZ;
    M[15] = -DotCamFwd;

    OutUbo.CameraPosTime    = { CamX, CamY, CamZ, Req.TimeSeconds };
    OutUbo.CameraRightScale = { RightX, RightY, RightZ, std::max(0.1f, Req.SizeScale) };
    OutUbo.CameraUpBoost    = { UpX, UpY, UpZ, std::max(0.25f, Req.EmissiveBoost) };

    uint32_t RibbonWidthBits = 0u;
    std::memcpy(&RibbonWidthBits, &Req.RibbonWidthMeters, sizeof(uint32_t));
    OutUbo.ViewportControl = {
        Req.ActiveCount,
        static_cast<uint32_t>(Req.Silhouette),
        static_cast<uint32_t>(Req.BlendMode),
        RibbonWidthBits,
    };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              VULKAN RASTER IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

struct ParticleViewportRasterization::RasterImplementation
{
    SimulationSpace::Particles::ParticleVulkanExecutor* Executor = nullptr;
    VkPhysicalDevice PhysicalDevice    = VK_NULL_HANDLE;
    VkDevice         Device            = VK_NULL_HANDLE;
    VkQueue          GraphicsQueue     = VK_NULL_HANDLE;
    uint32_t         QueueFamily       = 0u;

    VkRenderPass     TargetRenderStep  = VK_NULL_HANDLE;
    VkDescriptorPool DescriptorBank    = VK_NULL_HANDLE;
    VkCommandPool    CommandBank       = VK_NULL_HANDLE;
    VkCommandBuffer  CommandRecorder   = VK_NULL_HANDLE;
    VkFence          CompletionSignal  = VK_NULL_HANDLE;

    VkDescriptorSetLayout GridDescLayout       = VK_NULL_HANDLE;
    VkPipelineLayout      GridLayout           = VK_NULL_HANDLE;
    VkPipeline            BackdropExecutable   = VK_NULL_HANDLE;
    VkPipeline            FloorGridExecutable  = VK_NULL_HANDLE;
    VkDescriptorSet       GridDescriptorSlot   = VK_NULL_HANDLE;

    VkDescriptorSetLayout ParticleDescLayout       = VK_NULL_HANDLE;
    VkPipelineLayout      ParticleLayout           = VK_NULL_HANDLE;
    VkPipeline            BillboardAlphaExec       = VK_NULL_HANDLE;
    VkPipeline            BillboardAdditiveExec    = VK_NULL_HANDLE;
    VkPipeline            RibbonAlphaExec          = VK_NULL_HANDLE;
    VkDescriptorSet       BillboardDescriptorSlot  = VK_NULL_HANDLE;
    VkDescriptorSet       RibbonDescriptorSlot     = VK_NULL_HANDLE;

    VkBuffer         UniformBuffer    = VK_NULL_HANDLE;
    VkDeviceMemory   UniformMemory    = VK_NULL_HANDLE;
    void*            UniformMapped    = nullptr;

    VkImage          ColorImage       = VK_NULL_HANDLE;
    VkDeviceMemory   ColorMemory      = VK_NULL_HANDLE;
    VkImageView      ColorView        = VK_NULL_HANDLE;

    VkImage          DepthImage       = VK_NULL_HANDLE;
    VkDeviceMemory   DepthMemory      = VK_NULL_HANDLE;
    VkImageView      DepthView        = VK_NULL_HANDLE;

    VkFramebuffer    TargetBuffer     = VK_NULL_HANDLE;

    VkBuffer         ReadbackBuffer   = VK_NULL_HANDLE;
    VkDeviceMemory   ReadbackMemory   = VK_NULL_HANDLE;
    void*            ReadbackMapped   = nullptr;
    VkDeviceSize     ReadbackCapacity = 0u;

    uint32_t             TargetWidthPx  = 0u;
    uint32_t             TargetHeightPx = 0u;
    std::vector<uint8_t> CpuRgbaPixels  {};
    bool                 Ready          = false;

    [[nodiscard]] uint32_t FindMemoryType(uint32_t TypeMask, VkMemoryPropertyFlags RequiredFlags) const noexcept
    {
        VkPhysicalDeviceMemoryProperties Props{};
        vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &Props);
        for (uint32_t Idx = 0u; Idx < Props.memoryTypeCount; ++Idx)
        {
            if ((TypeMask & (1u << Idx)) != 0u &&
                (Props.memoryTypes[Idx].propertyFlags & RequiredFlags) == RequiredFlags)
            {
                return Idx;
            }
        }
        return UINT32_MAX;
    }

    bool CreateHostBuffer(
        VkDeviceSize       ByteSize,
        VkBufferUsageFlags UsageBits,
        VkBuffer&          OutBuf,
        VkDeviceMemory&    OutMem,
        void*&             OutMapped) noexcept
    {
        VkBufferCreateInfo BufInfo{};
        BufInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        BufInfo.size        = ByteSize;
        BufInfo.usage       = UsageBits;
        BufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(Device, &BufInfo, nullptr, &OutBuf) != VK_SUCCESS)
        {
            return false;
        }

        VkMemoryRequirements Req{};
        vkGetBufferMemoryRequirements(Device, OutBuf, &Req);
        const uint32_t MemIdx = FindMemoryType(
            Req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (MemIdx == UINT32_MAX)
        {
            return false;
        }

        VkMemoryAllocateInfo Alloc{};
        Alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        Alloc.allocationSize  = Req.size;
        Alloc.memoryTypeIndex = MemIdx;
        if (vkAllocateMemory(Device, &Alloc, nullptr, &OutMem) != VK_SUCCESS)
        {
            return false;
        }
        if (vkBindBufferMemory(Device, OutBuf, OutMem, 0u) != VK_SUCCESS)
        {
            return false;
        }
        return vkMapMemory(Device, OutMem, 0u, ByteSize, 0u, &OutMapped) == VK_SUCCESS;
    }

    bool CreateGraphicsExecutable(
        const std::filesystem::path& VertSpvPath,
        const char*                  VertEntry,
        const std::filesystem::path& FragSpvPath,
        const char*                  FragEntry,
        VkPipelineLayout             Layout,
        bool                         DepthTest,
        bool                         DepthWrite,
        bool                         EnableBlend,
        bool                         AdditiveBlend,
        VkPipeline&                  OutExecutable,
        std::string&                 OutDiagnostic) noexcept
    {
        std::vector<uint32_t> VertWords;
        std::vector<uint32_t> FragWords;
        if (!ReadSpirvWords(VertSpvPath, VertWords) || !ReadSpirvWords(FragSpvPath, FragWords))
        {
            OutDiagnostic = "Missing particle graphics SPIR-V binary: " + VertSpvPath.string();
            return false;
        }

        VkShaderModule VertMod = VK_NULL_HANDLE;
        VkShaderModule FragMod = VK_NULL_HANDLE;
        VkShaderModuleCreateInfo VmInfo{};
        VmInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        VmInfo.codeSize = VertWords.size() * sizeof(uint32_t);
        VmInfo.pCode    = VertWords.data();
        VkShaderModuleCreateInfo FmInfo{};
        FmInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        FmInfo.codeSize = FragWords.size() * sizeof(uint32_t);
        FmInfo.pCode    = FragWords.data();

        if (vkCreateShaderModule(Device, &VmInfo, nullptr, &VertMod) != VK_SUCCESS ||
            vkCreateShaderModule(Device, &FmInfo, nullptr, &FragMod) != VK_SUCCESS)
        {
            if (VertMod != VK_NULL_HANDLE) { vkDestroyShaderModule(Device, VertMod, nullptr); }
            if (FragMod != VK_NULL_HANDLE) { vkDestroyShaderModule(Device, FragMod, nullptr); }
            OutDiagnostic = "Failed to construct graphics SPIR-V modules.";
            return false;
        }

        std::array<VkPipelineShaderStageCreateInfo, 2u> ShaderSteps{};
        ShaderSteps[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ShaderSteps[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        ShaderSteps[0].module = VertMod;
        ShaderSteps[0].pName  = VertEntry;
        ShaderSteps[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ShaderSteps[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        ShaderSteps[1].module = FragMod;
        ShaderSteps[1].pName  = FragEntry;

        VkPipelineVertexInputStateCreateInfo VertInput{};
        VertInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo InputShape{};
        InputShape.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        InputShape.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo ViewportConfig{};
        ViewportConfig.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        ViewportConfig.viewportCount = 1u;
        ViewportConfig.scissorCount  = 1u;

        VkPipelineRasterizationStateCreateInfo Raster{};
        Raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        Raster.polygonMode = VK_POLYGON_MODE_FILL;
        Raster.cullMode    = VK_CULL_MODE_NONE;
        Raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        Raster.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo MultiSample{};
        MultiSample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        MultiSample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo DepthCfg{};
        DepthCfg.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        DepthCfg.depthTestEnable  = DepthTest ? VK_TRUE : VK_FALSE;
        DepthCfg.depthWriteEnable = DepthWrite ? VK_TRUE : VK_FALSE;
        DepthCfg.depthCompareOp   = VK_COMPARE_OP_GREATER_OR_EQUAL; // Reverse-Z

        VkPipelineColorBlendAttachmentState BlendAttach{};
        BlendAttach.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        BlendAttach.blendEnable         = EnableBlend ? VK_TRUE : VK_FALSE;
        BlendAttach.srcColorBlendFactor = AdditiveBlend ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_SRC_ALPHA;
        BlendAttach.dstColorBlendFactor = AdditiveBlend ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        BlendAttach.colorBlendOp        = VK_BLEND_OP_ADD;
        BlendAttach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        BlendAttach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        BlendAttach.alphaBlendOp        = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo BlendConfig{};
        BlendConfig.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        BlendConfig.attachmentCount = 1u;
        BlendConfig.pAttachments    = &BlendAttach;

        const std::array<VkDynamicState, 2u> DynStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo DynCfg{};
        DynCfg.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        DynCfg.dynamicStateCount = static_cast<uint32_t>(DynStates.size());
        DynCfg.pDynamicStates    = DynStates.data();

        VkGraphicsPipelineCreateInfo PipeInfo{};
        PipeInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        PipeInfo.stageCount          = static_cast<uint32_t>(ShaderSteps.size());
        PipeInfo.pStages             = ShaderSteps.data();
        PipeInfo.pVertexInputState   = &VertInput;
        PipeInfo.pInputAssemblyState = &InputShape;
        PipeInfo.pViewportState      = &ViewportConfig;
        PipeInfo.pRasterizationState = &Raster;
        PipeInfo.pMultisampleState   = &MultiSample;
        PipeInfo.pDepthStencilState  = &DepthCfg;
        PipeInfo.pColorBlendState    = &BlendConfig;
        PipeInfo.pDynamicState       = &DynCfg;
        PipeInfo.layout              = Layout;
        PipeInfo.renderPass          = TargetRenderStep;
        PipeInfo.subpass             = 0u;

        const VkResult Res = vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1u, &PipeInfo, nullptr, &OutExecutable);
        vkDestroyShaderModule(Device, VertMod, nullptr);
        vkDestroyShaderModule(Device, FragMod, nullptr);
        if (Res != VK_SUCCESS)
        {
            OutDiagnostic = "vkCreateGraphicsPipelines failed for particle rasterization.";
            return false;
        }
        return true;
    }

    void DestroyOffscreenTargets() noexcept
    {
        if (Device == VK_NULL_HANDLE)
        {
            return;
        }
        vkDeviceWaitIdle(Device);
        if (TargetBuffer != VK_NULL_HANDLE) { vkDestroyFramebuffer(Device, TargetBuffer, nullptr); TargetBuffer = VK_NULL_HANDLE; }
        if (ColorView != VK_NULL_HANDLE)    { vkDestroyImageView(Device, ColorView, nullptr); ColorView = VK_NULL_HANDLE; }
        if (ColorImage != VK_NULL_HANDLE)   { vkDestroyImage(Device, ColorImage, nullptr); ColorImage = VK_NULL_HANDLE; }
        if (ColorMemory != VK_NULL_HANDLE)  { vkFreeMemory(Device, ColorMemory, nullptr); ColorMemory = VK_NULL_HANDLE; }
        if (DepthView != VK_NULL_HANDLE)    { vkDestroyImageView(Device, DepthView, nullptr); DepthView = VK_NULL_HANDLE; }
        if (DepthImage != VK_NULL_HANDLE)   { vkDestroyImage(Device, DepthImage, nullptr); DepthImage = VK_NULL_HANDLE; }
        if (DepthMemory != VK_NULL_HANDLE)  { vkFreeMemory(Device, DepthMemory, nullptr); DepthMemory = VK_NULL_HANDLE; }
        if (ReadbackMapped != nullptr && ReadbackMemory != VK_NULL_HANDLE)
        {
            vkUnmapMemory(Device, ReadbackMemory);
            ReadbackMapped = nullptr;
        }
        if (ReadbackBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(Device, ReadbackBuffer, nullptr); ReadbackBuffer = VK_NULL_HANDLE; }
        if (ReadbackMemory != VK_NULL_HANDLE) { vkFreeMemory(Device, ReadbackMemory, nullptr); ReadbackMemory = VK_NULL_HANDLE; }
        ReadbackCapacity = 0u;
        TargetWidthPx    = 0u;
        TargetHeightPx   = 0u;
    }

    bool EnsureOffscreenTargets(uint32_t WidthPx, uint32_t HeightPx) noexcept
    {
        const uint32_t SafeW = std::max(64u, WidthPx);
        const uint32_t SafeH = std::max(64u, HeightPx);
        if (TargetBuffer != VK_NULL_HANDLE && TargetWidthPx == SafeW && TargetHeightPx == SafeH)
        {
            return true;
        }
        DestroyOffscreenTargets();

        auto CreateAttachment = [&](VkFormat Fmt, VkImageUsageFlags Usage, VkImageAspectFlags Aspect,
                                    VkImage& Img, VkDeviceMemory& Mem, VkImageView& View) -> bool
        {
            VkImageCreateInfo ImgInfo{};
            ImgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            ImgInfo.imageType     = VK_IMAGE_TYPE_2D;
            ImgInfo.format        = Fmt;
            ImgInfo.extent        = { SafeW, SafeH, 1u };
            ImgInfo.mipLevels     = 1u;
            ImgInfo.arrayLayers   = 1u;
            ImgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
            ImgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
            ImgInfo.usage         = Usage;
            ImgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (vkCreateImage(Device, &ImgInfo, nullptr, &Img) != VK_SUCCESS)
            {
                return false;
            }
            VkMemoryRequirements Req{};
            vkGetImageMemoryRequirements(Device, Img, &Req);
            uint32_t MemIdx = FindMemoryType(Req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (MemIdx == UINT32_MAX)
            {
                MemIdx = FindMemoryType(Req.memoryTypeBits, 0u);
            }
            if (MemIdx == UINT32_MAX)
            {
                return false;
            }
            VkMemoryAllocateInfo Alloc{};
            Alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            Alloc.allocationSize  = Req.size;
            Alloc.memoryTypeIndex = MemIdx;
            if (vkAllocateMemory(Device, &Alloc, nullptr, &Mem) != VK_SUCCESS ||
                vkBindImageMemory(Device, Img, Mem, 0u) != VK_SUCCESS)
            {
                return false;
            }
            VkImageViewCreateInfo ViewInfo{};
            ViewInfo.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            ViewInfo.image                       = Img;
            ViewInfo.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
            ViewInfo.format                      = Fmt;
            ViewInfo.subresourceRange.aspectMask = Aspect;
            ViewInfo.subresourceRange.levelCount = 1u;
            ViewInfo.subresourceRange.layerCount = 1u;
            return vkCreateImageView(Device, &ViewInfo, nullptr, &View) == VK_SUCCESS;
        };

        if (!CreateAttachment(
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                ColorImage,
                ColorMemory,
                ColorView) ||
            !CreateAttachment(
                VK_FORMAT_D32_SFLOAT,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT,
                DepthImage,
                DepthMemory,
                DepthView))
        {
            DestroyOffscreenTargets();
            return false;
        }

        const std::array<VkImageView, 2u> Views = { ColorView, DepthView };
        VkFramebufferCreateInfo FbInfo{};
        FbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        FbInfo.renderPass      = TargetRenderStep;
        FbInfo.attachmentCount = static_cast<uint32_t>(Views.size());
        FbInfo.pAttachments    = Views.data();
        FbInfo.width           = SafeW;
        FbInfo.height          = SafeH;
        FbInfo.layers          = 1u;
        if (vkCreateFramebuffer(Device, &FbInfo, nullptr, &TargetBuffer) != VK_SUCCESS)
        {
            DestroyOffscreenTargets();
            return false;
        }

        const VkDeviceSize ReadBytes = static_cast<VkDeviceSize>(SafeW) * SafeH * 4u;
        if (!CreateHostBuffer(ReadBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, ReadbackBuffer, ReadbackMemory, ReadbackMapped))
        {
            DestroyOffscreenTargets();
            return false;
        }
        ReadbackCapacity = ReadBytes;
        TargetWidthPx    = SafeW;
        TargetHeightPx   = SafeH;
        CpuRgbaPixels.assign(static_cast<std::size_t>(ReadBytes), 0u);
        return true;
    }

    void UpdateDescriptorSlots() noexcept
    {
        // Slot 0: Grid/Backdrop UBO only
        VkDescriptorBufferInfo UboInfo{ UniformBuffer, 0u, sizeof(ViewportGpuUniformBlock) };
        VkWriteDescriptorSet GridWrite{};
        GridWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        GridWrite.dstSet          = GridDescriptorSlot;
        GridWrite.dstBinding      = 0u;
        GridWrite.descriptorCount = 1u;
        GridWrite.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        GridWrite.pBufferInfo     = &UboInfo;
        vkUpdateDescriptorSets(Device, 1u, &GridWrite, 0u, nullptr);

        // Slot 1: Billboard (UBO + Kinematics + Lifespan + Appearance directly from ParticleVulkanExecutor!)
        VkDescriptorBufferInfo KinInfo{ static_cast<VkBuffer>(Executor->QueryKinematicsBufferHandle()), 0u, Executor->QueryKinematicsByteSize() };
        VkDescriptorBufferInfo LifeInfo{ static_cast<VkBuffer>(Executor->QueryLifespanBufferHandle()), 0u, Executor->QueryLifespanByteSize() };
        VkDescriptorBufferInfo AppInfo{ static_cast<VkBuffer>(Executor->QueryAppearanceBufferHandle()), 0u, Executor->QueryAppearanceByteSize() };
        VkDescriptorBufferInfo LinkInfo{ static_cast<VkBuffer>(Executor->QueryStrandLinkBufferHandle()), 0u, Executor->QueryStrandLinkByteSize() };

        std::array<VkWriteDescriptorSet, 4u> BillWrites{};
        const std::array<std::pair<VkDescriptorType, const VkDescriptorBufferInfo*>, 4u> BillBinds = {{
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &UboInfo },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &KinInfo },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &LifeInfo },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &AppInfo },
        }};
        for (uint32_t Idx = 0u; Idx < 4u; ++Idx)
        {
            BillWrites[Idx].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            BillWrites[Idx].dstSet          = BillboardDescriptorSlot;
            BillWrites[Idx].dstBinding      = Idx;
            BillWrites[Idx].descriptorCount = 1u;
            BillWrites[Idx].descriptorType  = BillBinds[Idx].first;
            BillWrites[Idx].pBufferInfo     = BillBinds[Idx].second;
        }
        vkUpdateDescriptorSets(Device, 4u, BillWrites.data(), 0u, nullptr);

        // Slot 2: Ribbon (UBO + Kinematics + Appearance + StrandLink directly from ParticleVulkanExecutor!)
        std::array<VkWriteDescriptorSet, 4u> RibWrites{};
        const std::array<std::pair<VkDescriptorType, const VkDescriptorBufferInfo*>, 4u> RibBinds = {{
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &UboInfo },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &KinInfo },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &AppInfo },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &LinkInfo },
        }};
        for (uint32_t Idx = 0u; Idx < 4u; ++Idx)
        {
            RibWrites[Idx].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            RibWrites[Idx].dstSet          = RibbonDescriptorSlot;
            RibWrites[Idx].dstBinding      = Idx;
            RibWrites[Idx].descriptorCount = 1u;
            RibWrites[Idx].descriptorType  = RibBinds[Idx].first;
            RibWrites[Idx].pBufferInfo     = RibBinds[Idx].second;
        }
        vkUpdateDescriptorSets(Device, 4u, RibWrites.data(), 0u, nullptr);
    }

    void Cleanup() noexcept
    {
        DestroyOffscreenTargets();
        if (Device != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(Device);
            if (UniformMapped != nullptr && UniformMemory != VK_NULL_HANDLE)
            {
                vkUnmapMemory(Device, UniformMemory);
                UniformMapped = nullptr;
            }
            if (UniformBuffer != VK_NULL_HANDLE)          { vkDestroyBuffer(Device, UniformBuffer, nullptr); }
            if (UniformMemory != VK_NULL_HANDLE)          { vkFreeMemory(Device, UniformMemory, nullptr); }
            if (BackdropExecutable != VK_NULL_HANDLE)     { vkDestroyPipeline(Device, BackdropExecutable, nullptr); }
            if (FloorGridExecutable != VK_NULL_HANDLE)    { vkDestroyPipeline(Device, FloorGridExecutable, nullptr); }
            if (BillboardAlphaExec != VK_NULL_HANDLE)     { vkDestroyPipeline(Device, BillboardAlphaExec, nullptr); }
            if (BillboardAdditiveExec != VK_NULL_HANDLE)  { vkDestroyPipeline(Device, BillboardAdditiveExec, nullptr); }
            if (RibbonAlphaExec != VK_NULL_HANDLE)        { vkDestroyPipeline(Device, RibbonAlphaExec, nullptr); }
            if (GridLayout != VK_NULL_HANDLE)             { vkDestroyPipelineLayout(Device, GridLayout, nullptr); }
            if (GridDescLayout != VK_NULL_HANDLE)         { vkDestroyDescriptorSetLayout(Device, GridDescLayout, nullptr); }
            if (ParticleLayout != VK_NULL_HANDLE)         { vkDestroyPipelineLayout(Device, ParticleLayout, nullptr); }
            if (ParticleDescLayout != VK_NULL_HANDLE)     { vkDestroyDescriptorSetLayout(Device, ParticleDescLayout, nullptr); }
            if (CompletionSignal != VK_NULL_HANDLE)       { vkDestroyFence(Device, CompletionSignal, nullptr); }
            if (CommandBank != VK_NULL_HANDLE)            { vkDestroyCommandPool(Device, CommandBank, nullptr); }
            if (DescriptorBank != VK_NULL_HANDLE)         { vkDestroyDescriptorPool(Device, DescriptorBank, nullptr); }
            if (TargetRenderStep != VK_NULL_HANDLE)       { vkDestroyRenderPass(Device, TargetRenderStep, nullptr); }
        }
        Ready = false;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 PUBLIC LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

ParticleViewportRasterization::ParticleViewportRasterization() noexcept = default;

ParticleViewportRasterization::~ParticleViewportRasterization() noexcept
{
    Finalize();
}

bool ParticleViewportRasterization::Initialize(
    SimulationSpace::Particles::ParticleVulkanExecutor& ComputeExecutor,
    const std::filesystem::path&                        ShaderBinaryDir,
    std::string&                                        OutDiagnostic) noexcept
{
    Finalize();
    if (!ComputeExecutor.QueryDeviceReady())
    {
        OutDiagnostic = "ParticleVulkanExecutor must be initialized before ParticleViewportRasterization.";
        return false;
    }

    Handle                 = std::make_unique<RasterImplementation>();
    Handle->Executor       = &ComputeExecutor;
    Handle->PhysicalDevice = static_cast<VkPhysicalDevice>(ComputeExecutor.QueryNativePhysicalDevice());
    Handle->Device         = static_cast<VkDevice>(ComputeExecutor.QueryNativeDevice());
    Handle->GraphicsQueue  = static_cast<VkQueue>(ComputeExecutor.QueryNativeQueue());
    Handle->QueueFamily    = ComputeExecutor.QueryQueueFamilyIndex();

    // Create RenderPass: RGBA8 color (transfer-src final layout) + D32 Reverse-Z depth
    std::array<VkAttachmentDescription, 2u> Attachments{};
    Attachments[0].format         = VK_FORMAT_R8G8B8A8_UNORM;
    Attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    Attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    Attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    Attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Attachments[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Attachments[0].finalLayout    = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    Attachments[1].format         = VK_FORMAT_D32_SFLOAT;
    Attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    Attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    Attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Attachments[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Attachments[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference ColorRef{ 0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference DepthRef{ 1u, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription Sub{};
    Sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    Sub.colorAttachmentCount    = 1u;
    Sub.pColorAttachments       = &ColorRef;
    Sub.pDepthStencilAttachment = &DepthRef;

    std::array<VkSubpassDependency, 2u> Deps{};
    Deps[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
    Deps[0].dstSubpass      = 0u;
    Deps[0].srcStageMask    = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    Deps[0].dstStageMask    = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    Deps[0].srcAccessMask   = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    Deps[0].dstAccessMask   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    Deps[1].srcSubpass      = 0u;
    Deps[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
    Deps[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    Deps[1].dstStageMask    = VK_PIPELINE_STAGE_TRANSFER_BIT;
    Deps[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    Deps[1].dstAccessMask   = VK_ACCESS_TRANSFER_READ_BIT;

    VkRenderPassCreateInfo RpInfo{};
    RpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    RpInfo.attachmentCount = static_cast<uint32_t>(Attachments.size());
    RpInfo.pAttachments    = Attachments.data();
    RpInfo.subpassCount    = 1u;
    RpInfo.pSubpasses      = &Sub;
    RpInfo.dependencyCount = static_cast<uint32_t>(Deps.size());
    RpInfo.pDependencies   = Deps.data();
    if (vkCreateRenderPass(Handle->Device, &RpInfo, nullptr, &Handle->TargetRenderStep) != VK_SUCCESS)
    {
        OutDiagnostic = "Failed to construct offscreen Vulkan render step.";
        Finalize();
        return false;
    }

    // Descriptor bank, command bank, completion fence, UBO
    const std::array<VkDescriptorPoolSize, 2u> BankCounts = {{
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4u },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 12u },
    }};
    VkDescriptorPoolCreateInfo DpInfo{};
    DpInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    DpInfo.maxSets       = 4u;
    DpInfo.poolSizeCount = static_cast<uint32_t>(BankCounts.size());
    DpInfo.pPoolSizes    = BankCounts.data();
    if (vkCreateDescriptorPool(Handle->Device, &DpInfo, nullptr, &Handle->DescriptorBank) != VK_SUCCESS)
    {
        OutDiagnostic = "Failed to construct rasterization descriptor bank.";
        Finalize();
        return false;
    }

    VkCommandPoolCreateInfo CpInfo{};
    CpInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    CpInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    CpInfo.queueFamilyIndex = Handle->QueueFamily;
    if (vkCreateCommandPool(Handle->Device, &CpInfo, nullptr, &Handle->CommandBank) != VK_SUCCESS)
    {
        OutDiagnostic = "Failed to construct rasterization command bank.";
        Finalize();
        return false;
    }

    VkCommandBufferAllocateInfo CbInfo{};
    CbInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    CbInfo.commandPool        = Handle->CommandBank;
    CbInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    CbInfo.commandBufferCount = 1u;
    if (vkAllocateCommandBuffers(Handle->Device, &CbInfo, &Handle->CommandRecorder) != VK_SUCCESS)
    {
        OutDiagnostic = "Failed to allocate rasterization command buffer.";
        Finalize();
        return false;
    }

    VkFenceCreateInfo FenceInfo{};
    FenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(Handle->Device, &FenceInfo, nullptr, &Handle->CompletionSignal) != VK_SUCCESS)
    {
        OutDiagnostic = "Failed to construct rasterization completion fence.";
        Finalize();
        return false;
    }

    if (!Handle->CreateHostBuffer(
            sizeof(ViewportGpuUniformBlock),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            Handle->UniformBuffer,
            Handle->UniformMemory,
            Handle->UniformMapped))
    {
        OutDiagnostic = "Failed to allocate viewport uniform buffer.";
        Finalize();
        return false;
    }

    // Layout A: Grid/Backdrop (1 UBO)
    VkDescriptorSetLayoutBinding GridBind{ 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo GridSetInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0u, 1u, &GridBind };
    if (vkCreateDescriptorSetLayout(Handle->Device, &GridSetInfo, nullptr, &Handle->GridDescLayout) != VK_SUCCESS)
    {
        OutDiagnostic = "Failed to construct grid descriptor layout.";
        Finalize();
        return false;
    }
    VkPipelineLayoutCreateInfo GridPlInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0u, 1u, &Handle->GridDescLayout, 0u, nullptr };
    if (vkCreatePipelineLayout(Handle->Device, &GridPlInfo, nullptr, &Handle->GridLayout) != VK_SUCCESS)
    {
        OutDiagnostic = "Failed to construct grid pipeline layout.";
        Finalize();
        return false;
    }

    // Layout B: Particles/Ribbons (1 UBO + 3 SSBOs)
    std::array<VkDescriptorSetLayoutBinding, 4u> PartBinds{};
    PartBinds[0] = { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    PartBinds[1] = { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT, nullptr };
    PartBinds[2] = { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT, nullptr };
    PartBinds[3] = { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo PartSetInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0u, 4u, PartBinds.data() };
    if (vkCreateDescriptorSetLayout(Handle->Device, &PartSetInfo, nullptr, &Handle->ParticleDescLayout) != VK_SUCCESS)
    {
        OutDiagnostic = "Failed to construct particle descriptor layout.";
        Finalize();
        return false;
    }
    VkPipelineLayoutCreateInfo PartPlInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0u, 1u, &Handle->ParticleDescLayout, 0u, nullptr };
    if (vkCreatePipelineLayout(Handle->Device, &PartPlInfo, nullptr, &Handle->ParticleLayout) != VK_SUCCESS)
    {
        OutDiagnostic = "Failed to construct particle raster layout.";
        Finalize();
        return false;
    }

    // Allocate descriptor slots
    const std::array<VkDescriptorSetLayout, 3u> Layouts = {
        Handle->GridDescLayout, Handle->ParticleDescLayout, Handle->ParticleDescLayout
    };
    std::array<VkDescriptorSet, 3u> Slots{};
    VkDescriptorSetAllocateInfo AllocSets{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, Handle->DescriptorBank, 3u, Layouts.data() };
    if (vkAllocateDescriptorSets(Handle->Device, &AllocSets, Slots.data()) != VK_SUCCESS)
    {
        OutDiagnostic = "Failed to allocate rasterization descriptor slots.";
        Finalize();
        return false;
    }
    Handle->GridDescriptorSlot      = Slots[0];
    Handle->BillboardDescriptorSlot = Slots[1];
    Handle->RibbonDescriptorSlot    = Slots[2];

    // Construct all 5 graphics executables from compiled Slang SPIR-V 1.3 modules
    if (!Handle->CreateGraphicsExecutable(
            ShaderBinaryDir / "ParticleBackdropRasterization.vert.spv", "ParticleBackdropVertex",
            ShaderBinaryDir / "ParticleBackdropRasterization.frag.spv", "ParticleBackdropFragment",
            Handle->GridLayout, false, false, false, false, Handle->BackdropExecutable, OutDiagnostic) ||
        !Handle->CreateGraphicsExecutable(
            ShaderBinaryDir / "ParticleFloorGridRasterization.vert.spv", "ParticleFloorGridVertex",
            ShaderBinaryDir / "ParticleFloorGridRasterization.frag.spv", "ParticleFloorGridFragment",
            Handle->GridLayout, true, false, true, false, Handle->FloorGridExecutable, OutDiagnostic) ||
        !Handle->CreateGraphicsExecutable(
            ShaderBinaryDir / "ParticleRibbonRasterization.vert.spv", "ParticleRibbonVertex",
            ShaderBinaryDir / "ParticleRibbonRasterization.frag.spv", "ParticleRibbonFragment",
            Handle->ParticleLayout, true, true, true, false, Handle->RibbonAlphaExec, OutDiagnostic) ||
        !Handle->CreateGraphicsExecutable(
            ShaderBinaryDir / "ParticleBillboardRasterization.vert.spv", "ParticleBillboardVertex",
            ShaderBinaryDir / "ParticleBillboardRasterization.frag.spv", "ParticleBillboardFragment",
            Handle->ParticleLayout, true, false, true, false, Handle->BillboardAlphaExec, OutDiagnostic) ||
        !Handle->CreateGraphicsExecutable(
            ShaderBinaryDir / "ParticleBillboardRasterization.vert.spv", "ParticleBillboardVertex",
            ShaderBinaryDir / "ParticleBillboardRasterization.frag.spv", "ParticleBillboardFragment",
            Handle->ParticleLayout, true, false, true, true, Handle->BillboardAdditiveExec, OutDiagnostic))
    {
        Finalize();
        return false;
    }

    Handle->Ready = true;
    return true;
}

void ParticleViewportRasterization::Finalize() noexcept
{
    if (Handle)
    {
        Handle->Cleanup();
        Handle.reset();
    }
}

bool ParticleViewportRasterization::QueryReady() const noexcept
{
    return Handle && Handle->Ready;
}

bool ParticleViewportRasterization::RenderOffscreen(
    const ParticleViewportRenderRequest& Request,
    std::string&                         OutDiagnostic) noexcept
{
    if (!QueryReady())
    {
        OutDiagnostic = "ParticleViewportRasterization is not initialized.";
        return false;
    }

    if (!Handle->EnsureOffscreenTargets(Request.WidthPx, Request.HeightPx))
    {
        OutDiagnostic = "Failed to allocate offscreen Vulkan viewport targets.";
        return false;
    }

    ViewportGpuUniformBlock Ubo{};
    const float Aspect = static_cast<float>(Handle->TargetWidthPx) / static_cast<float>(Handle->TargetHeightPx);
    AssembleReverseZCameraUniforms(Request.Camera, Aspect, Request, Ubo);
    std::memcpy(Handle->UniformMapped, &Ubo, sizeof(ViewportGpuUniformBlock));

    // Bind live GPU SSBOs straight from ParticleVulkanExecutor (zero CPU copy!)
    Handle->UpdateDescriptorSlots();

    vkResetCommandBuffer(Handle->CommandRecorder, 0u);
    VkCommandBufferBeginInfo BeginInfo{};
    BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(Handle->CommandRecorder, &BeginInfo) != VK_SUCCESS)
    {
        OutDiagnostic = "Failed to begin Vulkan rasterization command buffer.";
        return false;
    }

    // Ensure prior Compute SSBO writes & Host UBO writes are visible to Vertex/Fragment stages
    VkMemoryBarrier PreRasterBarrier{};
    PreRasterBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    PreRasterBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    PreRasterBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT;
    vkCmdPipelineBarrier(
        Handle->CommandRecorder,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0u,
        1u, &PreRasterBarrier,
        0u, nullptr,
        0u, nullptr);

    std::array<VkClearValue, 2u> ClearVals{};
    ClearVals[0].color        = { { 0.015f, 0.017f, 0.022f, 1.0f } };
    ClearVals[1].depthStencil = { 0.0f, 0u }; // Reverse-Z far clear

    VkRenderPassBeginInfo RpBegin{};
    RpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    RpBegin.renderPass        = Handle->TargetRenderStep;
    RpBegin.framebuffer       = Handle->TargetBuffer;
    RpBegin.renderArea.offset = { 0, 0 };
    RpBegin.renderArea.extent = { Handle->TargetWidthPx, Handle->TargetHeightPx };
    RpBegin.clearValueCount   = static_cast<uint32_t>(ClearVals.size());
    RpBegin.pClearValues      = ClearVals.data();

    VkViewport Vp{ 0.0f, 0.0f, static_cast<float>(Handle->TargetWidthPx), static_cast<float>(Handle->TargetHeightPx), 0.0f, 1.0f };
    VkRect2D   Sc{ { 0, 0 }, { Handle->TargetWidthPx, Handle->TargetHeightPx } };

    vkCmdBeginRenderPass(Handle->CommandRecorder, &RpBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(Handle->CommandRecorder, 0u, 1u, &Vp);
    vkCmdSetScissor(Handle->CommandRecorder, 0u, 1u, &Sc);

    // 1. Studio dark OLED radial backdrop
    vkCmdBindPipeline(Handle->CommandRecorder, VK_PIPELINE_BIND_POINT_GRAPHICS, Handle->BackdropExecutable);
    vkCmdBindDescriptorSets(Handle->CommandRecorder, VK_PIPELINE_BIND_POINT_GRAPHICS, Handle->GridLayout, 0u, 1u, &Handle->GridDescriptorSlot, 0u, nullptr);
    vkCmdDraw(Handle->CommandRecorder, 3u, 1u, 0u, 0u);

    // 2. GPU 3D perspective floor grid & +X/+Y axes
    if (Request.ShowFloorGrid)
    {
        vkCmdBindPipeline(Handle->CommandRecorder, VK_PIPELINE_BIND_POINT_GRAPHICS, Handle->FloorGridExecutable);
        vkCmdBindDescriptorSets(Handle->CommandRecorder, VK_PIPELINE_BIND_POINT_GRAPHICS, Handle->GridLayout, 0u, 1u, &Handle->GridDescriptorSlot, 0u, nullptr);
        vkCmdDraw(Handle->CommandRecorder, 6u, 1u, 0u, 0u);
    }

    // 3. GPU live strand / cloth ribbons directly from StrandConnectivity SSBO
    if (Request.RenderStrands && Request.ActiveCount > 0u)
    {
        vkCmdBindPipeline(Handle->CommandRecorder, VK_PIPELINE_BIND_POINT_GRAPHICS, Handle->RibbonAlphaExec);
        vkCmdBindDescriptorSets(Handle->CommandRecorder, VK_PIPELINE_BIND_POINT_GRAPHICS, Handle->ParticleLayout, 0u, 1u, &Handle->RibbonDescriptorSlot, 0u, nullptr);
        vkCmdDraw(Handle->CommandRecorder, 12u, Request.ActiveCount, 0u, 0u);
    }

    // 4. GPU instanced billboards / velocity sparks / glow orbs directly from Kinematics & Appearance SSBOs
    if (Request.RenderBillboards && Request.ActiveCount > 0u)
    {
        const VkPipeline BillExec = (Request.BlendMode == ParticleGpuBlendMode::Additive)
            ? Handle->BillboardAdditiveExec
            : Handle->BillboardAlphaExec;
        vkCmdBindPipeline(Handle->CommandRecorder, VK_PIPELINE_BIND_POINT_GRAPHICS, BillExec);
        vkCmdBindDescriptorSets(Handle->CommandRecorder, VK_PIPELINE_BIND_POINT_GRAPHICS, Handle->ParticleLayout, 0u, 1u, &Handle->BillboardDescriptorSlot, 0u, nullptr);
        vkCmdDraw(Handle->CommandRecorder, 6u, Request.ActiveCount, 0u, 0u);
    }

    vkCmdEndRenderPass(Handle->CommandRecorder);

    // Copy rendered RGBA8 image into host-visible readback buffer
    VkBufferImageCopy CopyRegion{};
    CopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    CopyRegion.imageSubresource.layerCount = 1u;
    CopyRegion.imageExtent                 = { Handle->TargetWidthPx, Handle->TargetHeightPx, 1u };
    vkCmdCopyImageToBuffer(
        Handle->CommandRecorder,
        Handle->ColorImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        Handle->ReadbackBuffer,
        1u,
        &CopyRegion);

    VkMemoryBarrier ReadbackBarrier{};
    ReadbackBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    ReadbackBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    ReadbackBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(
        Handle->CommandRecorder,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0u,
        1u, &ReadbackBarrier,
        0u, nullptr,
        0u, nullptr);

    if (vkEndCommandBuffer(Handle->CommandRecorder) != VK_SUCCESS)
    {
        OutDiagnostic = "Failed to finalize Vulkan rasterization command buffer.";
        return false;
    }

    vkResetFences(Handle->Device, 1u, &Handle->CompletionSignal);
    VkSubmitInfo SubInfo{};
    SubInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    SubInfo.commandBufferCount = 1u;
    SubInfo.pCommandBuffers    = &Handle->CommandRecorder;
    if (vkQueueSubmit(Handle->GraphicsQueue, 1u, &SubInfo, Handle->CompletionSignal) != VK_SUCCESS ||
        vkWaitForFences(Handle->Device, 1u, &Handle->CompletionSignal, VK_TRUE, 15'000'000'000ull) != VK_SUCCESS)
    {
        OutDiagnostic = "Vulkan particle viewport rasterization execution failed.";
        return false;
    }

    std::memcpy(Handle->CpuRgbaPixels.data(), Handle->ReadbackMapped, Handle->CpuRgbaPixels.size());
    return true;
}

std::span<const uint8_t> ParticleViewportRasterization::QueryColorRgbaPixels() const noexcept
{
    return Handle ? std::span<const uint8_t>(Handle->CpuRgbaPixels) : std::span<const uint8_t>{};
}

uint32_t ParticleViewportRasterization::QueryWidthPx() const noexcept
{
    return Handle ? Handle->TargetWidthPx : 0u;
}

uint32_t ParticleViewportRasterization::QueryHeightPx() const noexcept
{
    return Handle ? Handle->TargetHeightPx : 0u;
}

#endif // FRONTIER_WITH_VULKAN

} // namespace Frontier::Workspaces
