/*==========================================================================================================================================
                                                         PLUMEEXTENSION.H
==========================================================================================================================================*/
// 🧩 The bridge between the Smoke & fire editor and an application: the C++ plume (SmokeSequence for the sources and the step clock,
//    PlumeSimulationSequence for the lattices and the march) run on a Vulkan device for the gas the editor hands over, the test emitter it
//    asks for and the camera it orbits, and drawn into the picture the editor shows. The editor's viewport has no scene behind it: the programs
//    bind the engine's stand-ins (PlumeStandIns) and the march's radiance is composited over a flat backdrop on the CPU.

#pragma once

#include "Frontier/Engine/Interface/FluidPlumeEditor/PlumeViewportPresentation.h"
#include "Frontier/Engine/Rendering/GlobalIllumination/RefreshEncoding.h"
#include "Frontier/Engine/Rendering/PlumeSimulation/PlumeStandIns.h"
#include "Frontier/Engine/Rendering/PlumeSimulation/SmokeSequence.h"
#include "Frontier/Extensions/Plume/PlumeComposite.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Frontier
{

class WorldSpace;

// 📝 The picture as the host sees it: the view to register with the interface renderer (GENERAL layout) and what the editor reads.
struct PlumeViewportOutcome
{
    VkImageView         View = VK_NULL_HANDLE;
    PlumeViewportImage  Image{};
};

class PlumeExtension
{
  public:
    // 📝 ShaderRoot: the folder holding PlumeSimulation/ (the nine programs). HostDevice: the editor's Vulkan device, borrowed; null runs the plume
    //    on an own device (the loader or FRONTIER_VULKAN_DRIVER), as the captures do.
    PlumeExtension(std::filesystem::path ShaderRoot, const VulkanDeviceAccess* HostDevice);
    ~PlumeExtension();

    PlumeExtension(const PlumeExtension&)            = delete;
    PlumeExtension& operator=(const PlumeExtension&) = delete;

    // 📝 One refresh: the plume advanced by Request.Seconds, marched from the camera and composited into a picture of about Request.Extent. The
    //    lattice follows the level's Render settings (the quality tier): a new tier builds a new plume.
    PlumeViewportOutcome Render(const PlumeViewportRequest& Request, const WorldSpace& World);

    // 📝 The presented picture as RGBA8 (the captures read it; a host samples the view).
    bool ReadPresentedImage(std::vector<uint8_t>& Rgba, std::string* Diagnostic);

    // 📝 The grade a level asks for: its RenderPipeline record's quality tier (low, medium, high); high where the level has none.
    [[nodiscard]] static std::string ResolveGradeName(const WorldSpace& World);

    // 📝 The emitter the editor's Test emitter switches on: a steady smoke source, or a burning one, 3 m ahead of the car (IlluminationHost.js
    //    ResolveTestPlume, the fields the plume's sources read).
    [[nodiscard]] static PlumeSimulation::PlumeEmission ResolveTestEmission(bool Burning, const FluidDynamics::Vector3& CarPosition, const FluidDynamics::Vector3& Forward);

  private:
    bool ConstructDevice(std::array<uint32_t, 2> Picture, std::string& Diagnostic);
    bool ConstructPlume(const std::string& Grade, std::string& Diagnostic);
    bool ConstructPicture(std::array<uint32_t, 2> Picture, std::array<uint32_t, 2> Trace, std::string& Diagnostic);

    std::filesystem::path ShaderRoot_;
    bool                  HostDeviceAttached_ = false;
    VulkanDeviceAccess    Device_{};

    std::unique_ptr<DeviceExchange>                         Exchange_{};
    std::unique_ptr<GlobalIllumination::ShaderInterchange>  Shaders_{};
    PlumeSimulation::PlumeStandIns                          StandIns_{};
    std::unique_ptr<PlumeSimulation::SmokeSequence>         Smoke_{};
    std::unique_ptr<PlumeSimulation::PlumeSimulationSequence> Plume_{};
    std::shared_ptr<DeviceTexture>                          Presented_{};
    std::shared_ptr<DeviceTextureView>                      PresentedView_{};
    GlobalIllumination::RefreshEncoder                      Encoder_{};

    std::string             Grade_{};
    std::string             Failure_{};
    std::array<uint32_t, 2> StandInExtent_{}; // [px] the size the stand-in surface depth was built for
    std::array<uint32_t, 2> Picture_{};      // [px] the presented picture: a whole number of trace texels
    std::array<uint32_t, 2> Trace_{};        // [px] the march's extent
    uint32_t                TraceScale_ = 2; // [-] picture pixels per trace texel
    uint32_t                Refreshes_  = 0;

    std::vector<uint8_t> Radiance_{};        // [-] the march's rgba16float, read back
    std::vector<uint8_t> Pixels_{};          // [-] the picture, rgba8
    PlumeBackdropPicture Backdrop_{};        // [-] the composite's backdrop and texels, kept between refreshes
};

} // namespace Frontier
