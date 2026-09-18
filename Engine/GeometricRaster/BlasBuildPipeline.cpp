//============================================================================================================================================
//                                              BLASBUILDPIPELINE.CPP — D9b
//============================================================================================================================================
// 🧩 See the header. The one rule this file exists to enforce: the dispatch sequence is NOT written here, it is READ from
//    `BuildBlasDispatchPlan` / `BuildBlasRefitPlan` (BlasDevicePayload.cpp), which §⑨i checks against the tree the CPU
//    mirror actually builds. If the plan and the kernels disagree about the stage order, the gate catches it there — this
//    file only turns each plan entry into a dispatch and puts a barrier between them.
#include "BlasBuildPipeline.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>

namespace Frontier
{
namespace
{
    constexpr uint32_t kRefitBindingCount = 5u;
    constexpr uint32_t kBuildBindingCount = 8u;

    bool ReadFileBytes(const std::string& Path, std::vector<char>& Out)
    {
        std::ifstream Stream(Path, std::ios::binary | std::ios::ate);
        if (!Stream) return false;
        const std::streamoff Size = Stream.tellg();
        if (Size <= 0) return false;
        Out.resize(static_cast<size_t>(Size));
        Stream.seekg(0);
        Stream.read(Out.data(), Size);
        return Stream.good();
    }

    // The two SPIR-V blobs, by conventional location. The runner's --shaders flag is what a machine that built them
    //    somewhere else passes; the rest are the places this repository's own tools put them.
    bool LoadSpirv(const std::string& ShaderDir, const char* Name, std::vector<char>& Out)
    {
        const char* Roots[] = { "", "Engine/Shaders/", "Shaders/", "./" };
        if (!ShaderDir.empty())
        {
            std::string Path = ShaderDir;
            if (Path.back() != '/') Path += '/';
            if (ReadFileBytes(Path + Name, Out)) return true;
        }
        for (const char* Root : Roots)
        {
            if (ReadFileBytes(std::string(Root) + Name, Out)) return true;
        }
        return false;
    }

    void ComputeBarrier(VkCommandBuffer Cmd, const VulkanSourced& Api)
    {
        // One barrier shape for every stage edge: the next stage reads and writes what this one wrote, and all of it
        //    happens in the compute stage. A storage-buffer write is not visible to a later dispatch without it.
        VkMemoryBarrier Barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        Api.CmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               0u, 1u, &Barrier, 0u, nullptr, 0u, nullptr);
    }

    uint32_t FindMemoryType(const VulkanSourced& Api, VkPhysicalDevice Physical, uint32_t TypeBits, bool PreferCoherent,
                            bool& OutCoherent)
    {
        VkPhysicalDeviceMemoryProperties Properties{};
        Api.GetPhysicalDeviceMemoryProperties(Physical, &Properties);
        uint32_t Fallback = UINT32_MAX;
        for (uint32_t Index = 0u; Index < Properties.memoryTypeCount; ++Index)
        {
            if ((TypeBits & (1u << Index)) == 0u) continue;
            const VkMemoryPropertyFlags Flags = Properties.memoryTypes[Index].propertyFlags;
            if ((Flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0u) continue;
            const bool Coherent = (Flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0u;
            if (Coherent && PreferCoherent) { OutCoherent = true; return Index; }
            Fallback = Index;
        }
        OutCoherent = false;
        return Fallback;
    }

    bool CreateBuffer(const VulkanSourced& Api, VkPhysicalDevice Physical, VkDevice Device, uint64_t Bytes,
                      BlasDeviceJob::Buffer& Out, std::string& OutError)
    {
        VkBufferCreateInfo Info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        Info.size        = std::max<uint64_t>(Bytes, 4u);
        Info.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        Info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (Api.CreateBuffer(Device, &Info, nullptr, &Out.Handle) != VK_SUCCESS)
        {
            OutError = "vkCreateBuffer failed for " + std::to_string(Info.size) + " B";
            return false;
        }
        VkMemoryRequirements Requirements{};
        Api.GetBufferMemoryRequirements(Device, Out.Handle, &Requirements);
        bool Coherent = true;
        const uint32_t Type = FindMemoryType(Api, Physical, Requirements.memoryTypeBits, true, Coherent);
        if (Type == UINT32_MAX)
        {
            OutError = "no HOST_VISIBLE memory type for a storage buffer";
            return false;
        }
        VkMemoryAllocateInfo Allocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        Allocate.allocationSize  = Requirements.size;
        Allocate.memoryTypeIndex = Type;
        if (Api.AllocateMemory(Device, &Allocate, nullptr, &Out.Memory) != VK_SUCCESS ||
            Api.BindBufferMemory(Device, Out.Handle, Out.Memory, 0u) != VK_SUCCESS ||
            Api.MapMemory(Device, Out.Memory, 0u, Requirements.size, 0u, &Out.Mapped) != VK_SUCCESS)
        {
            OutError = "buffer memory allocation or mapping failed";
            return false;
        }
        Out.Bytes    = Info.size;
        Out.Coherent = Coherent;
        std::memset(Out.Mapped, 0, static_cast<size_t>(Requirements.size));
        return true;
    }

    void DestroyBuffer(const VulkanSourced& Api, VkDevice Device, BlasDeviceJob::Buffer& Buffer)
    {
        if (Buffer.Mapped) Api.UnmapMemory(Device, Buffer.Memory);
        if (Buffer.Handle) Api.DestroyBuffer(Device, Buffer.Handle, nullptr);
        if (Buffer.Memory) Api.FreeMemory(Device, Buffer.Memory, nullptr);
        Buffer = BlasDeviceJob::Buffer{};
    }

    bool Upload(const VulkanSourced& Api, VkDevice Device, BlasDeviceJob::Buffer& Buffer, const void* Source,
                uint64_t Bytes, std::string& OutError)
    {
        if (!Buffer.Mapped || Bytes > Buffer.Bytes)
        {
            OutError = "upload of " + std::to_string(Bytes) + " B does not fit the buffer (" +
                       std::to_string(Buffer.Bytes) + " B)";
            return false;
        }
        std::memcpy(Buffer.Mapped, Source, static_cast<size_t>(Bytes));
        if (!Buffer.Coherent)
        {
            VkMappedMemoryRange Range{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
            Range.memory = Buffer.Memory;
            Range.size   = VK_WHOLE_SIZE;
            Api.FlushMappedMemoryRanges(Device, 1u, &Range);
        }
        return true;
    }

    bool Download(VkDevice Device, const VulkanSourced& Api, BlasDeviceJob::Buffer& Buffer, void* Destination,
                  uint64_t Bytes)
    {
        if (!Buffer.Mapped || Bytes > Buffer.Bytes) return false;
        if (!Buffer.Coherent)
        {
            VkMappedMemoryRange Range{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
            Range.memory = Buffer.Memory;
            Range.size   = VK_WHOLE_SIZE;
            Api.InvalidateMappedMemoryRanges(Device, 1u, &Range);
        }
        std::memcpy(Destination, Buffer.Mapped, static_cast<size_t>(Bytes));
        return true;
    }

    VkDescriptorSetLayoutBinding Binding(uint32_t Index)
    {
        VkDescriptorSetLayoutBinding Out{};
        Out.binding         = Index;
        Out.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Out.descriptorCount = 1u;
        Out.stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        return Out;
    }

    VkWriteDescriptorSet Write(VkDescriptorSet Set, uint32_t BindingIndex, const VkDescriptorBufferInfo* Info)
    {
        VkWriteDescriptorSet Out{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        Out.dstSet          = Set;
        Out.dstBinding      = BindingIndex;
        Out.descriptorCount = 1u;
        Out.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Out.pBufferInfo     = Info;
        return Out;
    }
} // namespace

bool VulkanSourced::Complete() const noexcept
{
    return GetDeviceProcAddr && CreateShaderModule && DestroyShaderModule && CreateDescriptorSetLayout &&
           DestroyDescriptorSetLayout && CreatePipelineLayout && DestroyPipelineLayout && CreateComputePipelines &&
           DestroyPipeline && CreateDescriptorPool && DestroyDescriptorPool && AllocateDescriptorSets &&
           UpdateDescriptorSets && AllocateMemory && FreeMemory && CreateBuffer && DestroyBuffer &&
           GetBufferMemoryRequirements && BindBufferMemory && MapMemory && UnmapMemory && FlushMappedMemoryRanges &&
           InvalidateMappedMemoryRanges && CreateCommandPool && DestroyCommandPool && AllocateCommandBuffers &&
           ResetCommandBuffer && BeginCommandBuffer && EndCommandBuffer && CmdBindPipeline && CmdBindDescriptorSets &&
           CmdPushConstants && CmdDispatch && CmdPipelineBarrier && CmdWriteTimestamp && QueueSubmit &&
           QueueWaitIdle && CreateFence && DestroyFence && WaitForFences && ResetFences && CreateQueryPool &&
           DestroyQueryPool && GetQueryPoolResults && GetPhysicalDeviceProperties &&
           GetPhysicalDeviceMemoryProperties;
}

bool BlasBuildPipeline::Build(VkDevice InDevice, const VulkanSourced& InApi, const std::string& ShaderDir,
                              std::string& OutError) noexcept
{
    Device = InDevice;
    Api    = InApi;
    if (!Api.Complete()) { OutError = "the VulkanSourced table is incomplete"; return false; }

    // ── the build kernel's set: bindings 0-7, storage buffers, in the order the shader declares them ────────────────
    std::array<VkDescriptorSetLayoutBinding, kBuildBindingCount> BuildBindings{};
    for (uint32_t Index = 0u; Index < kBuildBindingCount; ++Index) BuildBindings[Index] = Binding(Index);
    VkDescriptorSetLayoutCreateInfo BuildSetInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    BuildSetInfo.bindingCount = kBuildBindingCount;
    BuildSetInfo.pBindings    = BuildBindings.data();
    if (Api.CreateDescriptorSetLayout(Device, &BuildSetInfo, nullptr, &BuildSetLayout) != VK_SUCCESS)
    {
        OutError = "vkCreateDescriptorSetLayout failed for the build kernel";
        return false;
    }

    // ── and the refit's: 0-4, the same five the shader declares ─────────────────────────────────────────────────────
    std::array<VkDescriptorSetLayoutBinding, kRefitBindingCount> RefitBindings{};
    for (uint32_t Index = 0u; Index < kRefitBindingCount; ++Index) RefitBindings[Index] = Binding(Index);
    VkDescriptorSetLayoutCreateInfo RefitSetInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    RefitSetInfo.bindingCount = kRefitBindingCount;
    RefitSetInfo.pBindings    = RefitBindings.data();
    if (Api.CreateDescriptorSetLayout(Device, &RefitSetInfo, nullptr, &RefitSetLayout) != VK_SUCCESS)
    {
        OutError = "vkCreateDescriptorSetLayout failed for the refit kernel";
        return false;
    }

    // ── pipeline layouts. The push ranges are the structs in BlasDevicePayload.h, which static_assert against the
    //    shaders' own blocks: 48 B for the build (four uints then two vec4), 16 B for the refit (four uints).
    VkPushConstantRange BuildRange{};
    BuildRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    BuildRange.offset     = 0u;
    BuildRange.size       = static_cast<uint32_t>(sizeof(BlasBuildConstants));
    VkPipelineLayoutCreateInfo BuildLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    BuildLayoutInfo.setLayoutCount         = 1u;
    BuildLayoutInfo.pSetLayouts            = &BuildSetLayout;
    BuildLayoutInfo.pushConstantRangeCount = 1u;
    BuildLayoutInfo.pPushConstantRanges    = &BuildRange;
    if (Api.CreatePipelineLayout(Device, &BuildLayoutInfo, nullptr, &BuildLayout) != VK_SUCCESS)
    {
        OutError = "vkCreatePipelineLayout failed for the build kernel";
        return false;
    }

    VkPushConstantRange RefitRange{};
    RefitRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    RefitRange.offset     = 0u;
    RefitRange.size       = static_cast<uint32_t>(sizeof(BlasRefitConstants));
    VkPipelineLayoutCreateInfo RefitLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    RefitLayoutInfo.setLayoutCount         = 1u;
    RefitLayoutInfo.pSetLayouts            = &RefitSetLayout;
    RefitLayoutInfo.pushConstantRangeCount = 1u;
    RefitLayoutInfo.pPushConstantRanges    = &RefitRange;
    if (Api.CreatePipelineLayout(Device, &RefitLayoutInfo, nullptr, &RefitLayout) != VK_SUCCESS)
    {
        OutError = "vkCreatePipelineLayout failed for the refit kernel";
        return false;
    }

    // ── deserialize the two SPIR-V blobs and make the pipelines ─────────────────────────────────────────────────────
    const auto MakePipeline = [&](const char* Name, VkPipelineLayout Layout, VkPipeline& OutPipeline) -> bool
    {
        std::vector<char> Code;
        if (!LoadSpirv(ShaderDir, Name, Code))
        {
            OutError = std::string("could not read ") + Name + " (build it with Tools/Build/CheckShaders.sh, or pass --shaders)";
            return false;
        }
        VkShaderModuleCreateInfo ModuleInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        ModuleInfo.codeSize = Code.size();
        ModuleInfo.pCode    = reinterpret_cast<const uint32_t*>(Code.data());
        VkShaderModule Module = VK_NULL_HANDLE;
        if (Api.CreateShaderModule(Device, &ModuleInfo, nullptr, &Module) != VK_SUCCESS)
        {
            OutError = std::string(Name) + " is not a valid SPIR-V module";
            return false;
        }
        VkComputePipelineCreateInfo Info{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        Info.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        Info.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        Info.stage.module = Module;
        Info.stage.pName  = "main";
        Info.layout       = Layout;
        const VkResult Result = Api.CreateComputePipelines(Device, VK_NULL_HANDLE, 1u, &Info, nullptr, &OutPipeline);
        Api.DestroyShaderModule(Device, Module, nullptr);
        if (Result != VK_SUCCESS)
        {
            OutError = std::string("vkCreateComputePipelines failed for ") + Name + " (VkResult " +
                       std::to_string(static_cast<int>(Result)) + ")";
            return false;
        }
        return true;
    };
    if (!MakePipeline("BlasBuild.spv", BuildLayout, BuildPipeline)) return false;
    if (!MakePipeline("BlasRefit.spv", RefitLayout, RefitPipeline)) return false;

    // ── the set pool: eight storage buffers for the build, five for the refit, one set apiece ───────────────────────
    const VkDescriptorPoolSize PoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kBuildBindingCount + kRefitBindingCount };
    VkDescriptorPoolCreateInfo PoolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    PoolInfo.maxSets       = 2u;
    PoolInfo.poolSizeCount = 1u;
    PoolInfo.pPoolSizes    = &PoolSize;
    if (Api.CreateDescriptorPool(Device, &PoolInfo, nullptr, &Pool) != VK_SUCCESS)
    {
        OutError = "vkCreateDescriptorPool failed";
        return false;
    }
    const VkDescriptorSetLayout Layouts[2] = { BuildSetLayout, RefitSetLayout };
    VkDescriptorSetAllocateInfo Allocate{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    Allocate.descriptorPool     = Pool;
    Allocate.descriptorSetCount = 2u;
    Allocate.pSetLayouts        = Layouts;
    VkDescriptorSet Sets[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    if (Api.AllocateDescriptorSets(Device, &Allocate, Sets) != VK_SUCCESS)
    {
        OutError = "vkAllocateDescriptorSets failed";
        return false;
    }
    BuildSet = Sets[0];
    RefitSet = Sets[1];
    return true;
}

void BlasBuildPipeline::Destroy(VkDevice InDevice) noexcept
{
    if (!Api.Complete()) return;
    const VkDevice Target = InDevice != VK_NULL_HANDLE ? InDevice : Device;
    if (Pool) Api.DestroyDescriptorPool(Target, Pool, nullptr);
    if (BuildPipeline) Api.DestroyPipeline(Target, BuildPipeline, nullptr);
    if (RefitPipeline) Api.DestroyPipeline(Target, RefitPipeline, nullptr);
    if (BuildLayout) Api.DestroyPipelineLayout(Target, BuildLayout, nullptr);
    if (RefitLayout) Api.DestroyPipelineLayout(Target, RefitLayout, nullptr);
    if (BuildSetLayout) Api.DestroyDescriptorSetLayout(Target, BuildSetLayout, nullptr);
    if (RefitSetLayout) Api.DestroyDescriptorSetLayout(Target, RefitSetLayout, nullptr);
    BuildPipeline = RefitPipeline = VK_NULL_HANDLE;
    BuildLayout = RefitLayout = VK_NULL_HANDLE;
    BuildSetLayout = RefitSetLayout = VK_NULL_HANDLE;
    Pool = VK_NULL_HANDLE;
    BuildSet = RefitSet = VK_NULL_HANDLE;
}

bool BlasBuildPipeline::CreateJob(VkPhysicalDevice Physical, VkDevice InDevice, const VulkanSourced& InApi,
                                  const std::vector<TriangleIndex>& Triangles, const float ObjectMin[3],
                                  const float ObjectMax[3], const std::vector<float>& MirrorNodes,
                                  const std::vector<float>& MirrorLeaves, const BlasBuildMirrorMetrics& MirrorMetrics,
                                  const std::vector<uint16_t>& MirrorLevels, BlasDeviceJob& Out,
                                  std::string& OutError) noexcept
{
    const VkDevice Target = InDevice != VK_NULL_HANDLE ? InDevice : Device;
    Api = InApi;
    Out = BlasDeviceJob{};
    if (Triangles.empty()) { OutError = "no triangles to build"; return false; }

    // ⚠️ The arena the plan over-dispatches is a CAP, not a reservation the build must use: the kernels guard on the
    //    level's node count, so a slot past it is a no-op lane. The caller passes the bound (payload.NodeSlotBound),
    //    which is why a 63 854-triangle level allocates 63 854 slots and the build fills 7 185 of them.
    Out.NodeSlots = BlasBuildPayload::NodeSlotBound(static_cast<uint32_t>(Triangles.size()));
    if (Out.NodeSlots == 0u) { OutError = "the arena bound came out empty"; return false; }

    Out.Triangles = Triangles;
    Out.ExpectedNodes.assign(MirrorNodes.begin(), MirrorNodes.end());
    Out.ExpectedLeaves.assign(MirrorLeaves.begin(), MirrorLeaves.end());
    Out.ExpectedNodeCount = MirrorMetrics.NodeCount;
    Out.ExpectedTriangles = MirrorMetrics.TriangleCount != 0u ? MirrorMetrics.TriangleCount
                                                              : static_cast<uint32_t>(MirrorLeaves.size() / 12u);
    Out.ExpectedLevels.assign(Out.NodeSlots, 0xFFFFFFFFu);
    for (size_t I = 0u; I < MirrorLevels.size() && I < Out.ExpectedLevels.size(); ++I)
        Out.ExpectedLevels[I] = MirrorLevels[I] == 0xFFFFu ? 0xFFFFFFFFu : MirrorLevels[I];
    Out.MirrorMetrics = MirrorMetrics;

    if (!BuildBlasBuildPayload(Triangles, ObjectMin, ObjectMax, Out.NodeSlots, Out.Payload))
    {
        OutError = "the payload refused the soup for " + std::to_string(Triangles.size()) + " triangles";
        return false;
    }

    const uint64_t NodeWords   = uint64_t(5u) * Out.NodeSlots * 4u;          // [vec4] kNodeBlocks per slot
    const uint64_t LeafWords   = uint64_t(3u) * Out.NodeSlots * 4u;          // [vec4] the leaf arena's upper bound
    const uint64_t SoupWords   = Out.Payload.Soup.size();
    // [uvec2] one entry per TRIANGLE. The arena bound IS the triangle count (payload.NodeSlotBound), so the ping and the
    //    pong are each NodeSlots entries — 8 B apiece.
    const uint64_t SortedBytes = uint64_t(Out.NodeSlots) * 8u;
    const uint64_t ScratchWords = BlasBuildScratchWords(Out.NodeSlots);
    const uint64_t BlockWords   = BlasBlockSumsWords(Out.NodeSlots);
    const uint64_t LevelWords   = Out.NodeSlots;

    const bool Ok =
        CreateBuffer(Api, Physical, Target, NodeWords * 4u, Out.Nodes, OutError) &&
        CreateBuffer(Api, Physical, Target, LeafWords * 4u, Out.Leaves, OutError) &&
        CreateBuffer(Api, Physical, Target, SoupWords * 4u, Out.Soup, OutError) &&
        CreateBuffer(Api, Physical, Target, SortedBytes, Out.SortedA, OutError) &&
        CreateBuffer(Api, Physical, Target, SortedBytes, Out.SortedB, OutError) &&
        CreateBuffer(Api, Physical, Target, ScratchWords * 4u, Out.Scratch, OutError) &&
        CreateBuffer(Api, Physical, Target, LevelWords * 4u, Out.Levels, OutError) &&
        CreateBuffer(Api, Physical, Target, BlockWords * 4u, Out.Blocks, OutError);
    if (!Ok) { DestroyJob(Target, Out); return false; }

    // The soup goes up as-is: it is already in the kernels' layout (3 vec4 per triangle) because the packer that wrote
    //    it is the one the shader text is pinned to. The rest of the job starts zeroed — the prepass fills the keys and
    //    the root, and every stage after it reads only what an earlier stage wrote.
    if (!Upload(Api, Target, Out.Soup, Out.Payload.Soup.data(), Out.Payload.Soup.size() * sizeof(float), OutError))
    {
        DestroyJob(Target, Out);
        return false;
    }
    return true;
}

void BlasBuildPipeline::DestroyJob(VkDevice InDevice, BlasDeviceJob& Job) noexcept
{
    const VkDevice Target = InDevice != VK_NULL_HANDLE ? InDevice : Device;
    DestroyBuffer(Api, Target, Job.Nodes);
    DestroyBuffer(Api, Target, Job.Leaves);
    DestroyBuffer(Api, Target, Job.Soup);
    DestroyBuffer(Api, Target, Job.SortedA);
    DestroyBuffer(Api, Target, Job.SortedB);
    DestroyBuffer(Api, Target, Job.Scratch);
    DestroyBuffer(Api, Target, Job.Levels);
    DestroyBuffer(Api, Target, Job.Blocks);
}

bool BlasBuildPipeline::UploadSoup(VkDevice InDevice, const BlasDeviceJob& Job, const std::vector<float>& Soup,
                                   std::string& OutError) noexcept
{
    const VkDevice Target = InDevice != VK_NULL_HANDLE ? InDevice : Device;
    if (Soup.size() != size_t(BlasSoupFloats(static_cast<uint32_t>(Job.Triangles.size()))))
    {
        OutError = "the deformed soup has " + std::to_string(Soup.size()) + " floats, the job's has " +
                   std::to_string(BlasSoupFloats(static_cast<uint32_t>(Job.Triangles.size()))) +
                   " — a refit deforms a topology, it does not change one";
        return false;
    }
    return Upload(Api, Target, const_cast<BlasDeviceJob::Buffer&>(Job.Soup), Soup.data(),
                  Soup.size() * sizeof(float), OutError);
}

bool BlasBuildPipeline::RecordBuild(VkCommandBuffer Cmd, const BlasDeviceJob& Job, bool BarrierBetweenStages,
                                    std::string& OutError) noexcept
{
    std::vector<BlasDispatch> Plan;
    if (!BuildBlasDispatchPlan(static_cast<uint32_t>(Job.Triangles.size()), Job.NodeSlots, Plan))
    {
        OutError = "the build plan refused this job";
        return false;
    }

    // The eight bindings, in the shader's order. Every job gets its own set: the sets are cheap and a shared one would
    //    silently couple two BLASes' buffers.
    const VkDescriptorBufferInfo Infos[kBuildBindingCount] = {
        { Job.Nodes.Handle,    0u, VK_WHOLE_SIZE },
        { Job.Leaves.Handle,   0u, VK_WHOLE_SIZE },
        { Job.Soup.Handle,     0u, VK_WHOLE_SIZE },
        { Job.SortedA.Handle,  0u, VK_WHOLE_SIZE },
        { Job.SortedB.Handle,  0u, VK_WHOLE_SIZE },
        { Job.Scratch.Handle,  0u, VK_WHOLE_SIZE },
        { Job.Levels.Handle,   0u, VK_WHOLE_SIZE },
        { Job.Blocks.Handle,   0u, VK_WHOLE_SIZE }
    };
    VkWriteDescriptorSet Writes[kBuildBindingCount];
    for (uint32_t Index = 0u; Index < kBuildBindingCount; ++Index) Writes[Index] = Write(BuildSet, Index, &Infos[Index]);
    Api.UpdateDescriptorSets(Device, kBuildBindingCount, Writes, 0u, nullptr);
    Api.CmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, BuildPipeline);
    Api.CmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, BuildLayout, 0u, 1u, &BuildSet, 0u, nullptr);

    BlasBuildConstants Constants{};
    Constants.TriangleCount = static_cast<uint32_t>(Job.Triangles.size());
    Constants.NodeCount     = Job.NodeSlots;
    for (int C = 0; C < 3; ++C)
    {
        Constants.ObjectMin[C]      = Job.Payload.ObjectMin[C];
        Constants.ObjectExtent[C]   = Job.Payload.ObjectExtent[C];
        Constants.ObjectMin[3]      = 0.0f;
        Constants.ObjectExtent[3]   = 0.0f;
    }

    for (const BlasDispatch& Entry : Plan)
    {
        Constants.Stage = Entry.Stage;
        Constants.Level = Entry.Level;
        Api.CmdPushConstants(Cmd, BuildLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0u,
                             static_cast<uint32_t>(sizeof(Constants)), &Constants);
        Api.CmdDispatch(Cmd, std::max(Entry.Groups, 1u), 1u, 1u);
        if (BarrierBetweenStages) ComputeBarrier(Cmd, Api);
    }
    BuildDispatchCount = static_cast<uint32_t>(Plan.size());
    return true;
}

bool BlasBuildPipeline::RecordRefit(VkCommandBuffer Cmd, const BlasDeviceJob& Job, uint32_t MaxLevel,
                                    bool BarrierBetweenStages, std::string& OutError) noexcept
{
    // The refit works on ONE BLAS inside buffers that hold many, so its bindings are the SAME buffers the build uses,
    //    seen through a placement record: the first entry of `BlasPlacements` describes this job. The offset arithmetic
    //    is in BlasRefit.slang, so what the host has to get right is the placement, not the indexing.
    BlasPlacement Placement{};
    Placement.NodeOffset     = 0u;
    Placement.LeafOffset     = 0u;
    Placement.PrimitiveCount = static_cast<uint32_t>(Job.Triangles.size());
    Placement.Reserved       = Job.MirrorMetrics.NodeCount;   // the refit's contract: Reserved carries the node count

    std::vector<uint32_t> Placements(sizeof(BlasPlacement) / sizeof(uint32_t), 0u);
    std::memcpy(Placements.data(), &Placement, sizeof(Placement));
    if (!Upload(Api, Device, const_cast<BlasDeviceJob::Buffer&>(Job.Blocks), Placements.data(),
                Placements.size() * sizeof(uint32_t), OutError))
    {
        OutError = "the refit placement upload failed (the job's block-sums buffer doubles as the placement slot)";
        return false;
    }

    const VkDescriptorBufferInfo Infos[kRefitBindingCount] = {
        { Job.Nodes.Handle, 0u, VK_WHOLE_SIZE },
        { Job.Leaves.Handle, 0u, VK_WHOLE_SIZE },
        { Job.Soup.Handle, 0u, VK_WHOLE_SIZE },
        { Job.Blocks.Handle, 0u, VK_WHOLE_SIZE },   // placements
        { Job.Levels.Handle, 0u, VK_WHOLE_SIZE }
    };
    VkWriteDescriptorSet Writes[kRefitBindingCount];
    for (uint32_t Index = 0u; Index < kRefitBindingCount; ++Index) Writes[Index] = Write(RefitSet, Index, &Infos[Index]);
    Api.UpdateDescriptorSets(Device, kRefitBindingCount, Writes, 0u, nullptr);
    Api.CmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, RefitPipeline);
    Api.CmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, RefitLayout, 0u, 1u, &RefitSet, 0u, nullptr);

    std::vector<BlasDispatch> Plan;
    if (!BuildBlasRefitPlan(Placement.PrimitiveCount, Job.MirrorMetrics.NodeCount, MaxLevel, Plan))
    {
        OutError = "the refit plan refused this job";
        return false;
    }
    BlasRefitConstants Constants{};
    Constants.BlasIndex = 0u;   // this job's placement is entry 0 of the array the binding points at
    for (const BlasDispatch& Entry : Plan)
    {
        Constants.Stage = Entry.Stage;
        Constants.Level = Entry.Level;
        Api.CmdPushConstants(Cmd, RefitLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0u,
                             static_cast<uint32_t>(sizeof(Constants)), &Constants);
        Api.CmdDispatch(Cmd, std::max(Entry.Groups, 1u), 1u, 1u);
        if (BarrierBetweenStages) ComputeBarrier(Cmd, Api);
    }
    RefitDispatchCount = static_cast<uint32_t>(Plan.size());
    return true;
}

bool BlasBuildPipeline::Verify(VkDevice InDevice, const VulkanSourced& InApi, BlasDeviceJob& Job, std::string& OutReport,
                               std::string& OutError) noexcept
{
    const VkDevice Target = InDevice != VK_NULL_HANDLE ? InDevice : Device;
    Api = InApi;

    // ── the node blob: every block the mirror wrote, byte for byte ──────────────────────────────────────────────────
    const size_t NodeBytes = Job.ExpectedNodes.size() * sizeof(float);
    std::vector<float> DeviceNodes(Job.ExpectedNodes.size(), 0.0f);
    if (!Download(Target, Api, Job.Nodes, DeviceNodes.data(), NodeBytes))
    {
        OutError = "could not read the node blob back";
        return false;
    }
    size_t FirstNodeDifference = NodeBytes / sizeof(float);
    size_t NodeDifferences = 0u;
    for (size_t I = 0u; I < DeviceNodes.size(); ++I)
    {
        if (std::memcmp(&DeviceNodes[I], &Job.ExpectedNodes[I], sizeof(float)) == 0) continue;
        if (FirstNodeDifference == NodeBytes / sizeof(float)) FirstNodeDifference = I;
        ++NodeDifferences;
    }

    // ── the leaf blob: the same, over the records the mirror wrote ──────────────────────────────────────────────────
    const size_t LeafBytes = Job.ExpectedLeaves.size() * sizeof(float);
    std::vector<float> DeviceLeaves(Job.ExpectedLeaves.size(), 0.0f);
    if (!Download(Target, Api, Job.Leaves, DeviceLeaves.data(), LeafBytes))
    {
        OutError = "could not read the leaf blob back";
        return false;
    }
    size_t FirstLeafDifference = LeafBytes / sizeof(float);
    size_t LeafDifferences = 0u;
    for (size_t I = 0u; I < DeviceLeaves.size(); ++I)
    {
        if (std::memcmp(&DeviceLeaves[I], &Job.ExpectedLeaves[I], sizeof(float)) == 0) continue;
        if (FirstLeafDifference == LeafBytes / sizeof(float)) FirstLeafDifference = I;
        ++LeafDifferences;
    }

    // ── the level table the kernel wrote, over the nodes it built ──────────────────────────────────────────────────
    std::vector<uint32_t> DeviceLevels(Job.NodeSlots, 0u);
    if (!Download(Target, Api, Job.Levels, DeviceLevels.data(), DeviceLevels.size() * sizeof(uint32_t)))
    {
        OutError = "could not read the level table back";
        return false;
    }
    size_t LevelDifferences = 0u;
    for (uint32_t Slot = 0u; Slot < Job.MirrorMetrics.NodeCount; ++Slot)
        if (DeviceLevels[Slot] != Job.ExpectedLevels[Slot]) ++LevelDifferences;

    // ── the scratch header's triangle total (stage 6 wrote it) ─────────────────────────────────────────────────────
    std::vector<uint32_t> Header(kBlasScratchHeader, 0u);
    if (!Download(Target, Api, Job.Scratch, Header.data(), Header.size() * sizeof(uint32_t)))
    {
        OutError = "could not read the scratch header back";
        return false;
    }
    const uint32_t DeviceTriangles = Header[4u];

    const bool NodeOk    = NodeDifferences == 0u;
    const bool LeafOk    = LeafDifferences == 0u;
    const bool LevelOk   = LevelDifferences == 0u;
    const bool TotalsOk  = DeviceTriangles == Job.ExpectedTriangles;
    const bool ArenaOk   = Job.ExpectedNodes.size() <= Job.NodeSlots * 20u && Job.ExpectedLeaves.size() <= Job.NodeSlots * 12u;
    OutReport = "device vs mirror: nodes " + std::to_string(NodeDifferences) + " differing floats" +
                (NodeOk ? " (identical)" : " first at float " + std::to_string(FirstNodeDifference)) +
                " · leaves " + std::to_string(LeafDifferences) + " differing floats" +
                (LeafOk ? " (identical)" : " first at float " + std::to_string(FirstLeafDifference)) +
                " · levels " + std::to_string(LevelDifferences) + " differing slots of " +
                std::to_string(Job.MirrorMetrics.NodeCount) + " · leaf triangles " + std::to_string(DeviceTriangles) +
                " vs " + std::to_string(Job.ExpectedTriangles) + (TotalsOk ? " (equal)" : " (MISMATCH)");
    return NodeOk && LeafOk && LevelOk && TotalsOk && ArenaOk;
}
} // namespace Frontier
