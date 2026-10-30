/*==========================================================================================================================================
                                                           ATLASREADBACK.H
==========================================================================================================================================*/
// 🧩 Exhibit-only GPU atlas extraction; production atlas rendering never maps thumbnail pixels.

#pragma once
#include "Exhibits/Renderer/Code/VulkanReadback.h"
#include <cmath>

namespace
{
struct AtlasValidationDevice : RasterValidationDevice
{
    using RasterValidationDevice::RasterValidationDevice;
    template<typename Recording, typename Confirmation>
    std::vector<unsigned char> Capture(VkImage TargetImage, VkExtent2D Extent, Recording Construct, Confirmation Submitted, uint32_t TexelBytes = 4)
    {
        AssertRasterCondition(TexelBytes == 4 || TexelBytes == 16, "Unsupported GPU readback texel precision.");
        const VkDeviceSize Bytes = VkDeviceSize(Extent.width) * Extent.height * TexelBytes;
        VkBuffer Readback = VK_NULL_HANDLE;
        VkDeviceMemory Storage = VK_NULL_HANDLE;
        try
        {
            VkBufferCreateInfo Creation{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            Creation.size = Bytes;
            Creation.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            ConfirmVulkanResult(CreateBuffer(Device, &Creation, nullptr, &Readback));
            VkMemoryRequirements Requirements;
            GetBufferMemoryRequirements(Device, Readback, &Requirements);
            uint32_t MemoryIndex = UINT32_MAX;
            for (uint32_t Index = 0; Index < Memory.memoryTypeCount; ++Index)
                if ((Requirements.memoryTypeBits & (1u << Index)) &&
                    (Memory.memoryTypes[Index].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                    (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                {
                    MemoryIndex = Index;
                    break;
                }
            AssertRasterCondition(MemoryIndex != UINT32_MAX, "No coherent readback memory");
            VkMemoryAllocateInfo Allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            Allocation.allocationSize = Requirements.size;
            Allocation.memoryTypeIndex = MemoryIndex;
            ConfirmVulkanResult(AllocateMemory(Device, &Allocation, nullptr, &Storage));
            ConfirmVulkanResult(BindBufferMemory(Device, Readback, Storage, 0));
            ConfirmVulkanResult(ResetCommandPool(Device, Pool, 0));
            ConfirmVulkanResult(ResetFences(Device, 1, &Completion));
            VkCommandBufferBeginInfo Begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            ConfirmVulkanResult(BeginCommandBuffer(Commands, &Begin));
            Construct(Commands);
            VkImageMemoryBarrier Image{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            Image.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
            Image.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            Image.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            Image.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            Image.srcQueueFamilyIndex = Image.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            Image.image = TargetImage;
            Image.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               0, 0, nullptr, 0, nullptr, 1, &Image);
            VkBufferImageCopy Region{};
            Region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
            Region.imageExtent = {Extent.width,Extent.height,1};
            CmdCopyImageToBuffer(Commands, TargetImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Readback, 1, &Region);
            Image.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            Image.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            Image.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            Image.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                               0, 0, nullptr, 0, nullptr, 1, &Image);
            VkMemoryBarrier Host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            Host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            Host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                               0, 1, &Host, 0, nullptr, 0, nullptr);
            ConfirmVulkanResult(EndCommandBuffer(Commands));
            if (BeforeSubmission) BeforeSubmission();
            VkSubmitInfo Submission{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            Submission.commandBufferCount = 1;
            Submission.pCommandBuffers = &Commands;
            ConfirmVulkanResult(QueueSubmit(Queue, 1, &Submission, Completion));
            Submitted();
            ConfirmVulkanResult(WaitForFences(Device, 1, &Completion, VK_TRUE, UINT64_MAX));
            void *Mapped = nullptr;
            ConfirmVulkanResult(MapMemory(Device, Storage, 0, Bytes, 0, &Mapped));
            std::vector<unsigned char> Pixels(static_cast<size_t>(Bytes));
            std::memcpy(Pixels.data(), Mapped, Pixels.size());
            UnmapMemory(Device, Storage);
            DestroyBuffer(Device, Readback, nullptr);
            FreeMemory(Device, Storage, nullptr);
            return Pixels;
        }
        catch (...)
        {
            (void)DeviceWaitIdle(Device);
            if (Readback) DestroyBuffer(Device, Readback, nullptr);
            if (Storage) FreeMemory(Device, Storage, nullptr);
            throw;
        }
    }
    template<typename Recording, typename Confirmation>
    std::vector<float> CaptureSurface(VkImage TargetImage, VkExtent2D Extent, Recording Construct, Confirmation Submitted)
    {
        const auto Bytes = Capture(TargetImage, Extent, Construct, Submitted, 16);
        std::vector<float> Components(Bytes.size() / sizeof(float));
        std::memcpy(Components.data(), Bytes.data(), Bytes.size());
        for (const float Component : Components)
            AssertRasterCondition(std::isfinite(Component), "GPU surface readback returned a non-finite value.");
        return Components;
    }
};
}
