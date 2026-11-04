/*==========================================================================================================================================
                                                 VULKAN COMPUTE EXCHANGE
                                     ------------------------------------------------------------------
                 Dynamic-loaded Vulkan 1.3 offscreen compute, RGBA16F / sRGB 2D-array images,
                         contiguous device memory spans, SPIR-V compilation, and host readback.
==========================================================================================================================================*/
// 🧩 Portable VK_NO_PROTOTYPES compute device exchange for multi-planet Environment & Celestial GPU submissions.

#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

#include "VulkanDeviceAccess.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                        FP16 <-> FP32 CONVERSION FOR RGBA16F READBACK
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] uint16_t PackHalfFloat(float Scalar) noexcept;
[[nodiscard]] float    UnpackHalfFloat(uint16_t Bits) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                       DEVICE MEMORY SPAN & 2D-ARRAY IMAGE SLOTS
//------------------------------------------------------------------------------------------------------------------------

struct DeviceMemorySpan
{
    VkBuffer       Handle      = VK_NULL_HANDLE;
    VkDeviceMemory Memory      = VK_NULL_HANDLE;
    VkDeviceSize   Capacity    = 0;
    void*          MappedBytes = nullptr;
};

struct DeviceImageArraySlot
{
    VkImage       Image         = VK_NULL_HANDLE;
    VkDeviceMemory Memory       = VK_NULL_HANDLE;
    VkImageView   ArrayView     = VK_NULL_HANDLE;
    VkFormat      Format        = VK_FORMAT_UNDEFINED;
    uint32_t      Width         = 0;
    uint32_t      Height        = 0;
    uint32_t      ArrayLayers   = 0;
    VkImageLayout CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct ComputeProgramSlot
{
    VkShaderModule                ShaderUnit    = VK_NULL_HANDLE;
    VkDescriptorSetLayout         SetLayout     = VK_NULL_HANDLE;
    VkPipelineLayout              Layout        = VK_NULL_HANDLE;
    VkPipeline                    Pipeline      = VK_NULL_HANDLE;
    std::vector<VkDescriptorType> BindingTypes{};
};

struct ComputeDescriptorArgument
{
    VkDescriptorType            Type       = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    const DeviceMemorySpan*     MemorySpan = nullptr;
    const DeviceImageArraySlot* ImageArray = nullptr;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 VULKAN COMPUTE EXCHANGE
//------------------------------------------------------------------------------------------------------------------------

class VulkanComputeExchange
{
  public:
    VulkanComputeExchange() = default;
    ~VulkanComputeExchange();

    VulkanComputeExchange(const VulkanComputeExchange&)            = delete;
    VulkanComputeExchange& operator=(const VulkanComputeExchange&) = delete;

    // Initializes Vulkan instance, physical device, and compute queue from explicit or discovered libvulkan.so.1.
    bool Initialize(const std::string& ExplicitDriverLibraryPath = "", std::string* OutDiagnostic = nullptr);
    // 📝 Borrows a device another owner created (see VulkanDeviceAccess.h); Shutdown then leaves the device and instance alive.
    bool AttachDevice(const VulkanDeviceAccess& Access, std::string* OutDiagnostic = nullptr);
    void Shutdown() noexcept;

    [[nodiscard]] bool               Ready() const noexcept { return Device_ != VK_NULL_HANDLE; }
    [[nodiscard]] const std::string& DeviceName() const noexcept { return DeviceName_; }

    // Contiguous GPU memory span management
    bool AllocateMemorySpan(VkDeviceSize ByteCount, DeviceMemorySpan& OutSlot);
    void ReleaseMemorySpan(DeviceMemorySpan& Slot) noexcept;
    bool UploadMemorySpan(DeviceMemorySpan& Slot, const void* SourceBytes, VkDeviceSize ByteCount);
    bool DownloadMemorySpan(const DeviceMemorySpan& Slot, void* DestinationBytes, VkDeviceSize ByteCount) const;

    // 2D-Array image management (VK_FORMAT_R16G16B16A16_SFLOAT storage+sampled or VK_FORMAT_R8G8B8A8_SRGB sampled)
    bool AllocateFloatRgbaArray(uint32_t Width, uint32_t Height, uint32_t ArrayLayers, VkFormat Format, DeviceImageArraySlot& OutSlot);
    bool AllocateRgba16fArray(uint32_t Width, uint32_t Height, uint32_t ArrayLayers, DeviceImageArraySlot& OutSlot)
    { return AllocateFloatRgbaArray(Width,Height,ArrayLayers,VK_FORMAT_R16G16B16A16_SFLOAT,OutSlot); }
    bool AllocateRgba32fArray(uint32_t Width, uint32_t Height, uint32_t ArrayLayers, DeviceImageArraySlot& OutSlot)
    { return AllocateFloatRgbaArray(Width,Height,ArrayLayers,VK_FORMAT_R32G32B32A32_SFLOAT,OutSlot); }
    bool AllocateSrgb8Array(uint32_t Width, uint32_t Height, uint32_t ArrayLayers, DeviceImageArraySlot& OutSlot);
    void ReleaseImage(DeviceImageArraySlot& Slot) noexcept;

    bool ClearRgba16fArray(DeviceImageArraySlot& Slot, std::array<float, 4> ClearColour = {0.0f, 0.0f, 0.0f, 0.0f});
    bool UploadSrgb8Array(DeviceImageArraySlot& Slot, std::span<const uint8_t> RgbaBytes);
    bool UploadRgba16fLayer(DeviceImageArraySlot& Slot, uint32_t LayerIndex, std::span<const float> RgbaFloats);
    bool DownloadFloatRgbaLayer(DeviceImageArraySlot& Slot, uint32_t LayerIndex, std::vector<float>& OutRgbaFloats);
    bool DownloadRgba16fLayer(DeviceImageArraySlot& Slot, uint32_t LayerIndex, std::vector<float>& OutRgbaFloats)
    { return DownloadFloatRgbaLayer(Slot,LayerIndex,OutRgbaFloats); }

    // SPIR-V compute program compilation & synchronous queue execution
    bool LoadComputeProgram(const std::string&                SpvFilePath,
                            const char*                       EntryPointName,
                            std::span<const VkDescriptorType> BindingTypes,
                            uint32_t                          PushConstantBytes,
                            ComputeProgramSlot&               OutProgram,
                            std::string*                      OutDiagnostic = nullptr);
    void ReleaseProgram(ComputeProgramSlot& Program) noexcept;

    bool DispatchCompute(const ComputeProgramSlot&                     Program,
                         std::span<const ComputeDescriptorArgument>    Arguments,
                         const void*                                   PushConstants,
                         uint32_t                                      PushConstantBytes,
                         uint32_t                                      GroupCountX,
                         uint32_t                                      GroupCountY = 1u,
                         uint32_t                                      GroupCountZ = 1u);

  private:
    [[nodiscard]] uint32_t SelectMemoryType(uint32_t TypeBits, VkMemoryPropertyFlags RequiredFlags) const;
    bool ResolveInstanceFunctions(std::string* OutDiagnostic);
    bool CompleteDevice(std::string* OutDiagnostic);
    bool TransitionImageLayout(VkCommandBuffer CommandStream, DeviceImageArraySlot& Slot, VkImageLayout TargetLayout);

    void*                            LibraryHandle_       = nullptr;
    VkInstance                       Instance_            = VK_NULL_HANDLE;
    VkPhysicalDevice                 PhysicalDevice_      = VK_NULL_HANDLE;
    VkDevice                         Device_              = VK_NULL_HANDLE;
    VkQueue                          Queue_               = VK_NULL_HANDLE;
    uint32_t                         QueueFamily_         = 0u;
    VkCommandPool                    CommandPool_         = VK_NULL_HANDLE;
    VkDescriptorPool                 DescriptorPool_      = VK_NULL_HANDLE;
    VkSampler                        LinearClampSampler_  = VK_NULL_HANDLE;
    VkSampler                        LinearRepeatSampler_ = VK_NULL_HANDLE;
    VkFence                          SubmitFence_         = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties MemoryProperties_{};
    std::string                      DeviceName_{};
    bool                             DeviceAttached_      = false;   // [-] the device belongs to another owner (AttachDevice)

    PFN_vkGetInstanceProcAddr                    pfnGetInstanceProcAddr_                    = nullptr;
    PFN_vkCreateInstance                         pfnCreateInstance_                         = nullptr;
    PFN_vkDestroyInstance                        pfnDestroyInstance_                        = nullptr;
    PFN_vkEnumeratePhysicalDevices               pfnEnumeratePhysicalDevices_               = nullptr;
    PFN_vkGetPhysicalDeviceProperties            pfnGetPhysicalDeviceProperties_            = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties      pfnGetPhysicalDeviceMemoryProperties_      = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties pfnGetPhysicalDeviceQueueFamilyProperties_ = nullptr;
    PFN_vkCreateDevice                           pfnCreateDevice_                           = nullptr;
    PFN_vkDestroyDevice                          pfnDestroyDevice_                          = nullptr;
    PFN_vkGetDeviceQueue                         pfnGetDeviceQueue_                         = nullptr;
    PFN_vkCreateCommandPool                      pfnCreateCommandPool_                      = nullptr;
    PFN_vkDestroyCommandPool                     pfnDestroyCommandPool_                     = nullptr;
    PFN_vkAllocateCommandBuffers                 pfnAllocateCommandStreams_                 = nullptr;
    PFN_vkFreeCommandBuffers                     pfnFreeCommandStreams_                     = nullptr;
    PFN_vkBeginCommandBuffer                     pfnBeginCommandStream_                     = nullptr;
    PFN_vkEndCommandBuffer                       pfnEndCommandStream_                       = nullptr;
    PFN_vkResetCommandBuffer                     pfnResetCommandStream_                     = nullptr;
    PFN_vkQueueSubmit                            pfnQueueSubmit_                            = nullptr;
    PFN_vkQueueWaitIdle                          pfnQueueWaitIdle_                          = nullptr;
    PFN_vkCreateFence                            pfnCreateFence_                            = nullptr;
    PFN_vkDestroyFence                           pfnDestroyFence_                           = nullptr;
    PFN_vkWaitForFences                          pfnWaitForFences_                          = nullptr;
    PFN_vkResetFences                            pfnResetFences_                            = nullptr;
    PFN_vkCreateBuffer                           pfnCreateMemorySpan_                       = nullptr;
    PFN_vkDestroyBuffer                          pfnDestroyMemorySpan_                      = nullptr;
    PFN_vkGetBufferMemoryRequirements            pfnGetSpanMemoryRequirements_              = nullptr;
    PFN_vkAllocateMemory                         pfnAllocateMemory_                         = nullptr;
    PFN_vkFreeMemory                             pfnFreeMemory_                             = nullptr;
    PFN_vkBindBufferMemory                       pfnBindSpanMemory_                         = nullptr;
    PFN_vkMapMemory                              pfnMapMemory_                              = nullptr;
    PFN_vkUnmapMemory                            pfnUnmapMemory_                            = nullptr;
    PFN_vkCreateImage                            pfnCreateImage_                            = nullptr;
    PFN_vkDestroyImage                           pfnDestroyImage_                           = nullptr;
    PFN_vkGetImageMemoryRequirements             pfnGetImageMemoryRequirements_             = nullptr;
    PFN_vkBindImageMemory                        pfnBindImageMemory_                        = nullptr;
    PFN_vkCreateImageView                        pfnCreateImageView_                        = nullptr;
    PFN_vkDestroyImageView                       pfnDestroyImageView_                       = nullptr;
    PFN_vkCreateSampler                          pfnCreateSampler_                          = nullptr;
    PFN_vkDestroySampler                         pfnDestroySampler_                         = nullptr;
    PFN_vkCreateShaderModule                     pfnCreateShaderUnit_                       = nullptr;
    PFN_vkDestroyShaderModule                    pfnDestroyShaderUnit_                      = nullptr;
    PFN_vkCreateDescriptorSetLayout              pfnCreateDescriptorSetLayout_              = nullptr;
    PFN_vkDestroyDescriptorSetLayout             pfnDestroyDescriptorSetLayout_             = nullptr;
    PFN_vkCreatePipelineLayout                   pfnCreatePipelineLayout_                   = nullptr;
    PFN_vkDestroyPipelineLayout                  pfnDestroyPipelineLayout_                  = nullptr;
    PFN_vkCreateComputePipelines                 pfnCreateComputePipelines_                 = nullptr;
    PFN_vkDestroyPipeline                        pfnDestroyPipeline_                        = nullptr;
    PFN_vkCreateDescriptorPool                   pfnCreateDescriptorPool_                   = nullptr;
    PFN_vkDestroyDescriptorPool                  pfnDestroyDescriptorPool_                  = nullptr;
    PFN_vkResetDescriptorPool                    pfnResetDescriptorPool_                    = nullptr;
    PFN_vkAllocateDescriptorSets                 pfnAllocateDescriptorSets_                 = nullptr;
    PFN_vkUpdateDescriptorSets                   pfnUpdateDescriptorSets_                   = nullptr;
    PFN_vkCmdPipelineBarrier                     pfnCmdPipelineBarrier_                     = nullptr;
    PFN_vkCmdBindPipeline                        pfnCmdBindPipeline_                        = nullptr;
    PFN_vkCmdBindDescriptorSets                  pfnCmdBindDescriptorSets_                  = nullptr;
    PFN_vkCmdPushConstants                       pfnCmdPushConstants_                       = nullptr;
    PFN_vkCmdDispatch                            pfnCmdDispatch_                            = nullptr;
    PFN_vkCmdClearColorImage                     pfnCmdClearColorImage_                     = nullptr;
    PFN_vkCmdCopyBufferToImage                   pfnCmdCopySpanToImage_                     = nullptr;
    PFN_vkCmdCopyImageToBuffer                   pfnCmdCopyImageToSpan_                     = nullptr;
};

} // namespace Frontier
