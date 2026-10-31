/*==========================================================================================================================================
                                           PATHREPLAYSUBMISSIONSEQUENCE.H
==========================================================================================================================================*/
// 🧩 Record replay commands while retaining history only after a completed Vulkan submission.
#pragma once

#include "PathReplayDomain.h"
#include "PathReplayVulkan.h"

#include <cstdint>
#include <optional>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                               RECORDING RESULT
//------------------------------------------------------------------------------------------------------------------------

struct PathReplaySequenceRecording
{
    LightComputeFootprint Extent{};      // [-] - Push constants recorded by RecordPathReplay
    std::uint64_t         Submission = 0; // [-] - Non-zero identity for this pending recording
    bool                  Reset = true;   // [-] - Prior output/history must not be reused
    bool                  CameraOnly = false; // [-] - Approved, validated camera-only revision
};

//------------------------------------------------------------------------------------------------------------------------
//                                                SUBMISSION SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

// 📝 The caller owns buffers, descriptor contents, command buffers, queue synchronization and GPU lifetime.
//     Only one recording may be in flight. Call Finalize after the fence confirms successful completion, or
//     Abandon on failed/uncertain execution. Change StorageRevision when output/history storage changes and
//     InputRevision when GPU inputs not described by Scene change. Keep inputs and output alive through the fence.
//     Abandon does not cancel GPU work: the caller must first quiesce the queue or use disjoint resources.
//     AllowCameraOnly must be set only for the bounded camera-only domain validated by the caller; it is not a
//     general moving/deforming-scene guarantee. This class does not create resources or present a viewport.
class PathReplaySubmissionSequence
{
  public:
    PathReplaySubmissionSequence() = default;
    PathReplaySubmissionSequence(const PathReplaySubmissionSequence&) = delete;
    PathReplaySubmissionSequence& operator=(const PathReplaySubmissionSequence&) = delete;

    [[nodiscard]] PathReplaySequenceRecording RecordReplay(VkCommandBuffer Commands,
                                                      const PathReplayVulkanAccess& Access,
                                                      const PathReplayVulkanBuffers& Buffers,
                                                      const PathReplaySceneView& Scene,
                                                      const LightComputeFootprint& Extent,
                                                      std::uint64_t StorageRevision,
                                                      bool AllowCameraOnly = false,
                                                      std::uint64_t InputRevision = 1u);
    void Finalize(std::uint64_t Submission);
    void Abandon(std::uint64_t Submission);
    void Invalidate() noexcept;
    [[nodiscard]] bool IsPending() const noexcept;

  private:
    struct Policy
    {
        std::uint64_t StorageRevision = 0, InputRevision = 0;
        std::uint32_t Width = 0, Height = 0, Options = 0;
        std::uint32_t Temporal = 0, Spatial = 0, Sources = 0, HistoryLimit = 0;
        std::uint32_t StackLimit = 0, CameraOffset = 0, SourceOffset = 0;
        VkPipelineLayout Layout = VK_NULL_HANDLE;
        VkDescriptorSet Descriptors = VK_NULL_HANDLE;
        VkPipeline Primary = VK_NULL_HANDLE, Generate = VK_NULL_HANDLE, Resolve = VK_NULL_HANDLE;
        VkPipeline GatherReconstruction = VK_NULL_HANDLE, FinalizeReconstruction = VK_NULL_HANDLE;
        VkPipeline GatherSampleHistory = VK_NULL_HANDLE, FinalizeSampleHistory = VK_NULL_HANDLE;
        VkPipeline GatherVarianceHistory = VK_NULL_HANDLE, IntegrateVarianceMoments = VK_NULL_HANDLE,
                   FilterVarianceWavelet = VK_NULL_HANDLE;
        bool operator==(const Policy&) const = default;
    };

    PathReplayHistoryDomain CompletedHistory;
    std::optional<PathReplayHistoryDomain> PendingHistory;
    Policy CompletedPolicy{};
    Policy PendingPolicy{};
    std::uint64_t NextSubmission = 1;
    std::uint64_t PendingSubmission = 0;
    std::uint32_t CompletedSequence = 0;
    std::uint32_t PendingSequence = 0;
    bool CompletedAvailable = false;
};

} // namespace Frontier
