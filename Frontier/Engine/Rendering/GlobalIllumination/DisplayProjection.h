/*==========================================================================================================================================
                                                          DISPLAYPROJECTION.H
==========================================================================================================================================*/
// 🧩 Recombination, temporal antialiasing, eye adaptation and the present raster (DisplayProjection.js).

#pragma once

#include "ShaderInterchange.h"

namespace Frontier::GlobalIllumination
{

class SurfaceProjection;
class ResamplingSequence;
class VarianceIntegrator;
class SurfaceCardDepot;

// 📝 Resources of systems outside the port (clouds, lens flare, lightning, smoke) that the display programs bind; with
//    those systems off in the demo they hold their idle content, which these neutral resources reproduce.
struct DisplayNeutralResources
{
    std::shared_ptr<DeviceRegion>      CloudParams{};
    std::shared_ptr<DeviceTextureView> CloudColour{};
    std::shared_ptr<DeviceTextureView> CloudGuide{};
    std::shared_ptr<DeviceTextureView> Lightning{};
};

class DisplayProjection
{
  public:
    DisplayProjection(DeviceExchange& Exchange, ShaderInterchange& Shaders);

    void ConstructPrograms(const std::shared_ptr<DeviceRegion>& RefreshRegion, const std::shared_ptr<DeviceRegion>& MaterialRegion,
                           const std::shared_ptr<DeviceRegion>& OccupantRegion,
                           TextureFormat PresentFormat);
    void ConstructTargets(std::array<uint32_t, 2> ScreenExtent, const SurfaceProjection& Surface, const ResamplingSequence& Resampling,
                          const VarianceIntegrator& Diffuse, const VarianceIntegrator& Glossy, const SurfaceCardDepot& Cards,
                          const DisplayNeutralResources& Neutral);
    // 📝 The demo's inspection march (InspectionMarch.wgsl): primary rays through the distance field or the BVH alone, for
    //    inspection view 6. Needs the march, sampling and ray-tracing layouts, so it is built after the other programs.
    void ConstructInspection(const std::shared_ptr<DescriptorLayout>& DistanceLayout, const std::shared_ptr<DescriptorLayout>& SamplingLayout,
                             const std::shared_ptr<DescriptorLayout>& RayTracingLayout);
    void RecordInspection(CommandSequence& Sequence, const std::shared_ptr<DescriptorGroup>& DistanceGroup,
                          const std::shared_ptr<DescriptorGroup>& SamplingGroup, const std::shared_ptr<DescriptorGroup>& RayTracingGroup);
    void RecordResolve(CommandSequence& Sequence, uint32_t Parity);
    // 📝 The resolve in two halves, for a layer drawn between them (the environment overlay): the recombination, then the
    //    temporal antialiasing of Source (the overlaid image; null = the recombined image) and the eye adaptation. The
    //    adaptation meters the recombined image, so the overlay leaves the exposure where the demo puts it.
    void RecordRecombination(CommandSequence& Sequence);
    void RecordAntialias(CommandSequence& Sequence, uint32_t Parity, const std::shared_ptr<DeviceTextureView>& Source);
    void RecordPresent(CommandSequence& Sequence, uint32_t Parity, const std::shared_ptr<DeviceTextureView>& Target);

    std::array<uint32_t, 2>                           ScreenExtent{0, 0};
    std::shared_ptr<DeviceTexture>                    RecombinedTexture{}, InspectionTexture{}, PostTexture{};
    std::array<std::shared_ptr<DeviceTexture>, 2>     ResolvedTextures{};
    std::shared_ptr<DeviceTextureView>                RecombinedView{}, InspectionView{}, PostView{};
    std::array<std::shared_ptr<DeviceTextureView>, 2> ResolvedViews{};
    std::shared_ptr<DeviceRegion>                     AdaptationRegion{};
    std::shared_ptr<DeviceSampler>                    LinearClamp{};

  private:
    DeviceExchange&                   Exchange_;
    ShaderInterchange&                Shaders_;
    std::shared_ptr<DeviceRegion>     RefreshRegion_{}, MaterialRegion_{}, OccupantRegion_{};
    std::shared_ptr<DescriptorLayout> RecombineLayout_{}, AntialiasLayout_{}, ExposureLayout_{}, PresentLayout_{}, InspectionLayout_{};
    std::shared_ptr<DeviceProgram>    RecombineProgram_{}, AntialiasProgram_{}, ExposureProgram_{}, PresentProgram_{}, InspectionProgram_{};
    std::shared_ptr<DescriptorGroup>  RecombineGroup_{}, RecombinedExposureGroup_{}, InspectionGroup_{};
    std::array<std::shared_ptr<DescriptorGroup>, 2> AntialiasGroups_{}, ExposureGroups_{}, PresentGroups_{}, SourceAntialiasGroups_{};
    std::shared_ptr<DeviceTextureView>              AntialiasSource_{};
    std::shared_ptr<DeviceTexture>    FlareVisibilityTexture_{}, FlareImageTexture_{}, FlareTransmissionTexture_{}, IdleSmokeTexture_{};
};

} // namespace Frontier::GlobalIllumination
