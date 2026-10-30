/*============================================================================================================================================
                                                   FACETCDFSUBMISSIONVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Verify maintained command ordering,range rejection and fence-published cache residency before GPU corpus
// replay.

#include "FacetCdfSubmissionComparisons.h"
#include <functional>
#include <type_traits>

namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                    COMMAND CAPTURE
//------------------------------------------------------------------------------------------------------------------------

std::vector<int>      CdfCommands;
std::vector<uint32_t> CdfGroups;
FacetCdfVulkanAccess  CdfExpected;
VkResult              CdfFence         = VK_NOT_READY;
bool                  CdfCommandsValid = true;
uint32_t              CdfChecks        = 0;

template <class T> T CdfHandle(uintptr_t Token)
{
    if constexpr (std::is_pointer_v<T>)
        return reinterpret_cast<T>(Token);
    else
        return static_cast<T>(Token);
}
void ValidateCondition(bool Condition)
{
    ++CdfChecks;
    Require(Condition, "CDF submission validation differs");
}
VKAPI_ATTR void VKAPI_CALL CdfBind(VkCommandBuffer, VkPipelineBindPoint Point, VkPipeline Pipeline)
{
    CdfCommandsValid &= Point == VK_PIPELINE_BIND_POINT_COMPUTE;
    CdfCommands.push_back(Pipeline == CdfExpected.Preparation  ? 3
                          : Pipeline == CdfExpected.Generation ? 6
                          : Pipeline == CdfExpected.Resolution ? 7
                                                               : -1);
}
VKAPI_ATTR void VKAPI_CALL CdfDescriptors(
    VkCommandBuffer,
    VkPipelineBindPoint Point,
    VkPipelineLayout Layout,
    uint32_t First,
    uint32_t Count,
    const VkDescriptorSet *Descriptors,
    uint32_t Dynamic,
    const uint32_t *)
{
    CdfCommandsValid &= Point == VK_PIPELINE_BIND_POINT_COMPUTE && Layout == CdfExpected.Layout &&
                        First == 0 && Count == 1 && Descriptors[0] == CdfExpected.Descriptors && Dynamic == 0;
    CdfCommands.push_back(1);
}
VKAPI_ATTR void VKAPI_CALL CdfPush(
    VkCommandBuffer,
    VkPipelineLayout Layout,
    VkShaderStageFlags Shader,
    uint32_t Offset,
    uint32_t Bytes,
    const void *Extent)
{
    CdfCommandsValid &= Layout == CdfExpected.Layout && Shader == VK_SHADER_STAGE_COMPUTE_BIT &&
                        Offset == 0 && Bytes == 128 && Extent != nullptr;
    CdfCommands.push_back(2);
}
VKAPI_ATTR void VKAPI_CALL CdfDispatch(
    VkCommandBuffer,
    uint32_t X,
    uint32_t Y,
    uint32_t Z)
{
    CdfCommandsValid &= X > 0 && Y == 1 && Z == 1;
    CdfGroups.push_back(X);
    CdfCommands.push_back(4);
}
VKAPI_ATTR void VKAPI_CALL CdfDependency(
    VkCommandBuffer,
    VkPipelineStageFlags Source,
    VkPipelineStageFlags Destination,
    VkDependencyFlags Flags,
    uint32_t Count,
    const VkMemoryBarrier *Barrier,
    uint32_t Buffers,
    const VkBufferMemoryBarrier *,
    uint32_t Images,
    const VkImageMemoryBarrier *)
{
    CdfCommandsValid &= Source == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT && Destination == Source &&
                        Flags == 0 && Count == 1 && Buffers == 0 && Images == 0 &&
                        Barrier->sType == VK_STRUCTURE_TYPE_MEMORY_BARRIER &&
                        Barrier->srcAccessMask == VK_ACCESS_SHADER_WRITE_BIT &&
                        Barrier->dstAccessMask == (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    CdfCommands.push_back(5);
}
VKAPI_ATTR VkResult VKAPI_CALL CdfQuery(VkDevice Device, VkFence Fence)
{
    CdfCommandsValid &= Device == CdfExpected.Device && Fence == CdfExpected.Completion;
    return CdfFence;
}

void ValidateSubmissionUnits()
{
    static_assert(!std::is_copy_constructible_v<FacetCdfSubmission> &&
                  !std::is_move_constructible_v<FacetCdfSubmission>);
    FacetCdfSubmission   Residency;
    FacetCdfVulkanAccess Access;
    Access.Device                     = CdfHandle<VkDevice>(1);
    Access.Completion                 = CdfHandle<VkFence>(2);
    Access.Layout                     = CdfHandle<VkPipelineLayout>(3);
    Access.Descriptors                = CdfHandle<VkDescriptorSet>(4);
    Access.Preparation                = CdfHandle<VkPipeline>(5);
    Access.Generation                 = CdfHandle<VkPipeline>(6);
    Access.Resolution                 = CdfHandle<VkPipeline>(7);
    Access.BindPipeline               = CdfBind;
    Access.BindDescriptors            = CdfDescriptors;
    Access.PushConstants              = CdfPush;
    Access.Dispatch                   = CdfDispatch;
    Access.Dependency                 = CdfDependency;
    Access.QueryFence                 = CdfQuery;
    Access.MaxGroupCount              = 17;
    Access.MaxPushBytes               = 128;
    const auto               Commands = CdfHandle<VkCommandBuffer>(8);
    FacetCdfDescriptorRanges Ranges{1029, 64, 16, 84, 82320, 24};
    LightComputeFootprint    Extent;
    Extent.Dimensions[0] = 1029;
    Extent.Grid[3]       = 64;
    Extent.Sampling      = {17, 0, 16, 64};
    Extent.Primary[2]    = 1;
    FacetCdfRevision Revision;
    const auto       Reject = [&](const std::function<void()> &Operation) {
        const auto Count    = CdfCommands.size();
        const bool Prepared = Residency.Prepared(), Pending = Residency.Pending();
        bool       Rejected = false;
        try
        {
            Operation();
        }
        catch (const std::exception &)
        {
            Rejected = true;
        }
        ValidateCondition(Rejected && CdfCommands.size() == Count && Residency.Prepared() == Prepared &&
                          Residency.Pending() == Pending);
    };
    const auto Encode = [&](bool Cold) {
        CdfExpected = Access;
        CdfCommands.clear();
        CdfGroups.clear();
        ValidateCondition(Residency.Encode(Commands, Access, Ranges, Extent, Revision) == Cold);
        ValidateCondition(!Residency.Prepared() && Residency.Pending());
        ValidateCondition(CdfCommands == (Cold ? std::vector<int>{1, 2, 3, 4, 5, 6, 4, 5, 7, 4}
                                               : std::vector<int>{1, 2, 6, 4, 5, 7, 4}));
        ValidateCondition(CdfGroups ==
                          (Cold ? std::vector<uint32_t>{1, 17, 17} : std::vector<uint32_t>{17, 17}));
    };
    const auto Finish = [&]() {
        Residency.Acknowledge();
        ValidateCondition(!Residency.Resolve() && Residency.Pending() && !Residency.Prepared());
        CdfFence = VK_SUCCESS;
        ValidateCondition(Residency.Resolve() && !Residency.Pending() && Residency.Prepared());
        CdfFence = VK_NOT_READY;
    };
    ValidateCondition(FacetCdfRequiredCacheWords(65) == 280 &&
                      FacetCdfRequiredCacheWords(UINT32_MAX) > UINT32_MAX);
    ValidateCondition(FacetCdfRequiredOutputQuartets(UINT32_MAX) == uint64_t(UINT32_MAX) * 80);
    ValidateCondition(!Residency.Prepared() && !Residency.Pending());
    Reject([&] { Residency.Resolve(); });
    Reject([&] { Residency.Acknowledge(); });
    Reject([&] { Residency.Discard(); });
    Encode(true);
    Reject([&] { Residency.Encode(Commands, Access, Ranges, Extent, Revision); });
    Reject([&] { Residency.Invalidate(); });
    Reject([&] { Residency.Resolve(); });
    Finish();
    Extent.Sampling[0] ^= 0x31415927;
    Encode(false);
    Residency.Discard();
    ValidateCondition(!Residency.Prepared() && !Residency.Pending());
    Encode(true);
    Finish();
    Encode(false);
    Finish();
    ++Revision.Geometry;
    Encode(true);
    Finish();
    ++Revision.Profile;
    Encode(true);
    Finish();
    ++Revision.Allocation;
    Encode(true);
    Finish();
    Access.Descriptors = CdfHandle<VkDescriptorSet>(40);
    Encode(true);
    Finish();
    Access.Preparation = CdfHandle<VkPipeline>(41);
    Encode(true);
    Finish();
    Access.Generation = CdfHandle<VkPipeline>(42);
    Encode(true);
    Finish();
    Access.Resolution = CdfHandle<VkPipeline>(43);
    Encode(true);
    Finish();
    Access.Layout = CdfHandle<VkPipelineLayout>(44);
    Encode(true);
    Finish();
    Access.Device = CdfHandle<VkDevice>(45);
    Encode(true);
    Finish();
    ++Ranges.CacheWords;
    Encode(true);
    Finish();
    ++Ranges.Facets;
    Encode(true);
    Finish();
    --Extent.Sampling[2];
    Encode(true);
    Finish();
    Residency.Invalidate();
    Encode(true);
    Finish();
    Reject([&] { Residency.Encode(VK_NULL_HANDLE, Access, Ranges, Extent, Revision); });
    for (uint32_t Mutation = 0; Mutation < 16; ++Mutation)
    {
        auto Invalid = Access;
        if (Mutation == 0)
            Invalid.Device = VK_NULL_HANDLE;
        if (Mutation == 1)
            Invalid.Completion = VK_NULL_HANDLE;
        if (Mutation == 2)
            Invalid.Layout = VK_NULL_HANDLE;
        if (Mutation == 3)
            Invalid.Descriptors = VK_NULL_HANDLE;
        if (Mutation == 4)
            Invalid.Preparation = VK_NULL_HANDLE;
        if (Mutation == 5)
            Invalid.Generation = VK_NULL_HANDLE;
        if (Mutation == 6)
            Invalid.Resolution = VK_NULL_HANDLE;
        if (Mutation == 7)
            Invalid.BindPipeline = nullptr;
        if (Mutation == 8)
            Invalid.BindDescriptors = nullptr;
        if (Mutation == 9)
            Invalid.PushConstants = nullptr;
        if (Mutation == 10)
            Invalid.Dispatch = nullptr;
        if (Mutation == 11)
            Invalid.Dependency = nullptr;
        if (Mutation == 12)
            Invalid.QueryFence = nullptr;
        if (Mutation == 13)
            Invalid.MaxGroupCount = 16;
        if (Mutation == 14)
            Invalid.MaxPushBytes = 127;
        if (Mutation == 15)
            Invalid.MaxGroupCount = 0;
        Reject([&] { Residency.Encode(Commands, Invalid, Ranges, Extent, Revision); });
    }
    for (uint32_t Mutation = 0; Mutation < 6; ++Mutation)
    {
        auto Invalid = Ranges;
        if (Mutation == 0)
            Invalid.Receivers = 1028;
        if (Mutation == 1)
            Invalid.Enclosures = 63;
        if (Mutation == 2)
            Invalid.Facets = 14;
        if (Mutation == 3)
            Invalid.CacheWords = 79;
        if (Mutation == 4)
            Invalid.OutputQuartets = 82319;
        if (Mutation == 5)
            Invalid.CounterWords = 23;
        Reject([&] { Residency.Encode(Commands, Access, Invalid, Extent, Revision); });
    }
    for (uint32_t Mutation = 0; Mutation < 12; ++Mutation)
    {
        auto Invalid = Extent;
        if (Mutation == 0)
            Invalid.Dimensions[0] = 0;
        if (Mutation == 1)
            Invalid.Dimensions[0] = UINT32_MAX;
        if (Mutation == 2)
            Invalid.Sampling[2] = 0;
        if (Mutation == 3)
            Invalid.Sampling[2] = UINT32_MAX;
        if (Mutation == 4)
            Invalid.Grid[3] = 0;
        if (Mutation == 5)
            Invalid.Grid[3] = 65;
        if (Mutation == 6)
            Invalid.Primary[2] = 0;
        if (Mutation == 7)
            Invalid.Primary[2] = 9;
        if (Mutation == 8)
            Invalid.Sampling[3] = 0;
        if (Mutation == 9)
            Invalid.Sampling[3] = 65;
        if (Mutation == 10)
            Invalid.Reconstruction[0] = 1;
        if (Mutation == 11)
            Invalid.Reconstruction[1] = 1;
        Reject([&] { Residency.Encode(Commands, Access, Ranges, Invalid, Revision); });
    }
    for (uint32_t Mutation = 0; Mutation < 3; ++Mutation)
    {
        auto Invalid = Revision;
        if (Mutation == 0)
            Invalid.Geometry = 0;
        if (Mutation == 1)
            Invalid.Profile = 0;
        if (Mutation == 2)
            Invalid.Allocation = 0;
        Reject([&] { Residency.Encode(Commands, Access, Ranges, Extent, Invalid); });
    }
    {
        auto LargeAccess = Access;
        LargeAccess.MaxGroupCount = UINT32_MAX;
        auto LargeRanges = Ranges;
        LargeRanges.Receivers = LargeRanges.Facets = LargeRanges.CacheWords = LargeRanges.OutputQuartets = UINT64_MAX;
        auto LargeExtent = Extent;
        LargeExtent.Dimensions[0] = UINT32_MAX;
        Reject([&] { Residency.Encode(Commands, LargeAccess, LargeRanges, LargeExtent, Revision); });
        LargeExtent = Extent;
        LargeExtent.Sampling[2] = UINT32_MAX;
        Reject([&] { Residency.Encode(Commands, LargeAccess, LargeRanges, LargeExtent, Revision); });
    }
    CdfExpected = Access;
    CdfFence    = VK_SUCCESS;
    Reject([&] { Residency.Encode(Commands, Access, Ranges, Extent, Revision); });
    CdfFence = VK_ERROR_DEVICE_LOST;
    Reject([&] { Residency.Encode(Commands, Access, Ranges, Extent, Revision); });
    CdfFence = VK_NOT_READY;
    Encode(false);
    Residency.Acknowledge();
    Reject([&] { Residency.Discard(); });
    Reject([&] { Residency.Acknowledge(); });
    Reject([&] { Residency.Invalidate(); });
    Reject([&] { Residency.Encode(Commands, Access, Ranges, Extent, Revision); });
    CdfFence = VK_ERROR_DEVICE_LOST;
    Reject([&] { Residency.Resolve(); });
    CdfFence = VK_NOT_READY;
    ValidateCondition(!Residency.Resolve() && Residency.Pending() && !Residency.Prepared());
    CdfFence = VK_SUCCESS;
    ValidateCondition(Residency.Resolve() && Residency.Prepared());
    ValidateCondition(CdfCommandsValid);
    std::cout << "CDF_SUBMISSION_UNIT checks=" << CdfChecks << " pass=1\n";
}
} // namespace

int main(int Count, char **Arguments)
{
    try
    {
        if (Count == 2 && std::strcmp(Arguments[1], "--unit") == 0)
        {
            ValidateSubmissionUnits();
            return 0;
        }
        return ExecuteCdfCorpus(Count, Arguments);
    }
    catch (const std::exception &Error)
    {
        std::cerr << Error.what() << '\n';
        return 2;
    }
}
