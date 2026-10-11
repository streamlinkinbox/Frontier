//============================================================================================================================================
//                                                       DISTANCEFIELDGISTAGE.CPP
//============================================================================================================================================
// 📦 Records geometry upload, distance construction, radiance propagation and resolve with explicit resource dependencies.

#include "DistanceFieldGIStage.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>

namespace Frontier
{
bool DistanceFieldGIStage::Bring(const DistanceFieldStageInit& Input) noexcept
{
    Destroy();
    Refusal.clear();
    if (!Input.PhysicalDevice || !Input.Device || !Input.Geometry || Input.Geometry->QueryFacets().empty() ||
        Input.Geometry->QueryBranches().empty() || !Input.OutputImageView || !Input.SurfaceImageView || !Input.NormalImageView ||
        !Input.TriangleBuffer || !Input.MaterialBuffer || !Input.InstanceBuffer || !Input.SlabBuffer || !Input.VertexBuffer ||
        !Input.IndexBuffer || !Input.TableSampler || !Input.EnergyLutView || !Input.SheenLutView ||
        !Input.ImageWidth || !Input.ImageHeight || Input.CardResolution < 2u ||
        Input.CardResolution > 32u || Input.VolumeResolution < 8u || Input.VolumeResolution > 64u || !std::isfinite(Input.ClipmapCellSize) ||
        Input.ClipmapCellSize <= 0.0f ||
        (Input.TextureCapacity &&
         (!Input.TextureCount || !Input.TextureViews || !Input.TextureSampler || Input.TextureCount > Input.TextureCapacity)))
    {
        Refusal = "SDF initialization is missing scene resources or has invalid clipmap dimensions";
        return false;
    }
    Initialization = Input;
    try
    {
        VoxelCount             = 3u * Input.VolumeResolution * Input.VolumeResolution * Input.VolumeResolution;
        const uint32_t Cards   = static_cast<uint32_t>(Input.Geometry->QueryFacets().size());
        const uint32_t Columns = static_cast<uint32_t>(std::ceil(std::sqrt(double(Cards))));
        CardWidth              = Columns * Input.CardResolution;
        CardHeight             = ((Cards + Columns - 1u) / Columns) * Input.CardResolution;
        VkPhysicalDeviceProperties Properties{};
        vkGetPhysicalDeviceProperties(Input.PhysicalDevice, &Properties);
        if (CardWidth > Properties.limits.maxImageDimension2D || CardHeight > Properties.limits.maxImageDimension2D ||
            Input.ImageWidth > Properties.limits.maxImageDimension2D || Input.ImageHeight > Properties.limits.maxImageDimension2D ||
            Properties.limits.maxPerStageDescriptorStorageImages < 10u ||
            Input.Geometry->QueryFacets().size() * sizeof(DistanceFieldFacet) > Properties.limits.maxStorageBufferRange ||
            Input.Geometry->QueryBranches().size() * sizeof(DistanceFieldBranch) > Properties.limits.maxStorageBufferRange)
        {
            Refusal = "Surface-card geometry exceeds device image or storage limits";
            Destroy();
            return false;
        }
        if (!Allocate(0u, VkDeviceSize(VoxelCount) * 64u) || !Allocate(3u, 64u) ||
            !Allocate(4u, Input.Geometry->QueryBranches().size() * sizeof(DistanceFieldBranch)) ||
            !Allocate(5u, Input.Geometry->QueryFacets().size() * sizeof(DistanceFieldFacet)) || !ConstructCardImages() ||
            !ConstructPipelines() || !WriteDescriptors())
        {
            Refusal = "SDF allocation, shader pipeline or descriptor creation failed";
            Destroy();
            return false;
        }
        Ready = true;
        return true;
    }
    catch (...)
    {
        Refusal = "SDF host allocation failed";
        Destroy();
        return false;
    }
}

bool DistanceFieldGIStage::Allocate(uint32_t Slot, VkDeviceSize Bytes)
{
    Sizes[Slot] = Bytes;
    VkBufferCreateInfo Information{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    Information.size  = Bytes;
    Information.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (vkCreateBuffer(Initialization.Device, &Information, nullptr, &Buffers[Slot]) != VK_SUCCESS) return false;
    VkMemoryRequirements Requirements{};
    vkGetBufferMemoryRequirements(Initialization.Device, Buffers[Slot], &Requirements);
    uint32_t Type = UINT32_MAX;
    for (uint32_t Index = 0u; Index < Initialization.MemoryProperties.memoryTypeCount; ++Index)
        if ((Requirements.memoryTypeBits & (1u << Index)) &&
            (Initialization.MemoryProperties.memoryTypes[Index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        {
            Type = Index;
            break;
        }
    if (Type == UINT32_MAX) return false;
    VkMemoryAllocateInfo Allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    Allocation.allocationSize  = Requirements.size;
    Allocation.memoryTypeIndex = Type;
    if (vkAllocateMemory(Initialization.Device, &Allocation, nullptr, &Memory[Slot]) != VK_SUCCESS) return false;
    return vkBindBufferMemory(Initialization.Device, Buffers[Slot], Memory[Slot], 0u) == VK_SUCCESS;
}

bool DistanceFieldGIStage::ConstructCardImages()
{
    for (uint32_t Slot = 0u; Slot < 7u; ++Slot)
    {
        VkImageCreateInfo Information{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        Information.imageType = VK_IMAGE_TYPE_2D;
        Information.format    = Slot < 5u ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R16G16B16A16_SFLOAT;
        Information.extent    = Slot < 5u ? VkExtent3D{CardWidth, CardHeight, 1u} :
                                           VkExtent3D{Initialization.ImageWidth, Initialization.ImageHeight, 1u};
        Information.mipLevels = Information.arrayLayers = 1u;
        Information.samples                             = VK_SAMPLE_COUNT_1_BIT;
        Information.tiling                              = VK_IMAGE_TILING_OPTIMAL;
        Information.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (vkCreateImage(Initialization.Device, &Information, nullptr, &CardImages[Slot]) != VK_SUCCESS) return false;
        VkMemoryRequirements Requirements{};
        vkGetImageMemoryRequirements(Initialization.Device, CardImages[Slot], &Requirements);
        uint32_t Selection = UINT32_MAX;
        for (uint32_t Index = 0u; Index < Initialization.MemoryProperties.memoryTypeCount; ++Index)
            if ((Requirements.memoryTypeBits & (1u << Index)) &&
                (Initialization.MemoryProperties.memoryTypes[Index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            {
                Selection = Index;
                break;
            }
        if (Selection == UINT32_MAX) return false;
        VkMemoryAllocateInfo Extent{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        Extent.allocationSize  = Requirements.size;
        Extent.memoryTypeIndex = Selection;
        if (vkAllocateMemory(Initialization.Device, &Extent, nullptr, &CardStorage[Slot]) != VK_SUCCESS ||
            vkBindImageMemory(Initialization.Device, CardImages[Slot], CardStorage[Slot], 0u) != VK_SUCCESS)
            return false;
        VkImageViewCreateInfo View{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        View.image            = CardImages[Slot];
        View.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        View.format           = Information.format;
        View.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
        if (vkCreateImageView(Initialization.Device, &View, nullptr, &CardViews[Slot]) != VK_SUCCESS) return false;
    }
    return true;
}

bool DistanceFieldGIStage::ConstructPipelines()
{
    std::vector<VkDescriptorSetLayoutBinding> Descriptors;
    auto                                      Append = [&](uint32_t Slot, VkDescriptorType Type, uint32_t Count = 1u)
    { Descriptors.push_back({Slot, Type, Count, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}); };
    for (uint32_t Slot : {0u, 1u, 2u, 4u, 8u, 9u, 10u, 11u, 21u, 22u})
        Append(Slot, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    for (uint32_t Slot : {3u, 5u, 6u, 7u, 15u, 16u, 17u, 18u, 19u, 20u})
        Append(Slot, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    Append(13u, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    Append(14u, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    Append(31u, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, std::max(1u, Initialization.TextureCapacity));
    std::vector<VkDescriptorBindingFlags> Flags(Descriptors.size(), 0u);
    if (Initialization.TextureCapacity)
        Flags.back() = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    if (Initialization.TextureCapacity && Initialization.TextureUpdateAfterBind) Flags.back() |= VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    VkDescriptorSetLayoutBindingFlagsCreateInfo DescriptorFlags{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    DescriptorFlags.bindingCount  = static_cast<uint32_t>(Flags.size());
    DescriptorFlags.pBindingFlags = Flags.data();
    VkDescriptorSetLayoutCreateInfo Layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    Layout.pNext        = Initialization.TextureCapacity ? &DescriptorFlags : nullptr;
    Layout.flags        = Initialization.TextureCapacity && Initialization.TextureUpdateAfterBind
                              ? VkDescriptorSetLayoutCreateFlags(VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT)
                              : 0u;
    Layout.bindingCount = static_cast<uint32_t>(Descriptors.size());
    Layout.pBindings    = Descriptors.data();
    if (vkCreateDescriptorSetLayout(Initialization.Device, &Layout, nullptr, &DescriptorLayout) != VK_SUCCESS) return false;
    VkPushConstantRange        Push{VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(DistanceFieldPush)};
    VkPipelineLayoutCreateInfo PipelineInformation{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    PipelineInformation.setLayoutCount         = 1u;
    PipelineInformation.pSetLayouts            = &DescriptorLayout;
    PipelineInformation.pushConstantRangeCount = 1u;
    PipelineInformation.pPushConstantRanges    = &Push;
    if (vkCreatePipelineLayout(Initialization.Device, &PipelineInformation, nullptr, &PipelineLayout) != VK_SUCCESS) return false;
    const char* Names[] = {
        "DistanceFieldConstruct.spv", Initialization.TextureCapacity ? "DistanceFieldCapture.spv" : "DistanceFieldCaptureFixed.spv",
        "DistanceFieldRadiance.spv",
        Initialization.TextureCapacity ? "DistanceFieldGather.spv" : "DistanceFieldGatherFixed.spv",
        Initialization.TextureCapacity ? "DistanceFieldGIResolve.spv" : "DistanceFieldGIResolveFixed.spv"};
    for (uint32_t Index = 0u; Index < 5u; ++Index)
    {
        std::ifstream Stream(Initialization.SpirvDirectory + "/" + Names[Index], std::ios::binary | std::ios::ate);
        if (!Stream) return false;
        const auto Length = Stream.tellg();
        if (Length < 20 || Length % 4 != 0) return false;
        std::vector<uint32_t> Words(static_cast<size_t>(Length) / 4u);
        Stream.seekg(0);
        if (!Stream.read(reinterpret_cast<char*>(Words.data()), Length) || Words[0] != 0x07230203u) return false;
        VkShaderModuleCreateInfo ModuleInformation{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ModuleInformation.codeSize = static_cast<size_t>(Length);
        ModuleInformation.pCode    = Words.data();
        VkShaderModule Module{};
        if (vkCreateShaderModule(Initialization.Device, &ModuleInformation, nullptr, &Module) != VK_SUCCESS) return false;
        VkComputePipelineCreateInfo Pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        Pipeline.stage        = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        Pipeline.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        Pipeline.stage.module = Module;
        Pipeline.stage.pName  = "main";
        Pipeline.layout       = PipelineLayout;
        const auto Result     = vkCreateComputePipelines(Initialization.Device, VK_NULL_HANDLE, 1u, &Pipeline, nullptr, &Pipelines[Index]);
        vkDestroyShaderModule(Initialization.Device, Module, nullptr);
        if (Result != VK_SUCCESS) return false;
    }
    return true;
}

bool DistanceFieldGIStage::WriteDescriptors()
{
    const uint32_t             Capacity = std::max(1u, Initialization.TextureCapacity);
    VkDescriptorPoolSize       Sizes[]  = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 20u},
                                           {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 20u},
                                           {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2u * (Capacity + 2u)}};
    VkDescriptorPoolCreateInfo Information{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    Information.maxSets       = 2u;
    Information.flags         = Initialization.TextureCapacity && Initialization.TextureUpdateAfterBind
                                    ? VkDescriptorPoolCreateFlags(VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT)
                                    : 0u;
    Information.poolSizeCount = 3u;
    Information.pPoolSizes    = Sizes;
    if (vkCreateDescriptorPool(Initialization.Device, &Information, nullptr, &Pool) != VK_SUCCESS) return false;
    std::vector<VkDescriptorImageInfo> Textures(Capacity);
    for (uint32_t Index = 0u; Index < Capacity; ++Index)
        Textures[Index] =
            Initialization.TextureCapacity
                ? VkDescriptorImageInfo{Initialization.TextureSampler,
                                        Initialization.TextureViews[std::min(Index, Initialization.TextureCount - 1u)],
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}
                : VkDescriptorImageInfo{Initialization.TableSampler, Initialization.EnergyLutView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    for (uint32_t Cycle = 0u; Cycle < 2u; ++Cycle)
    {
        VkDescriptorSetVariableDescriptorCountAllocateInfo Variable{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO};
        Variable.descriptorSetCount = 1u;
        Variable.pDescriptorCounts  = &Capacity;
        VkDescriptorSetAllocateInfo Allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        Allocation.pNext              = Initialization.TextureCapacity ? &Variable : nullptr;
        Allocation.descriptorPool     = Pool;
        Allocation.descriptorSetCount = 1u;
        Allocation.pSetLayouts        = &DescriptorLayout;
        if (vkAllocateDescriptorSets(Initialization.Device, &Allocation, &Sets[Cycle]) != VK_SUCCESS) return false;
        VkBuffer               Handles[] = {Buffers[0],
                                            Buffers[3],
                                            Buffers[4],
                                            Buffers[5],
                                            Initialization.TriangleBuffer,
                                            Initialization.MaterialBuffer,
                                            Initialization.InstanceBuffer,
                                            Initialization.SlabBuffer,
                                            Initialization.VertexBuffer,
                                            Initialization.IndexBuffer};
        const uint32_t         Slots[]   = {3u, 5u, 6u, 7u, 15u, 16u, 17u, 18u, 19u, 20u};
        VkDescriptorBufferInfo BufferInformation[10]{};
        VkDescriptorImageInfo  Images[] = {
            {VK_NULL_HANDLE, Initialization.OutputImageView, VK_IMAGE_LAYOUT_GENERAL},
            {VK_NULL_HANDLE, Initialization.SurfaceImageView, VK_IMAGE_LAYOUT_GENERAL},
            {VK_NULL_HANDLE, Initialization.NormalImageView, VK_IMAGE_LAYOUT_GENERAL},
            {VK_NULL_HANDLE, CardViews[Cycle], VK_IMAGE_LAYOUT_GENERAL},
            {VK_NULL_HANDLE, CardViews[1u - Cycle], VK_IMAGE_LAYOUT_GENERAL},
            {VK_NULL_HANDLE, CardViews[2], VK_IMAGE_LAYOUT_GENERAL},
            {VK_NULL_HANDLE, CardViews[3], VK_IMAGE_LAYOUT_GENERAL},
            {VK_NULL_HANDLE, CardViews[4], VK_IMAGE_LAYOUT_GENERAL},
            {VK_NULL_HANDLE, CardViews[5], VK_IMAGE_LAYOUT_GENERAL},
            {VK_NULL_HANDLE, CardViews[6], VK_IMAGE_LAYOUT_GENERAL},
            {Initialization.TableSampler, Initialization.EnergyLutView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {Initialization.TableSampler, Initialization.SheenLutView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        std::vector<VkWriteDescriptorSet> Writes;
        for (uint32_t Index = 0u; Index < 10u; ++Index)
        {
            BufferInformation[Index] = {Handles[Index], 0u, VK_WHOLE_SIZE};
            VkWriteDescriptorSet Write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            Write.dstSet          = Sets[Cycle];
            Write.dstBinding      = Slots[Index];
            Write.descriptorCount = 1u;
            Write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            Write.pBufferInfo     = &BufferInformation[Index];
            Writes.push_back(Write);
        }
        const uint32_t ImageSlots[] = {0u, 1u, 2u, 4u, 10u, 8u, 9u, 11u, 21u, 22u, 13u, 14u};
        for (uint32_t Index = 0u; Index < 12u; ++Index)
        {
            VkWriteDescriptorSet Write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            Write.dstSet          = Sets[Cycle];
            Write.dstBinding      = ImageSlots[Index];
            Write.descriptorCount = 1u;
            Write.descriptorType  = Index < 10u ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            Write.pImageInfo      = &Images[Index];
            Writes.push_back(Write);
        }
        VkWriteDescriptorSet Write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        Write.dstSet          = Sets[Cycle];
        Write.dstBinding      = 31u;
        Write.descriptorCount = Capacity;
        Write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        Write.pImageInfo      = Textures.data();
        Writes.push_back(Write);
        vkUpdateDescriptorSets(Initialization.Device, static_cast<uint32_t>(Writes.size()), Writes.data(), 0u, nullptr);
    }
    return true;
}

bool DistanceFieldGIStage::IsReady() const noexcept
{
    return Ready && Initialization.Geometry && !Initialization.Geometry->QueryFacets().empty() &&
           !Initialization.Geometry->QueryBranches().empty() &&
           Initialization.Geometry->QueryFacets().size() * sizeof(DistanceFieldFacet) <= Sizes[5] &&
           Initialization.Geometry->QueryBranches().size() * sizeof(DistanceFieldBranch) <= Sizes[4];
}

bool DistanceFieldGIStage::RecordFrame(VkCommandBuffer Command, const DistanceFieldFrameParams& Frame) noexcept
{
    if (!IsReady() || !Command || !Frame.RenderWidth || !Frame.RenderHeight ||
        Frame.RenderWidth > Initialization.ImageWidth || Frame.RenderHeight > Initialization.ImageHeight) return false;
    for (float Value : Frame.CameraEye)
        if (!std::isfinite(Value)) return false;
    struct GeometryConstants
    {
        float    Origins[12];
        uint32_t Counts[4];
    } Constants{};
    const uint32_t Resolution = Initialization.VolumeResolution;
    for (uint32_t Level = 0u; Level < 3u; ++Level)
    {
        const float Cell = Initialization.ClipmapCellSize * float(1u << Level);
        for (uint32_t Axis = 0u; Axis < 3u; ++Axis)
            Constants.Origins[4u * Level + Axis] = (std::floor(Frame.CameraEye[Axis] / Cell) - float(Resolution / 2u)) * Cell;
        Constants.Origins[4u * Level + 3u] = Cell;
    }
    const auto& Geometry        = *Initialization.Geometry;
    const bool  GeometryChanged = ResidentRevision != Geometry.QueryRevision();
    const bool  Reconstruct     = GeometryChanged || std::memcmp(Origins, Constants.Origins, sizeof(Origins)) != 0;
    Constants.Counts[0]         = Resolution;
    Constants.Counts[1]         = static_cast<uint32_t>(Geometry.QueryBranches().size());
    Constants.Counts[2]         = static_cast<uint32_t>(Geometry.QueryFacets().size());
    Constants.Counts[3]         = Initialization.CardResolution;
    const bool        Capture   = GeometryChanged || ResidentMaterials != Frame.MaterialRevision;
    DistanceFieldPush Push{};
    std::memcpy(Push.Sun, Frame.SunDirection, 12u);
    Push.Sun[3] = Frame.SunRadiance;
    std::memcpy(Push.SunColour, Frame.SunColour, 12u);
    std::memcpy(Push.SkyExposure, Frame.SkyAmbient, 12u);
    Push.SkyExposure[3] = Frame.Exposure;
    std::memcpy(Push.Eye, Frame.CameraEye, 12u);
    Push.Eye[3]      = static_cast<float>(std::min(Initialization.TextureCount, 0xFFFFu));
    Push.Tuning[0]   = std::max(0.01f, Frame.ShadowSoftness);
    Push.Tuning[1]   = std::clamp(Frame.GiBoost, 0.0f, 2.0f);
    const bool Reset = Capture || std::memcmp(PreviousLighting, Push.Sun, sizeof(PreviousLighting)) != 0;
    Push.Tuning[2]   = Reset ? 1.0f : 0.0f;
    Push.Counts[0]   = VoxelCount;
    Push.Counts[1]   = FrameNumber;
    Push.Counts[2]   = Frame.FeatureFlags;
    Push.Counts[3]   = Frame.ReflectionMode;
    Push.RenderExtent[0] = Frame.RenderWidth;
    Push.RenderExtent[1] = Frame.RenderHeight;
    auto Barrier =
        [&](VkPipelineStageFlags Source, VkAccessFlags SourceAccess, VkPipelineStageFlags Destination, VkAccessFlags DestinationAccess)
    {
        VkMemoryBarrier MemoryBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        MemoryBarrier.srcAccessMask = SourceAccess;
        MemoryBarrier.dstAccessMask = DestinationAccess;
        vkCmdPipelineBarrier(Command, Source, Destination, 0u, 1u, &MemoryBarrier, 0u, nullptr, 0u, nullptr);
    };
    // 📝 All stages run on the host's existing compute queue. Include previous reads before overwriting shared volumes.
    Barrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT);
    auto Upload = [&](VkBuffer Buffer, const void* Data, VkDeviceSize Bytes)
    {
        for (VkDeviceSize Offset = 0u; Offset < Bytes; Offset += 65536u)
            vkCmdUpdateBuffer(Command, Buffer, Offset, std::min<VkDeviceSize>(65536u, Bytes - Offset),
                              static_cast<const char*>(Data) + Offset);
    };
    if (GeometryChanged)
    {
        Upload(Buffers[4], Geometry.QueryBranches().data(), Geometry.QueryBranches().size() * sizeof(DistanceFieldBranch));
        Upload(Buffers[5], Geometry.QueryFacets().data(), Geometry.QueryFacets().size() * sizeof(DistanceFieldFacet));
    }
    Upload(Buffers[3], &Constants, sizeof(Constants));
    if (!CardImagesInitialized)
    {
        for (auto Image : CardImages)
        {
            VkImageMemoryBarrier Transition{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            Transition.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
            Transition.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
            Transition.srcQueueFamilyIndex = Transition.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            Transition.dstAccessMask                                        = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            Transition.image                                                = Image;
            Transition.subresourceRange                                     = {VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
            vkCmdPipelineBarrier(Command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u,
                                 &Transition);
        }
    }
    if (Reset)
    {
        VkClearColorValue       Clear{};
        VkImageSubresourceRange Range{VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
        for (uint32_t Slot : {0u, 1u})
            vkCmdClearColorImage(Command, CardImages[Slot], VK_IMAGE_LAYOUT_GENERAL, &Clear, 1u, &Range);
    }
    Barrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    vkCmdBindDescriptorSets(Command, VK_PIPELINE_BIND_POINT_COMPUTE, PipelineLayout, 0u, 1u, &Sets[FrameNumber & 1u], 0u, nullptr);
    vkCmdPushConstants(Command, PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(Push), &Push);
    if (Reconstruct)
    {
        vkCmdBindPipeline(Command, VK_PIPELINE_BIND_POINT_COMPUTE, Pipelines[0]);
        vkCmdDispatch(Command, (VoxelCount + 63u) / 64u, 1u, 1u);
        Barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    }
    if (Capture)
    {
        vkCmdBindPipeline(Command, VK_PIPELINE_BIND_POINT_COMPUTE, Pipelines[1]);
        vkCmdDispatch(Command, (CardWidth + 7u) / 8u, (CardHeight + 7u) / 8u, 1u);
        Barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT);
    }
    vkCmdBindPipeline(Command, VK_PIPELINE_BIND_POINT_COMPUTE, Pipelines[2]);
    vkCmdDispatch(Command, (CardWidth + 7u) / 8u, (CardHeight + 7u) / 8u, 1u);
    Barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT);
    if (Frame.FeatureFlags & 1u)
    {
        vkCmdBindPipeline(Command, VK_PIPELINE_BIND_POINT_COMPUTE, Pipelines[3]);
        vkCmdDispatch(Command, (Frame.RenderWidth + 15u) / 16u, (Frame.RenderHeight + 15u) / 16u, 1u);
        Barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT);
    }
    vkCmdBindPipeline(Command, VK_PIPELINE_BIND_POINT_COMPUTE, Pipelines[4]);
    vkCmdDispatch(Command, (Frame.RenderWidth + 15u) / 16u, (Frame.RenderHeight + 15u) / 16u, 1u);
    Barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    CardImagesInitialized = true;
    ResidentMaterials     = Frame.MaterialRevision;
    ResidentRevision      = Geometry.QueryRevision();
    std::memcpy(Origins, Constants.Origins, sizeof(Origins));
    std::memcpy(PreviousLighting, Push.Sun, sizeof(PreviousLighting));
    ++FrameNumber;
    return true;
}

void DistanceFieldGIStage::Destroy() noexcept
{
    Ready = false;
    if (Initialization.Device)
    {
        for (auto Pipeline : Pipelines)
            if (Pipeline) vkDestroyPipeline(Initialization.Device, Pipeline, nullptr);
        if (Pool) vkDestroyDescriptorPool(Initialization.Device, Pool, nullptr);
        if (PipelineLayout) vkDestroyPipelineLayout(Initialization.Device, PipelineLayout, nullptr);
        if (DescriptorLayout) vkDestroyDescriptorSetLayout(Initialization.Device, DescriptorLayout, nullptr);
        for (uint32_t Slot = 0u; Slot < 7u; ++Slot)
        {
            if (CardViews[Slot]) vkDestroyImageView(Initialization.Device, CardViews[Slot], nullptr);
            if (CardImages[Slot]) vkDestroyImage(Initialization.Device, CardImages[Slot], nullptr);
            if (CardStorage[Slot]) vkFreeMemory(Initialization.Device, CardStorage[Slot], nullptr);
        }
        for (uint32_t Slot = 0u; Slot < 6u; ++Slot)
        {
            if (Buffers[Slot]) vkDestroyBuffer(Initialization.Device, Buffers[Slot], nullptr);
            if (Memory[Slot]) vkFreeMemory(Initialization.Device, Memory[Slot], nullptr);
        }
    }
    std::fill(std::begin(Buffers), std::end(Buffers), VK_NULL_HANDLE);
    std::fill(std::begin(Memory), std::end(Memory), VK_NULL_HANDLE);
    std::fill(std::begin(Sizes), std::end(Sizes), 0u);
    std::fill(std::begin(Pipelines), std::end(Pipelines), VK_NULL_HANDLE);
    std::fill(std::begin(Sets), std::end(Sets), VK_NULL_HANDLE);
    Pool             = VK_NULL_HANDLE;
    PipelineLayout   = VK_NULL_HANDLE;
    DescriptorLayout = VK_NULL_HANDLE;
    std::fill(std::begin(CardImages), std::end(CardImages), VK_NULL_HANDLE);
    std::fill(std::begin(CardViews), std::end(CardViews), VK_NULL_HANDLE);
    std::fill(std::begin(CardStorage), std::end(CardStorage), VK_NULL_HANDLE);
    CardWidth = CardHeight = 0u;
    CardImagesInitialized  = false;
    ResidentMaterials      = UINT64_MAX;
    VoxelCount = FrameNumber = 0u;
    ResidentRevision         = 0u;
    Initialization           = {};
    std::fill(std::begin(Origins), std::end(Origins), 0.0f);
    std::fill(std::begin(PreviousLighting), std::end(PreviousLighting), 0.0f);
}
} // namespace Frontier
