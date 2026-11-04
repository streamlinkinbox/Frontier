/*====================================================================================================================================
                                                      SOLIDARCRASTERIZATION.CPP
====================================================================================================================================*/
// 🧩 Viewport rasterization: offscreen colour + R32 identity + depth, surface / edge / grid programs, probe and readback

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "UI/SolidArc/Viewport/SolidArcRasterization.h"

#include "UI/SolidArc/Viewport/Generated/SolidArcShaderArchive.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

namespace Frontier::SolidArc
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr VkFormat ColourFormat   = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat IdentityFormat = VK_FORMAT_R32_UINT;
constexpr VkFormat DepthFormat    = VK_FORMAT_D32_SFLOAT;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void Require(VkResult Result, const char* Label)
{
    if (Result != VK_SUCCESS) throw std::runtime_error(std::string(Label) + " failed (VkResult " + std::to_string(static_cast<int>(Result)) + ")");
}

template <size_t Words>
VkShaderModule CreateModule(VkDevice Device, const uint32_t (&Code)[Words])
{
    VkShaderModuleCreateInfo Request{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    Request.codeSize = Words * sizeof(uint32_t);
    Request.pCode    = Code;
    VkShaderModule Module = VK_NULL_HANDLE;
    Require(vkCreateShaderModule(Device, &Request, nullptr, &Module), "vkCreateShaderModule");
    return Module;
}

// 📝 X-ray occluded strokes count as hidden only when they lie more than this many pixels (world size at the target)
//    behind the face in front; below that, the stroke pixels of a visible rim spilling onto its own steep face would read
//    as hidden (measured: 1097 of 2743 revealed pixels at a bare depth bias, see ViewportValidation V9).
constexpr double OccludedMarginPixels = 8.0;

struct PipelineRecipe
{
    VkShaderModule                                 Vertex         = VK_NULL_HANDLE;
    VkShaderModule                                 Fragment       = VK_NULL_HANDLE;
    const char*                                    VertexEntry    = "main";
    const char*                                    FragmentEntry  = "main";
    std::vector<VkVertexInputBindingDescription>   Bindings       = {};
    std::vector<VkVertexInputAttributeDescription> Attributes     = {};
    bool                                           DepthWrite     = true;
    bool                                           ColourBlend    = false;
    bool                                           IdentityWrite  = true;
    bool                                           ColourWrite    = true;
    bool                                           DepthBias      = false;
    VkCompareOp                                    DepthCompare   = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkSampleCountFlagBits                          Samples        = VK_SAMPLE_COUNT_1_BIT;
};

// 📝 The largest count ≤ Wish (a power of two) that the device supports for the colour, identity and depth attachments.
VkSampleCountFlagBits SupportedSamples(const SolidArcDevice& Device, uint32_t Wish)
{
    VkSampleCountFlags Common = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT | VK_SAMPLE_COUNT_8_BIT;
    const std::pair<VkFormat, VkImageUsageFlags> Attachments[] = { { ColourFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT },
                                                                   { IdentityFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT },
                                                                   { DepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT } };
    for (const auto& [Format, Usage] : Attachments)
    {
        VkImageFormatProperties Described{};
        if (vkGetPhysicalDeviceImageFormatProperties(Device.Physical, Format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, Usage, 0, &Described) != VK_SUCCESS)
            return VK_SAMPLE_COUNT_1_BIT;
        Common &= Described.sampleCounts;
    }
    for (uint32_t Count = 8; Count > 1; Count /= 2)
        if (Count <= Wish && (Common & Count) != 0u) return VkSampleCountFlagBits(Count);
    return VK_SAMPLE_COUNT_1_BIT;
}

VkPipeline CreatePipeline(VkDevice Device, VkPipelineLayout Layout, const PipelineRecipe& Recipe)
{
    VkPipelineShaderStageCreateInfo Programs[2]{};
    Programs[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, Recipe.Vertex, Recipe.VertexEntry, nullptr };
    Programs[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, Recipe.Fragment, Recipe.FragmentEntry, nullptr };

    VkPipelineVertexInputStateCreateInfo Input{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    Input.vertexBindingDescriptionCount   = static_cast<uint32_t>(Recipe.Bindings.size());
    Input.pVertexBindingDescriptions      = Recipe.Bindings.data();
    Input.vertexAttributeDescriptionCount = static_cast<uint32_t>(Recipe.Attributes.size());
    Input.pVertexAttributeDescriptions    = Recipe.Attributes.data();
    VkPipelineInputAssemblyStateCreateInfo Assembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    Assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo Viewport{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    Viewport.viewportCount = 1;
    Viewport.scissorCount  = 1;
    VkPipelineRasterizationStateCreateInfo Raster{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    Raster.polygonMode = VK_POLYGON_MODE_FILL;
    Raster.cullMode    = VK_CULL_MODE_NONE;
    Raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;                                    // 📝 Outward CCW triangles (y-flip in ClipFromWorld)
    Raster.lineWidth   = 1.0f;
    if (Recipe.DepthBias)
    {
        Raster.depthBiasEnable         = VK_TRUE;
        Raster.depthBiasConstantFactor = -2.0f;
        Raster.depthBiasSlopeFactor    = -1.0f;
    }
    VkPipelineMultisampleStateCreateInfo Samples{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    Samples.rasterizationSamples = Recipe.Samples;
    VkPipelineDepthStencilStateCreateInfo DepthState{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    DepthState.depthTestEnable  = VK_TRUE;
    DepthState.depthWriteEnable = Recipe.DepthWrite ? VK_TRUE : VK_FALSE;
    DepthState.depthCompareOp   = Recipe.DepthCompare;
    VkPipelineColorBlendAttachmentState Blends[2]{};
    Blends[0].colorWriteMask = Recipe.ColourWrite ? VkColorComponentFlags(VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT)
                                                  : VkColorComponentFlags(0);
    if (Recipe.ColourBlend)
    {
        Blends[0].blendEnable         = VK_TRUE;
        Blends[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        Blends[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        Blends[0].colorBlendOp        = VK_BLEND_OP_ADD;
        Blends[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        Blends[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        Blends[0].alphaBlendOp        = VK_BLEND_OP_ADD;
    }
    Blends[1].colorWriteMask = Recipe.IdentityWrite ? VkColorComponentFlags(VK_COLOR_COMPONENT_R_BIT) : VkColorComponentFlags(0);         // 📝 Integer target: never blended
    VkPipelineColorBlendStateCreateInfo Blend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    Blend.attachmentCount = 2;
    Blend.pAttachments    = Blends;
    const VkDynamicState Dynamic[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo DynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    DynamicState.dynamicStateCount = 2;
    DynamicState.pDynamicStates    = Dynamic;
    const VkFormat ColourFormats[] = { ColourFormat, IdentityFormat };
    VkPipelineRenderingCreateInfo Rendering{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    Rendering.colorAttachmentCount    = 2;
    Rendering.pColorAttachmentFormats = ColourFormats;
    Rendering.depthAttachmentFormat   = DepthFormat;

    VkGraphicsPipelineCreateInfo Request{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    Request.pNext               = &Rendering;
    Request.stageCount          = 2;
    Request.pStages             = Programs;
    Request.pVertexInputState   = &Input;
    Request.pInputAssemblyState = &Assembly;
    Request.pViewportState      = &Viewport;
    Request.pRasterizationState = &Raster;
    Request.pMultisampleState   = &Samples;
    Request.pDepthStencilState  = &DepthState;
    Request.pColorBlendState    = &Blend;
    Request.pDynamicState       = &DynamicState;
    Request.layout              = Layout;
    VkPipeline Pipeline = VK_NULL_HANDLE;
    Require(vkCreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1, &Request, nullptr, &Pipeline), "vkCreateGraphicsPipelines");
    return Pipeline;
}

void Narrow(const CameraMatrix& Source, float Target[16])
{
    for (int Index = 0; Index < 16; ++Index) Target[Index] = static_cast<float>(Source[Index]);
}

void Narrow(const std::array<double, 3>& Source, float Target[4], float Fourth)
{
    for (int Index = 0; Index < 3; ++Index) Target[Index] = static_cast<float>(Source[Index]);
    Target[3] = Fourth;
}

} // namespace

bool FacesSolidArcImagePlane(const SolidArcImagePlaneDraw& Draw, const OrbitCameraFrame& Frame) noexcept
{
    if (!Draw.FrontOnlyCondition) return true; // Existing projects remain two-sided.
    // Image top-left + Right + Down: Down × Right faces the intended viewer.
    // Evaluated per raster pane, never only against the active pane's camera.
    const auto& R = Draw.Right; const auto& D = Draw.Down;
    const std::array<double, 3> N{ D[1]*R[2]-D[2]*R[1], D[2]*R[0]-D[0]*R[2], D[0]*R[1]-D[1]*R[0] };
    const double Facing = N[0]*Frame.Forward[0]+N[1]*Frame.Forward[1]+N[2]*Frame.Forward[2];
    return Facing < -1.0e-6*std::sqrt(N[0]*N[0]+N[1]*N[1]+N[2]*N[2]);
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    RASTERIZATION
//------------------------------------------------------------------------------------------------------------------------

SolidArcRasterization::~SolidArcRasterization()
{
    Destroy();
}

bool SolidArcRasterization::Create(const SolidArcDevice& Borrowed, std::string& Refusal, uint32_t SampleWish)
{
    Device  = &Borrowed;
    Samples = SupportedSamples(Borrowed, SampleWish);
    try
    {
        // ① descriptor set: face states (binding 0), edge states (binding 1), section plane (binding 2; the push block is full)
        VkDescriptorSetLayoutBinding Bindings[3]{};
        Bindings[0] = { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
        Bindings[1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
        Bindings[2] = { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo LayoutRequest{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        LayoutRequest.bindingCount = 3;
        LayoutRequest.pBindings    = Bindings;
        Require(vkCreateDescriptorSetLayout(Device->Device, &LayoutRequest, nullptr, &BufferLayout), "vkCreateDescriptorSetLayout");
        const VkDescriptorPoolSize PoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 };
        VkDescriptorPoolCreateInfo PoolRequest{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        PoolRequest.maxSets       = 1;
        PoolRequest.poolSizeCount = 1;
        PoolRequest.pPoolSizes    = &PoolSize;
        Require(vkCreateDescriptorPool(Device->Device, &PoolRequest, nullptr, &DescriptorPool), "vkCreateDescriptorPool");
        VkDescriptorSetAllocateInfo DescriptorRequest{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        DescriptorRequest.descriptorPool     = DescriptorPool;
        DescriptorRequest.descriptorSetCount = 1;
        DescriptorRequest.pSetLayouts        = &BufferLayout;
        Require(vkAllocateDescriptorSets(Device->Device, &DescriptorRequest, &BufferSet), "vkAllocateDescriptorSets");
        SectionBuffer = Device->CreateBuffer(8 * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        std::memset(SectionBuffer.Mapped, 0, SectionBuffer.Size);
        const VkDescriptorBufferInfo SectionTarget{ SectionBuffer.Buffer, 0, VK_WHOLE_SIZE };
        VkWriteDescriptorSet SectionWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        SectionWrite.dstSet          = BufferSet;
        SectionWrite.dstBinding      = 2;
        SectionWrite.descriptorCount = 1;
        SectionWrite.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        SectionWrite.pBufferInfo     = &SectionTarget;
        vkUpdateDescriptorSets(Device->Device, 1, &SectionWrite, 0, nullptr);

        // ② pipeline layout: the set + one 128-byte push range
        const VkPushConstantRange Push{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128 };
        VkPipelineLayoutCreateInfo PipelineLayoutRequest{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        PipelineLayoutRequest.setLayoutCount         = 1;
        PipelineLayoutRequest.pSetLayouts            = &BufferLayout;
        PipelineLayoutRequest.pushConstantRangeCount = 1;
        PipelineLayoutRequest.pPushConstantRanges    = &Push;
        Require(vkCreatePipelineLayout(Device->Device, &PipelineLayoutRequest, nullptr, &PipelineLayout), "vkCreatePipelineLayout");

        // ③ image planes: set 1 = one combined picture sampler; up to PictureLimit pictures
        constexpr uint32_t PictureLimit = 16;
        const VkDescriptorSetLayoutBinding PictureBinding{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo PictureLayoutRequest{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        PictureLayoutRequest.bindingCount = 1;
        PictureLayoutRequest.pBindings    = &PictureBinding;
        Require(vkCreateDescriptorSetLayout(Device->Device, &PictureLayoutRequest, nullptr, &PictureLayout), "vkCreateDescriptorSetLayout (pictures)");
        const VkDescriptorPoolSize PictureSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, PictureLimit };
        VkDescriptorPoolCreateInfo PicturePoolRequest{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        PicturePoolRequest.maxSets       = PictureLimit;
        PicturePoolRequest.poolSizeCount = 1;
        PicturePoolRequest.pPoolSizes    = &PictureSize;
        Require(vkCreateDescriptorPool(Device->Device, &PicturePoolRequest, nullptr, &PicturePool), "vkCreateDescriptorPool (pictures)");
        VkSamplerCreateInfo SamplerRequest{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        SamplerRequest.magFilter    = VK_FILTER_LINEAR;
        SamplerRequest.minFilter    = VK_FILTER_LINEAR;
        SamplerRequest.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        SamplerRequest.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        SamplerRequest.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        SamplerRequest.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        SamplerRequest.maxLod       = VK_LOD_CLAMP_NONE;
        Require(vkCreateSampler(Device->Device, &SamplerRequest, nullptr, &PictureSampler), "vkCreateSampler");
        const VkDescriptorSetLayout PlaneSets[2] = { BufferLayout, PictureLayout };
        VkPipelineLayoutCreateInfo PlaneLayoutRequest{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        PlaneLayoutRequest.setLayoutCount         = 2;
        PlaneLayoutRequest.pSetLayouts            = PlaneSets;
        PlaneLayoutRequest.pushConstantRangeCount = 1;
        PlaneLayoutRequest.pPushConstantRanges    = &Push;
        Require(vkCreatePipelineLayout(Device->Device, &PlaneLayoutRequest, nullptr, &PlaneLayout), "vkCreatePipelineLayout (pictures)");

        // ④ matcaps: the surface program shares the image-plane layout; set 1 = the matcap array (own pool, so releasing
        //    image-plane pictures never frees it)
        const VkDescriptorPoolSize MatcapSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
        VkDescriptorPoolCreateInfo MatcapPoolRequest{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        MatcapPoolRequest.maxSets       = 1;
        MatcapPoolRequest.poolSizeCount = 1;
        MatcapPoolRequest.pPoolSizes    = &MatcapSize;
        Require(vkCreateDescriptorPool(Device->Device, &MatcapPoolRequest, nullptr, &MatcapPool), "vkCreateDescriptorPool (matcaps)");
        VkDescriptorSetAllocateInfo MatcapRequest{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        MatcapRequest.descriptorPool     = MatcapPool;
        MatcapRequest.descriptorSetCount = 1;
        MatcapRequest.pSetLayouts        = &PictureLayout;
        Require(vkAllocateDescriptorSets(Device->Device, &MatcapRequest, &MatcapSet), "vkAllocateDescriptorSets (matcaps)");

        CreatePipelines();
        UploadMatcaps({});
        ProbeReadback = Device->CreateBuffer(16, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
        AssignStates({}, {});
        CreateTargets(64, 64);
        return true;
    }
    catch (const std::exception& Failure)
    {
        Refusal = Failure.what();
        Destroy();
        return false;
    }
}

void SolidArcRasterization::CreatePipelines()
{
    using namespace ShaderArchive;
    const VkShaderModule SurfaceVertex   = CreateModule(Device->Device, SolidArcSurfaceVertexCode);
    const VkShaderModule SurfaceFragment = CreateModule(Device->Device, SolidArcSurfaceFragmentCode);
    const VkShaderModule EdgeVertex      = CreateModule(Device->Device, SolidArcEdgeVertexCode);
    const VkShaderModule EdgeFragment    = CreateModule(Device->Device, SolidArcEdgeFragmentCode);
    const VkShaderModule EdgeOccluded    = CreateModule(Device->Device, SolidArcEdgeOccludedFragmentCode);
    const VkShaderModule EdgeIdentity    = CreateModule(Device->Device, SolidArcEdgeIdentityFragmentCode);
    const VkShaderModule GridVertex      = CreateModule(Device->Device, SolidArcGridVertexCode);
    const VkShaderModule GridFragment    = CreateModule(Device->Device, SolidArcGridFragmentCode);
    const VkShaderModule PlaneVertex     = CreateModule(Device->Device, SolidArcImagePlaneVertexCode);
    const VkShaderModule PlaneFragment   = CreateModule(Device->Device, SolidArcImagePlaneFragmentCode);
    const VkShaderModule GizmoVertex     = CreateModule(Device->Device, SolidArcGizmoVertexCode);
    const VkShaderModule GizmoFragment   = CreateModule(Device->Device, SolidArcGizmoFragmentCode);

    PipelineRecipe Surface;
    Surface.Samples       = Samples;
    Surface.Vertex        = SurfaceVertex;
    Surface.Fragment      = SurfaceFragment;
    Surface.VertexEntry   = SolidArcSurfaceVertexEntry;
    Surface.FragmentEntry = SolidArcSurfaceFragmentEntry;
    Surface.Bindings      = { { 0, sizeof(SolidArcSceneCorner), VK_VERTEX_INPUT_RATE_VERTEX } };
    Surface.Attributes    = { { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 }, { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12 }, { 2, 0, VK_FORMAT_R32_UINT, 24 } };
    SurfacePipeline       = CreatePipeline(Device->Device, PlaneLayout, Surface);

    // 📝 X-ray faces in two draws so each pixel blends exactly one face whatever the triangle order: ① the nearest depth
    //    and its identity (no colour), ② the same programs again, depth test ≤ without writes, so only the fragment
    //    at that depth passes (identical vertex program → identical depth) and blends at α over grid / image planes.
    PipelineRecipe SurfaceDepth = Surface;
    SurfaceDepth.ColourWrite    = false;
    SurfaceDepthPipeline        = CreatePipeline(Device->Device, PlaneLayout, SurfaceDepth);
    PipelineRecipe SurfaceGhost = Surface;
    SurfaceGhost.DepthWrite     = false;
    SurfaceGhost.ColourBlend    = true;
    SurfaceGhost.IdentityWrite  = false;
    SurfaceGhostPipeline        = CreatePipeline(Device->Device, PlaneLayout, SurfaceGhost);

    PipelineRecipe CutterDepth  = SurfaceDepth;
    CutterDepth.DepthBias       = true;
    CutterDepthPipeline         = CreatePipeline(Device->Device, PlaneLayout, CutterDepth);
    PipelineRecipe CutterGhost  = SurfaceGhost;
    CutterGhost.DepthBias       = true;
    CutterGhostPipeline         = CreatePipeline(Device->Device, PlaneLayout, CutterGhost);

    PipelineRecipe Edge;
    Edge.Samples       = Samples;
    Edge.Vertex        = EdgeVertex;
    Edge.Fragment      = EdgeFragment;
    Edge.VertexEntry   = SolidArcEdgeVertexEntry;
    Edge.FragmentEntry = SolidArcEdgeFragmentEntry;
    Edge.Bindings      = { { 0, sizeof(SolidArcSceneSegment), VK_VERTEX_INPUT_RATE_INSTANCE } };
    Edge.Attributes    = { { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 }, { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12 }, { 2, 0, VK_FORMAT_R32_UINT, 24 } };
    Edge.ColourBlend   = true;
    Edge.DepthWrite    = false;                                                              // 📝 Strokes never hide each other's fringe
    Edge.IdentityWrite = false;
    EdgePipeline       = CreatePipeline(Device->Device, PipelineLayout, Edge);

    // 📝 Pick codes for the stroke cores: the same quads with the colour masked (the soft fringe stays unpickable).
    PipelineRecipe EdgeCodes  = Edge;
    EdgeCodes.Fragment        = EdgeIdentity;
    EdgeCodes.FragmentEntry   = SolidArcEdgeIdentityFragmentEntry;
    EdgeCodes.ColourWrite     = false;
    EdgeCodes.IdentityWrite   = true;
    EdgeIdentityPipeline      = CreatePipeline(Device->Device, PipelineLayout, EdgeCodes);

    // 📝 X-ray ③: only stroke pixels behind the surface depth (GREATER), never writing depth; the identity is written so
    //    hidden edges hover and pick through the faces (the visible-edge draw afterwards wins where both cover a pixel).
    PipelineRecipe Occluded = Edge;
    Occluded.Fragment       = EdgeOccluded;
    Occluded.FragmentEntry  = SolidArcEdgeOccludedFragmentEntry;
    Occluded.DepthWrite     = false;
    Occluded.DepthCompare   = VK_COMPARE_OP_GREATER;
    EdgeOccludedPipeline    = CreatePipeline(Device->Device, PipelineLayout, Occluded);
    PipelineRecipe OccludedCodes = EdgeCodes;
    OccludedCodes.DepthCompare   = VK_COMPARE_OP_GREATER;
    EdgeOccludedIdentityPipeline = CreatePipeline(Device->Device, PipelineLayout, OccludedCodes);

    PipelineRecipe Grid;
    Grid.Samples       = Samples;
    Grid.Vertex        = GridVertex;
    Grid.Fragment      = GridFragment;
    Grid.VertexEntry   = SolidArcGridVertexEntry;
    Grid.FragmentEntry = SolidArcGridFragmentEntry;
    Grid.DepthWrite    = false;
    Grid.ColourBlend   = true;
    Grid.IdentityWrite = false;
    GridPipeline       = CreatePipeline(Device->Device, PipelineLayout, Grid);

    // 📝 Image planes: depth-tested (the model hides them), never written to depth or to the identity target (not pickable).
    PipelineRecipe Plane;
    Plane.Samples       = Samples;
    Plane.Vertex        = PlaneVertex;
    Plane.Fragment      = PlaneFragment;
    Plane.VertexEntry   = SolidArcImagePlaneVertexEntry;
    Plane.FragmentEntry = SolidArcImagePlaneFragmentEntry;
    Plane.DepthWrite    = false;
    Plane.ColourBlend   = true;
    Plane.IdentityWrite = false;
    PlanePipeline       = CreatePipeline(Device->Device, PlaneLayout, Plane);

    // 📝 Gizmo: drawn after a depth clear, so the model never hides it while its own handles still occlude each other;
    //    never written to the identity target (the CPU pick on the same triangles decides hover / grab).
    PipelineRecipe Gizmo;
    Gizmo.Samples       = Samples;
    Gizmo.Vertex        = GizmoVertex;
    Gizmo.Fragment      = GizmoFragment;
    Gizmo.VertexEntry   = SolidArcGizmoVertexEntry;
    Gizmo.FragmentEntry = SolidArcGizmoFragmentEntry;
    Gizmo.Bindings      = { { 0, sizeof(SolidArcGizmoVertex), VK_VERTEX_INPUT_RATE_VERTEX } };
    Gizmo.Attributes    = { { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 }, { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12 }, { 2, 0, VK_FORMAT_R8G8B8A8_UNORM, 24 } };
    Gizmo.ColourBlend   = true;
    Gizmo.IdentityWrite = false;
    GizmoPipeline       = CreatePipeline(Device->Device, PipelineLayout, Gizmo);

    for (VkShaderModule Module : { SurfaceVertex, SurfaceFragment, EdgeVertex, EdgeFragment, EdgeOccluded, EdgeIdentity, GridVertex, GridFragment, PlaneVertex, PlaneFragment, GizmoVertex, GizmoFragment })
        vkDestroyShaderModule(Device->Device, Module, nullptr);
}

void SolidArcRasterization::Destroy()
{
    if (!Device || !Device->Device) return;
    vkDeviceWaitIdle(Device->Device);
    DestroyTargets();
    for (SolidArcBuffer* Buffer : { &ProbeReadback, &CornerBuffer, &SegmentBuffer, &FaceStateBuffer, &EdgeStateBuffer, &SectionBuffer, &GizmoBuffer }) Device->DestroyBuffer(*Buffer);
    ReleasePictures();
    Device->DestroyImage(MatcapArray);
    if (MatcapPool) vkDestroyDescriptorPool(Device->Device, MatcapPool, nullptr);
    MatcapPool   = VK_NULL_HANDLE;
    MatcapSet    = VK_NULL_HANDLE;
    MatcapLayers = 0;
    for (VkPipeline* Pipeline : { &SurfacePipeline, &SurfaceDepthPipeline, &SurfaceGhostPipeline, &CutterDepthPipeline, &CutterGhostPipeline, &EdgePipeline, &EdgeIdentityPipeline, &EdgeOccludedPipeline, &EdgeOccludedIdentityPipeline, &GridPipeline, &PlanePipeline, &GizmoPipeline })
    {
        if (*Pipeline) vkDestroyPipeline(Device->Device, *Pipeline, nullptr);
        *Pipeline = VK_NULL_HANDLE;
    }
    if (PipelineLayout) vkDestroyPipelineLayout(Device->Device, PipelineLayout, nullptr);
    if (DescriptorPool) vkDestroyDescriptorPool(Device->Device, DescriptorPool, nullptr);
    if (BufferLayout) vkDestroyDescriptorSetLayout(Device->Device, BufferLayout, nullptr);
    if (PlaneLayout) vkDestroyPipelineLayout(Device->Device, PlaneLayout, nullptr);
    if (PicturePool) vkDestroyDescriptorPool(Device->Device, PicturePool, nullptr);
    if (PictureLayout) vkDestroyDescriptorSetLayout(Device->Device, PictureLayout, nullptr);
    if (PictureSampler) vkDestroySampler(Device->Device, PictureSampler, nullptr);
    PlaneLayout    = VK_NULL_HANDLE;
    PicturePool    = VK_NULL_HANDLE;
    PictureLayout  = VK_NULL_HANDLE;
    PictureSampler = VK_NULL_HANDLE;
    PipelineLayout   = VK_NULL_HANDLE;
    DescriptorPool   = VK_NULL_HANDLE;
    BufferLayout     = VK_NULL_HANDLE;
    BufferSet        = VK_NULL_HANDLE;
    UploadedGeneration = UINT64_MAX;
    Device           = nullptr;
}

void SolidArcRasterization::CreateTargets(uint32_t Width, uint32_t Height)
{
    Colour   = Device->CreateImage(Width, Height, ColourFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                   VK_IMAGE_ASPECT_COLOR_BIT);
    Identity = Device->CreateImage(Width, Height, IdentityFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    Depth    = Device->CreateImage(Width, Height, DepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, 0, Samples);
    // 📝 Multisampling: the passes draw into these and the end of rendering resolves them (colour averaged, identity
    //    sample 0), so silhouettes and strokes get smooth borders while picks stay whole codes.
    if (Samples != VK_SAMPLE_COUNT_1_BIT)
    {
        ColourSamples   = Device->CreateImage(Width, Height, ColourFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 0, Samples);
        IdentitySamples = Device->CreateImage(Width, Height, IdentityFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 0, Samples);
    }
    ColourReadback   = Device->CreateBuffer(VkDeviceSize(Width) * Height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
    IdentityReadback = Device->CreateBuffer(VkDeviceSize(Width) * Height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
    ++BuiltTargets;
    // 📝 The interface samples the colour target before the first Record(): start it in the sampled layout.
    Device->SubmitImmediately([&](VkCommandBuffer Commands)
    {
        TransitionSolidArcImage(Commands, Colour.Image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });
}

void SolidArcRasterization::DestroyTargets()
{
    for (SolidArcImage* Image : { &Colour, &Identity, &Depth, &ColourSamples, &IdentitySamples })
        if (Image->Image) Device->DestroyImage(*Image);
    for (SolidArcBuffer* Buffer : { &ColourReadback, &IdentityReadback }) Device->DestroyBuffer(*Buffer);
}

void SolidArcRasterization::Resize(uint32_t Width, uint32_t Height)
{
    Width  = std::max<uint32_t>(Width, 8);
    Height = std::max<uint32_t>(Height, 8);
    if (Width == Colour.Width && Height == Colour.Height) return;
    vkDeviceWaitIdle(Device->Device);
    DestroyTargets();
    CreateTargets(Width, Height);
}

void SolidArcRasterization::UploadScene(const SolidArcScene& Scene)
{
    vkDeviceWaitIdle(Device->Device);
    Device->DestroyBuffer(CornerBuffer);
    Device->DestroyBuffer(SegmentBuffer);
    CornerCount       = static_cast<uint32_t>(Scene.Corners.size());
    SolidCornerCount  = Scene.SolidCornerCount > 0 && Scene.SolidCornerCount <= CornerCount ? Scene.SolidCornerCount : (Scene.SolidCornerCount == 0 && !Scene.Bodies.empty() ? 0u : CornerCount);
    SegmentCount      = static_cast<uint32_t>(Scene.Segments.size());
    SolidSegmentCount = Scene.SolidSegmentCount > 0 && Scene.SolidSegmentCount <= SegmentCount ? Scene.SolidSegmentCount : (Scene.SolidSegmentCount == 0 && !Scene.Bodies.empty() ? 0u : SegmentCount);
    CornerBuffer  = Device->CreateBuffer(sizeof(SolidArcSceneCorner) * std::max<size_t>(CornerCount, 1), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
    SegmentBuffer = Device->CreateBuffer(sizeof(SolidArcSceneSegment) * std::max<size_t>(SegmentCount, 1), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
    if (CornerCount) std::memcpy(CornerBuffer.Mapped, Scene.Corners.data(), sizeof(SolidArcSceneCorner) * CornerCount);
    if (SegmentCount) std::memcpy(SegmentBuffer.Mapped, Scene.Segments.data(), sizeof(SolidArcSceneSegment) * SegmentCount);
    UploadedGeneration = Scene.Generation;
    AssignStates(std::vector<uint32_t>(Scene.Faces.size(), 0u), std::vector<uint32_t>(Scene.Edges.size(), 0u));
}

void SolidArcRasterization::ReleasePictures()
{
    if (!Device || !Device->Device) return;
    for (PictureTexture& Texture : Pictures) Device->DestroyImage(Texture.Image);
    Pictures.clear();
    if (PicturePool) vkResetDescriptorPool(Device->Device, PicturePool, 0);
}

void SolidArcRasterization::UploadPictures(const std::vector<SolidArcPicture>& Sources)
{
    vkDeviceWaitIdle(Device->Device);
    ReleasePictures();
    for (const SolidArcPicture& Source : Sources)
    {
        if (Pictures.size() >= 16) break;
        // 📝 A missing picture still takes its slot (1 × 1 white: drawn as faint paper) so draw indices stay aligned.
        const bool     ReadyCondition = Source.Refusal.empty() && Source.Width > 0 && Source.Height > 0 && Source.Pixels.size() == size_t(Source.Width) * Source.Height * 4;
        const uint32_t Width  = ReadyCondition ? Source.Width : 1;
        const uint32_t Height = ReadyCondition ? Source.Height : 1;
        static const uint8_t Placeholder[4] = { 255, 255, 255, 255 };
        PictureTexture Texture;
        // Full alpha-aware mip chain: a 7K line drawing must remain continuous
        // at ordinary/quad-view zoom, not alias into dots with a base-level sampler.
        std::vector<VkBufferImageCopy> Copies;
        VkDeviceSize Bytes = 0;
        for (uint32_t W = Width, H = Height, Level = 0; ; ++Level)
        {
            VkBufferImageCopy Copy{};
            Copy.bufferOffset = Bytes;
            Copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, Level, 0, 1 };
            Copy.imageExtent = { W, H, 1 };
            Copies.push_back(Copy);
            Bytes += VkDeviceSize(W)*H*4;
            if (W == 1 && H == 1) break;
            W = std::max(1u, W/2); H = std::max(1u, H/2);
        }
        const uint32_t MipLevels = uint32_t(Copies.size());
        Texture.Image = Device->CreateImage(Width, Height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                           VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_SAMPLE_COUNT_1_BIT, MipLevels);
        SolidArcBuffer Staging = Device->CreateBuffer(Bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
        auto* Pixels = static_cast<unsigned char*>(Staging.Mapped);
        std::memcpy(Pixels, ReadyCondition ? Source.Pixels.data() : Placeholder, size_t(Width)*Height*4);
        for (size_t Level = 1; Level < Copies.size(); ++Level)
        {
            const auto& A = Copies[Level-1]; const auto& B = Copies[Level];
            // STBIR_RGBA filters non-premultiplied RGBA with alpha weighting,
            // avoiding dark halos from transparent black around blueprint ink.
            if (!stbir_resize_uint8_linear(Pixels+A.bufferOffset, int(A.imageExtent.width), int(A.imageExtent.height), 0,
                                           Pixels+B.bufferOffset, int(B.imageExtent.width), int(B.imageExtent.height), 0, STBIR_RGBA))
                throw std::runtime_error("reference image mip filtering failed");
        }
        Device->SubmitImmediately([&](VkCommandBuffer Commands)
        {
            TransitionSolidArcImage(Commands, Texture.Image.Image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, MipLevels);
            vkCmdCopyBufferToImage(Commands, Staging.Buffer, Texture.Image.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, MipLevels, Copies.data());
            TransitionSolidArcImage(Commands, Texture.Image.Image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, MipLevels);
        });
        Device->DestroyBuffer(Staging);
        VkDescriptorSetAllocateInfo Request{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        Request.descriptorPool     = PicturePool;
        Request.descriptorSetCount = 1;
        Request.pSetLayouts        = &PictureLayout;
        Require(vkAllocateDescriptorSets(Device->Device, &Request, &Texture.Set), "vkAllocateDescriptorSets (picture)");
        const VkDescriptorImageInfo Target{ PictureSampler, Texture.Image.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet Write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        Write.dstSet          = Texture.Set;
        Write.dstBinding      = 0;
        Write.descriptorCount = 1;
        Write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        Write.pImageInfo      = &Target;
        vkUpdateDescriptorSets(Device->Device, 1, &Write, 0, nullptr);
        Pictures.push_back(Texture);
    }
}

void SolidArcRasterization::UploadMatcaps(const std::vector<SolidArcPicture>& Layers)
{
    vkDeviceWaitIdle(Device->Device);
    Device->DestroyImage(MatcapArray);
    // 📝 Every layer must match the first one's square extent; a mismatched or empty layer is filled mid grey.
    const uint32_t Extent = Layers.empty() || Layers.front().Width == 0 ? 1u : Layers.front().Width;
    const uint32_t Count  = std::max<uint32_t>(uint32_t(Layers.size()), 1u);
    const size_t   LayerBytes = size_t(Extent) * Extent * 4;
    SolidArcBuffer Staging = Device->CreateBuffer(VkDeviceSize(LayerBytes) * Count, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    for (uint32_t Layer = 0; Layer < Count; ++Layer)
    {
        uint8_t* Target = static_cast<uint8_t*>(Staging.Mapped) + LayerBytes * Layer;
        const bool FitCondition = Layer < Layers.size() && Layers[Layer].Width == Extent && Layers[Layer].Height == Extent && Layers[Layer].Pixels.size() == LayerBytes;
        if (FitCondition) std::memcpy(Target, Layers[Layer].Pixels.data(), LayerBytes);
        else std::memset(Target, 128, LayerBytes);
    }
    MatcapArray = Device->CreateImage(Extent, Extent, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT, Count);
    Device->SubmitImmediately([&](VkCommandBuffer Commands)
    {
        VkImageMemoryBarrier2 Barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        Barrier.srcStageMask     = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        Barrier.srcAccessMask    = VK_ACCESS_2_MEMORY_WRITE_BIT;
        Barrier.dstStageMask     = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        Barrier.dstAccessMask    = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        Barrier.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
        Barrier.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        Barrier.image            = MatcapArray.Image;
        Barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, Count };
        VkDependencyInfo Dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        Dependency.imageMemoryBarrierCount = 1;
        Dependency.pImageMemoryBarriers    = &Barrier;
        vkCmdPipelineBarrier2(Commands, &Dependency);
        VkBufferImageCopy Copy{};
        Copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, Count };
        Copy.imageExtent      = { Extent, Extent, 1 };
        vkCmdCopyBufferToImage(Commands, Staging.Buffer, MatcapArray.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Copy);
        Barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        Barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier2(Commands, &Dependency);
    });
    Device->DestroyBuffer(Staging);
    const VkDescriptorImageInfo Target{ PictureSampler, MatcapArray.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet Write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    Write.dstSet          = MatcapSet;
    Write.dstBinding      = 0;
    Write.descriptorCount = 1;
    Write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Write.pImageInfo      = &Target;
    vkUpdateDescriptorSets(Device->Device, 1, &Write, 0, nullptr);
    MatcapLayers = Layers.empty() ? 0u : Count;
}

void SolidArcRasterization::AssignStates(const std::vector<uint32_t>& FaceStates, const std::vector<uint32_t>& EdgeStates)
{
    // 📝 Reallocate only when the counts change (a new scene); otherwise overwrite through the persistent mapping.
    const bool ReshapeCondition = !FaceStateBuffer.Buffer || FaceStates.size() != FaceCount || EdgeStates.size() != EdgeCount;
    if (ReshapeCondition)
    {
        vkDeviceWaitIdle(Device->Device);
        Device->DestroyBuffer(FaceStateBuffer);
        Device->DestroyBuffer(EdgeStateBuffer);
        FaceCount       = static_cast<uint32_t>(FaceStates.size());
        EdgeCount       = static_cast<uint32_t>(EdgeStates.size());
        FaceStateBuffer = Device->CreateBuffer(4 * std::max<size_t>(FaceCount, 1), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        EdgeStateBuffer = Device->CreateBuffer(4 * std::max<size_t>(EdgeCount, 1), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        const VkDescriptorBufferInfo Targets[2] = { { FaceStateBuffer.Buffer, 0, VK_WHOLE_SIZE }, { EdgeStateBuffer.Buffer, 0, VK_WHOLE_SIZE } };
        VkWriteDescriptorSet Writes[2]{};
        for (uint32_t Binding = 0; Binding < 2; ++Binding)
        {
            Writes[Binding]                 = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            Writes[Binding].dstSet          = BufferSet;
            Writes[Binding].dstBinding      = Binding;
            Writes[Binding].descriptorCount = 1;
            Writes[Binding].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            Writes[Binding].pBufferInfo     = &Targets[Binding];
        }
        vkUpdateDescriptorSets(Device->Device, 2, Writes, 0, nullptr);
    }
    std::memset(FaceStateBuffer.Mapped, 0, FaceStateBuffer.Size);
    std::memset(EdgeStateBuffer.Mapped, 0, EdgeStateBuffer.Size);
    if (!FaceStates.empty()) std::memcpy(FaceStateBuffer.Mapped, FaceStates.data(), 4 * FaceStates.size());
    if (!EdgeStates.empty()) std::memcpy(EdgeStateBuffer.Mapped, EdgeStates.data(), 4 * EdgeStates.size());
}

void SolidArcRasterization::Record(VkCommandBuffer Commands, const OrbitCameraFrame& Frame, const SolidArcViewportStyle& Style,
                                   bool ReadbackCondition, int ProbeX, int ProbeY)
{
    SolidArcRasterPane Whole;
    Whole.OffsetX = 0;
    Whole.OffsetY = 0;
    Whole.Width   = Colour.Width;
    Whole.Height  = Colour.Height;
    Whole.Frame   = Frame;
    Whole.Style   = Style;
    RecordPanes(Commands, { std::move(Whole) }, Style.Background, ReadbackCondition, ProbeX, ProbeY);
}

void SolidArcRasterization::RecordPanes(VkCommandBuffer Commands, const std::vector<SolidArcRasterPane>& Panes, const float Background[4],
                                        bool ReadbackCondition, int ProbeX, int ProbeY)
{
    // Pre-pack all panes' gizmo vertices before beginning dynamic rendering so GizmoBuffer never reallocates mid-pass.
    std::vector<uint32_t> GizmoFirst(Panes.size(), 0u);
    std::vector<uint32_t> GizmoCount(Panes.size(), 0u);
    size_t TotalGizmoVerts = 0;
    for (size_t Idx = 0; Idx < Panes.size(); ++Idx)
    {
        GizmoFirst[Idx] = uint32_t(TotalGizmoVerts);
        GizmoCount[Idx] = uint32_t(Panes[Idx].Style.Gizmo.Vertices.size());
        TotalGizmoVerts += Panes[Idx].Style.Gizmo.Vertices.size();
    }
    if (TotalGizmoVerts > 0 && GizmoPipeline)
    {
        const VkDeviceSize Needed = sizeof(SolidArcGizmoVertex) * TotalGizmoVerts;
        if (GizmoBuffer.Size < Needed)
        {
            vkDeviceWaitIdle(Device->Device);
            Device->DestroyBuffer(GizmoBuffer);
            GizmoBuffer = Device->CreateBuffer(std::max<VkDeviceSize>(Needed * 2, 64 * 1024), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
        }
        auto* Dst = static_cast<SolidArcGizmoVertex*>(GizmoBuffer.Mapped);
        for (size_t Idx = 0; Idx < Panes.size(); ++Idx)
        {
            if (GizmoCount[Idx] > 0)
                std::memcpy(Dst + GizmoFirst[Idx], Panes[Idx].Style.Gizmo.Vertices.data(), sizeof(SolidArcGizmoVertex) * GizmoCount[Idx]);
        }
    }

    // ① targets → attachments (contents discarded: everything is cleared)
    TransitionSolidArcImage(Commands, Colour.Image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    TransitionSolidArcImage(Commands, Identity.Image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    TransitionSolidArcImage(Commands, Depth.Image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    const bool Multisampled = Samples != VK_SAMPLE_COUNT_1_BIT;
    if (Multisampled)
    {
        TransitionSolidArcImage(Commands, ColourSamples.Image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        TransitionSolidArcImage(Commands, IdentitySamples.Image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }

    VkRenderingAttachmentInfo ColourTargets[2]{};
    ColourTargets[0] = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    ColourTargets[0].imageView   = Colour.View;
    ColourTargets[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ColourTargets[0].loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ColourTargets[0].storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    std::memcpy(ColourTargets[0].clearValue.color.float32, Background, sizeof(float) * 4);
    ColourTargets[1] = ColourTargets[0];
    ColourTargets[1].imageView = Identity.View;
    ColourTargets[1].clearValue.color.uint32[0] = 0u;
    ColourTargets[1].clearValue.color.uint32[1] = 0u;
    ColourTargets[1].clearValue.color.uint32[2] = 0u;
    ColourTargets[1].clearValue.color.uint32[3] = 0u;
    if (Multisampled)
    {
        ColourTargets[0].imageView          = ColourSamples.View;
        ColourTargets[0].storeOp            = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        ColourTargets[0].resolveMode        = VK_RESOLVE_MODE_AVERAGE_BIT;
        ColourTargets[0].resolveImageView   = Colour.View;
        ColourTargets[0].resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ColourTargets[1].imageView          = IdentitySamples.View;
        ColourTargets[1].storeOp            = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        ColourTargets[1].resolveMode        = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;                // Integer codes are never averaged
        ColourTargets[1].resolveImageView   = Identity.View;
        ColourTargets[1].resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    VkRenderingAttachmentInfo DepthTarget{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    DepthTarget.imageView               = Depth.View;
    DepthTarget.imageLayout             = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    DepthTarget.loadOp                  = VK_ATTACHMENT_LOAD_OP_CLEAR;
    DepthTarget.storeOp                 = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    DepthTarget.clearValue.depthStencil = { 1.0f, 0 };
    VkRenderingInfo Rendering{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    Rendering.renderArea           = { { 0, 0 }, { Colour.Width, Colour.Height } };
    Rendering.layerCount           = 1;
    Rendering.colorAttachmentCount = 2;
    Rendering.pColorAttachments    = ColourTargets;
    Rendering.pDepthAttachment     = &DepthTarget;
    vkCmdBeginRendering(Commands, &Rendering);

    if (!Panes.empty() && SectionBuffer.Mapped)
    {
        const OrbitCameraFrame& FirstFrame = Panes.front().Frame;
        const SolidArcViewportStyle& FirstStyle = Panes.front().Style;
        const float Block[8] = { float(FirstStyle.SectionPlane[0]), float(FirstStyle.SectionPlane[1]), float(FirstStyle.SectionPlane[2]), float(FirstStyle.SectionPlane[3]),
                                 float(FirstFrame.Eye[0]), float(FirstFrame.Eye[1]), float(FirstFrame.Eye[2]), FirstStyle.SectionEnabled ? 1.0f : 0.0f };
        std::memcpy(SectionBuffer.Mapped, Block, sizeof(Block));
    }

    const VkShaderStageFlags PushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    const VkDeviceSize       Origin     = 0;

    for (size_t PaneIdx = 0; PaneIdx < Panes.size(); ++PaneIdx)
    {
        const SolidArcRasterPane&    Pane  = Panes[PaneIdx];
        const OrbitCameraFrame&      Frame = Pane.Frame;
        const SolidArcViewportStyle& Style = Pane.Style;

        const int32_t  ClampedX = std::clamp<int32_t>(Pane.OffsetX, 0, int32_t(Colour.Width));
        const int32_t  ClampedY = std::clamp<int32_t>(Pane.OffsetY, 0, int32_t(Colour.Height));
        const uint32_t MaxW     = uint32_t(int32_t(Colour.Width) - ClampedX);
        const uint32_t MaxH     = uint32_t(int32_t(Colour.Height) - ClampedY);
        const uint32_t ClampedW = std::min<uint32_t>(Pane.Width, MaxW);
        const uint32_t ClampedH = std::min<uint32_t>(Pane.Height, MaxH);
        if (ClampedW < 2 || ClampedH < 2) continue;

        const VkViewport Viewport{ float(ClampedX), float(ClampedY), float(ClampedW), float(ClampedH), 0.0f, 1.0f };
        const VkRect2D   Scissor{ { ClampedX, ClampedY }, { ClampedW, ClampedH } };
        vkCmdSetViewport(Commands, 0, 1, &Viewport);
        vkCmdSetScissor(Commands, 0, 1, &Scissor);
        vkCmdBindDescriptorSets(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineLayout, 0, 1, &BufferSet, 0, nullptr);

        // ② surfaces, the grid and image planes behind them, then the edges pulled toward the eye (x-ray: see below)
        SolidArcProjectionBlock Projection;
        Narrow(Frame.ClipFromWorld, Projection.ClipFromWorld);
        Narrow(Frame.Right, Projection.ViewRight, Frame.OrthographicEnabled ? 1.0f : 0.0f);
        Narrow(Frame.Up, Projection.ViewUp, Style.ZebraStripes);
        Narrow(Frame.Forward, Projection.ViewForward, Style.XRayEnabled ? std::clamp(Style.XRayOpacity, 0.01f, 1.0f) : 0.0f);
        Projection.DisplayStroke[0] = float(ClampedW);
        Projection.DisplayStroke[1] = float(ClampedH);
        Projection.DisplayStroke[2] = Style.EdgeStroke;
        Projection.DisplayStroke[3] = Style.EdgeDepthMargin;

        const auto DrawSurfaceSpan = [&](VkPipeline Pipeline, uint32_t FirstCorner, uint32_t SpanCount, float OpacityOverride)
        {
            if (!Style.SurfacesEnabled || !SpanCount) return;
            SolidArcProjectionBlock Block = Projection;
            if (OpacityOverride >= 0.0f) Block.ViewForward[3] = OpacityOverride;
            const VkDescriptorSet SurfaceSets[2] = { BufferSet, MatcapSet };
            vkCmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline);
            vkCmdBindDescriptorSets(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, PlaneLayout, 0, 2, SurfaceSets, 0, nullptr);
            vkCmdPushConstants(Commands, PlaneLayout, PushStages, 0, sizeof(Block), &Block);
            vkCmdBindVertexBuffers(Commands, 0, 1, &CornerBuffer.Buffer, &Origin);
            vkCmdDraw(Commands, SpanCount, 1, FirstCorner, 0);
            vkCmdBindDescriptorSets(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineLayout, 0, 1, &BufferSet, 0, nullptr);
        };
        const auto DrawSurfaces = [&](VkPipeline Pipeline) { DrawSurfaceSpan(Pipeline, 0, CornerCount, -1.0f); };
        const auto DrawEdgeSpan = [&](VkPipeline Pipeline, VkPipeline CodesPipeline, float DepthMargin, uint32_t FirstSegment, uint32_t SpanSegments)
        {
            if (!Style.EdgesEnabled || !SpanSegments) return;
            SolidArcProjectionBlock Stroke = Projection;
            Stroke.DisplayStroke[3] = DepthMargin;
            vkCmdPushConstants(Commands, PipelineLayout, PushStages, 0, sizeof(Stroke), &Stroke);
            vkCmdBindVertexBuffers(Commands, 0, 1, &SegmentBuffer.Buffer, &Origin);
            for (VkPipeline Each : { Pipeline, CodesPipeline })
            {
                vkCmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, Each);
                vkCmdDraw(Commands, 6, SpanSegments, 0, FirstSegment);
            }
        };
        const auto DrawEdges = [&](VkPipeline Pipeline, VkPipeline CodesPipeline, float DepthMargin) { DrawEdgeSpan(Pipeline, CodesPipeline, DepthMargin, 0, SegmentCount); };
        const auto DrawLiveCutters = [&]()
        {
            const uint32_t CutterCorners  = CornerCount > SolidCornerCount ? CornerCount - SolidCornerCount : 0u;
            const uint32_t CutterSegments = SegmentCount > SolidSegmentCount ? SegmentCount - SolidSegmentCount : 0u;
            if (CutterCorners > 0)
            {
                DrawSurfaceSpan(CutterDepthPipeline, SolidCornerCount, CutterCorners, 0.34f);
                DrawSurfaceSpan(CutterGhostPipeline, SolidCornerCount, CutterCorners, 0.34f);
            }
            if (CutterSegments > 0)
            {
                DrawEdgeSpan(EdgeOccludedPipeline, EdgeOccludedIdentityPipeline, -float(OccludedMarginPixels * Frame.WorldPerPixel), SolidSegmentCount, CutterSegments);
            }
        };
        const auto DrawPlanes = [&]()
        {
            if (Style.ImagePlanes.empty() || !PlanePipeline) return;
            vkCmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, PlanePipeline);
            vkCmdBindDescriptorSets(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, PlaneLayout, 0, 1, &BufferSet, 0, nullptr);
            for (const SolidArcImagePlaneDraw& Draw : Style.ImagePlanes)
            {
                if (Draw.Picture >= Pictures.size()) continue;
                if (!FacesSolidArcImagePlane(Draw, Frame)) continue;
                SolidArcImagePlaneBlock Block;
                Narrow(Frame.ClipFromWorld, Block.ClipFromWorld);
                Narrow(Draw.Corner, Block.Corner, Draw.Opacity);
                Narrow(Draw.Right, Block.Right, Draw.Mode);
                Narrow(Draw.Down, Block.Down, 0.0f);
                std::memcpy(Block.Crop, Draw.Crop.data(), sizeof(Block.Crop));
                vkCmdBindDescriptorSets(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, PlaneLayout, 1, 1, &Pictures[Draw.Picture].Set, 0, nullptr);
                vkCmdPushConstants(Commands, PlaneLayout, PushStages, 0, sizeof(Block), &Block);
                vkCmdDraw(Commands, 6, 1, 0, 0);
            }
            vkCmdBindDescriptorSets(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineLayout, 0, 1, &BufferSet, 0, nullptr);
        };
        const auto DrawGrid = [&]()
        {
            if (!Style.GridEnabled) return;
            static constexpr int   PlaneAxes[3][3] = { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 2, 0 } };
            static constexpr float AxisColours[3][4] = { { 0.86f, 0.26f, 0.26f, 1.0f }, { 0.30f, 0.80f, 0.34f, 1.0f }, { 0.30f, 0.52f, 0.92f, 1.0f } };
            const int  Plane = std::clamp(Style.GridPlane, 0, 2);
            CameraMatrix PlaneFromClip{};
            for (int Column = 0; Column < 4; ++Column)
            {
                for (int Row = 0; Row < 3; ++Row) PlaneFromClip[Column * 4 + Row] = Frame.WorldFromClip[Column * 4 + PlaneAxes[Plane][Row]];
                PlaneFromClip[Column * 4 + 3] = Frame.WorldFromClip[Column * 4 + 3];
            }
            SolidArcGridBlock Grid;
            Narrow(PlaneFromClip, Grid.PlaneFromClip);
            for (int Channel = 0; Channel < 4; ++Channel)
            {
                Grid.AlongU[Channel] = AxisColours[PlaneAxes[Plane][0]][Channel];
                Grid.AlongV[Channel] = AxisColours[PlaneAxes[Plane][1]][Channel];
            }
            vkCmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, GridPipeline);
            vkCmdPushConstants(Commands, PipelineLayout, PushStages, 0, sizeof(Grid), &Grid);
            vkCmdDraw(Commands, 3, 1, 0, 0);
        };

        if (Style.XRayEnabled)
        {
            DrawPlanes();
            DrawGrid();
            DrawSurfaces(SurfaceDepthPipeline);
            DrawSurfaces(SurfaceGhostPipeline);
            DrawEdges(EdgeOccludedPipeline, EdgeOccludedIdentityPipeline, -float(OccludedMarginPixels * Frame.WorldPerPixel));
            DrawEdges(EdgePipeline, EdgeIdentityPipeline, Style.EdgeDepthMargin);
        }
        else if (Style.SectionEnabled)
        {
            DrawPlanes();
            DrawGrid();
            DrawSurfaceSpan(SurfacePipeline, 0, SolidCornerCount, 0.0f);
            DrawLiveCutters();
            DrawEdges(EdgePipeline, EdgeIdentityPipeline, Style.EdgeDepthMargin);
        }
        else
        {
            DrawSurfaceSpan(SurfacePipeline, 0, SolidCornerCount, 0.0f);
            DrawPlanes();
            DrawGrid();
            DrawLiveCutters();
            DrawEdges(EdgePipeline, EdgeIdentityPipeline, Style.EdgeDepthMargin);
        }

        // ②′ gizmo on a cleared depth inside this pane's scissor (handles occlude each other, never the model)
        if (GizmoCount[PaneIdx] > 0 && GizmoPipeline)
        {
            VkClearAttachment DepthClear{};
            DepthClear.aspectMask              = VK_IMAGE_ASPECT_DEPTH_BIT;
            DepthClear.clearValue.depthStencil = { 1.0f, 0 };
            const VkClearRect PaneClear{ Scissor, 0, 1 };
            vkCmdClearAttachments(Commands, 1, &DepthClear, 1, &PaneClear);
            vkCmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, GizmoPipeline);
            vkCmdPushConstants(Commands, PipelineLayout, PushStages, 0, sizeof(Projection), &Projection);
            vkCmdBindVertexBuffers(Commands, 0, 1, &GizmoBuffer.Buffer, &Origin);
            vkCmdDraw(Commands, GizmoCount[PaneIdx], 1, GizmoFirst[PaneIdx], 0);
        }
    }
    vkCmdEndRendering(Commands);

    // ③ identity probe / full readback, then the colour target → sampled
    TransitionSolidArcImage(Commands, Identity.Image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    static_cast<uint32_t*>(ProbeReadback.Mapped)[0] = 0u;
    if (ProbeX >= 0 && ProbeY >= 0 && uint32_t(ProbeX) < Identity.Width && uint32_t(ProbeY) < Identity.Height)
    {
        VkBufferImageCopy Probe{};
        Probe.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        Probe.imageOffset      = { ProbeX, ProbeY, 0 };
        Probe.imageExtent      = { 1, 1, 1 };
        vkCmdCopyImageToBuffer(Commands, Identity.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ProbeReadback.Buffer, 1, &Probe);
    }
    if (ReadbackCondition)
    {
        VkBufferImageCopy Whole{};
        Whole.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        Whole.imageExtent      = { Colour.Width, Colour.Height, 1 };
        vkCmdCopyImageToBuffer(Commands, Identity.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, IdentityReadback.Buffer, 1, &Whole);
        TransitionSolidArcImage(Commands, Colour.Image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        vkCmdCopyImageToBuffer(Commands, Colour.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ColourReadback.Buffer, 1, &Whole);
        TransitionSolidArcImage(Commands, Colour.Image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    else
    {
        TransitionSolidArcImage(Commands, Colour.Image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
}

uint32_t SolidArcRasterization::ProbedIdentity() const
{
    return ProbeReadback.Mapped ? static_cast<const uint32_t*>(ProbeReadback.Mapped)[0] : 0u;
}

const uint32_t* SolidArcRasterization::ReadbackColour() const
{
    return static_cast<const uint32_t*>(ColourReadback.Mapped);
}

const uint32_t* SolidArcRasterization::ReadbackIdentity() const
{
    return static_cast<const uint32_t*>(IdentityReadback.Mapped);
}

} // namespace Frontier::SolidArc
