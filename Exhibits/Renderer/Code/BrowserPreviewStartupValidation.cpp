/*==========================================================================================================================================
                                                 BROWSERPREVIEWSTARTUPVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 The Project Browser's start-up rendering as Frontier.exe runs it, from a shader folder as it ships: the four browser archives,
//    BrowserPreviewRendering::Initialize, catalogue thumbnails through ConstructCommands/ConfirmSubmission, and one tile read back.

#include "VulkanReadback.h"
#include "Engine/Render/BrowserPreviewRendering.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace
{
constexpr uint32_t TileWidth = 384, TileHeight = 246; // 📝 BrowserTileWidth/BrowserTileHeight of BrowserPreviewRendering.cpp

//------------------------------------------------------------------------------------------------------------------------
//                                                     TILE READBACK
//------------------------------------------------------------------------------------------------------------------------

// 📝 A confirmed tile's resolve image rests in SHADER_READ_ONLY_OPTIMAL, where the editor's interface samples it.
std::vector<unsigned char> ReadTile(RasterValidationDevice &Device, VkImage Image)
{
    const VkDeviceSize Bytes = VkDeviceSize(TileWidth) * TileHeight * 4;
    VkBuffer Readback = VK_NULL_HANDLE;
    VkDeviceMemory Storage = VK_NULL_HANDLE;
    try
    {
        VkBufferCreateInfo Creation{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        Creation.size = Bytes;
        Creation.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        ConfirmVulkanResult(Device.CreateBuffer(Device.Device, &Creation, nullptr, &Readback));
        VkMemoryRequirements Requirements;
        Device.GetBufferMemoryRequirements(Device.Device, Readback, &Requirements);
        constexpr VkMemoryPropertyFlags Visible = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        uint32_t MemoryIndex = UINT32_MAX;
        for (uint32_t Index = 0; Index < Device.Memory.memoryTypeCount && MemoryIndex == UINT32_MAX; ++Index)
            if ((Requirements.memoryTypeBits & (1u << Index)) && (Device.Memory.memoryTypes[Index].propertyFlags & Visible) == Visible)
                MemoryIndex = Index;
        AssertRasterCondition(MemoryIndex != UINT32_MAX, "No coherent readback memory");
        VkMemoryAllocateInfo Allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        Allocation.allocationSize = Requirements.size;
        Allocation.memoryTypeIndex = MemoryIndex;
        ConfirmVulkanResult(Device.AllocateMemory(Device.Device, &Allocation, nullptr, &Storage));
        ConfirmVulkanResult(Device.BindBufferMemory(Device.Device, Readback, Storage, 0));
        ConfirmVulkanResult(Device.ResetCommandPool(Device.Device, Device.Pool, 0));
        ConfirmVulkanResult(Device.ResetFences(Device.Device, 1, &Device.Completion));
        VkCommandBufferBeginInfo Begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ConfirmVulkanResult(Device.BeginCommandBuffer(Device.Commands, &Begin));
        VkImageMemoryBarrier Transition{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        Transition.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        Transition.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        Transition.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        Transition.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        Transition.srcQueueFamilyIndex = Transition.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        Transition.image = Image;
        Transition.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        Device.CmdPipelineBarrier(Device.Commands, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
            nullptr, 0, nullptr, 1, &Transition);
        VkBufferImageCopy Region{};
        Region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        Region.imageExtent = {TileWidth, TileHeight, 1};
        Device.CmdCopyImageToBuffer(Device.Commands, Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Readback, 1, &Region);
        Transition.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        Transition.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        Transition.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        Transition.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        Device.CmdPipelineBarrier(Device.Commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
            nullptr, 0, nullptr, 1, &Transition);
        VkMemoryBarrier Host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        Host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        Host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        Device.CmdPipelineBarrier(Device.Commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &Host, 0,
            nullptr, 0, nullptr);
        ConfirmVulkanResult(Device.EndCommandBuffer(Device.Commands));
        VkSubmitInfo Submission{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        Submission.commandBufferCount = 1;
        Submission.pCommandBuffers = &Device.Commands;
        ConfirmVulkanResult(Device.QueueSubmit(Device.Queue, 1, &Submission, Device.Completion));
        ConfirmVulkanResult(Device.WaitForFences(Device.Device, 1, &Device.Completion, VK_TRUE, UINT64_MAX));
        void *Mapped = nullptr;
        ConfirmVulkanResult(Device.MapMemory(Device.Device, Storage, 0, Bytes, 0, &Mapped));
        std::vector<unsigned char> Pixels(static_cast<size_t>(Bytes));
        std::memcpy(Pixels.data(), Mapped, Pixels.size());
        Device.UnmapMemory(Device.Device, Storage);
        Device.DestroyBuffer(Device.Device, Readback, nullptr);
        Device.FreeMemory(Device.Device, Storage, nullptr);
        return Pixels;
    }
    catch (...)
    {
        (void)Device.DeviceWaitIdle(Device.Device);
        if (Readback) Device.DestroyBuffer(Device.Device, Readback, nullptr);
        if (Storage) Device.FreeMemory(Device.Device, Storage, nullptr);
        throw;
    }
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     EXECUTED VALIDATION
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char **Arguments)
{
    using namespace Frontier;
    try
    {
        AssertRasterCondition(ArgumentCount == 3 || ArgumentCount == 4,
            "Expected explicit Vulkan driver, the shipped Surface shader folder and an optional evidence folder");
        const std::filesystem::path Shaders(Arguments[2]);

        // ① The archives Frontier.exe opens first (WindowsFrontier.cpp, InitializeVulkan → BrowserPreviewRendering::Initialize).
        uint32_t SpirvVersion = 0;
        for (const char *Entry : {"BrowserPreviewVertex", "BrowserPreviewFragment", "BrowserDraftingVertex", "BrowserDraftingFragment"})
        {
            const auto Words = DecodeShaderArchive(Shaders / (std::string(Entry) + ".spv"));
            AssertRasterCondition(Words[0] == 0x07230203, "Browser archive is not SPIR-V");
            SpirvVersion = std::max(SpirvVersion, Words[1]);
            std::printf("[PASS] %s.spv: %zu bytes, SPIR-V %u.%u\n", Entry, Words.size() * 4, (Words[1] >> 16) & 0xffu,
                (Words[1] >> 8) & 0xffu);
        }

        // ② Initialize decodes the four archives and builds the browser's pipelines on the borrowed device.
        RasterValidationDevice Device(Arguments[1], SpirvVersion > 0x10300 ? VK_API_VERSION_1_2 : VK_API_VERSION_1_1);
        const uint32_t PipelinesBefore = RasterValidationDevice::PipelineCreationCount;
        BrowserPreviewRendering Rendering;
        Rendering.Initialize(Device.QueryAccess(), Shaders);
        const uint32_t Pipelines = RasterValidationDevice::PipelineCreationCount - PipelinesBefore;
        AssertRasterCondition(Pipelines >= 2, "Browser preview and drafting pipelines were not built");
        std::printf("[PASS] Initialize: %u graphics pipelines on %s\n", Pipelines, Device.Properties.deviceName);

        // ③ Thumbnails exactly as the browser requests them (key Token:Revision:View), one tile per submission.
        const auto Catalogue = ConstructBrowserCatalogue();
        AssertRasterCondition(!Catalogue.empty(), "Empty browser catalogue");
        uint64_t Completed = 0, Serial = 0;
        const auto Submit = [&]()
        {
            ++Serial;
            ConfirmVulkanResult(Device.ResetCommandPool(Device.Device, Device.Pool, 0));
            ConfirmVulkanResult(Device.ResetFences(Device.Device, 1, &Device.Completion));
            VkCommandBufferBeginInfo Begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            ConfirmVulkanResult(Device.BeginCommandBuffer(Device.Commands, &Begin));
            Rendering.ConstructCommands(Device.Commands, Completed, Serial);
            ConfirmVulkanResult(Device.EndCommandBuffer(Device.Commands));
            VkSubmitInfo Submission{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            Submission.commandBufferCount = 1;
            Submission.pCommandBuffers = &Device.Commands;
            ConfirmVulkanResult(Device.QueueSubmit(Device.Queue, 1, &Submission, Device.Completion));
            Rendering.ConfirmSubmission();
            ConfirmVulkanResult(Device.WaitForFences(Device.Device, 1, &Device.Completion, VK_TRUE, UINT64_MAX));
            Completed = Serial;
        };
        std::string Inspected;
        const size_t Requested = std::min<size_t>(Catalogue.size(), 4);
        for (size_t Index = 0; Index < Requested; ++Index)
        {
            const auto &Entry = Catalogue[Index];
            const std::string Key = Entry.Token + ":0:0";
            Rendering.RequestThumbnail(Key, ConfigureBrowserPreview(Entry, 0, 0));
            for (int Attempt = 0; Attempt < 4 && !Rendering.QueryThumbnail(Key).Ready; ++Attempt)
            {
                Rendering.RequestThumbnail(Key, ConfigureBrowserPreview(Entry, 0, 0));
                Submit();
            }
            AssertRasterCondition(Rendering.QueryThumbnail(Key).Ready, "A catalogue thumbnail never became ready");
            std::printf("[PASS] thumbnail %s (%s) ready after submission %llu\n", Key.c_str(), Entry.Title.c_str(),
                static_cast<unsigned long long>(Serial));
            if (Inspected.empty()) Inspected = Key;
        }

        // ④ The first tile holds a shaded picture, not a cleared target.
        const auto Pixels = ReadTile(Device, Rendering.QueryThumbnailImage(Inspected));
        std::set<uint32_t> Colours;
        for (size_t Offset = 0; Offset < Pixels.size(); Offset += 4)
            Colours.insert(uint32_t(Pixels[Offset]) | uint32_t(Pixels[Offset + 1]) << 8 | uint32_t(Pixels[Offset + 2]) << 16);
        AssertRasterCondition(Colours.size() >= 32, "The thumbnail read back as a flat image");
        std::printf("[PASS] %s read back: %zu distinct colours in %ux%u\n", Inspected.c_str(), Colours.size(), TileWidth, TileHeight);
        if (ArgumentCount == 4)
        {
            std::filesystem::create_directories(Arguments[3]);
            EncodeRasterCapture(std::filesystem::path(Arguments[3]) / "BrowserStartupThumbnail.ppm", Pixels, TileWidth, TileHeight);
        }
        Rendering.Finalize();
        std::printf("Browser start-up: 0 failures\n");
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::fprintf(stderr, "[FAIL] %s\n", Failure.what());
        return 1;
    }
}
