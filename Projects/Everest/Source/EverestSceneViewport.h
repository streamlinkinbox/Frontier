/*==========================================================================================================================================
                                                       EVERESTSCENEVIEWPORT.H
==========================================================================================================================================*/
// 🧩 Project Everest's live Scene Viewport: the level's environment, render-pipeline and geometry records drive the engine
//    SceneViewport (ported VehicleHMI ReSTIR GI of the proving ground + the PlanetaryAtmosphere environment) on the host's device.

#pragma once

#include "EverestSceneGeometry.h"
#include "EverestWorkspace.h"

#include "Frontier/Engine/Rendering/SceneViewport/SceneViewport.h"
#include "Frontier/Engine/Scene/SolarPlacement.h"

#include <array>
#include <filesystem>
#include <memory>
#include <string>

namespace Everest
{

// 📝 The level's environment records as the viewport reads them; an absent record switches its effect off.
[[nodiscard]] Frontier::GlobalIllumination::SceneEnvironment ResolveSceneEnvironment(const Frontier::SceneWorld& World, double ElapsedSeconds);

// 📝 The GlobalIllumination port's celestial solver (the demo's SolveCelestial) answering the Sun page and the day clock.
[[nodiscard]] Frontier::SolarPlacement SolveSolarPlacement(const Frontier::SolarPlacementQuery& Query);

// 📝 The demo's AdvanceCelestial for the level's sun: a running day clock advances the local solar time and links the
//    sun to it; a time-linked sun takes the solved elevation and azimuth. Returns true when the sun moved.
bool AdvanceSolarDay(Frontier::SolarConfiguration& Sun, double DeltaSeconds);

struct SceneViewportRefresh
{
    VkImageView             View   = VK_NULL_HANDLE;   // [-] presented image (UNORM view of sRGB bytes, GENERAL layout)
    std::array<uint32_t, 2> Extent = {0, 0};           // [px] render extent
    std::string             Status{};                  // [-] renderer, extent, refresh time — or why there is no image
    SceneViewportReadout    Readout{};                 // [-] the viewport bar, readout and status strip figures
};

class EverestSceneViewport
{
  public:
    // 📝 ProjectRoot holds Content/EnvironmentArchives/{ProvingGround,CelestialMaps}; ShaderRoot holds the GlobalIllumination
    //    and Environment SPIR-V folders. HostDevice null opens a device of its own (offscreen validation).
    EverestSceneViewport(std::filesystem::path ProjectRoot, std::filesystem::path ShaderRoot, const Frontier::VulkanDeviceAccess* HostDevice);

    // 📝 Renders one refresh for the request; rebuilds the renderer when the quality tier or the ray-tracing tier changes and holds
    //    the last image while the Render settings' live refresh is off.
    SceneViewportRefresh Render(const SceneViewportRequest& Request, const Frontier::SceneWorld& World, double ElapsedSeconds);

    [[nodiscard]] Frontier::GlobalIllumination::SceneViewport* AccessViewport() noexcept { return Viewport_.get(); }
    [[nodiscard]] const SceneGeometryOccupancy&                QueryGeometry() const noexcept { return Geometry_; }

  private:
    std::filesystem::path                                        ProjectRoot_;
    std::filesystem::path                                        ShaderRoot_;
    bool                                                         HostDeviceAttached_ = false;
    Frontier::VulkanDeviceAccess                                 Device_{};
    std::unique_ptr<Frontier::GlobalIllumination::SceneViewport> Viewport_{};
    std::string                                                  Quality_{};
    bool                                                         RayTracing_ = true;
    std::string                                                  Failure_{};
    double                                                       RefreshMilliseconds_ = 0.0;
    std::array<uint32_t, 2>                                      RequestedExtent_     = {0, 0};
    uint32_t                                                     StableExtentCount_   = 0;   // [-] interface ticks at RequestedExtent_
    SceneViewportRefresh                                         Held_{};                     // [-] the last outcome, shown while live refresh is off
    SceneGeometryOccupancy                                       Geometry_{};                 // [-] the level's geometry records on the scene's occupants
    const Frontier::GlobalIllumination::SceneStructure*          GeometryScene_ = nullptr;    // [-] the scene Geometry_ was reset with
    SceneViewportPick                                            Pick_{};                     // [-] the answer to the last pick serial
};

} // namespace Everest
