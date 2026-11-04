/*==========================================================================================================================================
                                                       SOLARDEPTHRASTERIZATION.H
==========================================================================================================================================*/
// 🧩 The sun's depth map for the analytic illumination: every rastered occupant projected orthographically along the sun.
#pragma once
#include "Rendering/GlobalIllumination/SceneStructure.h"
#include "Rendering/GlobalIllumination/ShaderInterchange.h"
namespace Frontier::AnalyticIllumination
{
class SolarDepthRasterization
{
  public:
    SolarDepthRasterization(DeviceExchange& Exchange, GlobalIllumination::ShaderInterchange& Shaders);
    void ConstructPrograms(const std::shared_ptr<DescriptorLayout>& Layout);
    void ConstructTargets(uint32_t Extent);
    void Record(CommandSequence& Sequence, const std::shared_ptr<DescriptorGroup>& Group, const GlobalIllumination::SceneStructure& Scene,
                const std::shared_ptr<DeviceRegion>& VertexRegion, const std::shared_ptr<DeviceRegion>& Instances);
    uint32_t                           Extent = 0;   // [texel]
    std::shared_ptr<DeviceTexture>     DepthTexture{};
    std::shared_ptr<DeviceTextureView> DepthView{};
  private:
    DeviceExchange&                        Exchange_;
    GlobalIllumination::ShaderInterchange& Shaders_;
    std::shared_ptr<DeviceProgram>         Program_{};
};
} // namespace Frontier::AnalyticIllumination
