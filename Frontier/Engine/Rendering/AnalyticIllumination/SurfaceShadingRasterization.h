/*==========================================================================================================================================
                                                     SURFACESHADINGRASTERIZATION.H
==========================================================================================================================================*/
// 🧩 The analytic image: the gradient sky, then every rastered occupant lit in closed form, tone mapped into the presented image.
#pragma once
#include "Rendering/GlobalIllumination/SceneStructure.h"
#include "Rendering/GlobalIllumination/ShaderInterchange.h"
namespace Frontier::AnalyticIllumination
{
class SurfaceShadingRasterization
{
  public:
    SurfaceShadingRasterization(DeviceExchange& Exchange, GlobalIllumination::ShaderInterchange& Shaders);
    void ConstructPrograms(const std::shared_ptr<DescriptorLayout>& Layout);
    void ConstructTargets(uint32_t Width, uint32_t Height);
    void Record(CommandSequence& Sequence, const std::shared_ptr<DescriptorGroup>& Group, const GlobalIllumination::SceneStructure& Scene,
                const std::shared_ptr<DeviceRegion>& VertexRegion, const std::shared_ptr<DeviceRegion>& Instances,
                const std::shared_ptr<DeviceTextureView>& Target);
    std::array<uint32_t, 2>            Extent{0, 0};   // [px]
    std::shared_ptr<DeviceTexture>     DepthTexture{};
    std::shared_ptr<DeviceTextureView> DepthView{};
  private:
    DeviceExchange&                        Exchange_;
    GlobalIllumination::ShaderInterchange& Shaders_;
    std::shared_ptr<DeviceProgram>         SkyProgram_{}, SurfaceProgram_{};
};
} // namespace Frontier::AnalyticIllumination
