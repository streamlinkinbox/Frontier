//============================================================================================================================================
//                                                     VISIBILITYEXCHANGE.CPP
//============================================================================================================================================
// 🧩 Vulkan side of the R2 front end: resident scene buffers, cull / raster / HiZ / resolve pipelines, per-slot frame
//    constants, indirect draw lists, timestamp queries and the telemetry read-back.

#include <vulkan/vulkan.h>
#include "VisibilityExchange.h"
#include "../GeometricRaster/SceneStructure.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

static constexpr uint32_t kMaximumCycleSlots = 3u;
static constexpr uint32_t kTimestampCount    = 12u;   // per slot: cull1 ×2, raster1 ×2, hiz ×2, cull2 ×2 (folded), raster2, resolve ×2, kernel ×2
static constexpr uint32_t kCounterCount      = 8u;    // SceneRecords.slang kCounterCount
static constexpr uint32_t kCounterDrawPhaseTwoByte = 7u * 4u;

// Frame constants — std140 mirror of FrameConstants in Shaders/SceneRecords.slang.
struct alignas(16) FrameConstantRecord
{
    float    ViewClip[16];
    float    PreviousViewClip[16];
    float    FrustumPlanes[5][4];
    float    CameraOrigin[4];
    float    CameraForward[4];
    float    CameraRight[4];
    float    CameraUp[4];
    float    Jitter[4];
    uint32_t Extent[4];
    uint32_t Control[4];
    float    Projection[4];
};
static_assert(sizeof(FrameConstantRecord) == 336u, "FrameConstantRecord must match the std140 block");

struct HiZPushRecord { uint32_t SourceExtent[2]; uint32_t TargetExtent[2]; uint32_t CopyLevelZero; };

const char* DebugViewName(DebugViewCategory View) noexcept
{
    static const char* Names[] = { "Off", "Depth", "Visibility ID", "Motion Vectors", "Cluster ID", "HiZ (level 3)", "Albedo", "Normal", "Roughness", "Metalness", "Shading Normal", "Reservoir M", "Reservoir W", "Reservoir Age" };
    const uint32_t I = static_cast<uint32_t>(View);
    return I < static_cast<uint32_t>(DebugViewCategory::Count) ? Names[I] : Names[0];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    VULKAN RECORD
//------------------------------------------------------------------------------------------------------------------------

struct GpuBuffer
{
    VkBuffer       Buffer = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkDeviceSize   Bytes  = 0u;
    void*          Mapped = nullptr;
};

struct GpuImage
{
    VkImage        Image  = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkImageView    View   = VK_NULL_HANDLE;               // whole image
    std::vector<VkImageView> LevelViews;                  // HiZ per-level storage views
    uint32_t       Levels = 1u;
    VkFormat       Format = VK_FORMAT_UNDEFINED;
};

struct VisibilityExchange::VulkanRecord
{
    VkDevice                          Device         = VK_NULL_HANDLE;
    VkPhysicalDevice                  PhysicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties  MemoryProperties{};
    float                             TimestampPeriod = 1.0f;   // [ns / tick]
    uint32_t                          SlotCount      = 2u;
    bool                              DrawIndirectCount = false;

    // Scene (host-visible, uploaded once)
    GpuBuffer Vertices, Indices, Instances, Clusters, Materials, Luminaires, FlatTriangles;
    VkBuffer  BorrowedSlabs = VK_NULL_HANDLE;                   // R4b: SwapchainExchange's MaterialSlabRecord[] (raster binding 6)
    VkBuffer  BorrowedReservoir = VK_NULL_HANDLE;             // R6 row 3: kernel's prev-frame reservoirs (resolve binding 13, M/W/Age views)
    VkSampler BorrowedSampler = VK_NULL_HANDLE;                 // R4b: bindless table sampler + views (raster binding 7)
    std::vector<VkImageView> BorrowedTextures;
    uint32_t  TextureSlotCapacity = 0u;                         // 0 = no descriptor indexing: raster set stops at binding 5
    GpuBuffer Draws, Counters, VisibleBitsA, VisibleBitsB;      // per-frame cull state (device-local except counters)
    GpuBuffer CounterReadback[kMaximumCycleSlots];              // host-visible copies for telemetry
    GpuBuffer FrameConstants[kMaximumCycleSlots][2];            // [slot][phase]
    bool      VisibleParity = false;                            // which bit buffer is "previous" this frame

    // Targets (render-size)
    GpuImage Depth, Visibility, Motion, HiZ, Surface, Normal;
    VkImageView PresentationView = VK_NULL_HANDLE;              // borrowed from SwapchainExchange
    VkFramebuffer Framebuffer    = VK_NULL_HANDLE;
    VkExtent2D    TargetExtent{};
    bool          TargetsInitialised = false;

    // Pipelines
    VkRenderPass          RasterPassClear = VK_NULL_HANDLE, RasterPassLoad = VK_NULL_HANDLE;
    VkDescriptorSetLayout CullLayout = VK_NULL_HANDLE, RasterLayout = VK_NULL_HANDLE, HiZLayout = VK_NULL_HANDLE, ResolveLayout = VK_NULL_HANDLE;
    VkPipelineLayout      CullPipelineLayout = VK_NULL_HANDLE, RasterPipelineLayout = VK_NULL_HANDLE, HiZPipelineLayout = VK_NULL_HANDLE, ResolvePipelineLayout = VK_NULL_HANDLE;
    VkPipeline            CullPipeline = VK_NULL_HANDLE, RasterPipelineClear = VK_NULL_HANDLE, RasterPipelineLoad = VK_NULL_HANDLE, HiZPipeline = VK_NULL_HANDLE, ResolvePipeline = VK_NULL_HANDLE;
    VkSampler             PointSampler = VK_NULL_HANDLE;

    // Descriptor sets: [slot][phase] for cull / raster / resolve; HiZ one per level (+1 for the level-0 copy)
    VkDescriptorPool      Pool = VK_NULL_HANDLE;
    VkDescriptorSet       CullSets[kMaximumCycleSlots][2]{};
    VkDescriptorSet       RasterSets[kMaximumCycleSlots][2]{};
    VkDescriptorSet       ResolveSets[kMaximumCycleSlots]{};
    std::vector<VkDescriptorSet> HiZSets;

    // Timing
    VkQueryPool           Timestamps = VK_NULL_HANDLE;
    bool                  SlotRecorded[kMaximumCycleSlots]{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    LOCAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace {

std::filesystem::path ResolveAssetPath(const std::string& Relative)
{
    std::error_code Error;
    if (std::filesystem::exists(Relative, Error)) return Relative;
    std::filesystem::path Probe = std::filesystem::current_path(Error);
    for (int Depth = 0; Depth < 12 && !Probe.empty(); ++Depth)
    {
        const std::filesystem::path Candidate = Probe / Relative;
        if (std::filesystem::exists(Candidate, Error)) return Candidate;
        const std::filesystem::path Parent = Probe.parent_path();
        if (Parent == Probe) break;
        Probe = Parent;
    }
    return Relative;
}

VkShaderModule LoadShader(VkDevice Device, const char* Relative)
{
    const std::filesystem::path Path = ResolveAssetPath(Relative);
    std::ifstream File(Path, std::ios::binary | std::ios::ate);
    if (!File.is_open()) { std::cerr << "[VisibilityExchange] Cannot open SPIR-V: " << Relative << "\n"; return VK_NULL_HANDLE; }
    const std::streamsize Bytes = File.tellg();
    if (Bytes < 4 || Bytes % 4) { std::cerr << "[VisibilityExchange] Malformed SPIR-V: " << Relative << "\n"; return VK_NULL_HANDLE; }
    std::vector<uint32_t> Code(static_cast<size_t>(Bytes) / 4u);
    File.seekg(0);
    File.read(reinterpret_cast<char*>(Code.data()), Bytes);
    VkShaderModuleCreateInfo Info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    Info.codeSize = Code.size() * 4u;
    Info.pCode    = Code.data();
    VkShaderModule Module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(Device, &Info, nullptr, &Module) != VK_SUCCESS) { std::cerr << "[VisibilityExchange] vkCreateShaderModule failed: " << Relative << "\n"; return VK_NULL_HANDLE; }
    std::cerr << "[VisibilityExchange] Loaded SPIR-V: " << Path.string() << "\n";
    return Module;
}

uint32_t FindMemoryType(const VkPhysicalDeviceMemoryProperties& P, uint32_t Mask, VkMemoryPropertyFlags Flags)
{
    for (uint32_t I = 0u; I < P.memoryTypeCount; ++I)
        if ((Mask & (1u << I)) && (P.memoryTypes[I].propertyFlags & Flags) == Flags) return I;
    return 0u;
}

void DestroyBuffer(VkDevice Device, GpuBuffer& B)
{
    if (B.Mapped) vkUnmapMemory(Device, B.Memory);
    if (B.Buffer) vkDestroyBuffer(Device, B.Buffer, nullptr);
    if (B.Memory) vkFreeMemory(Device, B.Memory, nullptr);
    B = {};
}

bool CreateBuffer(VkDevice Device, const VkPhysicalDeviceMemoryProperties& P, GpuBuffer& B, VkDeviceSize Bytes, VkBufferUsageFlags Usage, bool HostVisible, const char* Label)
{
    DestroyBuffer(Device, B);
    B.Bytes = std::max<VkDeviceSize>(Bytes, 16u);
    VkBufferCreateInfo Info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    Info.size  = B.Bytes;
    Info.usage = Usage;
    if (vkCreateBuffer(Device, &Info, nullptr, &B.Buffer) != VK_SUCCESS) { std::cerr << "[VisibilityExchange] vkCreateBuffer (" << Label << ") failed.\n"; return false; }
    VkMemoryRequirements R{};
    vkGetBufferMemoryRequirements(Device, B.Buffer, &R);
    VkMemoryAllocateInfo Allocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    Allocate.allocationSize  = R.size;
    Allocate.memoryTypeIndex = FindMemoryType(P, R.memoryTypeBits, HostVisible ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(Device, &Allocate, nullptr, &B.Memory) != VK_SUCCESS) { std::cerr << "[VisibilityExchange] vkAllocateMemory (" << Label << ") failed.\n"; return false; }
    vkBindBufferMemory(Device, B.Buffer, B.Memory, 0u);
    if (HostVisible && vkMapMemory(Device, B.Memory, 0u, B.Bytes, 0u, &B.Mapped) != VK_SUCCESS) B.Mapped = nullptr;
    return true;
}

bool UploadBuffer(VkDevice Device, const VkPhysicalDeviceMemoryProperties& P, GpuBuffer& B, const void* Source, size_t Bytes, VkBufferUsageFlags Usage, const char* Label)
{
    if (!CreateBuffer(Device, P, B, Bytes, Usage, true, Label)) return false;
    if (B.Mapped) { std::memset(B.Mapped, 0, static_cast<size_t>(B.Bytes)); if (Bytes) std::memcpy(B.Mapped, Source, Bytes); }
    return true;
}

void DestroyImage(VkDevice Device, GpuImage& I)
{
    for (VkImageView V : I.LevelViews) if (V) vkDestroyImageView(Device, V, nullptr);
    I.LevelViews.clear();
    if (I.View)   vkDestroyImageView(Device, I.View, nullptr);
    if (I.Image)  vkDestroyImage(Device, I.Image, nullptr);
    if (I.Memory) vkFreeMemory(Device, I.Memory, nullptr);
    I = {};
}

bool CreateImage(VkDevice Device, const VkPhysicalDeviceMemoryProperties& P, GpuImage& I, VkFormat Format, VkExtent2D Extent, uint32_t Levels, VkImageUsageFlags Usage, VkImageAspectFlags Aspect, const char* Label)
{
    DestroyImage(Device, I);
    I.Format = Format;
    I.Levels = Levels;
    VkImageCreateInfo Info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    Info.imageType     = VK_IMAGE_TYPE_2D;
    Info.format        = Format;
    Info.extent        = { Extent.width, Extent.height, 1u };
    Info.mipLevels     = Levels;
    Info.arrayLayers   = 1u;
    Info.samples       = VK_SAMPLE_COUNT_1_BIT;
    Info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    Info.usage         = Usage;
    Info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    Info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(Device, &Info, nullptr, &I.Image) != VK_SUCCESS) { std::cerr << "[VisibilityExchange] vkCreateImage (" << Label << ") failed.\n"; return false; }
    VkMemoryRequirements R{};
    vkGetImageMemoryRequirements(Device, I.Image, &R);
    VkMemoryAllocateInfo Allocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    Allocate.allocationSize  = R.size;
    Allocate.memoryTypeIndex = FindMemoryType(P, R.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(Device, &Allocate, nullptr, &I.Memory) != VK_SUCCESS) { std::cerr << "[VisibilityExchange] vkAllocateMemory (" << Label << ") failed.\n"; return false; }
    vkBindImageMemory(Device, I.Image, I.Memory, 0u);

    VkImageViewCreateInfo View{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    View.image    = I.Image;
    View.viewType = VK_IMAGE_VIEW_TYPE_2D;
    View.format   = Format;
    View.subresourceRange = { Aspect, 0u, Levels, 0u, 1u };
    if (vkCreateImageView(Device, &View, nullptr, &I.View) != VK_SUCCESS) { std::cerr << "[VisibilityExchange] vkCreateImageView (" << Label << ") failed.\n"; return false; }
    if (Levels > 1u)
    {
        I.LevelViews.resize(Levels);
        for (uint32_t L = 0u; L < Levels; ++L)
        {
            View.subresourceRange = { Aspect, L, 1u, 0u, 1u };
            if (vkCreateImageView(Device, &View, nullptr, &I.LevelViews[L]) != VK_SUCCESS) return false;
        }
    }
    return true;
}

void ImageBarrier(VkCommandBuffer Command, VkImage Image, VkImageAspectFlags Aspect, VkImageLayout From, VkImageLayout To,
                  VkAccessFlags SrcAccess, VkAccessFlags DstAccess, VkPipelineStageFlags SrcStage, VkPipelineStageFlags DstStage,
                  uint32_t BaseLevel = 0u, uint32_t Levels = VK_REMAINING_MIP_LEVELS)
{
    VkImageMemoryBarrier B{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    B.oldLayout = From; B.newLayout = To;
    B.srcQueueFamilyIndex = B.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    B.image = Image;
    B.subresourceRange = { Aspect, BaseLevel, Levels, 0u, 1u };
    B.srcAccessMask = SrcAccess; B.dstAccessMask = DstAccess;
    vkCmdPipelineBarrier(Command, SrcStage, DstStage, 0u, 0u, nullptr, 0u, nullptr, 1u, &B);
}

void BufferBarrier(VkCommandBuffer Command, VkBuffer Buffer, VkAccessFlags SrcAccess, VkAccessFlags DstAccess, VkPipelineStageFlags SrcStage, VkPipelineStageFlags DstStage)
{
    VkBufferMemoryBarrier B{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    B.srcAccessMask = SrcAccess; B.dstAccessMask = DstAccess;
    B.srcQueueFamilyIndex = B.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    B.buffer = Buffer; B.offset = 0u; B.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(Command, SrcStage, DstStage, 0u, 0u, nullptr, 1u, &B, 0u, nullptr);
}

uint32_t MipCount(uint32_t W, uint32_t H)
{
    uint32_t Levels = 1u;
    while ((W | H) >> Levels) ++Levels;
    return Levels;
}

// Six frustum planes from a view-clip matrix would need the far plane; with an infinite projection we derive the five
//    finite planes directly from the camera basis (normal points inward, |n| = 1).
void ConstructFrustumPlanes(const CameraClipConfiguration& C, float Planes[5][4])
{
    const float TanY = C.TanHalfFieldOfView, TanX = TanY * C.AspectRatio;
    const auto Emit = [&](int I, Vector3 N) { N = N.Normalized(); Planes[I][0] = N.x; Planes[I][1] = N.y; Planes[I][2] = N.z; Planes[I][3] = -OrientationClassifier::DotProduct(N, C.Origin); };
    Emit(0, C.Forward * TanX + C.Right);          // left   : points +Right-ish
    Emit(1, C.Forward * TanX - C.Right);          // right
    Emit(2, C.Forward * TanY + C.Up);             // bottom
    Emit(3, C.Forward * TanY - C.Up);             // top
    Vector3 N = C.Forward;
    Planes[4][0] = N.x; Planes[4][1] = N.y; Planes[4][2] = N.z;
    Planes[4][3] = -OrientationClassifier::DotProduct(N, C.Origin) - C.NearDistance;   // near
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

VisibilityExchange::VisibilityExchange() noexcept : Vulkan(new VulkanRecord{}) {}
VisibilityExchange::~VisibilityExchange() noexcept { Retire(); delete Vulkan; }

bool VisibilityExchange::Bring(void* Device, void* PhysicalDevice, uint32_t CycleSlotCount, bool DrawIndirectCount, uint32_t TextureSlotCapacity) noexcept
{
    Vulkan->Device            = static_cast<VkDevice>(Device);
    Vulkan->PhysicalDevice    = static_cast<VkPhysicalDevice>(PhysicalDevice);
    Vulkan->SlotCount         = std::clamp(CycleSlotCount, 1u, kMaximumCycleSlots);
    Vulkan->DrawIndirectCount = DrawIndirectCount;
    Vulkan->TextureSlotCapacity = TextureSlotCapacity;
    vkGetPhysicalDeviceMemoryProperties(Vulkan->PhysicalDevice, &Vulkan->MemoryProperties);
    VkPhysicalDeviceProperties Properties{};
    vkGetPhysicalDeviceProperties(Vulkan->PhysicalDevice, &Properties);
    Vulkan->TimestampPeriod = Properties.limits.timestampPeriod;

    if (!DrawIndirectCount)
        std::cerr << "[VisibilityExchange] drawIndirectCount unavailable - falling back to vkCmdDrawIndexedIndirect with the full cluster count (empty draws are zero-sized).\n";

    VkSamplerCreateInfo Sampler{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    Sampler.magFilter = Sampler.minFilter = VK_FILTER_NEAREST;
    Sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    Sampler.addressModeU = Sampler.addressModeV = Sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    Sampler.maxLod = VK_LOD_CLAMP_NONE;
    if (vkCreateSampler(Vulkan->Device, &Sampler, nullptr, &Vulkan->PointSampler) != VK_SUCCESS) return false;

    VkQueryPoolCreateInfo Query{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
    Query.queryType  = VK_QUERY_TYPE_TIMESTAMP;
    Query.queryCount = kTimestampCount * kMaximumCycleSlots;
    if (vkCreateQueryPool(Vulkan->Device, &Query, nullptr, &Vulkan->Timestamps) != VK_SUCCESS) return false;

    for (uint32_t S = 0u; S < Vulkan->SlotCount; ++S)
    {
        for (uint32_t P = 0u; P < 2u; ++P)
            if (!CreateBuffer(Vulkan->Device, Vulkan->MemoryProperties, Vulkan->FrameConstants[S][P], sizeof(FrameConstantRecord), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, "frame constants")) return false;
        if (!CreateBuffer(Vulkan->Device, Vulkan->MemoryProperties, Vulkan->CounterReadback[S], kCounterCount * 4u, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "counter readback")) return false;
    }
    if (!CreateBuffer(Vulkan->Device, Vulkan->MemoryProperties, Vulkan->Counters, kCounterCount * 4u,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false, "cull counters")) return false;

    if (!BringPipelines()) return false;
    Ready = true;
    return true;
}

void VisibilityExchange::Retire() noexcept
{
    if (!Vulkan || !Vulkan->Device) return;
    VkDevice D = Vulkan->Device;
    vkDeviceWaitIdle(D);
    RetireTargets();
    for (GpuBuffer* B : { &Vulkan->Vertices, &Vulkan->Indices, &Vulkan->Instances, &Vulkan->Clusters, &Vulkan->Materials, &Vulkan->Luminaires, &Vulkan->FlatTriangles,
                          &Vulkan->Draws, &Vulkan->Counters, &Vulkan->VisibleBitsA, &Vulkan->VisibleBitsB })
        DestroyBuffer(D, *B);
    for (uint32_t S = 0u; S < kMaximumCycleSlots; ++S)
    {
        DestroyBuffer(D, Vulkan->CounterReadback[S]);
        for (uint32_t P = 0u; P < 2u; ++P) DestroyBuffer(D, Vulkan->FrameConstants[S][P]);
    }
    if (Vulkan->Pool) vkDestroyDescriptorPool(D, Vulkan->Pool, nullptr);
    for (VkPipeline P : { Vulkan->CullPipeline, Vulkan->RasterPipelineClear, Vulkan->RasterPipelineLoad, Vulkan->HiZPipeline, Vulkan->ResolvePipeline }) if (P) vkDestroyPipeline(D, P, nullptr);
    for (VkPipelineLayout L : { Vulkan->CullPipelineLayout, Vulkan->RasterPipelineLayout, Vulkan->HiZPipelineLayout, Vulkan->ResolvePipelineLayout }) if (L) vkDestroyPipelineLayout(D, L, nullptr);
    for (VkDescriptorSetLayout L : { Vulkan->CullLayout, Vulkan->RasterLayout, Vulkan->HiZLayout, Vulkan->ResolveLayout }) if (L) vkDestroyDescriptorSetLayout(D, L, nullptr);
    if (Vulkan->RasterPassClear) vkDestroyRenderPass(D, Vulkan->RasterPassClear, nullptr);
    if (Vulkan->RasterPassLoad)  vkDestroyRenderPass(D, Vulkan->RasterPassLoad, nullptr);
    if (Vulkan->PointSampler)    vkDestroySampler(D, Vulkan->PointSampler, nullptr);
    if (Vulkan->Timestamps)      vkDestroyQueryPool(D, Vulkan->Timestamps, nullptr);
    *Vulkan = VulkanRecord{};
    Ready = false;
}

void VisibilityExchange::RetireTargets() noexcept
{
    VkDevice D = Vulkan->Device;
    if (Vulkan->Framebuffer) vkDestroyFramebuffer(D, Vulkan->Framebuffer, nullptr);
    Vulkan->Framebuffer = VK_NULL_HANDLE;
    for (GpuImage* I : { &Vulkan->Depth, &Vulkan->Visibility, &Vulkan->Motion, &Vulkan->HiZ, &Vulkan->Surface, &Vulkan->Normal }) DestroyImage(D, *I);
    Vulkan->TargetsInitialised = false;
    PreviousValid = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      PIPELINES
//------------------------------------------------------------------------------------------------------------------------

bool VisibilityExchange::BringPipelines() noexcept
{
    VkDevice D = Vulkan->Device;

    const auto Binding = [](uint32_t Index, VkDescriptorType Type, VkShaderStageFlags Stages) { VkDescriptorSetLayoutBinding B{}; B.binding = Index; B.descriptorType = Type; B.descriptorCount = 1u; B.stageFlags = Stages; return B; };
    const auto MakeLayout = [&](const std::vector<VkDescriptorSetLayoutBinding>& Bindings, VkDescriptorSetLayout& Out)
    {
        VkDescriptorSetLayoutCreateInfo Info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        Info.bindingCount = static_cast<uint32_t>(Bindings.size());
        Info.pBindings    = Bindings.data();
        return vkCreateDescriptorSetLayout(D, &Info, nullptr, &Out) == VK_SUCCESS;
    };
    const auto MakePipelineLayout = [&](VkDescriptorSetLayout Set, uint32_t PushBytes, VkShaderStageFlags PushStages, VkPipelineLayout& Out)
    {
        VkPushConstantRange Range{ PushStages, 0u, PushBytes };
        VkPipelineLayoutCreateInfo Info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        Info.setLayoutCount = 1u; Info.pSetLayouts = &Set;
        Info.pushConstantRangeCount = PushBytes ? 1u : 0u; Info.pPushConstantRanges = PushBytes ? &Range : nullptr;
        return vkCreatePipelineLayout(D, &Info, nullptr, &Out) == VK_SUCCESS;
    };
    const auto MakeCompute = [&](const char* Spirv, VkPipelineLayout Layout, VkPipeline& Out)
    {
        VkShaderModule Module = LoadShader(D, Spirv);
        if (!Module) return false;
        VkComputePipelineCreateInfo Info{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        Info.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0u, VK_SHADER_STAGE_COMPUTE_BIT, Module, "main", nullptr };
        Info.layout = Layout;
        const VkResult R = vkCreateComputePipelines(D, VK_NULL_HANDLE, 1u, &Info, nullptr, &Out);
        vkDestroyShaderModule(D, Module, nullptr);
        if (R != VK_SUCCESS) std::cerr << "[VisibilityExchange] compute pipeline failed: " << Spirv << "\n";
        return R == VK_SUCCESS;
    };

    constexpr VkShaderStageFlags CS = VK_SHADER_STAGE_COMPUTE_BIT, VS = VK_SHADER_STAGE_VERTEX_BIT, FS = VK_SHADER_STAGE_FRAGMENT_BIT;
    constexpr VkDescriptorType UBO = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SSBO = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, IMG = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, TEX = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    // ① Cull: 0 frame, 1 instances, 2 clusters, 3 draws, 4 counters, 5 previous bits, 6 current bits, 7 HiZ
    if (!MakeLayout({ Binding(0, UBO, CS), Binding(1, SSBO, CS), Binding(2, SSBO, CS), Binding(3, SSBO, CS), Binding(4, SSBO, CS), Binding(5, SSBO, CS), Binding(6, SSBO, CS), Binding(7, TEX, CS) }, Vulkan->CullLayout)) return false;
    if (!MakePipelineLayout(Vulkan->CullLayout, 0u, 0u, Vulkan->CullPipelineLayout)) return false;
    if (!MakeCompute("Engine/Shaders/ClusterCull.spv", Vulkan->CullPipelineLayout, Vulkan->CullPipeline)) return false;

    // ② Raster: 0 frame, 1 instances, 2 clusters, 3 vertices — R4b: 4 (unused), 5 materials, 6 slabs, 7 Textures[] (variable count, last) for the alpha mask
    {
        std::vector<VkDescriptorSetLayoutBinding> RasterBindings{ Binding(0, UBO, VS | FS), Binding(1, SSBO, VS | FS), Binding(2, SSBO, VS | FS), Binding(3, SSBO, VS) };
        if (Vulkan->TextureSlotCapacity > 0u)
        {
            RasterBindings.push_back(Binding(5, SSBO, FS));
            RasterBindings.push_back(Binding(6, SSBO, FS));
            VkDescriptorSetLayoutBinding Table = Binding(7, TEX, FS); Table.descriptorCount = Vulkan->TextureSlotCapacity;
            RasterBindings.push_back(Table);
            std::vector<VkDescriptorBindingFlags> Flags(RasterBindings.size(), 0u);
            Flags.back() = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
            VkDescriptorSetLayoutBindingFlagsCreateInfo FlagsInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
            FlagsInfo.bindingCount = static_cast<uint32_t>(Flags.size()); FlagsInfo.pBindingFlags = Flags.data();
            VkDescriptorSetLayoutCreateInfo Info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            Info.pNext = &FlagsInfo; Info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
            Info.bindingCount = static_cast<uint32_t>(RasterBindings.size()); Info.pBindings = RasterBindings.data();
            if (vkCreateDescriptorSetLayout(D, &Info, nullptr, &Vulkan->RasterLayout) != VK_SUCCESS) return false;
        }
        else if (!MakeLayout(RasterBindings, Vulkan->RasterLayout)) return false;
    }
    if (!MakePipelineLayout(Vulkan->RasterLayout, 0u, 0u, Vulkan->RasterPipelineLayout)) return false;

    // ③ HiZ: 0 source (sampled), 1 target level (storage)
    if (!MakeLayout({ Binding(0, TEX, CS), Binding(1, IMG, CS) }, Vulkan->HiZLayout)) return false;
    if (!MakePipelineLayout(Vulkan->HiZLayout, sizeof(HiZPushRecord), CS, Vulkan->HiZPipelineLayout)) return false;
    if (!MakeCompute("Engine/Shaders/HiZReduce.spv", Vulkan->HiZPipelineLayout, Vulkan->HiZPipeline)) return false;

    // ④ Resolve: 0 frame, 1 instances, 2 clusters, 3 vertices, 4 indices, 5 materials, 6 visibility, 7 motion, 8 depth, 9 HiZ, 10 surface, 11 normal, 12 presentation, 13 reservoirs (R6 row 3, M/W/Age views)
    if (!MakeLayout({ Binding(0, UBO, CS), Binding(1, SSBO, CS), Binding(2, SSBO, CS), Binding(3, SSBO, CS), Binding(4, SSBO, CS), Binding(5, SSBO, CS),
                      Binding(6, TEX, CS), Binding(7, TEX, CS), Binding(8, TEX, CS), Binding(9, TEX, CS), Binding(10, IMG, CS), Binding(11, IMG, CS), Binding(12, IMG, CS), Binding(13, SSBO, CS) }, Vulkan->ResolveLayout)) return false;
    if (!MakePipelineLayout(Vulkan->ResolveLayout, 0u, 0u, Vulkan->ResolvePipelineLayout)) return false;
    if (!MakeCompute("Engine/Shaders/SurfaceResolve.spv", Vulkan->ResolvePipelineLayout, Vulkan->ResolvePipeline)) return false;

    // ⑤ Visibility render passes: attachments 0 visId R32_UINT, 1 motion RG16F, 2 depth D32. Clear (phase 1) / Load (phase 2).
    for (int Variant = 0; Variant < 2; ++Variant)
    {
        const VkAttachmentLoadOp Load = Variant == 0 ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        const VkImageLayout Initial   = Variant == 0 ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL;
        std::array<VkAttachmentDescription, 3u> A{};
        A[0].format = VK_FORMAT_R32_UINT;      A[1].format = VK_FORMAT_R16G16_SFLOAT; A[2].format = VK_FORMAT_D32_SFLOAT;
        for (VkAttachmentDescription& X : A)
        {
            X.samples = VK_SAMPLE_COUNT_1_BIT; X.loadOp = Load; X.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            X.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; X.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            X.initialLayout = Initial; X.finalLayout = VK_IMAGE_LAYOUT_GENERAL;   // GENERAL: sampled by HiZ / resolve and loaded by phase 2
        }
        VkAttachmentReference Colour[2] = { { 0u, VK_IMAGE_LAYOUT_GENERAL }, { 1u, VK_IMAGE_LAYOUT_GENERAL } };
        VkAttachmentReference Depth{ 2u, VK_IMAGE_LAYOUT_GENERAL };
        VkSubpassDescription Subpass{};
        Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        Subpass.colorAttachmentCount = 2u; Subpass.pColorAttachments = Colour; Subpass.pDepthStencilAttachment = &Depth;
        VkSubpassDependency Dependencies[2]{};
        Dependencies[0] = { VK_SUBPASS_EXTERNAL, 0u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 0u };
        Dependencies[1] = { 0u, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 0u };
        VkRenderPassCreateInfo Info{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        Info.attachmentCount = 3u; Info.pAttachments = A.data(); Info.subpassCount = 1u; Info.pSubpasses = &Subpass; Info.dependencyCount = 2u; Info.pDependencies = Dependencies;
        if (vkCreateRenderPass(D, &Info, nullptr, Variant == 0 ? &Vulkan->RasterPassClear : &Vulkan->RasterPassLoad) != VK_SUCCESS) return false;
    }

    // ⑥ Raster graphics pipeline (dynamic viewport/scissor; reverse-Z GREATER; no culling — the cone test decides, and the kernel is two-sided)
    VkShaderModule Vertex = LoadShader(D, "Engine/Shaders/VisibilityRaster.vert.spv");
    VkShaderModule Fragment = LoadShader(D, "Engine/Shaders/VisibilityRaster.frag.spv");
    if (!Vertex || !Fragment) return false;
    VkPipelineShaderStageCreateInfo Stages[2] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0u, VK_SHADER_STAGE_VERTEX_BIT,   Vertex,   "main", nullptr },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0u, VK_SHADER_STAGE_FRAGMENT_BIT, Fragment, "main", nullptr } };
    VkPipelineVertexInputStateCreateInfo   VertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };   // pulled from the SSBO
    VkPipelineInputAssemblyStateCreateInfo Assembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    Assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo Viewport{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    Viewport.viewportCount = 1u; Viewport.scissorCount = 1u;
    VkPipelineRasterizationStateCreateInfo Raster{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    Raster.polygonMode = VK_POLYGON_MODE_FILL; Raster.cullMode = VK_CULL_MODE_NONE; Raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; Raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo Multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo DepthState{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    DepthState.depthTestEnable = VK_TRUE; DepthState.depthWriteEnable = VK_TRUE; DepthState.depthCompareOp = VK_COMPARE_OP_GREATER;
    VkPipelineColorBlendAttachmentState Blend[2]{};
    Blend[0].colorWriteMask = Blend[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo BlendState{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    BlendState.attachmentCount = 2u; BlendState.pAttachments = Blend;
    const VkDynamicState Dynamic[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo DynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    DynamicState.dynamicStateCount = 2u; DynamicState.pDynamicStates = Dynamic;

    VkGraphicsPipelineCreateInfo Graphics{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    Graphics.stageCount = 2u; Graphics.pStages = Stages;
    Graphics.pVertexInputState = &VertexInput; Graphics.pInputAssemblyState = &Assembly; Graphics.pViewportState = &Viewport;
    Graphics.pRasterizationState = &Raster; Graphics.pMultisampleState = &Multisample; Graphics.pDepthStencilState = &DepthState;
    Graphics.pColorBlendState = &BlendState; Graphics.pDynamicState = &DynamicState;
    Graphics.layout = Vulkan->RasterPipelineLayout;
    Graphics.renderPass = Vulkan->RasterPassClear;
    VkResult R = vkCreateGraphicsPipelines(D, VK_NULL_HANDLE, 1u, &Graphics, nullptr, &Vulkan->RasterPipelineClear);
    Graphics.renderPass = Vulkan->RasterPassLoad;
    if (R == VK_SUCCESS) R = vkCreateGraphicsPipelines(D, VK_NULL_HANDLE, 1u, &Graphics, nullptr, &Vulkan->RasterPipelineLoad);
    vkDestroyShaderModule(D, Vertex, nullptr);
    vkDestroyShaderModule(D, Fragment, nullptr);
    if (R != VK_SUCCESS) { std::cerr << "[VisibilityExchange] visibility raster pipeline failed (VkResult " << static_cast<int>(R) << ").\n"; return false; }

    return BringDescriptorSets();
}

bool VisibilityExchange::BringDescriptorSets() noexcept
{
    VkDevice D = Vulkan->Device;
    constexpr uint32_t MaxHiZLevels = 16u;
    const uint32_t RasterSetCount = kMaximumCycleSlots * 2u;
    std::array<VkDescriptorPoolSize, 4u> Sizes{ { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 32u }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 128u + 2u * RasterSetCount },
                                                  { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64u + Vulkan->TextureSlotCapacity * RasterSetCount }, { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 64u } } };
    VkDescriptorPoolCreateInfo Pool{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    Pool.flags   = Vulkan->TextureSlotCapacity > 0u ? static_cast<VkDescriptorPoolCreateFlags>(VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT) : 0u;
    Pool.maxSets = kMaximumCycleSlots * 5u + MaxHiZLevels;
    Pool.poolSizeCount = static_cast<uint32_t>(Sizes.size()); Pool.pPoolSizes = Sizes.data();
    if (vkCreateDescriptorPool(D, &Pool, nullptr, &Vulkan->Pool) != VK_SUCCESS) return false;

    const auto Allocate = [&](VkDescriptorSetLayout Layout, VkDescriptorSet& Out)
    {
        VkDescriptorSetVariableDescriptorCountAllocateInfo Variable{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO };
        Variable.descriptorSetCount = 1u; Variable.pDescriptorCounts = &Vulkan->TextureSlotCapacity;
        VkDescriptorSetAllocateInfo Info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        Info.pNext = (Layout == Vulkan->RasterLayout && Vulkan->TextureSlotCapacity > 0u) ? &Variable : nullptr;
        Info.descriptorPool = Vulkan->Pool; Info.descriptorSetCount = 1u; Info.pSetLayouts = &Layout;
        return vkAllocateDescriptorSets(D, &Info, &Out) == VK_SUCCESS;
    };
    for (uint32_t S = 0u; S < Vulkan->SlotCount; ++S)
    {
        for (uint32_t P = 0u; P < 2u; ++P)
        {
            if (!Allocate(Vulkan->CullLayout,   Vulkan->CullSets[S][P]))   return false;
            if (!Allocate(Vulkan->RasterLayout, Vulkan->RasterSets[S][P])) return false;
        }
        if (!Allocate(Vulkan->ResolveLayout, Vulkan->ResolveSets[S])) return false;
    }
    Vulkan->HiZSets.resize(MaxHiZLevels);
    for (VkDescriptorSet& Set : Vulkan->HiZSets) if (!Allocate(Vulkan->HiZLayout, Set)) return false;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        RESIZE
//------------------------------------------------------------------------------------------------------------------------

bool VisibilityExchange::Resize(uint32_t NewWidth, uint32_t NewHeight, void* PresentationView) noexcept
{
    if (!Vulkan->Device) return false;
    vkDeviceWaitIdle(Vulkan->Device);
    RetireTargets();
    Width = std::max(1u, NewWidth); Height = std::max(1u, NewHeight);
    Vulkan->TargetExtent = { Width, Height };
    Vulkan->PresentationView = static_cast<VkImageView>(PresentationView);
    const VkPhysicalDeviceMemoryProperties& P = Vulkan->MemoryProperties;
    VkDevice D = Vulkan->Device;

    constexpr VkImageUsageFlags Sampled = VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!CreateImage(D, P, Vulkan->Depth,      VK_FORMAT_D32_SFLOAT,          Vulkan->TargetExtent, 1u, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | Sampled, VK_IMAGE_ASPECT_DEPTH_BIT, "depth")) return false;
    if (!CreateImage(D, P, Vulkan->Visibility, VK_FORMAT_R32_UINT,            Vulkan->TargetExtent, 1u, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | Sampled, VK_IMAGE_ASPECT_COLOR_BIT, "visibility")) return false;
    if (!CreateImage(D, P, Vulkan->Motion,     VK_FORMAT_R16G16_SFLOAT,       Vulkan->TargetExtent, 1u, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | Sampled, VK_IMAGE_ASPECT_COLOR_BIT, "motion")) return false;
    if (!CreateImage(D, P, Vulkan->HiZ,        VK_FORMAT_R32_SFLOAT,          Vulkan->TargetExtent, MipCount(Width, Height), VK_IMAGE_USAGE_STORAGE_BIT | Sampled | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT, "hiz")) return false;
    if (!CreateImage(D, P, Vulkan->Surface,    VK_FORMAT_R32G32B32A32_SFLOAT, Vulkan->TargetExtent, 1u, VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, "surface")) return false;
    if (!CreateImage(D, P, Vulkan->Normal,     VK_FORMAT_R16G16B16A16_SFLOAT, Vulkan->TargetExtent, 1u, VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, "normal")) return false;
    if (Vulkan->HiZ.Levels > Vulkan->HiZSets.size()) { std::cerr << "[VisibilityExchange] HiZ needs more than 16 levels.\n"; return false; }

    const VkImageView Attachments[3] = { Vulkan->Visibility.View, Vulkan->Motion.View, Vulkan->Depth.View };
    VkFramebufferCreateInfo Framebuffer{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    Framebuffer.renderPass = Vulkan->RasterPassClear;   // compatible with the Load variant (same formats / samples)
    Framebuffer.attachmentCount = 3u; Framebuffer.pAttachments = Attachments;
    Framebuffer.width = Width; Framebuffer.height = Height; Framebuffer.layers = 1u;
    if (vkCreateFramebuffer(D, &Framebuffer, nullptr, &Vulkan->Framebuffer) != VK_SUCCESS) return false;

    WriteDescriptorSets();
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      UPLOAD SCENE
//------------------------------------------------------------------------------------------------------------------------

void VisibilityExchange::UploadScene(const SceneStructure& Scene) noexcept
{
    if (!Vulkan->Device) return;
    vkDeviceWaitIdle(Vulkan->Device);
    VkDevice D = Vulkan->Device;
    const VkPhysicalDeviceMemoryProperties& P = Vulkan->MemoryProperties;
    constexpr VkBufferUsageFlags S = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    const auto Bytes = [](const auto& V) { return V.size() * sizeof(V[0]); };
    (void)UploadBuffer(D, P, Vulkan->Vertices,      Scene.QueryVertices().data(),      Bytes(Scene.QueryVertices()),      S, "vertices");
    (void)UploadBuffer(D, P, Vulkan->Indices,       Scene.QueryIndices().data(),       Bytes(Scene.QueryIndices()),       S | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, "indices");
    (void)UploadBuffer(D, P, Vulkan->Instances,     Scene.QueryInstances().data(),     Bytes(Scene.QueryInstances()),     S, "instances");
    (void)UploadBuffer(D, P, Vulkan->Clusters,      Scene.QueryClusters().data(),      Bytes(Scene.QueryClusters()),      S, "clusters");
    (void)UploadBuffer(D, P, Vulkan->Materials,     Scene.QueryMaterials().QueryRecords().data(), Bytes(Scene.QueryMaterials().QueryRecords()), S, "materials");   // R4a MaterialRecord[]
    (void)UploadBuffer(D, P, Vulkan->Luminaires,    Scene.QueryLuminaires().data(),    Bytes(Scene.QueryLuminaires()),    S, "luminaires");
    (void)UploadBuffer(D, P, Vulkan->FlatTriangles, Scene.QueryFlatTriangles().data(), Bytes(Scene.QueryFlatTriangles()), S, "flat triangles");

    TriangleCount  = Scene.QueryTriangleCount();
    ClusterCount   = static_cast<uint32_t>(Scene.QueryClusters().size());
    LuminaireCount = static_cast<uint32_t>(Scene.QueryLuminaires().size());

    const VkDeviceSize DrawBytes = static_cast<VkDeviceSize>(std::max(ClusterCount, 1u)) * 2u * sizeof(VkDrawIndexedIndirectCommand);
    const VkDeviceSize BitBytes  = static_cast<VkDeviceSize>((std::max(ClusterCount, 1u) + 31u) / 32u) * 4u;
    (void)CreateBuffer(D, P, Vulkan->Draws,        DrawBytes, S | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false, "draws");
    (void)CreateBuffer(D, P, Vulkan->VisibleBitsA, BitBytes,  S | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false, "visible bits A");
    (void)CreateBuffer(D, P, Vulkan->VisibleBitsB, BitBytes,  S | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false, "visible bits B");
    Vulkan->TargetsInitialised = false;   // forces the bit buffers to be cleared before first use
    PreviousValid = false;

    std::cerr << "[VisibilityExchange] Scene resident: " << TriangleCount << " triangles, " << Scene.QueryVertices().size() << " vertices, "
              << Scene.QueryInstances().size() << " instances, " << ClusterCount << " clusters, " << LuminaireCount << " luminaires.\n";
    WriteDescriptorSets();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   DESCRIPTOR WRITES
//------------------------------------------------------------------------------------------------------------------------

void VisibilityExchange::WriteDescriptorSets() noexcept
{
    if (!Vulkan->Pool || !Vulkan->Clusters.Buffer || !Vulkan->Depth.View) return;
    VkDevice D = Vulkan->Device;

    std::vector<VkWriteDescriptorSet>   Writes;
    std::vector<VkDescriptorBufferInfo> Buffers;  Buffers.reserve(256);
    std::vector<VkDescriptorImageInfo>  Images;   Images.reserve(128);
    const auto Buffer = [&](VkDescriptorSet Set, uint32_t Binding, VkDescriptorType Type, VkBuffer B)
    {
        Buffers.push_back({ B, 0u, VK_WHOLE_SIZE });
        VkWriteDescriptorSet W{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET }; W.dstSet = Set; W.dstBinding = Binding; W.descriptorCount = 1u; W.descriptorType = Type;
        // 1-based index: a 0-based index 0 would read back as nullptr and the patch loop below would skip it.
        W.pBufferInfo = reinterpret_cast<const VkDescriptorBufferInfo*>(Buffers.size());   // patched below (vector may grow)
        Writes.push_back(W);
    };
    const auto Image = [&](VkDescriptorSet Set, uint32_t Binding, VkDescriptorType Type, VkImageView V)
    {
        Images.push_back({ Type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ? Vulkan->PointSampler : VK_NULL_HANDLE, V, VK_IMAGE_LAYOUT_GENERAL });
        VkWriteDescriptorSet W{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET }; W.dstSet = Set; W.dstBinding = Binding; W.descriptorCount = 1u; W.descriptorType = Type;
        W.pImageInfo = reinterpret_cast<const VkDescriptorImageInfo*>(Images.size());   // 1-based, see Buffer above
        Writes.push_back(W);
    };
    constexpr VkDescriptorType UBO = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SSBO = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, IMG = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, TEX = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    for (uint32_t S = 0u; S < Vulkan->SlotCount; ++S)
    {
        for (uint32_t P = 0u; P < 2u; ++P)
        {
            VkDescriptorSet C = Vulkan->CullSets[S][P];
            Buffer(C, 0u, UBO,  Vulkan->FrameConstants[S][P].Buffer);
            Buffer(C, 1u, SSBO, Vulkan->Instances.Buffer);
            Buffer(C, 2u, SSBO, Vulkan->Clusters.Buffer);
            Buffer(C, 3u, SSBO, Vulkan->Draws.Buffer);
            Buffer(C, 4u, SSBO, Vulkan->Counters.Buffer);
            // bindings 5/6 (previous / current bits) are written per frame in RecordFrame (parity swaps)
            Image (C, 7u, TEX,  Vulkan->HiZ.View);

            VkDescriptorSet R = Vulkan->RasterSets[S][P];
            Buffer(R, 0u, UBO,  Vulkan->FrameConstants[S][P].Buffer);
            Buffer(R, 1u, SSBO, Vulkan->Instances.Buffer);
            Buffer(R, 2u, SSBO, Vulkan->Clusters.Buffer);
            Buffer(R, 3u, SSBO, Vulkan->Vertices.Buffer);
            if (Vulkan->TextureSlotCapacity > 0u)
            {
                Buffer(R, 5u, SSBO, Vulkan->Materials.Buffer);
                if (Vulkan->BorrowedSlabs) Buffer(R, 6u, SSBO, Vulkan->BorrowedSlabs);
            }
        }
        VkDescriptorSet X = Vulkan->ResolveSets[S];
        Buffer(X, 0u, UBO,  Vulkan->FrameConstants[S][1].Buffer);
        Buffer(X, 1u, SSBO, Vulkan->Instances.Buffer);
        Buffer(X, 2u, SSBO, Vulkan->Clusters.Buffer);
        Buffer(X, 3u, SSBO, Vulkan->Vertices.Buffer);
        Buffer(X, 4u, SSBO, Vulkan->Indices.Buffer);
        Buffer(X, 5u, SSBO, Vulkan->Materials.Buffer);
        Image (X, 6u, TEX,  Vulkan->Visibility.View);
        Image (X, 7u, TEX,  Vulkan->Motion.View);
        Image (X, 8u, TEX,  Vulkan->Depth.View);
        Image (X, 9u, TEX,  Vulkan->HiZ.View);
        Image (X, 10u, IMG, Vulkan->Surface.View);
        Image (X, 11u, IMG, Vulkan->Normal.View);
        if (Vulkan->PresentationView) Image(X, 12u, IMG, Vulkan->PresentationView);
    }
    // HiZ sets: set L writes level L; set 0 reads the depth attachment, set L>0 reads level L−1.
    for (uint32_t L = 0u; L < Vulkan->HiZ.Levels; ++L)
    {
        Image(Vulkan->HiZSets[L], 0u, TEX, L == 0u ? Vulkan->Depth.View : Vulkan->HiZ.LevelViews[L - 1u]);
        Image(Vulkan->HiZSets[L], 1u, IMG, Vulkan->HiZ.LevelViews[L]);
    }

    for (VkWriteDescriptorSet& W : Writes)
    {
        if (W.pBufferInfo) W.pBufferInfo = &Buffers[reinterpret_cast<size_t>(W.pBufferInfo) - 1u];
        if (W.pImageInfo)  W.pImageInfo  = &Images[reinterpret_cast<size_t>(W.pImageInfo) - 1u];
    }
    vkUpdateDescriptorSets(D, static_cast<uint32_t>(Writes.size()), Writes.data(), 0u, nullptr);

    // R4b: bindless table into every raster set (binding 7) — one write per set, partially bound past ViewCount.
    if (Vulkan->TextureSlotCapacity > 0u && Vulkan->BorrowedSampler && !Vulkan->BorrowedTextures.empty())
    {
        std::vector<VkDescriptorImageInfo> Table; Table.reserve(Vulkan->BorrowedTextures.size());
        for (VkImageView V : Vulkan->BorrowedTextures) Table.push_back({ Vulkan->BorrowedSampler, V, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
        for (uint32_t S = 0u; S < Vulkan->SlotCount; ++S)
            for (uint32_t P = 0u; P < 2u; ++P)
            {
                VkWriteDescriptorSet W{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                W.dstSet = Vulkan->RasterSets[S][P]; W.dstBinding = 7u; W.descriptorCount = static_cast<uint32_t>(Table.size());
                W.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; W.pImageInfo = Table.data();
                vkUpdateDescriptorSets(D, 1u, &W, 0u, nullptr);
            }
    }
}

void VisibilityExchange::AssignRasterMaterials(void* SlabBuffer, void* Sampler, const void* const* Views, uint32_t ViewCount) noexcept
{
    Vulkan->BorrowedSlabs   = static_cast<VkBuffer>(SlabBuffer);
    Vulkan->BorrowedSampler = static_cast<VkSampler>(Sampler);
    Vulkan->BorrowedTextures.clear();
    const uint32_t Count = std::min(ViewCount, Vulkan->TextureSlotCapacity);
    for (uint32_t I = 0u; I < Count; ++I) if (Views[I]) Vulkan->BorrowedTextures.push_back(static_cast<VkImageView>(const_cast<void*>(Views[I])));
    if (Vulkan->Device) vkDeviceWaitIdle(Vulkan->Device);
    WriteDescriptorSets();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    RESERVOIR VIEW (R6 row 3)
//------------------------------------------------------------------------------------------------------------------------

void VisibilityExchange::AssignReservoirView(void* PrevReservoirBuffer) noexcept
{
    Vulkan->BorrowedReservoir = static_cast<VkBuffer>(PrevReservoirBuffer);
    // No WriteDescriptorSets here: the resolve set's binding 13 is rewritten per frame in RecordFrame (parity swaps
    //    every frame), so a full rewrite on every assign would be redundant.
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FRAME CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

void VisibilityExchange::WriteFrameConstants(uint32_t Slot, uint32_t Phase, const VisibilityFrameConfiguration& Frame) noexcept
{
    FrameConstantRecord R{};
    const Matrix4x4 ViewClip = ConstructViewClipProjection(Frame.Camera);
    const Matrix4x4 Previous = PreviousValid ? PreviousViewClip : ViewClip;
    std::memcpy(R.ViewClip,         &ViewClip.Columns[0][0], sizeof(R.ViewClip));
    std::memcpy(R.PreviousViewClip, &Previous.Columns[0][0], sizeof(R.PreviousViewClip));
    ConstructFrustumPlanes(Frame.Camera, R.FrustumPlanes);
    const Vector3& O = Frame.Camera.Origin; const Vector3& F = Frame.Camera.Forward; const Vector3& Rt = Frame.Camera.Right; const Vector3& U = Frame.Camera.Up;
    R.CameraOrigin[0]  = O.x;  R.CameraOrigin[1]  = O.y;  R.CameraOrigin[2]  = O.z;  R.CameraOrigin[3]  = Frame.Camera.NearDistance;
    R.CameraForward[0] = F.x;  R.CameraForward[1] = F.y;  R.CameraForward[2] = F.z;  R.CameraForward[3] = Frame.Camera.TanHalfFieldOfView;
    R.CameraRight[0]   = Rt.x; R.CameraRight[1]   = Rt.y; R.CameraRight[2]   = Rt.z; R.CameraRight[3]   = Frame.Camera.AspectRatio;
    R.CameraUp[0]      = U.x;  R.CameraUp[1]      = U.y;  R.CameraUp[2]      = U.z;
    // Pixel jitter j ∈ [0,1) → clip offset: NDC spans 2 units over W pixels, pixel centre is 0.5; NDC y points down so no extra sign.
    R.Jitter[0] = (Frame.JitterX - 0.5f) * 2.0f / static_cast<float>(Width);
    R.Jitter[1] = (Frame.JitterY - 0.5f) * 2.0f / static_cast<float>(Height);
    R.Jitter[2] = Frame.JitterX; R.Jitter[3] = Frame.JitterY;
    R.Extent[0] = Frame.RenderWidth; R.Extent[1] = Frame.RenderHeight; R.Extent[2] = Vulkan->HiZ.Levels; R.Extent[3] = ClusterCount;
    R.Control[0] = Phase; R.Control[1] = Frame.FrameIndex; R.Control[2] = static_cast<uint32_t>(Frame.DebugView);
    R.Control[3] = (Frame.OcclusionCulling && PreviousValid ? 1u : 0u) | (Frame.ConeCulling ? 2u : 0u);
    R.Projection[0] = ViewClip.Columns[0][0]; R.Projection[1] = std::fabs(ViewClip.Columns[1][1]);
    if (Vulkan->FrameConstants[Slot][Phase - 1u].Mapped) std::memcpy(Vulkan->FrameConstants[Slot][Phase - 1u].Mapped, &R, sizeof(R));
    if (Phase == 2u) { PreviousViewClip = ViewClip; PreviousValid = true; }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      TELEMETRY
//------------------------------------------------------------------------------------------------------------------------

void VisibilityExchange::ReadTelemetry(uint32_t Slot) noexcept
{
    if (!Vulkan->SlotRecorded[Slot]) return;
    const uint32_t* Counters = static_cast<const uint32_t*>(Vulkan->CounterReadback[Slot].Mapped);
    if (Counters)
    {
        Telemetry.PhaseOneDraws   = Counters[0];
        Telemetry.ClusterTotal    = Counters[1];
        Telemetry.FrustumPassed   = Counters[2];
        Telemetry.ConePassed      = Counters[3];
        Telemetry.OcclusionPassed = Counters[4];
        Telemetry.TrianglesDrawn  = Counters[5];
        Telemetry.PhaseTwoDraws   = Counters[7];
    }
    uint64_t Stamps[kTimestampCount]{};
    if (vkGetQueryPoolResults(Vulkan->Device, Vulkan->Timestamps, Slot * kTimestampCount, kTimestampCount, sizeof(Stamps), Stamps, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
    {
        const auto Ms = [&](uint32_t A, uint32_t B) { return Stamps[B] > Stamps[A] ? static_cast<float>(static_cast<double>(Stamps[B] - Stamps[A]) * Vulkan->TimestampPeriod * 1e-6) : 0.0f; };
        Telemetry.CullMilliseconds    = Ms(0, 1) + Ms(6, 7);
        Telemetry.RasterMilliseconds  = Ms(2, 3) + Ms(8, 9);
        Telemetry.HiZMilliseconds     = Ms(4, 5);
        Telemetry.ResolveMilliseconds = Ms(9, 10);
        Telemetry.KernelMilliseconds  = Ms(10, 11);
    }
    Telemetry.Valid = true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      RECORD FRAME
//------------------------------------------------------------------------------------------------------------------------

void VisibilityExchange::RecordFrame(void* CommandHandle, uint32_t Slot, const VisibilityFrameConfiguration& Frame) noexcept
{
    if (!IsReady() || !Vulkan->Framebuffer) return;
    VkCommandBuffer Command = static_cast<VkCommandBuffer>(CommandHandle);
    ReadTelemetry(Slot);

    const uint32_t Q = Slot * kTimestampCount;
    vkCmdResetQueryPool(Command, Vulkan->Timestamps, Q, kTimestampCount);

    // Bit buffers: previous = the one phase 2 wrote last frame; current = the other. Both zeroed on the first frame.
    GpuBuffer& Previous = Vulkan->VisibleParity ? Vulkan->VisibleBitsB : Vulkan->VisibleBitsA;
    GpuBuffer& Current  = Vulkan->VisibleParity ? Vulkan->VisibleBitsA : Vulkan->VisibleBitsB;
    Vulkan->VisibleParity = !Vulkan->VisibleParity;
    {
        VkDescriptorBufferInfo Infos[4] = { { Previous.Buffer, 0u, VK_WHOLE_SIZE }, { Current.Buffer, 0u, VK_WHOLE_SIZE }, { Previous.Buffer, 0u, VK_WHOLE_SIZE }, { Current.Buffer, 0u, VK_WHOLE_SIZE } };
        VkWriteDescriptorSet W[4]{};
        for (uint32_t I = 0u; I < 4u; ++I)
        {
            W[I] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET }; W[I].dstSet = Vulkan->CullSets[Slot][I / 2u]; W[I].dstBinding = 5u + (I & 1u);
            W[I].descriptorCount = 1u; W[I].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; W[I].pBufferInfo = &Infos[I];
        }
        vkUpdateDescriptorSets(Vulkan->Device, 4u, W, 0u, nullptr);
    }

    constexpr VkPipelineStageFlags Compute = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, Transfer = VK_PIPELINE_STAGE_TRANSFER_BIT, Indirect = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;

    // Cross-submission ordering: last frame's phase-2 bit writes and counter copies must be visible to this frame.
    {
        VkMemoryBarrier Global{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        Global.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        Global.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(Command, Compute | Transfer, Compute | Transfer, 0u, 1u, &Global, 0u, nullptr, 0u, nullptr);
    }

    // ⓪ First use: zero both bit buffers, HiZ → GENERAL (all levels), storage targets → GENERAL.
    if (!Vulkan->TargetsInitialised)
    {
        vkCmdFillBuffer(Command, Previous.Buffer, 0u, VK_WHOLE_SIZE, 0u);
        vkCmdFillBuffer(Command, Current.Buffer,  0u, VK_WHOLE_SIZE, 0u);
        ImageBarrier(Command, Vulkan->HiZ.Image,     VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0u, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, Compute);
        ImageBarrier(Command, Vulkan->Surface.Image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0u, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, Compute);
        ImageBarrier(Command, Vulkan->Normal.Image,  VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0u, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, Compute);
        VkClearColorValue Zero{}; VkImageSubresourceRange All{ VK_IMAGE_ASPECT_COLOR_BIT, 0u, VK_REMAINING_MIP_LEVELS, 0u, 1u };
        vkCmdClearColorImage(Command, Vulkan->HiZ.Image, VK_IMAGE_LAYOUT_GENERAL, &Zero, 1u, &All);
        ImageBarrier(Command, Vulkan->HiZ.Image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, Transfer, Compute);
        Vulkan->TargetsInitialised = true;
    }

    // ① Reset counters and draw list.
    vkCmdFillBuffer(Command, Vulkan->Counters.Buffer, 0u, VK_WHOLE_SIZE, 0u);
    BufferBarrier(Command, Vulkan->Counters.Buffer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, Transfer, Compute);
    BufferBarrier(Command, Current.Buffer,          VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT, Transfer | Compute, Compute);

    const auto Cull = [&](uint32_t Phase)
    {
        WriteFrameConstants(Slot, Phase, Frame);
        vkCmdBindPipeline(Command, VK_PIPELINE_BIND_POINT_COMPUTE, Vulkan->CullPipeline);
        vkCmdBindDescriptorSets(Command, VK_PIPELINE_BIND_POINT_COMPUTE, Vulkan->CullPipelineLayout, 0u, 1u, &Vulkan->CullSets[Slot][Phase - 1u], 0u, nullptr);
        vkCmdDispatch(Command, (ClusterCount + 63u) / 64u, 1u, 1u);
        BufferBarrier(Command, Vulkan->Draws.Buffer,    VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT, Compute, Indirect);
        BufferBarrier(Command, Vulkan->Counters.Buffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, Compute, Indirect | Compute);
        BufferBarrier(Command, Current.Buffer,          VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, Compute, Compute);
    };

    const auto Raster = [&](uint32_t Phase)
    {
        VkClearValue Clears[3]{};
        Clears[0].color.uint32[0] = 0xFFFFFFFFu;          // visibility: invalid
        Clears[1].color.float32[0] = Clears[1].color.float32[1] = 0.0f;
        Clears[2].depthStencil = { 0.0f, 0u };            // reverse-Z: far
        VkRenderPassBeginInfo Begin{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        Begin.renderPass = Phase == 1u ? Vulkan->RasterPassClear : Vulkan->RasterPassLoad;
        Begin.framebuffer = Vulkan->Framebuffer;
        Begin.renderArea = { { 0, 0 }, { Frame.RenderWidth, Frame.RenderHeight } };
        Begin.clearValueCount = 3u; Begin.pClearValues = Clears;
        vkCmdBeginRenderPass(Command, &Begin, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport Viewport{ 0.0f, 0.0f, static_cast<float>(Frame.RenderWidth), static_cast<float>(Frame.RenderHeight), 0.0f, 1.0f };
        VkRect2D   Scissor{ { 0, 0 }, { Frame.RenderWidth, Frame.RenderHeight } };
        vkCmdSetViewport(Command, 0u, 1u, &Viewport);
        vkCmdSetScissor(Command, 0u, 1u, &Scissor);
        vkCmdBindPipeline(Command, VK_PIPELINE_BIND_POINT_GRAPHICS, Phase == 1u ? Vulkan->RasterPipelineClear : Vulkan->RasterPipelineLoad);
        vkCmdBindDescriptorSets(Command, VK_PIPELINE_BIND_POINT_GRAPHICS, Vulkan->RasterPipelineLayout, 0u, 1u, &Vulkan->RasterSets[Slot][Phase - 1u], 0u, nullptr);
        vkCmdBindIndexBuffer(Command, Vulkan->Indices.Buffer, 0u, VK_INDEX_TYPE_UINT32);
        const VkDeviceSize DrawOffset  = Phase == 1u ? 0u : static_cast<VkDeviceSize>(ClusterCount) * sizeof(VkDrawIndexedIndirectCommand);
        const VkDeviceSize CountOffset = Phase == 1u ? 0u : kCounterDrawPhaseTwoByte;
        if (Vulkan->DrawIndirectCount)
            vkCmdDrawIndexedIndirectCount(Command, Vulkan->Draws.Buffer, DrawOffset, Vulkan->Counters.Buffer, CountOffset, ClusterCount, sizeof(VkDrawIndexedIndirectCommand));
        else
            vkCmdDrawIndexedIndirect(Command, Vulkan->Draws.Buffer, DrawOffset, ClusterCount, sizeof(VkDrawIndexedIndirectCommand));   // unused slots must be zero → see fill below
        vkCmdEndRenderPass(Command);
    };

    if (!Vulkan->DrawIndirectCount)
    {
        vkCmdFillBuffer(Command, Vulkan->Draws.Buffer, 0u, VK_WHOLE_SIZE, 0u);
        BufferBarrier(Command, Vulkan->Draws.Buffer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, Transfer, Compute);
    }

    // ② Phase 1: last frame's visible clusters, no occlusion test.
    vkCmdWriteTimestamp(Command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, Vulkan->Timestamps, Q + 0u);
    Cull(1u);
    vkCmdWriteTimestamp(Command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, Vulkan->Timestamps, Q + 1u);
    vkCmdWriteTimestamp(Command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, Vulkan->Timestamps, Q + 2u);
    Raster(1u);
    vkCmdWriteTimestamp(Command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, Vulkan->Timestamps, Q + 3u);

    // ③ HiZ pyramid from the phase-1 depth.
    vkCmdWriteTimestamp(Command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, Vulkan->Timestamps, Q + 4u);
    vkCmdBindPipeline(Command, VK_PIPELINE_BIND_POINT_COMPUTE, Vulkan->HiZPipeline);
    for (uint32_t L = 0u; L < Vulkan->HiZ.Levels; ++L)
    {
        const uint32_t SourceW = L == 0u ? Frame.RenderWidth  : std::max(1u, Frame.RenderWidth  >> (L - 1u));
        const uint32_t SourceH = L == 0u ? Frame.RenderHeight : std::max(1u, Frame.RenderHeight >> (L - 1u));
        const uint32_t TargetW = std::max(1u, Frame.RenderWidth  >> L);
        const uint32_t TargetH = std::max(1u, Frame.RenderHeight >> L);
        HiZPushRecord Push{ { SourceW, SourceH }, { TargetW, TargetH }, L == 0u ? 1u : 0u };
        vkCmdPushConstants(Command, Vulkan->HiZPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(Push), &Push);
        vkCmdBindDescriptorSets(Command, VK_PIPELINE_BIND_POINT_COMPUTE, Vulkan->HiZPipelineLayout, 0u, 1u, &Vulkan->HiZSets[L], 0u, nullptr);
        vkCmdDispatch(Command, (TargetW + 7u) / 8u, (TargetH + 7u) / 8u, 1u);
        ImageBarrier(Command, Vulkan->HiZ.Image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, Compute, Compute, L, 1u);
    }
    vkCmdWriteTimestamp(Command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, Vulkan->Timestamps, Q + 5u);

    // ④ Phase 2: everything, HiZ-tested; only newly visible clusters are drawn (depth/visibility loaded, not cleared).
    vkCmdWriteTimestamp(Command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, Vulkan->Timestamps, Q + 6u);
    Cull(2u);
    vkCmdWriteTimestamp(Command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, Vulkan->Timestamps, Q + 7u);
    vkCmdWriteTimestamp(Command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, Vulkan->Timestamps, Q + 8u);
    Raster(2u);
    vkCmdWriteTimestamp(Command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, Vulkan->Timestamps, Q + 9u);

    // ⑤ Surface resolve (thin G-buffer; debug view straight to the presentation image).
    // R6 row 3: binding 13 tracks the kernel's prev-frame reservoir buffer, which swaps parity every frame — hence
    //    the per-frame rewrite here instead of once in WriteDescriptorSets. Skipped while null (no reservoir buffers
    //    yet): the M/W/Age views cannot be selected before the first kernel frame anyway, and an unbound binding
    //    is only UB if the shader actually reads it.
    if (Vulkan->BorrowedReservoir)
    {
        VkDescriptorBufferInfo ReservoirInfo{ Vulkan->BorrowedReservoir, 0u, VK_WHOLE_SIZE };
        VkWriteDescriptorSet ReservoirWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        ReservoirWrite.dstSet          = Vulkan->ResolveSets[Slot];
        ReservoirWrite.dstBinding      = 13u;
        ReservoirWrite.descriptorCount = 1u;
        ReservoirWrite.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ReservoirWrite.pBufferInfo     = &ReservoirInfo;
        vkUpdateDescriptorSets(Vulkan->Device, 1u, &ReservoirWrite, 0u, nullptr);
    }
    vkCmdBindPipeline(Command, VK_PIPELINE_BIND_POINT_COMPUTE, Vulkan->ResolvePipeline);
    vkCmdBindDescriptorSets(Command, VK_PIPELINE_BIND_POINT_COMPUTE, Vulkan->ResolvePipelineLayout, 0u, 1u, &Vulkan->ResolveSets[Slot], 0u, nullptr);
    vkCmdDispatch(Command, (Frame.RenderWidth + 15u) / 16u, (Frame.RenderHeight + 15u) / 16u, 1u);
    ImageBarrier(Command, Vulkan->Surface.Image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, Compute, Compute);
    ImageBarrier(Command, Vulkan->Normal.Image,  VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, Compute, Compute);
    vkCmdWriteTimestamp(Command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, Vulkan->Timestamps, Q + 10u);

    // ⑥ Counters → this slot's read-back buffer (read two frames later, after the fence).
    BufferBarrier(Command, Vulkan->Counters.Buffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, Compute, Transfer);
    VkBufferCopy Copy{ 0u, 0u, kCounterCount * 4u };
    vkCmdCopyBuffer(Command, Vulkan->Counters.Buffer, Vulkan->CounterReadback[Slot].Buffer, 1u, &Copy);
    Vulkan->SlotRecorded[Slot] = true;
}

void VisibilityExchange::RecordKernelBegin(void* Command, uint32_t Slot) noexcept
{
    if (!IsReady()) return;
    (void)Slot;   // the kernel start is timestamp 10 (end of resolve) — nothing else runs between the two
    (void)Command;
}

void VisibilityExchange::RecordKernelEnd(void* CommandHandle, uint32_t Slot) noexcept
{
    if (!IsReady() || !Vulkan->SlotRecorded[Slot]) return;
    vkCmdWriteTimestamp(static_cast<VkCommandBuffer>(CommandHandle), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, Vulkan->Timestamps, Slot * kTimestampCount + 11u);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       QUERIES
//------------------------------------------------------------------------------------------------------------------------

void* VisibilityExchange::QuerySurfaceView()        const noexcept { return Vulkan->Surface.View; }
void* VisibilityExchange::QueryNormalView()         const noexcept { return Vulkan->Normal.View; }
void* VisibilityExchange::QueryMotionView()         const noexcept { return Vulkan->Motion.View; }
void* VisibilityExchange::QueryLuminaireBuffer()    const noexcept { return Vulkan->Luminaires.Buffer; }
void* VisibilityExchange::QueryInstanceBuffer()     const noexcept { return Vulkan->Instances.Buffer; }
void* VisibilityExchange::QueryFlatTriangleBuffer() const noexcept { return Vulkan->FlatTriangles.Buffer; }
void* VisibilityExchange::QueryMaterialBuffer()     const noexcept { return Vulkan->Materials.Buffer; }
void* VisibilityExchange::QueryVertexBuffer()       const noexcept { return Vulkan->Vertices.Buffer; }
void* VisibilityExchange::QueryIndexBuffer()        const noexcept { return Vulkan->Indices.Buffer; }

} // namespace Frontier
