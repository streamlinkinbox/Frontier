/*==========================================================================================================================================
                                                          VULKANDEVICEACCESS.H
==========================================================================================================================================*/
// 🧩 Handles of a Vulkan device another owner created (the editor's presentation device) that an exchange borrows instead of
//    opening its own: one device, one queue, every submission ordered on that queue.

#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

#include <cstdint>

namespace Frontier
{

// 📝 The owner keeps the instance and device alive until every borrowing exchange shut down; borrowers never destroy
//    them, and submit only from the owner's thread (the queue is not externally synchronised by the borrower).
struct VulkanDeviceAccess
{
    VkInstance                Instance        = VK_NULL_HANDLE;
    VkPhysicalDevice          PhysicalDevice  = VK_NULL_HANDLE;
    VkDevice                  Device          = VK_NULL_HANDLE;
    VkQueue                   Queue           = VK_NULL_HANDLE;
    uint32_t                  QueueFamily     = 0u;
    PFN_vkGetInstanceProcAddr ResolveInstance = nullptr;
    uint32_t                  ApiVersion      = 0u;   // [-] VK_API_VERSION_* the instance and device were created for
    bool                      DrawIndirectCount = false;   // [-] drawIndirectCount enabled (vkCmdDrawIndexedIndirectCount)
    bool                      MultiDrawIndirect = false;   // [-] multiDrawIndirect enabled (drawCount > 1)

    [[nodiscard]] bool Valid() const noexcept
    {
        return Instance != VK_NULL_HANDLE && PhysicalDevice != VK_NULL_HANDLE && Device != VK_NULL_HANDLE && Queue != VK_NULL_HANDLE &&
               ResolveInstance != nullptr;
    }
};

} // namespace Frontier
