/*==========================================================================================================================================
                                                   PLUMESIMULATIONSEQUENCE.CPP
==========================================================================================================================================*/

#include "PlumeSimulationSequence.h"

#include <algorithm>

namespace Frontier::PlumeSimulation
{

namespace
{

// 📝 ceil(Cells / 4) per axis: the groups of four cubed that cover a lattice.
std::array<uint32_t, 3> ResolveGroups(const std::array<uint32_t, 3>& Extent)
{
    return {(Extent[0] + 3u) / 4u, (Extent[1] + 3u) / 4u, (Extent[2] + 3u) / 4u};
}

TextureSpecification ConstructLatticeSpecification(const char* Label, const std::array<uint32_t, 3>& Extent)
{
    TextureSpecification Specification;
    Specification.Label         = Label;
    Specification.Width         = Extent[0];
    Specification.Height        = Extent[1];
    Specification.DepthOrLayers = Extent[2];
    Specification.Dimension     = TextureDimension::Volumetric;
    Specification.Format        = TextureFormat::Rgba16Float;
    Specification.Usage         = TextureUsage::Storage | TextureUsage::Sampled | TextureUsage::CopySource;
    return Specification;
}

std::shared_ptr<DeviceTextureView> ConstructVolumeView(DeviceExchange& Exchange, const std::shared_ptr<DeviceTexture>& Texture)
{
    TextureViewSpecification Specification;
    Specification.Dimension    = ViewDimension::Volumetric;
    Specification.DimensionSet = true;
    return Exchange.ConstructView(Texture, Specification);
}

} // namespace

PlumeSimulationSequence::PlumeSimulationSequence(DeviceExchange& Exchange, GlobalIllumination::ShaderInterchange& Shaders, PlumeSchedule& Schedule)
    : Exchange_(Exchange), Shaders_(Shaders), Schedule_(Schedule)
{
}

bool PlumeSimulationSequence::ConstructResources(const std::shared_ptr<DescriptorLayout>& DistanceLayout, const std::shared_ptr<DeviceRegion>& OccupantRegion)
{
    using D = DescriptorCode;
    OccupantRegion_ = OccupantRegion;
    RecordRegion_    = Exchange_.ConstructRegion(PlumeRecordBytes, "Plume record");
    FireLightRegion_ = Exchange_.ConstructRegion(FireLightBytes, "Fire lights");
    SourceRegion_    = Exchange_.ConstructRegion(FluidDynamics::PlumeSourceCapacity * FluidDynamics::PlumeSourceStride * sizeof(float), "Plume sources");
    CoverageRegion_  = Exchange_.ConstructRegion(32, "Plume coverage");
    Exchange_.WriteRegion(CoverageRegion_, 0, Schedule_.QueryCoverageReset().data(), 32);
    Exchange_.WriteRegion(FireLightRegion_, 0, Schedule_.QueryFireLightBytes().data(), FireLightBytes);

    SamplerSpecification Lattice;
    Lattice.MagFilter = Lattice.MinFilter = FilterMode::Linear;
    Lattice.AddressU  = Lattice.AddressV = AddressMode::Repeat;
    Lattice.AddressW  = AddressMode::ClampToEdge;
    LatticeSampler_   = Exchange_.ConstructSampler(Lattice);
    SamplerSpecification Detail;
    Detail.MagFilter = Detail.MinFilter = FilterMode::Linear;
    Detail.AddressU  = Detail.AddressV = Detail.AddressW = AddressMode::Repeat;
    DetailSampler_   = Exchange_.ConstructSampler(Detail);

    // 📝 The tiling detail volume: DetailExtent³ rgba8unorm texels, written once.
    TextureSpecification DetailSpecification;
    DetailSpecification.Label         = "Plume detail";
    DetailSpecification.Width         = DetailExtent;
    DetailSpecification.Height        = DetailExtent;
    DetailSpecification.DepthOrLayers = DetailExtent;
    DetailSpecification.Dimension     = TextureDimension::Volumetric;
    DetailSpecification.Format        = TextureFormat::Rgba8Unorm;
    DetailSpecification.Usage         = TextureUsage::Sampled | TextureUsage::CopyTarget;
    DetailTexture_ = Exchange_.ConstructTexture(DetailSpecification);
    const std::vector<uint8_t> Texels = PlumeSchedule::ConstructDetail();
    Exchange_.WriteTexture(DetailTexture_, 0, Texels.data(), Texels.size(), DetailExtent, DetailExtent, DetailExtent);
    DetailView_ = ConstructVolumeView(Exchange_, DetailTexture_);

    const DescriptorSlot LatticeStore{D::StorageTexture3D, TextureFormat::Rgba16Float};
    AdvectionLayout_ = Exchange_.ConstructDescriptorLayout(
        std::vector<DescriptorSlot>{{D::Uniform}, {D::Texture3D}, {D::Texture3D}, {D::Sampler}, LatticeStore, {D::StorageRead}, {D::Storage}, {D::StorageRead}}, "Plume advection");
    VorticityLayout_ = Exchange_.ConstructDescriptorLayout(std::vector<DescriptorSlot>{{D::Uniform}, {D::Texture3D}, {D::Storage}, LatticeStore}, "Plume vorticity");
    PressureLayout_  = Exchange_.ConstructDescriptorLayout(
        std::vector<DescriptorSlot>{{D::Uniform}, {D::Texture3D}, {D::Storage}, {D::Storage}, {D::Storage}, LatticeStore}, "Plume pressure");
    MarchLayout_     = Exchange_.ConstructDescriptorLayout(
        std::vector<DescriptorSlot>{{D::Uniform}, {D::Uniform}, {D::Texture3D}, {D::Sampler}, {D::TextureDepth2D}, {D::StorageRead}, {D::TextureDepth2DLayered},
                                    {D::SamplerComparison}, {D::Uniform}, {D::Texture2D}, {D::Sampler}, {D::StorageTexture2D, TextureFormat::Rgba16Float}, {D::Texture3D},
                                    {D::Uniform}, {D::Texture3D}, {D::Sampler}}, "Plume march");

    Diagnostic_.clear();
    const auto Program = [&](const char* Unit, const char* Entry, std::vector<std::shared_ptr<DescriptorLayout>> Layouts) {
        std::shared_ptr<DeviceProgram> Constructed = Shaders_.ConstructComputeProgram(Unit, Entry, std::move(Layouts));
        if (!Constructed) Diagnostic_ += Shaders_.LastDiagnostic() + "\n";
        return Constructed;
    };
    AdvectVelocityProgram_ = Program("PlumeAdvection", "AdvectVelocity", {AdvectionLayout_, DistanceLayout});
    AdvectThermoProgram_   = Program("PlumeAdvection", "AdvectThermo", {AdvectionLayout_, DistanceLayout});
    LightProgram_          = Program("PlumeAdvection", "ResolveSunTransmittance", {AdvectionLayout_, DistanceLayout});
    CurlProgram_           = Program("PlumeVorticity", "ResolveCurl", {VorticityLayout_});
    ConfineProgram_        = Program("PlumeVorticity", "ConfineVorticity", {VorticityLayout_});
    DivergenceProgram_     = Program("PlumePressure", "ResolveDivergence", {PressureLayout_});
    RelaxProgram_          = Program("PlumePressure", "RelaxPressure", {PressureLayout_});
    ProjectProgram_        = Program("PlumePressure", "ProjectVelocity", {PressureLayout_});
    MarchProgram_          = Program("PlumeMarch", "MarchPlume", {MarchLayout_});
    ConstructLattices();
    return AdvectVelocityProgram_ && AdvectThermoProgram_ && LightProgram_ && CurlProgram_ && ConfineProgram_ && DivergenceProgram_ && RelaxProgram_
        && ProjectProgram_ && MarchProgram_;
}

void PlumeSimulationSequence::ConstructLattices()
{
    const std::array<uint32_t, 3>& VelocityExtent = Schedule_.QueryVelocityExtent();
    const std::array<uint32_t, 3>& ThermoExtent   = Schedule_.QueryThermoExtent();
    VelocityTextures_[0] = Exchange_.ConstructTexture(ConstructLatticeSpecification("Plume velocity A", VelocityExtent));
    VelocityTextures_[1] = Exchange_.ConstructTexture(ConstructLatticeSpecification("Plume velocity B", VelocityExtent));
    ThermoTextures_[0]   = Exchange_.ConstructTexture(ConstructLatticeSpecification("Plume thermo A", ThermoExtent));
    ThermoTextures_[1]   = Exchange_.ConstructTexture(ConstructLatticeSpecification("Plume thermo B", ThermoExtent));
    LightTexture_        = Exchange_.ConstructTexture(ConstructLatticeSpecification("Plume transmittance", VelocityExtent));
    LightView_           = ConstructVolumeView(Exchange_, LightTexture_);
    for (uint32_t Parity = 0; Parity < 2; ++Parity)
    {
        VelocityViews_[Parity] = ConstructVolumeView(Exchange_, VelocityTextures_[Parity]);
        ThermoViews_[Parity]   = ConstructVolumeView(Exchange_, ThermoTextures_[Parity]);
    }
    const uint64_t CoarseCells = static_cast<uint64_t>(VelocityExtent[0]) * VelocityExtent[1] * VelocityExtent[2];
    CurlRegion_       = Exchange_.ConstructRegion(CoarseCells * 16, "Plume curl");
    DivergenceRegion_ = Exchange_.ConstructRegion(CoarseCells * 4, "Plume divergence");
    PressureRegions_  = {Exchange_.ConstructRegion(CoarseCells * 4, "Plume pressure A"), Exchange_.ConstructRegion(CoarseCells * 4, "Plume pressure B")};

    // 📝 Per state parity p: the step reads velocity[p] and thermo[p] and leaves the next state in [1 − p].
    const auto& V = VelocityViews_;
    const auto& T = ThermoViews_;
    const auto& P = PressureRegions_;
    for (uint32_t State = 0; State < 2; ++State)
    {
        AdvectVelocityGroups_[State] = Exchange_.ConstructDescriptorGroup(
            AdvectionLayout_, {RecordRegion_, V[State], T[State], LatticeSampler_, V[1 - State], SourceRegion_, CoverageRegion_, OccupantRegion_});
        AdvectThermoGroups_[State] = Exchange_.ConstructDescriptorGroup(
            AdvectionLayout_, {RecordRegion_, V[1 - State], T[State], LatticeSampler_, T[1 - State], SourceRegion_, CoverageRegion_, OccupantRegion_});
        LightGroups_[State] = Exchange_.ConstructDescriptorGroup(
            AdvectionLayout_, {RecordRegion_, V[1 - State], T[1 - State], LatticeSampler_, LightView_, SourceRegion_, CoverageRegion_, OccupantRegion_});
        VorticityGroups_[State] = Exchange_.ConstructDescriptorGroup(VorticityLayout_, {RecordRegion_, V[1 - State], CurlRegion_, V[State]});
        PressureGroups_[State][0] = Exchange_.ConstructDescriptorGroup(PressureLayout_, {RecordRegion_, V[State], DivergenceRegion_, P[0], P[1], V[1 - State]});
        PressureGroups_[State][1] = Exchange_.ConstructDescriptorGroup(PressureLayout_, {RecordRegion_, V[State], DivergenceRegion_, P[1], P[0], V[1 - State]});
    }
    Schedule_.ResetState();
}

void PlumeSimulationSequence::ReconstructGrade(const PlumeGrade& Configuration)
{
    Schedule_.AssignConfiguration(Configuration);
    ConstructLattices();
}

void PlumeSimulationSequence::ConstructTargets(std::array<uint32_t, 2> TraceExtent, const PlumeMarchInputs& Inputs)
{
    TraceExtent_ = TraceExtent;
    TextureSpecification Specification;
    Specification.Label  = "Plume radiance";
    Specification.Width  = TraceExtent[0];
    Specification.Height = TraceExtent[1];
    Specification.Format = TextureFormat::Rgba16Float;
    Specification.Usage  = TextureUsage::Storage | TextureUsage::Sampled | TextureUsage::CopySource;
    MarchTexture_ = Exchange_.ConstructTexture(Specification);
    MarchView_    = Exchange_.ConstructView(MarchTexture_);
    for (uint32_t State = 0; State < 2; ++State)
    {
        MarchGroups_[State] = Exchange_.ConstructDescriptorGroup(
            MarchLayout_, {Inputs.Refresh, RecordRegion_, ThermoViews_[State], LatticeSampler_, Inputs.SurfaceDepth, CoverageRegion_, Inputs.ShadowLayered,
                           Inputs.ShadowComparison, Inputs.ShadowReceiver, Inputs.SkyView, Inputs.SkySampler, MarchView_, LightView_, FireLightRegion_,
                           DetailView_, DetailSampler_});
    }
}

void PlumeSimulationSequence::UploadStep(const PlumeSourceProjection& Sources)
{
    if (!Schedule_.QueryStepPending())
    {
        return;
    }
    Exchange_.WriteRegion(RecordRegion_, 0, Schedule_.QueryRecordBytes().data(), PlumeRecordBytes);
    if (Sources.QueryCount() > 0)
    {
        Exchange_.WriteRegion(SourceRegion_, 0, Sources.QueryRecords().data(), static_cast<VkDeviceSize>(Sources.QueryCount()) * FluidDynamics::PlumeSourceStride * sizeof(float));
    }
    Exchange_.WriteRegion(CoverageRegion_, 0, Schedule_.QueryCoverageReset().data(), 32);
}

void PlumeSimulationSequence::UploadFireLights()
{
    Exchange_.WriteRegion(FireLightRegion_, 0, Schedule_.QueryFireLightBytes().data(), FireLightBytes);
}

void PlumeSimulationSequence::RecordTransport(CommandSequence& Sequence, const std::shared_ptr<DescriptorGroup>& DistanceGroup)
{
    if (!Schedule_.QueryStepPending())
    {
        return;
    }
    const uint32_t                 State  = Schedule_.QueryState();
    const std::array<uint32_t, 3>  Coarse = ResolveGroups(Schedule_.QueryVelocityExtent());
    const std::array<uint32_t, 3>  Fine   = ResolveGroups(Schedule_.QueryThermoExtent());
    Sequence.BindProgram(AdvectVelocityProgram_);
    Sequence.BindGroup(0, AdvectVelocityGroups_[State]);
    Sequence.BindGroup(1, DistanceGroup);
    Sequence.Dispatch(Coarse[0], Coarse[1], Coarse[2]);
    Sequence.BindProgram(CurlProgram_);
    Sequence.BindGroup(0, VorticityGroups_[State]);
    Sequence.Dispatch(Coarse[0], Coarse[1], Coarse[2]);
    Sequence.BindProgram(ConfineProgram_);
    Sequence.Dispatch(Coarse[0], Coarse[1], Coarse[2]);
    Sequence.BindProgram(DivergenceProgram_);
    Sequence.BindGroup(0, PressureGroups_[State][0]);
    Sequence.Dispatch(Coarse[0], Coarse[1], Coarse[2]);
    Sequence.BindProgram(RelaxProgram_);
    for (uint32_t Sweep = 0; Sweep < Schedule_.QueryConfiguration().PressureSweeps; ++Sweep)
    {
        Sequence.BindGroup(0, PressureGroups_[State][Sweep & 1u]);
        Sequence.Dispatch(Coarse[0], Coarse[1], Coarse[2]);
    }
    Sequence.BindProgram(ProjectProgram_);
    Sequence.BindGroup(0, PressureGroups_[State][0]);
    Sequence.Dispatch(Coarse[0], Coarse[1], Coarse[2]);
    Sequence.BindProgram(AdvectThermoProgram_);
    Sequence.BindGroup(0, AdvectThermoGroups_[State]);
    Sequence.BindGroup(1, DistanceGroup);
    Sequence.Dispatch(Fine[0], Fine[1], Fine[2]);
    Sequence.BindProgram(LightProgram_);
    Sequence.BindGroup(0, LightGroups_[State]);
    Sequence.Dispatch(Coarse[0], Coarse[1], Coarse[2]);
    Schedule_.CompleteStep();
}

void PlumeSimulationSequence::RecordMarch(CommandSequence& Sequence)
{
    if (!Schedule_.QueryResident() || !MarchGroups_[0])
    {
        return;
    }
    Sequence.BindProgram(MarchProgram_);
    Sequence.BindGroup(0, MarchGroups_[Schedule_.QueryState()]);
    Sequence.Dispatch((TraceExtent_[0] + 7u) / 8u, (TraceExtent_[1] + 7u) / 8u);
}

} // namespace Frontier::PlumeSimulation
