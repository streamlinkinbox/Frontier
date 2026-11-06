/*==========================================================================================================================================
                                                       FLUIDVOLUMEEXTENSION.H
==========================================================================================================================================*/
// 🧩 The bridge between the Fluid editor and an application: the Pyro solver (PyroSolverSession, on its own Vulkan device) run for the settings the
//    editor hands over, the bursts and brush it raises and the camera it orbits, and its picture written into a texture on the application's device,
//    for the editor's interface to sample. The viewport has no scene behind it: the solver draws its own studio floor.

#pragma once

#include "Frontier/Engine/DeviceExchange/DeviceExchange.h"
#include "Frontier/Engine/Interface/FluidEditor/FluidViewportPresentation.h"
#include "Frontier/Extensions/Fluid/PyroSolverSession.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Frontier
{

// 📝 The picture as the host sees it: the view to register with the interface renderer (GENERAL layout) and what the editor reads.
struct FluidViewportOutcome
{
    VkImageView         View = VK_NULL_HANDLE;
    FluidViewportImage  Image{};
};

class FluidVolumeExtension
{
  public:
    // 📝 HostDevice: the editor's Vulkan device, borrowed, on which the picture lives; null presents on an own device (the loader or
    //    FRONTIER_VULKAN_DRIVER), as the captures do.
    explicit FluidVolumeExtension(const VulkanDeviceAccess* HostDevice);
    ~FluidVolumeExtension();

    FluidVolumeExtension(const FluidVolumeExtension&)            = delete;
    FluidVolumeExtension& operator=(const FluidVolumeExtension&) = delete;

    // 📝 One refresh: the solver advanced as the request says, drawn from its camera and written into the presented texture.
    FluidViewportOutcome Render(const FluidViewportRequest& Request);

    // 📝 The presented picture as RGBA8 (the captures read it; a host samples the view).
    bool ReadPresentedImage(std::vector<uint8_t>& Rgba, std::string* Diagnostic);

  private:
    bool ConstructExchange(std::string& Diagnostic);
    bool ConstructPicture(std::array<uint32_t, 2> Picture, std::string& Diagnostic);

    bool               HostDeviceAttached_ = false;
    VulkanDeviceAccess Device_{};

    PyroSolverSession                  Solver_;
    std::unique_ptr<DeviceExchange>    Exchange_{};
    std::shared_ptr<DeviceTexture>     Presented_{};
    std::shared_ptr<DeviceTextureView> PresentedView_{};
    std::array<uint32_t, 2>            Picture_{};
    std::string                        Failure_{};
    std::vector<uint8_t>               Pixels_{};
};

} // namespace Frontier
