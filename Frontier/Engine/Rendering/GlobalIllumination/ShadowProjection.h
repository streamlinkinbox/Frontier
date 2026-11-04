/*==========================================================================================================================================
                                                          SHADOWPROJECTION.H
==========================================================================================================================================*/
// 🧩 Cascaded sun shadow depth: bounding-sphere slices of the view frustum, texel-snapped against shimmer (ShadowProjection.js).

#pragma once

#include "SceneStructure.h"
#include "ShaderInterchange.h"

namespace Frontier::GlobalIllumination
{

constexpr uint32_t ShadowCascadeCount = 3;

// 📝 The camera inputs the cascades are fitted to (the demo's ResolveCamera result).
struct CameraPose
{
    Vector3 Position{};
    Vector3 Target{};
    Vector3 Forward{};
    Vector3 Up{0.0, 0.0, 1.0};
    double  VerticalFieldOfView = 1.0471975511965976;   // [rad]
    double  Aspect              = 16.0 / 9.0;           // [-]
    double  Near                = 0.05;                 // [m]
};

class ShadowProjection
{
  public:
    ShadowProjection(DeviceExchange& Exchange, ShaderInterchange& Shaders, uint32_t Extent);

    void ConstructResources(const std::shared_ptr<DeviceRegion>& OccupantRegion);
    void ConstructMaskedProgram(const std::shared_ptr<DeviceRegion>& OccupantRegion, const std::shared_ptr<DeviceRegion>& EffectRegion,
                                const std::shared_ptr<DeviceRegion>& VehicleRegion);
    void ResolveCascades(const CameraPose& Camera, const Vector3& LightDirection);
    void RecordCascades(CommandSequence& Sequence, const SceneStructure& Scene, const std::shared_ptr<DeviceRegion>& VertexRegion);

    uint32_t                                        Extent = 2048;
    std::shared_ptr<DeviceTexture>                  DepthTexture{};
    std::vector<std::shared_ptr<DeviceTextureView>> LayerViews{};
    std::shared_ptr<DeviceTextureView>              LayeredView{};
    std::shared_ptr<DeviceSampler>                  ComparisonSampler{};
    std::shared_ptr<DeviceRegion>                   CascadeRegion{};
    std::shared_ptr<DeviceRegion>                   ReceiverRegion{};
    std::vector<uint8_t>                            LastCascadeBytes{};
    std::vector<uint8_t>                            LastReceiverBytes{};

  private:
    DeviceExchange&                   Exchange_;
    ShaderInterchange&                Shaders_;
    std::shared_ptr<DescriptorLayout> Layout_{}, MaskedLayout_{};
    std::shared_ptr<DescriptorGroup>  Group_{}, MaskedGroup_{};
    std::shared_ptr<DeviceProgram>    Program_{}, MaskedProgram_{};
};

} // namespace Frontier::GlobalIllumination
