/*====================================================================================================================================
                                                          SOLIDARCDEVICE.CPP
====================================================================================================================================*/
// 🧩 Vulkan 1.3 device provisioning (volk; system loader or an explicit driver such as SwiftShader), buffers and images

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "UI/SolidArc/Viewport/SolidArcDevice.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Frontier::SolidArc
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void Require(VkResult Result, const char* Label)
{
    if (Result != VK_SUCCESS) throw std::runtime_error(std::string(Label) + " failed (VkResult " + std::to_string(static_cast<int>(Result)) + ")");
}

void* OpenLibrary(const std::string& Path)
{
#if defined(_WIN32)
    return reinterpret_cast<void*>(LoadLibraryA(Path.c_str()));
#else
    return dlopen(Path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void* FindSymbol(void* Library, const char* Name)
{
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(Library), Name));
#else
    return dlsym(Library, Name);
#endif
}

void CloseLibrary(void* Library)
{
    if (!Library) return;
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(Library));
#else
    dlclose(Library);
#endif
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    DEVICE
//------------------------------------------------------------------------------------------------------------------------

SolidArcDevice::~SolidArcDevice()
{
    Close();
}

bool SolidArcDevice::Open(const SolidArcDeviceSettings& Settings, std::string& Refusal)
{
    try
    {
        // ① entry point: an explicit driver (headless captures) or the system loader
        if (!Settings.DriverPath.empty())
        {
            DriverLibrary = OpenLibrary(Settings.DriverPath);
            if (!DriverLibrary) { Refusal = "cannot load the Vulkan driver " + Settings.DriverPath; return false; }
            auto Entry = reinterpret_cast<PFN_vkGetInstanceProcAddr>(FindSymbol(DriverLibrary, "vkGetInstanceProcAddr"));
            if (!Entry) Entry = reinterpret_cast<PFN_vkGetInstanceProcAddr>(FindSymbol(DriverLibrary, "vk_icdGetInstanceProcAddr"));
            if (!Entry) { Refusal = "the driver exports no vkGetInstanceProcAddr"; return false; }
            volkInitializeCustom(Entry);
        }
        else if (volkInitialize() != VK_SUCCESS)
        {
            Refusal = "no Vulkan loader (install a Vulkan driver, or pass --vulkan-driver <ICD library>)";
            return false;
        }
        if (volkGetInstanceVersion() < VK_API_VERSION_1_3) { Refusal = "Vulkan 1.3 instance support required"; return false; }

        // ② instance
        std::vector<const char*> Extensions;
        for (const std::string& Name : Settings.InstanceExtensions) Extensions.push_back(Name.c_str());
        std::vector<const char*> Layers;
        if (Settings.ValidationEnabled)
        {
            uint32_t LayerCount = 0;
            vkEnumerateInstanceLayerProperties(&LayerCount, nullptr);
            std::vector<VkLayerProperties> Available(LayerCount);
            vkEnumerateInstanceLayerProperties(&LayerCount, Available.data());
            for (const VkLayerProperties& Layer : Available)
                if (std::strcmp(Layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) Layers.push_back("VK_LAYER_KHRONOS_validation");
        }
        VkApplicationInfo Application{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
        Application.pApplicationName = "SolidArcWorkspace";
        Application.pEngineName      = "Frontier";
        Application.apiVersion       = ApiVersion;
        VkInstanceCreateInfo InstanceRequest{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        InstanceRequest.pApplicationInfo        = &Application;
        InstanceRequest.enabledExtensionCount   = static_cast<uint32_t>(Extensions.size());
        InstanceRequest.ppEnabledExtensionNames = Extensions.data();
        InstanceRequest.enabledLayerCount       = static_cast<uint32_t>(Layers.size());
        InstanceRequest.ppEnabledLayerNames     = Layers.data();
        Require(vkCreateInstance(&InstanceRequest, nullptr, &Instance), "vkCreateInstance");
        volkLoadInstanceOnly(Instance);

        // ③ physical device: Vulkan 1.3 with a graphics queue; discrete > integrated > others
        uint32_t PhysicalCount = 0;
        vkEnumeratePhysicalDevices(Instance, &PhysicalCount, nullptr);
        std::vector<VkPhysicalDevice> Candidates(PhysicalCount);
        vkEnumeratePhysicalDevices(Instance, &PhysicalCount, Candidates.data());
        int BestScore = -1;
        for (VkPhysicalDevice Candidate : Candidates)
        {
            VkPhysicalDeviceProperties Described;
            vkGetPhysicalDeviceProperties(Candidate, &Described);
            if (Described.apiVersion < VK_API_VERSION_1_3) continue;
            uint32_t FamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(Candidate, &FamilyCount, nullptr);
            std::vector<VkQueueFamilyProperties> Families(FamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(Candidate, &FamilyCount, Families.data());
            for (uint32_t Family = 0; Family < FamilyCount; ++Family)
            {
                if (!(Families[Family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
                const int Score = Described.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 : Described.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
                if (Score > BestScore) { BestScore = Score; Physical = Candidate; QueueFamily = Family; }
                break;
            }
        }
        if (!Physical) { Refusal = "no Vulkan 1.3 device with a graphics queue"; Close(); return false; }
        vkGetPhysicalDeviceProperties(Physical, &Properties);
        vkGetPhysicalDeviceMemoryProperties(Physical, &MemoryLayout);

        // ④ device: dynamic rendering + synchronization2 (1.3 core)
        const float Priority = 1.0f;
        VkDeviceQueueCreateInfo QueueRequest{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        QueueRequest.queueFamilyIndex = QueueFamily;
        QueueRequest.queueCount       = 1;
        QueueRequest.pQueuePriorities = &Priority;
        VkPhysicalDeviceVulkan13Features Features13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        Features13.dynamicRendering = VK_TRUE;
        Features13.synchronization2 = VK_TRUE;
        std::vector<const char*> DeviceExtensions;
        if (Settings.PresentationEnabled) DeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        VkDeviceCreateInfo DeviceRequest{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        DeviceRequest.pNext                   = &Features13;
        DeviceRequest.queueCreateInfoCount    = 1;
        DeviceRequest.pQueueCreateInfos       = &QueueRequest;
        DeviceRequest.enabledExtensionCount   = static_cast<uint32_t>(DeviceExtensions.size());
        DeviceRequest.ppEnabledExtensionNames = DeviceExtensions.data();
        Require(vkCreateDevice(Physical, &DeviceRequest, nullptr, &Device), "vkCreateDevice");
        volkLoadDevice(Device);
        vkGetDeviceQueue(Device, QueueFamily, 0, &Queue);

        VkCommandPoolCreateInfo PoolRequest{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        PoolRequest.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        PoolRequest.queueFamilyIndex = QueueFamily;
        Require(vkCreateCommandPool(Device, &PoolRequest, nullptr, &CommandPool), "vkCreateCommandPool");
        return true;
    }
    catch (const std::exception& Failure)
    {
        Refusal = Failure.what();
        Close();
        return false;
    }
}

void SolidArcDevice::Close()
{
    if (Device)
    {
        vkDeviceWaitIdle(Device);
        if (CommandPool) vkDestroyCommandPool(Device, CommandPool, nullptr);
        vkDestroyDevice(Device, nullptr);
    }
    if (Instance) vkDestroyInstance(Instance, nullptr);
    CommandPool = VK_NULL_HANDLE;
    Device      = VK_NULL_HANDLE;
    Instance    = VK_NULL_HANDLE;
    Physical    = VK_NULL_HANDLE;
    CloseLibrary(DriverLibrary);
    DriverLibrary = nullptr;
}

uint32_t SolidArcDevice::FindMemoryType(uint32_t TypeBits, VkMemoryPropertyFlags Required) const
{
    for (uint32_t Index = 0; Index < MemoryLayout.memoryTypeCount; ++Index)
        if ((TypeBits & (1u << Index)) && (MemoryLayout.memoryTypes[Index].propertyFlags & Required) == Required) return Index;
    throw std::runtime_error("no Vulkan memory type with the required properties");
}

std::string SolidArcDevice::Describe() const
{
    return std::string(Properties.deviceName) + " · Vulkan " + std::to_string(VK_API_VERSION_MAJOR(Properties.apiVersion)) + "." +
           std::to_string(VK_API_VERSION_MINOR(Properties.apiVersion)) + "." + std::to_string(VK_API_VERSION_PATCH(Properties.apiVersion));
}

void SolidArcDevice::SubmitImmediately(const std::function<void(VkCommandBuffer)>& Record) const
{
    VkCommandBufferAllocateInfo Allocation{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    Allocation.commandPool        = CommandPool;
    Allocation.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    Allocation.commandBufferCount = 1;
    VkCommandBuffer Commands = VK_NULL_HANDLE;
    Require(vkAllocateCommandBuffers(Device, &Allocation, &Commands), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo Begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(Commands, &Begin);
    Record(Commands);
    vkEndCommandBuffer(Commands);
    VkFenceCreateInfo FenceRequest{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence Fence = VK_NULL_HANDLE;
    Require(vkCreateFence(Device, &FenceRequest, nullptr, &Fence), "vkCreateFence");
    VkSubmitInfo Submission{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    Submission.commandBufferCount = 1;
    Submission.pCommandBuffers    = &Commands;
    Require(vkQueueSubmit(Queue, 1, &Submission, Fence), "vkQueueSubmit");
    Require(vkWaitForFences(Device, 1, &Fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    vkDestroyFence(Device, Fence, nullptr);
    vkFreeCommandBuffers(Device, CommandPool, 1, &Commands);
}

SolidArcBuffer SolidArcDevice::CreateBuffer(VkDeviceSize Size, VkBufferUsageFlags Usage, bool HostVisibleCondition) const
{
    SolidArcBuffer Buffer;
    Buffer.Size = std::max<VkDeviceSize>(Size, 16);
    VkBufferCreateInfo Request{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    Request.size        = Buffer.Size;
    Request.usage       = Usage;
    Request.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    Require(vkCreateBuffer(Device, &Request, nullptr, &Buffer.Buffer), "vkCreateBuffer");
    VkMemoryRequirements Needs;
    vkGetBufferMemoryRequirements(Device, Buffer.Buffer, &Needs);
    VkMemoryAllocateInfo Allocation{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    Allocation.allocationSize  = Needs.size;
    Allocation.memoryTypeIndex = FindMemoryType(Needs.memoryTypeBits, HostVisibleCondition ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                                                                                           : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Require(vkAllocateMemory(Device, &Allocation, nullptr, &Buffer.Memory), "vkAllocateMemory");
    Require(vkBindBufferMemory(Device, Buffer.Buffer, Buffer.Memory, 0), "vkBindBufferMemory");
    if (HostVisibleCondition) Require(vkMapMemory(Device, Buffer.Memory, 0, VK_WHOLE_SIZE, 0, &Buffer.Mapped), "vkMapMemory");
    return Buffer;
}

void SolidArcDevice::DestroyBuffer(SolidArcBuffer& Buffer) const
{
    if (Buffer.Buffer) vkDestroyBuffer(Device, Buffer.Buffer, nullptr);
    if (Buffer.Memory) vkFreeMemory(Device, Buffer.Memory, nullptr);
    Buffer = {};
}

SolidArcImage SolidArcDevice::CreateImage(uint32_t Width, uint32_t Height, VkFormat Format, VkImageUsageFlags Usage, VkImageAspectFlags Aspect,
                                         uint32_t ArrayLayers, VkSampleCountFlagBits Samples, uint32_t MipLevels) const
{
    SolidArcImage Image;
    Image.Format = Format;
    Image.Width  = Width;
    Image.Height = Height;
    VkImageCreateInfo Request{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    Request.imageType     = VK_IMAGE_TYPE_2D;
    Request.format        = Format;
    Request.extent        = { Width, Height, 1 };
    Request.mipLevels     = MipLevels;
    Request.arrayLayers   = std::max<uint32_t>(ArrayLayers, 1);
    Request.samples       = Samples;
    Request.tiling        = VK_IMAGE_TILING_OPTIMAL;
    Request.usage         = Usage;
    Request.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    Require(vkCreateImage(Device, &Request, nullptr, &Image.Image), "vkCreateImage");
    VkMemoryRequirements Needs;
    vkGetImageMemoryRequirements(Device, Image.Image, &Needs);
    VkMemoryAllocateInfo Allocation{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    Allocation.allocationSize  = Needs.size;
    Allocation.memoryTypeIndex = FindMemoryType(Needs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Require(vkAllocateMemory(Device, &Allocation, nullptr, &Image.Memory), "vkAllocateMemory");
    Require(vkBindImageMemory(Device, Image.Image, Image.Memory, 0), "vkBindImageMemory");
    VkImageViewCreateInfo ViewRequest{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ViewRequest.image            = Image.Image;
    ViewRequest.viewType         = ArrayLayers ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    ViewRequest.format           = Format;
    ViewRequest.subresourceRange = { Aspect, 0, MipLevels, 0, std::max<uint32_t>(ArrayLayers, 1) };
    Require(vkCreateImageView(Device, &ViewRequest, nullptr, &Image.View), "vkCreateImageView");
    return Image;
}

void SolidArcDevice::DestroyImage(SolidArcImage& Image) const
{
    if (Image.View) vkDestroyImageView(Device, Image.View, nullptr);
    if (Image.Image) vkDestroyImage(Device, Image.Image, nullptr);
    if (Image.Memory) vkFreeMemory(Device, Image.Memory, nullptr);
    Image = {};
}

void TransitionSolidArcImage(VkCommandBuffer Commands, VkImage Image, VkImageAspectFlags Aspect, VkImageLayout From, VkImageLayout To, uint32_t MipLevels)
{
    // 📝 Coarse but correct: all commands before → all commands after. The viewport issues a handful per frame.
    VkImageMemoryBarrier2 Barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    Barrier.srcStageMask        = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    Barrier.srcAccessMask       = VK_ACCESS_2_MEMORY_WRITE_BIT;
    Barrier.dstStageMask        = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    Barrier.dstAccessMask       = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    Barrier.oldLayout           = From;
    Barrier.newLayout           = To;
    Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.image               = Image;
    Barrier.subresourceRange    = { Aspect, 0, MipLevels, 0, 1 };
    VkDependencyInfo Dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    Dependency.imageMemoryBarrierCount = 1;
    Dependency.pImageMemoryBarriers    = &Barrier;
    vkCmdPipelineBarrier2(Commands, &Dependency);
}

} // namespace Frontier::SolidArc
