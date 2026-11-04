/*==========================================================================================================================================
                                             ATMOSPHERE PROJECTION DEFINITION
==========================================================================================================================================*/
// 🧩 Dirty-gated per-realm RGBA16F Transmittance, Multi-Scattering, and Atmosphere Sky-View 2D-Array compute bakes.

#include "Engine/PlanetaryAtmosphere/AtmosphereProjection.h"

#include <algorithm>
#include <array>

namespace Frontier
{
namespace
{
struct PushHeader
{
    uint32_t Counts[4];
    float    Tuning[4];
};
} // namespace

bool AtmosphereProjection::Initialize(VulkanComputeExchange& Exchange,
                                      const std::string&     ShaderBinaryDirectory,
                                      std::string*           OutDiagnostic)
{
    const std::array<VkDescriptorType, 4> Bindings{{
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    }};

    if (!Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/BakeTransmittanceLut.spv",
                                     "BakeTransmittanceLutMain",
                                     Bindings,
                                     sizeof(PushHeader),
                                     TransmittanceProgram_,
                                     OutDiagnostic))
    {
        return false;
    }
    if (!Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/BakeMultiScatterLut.spv",
                                     "BakeMultiScatterLutMain",
                                     Bindings,
                                     sizeof(PushHeader),
                                     MultiScatterProgram_,
                                     OutDiagnostic))
    {
        return false;
    }
    if (!Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/BakeAtmosphereProjectionLut.spv",
                                     "BakeAtmosphereProjectionLutMain",
                                     Bindings,
                                     sizeof(PushHeader),
                                     SkyViewProgram_,
                                     OutDiagnostic))
    {
        return false;
    }
    return true;
}

void AtmosphereProjection::Shutdown(VulkanComputeExchange& Exchange) noexcept
{
    Exchange.ReleaseImage(SkyViewArray_);
    Exchange.ReleaseImage(MultiScatteringArray_);
    Exchange.ReleaseImage(TransmittanceArray_);
    Exchange.ReleaseProgram(SkyViewProgram_);
    Exchange.ReleaseProgram(MultiScatterProgram_);
    Exchange.ReleaseProgram(TransmittanceProgram_);
    BakedAtmosphereRevisions_.clear();
    BakedSkyViewRevisions_.clear();
    BakedSampling_.clear();
    BakedMultiple_.clear();
}

bool AtmosphereProjection::Execute(VulkanComputeExchange&                   Exchange,
                                   const DeviceMemorySpan&                  EnsembleSpan,
                                   std::span<const AtmosphereRealmRevision> RealmRevisions)
{
    const uint32_t RealmCount = std::max<uint32_t>(1u, static_cast<uint32_t>(RealmRevisions.size()));
    if (TransmittanceArray_.ArrayLayers < RealmCount)
    {
        if (!Exchange.AllocateRgba16fArray(kTransmittanceLutWidth, kTransmittanceLutHeight, RealmCount, TransmittanceArray_) ||
            !Exchange.AllocateRgba16fArray(kMultiScatteringLutDim, kMultiScatteringLutDim, RealmCount, MultiScatteringArray_) ||
            !Exchange.AllocateRgba16fArray(kSkyViewLutWidth, kSkyViewLutHeight, RealmCount, SkyViewArray_))
        {
            return false;
        }
        BakedAtmosphereRevisions_.assign(RealmCount, 0u);
        BakedSkyViewRevisions_.assign(RealmCount, 0u);
        BakedSampling_.assign(RealmCount,0);
        BakedMultiple_.assign(RealmCount,false);
    }

    LastAtmosphereBakedCount_ = 0u;
    LastSkyViewBakedCount_    = 0u;

    for (size_t RealmIdx = 0u; RealmIdx < RealmRevisions.size(); ++RealmIdx)
    {
        const AtmosphereRealmRevision& Revision  = RealmRevisions[RealmIdx];
        const bool AtmoDirty = BakedAtmosphereRevisions_[RealmIdx] != Revision.AtmosphereRevision ||
            BakedSampling_[RealmIdx] != Revision.IntegrationSamples || BakedMultiple_[RealmIdx] != Revision.MultipleScattering;
        const bool                     SkyDirty  = AtmoDirty || (BakedSkyViewRevisions_[RealmIdx] != Revision.SkyViewRevision);

        if (AtmoDirty)
        {
            PushHeader Push{{kTransmittanceLutWidth, kTransmittanceLutHeight, static_cast<uint32_t>(RealmIdx), 0u},
                            {static_cast<float>(std::clamp(Revision.IntegrationSamples,8u,64u)), Revision.MultipleScattering ? 1.0f : 0.0f, 0.0f, 0.0f}};
            const std::array<ComputeDescriptorArgument, 4> TransArgs{{
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &EnsembleSpan, nullptr},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &MultiScatteringArray_},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &SkyViewArray_},
                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &TransmittanceArray_},
            }};
            if (!Exchange.DispatchCompute(TransmittanceProgram_,
                                          TransArgs,
                                          &Push,
                                          sizeof(Push),
                                          (kTransmittanceLutWidth + 7u) / 8u,
                                          (kTransmittanceLutHeight + 7u) / 8u,
                                          1u))
            {
                return false;
            }

            Push.Counts[0] = kMultiScatteringLutDim;
            Push.Counts[1] = kMultiScatteringLutDim;
            const std::array<ComputeDescriptorArgument, 4> MsArgs{{
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &EnsembleSpan, nullptr},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &TransmittanceArray_},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &SkyViewArray_},
                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &MultiScatteringArray_},
            }};
            if (!Exchange.DispatchCompute(MultiScatterProgram_,
                                          MsArgs,
                                          &Push,
                                          sizeof(Push),
                                          (kMultiScatteringLutDim + 7u) / 8u,
                                          (kMultiScatteringLutDim + 7u) / 8u,
                                          1u))
            {
                return false;
            }

            BakedAtmosphereRevisions_[RealmIdx] = Revision.AtmosphereRevision;
            BakedSampling_[RealmIdx] = Revision.IntegrationSamples;
            BakedMultiple_[RealmIdx] = Revision.MultipleScattering;
            ++LastAtmosphereBakedCount_;
        }

        if (SkyDirty)
        {
            PushHeader Push{{kSkyViewLutWidth, kSkyViewLutHeight, static_cast<uint32_t>(RealmIdx), 0u},
                            {static_cast<float>(std::clamp(Revision.IntegrationSamples,8u,64u)), Revision.MultipleScattering ? 1.0f : 0.0f, 0.0f, 0.0f}};
            const std::array<ComputeDescriptorArgument, 4> SkyArgs{{
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &EnsembleSpan, nullptr},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &TransmittanceArray_},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &MultiScatteringArray_},
                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &SkyViewArray_},
            }};
            if (!Exchange.DispatchCompute(SkyViewProgram_,
                                          SkyArgs,
                                          &Push,
                                          sizeof(Push),
                                          (kSkyViewLutWidth + 7u) / 8u,
                                          (kSkyViewLutHeight + 7u) / 8u,
                                          1u))
            {
                return false;
            }

            BakedSkyViewRevisions_[RealmIdx] = Revision.SkyViewRevision;
            ++LastSkyViewBakedCount_;
        }
    }

    return true;
}

bool AtmosphereProjection::ReadbackTransmittanceLayer(VulkanComputeExchange& Exchange,
                                                      uint32_t               RealmIndex,
                                                      std::vector<float>&    OutRgba)
{
    return Exchange.DownloadRgba16fLayer(TransmittanceArray_, RealmIndex, OutRgba);
}

bool AtmosphereProjection::ReadbackMultiScatteringLayer(VulkanComputeExchange& Exchange,
                                                        uint32_t               RealmIndex,
                                                        std::vector<float>&    OutRgba)
{
    return Exchange.DownloadRgba16fLayer(MultiScatteringArray_, RealmIndex, OutRgba);
}

bool AtmosphereProjection::ReadbackSkyViewLayer(VulkanComputeExchange& Exchange,
                                                uint32_t               RealmIndex,
                                                std::vector<float>&    OutRgba)
{
    return Exchange.DownloadRgba16fLayer(SkyViewArray_, RealmIndex, OutRgba);
}

} // namespace Frontier
