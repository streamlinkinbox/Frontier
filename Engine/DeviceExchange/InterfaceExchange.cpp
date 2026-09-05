//============================================================================================================================================
//                                                     INTERFACEEXCHANGE.CPP
//============================================================================================================================================
// 🧩 One graphics state object, one uniform block per cycle slot, one host-visible instance extent per cycle slot,
//    and one draw. Modelled directly on VisibilityExchange's raster setup so there is a single Vulkan idiom in the
//    engine rather than two competing ones.

#include <vulkan/vulkan.h>
#include "InterfaceExchange.h"
#include "../SpatialInterface/InterfaceLayoutCodec.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                      CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

static constexpr uint32_t kMaximumCycleSlots = 3u;

// std140 mirror of InterfaceConstants in Shaders/InterfaceRecords.slang.
struct alignas(16) InterfaceConstantRecord
{
    float ViewClip[16];
    float CameraOrigin[4];
    float Extent[4];
};
static_assert(sizeof(InterfaceConstantRecord) == 96u, "InterfaceConstantRecord must match the std140 block");

//------------------------------------------------------------------------------------------------------------------------
//                                                    VULKAN RECORD
//------------------------------------------------------------------------------------------------------------------------

namespace {

struct InterfaceBuffer
{
    VkBuffer       Buffer = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkDeviceSize   Bytes  = 0u;
    void*          Mapped = nullptr;
};

// Asset paths are resolved by walking up from the working directory, the same way VisibilityExchange does, so the
//    executable runs from either the repository root or its own output folder.
std::filesystem::path ResolveInterfaceAssetPath(const char* Relative)
{
    std::filesystem::path Probe = std::filesystem::current_path();
    for (int Depth = 0; Depth < 6; ++Depth)
    {
        std::filesystem::path Candidate = Probe / Relative;
        std::error_code Error;
        if (std::filesystem::exists(Candidate, Error)) return Candidate;
        std::filesystem::path Parent = Probe.parent_path();
        if (Parent == Probe) break;
        Probe = Parent;
    }
    return std::filesystem::path(Relative);
}

VkShaderModule LoadInterfaceShader(VkDevice Device, const char* Relative)
{
    const std::filesystem::path Path = ResolveInterfaceAssetPath(Relative);
    std::ifstream File(Path, std::ios::binary | std::ios::ate);
    if (!File.is_open()) { std::cerr << "[InterfaceExchange] Cannot open SPIR-V: " << Relative << "\n"; return VK_NULL_HANDLE; }

    const std::streamsize Bytes = File.tellg();
    if (Bytes < 4 || Bytes % 4) { std::cerr << "[InterfaceExchange] Malformed SPIR-V: " << Relative << "\n"; return VK_NULL_HANDLE; }

    std::vector<uint32_t> Code(static_cast<size_t>(Bytes) / 4u);
    File.seekg(0);
    File.read(reinterpret_cast<char*>(Code.data()), Bytes);

    VkShaderModuleCreateInfo Info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    Info.codeSize = Code.size() * 4u;
    Info.pCode    = Code.data();

    VkShaderModule Module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(Device, &Info, nullptr, &Module) != VK_SUCCESS)
    {
        std::cerr << "[InterfaceExchange] vkCreateShaderModule failed: " << Relative << "\n";
        return VK_NULL_HANDLE;
    }
    std::cerr << "[InterfaceExchange] Loaded SPIR-V: " << Path.string() << "\n";
    return Module;
}

uint32_t FindInterfaceMemoryType(const VkPhysicalDeviceMemoryProperties& Properties, uint32_t Mask, VkMemoryPropertyFlags Flags)
{
    for (uint32_t I = 0u; I < Properties.memoryTypeCount; ++I)
        if ((Mask & (1u << I)) && (Properties.memoryTypes[I].propertyFlags & Flags) == Flags) return I;
    return 0u;
}

void DestroyInterfaceBuffer(VkDevice Device, InterfaceBuffer& Target)
{
    if (Target.Mapped) vkUnmapMemory(Device, Target.Memory);
    if (Target.Buffer) vkDestroyBuffer(Device, Target.Buffer, nullptr);
    if (Target.Memory) vkFreeMemory(Device, Target.Memory, nullptr);
    Target = {};
}

bool CreateInterfaceBuffer(VkDevice Device, const VkPhysicalDeviceMemoryProperties& Properties,
                           InterfaceBuffer& Target, VkDeviceSize Bytes, VkBufferUsageFlags Usage, const char* Label)
{
    DestroyInterfaceBuffer(Device, Target);
    Target.Bytes = std::max<VkDeviceSize>(Bytes, 16u);

    VkBufferCreateInfo Info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    Info.size  = Target.Bytes;
    Info.usage = Usage;
    if (vkCreateBuffer(Device, &Info, nullptr, &Target.Buffer) != VK_SUCCESS)
    {
        std::cerr << "[InterfaceExchange] vkCreateBuffer (" << Label << ") failed.\n";
        return false;
    }

    VkMemoryRequirements Requirements{};
    vkGetBufferMemoryRequirements(Device, Target.Buffer, &Requirements);

    VkMemoryAllocateInfo Allocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    Allocate.allocationSize  = Requirements.size;
    Allocate.memoryTypeIndex = FindInterfaceMemoryType(Properties, Requirements.memoryTypeBits,
                                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(Device, &Allocate, nullptr, &Target.Memory) != VK_SUCCESS)
    {
        std::cerr << "[InterfaceExchange] vkAllocateMemory (" << Label << ") failed.\n";
        return false;
    }

    vkBindBufferMemory(Device, Target.Buffer, Target.Memory, 0u);
    if (vkMapMemory(Device, Target.Memory, 0u, Target.Bytes, 0u, &Target.Mapped) != VK_SUCCESS) Target.Mapped = nullptr;
    return true;
}

} // namespace

struct InterfaceExchange::VulkanRecord
{
    VkDevice                           Device         = VK_NULL_HANDLE;
    VkPhysicalDevice                   Physical       = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties   MemoryProperties{};

    VkFormat                           ColourFormat   = VK_FORMAT_UNDEFINED;
    VkFormat                           DepthFormat    = VK_FORMAT_UNDEFINED;

    VkRenderPass                       RenderPass     = VK_NULL_HANDLE;
    VkFramebuffer                      Framebuffer    = VK_NULL_HANDLE;
    VkImageView                        ColourView     = VK_NULL_HANDLE;
    VkImageView                        DepthView      = VK_NULL_HANDLE;

    VkDescriptorSetLayout              SetLayout      = VK_NULL_HANDLE;
    VkPipelineLayout                   PipelineLayout = VK_NULL_HANDLE;
    VkPipeline                         Pipeline       = VK_NULL_HANDLE;
    VkDescriptorPool                   DescriptorPool = VK_NULL_HANDLE;

    uint32_t                           CycleSlotCount = 0u;
    std::array<InterfaceBuffer, kMaximumCycleSlots> Constants{};
    std::array<InterfaceBuffer, kMaximumCycleSlots> Instances{};
    std::array<VkDescriptorSet, kMaximumCycleSlots> Sets{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

InterfaceExchange::InterfaceExchange() noexcept
    : Vulkan(new VulkanRecord())
{
}

InterfaceExchange::~InterfaceExchange() noexcept
{
    Retire();
    delete Vulkan;
    Vulkan = nullptr;
}

bool InterfaceExchange::Bring(void* Device, void* PhysicalDevice, uint32_t CycleSlotCount,
                              uint32_t ColourFormat, uint32_t DepthFormat, uint32_t FigureCapacity) noexcept
{
    if (Device == nullptr || PhysicalDevice == nullptr) return false;

    Vulkan->Device         = static_cast<VkDevice>(Device);
    Vulkan->Physical       = static_cast<VkPhysicalDevice>(PhysicalDevice);
    Vulkan->CycleSlotCount = std::clamp(CycleSlotCount, 1u, kMaximumCycleSlots);
    Vulkan->ColourFormat   = static_cast<VkFormat>(ColourFormat);
    Vulkan->DepthFormat    = static_cast<VkFormat>(DepthFormat);

    vkGetPhysicalDeviceMemoryProperties(Vulkan->Physical, &Vulkan->MemoryProperties);

    Capacity    = std::max(FigureCapacity, 1u);
    DepthTested = Vulkan->DepthFormat != VK_FORMAT_UNDEFINED;

    // One constants block and one instance extent per cycle slot: the CPU writes slot N while the GPU reads N−1.
    for (uint32_t Slot = 0u; Slot < Vulkan->CycleSlotCount; ++Slot)
    {
        if (!CreateInterfaceBuffer(Vulkan->Device, Vulkan->MemoryProperties, Vulkan->Constants[Slot],
                                   sizeof(InterfaceConstantRecord), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, "constants"))
            return false;

        if (!CreateInterfaceBuffer(Vulkan->Device, Vulkan->MemoryProperties, Vulkan->Instances[Slot],
                                   static_cast<VkDeviceSize>(Capacity) * sizeof(InterfaceInstanceFigure),
                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "instances"))
            return false;
    }

    if (!BringPipeline())       return false;
    if (!BringDescriptorSets()) return false;

    Ready = true;
    std::cerr << "[InterfaceExchange] Ready: capacity " << Capacity << " figures, "
              << Vulkan->CycleSlotCount << " cycle slots, depth test " << (DepthTested ? "on" : "off") << ".\n";
    return true;
}

void InterfaceExchange::Retire() noexcept
{
    if (Vulkan == nullptr || Vulkan->Device == VK_NULL_HANDLE) return;
    VkDevice D = Vulkan->Device;

    vkDeviceWaitIdle(D);

    if (Vulkan->Framebuffer)    vkDestroyFramebuffer(D, Vulkan->Framebuffer, nullptr);
    if (Vulkan->Pipeline)       vkDestroyPipeline(D, Vulkan->Pipeline, nullptr);
    if (Vulkan->PipelineLayout) vkDestroyPipelineLayout(D, Vulkan->PipelineLayout, nullptr);
    if (Vulkan->SetLayout)      vkDestroyDescriptorSetLayout(D, Vulkan->SetLayout, nullptr);
    if (Vulkan->DescriptorPool) vkDestroyDescriptorPool(D, Vulkan->DescriptorPool, nullptr);
    if (Vulkan->RenderPass)     vkDestroyRenderPass(D, Vulkan->RenderPass, nullptr);

    for (uint32_t Slot = 0u; Slot < kMaximumCycleSlots; ++Slot)
    {
        DestroyInterfaceBuffer(D, Vulkan->Constants[Slot]);
        DestroyInterfaceBuffer(D, Vulkan->Instances[Slot]);
    }

    Vulkan->Framebuffer = VK_NULL_HANDLE; Vulkan->Pipeline = VK_NULL_HANDLE;
    Vulkan->PipelineLayout = VK_NULL_HANDLE; Vulkan->SetLayout = VK_NULL_HANDLE;
    Vulkan->DescriptorPool = VK_NULL_HANDLE; Vulkan->RenderPass = VK_NULL_HANDLE;
    Vulkan->Device = VK_NULL_HANDLE;

    Ready = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      PIPELINE
//------------------------------------------------------------------------------------------------------------------------

bool InterfaceExchange::BringPipeline() noexcept
{
    VkDevice D = Vulkan->Device;

    // ── Render pass: load the resolved scene colour, keep it, never clear. Depth is loaded read-only when present.
    {
        std::array<VkAttachmentDescription, 2u> Attachments{};
        uint32_t AttachmentCount = 1u;

        Attachments[0].format         = Vulkan->ColourFormat;
        Attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
        Attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;      // composite over the lit image
        Attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        Attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        Attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        Attachments[0].initialLayout  = VK_IMAGE_LAYOUT_GENERAL;
        Attachments[0].finalLayout    = VK_IMAGE_LAYOUT_GENERAL;

        if (DepthTested)
        {
            Attachments[1].format         = Vulkan->DepthFormat;
            Attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
            Attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
            Attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            Attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            Attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            Attachments[1].initialLayout  = VK_IMAGE_LAYOUT_GENERAL;
            Attachments[1].finalLayout    = VK_IMAGE_LAYOUT_GENERAL;
            AttachmentCount = 2u;
        }

        VkAttachmentReference Colour{ 0u, VK_IMAGE_LAYOUT_GENERAL };
        VkAttachmentReference Depth { 1u, VK_IMAGE_LAYOUT_GENERAL };

        VkSubpassDescription Subpass{};
        Subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        Subpass.colorAttachmentCount    = 1u;
        Subpass.pColorAttachments       = &Colour;
        Subpass.pDepthStencilAttachment = DepthTested ? &Depth : nullptr;

        // The kernel writes the colour image with compute before this runs, and the presentation blit reads it after.
        VkSubpassDependency Dependencies[2]{};
        Dependencies[0] = { VK_SUBPASS_EXTERNAL, 0u,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                            VK_ACCESS_SHADER_WRITE_BIT,
                            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                            0u };
        Dependencies[1] = { 0u, VK_SUBPASS_EXTERNAL,
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
                            0u };

        VkRenderPassCreateInfo Info{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        Info.attachmentCount = AttachmentCount;
        Info.pAttachments    = Attachments.data();
        Info.subpassCount    = 1u;
        Info.pSubpasses      = &Subpass;
        Info.dependencyCount = 2u;
        Info.pDependencies   = Dependencies;

        if (vkCreateRenderPass(D, &Info, nullptr, &Vulkan->RenderPass) != VK_SUCCESS)
        {
            std::cerr << "[InterfaceExchange] vkCreateRenderPass failed.\n";
            return false;
        }
    }

    // ── Descriptor set layout: binding 0 = view constants, binding 1 = the instance extent.
    {
        std::array<VkDescriptorSetLayoutBinding, 2u> Bindings{};
        Bindings[0] = { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
        Bindings[1] = { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };

        VkDescriptorSetLayoutCreateInfo Info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        Info.bindingCount = static_cast<uint32_t>(Bindings.size());
        Info.pBindings    = Bindings.data();
        if (vkCreateDescriptorSetLayout(D, &Info, nullptr, &Vulkan->SetLayout) != VK_SUCCESS) return false;

        VkPipelineLayoutCreateInfo LayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        LayoutInfo.setLayoutCount = 1u;
        LayoutInfo.pSetLayouts    = &Vulkan->SetLayout;
        if (vkCreatePipelineLayout(D, &LayoutInfo, nullptr, &Vulkan->PipelineLayout) != VK_SUCCESS) return false;
    }

    // ── Graphics state. No vertex input at all: the quad corner comes from gl_VertexIndex.
    VkShaderModule Vertex   = LoadInterfaceShader(D, "Engine/Shaders/InterfaceRaster.vert.spv");
    VkShaderModule Fragment = LoadInterfaceShader(D, "Engine/Shaders/InterfaceRaster.frag.spv");
    if (!Vertex || !Fragment) return false;

    VkPipelineShaderStageCreateInfo Stages[2] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0u, VK_SHADER_STAGE_VERTEX_BIT,   Vertex,   "main", nullptr },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0u, VK_SHADER_STAGE_FRAGMENT_BIT, Fragment, "main", nullptr } };

    VkPipelineVertexInputStateCreateInfo VertexInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

    VkPipelineInputAssemblyStateCreateInfo Assembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    Assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    VkPipelineViewportStateCreateInfo Viewport{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    Viewport.viewportCount = 1u;
    Viewport.scissorCount  = 1u;

    VkPipelineRasterizationStateCreateInfo Raster{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    Raster.polygonMode = VK_POLYGON_MODE_FILL;
    Raster.cullMode    = VK_CULL_MODE_NONE;          // a panel is legible from behind; the sort already ordered it
    Raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    Raster.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo Multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Reverse-Z GREATER to match the visibility raster. Depth WRITE is off: the CPU sort owns interface ordering, and
    //    writing depth would make a transparent panel occlude the one behind it.
    VkPipelineDepthStencilStateCreateInfo DepthState{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    DepthState.depthTestEnable  = DepthTested ? VK_TRUE : VK_FALSE;
    DepthState.depthWriteEnable = VK_FALSE;
    DepthState.depthCompareOp   = VK_COMPARE_OP_GREATER;

    // Premultiplied alpha over the resolved scene.
    VkPipelineColorBlendAttachmentState Blend{};
    Blend.blendEnable         = VK_TRUE;
    Blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    Blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    Blend.colorBlendOp        = VK_BLEND_OP_ADD;
    Blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    Blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    Blend.alphaBlendOp        = VK_BLEND_OP_ADD;
    Blend.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                              | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo BlendState{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    BlendState.attachmentCount = 1u;
    BlendState.pAttachments    = &Blend;

    // Scissor is dynamic so P2's hardware-scissor clipping tier costs no new state object.
    const VkDynamicState Dynamic[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo DynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    DynamicState.dynamicStateCount = 2u;
    DynamicState.pDynamicStates    = Dynamic;

    VkGraphicsPipelineCreateInfo Graphics{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    Graphics.stageCount          = 2u;
    Graphics.pStages             = Stages;
    Graphics.pVertexInputState   = &VertexInput;
    Graphics.pInputAssemblyState = &Assembly;
    Graphics.pViewportState      = &Viewport;
    Graphics.pRasterizationState = &Raster;
    Graphics.pMultisampleState   = &Multisample;
    Graphics.pDepthStencilState  = &DepthState;
    Graphics.pColorBlendState    = &BlendState;
    Graphics.pDynamicState       = &DynamicState;
    Graphics.layout              = Vulkan->PipelineLayout;
    Graphics.renderPass          = Vulkan->RenderPass;

    const VkResult Result = vkCreateGraphicsPipelines(D, VK_NULL_HANDLE, 1u, &Graphics, nullptr, &Vulkan->Pipeline);

    vkDestroyShaderModule(D, Vertex, nullptr);
    vkDestroyShaderModule(D, Fragment, nullptr);

    if (Result != VK_SUCCESS)
    {
        std::cerr << "[InterfaceExchange] graphics pipeline failed (VkResult " << static_cast<int>(Result) << ").\n";
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  DESCRIPTOR SETS
//------------------------------------------------------------------------------------------------------------------------

bool InterfaceExchange::BringDescriptorSets() noexcept
{
    VkDevice D = Vulkan->Device;

    std::array<VkDescriptorPoolSize, 2u> Sizes{ { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaximumCycleSlots },
                                                  { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kMaximumCycleSlots } } };

    VkDescriptorPoolCreateInfo Pool{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    Pool.maxSets       = kMaximumCycleSlots;
    Pool.poolSizeCount = static_cast<uint32_t>(Sizes.size());
    Pool.pPoolSizes    = Sizes.data();
    if (vkCreateDescriptorPool(D, &Pool, nullptr, &Vulkan->DescriptorPool) != VK_SUCCESS) return false;

    for (uint32_t Slot = 0u; Slot < Vulkan->CycleSlotCount; ++Slot)
    {
        VkDescriptorSetAllocateInfo Allocate{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        Allocate.descriptorPool     = Vulkan->DescriptorPool;
        Allocate.descriptorSetCount = 1u;
        Allocate.pSetLayouts        = &Vulkan->SetLayout;
        if (vkAllocateDescriptorSets(D, &Allocate, &Vulkan->Sets[Slot]) != VK_SUCCESS) return false;
    }

    WriteDescriptorSets();
    return true;
}

void InterfaceExchange::WriteDescriptorSets() noexcept
{
    VkDevice D = Vulkan->Device;

    for (uint32_t Slot = 0u; Slot < Vulkan->CycleSlotCount; ++Slot)
    {
        VkDescriptorBufferInfo ConstantInfo{ Vulkan->Constants[Slot].Buffer, 0u, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo InstanceInfo{ Vulkan->Instances[Slot].Buffer, 0u, VK_WHOLE_SIZE };

        std::array<VkWriteDescriptorSet, 2u> Writes{};
        Writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, Vulkan->Sets[Slot], 0u, 0u, 1u,
                      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &ConstantInfo, nullptr };
        Writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, Vulkan->Sets[Slot], 1u, 0u, 1u,
                      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &InstanceInfo, nullptr };

        vkUpdateDescriptorSets(D, static_cast<uint32_t>(Writes.size()), Writes.data(), 0u, nullptr);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       RESIZE
//------------------------------------------------------------------------------------------------------------------------

bool InterfaceExchange::Resize(uint32_t NewWidth, uint32_t NewHeight, void* ColourView, void* DepthView) noexcept
{
    if (Vulkan->Device == VK_NULL_HANDLE || ColourView == nullptr) return false;
    if (NewWidth == 0u || NewHeight == 0u) return false;

    VkDevice D = Vulkan->Device;

    Width      = NewWidth;
    Height     = NewHeight;
    Vulkan->ColourView = static_cast<VkImageView>(ColourView);
    Vulkan->DepthView  = static_cast<VkImageView>(DepthView);

    if (Vulkan->Framebuffer) { vkDestroyFramebuffer(D, Vulkan->Framebuffer, nullptr); Vulkan->Framebuffer = VK_NULL_HANDLE; }

    std::array<VkImageView, 2u> Views{ Vulkan->ColourView, Vulkan->DepthView };
    const uint32_t ViewCount = (DepthTested && Vulkan->DepthView != VK_NULL_HANDLE) ? 2u : 1u;

    VkFramebufferCreateInfo Info{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    Info.renderPass      = Vulkan->RenderPass;
    Info.attachmentCount = ViewCount;
    Info.pAttachments    = Views.data();
    Info.width           = Width;
    Info.height          = Height;
    Info.layers          = 1u;

    if (vkCreateFramebuffer(D, &Info, nullptr, &Vulkan->Framebuffer) != VK_SUCCESS)
    {
        std::cerr << "[InterfaceExchange] vkCreateFramebuffer failed.\n";
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  INSTANCE UPLOAD
//------------------------------------------------------------------------------------------------------------------------

void InterfaceExchange::UploadInstances(const InterfaceInstanceFigure* Instances, uint32_t Count, uint32_t CycleSlot) noexcept
{
    PendingCount = 0u;
    if (!Ready || Instances == nullptr || Count == 0u) return;

    const uint32_t Slot   = CycleSlot % Vulkan->CycleSlotCount;
    const uint32_t Actual = std::min(Count, Capacity);

    InterfaceBuffer& Target = Vulkan->Instances[Slot];
    if (Target.Mapped == nullptr) return;

    const size_t Bytes = static_cast<size_t>(Actual) * sizeof(InterfaceInstanceFigure);
    std::memcpy(Target.Mapped, Instances, Bytes);

    PendingCount        = Actual;
    Metrics.UploadBytes = static_cast<uint32_t>(Bytes);

    if (Count > Capacity)
        std::cerr << "[InterfaceExchange] " << Count << " figures exceeds the capacity of " << Capacity << "; the tail was dropped.\n";
}

void InterfaceExchange::WriteViewConstants(uint32_t CycleSlot, const InterfaceViewClip& View) noexcept
{
    const uint32_t Slot = CycleSlot % Vulkan->CycleSlotCount;
    InterfaceBuffer& Target = Vulkan->Constants[Slot];
    if (Target.Mapped == nullptr) return;

    InterfaceConstantRecord Record{};
    std::memcpy(Record.ViewClip, View.ViewClip, sizeof(Record.ViewClip));
    Record.CameraOrigin[0] = View.EyeX;
    Record.CameraOrigin[1] = View.EyeY;
    Record.CameraOrigin[2] = View.EyeZ;
    Record.CameraOrigin[3] = 0.0f;
    Record.Extent[0] = static_cast<float>(View.RenderWidth);
    Record.Extent[1] = static_cast<float>(View.RenderHeight);

    std::memcpy(Target.Mapped, &Record, sizeof(Record));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE ONE DRAW
//------------------------------------------------------------------------------------------------------------------------

void InterfaceExchange::RecordInterface(void* Command, uint32_t CycleSlot, const InterfaceViewClip& View) noexcept
{
    Metrics.InstanceCount = PendingCount;
    Metrics.DrawCount     = 0u;
    Metrics.Valid         = false;

    if (!Ready || Command == nullptr || PendingCount == 0u) return;
    if (Vulkan->Framebuffer == VK_NULL_HANDLE) return;

    const uint32_t     Slot   = CycleSlot % Vulkan->CycleSlotCount;
    VkCommandBuffer    Buffer = static_cast<VkCommandBuffer>(Command);

    WriteViewConstants(Slot, View);

    VkRenderPassBeginInfo Begin{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    Begin.renderPass        = Vulkan->RenderPass;
    Begin.framebuffer       = Vulkan->Framebuffer;
    Begin.renderArea.extent = { Width, Height };

    vkCmdBeginRenderPass(Buffer, &Begin, VK_SUBPASS_CONTENTS_INLINE);

    const VkViewport Viewport{ 0.0f, 0.0f, static_cast<float>(View.RenderWidth), static_cast<float>(View.RenderHeight), 0.0f, 1.0f };
    const VkRect2D   Scissor { { 0, 0 }, { View.RenderWidth, View.RenderHeight } };
    vkCmdSetViewport(Buffer, 0u, 1u, &Viewport);
    vkCmdSetScissor (Buffer, 0u, 1u, &Scissor);

    vkCmdBindPipeline(Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Vulkan->Pipeline);
    vkCmdBindDescriptorSets(Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Vulkan->PipelineLayout, 0u, 1u, &Vulkan->Sets[Slot], 0u, nullptr);

    // The entire spatial interface, whatever it contains, in one call.
    vkCmdDraw(Buffer, 4u, PendingCount, 0u, 0u);

    vkCmdEndRenderPass(Buffer);

    Metrics.DrawCount = 1u;
    Metrics.Valid     = true;
}

} // namespace Frontier
