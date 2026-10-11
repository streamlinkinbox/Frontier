//============================================================================================================================================
// 📦 Engine/DeviceExchange/SurfelGIStage.cpp — implementation. See SurfelGIStage.h for the contract.
//============================================================================================================================================
#include "SurfelGIStage.h"

#include <algorithm>
#include <array>
#include <vector>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>

namespace Frontier
{
namespace
{
    //--------------------------------------------------------------------------------------------------------------------
    // Buffer allocation — mirrors SwapchainExchange::AllocateBuffer so the two agree on memory-type selection.
    //--------------------------------------------------------------------------------------------------------------------
    bool AllocateBuffer(VkDevice Device, const VkPhysicalDeviceMemoryProperties& Mem, VkDeviceSize Bytes,
                        VkBufferUsageFlags Usage, uint32_t MemoryFlags, VkBuffer& OutBuffer, VkDeviceMemory& OutMemory) noexcept
    {
        VkBufferCreateInfo BufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        BufferInfo.size = Bytes; BufferInfo.usage = Usage; BufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(Device, &BufferInfo, nullptr, &OutBuffer) != VK_SUCCESS) return false;

        VkMemoryRequirements Req{};
        vkGetBufferMemoryRequirements(Device, OutBuffer, &Req);

        uint32_t TypeIndex = UINT32_MAX;
        for (uint32_t i = 0u; i < Mem.memoryTypeCount; ++i)
            if ((Req.memoryTypeBits & (1u << i)) && (Mem.memoryTypes[i].propertyFlags & MemoryFlags) == MemoryFlags) { TypeIndex = i; break; }
        if (TypeIndex == UINT32_MAX) return false;

        VkMemoryAllocateInfo AllocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        AllocInfo.allocationSize = Req.size; AllocInfo.memoryTypeIndex = TypeIndex;
        if (vkAllocateMemory(Device, &AllocInfo, nullptr, &OutMemory) != VK_SUCCESS) return false;
        return vkBindBufferMemory(Device, OutBuffer, OutMemory, 0) == VK_SUCCESS;
    }

    std::vector<uint32_t> LoadSpirv(const std::string& Path) noexcept
    {
        std::ifstream File(Path, std::ios::binary | std::ios::ate);
        if (!File.is_open()) { std::cerr << "[SurfelGIStage] cannot open SPIR-V: " << Path << "\n"; return {}; }
        const std::streamsize Bytes = File.tellg();
        if (Bytes < 4 || (Bytes % 4) != 0) { std::cerr << "[SurfelGIStage] malformed SPIR-V: " << Path << "\n"; return {}; }
        std::vector<uint32_t> Spirv(static_cast<size_t>(Bytes) / 4u);
        File.seekg(0); File.read(reinterpret_cast<char*>(Spirv.data()), Bytes);
        return Spirv;
    }

    VkShaderModule MakeModule(VkDevice Device, const std::string& Path) noexcept
    {
        const std::vector<uint32_t> Spirv = LoadSpirv(Path);
        if (Spirv.empty()) return VK_NULL_HANDLE;
        VkShaderModuleCreateInfo Info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        Info.codeSize = Spirv.size() * 4u; Info.pCode = Spirv.data();
        VkShaderModule Module = VK_NULL_HANDLE;
        (void)vkCreateShaderModule(Device, &Info, nullptr, &Module);
        return Module;
    }

    VkDescriptorSetLayoutBinding StorageBuffer(uint32_t b) noexcept
    { return { b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }; }
    VkDescriptorSetLayoutBinding StorageImage(uint32_t b) noexcept
    { return { b, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }; }
    VkDescriptorSetLayoutBinding Sampled(uint32_t b, uint32_t Count = 1u) noexcept
    { return { b, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, Count, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }; }
    // Matches CellHash() in SurfelIrradianceUpdate.slang / SurfelGIResolve.slang exactly.
    inline uint32_t CellHash(int cx, int cy, int cz, uint32_t hashSize) noexcept
    {
        const uint32_t h = static_cast<uint32_t>(cx + 1024) * 73856093u
                         ^ static_cast<uint32_t>(cy + 1024) * 19349663u
                         ^ static_cast<uint32_t>(cz + 1024) * 83492791u;
        return h % hashSize;
    }
}

//========================================================================================================================
// Bring-up / teardown
//========================================================================================================================
bool SurfelGIStage::Bring(const SurfelStageInit& Init) noexcept
{
    I = Init;
    if (I.Device == VK_NULL_HANDLE || I.CwbvhNodeBuffer == VK_NULL_HANDLE || I.CwbvhLeafBuffer == VK_NULL_HANDLE ||
        I.OutputImageView == VK_NULL_HANDLE || I.SurfaceImageView == VK_NULL_HANDLE || I.NormalImageView == VK_NULL_HANDLE ||
        I.TriangleBuffer == VK_NULL_HANDLE || I.MaterialBuffer == VK_NULL_HANDLE || I.InstanceBuffer == VK_NULL_HANDLE ||
        I.SlabBuffer == VK_NULL_HANDLE || I.VertexBuffer == VK_NULL_HANDLE || I.IndexBuffer == VK_NULL_HANDLE ||
        I.EnergyLutView == VK_NULL_HANDLE || I.SheenLutView == VK_NULL_HANDLE || I.GridCellSize <= 0.0f)
    {
        std::cerr << "[SurfelGIStage] deferred: scene traversal, visibility targets, scene buffers or shading "
                     "tables are not resident.\n";
        I = {};
        return false;
    }
    HostSurfels.reserve(I.MaxSurfels);
    HostGridHead.assign(I.GridHashSize, 0);
    HostGridNext.assign(I.MaxSurfels, 0);
    if (!CreateBuffers())   { std::cerr << "[SurfelGIStage] buffer allocation failed\n"; Destroy(); return false; }
    if (!CreatePipelines()) { std::cerr << "[SurfelGIStage] pipeline creation failed\n"; Destroy(); return false; }
    if (!WriteDescriptors()){ std::cerr << "[SurfelGIStage] descriptor write failed\n"; Destroy(); return false; }
    std::cerr << "[SurfelGIStage] up: max=" << I.MaxSurfels << " grid=" << I.GridHashSize
              << " cell=" << I.GridCellSize << " m\n";
    return true;
}

bool SurfelGIStage::CreateBuffers() noexcept
{
    const VkDeviceSize SurfelBytes = VkDeviceSize(I.MaxSurfels) * sizeof(GpuSurfel);
    const VkDeviceSize HeadBytes   = VkDeviceSize(I.GridHashSize) * sizeof(int32_t);
    const VkDeviceSize NextBytes   = VkDeviceSize(I.MaxSurfels) * sizeof(int32_t);
    const uint32_t DeviceLocal = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    const uint32_t HostVisible = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    const VkBufferUsageFlags Ssbo = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    const VkBufferUsageFlags Src  = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    return AllocateBuffer(I.Device, I.MemoryProperties, SurfelBytes, Ssbo, DeviceLocal, SurfelBuffer, SurfelMem)
        && AllocateBuffer(I.Device, I.MemoryProperties, HeadBytes,   Ssbo, DeviceLocal, HeadBuffer,   HeadMem)
        && AllocateBuffer(I.Device, I.MemoryProperties, NextBytes,   Ssbo, DeviceLocal, NextBuffer,   NextMem)
        && AllocateBuffer(I.Device, I.MemoryProperties, SurfelBytes, Src,  HostVisible, SurfelStage,  SurfelStageMem)
        && AllocateBuffer(I.Device, I.MemoryProperties, HeadBytes,   Src,  HostVisible, HeadStage,    HeadStageMem)
        && AllocateBuffer(I.Device, I.MemoryProperties, NextBytes,   Src,  HostVisible, NextStage,    NextStageMem);
}

bool SurfelGIStage::CreatePipelines() noexcept
{
    auto BuildLayout = [&](const std::vector<VkDescriptorSetLayoutBinding>& Bindings,
                           uint32_t PushBytes, VkDescriptorSetLayout& SetLayout, VkPipelineLayout& PipeLayout)
    {
        VkDescriptorSetLayoutCreateInfo LI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        LI.bindingCount = static_cast<uint32_t>(Bindings.size()); LI.pBindings = Bindings.data();
        if (vkCreateDescriptorSetLayout(I.Device, &LI, nullptr, &SetLayout) != VK_SUCCESS) return false;
        VkPushConstantRange Push{ VK_SHADER_STAGE_COMPUTE_BIT, 0u, PushBytes };
        VkPipelineLayoutCreateInfo PI{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        PI.setLayoutCount = 1u; PI.pSetLayouts = &SetLayout; PI.pushConstantRangeCount = 1u; PI.pPushConstantRanges = &Push;
        return vkCreatePipelineLayout(I.Device, &PI, nullptr, &PipeLayout) == VK_SUCCESS;
    };
    auto BuildPipeline = [&](const std::string& Spv, VkPipelineLayout PipeLayout, VkPipeline& Out)
    {
        VkShaderModule Module = MakeModule(I.Device, I.SpirvDirectory + "/" + Spv);
        if (Module == VK_NULL_HANDLE) return false;
        VkPipelineShaderStageCreateInfo Stage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        Stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; Stage.module = Module; Stage.pName = "main";
        VkComputePipelineCreateInfo CI{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        CI.stage = Stage; CI.layout = PipeLayout;
        const VkResult R = vkCreateComputePipelines(I.Device, VK_NULL_HANDLE, 1u, &CI, nullptr, &Out);
        vkDestroyShaderModule(I.Device, Module, nullptr);
        return R == VK_SUCCESS;
    };

    // Both shading passes carry the full scene now, so the pool has to cover it: 6 scene SSBOs each, the two
    //    shading LUTs each, and one variable-count bindless table each. The table is the expensive entry — it is
    //    sized by the device's slot capacity exactly as the ReSTIR set is.
    const uint32_t Slots = I.TextureCapacity;                      // 0 ⇒ no descriptor indexing; the table is 1 slot
    const uint32_t TableSize = Slots > 0u ? Slots : 1u;
    std::array<VkDescriptorPoolSize, 3> PoolSizes{{
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,        24u },                 // update 11 + commit 1 + resolve 11 (+slack)
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          4u },                 // resolve: output, surface, normal (+slack)
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2u * TableSize + 8u },// two LUTs + the sky cube + two tables
    }};
    VkDescriptorPoolCreateInfo PoolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    PoolInfo.maxSets       = 3u;
    PoolInfo.flags         = Slots > 0u ? VkDescriptorPoolCreateFlags(VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT) : 0u;
    PoolInfo.poolSizeCount = static_cast<uint32_t>(PoolSizes.size());
    PoolInfo.pPoolSizes    = PoolSizes.data();
    if (vkCreateDescriptorPool(I.Device, &PoolInfo, nullptr, &DescriptorPool) != VK_SUCCESS) return false;

    // The bindless table is the LAST binding in both shading sets (31), variable-count and partially bound, which
    //    is what VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT requires. Matching the kernel's set exactly
    //    means the same SPIR-V declaration compiles against either.
    auto BuildShadingLayout = [&](std::vector<VkDescriptorSetLayoutBinding> Bindings,
                                  VkDescriptorSetLayout& SetLayout, VkPipelineLayout& PipeLayout)
    {
        Bindings.push_back(Sampled(31u, TableSize));
        std::vector<VkDescriptorBindingFlags> Flags(Bindings.size(), 0u);
        if (Slots > 0u) Flags.back() = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
                                     | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT
                                     | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
        VkDescriptorSetLayoutBindingFlagsCreateInfo FlagsInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
        FlagsInfo.bindingCount = static_cast<uint32_t>(Flags.size());
        FlagsInfo.pBindingFlags = Flags.data();
        VkDescriptorSetLayoutCreateInfo LI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        LI.pNext = Slots > 0u ? &FlagsInfo : nullptr;
        LI.flags = Slots > 0u ? VkDescriptorSetLayoutCreateFlags(VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT) : 0u;
        LI.bindingCount = static_cast<uint32_t>(Bindings.size()); LI.pBindings = Bindings.data();
        if (vkCreateDescriptorSetLayout(I.Device, &LI, nullptr, &SetLayout) != VK_SUCCESS) return false;
        VkPushConstantRange Push{ VK_SHADER_STAGE_COMPUTE_BIT, 0u, 96u };
        VkPipelineLayoutCreateInfo PI{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        PI.setLayoutCount = 1u; PI.pSetLayouts = &SetLayout; PI.pushConstantRangeCount = 1u; PI.pPushConstantRanges = &Push;
        return vkCreatePipelineLayout(I.Device, &PI, nullptr, &PipeLayout) == VK_SUCCESS;
    };

    // push blocks: update/resolve = 6×vec4/uvec4 = 96 B; commit = one uvec4 = 16 B (see the shaders).
    // Update  — 0/1/2 surfel field · 3/4/5 tri/mat/inst · 8/9 CWBVH · 10/11/12 slab/vtx/idx · 13/14 LUTs · 31 table.
    if (!BuildShadingLayout({ StorageBuffer(0), StorageBuffer(1), StorageBuffer(2),
                              StorageBuffer(3), StorageBuffer(4), StorageBuffer(5),
                              StorageBuffer(8), StorageBuffer(9),
                              StorageBuffer(10), StorageBuffer(11), StorageBuffer(12),
                              Sampled(13), Sampled(14) },
                            UpdateLayout, UpdatePipeLayout)) return false;
    if (!BuildLayout({ StorageBuffer(0) }, 16u, CommitLayout, CommitPipeLayout)) return false;
    // Resolve — 0/1/2 images · 8/9 CWBVH · 10/11/12 surfel field · 13/14 LUTs · 15–20 scene · 31 table.
    //    3 (albedo) and 4 (material aux) are GONE: the slab is decoded from SurfaceImage.w instead of being
    //    re-encoded into an rgba8 and an rgba16f on the way past.
    if (!BuildShadingLayout({ StorageImage(0), StorageImage(1), StorageImage(2),
                              StorageBuffer(8), StorageBuffer(9),
                              StorageBuffer(10), StorageBuffer(11), StorageBuffer(12),
                              Sampled(13), Sampled(14),
                              StorageBuffer(15), StorageBuffer(16), StorageBuffer(17),
                              StorageBuffer(18), StorageBuffer(19), StorageBuffer(20) },
                            ResolveLayout, ResolvePipeLayout)) return false;

    return BuildPipeline("SurfelIrradianceUpdate.spv", UpdatePipeLayout,  UpdatePipeline)
        && BuildPipeline("SurfelCommit.spv",           CommitPipeLayout,  CommitPipeline)
        && BuildPipeline("SurfelGIResolve.spv",        ResolvePipeLayout, ResolvePipeline);
}

bool SurfelGIStage::WriteDescriptors() noexcept
{
    std::array<VkDescriptorSetLayout, 3> Layouts{ UpdateLayout, CommitLayout, ResolveLayout };
    std::array<VkDescriptorSet*, 3>      Sets{ &UpdateSet, &CommitSet, &ResolveSet };
    // The two shading sets end in a variable-count bindless table, so their allocation has to say how many slots
    //    it actually wants. The commit set (index 1) has no table and must not carry the chain.
    const uint32_t VariableCount = I.TextureCapacity > 0u ? I.TextureCapacity : 1u;
    VkDescriptorSetVariableDescriptorCountAllocateInfo VariableInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO };
    VariableInfo.descriptorSetCount = 1u; VariableInfo.pDescriptorCounts = &VariableCount;
    for (size_t i = 0; i < 3; ++i)
    {
        VkDescriptorSetAllocateInfo AI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        AI.pNext = (i != 1u && I.TextureCapacity > 0u) ? &VariableInfo : nullptr;
        AI.descriptorPool = DescriptorPool; AI.descriptorSetCount = 1u; AI.pSetLayouts = &Layouts[i];
        if (vkAllocateDescriptorSets(I.Device, &AI, Sets[i]) != VK_SUCCESS) return false;
    }

    auto Buf = [](VkBuffer b){ return VkDescriptorBufferInfo{ b, 0u, VK_WHOLE_SIZE }; };
    auto Img = [](VkImageView v){ return VkDescriptorImageInfo{ VK_NULL_HANDLE, v, VK_IMAGE_LAYOUT_GENERAL }; };
    const VkDescriptorBufferInfo Surfel = Buf(SurfelBuffer), Head = Buf(HeadBuffer), Next = Buf(NextBuffer);
    const VkDescriptorBufferInfo Nodes  = Buf(I.CwbvhNodeBuffer), Tris = Buf(I.CwbvhLeafBuffer);
    const VkDescriptorImageInfo  Out = Img(I.OutputImageView), Surf = Img(I.SurfaceImageView), Norm = Img(I.NormalImageView);
    // The scene the two shading passes decode — the same handles the ReSTIR kernel binds, borrowed, not copied.
    const VkDescriptorBufferInfo Tri   = Buf(I.TriangleBuffer), Mat = Buf(I.MaterialBuffer), Inst = Buf(I.InstanceBuffer);
    const VkDescriptorBufferInfo Slab  = Buf(I.SlabBuffer), Vtx = Buf(I.VertexBuffer), Idx = Buf(I.IndexBuffer);
    auto Tex = [&](VkImageView v){ return VkDescriptorImageInfo{ I.TableSampler, v, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }; };
    const VkDescriptorImageInfo  Energy = Tex(I.EnergyLutView), Sheen = Tex(I.SheenLutView);
    // The bindless table. Unused slots repeat slot 0 rather than staying null: PARTIALLY_BOUND permits null, but
    //    only where the shader provably never indexes it, and kMaterialTextureNone is the only guard there is.
    std::vector<VkDescriptorImageInfo> Table;
    if (I.TextureCapacity > 0u && I.TextureCount > 0u)
    {
        Table.resize(I.TextureCapacity);
        for (uint32_t Slot = 0u; Slot < I.TextureCapacity; ++Slot)
            Table[Slot] = VkDescriptorImageInfo{ I.TextureSampler,
                                                 I.TextureViews[Slot < I.TextureCount ? Slot : 0u],
                                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    }

    std::vector<VkWriteDescriptorSet> W;
    auto WB = [&](VkDescriptorSet s, uint32_t b, const VkDescriptorBufferInfo* i)
    { W.push_back({ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s, b, 0u, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, i, nullptr }); };
    auto WI = [&](VkDescriptorSet s, uint32_t b, const VkDescriptorImageInfo* i)
    { W.push_back({ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s, b, 0u, 1u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, i, nullptr, nullptr }); };
    auto WS = [&](VkDescriptorSet s, uint32_t b, const VkDescriptorImageInfo* i)
    { W.push_back({ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s, b, 0u, 1u, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, i, nullptr, nullptr }); };
    auto WTable = [&](VkDescriptorSet s)
    {
        if (Table.empty()) return;
        W.push_back({ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s, 31u, 0u,
                      static_cast<uint32_t>(Table.size()), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                      Table.data(), nullptr, nullptr });
    };

    // Update: b0/1/2 surfel field · b3/4/5 tri/mat/inst · b8/9 CWBVH · b10/11/12 slab/vtx/idx · b13/14 LUTs · b31 table
    WB(UpdateSet, 0, &Surfel); WB(UpdateSet, 1, &Head); WB(UpdateSet, 2, &Next);
    WB(UpdateSet, 3, &Tri); WB(UpdateSet, 4, &Mat); WB(UpdateSet, 5, &Inst);
    WB(UpdateSet, 8, &Nodes); WB(UpdateSet, 9, &Tris);
    WB(UpdateSet, 10, &Slab); WB(UpdateSet, 11, &Vtx); WB(UpdateSet, 12, &Idx);
    WS(UpdateSet, 13, &Energy); WS(UpdateSet, 14, &Sheen); WTable(UpdateSet);
    // Commit: b0 Surfels
    WB(CommitSet, 0, &Surfel);
    // Resolve: images 0/1/2 · b8/9 CWBVH · b10/11/12 surfel field · b13/14 LUTs · b15–20 scene · b31 table.
    //    The albedo and material-aux images are gone with the approximation that needed them.
    WI(ResolveSet, 0, &Out); WI(ResolveSet, 1, &Surf); WI(ResolveSet, 2, &Norm);
    WB(ResolveSet, 8, &Nodes); WB(ResolveSet, 9, &Tris);
    WB(ResolveSet, 10, &Surfel); WB(ResolveSet, 11, &Head); WB(ResolveSet, 12, &Next);
    WS(ResolveSet, 13, &Energy); WS(ResolveSet, 14, &Sheen);
    WB(ResolveSet, 15, &Tri); WB(ResolveSet, 16, &Mat); WB(ResolveSet, 17, &Inst);
    WB(ResolveSet, 18, &Slab); WB(ResolveSet, 19, &Vtx); WB(ResolveSet, 20, &Idx);
    WTable(ResolveSet);

    vkUpdateDescriptorSets(I.Device, static_cast<uint32_t>(W.size()), W.data(), 0u, nullptr);
    return true;
}

//========================================================================================================================
// Host-side field maintenance + hash-grid build
//========================================================================================================================
void SurfelGIStage::UpdateField(const std::vector<SurfaceSample>& Samples, const SurfelFrameParams& Params) noexcept
{
    if (!IsReady()) return;
    ++FrameCounter;
    const float cell = I.GridCellSize;

    // 1) The GPU owns age and irradiance after the first upload. Keeping the CPU mirror structural-only is
    // essential: uploading it again every frame would reset the GPU running mean to zero and defeat persistence.

    // 2) spawn: for each supplied surface sample, if no live surfel already covers it (within its radius on the
    //    tangent plane) allocate one — reusing a dead slot when available, else growing the pool up to MaxSurfels.
    uint32_t spawned = 0u, recycled = 0u;
    bool topologyChanged = false;
    auto radiusAt = [&](const float* /*p*/) { return cell; };   // one surfel per cell; caller scales cell to the scene
    for (const SurfaceSample& q : Samples)
    {
        const int cx = int(std::floor((q.Position[0] - Params.GridOrigin[0]) / cell));
        const int cy = int(std::floor((q.Position[1] - Params.GridOrigin[1]) / cell));
        const int cz = int(std::floor((q.Position[2] - Params.GridOrigin[2]) / cell));
        // covered? scan the cell chain we built last frame (cheap; empty on frame 0)
        bool covered = false;
        if (!HostGridHead.empty())
        {
            int idx = HostGridHead[CellHash(cx, cy, cz, I.GridHashSize)] - 1;
            for (int guard = 0; idx >= 0 && guard < 256; ++guard)
            {
                const GpuSurfel& s = HostSurfels[idx];
                const float d[3]{ q.Position[0]-s.PositionRadius[0], q.Position[1]-s.PositionRadius[1], q.Position[2]-s.PositionRadius[2] };
                const float dist = std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
                if (s.Albedo[3] > 0.5f && dist < s.PositionRadius[3]) { covered = true; break; }
                idx = HostGridNext[idx] - 1;
            }
        }
        if (covered) continue;

        GpuSurfel s{};
        const float r = radiusAt(q.Position);
        s.PositionRadius[0]=q.Position[0]; s.PositionRadius[1]=q.Position[1]; s.PositionRadius[2]=q.Position[2]; s.PositionRadius[3]=r;
        s.NormalAge[0]=q.Normal[0]; s.NormalAge[1]=q.Normal[1]; s.NormalAge[2]=q.Normal[2]; s.NormalAge[3]=0.0f;
        s.Albedo[0]=q.Albedo[0]; s.Albedo[1]=q.Albedo[1]; s.Albedo[2]=q.Albedo[2]; s.Albedo[3]=1.0f;   // alive

        // reuse a dead slot if one exists, else append
        bool placed = false;
        for (uint32_t k = 0; k < HostSurfels.size(); ++k)
            if (HostSurfels[k].Albedo[3] < 0.5f) { HostSurfels[k] = s; ++recycled; placed = true; topologyChanged = true; break; }
        if (!placed && HostSurfels.size() < I.MaxSurfels) { HostSurfels.push_back(s); ++spawned; topologyChanged = true; }
    }

    // 3) build the hash grid on the host: clear heads, then prepend each live surfel to its cell chain (+1 encoding).
    LiveCount = 0u;
    std::fill(HostGridHead.begin(), HostGridHead.end(), 0);
    if (HostGridNext.size() < HostSurfels.size()) HostGridNext.resize(HostSurfels.size(), 0);
    for (uint32_t i = 0; i < HostSurfels.size(); ++i)
    {
        const GpuSurfel& s = HostSurfels[i];
        if (s.Albedo[3] < 0.5f) { HostGridNext[i] = 0; continue; }
        ++LiveCount;
        const int cx = int(std::floor((s.PositionRadius[0] - Params.GridOrigin[0]) / cell));
        const int cy = int(std::floor((s.PositionRadius[1] - Params.GridOrigin[1]) / cell));
        const int cz = int(std::floor((s.PositionRadius[2] - Params.GridOrigin[2]) / cell));
        const uint32_t h = CellHash(cx, cy, cz, I.GridHashSize);
        HostGridNext[i]  = HostGridHead[h];        // previous head becomes our next (+1-encoded; 0 = end)
        HostGridHead[h]  = int32_t(i) + 1;         // we are the new head (+1-encoded)
    }

    // Stage a structural refresh only on population changes. The compute passes retain and refine Irradiance on
    // ordinary frames; a full host upload on every frame would make the advertised running mean non-persistent.
    if (topologyChanged)
    {
        FieldUploadPending = true;
        UploadBuffers();
    }

    // 4) log (task requirement). Throttle to once a second at 60 fps so the console is readable.
    if (FrameCounter <= 3 || (FrameCounter % 60u) == 0u)
    {
        const float coverage = I.GridHashSize ? float(LiveCount) / float(I.GridHashSize) : 0.0f;
        std::cerr << "[SurfelGI] frame " << FrameCounter
                  << "  live=" << LiveCount << "/" << I.MaxSurfels
                  << "  spawned=" << spawned << "  recycled=" << recycled
                  << "  coverage=" << coverage << " surfels/cell"
                  << "  rays=" << Params.RayCount << "\n";
    }
}

void SurfelGIStage::UploadBuffers() noexcept
{
    if (!FieldUploadPending) return;
    auto Copy = [&](VkDeviceMemory Mem, const void* Src, VkDeviceSize Bytes)
    {
        if (Bytes == 0) return;
        void* Mapped = nullptr;
        if (vkMapMemory(I.Device, Mem, 0, Bytes, 0, &Mapped) == VK_SUCCESS)
        {
            std::memcpy(Mapped, Src, static_cast<size_t>(Bytes));
            vkUnmapMemory(I.Device, Mem);
        }
    };
    Copy(SurfelStageMem, HostSurfels.data(),  VkDeviceSize(HostSurfels.size()) * sizeof(GpuSurfel));
    Copy(HeadStageMem,   HostGridHead.data(), VkDeviceSize(HostGridHead.size()) * sizeof(int32_t));
    Copy(NextStageMem,   HostGridNext.data(), VkDeviceSize(HostSurfels.size()) * sizeof(int32_t));
    // NOTE: the staging→device transfers are recorded at the top of RecordFrame() so they land inside the frame's
    //       command buffer (one queue, correct ordering). See RecordFrame().
}

//========================================================================================================================
// Per-frame recording: (upload) → update → commit → resolve
//========================================================================================================================
bool SurfelGIStage::RecordFrame(VkCommandBuffer Command, const SurfelFrameParams& Params) noexcept
{
    if (!IsReady() || Command == VK_NULL_HANDLE || LiveCount == 0u || Params.RenderWidth == 0u || Params.RenderHeight == 0u)
        return false;   // nothing to shade this frame; caller falls back to the existing renderer

    // 0) Upload only when the field topology changed. In steady state the GPU's Irradiance is authoritative;
    // copying the CPU seed every frame would erase its running mean. A compute→compute barrier still makes the
    // previous submission's commit visible before this frame's Jacobi update.
    if (FieldUploadPending)
    {
        auto CopyBuf = [&](VkBuffer Stage, VkBuffer Dst, VkDeviceSize Bytes)
        { VkBufferCopy R{ 0, 0, Bytes }; vkCmdCopyBuffer(Command, Stage, Dst, 1u, &R); };
        CopyBuf(SurfelStage, SurfelBuffer, VkDeviceSize(HostSurfels.size()) * sizeof(GpuSurfel));
        CopyBuf(HeadStage,   HeadBuffer,   VkDeviceSize(HostGridHead.size()) * sizeof(int32_t));
        CopyBuf(NextStage,   NextBuffer,   VkDeviceSize(HostSurfels.size()) * sizeof(int32_t));

        VkMemoryBarrier ToCompute{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        ToCompute.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        ToCompute.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(Command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0u, 1u, &ToCompute, 0u, nullptr, 0u, nullptr);
        FieldUploadPending = false;
    }
    else
    {
        VkMemoryBarrier PreviousCommit{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        PreviousCommit.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        PreviousCommit.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(Command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0u, 1u, &PreviousCommit, 0u, nullptr, 0u, nullptr);
    }

    VkMemoryBarrier ComputeRW{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    ComputeRW.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    ComputeRW.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    auto ComputeBarrier = [&]{
        vkCmdPipelineBarrier(Command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0u, 1u, &ComputeRW, 0u, nullptr, 0u, nullptr);
    };

    const uint32_t Groups1D = (LiveCount + 63u) / 64u;

    // 1) SurfelIrradianceUpdate — one thread per surfel; writes IrradianceNew (Jacobi target).
    struct UpdatePush { float Sun[4], SunCol[4], Sky[4], Grid[4]; uint32_t Counts[4]; float Tuning[4]; } up{};
    up.Sun[0]=Params.SunDirection[0]; up.Sun[1]=Params.SunDirection[1]; up.Sun[2]=Params.SunDirection[2]; up.Sun[3]=Params.SunRadiance;
    up.SunCol[0]=Params.SunColour[0]; up.SunCol[1]=Params.SunColour[1]; up.SunCol[2]=Params.SunColour[2];
    up.Sky[0]=Params.SkyAmbient[0]; up.Sky[1]=Params.SkyAmbient[1]; up.Sky[2]=Params.SkyAmbient[2];
    up.Grid[0]=Params.GridOrigin[0]; up.Grid[1]=Params.GridOrigin[1]; up.Grid[2]=Params.GridOrigin[2]; up.Grid[3]=I.GridCellSize;
    up.Counts[0]=LiveCount; up.Counts[1]=Params.FrameIndex; up.Counts[2]=Params.RayCount; up.Counts[3]=I.GridHashSize;
    up.Tuning[0]=Params.FireflyClamp; up.Tuning[1]=Params.AgeCap;
    vkCmdBindPipeline(Command, VK_PIPELINE_BIND_POINT_COMPUTE, UpdatePipeline);
    vkCmdBindDescriptorSets(Command, VK_PIPELINE_BIND_POINT_COMPUTE, UpdatePipeLayout, 0u, 1u, &UpdateSet, 0u, nullptr);
    vkCmdPushConstants(Command, UpdatePipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(up), &up);
    vkCmdDispatch(Command, Groups1D, 1u, 1u);
    ComputeBarrier();

    // 1.5) SurfelCommit — Irradiance = IrradianceNew (deferred Jacobi swap).
    uint32_t commit[4]{ LiveCount, 0u, 0u, 0u };
    vkCmdBindPipeline(Command, VK_PIPELINE_BIND_POINT_COMPUTE, CommitPipeline);
    vkCmdBindDescriptorSets(Command, VK_PIPELINE_BIND_POINT_COMPUTE, CommitPipeLayout, 0u, 1u, &CommitSet, 0u, nullptr);
    vkCmdPushConstants(Command, CommitPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(commit), commit);
    vkCmdDispatch(Command, Groups1D, 1u, 1u);
    ComputeBarrier();

    // 2) SurfelGIResolve — per-pixel; reads the G-buffer + surfel field, writes the presentation image.
    struct ResolvePush { float Sun[4], SunCol[4], Sky[4], Grid[4], Eye[4]; uint32_t Counts[4]; } rp{};
    std::memcpy(rp.Sun,  up.Sun,  sizeof(rp.Sun));
    std::memcpy(rp.SunCol, up.SunCol, sizeof(rp.SunCol));
    std::memcpy(rp.Grid, up.Grid, sizeof(rp.Grid));
    rp.Sky[0]=Params.SkyAmbient[0]; rp.Sky[1]=Params.SkyAmbient[1]; rp.Sky[2]=Params.SkyAmbient[2]; rp.Sky[3]=Params.Exposure;
    rp.Eye[0]=Params.CameraEye[0]; rp.Eye[1]=Params.CameraEye[1]; rp.Eye[2]=Params.CameraEye[2];
    rp.Counts[0]=LiveCount; rp.Counts[1]=I.GridHashSize; rp.Counts[2]=Params.FeatureFlags; rp.Counts[3]=Params.ReflectionMode;
    const uint32_t Gx = (Params.RenderWidth  + 15u) / 16u;
    const uint32_t Gy = (Params.RenderHeight + 15u) / 16u;
    vkCmdBindPipeline(Command, VK_PIPELINE_BIND_POINT_COMPUTE, ResolvePipeline);
    vkCmdBindDescriptorSets(Command, VK_PIPELINE_BIND_POINT_COMPUTE, ResolvePipeLayout, 0u, 1u, &ResolveSet, 0u, nullptr);
    vkCmdPushConstants(Command, ResolvePipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(rp), &rp);
    vkCmdDispatch(Command, Gx, Gy, 1u);
    return true;
}

//========================================================================================================================
// Teardown
//========================================================================================================================
void SurfelGIStage::Destroy() noexcept
{
    if (I.Device == VK_NULL_HANDLE) return;
    auto DB = [&](VkBuffer& b, VkDeviceMemory& m){ if (b) vkDestroyBuffer(I.Device, b, nullptr); if (m) vkFreeMemory(I.Device, m, nullptr); b=VK_NULL_HANDLE; m=VK_NULL_HANDLE; };
    DB(SurfelBuffer, SurfelMem); DB(HeadBuffer, HeadMem); DB(NextBuffer, NextMem);
    DB(SurfelStage, SurfelStageMem); DB(HeadStage, HeadStageMem); DB(NextStage, NextStageMem);
    auto DP = [&](VkPipeline& p){ if (p) vkDestroyPipeline(I.Device, p, nullptr); p=VK_NULL_HANDLE; };
    DP(UpdatePipeline); DP(CommitPipeline); DP(ResolvePipeline);
    auto DPL = [&](VkPipelineLayout& l){ if (l) vkDestroyPipelineLayout(I.Device, l, nullptr); l=VK_NULL_HANDLE; };
    DPL(UpdatePipeLayout); DPL(CommitPipeLayout); DPL(ResolvePipeLayout);
    // Descriptor sets die with their private pool; release it before their layouts to keep validation lifetime
    // ordering explicit on scene replacement and swapchain resize.
    if (DescriptorPool) vkDestroyDescriptorPool(I.Device, DescriptorPool, nullptr);
    DescriptorPool = VK_NULL_HANDLE;
    UpdateSet = CommitSet = ResolveSet = VK_NULL_HANDLE;
    auto DSL = [&](VkDescriptorSetLayout& l){ if (l) vkDestroyDescriptorSetLayout(I.Device, l, nullptr); l=VK_NULL_HANDLE; };
    DSL(UpdateLayout); DSL(CommitLayout); DSL(ResolveLayout);
    HostSurfels.clear(); HostGridHead.clear(); HostGridNext.clear();
    LiveCount = 0u; FrameCounter = 0u; FieldUploadPending = false;
    I = {};
}
}
