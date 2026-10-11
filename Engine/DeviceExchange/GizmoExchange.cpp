//============================================================================================================================================
//                                                      GIZMOEXCHANGE.CPP
//============================================================================================================================================
// 🧩 Two pipelines (triangle list + line list) over one pulled-vertex SSBO per cycle slot, one render pass that
//    loads the resolved scene and composites the gizmo over it. Modelled on InterfaceExchange line for line so
//    there is a single overlay idiom in the engine; the only structural differences are the second topology and
//    push constants instead of a uniform extent (the view is 96 bytes — well under every minimum).

#include <vulkan/vulkan.h>
#include "GizmoExchange.h"
#include "TelemetryProbe.h"   // dev/debug-only shader-load timing; compiles out of ship builds

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

static constexpr uint32_t kGizmoMaximumCycleSlots = 3u;

// Push mirror of GizmoConstants in Shaders/GizmoRaster.*.slang.
struct alignas(16) GizmoConstantRecord
{
    float ViewClip[16];
    float CameraOrigin[4];
    float LightToward[4];
};
static_assert(sizeof(GizmoConstantRecord) == 96u, "GizmoConstantRecord must match the shader's push block");

//------------------------------------------------------------------------------------------------------------------------
//                                                    VULKAN RECORD
//------------------------------------------------------------------------------------------------------------------------

namespace {

struct GizmoDeviceExtent
{
    VkBuffer       Extent = VK_NULL_HANDLE;
    VkDeviceMemory Reserve = VK_NULL_HANDLE;
    VkDeviceSize   Bytes  = 0u;
    void*          Mapped = nullptr;
};

std::filesystem::path ResolveGizmoAssetPath(const char* Relative)
{
    std::filesystem::path Walk = std::filesystem::current_path();
    for (int Depth = 0; Depth < 6; ++Depth)
    {
        std::filesystem::path Candidate = Walk / Relative;
        std::error_code Error;
        if (std::filesystem::exists(Candidate, Error)) return Candidate;
        std::filesystem::path Above = Walk.parent_path();
        if (Above == Walk) break;
        Walk = Above;
    }
    return std::filesystem::path(Relative);
}

VkShaderModule LoadGizmoShader(VkDevice Device, const char* Relative)
{
    FRONTIER_PROBE_SHADER_SCOPE(Relative);   // dev/debug only
    const std::filesystem::path Path = ResolveGizmoAssetPath(Relative);
    std::ifstream File(Path, std::ios::binary | std::ios::ate);
    if (!File.is_open()) { std::cerr << "[GizmoExchange] Cannot open SPIR-V: " << Relative << "\n"; return VK_NULL_HANDLE; }

    const std::streamsize Bytes = File.tellg();
    if (Bytes < 4 || Bytes % 4) { std::cerr << "[GizmoExchange] Malformed SPIR-V: " << Relative << "\n"; return VK_NULL_HANDLE; }

    std::vector<uint32_t> Code(static_cast<size_t>(Bytes) / 4u);
    File.seekg(0);
    File.read(reinterpret_cast<char*>(Code.data()), Bytes);

    VkShaderModuleCreateInfo Making{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    Making.codeSize = Code.size() * 4u;
    Making.pCode    = Code.data();

    VkShaderModule Piece = VK_NULL_HANDLE;
    if (vkCreateShaderModule(Device, &Making, nullptr, &Piece) != VK_SUCCESS)
    {
        std::cerr << "[GizmoExchange] vkCreateShaderModule failed: " << Relative << "\n";
        return VK_NULL_HANDLE;
    }
    std::cerr << "[GizmoExchange] Loaded SPIR-V: " << Path.string() << "\n";
    return Piece;
}

uint32_t FindGizmoDeviceReserve(const VkPhysicalDeviceMemoryProperties& Properties, uint32_t Mask, VkMemoryPropertyFlags Wanted)
{
    for (uint32_t I = 0u; I < Properties.memoryTypeCount; ++I)
        if ((Mask & (1u << I)) && (Properties.memoryTypes[I].propertyFlags & Wanted) == Wanted) return I;
    return 0u;
}

void RetireGizmoExtent(VkDevice Device, GizmoDeviceExtent& Retired)
{
    if (Retired.Mapped) vkUnmapMemory(Device, Retired.Reserve);
    if (Retired.Extent) vkDestroyBuffer(Device, Retired.Extent, nullptr);
    if (Retired.Reserve) vkFreeMemory(Device, Retired.Reserve, nullptr);
    Retired = {};
}

bool BringGizmoExtent(VkDevice Device, const VkPhysicalDeviceMemoryProperties& Properties,
                      GizmoDeviceExtent& Brought, VkDeviceSize Bytes, const char* Label)
{
    RetireGizmoExtent(Device, Brought);
    Brought.Bytes = std::max<VkDeviceSize>(Bytes, 16u);

    VkBufferCreateInfo Making{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    Making.size  = Brought.Bytes;
    Making.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (vkCreateBuffer(Device, &Making, nullptr, &Brought.Extent) != VK_SUCCESS)
    {
        std::cerr << "[GizmoExchange] vkCreateBuffer (" << Label << ") failed.\n";
        return false;
    }

    VkMemoryRequirements Needs{};
    vkGetBufferMemoryRequirements(Device, Brought.Extent, &Needs);

    VkMemoryAllocateInfo Granting{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    Granting.allocationSize  = Needs.size;
    Granting.memoryTypeIndex = FindGizmoDeviceReserve(Properties, Needs.memoryTypeBits,
                                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(Device, &Granting, nullptr, &Brought.Reserve) != VK_SUCCESS)
    {
        std::cerr << "[GizmoExchange] vkAllocateMemory (" << Label << ") failed.\n";
        return false;
    }

    vkBindBufferMemory(Device, Brought.Extent, Brought.Reserve, 0u);
    if (vkMapMemory(Device, Brought.Reserve, 0u, Brought.Bytes, 0u, &Brought.Mapped) != VK_SUCCESS) Brought.Mapped = nullptr;
    return true;
}

} // namespace

struct GizmoExchange::VulkanRecord
{
    VkDevice                          Device          = VK_NULL_HANDLE;
    VkPhysicalDevice                  Physical        = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties  Properties{};

    VkFormat                          ColourFormat    = VK_FORMAT_UNDEFINED;

    VkRenderPass                      RenderTarget    = VK_NULL_HANDLE;
    VkFramebuffer                     Framing         = VK_NULL_HANDLE;
    VkImageView                       ColourView      = VK_NULL_HANDLE;

    VkDescriptorSetLayout             SetLayout       = VK_NULL_HANDLE;
    VkPipelineLayout                  DrawLayout      = VK_NULL_HANDLE;
    VkPipeline                        TrianglePieces  = VK_NULL_HANDLE;
    VkPipeline                        StrokePieces    = VK_NULL_HANDLE;
    VkDescriptorPool                  Granting        = VK_NULL_HANDLE;

    uint32_t                          CycleSlotCount  = 0u;
    std::array<GizmoDeviceExtent, kGizmoMaximumCycleSlots> Vertices{};
    std::array<VkDescriptorSet, kGizmoMaximumCycleSlots>   Sets{};
    std::array<uint32_t, kGizmoMaximumCycleSlots>          TriangleCounts{};
    std::array<uint32_t, kGizmoMaximumCycleSlots>          StrokeCounts{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

GizmoExchange::GizmoExchange() noexcept
    : Vulkan(new VulkanRecord())
{
}

GizmoExchange::~GizmoExchange() noexcept
{
    Retire();
    delete Vulkan;
    Vulkan = nullptr;
}

bool GizmoExchange::Bring(void* Device, void* PhysicalDevice, uint32_t CycleSlotCount,
                          uint32_t ColourFormat, uint32_t VertexCapacity) noexcept
{
    if (Device == nullptr || PhysicalDevice == nullptr) return false;

    Vulkan->Device         = static_cast<VkDevice>(Device);
    Vulkan->Physical       = static_cast<VkPhysicalDevice>(PhysicalDevice);
    Vulkan->CycleSlotCount = std::clamp(CycleSlotCount, 1u, kGizmoMaximumCycleSlots);
    Vulkan->ColourFormat   = static_cast<VkFormat>(ColourFormat);

    vkGetPhysicalDeviceMemoryProperties(Vulkan->Physical, &Vulkan->Properties);

    Capacity = std::max(VertexCapacity, 3u);

    for (uint32_t Slot = 0u; Slot < Vulkan->CycleSlotCount; ++Slot)
        if (!BringGizmoExtent(Vulkan->Device, Vulkan->Properties, Vulkan->Vertices[Slot],
                              static_cast<VkDeviceSize>(Capacity) * sizeof(GizmoVertex), "vertices"))
            return false;

    if (!BringPipelines())      return false;
    if (!BringDescriptorSets()) return false;

    Ready = true;
    std::cerr << "[GizmoExchange] Ready: capacity " << Capacity << " vertices, "
              << Vulkan->CycleSlotCount << " cycle slots.\n";
    return true;
}

void GizmoExchange::Retire() noexcept
{
    if (Vulkan == nullptr || Vulkan->Device == VK_NULL_HANDLE) return;
    VkDevice D = Vulkan->Device;

    vkDeviceWaitIdle(D);

    if (Vulkan->Framing)        vkDestroyFramebuffer(D, Vulkan->Framing, nullptr);
    if (Vulkan->TrianglePieces) vkDestroyPipeline(D, Vulkan->TrianglePieces, nullptr);
    if (Vulkan->StrokePieces)   vkDestroyPipeline(D, Vulkan->StrokePieces, nullptr);
    if (Vulkan->DrawLayout)     vkDestroyPipelineLayout(D, Vulkan->DrawLayout, nullptr);
    if (Vulkan->SetLayout)      vkDestroyDescriptorSetLayout(D, Vulkan->SetLayout, nullptr);
    if (Vulkan->Granting)       vkDestroyDescriptorPool(D, Vulkan->Granting, nullptr);
    if (Vulkan->RenderTarget)   vkDestroyRenderPass(D, Vulkan->RenderTarget, nullptr);

    for (uint32_t Slot = 0u; Slot < kGizmoMaximumCycleSlots; ++Slot)
        RetireGizmoExtent(D, Vulkan->Vertices[Slot]);

    Vulkan->Framing = VK_NULL_HANDLE; Vulkan->TrianglePieces = VK_NULL_HANDLE; Vulkan->StrokePieces = VK_NULL_HANDLE;
    Vulkan->DrawLayout = VK_NULL_HANDLE; Vulkan->SetLayout = VK_NULL_HANDLE;
    Vulkan->Granting = VK_NULL_HANDLE; Vulkan->RenderTarget = VK_NULL_HANDLE;
    Vulkan->Device = VK_NULL_HANDLE;

    Ready = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      PIPELINES
//------------------------------------------------------------------------------------------------------------------------

bool GizmoExchange::BringPipelines() noexcept
{
    VkDevice D = Vulkan->Device;

    // ── Render pass: load the resolved scene colour, keep it, never clear. No depth: the gizmo reads over the
    //    scene the way Blender's does — a grip must never sink into the object it manipulates.
    {
        VkAttachmentDescription Colour{};
        Colour.format         = Vulkan->ColourFormat;
        Colour.samples        = VK_SAMPLE_COUNT_1_BIT;
        Colour.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
        Colour.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        Colour.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        Colour.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        // Same bracket as InterfaceExchange: the caller hands the image over in COLOR_ATTACHMENT_OPTIMAL.
        Colour.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        Colour.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference ColourAt{ 0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

        VkSubpassDescription Drawing{};
        Drawing.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        Drawing.colorAttachmentCount = 1u;
        Drawing.pColorAttachments    = &ColourAt;

        VkSubpassDependency Ordering[2]{};
        Ordering[0] = { VK_SUBPASS_EXTERNAL, 0u,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                        0u };
        Ordering[1] = { 0u, VK_SUBPASS_EXTERNAL,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
                        0u };

        VkRenderPassCreateInfo Making{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        Making.attachmentCount = 1u;
        Making.pAttachments    = &Colour;
        Making.subpassCount    = 1u;
        Making.pSubpasses      = &Drawing;
        Making.dependencyCount = 2u;
        Making.pDependencies   = Ordering;

        if (vkCreateRenderPass(D, &Making, nullptr, &Vulkan->RenderTarget) != VK_SUCCESS)
        {
            std::cerr << "[GizmoExchange] vkCreateRenderPass failed.\n";
            return false;
        }
    }

    // ── Set layout (binding 0: the pulled vertices) + push range (the 96-byte view).
    {
        VkDescriptorSetLayoutBinding Pulled{ 0u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u,
                                             VK_SHADER_STAGE_VERTEX_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo Making{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        Making.bindingCount = 1u;
        Making.pBindings    = &Pulled;
        if (vkCreateDescriptorSetLayout(D, &Making, nullptr, &Vulkan->SetLayout) != VK_SUCCESS) return false;

        VkPushConstantRange Pushing{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0u, sizeof(GizmoConstantRecord) };
        VkPipelineLayoutCreateInfo Layering{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        Layering.setLayoutCount         = 1u;
        Layering.pSetLayouts            = &Vulkan->SetLayout;
        Layering.pushConstantRangeCount = 1u;
        Layering.pPushConstantRanges    = &Pushing;
        if (vkCreatePipelineLayout(D, &Layering, nullptr, &Vulkan->DrawLayout) != VK_SUCCESS) return false;
    }

    // ── Graphics twice: triangle list, then line list. No vertex input; alpha over the resolved scene.
    VkShaderModule Vertex   = LoadGizmoShader(D, "Engine/Shaders/GizmoRaster.vert.spv");
    VkShaderModule Fragment = LoadGizmoShader(D, "Engine/Shaders/GizmoRaster.frag.spv");
    if (!Vertex || !Fragment) return false;

    VkPipelineShaderStageCreateInfo Speaking[2] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0u, VK_SHADER_STAGE_VERTEX_BIT,   Vertex,   "main", nullptr },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0u, VK_SHADER_STAGE_FRAGMENT_BIT, Fragment, "main", nullptr } };

    VkPipelineVertexInputStateCreateInfo Pulling{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

    VkPipelineInputAssemblyStateCreateInfo Assembling{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };

    VkPipelineViewportStateCreateInfo Viewing{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    Viewing.viewportCount = 1u;
    Viewing.scissorCount  = 1u;

    VkPipelineRasterizationStateCreateInfo Rastering{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    Rastering.polygonMode = VK_POLYGON_MODE_FILL;
    Rastering.cullMode    = VK_CULL_MODE_NONE;    // the reference's grips are double sided
    Rastering.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    Rastering.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo Sampling{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    Sampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // No depth attachment at all — see the render pass note.
    VkPipelineDepthStencilStateCreateInfo Depthing{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };

    // Straight alpha: only the corner quads carry opacity below one (0.28 rest / 0.55 hovered).
    VkPipelineColorBlendAttachmentState Mixing{};
    Mixing.blendEnable         = VK_TRUE;
    Mixing.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    Mixing.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    Mixing.colorBlendOp        = VK_BLEND_OP_ADD;
    Mixing.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    Mixing.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    Mixing.alphaBlendOp        = VK_BLEND_OP_ADD;
    Mixing.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                               | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo MixingAll{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    MixingAll.attachmentCount = 1u;
    MixingAll.pAttachments    = &Mixing;

    const VkDynamicState Changing[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ChangingAll{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    ChangingAll.dynamicStateCount = 2u;
    ChangingAll.pDynamicStates    = Changing;

    VkGraphicsPipelineCreateInfo Making{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    Making.stageCount          = 2u;
    Making.pStages             = Speaking;
    Making.pVertexInputState   = &Pulling;
    Making.pInputAssemblyState = &Assembling;
    Making.pViewportState      = &Viewing;
    Making.pRasterizationState = &Rastering;
    Making.pMultisampleState   = &Sampling;
    Making.pDepthStencilState  = &Depthing;
    Making.pColorBlendState    = &MixingAll;
    Making.pDynamicState       = &ChangingAll;
    Making.layout              = Vulkan->DrawLayout;
    Making.renderPass          = Vulkan->RenderTarget;

    Assembling.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkResult Made = vkCreateGraphicsPipelines(D, VK_NULL_HANDLE, 1u, &Making, nullptr, &Vulkan->TrianglePieces);
    if (Made == VK_SUCCESS)
    {
        Assembling.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        Made = vkCreateGraphicsPipelines(D, VK_NULL_HANDLE, 1u, &Making, nullptr, &Vulkan->StrokePieces);
    }

    vkDestroyShaderModule(D, Vertex, nullptr);
    vkDestroyShaderModule(D, Fragment, nullptr);

    if (Made != VK_SUCCESS)
    {
        std::cerr << "[GizmoExchange] graphics pipeline failed (VkResult " << static_cast<int>(Made) << ").\n";
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   DESCRIPTOR SETS
//------------------------------------------------------------------------------------------------------------------------

bool GizmoExchange::BringDescriptorSets() noexcept
{
    VkDevice D = Vulkan->Device;

    VkDescriptorPoolSize Sizing{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kGizmoMaximumCycleSlots };
    VkDescriptorPoolCreateInfo Making{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    Making.maxSets       = kGizmoMaximumCycleSlots;
    Making.poolSizeCount = 1u;
    Making.pPoolSizes    = &Sizing;
    if (vkCreateDescriptorPool(D, &Making, nullptr, &Vulkan->Granting) != VK_SUCCESS) return false;

    for (uint32_t Slot = 0u; Slot < Vulkan->CycleSlotCount; ++Slot)
    {
        VkDescriptorSetAllocateInfo Granting{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        Granting.descriptorPool     = Vulkan->Granting;
        Granting.descriptorSetCount = 1u;
        Granting.pSetLayouts        = &Vulkan->SetLayout;
        if (vkAllocateDescriptorSets(D, &Granting, &Vulkan->Sets[Slot]) != VK_SUCCESS) return false;

        VkDescriptorBufferInfo Pulled{ Vulkan->Vertices[Slot].Extent, 0u, VK_WHOLE_SIZE };
        VkWriteDescriptorSet Writing{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, Vulkan->Sets[Slot], 0u, 0u, 1u,
                                      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &Pulled, nullptr };
        vkUpdateDescriptorSets(D, 1u, &Writing, 0u, nullptr);
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        RESIZE
//------------------------------------------------------------------------------------------------------------------------

bool GizmoExchange::Resize(uint32_t NewWidth, uint32_t NewHeight, void* ColourView) noexcept
{
    if (Vulkan->Device == VK_NULL_HANDLE || ColourView == nullptr) return false;
    if (NewWidth == 0u || NewHeight == 0u) return false;

    VkDevice D = Vulkan->Device;

    Width  = NewWidth;
    Height = NewHeight;
    Vulkan->ColourView = static_cast<VkImageView>(ColourView);

    if (Vulkan->Framing) { vkDestroyFramebuffer(D, Vulkan->Framing, nullptr); Vulkan->Framing = VK_NULL_HANDLE; }

    VkFramebufferCreateInfo Making{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    Making.renderPass      = Vulkan->RenderTarget;
    Making.attachmentCount = 1u;
    Making.pAttachments    = &Vulkan->ColourView;
    Making.width           = Width;
    Making.height          = Height;
    Making.layers          = 1u;

    if (vkCreateFramebuffer(D, &Making, nullptr, &Vulkan->Framing) != VK_SUCCESS)
    {
        std::cerr << "[GizmoExchange] vkCreateFramebuffer failed.\n";
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    VERTEX UPLOAD
//------------------------------------------------------------------------------------------------------------------------

void GizmoExchange::UploadVertices(const GizmoVertex* Triangles, uint32_t TriangleCount,
                                   const GizmoVertex* Strokes, uint32_t StrokeCount, uint32_t CycleSlot) noexcept
{
    if (!Ready) return;
    const uint32_t Slot = CycleSlot % Vulkan->CycleSlotCount;
    Vulkan->TriangleCounts[Slot] = 0u;
    Vulkan->StrokeCounts[Slot]   = 0u;

    GizmoDeviceExtent& Seat = Vulkan->Vertices[Slot];
    if (Seat.Mapped == nullptr) return;

    uint32_t SeatedTriangles = std::min(TriangleCount, Capacity);
    uint32_t SeatedStrokes   = std::min(StrokeCount, Capacity - SeatedTriangles);
    if (Triangles != nullptr && SeatedTriangles > 0u)
        std::memcpy(Seat.Mapped, Triangles, static_cast<size_t>(SeatedTriangles) * sizeof(GizmoVertex));
    if (Strokes != nullptr && SeatedStrokes > 0u)
        std::memcpy(static_cast<GizmoVertex*>(Seat.Mapped) + SeatedTriangles, Strokes,
                    static_cast<size_t>(SeatedStrokes) * sizeof(GizmoVertex));

    Vulkan->TriangleCounts[Slot] = Triangles != nullptr ? SeatedTriangles : 0u;
    Vulkan->StrokeCounts[Slot]   = Strokes != nullptr ? SeatedStrokes : 0u;

    if (TriangleCount + StrokeCount > Capacity)
        std::cerr << "[GizmoExchange] " << (TriangleCount + StrokeCount)
                  << " vertices exceeds the capacity of " << Capacity << "; the tail was dropped.\n";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE DRAWS
//------------------------------------------------------------------------------------------------------------------------

void GizmoExchange::RecordGizmo(void* Command, uint32_t CycleSlot, const GizmoViewClip& View) noexcept
{
    if (!Ready || Command == nullptr) return;
    if (Vulkan->Framing == VK_NULL_HANDLE) return;

    const uint32_t Slot = CycleSlot % Vulkan->CycleSlotCount;
    const uint32_t TriangleCount = Vulkan->TriangleCounts[Slot];
    const uint32_t StrokeCount   = Vulkan->StrokeCounts[Slot];
    if (TriangleCount == 0u && StrokeCount == 0u) return;

    VkCommandBuffer Recording = static_cast<VkCommandBuffer>(Command);

    GizmoConstantRecord Pushed{};
    std::memcpy(Pushed.ViewClip, View.ViewClip, sizeof(Pushed.ViewClip));
    Pushed.CameraOrigin[0] = View.EyeX;   Pushed.CameraOrigin[1] = View.EyeY;   Pushed.CameraOrigin[2] = View.EyeZ;
    Pushed.LightToward[0]  = View.LightX; Pushed.LightToward[1]  = View.LightY; Pushed.LightToward[2]  = View.LightZ;

    VkRenderPassBeginInfo Opening{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    Opening.renderPass        = Vulkan->RenderTarget;
    Opening.framebuffer       = Vulkan->Framing;
    Opening.renderArea.extent = { Width, Height };

    vkCmdBeginRenderPass(Recording, &Opening, VK_SUBPASS_CONTENTS_INLINE);

    const VkViewport Viewing{ 0.0f, 0.0f, static_cast<float>(View.RenderWidth), static_cast<float>(View.RenderHeight), 0.0f, 1.0f };
    const VkRect2D   Clipping{ { 0, 0 }, { View.RenderWidth, View.RenderHeight } };
    vkCmdSetViewport(Recording, 0u, 1u, &Viewing);
    vkCmdSetScissor (Recording, 0u, 1u, &Clipping);

    vkCmdBindDescriptorSets(Recording, VK_PIPELINE_BIND_POINT_GRAPHICS, Vulkan->DrawLayout, 0u, 1u, &Vulkan->Sets[Slot], 0u, nullptr);
    vkCmdPushConstants(Recording, Vulkan->DrawLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0u, sizeof(Pushed), &Pushed);

    if (TriangleCount > 0u)
    {
        vkCmdBindPipeline(Recording, VK_PIPELINE_BIND_POINT_GRAPHICS, Vulkan->TrianglePieces);
        vkCmdDraw(Recording, TriangleCount, 1u, 0u, 0u);
    }
    if (StrokeCount > 0u)
    {
        vkCmdBindPipeline(Recording, VK_PIPELINE_BIND_POINT_GRAPHICS, Vulkan->StrokePieces);
        vkCmdDraw(Recording, StrokeCount, 1u, TriangleCount, 0u);
    }

    vkCmdEndRenderPass(Recording);
}

} // namespace Frontier
