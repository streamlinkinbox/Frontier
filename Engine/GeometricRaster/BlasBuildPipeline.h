//============================================================================================================================================
//                                              BLASBUILDPIPELINE.H — D9b
//============================================================================================================================================
// 🧩 The Vulkan half of D9: the two compute pipelines (`BlasBuild.spv`, `BlasRefit.spv`), the buffers one BLAS build
//    needs, the recorded dispatch sequence, and the readback that checks the device's bytes against the CPU mirror's.
//
// 🔑 The point of this file is that the GPU path stops being a plan. Everything the kernels need — the payloads and the
//    dispatch order — was already written and gated (BlasDevicePayload.{h,cpp}: push blocks, soup, level table, plan);
//    what was missing was the plumbing that turns a plan entry into a vkCmdDispatch between two barriers, and the
//    validation that says the device produced what the mirror said it would.
//
// ⚠️ NO VULKAN LOADER IS LINKED. This translation unit takes function pointers through `VulkanSourced`, which the runner
//    fills from the loader it opens at runtime (Exhibits/Workbench/Traversal/BlasDeviceRun.cpp). That keeps the engine's
//    link line unchanged — the pipeline compiles and links with the Vulkan-Headers alone, which is what makes it
//    buildable in a sandbox that has no libvulkan.so, and what lets the run happen on any machine that has one.
#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

#include "BlasDevicePayload.h"
#include "SwapchainExchange.h"       // TriangleIndex
#include "InstanceAcceleration.h"    // BlasRecord

namespace Frontier
{
    // Every Vulkan entry point these two files call, filled in by the caller. Vulkan's own loader is the only thing that
    //    knows how to fill it, which is exactly why it is the caller's job and not a link-time dependency here.
    struct VulkanSourced
    {
        PFN_vkGetDeviceProcAddr         GetDeviceProcAddr         = nullptr;
        PFN_vkCreateShaderModule        CreateShaderModule        = nullptr;
        PFN_vkDestroyShaderModule       DestroyShaderModule       = nullptr;
        PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout = nullptr;
        PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout = nullptr;
        PFN_vkCreatePipelineLayout      CreatePipelineLayout      = nullptr;
        PFN_vkDestroyPipelineLayout     DestroyPipelineLayout     = nullptr;
        PFN_vkCreateComputePipelines    CreateComputePipelines    = nullptr;
        PFN_vkDestroyPipeline           DestroyPipeline           = nullptr;
        PFN_vkCreateDescriptorPool      CreateDescriptorPool      = nullptr;
        PFN_vkDestroyDescriptorPool     DestroyDescriptorPool     = nullptr;
        PFN_vkAllocateDescriptorSets    AllocateDescriptorSets    = nullptr;
        PFN_vkUpdateDescriptorSets      UpdateDescriptorSets      = nullptr;
        PFN_vkAllocateMemory            AllocateMemory            = nullptr;
        PFN_vkFreeMemory                FreeMemory                = nullptr;
        PFN_vkCreateBuffer              CreateBuffer              = nullptr;
        PFN_vkDestroyBuffer             DestroyBuffer             = nullptr;
        PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements = nullptr;
        PFN_vkBindBufferMemory          BindBufferMemory          = nullptr;
        PFN_vkMapMemory                 MapMemory                 = nullptr;
        PFN_vkUnmapMemory               UnmapMemory               = nullptr;
        PFN_vkFlushMappedMemoryRanges   FlushMappedMemoryRanges   = nullptr;
        PFN_vkInvalidateMappedMemoryRanges InvalidateMappedMemoryRanges = nullptr;
        PFN_vkCreateCommandPool         CreateCommandPool         = nullptr;
        PFN_vkDestroyCommandPool        DestroyCommandPool        = nullptr;
        PFN_vkAllocateCommandBuffers    AllocateCommandBuffers    = nullptr;
        PFN_vkResetCommandBuffer        ResetCommandBuffer        = nullptr;
        PFN_vkBeginCommandBuffer        BeginCommandBuffer        = nullptr;
        PFN_vkEndCommandBuffer          EndCommandBuffer          = nullptr;
        PFN_vkCmdBindPipeline           CmdBindPipeline           = nullptr;
        PFN_vkCmdBindDescriptorSets     CmdBindDescriptorSets     = nullptr;
        PFN_vkCmdPushConstants          CmdPushConstants          = nullptr;
        PFN_vkCmdDispatch               CmdDispatch               = nullptr;
        PFN_vkCmdPipelineBarrier        CmdPipelineBarrier        = nullptr;
        PFN_vkCmdWriteTimestamp         CmdWriteTimestamp         = nullptr;
        PFN_vkQueueSubmit               QueueSubmit               = nullptr;
        PFN_vkQueueWaitIdle             QueueWaitIdle             = nullptr;
        PFN_vkCreateFence               CreateFence               = nullptr;
        PFN_vkDestroyFence              DestroyFence              = nullptr;
        PFN_vkWaitForFences             WaitForFences              = nullptr;
        PFN_vkResetFences               ResetFences               = nullptr;
        PFN_vkCreateQueryPool           CreateQueryPool           = nullptr;
        PFN_vkDestroyQueryPool          DestroyQueryPool          = nullptr;
        PFN_vkGetQueryPoolResults       GetQueryPoolResults       = nullptr;
        PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties = nullptr;
        PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties = nullptr;

        [[nodiscard]] bool Complete() const noexcept;
    };

    // ── one BLAS' worth of device buffers, plus what the CPU mirror said the answer is ──────────────────────────────
    //    Every buffer is HOST_VISIBLE (a run tool, not a shipping path): the payloads are uploaded by memcpy and the
    //    results are read back the same way, with a flush/invalidate around the copy when the memory type is not
    //    coherent. `Expected*` is the mirror's own output for the same input — the comparison is the run's verdict.
    struct BlasDeviceJob
    {
        struct Buffer
        {
            VkBuffer       Handle = VK_NULL_HANDLE;
            VkDeviceMemory Memory = VK_NULL_HANDLE;
            void*          Mapped = nullptr;
            uint64_t       Bytes  = 0u;
            bool           Coherent = true;
        };

        Buffer Nodes, Leaves, Soup, SortedA, SortedB, Scratch, Levels, Blocks;
        uint32_t NodeSlots = 0u;   // [cnt] the arena the caller allocated (the plan's over-dispatch cap)

        // Inputs (the CPU side of the same build), kept so the run can re-check itself:
        std::vector<TriangleIndex> Triangles;
        std::vector<float>         ExpectedNodes;      // the mirror's node blob
        std::vector<float>         ExpectedLeaves;     // the mirror's leaf blob
        std::vector<uint32_t>      ExpectedLevels;     // the mirror's BFS level per node slot
        uint32_t                   ExpectedTriangles = 0u;   // [cnt] leaf records the mirror wrote
        uint32_t                   ExpectedNodeCount = 0u;   // [cnt] nodes the mirror made
        BlasBuildPayload           Payload;            // soup layout, bounds, derived sizes
        BlasBuildMirrorMetrics     MirrorMetrics;

        [[nodiscard]] bool Valid() const noexcept { return NodeSlots > 0u && Payload.SizesAgree() && !ExpectedNodes.empty(); }
    };

    class BlasBuildPipeline
    {
    public:
        // Creates both pipelines. `ShaderDir` is tried for the two .spv files, then a couple of conventional locations
        //    (Engine/Shaders, ./Shaders, next to the executable) — see the .cpp's LoadSpirv.
        [[nodiscard]] bool Build(VkDevice Device, const VulkanSourced& Api, const std::string& ShaderDir,
                                 std::string& OutError) noexcept;
        void Destroy(VkDevice Device) noexcept;

        // Allocates the job's buffers on a HOST_VISIBLE memory type of `PhysicalDevice` and fills them from the CPU side:
        //    the soup and the level-independent scratch header, plus the expected results for the verification.
        [[nodiscard]] bool CreateJob(VkPhysicalDevice PhysicalDevice, VkDevice Device, const VulkanSourced& Api,
                                     const std::vector<TriangleIndex>& Triangles, const float ObjectMin[3],
                                     const float ObjectMax[3], const std::vector<float>& MirrorNodes,
                                     const std::vector<float>& MirrorLeaves, const BlasBuildMirrorMetrics& MirrorMetrics,
                                     const std::vector<uint16_t>& MirrorLevels, BlasDeviceJob& Out,
                                     std::string& OutError) noexcept;
        void DestroyJob(VkDevice Device, BlasDeviceJob& Job) noexcept;

        // Replaces the job's soup with another one of the same triangle count — the refit path's input, which is the
        //    same level DEFORMED. Same layout, same size: a different count is a different topology and belongs in a
        //    build, not a refit (which is the contract `BlasRefit.slang` states at the top of the file).
        [[nodiscard]] bool UploadSoup(VkDevice Device, const BlasDeviceJob& Job, const std::vector<float>& Soup,
                                      std::string& OutError) noexcept;

        // Records ONE build: prepass, then per level {partition, count+scan, block scan, emit}, then the run stages —
        //    one vkCmdDispatch per plan entry, with a compute→compute barrier between stages. `BarrierBetweenStages`
        //    false is only for timing; correctness needs the barriers.
        [[nodiscard]] bool RecordBuild(VkCommandBuffer Cmd, const BlasDeviceJob& Job, bool BarrierBetweenStages,
                                       std::string& OutError) noexcept;

        // Records ONE refit of the job's BLAS: the leaf rewrite, then one dispatch per level deepest-first, reading the
        //    level table the build wrote (or the mirror's, when the caller filled it).
        [[nodiscard]] bool RecordRefit(VkCommandBuffer Cmd, const BlasDeviceJob& Job, uint32_t MaxLevel,
                                       bool BarrierBetweenStages, std::string& OutError) noexcept;

        // The verdict: does the device's blob equal the mirror's, block for block? Checks the node and leaf bytes the
        //    mirror produced, the level table over the mirror's node count, and the kernel's own triangle total.
        [[nodiscard]] bool Verify(VkDevice Device, const VulkanSourced& Api, BlasDeviceJob& Job, std::string& OutReport,
                                  std::string& OutError) noexcept;

        [[nodiscard]] uint32_t BuildDispatches() const noexcept { return BuildDispatchCount; }
        [[nodiscard]] uint32_t RefitDispatches() const noexcept { return RefitDispatchCount; }

    private:
        VulkanSourced Api;
        VkDevice      Device = VK_NULL_HANDLE;
        VkDescriptorSetLayout BuildSetLayout = VK_NULL_HANDLE;
        VkPipelineLayout      BuildLayout    = VK_NULL_HANDLE;
        VkPipeline            BuildPipeline  = VK_NULL_HANDLE;
        VkDescriptorSetLayout RefitSetLayout = VK_NULL_HANDLE;
        VkPipelineLayout      RefitLayout    = VK_NULL_HANDLE;
        VkPipeline            RefitPipeline  = VK_NULL_HANDLE;
        VkDescriptorPool      Pool           = VK_NULL_HANDLE;
        VkDescriptorSet       BuildSet       = VK_NULL_HANDLE;
        VkDescriptorSet       RefitSet       = VK_NULL_HANDLE;
        uint32_t              BuildDispatchCount = 0u;
        uint32_t              RefitDispatchCount = 0u;
    };
}
