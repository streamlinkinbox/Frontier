/*==========================================================================================================================================
                                                         RESAMPLINGSEQUENCE.H
==========================================================================================================================================*/
// 🧩 ReSTIR GI: initial candidates + temporal reuse, spatial reuse + resolve, and the glossy reflection rays (ResamplingSequence.js).

#pragma once

#include "ShaderInterchange.h"

namespace Frontier::GlobalIllumination
{

class SurfaceProjection;

// 📝 Per refresh: ① ResampleTemporal writes this refresh's guide and reservoirs; ② ResampleSpatial reuses and resolves
//    the demodulated irradiance; ③ TraverseReflection. Reservoirs and guides alternate between two slots by parity.
class ResamplingSequence
{
  public:
    ResamplingSequence(DeviceExchange& Exchange, ShaderInterchange& Shaders);

    void ConstructPrograms(const std::shared_ptr<DeviceRegion>& RefreshRegion, const std::shared_ptr<DeviceRegion>& MaterialRegion,
                           const std::shared_ptr<DescriptorLayout>& DistanceLayout, const std::shared_ptr<DescriptorLayout>& SamplingLayout,
                           const std::shared_ptr<DescriptorLayout>& RayTracingLayout);
    void ConstructTargets(std::array<uint32_t, 2> TraceExtent, const SurfaceProjection& Surface);
    void RecordResampling(CommandSequence& Sequence, uint32_t Parity, const std::shared_ptr<DescriptorGroup>& DistanceGroup,
                          const std::shared_ptr<DescriptorGroup>& SamplingGroup, const std::shared_ptr<DescriptorGroup>& RayTracingGroup);
    // 📝 Beyond the demo: this refresh's guide alone (ReservoirTemporal step ①), for a refresh whose indirect illumination is off;
    //    the reflections' variance filter reads it. RecordResampling writes it otherwise.
    void RecordGuide(CommandSequence& Sequence, uint32_t Parity);
    void RecordReflection(CommandSequence& Sequence, const std::shared_ptr<DescriptorGroup>& DistanceGroup,
                          const std::shared_ptr<DescriptorGroup>& SamplingGroup, const std::shared_ptr<DescriptorGroup>& RayTracingGroup);
    // 📝 RefinementMirrorTrace: smooth surfaces traced per screen pixel into MirrorView (the recombination takes it unfiltered).
    void RecordMirrorTrace(CommandSequence& Sequence, const std::shared_ptr<DescriptorGroup>& DistanceGroup,
                           const std::shared_ptr<DescriptorGroup>& SamplingGroup, const std::shared_ptr<DescriptorGroup>& RayTracingGroup);

    std::array<uint32_t, 2>                         TraceExtent{0, 0};
    std::array<std::shared_ptr<DeviceTexture>, 2>   GuideTextures{};
    std::shared_ptr<DeviceTexture>                  IrradianceTexture{}, ReflectionTexture{}, MirrorTexture{};
    std::array<std::shared_ptr<DeviceTextureView>, 2> GuideViews{};
    std::shared_ptr<DeviceTextureView>              IrradianceView{}, ReflectionView{}, MirrorView{};   // [-] MirrorView: screen resolution
    std::array<std::shared_ptr<DeviceRegion>, 2>    Reservoirs{};

  private:
    DeviceExchange&                   Exchange_;
    ShaderInterchange&                Shaders_;
    std::shared_ptr<DeviceRegion>     RefreshRegion_{}, MaterialRegion_{};
    std::shared_ptr<DescriptorLayout> TemporalLayout_{}, SpatialLayout_{}, ReflectionLayout_{};
    std::shared_ptr<DeviceProgram>    TemporalProgram_{}, SpatialProgram_{}, ReflectionProgram_{}, MirrorProgram_{}, GuideProgram_{};
    std::array<std::shared_ptr<DescriptorGroup>, 2> TemporalGroups_{}, SpatialGroups_{};
    std::shared_ptr<DescriptorGroup>  ReflectionGroup_{}, MirrorGroup_{};
    std::array<uint32_t, 2>           ScreenExtent_{0, 0};
};

struct VarianceSettings
{
    float    MinimumWeight     = 0.08f;
    uint32_t TallyLimit        = 24;
    uint32_t WaveletIterations = 3;
    float    ColourSigma       = 4.0f;
};

// 📝 Reconstruction of one resolved signal: temporal moments, à-trous wavelet iterations, joint bilateral projection.
class VarianceIntegrator
{
  public:
    VarianceIntegrator(DeviceExchange& Exchange, ShaderInterchange& Shaders, std::string Label, const VarianceSettings& Settings);

    void ConstructPrograms(const std::shared_ptr<DeviceRegion>& RefreshRegion);
    void ApplySettings(const VarianceSettings& Settings);
    void ConstructTargets(std::array<uint32_t, 2> TraceExtent, std::array<uint32_t, 2> ScreenExtent, const SurfaceProjection& Surface,
                          const std::array<std::shared_ptr<DeviceTextureView>, 2>& SourceViews,
                          const std::array<std::shared_ptr<DeviceTextureView>, 2>& GuideViews,
                          const std::shared_ptr<DeviceTextureView>& MirrorTarget = nullptr);
    void Record(CommandSequence& Sequence, uint32_t Parity, bool Reconstruct);
    // 📝 RefinementMirrorFilter: the unfiltered signal projected onto the screen into the mirror target given at ConstructTargets.
    void RecordMirrorProjection(CommandSequence& Sequence, uint32_t Parity);

    std::string                        Label{};
    VarianceSettings                   Settings{};
    std::shared_ptr<DeviceTexture>     IntegratedTexture{}, ScreenTexture{};
    std::shared_ptr<DeviceTextureView> ScreenView{};

  private:
    DeviceExchange&                   Exchange_;
    ShaderInterchange&                Shaders_;
    std::shared_ptr<DeviceRegion>     RefreshRegion_{};
    std::array<std::shared_ptr<DeviceRegion>, 5> StepRegions_{};
    std::shared_ptr<DescriptorLayout> TemporalLayout_{}, WaveletLayout_{}, ProjectionLayout_{};
    std::array<std::shared_ptr<DescriptorGroup>, 2> MirrorGroups_{};
    std::shared_ptr<DeviceProgram>    TemporalProgram_{}, WaveletProgram_{}, ProjectionProgram_{};
    std::vector<std::shared_ptr<DeviceTexture>> Textures_{};
    std::array<std::shared_ptr<DescriptorGroup>, 2> TemporalGroups_{};
    std::array<std::array<std::shared_ptr<DescriptorGroup>, 5>, 2> WaveletGroups_{};
    std::array<std::array<std::shared_ptr<DescriptorGroup>, 4>, 2> ProjectionGroups_{};
    std::array<uint32_t, 2>           TraceExtent_{0, 0};
    std::array<uint32_t, 2>           ScreenExtent_{0, 0};
};

} // namespace Frontier::GlobalIllumination
