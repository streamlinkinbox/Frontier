/*==========================================================================================================================================
                                                   PLUMESIMULATIONSEQUENCE.H
==========================================================================================================================================*/
// 🧩 The smoke-and-fire cell lattices on the device exchange: the two velocity and thermo lattices, the light lattice, the pressure and curl
//    regions, the nine programs of the PlumeSimulation archive set, the transport step and the march. The device half of PlumeSpace.js
//    (ConstructResources, ConstructLattices, ConstructTargets, RecordTransport, RecordMarch); PlumeSchedule holds the bookkeeping, the step record
//    and the detail volume, and this class uploads what the schedule decided.

#pragma once

#include "PlumeSchedule.h"

#include "Frontier/Engine/DeviceExchange/DeviceExchange.h"
#include "Frontier/Engine/Rendering/GlobalIllumination/ShaderInterchange.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace Frontier::PlumeSimulation
{

// 📝 What the march reads beside the plume's own lattices (PlumeSpace.ConstructTargets): the refresh constants, the picture's depth, the cascaded
//    shadow and the sky view of the illumination port.
struct PlumeMarchInputs
{
    std::shared_ptr<DeviceRegion>      Refresh{};
    std::shared_ptr<DeviceTextureView> SurfaceDepth{};
    std::shared_ptr<DeviceTextureView> ShadowLayered{};
    std::shared_ptr<DeviceSampler>     ShadowComparison{};
    std::shared_ptr<DeviceRegion>      ShadowReceiver{};
    std::shared_ptr<DeviceTextureView> SkyView{};
    std::shared_ptr<DeviceSampler>     SkySampler{};
};

class PlumeSimulationSequence
{
public:
    PlumeSimulationSequence(DeviceExchange& Exchange, GlobalIllumination::ShaderInterchange& Shaders, PlumeSchedule& Schedule);

    // 📝 The constant regions, the samplers, the detail volume, the four layouts and the nine programs; the transport's programs bind the scene
    //    distance field's group after their own (DistanceLayout). Returns false when a program fails (QueryDiagnostic names it).
    bool ConstructResources(const std::shared_ptr<DescriptorLayout>& DistanceLayout, const std::shared_ptr<DeviceRegion>& OccupantRegion);

    // 📝 (Re)builds every lattice-sized resource and descriptor group for the schedule's configuration. The programs do not depend on the extents —
    //    the record carries them — so a grade change costs allocations, never compiles.
    void ConstructLattices();

    // 📝 Switches to another grade: new lattices, a clean plume. The caller rebuilds the march targets afterwards.
    void ReconstructGrade(const PlumeGrade& Configuration);

    void ConstructTargets(std::array<uint32_t, 2> TraceExtent, const PlumeMarchInputs& Inputs);

    // 📝 What PlumeSpace.Schedule writes when a step is pending: the step record, the sources and the coverage reset.
    void UploadStep(const PlumeSourceProjection& Sources);

    // 📝 PlumeSpace.AssignFireLights' device write: every refresh, resident or not.
    void UploadFireLights();

    void RecordTransport(CommandSequence& Sequence, const std::shared_ptr<DescriptorGroup>& DistanceGroup);
    void RecordMarch(CommandSequence& Sequence);

    [[nodiscard]] const std::shared_ptr<DeviceTexture>& QueryVelocityTexture(uint32_t Parity) const { return VelocityTextures_.at(Parity); }
    [[nodiscard]] const std::shared_ptr<DeviceTexture>& QueryThermoTexture(uint32_t Parity) const { return ThermoTextures_.at(Parity); }
    [[nodiscard]] const std::shared_ptr<DeviceTexture>& QueryLightTexture() const { return LightTexture_; }
    [[nodiscard]] const std::shared_ptr<DeviceTexture>& QueryMarchTexture() const { return MarchTexture_; }
    [[nodiscard]] const std::shared_ptr<DeviceRegion>&  QueryCoverageRegion() const { return CoverageRegion_; }
    [[nodiscard]] const std::shared_ptr<DeviceRegion>&  QueryPressureRegion(uint32_t Which) const { return PressureRegions_.at(Which); }
    [[nodiscard]] const std::shared_ptr<DeviceRegion>&  QueryCurlRegion() const { return CurlRegion_; }
    [[nodiscard]] const std::shared_ptr<DeviceRegion>&  QueryDivergenceRegion() const { return DivergenceRegion_; }
    [[nodiscard]] const std::string&                    QueryDiagnostic() const noexcept { return Diagnostic_; }

private:
    DeviceExchange&                        Exchange_;
    GlobalIllumination::ShaderInterchange& Shaders_;
    PlumeSchedule&                         Schedule_;
    std::shared_ptr<DeviceRegion>          OccupantRegion_{};
    std::shared_ptr<DeviceRegion>          RecordRegion_{}, FireLightRegion_{}, SourceRegion_{}, CoverageRegion_{};
    std::shared_ptr<DeviceSampler>         LatticeSampler_{}, DetailSampler_{};
    std::shared_ptr<DeviceTexture>         DetailTexture_{};
    std::shared_ptr<DeviceTextureView>     DetailView_{};
    std::shared_ptr<DescriptorLayout>      AdvectionLayout_{}, VorticityLayout_{}, PressureLayout_{}, MarchLayout_{};
    std::shared_ptr<DeviceProgram>         AdvectVelocityProgram_{}, AdvectThermoProgram_{}, LightProgram_{}, CurlProgram_{}, ConfineProgram_{},
                                           DivergenceProgram_{}, RelaxProgram_{}, ProjectProgram_{}, MarchProgram_{};

    std::array<std::shared_ptr<DeviceTexture>, 2>     VelocityTextures_{}, ThermoTextures_{};
    std::array<std::shared_ptr<DeviceTextureView>, 2> VelocityViews_{}, ThermoViews_{};
    std::shared_ptr<DeviceTexture>                    LightTexture_{};
    std::shared_ptr<DeviceTextureView>                LightView_{};
    std::shared_ptr<DeviceRegion>                     CurlRegion_{}, DivergenceRegion_{};
    std::array<std::shared_ptr<DeviceRegion>, 2>      PressureRegions_{};
    std::array<std::shared_ptr<DescriptorGroup>, 2>   AdvectVelocityGroups_{}, AdvectThermoGroups_{}, LightGroups_{}, VorticityGroups_{};
    std::array<std::array<std::shared_ptr<DescriptorGroup>, 2>, 2> PressureGroups_{};

    std::array<uint32_t, 2>                           TraceExtent_{};
    std::shared_ptr<DeviceTexture>                    MarchTexture_{};
    std::shared_ptr<DeviceTextureView>                MarchView_{};
    std::array<std::shared_ptr<DescriptorGroup>, 2>   MarchGroups_{};
    std::string                                       Diagnostic_{};
};

} // namespace Frontier::PlumeSimulation
