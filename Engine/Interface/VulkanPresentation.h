/*==========================================================================================================================================
                                                           VULKANPRESENTATION.H
==========================================================================================================================================*/
// 🧩 Own Vulkan UI presentation while the inherited ImGui adapter renders secondary GLFW windows.

#pragma once
#include <memory>
#include <functional>
#include "Engine/Render/SurfaceRasterization.h"
#include "Frontier/Engine/DeviceExchange/VulkanDeviceAccess.h"

struct GLFWwindow;
struct ImDrawData;

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     VULKAN UI PRESENTATION
//------------------------------------------------------------------------------------------------------------------------

class VulkanPresentation
{
  public:
    VulkanPresentation();
    ~VulkanPresentation();
    VulkanPresentation(const VulkanPresentation &)            = delete;
    VulkanPresentation &operator=(const VulkanPresentation &) = delete;
    // 📝 SceneRendering asks for a Vulkan 1.3 device with the scene viewport's features (dynamic rendering, scalar layouts,
    //    draw parameters) so the illumination and environment exchanges can borrow it; without such a device it falls back.
    void                Initialize(GLFWwindow *Window, bool MaterialRendering = false, bool SceneRendering = false);
    VulkanRasterAccess QueryRasterAccess() const;
    // 📝 The presentation device as the scene viewport borrows it (Valid() is false when SceneRendering was refused).
    VulkanDeviceAccess  QueryDeviceAccess() const;
    void ConfigureRendering(std::function<void(VkCommandBuffer, uint64_t, uint64_t)> Construct,
                            std::function<void()> Confirm);
    bool                Reconfigure(int Width, int Height);
    void                RenderMain(ImDrawData *Commands);
    void                PresentMain();
    void                AwaitCompletion() noexcept;
    void                Finalize() noexcept;

  private:
    struct PresentationAllocation;
    std::unique_ptr<PresentationAllocation> Allocation;
};
} // namespace Frontier
