/*====================================================================================================================================
                                                           SOLIDARCDEVICE.H
====================================================================================================================================*/
// 🧩 Vulkan 1.3 device provisioning (volk; system loader or an explicit driver such as SwiftShader), buffers and images

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <volk.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Frontier::SolidArc
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct SolidArcDeviceSettings
{
    std::string              DriverPath          = {};                                       // [-] - ICD library loaded directly (empty: system loader)
    std::vector<std::string> InstanceExtensions  = {};                                       // [-] - Extra (e.g. GLFW surface extensions)
    bool                     PresentationEnabled = false;                                    // [-] - Enable VK_KHR_swapchain on the device
    bool                     ValidationEnabled   = false;                                    // [-] - VK_LAYER_KHRONOS_validation when present
};

struct SolidArcBuffer
{
    VkBuffer       Buffer = VK_NULL_HANDLE;                                                  // [-] - Buffer
    VkDeviceMemory Memory = VK_NULL_HANDLE;                                                  // [-] - Backing allocation
    VkDeviceSize   Size   = 0;                                                               // [B] - Capacity
    void*          Mapped = nullptr;                                                         // [-] - Persistent host mapping (host-visible only)
};

struct SolidArcImage
{
    VkImage        Image  = VK_NULL_HANDLE;                                                  // [-] - Image
    VkDeviceMemory Memory = VK_NULL_HANDLE;                                                  // [-] - Backing allocation
    VkImageView    View   = VK_NULL_HANDLE;                                                  // [-] - Whole-image view
    VkFormat       Format = VK_FORMAT_UNDEFINED;                                             // [-] - Texel format
    uint32_t       Width  = 0;                                                               // [px] - Extent
    uint32_t       Height = 0;                                                               // [px] - Extent
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    DEVICE
//------------------------------------------------------------------------------------------------------------------------

// 📝 One graphics queue, one command pool, synchronous helpers. Vulkan 1.3 core: dynamic rendering + synchronization2.
class SolidArcDevice
{
public:
    SolidArcDevice() = default;
    SolidArcDevice(const SolidArcDevice&)            = delete;
    SolidArcDevice& operator=(const SolidArcDevice&) = delete;
    ~SolidArcDevice();

    // Returns false with Refusal filled (no loader, no driver, no 1.3 device, …).
    bool Open(const SolidArcDeviceSettings& Settings, std::string& Refusal);
    void Close();

    [[nodiscard]] uint32_t FindMemoryType(uint32_t TypeBits, VkMemoryPropertyFlags Required) const;
    [[nodiscard]] std::string Describe() const;

    // Records with Record into a one-time command buffer, submits it and waits for completion.
    void SubmitImmediately(const std::function<void(VkCommandBuffer)>& Record) const;

    [[nodiscard]] SolidArcBuffer CreateBuffer(VkDeviceSize Size, VkBufferUsageFlags Usage, bool HostVisibleCondition) const;
    void                         DestroyBuffer(SolidArcBuffer& Buffer) const;
    // ArrayLayers 0: a plain 2D image; ≥ 1: a 2D array with a 2D_ARRAY view over every layer (matcap library).
    // Samples > 1: a multisampled attachment (resolved into a single-sample image at the end of rendering).
    [[nodiscard]] SolidArcImage  CreateImage(uint32_t Width, uint32_t Height, VkFormat Format, VkImageUsageFlags Usage, VkImageAspectFlags Aspect,
                                             uint32_t ArrayLayers = 0, VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT, uint32_t MipLevels = 1) const;
    void                         DestroyImage(SolidArcImage& Image) const;

    VkInstance                       Instance       = VK_NULL_HANDLE;
    VkPhysicalDevice                 Physical       = VK_NULL_HANDLE;
    VkDevice                         Device         = VK_NULL_HANDLE;
    VkQueue                          Queue          = VK_NULL_HANDLE;
    uint32_t                         QueueFamily    = 0;
    VkCommandPool                    CommandPool    = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties       Properties     = {};
    VkPhysicalDeviceMemoryProperties MemoryLayout   = {};
    uint32_t                         ApiVersion     = VK_API_VERSION_1_3;

private:
    void* DriverLibrary = nullptr;                                                           // [-] - dlopen / LoadLibrary token of DriverPath
};

// Image layout transition (synchronization2, whole image, one aspect).
void TransitionSolidArcImage(VkCommandBuffer Commands, VkImage Image, VkImageAspectFlags Aspect, VkImageLayout From, VkImageLayout To, uint32_t MipLevels = 1);

} // namespace Frontier::SolidArc
