/*==========================================================================================================================================
                                          PATHREPLAYSUBMISSIONSEQUENCE.CPP
==========================================================================================================================================*/
// 🧩 Validate replay revisions and advance retained history only after caller-confirmed GPU completion.

#include "PathReplaySubmissionSequence.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace Frontier
{
namespace
{
void RequireSequence(bool Condition, const char* Message)
{
    if (!Condition) throw std::invalid_argument(Message);
}

PathReplayHistorySpecification DescribeReplayHistory(const LightComputeFootprint& Extent)
{
    const std::uint32_t Options = Extent.Primary[3];
    PathReplayHistorySpecification Result{};
    Result.BounceCount = Extent.Primary[2];
    Result.PositiveFloor = Extent.Maximum[0];
    Result.Environment = {Extent.Minimum[0], Extent.Minimum[1], Extent.Minimum[2]};
    Result.PrimaryVisibility = Extent.Primary[0] != 0;
    Result.RoughOpaque = (Options & PathReplayRoughOpaque) != 0;
    Result.SampleFacets = (Options & PathReplaySampleFacets) != 0;
    Result.DoubleCountForAudit = (Options & PathReplayAuditDoubleCount) != 0;
    Result.StrictMotionForAudit = (Options & PathReplayAuditStrictMotion) != 0;
    Result.BalanceSampling = (Options & PathReplayBalanceSampling) != 0;
    Result.TexturedDiffuse = (Options & PathReplayTexturedDiffuse) != 0;
    Result.ImportanceRough = (Options & PathReplayImportanceRough) != 0;
    Result.PowerWeightedFacets = (Options & PathReplayPowerWeightedFacets) != 0;
    Result.UniformFacetMixture = (Options & PathReplayUniformFacetMixture) != 0;
    Result.InitialCandidates = std::max(Extent.Sampling[1], 1u);
    Result.PowerMis = (Options & PathReplayPowerMis) != 0;
    Result.CompactStorage = (Options & PathReplayCompactStorage) != 0;
    Result.AuthoredMaterials = (Options & PathReplayAuthoredMaterials) != 0;
    return Result;
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                             RECORD AND COMPLETION
//------------------------------------------------------------------------------------------------------------------------

PathReplaySequenceRecording PathReplaySubmissionSequence::RecordReplay(VkCommandBuffer Commands,
                                                                  const PathReplayVulkanAccess& Access,
                                                                  const PathReplayVulkanBuffers& Buffers,
                                                                  const PathReplaySceneView& Scene,
                                                                  const LightComputeFootprint& Extent,
                                                                  std::uint64_t StorageRevision,
                                                                  bool AllowCameraOnly,
                                                                  std::uint64_t InputRevision)
{
    RequireSequence(!IsPending(), "A replay submission is already pending");
    RequireSequence(StorageRevision != 0 && InputRevision != 0, "Replay storage and input revisions must be non-zero");
    RequireSequence(NextSubmission != std::numeric_limits<std::uint64_t>::max(), "Replay submission identity exhausted");
    const std::uint64_t Count = Extent.Dimensions[0];
    const bool Primary = Extent.Primary[0] != 0;
    RequireSequence(Count > 0 && std::uint64_t(Extent.History[0]) * Extent.History[1] == Count,
                    "Replay dimensions do not match the receiver count");
    RequireSequence((Primary && Scene.Receivers.empty()) || Scene.Receivers.size() == Count,
                    "Replay scene receiver span does not match the dispatch");
    RequireSequence(Scene.Surfaces.size() == Extent.Sampling[2] && Scene.Enclosures.size() == Extent.Grid[3],
                    "Replay scene geometry does not match the dispatch");
    RequireSequence(!AllowCameraOnly || Primary, "Camera-only continuation requires primary visibility");

    const std::uint32_t Options = Extent.Primary[3];
    PathReplayDomainSpecification Domain{};
    Domain.PrimaryVisibility = Primary;
    Domain.RoughOpaque = (Options & PathReplayRoughOpaque) != 0;
    Domain.TexturedDiffuse = (Options & PathReplayTexturedDiffuse) != 0;
    Domain.ImportanceRough = (Options & PathReplayImportanceRough) != 0;
    Domain.DiffuseCameraReprojection = (Options & (PathReplayDiffuseCameraReprojection | PathReplaySampleHistory)) != 0;
    // 📝 This also validates both camera projections before optional camera-only continuation.
    Domain.VarianceFilter = (Options & PathReplayVarianceFilter) != 0 || AllowCameraOnly;
    Domain.HardwareRayQuery = (Options & PathReplayHardwareRayQuery) != 0;
    Domain.AuthoredMaterials = (Options & PathReplayAuthoredMaterials) != 0;
    ValidatePathReplayDomain(Scene, Domain);

    constexpr std::uint32_t ResetOptions = PathReplayResetReconstruction | PathReplayResetSampleHistory |
                                            PathReplayResetVarianceFilter;
    Policy CurrentPolicy{};
    CurrentPolicy.StorageRevision = StorageRevision;
    CurrentPolicy.InputRevision = InputRevision;
    CurrentPolicy.Layout = Access.PipelineLayout;
    CurrentPolicy.Descriptors = Access.Descriptors;
    CurrentPolicy.Primary = Access.Primary;
    CurrentPolicy.Generate = Access.Generate;
    CurrentPolicy.Resolve = Access.Resolve;
    CurrentPolicy.GatherReconstruction = Access.GatherReconstruction;
    CurrentPolicy.FinalizeReconstruction = Access.FinalizeReconstruction;
    CurrentPolicy.GatherSampleHistory = Access.GatherSampleHistory;
    CurrentPolicy.FinalizeSampleHistory = Access.FinalizeSampleHistory;
    CurrentPolicy.GatherVarianceHistory = Access.GatherVarianceHistory;
    CurrentPolicy.IntegrateVarianceMoments = Access.IntegrateVarianceMoments;
    CurrentPolicy.FilterVarianceWavelet = Access.FilterVarianceWavelet;
    CurrentPolicy.Width = Extent.History[0];
    CurrentPolicy.Height = Extent.History[1];
    CurrentPolicy.Options = Options & ~ResetOptions;
    CurrentPolicy.Temporal = Extent.Reconstruction[0];
    CurrentPolicy.Spatial = Extent.Reconstruction[1];
    CurrentPolicy.Sources = Extent.Reconstruction[2];
    CurrentPolicy.HistoryLimit = Extent.Reconstruction[3];
    CurrentPolicy.StackLimit = Extent.Sampling[3];
    CurrentPolicy.CameraOffset = Extent.Primary[1];
    std::memcpy(&CurrentPolicy.SourceOffset, &Extent.Maximum[1], sizeof(CurrentPolicy.SourceOffset));

    // 📝 Work on a copy: recording does not advance the history snapshot seen by completed submissions.
    PathReplayHistoryDomain Candidate = CompletedHistory;
    const auto DomainDecision = Candidate.Resolve(Scene, DescribeReplayHistory(Extent));
    const bool PolicyChanged = !CompletedAvailable || !(CompletedPolicy == CurrentPolicy);
    const bool CameraOnly = AllowCameraOnly && DomainDecision.CameraOnly && !PolicyChanged;
    const bool Reset = Extent.History[3] != 0 || (Options & ResetOptions) != 0 || PolicyChanged ||
                       (DomainDecision.Changed && !CameraOnly);

    PathReplaySequenceRecording Recording{};
    Recording.Extent = Extent;
    Recording.Submission = NextSubmission;
    Recording.Reset = Reset;
    Recording.CameraOnly = CameraOnly;
    Recording.Extent.History[2] = Reset ? 0u : CompletedSequence;
    Recording.Extent.History[3] = Reset ? 1u : 0u;
    Recording.Extent.Reconstruction[3] = std::min(Extent.Reconstruction[3], DomainDecision.MaximumHistory);
    if (Reset)
    {
        if ((Options & PathReplayReconstruct) != 0) Recording.Extent.Primary[3] |= PathReplayResetReconstruction;
        if ((Options & PathReplaySampleHistory) != 0) Recording.Extent.Primary[3] |= PathReplayResetSampleHistory;
        if ((Options & PathReplayVarianceFilter) != 0) Recording.Extent.Primary[3] |= PathReplayResetVarianceFilter;
    }

    PendingHistory.emplace(std::move(Candidate));
    PendingPolicy = CurrentPolicy;
    PendingSequence = Reset ? 1u : (CompletedSequence == std::numeric_limits<std::uint32_t>::max()
                                      ? CompletedSequence : CompletedSequence + 1u);
    try
    {
        RecordPathReplay(Commands, Access, Buffers, Recording.Extent);
    }
    catch (...)
    {
        // 📝 No completion can be assumed after a partially recorded or externally failed operation.
        Invalidate();
        throw;
    }
    PendingSubmission = NextSubmission++;
    return Recording;
}

void PathReplaySubmissionSequence::Finalize(std::uint64_t Submission)
{
    RequireSequence(IsPending() && Submission == PendingSubmission, "Replay submission identity does not match");
    CompletedHistory = std::move(*PendingHistory);
    CompletedPolicy = PendingPolicy;
    CompletedSequence = PendingSequence;
    CompletedAvailable = true;
    PendingHistory.reset();
    PendingSubmission = 0;
}

void PathReplaySubmissionSequence::Abandon(std::uint64_t Submission)
{
    RequireSequence(IsPending() && Submission == PendingSubmission, "Replay submission identity does not match");
    Invalidate();
}

void PathReplaySubmissionSequence::Invalidate() noexcept
{
    CompletedHistory.Invalidate();
    PendingHistory.reset();
    PendingSubmission = 0;
    CompletedSequence = 0;
    PendingSequence = 0;
    CompletedAvailable = false;
}

bool PathReplaySubmissionSequence::IsPending() const noexcept
{
    return PendingSubmission != 0;
}
} // namespace Frontier
