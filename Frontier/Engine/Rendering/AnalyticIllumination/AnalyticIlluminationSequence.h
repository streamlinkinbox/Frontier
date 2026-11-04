/*==========================================================================================================================================
                                                    ANALYTICILLUMINATIONSEQUENCE.H
==========================================================================================================================================*/
// 🧩 The analytic illumination: the editor's fast closed-form renderer of the scene the global illumination loaded — one shadowed sun,
//    Lambert and Blinn-Phong surfaces, a sky/ground hemisphere fill and a gradient sky, lit by the level's Sun record.

// 📝 Editor and development use: no distance fields, cards, path tracing or temporal reuse, so it opens instantly and stays
//    interactive on any Vulkan 1.3 device. It reads the global illumination's vertex, occupant and material regions and writes the
//    same presented image. Selected by RenderPipelineConfiguration::AnalyticIllumination (levelspace analytic_illumination).
#pragma once
#include "SolarDepthRasterization.h"
#include "SurfaceShadingRasterization.h"
#include <filesystem>
#include <memory>
#include <string>
namespace Frontier::AnalyticIllumination
{
struct AnalyticCamera
{
    GlobalIllumination::Vector3 Position{0.0, -10.0, 4.0};   // [m]
    GlobalIllumination::Vector3 Target{0.0, 0.0, 0.0};       // [m]
    GlobalIllumination::Vector3 Up{0.0, 0.0, 1.0};           // [-]
    double                      VerticalFieldOfView = 1.0471975511965976;   // [rad]
    double                      Near                = 0.05;  // [m]
};
// 📝 The level's Sun record as the analytic light reads it.
struct AnalyticSun
{
    bool   Enabled            = true;
    double ElevationDegrees   = 38.0;    // [deg]
    double AzimuthDegrees     = 132.0;   // [deg]
    double IlluminanceKilolux = 98.0;    // [klx]
    double KelvinTemperature  = 5778.0;  // [K]
    double AngularDiameter    = 0.53;    // [deg]
    double ExposureCompensation = 0.0;   // [EV]
};

class AnalyticIlluminationSequence
{
  public:
    AnalyticIlluminationSequence(DeviceExchange& Exchange, std::filesystem::path SpirvFolder);
    bool Construct(const GlobalIllumination::SceneStructure& Scene, const std::shared_ptr<DeviceRegion>& VertexRegion,
                   const std::shared_ptr<DeviceRegion>& OccupantRegion, const std::shared_ptr<DeviceRegion>& MaterialRegion,
                   std::string* OutDiagnostic = nullptr);
    // 📝 One refresh into Target (an rgba8 sRGB view of Extent), submitted and awaited.
    bool Refresh(const AnalyticCamera& Camera, const AnalyticSun& Sun, std::array<uint32_t, 2> Extent, const std::shared_ptr<DeviceTextureView>& Target,
                 std::string* OutDiagnostic = nullptr);
    [[nodiscard]] double LastRefreshMilliseconds() const noexcept { return LastRefreshMilliseconds_; }

  private:
    DeviceExchange&                                Exchange_;
    GlobalIllumination::ShaderInterchange          Shaders_;
    const GlobalIllumination::SceneStructure*      Scene_ = nullptr;
    std::shared_ptr<DeviceRegion>                  VertexRegion_{}, OccupantRegion_{}, MaterialRegion_{}, ConstantRegion_{}, Instances_{};
    std::shared_ptr<DescriptorLayout>              Layout_{};
    std::shared_ptr<DescriptorGroup>               Group_{};
    std::shared_ptr<DeviceSampler>                 Comparison_{};
    std::unique_ptr<SolarDepthRasterization>       Solar_{};
    std::unique_ptr<SurfaceShadingRasterization>   Shading_{};
    double                                         LastRefreshMilliseconds_ = 0.0;
};
} // namespace Frontier::AnalyticIllumination
