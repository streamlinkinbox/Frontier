/*==========================================================================================================================================
                                                   VULKANREADBACK.H
==========================================================================================================================================*/
// 🧩 Explicit-driver lifetime, command submission and real Vulkan image readback for shared rendering validation.

#pragma once
#include "Engine/Render/SurfaceRasterization.h"
#include <functional>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace
{
void AssertRasterCondition(bool Condition, const char *Message)
{
    if (!Condition) throw std::runtime_error(Message);
}
void ConfirmVulkanResult(VkResult Result)
{
    if (Result != VK_SUCCESS) throw std::runtime_error("Vulkan validation operation failed: " + std::to_string(int(Result)));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     EXPLICIT DRIVER LIFETIME
//------------------------------------------------------------------------------------------------------------------------

struct RasterValidationDevice
{
#ifdef _WIN32
    HMODULE Library = nullptr;
#else
    void *Library = nullptr;
#endif
    PFN_vkGetInstanceProcAddr ResolveInstance = nullptr;
    PFN_vkGetDeviceProcAddr ResolveDevice = nullptr;
    VkInstance Instance = VK_NULL_HANDLE;
    VkPhysicalDevice Physical = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    VkQueue Queue = VK_NULL_HANDLE;
    uint32_t QueueFamily = 0;
    VkQueueFlags QueueCapabilities = 0;
    VkPhysicalDeviceMemoryProperties Memory{};
    VkPhysicalDeviceProperties Properties{};
    VkCommandPool Pool = VK_NULL_HANDLE;
    VkCommandBuffer Commands = VK_NULL_HANDLE;
    VkFence Completion = VK_NULL_HANDLE;
    std::function<void()> BeforeSubmission;
    static inline PFN_vkGetDeviceProcAddr DirectDevice = nullptr;
    static inline PFN_vkCreateGraphicsPipelines DirectPipelines = nullptr;
    static inline uint32_t PipelineCreationCount = 0;
    static inline PFN_vkCreateSampler DirectSampler = nullptr;
    static inline uint32_t SamplerCreationCount = 0;

    static VkResult VKAPI_CALL ConstructTrackedSampler(
        VkDevice                   Device,
        const VkSamplerCreateInfo *Declaration,
        const VkAllocationCallbacks *Allocation,
        VkSampler                 *Sampler)
    {
        ++SamplerCreationCount;
        return DirectSampler(Device,Declaration,Allocation,Sampler);
    }

    static VkResult VKAPI_CALL ConstructTrackedPipelines(
        VkDevice                            Device,
        VkPipelineCache                     Cache,
        uint32_t                            Count,
        const VkGraphicsPipelineCreateInfo *Declarations,
        const VkAllocationCallbacks        *Allocation,
        VkPipeline                         *Pipelines)
    {
        PipelineCreationCount += Count;
        return DirectPipelines(Device, Cache, Count, Declarations, Allocation, Pipelines);
    }
    static PFN_vkVoidFunction VKAPI_CALL ResolveTrackedDevice(VkDevice Device, const char *Name)
    {
        if (std::strcmp(Name,"vkCreateGraphicsPipelines") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(ConstructTrackedPipelines);
        if (std::strcmp(Name,"vkCreateSampler") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(ConstructTrackedSampler);
        return DirectDevice(Device,Name);
    }
#define RASTER_VALIDATION_FUNCTIONS(Function) \
    Function(DestroyInstance) Function(GetPhysicalDeviceMemoryProperties) Function(GetPhysicalDeviceFormatProperties) \
    Function(GetPhysicalDeviceProperties) Function(DestroyDevice) Function(DeviceWaitIdle) Function(GetDeviceQueue) \
    Function(CreateCommandPool) Function(DestroyCommandPool) Function(AllocateCommandBuffers) Function(ResetCommandPool) \
    Function(BeginCommandBuffer) Function(EndCommandBuffer) Function(CreateFence) Function(DestroyFence) Function(ResetFences) \
    Function(QueueSubmit) Function(WaitForFences) Function(CreateBuffer) Function(DestroyBuffer) \
    Function(GetBufferMemoryRequirements) Function(AllocateMemory) Function(FreeMemory) Function(BindBufferMemory) \
    Function(MapMemory) Function(UnmapMemory) Function(CmdPipelineBarrier) Function(CmdCopyImageToBuffer)
#define RASTER_VALIDATION_POINTER(Name) PFN_vk##Name Name = nullptr;
    RASTER_VALIDATION_FUNCTIONS(RASTER_VALIDATION_POINTER)
#undef RASTER_VALIDATION_POINTER

    template<typename Function> Function Resolve(const char *Name)
    {
        const auto Address = ResolveInstance(Instance, Name);
        AssertRasterCondition(Address != nullptr, Name);
        return reinterpret_cast<Function>(Address);
    }

    explicit RasterValidationDevice(const char *Driver, uint32_t MinimumApi)
    {
#ifdef _WIN32
        Library = LoadLibraryW(std::filesystem::path(Driver).c_str());
        if (Library) ResolveInstance = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(Library, "vkGetInstanceProcAddr"));
#else
        Library = dlopen(Driver, RTLD_NOW | RTLD_LOCAL);
        if (Library) ResolveInstance = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(Library, "vkGetInstanceProcAddr"));
#endif
        try
        {
            AssertRasterCondition(ResolveInstance != nullptr, "Explicit Vulkan driver failed to load");
            VkApplicationInfo Application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
            Application.pApplicationName = "Frontier CAD raster validation";
            Application.apiVersion = MinimumApi;
            VkInstanceCreateInfo Creation{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
            Creation.pApplicationInfo = &Application;
            ConfirmVulkanResult(Resolve<PFN_vkCreateInstance>("vkCreateInstance")(&Creation, nullptr, &Instance));
#define RASTER_VALIDATION_LOAD(Name) Name = Resolve<PFN_vk##Name>("vk" #Name);
            RASTER_VALIDATION_FUNCTIONS(RASTER_VALIDATION_LOAD)
#undef RASTER_VALIDATION_LOAD
            ResolveDevice = Resolve<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
            const auto Enumerate = Resolve<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");
            uint32_t Count = 0;
            ConfirmVulkanResult(Enumerate(Instance, &Count, nullptr));
            AssertRasterCondition(Count > 0, "No Vulkan device enumerated");
            std::vector<VkPhysicalDevice> Devices(Count);
            ConfirmVulkanResult(Enumerate(Instance, &Count, Devices.data()));
            for (auto Candidate : Devices)
            {
                uint32_t Families = 0;
                const auto Query = Resolve<PFN_vkGetPhysicalDeviceQueueFamilyProperties>("vkGetPhysicalDeviceQueueFamilyProperties");
                Query(Candidate, &Families, nullptr);
                std::vector<VkQueueFamilyProperties> Descriptions(Families);
                Query(Candidate, &Families, Descriptions.data());
                for (uint32_t Index = 0; Index < Families; ++Index)
                    if ((Descriptions[Index].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
                        (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
                    {
                        Physical = Candidate;
                        QueueFamily = Index;
                        QueueCapabilities = Descriptions[Index].queueFlags;
                        break;
                    }
                if (Physical) break;
            }
            AssertRasterCondition(Physical != VK_NULL_HANDLE, "No Vulkan graphics queue");
            GetPhysicalDeviceProperties(Physical, &Properties);
            GetPhysicalDeviceMemoryProperties(Physical, &Memory);
            AssertRasterCondition(Properties.apiVersion >= MinimumApi, "Vulkan device cannot accept the compiled SPIR-V version");
            const float Priority = 1;
            VkDeviceQueueCreateInfo QueueCreation{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            QueueCreation.queueFamilyIndex = QueueFamily;
            QueueCreation.queueCount = 1;
            QueueCreation.pQueuePriorities = &Priority;
            VkDeviceCreateInfo DeviceCreation{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
            DeviceCreation.queueCreateInfoCount = 1;
            DeviceCreation.pQueueCreateInfos = &QueueCreation;
            // 📝 No optional features or device extensions: CAD does not need wide lines, non-solid fill or RT.
            ConfirmVulkanResult(Resolve<PFN_vkCreateDevice>("vkCreateDevice")(Physical, &DeviceCreation, nullptr, &Device));
            DirectDevice = ResolveDevice;
            DirectPipelines = reinterpret_cast<PFN_vkCreateGraphicsPipelines>(ResolveDevice(Device,"vkCreateGraphicsPipelines"));
            AssertRasterCondition(DirectPipelines != nullptr,"Pipeline construction dispatch absent");
            DirectSampler = reinterpret_cast<PFN_vkCreateSampler>(ResolveDevice(Device,"vkCreateSampler"));
            AssertRasterCondition(DirectSampler != nullptr,"Sampler construction dispatch absent");
            GetDeviceQueue(Device, QueueFamily, 0, &Queue);
            VkCommandPoolCreateInfo PoolCreation{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            PoolCreation.queueFamilyIndex = QueueFamily;
            ConfirmVulkanResult(CreateCommandPool(Device, &PoolCreation, nullptr, &Pool));
            VkCommandBufferAllocateInfo CommandCreation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            CommandCreation.commandPool = Pool;
            CommandCreation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            CommandCreation.commandBufferCount = 1;
            ConfirmVulkanResult(AllocateCommandBuffers(Device, &CommandCreation, &Commands));
            VkFenceCreateInfo FenceCreation{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            ConfirmVulkanResult(CreateFence(Device, &FenceCreation, nullptr, &Completion));
        }
        catch (...)
        {
            Finalize(); throw;
        }
    }

    void Finalize() noexcept
    {
        if (Device && DeviceWaitIdle) (void)DeviceWaitIdle(Device);
        if (Completion) DestroyFence(Device, Completion, nullptr);
        if (Pool) DestroyCommandPool(Device, Pool, nullptr);
        if (Device) DestroyDevice(Device, nullptr);
        if (Instance && DestroyInstance) DestroyInstance(Instance, nullptr);
#ifdef _WIN32
        if (Library) FreeLibrary(Library);
#else
        if (Library) dlclose(Library);
#endif
        Completion = VK_NULL_HANDLE;
        Pool = VK_NULL_HANDLE;
        Device = VK_NULL_HANDLE;
        Instance = VK_NULL_HANDLE;
        Library = nullptr;
    }
    ~RasterValidationDevice()
    {
        Finalize();
    }

    Frontier::VulkanRasterAccess QueryAccess() const
    {
        return {Device, Physical, ResolveTrackedDevice, GetPhysicalDeviceMemoryProperties,
                GetPhysicalDeviceFormatProperties, GetPhysicalDeviceProperties, QueueCapabilities};
    }

//------------------------------------------------------------------------------------------------------------------------
//                                                     ACTUAL DEVICE READBACK
//------------------------------------------------------------------------------------------------------------------------

    template<typename ProjectionSpecification, typename... TextureResidence>
    std::vector<unsigned char> ExtractCapture(
        Frontier::SurfaceRasterization &Raster,
        const ProjectionSpecification  &Projection,
        const TextureResidence        &...Textures)
    {
        const auto Extent = Raster.QueryExtent();
        const VkDeviceSize Bytes = VkDeviceSize(Extent.width) * Extent.height * 4;
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
            Raster.ConstructCommands(Commands, Projection, Textures...);
            VkImageMemoryBarrier Image{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            Image.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            Image.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            Image.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            Image.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            Image.srcQueueFamilyIndex = Image.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            Image.image = Raster.QueryImage();
            Image.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               0, 0, nullptr, 0, nullptr, 1, &Image);
            VkBufferImageCopy Region{};
            Region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
            Region.imageExtent = {Extent.width,Extent.height,1};
            CmdCopyImageToBuffer(Commands, Raster.QueryImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Readback, 1, &Region);
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
            Raster.ConfirmSubmission();
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
};
#undef RASTER_VALIDATION_FUNCTIONS

std::vector<uint32_t> DecodeShaderArchive(const std::filesystem::path &Path)
{
    std::ifstream Input(Path, std::ios::binary | std::ios::ate);
    AssertRasterCondition(bool(Input), "Shader file unavailable");
    const auto Bytes = Input.tellg();
    AssertRasterCondition(Bytes >= 20 && Bytes <= 16777216 && Bytes % 4 == 0, "Malformed shader file");
    std::vector<uint32_t> Words(static_cast<size_t>(Bytes) / 4);
    Input.seekg(0);
    Input.read(reinterpret_cast<char *>(Words.data()), Bytes);
    AssertRasterCondition(bool(Input), "Shader read failed");
    return Words;
}

void EncodeRasterCapture(
    const std::filesystem::path       &Path,
    const std::vector<unsigned char>  &Pixels,
    uint32_t                          Width,
    uint32_t                          Height)
{
    std::ofstream Output(Path, std::ios::binary);
    Output << "P6\n" << Width << ' ' << Height << "\n255\n";
    for (size_t Offset = 0; Offset < Pixels.size(); Offset += 4)
        Output.write(reinterpret_cast<const char *>(Pixels.data() + Offset), 3);
    AssertRasterCondition(bool(Output), "Capture write failed");
}

size_t QueryBrightPixelCount(const std::vector<unsigned char> &Pixels)
{
    size_t Count = 0;
    for (size_t Offset = 0; Offset < Pixels.size(); Offset += 4) Count += Pixels[Offset] > 100;
    return Count;
}
} // namespace
