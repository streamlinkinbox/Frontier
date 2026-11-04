/*==========================================================================================================================================
                                             VULKAN COMPUTE EXCHANGE DEFINITION
==========================================================================================================================================*/
// 🧩 Dynamic-loaded Vulkan 1.3 offscreen compute device exchange for multi-planet Environment & Celestial submissions.

#include "Engine/DeviceExchange/VulkanComputeExchange.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Frontier
{
namespace
{

void* OpenDynamicLibrary(const char* Path) noexcept
{
#if defined(_WIN32)
    return reinterpret_cast<void*>(LoadLibraryA(Path));
#else
    return dlopen(Path, RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
#endif
}

void CloseDynamicLibrary(void* Handle) noexcept
{
    if (!Handle)
    {
        return;
    }
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(Handle));
#else
    dlclose(Handle);
#endif
}

void* LookupDynamicSymbol(void* Handle, const char* Name) noexcept
{
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(Handle), Name));
#else
    return dlsym(Handle, Name);
#endif
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                            FP16 <-> FP32 CONVERSION
//------------------------------------------------------------------------------------------------------------------------

uint16_t PackHalfFloat(float Scalar) noexcept
{
    uint32_t Bits = 0u;
    std::memcpy(&Bits, &Scalar, sizeof(uint32_t));
    const uint32_t Sign = (Bits >> 16u) & 0x8000u;
    int32_t        Exp  = static_cast<int32_t>((Bits >> 23u) & 0xFFu) - 127 + 15;
    uint32_t       Mant = Bits & 0x007FFFFFu;

    if (Exp <= 0)
    {
        if (Exp < -10)
        {
            return static_cast<uint16_t>(Sign);
        }
        Mant = (Mant | 0x00800000u) >> static_cast<uint32_t>(1 - Exp);
        return static_cast<uint16_t>(Sign | ((Mant + 0x00001000u) >> 13u));
    }
    if (Exp >= 31)
    {
        return static_cast<uint16_t>(Sign | 0x7C00u);
    }
    return static_cast<uint16_t>(Sign | (static_cast<uint32_t>(Exp) << 10u) | ((Mant + 0x00001000u) >> 13u));
}

float UnpackHalfFloat(uint16_t Bits) noexcept
{
    const uint32_t Sign = static_cast<uint32_t>(Bits & 0x8000u) << 16u;
    const uint32_t Exp  = (Bits >> 10u) & 0x1Fu;
    const uint32_t Mant = Bits & 0x03FFu;

    uint32_t OutBits = 0u;
    if (Exp == 0u)
    {
        if (Mant == 0u)
        {
            OutBits = Sign;
        }
        else
        {
            uint32_t Subnormal = Mant;
            int32_t  ShiftExp  = -14;
            while ((Subnormal & 0x0400u) == 0u)
            {
                Subnormal <<= 1u;
                --ShiftExp;
            }
            Subnormal &= 0x03FFu;
            OutBits = Sign | (static_cast<uint32_t>(ShiftExp + 127) << 23u) | (Subnormal << 13u);
        }
    }
    else if (Exp == 31u)
    {
        OutBits = Sign | 0x7F800000u | (Mant << 13u);
    }
    else
    {
        OutBits = Sign | ((Exp + 127u - 15u) << 23u) | (Mant << 13u);
    }
    float Result = 0.0f;
    std::memcpy(&Result, &OutBits, sizeof(float));
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                             LIFECYCLE & INITIALIZATION
//------------------------------------------------------------------------------------------------------------------------

VulkanComputeExchange::~VulkanComputeExchange()
{
    Shutdown();
}

bool VulkanComputeExchange::Initialize(const std::string& ExplicitDriverLibraryPath, std::string* OutDiagnostic)
{
    Shutdown();

    std::vector<std::string> Candidates;
    if (!ExplicitDriverLibraryPath.empty())
    {
        Candidates.push_back(ExplicitDriverLibraryPath);
    }
    if (const char* EnvLib = std::getenv("FRONTIER_VULKAN_DRIVER"))
    {
        if (EnvLib[0] != '\0')
        {
            Candidates.push_back(EnvLib);
        }
    }
#if defined(_WIN32)
    Candidates.push_back("vulkan-1.dll");
#else
    Candidates.push_back("ExternalPackages/VulkanCompute/SwiftShader/libvulkan.so.1");
    Candidates.push_back("../ExternalPackages/VulkanCompute/SwiftShader/libvulkan.so.1");
    Candidates.push_back("libvulkan.so.1");
#endif

    std::string ChosenPath;
    for (const std::string& Candidate : Candidates)
    {
#if !defined(_WIN32)
        std::error_code             ErrorCode;
        const std::filesystem::path LibPath = std::filesystem::absolute(Candidate, ErrorCode);
        if (!ErrorCode && std::filesystem::exists(LibPath, ErrorCode))
        {
            const std::filesystem::path IcdJson = LibPath.parent_path() / "vk_swiftshader_icd.json";
            if (std::filesystem::exists(IcdJson, ErrorCode))
            {
                setenv("VK_ICD_FILENAMES", IcdJson.string().c_str(), 1);
            }
            LibraryHandle_ = OpenDynamicLibrary(LibPath.string().c_str());
            if (LibraryHandle_)
            {
                ChosenPath = LibPath.string();
                break;
            }
        }
#endif
        LibraryHandle_ = OpenDynamicLibrary(Candidate.c_str());
        if (LibraryHandle_)
        {
            ChosenPath = Candidate;
            break;
        }
    }

    if (!LibraryHandle_)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "Could not open Vulkan loader library";
        }
        return false;
    }

    pfnGetInstanceProcAddr_ = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        LookupDynamicSymbol(LibraryHandle_, "vkGetInstanceProcAddr"));
    if (!pfnGetInstanceProcAddr_)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "vkGetInstanceProcAddr missing in " + ChosenPath;
        }
        Shutdown();
        return false;
    }

    pfnCreateInstance_ = reinterpret_cast<PFN_vkCreateInstance>(
        pfnGetInstanceProcAddr_(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!pfnCreateInstance_)
    {
        Shutdown();
        return false;
    }

    VkApplicationInfo AppSpec{};
    AppSpec.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    AppSpec.pApplicationName   = "FrontierPlanetaryCompute";
    AppSpec.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    AppSpec.pEngineName        = "Frontier";
    AppSpec.engineVersion      = VK_MAKE_API_VERSION(0, 1, 0, 0);
    AppSpec.apiVersion         = VK_API_VERSION_1_2;

    VkInstanceCreateInfo InstanceSpec{};
    InstanceSpec.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    InstanceSpec.pApplicationInfo = &AppSpec;

    if (pfnCreateInstance_(&InstanceSpec, nullptr, &Instance_) != VK_SUCCESS || Instance_ == VK_NULL_HANDLE)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "vkCreateInstance failed";
        }
        Shutdown();
        return false;
    }

    if (!ResolveInstanceFunctions(OutDiagnostic))
    {
        Shutdown();
        return false;
    }

    uint32_t DeviceCount = 0u;
    if (pfnEnumeratePhysicalDevices_(Instance_, &DeviceCount, nullptr) != VK_SUCCESS || DeviceCount == 0u)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "No Vulkan physical devices enumerated";
        }
        Shutdown();
        return false;
    }
    std::vector<VkPhysicalDevice> Devices(DeviceCount);
    pfnEnumeratePhysicalDevices_(Instance_, &DeviceCount, Devices.data());
    PhysicalDevice_ = Devices[0];

    VkPhysicalDeviceProperties Props{};
    pfnGetPhysicalDeviceProperties_(PhysicalDevice_, &Props);
    DeviceName_ = Props.deviceName;
    pfnGetPhysicalDeviceMemoryProperties_(PhysicalDevice_, &MemoryProperties_);

    uint32_t FamilyCount = 0u;
    pfnGetPhysicalDeviceQueueFamilyProperties_(PhysicalDevice_, &FamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> Families(FamilyCount);
    pfnGetPhysicalDeviceQueueFamilyProperties_(PhysicalDevice_, &FamilyCount, Families.data());

    bool FoundCompute = false;
    for (uint32_t FamilyIndex = 0u; FamilyIndex < FamilyCount; ++FamilyIndex)
    {
        if ((Families[FamilyIndex].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0u)
        {
            QueueFamily_ = FamilyIndex;
            FoundCompute = true;
            break;
        }
    }
    if (!FoundCompute)
    {
        Shutdown();
        return false;
    }

    const float             Priority = 1.0f;
    VkDeviceQueueCreateInfo QueueSpec{};
    QueueSpec.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    QueueSpec.queueFamilyIndex = QueueFamily_;
    QueueSpec.queueCount       = 1u;
    QueueSpec.pQueuePriorities = &Priority;

    VkDeviceCreateInfo DeviceSpec{};
    DeviceSpec.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    DeviceSpec.queueCreateInfoCount = 1u;
    DeviceSpec.pQueueCreateInfos    = &QueueSpec;

    if (pfnCreateDevice_(PhysicalDevice_, &DeviceSpec, nullptr, &Device_) != VK_SUCCESS || Device_ == VK_NULL_HANDLE)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "vkCreateDevice failed";
        }
        Shutdown();
        return false;
    }

    if (!CompleteDevice(OutDiagnostic))
    {
        Shutdown();
        return false;
    }
    return true;
}

bool VulkanComputeExchange::ResolveInstanceFunctions(std::string* OutDiagnostic)
{
#define RESOLVE_VK_INST(Field, Symbol)                                                        \
    Field = reinterpret_cast<decltype(Field)>(pfnGetInstanceProcAddr_(Instance_, Symbol));    \
    if (!Field)                                                                               \
    {                                                                                         \
        if (OutDiagnostic) *OutDiagnostic = std::string("Missing Vulkan symbol: ") + Symbol;  \
        return false;                                                                         \
    }

    RESOLVE_VK_INST(pfnDestroyInstance_, "vkDestroyInstance");
    RESOLVE_VK_INST(pfnEnumeratePhysicalDevices_, "vkEnumeratePhysicalDevices");
    RESOLVE_VK_INST(pfnGetPhysicalDeviceProperties_, "vkGetPhysicalDeviceProperties");
    RESOLVE_VK_INST(pfnGetPhysicalDeviceMemoryProperties_, "vkGetPhysicalDeviceMemoryProperties");
    RESOLVE_VK_INST(pfnGetPhysicalDeviceQueueFamilyProperties_, "vkGetPhysicalDeviceQueueFamilyProperties");
    RESOLVE_VK_INST(pfnCreateDevice_, "vkCreateDevice");
#undef RESOLVE_VK_INST
    return true;
}

// 📝 Everything after the device exists: device entry points, the queue (unless adopted), pools, samplers and the fence.
bool VulkanComputeExchange::CompleteDevice(std::string* OutDiagnostic)
{
    auto GetDeviceProc = reinterpret_cast<PFN_vkGetDeviceProcAddr>(pfnGetInstanceProcAddr_(Instance_, "vkGetDeviceProcAddr"));
    if (!GetDeviceProc)
    {
        return false;
    }

#define RESOLVE_VK_DEV(Field, Symbol)                                                        \
    Field = reinterpret_cast<decltype(Field)>(GetDeviceProc(Device_, Symbol));               \
    if (!Field)                                                                              \
    {                                                                                        \
        if (OutDiagnostic) *OutDiagnostic = std::string("Missing device symbol: ") + Symbol; \
        return false;                                                                        \
    }

    RESOLVE_VK_DEV(pfnDestroyDevice_, "vkDestroyDevice");
    RESOLVE_VK_DEV(pfnGetDeviceQueue_, "vkGetDeviceQueue");
    RESOLVE_VK_DEV(pfnCreateCommandPool_, "vkCreateCommandPool");
    RESOLVE_VK_DEV(pfnDestroyCommandPool_, "vkDestroyCommandPool");
    RESOLVE_VK_DEV(pfnAllocateCommandStreams_, "vkAllocateCommandBuffers");
    RESOLVE_VK_DEV(pfnFreeCommandStreams_, "vkFreeCommandBuffers");
    RESOLVE_VK_DEV(pfnBeginCommandStream_, "vkBeginCommandBuffer");
    RESOLVE_VK_DEV(pfnEndCommandStream_, "vkEndCommandBuffer");
    RESOLVE_VK_DEV(pfnResetCommandStream_, "vkResetCommandBuffer");
    RESOLVE_VK_DEV(pfnQueueSubmit_, "vkQueueSubmit");
    RESOLVE_VK_DEV(pfnQueueWaitIdle_, "vkQueueWaitIdle");
    RESOLVE_VK_DEV(pfnCreateFence_, "vkCreateFence");
    RESOLVE_VK_DEV(pfnDestroyFence_, "vkDestroyFence");
    RESOLVE_VK_DEV(pfnWaitForFences_, "vkWaitForFences");
    RESOLVE_VK_DEV(pfnResetFences_, "vkResetFences");
    RESOLVE_VK_DEV(pfnCreateMemorySpan_, "vkCreateBuffer");
    RESOLVE_VK_DEV(pfnDestroyMemorySpan_, "vkDestroyBuffer");
    RESOLVE_VK_DEV(pfnGetSpanMemoryRequirements_, "vkGetBufferMemoryRequirements");
    RESOLVE_VK_DEV(pfnAllocateMemory_, "vkAllocateMemory");
    RESOLVE_VK_DEV(pfnFreeMemory_, "vkFreeMemory");
    RESOLVE_VK_DEV(pfnBindSpanMemory_, "vkBindBufferMemory");
    RESOLVE_VK_DEV(pfnMapMemory_, "vkMapMemory");
    RESOLVE_VK_DEV(pfnUnmapMemory_, "vkUnmapMemory");
    RESOLVE_VK_DEV(pfnCreateImage_, "vkCreateImage");
    RESOLVE_VK_DEV(pfnDestroyImage_, "vkDestroyImage");
    RESOLVE_VK_DEV(pfnGetImageMemoryRequirements_, "vkGetImageMemoryRequirements");
    RESOLVE_VK_DEV(pfnBindImageMemory_, "vkBindImageMemory");
    RESOLVE_VK_DEV(pfnCreateImageView_, "vkCreateImageView");
    RESOLVE_VK_DEV(pfnDestroyImageView_, "vkDestroyImageView");
    RESOLVE_VK_DEV(pfnCreateSampler_, "vkCreateSampler");
    RESOLVE_VK_DEV(pfnDestroySampler_, "vkDestroySampler");
    RESOLVE_VK_DEV(pfnCreateShaderUnit_, "vkCreateShaderModule");
    RESOLVE_VK_DEV(pfnDestroyShaderUnit_, "vkDestroyShaderModule");
    RESOLVE_VK_DEV(pfnCreateDescriptorSetLayout_, "vkCreateDescriptorSetLayout");
    RESOLVE_VK_DEV(pfnDestroyDescriptorSetLayout_, "vkDestroyDescriptorSetLayout");
    RESOLVE_VK_DEV(pfnCreatePipelineLayout_, "vkCreatePipelineLayout");
    RESOLVE_VK_DEV(pfnDestroyPipelineLayout_, "vkDestroyPipelineLayout");
    RESOLVE_VK_DEV(pfnCreateComputePipelines_, "vkCreateComputePipelines");
    RESOLVE_VK_DEV(pfnDestroyPipeline_, "vkDestroyPipeline");
    RESOLVE_VK_DEV(pfnCreateDescriptorPool_, "vkCreateDescriptorPool");
    RESOLVE_VK_DEV(pfnDestroyDescriptorPool_, "vkDestroyDescriptorPool");
    RESOLVE_VK_DEV(pfnResetDescriptorPool_, "vkResetDescriptorPool");
    RESOLVE_VK_DEV(pfnAllocateDescriptorSets_, "vkAllocateDescriptorSets");
    RESOLVE_VK_DEV(pfnUpdateDescriptorSets_, "vkUpdateDescriptorSets");
    RESOLVE_VK_DEV(pfnCmdPipelineBarrier_, "vkCmdPipelineBarrier");
    RESOLVE_VK_DEV(pfnCmdBindPipeline_, "vkCmdBindPipeline");
    RESOLVE_VK_DEV(pfnCmdBindDescriptorSets_, "vkCmdBindDescriptorSets");
    RESOLVE_VK_DEV(pfnCmdPushConstants_, "vkCmdPushConstants");
    RESOLVE_VK_DEV(pfnCmdDispatch_, "vkCmdDispatch");
    RESOLVE_VK_DEV(pfnCmdClearColorImage_, "vkCmdClearColorImage");
    RESOLVE_VK_DEV(pfnCmdCopySpanToImage_, "vkCmdCopyBufferToImage");
    RESOLVE_VK_DEV(pfnCmdCopyImageToSpan_, "vkCmdCopyImageToBuffer");

#undef RESOLVE_VK_INST
#undef RESOLVE_VK_DEV

    if (Queue_ == VK_NULL_HANDLE)
    {
        pfnGetDeviceQueue_(Device_, QueueFamily_, 0u, &Queue_);
    }

    VkCommandPoolCreateInfo PoolSpec{};
    PoolSpec.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    PoolSpec.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    PoolSpec.queueFamilyIndex = QueueFamily_;
    if (pfnCreateCommandPool_(Device_, &PoolSpec, nullptr, &CommandPool_) != VK_SUCCESS)
    {
        return false;
    }

    const std::array<VkDescriptorPoolSize, 3> PoolSizes{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 256u},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 256u},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256u},
    }};
    VkDescriptorPoolCreateInfo DescPoolSpec{};
    DescPoolSpec.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    DescPoolSpec.maxSets       = 64u;
    DescPoolSpec.poolSizeCount = static_cast<uint32_t>(PoolSizes.size());
    DescPoolSpec.pPoolSizes    = PoolSizes.data();
    if (pfnCreateDescriptorPool_(Device_, &DescPoolSpec, nullptr, &DescriptorPool_) != VK_SUCCESS)
    {
        return false;
    }

    VkSamplerCreateInfo SamplerSpec{};
    SamplerSpec.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    SamplerSpec.magFilter    = VK_FILTER_LINEAR;
    SamplerSpec.minFilter    = VK_FILTER_LINEAR;
    SamplerSpec.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    SamplerSpec.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerSpec.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerSpec.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerSpec.maxLod       = 0.0f;
    if (pfnCreateSampler_(Device_, &SamplerSpec, nullptr, &LinearClampSampler_) != VK_SUCCESS)
    {
        return false;
    }

    SamplerSpec.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    if (pfnCreateSampler_(Device_, &SamplerSpec, nullptr, &LinearRepeatSampler_) != VK_SUCCESS)
    {
        return false;
    }

    VkFenceCreateInfo FenceSpec{};
    FenceSpec.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (pfnCreateFence_(Device_, &FenceSpec, nullptr, &SubmitFence_) != VK_SUCCESS)
    {
        return false;
    }

    return true;
}

bool VulkanComputeExchange::AttachDevice(const VulkanDeviceAccess& Access, std::string* OutDiagnostic)
{
    Shutdown();
    if (!Access.Valid())
    {
        if (OutDiagnostic) *OutDiagnostic = "adopted device incomplete";
        return false;
    }
    pfnGetInstanceProcAddr_ = Access.ResolveInstance;
    Instance_               = Access.Instance;
    PhysicalDevice_         = Access.PhysicalDevice;
    Device_                 = Access.Device;
    Queue_                  = Access.Queue;
    QueueFamily_            = Access.QueueFamily;
    DeviceAttached_         = true;
    if (!ResolveInstanceFunctions(OutDiagnostic))
    {
        Shutdown();
        return false;
    }
    VkPhysicalDeviceProperties Props{};
    pfnGetPhysicalDeviceProperties_(PhysicalDevice_, &Props);
    DeviceName_ = Props.deviceName;
    pfnGetPhysicalDeviceMemoryProperties_(PhysicalDevice_, &MemoryProperties_);
    if (!CompleteDevice(OutDiagnostic))
    {
        Shutdown();
        return false;
    }
    return true;
}

void VulkanComputeExchange::Shutdown() noexcept
{
    if (Device_ != VK_NULL_HANDLE)
    {
        if (pfnQueueWaitIdle_ && Queue_ != VK_NULL_HANDLE)
        {
            pfnQueueWaitIdle_(Queue_);
        }
        if (SubmitFence_ != VK_NULL_HANDLE && pfnDestroyFence_)
        {
            pfnDestroyFence_(Device_, SubmitFence_, nullptr);
        }
        if (LinearRepeatSampler_ != VK_NULL_HANDLE && pfnDestroySampler_)
        {
            pfnDestroySampler_(Device_, LinearRepeatSampler_, nullptr);
        }
        if (LinearClampSampler_ != VK_NULL_HANDLE && pfnDestroySampler_)
        {
            pfnDestroySampler_(Device_, LinearClampSampler_, nullptr);
        }
        if (DescriptorPool_ != VK_NULL_HANDLE && pfnDestroyDescriptorPool_)
        {
            pfnDestroyDescriptorPool_(Device_, DescriptorPool_, nullptr);
        }
        if (CommandPool_ != VK_NULL_HANDLE && pfnDestroyCommandPool_)
        {
            pfnDestroyCommandPool_(Device_, CommandPool_, nullptr);
        }
        if (pfnDestroyDevice_ && !DeviceAttached_)
        {
            pfnDestroyDevice_(Device_, nullptr);
        }
    }
    if (Instance_ != VK_NULL_HANDLE && pfnDestroyInstance_ && !DeviceAttached_)
    {
        pfnDestroyInstance_(Instance_, nullptr);
    }
    if (LibraryHandle_ != nullptr)
    {
        CloseDynamicLibrary(LibraryHandle_);
    }
    DeviceAttached_ = false;

    LibraryHandle_       = nullptr;
    Instance_            = VK_NULL_HANDLE;
    PhysicalDevice_      = VK_NULL_HANDLE;
    Device_              = VK_NULL_HANDLE;
    Queue_               = VK_NULL_HANDLE;
    CommandPool_         = VK_NULL_HANDLE;
    DescriptorPool_      = VK_NULL_HANDLE;
    LinearClampSampler_  = VK_NULL_HANDLE;
    LinearRepeatSampler_ = VK_NULL_HANDLE;
    SubmitFence_         = VK_NULL_HANDLE;
    DeviceName_.clear();
}

uint32_t VulkanComputeExchange::SelectMemoryType(uint32_t TypeBits, VkMemoryPropertyFlags RequiredFlags) const
{
    for (uint32_t TypeIndex = 0u; TypeIndex < MemoryProperties_.memoryTypeCount; ++TypeIndex)
    {
        if ((TypeBits & (1u << TypeIndex)) != 0u &&
            (MemoryProperties_.memoryTypes[TypeIndex].propertyFlags & RequiredFlags) == RequiredFlags)
        {
            return TypeIndex;
        }
    }
    return UINT32_MAX;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      MEMORY SPAN & IMAGE ARRAY ALLOCATION / IO
//------------------------------------------------------------------------------------------------------------------------

bool VulkanComputeExchange::AllocateMemorySpan(VkDeviceSize ByteCount, DeviceMemorySpan& OutSlot)
{
    ReleaseMemorySpan(OutSlot);
    if (!Ready() || ByteCount == 0)
    {
        return false;
    }

    VkBufferCreateInfo SpanSpec{};
    SpanSpec.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    SpanSpec.size        = std::max<VkDeviceSize>(ByteCount, 16u);
    SpanSpec.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    SpanSpec.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (pfnCreateMemorySpan_(Device_, &SpanSpec, nullptr, &OutSlot.Handle) != VK_SUCCESS)
    {
        return false;
    }
    VkMemoryRequirements Requirements{};
    pfnGetSpanMemoryRequirements_(Device_, OutSlot.Handle, &Requirements);

    const uint32_t MemoryType = SelectMemoryType(
        Requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (MemoryType == UINT32_MAX)
    {
        ReleaseMemorySpan(OutSlot);
        return false;
    }

    VkMemoryAllocateInfo AllocSpec{};
    AllocSpec.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    AllocSpec.allocationSize  = Requirements.size;
    AllocSpec.memoryTypeIndex = MemoryType;

    if (pfnAllocateMemory_(Device_, &AllocSpec, nullptr, &OutSlot.Memory) != VK_SUCCESS ||
        pfnBindSpanMemory_(Device_, OutSlot.Handle, OutSlot.Memory, 0) != VK_SUCCESS ||
        pfnMapMemory_(Device_, OutSlot.Memory, 0, SpanSpec.size, 0, &OutSlot.MappedBytes) != VK_SUCCESS)
    {
        ReleaseMemorySpan(OutSlot);
        return false;
    }
    OutSlot.Capacity = SpanSpec.size;
    std::memset(OutSlot.MappedBytes, 0, static_cast<size_t>(SpanSpec.size));
    return true;
}

void VulkanComputeExchange::ReleaseMemorySpan(DeviceMemorySpan& Slot) noexcept
{
    if (!Ready())
    {
        Slot = DeviceMemorySpan{};
        return;
    }
    if (Slot.MappedBytes && Slot.Memory != VK_NULL_HANDLE)
    {
        pfnUnmapMemory_(Device_, Slot.Memory);
    }
    if (Slot.Handle != VK_NULL_HANDLE)
    {
        pfnDestroyMemorySpan_(Device_, Slot.Handle, nullptr);
    }
    if (Slot.Memory != VK_NULL_HANDLE)
    {
        pfnFreeMemory_(Device_, Slot.Memory, nullptr);
    }
    Slot = DeviceMemorySpan{};
}

bool VulkanComputeExchange::UploadMemorySpan(DeviceMemorySpan& Slot, const void* SourceBytes, VkDeviceSize ByteCount)
{
    if (ByteCount == 0)
    {
        return true;
    }
    if (Slot.Handle == VK_NULL_HANDLE || Slot.Capacity < ByteCount)
    {
        if (!AllocateMemorySpan(ByteCount, Slot))
        {
            return false;
        }
    }
    std::memcpy(Slot.MappedBytes, SourceBytes, static_cast<size_t>(ByteCount));
    return true;
}

bool VulkanComputeExchange::DownloadMemorySpan(const DeviceMemorySpan& Slot, void* DestinationBytes, VkDeviceSize ByteCount) const
{
    if (!Slot.MappedBytes || Slot.Capacity < ByteCount)
    {
        return false;
    }
    std::memcpy(DestinationBytes, Slot.MappedBytes, static_cast<size_t>(ByteCount));
    return true;
}

bool VulkanComputeExchange::AllocateFloatRgbaArray(uint32_t Width, uint32_t Height, uint32_t ArrayLayers, VkFormat Format, DeviceImageArraySlot& OutSlot)
{
    ReleaseImage(OutSlot);
    if (!Ready() || Width == 0u || Height == 0u || ArrayLayers == 0u ||
        (Format != VK_FORMAT_R16G16B16A16_SFLOAT && Format != VK_FORMAT_R32G32B32A32_SFLOAT))
    {
        return false;
    }

    VkImageCreateInfo ImageSpec{};
    ImageSpec.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ImageSpec.imageType     = VK_IMAGE_TYPE_2D;
    ImageSpec.format        = Format;
    ImageSpec.extent        = {Width, Height, 1u};
    ImageSpec.mipLevels     = 1u;
    ImageSpec.arrayLayers   = ArrayLayers;
    ImageSpec.samples       = VK_SAMPLE_COUNT_1_BIT;
    ImageSpec.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ImageSpec.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ImageSpec.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ImageSpec.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (pfnCreateImage_(Device_, &ImageSpec, nullptr, &OutSlot.Image) != VK_SUCCESS)
    {
        return false;
    }
    VkMemoryRequirements Requirements{};
    pfnGetImageMemoryRequirements_(Device_, OutSlot.Image, &Requirements);
    const uint32_t MemoryType = SelectMemoryType(Requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (MemoryType == UINT32_MAX)
    {
        ReleaseImage(OutSlot);
        return false;
    }

    VkMemoryAllocateInfo AllocSpec{};
    AllocSpec.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    AllocSpec.allocationSize  = Requirements.size;
    AllocSpec.memoryTypeIndex = MemoryType;

    if (pfnAllocateMemory_(Device_, &AllocSpec, nullptr, &OutSlot.Memory) != VK_SUCCESS ||
        pfnBindImageMemory_(Device_, OutSlot.Image, OutSlot.Memory, 0) != VK_SUCCESS)
    {
        ReleaseImage(OutSlot);
        return false;
    }

    VkImageViewCreateInfo ViewSpec{};
    ViewSpec.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ViewSpec.image                           = OutSlot.Image;
    ViewSpec.viewType                        = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    ViewSpec.format                          = ImageSpec.format;
    ViewSpec.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    ViewSpec.subresourceRange.baseMipLevel   = 0u;
    ViewSpec.subresourceRange.levelCount     = 1u;
    ViewSpec.subresourceRange.baseArrayLayer = 0u;
    ViewSpec.subresourceRange.layerCount     = ArrayLayers;

    if (pfnCreateImageView_(Device_, &ViewSpec, nullptr, &OutSlot.ArrayView) != VK_SUCCESS)
    {
        ReleaseImage(OutSlot);
        return false;
    }

    OutSlot.Format        = ImageSpec.format;
    OutSlot.Width         = Width;
    OutSlot.Height        = Height;
    OutSlot.ArrayLayers   = ArrayLayers;
    OutSlot.CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return ClearRgba16fArray(OutSlot);
}

bool VulkanComputeExchange::AllocateSrgb8Array(uint32_t Width, uint32_t Height, uint32_t ArrayLayers, DeviceImageArraySlot& OutSlot)
{
    ReleaseImage(OutSlot);
    if (!Ready() || Width == 0u || Height == 0u || ArrayLayers == 0u)
    {
        return false;
    }

    VkImageCreateInfo ImageSpec{};
    ImageSpec.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ImageSpec.imageType     = VK_IMAGE_TYPE_2D;
    ImageSpec.format        = VK_FORMAT_R8G8B8A8_SRGB;
    ImageSpec.extent        = {Width, Height, 1u};
    ImageSpec.mipLevels     = 1u;
    ImageSpec.arrayLayers   = ArrayLayers;
    ImageSpec.samples       = VK_SAMPLE_COUNT_1_BIT;
    ImageSpec.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ImageSpec.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ImageSpec.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ImageSpec.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (pfnCreateImage_(Device_, &ImageSpec, nullptr, &OutSlot.Image) != VK_SUCCESS)
    {
        return false;
    }
    VkMemoryRequirements Requirements{};
    pfnGetImageMemoryRequirements_(Device_, OutSlot.Image, &Requirements);
    const uint32_t MemoryType = SelectMemoryType(Requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (MemoryType == UINT32_MAX)
    {
        ReleaseImage(OutSlot);
        return false;
    }

    VkMemoryAllocateInfo AllocSpec{};
    AllocSpec.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    AllocSpec.allocationSize  = Requirements.size;
    AllocSpec.memoryTypeIndex = MemoryType;

    if (pfnAllocateMemory_(Device_, &AllocSpec, nullptr, &OutSlot.Memory) != VK_SUCCESS ||
        pfnBindImageMemory_(Device_, OutSlot.Image, OutSlot.Memory, 0) != VK_SUCCESS)
    {
        ReleaseImage(OutSlot);
        return false;
    }

    VkImageViewCreateInfo ViewSpec{};
    ViewSpec.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ViewSpec.image                           = OutSlot.Image;
    ViewSpec.viewType                        = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    ViewSpec.format                          = ImageSpec.format;
    ViewSpec.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    ViewSpec.subresourceRange.baseMipLevel   = 0u;
    ViewSpec.subresourceRange.levelCount     = 1u;
    ViewSpec.subresourceRange.baseArrayLayer = 0u;
    ViewSpec.subresourceRange.layerCount     = ArrayLayers;

    if (pfnCreateImageView_(Device_, &ViewSpec, nullptr, &OutSlot.ArrayView) != VK_SUCCESS)
    {
        ReleaseImage(OutSlot);
        return false;
    }

    OutSlot.Format        = ImageSpec.format;
    OutSlot.Width         = Width;
    OutSlot.Height        = Height;
    OutSlot.ArrayLayers   = ArrayLayers;
    OutSlot.CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

void VulkanComputeExchange::ReleaseImage(DeviceImageArraySlot& Slot) noexcept
{
    if (!Ready())
    {
        Slot = DeviceImageArraySlot{};
        return;
    }
    if (Slot.ArrayView != VK_NULL_HANDLE)
    {
        pfnDestroyImageView_(Device_, Slot.ArrayView, nullptr);
    }
    if (Slot.Image != VK_NULL_HANDLE)
    {
        pfnDestroyImage_(Device_, Slot.Image, nullptr);
    }
    if (Slot.Memory != VK_NULL_HANDLE)
    {
        pfnFreeMemory_(Device_, Slot.Memory, nullptr);
    }
    Slot = DeviceImageArraySlot{};
}

bool VulkanComputeExchange::TransitionImageLayout(VkCommandBuffer       CommandStream,
                                                  DeviceImageArraySlot& Slot,
                                                  VkImageLayout         TargetLayout)
{
    if (Slot.CurrentLayout == TargetLayout)
    {
        return true;
    }
    VkImageMemoryBarrier Barrier{};
    Barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    Barrier.oldLayout                       = Slot.CurrentLayout;
    Barrier.newLayout                       = TargetLayout;
    Barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    Barrier.image                           = Slot.Image;
    Barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    Barrier.subresourceRange.baseMipLevel   = 0u;
    Barrier.subresourceRange.levelCount     = 1u;
    Barrier.subresourceRange.baseArrayLayer = 0u;
    Barrier.subresourceRange.layerCount     = Slot.ArrayLayers;
    Barrier.srcAccessMask                   = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
    Barrier.dstAccessMask                   = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;

    pfnCmdPipelineBarrier_(CommandStream,
                           VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                           VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                           0u,
                           0u,
                           nullptr,
                           0u,
                           nullptr,
                           1u,
                           &Barrier);
    Slot.CurrentLayout = TargetLayout;
    return true;
}

bool VulkanComputeExchange::ClearRgba16fArray(DeviceImageArraySlot& Slot, std::array<float, 4> ClearColour)
{
    if (!Ready() || Slot.Image == VK_NULL_HANDLE)
    {
        return false;
    }
    VkCommandBufferAllocateInfo StreamAlloc{};
    StreamAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    StreamAlloc.commandPool        = CommandPool_;
    StreamAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    StreamAlloc.commandBufferCount = 1u;
    VkCommandBuffer CommandStream  = VK_NULL_HANDLE;
    if (pfnAllocateCommandStreams_(Device_, &StreamAlloc, &CommandStream) != VK_SUCCESS)
    {
        return false;
    }

    VkCommandBufferBeginInfo BeginSpec{};
    BeginSpec.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    BeginSpec.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pfnBeginCommandStream_(CommandStream, &BeginSpec);
    TransitionImageLayout(CommandStream, Slot, VK_IMAGE_LAYOUT_GENERAL);

    VkClearColorValue ClearColor{};
    ClearColor.float32[0] = ClearColour[0];
    ClearColor.float32[1] = ClearColour[1];
    ClearColor.float32[2] = ClearColour[2];
    ClearColor.float32[3] = ClearColour[3];

    VkImageSubresourceRange Subresource{};
    Subresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    Subresource.baseMipLevel   = 0u;
    Subresource.levelCount     = 1u;
    Subresource.baseArrayLayer = 0u;
    Subresource.layerCount     = Slot.ArrayLayers;

    pfnCmdClearColorImage_(CommandStream, Slot.Image, VK_IMAGE_LAYOUT_GENERAL, &ClearColor, 1u, &Subresource);
    pfnEndCommandStream_(CommandStream);

    pfnResetFences_(Device_, 1u, &SubmitFence_);
    VkSubmitInfo SubmitSpec{};
    SubmitSpec.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    SubmitSpec.commandBufferCount = 1u;
    SubmitSpec.pCommandBuffers    = &CommandStream;
    const bool Ok = (pfnQueueSubmit_(Queue_, 1u, &SubmitSpec, SubmitFence_) == VK_SUCCESS) &&
                    (pfnWaitForFences_(Device_, 1u, &SubmitFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    pfnFreeCommandStreams_(Device_, CommandPool_, 1u, &CommandStream);
    return Ok;
}

bool VulkanComputeExchange::UploadSrgb8Array(DeviceImageArraySlot& Slot, std::span<const uint8_t> RgbaBytes)
{
    const size_t Expected = static_cast<size_t>(Slot.Width) * Slot.Height * 4u * Slot.ArrayLayers;
    if (!Ready() || Slot.Image == VK_NULL_HANDLE || RgbaBytes.size() != Expected)
    {
        return false;
    }
    DeviceMemorySpan TransferSpan;
    if (!UploadMemorySpan(TransferSpan, RgbaBytes.data(), Expected))
    {
        return false;
    }

    VkCommandBufferAllocateInfo StreamAlloc{};
    StreamAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    StreamAlloc.commandPool        = CommandPool_;
    StreamAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    StreamAlloc.commandBufferCount = 1u;
    VkCommandBuffer CommandStream  = VK_NULL_HANDLE;
    pfnAllocateCommandStreams_(Device_, &StreamAlloc, &CommandStream);

    VkCommandBufferBeginInfo BeginSpec{};
    BeginSpec.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    BeginSpec.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pfnBeginCommandStream_(CommandStream, &BeginSpec);
    TransitionImageLayout(CommandStream, Slot, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy CopyRegion{};
    CopyRegion.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    CopyRegion.imageSubresource.mipLevel       = 0u;
    CopyRegion.imageSubresource.baseArrayLayer = 0u;
    CopyRegion.imageSubresource.layerCount     = Slot.ArrayLayers;
    CopyRegion.imageExtent                     = {Slot.Width, Slot.Height, 1u};

    pfnCmdCopySpanToImage_(CommandStream, TransferSpan.Handle, Slot.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &CopyRegion);
    TransitionImageLayout(CommandStream, Slot, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    pfnEndCommandStream_(CommandStream);

    pfnResetFences_(Device_, 1u, &SubmitFence_);
    VkSubmitInfo SubmitSpec{};
    SubmitSpec.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    SubmitSpec.commandBufferCount = 1u;
    SubmitSpec.pCommandBuffers    = &CommandStream;
    const bool Ok = (pfnQueueSubmit_(Queue_, 1u, &SubmitSpec, SubmitFence_) == VK_SUCCESS) &&
                    (pfnWaitForFences_(Device_, 1u, &SubmitFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    pfnFreeCommandStreams_(Device_, CommandPool_, 1u, &CommandStream);
    ReleaseMemorySpan(TransferSpan);
    return Ok;
}

bool VulkanComputeExchange::UploadRgba16fLayer(DeviceImageArraySlot&  Slot,
                                               uint32_t               LayerIndex,
                                               std::span<const float> RgbaFloats)
{
    const size_t FloatCount = static_cast<size_t>(Slot.Width) * Slot.Height * 4u;
    if (!Ready() || Slot.Image == VK_NULL_HANDLE || LayerIndex >= Slot.ArrayLayers || RgbaFloats.size() != FloatCount)
    {
        return false;
    }
    std::vector<uint16_t> HalfTexels(FloatCount);
    for (size_t Index = 0u; Index < FloatCount; ++Index)
    {
        HalfTexels[Index] = PackHalfFloat(RgbaFloats[Index]);
    }
    DeviceMemorySpan TransferSpan;
    if (!UploadMemorySpan(TransferSpan, HalfTexels.data(), HalfTexels.size() * sizeof(uint16_t)))
    {
        return false;
    }

    VkCommandBufferAllocateInfo StreamAlloc{};
    StreamAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    StreamAlloc.commandPool        = CommandPool_;
    StreamAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    StreamAlloc.commandBufferCount = 1u;
    VkCommandBuffer CommandStream  = VK_NULL_HANDLE;
    pfnAllocateCommandStreams_(Device_, &StreamAlloc, &CommandStream);

    VkCommandBufferBeginInfo BeginSpec{};
    BeginSpec.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    BeginSpec.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pfnBeginCommandStream_(CommandStream, &BeginSpec);
    TransitionImageLayout(CommandStream, Slot, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy CopyRegion{};
    CopyRegion.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    CopyRegion.imageSubresource.mipLevel       = 0u;
    CopyRegion.imageSubresource.baseArrayLayer = LayerIndex;
    CopyRegion.imageSubresource.layerCount     = 1u;
    CopyRegion.imageExtent                     = {Slot.Width, Slot.Height, 1u};

    pfnCmdCopySpanToImage_(CommandStream, TransferSpan.Handle, Slot.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &CopyRegion);
    TransitionImageLayout(CommandStream, Slot, VK_IMAGE_LAYOUT_GENERAL);
    pfnEndCommandStream_(CommandStream);

    pfnResetFences_(Device_, 1u, &SubmitFence_);
    VkSubmitInfo SubmitSpec{};
    SubmitSpec.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    SubmitSpec.commandBufferCount = 1u;
    SubmitSpec.pCommandBuffers    = &CommandStream;
    const bool Ok = (pfnQueueSubmit_(Queue_, 1u, &SubmitSpec, SubmitFence_) == VK_SUCCESS) &&
                    (pfnWaitForFences_(Device_, 1u, &SubmitFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    pfnFreeCommandStreams_(Device_, CommandPool_, 1u, &CommandStream);
    ReleaseMemorySpan(TransferSpan);
    return Ok;
}

bool VulkanComputeExchange::DownloadFloatRgbaLayer(DeviceImageArraySlot& Slot,
                                                 uint32_t              LayerIndex,
                                                 std::vector<float>&   OutRgbaFloats)
{
    const size_t FloatCount = static_cast<size_t>(Slot.Width) * Slot.Height * 4u;
    if (!Ready() || Slot.Image == VK_NULL_HANDLE || LayerIndex >= Slot.ArrayLayers)
    {
        return false;
    }
    const bool FullPrecision = Slot.Format == VK_FORMAT_R32G32B32A32_SFLOAT;
    if (!FullPrecision && Slot.Format != VK_FORMAT_R16G16B16A16_SFLOAT) return false;
    DeviceMemorySpan TransferSpan;
    if (!AllocateMemorySpan(FloatCount * (FullPrecision ? sizeof(float) : sizeof(uint16_t)), TransferSpan))
    {
        return false;
    }

    VkCommandBufferAllocateInfo StreamAlloc{};
    StreamAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    StreamAlloc.commandPool        = CommandPool_;
    StreamAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    StreamAlloc.commandBufferCount = 1u;
    VkCommandBuffer CommandStream  = VK_NULL_HANDLE;
    pfnAllocateCommandStreams_(Device_, &StreamAlloc, &CommandStream);

    VkCommandBufferBeginInfo BeginSpec{};
    BeginSpec.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    BeginSpec.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pfnBeginCommandStream_(CommandStream, &BeginSpec);
    TransitionImageLayout(CommandStream, Slot, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkBufferImageCopy CopyRegion{};
    CopyRegion.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    CopyRegion.imageSubresource.mipLevel       = 0u;
    CopyRegion.imageSubresource.baseArrayLayer = LayerIndex;
    CopyRegion.imageSubresource.layerCount     = 1u;
    CopyRegion.imageExtent                     = {Slot.Width, Slot.Height, 1u};

    pfnCmdCopyImageToSpan_(CommandStream, Slot.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, TransferSpan.Handle, 1u, &CopyRegion);
    TransitionImageLayout(CommandStream, Slot, VK_IMAGE_LAYOUT_GENERAL);
    pfnEndCommandStream_(CommandStream);

    pfnResetFences_(Device_, 1u, &SubmitFence_);
    VkSubmitInfo SubmitSpec{};
    SubmitSpec.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    SubmitSpec.commandBufferCount = 1u;
    SubmitSpec.pCommandBuffers    = &CommandStream;
    const bool Ok = (pfnQueueSubmit_(Queue_, 1u, &SubmitSpec, SubmitFence_) == VK_SUCCESS) &&
                    (pfnWaitForFences_(Device_, 1u, &SubmitFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    pfnFreeCommandStreams_(Device_, CommandPool_, 1u, &CommandStream);
    if (!Ok)
    {
        ReleaseMemorySpan(TransferSpan);
        return false;
    }

    OutRgbaFloats.resize(FloatCount);
    if (FullPrecision) std::memcpy(OutRgbaFloats.data(),TransferSpan.MappedBytes,FloatCount*sizeof(float));
    else
    {
        const auto* HalfPtr = static_cast<const uint16_t*>(TransferSpan.MappedBytes);
        for (size_t Index=0;Index<FloatCount;++Index) OutRgbaFloats[Index]=UnpackHalfFloat(HalfPtr[Index]);
    }
    ReleaseMemorySpan(TransferSpan);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           COMPUTE PROGRAM & DISPATCH
//------------------------------------------------------------------------------------------------------------------------

bool VulkanComputeExchange::LoadComputeProgram(const std::string&                SpvFilePath,
                                               const char*                       EntryPointName,
                                               std::span<const VkDescriptorType> BindingTypes,
                                               uint32_t                          PushConstantBytes,
                                               ComputeProgramSlot&               OutProgram,
                                               std::string*                      OutDiagnostic)
{
    ReleaseProgram(OutProgram);
    if (!Ready())
    {
        return false;
    }

    std::ifstream File(SpvFilePath, std::ios::binary | std::ios::ate);
    if (!File.is_open())
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "Could not open SPIR-V file: " + SpvFilePath;
        }
        return false;
    }
    const std::streamsize ByteSize = File.tellg();
    if (ByteSize <= 0 || (ByteSize % 4) != 0)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "Invalid SPIR-V byte size: " + SpvFilePath;
        }
        return false;
    }
    File.seekg(0, std::ios::beg);
    std::vector<uint32_t> Words(static_cast<size_t>(ByteSize) / 4u);
    File.read(reinterpret_cast<char*>(Words.data()), ByteSize);

    VkShaderModuleCreateInfo UnitSpec{};
    UnitSpec.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    UnitSpec.codeSize = static_cast<size_t>(ByteSize);
    UnitSpec.pCode    = Words.data();
    if (pfnCreateShaderUnit_(Device_, &UnitSpec, nullptr, &OutProgram.ShaderUnit) != VK_SUCCESS)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "vkCreateShaderModule failed for " + SpvFilePath;
        }
        return false;
    }

    std::vector<VkDescriptorSetLayoutBinding> Bindings(BindingTypes.size());
    for (size_t SlotIndex = 0u; SlotIndex < BindingTypes.size(); ++SlotIndex)
    {
        Bindings[SlotIndex].binding         = static_cast<uint32_t>(SlotIndex);
        Bindings[SlotIndex].descriptorType  = BindingTypes[SlotIndex];
        Bindings[SlotIndex].descriptorCount = 1u;
        Bindings[SlotIndex].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo SetLayoutSpec{};
    SetLayoutSpec.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    SetLayoutSpec.bindingCount = static_cast<uint32_t>(Bindings.size());
    SetLayoutSpec.pBindings    = Bindings.data();
    if (pfnCreateDescriptorSetLayout_(Device_, &SetLayoutSpec, nullptr, &OutProgram.SetLayout) != VK_SUCCESS)
    {
        ReleaseProgram(OutProgram);
        return false;
    }

    VkPushConstantRange PushRange{};
    PushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    PushRange.offset     = 0u;
    PushRange.size       = PushConstantBytes;

    VkPipelineLayoutCreateInfo PipeLayoutSpec{};
    PipeLayoutSpec.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    PipeLayoutSpec.setLayoutCount         = 1u;
    PipeLayoutSpec.pSetLayouts            = &OutProgram.SetLayout;
    PipeLayoutSpec.pushConstantRangeCount = PushConstantBytes > 0u ? 1u : 0u;
    PipeLayoutSpec.pPushConstantRanges    = PushConstantBytes > 0u ? &PushRange : nullptr;

    if (pfnCreatePipelineLayout_(Device_, &PipeLayoutSpec, nullptr, &OutProgram.Layout) != VK_SUCCESS)
    {
        ReleaseProgram(OutProgram);
        return false;
    }

    std::string ResolvedEntryName = (EntryPointName && EntryPointName[0] != '\0') ? EntryPointName : "main";
    {
        std::vector<std::string> ComputeEntries;
        size_t                   WordIndex = 5u;
        while (WordIndex < Words.size())
        {
            const uint32_t WordCount = Words[WordIndex] >> 16u;
            const uint32_t Opcode    = Words[WordIndex] & 0xFFFFu;
            if (WordCount == 0u || WordIndex + WordCount > Words.size())
            {
                break;
            }
            if (Opcode == 15u && WordCount >= 4u && Words[WordIndex + 1u] == 5u)
            {
                const char*  RawChars = reinterpret_cast<const char*>(&Words[WordIndex + 3u]);
                const size_t MaxBytes = (WordCount - 3u) * sizeof(uint32_t);
                size_t       Length   = 0u;
                while (Length < MaxBytes && RawChars[Length] != '\0')
                {
                    ++Length;
                }
                ComputeEntries.emplace_back(RawChars, Length);
            }
            WordIndex += WordCount;
        }
        if (!ComputeEntries.empty() &&
            std::find(ComputeEntries.begin(), ComputeEntries.end(), ResolvedEntryName) == ComputeEntries.end())
        {
            ResolvedEntryName = ComputeEntries.front();
        }
    }

    VkComputePipelineCreateInfo PipeSpec{};
    PipeSpec.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    PipeSpec.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    PipeSpec.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    PipeSpec.stage.module = OutProgram.ShaderUnit;
    PipeSpec.stage.pName  = ResolvedEntryName.c_str();
    PipeSpec.layout       = OutProgram.Layout;

    if (pfnCreateComputePipelines_(Device_, VK_NULL_HANDLE, 1u, &PipeSpec, nullptr, &OutProgram.Pipeline) != VK_SUCCESS)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = std::string("vkCreateComputePipelines failed for entry ") + EntryPointName;
        }
        ReleaseProgram(OutProgram);
        return false;
    }

    OutProgram.BindingTypes.assign(BindingTypes.begin(), BindingTypes.end());
    return true;
}

void VulkanComputeExchange::ReleaseProgram(ComputeProgramSlot& Program) noexcept
{
    if (!Ready())
    {
        Program = ComputeProgramSlot{};
        return;
    }
    if (Program.Pipeline != VK_NULL_HANDLE)
    {
        pfnDestroyPipeline_(Device_, Program.Pipeline, nullptr);
    }
    if (Program.Layout != VK_NULL_HANDLE)
    {
        pfnDestroyPipelineLayout_(Device_, Program.Layout, nullptr);
    }
    if (Program.SetLayout != VK_NULL_HANDLE)
    {
        pfnDestroyDescriptorSetLayout_(Device_, Program.SetLayout, nullptr);
    }
    if (Program.ShaderUnit != VK_NULL_HANDLE)
    {
        pfnDestroyShaderUnit_(Device_, Program.ShaderUnit, nullptr);
    }
    Program = ComputeProgramSlot{};
}

bool VulkanComputeExchange::DispatchCompute(const ComputeProgramSlot&                  Program,
                                            std::span<const ComputeDescriptorArgument> Arguments,
                                            const void*                                PushConstants,
                                            uint32_t                                   PushConstantBytes,
                                            uint32_t                                   GroupCountX,
                                            uint32_t                                   GroupCountY,
                                            uint32_t                                   GroupCountZ)
{
    if (!Ready() || Program.Pipeline == VK_NULL_HANDLE || Arguments.size() != Program.BindingTypes.size())
    {
        return false;
    }
    if (GroupCountX == 0u || GroupCountY == 0u || GroupCountZ == 0u)
    {
        return true;
    }

    pfnResetDescriptorPool_(Device_, DescriptorPool_, 0u);
    VkDescriptorSetAllocateInfo SetAlloc{};
    SetAlloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    SetAlloc.descriptorPool     = DescriptorPool_;
    SetAlloc.descriptorSetCount = 1u;
    SetAlloc.pSetLayouts        = &Program.SetLayout;
    VkDescriptorSet DescSet     = VK_NULL_HANDLE;
    if (pfnAllocateDescriptorSets_(Device_, &SetAlloc, &DescSet) != VK_SUCCESS)
    {
        return false;
    }

    std::vector<VkDescriptorBufferInfo> SpanDescriptors(Arguments.size());
    std::vector<VkDescriptorImageInfo>  ImageDescriptors(Arguments.size());
    std::vector<VkWriteDescriptorSet>   Writes(Arguments.size());

    for (size_t SlotIndex = 0u; SlotIndex < Arguments.size(); ++SlotIndex)
    {
        Writes[SlotIndex].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[SlotIndex].dstSet          = DescSet;
        Writes[SlotIndex].dstBinding      = static_cast<uint32_t>(SlotIndex);
        Writes[SlotIndex].descriptorCount = 1u;
        Writes[SlotIndex].descriptorType  = Program.BindingTypes[SlotIndex];

        if (Program.BindingTypes[SlotIndex] == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        {
            if (!Arguments[SlotIndex].MemorySpan || Arguments[SlotIndex].MemorySpan->Handle == VK_NULL_HANDLE)
            {
                return false;
            }
            SpanDescriptors[SlotIndex].buffer = Arguments[SlotIndex].MemorySpan->Handle;
            SpanDescriptors[SlotIndex].offset = 0u;
            SpanDescriptors[SlotIndex].range  = VK_WHOLE_SIZE;
            Writes[SlotIndex].pBufferInfo     = &SpanDescriptors[SlotIndex];
        }
        else
        {
            if (!Arguments[SlotIndex].ImageArray || Arguments[SlotIndex].ImageArray->ArrayView == VK_NULL_HANDLE)
            {
                return false;
            }
            ImageDescriptors[SlotIndex].imageView = Arguments[SlotIndex].ImageArray->ArrayView;
            ImageDescriptors[SlotIndex].sampler   = (Program.BindingTypes[SlotIndex] == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                                                        ? (Arguments[SlotIndex].ImageArray->Format == VK_FORMAT_R8G8B8A8_SRGB
                                                               ? LinearRepeatSampler_
                                                               : LinearClampSampler_)
                                                        : VK_NULL_HANDLE;
            ImageDescriptors[SlotIndex].imageLayout = (Arguments[SlotIndex].ImageArray->Format == VK_FORMAT_R8G8B8A8_SRGB)
                                                          ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                          : VK_IMAGE_LAYOUT_GENERAL;
            Writes[SlotIndex].pImageInfo = &ImageDescriptors[SlotIndex];
        }
    }
    pfnUpdateDescriptorSets_(Device_, static_cast<uint32_t>(Writes.size()), Writes.data(), 0u, nullptr);

    VkCommandBufferAllocateInfo StreamAlloc{};
    StreamAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    StreamAlloc.commandPool        = CommandPool_;
    StreamAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    StreamAlloc.commandBufferCount = 1u;
    VkCommandBuffer CommandStream  = VK_NULL_HANDLE;
    if (pfnAllocateCommandStreams_(Device_, &StreamAlloc, &CommandStream) != VK_SUCCESS)
    {
        return false;
    }

    VkCommandBufferBeginInfo BeginSpec{};
    BeginSpec.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    BeginSpec.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pfnBeginCommandStream_(CommandStream, &BeginSpec);

    pfnCmdBindPipeline_(CommandStream, VK_PIPELINE_BIND_POINT_COMPUTE, Program.Pipeline);
    pfnCmdBindDescriptorSets_(CommandStream, VK_PIPELINE_BIND_POINT_COMPUTE, Program.Layout, 0u, 1u, &DescSet, 0u, nullptr);
    if (PushConstants && PushConstantBytes > 0u)
    {
        pfnCmdPushConstants_(CommandStream, Program.Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0u, PushConstantBytes, PushConstants);
    }
    pfnCmdDispatch_(CommandStream, GroupCountX, GroupCountY, GroupCountZ);

    VkMemoryBarrier Barrier{};
    Barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    pfnCmdPipelineBarrier_(CommandStream,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0u,
                           1u,
                           &Barrier,
                           0u,
                           nullptr,
                           0u,
                           nullptr);

    pfnEndCommandStream_(CommandStream);

    pfnResetFences_(Device_, 1u, &SubmitFence_);
    VkSubmitInfo SubmitSpec{};
    SubmitSpec.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    SubmitSpec.commandBufferCount = 1u;
    SubmitSpec.pCommandBuffers    = &CommandStream;
    const bool Ok = (pfnQueueSubmit_(Queue_, 1u, &SubmitSpec, SubmitFence_) == VK_SUCCESS) &&
                    (pfnWaitForFences_(Device_, 1u, &SubmitFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS);
    pfnFreeCommandStreams_(Device_, CommandPool_, 1u, &CommandStream);
    return Ok;
}

} // namespace Frontier
