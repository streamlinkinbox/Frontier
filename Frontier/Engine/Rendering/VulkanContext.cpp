/*==========================================================================================================================================
                                                            VULKANCONTEXT.CPP
==========================================================================================================================================*/
// 🧩 Pure Vulkan 1.1 dynamic dispatch, headless & GLFW-swapchain device runtime, render-target & pipeline cache,
//    per-frame dynamic UBO + descriptor ring, and Dear ImGui Vulkan presenter. Zero OpenGL dependencies.

#include "Frontier/Engine/Rendering/VulkanContext.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace Frontier
{

void* OpenPlatformDynamicLibrary(const char* LibraryPath) noexcept;
PFN_vkVoidFunction LookupPlatformDynamicSymbol(void* LibraryHandle, const char* SymbolName) noexcept;
void ClosePlatformDynamicLibrary(void* LibraryHandle) noexcept;

namespace
{

template <typename ProcT>
bool ResolveGlobalProc(PFN_vkGetInstanceProcAddr Resolver, const char* Name, ProcT& OutFn) noexcept
{
    OutFn = reinterpret_cast<ProcT>(Resolver(VK_NULL_HANDLE, Name));
    return OutFn != nullptr;
}

template <typename ProcT>
bool ResolveInstanceProc(PFN_vkGetInstanceProcAddr Resolver, VkInstance Inst, const char* Name, ProcT& OutFn) noexcept
{
    OutFn = reinterpret_cast<ProcT>(Resolver(Inst, Name));
    return OutFn != nullptr;
}

template <typename ProcT>
bool ResolveDeviceProc(PFN_vkGetDeviceProcAddr Resolver, VkDevice Dev, const char* Name, ProcT& OutFn) noexcept
{
    OutFn = reinterpret_cast<ProcT>(Resolver(Dev, Name));
    return OutFn != nullptr;
}

void ComputeBarrierStagesAndAccess(VkImageLayout Layout, VkPipelineStageFlags& OutBarrierScope, VkAccessFlags& OutAccess) noexcept
{
    switch (Layout)
    {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            OutBarrierScope  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            OutAccess = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            OutBarrierScope  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            OutAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            OutBarrierScope  = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            OutAccess = VK_ACCESS_SHADER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            OutBarrierScope  = VK_PIPELINE_STAGE_TRANSFER_BIT;
            OutAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            OutBarrierScope  = VK_PIPELINE_STAGE_TRANSFER_BIT;
            OutAccess = VK_ACCESS_TRANSFER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            OutBarrierScope  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            OutAccess = 0u;
            break;
        default:
            OutBarrierScope  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            OutAccess = 0u;
            break;
    }
}

} // namespace

//----------------------------------------------------------------------------------------------------------------------
//                                         DYNAMIC LOADER & INITIALIZATION
//----------------------------------------------------------------------------------------------------------------------

VulkanContext::~VulkanContext()
{
    Shutdown();
}

bool VulkanContext::LoadVulkanLibrary(std::string& OutDiagnostic)
{
#if defined(_WIN32)
    LibraryHandle = OpenPlatformDynamicLibrary("vulkan-1.dll");
    if (!LibraryHandle)
    {
        OutDiagnostic = "Could not load vulkan-1.dll.";
        return false;
    }
    Fn.GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        LookupPlatformDynamicSymbol(LibraryHandle, "vkGetInstanceProcAddr"));
#else
    // Automatically point Vulkan loader at sandbox SwiftShader ICD if system ICD is absent
    if (std::getenv("VK_ICD_FILENAMES") == nullptr && std::getenv("VK_DRIVER_FILES") == nullptr)
    {
        if (!std::filesystem::exists("/usr/share/vulkan/icd.d") &&
            std::filesystem::exists("/tmp/vulkan-icd/vk_swiftshader_icd.json"))
        {
            setenv("VK_ICD_FILENAMES", "/tmp/vulkan-icd/vk_swiftshader_icd.json", 0);
        }
    }

    std::vector<std::string> Candidates;
    if (const char* Custom = std::getenv("FRONTIER_VULKAN_LIB"); Custom && *Custom)
        Candidates.emplace_back(Custom);
    #if defined(__APPLE__)
    Candidates.emplace_back("libvulkan.1.dylib");
    Candidates.emplace_back("libvulkan.dylib");
    Candidates.emplace_back("libMoltenVK.dylib");
    Candidates.emplace_back("/opt/homebrew/lib/libvulkan.1.dylib");
    Candidates.emplace_back("/usr/local/lib/libvulkan.1.dylib");
    #else
    Candidates.emplace_back("libvulkan.so.1");
    Candidates.emplace_back("libvulkan.so");
    Candidates.emplace_back("/tmp/vulkan-icd/libvulkan.so.1");
    #endif

    for (const std::string& Path : Candidates)
    {
        LibraryHandle = OpenPlatformDynamicLibrary(Path.c_str());
        if (LibraryHandle)
            break;
    }
    if (!LibraryHandle)
    {
        OutDiagnostic = "Could not load Vulkan shared library (libvulkan).";
        return false;
    }
    Fn.GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        LookupPlatformDynamicSymbol(LibraryHandle, "vkGetInstanceProcAddr"));
#endif

    if (!Fn.GetInstanceProcAddr)
    {
        OutDiagnostic = "vkGetInstanceProcAddr missing from Vulkan library.";
        return false;
    }

    ResolveGlobalProc(Fn.GetInstanceProcAddr, "vkEnumerateInstanceExtensionProperties", Fn.EnumerateInstanceExtensionProps);
    if (!ResolveGlobalProc(Fn.GetInstanceProcAddr, "vkCreateInstance", Fn.CreateInstance))
    {
        OutDiagnostic = "vkCreateInstance missing.";
        return false;
    }
    return true;
}

bool VulkanContext::InitializeHeadless(std::string& OutDiagnostic)
{
    Shutdown();
    if (!LoadVulkanLibrary(OutDiagnostic))
        return false;

    std::vector<const char*> EnabledInstanceExts;
    VkInstanceCreateFlags InstanceCreateBits = 0u;
    if (Fn.EnumerateInstanceExtensionProps)
    {
        uint32_t ExtCount = 0u;
        Fn.EnumerateInstanceExtensionProps(nullptr, &ExtCount, nullptr);
        std::vector<VkExtensionProperties> Props(ExtCount);
        if (ExtCount > 0u)
            Fn.EnumerateInstanceExtensionProps(nullptr, &ExtCount, Props.data());
        for (const VkExtensionProperties& Ext : Props)
        {
            if (std::strcmp(Ext.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0)
            {
                EnabledInstanceExts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
                InstanceCreateBits |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
            }
        }
    }

    VkApplicationInfo AppInfo{};
    AppInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    AppInfo.pApplicationName   = "Frontier Engine";
    AppInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    AppInfo.pEngineName        = "Frontier";
    AppInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    AppInfo.apiVersion         = VK_API_VERSION_1_1;

    VkInstanceCreateInfo InstInfo{};
    InstInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    InstInfo.flags                   = InstanceCreateBits;
    InstInfo.pApplicationInfo        = &AppInfo;
    InstInfo.enabledExtensionCount   = static_cast<uint32_t>(EnabledInstanceExts.size());
    InstInfo.ppEnabledExtensionNames = EnabledInstanceExts.empty() ? nullptr : EnabledInstanceExts.data();

    if (Fn.CreateInstance(&InstInfo, nullptr, &Instance) != VK_SUCCESS)
    {
        OutDiagnostic = "vkCreateInstance (headless) failed.";
        Shutdown();
        return false;
    }

    if (!CreateDeviceAndCorePools(false, OutDiagnostic))
    {
        Shutdown();
        return false;
    }
    Initialized = true;
    return true;
}

bool VulkanContext::InitializeWithSurface(const char* const* InstanceExtensions, uint32_t ExtensionCount,
                                          SurfaceFactoryFn SurfaceBuilder, void* UserData,
                                          int InitialWidth, int InitialHeight, std::string& OutDiagnostic)
{
    Shutdown();
    if (!LoadVulkanLibrary(OutDiagnostic))
        return false;

    std::vector<const char*> EnabledInstanceExts(InstanceExtensions, InstanceExtensions + ExtensionCount);
    VkInstanceCreateFlags InstanceCreateBits = 0u;

    if (Fn.EnumerateInstanceExtensionProps)
    {
        uint32_t ExtCount = 0u;
        Fn.EnumerateInstanceExtensionProps(nullptr, &ExtCount, nullptr);
        std::vector<VkExtensionProperties> Props(ExtCount);
        if (ExtCount > 0u)
            Fn.EnumerateInstanceExtensionProps(nullptr, &ExtCount, Props.data());
        for (const VkExtensionProperties& Ext : Props)
        {
            if (std::strcmp(Ext.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0)
            {
                EnabledInstanceExts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
                InstanceCreateBits |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
            }
        }
    }

    VkApplicationInfo AppInfo{};
    AppInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    AppInfo.pApplicationName   = "Project Everest";
    AppInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    AppInfo.pEngineName        = "Frontier";
    AppInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    AppInfo.apiVersion         = VK_API_VERSION_1_1;

    VkInstanceCreateInfo InstInfo{};
    InstInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    InstInfo.flags                   = InstanceCreateBits;
    InstInfo.pApplicationInfo        = &AppInfo;
    InstInfo.enabledExtensionCount   = static_cast<uint32_t>(EnabledInstanceExts.size());
    InstInfo.ppEnabledExtensionNames = EnabledInstanceExts.empty() ? nullptr : EnabledInstanceExts.data();

    if (Fn.CreateInstance(&InstInfo, nullptr, &Instance) != VK_SUCCESS)
    {
        OutDiagnostic = "vkCreateInstance (windowed) failed.";
        Shutdown();
        return false;
    }

    ResolveInstanceProc(Fn.GetInstanceProcAddr, Instance, "vkDestroySurfaceKHR", Fn.DestroySurfaceKHR);
    ResolveInstanceProc(Fn.GetInstanceProcAddr, Instance, "vkGetPhysicalDeviceSurfaceSupportKHR", Fn.GetPhysicalDeviceSurfaceSupportKHR);
    ResolveInstanceProc(Fn.GetInstanceProcAddr, Instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR", Fn.GetPhysicalDeviceSurfaceCaps);
    ResolveInstanceProc(Fn.GetInstanceProcAddr, Instance, "vkGetPhysicalDeviceSurfaceFormatsKHR", Fn.GetPhysicalDeviceSurfaceFormatsKHR);
    ResolveInstanceProc(Fn.GetInstanceProcAddr, Instance, "vkGetPhysicalDeviceSurfacePresentModesKHR", Fn.GetPhysicalDevicePresentModes);

    if (!SurfaceBuilder || !SurfaceBuilder(Instance, UserData, Surface) || Surface == VK_NULL_HANDLE)
    {
        OutDiagnostic = "Failed to create Vulkan window surface.";
        Shutdown();
        return false;
    }

    if (!CreateDeviceAndCorePools(true, OutDiagnostic))
    {
        Shutdown();
        return false;
    }
    if (!RebuildSwapchain(std::max(1, InitialWidth), std::max(1, InitialHeight), OutDiagnostic))
    {
        Shutdown();
        return false;
    }
    Initialized = true;
    return true;
}

bool VulkanContext::CreateDeviceAndCorePools(bool RequireSwapchain, std::string& OutDiagnostic)
{
    ResolveInstanceProc(Fn.GetInstanceProcAddr, Instance, "vkDestroyInstance", Fn.DestroyInstance);
    ResolveInstanceProc(Fn.GetInstanceProcAddr, Instance, "vkEnumeratePhysicalDevices", Fn.EnumeratePhysicalDevices);
    ResolveInstanceProc(Fn.GetInstanceProcAddr, Instance, "vkGetPhysicalDeviceProperties", Fn.GetPhysicalDeviceProperties);
    ResolveInstanceProc(Fn.GetInstanceProcAddr, Instance, "vkGetPhysicalDeviceFeatures", Fn.GetPhysicalDeviceFeatures);
    ResolveInstanceProc(Fn.GetInstanceProcAddr, Instance, "vkGetPhysicalDeviceMemoryProperties", Fn.GetPhysicalDeviceMemoryProperties);
    ResolveInstanceProc(Fn.GetInstanceProcAddr, Instance, "vkGetPhysicalDeviceQueueFamilyProperties", Fn.GetPhysicalDeviceQueueFamilies);
    ResolveInstanceProc(Fn.GetInstanceProcAddr, Instance, "vkCreateDevice", Fn.CreateDevice);
    ResolveInstanceProc(Fn.GetInstanceProcAddr, Instance, "vkGetDeviceProcAddr", Fn.GetDeviceProcAddr);

    auto EnumDevExts = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
        Fn.GetInstanceProcAddr(Instance, "vkEnumerateDeviceExtensionProperties"));

    uint32_t GpuCount = 0u;
    if (!Fn.EnumeratePhysicalDevices || Fn.EnumeratePhysicalDevices(Instance, &GpuCount, nullptr) != VK_SUCCESS || GpuCount == 0u)
    {
        OutDiagnostic = "No Vulkan physical devices enumerated.";
        return false;
    }
    std::vector<VkPhysicalDevice> Gpus(GpuCount);
    Fn.EnumeratePhysicalDevices(Instance, &GpuCount, Gpus.data());

    // Score physical devices (Discrete > Integrated > Virtual > CPU/SwiftShader)
    int BestScore = -1;
    for (VkPhysicalDevice Candidate : Gpus)
    {
        VkPhysicalDeviceProperties Props{};
        Fn.GetPhysicalDeviceProperties(Candidate, &Props);

        uint32_t QCount = 0u;
        Fn.GetPhysicalDeviceQueueFamilies(Candidate, &QCount, nullptr);
        std::vector<VkQueueFamilyProperties> QFamilies(QCount);
        Fn.GetPhysicalDeviceQueueFamilies(Candidate, &QCount, QFamilies.data());

        int ChosenFamily = -1;
        for (uint32_t QIdx = 0u; QIdx < QCount; ++QIdx)
        {
            if ((QFamilies[QIdx].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0u)
                continue;
            if (RequireSwapchain && Fn.GetPhysicalDeviceSurfaceSupportKHR)
            {
                VkBool32 PresentSupported = VK_FALSE;
                Fn.GetPhysicalDeviceSurfaceSupportKHR(Candidate, QIdx, Surface, &PresentSupported);
                if (!PresentSupported)
                    continue;
            }
            ChosenFamily = static_cast<int>(QIdx);
            break;
        }
        if (ChosenFamily < 0)
            continue;

        int Score = 10;
        if (Props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)   Score = 1000;
        if (Props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) Score = 500;
        if (Props.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU)    Score = 200;
        if (Score > BestScore)
        {
            BestScore           = Score;
            PhysicalDevice      = Candidate;
            DeviceProps         = Props;
            GraphicsQueueFamily = static_cast<uint32_t>(ChosenFamily);
        }
    }

    if (PhysicalDevice == VK_NULL_HANDLE)
    {
        OutDiagnostic = "No suitable Vulkan graphics queue family found.";
        return false;
    }

    Fn.GetPhysicalDeviceMemoryProperties(PhysicalDevice, &MemoryProps);
    VkPhysicalDeviceFeatures SupportedFeatures{};
    Fn.GetPhysicalDeviceFeatures(PhysicalDevice, &SupportedFeatures);

    VkPhysicalDeviceFeatures EnabledFeatures{};
    if (SupportedFeatures.largePoints)
        EnabledFeatures.largePoints = VK_TRUE;
    if (SupportedFeatures.independentBlend)
        EnabledFeatures.independentBlend = VK_TRUE;

    std::vector<const char*> DeviceExts;
    if (RequireSwapchain)
        DeviceExts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    if (EnumDevExts)
    {
        uint32_t DCount = 0u;
        EnumDevExts(PhysicalDevice, nullptr, &DCount, nullptr);
        std::vector<VkExtensionProperties> DProps(DCount);
        if (DCount > 0u)
            EnumDevExts(PhysicalDevice, nullptr, &DCount, DProps.data());
        for (const VkExtensionProperties& Ext : DProps)
        {
            if (std::strcmp(Ext.extensionName, "VK_KHR_portability_subset") == 0)
                DeviceExts.push_back("VK_KHR_portability_subset");
        }
    }

    const float QueuePriority = 1.0f;
    VkDeviceQueueCreateInfo QueueInfo{};
    QueueInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    QueueInfo.queueFamilyIndex = GraphicsQueueFamily;
    QueueInfo.queueCount       = 1u;
    QueueInfo.pQueuePriorities = &QueuePriority;

    VkDeviceCreateInfo DevInfo{};
    DevInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    DevInfo.queueCreateInfoCount    = 1u;
    DevInfo.pQueueCreateInfos       = &QueueInfo;
    DevInfo.enabledExtensionCount   = static_cast<uint32_t>(DeviceExts.size());
    DevInfo.ppEnabledExtensionNames = DeviceExts.empty() ? nullptr : DeviceExts.data();
    DevInfo.pEnabledFeatures        = &EnabledFeatures;

    if (Fn.CreateDevice(PhysicalDevice, &DevInfo, nullptr, &Device) != VK_SUCCESS)
    {
        OutDiagnostic = "vkCreateDevice failed.";
        return false;
    }

    DeviceSummary = std::string("Vulkan · ") + DeviceProps.deviceName;

    // Populate device procedure table
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkDestroyDevice", Fn.DestroyDevice);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkGetDeviceQueue", Fn.GetDeviceQueue);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkDeviceWaitIdle", Fn.DeviceWaitIdle);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkQueueSubmit", Fn.QueueSubmit);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkQueueWaitIdle", Fn.QueueWaitIdle);

    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkAllocateMemory", Fn.AllocateMemory);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkFreeMemory", Fn.FreeMemory);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkMapMemory", Fn.MapMemory);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkUnmapMemory", Fn.UnmapMemory);

    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCreateBuffer", Fn.CreateBuffer);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkDestroyBuffer", Fn.DestroyBuffer);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkGetBufferMemoryRequirements", Fn.GetBufferMemoryRequirements);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkBindBufferMemory", Fn.BindBufferMemory);

    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCreateImage", Fn.CreateImage);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkDestroyImage", Fn.DestroyImage);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkGetImageMemoryRequirements", Fn.GetImageMemoryRequirements);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkBindImageMemory", Fn.BindImageMemory);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCreateImageView", Fn.CreateImageView);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkDestroyImageView", Fn.DestroyImageView);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCreateSampler", Fn.CreateSampler);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkDestroySampler", Fn.DestroySampler);

    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCreateShaderModule", Fn.CreateShaderModule);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkDestroyShaderModule", Fn.DestroyShaderModule);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCreateDescriptorSetLayout", Fn.CreateDescriptorSetLayout);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkDestroyDescriptorSetLayout", Fn.DestroyDescriptorSetLayout);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCreateDescriptorPool", Fn.CreateDescriptorPool);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkDestroyDescriptorPool", Fn.DestroyDescriptorPool);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkAllocateDescriptorSets", Fn.AllocateDescriptorSets);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkUpdateDescriptorSets", Fn.UpdateDescriptorSets);

    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCreatePipelineLayout", Fn.CreatePipelineLayout);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkDestroyPipelineLayout", Fn.DestroyPipelineLayout);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCreateRenderPass", Fn.CreateRenderPass);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkDestroyRenderPass", Fn.DestroyRenderPass);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCreateFramebuffer", Fn.CreateFramebuffer);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkDestroyFramebuffer", Fn.DestroyFramebuffer);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCreateGraphicsPipelines", Fn.CreateGraphicsPipelines);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkDestroyPipeline", Fn.DestroyPipeline);

    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCreateCommandPool", Fn.CreateCommandPool);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkDestroyCommandPool", Fn.DestroyCommandPool);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkResetCommandPool", Fn.ResetCommandPool);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkAllocateCommandBuffers", Fn.AllocateCommandBuffers);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkBeginCommandBuffer", Fn.BeginCommandBuffer);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkEndCommandBuffer", Fn.EndCommandBuffer);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkResetCommandBuffer", Fn.ResetCommandBuffer);

    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCmdPipelineBarrier", Fn.CmdPipelineBarrier);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCmdBeginRenderPass", Fn.CmdBeginRenderPass);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCmdEndRenderPass", Fn.CmdEndRenderPass);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCmdSetViewport", Fn.CmdSetViewport);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCmdSetScissor", Fn.CmdSetScissor);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCmdBindPipeline", Fn.CmdBindPipeline);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCmdBindDescriptorSets", Fn.CmdBindDescriptorSets);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCmdBindVertexBuffers", Fn.CmdBindVertexBuffers);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCmdBindIndexBuffer", Fn.CmdBindIndexBuffer);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCmdDraw", Fn.CmdDraw);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCmdDrawIndexed", Fn.CmdDrawIndexed);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCmdCopyBufferToImage", Fn.CmdCopyBufferToImage);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCmdCopyImageToBuffer", Fn.CmdCopyImageToBuffer);

    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCreateFence", Fn.CreateFence);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkDestroyFence", Fn.DestroyFence);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkWaitForFences", Fn.WaitForFences);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkResetFences", Fn.ResetFences);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCreateSemaphore", Fn.CreateSemaphore);
    ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkDestroySemaphore", Fn.DestroySemaphore);

    if (RequireSwapchain)
    {
        ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkCreateSwapchainKHR", Fn.CreateSwapchainKHR);
        ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkDestroySwapchainKHR", Fn.DestroySwapchainKHR);
        ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkGetSwapchainImagesKHR", Fn.GetSwapchainImagesKHR);
        ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkAcquireNextImageKHR", Fn.AcquireNextImageKHR);
        ResolveDeviceProc(Fn.GetDeviceProcAddr, Device, "vkQueuePresentKHR", Fn.QueuePresentKHR);
    }

    Fn.GetDeviceQueue(Device, GraphicsQueueFamily, 0u, &GraphicsQueue);

    // Command pool + command buffer + synchronization fence
    VkCommandPoolCreateInfo PoolInfo{};
    PoolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    PoolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    PoolInfo.queueFamilyIndex = GraphicsQueueFamily;
    if (Fn.CreateCommandPool(Device, &PoolInfo, nullptr, &CommandPool) != VK_SUCCESS)
    {
        OutDiagnostic = "vkCreateCommandPool failed.";
        return false;
    }

    VkCommandBufferAllocateInfo CbAlloc{};
    CbAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    CbAlloc.commandPool        = CommandPool;
    CbAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    CbAlloc.commandBufferCount = 1u;
    Fn.AllocateCommandBuffers(Device, &CbAlloc, &WorkCommandBuffer);

    VkFenceCreateInfo FenceInfo{};
    FenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    FenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    Fn.CreateFence(Device, &FenceInfo, nullptr, &WorkFence);

    if (RequireSwapchain)
    {
        VkSemaphoreCreateInfo SemInfo{};
        SemInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        Fn.CreateSemaphore(Device, &SemInfo, nullptr, &ImageAvailableSem);
        Fn.CreateSemaphore(Device, &SemInfo, nullptr, &RenderFinishedSem);
    }

    // Nearest & Linear Clamp Samplers
    VkSamplerCreateInfo SampInfo{};
    SampInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    SampInfo.magFilter    = VK_FILTER_NEAREST;
    SampInfo.minFilter    = VK_FILTER_NEAREST;
    SampInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    SampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SampInfo.maxLod       = 1.0f;
    Fn.CreateSampler(Device, &SampInfo, nullptr, &NearestSampler);

    SampInfo.magFilter = VK_FILTER_LINEAR;
    SampInfo.minFilter = VK_FILTER_LINEAR;
    Fn.CreateSampler(Device, &SampInfo, nullptr, &LinearSampler);

    // Unified DescriptorSetLayout: binding 0 = Dynamic UBO, binding 1..5 = Combined Image Samplers
    std::array<VkDescriptorSetLayoutBinding, 6> Bindings{};
    Bindings[0].binding         = 0u;
    Bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    Bindings[0].descriptorCount = 1u;
    Bindings[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    for (uint32_t Idx = 1u; Idx <= 5u; ++Idx)
    {
        Bindings[Idx].binding         = Idx;
        Bindings[Idx].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        Bindings[Idx].descriptorCount = 1u;
        Bindings[Idx].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo DslInfo{};
    DslInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    DslInfo.bindingCount = static_cast<uint32_t>(Bindings.size());
    DslInfo.pBindings    = Bindings.data();
    Fn.CreateDescriptorSetLayout(Device, &DslInfo, nullptr, &UnifiedSetLayout);

    VkPipelineLayoutCreateInfo PlInfo{};
    PlInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    PlInfo.setLayoutCount = 1u;
    PlInfo.pSetLayouts    = &UnifiedSetLayout;
    Fn.CreatePipelineLayout(Device, &PlInfo, nullptr, &UnifiedPipelineLayout);

    // Dynamic UBO Arena (aligned to minUniformBufferOffsetAlignment, >= 2048 bytes per draw to fit PyroUniformBlock = 1088 B)
    const uint32_t MinAlign = std::max<uint32_t>(16u, static_cast<uint32_t>(DeviceProps.limits.minUniformBufferOffsetAlignment));
    UboSliceStride          = ((2048u + MinAlign - 1u) / MinAlign) * MinAlign;
    if (!CreateHostBuffer(static_cast<VkDeviceSize>(UboSliceStride) * MaxDrawsPerFrame,
                          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, UniformArena, OutDiagnostic))
    {
        return false;
    }

    // Pre-allocate Transient Descriptor Pool & Sets
    std::array<VkDescriptorPoolSize, 2> PoolSizes{};
    PoolSizes[0] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, MaxDrawsPerFrame };
    PoolSizes[1] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MaxDrawsPerFrame * 5u };

    VkDescriptorPoolCreateInfo DpInfo{};
    DpInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    DpInfo.maxSets       = MaxDrawsPerFrame;
    DpInfo.poolSizeCount = static_cast<uint32_t>(PoolSizes.size());
    DpInfo.pPoolSizes    = PoolSizes.data();
    if (Fn.CreateDescriptorPool(Device, &DpInfo, nullptr, &TransientDescPool) != VK_SUCCESS)
    {
        OutDiagnostic = "vkCreateDescriptorPool failed.";
        return false;
    }

    std::vector<VkDescriptorSetLayout> Layouts(MaxDrawsPerFrame, UnifiedSetLayout);
    TransientSets.resize(MaxDrawsPerFrame, VK_NULL_HANDLE);
    VkDescriptorSetAllocateInfo DsAlloc{};
    DsAlloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    DsAlloc.descriptorPool     = TransientDescPool;
    DsAlloc.descriptorSetCount = MaxDrawsPerFrame;
    DsAlloc.pSetLayouts        = Layouts.data();
    if (Fn.AllocateDescriptorSets(Device, &DsAlloc, TransientSets.data()) != VK_SUCCESS)
    {
        OutDiagnostic = "vkAllocateDescriptorSets failed.";
        return false;
    }

    // Create 1x1 black dummy image in SHADER_READ_ONLY_OPTIMAL for unbound texture slots
    if (!CreateImageTarget(1, 1, VK_FORMAT_R8G8B8A8_UNORM, 0u, false, DummyBlackImage, OutDiagnostic))
        return false;
    const uint8_t ZeroPixel[4] = { 0u, 0u, 0u, 0u };
    if (!UploadRgba8Image(DummyBlackImage, ZeroPixel, 4u, OutDiagnostic))
        return false;

    return CreateCanonicalTargets(OutDiagnostic);
}

bool VulkanContext::CreateCanonicalTargets(std::string& OutDiagnostic)
{
    auto BuildColorTarget = [&](const VkFormat* ColorFormats, uint32_t ColorCount,
                             VkAttachmentLoadOp LoadOp, bool WithDepth, bool ClearDepth,
                             VkImageLayout FinalColorLayout) -> VkRenderPass
    {
        std::vector<VkAttachmentDescription> Attachments(ColorCount + (WithDepth ? 1u : 0u));
        std::vector<VkAttachmentReference> ColorRefs(ColorCount);

        for (uint32_t Idx = 0u; Idx < ColorCount; ++Idx)
        {
            Attachments[Idx].format         = ColorFormats[Idx];
            Attachments[Idx].samples        = VK_SAMPLE_COUNT_1_BIT;
            Attachments[Idx].loadOp         = LoadOp;
            Attachments[Idx].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            Attachments[Idx].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            Attachments[Idx].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            Attachments[Idx].initialLayout  = (LoadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
                                            ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                            : VK_IMAGE_LAYOUT_UNDEFINED;
            Attachments[Idx].finalLayout    = FinalColorLayout;

            ColorRefs[Idx].attachment = Idx;
            ColorRefs[Idx].layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        VkAttachmentReference DepthRef{};
        if (WithDepth)
        {
            VkAttachmentDescription& DAtt = Attachments[ColorCount];
            DAtt.format         = VK_FORMAT_D32_SFLOAT;
            DAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
            DAtt.loadOp         = ClearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            DAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            DAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            DAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            DAtt.initialLayout  = ClearDepth ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            DAtt.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            DepthRef.attachment = ColorCount;
            DepthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }

        VkSubpassDescription Sub{};
        Sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        Sub.colorAttachmentCount    = ColorCount;
        Sub.pColorAttachments       = ColorRefs.data();
        Sub.pDepthStencilAttachment = WithDepth ? &DepthRef : nullptr;

        VkSubpassDependency Dep{};
        Dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
        Dep.dstSubpass    = 0u;
        Dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        Dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        Dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        Dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo RpInfo{};
        RpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        RpInfo.attachmentCount = static_cast<uint32_t>(Attachments.size());
        RpInfo.pAttachments    = Attachments.data();
        RpInfo.subpassCount    = 1u;
        RpInfo.pSubpasses      = &Sub;
        RpInfo.dependencyCount = 1u;
        RpInfo.pDependencies   = &Dep;

        VkRenderPass Created = VK_NULL_HANDLE;
        Fn.CreateRenderPass(Device, &RpInfo, nullptr, &Created);
        return Created;
    };

    const VkFormat Fmt16f[2]   = { VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT };
    const VkFormat FmtRgba8[1] = { VK_FORMAT_R8G8B8A8_UNORM };
    const VkFormat FmtWorld[2] = { VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT };

    TargetSingleRgba16f    = BuildColorTarget(Fmt16f,   1u, VK_ATTACHMENT_LOAD_OP_DONT_CARE, false, false, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    TargetDualRgba16f      = BuildColorTarget(Fmt16f,   2u, VK_ATTACHMENT_LOAD_OP_DONT_CARE, false, false, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    TargetSingleRgba8Clear = BuildColorTarget(FmtRgba8, 1u, VK_ATTACHMENT_LOAD_OP_CLEAR,     false, false, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    TargetSingleRgba8Load  = BuildColorTarget(FmtRgba8, 1u, VK_ATTACHMENT_LOAD_OP_LOAD,      false, false, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    TargetWorldMrtClear    = BuildColorTarget(FmtWorld, 2u, VK_ATTACHMENT_LOAD_OP_CLEAR,     true,  true,  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    TargetWorldMrtLoad     = BuildColorTarget(FmtWorld, 2u, VK_ATTACHMENT_LOAD_OP_LOAD,      true,  false, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    if (!TargetSingleRgba16f || !TargetDualRgba16f || !TargetSingleRgba8Clear ||
        !TargetSingleRgba8Load || !TargetWorldMrtClear || !TargetWorldMrtLoad)
    {
        OutDiagnostic = "Failed to create canonical Vulkan target render definitions.";
        return false;
    }
    return true;
}

void VulkanContext::Shutdown() noexcept
{
    if (Device != VK_NULL_HANDLE && Fn.DeviceWaitIdle)
        Fn.DeviceWaitIdle(Device);

    DestroySwapchain();
    DestroyBuffer(StagingBuffer);
    DestroyImageTarget(DummyBlackImage);
    DestroyBuffer(UniformArena);

    auto SafeDestroyRp = [&](VkRenderPass& Rp)
    {
        if (Rp != VK_NULL_HANDLE && Fn.DestroyRenderPass) Fn.DestroyRenderPass(Device, Rp, nullptr);
        Rp = VK_NULL_HANDLE;
    };
    SafeDestroyRp(TargetSwapchainClear);
    SafeDestroyRp(TargetWorldMrtLoad);
    SafeDestroyRp(TargetWorldMrtClear);
    SafeDestroyRp(TargetSingleRgba8Load);
    SafeDestroyRp(TargetSingleRgba8Clear);
    SafeDestroyRp(TargetDualRgba16f);
    SafeDestroyRp(TargetSingleRgba16f);

    if (TransientDescPool != VK_NULL_HANDLE && Fn.DestroyDescriptorPool)
        Fn.DestroyDescriptorPool(Device, TransientDescPool, nullptr);
    TransientDescPool = VK_NULL_HANDLE;
    TransientSets.clear();

    if (UnifiedPipelineLayout != VK_NULL_HANDLE && Fn.DestroyPipelineLayout)
        Fn.DestroyPipelineLayout(Device, UnifiedPipelineLayout, nullptr);
    UnifiedPipelineLayout = VK_NULL_HANDLE;

    if (UnifiedSetLayout != VK_NULL_HANDLE && Fn.DestroyDescriptorSetLayout)
        Fn.DestroyDescriptorSetLayout(Device, UnifiedSetLayout, nullptr);
    UnifiedSetLayout = VK_NULL_HANDLE;

    if (NearestSampler != VK_NULL_HANDLE && Fn.DestroySampler)
        Fn.DestroySampler(Device, NearestSampler, nullptr);
    if (LinearSampler != VK_NULL_HANDLE && Fn.DestroySampler)
        Fn.DestroySampler(Device, LinearSampler, nullptr);
    NearestSampler = VK_NULL_HANDLE;
    LinearSampler  = VK_NULL_HANDLE;

    if (ImageAvailableSem != VK_NULL_HANDLE && Fn.DestroySemaphore)
        Fn.DestroySemaphore(Device, ImageAvailableSem, nullptr);
    if (RenderFinishedSem != VK_NULL_HANDLE && Fn.DestroySemaphore)
        Fn.DestroySemaphore(Device, RenderFinishedSem, nullptr);
    ImageAvailableSem = VK_NULL_HANDLE;
    RenderFinishedSem = VK_NULL_HANDLE;

    if (WorkFence != VK_NULL_HANDLE && Fn.DestroyFence)
        Fn.DestroyFence(Device, WorkFence, nullptr);
    WorkFence = VK_NULL_HANDLE;

    if (CommandPool != VK_NULL_HANDLE && Fn.DestroyCommandPool)
        Fn.DestroyCommandPool(Device, CommandPool, nullptr);
    CommandPool       = VK_NULL_HANDLE;
    WorkCommandBuffer = VK_NULL_HANDLE;

    if (Device != VK_NULL_HANDLE && Fn.DestroyDevice)
        Fn.DestroyDevice(Device, nullptr);
    Device = VK_NULL_HANDLE;

    if (Surface != VK_NULL_HANDLE && Fn.DestroySurfaceKHR)
        Fn.DestroySurfaceKHR(Instance, Surface, nullptr);
    Surface = VK_NULL_HANDLE;

    if (Instance != VK_NULL_HANDLE && Fn.DestroyInstance)
        Fn.DestroyInstance(Instance, nullptr);
    Instance = VK_NULL_HANDLE;

    if (LibraryHandle)
        ClosePlatformDynamicLibrary(LibraryHandle);
    LibraryHandle = nullptr;
    Fn            = VulkanFunctionTable{};
    UiTextureRegistry.clear();
    RecordingActive = false;
    Initialized     = false;
}

void VulkanContext::WaitIdle() const noexcept
{
    if (Device != VK_NULL_HANDLE && Fn.DeviceWaitIdle)
        Fn.DeviceWaitIdle(Device);
}

//----------------------------------------------------------------------------------------------------------------------
//                                       MEMORY, BUFFERS, IMAGES & PIPELINES
//----------------------------------------------------------------------------------------------------------------------

uint32_t VulkanContext::FindMemoryType(uint32_t TypeBits, VkMemoryPropertyFlags RequiredMask) const noexcept
{
    for (uint32_t Idx = 0u; Idx < MemoryProps.memoryTypeCount; ++Idx)
    {
        if ((TypeBits & (1u << Idx)) != 0u &&
            (MemoryProps.memoryTypes[Idx].propertyFlags & RequiredMask) == RequiredMask)
        {
            return Idx;
        }
    }
    return UINT32_MAX;
}

bool VulkanContext::CreateHostBuffer(VkDeviceSize ByteSize, VkBufferUsageFlags Usage,
                                     VulkanBufferResource& OutBuffer, std::string& OutDiag)
{
    DestroyBuffer(OutBuffer);
    VkBufferCreateInfo BufInfo{};
    BufInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    BufInfo.size        = std::max<VkDeviceSize>(16u, ByteSize);
    BufInfo.usage       = Usage;
    BufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (Fn.CreateBuffer(Device, &BufInfo, nullptr, &OutBuffer.Buffer) != VK_SUCCESS)
    {
        OutDiag = "vkCreateBuffer failed.";
        return false;
    }

    VkMemoryRequirements Req{};
    Fn.GetBufferMemoryRequirements(Device, OutBuffer.Buffer, &Req);
    const uint32_t MemType = FindMemoryType(Req.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (MemType == UINT32_MAX)
    {
        OutDiag = "No host-coherent Vulkan memory type found.";
        DestroyBuffer(OutBuffer);
        return false;
    }

    VkMemoryAllocateInfo Alloc{};
    Alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    Alloc.allocationSize  = Req.size;
    Alloc.memoryTypeIndex = MemType;
    if (Fn.AllocateMemory(Device, &Alloc, nullptr, &OutBuffer.Memory) != VK_SUCCESS)
    {
        OutDiag = "vkAllocateMemory (buffer) failed.";
        DestroyBuffer(OutBuffer);
        return false;
    }

    Fn.BindBufferMemory(Device, OutBuffer.Buffer, OutBuffer.Memory, 0u);
    Fn.MapMemory(Device, OutBuffer.Memory, 0u, BufInfo.size, 0u, &OutBuffer.Mapped);
    OutBuffer.Size = BufInfo.size;
    return true;
}

void VulkanContext::DestroyBuffer(VulkanBufferResource& Buf) noexcept
{
    if (Device == VK_NULL_HANDLE)
        return;
    if (Buf.Mapped && Fn.UnmapMemory)
        Fn.UnmapMemory(Device, Buf.Memory);
    if (Buf.Buffer != VK_NULL_HANDLE && Fn.DestroyBuffer)
        Fn.DestroyBuffer(Device, Buf.Buffer, nullptr);
    if (Buf.Memory != VK_NULL_HANDLE && Fn.FreeMemory)
        Fn.FreeMemory(Device, Buf.Memory, nullptr);
    Buf = VulkanBufferResource{};
}

bool VulkanContext::CreateImageTarget(int32_t Width, int32_t Height, VkFormat Format, VkImageUsageFlags ExtraUsage,
                                      bool LinearFilter, VulkanImageTarget& OutTarget, std::string& OutDiag)
{
    DestroyImageTarget(OutTarget);

    const bool IsDepth = (Format == VK_FORMAT_D32_SFLOAT);
    VkImageUsageFlags Usage = ExtraUsage;
    if (IsDepth)
        Usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    else
        Usage |= VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VkImageCreateInfo ImgInfo{};
    ImgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ImgInfo.imageType     = VK_IMAGE_TYPE_2D;
    ImgInfo.format        = Format;
    ImgInfo.extent        = { static_cast<uint32_t>(std::max(1, Width)), static_cast<uint32_t>(std::max(1, Height)), 1u };
    ImgInfo.mipLevels     = 1u;
    ImgInfo.arrayLayers   = 1u;
    ImgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    ImgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ImgInfo.usage         = Usage;
    ImgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ImgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (Fn.CreateImage(Device, &ImgInfo, nullptr, &OutTarget.Image) != VK_SUCCESS)
    {
        OutDiag = "vkCreateImage failed.";
        return false;
    }

    VkMemoryRequirements Req{};
    Fn.GetImageMemoryRequirements(Device, OutTarget.Image, &Req);
    uint32_t MemType = FindMemoryType(Req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (MemType == UINT32_MAX)
        MemType = FindMemoryType(Req.memoryTypeBits, 0u);

    VkMemoryAllocateInfo Alloc{};
    Alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    Alloc.allocationSize  = Req.size;
    Alloc.memoryTypeIndex = MemType;
    if (Fn.AllocateMemory(Device, &Alloc, nullptr, &OutTarget.Memory) != VK_SUCCESS)
    {
        OutDiag = "vkAllocateMemory (image) failed.";
        DestroyImageTarget(OutTarget);
        return false;
    }
    Fn.BindImageMemory(Device, OutTarget.Image, OutTarget.Memory, 0u);

    OutTarget.Aspect = IsDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageViewCreateInfo ViewInfo{};
    ViewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ViewInfo.image                           = OutTarget.Image;
    ViewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    ViewInfo.format                          = Format;
    ViewInfo.subresourceRange.aspectMask     = OutTarget.Aspect;
    ViewInfo.subresourceRange.baseMipLevel   = 0u;
    ViewInfo.subresourceRange.levelCount     = 1u;
    ViewInfo.subresourceRange.baseArrayLayer = 0u;
    ViewInfo.subresourceRange.layerCount     = 1u;

    if (Fn.CreateImageView(Device, &ViewInfo, nullptr, &OutTarget.View) != VK_SUCCESS)
    {
        OutDiag = "vkCreateImageView failed.";
        DestroyImageTarget(OutTarget);
        return false;
    }

    OutTarget.Format         = Format;
    OutTarget.CurrentLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    OutTarget.Width          = std::max(1, Width);
    OutTarget.Height         = std::max(1, Height);
    OutTarget.LinearFiltered = LinearFilter;

    if (!IsDepth)
    {
        // Reuse free registry slot or append
        uint64_t AssignedToken = 0u;
        for (size_t Idx = 0u; Idx < UiTextureRegistry.size(); ++Idx)
        {
            if (UiTextureRegistry[Idx] == nullptr)
            {
                UiTextureRegistry[Idx] = &OutTarget;
                AssignedToken          = static_cast<uint64_t>(Idx + 1u);
                break;
            }
        }
        if (AssignedToken == 0u)
        {
            UiTextureRegistry.push_back(&OutTarget);
            AssignedToken = static_cast<uint64_t>(UiTextureRegistry.size());
        }
        OutTarget.UiToken = AssignedToken;
    }
    return true;
}

void VulkanContext::DestroyImageTarget(VulkanImageTarget& Target) noexcept
{
    if (Target.UiToken > 0u && Target.UiToken <= UiTextureRegistry.size())
        UiTextureRegistry[static_cast<size_t>(Target.UiToken - 1u)] = nullptr;

    if (Device != VK_NULL_HANDLE)
    {
        if (Target.View != VK_NULL_HANDLE && Fn.DestroyImageView)
            Fn.DestroyImageView(Device, Target.View, nullptr);
        if (Target.Image != VK_NULL_HANDLE && Fn.DestroyImage)
            Fn.DestroyImage(Device, Target.Image, nullptr);
        if (Target.Memory != VK_NULL_HANDLE && Fn.FreeMemory)
            Fn.FreeMemory(Device, Target.Memory, nullptr);
    }
    Target = VulkanImageTarget{};
}

VulkanImageTarget* VulkanContext::LookupUiTexture(uint64_t UiToken) noexcept
{
    if (UiToken == 0u || UiToken > UiTextureRegistry.size())
        return &DummyBlackImage;
    VulkanImageTarget* Found = UiTextureRegistry[static_cast<size_t>(UiToken - 1u)];
    return Found ? Found : &DummyBlackImage;
}

namespace
{

uint16_t Float32ToHalf16(float InputScalar) noexcept
{
    uint32_t Bits = 0u;
    std::memcpy(&Bits, &InputScalar, sizeof(uint32_t));
    const uint32_t Sign = (Bits >> 16u) & 0x8000u;
    int32_t Exp         = static_cast<int32_t>((Bits >> 23u) & 0xFFu) - 127 + 15;
    uint32_t Mantissa   = Bits & 0x007FFFFFu;
    if (Exp <= 0)
        return static_cast<uint16_t>(Sign);
    if (Exp >= 31)
        return static_cast<uint16_t>(Sign | 0x7C00u);
    return static_cast<uint16_t>(Sign | (static_cast<uint32_t>(Exp) << 10u) | (Mantissa >> 13u));
}

} // namespace

bool VulkanContext::UploadRgba8Image(VulkanImageTarget& Target, const uint8_t* Pixels, size_t ByteCount, std::string& OutDiag)
{
    if (!Pixels || ByteCount == 0u || Target.Image == VK_NULL_HANDLE)
        return false;
    if (StagingBuffer.Size < ByteCount)
    {
        if (!CreateHostBuffer(ByteCount, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              StagingBuffer, OutDiag))
            return false;
    }
    std::memcpy(StagingBuffer.Mapped, Pixels, ByteCount);

    const bool Nested = RecordingActive;
    VkCommandBuffer Cmd = Nested ? WorkCommandBuffer : BeginFrameCommands();

    TransitionImage(Cmd, Target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy Copy{};
    Copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    Copy.imageSubresource.layerCount = 1u;
    Copy.imageExtent                 = { static_cast<uint32_t>(Target.Width), static_cast<uint32_t>(Target.Height), 1u };
    Fn.CmdCopyBufferToImage(Cmd, StagingBuffer.Buffer, Target.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &Copy);
    TransitionImage(Cmd, Target, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    return Nested ? true : SubmitAndWaitFrameCommands();
}

bool VulkanContext::UploadRgba32fImage(VulkanImageTarget& Target, const float* FloatTexels, size_t FloatCount, std::string& OutDiag)
{
    if (!FloatTexels || FloatCount == 0u || Target.Image == VK_NULL_HANDLE)
        return false;

    // Convert float32 RGBA -> IEEE754 float16 RGBA for VK_FORMAT_R16G16B16A16_SFLOAT targets
    const size_t ByteCount = FloatCount * sizeof(uint16_t);
    if (StagingBuffer.Size < ByteCount)
    {
        if (!CreateHostBuffer(ByteCount, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              StagingBuffer, OutDiag))
            return false;
    }
    auto* DstHalf = static_cast<uint16_t*>(StagingBuffer.Mapped);
    for (size_t Idx = 0u; Idx < FloatCount; ++Idx)
        DstHalf[Idx] = Float32ToHalf16(FloatTexels[Idx]);

    if (RecordingActive)
    {
        // Flush pending work before reusing shared StagingBuffer across multiple uploads in one frame!
        SubmitAndWaitFrameCommands();
        BeginFrameCommands();
    }

    const bool Nested = RecordingActive;
    VkCommandBuffer Cmd = Nested ? WorkCommandBuffer : BeginFrameCommands();

    TransitionImage(Cmd, Target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy Copy{};
    Copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    Copy.imageSubresource.layerCount = 1u;
    Copy.imageExtent                 = { static_cast<uint32_t>(Target.Width), static_cast<uint32_t>(Target.Height), 1u };
    Fn.CmdCopyBufferToImage(Cmd, StagingBuffer.Buffer, Target.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &Copy);
    TransitionImage(Cmd, Target, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    if (Nested)
    {
        SubmitAndWaitFrameCommands();
        BeginFrameCommands();
        return true;
    }
    return SubmitAndWaitFrameCommands();
}

bool VulkanContext::DownloadRgba8Image(VulkanImageTarget& Target, std::vector<uint8_t>& OutTopDownRgba)
{
    if (Target.Image == VK_NULL_HANDLE || Target.Width <= 0 || Target.Height <= 0)
        return false;

    if (RecordingActive)
        SubmitAndWaitFrameCommands();

    const size_t TotalBytes = static_cast<size_t>(Target.Width) * static_cast<size_t>(Target.Height) * 4u;
    std::string Diag;
    if (StagingBuffer.Size < TotalBytes)
    {
        if (!CreateHostBuffer(TotalBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              StagingBuffer, Diag))
            return false;
    }

    const VkImageLayout PreviousLayout = Target.CurrentLayout;
    VkCommandBuffer Cmd = BeginFrameCommands();
    TransitionImage(Cmd, Target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkBufferImageCopy Copy{};
    Copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    Copy.imageSubresource.layerCount = 1u;
    Copy.imageExtent                 = { static_cast<uint32_t>(Target.Width), static_cast<uint32_t>(Target.Height), 1u };
    Fn.CmdCopyImageToBuffer(Cmd, Target.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, StagingBuffer.Buffer, 1u, &Copy);

    if (PreviousLayout != VK_IMAGE_LAYOUT_UNDEFINED)
        TransitionImage(Cmd, Target, PreviousLayout);
    else
        TransitionImage(Cmd, Target, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    if (!SubmitAndWaitFrameCommands())
        return false;

    OutTopDownRgba.resize(TotalBytes);
    std::memcpy(OutTopDownRgba.data(), StagingBuffer.Mapped, TotalBytes);
    return true;
}

VkFramebuffer VulkanContext::CreateFramebuffer(VkRenderPass TargetFormat, const VulkanImageTarget* const* Attachments,
                                               uint32_t AttachmentCount, int32_t Width, int32_t Height)
{
    std::vector<VkImageView> Views(AttachmentCount);
    for (uint32_t Idx = 0u; Idx < AttachmentCount; ++Idx)
        Views[Idx] = Attachments[Idx]->View;

    VkFramebufferCreateInfo FbInfo{};
    FbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    FbInfo.renderPass      = TargetFormat;
    FbInfo.attachmentCount = AttachmentCount;
    FbInfo.pAttachments    = Views.data();
    FbInfo.width           = static_cast<uint32_t>(Width);
    FbInfo.height          = static_cast<uint32_t>(Height);
    FbInfo.layers          = 1u;

    VkFramebuffer Fbo = VK_NULL_HANDLE;
    Fn.CreateFramebuffer(Device, &FbInfo, nullptr, &Fbo);
    return Fbo;
}

void VulkanContext::DestroyFramebuffer(VkFramebuffer& Fbo) noexcept
{
    if (Fbo != VK_NULL_HANDLE && Device != VK_NULL_HANDLE && Fn.DestroyFramebuffer)
        Fn.DestroyFramebuffer(Device, Fbo, nullptr);
    Fbo = VK_NULL_HANDLE;
}

VkPipeline VulkanContext::CreateGraphicsPipeline(const VulkanPipelineSpec& Spec, std::string& OutDiag)
{
    auto MakeModule = [&](const VulkanShaders::SpirvBinaryView& Bin) -> VkShaderModule
    {
        VkShaderModuleCreateInfo ModInfo{};
        ModInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ModInfo.codeSize = Bin.WordCount * sizeof(uint32_t);
        ModInfo.pCode    = Bin.Words;
        VkShaderModule Mod = VK_NULL_HANDLE;
        Fn.CreateShaderModule(Device, &ModInfo, nullptr, &Mod);
        return Mod;
    };

    VkShaderModule VertMod = MakeModule(Spec.VertexShader);
    VkShaderModule FragMod = MakeModule(Spec.FragmentShader);
    if (!VertMod || !FragMod)
    {
        if (VertMod) Fn.DestroyShaderModule(Device, VertMod, nullptr);
        if (FragMod) Fn.DestroyShaderModule(Device, FragMod, nullptr);
        OutDiag = "vkCreateShaderModule failed.";
        return VK_NULL_HANDLE;
    }

    VkPipelineShaderStageCreateInfo ShaderEntries[2]{};
    ShaderEntries[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ShaderEntries[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    ShaderEntries[0].module = VertMod;
    ShaderEntries[0].pName  = "main";

    ShaderEntries[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ShaderEntries[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    ShaderEntries[1].module = FragMod;
    ShaderEntries[1].pName  = "main";

    VkVertexInputBindingDescription BindingDesc{};
    std::vector<VkVertexInputAttributeDescription> Attrs;
    bool HasVertexInput = false;

    if (Spec.VertexFormat == VulkanVertexLayout::FloatSeed1)
    {
        HasVertexInput        = true;
        BindingDesc.binding   = 0u;
        BindingDesc.stride    = sizeof(float);
        BindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        Attrs.push_back({ 0u, 0u, VK_FORMAT_R32_SFLOAT, 0u });
    }
    else if (Spec.VertexFormat == VulkanVertexLayout::WorldFacet9)
    {
        HasVertexInput        = true;
        BindingDesc.binding   = 0u;
        BindingDesc.stride    = sizeof(float) * 9u;
        BindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        Attrs.push_back({ 0u, 0u, VK_FORMAT_R32G32B32_SFLOAT, 0u });
        Attrs.push_back({ 1u, 0u, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3u });
        Attrs.push_back({ 2u, 0u, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 6u });
    }
    else if (Spec.VertexFormat == VulkanVertexLayout::ImGuiVert20)
    {
        HasVertexInput        = true;
        BindingDesc.binding   = 0u;
        BindingDesc.stride    = 20u; // sizeof(ImDrawVert)
        BindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        Attrs.push_back({ 0u, 0u, VK_FORMAT_R32G32_SFLOAT,  0u });
        Attrs.push_back({ 1u, 0u, VK_FORMAT_R32G32_SFLOAT,  8u });
        Attrs.push_back({ 2u, 0u, VK_FORMAT_R8G8B8A8_UNORM, 16u });
    }

    VkPipelineVertexInputStateCreateInfo ViInfo{};
    ViInfo.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    ViInfo.vertexBindingDescriptionCount   = HasVertexInput ? 1u : 0u;
    ViInfo.pVertexBindingDescriptions      = HasVertexInput ? &BindingDesc : nullptr;
    ViInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(Attrs.size());
    ViInfo.pVertexAttributeDescriptions    = Attrs.empty() ? nullptr : Attrs.data();

    VkPipelineInputAssemblyStateCreateInfo IaInfo{};
    IaInfo.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    IaInfo.topology = Spec.Topology;

    VkPipelineViewportStateCreateInfo VpInfo{};
    VpInfo.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    VpInfo.viewportCount = 1u;
    VpInfo.scissorCount  = 1u;

    VkPipelineRasterizationStateCreateInfo RsInfo{};
    RsInfo.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    RsInfo.polygonMode = VK_POLYGON_MODE_FILL;
    RsInfo.cullMode    = Spec.CullBackFaces ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    RsInfo.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    RsInfo.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo MsInfo{};
    MsInfo.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    MsInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo DsInfo{};
    DsInfo.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    DsInfo.depthTestEnable  = Spec.DepthTest  ? VK_TRUE : VK_FALSE;
    DsInfo.depthWriteEnable = Spec.DepthWrite ? VK_TRUE : VK_FALSE;
    DsInfo.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    std::vector<VkPipelineColorBlendAttachmentState> BlendAtts(Spec.ColorAttachments);
    for (uint32_t Idx = 0u; Idx < Spec.ColorAttachments; ++Idx)
    {
        VkPipelineColorBlendAttachmentState& B = BlendAtts[Idx];
        B.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        // Attachment 1 on World MRT is linear depth tOpaque — keep attachment 1 unblended!
        if (Idx == 0u && Spec.Blend == VulkanBlendFormula::PremultipliedAlpha)
        {
            B.blendEnable         = VK_TRUE;
            B.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            B.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            B.colorBlendOp        = VK_BLEND_OP_ADD;
            B.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            B.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            B.alphaBlendOp        = VK_BLEND_OP_ADD;
        }
        else if (Idx == 0u && Spec.Blend == VulkanBlendFormula::StandardAlpha)
        {
            B.blendEnable         = VK_TRUE;
            B.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            B.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            B.colorBlendOp        = VK_BLEND_OP_ADD;
            B.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            B.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            B.alphaBlendOp        = VK_BLEND_OP_ADD;
        }
    }

    VkPipelineColorBlendStateCreateInfo CbInfo{};
    CbInfo.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    CbInfo.attachmentCount = static_cast<uint32_t>(BlendAtts.size());
    CbInfo.pAttachments    = BlendAtts.data();

    const VkDynamicState DynamicEntries[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo DynInfo{};
    DynInfo.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    DynInfo.dynamicStateCount = 2u;
    DynInfo.pDynamicStates    = DynamicEntries;

    VkGraphicsPipelineCreateInfo PipeInfo{};
    PipeInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    PipeInfo.stageCount          = 2u;
    PipeInfo.pStages             = ShaderEntries;
    PipeInfo.pVertexInputState   = &ViInfo;
    PipeInfo.pInputAssemblyState = &IaInfo;
    PipeInfo.pViewportState      = &VpInfo;
    PipeInfo.pRasterizationState = &RsInfo;
    PipeInfo.pMultisampleState   = &MsInfo;
    PipeInfo.pDepthStencilState  = &DsInfo;
    PipeInfo.pColorBlendState    = &CbInfo;
    PipeInfo.pDynamicState       = &DynInfo;
    PipeInfo.layout              = UnifiedPipelineLayout;
    PipeInfo.renderPass          = Spec.CompatibleTarget;
    PipeInfo.subpass             = 0u;

    VkPipeline Created = VK_NULL_HANDLE;
    const VkResult Res = Fn.CreateGraphicsPipelines(Device, VK_NULL_HANDLE, 1u, &PipeInfo, nullptr, &Created);

    Fn.DestroyShaderModule(Device, VertMod, nullptr);
    Fn.DestroyShaderModule(Device, FragMod, nullptr);

    if (Res != VK_SUCCESS)
    {
        OutDiag = "vkCreateGraphicsPipelines failed.";
        return VK_NULL_HANDLE;
    }
    return Created;
}

void VulkanContext::DestroyPipeline(VkPipeline& Pipeline) noexcept
{
    if (Pipeline != VK_NULL_HANDLE && Device != VK_NULL_HANDLE && Fn.DestroyPipeline)
        Fn.DestroyPipeline(Device, Pipeline, nullptr);
    Pipeline = VK_NULL_HANDLE;
}

//----------------------------------------------------------------------------------------------------------------------
//                                   COMMAND RECORDING, BARRIERS & SWAPCHAIN
//----------------------------------------------------------------------------------------------------------------------

void VulkanContext::ResetTransientPools() noexcept
{
    UniformDrawCursor  = 0u;
    TransientSetCursor = 0u;
}

VkCommandBuffer VulkanContext::BeginFrameCommands()
{
    if (RecordingActive)
        return WorkCommandBuffer;

    Fn.WaitForFences(Device, 1u, &WorkFence, VK_TRUE, UINT64_MAX);
    Fn.ResetFences(Device, 1u, &WorkFence);
    Fn.ResetCommandBuffer(WorkCommandBuffer, 0u);
    ResetTransientPools();

    VkCommandBufferBeginInfo BeginInfo{};
    BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Fn.BeginCommandBuffer(WorkCommandBuffer, &BeginInfo);
    RecordingActive = true;
    return WorkCommandBuffer;
}

bool VulkanContext::SubmitAndWaitFrameCommands()
{
    if (!RecordingActive)
        return true;

    Fn.EndCommandBuffer(WorkCommandBuffer);
    RecordingActive = false;

    VkSubmitInfo Submit{};
    Submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    Submit.commandBufferCount = 1u;
    Submit.pCommandBuffers    = &WorkCommandBuffer;

    if (Fn.QueueSubmit(GraphicsQueue, 1u, &Submit, WorkFence) != VK_SUCCESS)
        return false;
    return Fn.WaitForFences(Device, 1u, &WorkFence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
}

void VulkanContext::TransitionImage(VkCommandBuffer Cmd, VulkanImageTarget& Target, VkImageLayout NewLayout)
{
    if (Target.Image == VK_NULL_HANDLE || Target.CurrentLayout == NewLayout)
        return;

    VkPipelineStageFlags SrcBarrierScope = 0u, DstBarrierScope = 0u;
    VkAccessFlags SrcAccess = 0u, DstAccess = 0u;
    ComputeBarrierStagesAndAccess(Target.CurrentLayout, SrcBarrierScope, SrcAccess);
    ComputeBarrierStagesAndAccess(NewLayout, DstBarrierScope, DstAccess);

    VkImageMemoryBarrier Barrier{};
    Barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    Barrier.oldLayout                       = Target.CurrentLayout;
    Barrier.newLayout                       = NewLayout;
    Barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    Barrier.image                           = Target.Image;
    Barrier.subresourceRange.aspectMask     = Target.Aspect;
    Barrier.subresourceRange.baseMipLevel   = 0u;
    Barrier.subresourceRange.levelCount     = 1u;
    Barrier.subresourceRange.baseArrayLayer = 0u;
    Barrier.subresourceRange.layerCount     = 1u;
    Barrier.srcAccessMask                   = SrcAccess;
    Barrier.dstAccessMask                   = DstAccess;

    Fn.CmdPipelineBarrier(Cmd, SrcBarrierScope, DstBarrierScope, 0u, 0u, nullptr, 0u, nullptr, 1u, &Barrier);
    Target.CurrentLayout = NewLayout;
}

void VulkanContext::BeginTarget(VkCommandBuffer Cmd, VkRenderPass TargetFormat, VkFramebuffer Fbo,
                                int32_t Width, int32_t Height, const std::array<float, 4>* ClearColor0,
                                bool ClearDepth)
{
    std::array<VkClearValue, 3> Clears{};
    if (ClearColor0)
    {
        Clears[0].color = { { (*ClearColor0)[0], (*ClearColor0)[1], (*ClearColor0)[2], (*ClearColor0)[3] } };
        Clears[1].color = { { 1.0e6f, 0.0f, 0.0f, 1.0f } };
    }
    if (ClearDepth)
        Clears[2].depthStencil = { 1.0f, 0u };

    VkRenderPassBeginInfo RpBegin{};
    RpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    RpBegin.renderPass        = TargetFormat;
    RpBegin.framebuffer       = Fbo;
    RpBegin.renderArea.offset = { 0, 0 };
    RpBegin.renderArea.extent = { static_cast<uint32_t>(Width), static_cast<uint32_t>(Height) };
    RpBegin.clearValueCount   = 3u;
    RpBegin.pClearValues      = Clears.data();

    Fn.CmdBeginRenderPass(Cmd, &RpBegin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport Vp{ 0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height), 0.0f, 1.0f };
    VkRect2D   Sc{ { 0, 0 }, { static_cast<uint32_t>(Width), static_cast<uint32_t>(Height) } };
    Fn.CmdSetViewport(Cmd, 0u, 1u, &Vp);
    Fn.CmdSetScissor(Cmd, 0u, 1u, &Sc);
}

void VulkanContext::EndTarget(VkCommandBuffer Cmd)
{
    Fn.CmdEndRenderPass(Cmd);
}

bool VulkanContext::BindPipelineAndDescriptors(VkCommandBuffer Cmd, VkPipeline Pipeline,
                                               const void* UniformData, size_t UniformBytes,
                                               VulkanImageTarget* Tex0, VulkanImageTarget* Tex1,
                                               VulkanImageTarget* Tex2, VulkanImageTarget* Tex3,
                                               VulkanImageTarget* Tex4)
{
    if (UniformDrawCursor >= MaxDrawsPerFrame || TransientSetCursor >= MaxDrawsPerFrame)
    {
        // Flush current command buffer if an extraordinary frame exceeds 2560 draws!
        SubmitAndWaitFrameCommands();
        BeginFrameCommands();
    }

    const uint32_t DynOffset = UniformDrawCursor * UboSliceStride;
    ++UniformDrawCursor;
    if (UniformData && UniformBytes > 0u)
    {
        std::memcpy(static_cast<uint8_t*>(UniformArena.Mapped) + DynOffset,
                    UniformData, std::min<size_t>(UniformBytes, UboSliceStride));
    }

    const VkDescriptorSet Set = TransientSets[TransientSetCursor++];

    VkDescriptorBufferInfo BufDesc{};
    BufDesc.buffer = UniformArena.Buffer;
    BufDesc.offset = 0u;
    BufDesc.range  = UboSliceStride;

    VulkanImageTarget* Slots[5] = { Tex0, Tex1, Tex2, Tex3, Tex4 };
    std::array<VkDescriptorImageInfo, 5> ImgDescs{};
    for (int Idx = 0; Idx < 5; ++Idx)
    {
        VulkanImageTarget* Chosen = (Slots[Idx] && Slots[Idx]->View != VK_NULL_HANDLE) ? Slots[Idx] : &DummyBlackImage;
        ImgDescs[static_cast<size_t>(Idx)].sampler     = Chosen->LinearFiltered ? LinearSampler : NearestSampler;
        ImgDescs[static_cast<size_t>(Idx)].imageView   = Chosen->View;
        ImgDescs[static_cast<size_t>(Idx)].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    std::array<VkWriteDescriptorSet, 2> Writes{};
    Writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[0].dstSet          = Set;
    Writes[0].dstBinding      = 0u;
    Writes[0].descriptorCount = 1u;
    Writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    Writes[0].pBufferInfo     = &BufDesc;

    Writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    Writes[1].dstSet          = Set;
    Writes[1].dstBinding      = 1u;
    Writes[1].descriptorCount = 5u;
    Writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    Writes[1].pImageInfo      = ImgDescs.data();

    Fn.UpdateDescriptorSets(Device, static_cast<uint32_t>(Writes.size()), Writes.data(), 0u, nullptr);

    Fn.CmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline);
    Fn.CmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, UnifiedPipelineLayout,
                             0u, 1u, &Set, 1u, &DynOffset);
    return true;
}

bool VulkanContext::RebuildSwapchain(int Width, int Height, std::string& OutDiagnostic)
{
    WaitIdle();
    DestroySwapchain();

    VkSurfaceCapabilitiesKHR Caps{};
    Fn.GetPhysicalDeviceSurfaceCaps(PhysicalDevice, Surface, &Caps);

    uint32_t FmtCount = 0u;
    Fn.GetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, Surface, &FmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> Formats(FmtCount);
    if (FmtCount > 0u)
        Fn.GetPhysicalDeviceSurfaceFormatsKHR(PhysicalDevice, Surface, &FmtCount, Formats.data());

    VkSurfaceFormatKHR ChosenFmt = { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
    if (!Formats.empty())
    {
        ChosenFmt = Formats[0];
        for (const VkSurfaceFormatKHR& F : Formats)
        {
            if ((F.format == VK_FORMAT_B8G8R8A8_UNORM || F.format == VK_FORMAT_R8G8B8A8_UNORM) &&
                F.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                ChosenFmt = F;
                break;
            }
        }
    }
    SwapchainFormat = ChosenFmt.format;

    if (Caps.currentExtent.width != UINT32_MAX)
        SwapchainExtent = Caps.currentExtent;
    else
    {
        SwapchainExtent.width  = std::clamp(static_cast<uint32_t>(Width),  Caps.minImageExtent.width,  Caps.maxImageExtent.width);
        SwapchainExtent.height = std::clamp(static_cast<uint32_t>(Height), Caps.minImageExtent.height, Caps.maxImageExtent.height);
    }

    uint32_t ImageCount = Caps.minImageCount + 1u;
    if (Caps.maxImageCount > 0u && ImageCount > Caps.maxImageCount)
        ImageCount = Caps.maxImageCount;

    VkSwapchainCreateInfoKHR ScInfo{};
    ScInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ScInfo.surface          = Surface;
    ScInfo.minImageCount    = ImageCount;
    ScInfo.imageFormat      = SwapchainFormat;
    ScInfo.imageColorSpace  = ChosenFmt.colorSpace;
    ScInfo.imageExtent      = SwapchainExtent;
    ScInfo.imageArrayLayers = 1u;
    ScInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ScInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ScInfo.preTransform     = Caps.currentTransform;
    ScInfo.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ScInfo.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
    ScInfo.clipped          = VK_TRUE;

    if (Fn.CreateSwapchainKHR(Device, &ScInfo, nullptr, &Swapchain) != VK_SUCCESS)
    {
        OutDiagnostic = "vkCreateSwapchainKHR failed.";
        return false;
    }

    // Create swapchain render target definition if not yet created
    if (TargetSwapchainClear == VK_NULL_HANDLE)
    {
        VkAttachmentDescription Att{};
        Att.format         = SwapchainFormat;
        Att.samples        = VK_SAMPLE_COUNT_1_BIT;
        Att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        Att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        Att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        Att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        Att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        Att.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference Ref{ 0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription Sub{};
        Sub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        Sub.colorAttachmentCount = 1u;
        Sub.pColorAttachments    = &Ref;

        VkRenderPassCreateInfo RpInfo{};
        RpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        RpInfo.attachmentCount = 1u;
        RpInfo.pAttachments    = &Att;
        RpInfo.subpassCount    = 1u;
        RpInfo.pSubpasses      = &Sub;
        Fn.CreateRenderPass(Device, &RpInfo, nullptr, &TargetSwapchainClear);
    }

    uint32_t ActualImages = 0u;
    Fn.GetSwapchainImagesKHR(Device, Swapchain, &ActualImages, nullptr);
    SwapchainImages.resize(ActualImages);
    Fn.GetSwapchainImagesKHR(Device, Swapchain, &ActualImages, SwapchainImages.data());

    SwapchainViews.resize(ActualImages, VK_NULL_HANDLE);
    SwapchainFramebuffers.resize(ActualImages, VK_NULL_HANDLE);
    for (uint32_t Idx = 0u; Idx < ActualImages; ++Idx)
    {
        VkImageViewCreateInfo ViewInfo{};
        ViewInfo.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ViewInfo.image                       = SwapchainImages[Idx];
        ViewInfo.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
        ViewInfo.format                      = SwapchainFormat;
        ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ViewInfo.subresourceRange.levelCount = 1u;
        ViewInfo.subresourceRange.layerCount = 1u;
        Fn.CreateImageView(Device, &ViewInfo, nullptr, &SwapchainViews[Idx]);

        VkFramebufferCreateInfo FbInfo{};
        FbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        FbInfo.renderPass      = TargetSwapchainClear;
        FbInfo.attachmentCount = 1u;
        FbInfo.pAttachments    = &SwapchainViews[Idx];
        FbInfo.width           = SwapchainExtent.width;
        FbInfo.height          = SwapchainExtent.height;
        FbInfo.layers          = 1u;
        Fn.CreateFramebuffer(Device, &FbInfo, nullptr, &SwapchainFramebuffers[Idx]);
    }
    return true;
}

void VulkanContext::DestroySwapchain() noexcept
{
    if (Device == VK_NULL_HANDLE)
        return;
    for (VkFramebuffer Fbo : SwapchainFramebuffers)
        if (Fbo != VK_NULL_HANDLE && Fn.DestroyFramebuffer) Fn.DestroyFramebuffer(Device, Fbo, nullptr);
    for (VkImageView View : SwapchainViews)
        if (View != VK_NULL_HANDLE && Fn.DestroyImageView) Fn.DestroyImageView(Device, View, nullptr);
    SwapchainFramebuffers.clear();
    SwapchainViews.clear();
    SwapchainImages.clear();

    if (Swapchain != VK_NULL_HANDLE && Fn.DestroySwapchainKHR)
        Fn.DestroySwapchainKHR(Device, Swapchain, nullptr);
    Swapchain = VK_NULL_HANDLE;
}

bool VulkanContext::BeginSwapchainFrame(int WindowWidth, int WindowHeight, VkCommandBuffer& OutCmd,
                                        VkFramebuffer& OutSwapFbo, VkExtent2D& OutExtent)
{
    if (WindowWidth <= 0 || WindowHeight <= 0)
        return false;

    std::string Diag;
    if (SwapchainExtent.width != static_cast<uint32_t>(WindowWidth) ||
        SwapchainExtent.height != static_cast<uint32_t>(WindowHeight))
    {
        if (!RebuildSwapchain(WindowWidth, WindowHeight, Diag))
            return false;
    }

    const VkResult Acq = Fn.AcquireNextImageKHR(Device, Swapchain, UINT64_MAX, ImageAvailableSem,
                                                VK_NULL_HANDLE, &ActiveSwapImageIndex);
    if (Acq == VK_ERROR_OUT_OF_DATE_KHR)
    {
        RebuildSwapchain(WindowWidth, WindowHeight, Diag);
        return false;
    }
    if (Acq != VK_SUCCESS && Acq != VK_SUBOPTIMAL_KHR)
        return false;

    OutCmd     = BeginFrameCommands();
    OutSwapFbo = SwapchainFramebuffers[ActiveSwapImageIndex];
    OutExtent  = SwapchainExtent;
    return true;
}

bool VulkanContext::EndAndPresentSwapchainFrame(int WindowWidth, int WindowHeight)
{
    if (!RecordingActive)
        return false;
    Fn.EndCommandBuffer(WorkCommandBuffer);
    RecordingActive = false;

    const VkPipelineStageFlags WaitBarrierScope = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo Submit{};
    Submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    Submit.waitSemaphoreCount   = 1u;
    Submit.pWaitSemaphores      = &ImageAvailableSem;
    Submit.pWaitDstStageMask    = &WaitBarrierScope;
    Submit.commandBufferCount   = 1u;
    Submit.pCommandBuffers      = &WorkCommandBuffer;
    Submit.signalSemaphoreCount = 1u;
    Submit.pSignalSemaphores    = &RenderFinishedSem;

    if (Fn.QueueSubmit(GraphicsQueue, 1u, &Submit, WorkFence) != VK_SUCCESS)
        return false;

    VkPresentInfoKHR Present{};
    Present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    Present.waitSemaphoreCount = 1u;
    Present.pWaitSemaphores    = &RenderFinishedSem;
    Present.swapchainCount     = 1u;
    Present.pSwapchains        = &Swapchain;
    Present.pImageIndices      = &ActiveSwapImageIndex;

    const VkResult Pres = Fn.QueuePresentKHR(GraphicsQueue, &Present);
    Fn.WaitForFences(Device, 1u, &WorkFence, VK_TRUE, UINT64_MAX);

    if (Pres == VK_ERROR_OUT_OF_DATE_KHR || Pres == VK_SUBOPTIMAL_KHR)
    {
        std::string Diag;
        RebuildSwapchain(std::max(1, WindowWidth), std::max(1, WindowHeight), Diag);
    }
    return true;
}

//----------------------------------------------------------------------------------------------------------------------
//                                          DEAR IMGUI VULKAN PRESENTER
//----------------------------------------------------------------------------------------------------------------------

bool VulkanImGuiPresenter::Initialize(VulkanContext& Ctx, VkRenderPass TargetFormat, std::string& OutDiagnostic)
{
    ImGuiIO& Io = ImGui::GetIO();
    Io.BackendRendererName = "Frontier_Vulkan11";
    Io.BackendFlags       |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;

    VulkanPipelineSpec Spec{};
    Spec.VertexShader     = VulkanShaders::ImGuiVulkanVert;
    Spec.FragmentShader   = VulkanShaders::ImGuiVulkanFrag;
    Spec.CompatibleTarget = TargetFormat;
    Spec.ColorAttachments = 1u;
    Spec.VertexFormat     = VulkanVertexLayout::ImGuiVert20;
    Spec.Topology         = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    Spec.Blend            = VulkanBlendFormula::StandardAlpha;
    Spec.DepthTest        = false;
    Spec.DepthWrite       = false;
    Spec.CullBackFaces    = false;

    GuiPipeline = Ctx.CreateGraphicsPipeline(Spec, OutDiagnostic);
    if (GuiPipeline == VK_NULL_HANDLE)
        return false;

    Initialized = true;
    return true;
}

void VulkanImGuiPresenter::Shutdown(VulkanContext& Ctx) noexcept
{
    Ctx.DestroyBuffer(VertexBuffer);
    Ctx.DestroyBuffer(IndexBuffer);
    Ctx.DestroyImageTarget(FontAtlasImg);
    Ctx.DestroyPipeline(GuiPipeline);
    Initialized = false;
}

void VulkanImGuiPresenter::SyncManagedTextures(VulkanContext& Ctx)
{
    ImGuiPlatformIO& PlatIo = ImGui::GetPlatformIO();
    std::string Diag;
    for (ImTextureData* Tex : PlatIo.Textures)
    {
        if (!Tex || Tex->Status == ImTextureStatus_OK)
            continue;
        if (Tex->Status == ImTextureStatus_WantCreate || Tex->Status == ImTextureStatus_WantUpdates)
        {
            const auto* Pixels = static_cast<const unsigned char*>(Tex->GetPixels());
            if (Pixels && Tex->Width > 0 && Tex->Height > 0)
            {
                if (FontAtlasImg.Width != Tex->Width || FontAtlasImg.Height != Tex->Height)
                {
                    Ctx.CreateImageTarget(Tex->Width, Tex->Height, VK_FORMAT_R8G8B8A8_UNORM,
                                          0u, true, FontAtlasImg, Diag);
                }
                const size_t ByteTotal = static_cast<size_t>(Tex->Width) * static_cast<size_t>(Tex->Height) * 4u;
                Ctx.UploadRgba8Image(FontAtlasImg, Pixels, ByteTotal, Diag);
                Tex->SetTexID(static_cast<ImTextureID>(FontAtlasImg.UiToken));
                Tex->SetStatus(ImTextureStatus_OK);
            }
        }
        else if (Tex->Status == ImTextureStatus_WantDestroy)
        {
            Ctx.DestroyImageTarget(FontAtlasImg);
            Tex->SetTexID(ImTextureID_Invalid);
            Tex->SetStatus(ImTextureStatus_Destroyed);
        }
    }
}

void VulkanImGuiPresenter::RecordDrawData(VulkanContext& Ctx, VkCommandBuffer Cmd, ImDrawData* DrawData, int Width, int Height)
{
    if (!Initialized || !DrawData || DrawData->TotalVtxCount <= 0 || Width <= 0 || Height <= 0)
        return;

    if (DrawData->Textures != nullptr)
    {
        for (ImTextureData* Tex : *DrawData->Textures)
        {
            if (Tex && Tex->Status != ImTextureStatus_OK)
            {
                SyncManagedTextures(Ctx);
                break;
            }
        }
    }

    const VkDeviceSize VtxBytes = static_cast<VkDeviceSize>(DrawData->TotalVtxCount) * sizeof(ImDrawVert);
    const VkDeviceSize IdxBytes = static_cast<VkDeviceSize>(DrawData->TotalIdxCount) * sizeof(ImDrawIdx);
    std::string Diag;
    if (VertexBuffer.Size < VtxBytes)
        Ctx.CreateHostBuffer(VtxBytes * 2u, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VertexBuffer, Diag);
    if (IndexBuffer.Size < IdxBytes)
        Ctx.CreateHostBuffer(IdxBytes * 2u, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, IndexBuffer, Diag);

    auto* DstVtx = static_cast<ImDrawVert*>(VertexBuffer.Mapped);
    auto* DstIdx = static_cast<ImDrawIdx*>(IndexBuffer.Mapped);
    for (int ListIdx = 0; ListIdx < DrawData->CmdListsCount; ++ListIdx)
    {
        const ImDrawList* CmdList = DrawData->CmdLists[ListIdx];
        std::memcpy(DstVtx, CmdList->VtxBuffer.Data, static_cast<size_t>(CmdList->VtxBuffer.Size) * sizeof(ImDrawVert));
        std::memcpy(DstIdx, CmdList->IdxBuffer.Data, static_cast<size_t>(CmdList->IdxBuffer.Size) * sizeof(ImDrawIdx));
        DstVtx += CmdList->VtxBuffer.Size;
        DstIdx += CmdList->IdxBuffer.Size;
    }

    // Vulkan orthographic projection: X in [L..R] -> [-1..+1], Y in [T..B] -> [-1..+1] (top-down native Vulkan!)
    const float L = DrawData->DisplayPos.x;
    const float R = DrawData->DisplayPos.x + DrawData->DisplaySize.x;
    const float T = DrawData->DisplayPos.y;
    const float B = DrawData->DisplayPos.y + DrawData->DisplaySize.y;

    alignas(16) float OrthoProj[16] = {
         2.0f / (R - L),       0.0f,                  0.0f, 0.0f,
         0.0f,                 2.0f / (B - T),        0.0f, 0.0f,
         0.0f,                 0.0f,                  0.5f, 0.0f,
        (R + L) / (L - R),    (T + B) / (T - B),      0.5f, 1.0f,
    };

    const VkDeviceSize ZeroOffset = 0u;
    Ctx.Vk().CmdBindVertexBuffers(Cmd, 0u, 1u, &VertexBuffer.Buffer, &ZeroOffset);
    Ctx.Vk().CmdBindIndexBuffer(Cmd, IndexBuffer.Buffer, 0u,
                                sizeof(ImDrawIdx) == 2u ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);

    const ImVec2 ClipOff   = DrawData->DisplayPos;
    const ImVec2 ClipScale = DrawData->FramebufferScale;

    int GlobalVtxOffset = 0;
    int GlobalIdxOffset = 0;
    for (int ListIdx = 0; ListIdx < DrawData->CmdListsCount; ++ListIdx)
    {
        const ImDrawList* CmdList = DrawData->CmdLists[ListIdx];
        for (int CmdIdx = 0; CmdIdx < CmdList->CmdBuffer.Size; ++CmdIdx)
        {
            const ImDrawCmd& Pcmd = CmdList->CmdBuffer[CmdIdx];
            if (Pcmd.UserCallback != nullptr)
            {
                Pcmd.UserCallback(CmdList, &Pcmd);
                continue;
            }

            const float ClipMinX = std::max(0.0f, (Pcmd.ClipRect.x - ClipOff.x) * ClipScale.x);
            const float ClipMinY = std::max(0.0f, (Pcmd.ClipRect.y - ClipOff.y) * ClipScale.y);
            const float ClipMaxX = std::min(static_cast<float>(Width),  (Pcmd.ClipRect.z - ClipOff.x) * ClipScale.x);
            const float ClipMaxY = std::min(static_cast<float>(Height), (Pcmd.ClipRect.w - ClipOff.y) * ClipScale.y);
            if (ClipMaxX <= ClipMinX || ClipMaxY <= ClipMinY)
                continue;

            VkRect2D Scissor{};
            Scissor.offset = { static_cast<int32_t>(ClipMinX), static_cast<int32_t>(ClipMinY) };
            Scissor.extent = { static_cast<uint32_t>(ClipMaxX - ClipMinX), static_cast<uint32_t>(ClipMaxY - ClipMinY) };
            Ctx.Vk().CmdSetScissor(Cmd, 0u, 1u, &Scissor);

            VulkanImageTarget* SampledTex = Ctx.LookupUiTexture(static_cast<uint64_t>(Pcmd.GetTexID()));
            Ctx.BindPipelineAndDescriptors(Cmd, GuiPipeline, OrthoProj, sizeof(OrthoProj), SampledTex);
            Ctx.Vk().CmdDrawIndexed(Cmd, Pcmd.ElemCount, 1u,
                                    static_cast<uint32_t>(Pcmd.IdxOffset + GlobalIdxOffset),
                                    static_cast<int32_t>(Pcmd.VtxOffset + GlobalVtxOffset), 0u);
        }
        GlobalIdxOffset += CmdList->IdxBuffer.Size;
        GlobalVtxOffset += CmdList->VtxBuffer.Size;
    }
}

} // namespace Frontier

//----------------------------------------------------------------------------------------------------------------------
//                          ISOLATED OS SHARED-LIBRARY LOADER (ZERO WIN32 MACRO POLLUTION)
//----------------------------------------------------------------------------------------------------------------------

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h>

namespace Frontier
{

void* OpenPlatformDynamicLibrary(const char* LibraryPath) noexcept
{
    return reinterpret_cast<void*>(LoadLibraryA(LibraryPath));
}

PFN_vkVoidFunction LookupPlatformDynamicSymbol(void* LibraryHandle, const char* SymbolName) noexcept
{
    return reinterpret_cast<PFN_vkVoidFunction>(
        GetProcAddress(reinterpret_cast<HMODULE>(LibraryHandle), SymbolName));
}

void ClosePlatformDynamicLibrary(void* LibraryHandle) noexcept
{
    if (LibraryHandle)
        FreeLibrary(reinterpret_cast<HMODULE>(LibraryHandle));
}

} // namespace Frontier
#else
    #include <dlfcn.h>

namespace Frontier
{

void* OpenPlatformDynamicLibrary(const char* LibraryPath) noexcept
{
    return dlopen(LibraryPath, RTLD_NOW | RTLD_LOCAL);
}

PFN_vkVoidFunction LookupPlatformDynamicSymbol(void* LibraryHandle, const char* SymbolName) noexcept
{
    return reinterpret_cast<PFN_vkVoidFunction>(dlsym(LibraryHandle, SymbolName));
}

void ClosePlatformDynamicLibrary(void* LibraryHandle) noexcept
{
    if (LibraryHandle)
        dlclose(LibraryHandle);
}

} // namespace Frontier
#endif
