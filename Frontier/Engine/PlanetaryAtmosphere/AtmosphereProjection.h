/*==========================================================================================================================================
                                                 ATMOSPHERE PROJECTION
                                     ------------------------------------------------------------------
             Dirty-gated per-realm RGBA16F 2D-Array compute bakes: Transmittance (256x64xR),
                      Multi-Scattering (32x32xR), and Atmosphere Sky-View (192x108xR).
==========================================================================================================================================*/
// 🧩 Physical planetary atmosphere & twilight GPU LUT projection across all active realms.

#pragma once

#include "Engine/DeviceExchange/VulkanComputeExchange.h"
#include "Engine/PlanetaryAtmosphere/AtmosphereScattering.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Frontier
{

struct AtmosphereRealmRevision
{
    uint64_t AtmosphereRevision = 1u;
    uint64_t SkyViewRevision    = 1u;
    uint32_t IntegrationSamples = 40;
    bool MultipleScattering = true;
};

class AtmosphereProjection
{
  public:
    bool Initialize(VulkanComputeExchange& Exchange,
                    const std::string&     ShaderBinaryDirectory,
                    std::string*           OutDiagnostic = nullptr);
    void Shutdown(VulkanComputeExchange& Exchange) noexcept;

    bool Execute(VulkanComputeExchange&                   Exchange,
                 const DeviceMemorySpan&                  EnsembleSpan,
                 std::span<const AtmosphereRealmRevision> RealmRevisions);

    [[nodiscard]] const DeviceImageArraySlot& TransmittanceArray() const noexcept { return TransmittanceArray_; }
    [[nodiscard]] const DeviceImageArraySlot& MultiScatteringArray() const noexcept { return MultiScatteringArray_; }
    [[nodiscard]] const DeviceImageArraySlot& SkyViewArray() const noexcept { return SkyViewArray_; }

    [[nodiscard]] uint32_t LastAtmosphereBakedCount() const noexcept { return LastAtmosphereBakedCount_; }
    [[nodiscard]] uint32_t LastSkyViewBakedCount() const noexcept { return LastSkyViewBakedCount_; }

    bool ReadbackTransmittanceLayer(VulkanComputeExchange& Exchange, uint32_t RealmIndex, std::vector<float>& OutRgba);
    bool ReadbackMultiScatteringLayer(VulkanComputeExchange& Exchange, uint32_t RealmIndex, std::vector<float>& OutRgba);
    bool ReadbackSkyViewLayer(VulkanComputeExchange& Exchange, uint32_t RealmIndex, std::vector<float>& OutRgba);

  private:
    ComputeProgramSlot    TransmittanceProgram_{};
    ComputeProgramSlot    MultiScatterProgram_{};
    ComputeProgramSlot    SkyViewProgram_{};
    DeviceImageArraySlot  TransmittanceArray_{};
    DeviceImageArraySlot  MultiScatteringArray_{};
    DeviceImageArraySlot  SkyViewArray_{};
    std::vector<uint64_t> BakedAtmosphereRevisions_{};
    std::vector<uint64_t> BakedSkyViewRevisions_{};
    std::vector<uint32_t> BakedSampling_{};
    std::vector<bool> BakedMultiple_{};
    uint32_t              LastAtmosphereBakedCount_ = 0u;
    uint32_t              LastSkyViewBakedCount_    = 0u;
};

} // namespace Frontier
