/*==========================================================================================================================================
                                                           DEVICEEXCHANGE.CPP
==========================================================================================================================================*/
// 🧩 Vulkan raster + compute device exchange with WebGPU ordering semantics, for the 1:1 VehicleHMI illumination port.

#include "DeviceExchange.h"

#include <chrono>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <tuple>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   FUNCTION TABLE
//------------------------------------------------------------------------------------------------------------------------

#define FRONTIER_EXCHANGE_INSTANCE_FUNCTIONS(X)                                                                            \
    X(vkDestroyInstance)                                                                                                   \
    X(vkEnumeratePhysicalDevices)                                                                                          \
    X(vkGetPhysicalDeviceProperties)                                                                                       \
    X(vkGetPhysicalDeviceFeatures)                                                                                         \
    X(vkGetPhysicalDeviceFeatures2)                                                                                        \
    X(vkGetPhysicalDeviceMemoryProperties)                                                                                 \
    X(vkGetPhysicalDeviceQueueFamilyProperties)                                                                            \
    X(vkCreateDevice)                                                                                                      \
    X(vkGetDeviceProcAddr)

#define FRONTIER_EXCHANGE_DEVICE_FUNCTIONS(X)                                                                              \
    X(vkDestroyDevice)                                                                                                     \
    X(vkGetDeviceQueue)                                                                                                    \
    X(vkCreateCommandPool)                                                                                                 \
    X(vkDestroyCommandPool)                                                                                                \
    X(vkAllocateCommandBuffers)                                                                                            \
    X(vkFreeCommandBuffers)                                                                                                \
    X(vkBeginCommandBuffer)                                                                                                \
    X(vkEndCommandBuffer)                                                                                                  \
    X(vkQueueSubmit)                                                                                                       \
    X(vkQueueWaitIdle)                                                                                                     \
    X(vkDeviceWaitIdle)                                                                                                    \
    X(vkCreateFence)                                                                                                       \
    X(vkDestroyFence)                                                                                                      \
    X(vkWaitForFences)                                                                                                     \
    X(vkGetFenceStatus)                                                                                                    \
    X(vkResetFences)                                                                                                       \
    X(vkCreateBuffer)                                                                                                      \
    X(vkDestroyBuffer)                                                                                                     \
    X(vkGetBufferMemoryRequirements)                                                                                       \
    X(vkAllocateMemory)                                                                                                    \
    X(vkFreeMemory)                                                                                                        \
    X(vkBindBufferMemory)                                                                                                  \
    X(vkMapMemory)                                                                                                         \
    X(vkUnmapMemory)                                                                                                       \
    X(vkCreateImage)                                                                                                       \
    X(vkDestroyImage)                                                                                                      \
    X(vkGetImageMemoryRequirements)                                                                                        \
    X(vkBindImageMemory)                                                                                                   \
    X(vkCreateImageView)                                                                                                   \
    X(vkDestroyImageView)                                                                                                  \
    X(vkCreateSampler)                                                                                                     \
    X(vkDestroySampler)                                                                                                    \
    X(vkCreateShaderModule)                                                                                                \
    X(vkDestroyShaderModule)                                                                                               \
    X(vkCreateDescriptorSetLayout)                                                                                         \
    X(vkDestroyDescriptorSetLayout)                                                                                        \
    X(vkCreatePipelineLayout)                                                                                              \
    X(vkDestroyPipelineLayout)                                                                                             \
    X(vkCreateComputePipelines)                                                                                            \
    X(vkCreateGraphicsPipelines)                                                                                           \
    X(vkDestroyPipeline)                                                                                                   \
    X(vkCreateDescriptorPool)                                                                                              \
    X(vkDestroyDescriptorPool)                                                                                             \
    X(vkAllocateDescriptorSets)                                                                                            \
    X(vkFreeDescriptorSets)                                                                                                \
    X(vkUpdateDescriptorSets)                                                                                              \
    X(vkCmdPipelineBarrier)                                                                                                \
    X(vkCmdBindPipeline)                                                                                                   \
    X(vkCmdBindDescriptorSets)                                                                                             \
    X(vkCmdDispatch)                                                                                                       \
    X(vkCmdCopyBuffer)                                                                                                     \
    X(vkCmdCopyBufferToImage)                                                                                              \
    X(vkCmdCopyImageToBuffer)                                                                                              \
    X(vkCmdClearColorImage)                                                                                                \
    X(vkCmdBindVertexBuffers)                                                                                              \
    X(vkCmdBindIndexBuffer)                                                                                                \
    X(vkCmdDraw)                                                                                                           \
    X(vkCmdDrawIndexedIndirect)                                                                                            \
    X(vkCmdDrawIndexedIndirectCount)                                                                                       \
    X(vkCmdSetViewport)                                                                                                    \
    X(vkCmdSetScissor)

struct DeviceExchange::ResolvedFunctions
{
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
    PFN_vkCreateInstance      vkCreateInstance      = nullptr;
    PFN_vkCmdBeginRendering   vkCmdBeginRendering   = nullptr;
    PFN_vkCmdEndRendering     vkCmdEndRendering     = nullptr;
#define FRONTIER_EXCHANGE_DECLARE(Name) PFN_##Name Name = nullptr;
    FRONTIER_EXCHANGE_INSTANCE_FUNCTIONS(FRONTIER_EXCHANGE_DECLARE)
    FRONTIER_EXCHANGE_DEVICE_FUNCTIONS(FRONTIER_EXCHANGE_DECLARE)
#undef FRONTIER_EXCHANGE_DECLARE
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   FORMAT TRANSLATION
//------------------------------------------------------------------------------------------------------------------------

VkFormat TranslateTextureFormat(TextureFormat Format) noexcept
{
    switch (Format)
    {
        case TextureFormat::Rgba16Float:    return VK_FORMAT_R16G16B16A16_SFLOAT;
        case TextureFormat::Rg16Float:      return VK_FORMAT_R16G16_SFLOAT;
        case TextureFormat::R16Float:       return VK_FORMAT_R16_SFLOAT;
        case TextureFormat::R32Float:       return VK_FORMAT_R32_SFLOAT;
        case TextureFormat::Rgba32Float:    return VK_FORMAT_R32G32B32A32_SFLOAT;
        case TextureFormat::Rgba8Unorm:     return VK_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::Rgba8UnormSrgb: return VK_FORMAT_R8G8B8A8_SRGB;
        case TextureFormat::R8Unorm:        return VK_FORMAT_R8_UNORM;
        case TextureFormat::R32Uint:        return VK_FORMAT_R32_UINT;
        case TextureFormat::Rg32Uint:       return VK_FORMAT_R32G32_UINT;
        case TextureFormat::Depth32Float:   return VK_FORMAT_D32_SFLOAT;
    }
    return VK_FORMAT_UNDEFINED;
}

uint32_t QueryTexelBytes(TextureFormat Format) noexcept
{
    switch (Format)
    {
        case TextureFormat::Rgba16Float:    return 8u;
        case TextureFormat::Rg16Float:      return 4u;
        case TextureFormat::R16Float:       return 2u;
        case TextureFormat::R32Float:       return 4u;
        case TextureFormat::Rgba32Float:    return 16u;
        case TextureFormat::Rgba8Unorm:     return 4u;
        case TextureFormat::Rgba8UnormSrgb: return 4u;
        case TextureFormat::R8Unorm:        return 1u;
        case TextureFormat::R32Uint:        return 4u;
        case TextureFormat::Rg32Uint:       return 8u;
        case TextureFormat::Depth32Float:   return 4u;
    }
    return 0u;
}

namespace
{

bool DepthFormat(TextureFormat Format) noexcept
{
    return Format == TextureFormat::Depth32Float;
}

VkImageAspectFlags AspectOf(TextureFormat Format) noexcept
{
    return DepthFormat(Format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}

VkFilter TranslateFilter(FilterMode Mode) noexcept
{
    return Mode == FilterMode::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
}

VkSamplerAddressMode TranslateAddress(AddressMode Mode) noexcept
{
    switch (Mode)
    {
        case AddressMode::Repeat:       return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case AddressMode::MirrorRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case AddressMode::ClampToEdge:  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}

VkCompareOp TranslateCompare(CompareFunction Function) noexcept
{
    switch (Function)
    {
        case CompareFunction::Never:        return VK_COMPARE_OP_NEVER;
        case CompareFunction::Less:         return VK_COMPARE_OP_LESS;
        case CompareFunction::Equal:        return VK_COMPARE_OP_EQUAL;
        case CompareFunction::LessEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareFunction::Greater:      return VK_COMPARE_OP_GREATER;
        case CompareFunction::NotEqual:     return VK_COMPARE_OP_NOT_EQUAL;
        case CompareFunction::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareFunction::Always:       return VK_COMPARE_OP_ALWAYS;
        case CompareFunction::Disabled:     return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_ALWAYS;
}

VkBlendFactor TranslateBlendFactor(BlendFactor Factor) noexcept
{
    switch (Factor)
    {
        case BlendFactor::Zero:                     return VK_BLEND_FACTOR_ZERO;
        case BlendFactor::One:                      return VK_BLEND_FACTOR_ONE;
        case BlendFactor::SourceAlpha:              return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendFactor::OneMinusSourceAlpha:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DestinationAlpha:         return VK_BLEND_FACTOR_DST_ALPHA;
        case BlendFactor::OneMinusDestinationAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case BlendFactor::Source:                   return VK_BLEND_FACTOR_SRC_COLOR;
        case BlendFactor::OneMinusSource:           return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::Destination:              return VK_BLEND_FACTOR_DST_COLOR;
        case BlendFactor::OneMinusDestination:      return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    }
    return VK_BLEND_FACTOR_ONE;
}

VkBlendOp TranslateBlendOperation(BlendOperation Operation) noexcept
{
    switch (Operation)
    {
        case BlendOperation::Add:             return VK_BLEND_OP_ADD;
        case BlendOperation::Subtract:        return VK_BLEND_OP_SUBTRACT;
        case BlendOperation::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case BlendOperation::Minimum:         return VK_BLEND_OP_MIN;
        case BlendOperation::Maximum:         return VK_BLEND_OP_MAX;
    }
    return VK_BLEND_OP_ADD;
}

VkFormat TranslateVertexFormat(VertexFormat Format) noexcept
{
    switch (Format)
    {
        case VertexFormat::Float32x2: return VK_FORMAT_R32G32_SFLOAT;
        case VertexFormat::Float32x3: return VK_FORMAT_R32G32B32_SFLOAT;
        case VertexFormat::Float32x4: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case VertexFormat::Uint32:    return VK_FORMAT_R32_UINT;
        case VertexFormat::Uint32x2:  return VK_FORMAT_R32G32_UINT;
        case VertexFormat::Uint32x4:  return VK_FORMAT_R32G32B32A32_UINT;
    }
    return VK_FORMAT_UNDEFINED;
}

VkDescriptorType TranslateDescriptorCode(DescriptorCode Code) noexcept
{
    switch (Code)
    {
        case DescriptorCode::Uniform:               return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case DescriptorCode::UniformDynamic:        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        case DescriptorCode::StorageRead:           return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case DescriptorCode::Storage:               return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case DescriptorCode::Texture2D:             return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case DescriptorCode::Texture2DUnfiltered:   return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case DescriptorCode::Texture2DLayered:      return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case DescriptorCode::Texture3D:             return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case DescriptorCode::TextureUint2D:         return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case DescriptorCode::TextureDepth2D:        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case DescriptorCode::TextureDepth2DLayered: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case DescriptorCode::StorageTexture2D:      return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case DescriptorCode::StorageTexture3D:      return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case DescriptorCode::Sampler:               return VK_DESCRIPTOR_TYPE_SAMPLER;
        case DescriptorCode::SamplerUnfiltered:     return VK_DESCRIPTOR_TYPE_SAMPLER;
        case DescriptorCode::SamplerComparison:     return VK_DESCRIPTOR_TYPE_SAMPLER;
    }
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}

void ReportDiagnostic(std::string* OutDiagnostic, const std::string& Message)
{
    if (OutDiagnostic != nullptr)
    {
        *OutDiagnostic = Message;
    }
}

std::string NameResult(VkResult Result)
{
    switch (Result)
    {
        case VK_SUCCESS:                    return "VK_SUCCESS";
        case VK_NOT_READY:                  return "VK_NOT_READY";
        case VK_TIMEOUT:                    return "VK_TIMEOUT";
        case VK_ERROR_OUT_OF_HOST_MEMORY:   return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:          return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:    return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_OUT_OF_POOL_MEMORY:   return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_ERROR_FRAGMENTED_POOL:      return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_UNKNOWN:              return "VK_ERROR_UNKNOWN";
        default:                            return "VkResult " + std::to_string(static_cast<int>(Result));
    }
}

} // namespace

SpecializationConstant SpecializationConstant::FromFloat(float Scalar)
{
    SpecializationConstant Result;
    Result.Kind = Category::Float;
    std::memcpy(&Result.Bits, &Scalar, sizeof(float));
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   RESOURCE DESTRUCTORS
//------------------------------------------------------------------------------------------------------------------------

DeviceRegion::~DeviceRegion()
{
    if (Exchange != nullptr) Exchange->ReleaseRegion(*this);
}

DeviceTexture::~DeviceTexture()
{
    if (Exchange != nullptr) Exchange->ReleaseTexture(*this);
}

DeviceTextureView::~DeviceTextureView()
{
    if (Exchange != nullptr) Exchange->ReleaseView(*this);
}

DeviceSampler::~DeviceSampler()
{
    if (Exchange != nullptr) Exchange->ReleaseSampler(*this);
}

DescriptorLayout::~DescriptorLayout()
{
    if (Exchange != nullptr) Exchange->ReleaseDescriptorLayout(*this);
}

DescriptorGroup::~DescriptorGroup()
{
    if (Exchange != nullptr) Exchange->ReleaseDescriptorGroup(*this);
}

DeviceProgram::~DeviceProgram()
{
    if (Exchange != nullptr) Exchange->ReleaseProgram(*this);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

DeviceExchange::DeviceExchange() = default;

DeviceExchange::~DeviceExchange()
{
    Shutdown();
}

bool DeviceExchange::Initialize(const std::string& ExplicitDriverLibraryPath, std::string* OutDiagnostic)
{
    Functions_ = std::make_unique<ResolvedFunctions>();
    std::string LibraryPath = ExplicitDriverLibraryPath;
    if (LibraryPath.empty())
    {
        if (const char* Environment = std::getenv("FRONTIER_VULKAN_DRIVER"))
        {
            LibraryPath = Environment;
        }
    }
#ifdef _WIN32
    if (LibraryPath.empty()) LibraryPath = "vulkan-1.dll";
    LibraryHandle_ = reinterpret_cast<void*>(LoadLibraryA(LibraryPath.c_str()));
    if (LibraryHandle_ == nullptr)
    {
        ReportDiagnostic(OutDiagnostic, "Vulkan loader not found: " + LibraryPath);
        return false;
    }
    Functions_->vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        GetProcAddress(reinterpret_cast<HMODULE>(LibraryHandle_), "vkGetInstanceProcAddr"));
#else
    if (LibraryPath.empty()) LibraryPath = "libvulkan.so.1";
    LibraryHandle_ = dlopen(LibraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (LibraryHandle_ == nullptr)
    {
        ReportDiagnostic(OutDiagnostic, "Vulkan loader not found: " + LibraryPath);
        return false;
    }
    Functions_->vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(LibraryHandle_, "vkGetInstanceProcAddr"));
#endif
    if (Functions_->vkGetInstanceProcAddr == nullptr)
    {
        ReportDiagnostic(OutDiagnostic, "vkGetInstanceProcAddr missing in " + LibraryPath);
        return false;
    }
    Functions_->vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(Functions_->vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));

    VkApplicationInfo Application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    Application.pApplicationName = "Frontier Illumination";
    Application.pEngineName      = "Frontier";
    Application.apiVersion       = VK_API_VERSION_1_3;
    VkInstanceCreateInfo InstanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    InstanceInfo.pApplicationInfo = &Application;
    if (Functions_->vkCreateInstance(&InstanceInfo, nullptr, &Instance_) != VK_SUCCESS)
    {
        ReportDiagnostic(OutDiagnostic, "vkCreateInstance failed (Vulkan 1.3 required)");
        return false;
    }
#define FRONTIER_EXCHANGE_LOAD_INSTANCE(Name) \
    Functions_->Name = reinterpret_cast<PFN_##Name>(Functions_->vkGetInstanceProcAddr(Instance_, #Name));
    FRONTIER_EXCHANGE_INSTANCE_FUNCTIONS(FRONTIER_EXCHANGE_LOAD_INSTANCE)
#undef FRONTIER_EXCHANGE_LOAD_INSTANCE

    uint32_t DeviceCount = 0;
    Functions_->vkEnumeratePhysicalDevices(Instance_, &DeviceCount, nullptr);
    if (DeviceCount == 0)
    {
        ReportDiagnostic(OutDiagnostic, "no Vulkan physical device");
        return false;
    }
    std::vector<VkPhysicalDevice> Devices(DeviceCount);
    Functions_->vkEnumeratePhysicalDevices(Instance_, &DeviceCount, Devices.data());
    // 📝 Prefer a discrete GPU (the demo requests powerPreference "high-performance"), else the first device.
    PhysicalDevice_ = Devices[0];
    for (VkPhysicalDevice Candidate : Devices)
    {
        VkPhysicalDeviceProperties Properties{};
        Functions_->vkGetPhysicalDeviceProperties(Candidate, &Properties);
        if (Properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            PhysicalDevice_ = Candidate;
            break;
        }
    }
    if (DescribePhysicalDevice() < VK_API_VERSION_1_3)
    {
        ReportDiagnostic(OutDiagnostic, "device " + AdapterDescription_ + " lacks Vulkan 1.3 (dynamic rendering)");
        return false;
    }

    uint32_t FamilyCount = 0;
    Functions_->vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice_, &FamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> Families(FamilyCount);
    Functions_->vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice_, &FamilyCount, Families.data());
    bool FamilyFound = false;
    for (uint32_t Family = 0; Family < FamilyCount; ++Family)
    {
        if ((Families[Family].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
        {
            QueueFamily_ = Family;
            FamilyFound  = true;
            break;
        }
    }
    if (!FamilyFound)
    {
        ReportDiagnostic(OutDiagnostic, "no graphics+compute queue family");
        return false;
    }

    VkPhysicalDeviceFeatures Supported{};
    Functions_->vkGetPhysicalDeviceFeatures(PhysicalDevice_, &Supported);
    VkPhysicalDeviceFeatures Enabled{};
    Enabled.depthBiasClamp                       = Supported.depthBiasClamp;
    Enabled.depthClamp                           = Supported.depthClamp;
    Enabled.fragmentStoresAndAtomics             = Supported.fragmentStoresAndAtomics;
    Enabled.independentBlend                     = Supported.independentBlend;
    Enabled.shaderStorageImageWriteWithoutFormat = Supported.shaderStorageImageWriteWithoutFormat;
    Enabled.robustBufferAccess                   = Supported.robustBufferAccess;
    Enabled.samplerAnisotropy                    = Supported.samplerAnisotropy;
    Enabled.multiDrawIndirect                    = Supported.multiDrawIndirect;
    Enabled.textureCompressionBC                 = VK_FALSE;

    VkPhysicalDeviceVulkan13Features Features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features Features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan11Features Features11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    Features13.pNext = &Features12;
    Features12.pNext = &Features11;
    VkPhysicalDeviceFeatures2 Query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    Query.pNext = &Features13;
    Functions_->vkGetPhysicalDeviceFeatures2(PhysicalDevice_, &Query);
    if (!Features13.dynamicRendering)
    {
        ReportDiagnostic(OutDiagnostic, "device lacks dynamicRendering");
        return false;
    }
    VkPhysicalDeviceVulkan13Features Enable13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    Enable13.dynamicRendering = VK_TRUE;
    Enable13.synchronization2 = Features13.synchronization2;
    // 📝 WebGPU never faults on an image access outside the image (Tint clamps loads, drops stores); robustImageAccess, which
    //    every Vulkan 1.3 device supports, gives the ported shaders that guarantee.
    Enable13.robustImageAccess = Features13.robustImageAccess;
    VkPhysicalDeviceVulkan12Features Enable12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    Enable12.scalarBlockLayout = Features12.scalarBlockLayout;
    Enable12.drawIndirectCount = Features12.drawIndirectCount;
    Capabilities_.DrawIndirectCount = Features12.drawIndirectCount == VK_TRUE;
    Capabilities_.MultiDrawIndirect = Supported.multiDrawIndirect == VK_TRUE;
    Enable13.pNext = &Enable12;
    VkPhysicalDeviceVulkan11Features Enable11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    Enable11.shaderDrawParameters = Features11.shaderDrawParameters;
    Enable12.pNext = &Enable11;

    const float              Priority = 1.0f;
    VkDeviceQueueCreateInfo  QueueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    QueueInfo.queueFamilyIndex = QueueFamily_;
    QueueInfo.queueCount       = 1;
    QueueInfo.pQueuePriorities = &Priority;
    VkDeviceCreateInfo DeviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    DeviceInfo.pNext                = &Enable13;
    DeviceInfo.queueCreateInfoCount = 1;
    DeviceInfo.pQueueCreateInfos    = &QueueInfo;
    DeviceInfo.pEnabledFeatures     = &Enabled;
    if (Functions_->vkCreateDevice(PhysicalDevice_, &DeviceInfo, nullptr, &Device_) != VK_SUCCESS)
    {
        ReportDiagnostic(OutDiagnostic, "vkCreateDevice failed");
        Device_ = VK_NULL_HANDLE;
        return false;
    }
    return CompleteDevice(OutDiagnostic);
}

uint32_t DeviceExchange::DescribePhysicalDevice()
{
    VkPhysicalDeviceProperties Properties{};
    Functions_->vkGetPhysicalDeviceProperties(PhysicalDevice_, &Properties);
    AdapterDescription_ = Properties.deviceName;
    Limits_.MaximumTexture3D        = Properties.limits.maxImageDimension3D;
    Limits_.MaximumTexture2D        = Properties.limits.maxImageDimension2D;
    Limits_.MaximumComputeGroups    = Properties.limits.maxComputeWorkGroupCount[0];
    Limits_.MinimumUniformAlignment = static_cast<uint32_t>(Properties.limits.minUniformBufferOffsetAlignment);
    Limits_.MinimumStorageAlignment = static_cast<uint32_t>(Properties.limits.minStorageBufferOffsetAlignment);
    Functions_->vkGetPhysicalDeviceMemoryProperties(PhysicalDevice_, &MemoryProperties_);
    return Properties.apiVersion;
}

// 📝 Everything after the device exists: device entry points, the queue (unless adopted), pools, fence and staging.
bool DeviceExchange::CompleteDevice(std::string* OutDiagnostic)
{
#define FRONTIER_EXCHANGE_LOAD_DEVICE(Name) \
    Functions_->Name = reinterpret_cast<PFN_##Name>(Functions_->vkGetDeviceProcAddr(Device_, #Name));
    FRONTIER_EXCHANGE_DEVICE_FUNCTIONS(FRONTIER_EXCHANGE_LOAD_DEVICE)
#undef FRONTIER_EXCHANGE_LOAD_DEVICE
    Functions_->vkCmdBeginRendering = reinterpret_cast<PFN_vkCmdBeginRendering>(Functions_->vkGetDeviceProcAddr(Device_, "vkCmdBeginRendering"));
    Functions_->vkCmdEndRendering   = reinterpret_cast<PFN_vkCmdEndRendering>(Functions_->vkGetDeviceProcAddr(Device_, "vkCmdEndRendering"));
    if (Queue_ == VK_NULL_HANDLE) Functions_->vkGetDeviceQueue(Device_, QueueFamily_, 0, &Queue_);
    if (!Functions_->vkCmdDrawIndexedIndirectCount) Capabilities_.DrawIndirectCount = false;

    VkCommandPoolCreateInfo PoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    PoolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    PoolInfo.queueFamilyIndex = QueueFamily_;
    Functions_->vkCreateCommandPool(Device_, &PoolInfo, nullptr, &CommandPool_);

    const std::array<VkDescriptorPoolSize, 5> PoolSizes = {{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4096},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 256},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8192},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8192},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4096},
    }};
    std::array<VkDescriptorPoolSize, 6> AllSizes{};
    std::copy(PoolSizes.begin(), PoolSizes.end(), AllSizes.begin());
    AllSizes[5] = {VK_DESCRIPTOR_TYPE_SAMPLER, 4096};
    VkDescriptorPoolCreateInfo DescriptorInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    DescriptorInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    DescriptorInfo.maxSets       = 4096;
    DescriptorInfo.poolSizeCount = static_cast<uint32_t>(AllSizes.size());
    DescriptorInfo.pPoolSizes    = AllSizes.data();
    Functions_->vkCreateDescriptorPool(Device_, &DescriptorInfo, nullptr, &DescriptorPool_);

    VkFenceCreateInfo FenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    Functions_->vkCreateFence(Device_, &FenceInfo, nullptr, &SubmitFence_);
    if (CommandPool_ == VK_NULL_HANDLE || DescriptorPool_ == VK_NULL_HANDLE || SubmitFence_ == VK_NULL_HANDLE)
    {
        ReportDiagnostic(OutDiagnostic, "command pool, descriptor pool or fence creation failed");
        return false;
    }
    return EnsureStaging(64ull << 20);
}

VulkanDeviceAccess DeviceExchange::QueryDeviceAccess() const noexcept
{
    VulkanDeviceAccess Access;
    Access.Instance        = Instance_;
    Access.PhysicalDevice  = PhysicalDevice_;
    Access.Device          = Device_;
    Access.Queue           = Queue_;
    Access.QueueFamily     = QueueFamily_;
    Access.ResolveInstance = Functions_ ? Functions_->vkGetInstanceProcAddr : nullptr;
    Access.ApiVersion      = VK_API_VERSION_1_3;
    Access.DrawIndirectCount = Capabilities_.DrawIndirectCount;
    Access.MultiDrawIndirect = Capabilities_.MultiDrawIndirect;
    return Access;
}

bool DeviceExchange::AttachDevice(const VulkanDeviceAccess& Access, std::string* OutDiagnostic)
{
    Shutdown();
    if (!Access.Valid())
    {
        ReportDiagnostic(OutDiagnostic, "adopted device incomplete");
        return false;
    }
    Functions_ = std::make_unique<ResolvedFunctions>();
    Functions_->vkGetInstanceProcAddr = Access.ResolveInstance;
    Instance_ = Access.Instance;
#define FRONTIER_EXCHANGE_LOAD_INSTANCE(Name) \
    Functions_->Name = reinterpret_cast<PFN_##Name>(Functions_->vkGetInstanceProcAddr(Instance_, #Name));
    FRONTIER_EXCHANGE_INSTANCE_FUNCTIONS(FRONTIER_EXCHANGE_LOAD_INSTANCE)
#undef FRONTIER_EXCHANGE_LOAD_INSTANCE
    PhysicalDevice_ = Access.PhysicalDevice;
    Device_         = Access.Device;
    Queue_          = Access.Queue;
    QueueFamily_    = Access.QueueFamily;
    DeviceAttached_ = true;
    Capabilities_.DrawIndirectCount = Access.DrawIndirectCount;
    Capabilities_.MultiDrawIndirect = Access.MultiDrawIndirect;
    if (DescribePhysicalDevice() < VK_API_VERSION_1_3 || Access.ApiVersion < VK_API_VERSION_1_3)
    {
        ReportDiagnostic(OutDiagnostic, "adopted device " + AdapterDescription_ + " was not created for Vulkan 1.3 (dynamic rendering)");
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

void DeviceExchange::Shutdown() noexcept
{
    if (Device_ != VK_NULL_HANDLE)
    {
        Functions_->vkDeviceWaitIdle(Device_);
        PendingRegions_.clear();
        PendingTextures_.clear();
        PendingTransitions_.clear();
        if (StagingHandle_ != VK_NULL_HANDLE)
        {
            Functions_->vkUnmapMemory(Device_, StagingMemory_);
            Functions_->vkDestroyBuffer(Device_, StagingHandle_, nullptr);
            Functions_->vkFreeMemory(Device_, StagingMemory_, nullptr);
            StagingHandle_ = VK_NULL_HANDLE;
        }
        if (SubmitFence_ != VK_NULL_HANDLE) Functions_->vkDestroyFence(Device_, SubmitFence_, nullptr);
        if (DescriptorPool_ != VK_NULL_HANDLE) Functions_->vkDestroyDescriptorPool(Device_, DescriptorPool_, nullptr);
        if (CommandPool_ != VK_NULL_HANDLE) Functions_->vkDestroyCommandPool(Device_, CommandPool_, nullptr);
        if (!DeviceAttached_) Functions_->vkDestroyDevice(Device_, nullptr);
        Device_ = VK_NULL_HANDLE;
        Queue_  = VK_NULL_HANDLE;
        CommandPool_ = VK_NULL_HANDLE;
        DescriptorPool_ = VK_NULL_HANDLE;
        SubmitFence_ = VK_NULL_HANDLE;
    }
    if (Instance_ != VK_NULL_HANDLE)
    {
        if (!DeviceAttached_) Functions_->vkDestroyInstance(Instance_, nullptr);
        Instance_ = VK_NULL_HANDLE;
    }
    DeviceAttached_ = false;
    if (LibraryHandle_ != nullptr)
    {
#ifdef _WIN32
        FreeLibrary(reinterpret_cast<HMODULE>(LibraryHandle_));
#else
        dlclose(LibraryHandle_);
#endif
        LibraryHandle_ = nullptr;
    }
}

uint32_t DeviceExchange::SelectMemoryType(uint32_t TypeBits, VkMemoryPropertyFlags Required) const
{
    for (uint32_t Type = 0; Type < MemoryProperties_.memoryTypeCount; ++Type)
    {
        if ((TypeBits & (1u << Type)) && (MemoryProperties_.memoryTypes[Type].propertyFlags & Required) == Required)
        {
            return Type;
        }
    }
    return UINT32_MAX;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   STAGING
//------------------------------------------------------------------------------------------------------------------------

bool DeviceExchange::EnsureStaging(VkDeviceSize ByteCount)
{
    if (StagingHandle_ != VK_NULL_HANDLE && StagingCursor_ + ByteCount <= StagingCapacity_)
    {
        return true;
    }
    // 📝 Growing invalidates pending staging offsets: run the pending writes first (they precede the next submission
    //    either way, so executing them now keeps WebGPU's write-before-submit order).
    if (StagingHandle_ != VK_NULL_HANDLE && (!PendingRegions_.empty() || !PendingTextures_.empty() || !PendingTransitions_.empty()))
    {
        if (!FlushWrites()) return false;
    }
    if (StagingHandle_ != VK_NULL_HANDLE && StagingCursor_ + ByteCount <= StagingCapacity_)
    {
        return true;
    }
    const VkDeviceSize Required = std::max<VkDeviceSize>(StagingCapacity_ * 2, ByteCount + (16ull << 20));
    if (StagingHandle_ != VK_NULL_HANDLE)
    {
        Functions_->vkUnmapMemory(Device_, StagingMemory_);
        Functions_->vkDestroyBuffer(Device_, StagingHandle_, nullptr);
        Functions_->vkFreeMemory(Device_, StagingMemory_, nullptr);
        StagingHandle_ = VK_NULL_HANDLE;
    }
    VkBufferCreateInfo Info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    Info.size        = Required;
    Info.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    Info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (Functions_->vkCreateBuffer(Device_, &Info, nullptr, &StagingHandle_) != VK_SUCCESS) return false;
    VkMemoryRequirements Requirements{};
    Functions_->vkGetBufferMemoryRequirements(Device_, StagingHandle_, &Requirements);
    VkMemoryAllocateInfo Allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    Allocate.allocationSize  = Requirements.size;
    Allocate.memoryTypeIndex = SelectMemoryType(Requirements.memoryTypeBits,
                                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (Functions_->vkAllocateMemory(Device_, &Allocate, nullptr, &StagingMemory_) != VK_SUCCESS) return false;
    Functions_->vkBindBufferMemory(Device_, StagingHandle_, StagingMemory_, 0);
    void* Mapped = nullptr;
    Functions_->vkMapMemory(Device_, StagingMemory_, 0, VK_WHOLE_SIZE, 0, &Mapped);
    StagingBytes_    = static_cast<uint8_t*>(Mapped);
    StagingCapacity_ = Required;
    StagingCursor_   = 0;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   RESOURCES
//------------------------------------------------------------------------------------------------------------------------

std::shared_ptr<DeviceRegion> DeviceExchange::ConstructRegion(VkDeviceSize ByteCount, const std::string& Label)
{
    // 📝 DeviceExchange.js aligns every allocation to 16 bytes with a 16-byte minimum.
    const VkDeviceSize Aligned = std::max<VkDeviceSize>(16, (ByteCount + 15) / 16 * 16);
    auto Region      = std::make_shared<DeviceRegion>();
    Region->Capacity = Aligned;
    Region->Label    = Label;
    VkBufferCreateInfo Info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    Info.size  = Aligned;
    Info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                 VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    Info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (Functions_->vkCreateBuffer(Device_, &Info, nullptr, &Region->Handle) != VK_SUCCESS) return nullptr;
    VkMemoryRequirements Requirements{};
    Functions_->vkGetBufferMemoryRequirements(Device_, Region->Handle, &Requirements);
    VkMemoryAllocateInfo Allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    Allocate.allocationSize  = Requirements.size;
    Allocate.memoryTypeIndex = SelectMemoryType(Requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (Allocate.memoryTypeIndex == UINT32_MAX)
    {
        Allocate.memoryTypeIndex = SelectMemoryType(Requirements.memoryTypeBits, 0);
    }
    if (Functions_->vkAllocateMemory(Device_, &Allocate, nullptr, &Region->Memory) != VK_SUCCESS)
    {
        Functions_->vkDestroyBuffer(Device_, Region->Handle, nullptr);
        Region->Handle = VK_NULL_HANDLE;
        return nullptr;
    }
    Functions_->vkBindBufferMemory(Device_, Region->Handle, Region->Memory, 0);
    Region->Exchange = this;
    // 📝 WebGPU buffers start zeroed.
    std::vector<uint8_t> Zeros(static_cast<size_t>(Aligned), 0u);
    WriteRegion(Region, 0, Zeros.data(), Aligned);
    return Region;
}

std::shared_ptr<DeviceRegion> DeviceExchange::ConstructRegionFrom(const void* Bytes, VkDeviceSize ByteCount, const std::string& Label)
{
    auto Region = ConstructRegion(ByteCount, Label);
    if (Region && ByteCount > 0)
    {
        WriteRegion(Region, 0, Bytes, ByteCount);
    }
    return Region;
}

std::shared_ptr<DeviceTexture> DeviceExchange::ConstructTexture(const TextureSpecification& Specification)
{
    auto Texture           = std::make_shared<DeviceTexture>();
    Texture->Specification = Specification;
    VkImageCreateInfo Info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    const bool Volume = Specification.Dimension == TextureDimension::Volumetric;
    Info.imageType    = Volume ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    Info.format       = TranslateTextureFormat(Specification.Format);
    Info.extent       = {Specification.Width, Specification.Height, Volume ? Specification.DepthOrLayers : 1u};
    Info.mipLevels    = Specification.Levels;
    Info.arrayLayers  = Volume ? 1u : Specification.DepthOrLayers;
    Info.samples      = VK_SAMPLE_COUNT_1_BIT;
    Info.tiling       = VK_IMAGE_TILING_OPTIMAL;
    Info.usage        = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (Specification.Usage & TextureUsage::Sampled) Info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (Specification.Usage & TextureUsage::Storage) Info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (Specification.Usage & TextureUsage::RenderAttachment)
    {
        Info.usage |= DepthFormat(Specification.Format) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if (!Specification.ViewFormats.empty())
    {
        Info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    }
    Info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    Info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (Functions_->vkCreateImage(Device_, &Info, nullptr, &Texture->Image) != VK_SUCCESS) return nullptr;
    VkMemoryRequirements Requirements{};
    Functions_->vkGetImageMemoryRequirements(Device_, Texture->Image, &Requirements);
    VkMemoryAllocateInfo Allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    Allocate.allocationSize  = Requirements.size;
    Allocate.memoryTypeIndex = SelectMemoryType(Requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (Allocate.memoryTypeIndex == UINT32_MAX) Allocate.memoryTypeIndex = SelectMemoryType(Requirements.memoryTypeBits, 0);
    if (Functions_->vkAllocateMemory(Device_, &Allocate, nullptr, &Texture->Memory) != VK_SUCCESS)
    {
        Functions_->vkDestroyImage(Device_, Texture->Image, nullptr);
        Texture->Image = VK_NULL_HANDLE;
        return nullptr;
    }
    Functions_->vkBindImageMemory(Device_, Texture->Image, Texture->Memory, 0);
    Texture->Exchange = this;
    // 📝 Transition to GENERAL and zero-clear at the head of the next submission (WebGPU textures start zeroed).
    PendingTransitions_.push_back(Texture);
    return Texture;
}

std::shared_ptr<DeviceTexture> DeviceExchange::WrapTexture(VkImage Image, const TextureSpecification& Specification)
{
    if (Image == VK_NULL_HANDLE) return nullptr;
    auto Texture           = std::make_shared<DeviceTexture>();
    Texture->Specification = Specification;
    Texture->Image         = Image;
    Texture->Owned         = false;
    Texture->Exchange      = this;
    return Texture;
}

std::shared_ptr<DeviceTextureView> DeviceExchange::ConstructView(const std::shared_ptr<DeviceTexture>& Texture,
                                                                 const TextureViewSpecification& Specification)
{
    auto View      = std::make_shared<DeviceTextureView>();
    View->Texture  = Texture;
    const auto& Source = Texture->Specification;
    const bool Volume  = Source.Dimension == TextureDimension::Volumetric;
    View->Dimension  = Specification.DimensionSet ? Specification.Dimension
                                                  : (Volume ? ViewDimension::Volumetric
                                                            : (Source.DepthOrLayers > 1 ? ViewDimension::PlanarLayered : ViewDimension::Planar));
    View->Format     = Specification.FormatSet ? Specification.Format : Source.Format;
    View->BaseLayer  = Specification.BaseLayer;
    View->LayerCount = Volume ? 1u : (Specification.LayerCount ? Specification.LayerCount : Source.DepthOrLayers - Specification.BaseLayer);
    View->BaseLevel  = Specification.BaseLevel;
    View->LevelCount = Specification.LevelCount ? Specification.LevelCount : Source.Levels - Specification.BaseLevel;
    VkImageViewCreateInfo Info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    Info.image    = Texture->Image;
    Info.viewType = View->Dimension == ViewDimension::Volumetric ? VK_IMAGE_VIEW_TYPE_3D
                  : (View->Dimension == ViewDimension::PlanarLayered ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D);
    Info.format   = TranslateTextureFormat(View->Format);
    Info.subresourceRange = {AspectOf(Source.Format), View->BaseLevel, View->LevelCount, View->BaseLayer, View->LayerCount};
    if (Functions_->vkCreateImageView(Device_, &Info, nullptr, &View->Handle) != VK_SUCCESS) return nullptr;
    View->Exchange = this;
    return View;
}

std::shared_ptr<DeviceSampler> DeviceExchange::ConstructSampler(const SamplerSpecification& Specification)
{
    auto Sampler = std::make_shared<DeviceSampler>();
    VkSamplerCreateInfo Info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    Info.magFilter     = TranslateFilter(Specification.MagFilter);
    Info.minFilter     = TranslateFilter(Specification.MinFilter);
    Info.mipmapMode    = Specification.MipmapFilter == FilterMode::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    Info.addressModeU  = TranslateAddress(Specification.AddressU);
    Info.addressModeV  = TranslateAddress(Specification.AddressV);
    Info.addressModeW  = TranslateAddress(Specification.AddressW);
    Info.minLod        = Specification.LodMinimum;
    Info.maxLod        = Specification.LodMaximum;
    Info.compareEnable = Specification.Compare != CompareFunction::Disabled ? VK_TRUE : VK_FALSE;
    Info.compareOp     = TranslateCompare(Specification.Compare);
    Info.maxAnisotropy = 1.0f;
    if (Functions_->vkCreateSampler(Device_, &Info, nullptr, &Sampler->Handle) != VK_SUCCESS) return nullptr;
    Sampler->Exchange = this;
    return Sampler;
}

std::shared_ptr<DescriptorLayout> DeviceExchange::ConstructDescriptorLayout(std::span<const DescriptorSlot> Slots, const std::string& Label)
{
    auto Layout   = std::make_shared<DescriptorLayout>();
    Layout->Slots.assign(Slots.begin(), Slots.end());
    Layout->Label = Label;
    std::vector<VkDescriptorSetLayoutBinding> Bindings;
    for (uint32_t Slot = 0; Slot < Slots.size(); ++Slot)
    {
        VkDescriptorSetLayoutBinding Binding{};
        Binding.binding         = Slot;
        Binding.descriptorType  = TranslateDescriptorCode(Slots[Slot].Code);
        Binding.descriptorCount = 1;
        Binding.stageFlags      = VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT;
        Bindings.push_back(Binding);
    }
    VkDescriptorSetLayoutCreateInfo Info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    Info.bindingCount = static_cast<uint32_t>(Bindings.size());
    Info.pBindings    = Bindings.data();
    if (Functions_->vkCreateDescriptorSetLayout(Device_, &Info, nullptr, &Layout->Handle) != VK_SUCCESS) return nullptr;
    Layout->Exchange = this;
    return Layout;
}

std::shared_ptr<DescriptorGroup> DeviceExchange::ConstructDescriptorGroup(const std::shared_ptr<DescriptorLayout>& Layout,
                                                                          std::vector<DescriptorResource> Resources, const std::string& Label)
{
    auto Group       = std::make_shared<DescriptorGroup>();
    Group->Layout    = Layout;
    Group->Resources = std::move(Resources);
    Group->Label     = Label;
    VkDescriptorSetAllocateInfo Allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    Allocate.descriptorPool     = DescriptorPool_;
    Allocate.descriptorSetCount = 1;
    Allocate.pSetLayouts        = &Layout->Handle;
    if (Functions_->vkAllocateDescriptorSets(Device_, &Allocate, &Group->Handle) != VK_SUCCESS) return nullptr;
    Group->Exchange = this;
    std::vector<VkWriteDescriptorSet>   Writes;
    std::vector<VkDescriptorBufferInfo> BufferInfos(Group->Resources.size());
    std::vector<VkDescriptorImageInfo>  ImageInfos(Group->Resources.size());
    // 🔍 A binding left unwritten holds no descriptor: the GPU reads garbage through it (a fault, a lost device).
    for (uint32_t Slot = 0; Slot < Layout->Slots.size(); ++Slot)
    {
        const bool Present = Slot < Group->Resources.size() &&
                             (Group->Resources[Slot].Region || Group->Resources[Slot].View || Group->Resources[Slot].Sampler);
        if (!Present)
        {
            RecordFault("descriptor group '" + Label + "' leaves binding " + std::to_string(Slot) + " of layout '" + Layout->Label + "' unwritten");
        }
    }
    for (uint32_t Slot = 0; Slot < Group->Resources.size() && Slot < Layout->Slots.size(); ++Slot)
    {
        const DescriptorResource& Resource = Group->Resources[Slot];
        VkWriteDescriptorSet Write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        Write.dstSet          = Group->Handle;
        Write.dstBinding      = Slot;
        Write.descriptorCount = 1;
        Write.descriptorType  = TranslateDescriptorCode(Layout->Slots[Slot].Code);
        if (Resource.Region)
        {
            BufferInfos[Slot] = {Resource.Region->Handle, Resource.Offset, Resource.Size};
            Write.pBufferInfo = &BufferInfos[Slot];
        }
        else if (Resource.View)
        {
            ImageInfos[Slot]  = {VK_NULL_HANDLE, Resource.View->Handle, VK_IMAGE_LAYOUT_GENERAL};
            Write.pImageInfo  = &ImageInfos[Slot];
        }
        else if (Resource.Sampler)
        {
            ImageInfos[Slot]  = {Resource.Sampler->Handle, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
            Write.pImageInfo  = &ImageInfos[Slot];
        }
        else
        {
            continue;
        }
        Writes.push_back(Write);
    }
    Functions_->vkUpdateDescriptorSets(Device_, static_cast<uint32_t>(Writes.size()), Writes.data(), 0, nullptr);
    return Group;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PROGRAMS
//------------------------------------------------------------------------------------------------------------------------

bool DeviceExchange::LoadShaderUnit(const std::string& SpirvPath, VkShaderModule& OutUnit, std::string* OutDiagnostic)
{
    std::ifstream Stream(SpirvPath, std::ios::binary | std::ios::ate);
    if (!Stream)
    {
        ReportDiagnostic(OutDiagnostic, "SPIR-V missing: " + SpirvPath);
        return false;
    }
    const std::streamsize Size = Stream.tellg();
    Stream.seekg(0);
    std::vector<uint32_t> Words(static_cast<size_t>((Size + 3) / 4));
    Stream.read(reinterpret_cast<char*>(Words.data()), Size);
    VkShaderModuleCreateInfo Info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    Info.codeSize = static_cast<size_t>(Size);
    Info.pCode    = Words.data();
    if (Functions_->vkCreateShaderModule(Device_, &Info, nullptr, &OutUnit) != VK_SUCCESS)
    {
        ReportDiagnostic(OutDiagnostic, "vkCreateShaderModule failed: " + SpirvPath);
        return false;
    }
    return true;
}

namespace
{

struct SpecializationBlock
{
    std::vector<VkSpecializationMapEntry> Entries{};
    std::vector<uint32_t>                 Words{};
    VkSpecializationInfo                  Info{};

    explicit SpecializationBlock(std::span<const SpecializationConstant> Constants)
    {
        for (uint32_t Index = 0; Index < Constants.size(); ++Index)
        {
            Entries.push_back({Index, Index * 4u, 4u});
            Words.push_back(Constants[Index].Bits);
        }
        Info.mapEntryCount = static_cast<uint32_t>(Entries.size());
        Info.pMapEntries   = Entries.data();
        Info.dataSize      = Words.size() * sizeof(uint32_t);
        Info.pData         = Words.data();
    }
};

} // namespace

std::shared_ptr<DeviceProgram> DeviceExchange::ConstructComputeProgram(const std::string& SpirvPath, const std::string& EntryPoint,
                                                                       std::vector<std::shared_ptr<DescriptorLayout>> Layouts,
                                                                       std::span<const SpecializationConstant> Specialization,
                                                                       std::string* OutDiagnostic)
{
    VkShaderModule Unit = VK_NULL_HANDLE;
    if (!LoadShaderUnit(SpirvPath, Unit, OutDiagnostic)) return nullptr;
    auto Program       = std::make_shared<DeviceProgram>();
    Program->Layouts   = std::move(Layouts);
    Program->Label     = SpirvPath + ":" + EntryPoint;
    Program->BindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    std::vector<VkDescriptorSetLayout> Handles;
    for (const auto& Layout : Program->Layouts) Handles.push_back(Layout->Handle);
    VkPipelineLayoutCreateInfo LayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    LayoutInfo.setLayoutCount = static_cast<uint32_t>(Handles.size());
    LayoutInfo.pSetLayouts    = Handles.data();
    Functions_->vkCreatePipelineLayout(Device_, &LayoutInfo, nullptr, &Program->Layout);
    SpecializationBlock Block(Specialization);
    VkComputePipelineCreateInfo Info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    Info.stage.sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    Info.stage.stage               = VK_SHADER_STAGE_COMPUTE_BIT;
    Info.stage.module              = Unit;
    Info.stage.pName               = EntryPoint.c_str();
    Info.stage.pSpecializationInfo = Specialization.empty() ? nullptr : &Block.Info;
    Info.layout                    = Program->Layout;
    const VkResult Result = Functions_->vkCreateComputePipelines(Device_, VK_NULL_HANDLE, 1, &Info, nullptr, &Program->Pipeline);
    Functions_->vkDestroyShaderModule(Device_, Unit, nullptr);
    Program->Exchange = this;
    if (Result != VK_SUCCESS)
    {
        ReportDiagnostic(OutDiagnostic, "vkCreateComputePipelines failed: " + Program->Label);
        return nullptr;
    }
    return Program;
}

std::shared_ptr<DeviceProgram> DeviceExchange::ConstructRasterProgram(const RasterProgramSpecification& Specification, std::string* OutDiagnostic)
{
    VkShaderModule VertexUnit   = VK_NULL_HANDLE;
    VkShaderModule FragmentUnit = VK_NULL_HANDLE;
    if (!LoadShaderUnit(Specification.VertexSpirvPath, VertexUnit, OutDiagnostic)) return nullptr;
    if (!Specification.FragmentSpirvPath.empty() && !LoadShaderUnit(Specification.FragmentSpirvPath, FragmentUnit, OutDiagnostic))
    {
        Functions_->vkDestroyShaderModule(Device_, VertexUnit, nullptr);
        return nullptr;
    }
    auto Program       = std::make_shared<DeviceProgram>();
    Program->Layouts   = Specification.Layouts;
    Program->Label     = Specification.Label;
    Program->BindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    std::vector<VkDescriptorSetLayout> Handles;
    for (const auto& Layout : Program->Layouts) Handles.push_back(Layout->Handle);
    VkPipelineLayoutCreateInfo LayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    LayoutInfo.setLayoutCount = static_cast<uint32_t>(Handles.size());
    LayoutInfo.pSetLayouts    = Handles.data();
    Functions_->vkCreatePipelineLayout(Device_, &LayoutInfo, nullptr, &Program->Layout);

    SpecializationBlock Block(Specification.Specialization);
    std::vector<VkPipelineShaderStageCreateInfo> Stages;
    VkPipelineShaderStageCreateInfo VertexStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    VertexStage.stage               = VK_SHADER_STAGE_VERTEX_BIT;
    VertexStage.module              = VertexUnit;
    VertexStage.pName               = Specification.VertexEntry.c_str();
    VertexStage.pSpecializationInfo = Specification.Specialization.empty() ? nullptr : &Block.Info;
    Stages.push_back(VertexStage);
    if (FragmentUnit != VK_NULL_HANDLE)
    {
        VkPipelineShaderStageCreateInfo FragmentStage = VertexStage;
        FragmentStage.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        FragmentStage.module = FragmentUnit;
        FragmentStage.pName  = Specification.FragmentEntry.c_str();
        Stages.push_back(FragmentStage);
    }

    std::vector<VkVertexInputBindingDescription>   Bindings;
    std::vector<VkVertexInputAttributeDescription> Attributes;
    for (uint32_t Slot = 0; Slot < Specification.VertexLayouts.size(); ++Slot)
    {
        const VertexLayout& Layout = Specification.VertexLayouts[Slot];
        Bindings.push_back({Slot, Layout.Stride, Layout.Instance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX});
        for (const VertexAttribute& Attribute : Layout.Attributes)
        {
            Attributes.push_back({Attribute.Location, Slot, TranslateVertexFormat(Attribute.Format), Attribute.Offset});
        }
    }
    VkPipelineVertexInputStateCreateInfo VertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VertexInput.vertexBindingDescriptionCount   = static_cast<uint32_t>(Bindings.size());
    VertexInput.pVertexBindingDescriptions      = Bindings.data();
    VertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(Attributes.size());
    VertexInput.pVertexAttributeDescriptions    = Attributes.data();
    VkPipelineInputAssemblyStateCreateInfo Assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    Assembly.topology = Specification.Topology;
    VkPipelineViewportStateCreateInfo ViewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    ViewportState.viewportCount = 1;
    ViewportState.scissorCount  = 1;
    VkPipelineRasterizationStateCreateInfo Raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    Raster.polygonMode             = VK_POLYGON_MODE_FILL;
    Raster.cullMode                = Specification.CullMode;
    Raster.frontFace               = Specification.FrontFace;
    Raster.depthClampEnable        = Specification.UnclippedDepth ? VK_TRUE : VK_FALSE;
    Raster.depthBiasEnable         = (Specification.DepthBias != 0.0f || Specification.DepthBiasSlope != 0.0f) ? VK_TRUE : VK_FALSE;
    Raster.depthBiasConstantFactor = Specification.DepthBias;
    Raster.depthBiasSlopeFactor    = Specification.DepthBiasSlope;
    Raster.depthBiasClamp          = Specification.DepthBiasClamp;
    Raster.lineWidth               = 1.0f;
    VkPipelineMultisampleStateCreateInfo Multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    Multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo DepthState{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    DepthState.depthTestEnable  = Specification.DepthEnabled ? VK_TRUE : VK_FALSE;
    DepthState.depthWriteEnable = Specification.DepthWrite ? VK_TRUE : VK_FALSE;
    DepthState.depthCompareOp   = TranslateCompare(Specification.DepthCompare);
    std::vector<VkPipelineColorBlendAttachmentState> BlendStates;
    std::vector<VkFormat>                            ColorFormats;
    for (const ColorTarget& Target : Specification.Targets)
    {
        VkPipelineColorBlendAttachmentState State{};
        State.blendEnable         = Target.BlendEnabled ? VK_TRUE : VK_FALSE;
        State.srcColorBlendFactor = TranslateBlendFactor(Target.Color.Source);
        State.dstColorBlendFactor = TranslateBlendFactor(Target.Color.Destination);
        State.colorBlendOp        = TranslateBlendOperation(Target.Color.Operation);
        State.srcAlphaBlendFactor = TranslateBlendFactor(Target.Alpha.Source);
        State.dstAlphaBlendFactor = TranslateBlendFactor(Target.Alpha.Destination);
        State.alphaBlendOp        = TranslateBlendOperation(Target.Alpha.Operation);
        State.colorWriteMask      = Target.WriteMask;
        BlendStates.push_back(State);
        ColorFormats.push_back(TranslateTextureFormat(Target.Format));
    }
    VkPipelineColorBlendStateCreateInfo BlendState{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    BlendState.attachmentCount = static_cast<uint32_t>(BlendStates.size());
    BlendState.pAttachments    = BlendStates.data();
    const std::array<VkDynamicState, 2> DynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo Dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    Dynamic.dynamicStateCount = static_cast<uint32_t>(DynamicStates.size());
    Dynamic.pDynamicStates    = DynamicStates.data();
    VkPipelineRenderingCreateInfo Rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    Rendering.colorAttachmentCount    = static_cast<uint32_t>(ColorFormats.size());
    Rendering.pColorAttachmentFormats = ColorFormats.data();
    Rendering.depthAttachmentFormat   = Specification.DepthEnabled ? TranslateTextureFormat(Specification.DepthFormat) : VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo Info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    Info.pNext               = &Rendering;
    Info.stageCount          = static_cast<uint32_t>(Stages.size());
    Info.pStages             = Stages.data();
    Info.pVertexInputState   = &VertexInput;
    Info.pInputAssemblyState = &Assembly;
    Info.pViewportState      = &ViewportState;
    Info.pRasterizationState = &Raster;
    Info.pMultisampleState   = &Multisample;
    Info.pDepthStencilState  = &DepthState;
    Info.pColorBlendState    = &BlendState;
    Info.pDynamicState       = &Dynamic;
    Info.layout              = Program->Layout;
    const VkResult Result = Functions_->vkCreateGraphicsPipelines(Device_, VK_NULL_HANDLE, 1, &Info, nullptr, &Program->Pipeline);
    Functions_->vkDestroyShaderModule(Device_, VertexUnit, nullptr);
    if (FragmentUnit != VK_NULL_HANDLE) Functions_->vkDestroyShaderModule(Device_, FragmentUnit, nullptr);
    Program->Exchange = this;
    if (Result != VK_SUCCESS)
    {
        ReportDiagnostic(OutDiagnostic, "vkCreateGraphicsPipelines failed: " + Program->Label);
        return nullptr;
    }
    return Program;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   RELEASE
//------------------------------------------------------------------------------------------------------------------------

void DeviceExchange::ReleaseRegion(DeviceRegion& Region) noexcept
{
    if (Device_ == VK_NULL_HANDLE) return;
    if (Region.Handle != VK_NULL_HANDLE) Functions_->vkDestroyBuffer(Device_, Region.Handle, nullptr);
    if (Region.Memory != VK_NULL_HANDLE) Functions_->vkFreeMemory(Device_, Region.Memory, nullptr);
    Region.Handle = VK_NULL_HANDLE;
    Region.Memory = VK_NULL_HANDLE;
}

void DeviceExchange::ReleaseTexture(DeviceTexture& Texture) noexcept
{
    if (Device_ == VK_NULL_HANDLE) return;
    if (Texture.Image != VK_NULL_HANDLE && Texture.Owned) Functions_->vkDestroyImage(Device_, Texture.Image, nullptr);
    if (Texture.Memory != VK_NULL_HANDLE) Functions_->vkFreeMemory(Device_, Texture.Memory, nullptr);
    Texture.Image  = VK_NULL_HANDLE;
    Texture.Memory = VK_NULL_HANDLE;
}

void DeviceExchange::ReleaseView(DeviceTextureView& View) noexcept
{
    if (Device_ == VK_NULL_HANDLE) return;
    if (View.Handle != VK_NULL_HANDLE) Functions_->vkDestroyImageView(Device_, View.Handle, nullptr);
    View.Handle = VK_NULL_HANDLE;
}

void DeviceExchange::ReleaseSampler(DeviceSampler& Sampler) noexcept
{
    if (Device_ == VK_NULL_HANDLE) return;
    if (Sampler.Handle != VK_NULL_HANDLE) Functions_->vkDestroySampler(Device_, Sampler.Handle, nullptr);
    Sampler.Handle = VK_NULL_HANDLE;
}

void DeviceExchange::ReleaseDescriptorLayout(DescriptorLayout& Layout) noexcept
{
    if (Device_ == VK_NULL_HANDLE) return;
    if (Layout.Handle != VK_NULL_HANDLE) Functions_->vkDestroyDescriptorSetLayout(Device_, Layout.Handle, nullptr);
    Layout.Handle = VK_NULL_HANDLE;
}

void DeviceExchange::ReleaseDescriptorGroup(DescriptorGroup& Group) noexcept
{
    if (Device_ == VK_NULL_HANDLE) return;
    if (Group.Handle != VK_NULL_HANDLE) Functions_->vkFreeDescriptorSets(Device_, DescriptorPool_, 1, &Group.Handle);
    Group.Handle = VK_NULL_HANDLE;
}

void DeviceExchange::ReleaseProgram(DeviceProgram& Program) noexcept
{
    if (Device_ == VK_NULL_HANDLE) return;
    if (Program.Pipeline != VK_NULL_HANDLE) Functions_->vkDestroyPipeline(Device_, Program.Pipeline, nullptr);
    if (Program.Layout != VK_NULL_HANDLE) Functions_->vkDestroyPipelineLayout(Device_, Program.Layout, nullptr);
    Program.Pipeline = VK_NULL_HANDLE;
    Program.Layout   = VK_NULL_HANDLE;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   QUEUE WRITES
//------------------------------------------------------------------------------------------------------------------------

void DeviceExchange::WriteRegion(const std::shared_ptr<DeviceRegion>& Region, VkDeviceSize Offset, const void* Bytes, VkDeviceSize ByteCount)
{
    if (!Region || ByteCount == 0) return;
    // 📝 GPUQueue.writeBuffer past the end is a validation error that writes nothing; a Vulkan copy past the end faults the GPU.
    if (Offset > Region->Capacity || ByteCount > Region->Capacity - Offset)
    {
        RecordFault("write of " + std::to_string(ByteCount) + " B at " + std::to_string(Offset) + " overruns '" + Region->Label + "' (" +
                    std::to_string(Region->Capacity) + " B); not written");
        return;
    }
    if (WriteObserver) WriteObserver(Region->Label, Offset, Bytes, ByteCount);
    if (!EnsureStaging(ByteCount + 16)) return;
    const VkDeviceSize StagingOffset = (StagingCursor_ + 15) / 16 * 16;
    std::memcpy(StagingBytes_ + StagingOffset, Bytes, static_cast<size_t>(ByteCount));
    StagingCursor_ = StagingOffset + ByteCount;
    PendingRegions_.push_back({Region, Offset, StagingOffset, ByteCount});
}

void DeviceExchange::WriteTexture(const std::shared_ptr<DeviceTexture>& Texture, uint32_t Layer, const void* Bytes, VkDeviceSize ByteCount,
                                  uint32_t Width, uint32_t Height, uint32_t Depth, uint32_t OriginX, uint32_t OriginY,
                                  uint32_t OriginZ, uint32_t Level)
{
    if (!Texture || ByteCount == 0) return;
    {
        // 📝 GPUQueue.writeTexture outside the subresource is a validation error that writes nothing.
        const TextureSpecification& Specification = Texture->Specification;
        const bool     Volume    = Specification.Dimension == TextureDimension::Volumetric;
        const uint32_t MipWidth  = std::max(1u, Specification.Width >> Level);
        const uint32_t MipHeight = std::max(1u, Specification.Height >> Level);
        const uint32_t MipDepth  = Volume ? std::max(1u, Specification.DepthOrLayers >> Level) : 1u;
        const VkDeviceSize Needed = static_cast<VkDeviceSize>(Width) * Height * Depth * QueryTexelBytes(Specification.Format);
        const bool Inside = Level < Specification.Levels && (Volume || Layer < Specification.DepthOrLayers) &&
                            static_cast<uint64_t>(OriginX) + Width <= MipWidth && static_cast<uint64_t>(OriginY) + Height <= MipHeight &&
                            static_cast<uint64_t>(OriginZ) + Depth <= MipDepth && ByteCount >= Needed;
        if (!Inside)
        {
            RecordFault("texture write " + std::to_string(Width) + "x" + std::to_string(Height) + "x" + std::to_string(Depth) + " at (" +
                        std::to_string(OriginX) + ", " + std::to_string(OriginY) + ", " + std::to_string(OriginZ) + ") layer " + std::to_string(Layer) +
                        " level " + std::to_string(Level) + " lies outside '" + Specification.Label + "' (" + std::to_string(Specification.Width) + "x" +
                        std::to_string(Specification.Height) + "x" + std::to_string(Specification.DepthOrLayers) + ", " + std::to_string(ByteCount) +
                        " B supplied); not written");
            return;
        }
    }
    if (!EnsureStaging(ByteCount + 16)) return;
    const VkDeviceSize StagingOffset = (StagingCursor_ + 15) / 16 * 16;
    std::memcpy(StagingBytes_ + StagingOffset, Bytes, static_cast<size_t>(ByteCount));
    StagingCursor_ = StagingOffset + ByteCount;
    PendingTextureWrite Write;
    Write.Texture                          = Texture;
    Write.StagingOffset                    = StagingOffset;
    Write.Copy.bufferOffset                = StagingOffset;
    Write.Copy.imageSubresource.aspectMask = AspectOf(Texture->Specification.Format);
    Write.Copy.imageSubresource.mipLevel   = Level;
    const bool Volume = Texture->Specification.Dimension == TextureDimension::Volumetric;
    Write.Copy.imageSubresource.baseArrayLayer = Volume ? 0u : Layer;
    Write.Copy.imageSubresource.layerCount     = 1;
    Write.Copy.imageOffset = {static_cast<int32_t>(OriginX), static_cast<int32_t>(OriginY), static_cast<int32_t>(OriginZ)};
    Write.Copy.imageExtent = {Width, Height, Depth};
    PendingTextures_.push_back(Write);
}

namespace
{

void RecordGlobalDependency(const DeviceExchange::ResolvedFunctions& Functions, VkCommandBuffer Stream)
{
    VkMemoryBarrier Barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    Barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    Barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    Functions.vkCmdPipelineBarrier(Stream, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &Barrier,
                                   0, nullptr, 0, nullptr);
}

} // namespace

void DeviceExchange::TransitionToGeneral(VkCommandBuffer Stream, DeviceTexture& Texture)
{
    VkImageMemoryBarrier Barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    Barrier.srcAccessMask       = 0;
    Barrier.dstAccessMask       = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    Barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    Barrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Barrier.image               = Texture.Image;
    const bool Volume           = Texture.Specification.Dimension == TextureDimension::Volumetric;
    Barrier.subresourceRange    = {AspectOf(Texture.Specification.Format), 0, Texture.Specification.Levels, 0,
                                   Volume ? 1u : Texture.Specification.DepthOrLayers};
    Functions_->vkCmdPipelineBarrier(Stream, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                                     nullptr, 1, &Barrier);
    if (!DepthFormat(Texture.Specification.Format))
    {
        VkClearColorValue Zero{};
        Functions_->vkCmdClearColorImage(Stream, Texture.Image, VK_IMAGE_LAYOUT_GENERAL, &Zero, 1, &Barrier.subresourceRange);
    }
}

bool DeviceExchange::ExecuteImmediately(const std::function<void(VkCommandBuffer)>& Record, std::string* OutFailure)
{
    VkCommandBufferAllocateInfo Allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    Allocate.commandPool        = CommandPool_;
    Allocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    Allocate.commandBufferCount = 1;
    VkCommandBuffer Stream      = VK_NULL_HANDLE;
    VkResult Result = Functions_->vkAllocateCommandBuffers(Device_, &Allocate, &Stream);
    if (Result != VK_SUCCESS)
    {
        ReportDiagnostic(OutFailure, "vkAllocateCommandBuffers returned " + NameResult(Result));
        return false;
    }
    VkCommandBufferBeginInfo Begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Functions_->vkBeginCommandBuffer(Stream, &Begin);
    Record(Stream);
    Functions_->vkEndCommandBuffer(Stream);
    VkSubmitInfo Submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    Submit.commandBufferCount = 1;
    Submit.pCommandBuffers    = &Stream;
    Functions_->vkResetFences(Device_, 1, &SubmitFence_);
    Result = Functions_->vkQueueSubmit(Queue_, 1, &Submit, SubmitFence_);
    if (Result != VK_SUCCESS)
    {
        ReportDiagnostic(OutFailure, "vkQueueSubmit returned " + NameResult(Result));
    }
    else
    {
        // 📝 A device lost while this work ran may still signal the fence; the status query then reports the loss.
        Result = Functions_->vkWaitForFences(Device_, 1, &SubmitFence_, VK_TRUE, UINT64_MAX);
        if (Result == VK_SUCCESS) Result = Functions_->vkGetFenceStatus(Device_, SubmitFence_);
        if (Result != VK_SUCCESS) ReportDiagnostic(OutFailure, "the fence wait returned " + NameResult(Result));
    }
    Functions_->vkFreeCommandBuffers(Device_, CommandPool_, 1, &Stream);
    if (Result == VK_SUCCESS) ++CompletedSubmissions_;
    return Result == VK_SUCCESS;
}

std::string DeviceExchange::DescribeSubmissionPoint() const
{
    std::string Point = SubmissionLabel_.empty() ? std::string("unlabelled work") : "'" + SubmissionLabel_ + "'";
    Point += ", after " + std::to_string(CompletedSubmissions_) + " completed submissions";
    if (!CompletedLabel_.empty()) Point += " (the last: '" + CompletedLabel_ + "')";
    return Point;
}

void DeviceExchange::RecordFault(const std::string& Fault)
{
    if (Faults_.size() < 64) Faults_.push_back(Fault);
    if (FaultObserver) FaultObserver(Fault);
}

VkResult DeviceExchange::ProbeDevice() noexcept
{
    if (Device_ == VK_NULL_HANDLE || Queue_ == VK_NULL_HANDLE) return VK_ERROR_INITIALIZATION_FAILED;
    return Functions_->vkQueueWaitIdle(Queue_);
}

bool DeviceExchange::FlushWrites(std::string* OutDiagnostic)
{
    if (PendingRegions_.empty() && PendingTextures_.empty() && PendingTransitions_.empty()) return true;
    auto Transitions = std::move(PendingTransitions_);
    auto Regions     = std::move(PendingRegions_);
    auto Textures    = std::move(PendingTextures_);
    PendingTransitions_.clear();
    PendingRegions_.clear();
    PendingTextures_.clear();
    std::string Failure;
    const bool Executed = ExecuteImmediately([&](VkCommandBuffer Stream) {
        RecordGlobalDependency(*Functions_, Stream);
        for (auto& Texture : Transitions)
        {
            if (Texture && Texture->Image != VK_NULL_HANDLE) TransitionToGeneral(Stream, *Texture);
        }
        RecordGlobalDependency(*Functions_, Stream);
        // 📝 Queue writes land in the order they were issued (a region's zero fill, then its contents). Copies without a
        //    barrier between them may run concurrently on a GPU, so a write that overlaps an earlier one of this flush
        //    waits for it; writes to disjoint ranges still run together.
        std::vector<std::tuple<VkBuffer, VkDeviceSize, VkDeviceSize>> Written;
        for (const auto& Write : Regions)
        {
            if (!Write.Region || Write.Region->Handle == VK_NULL_HANDLE) continue;
            if (Write.StagingOffset + Write.ByteCount > StagingCapacity_) continue;
            const VkDeviceSize Start = Write.TargetOffset, End = Write.TargetOffset + Write.ByteCount;
            const bool Overlaps = std::any_of(Written.begin(), Written.end(), [&](const auto& Earlier) {
                return std::get<0>(Earlier) == Write.Region->Handle && Start < std::get<2>(Earlier) && std::get<1>(Earlier) < End;
            });
            if (Overlaps)
            {
                RecordGlobalDependency(*Functions_, Stream);
                Written.clear();
            }
            Written.emplace_back(Write.Region->Handle, Start, End);
            VkBufferCopy Copy{Write.StagingOffset, Write.TargetOffset, Write.ByteCount};
            Functions_->vkCmdCopyBuffer(Stream, StagingHandle_, Write.Region->Handle, 1, &Copy);
        }
        std::vector<VkImage> Imaged;
        for (const auto& Write : Textures)
        {
            if (!Write.Texture || Write.Texture->Image == VK_NULL_HANDLE) continue;
            if (std::find(Imaged.begin(), Imaged.end(), Write.Texture->Image) != Imaged.end())
            {
                RecordGlobalDependency(*Functions_, Stream);
                Imaged.clear();
            }
            Imaged.push_back(Write.Texture->Image);
            Functions_->vkCmdCopyBufferToImage(Stream, StagingHandle_, Write.Texture->Image, VK_IMAGE_LAYOUT_GENERAL, 1, &Write.Copy);
        }
        RecordGlobalDependency(*Functions_, Stream);
    }, &Failure);
    StagingCursor_ = 0;
    if (!Executed)
    {
        const std::string Report = "staged write submission failed: " + Failure + " — " + DescribeSubmissionPoint();
        RecordFault(Report);
        ReportDiagnostic(OutDiagnostic, Report);
    }
    return Executed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   SUBMISSION
//------------------------------------------------------------------------------------------------------------------------

CommandSequence DeviceExchange::BeginSequence()
{
    VkCommandBufferAllocateInfo Allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    Allocate.commandPool        = CommandPool_;
    Allocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    Allocate.commandBufferCount = 1;
    VkCommandBuffer Stream      = VK_NULL_HANDLE;
    Functions_->vkAllocateCommandBuffers(Device_, &Allocate, &Stream);
    VkCommandBufferBeginInfo Begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Functions_->vkBeginCommandBuffer(Stream, &Begin);
    return CommandSequence(*this, Stream);
}

bool DeviceExchange::Submit(CommandSequence& Sequence, std::string* OutDiagnostic)
{
    // ① staged writes execute first (they were issued before this submission), ② then the recorded commands.
    if (!FlushWrites(OutDiagnostic)) return false;
    VkCommandBuffer Stream = Sequence.Stream();
    Functions_->vkEndCommandBuffer(Stream);
    VkSubmitInfo Submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    Submit.commandBufferCount = 1;
    Submit.pCommandBuffers    = &Stream;
    Functions_->vkResetFences(Device_, 1, &SubmitFence_);
    // 🔍 FRONTIER_DEVICE_TRACE=1 names every submission on stderr before it runs (a driver crash then names its work).
    static const bool Trace = std::getenv("FRONTIER_DEVICE_TRACE") != nullptr;
    if (Trace) std::fprintf(stderr, "[DeviceExchange] submission %llu: %s\n", static_cast<unsigned long long>(CompletedSubmissions_ + 1), SubmissionLabel_.c_str());
    const auto     SubmitStart = std::chrono::steady_clock::now();
    const VkResult Submitted = Functions_->vkQueueSubmit(Queue_, 1, &Submit, SubmitFence_);
    if (Submitted != VK_SUCCESS)
    {
        Functions_->vkFreeCommandBuffers(Device_, CommandPool_, 1, &Stream);
        const std::string Report = "vkQueueSubmit returned " + NameResult(Submitted) + " — " + DescribeSubmissionPoint();
        RecordFault(Report);
        ReportDiagnostic(OutDiagnostic, Report);
        return false;
    }
    VkResult Waited = Functions_->vkWaitForFences(Device_, 1, &SubmitFence_, VK_TRUE, UINT64_MAX);
    if (Waited == VK_SUCCESS) Waited = Functions_->vkGetFenceStatus(Device_, SubmitFence_);
    Functions_->vkFreeCommandBuffers(Device_, CommandPool_, 1, &Stream);
    if (Waited != VK_SUCCESS)
    {
        const std::string Report = "the submission's fence wait returned " + NameResult(Waited) + " — " + DescribeSubmissionPoint();
        RecordFault(Report);
        ReportDiagnostic(OutDiagnostic, Report);
        return false;
    }
    ++CompletedSubmissions_;
    CompletedLabel_ = SubmissionLabel_;
    if (SubmissionTiming)
        SubmissionTiming(SubmissionLabel_, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - SubmitStart).count());
    return true;
}

bool DeviceExchange::SubmitSeries(std::span<CommandSequence* const> Series, std::span<const std::string> Labels, std::string* OutDiagnostic)
{
    bool Healthy = FlushWrites(OutDiagnostic);
    for (size_t Index = 0; Index < Series.size(); ++Index)
    {
        if (!Healthy)
        {
            // 📝 The streams after a failure are never submitted; they go back to the pool.
            VkCommandBuffer Stream = Series[Index]->Stream();
            Functions_->vkEndCommandBuffer(Stream);
            Functions_->vkFreeCommandBuffers(Device_, CommandPool_, 1, &Stream);
            continue;
        }
        if (Index < Labels.size()) SubmissionLabel_ = Labels[Index];
        Healthy = Submit(*Series[Index], OutDiagnostic);
    }
    return Healthy;
}

void DeviceExchange::AwaitIdle() noexcept
{
    if (Queue_ != VK_NULL_HANDLE) Functions_->vkQueueWaitIdle(Queue_);
}

bool DeviceExchange::ReadRegion(const std::shared_ptr<DeviceRegion>& Region, VkDeviceSize Offset, VkDeviceSize ByteCount,
                                std::vector<uint8_t>& OutBytes)
{
    if (!Region || Offset > Region->Capacity || ByteCount > Region->Capacity - Offset)
    {
        RecordFault("read of " + std::to_string(ByteCount) + " B at " + std::to_string(Offset) + " overruns '" + (Region ? Region->Label : std::string("?")) + "'");
        return false;
    }
    if (!FlushWrites()) return false;
    if (!EnsureStaging(ByteCount)) return false;
    const bool Executed = ExecuteImmediately([&](VkCommandBuffer Stream) {
        RecordGlobalDependency(*Functions_, Stream);
        VkBufferCopy Copy{Offset, 0, ByteCount};
        Functions_->vkCmdCopyBuffer(Stream, Region->Handle, StagingHandle_, 1, &Copy);
        RecordGlobalDependency(*Functions_, Stream);
    });
    if (!Executed) return false;
    OutBytes.assign(StagingBytes_, StagingBytes_ + ByteCount);
    return true;
}

bool DeviceExchange::ReadTexture(const std::shared_ptr<DeviceTexture>& Texture, uint32_t Layer, uint32_t Level, std::vector<uint8_t>& OutBytes)
{
    if (!FlushWrites()) return false;
    const auto& Specification = Texture->Specification;
    const bool  Volume  = Specification.Dimension == TextureDimension::Volumetric;
    const uint32_t Width  = std::max(1u, Specification.Width >> Level);
    const uint32_t Height = std::max(1u, Specification.Height >> Level);
    const uint32_t Depth  = Volume ? std::max(1u, Specification.DepthOrLayers >> Level) : 1u;
    const VkDeviceSize ByteCount = static_cast<VkDeviceSize>(Width) * Height * Depth * QueryTexelBytes(Specification.Format);
    if (!EnsureStaging(ByteCount)) return false;
    const bool Executed = ExecuteImmediately([&](VkCommandBuffer Stream) {
        RecordGlobalDependency(*Functions_, Stream);
        VkBufferImageCopy Copy{};
        Copy.imageSubresource = {AspectOf(Specification.Format), Level, Volume ? 0u : Layer, 1};
        Copy.imageExtent      = {Width, Height, Depth};
        Functions_->vkCmdCopyImageToBuffer(Stream, Texture->Image, VK_IMAGE_LAYOUT_GENERAL, StagingHandle_, 1, &Copy);
        RecordGlobalDependency(*Functions_, Stream);
    });
    if (!Executed) return false;
    OutBytes.assign(StagingBytes_, StagingBytes_ + ByteCount);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   COMMAND SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

CommandSequence::CommandSequence(DeviceExchange& Exchange, VkCommandBuffer CommandStream)
    : Exchange_(Exchange), CommandStream_(CommandStream)
{
}

void CommandSequence::OrderAfterEarlierWork()
{
    RecordGlobalDependency(Exchange_.Functions(), CommandStream_);
}

void CommandSequence::BindProgram(const std::shared_ptr<DeviceProgram>& Program)
{
    Retained_.push_back(Program);
    Exchange_.Functions().vkCmdBindPipeline(CommandStream_, Program->BindPoint, Program->Pipeline);
    BoundPoint_  = Program->BindPoint;
    BoundLayout_ = Program->Layout;
}

void CommandSequence::BindGroup(uint32_t Index, const std::shared_ptr<DescriptorGroup>& Group, std::span<const uint32_t> DynamicOffsets)
{
    Retained_.push_back(Group);
    Exchange_.Functions().vkCmdBindDescriptorSets(CommandStream_, BoundPoint_, BoundLayout_, Index, 1, &Group->Handle,
                                                  static_cast<uint32_t>(DynamicOffsets.size()), DynamicOffsets.data());
}

void CommandSequence::Dispatch(uint32_t GroupCountX, uint32_t GroupCountY, uint32_t GroupCountZ)
{
    if (GroupCountX == 0 || GroupCountY == 0 || GroupCountZ == 0) return;
    OrderAfterEarlierWork();
    Exchange_.Functions().vkCmdDispatch(CommandStream_, GroupCountX, GroupCountY, GroupCountZ);
}

void CommandSequence::BeginRaster(std::span<const ColorAttachment> Colors, const DepthAttachment* Depth, const std::string& Label)
{
    (void)Label;
    OrderAfterEarlierWork();
    std::vector<VkRenderingAttachmentInfo> ColorInfos;
    uint32_t Width  = 0;
    uint32_t Height = 0;
    for (const ColorAttachment& Attachment : Colors)
    {
        Retained_.push_back(Attachment.View);
        VkRenderingAttachmentInfo Info{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        Info.imageView   = Attachment.View->Handle;
        Info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        Info.loadOp      = Attachment.Load == LoadOperation::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        Info.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        std::memcpy(Info.clearValue.color.float32, Attachment.Clear.data(), sizeof(float) * 4);
        ColorInfos.push_back(Info);
        Width  = std::max(1u, Attachment.View->Texture->Specification.Width >> Attachment.View->BaseLevel);
        Height = std::max(1u, Attachment.View->Texture->Specification.Height >> Attachment.View->BaseLevel);
    }
    VkRenderingAttachmentInfo DepthInfo{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    uint32_t LayerBase = 0;
    if (Depth != nullptr)
    {
        Retained_.push_back(Depth->View);
        DepthInfo.imageView               = Depth->View->Handle;
        DepthInfo.imageLayout             = VK_IMAGE_LAYOUT_GENERAL;
        DepthInfo.loadOp                  = Depth->Load == LoadOperation::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        DepthInfo.storeOp                 = VK_ATTACHMENT_STORE_OP_STORE;
        DepthInfo.clearValue.depthStencil = {Depth->Clear, 0};
        Width     = std::max(1u, Depth->View->Texture->Specification.Width >> Depth->View->BaseLevel);
        Height    = std::max(1u, Depth->View->Texture->Specification.Height >> Depth->View->BaseLevel);
        LayerBase = Depth->View->BaseLayer;
    }
    (void)LayerBase;
    VkRenderingInfo Info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    Info.renderArea           = {{0, 0}, {Width, Height}};
    Info.layerCount           = 1;
    Info.colorAttachmentCount = static_cast<uint32_t>(ColorInfos.size());
    Info.pColorAttachments    = ColorInfos.data();
    Info.pDepthAttachment     = Depth != nullptr ? &DepthInfo : nullptr;
    Exchange_.Functions().vkCmdBeginRendering(CommandStream_, &Info);
    InsideRaster_ = true;
    RasterWidth_  = Width;
    RasterHeight_ = Height;
    SetViewport(0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height));
    VkRect2D Scissor{{0, 0}, {Width, Height}};
    Exchange_.Functions().vkCmdSetScissor(CommandStream_, 0, 1, &Scissor);
}

void CommandSequence::SetViewport(float X, float Y, float Width, float Height, float MinimumDepth, float MaximumDepth)
{
    // 📝 WebGPU clip space is y-up; a negative-height viewport (Vulkan 1.1) maps it onto the y-down framebuffer.
    VkViewport Viewport{X, Y + Height, Width, -Height, MinimumDepth, MaximumDepth};
    Exchange_.Functions().vkCmdSetViewport(CommandStream_, 0, 1, &Viewport);
}

void CommandSequence::SetScissor(uint32_t X, uint32_t Y, uint32_t Width, uint32_t Height)
{
    VkRect2D Scissor{{static_cast<int32_t>(X), static_cast<int32_t>(Y)}, {Width, Height}};
    Exchange_.Functions().vkCmdSetScissor(CommandStream_, 0, 1, &Scissor);
}

void CommandSequence::BindVertexRegion(uint32_t Slot, const std::shared_ptr<DeviceRegion>& Region, VkDeviceSize Offset)
{
    Retained_.push_back(Region);
    Exchange_.Functions().vkCmdBindVertexBuffers(CommandStream_, Slot, 1, &Region->Handle, &Offset);
}

void CommandSequence::BindIndexRegion(const std::shared_ptr<DeviceRegion>& Region, VkIndexType IndexType)
{
    Retained_.push_back(Region);
    Exchange_.Functions().vkCmdBindIndexBuffer(CommandStream_, Region->Handle, 0, IndexType);
}

void CommandSequence::Draw(uint32_t VertexCount, uint32_t InstanceCount, uint32_t FirstVertex, uint32_t FirstInstance)
{
    if (VertexCount == 0 || InstanceCount == 0) return;
    Exchange_.Functions().vkCmdDraw(CommandStream_, VertexCount, InstanceCount, FirstVertex, FirstInstance);
}

void CommandSequence::DrawIndexedIndirect(const std::shared_ptr<DeviceRegion>& Region, VkDeviceSize Offset)
{
    Retained_.push_back(Region);
    Exchange_.Functions().vkCmdDrawIndexedIndirect(CommandStream_, Region->Handle, Offset, 1, 20);
}

void CommandSequence::DrawIndexedIndirectCount(const std::shared_ptr<DeviceRegion>& Region, VkDeviceSize Offset,
                                               const std::shared_ptr<DeviceRegion>& CountRegion, VkDeviceSize CountOffset, uint32_t MaximumDraws,
                                               uint32_t Stride)
{
    if (MaximumDraws == 0) return;
    Retained_.push_back(Region);
    Retained_.push_back(CountRegion);
    Exchange_.Functions().vkCmdDrawIndexedIndirectCount(CommandStream_, Region->Handle, Offset, CountRegion->Handle, CountOffset, MaximumDraws, Stride);
}

void CommandSequence::DrawIndexedIndirectSeries(const std::shared_ptr<DeviceRegion>& Region, VkDeviceSize Offset, uint32_t DrawCount, uint32_t Stride)
{
    if (DrawCount == 0) return;
    Retained_.push_back(Region);
    Exchange_.Functions().vkCmdDrawIndexedIndirect(CommandStream_, Region->Handle, Offset, DrawCount, Stride);
}

void CommandSequence::EndRaster()
{
    if (!InsideRaster_) return;
    Exchange_.Functions().vkCmdEndRendering(CommandStream_);
    InsideRaster_ = false;
}

void CommandSequence::CopyRegion(const std::shared_ptr<DeviceRegion>& Source, VkDeviceSize SourceOffset,
                                 const std::shared_ptr<DeviceRegion>& Target, VkDeviceSize TargetOffset, VkDeviceSize ByteCount)
{
    if (ByteCount == 0) return;
    // 📝 copyBufferToBuffer outside either buffer is a validation error that copies nothing.
    if (SourceOffset > Source->Capacity || ByteCount > Source->Capacity - SourceOffset || TargetOffset > Target->Capacity ||
        ByteCount > Target->Capacity - TargetOffset)
    {
        Exchange_.RecordFault("copy of " + std::to_string(ByteCount) + " B from '" + Source->Label + "' @" + std::to_string(SourceOffset) + " to '" +
                              Target->Label + "' @" + std::to_string(TargetOffset) + " overruns a region; not copied");
        return;
    }
    OrderAfterEarlierWork();
    Retained_.push_back(Source);
    Retained_.push_back(Target);
    VkBufferCopy Copy{SourceOffset, TargetOffset, ByteCount};
    Exchange_.Functions().vkCmdCopyBuffer(CommandStream_, Source->Handle, Target->Handle, 1, &Copy);
}

void CommandSequence::ClearTexture(const std::shared_ptr<DeviceTexture>& Texture, std::array<float, 4> Colour)
{
    OrderAfterEarlierWork();
    Retained_.push_back(Texture);
    VkClearColorValue Value{};
    std::memcpy(Value.float32, Colour.data(), sizeof(float) * 4);
    const bool Volume = Texture->Specification.Dimension == TextureDimension::Volumetric;
    VkImageSubresourceRange Range{VK_IMAGE_ASPECT_COLOR_BIT, 0, Texture->Specification.Levels, 0,
                                  Volume ? 1u : Texture->Specification.DepthOrLayers};
    Exchange_.Functions().vkCmdClearColorImage(CommandStream_, Texture->Image, VK_IMAGE_LAYOUT_GENERAL, &Value, 1, &Range);
}

} // namespace Frontier
