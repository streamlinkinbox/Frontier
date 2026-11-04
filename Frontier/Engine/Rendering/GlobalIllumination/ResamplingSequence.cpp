/*==========================================================================================================================================
                                                        RESAMPLINGSEQUENCE.CPP
==========================================================================================================================================*/
// 🧩 ReSTIR GI: initial candidates + temporal reuse, spatial reuse + resolve, and the glossy reflection rays (ResamplingSequence.js).

#include "ResamplingSequence.h"

#include "SurfaceProjection.h"

#include <algorithm>
#include <cstring>

namespace Frontier::GlobalIllumination
{

namespace
{
constexpr uint32_t ReservoirStride = 32;   // [B] PackedReservoir
using D = DescriptorCode;
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                   RESAMPLING SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

ResamplingSequence::ResamplingSequence(DeviceExchange& Exchange, ShaderInterchange& Shaders) : Exchange_(Exchange), Shaders_(Shaders)
{
}

void ResamplingSequence::ConstructPrograms(const std::shared_ptr<DeviceRegion>& RefreshRegion, const std::shared_ptr<DeviceRegion>& MaterialRegion,
                                           const std::shared_ptr<DescriptorLayout>& DistanceLayout, const std::shared_ptr<DescriptorLayout>& SamplingLayout,
                                           const std::shared_ptr<DescriptorLayout>& RayTracingLayout)
{
    RefreshRegion_  = RefreshRegion;
    MaterialRegion_ = MaterialRegion;
    const std::vector<DescriptorSlot> TemporalSlots = {{D::Uniform}, {D::Texture2D}, {D::TextureDepth2D}, {D::Texture2D}, {D::Texture2D},
                                                       {D::StorageTexture2D, TextureFormat::Rgba16Float}, {D::StorageRead}, {D::Storage}};
    const std::vector<DescriptorSlot> SpatialSlots = {{D::Uniform}, {D::Texture2D}, {D::TextureDepth2D}, {D::StorageRead},
                                                      {D::StorageTexture2D, TextureFormat::Rgba16Float}};
    const std::vector<DescriptorSlot> ReflectionSlots = {{D::Uniform}, {D::StorageRead}, {D::Texture2D}, {D::TextureDepth2D},
                                                         {D::StorageTexture2D, TextureFormat::Rgba16Float}};
    TemporalLayout_   = Exchange_.ConstructDescriptorLayout(TemporalSlots, "Reservoir temporal");
    SpatialLayout_    = Exchange_.ConstructDescriptorLayout(SpatialSlots, "Reservoir spatial");
    ReflectionLayout_ = Exchange_.ConstructDescriptorLayout(ReflectionSlots, "Reflection trace");
    TemporalProgram_   = Shaders_.ConstructComputeProgram("ReservoirTemporal", "ResampleTemporal", {TemporalLayout_, DistanceLayout, SamplingLayout, RayTracingLayout});
    SpatialProgram_    = Shaders_.ConstructComputeProgram("ReservoirSpatial", "ResampleSpatial", {SpatialLayout_, DistanceLayout, SamplingLayout, RayTracingLayout});
    ReflectionProgram_ = Shaders_.ConstructComputeProgram("ReflectionTrace", "TraverseReflection", {ReflectionLayout_, DistanceLayout, SamplingLayout, RayTracingLayout});
    MirrorProgram_     = Shaders_.ConstructComputeProgram("ReflectionTrace", "TraverseMirrorReflection",
                                                          {ReflectionLayout_, DistanceLayout, SamplingLayout, RayTracingLayout});
    GuideProgram_      = Shaders_.ConstructComputeProgram("ReservoirTemporal", "ProjectResamplingGuide", {TemporalLayout_});
}

void ResamplingSequence::ConstructTargets(std::array<uint32_t, 2> Extent, const SurfaceProjection& Surface)
{
    TraceExtent = Extent;
    auto Construct = [&](const std::string& Label) {
        TextureSpecification Specification;
        Specification.Label  = Label;
        Specification.Width  = Extent[0];
        Specification.Height = Extent[1];
        Specification.Format = TextureFormat::Rgba16Float;
        Specification.Usage  = TextureUsage::Storage | TextureUsage::Sampled;
        return Exchange_.ConstructTexture(Specification);
    };
    GuideTextures     = {Construct("Resampling guide A"), Construct("Resampling guide B")};
    IrradianceTexture = Construct("Resampled irradiance");
    ReflectionTexture = Construct("Reflection radiance");
    GuideViews        = {Exchange_.ConstructView(GuideTextures[0]), Exchange_.ConstructView(GuideTextures[1])};
    IrradianceView    = Exchange_.ConstructView(IrradianceTexture);
    ReflectionView    = Exchange_.ConstructView(ReflectionTexture);
    for (uint32_t Slot = 0; Slot < 2; ++Slot)
    {
        Reservoirs[Slot] = Exchange_.ConstructRegion(static_cast<VkDeviceSize>(Extent[0]) * Extent[1] * ReservoirStride, "Reservoirs " + std::to_string(Slot));
    }
    for (uint32_t Parity = 0; Parity < 2; ++Parity)
    {
        TemporalGroups_[Parity] = Exchange_.ConstructDescriptorGroup(TemporalLayout_, {RefreshRegion_, Surface.AttributeView, Surface.DepthView, Surface.MotionView,
                                                                                       GuideViews[1 - Parity], GuideViews[Parity], Reservoirs[1 - Parity],
                                                                                       Reservoirs[Parity]});
        SpatialGroups_[Parity] = Exchange_.ConstructDescriptorGroup(SpatialLayout_, {RefreshRegion_, GuideViews[Parity], Surface.DepthView, Reservoirs[Parity],
                                                                                     IrradianceView});
    }
    ReflectionGroup_ = Exchange_.ConstructDescriptorGroup(ReflectionLayout_, {RefreshRegion_, MaterialRegion_, Surface.AttributeView, Surface.DepthView,
                                                                              ReflectionView});
    TextureSpecification Mirror;
    Mirror.Label  = "Mirror radiance";
    Mirror.Width  = Surface.Extent[0];
    Mirror.Height = Surface.Extent[1];
    Mirror.Format = TextureFormat::Rgba16Float;
    Mirror.Usage  = TextureUsage::Storage | TextureUsage::Sampled;
    MirrorTexture = Exchange_.ConstructTexture(Mirror);
    MirrorView    = Exchange_.ConstructView(MirrorTexture);
    ScreenExtent_ = Surface.Extent;
    MirrorGroup_  = Exchange_.ConstructDescriptorGroup(ReflectionLayout_, {RefreshRegion_, MaterialRegion_, Surface.AttributeView, Surface.DepthView, MirrorView},
                                                       "Mirror trace");
}

void ResamplingSequence::RecordResampling(CommandSequence& Sequence, uint32_t Parity, const std::shared_ptr<DescriptorGroup>& DistanceGroup,
                                          const std::shared_ptr<DescriptorGroup>& SamplingGroup, const std::shared_ptr<DescriptorGroup>& RayTracingGroup)
{
    const uint32_t GroupsX = (TraceExtent[0] + 7) / 8, GroupsY = (TraceExtent[1] + 7) / 8;
    Sequence.BindProgram(TemporalProgram_);
    Sequence.BindGroup(1, DistanceGroup);
    Sequence.BindGroup(2, SamplingGroup);
    Sequence.BindGroup(3, RayTracingGroup);
    Sequence.BindGroup(0, TemporalGroups_[Parity]);
    Sequence.Dispatch(GroupsX, GroupsY);
    Sequence.BindProgram(SpatialProgram_);
    Sequence.BindGroup(1, DistanceGroup);
    Sequence.BindGroup(2, SamplingGroup);
    Sequence.BindGroup(3, RayTracingGroup);
    Sequence.BindGroup(0, SpatialGroups_[Parity]);
    Sequence.Dispatch(GroupsX, GroupsY);
}

void ResamplingSequence::RecordGuide(CommandSequence& Sequence, uint32_t Parity)
{
    Sequence.BindProgram(GuideProgram_);
    Sequence.BindGroup(0, TemporalGroups_[Parity]);
    Sequence.Dispatch((TraceExtent[0] + 7) / 8, (TraceExtent[1] + 7) / 8);
}

void ResamplingSequence::RecordReflection(CommandSequence& Sequence, const std::shared_ptr<DescriptorGroup>& DistanceGroup,
                                          const std::shared_ptr<DescriptorGroup>& SamplingGroup, const std::shared_ptr<DescriptorGroup>& RayTracingGroup)
{
    Sequence.BindProgram(ReflectionProgram_);
    Sequence.BindGroup(0, ReflectionGroup_);
    Sequence.BindGroup(1, DistanceGroup);
    Sequence.BindGroup(2, SamplingGroup);
    Sequence.BindGroup(3, RayTracingGroup);
    Sequence.Dispatch((TraceExtent[0] + 7) / 8, (TraceExtent[1] + 7) / 8);
}

void ResamplingSequence::RecordMirrorTrace(CommandSequence& Sequence, const std::shared_ptr<DescriptorGroup>& DistanceGroup,
                                           const std::shared_ptr<DescriptorGroup>& SamplingGroup, const std::shared_ptr<DescriptorGroup>& RayTracingGroup)
{
    Sequence.BindProgram(MirrorProgram_);
    Sequence.BindGroup(0, MirrorGroup_);
    Sequence.BindGroup(1, DistanceGroup);
    Sequence.BindGroup(2, SamplingGroup);
    Sequence.BindGroup(3, RayTracingGroup);
    Sequence.Dispatch((ScreenExtent_[0] + 7) / 8, (ScreenExtent_[1] + 7) / 8);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   VARIANCE INTEGRATOR
//------------------------------------------------------------------------------------------------------------------------

VarianceIntegrator::VarianceIntegrator(DeviceExchange& Exchange, ShaderInterchange& Shaders, std::string IntegratorLabel, const VarianceSettings& Initial)
    : Label(std::move(IntegratorLabel)), Settings(Initial), Exchange_(Exchange), Shaders_(Shaders)
{
}

void VarianceIntegrator::ConstructPrograms(const std::shared_ptr<DeviceRegion>& RefreshRegion)
{
    RefreshRegion_ = RefreshRegion;
    const std::vector<DescriptorSlot> TemporalSlots = {{D::Uniform}, {D::Uniform}, {D::Texture2D}, {D::Texture2D}, {D::Texture2D}, {D::Texture2D},
                                                       {D::Texture2D}, {D::Texture2D}, {D::StorageTexture2D, TextureFormat::Rgba16Float},
                                                       {D::StorageTexture2D, TextureFormat::Rgba16Float}};
    const std::vector<DescriptorSlot> WaveletSlots = {{D::Uniform}, {D::Uniform}, {D::Texture2D}, {D::Texture2D}, {D::StorageTexture2D, TextureFormat::Rgba16Float}};
    const std::vector<DescriptorSlot> ProjectionSlots = {{D::Uniform}, {D::Texture2D}, {D::Texture2D}, {D::Texture2D}, {D::TextureDepth2D},
                                                         {D::StorageTexture2D, TextureFormat::Rgba16Float}};
    TemporalLayout_   = Exchange_.ConstructDescriptorLayout(TemporalSlots, Label + " temporal");
    WaveletLayout_    = Exchange_.ConstructDescriptorLayout(WaveletSlots, Label + " wavelet");
    ProjectionLayout_ = Exchange_.ConstructDescriptorLayout(ProjectionSlots, Label + " projection");
    TemporalProgram_   = Shaders_.ConstructComputeProgram("VarianceTemporal", "IntegrateTemporal", {TemporalLayout_});
    WaveletProgram_    = Shaders_.ConstructComputeProgram("VarianceWavelet", "ConvolveWavelet", {WaveletLayout_});
    ProjectionProgram_ = Shaders_.ConstructComputeProgram("VarianceUpsample", "ProjectScreenSignal", {ProjectionLayout_});
    for (uint32_t Iteration = 0; Iteration < 5; ++Iteration)
    {
        StepRegions_[Iteration] = Exchange_.ConstructRegion(16, Label + " step " + std::to_string(Iteration));
    }
    ApplySettings(Settings);
}

void VarianceIntegrator::ApplySettings(const VarianceSettings& NewSettings)
{
    Settings = NewSettings;
    for (uint32_t Iteration = 0; Iteration < 5; ++Iteration)
    {
        uint8_t        Bytes[16] = {};
        const uint32_t Step      = 1u << (Iteration > 0 ? Iteration - 1 : 0u);
        const float    Scalars[3] = {Settings.MinimumWeight, Settings.ColourSigma, static_cast<float>(Settings.TallyLimit)};
        std::memcpy(Bytes, &Step, 4);
        std::memcpy(Bytes + 4, Scalars, 12);
        Exchange_.WriteRegion(StepRegions_[Iteration], 0, Bytes, sizeof(Bytes));
    }
}

void VarianceIntegrator::ConstructTargets(std::array<uint32_t, 2> TraceExtent, std::array<uint32_t, 2> ScreenExtent, const SurfaceProjection& Surface,
                                          const std::array<std::shared_ptr<DeviceTextureView>, 2>& SourceViews,
                                          const std::array<std::shared_ptr<DeviceTextureView>, 2>& GuideViews,
                                          const std::shared_ptr<DeviceTextureView>& MirrorTarget)
{
    auto Construct = [&](const std::string& Name, std::array<uint32_t, 2> Extent) {
        TextureSpecification Specification;
        Specification.Label  = Label + " " + Name;
        Specification.Width  = Extent[0];
        Specification.Height = Extent[1];
        Specification.Format = TextureFormat::Rgba16Float;
        Specification.Usage  = TextureUsage::Storage | TextureUsage::Sampled;
        return Exchange_.ConstructTexture(Specification);
    };
    IntegratedTexture = Construct("integrated", TraceExtent);
    const std::array<std::shared_ptr<DeviceTexture>, 2> Retained = {Construct("retained A", TraceExtent), Construct("retained B", TraceExtent)};
    const std::array<std::shared_ptr<DeviceTexture>, 2> Moments  = {Construct("moments A", TraceExtent), Construct("moments B", TraceExtent)};
    const std::array<std::shared_ptr<DeviceTexture>, 2> Wavelet  = {Construct("wavelet A", TraceExtent), Construct("wavelet B", TraceExtent)};
    ScreenTexture = Construct("screen", ScreenExtent);
    Textures_     = {IntegratedTexture, Retained[0], Retained[1], Moments[0], Moments[1], Wavelet[0], Wavelet[1], ScreenTexture};
    auto IntegratedView = Exchange_.ConstructView(IntegratedTexture);
    const std::array<std::shared_ptr<DeviceTextureView>, 2> RetainedViews = {Exchange_.ConstructView(Retained[0]), Exchange_.ConstructView(Retained[1])};
    const std::array<std::shared_ptr<DeviceTextureView>, 2> MomentViews   = {Exchange_.ConstructView(Moments[0]), Exchange_.ConstructView(Moments[1])};
    const std::array<std::shared_ptr<DeviceTextureView>, 2> WaveletViews  = {Exchange_.ConstructView(Wavelet[0]), Exchange_.ConstructView(Wavelet[1])};
    ScreenView    = Exchange_.ConstructView(ScreenTexture);
    TraceExtent_  = TraceExtent;
    ScreenExtent_ = ScreenExtent;
    for (uint32_t Parity = 0; Parity < 2; ++Parity)
    {
        TemporalGroups_[Parity] = Exchange_.ConstructDescriptorGroup(TemporalLayout_, {RefreshRegion_, StepRegions_[0], SourceViews[Parity], Surface.MotionView,
                                                                                       GuideViews[Parity], GuideViews[1 - Parity], RetainedViews[1 - Parity],
                                                                                       MomentViews[1 - Parity], IntegratedView, MomentViews[Parity]});
        // 📝 Iteration 0 writes the history; later iterations alternate between the two wavelet targets.
        std::shared_ptr<DeviceTextureView> Source = IntegratedView;
        for (uint32_t Iteration = 0; Iteration < 5; ++Iteration)
        {
            const auto Target = Iteration == 0 ? RetainedViews[Parity] : WaveletViews[(Iteration - 1) & 1u];
            WaveletGroups_[Parity][Iteration] = Exchange_.ConstructDescriptorGroup(WaveletLayout_, {RefreshRegion_, StepRegions_[Iteration], Source,
                                                                                                    GuideViews[Parity], Target});
            Source = Target;
        }
        MirrorGroups_[Parity] = MirrorTarget ? Exchange_.ConstructDescriptorGroup(ProjectionLayout_, {RefreshRegion_, SourceViews[Parity], GuideViews[Parity],
                                                                                                   Surface.AttributeView, Surface.DepthView, MirrorTarget})
                                             : nullptr;
        const std::array<std::shared_ptr<DeviceTextureView>, 4> Candidates = {SourceViews[Parity], RetainedViews[Parity], WaveletViews[0], WaveletViews[1]};
        for (uint32_t Candidate = 0; Candidate < 4; ++Candidate)
        {
            ProjectionGroups_[Parity][Candidate] = Exchange_.ConstructDescriptorGroup(ProjectionLayout_, {RefreshRegion_, Candidates[Candidate], GuideViews[Parity],
                                                                                                          Surface.AttributeView, Surface.DepthView, ScreenView});
        }
    }
}

void VarianceIntegrator::Record(CommandSequence& Sequence, uint32_t Parity, bool Reconstruct)
{
    const uint32_t GroupsX = (TraceExtent_[0] + 7) / 8, GroupsY = (TraceExtent_[1] + 7) / 8;
    uint32_t Projection = 0;
    if (Reconstruct)
    {
        Sequence.BindProgram(TemporalProgram_);
        Sequence.BindGroup(0, TemporalGroups_[Parity]);
        Sequence.Dispatch(GroupsX, GroupsY);
        const uint32_t Iterations = std::max(1u, std::min(4u, Settings.WaveletIterations + 1));
        Sequence.BindProgram(WaveletProgram_);
        for (uint32_t Iteration = 0; Iteration < Iterations; ++Iteration)
        {
            Sequence.BindGroup(0, WaveletGroups_[Parity][Iteration]);
            Sequence.Dispatch(GroupsX, GroupsY);
        }
        Projection = Iterations == 1 ? 1u : 2u + ((Iterations - 2) & 1u);
    }
    Sequence.BindProgram(ProjectionProgram_);
    Sequence.BindGroup(0, ProjectionGroups_[Parity][Projection]);
    Sequence.Dispatch((ScreenExtent_[0] + 7) / 8, (ScreenExtent_[1] + 7) / 8);
}

void VarianceIntegrator::RecordMirrorProjection(CommandSequence& Sequence, uint32_t Parity)
{
    if (!MirrorGroups_[Parity]) return;
    Sequence.BindProgram(ProjectionProgram_);
    Sequence.BindGroup(0, MirrorGroups_[Parity]);
    Sequence.Dispatch((ScreenExtent_[0] + 7) / 8, (ScreenExtent_[1] + 7) / 8);
}

} // namespace Frontier::GlobalIllumination
