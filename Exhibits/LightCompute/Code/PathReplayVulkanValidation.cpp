/*==========================================================================================================================================
                                                 PATHREPLAYVULKANVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Verify replay recorder ordering, shader barriers and fail-before-recording guards.
#define VK_NO_PROTOTYPES
#include "Engine/Rendering/PathReplayVulkan.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace
{
using namespace Frontier;
static_assert(PathReplayRequiredOutputRecords(65) == 17375);
static_assert(PathReplayRequiredOutputRecords(PathReplayMaxReceivers) - 1 <= UINT32_MAX);
static_assert(PathReplayRequiredOutputRecords(PathReplayMaxReceivers + 1) - 1 > UINT32_MAX);
static_assert(PathReplayRequiredReconstructedOutputRecords(65) == 17765);
static_assert(PathReplayRequiredReconstructedOutputRecords(PathReplayMaxReconstructedReceivers) - 1 <= UINT32_MAX);
static_assert(PathReplayRequiredReconstructedOutputRecords(PathReplayMaxReconstructedReceivers + 1) - 1 > UINT32_MAX);
static_assert(PathReplayRequiredSampleHistoryOutputRecords(65) == 18025);
static_assert(PathReplayRequiredSampleHistoryOutputRecords(PathReplayMaxSampleHistoryReceivers) - 1 <= UINT32_MAX);
static_assert(PathReplayRequiredSampleHistoryOutputRecords(PathReplayMaxSampleHistoryReceivers + 1) - 1 > UINT32_MAX);
static_assert(PathReplayRequiredCompactOutputRecords(65) == 715);
static_assert(PathReplayRequiredCompactOutputRecords(PathReplayMaxCompactReceivers) - 1 <= UINT32_MAX);
static_assert(PathReplayRequiredCompactOutputRecords(PathReplayMaxCompactReceivers + 1) - 1 > UINT32_MAX);
static_assert(PathReplayCompactSlot(0, 65, 0) == 65);
static_assert(PathReplayCompactSlot(4, 65, 64) == 389);
static_assert(PathReplayCompactSlot(11, 65, 0) == 390);
static_assert(PathReplayCompactSlot(14, 65, 64) == 649);
static_assert(PathReplayCompactPrimarySlot(65, 64) == 714);
static_assert(PathReplayCompactSlot(5, 65, 0) == UINT64_MAX);
static_assert(PathReplayCompactSlot(15, 65, 0) == UINT64_MAX);
static_assert(PathReplayCompactSlot(0, 65, 65) == UINT64_MAX);
static_assert(PathReplayCompactPrimarySlot(65, 65) == UINT64_MAX);
template <class T> T Handle(std::uintptr_t Value)
{
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(Value);
    else return static_cast<T>(Value);
}
template <class T> std::uintptr_t HandleValue(T Value)
{
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<std::uintptr_t>(Value);
    else return static_cast<std::uintptr_t>(Value);
}
struct Event { char Kind; std::uintptr_t Value; };
std::vector<Event> Events;
LightComputeFootprint Pushed;
unsigned CallbackErrors = 0;

VKAPI_ATTR void VKAPI_CALL BindPipeline(VkCommandBuffer, VkPipelineBindPoint Point, VkPipeline Pipeline)
{
    if (Point != VK_PIPELINE_BIND_POINT_COMPUTE) ++CallbackErrors;
    Events.push_back({'B', HandleValue(Pipeline)});
}
VKAPI_ATTR void VKAPI_CALL BindDescriptors(VkCommandBuffer, VkPipelineBindPoint Point, VkPipelineLayout Layout,
    std::uint32_t First, std::uint32_t Count, const VkDescriptorSet *Sets, std::uint32_t DynamicCount,
    const std::uint32_t *)
{
    if (Point != VK_PIPELINE_BIND_POINT_COMPUTE || Layout != Handle<VkPipelineLayout>(1) || First != 0 ||
        Count != 1 || !Sets || *Sets != Handle<VkDescriptorSet>(2) || DynamicCount != 0) ++CallbackErrors;
    Events.push_back({'D', Count});
}
VKAPI_ATTR void VKAPI_CALL PushConstants(VkCommandBuffer, VkPipelineLayout Layout, VkShaderStageFlags Stage,
    std::uint32_t Offset, std::uint32_t Size, const void *Data)
{
    if (Layout != Handle<VkPipelineLayout>(1) || Stage != VK_SHADER_STAGE_COMPUTE_BIT || Offset != 0 ||
        Size != sizeof(Pushed) || !Data) ++CallbackErrors;
    if (Size == sizeof(Pushed) && Data) std::memcpy(&Pushed, Data, sizeof(Pushed));
    Events.push_back({'P', Size});
}
VKAPI_ATTR void VKAPI_CALL Dispatch(VkCommandBuffer, std::uint32_t X, std::uint32_t Y, std::uint32_t Z)
{
    if (Y != 1 || Z != 1) ++CallbackErrors;
    Events.push_back({'X', X});
}
VKAPI_ATTR void VKAPI_CALL PipelineBarrier(VkCommandBuffer, VkPipelineStageFlags Source, VkPipelineStageFlags Target,
    VkDependencyFlags Flags, std::uint32_t MemoryCount, const VkMemoryBarrier *Memory,
    std::uint32_t BufferCount, const VkBufferMemoryBarrier *, std::uint32_t ImageCount, const VkImageMemoryBarrier *)
{
    if (Source != VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT || Target != VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT ||
        Flags != 0 || MemoryCount != 1 || !Memory || BufferCount != 0 || ImageCount != 0 ||
        Memory->sType != VK_STRUCTURE_TYPE_MEMORY_BARRIER ||
        (Memory->srcAccessMask != VK_ACCESS_SHADER_WRITE_BIT &&
         Memory->srcAccessMask != (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)) ||
        Memory->dstAccessMask != (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)) ++CallbackErrors;
    Events.push_back({Memory && (Memory->srcAccessMask & VK_ACCESS_SHADER_READ_BIT) ? 'R' : 'M', MemoryCount});
}
void Require(bool Condition, const char *Message)
{
    if (!Condition) throw std::runtime_error(Message);
}
template <class Action> void RequireRejection(Action Check)
{
    Events.clear();
    bool Rejected = false;
    try { Check(); }
    catch (const std::invalid_argument &) { Rejected = true; }
    Require(Rejected && Events.empty(), "Invalid request did not fail before recording");
}
} // namespace

int main()
{
    try
    {
        const auto Commands = Handle<VkCommandBuffer>(3);
        PathReplayVulkanAccess Access{Handle<VkPipelineLayout>(1), Handle<VkDescriptorSet>(2),
            Handle<VkPipeline>(4), Handle<VkPipeline>(5), Handle<VkPipeline>(6),
            BindPipeline, BindDescriptors, PushConstants, Dispatch, PipelineBarrier, 65535, 128};
        LightComputeFootprint Extent;
        Extent.Dimensions[0] = 65;
        Extent.Minimum = {0, .1f, .2f, 0};
        Extent.Maximum = {.01f, 0, 0, 0};
        Extent.Grid[3] = 0;
        Extent.Sampling = {1, 0, 0, 64};
        Extent.History = {13, 5, 1, 0};
        Extent.Reconstruction = {1, 1, 0, 64};
        Extent.Primary = {1, 0, 4, 0};
        PathReplayVulkanBuffers Buffers{65, 0, 0, 48, 30 * 65 + 15425, 23};
        RecordPathReplay(Commands, Access, Buffers, Extent);
        const std::vector<Event> Expected{{'D',1}, {'P',128}, {'B',4}, {'X',2}, {'M',1},
                                          {'B',5}, {'X',2}, {'M',1}, {'B',6}, {'X',2}};
        Require(Events.size() == Expected.size(), "Unexpected command count");
        for (std::size_t I = 0; I < Events.size(); ++I)
            Require(Events[I].Kind == Expected[I].Kind && Events[I].Value == Expected[I].Value,
                    "Replay stage ordering differs");
        Require(CallbackErrors == 0 && std::memcmp(&Pushed, &Extent, sizeof(Pushed)) == 0,
                "Replay descriptor, push constant or barrier differs");
        Events.clear();
        Extent.Primary[0] = 0;
        Access.Primary = VK_NULL_HANDLE;
        RecordPathReplay(Commands, Access, Buffers, Extent);
        const std::vector<Event> WithoutPrimary{{'D',1}, {'P',128}, {'B',5}, {'X',2}, {'M',1}, {'B',6}, {'X',2}};
        Require(Events.size() == WithoutPrimary.size(), "Unexpected non-primary command count");
        for (std::size_t I = 0; I < Events.size(); ++I)
            Require(Events[I].Kind == WithoutPrimary[I].Kind && Events[I].Value == WithoutPrimary[I].Value,
                    "Non-primary replay stage ordering differs");
        for (std::uint32_t Candidates = 1; Candidates <= PathReplayMaximumInitialCandidates; ++Candidates)
        {
            auto CandidateExtent = Extent;
            CandidateExtent.Sampling[1] = Candidates;
            Events.clear();
            RecordPathReplay(Commands, Access, Buffers, CandidateExtent);
            Require(!Events.empty() && Pushed.Sampling[1] == Candidates, "Candidate count was not recorded");
        }
        auto ReconstructionAccess = Access;
        ReconstructionAccess.GatherReconstruction = Handle<VkPipeline>(7);
        ReconstructionAccess.FinalizeReconstruction = Handle<VkPipeline>(8);
        auto ReconstructionExtent = Extent; ReconstructionExtent.Primary[3] = PathReplayReconstruct;
        auto ReconstructionBuffers = Buffers;
        ReconstructionBuffers.CounterWords = PathReplayReconstructionCounterWords;
        ReconstructionBuffers.OutputColourRecords = PathReplayRequiredReconstructedOutputRecords(65);
        Events.clear();
        RecordPathReplay(Commands, ReconstructionAccess, ReconstructionBuffers, ReconstructionExtent);
        const std::vector<Event> WithReconstruction{{'D',1}, {'P',128}, {'B',5}, {'X',2}, {'M',1},
            {'B',7}, {'X',2}, {'R',1}, {'B',6}, {'X',2}, {'M',1}, {'B',8}, {'X',2}};
        Require(Events.size() == WithReconstruction.size(), "Unexpected reconstruction command count");
        for (std::size_t I = 0; I < Events.size(); ++I)
            Require(Events[I].Kind == WithReconstruction[I].Kind && Events[I].Value == WithReconstruction[I].Value,
                    "Reconstruction command order or write-after-read barrier differs");
        auto CameraAccess = ReconstructionAccess;
        CameraAccess.Primary = Handle<VkPipeline>(4);
        auto CameraExtent = ReconstructionExtent;
        CameraExtent.Primary[0] = 1;
        CameraExtent.Primary[3] |= PathReplayDiffuseCameraReprojection;
        Events.clear();
        RecordPathReplay(Commands, CameraAccess, ReconstructionBuffers, CameraExtent);
        Require(Pushed.Primary[3] == CameraExtent.Primary[3] && Events.size() == WithReconstruction.size() + 3,
                "Diffuse camera reprojection was not recorded with primary rays");
        auto InvalidCamera = CameraExtent;
        InvalidCamera.Primary[3] = PathReplayDiffuseCameraReprojection;
        RequireRejection([&] { RecordPathReplay(Commands, CameraAccess, ReconstructionBuffers, InvalidCamera); });
        InvalidCamera = CameraExtent; InvalidCamera.Primary[0] = 0;
        RequireRejection([&] { RecordPathReplay(Commands, CameraAccess, ReconstructionBuffers, InvalidCamera); });
        InvalidCamera = CameraExtent; InvalidCamera.Primary[3] |= PathReplayRoughOpaque;
        RequireRejection([&] { RecordPathReplay(Commands, CameraAccess, ReconstructionBuffers, InvalidCamera); });
        InvalidCamera = CameraExtent; InvalidCamera.Primary[3] |= PathReplayTexturedDiffuse;
        RequireRejection([&] { RecordPathReplay(Commands, CameraAccess, ReconstructionBuffers, InvalidCamera); });

        auto SampleAccess = Access;
        SampleAccess.Primary = Handle<VkPipeline>(4);
        SampleAccess.GatherSampleHistory = Handle<VkPipeline>(9);
        SampleAccess.FinalizeSampleHistory = Handle<VkPipeline>(10);
        auto SampleExtent = Extent;
        SampleExtent.Primary[0] = 1;
        SampleExtent.Primary[3] = PathReplaySampleHistory | PathReplayResetSampleHistory;
        SampleExtent.Reconstruction[0] = SampleExtent.Reconstruction[1] = 0;
        auto SampleBuffers = Buffers;
        SampleBuffers.OutputColourRecords = PathReplayRequiredSampleHistoryOutputRecords(65);
        SampleBuffers.CounterWords = PathReplayReconstructionCounterWords;
        Events.clear();
        RecordPathReplay(Commands, SampleAccess, SampleBuffers, SampleExtent);
        const std::vector<Event> WithSampleHistory{{'D',1}, {'P',128}, {'B',4}, {'X',2}, {'M',1},
            {'B',5}, {'X',2}, {'M',1}, {'B',9}, {'X',2}, {'R',1}, {'B',6}, {'X',2}, {'M',1},
            {'B',10}, {'X',2}};
        Require(Events.size() == WithSampleHistory.size(), "Unexpected sample-history command count");
        for (std::size_t I = 0; I < Events.size(); ++I)
            Require(Events[I].Kind == WithSampleHistory[I].Kind && Events[I].Value == WithSampleHistory[I].Value,
                    "Sample-history command order or write-after-read barrier differs");
        auto SeedExtent = SampleExtent;
        SeedExtent.Primary[3] |= PathReplaySeedReservoir;
        SeedExtent.Reconstruction[1] = 1;
        Events.clear();
        RecordPathReplay(Commands, SampleAccess, SampleBuffers, SeedExtent);
        const std::vector<Event> WithSeedReservoir{{'D',1}, {'P',128}, {'B',4}, {'X',2}, {'M',1},
            {'B',9}, {'X',2}, {'R',1}, {'B',5}, {'X',2}, {'M',1}, {'B',6}, {'X',2}, {'M',1},
            {'B',10}, {'X',2}};
        Require(Events.size() == WithSeedReservoir.size(), "Unexpected seed-reservoir command count");
        for (std::size_t I = 0; I < Events.size(); ++I)
            Require(Events[I].Kind == WithSeedReservoir[I].Kind && Events[I].Value == WithSeedReservoir[I].Value,
                    "Seed-reservoir gather must precede generation with a read/write barrier");
        auto InvalidSeed = SeedExtent;
        InvalidSeed.Primary[3] = PathReplaySeedReservoir;
        RequireRejection([&] { RecordPathReplay(Commands, SampleAccess, SampleBuffers, InvalidSeed); });
        InvalidSeed = SeedExtent; InvalidSeed.Reconstruction[0] = 1;
        RequireRejection([&] { RecordPathReplay(Commands, SampleAccess, SampleBuffers, InvalidSeed); });
        InvalidSeed = SeedExtent; InvalidSeed.Sampling[1] = 2;
        RequireRejection([&] { RecordPathReplay(Commands, SampleAccess, SampleBuffers, InvalidSeed); });
        auto CompactExtent = SeedExtent;
        CompactExtent.Primary[3] |= PathReplayCompactStorage;
        auto CompactBuffers = SampleBuffers;
        CompactBuffers.OutputColourRecords = PathReplayRequiredCompactOutputRecords(65);
        Events.clear();
        RecordPathReplay(Commands, SampleAccess, CompactBuffers, CompactExtent);
        Require(Events.size() == WithSeedReservoir.size(), "Compact addressing changed dispatch count");
        for (std::size_t I = 0; I < Events.size(); ++I)
            Require(Events[I].Kind == WithSeedReservoir[I].Kind && Events[I].Value == WithSeedReservoir[I].Value,
                    "Compact addressing changed dependency ordering");
        auto InvalidCompact = CompactExtent;
        InvalidCompact.Primary[3] = PathReplayCompactStorage;
        RequireRejection([&] { RecordPathReplay(Commands, SampleAccess, CompactBuffers, InvalidCompact); });
        auto SmallCompact = CompactBuffers; --SmallCompact.OutputColourRecords;
        RequireRejection([&] { RecordPathReplay(Commands, SampleAccess, SmallCompact, CompactExtent); });
        InvalidCompact = CompactExtent; InvalidCompact.Primary[3] &= ~PathReplayCompactStorage;
        RequireRejection([&] { RecordPathReplay(Commands, SampleAccess, CompactBuffers, InvalidCompact); });
        InvalidCompact = CompactExtent;
        InvalidCompact.Dimensions[0] = InvalidCompact.History[0] = PathReplayMaxCompactReceivers + 1;
        InvalidCompact.History[1] = 1;
        auto LargeCompact = CompactBuffers;
        LargeCompact.ReceiverRecords = PathReplayMaxCompactReceivers + 1;
        LargeCompact.OutputColourRecords = UINT64_MAX;
        auto WideCompact = SampleAccess; WideCompact.MaxGroupCountX = UINT32_MAX;
        RequireRejection([&] { RecordPathReplay(Commands, WideCompact, LargeCompact, InvalidCompact); });
        InvalidCompact.Dimensions[0] = InvalidCompact.History[0] = PathReplayMaxCompactReceivers;
        LargeCompact.OutputColourRecords = PathReplayRequiredCompactOutputRecords(PathReplayMaxCompactReceivers);
        Events.clear();
        RecordPathReplay(Commands, WideCompact, LargeCompact, InvalidCompact);
        Require(!Events.empty(), "Largest addressable compact span was rejected");
        auto InvalidSample = SampleExtent;
        InvalidSample.Primary[0] = 0;
        RequireRejection([&] { RecordPathReplay(Commands, SampleAccess, SampleBuffers, InvalidSample); });
        InvalidSample = SampleExtent; InvalidSample.Primary[3] |= PathReplayRoughOpaque;
        RequireRejection([&] { RecordPathReplay(Commands, SampleAccess, SampleBuffers, InvalidSample); });
        InvalidSample = SampleExtent; InvalidSample.Primary[3] |= PathReplayTexturedDiffuse;
        RequireRejection([&] { RecordPathReplay(Commands, SampleAccess, SampleBuffers, InvalidSample); });
        InvalidSample = SampleExtent; InvalidSample.Primary[3] |= PathReplayReconstruct;
        RequireRejection([&] { RecordPathReplay(Commands, SampleAccess, SampleBuffers, InvalidSample); });
        InvalidSample = SampleExtent; InvalidSample.Primary[3] |= PathReplayAuditDoubleCount | PathReplaySampleFacets;
        RequireRejection([&] { RecordPathReplay(Commands, SampleAccess, SampleBuffers, InvalidSample); });
        InvalidSample = SampleExtent; InvalidSample.Reconstruction[0] = 1;
        RequireRejection([&] { RecordPathReplay(Commands, SampleAccess, SampleBuffers, InvalidSample); });
        InvalidSample = SampleExtent; InvalidSample.Reconstruction[1] = 1;
        RequireRejection([&] { RecordPathReplay(Commands, SampleAccess, SampleBuffers, InvalidSample); });
        InvalidSample = SampleExtent; InvalidSample.Sampling[1] = 2;
        RequireRejection([&] { RecordPathReplay(Commands, SampleAccess, SampleBuffers, InvalidSample); });
        InvalidSample = SampleExtent; InvalidSample.Primary[3] = PathReplayResetSampleHistory;
        RequireRejection([&] { RecordPathReplay(Commands, SampleAccess, SampleBuffers, InvalidSample); });
        auto MissingSample = SampleAccess; MissingSample.GatherSampleHistory = VK_NULL_HANDLE;
        RequireRejection([&] { RecordPathReplay(Commands, MissingSample, SampleBuffers, SampleExtent); });
        MissingSample = SampleAccess; MissingSample.FinalizeSampleHistory = VK_NULL_HANDLE;
        RequireRejection([&] { RecordPathReplay(Commands, MissingSample, SampleBuffers, SampleExtent); });
        auto SmallSample = SampleBuffers; --SmallSample.OutputColourRecords;
        RequireRejection([&] { RecordPathReplay(Commands, SampleAccess, SmallSample, SampleExtent); });
        SmallSample = SampleBuffers; --SmallSample.CounterWords;
        RequireRejection([&] { RecordPathReplay(Commands, SampleAccess, SmallSample, SampleExtent); });
        InvalidSample = SampleExtent; InvalidSample.Dimensions[0] = PathReplayMaxSampleHistoryReceivers + 1;
        InvalidSample.History[0] = PathReplayMaxSampleHistoryReceivers + 1; InvalidSample.History[1] = 1;
        SmallSample = SampleBuffers; SmallSample.ReceiverRecords = PathReplayMaxSampleHistoryReceivers + 1;
        SmallSample.OutputColourRecords = UINT64_MAX;
        MissingSample = SampleAccess; MissingSample.MaxGroupCountX = UINT32_MAX;
        RequireRejection([&] { RecordPathReplay(Commands, MissingSample, SmallSample, InvalidSample); });
        InvalidSample.Dimensions[0] = InvalidSample.History[0] = PathReplayMaxSampleHistoryReceivers;
        Events.clear();
        RecordPathReplay(Commands, MissingSample, SmallSample, InvalidSample);
        Require(!Events.empty(), "Largest sample-history output span was rejected");

        auto InvalidExtent = Extent; InvalidExtent.History[0] = 12;
        RequireRejection([&] { RecordPathReplay(Commands, Access, Buffers, InvalidExtent); });
        InvalidExtent = Extent; InvalidExtent.Dimensions[0] = 0;
        RequireRejection([&] { RecordPathReplay(Commands, Access, Buffers, InvalidExtent); });
        auto InvalidAccess = Access; InvalidAccess.MaxGroupCountX = 1;
        RequireRejection([&] { RecordPathReplay(Commands, InvalidAccess, Buffers, Extent); });
        InvalidAccess = Access; InvalidAccess.PipelineBarrier = nullptr;
        RequireRejection([&] { RecordPathReplay(Commands, InvalidAccess, Buffers, Extent); });
        InvalidAccess = Access; InvalidAccess.MaxPushConstantBytes = 127;
        RequireRejection([&] { RecordPathReplay(Commands, InvalidAccess, Buffers, Extent); });
        InvalidExtent = Extent; InvalidExtent.Primary[0] = 1;
        RequireRejection([&] { RecordPathReplay(Commands, Access, Buffers, InvalidExtent); });
        auto InvalidBuffers = Buffers; --InvalidBuffers.ReceiverRecords;
        RequireRejection([&] { RecordPathReplay(Commands, Access, InvalidBuffers, Extent); });
        InvalidBuffers = Buffers; --InvalidBuffers.OutputColourRecords;
        RequireRejection([&] { RecordPathReplay(Commands, Access, InvalidBuffers, Extent); });
        InvalidBuffers = Buffers; --InvalidBuffers.CounterWords;
        RequireRejection([&] { RecordPathReplay(Commands, Access, InvalidBuffers, Extent); });
        InvalidExtent = Extent; InvalidExtent.Primary[2] = 9;
        RequireRejection([&] { RecordPathReplay(Commands, Access, Buffers, InvalidExtent); });
        InvalidExtent = Extent; InvalidExtent.Sampling[1] = PathReplayMaximumInitialCandidates + 1;
        RequireRejection([&] { RecordPathReplay(Commands, Access, Buffers, InvalidExtent); });
        InvalidExtent = Extent; InvalidExtent.Sampling[3] = 0;
        RequireRejection([&] { RecordPathReplay(Commands, Access, Buffers, InvalidExtent); });
        InvalidExtent = Extent; InvalidExtent.Maximum[0] = std::numeric_limits<float>::quiet_NaN();
        RequireRejection([&] { RecordPathReplay(Commands, Access, Buffers, InvalidExtent); });
        InvalidExtent = Extent; InvalidExtent.Maximum[3] = -0.0f;
        RequireRejection([&] { RecordPathReplay(Commands, Access, Buffers, InvalidExtent); });
        InvalidExtent = Extent; InvalidExtent.Minimum[3] = -0.0f;
        RequireRejection([&] { RecordPathReplay(Commands, Access, Buffers, InvalidExtent); });
        InvalidExtent = Extent; InvalidExtent.Primary[3] = 4096;
        RequireRejection([&] { RecordPathReplay(Commands, Access, Buffers, InvalidExtent); });
        InvalidExtent = Extent; InvalidExtent.Primary[3] = 16384;
        RequireRejection([&] { RecordPathReplay(Commands, Access, Buffers, InvalidExtent); });
        // Textured secondary rays need the camera even when the primary stage is disabled.
        InvalidExtent = Extent; InvalidExtent.Primary[3] = PathReplayTexturedDiffuse;
        InvalidBuffers = Buffers; InvalidBuffers.AngleFloats = PathReplayCameraFloats - 1;
        RequireRejection([&] { RecordPathReplay(Commands, Access, InvalidBuffers, InvalidExtent); });
        InvalidExtent = Extent; InvalidExtent.Sampling[2] = 1;
        RequireRejection([&] { RecordPathReplay(Commands, Access, Buffers, InvalidExtent); });
        InvalidExtent = Extent; InvalidExtent.Dimensions[0] = PathReplayMaxReceivers + 1;
        InvalidExtent.History[0] = PathReplayMaxReceivers + 1; InvalidExtent.History[1] = 1;
        InvalidBuffers = Buffers; InvalidBuffers.ReceiverRecords = PathReplayMaxReceivers + 1;
        InvalidBuffers.OutputColourRecords = UINT64_MAX;
        InvalidAccess = Access; InvalidAccess.MaxGroupCountX = UINT32_MAX;
        RequireRejection([&] { RecordPathReplay(Commands, InvalidAccess, InvalidBuffers, InvalidExtent); });
        InvalidExtent.Dimensions[0] = InvalidExtent.History[0] = PathReplayMaxReceivers;
        Events.clear();
        RecordPathReplay(Commands, InvalidAccess, InvalidBuffers, InvalidExtent);
        Require(!Events.empty(), "Largest uint-indexed output span was rejected");
        InvalidExtent = Extent; InvalidExtent.Primary[0] = 1; InvalidExtent.Primary[1] = UINT32_MAX - 20u;
        InvalidAccess = Access; InvalidAccess.Primary = Handle<VkPipeline>(4);
        InvalidBuffers = Buffers; InvalidBuffers.AngleFloats = UINT64_MAX;
        RequireRejection([&] { RecordPathReplay(Commands, InvalidAccess, InvalidBuffers, InvalidExtent); });
        InvalidExtent = Extent; InvalidExtent.Primary[3] = 128 | 8192; InvalidExtent.Sampling[2] = 2;
        InvalidExtent.Grid[3] = 1; InvalidExtent.Reconstruction[2] = 2;
        const std::uint32_t Offset = 44;
        std::memcpy(&InvalidExtent.Maximum[1], &Offset, sizeof(Offset));
        InvalidBuffers = Buffers; InvalidBuffers.EnclosureRecords = 1; InvalidBuffers.FacetRecords = 2;
        RequireRejection([&] { RecordPathReplay(Commands, Access, InvalidBuffers, InvalidExtent); });
        InvalidBuffers.AngleFloats = 53; // 44 + header + two four-float entries.
        Events.clear();
        RecordPathReplay(Commands, Access, InvalidBuffers, InvalidExtent);
        Require(!Events.empty() && CallbackErrors == 0, "Valid weighted source range was rejected");
        const std::uint32_t OverflowOffset = UINT32_MAX - 2u;
        std::memcpy(&InvalidExtent.Maximum[1], &OverflowOffset, sizeof(OverflowOffset));
        InvalidBuffers.AngleFloats = UINT64_MAX;
        RequireRejection([&] { RecordPathReplay(Commands, Access, InvalidBuffers, InvalidExtent); });
        RequireRejection([&] { RecordPathReplay(Commands, Access, ReconstructionBuffers, ReconstructionExtent); });
        InvalidExtent = Extent; InvalidExtent.Primary[3] = PathReplayResetReconstruction;
        RequireRejection([&] { RecordPathReplay(Commands, ReconstructionAccess, ReconstructionBuffers, InvalidExtent); });
        InvalidBuffers = ReconstructionBuffers; --InvalidBuffers.OutputColourRecords;
        RequireRejection([&] { RecordPathReplay(Commands, ReconstructionAccess, InvalidBuffers, ReconstructionExtent); });
        InvalidBuffers = ReconstructionBuffers; --InvalidBuffers.CounterWords;
        RequireRejection([&] { RecordPathReplay(Commands, ReconstructionAccess, InvalidBuffers, ReconstructionExtent); });
        InvalidExtent = ReconstructionExtent; InvalidExtent.Dimensions[0] = PathReplayMaxReconstructedReceivers + 1;
        InvalidExtent.History[0] = PathReplayMaxReconstructedReceivers + 1; InvalidExtent.History[1] = 1;
        InvalidBuffers = ReconstructionBuffers; InvalidBuffers.ReceiverRecords = PathReplayMaxReconstructedReceivers + 1;
        InvalidBuffers.OutputColourRecords = UINT64_MAX;
        InvalidAccess = ReconstructionAccess; InvalidAccess.MaxGroupCountX = UINT32_MAX;
        RequireRejection([&] { RecordPathReplay(Commands, InvalidAccess, InvalidBuffers, InvalidExtent); });
        std::printf("REPLAY_RECORDERS primary_and_input=1 barriers=3 padded_dispatch=1 push128=1 "
                    "invalid_before_recording=32 candidates_0_to_4=1 weighted_span=1 uint_index_limit=1 "
                    "secondary_camera=1 reconstruction_order_and_WAR=1 diffuse_camera_guards=1 "
                    "sample_history_order_and_WAR=1 sample_history_rejections=14 sample_uint_limit=1\n");
        return 0;
    }
    catch (const std::exception &Error)
    {
        std::fprintf(stderr, "FAIL %s\n", Error.what());
        return 1;
    }
}
