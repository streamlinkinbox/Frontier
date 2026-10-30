/*============================================================================================================================================
                                                        FACETCDFSUBMISSION.H
============================================================================================================================================*/
// 🧩 Encode caller-owned facet CDF dispatches and publish reusable residency only after fence completion.

#pragma once
#include "LightComputeFootprint.h"
#include <cstdint>
#include <stdexcept>
#include <vulkan/vulkan.h>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                   RESOURCE CONTRACT
//------------------------------------------------------------------------------------------------------------------------

constexpr std::uint64_t FacetCdfRequiredCacheWords(std::uint32_t Geometry) noexcept
{
    return (std::uint64_t(Geometry) + 5) * 4;
}
constexpr std::uint64_t FacetCdfRequiredOutputQuartets(std::uint32_t Receivers) noexcept
{
    return std::uint64_t(Receivers) * 80;
}

struct FacetCdfRevision
{
    std::uint64_t Geometry = 1, Profile = 1, Allocation = 1;
};
struct FacetCdfDescriptorRanges
{
    std::uint64_t Receivers = 0, Enclosures = 0, Facets = 0;
    std::uint64_t CacheWords = 0, OutputQuartets = 0, CounterWords = 0;
};
struct FacetCdfVulkanAccess
{
    VkDevice                    Device          = VK_NULL_HANDLE;
    VkFence                     Completion      = VK_NULL_HANDLE;
    VkPipelineLayout            Layout          = VK_NULL_HANDLE;
    VkDescriptorSet             Descriptors     = VK_NULL_HANDLE;
    VkPipeline                  Preparation     = VK_NULL_HANDLE;
    VkPipeline                  Generation      = VK_NULL_HANDLE;
    VkPipeline                  Resolution      = VK_NULL_HANDLE;
    PFN_vkCmdBindPipeline       BindPipeline    = nullptr;
    PFN_vkCmdBindDescriptorSets BindDescriptors = nullptr;
    PFN_vkCmdPushConstants      PushConstants   = nullptr;
    PFN_vkCmdDispatch           Dispatch        = nullptr;
    PFN_vkCmdPipelineBarrier    Dependency      = nullptr;
    PFN_vkGetFenceStatus        QueryFence      = nullptr;
    std::uint32_t               MaxGroupCount = 0, MaxPushBytes = 0;
};

// 📝 Slot0–7 and the 128B push range must match the validated stateless diffuse facet-CDF Slang ABI.
//    Slot5 is private geometry-sized cache,not a lighting grid. Caller initializes cache words2/9/10 to zero,
//    clears counters per submission,and supplies upload/output and queue-ownership dependencies. Ranges
//    describe descriptors,not backing allocations. Cache must not alias inputs/output/counters; descriptor
//    contents and pipeline compatibility cannot be inspected here. Revisions are nonzero equality tokens,not
//    timestamps. Revise Geometry on geometry/enclosure/material changes,Profile on header/permutation
//    changes,and Allocation on descriptor rewrites or buffer replacement. Tokens must not wrap/recur while
//    residency can be reused. All Vulkan resources,including the fence,remain caller-owned and alive until
//    successful resolution. Reset the fence before Encode;acknowledge successful QueueSubmit immediately with
//    Acknowledge. Discard only work that was never successfully submitted. Query errors leave ownership
//    pending;drain/tear down the device before reclaiming resources. The interface cannot enforce caller
//    queue submission or prevent external fence resets. Serialize access externally. Command callbacks must
//    be valid nonthrowing Vulkan entry points. This is not history reuse,allocation,presentation,hardware RT
//    or a complete renderer integration.
class FacetCdfSubmission
{
  public:
    FacetCdfSubmission()                                      = default;
    FacetCdfSubmission(const FacetCdfSubmission &)            = delete;
    FacetCdfSubmission &operator=(const FacetCdfSubmission &) = delete;
    FacetCdfSubmission(FacetCdfSubmission &&)                 = delete;
    FacetCdfSubmission &operator=(FacetCdfSubmission &&)      = delete;

    bool Prepared() const noexcept
    {
        return PreparedEnabled;
    }
    bool Pending() const noexcept
    {
        return EncodedEnabled || SubmittedEnabled;
    }

    // 📝 Returns whether preparation was encoded. No command or residency mutation on invalid arguments.
    bool Encode(
        VkCommandBuffer                Commands,
        const FacetCdfVulkanAccess     &Access,
        const FacetCdfDescriptorRanges &Ranges,
        const LightComputeFootprint   &Extent,
        const FacetCdfRevision         &Revision)
    {
        const std::uint64_t Groups   = (std::uint64_t(Extent.Dimensions[0]) + 63) / 64;
        const auto          Geometry = Extent.Sampling[2];
        Require(!Pending() && Commands && Access.Device && Access.Completion && Access.Layout &&
                Access.Descriptors && Access.Preparation && Access.Generation && Access.Resolution);
        Require(Access.BindPipeline && Access.BindDescriptors && Access.PushConstants && Access.Dispatch &&
                Access.Dependency && Access.QueryFence);
        Require(Revision.Geometry && Revision.Profile && Revision.Allocation);
        Require(Groups && Groups <= Access.MaxGroupCount && Access.MaxPushBytes >= sizeof(Extent));
        Require(Geometry && Ranges.Facets >= Geometry && Extent.Grid[3] &&
                Ranges.Enclosures >= Extent.Grid[3] && Ranges.Receivers >= Extent.Dimensions[0]);
        Require(FacetCdfRequiredCacheWords(Geometry) <= Ranges.CacheWords &&
                FacetCdfRequiredCacheWords(Geometry) <= std::uint64_t(UINT32_MAX) + 1);
        Require(FacetCdfRequiredOutputQuartets(Extent.Dimensions[0]) <= Ranges.OutputQuartets &&
                FacetCdfRequiredOutputQuartets(Extent.Dimensions[0]) <= std::uint64_t(UINT32_MAX) + 1 &&
                Ranges.CounterWords >= 24);
        Require(Extent.Primary[2] >= 1 && Extent.Primary[2] <= 8 && Extent.Sampling[3] >= 1 &&
                Extent.Sampling[3] <= 64 && Extent.Reconstruction[0] == 0 && Extent.Reconstruction[1] == 0);
        Require(Access.QueryFence(Access.Device, Access.Completion) == VK_NOT_READY);
        const bool Prepare = !PreparedEnabled || !SameCache(
        Access,
        Ranges,
        Geometry,
        Revision);
        PendingAccess      = Access;
        PendingRanges      = Ranges;
        PendingGeometry    = Geometry;
        PendingRevision    = Revision;
        EncodedEnabled     = true;
        PreparedEnabled    = false;
        Access.BindDescriptors(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, Access.Layout, 0, 1,
                               &Access.Descriptors, 0, nullptr);
        Access.PushConstants(Commands, Access.Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Extent),
                             &Extent);
        const auto Dependency = [&]() {
            VkMemoryBarrier Barrier{};
            Barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            Access.Dependency(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &Barrier, 0, nullptr, 0, nullptr);
        };
        const auto Dispatch = [&](VkPipeline Pipeline, std::uint32_t Count) {
            Access.BindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
            Access.Dispatch(Commands, Count, 1, 1);
        };
        if (Prepare)
        {
            Dispatch(Access.Preparation, 1);
            Dependency();
        }
        Dispatch(Access.Generation, static_cast<std::uint32_t>(Groups));
        Dependency();
        Dispatch(Access.Resolution, static_cast<std::uint32_t>(Groups));
        return Prepare;
    }

    void Acknowledge()
    {
        Require(EncodedEnabled && !SubmittedEnabled);
        EncodedEnabled   = false;
        SubmittedEnabled = true;
    }
    bool Resolve()
    {
        Require(SubmittedEnabled);
        const auto Completion = PendingAccess.QueryFence(PendingAccess.Device, PendingAccess.Completion);
        if (Completion == VK_NOT_READY)
            return false;
        if (Completion != VK_SUCCESS)
            throw std::runtime_error("Facet CDF fence query failed;resources remain pending");
        PreparedAccess   = PendingAccess;
        PreparedRanges   = PendingRanges;
        PreparedGeometry = PendingGeometry;
        PreparedRevision = PendingRevision;
        SubmittedEnabled = false;
        PreparedEnabled  = true;
        return true;
    }
    void Discard()
    {
        Require(EncodedEnabled && !SubmittedEnabled);
        EncodedEnabled  = false;
        PreparedEnabled = false;
    }
    void Invalidate()
    {
        Require(!Pending());
        PreparedEnabled = false;
    }

  private:
    bool                     PreparedEnabled = false, EncodedEnabled = false, SubmittedEnabled = false;
    FacetCdfVulkanAccess     PreparedAccess{}, PendingAccess{};
    FacetCdfDescriptorRanges PreparedRanges{}, PendingRanges{};
    FacetCdfRevision         PreparedRevision{}, PendingRevision{};
    std::uint32_t            PreparedGeometry = 0, PendingGeometry = 0;

    static void Require(bool Condition)
    {
        if (!Condition)
            throw std::invalid_argument("Invalid facet CDF submission or residency transition");
    }
    bool SameCache(
        const FacetCdfVulkanAccess     &Access,
        const FacetCdfDescriptorRanges &Ranges,
        std::uint32_t                  Geometry,
        const FacetCdfRevision         &Revision) const noexcept
    {
        return Revision.Geometry == PreparedRevision.Geometry &&
               Revision.Profile == PreparedRevision.Profile &&
               Revision.Allocation == PreparedRevision.Allocation && Geometry == PreparedGeometry &&
               Access.Device == PreparedAccess.Device && Access.Layout == PreparedAccess.Layout &&
               Access.Descriptors == PreparedAccess.Descriptors &&
               Access.Preparation == PreparedAccess.Preparation &&
               Access.Generation == PreparedAccess.Generation &&
               Access.Resolution == PreparedAccess.Resolution &&
               Ranges.CacheWords == PreparedRanges.CacheWords && Ranges.Facets == PreparedRanges.Facets;
    }
};
} // namespace Frontier
