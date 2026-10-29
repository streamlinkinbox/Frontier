/*==========================================================================================================================================
                                         REPLAYSUBMISSIONSEQUENCEVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Verify replay submission sequencing, history reset policy and failure-safe completion.
#define VK_NO_PROTOTYPES
#include "Engine/Rendering/PathReplaySubmissionSequence.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace
{
using namespace Frontier;

void Require(bool Condition, const char* Message)
{
    if (!Condition) throw std::runtime_error(Message);
}

template <class T> T Handle(std::uintptr_t Identity)
{
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(Identity);
    else return static_cast<T>(Identity);
}

template <class Action> void RequireRejection(Action Check)
{
    bool Rejected = false;
    try { Check(); }
    catch (const std::invalid_argument&) { Rejected = true; }
    Require(Rejected, "Invalid sequence request was accepted");
}

uint32_t Dispatches = 0;
VKAPI_ATTR void VKAPI_CALL BindPipeline(VkCommandBuffer, VkPipelineBindPoint, VkPipeline) {}
VKAPI_ATTR void VKAPI_CALL BindDescriptors(VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout, uint32_t,
                                            uint32_t, const VkDescriptorSet*, uint32_t, const uint32_t*) {}
VKAPI_ATTR void VKAPI_CALL PushConstants(VkCommandBuffer, VkPipelineLayout, VkShaderStageFlags, uint32_t,
                                          uint32_t, const void*) {}
VKAPI_ATTR void VKAPI_CALL Dispatch(VkCommandBuffer, uint32_t, uint32_t, uint32_t) { ++Dispatches; }
VKAPI_ATTR void VKAPI_CALL Barrier(VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags, VkDependencyFlags,
                                    uint32_t, const VkMemoryBarrier*, uint32_t, const VkBufferMemoryBarrier*,
                                    uint32_t, const VkImageMemoryBarrier*) {}

PathReplayVulkanAccess MakeAccess()
{
    PathReplayVulkanAccess Access{};
    Access.PipelineLayout = Handle<VkPipelineLayout>(1);
    Access.Descriptors = Handle<VkDescriptorSet>(2);
    Access.Primary = Handle<VkPipeline>(3);
    Access.Generate = Handle<VkPipeline>(4);
    Access.Resolve = Handle<VkPipeline>(5);
    Access.GatherReconstruction = Handle<VkPipeline>(6);
    Access.FinalizeReconstruction = Handle<VkPipeline>(7);
    Access.GatherSampleHistory = Handle<VkPipeline>(8);
    Access.FinalizeSampleHistory = Handle<VkPipeline>(9);
    Access.GatherVarianceHistory = Handle<VkPipeline>(10);
    Access.IntegrateVarianceMoments = Handle<VkPipeline>(11);
    Access.FilterVarianceWavelet = Handle<VkPipeline>(12);
    Access.BindPipeline = BindPipeline;
    Access.BindDescriptors = BindDescriptors;
    Access.PushConstants = PushConstants;
    Access.Dispatch = Dispatch;
    Access.PipelineBarrier = Barrier;
    Access.MaxGroupCountX = 65535u;
    Access.MaxPushConstantBytes = sizeof(LightComputeFootprint);
    return Access;
}

LightComputeFootprint MakeExtent()
{
    LightComputeFootprint Extent{};
    Extent.Dimensions = {4, 0, 0, 0};
    Extent.Minimum = {0, 0, 0, 0};
    Extent.Maximum = {.01f, 0, 0, 0};
    Extent.Grid[3] = 0;
    Extent.Sampling = {17, 1, 0, 64};
    Extent.History = {2, 2, 0, 0};
    Extent.Reconstruction = {1, 0, 0, 64};
    Extent.Primary = {0, 0, 1, 0};
    return Extent;
}

std::array<std::array<float, 4>, 12> MakeCamera()
{
    std::array<std::array<float, 4>, 12> Camera{};
    Camera[0] = {0, 0, 2, .01f};
    Camera[1] = {0, 0, -1, 10};
    Camera[2] = {1, 0, 0, .5f};
    Camera[3] = {0, 1, 0, .5f};
    for (uint32_t Index = 0; Index < 4u; ++Index) Camera[Index + 4u] = Camera[Index];
    Camera[11] = {1, 0, 1, 0};
    return Camera;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            FAILURE AND REVISION CHECKS
//------------------------------------------------------------------------------------------------------------------------

void ValidateCompletionAndPolicy()
{
    const auto Commands = Handle<VkCommandBuffer>(20);
    const auto Access = MakeAccess();
    auto Extent = MakeExtent();
    PathReplayVulkanBuffers Buffers{4, 0, 0, 0, PathReplayRequiredOutputRecords(4), PathReplayCounterWords};
    std::vector<PathReplayReceiver> Receivers(4);
    const auto Camera = MakeCamera();
    const PathReplaySceneView Scene{Receivers, {}, {}, {}, {}, Camera, 0};
    PathReplaySubmissionSequence Sequence;

    auto First = Sequence.RecordReplay(Commands, Access, Buffers, Scene, Extent, 1);
    Require(First.Reset && First.Extent.History[2] == 0 && First.Extent.History[3] == 1 && Sequence.IsPending(),
            "First replay recording did not reset history");
    const uint32_t Recorded = Dispatches;
    RequireRejection([&] { (void)Sequence.RecordReplay(Commands, Access, Buffers, Scene, Extent, 1); });
    Require(Dispatches == Recorded && Sequence.IsPending(), "Concurrent recording changed the pending sequence");
    RequireRejection([&] { Sequence.Finalize(First.Submission + 1u); });
    Require(Sequence.IsPending(), "Wrong completion identity advanced history");
    Sequence.Finalize(First.Submission);
    Extent.Sampling[0] = 19u;
    auto Next = Sequence.RecordReplay(Commands, Access, Buffers, Scene, Extent, 1);
    Require(!Next.Reset && Next.Extent.History[2] == 1 && Next.Extent.History[3] == 0 &&
                Next.Extent.Reconstruction[3] == 64,
            "Changing only the fresh random seed must retain completed history");
    Sequence.Abandon(Next.Submission);
    auto Recovered = Sequence.RecordReplay(Commands, Access, Buffers, Scene, Extent, 1);
    Require(Recovered.Reset && Recovered.Extent.History[2] == 0,
            "Abandoned GPU execution did not invalidate histories");
    Sequence.Finalize(Recovered.Submission);

    Extent.Reconstruction[1] = 1;
    auto SpatialToggle = Sequence.RecordReplay(Commands, Access, Buffers, Scene, Extent, 1);
    Require(SpatialToggle.Reset, "Spatial policy revision did not reset histories");
    Sequence.Finalize(SpatialToggle.Submission);
    auto AllocationToggle = Sequence.RecordReplay(Commands, Access, Buffers, Scene, Extent, 2);
    Require(AllocationToggle.Reset, "New output storage revision did not reset histories");
    Sequence.Finalize(AllocationToggle.Submission);
    auto InputToggle = Sequence.RecordReplay(Commands, Access, Buffers, Scene, Extent, 2, false, 2);
    Require(InputToggle.Reset, "Untracked GPU input revision did not reset histories");
    Sequence.Finalize(InputToggle.Submission);
    auto NewAccess = Access;
    NewAccess.Generate = Handle<VkPipeline>(21);
    auto PipelineToggle = Sequence.RecordReplay(Commands, NewAccess, Buffers, Scene, Extent, 2, false, 2);
    Require(PipelineToggle.Reset, "Replay pipeline replacement did not reset histories");
    Sequence.Finalize(PipelineToggle.Submission);

    const uint32_t BeforeInvalid = Dispatches;
    auto SmallBuffers = Buffers;
    SmallBuffers.OutputColourRecords = 0;
    RequireRejection([&] { (void)Sequence.RecordReplay(Commands, Access, SmallBuffers, Scene, Extent, 2); });
    Require(Dispatches == BeforeInvalid && !Sequence.IsPending(), "Invalid range recorded commands or left a pending ticket");
    auto AfterFailure = Sequence.RecordReplay(Commands, Access, Buffers, Scene, Extent, 2);
    Require(AfterFailure.Reset, "Failed recording did not conservatively invalidate history");
    Sequence.Finalize(AfterFailure.Submission);
    RequireRejection([&] { (void)Sequence.RecordReplay(Commands, Access, Buffers, Scene, Extent, 0); });
}

void ValidateCameraAndOptionalHistories()
{
    const auto Commands = Handle<VkCommandBuffer>(20);
    const auto Access = MakeAccess();
    auto Extent = MakeExtent();
    Extent.Primary[0] = 1;
    Extent.Primary[3] = PathReplaySampleHistory | PathReplayVarianceFilter;
    Extent.Reconstruction[1] = 1;
    PathReplayVulkanBuffers Buffers{4, 0, 0, 48, PathReplayRequiredVarianceFilteredOutputRecords(4),
                                     PathReplayReconstructionCounterWords};
    std::vector<PathReplayReceiver> Receivers(4);
    auto Camera = MakeCamera();
    PathReplaySceneView Scene{Receivers, {}, {}, {}, {}, Camera, 0};
    PathReplaySubmissionSequence Sequence;

    auto First = Sequence.RecordReplay(Commands, Access, Buffers, Scene, Extent, 5, true);
    Require(First.Reset && (First.Extent.Primary[3] & PathReplayResetSampleHistory) != 0 &&
                (First.Extent.Primary[3] & PathReplayResetVarianceFilter) != 0,
            "Initial optional histories were not reset");
    Sequence.Finalize(First.Submission);
    auto Warm = Sequence.RecordReplay(Commands, Access, Buffers, Scene, Extent, 5, true);
    Require(!Warm.Reset && (Warm.Extent.Primary[3] & (PathReplayResetSampleHistory |
                                                      PathReplayResetVarianceFilter)) == 0,
            "Unchanged optional histories were reset unexpectedly");
    Sequence.Finalize(Warm.Submission);

    auto Moved = Camera;
    Moved[0][0] = .1f;
    Scene.Camera = Moved;
    auto CameraOnly = Sequence.RecordReplay(Commands, Access, Buffers, Scene, Extent, 5, true);
    Require(!CameraOnly.Reset && CameraOnly.CameraOnly && CameraOnly.Extent.Reconstruction[3] == 1,
            "Validated camera-only continuation was not recognized or bounded");
    Sequence.Finalize(CameraOnly.Submission);

    auto WrongPrevious = Moved;
    WrongPrevious[0][0] = .2f;
    WrongPrevious[4][0] = -.5f;
    Scene.Camera = WrongPrevious;
    auto CameraCut = Sequence.RecordReplay(Commands, Access, Buffers, Scene, Extent, 5, true);
    Require(CameraCut.Reset && !CameraCut.CameraOnly &&
                (CameraCut.Extent.Primary[3] & PathReplayResetSampleHistory) != 0 &&
                (CameraCut.Extent.Primary[3] & PathReplayResetVarianceFilter) != 0,
            "Mismatched previous projection did not reset optional histories");
    Sequence.Finalize(CameraCut.Submission);

    Extent.Primary[3] &= ~PathReplaySampleHistory;
    auto SamplingToggle = Sequence.RecordReplay(Commands, Access, Buffers, Scene, Extent, 5, true);
    Require(SamplingToggle.Reset && (SamplingToggle.Extent.Primary[3] & PathReplayResetVarianceFilter) != 0,
            "Seed-bank policy transition did not reset histories");
    Sequence.Finalize(SamplingToggle.Submission);
}
} // namespace

int main()
{
    try
    {
        ValidateCompletionAndPolicy();
        ValidateCameraAndOptionalHistories();
        std::printf("PASS ReplaySubmissionSequenceValidation (completion, resets, camera and failure paths)\n");
        return 0;
    }
    catch (const std::exception& Error)
    {
        std::fprintf(stderr, "FAIL ReplaySubmissionSequenceValidation: %s\n", Error.what());
        return 1;
    }
}
