/*==========================================================================================================================================
                                                        BAKERASTERIZATION.CPP
==========================================================================================================================================*/
// 🧩 Submit BakeFieldCompute over a staged, device-local bake BVH in watchdog-bounded texel bands and harvest
//    each band's evaluated fields as its frame submission retires — no queue waits, one bake in flight at a time.

#include "BakeRasterization.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <string_view>
#include <utility>

namespace Frontier
{
namespace
{
void ConfirmBakeResult(VkResult Result)
{
    if (Result != VK_SUCCESS) throw std::runtime_error("Vulkan bake rasterization failed: " + std::to_string(int(Result)));
}

// 📝 The field an empty-extent evaluation produces for every texel (BakeField.cpp's EvaluateTexel with no hit:
// bent/bevel fall back to the low normal, visibilities stay 1). Written host-side when a request's high mesh
// holds no triangles, so a zero-extent dispatch (and its undefined buffer dimensions) never reaches the GPU.
BakeTexelField WriteMissField(const BakeTexelSurface &Surface) noexcept
{
    BakeTexelField Field{};
    Field.Bent[0] = Surface.WorldNormal[0];
    Field.Bent[1] = Surface.WorldNormal[1];
    Field.Bent[2] = Surface.WorldNormal[2];
    Field.Bent[3] = 1.f;
    Field.Bevel[0] = Surface.WorldNormal[0];
    Field.Bevel[1] = Surface.WorldNormal[1];
    Field.Bevel[2] = Surface.WorldNormal[2];
    Field.Bevel[3] = 0.f;
    Field.Scalars[0] = 1.f;
    return Field;
}
} // namespace

struct BakeRasterization::BakeAllocation
{
    VulkanRasterAccess Access;
    VkPhysicalDeviceMemoryProperties Memory{};
    VkDeviceSize MaxStorageBufferRange = 0;

    VkBuffer Triangles = VK_NULL_HANDLE, Extents = VK_NULL_HANDLE, Staging = VK_NULL_HANDLE;
    VkBuffer Surfaces = VK_NULL_HANDLE, Fields = VK_NULL_HANDLE;
    VkDeviceMemory TriangleMemory = VK_NULL_HANDLE, ExtentMemory = VK_NULL_HANDLE, StagingMemory = VK_NULL_HANDLE;
    VkDeviceMemory SurfaceMemory = VK_NULL_HANDLE, FieldMemory = VK_NULL_HANDLE;
    uint64_t TriangleCapacity = 0, ExtentCapacity = 0, StagingCapacity = 0; // [slots]/[slots]/[B]
    VkDescriptorSetLayout DescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool Pool = VK_NULL_HANDLE;
    VkDescriptorSet Descriptor = VK_NULL_HANDLE;
    VkPipelineLayout Layout = VK_NULL_HANDLE;
    VkPipeline Pipeline = VK_NULL_HANDLE;

    // 📝 Active-bake sequencing: Next* is the band ConstructCommands will dispatch, InFlight* the band whose
    // fields the next retired submission makes readable. One band in flight keeps every host-buffer rewrite
    // behind a completed submission serial, the same discipline the coverage units keep.
    std::shared_ptr<const BakeFieldRequest> Request;
    size_t NextTarget = 0, InFlightTarget = 0;
    uint32_t NextFirst = 0, InFlightFirst = 0, InFlightCount = 0;
    bool TrianglesUploaded = false, BandInFlight = false, Completed = false;
    uint64_t LastSubmission = 0, PendingSubmission = 0, Dispatches = 0;
    bool Recorded = false;
    uint64_t HarvestedTexels = 0, TotalTexels = 0;
    std::vector<std::vector<BakeTexelField>> OutcomeFields;
    bool Faulted = false;
    std::string StoredFailure;

#define FRONTIER_BAKE_FUNCTIONS(Function) \
    Function(CreateBuffer) Function(DestroyBuffer) Function(GetBufferMemoryRequirements) Function(AllocateMemory) \
    Function(FreeMemory) Function(BindBufferMemory) Function(MapMemory) Function(UnmapMemory) \
    Function(CreateDescriptorSetLayout) Function(DestroyDescriptorSetLayout) Function(CreateDescriptorPool) \
    Function(DestroyDescriptorPool) Function(AllocateDescriptorSets) Function(UpdateDescriptorSets) \
    Function(CreatePipelineLayout) Function(DestroyPipelineLayout) Function(CreateShaderModule) \
    Function(DestroyShaderModule) Function(CreateComputePipelines) Function(DestroyPipeline) \
    Function(CmdPipelineBarrier) Function(CmdBindPipeline) Function(CmdBindDescriptorSets) \
    Function(CmdPushConstants) Function(CmdDispatch) Function(CmdCopyBuffer)
#define FRONTIER_BAKE_POINTER(Name) PFN_vk##Name Name = nullptr;
    FRONTIER_BAKE_FUNCTIONS(FRONTIER_BAKE_POINTER)
#undef FRONTIER_BAKE_POINTER

    ~BakeAllocation()
    {
        if (!Access.Device) return;
        if (Pipeline) DestroyPipeline(Access.Device, Pipeline, nullptr);
        if (Layout) DestroyPipelineLayout(Access.Device, Layout, nullptr);
        if (Pool) DestroyDescriptorPool(Access.Device, Pool, nullptr);
        if (DescriptorLayout) DestroyDescriptorSetLayout(Access.Device, DescriptorLayout, nullptr);
        for (auto Buffer : {Triangles, Extents, Staging, Surfaces, Fields})
            if (Buffer) DestroyBuffer(Access.Device, Buffer, nullptr);
        for (auto Storage : {TriangleMemory, ExtentMemory, StagingMemory, SurfaceMemory, FieldMemory})
            if (Storage) FreeMemory(Access.Device, Storage, nullptr);
    }
    void Allocate(VkMemoryRequirements Requirements, VkMemoryPropertyFlags Properties, VkDeviceMemory &Storage)
    {
        uint32_t Selection = UINT32_MAX;
        for (uint32_t Index = 0; Index < Memory.memoryTypeCount; ++Index)
            if ((Requirements.memoryTypeBits & (1u << Index)) && (Memory.memoryTypes[Index].propertyFlags & Properties) == Properties) { Selection = Index; break; }
        if (Selection == UINT32_MAX) throw std::runtime_error("Vulkan bake memory category is unavailable.");
        VkMemoryAllocateInfo Creation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        Creation.allocationSize = Requirements.size; Creation.memoryTypeIndex = Selection;
        ConfirmBakeResult(AllocateMemory(Access.Device, &Creation, nullptr, &Storage));
    }
    void AllocateBuffer(VkDeviceSize Bytes, VkBufferUsageFlags Usage, VkMemoryPropertyFlags Properties, VkBuffer &Buffer,
        VkDeviceMemory &Storage)
    {
        VkBufferCreateInfo Creation{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        Creation.size = std::max<VkDeviceSize>(Bytes, 4); Creation.usage = Usage;
        ConfirmBakeResult(CreateBuffer(Access.Device, &Creation, nullptr, &Buffer));
        VkMemoryRequirements Requirements; GetBufferMemoryRequirements(Access.Device, Buffer, &Requirements);
        Allocate(Requirements, Properties, Storage);
        ConfirmBakeResult(BindBufferMemory(Access.Device, Buffer, Storage, 0));
    }
    void ReleaseBuffer(VkBuffer &Buffer, VkDeviceMemory &Storage) noexcept
    {
        if (Buffer) DestroyBuffer(Access.Device, Buffer, nullptr);
        if (Storage) FreeMemory(Access.Device, Storage, nullptr);
        Buffer = VK_NULL_HANDLE; Storage = VK_NULL_HANDLE;
    }
    void Upload(VkDeviceMemory Storage, VkDeviceSize Offset, const void *Source, size_t Bytes)
    {
        if (!Bytes) return;
        void *Destination = nullptr;
        ConfirmBakeResult(MapMemory(Access.Device, Storage, Offset, Bytes, 0, &Destination));
        std::memcpy(Destination, Source, Bytes); UnmapMemory(Access.Device, Storage);
    }
    void WriteDescriptors()
    {
        const VkDescriptorBufferInfo Buffers[4]{{Triangles, 0, VK_WHOLE_SIZE}, {Extents, 0, VK_WHOLE_SIZE},
            {Surfaces, 0, VK_WHOLE_SIZE}, {Fields, 0, VK_WHOLE_SIZE}};
        VkWriteDescriptorSet Writes[4]{};
        for (uint32_t Index = 0; Index < 4; ++Index)
        {
            Writes[Index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            Writes[Index].dstSet = Descriptor;
            Writes[Index].dstBinding = Index;
            Writes[Index].descriptorCount = 1;
            Writes[Index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            Writes[Index].pBufferInfo = &Buffers[Index];
        }
        UpdateDescriptorSets(Access.Device, 4, Writes, 0, nullptr);
    }
    // 📝 Grows the staged residence to the active request and refreshes descriptors. Only called before the
    // request's first dispatch, with every earlier submission retired, so releasing buffers here can never
    // pull storage from under an in-flight dispatch.
    void EnsureResidence()
    {
        const uint64_t TrianglesNeeded = Request->Triangles.Triangles.size();
        const uint64_t ExtentsNeeded = Request->Triangles.Extents.size();
        const uint64_t StagingNeeded = TrianglesNeeded * sizeof(BakeTriangleSlot) + ExtentsNeeded * sizeof(BakeExtentSlot);
        bool Recreated = false;
        if (TrianglesNeeded > TriangleCapacity)
        {
            ReleaseBuffer(Triangles, TriangleMemory);
            AllocateBuffer(TrianglesNeeded * sizeof(BakeTriangleSlot),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Triangles, TriangleMemory);
            TriangleCapacity = TrianglesNeeded; Recreated = true;
        }
        if (ExtentsNeeded > ExtentCapacity)
        {
            ReleaseBuffer(Extents, ExtentMemory);
            AllocateBuffer(ExtentsNeeded * sizeof(BakeExtentSlot),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Extents, ExtentMemory);
            ExtentCapacity = ExtentsNeeded; Recreated = true;
        }
        if (StagingNeeded > StagingCapacity)
        {
            ReleaseBuffer(Staging, StagingMemory);
            AllocateBuffer(StagingNeeded, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, Staging, StagingMemory);
            StagingCapacity = StagingNeeded;
        }
        if (Recreated || !TrianglesUploaded) WriteDescriptors();
    }
    void BufferBarrier(VkCommandBuffer Commands, VkBuffer Buffer, VkAccessFlags SourceAccess, VkAccessFlags DestinationAccess,
        VkPipelineStageFlags SourceStage, VkPipelineStageFlags DestinationStage)
    {
        VkBufferMemoryBarrier Barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        Barrier.srcAccessMask = SourceAccess; Barrier.dstAccessMask = DestinationAccess;
        Barrier.srcQueueFamilyIndex = Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        Barrier.buffer = Buffer; Barrier.offset = 0; Barrier.size = VK_WHOLE_SIZE;
        CmdPipelineBarrier(Commands, SourceStage, DestinationStage, 0, 0, nullptr, 1, &Barrier, 0, nullptr);
    }
};

BakeRasterization::BakeRasterization() = default;
BakeRasterization::~BakeRasterization() = default;

void BakeRasterization::Initialize(const VulkanRasterAccess &Access, RasterShader Shader)
{
    if (Allocation || !Access.Device || !Access.PhysicalDevice || !Access.ResolveDevice || !Access.QueryMemory ||
        !Access.QueryLimits || Shader.Words.size() < 5 || Shader.Words.size() > 4 * 1024 * 1024 ||
        Shader.Words[0] != 0x07230203 || !Shader.Entry)
        throw std::runtime_error("Invalid Vulkan bake initialization.");
    if (!(Access.QueueCapabilities & VK_QUEUE_COMPUTE_BIT))
        throw std::runtime_error("Bake rasterization requires an explicitly verified compute-capable command queue.");
    bool EntryPresent = false;
    for (size_t Offset = 5; Offset < Shader.Words.size();)
    {
        const size_t Words = Shader.Words[Offset] >> 16;
        if (!Words || Words > Shader.Words.size() - Offset) throw std::runtime_error("Malformed bake SPIR-V instruction.");
        if ((Shader.Words[Offset] & 65535) == 15 && Words >= 4 && Shader.Words[Offset + 1] == 5)
        {
            const auto *Name = reinterpret_cast<const char *>(Shader.Words.data() + Offset + 3);
            const size_t Bytes = (Words - 3) * sizeof(uint32_t);
            const auto *Terminator = static_cast<const char *>(std::memchr(Name, 0, Bytes));
            if (Terminator && std::string_view(Name, size_t(Terminator - Name)) == Shader.Entry) EntryPresent = true;
        }
        Offset += Words;
    }
    if (!EntryPresent) throw std::runtime_error("Bake shader compute entry is absent.");

    auto Resident = std::make_unique<BakeAllocation>();
    Resident->Access = Access;
#define FRONTIER_BAKE_RESOLVE(Name) Resident->Name = reinterpret_cast<PFN_vk##Name>(Access.ResolveDevice(Access.Device, "vk" #Name)); if (!Resident->Name) throw std::runtime_error("Missing Vulkan bake function: vk" #Name);
    FRONTIER_BAKE_FUNCTIONS(FRONTIER_BAKE_RESOLVE)
#undef FRONTIER_BAKE_RESOLVE
    Access.QueryMemory(Access.PhysicalDevice, &Resident->Memory);
    VkPhysicalDeviceProperties Properties{}; Access.QueryLimits(Access.PhysicalDevice, &Properties);
    const auto &Limits = Properties.limits;
    constexpr VkDeviceSize SurfaceBandBytes = VkDeviceSize(BandTexels) * sizeof(BakeTexelSurface);
    constexpr VkDeviceSize FieldBandBytes = VkDeviceSize(BandTexels) * sizeof(BakeTexelField);
    if (Limits.maxPushConstantsSize < sizeof(BakeFieldUniforms) || Limits.maxPerStageDescriptorStorageBuffers < 4 ||
        Limits.maxDescriptorSetStorageBuffers < 4 || Limits.maxBoundDescriptorSets < 1 ||
        Limits.maxComputeWorkGroupInvocations < 64 || Limits.maxComputeWorkGroupSize[0] < 64 ||
        Limits.maxComputeWorkGroupCount[0] < BandTexels / 64 ||
        Limits.maxStorageBufferRange < std::max(SurfaceBandBytes, FieldBandBytes))
        throw std::runtime_error("Vulkan device cannot support bounded bake field evaluation.");
    Resident->MaxStorageBufferRange = Limits.maxStorageBufferRange;
    DeviceName = Properties.deviceName;

    // 📝 Band buffers are fixed-size for the unit's lifetime (one band in flight); the BVH residence grows
    // per request in EnsureResidence. Surfaces stream in and fields stream out exactly once per texel, so
    // host-visible placement costs nothing there, while the traversal's random access wants device-local.
    Resident->AllocateBuffer(SurfaceBandBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, Resident->Surfaces, Resident->SurfaceMemory);
    Resident->AllocateBuffer(FieldBandBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, Resident->Fields, Resident->FieldMemory);

    const VkDescriptorSetLayoutBinding Bindings[]{{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo Descriptor{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    Descriptor.bindingCount = 4; Descriptor.pBindings = Bindings;
    ConfirmBakeResult(Resident->CreateDescriptorSetLayout(Access.Device, &Descriptor, nullptr, &Resident->DescriptorLayout));
    const VkDescriptorPoolSize PoolSizes[]{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4}};
    VkDescriptorPoolCreateInfo Pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    Pool.maxSets = 1; Pool.poolSizeCount = 1; Pool.pPoolSizes = PoolSizes;
    ConfirmBakeResult(Resident->CreateDescriptorPool(Access.Device, &Pool, nullptr, &Resident->Pool));
    VkDescriptorSetAllocateInfo Selection{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    Selection.descriptorPool = Resident->Pool; Selection.descriptorSetCount = 1; Selection.pSetLayouts = &Resident->DescriptorLayout;
    ConfirmBakeResult(Resident->AllocateDescriptorSets(Access.Device, &Selection, &Resident->Descriptor));
    VkPushConstantRange PushRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, uint32_t(sizeof(BakeFieldUniforms))};
    VkPipelineLayoutCreateInfo Layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    Layout.setLayoutCount = 1; Layout.pSetLayouts = &Resident->DescriptorLayout;
    Layout.pushConstantRangeCount = 1; Layout.pPushConstantRanges = &PushRange;
    ConfirmBakeResult(Resident->CreatePipelineLayout(Access.Device, &Layout, nullptr, &Resident->Layout));
    VkShaderModule ShaderLibrary = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo Source{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    Source.codeSize = Shader.Words.size_bytes(); Source.pCode = Shader.Words.data();
    ConfirmBakeResult(Resident->CreateShaderModule(Access.Device, &Source, nullptr, &ShaderLibrary));
    VkComputePipelineCreateInfo Pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    Pipeline.layout = Resident->Layout;
    Pipeline.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, ShaderLibrary, Shader.Entry, nullptr};
    const auto Result = Resident->CreateComputePipelines(Access.Device, VK_NULL_HANDLE, 1, &Pipeline, nullptr, &Resident->Pipeline);
    Resident->DestroyShaderModule(Access.Device, ShaderLibrary, nullptr); ConfirmBakeResult(Result);
    Allocation = std::move(Resident);
}

void BakeRasterization::Initialize(const VulkanRasterAccess &Access, const std::filesystem::path &ShaderArchive)
{
    std::ifstream Input(ShaderArchive, std::ios::binary | std::ios::ate);
    const auto Bytes = Input.tellg();
    if (!Input || Bytes < 20 || Bytes > 16777216 || Bytes % 4)
        throw std::runtime_error("Missing or malformed bake field shader archive.");
    std::vector<uint32_t> Words(size_t(Bytes) / 4);
    Input.seekg(0);
    if (!Input.read(reinterpret_cast<char *>(Words.data()), Bytes) || Words[0] != 0x07230203)
        throw std::runtime_error("Invalid bake field SPIR-V archive.");
    Initialize(Access, RasterShader{Words, "main"});
}

bool BakeRasterization::ConfigureRequest(std::shared_ptr<const BakeFieldRequest> Request)
{
    if (!Allocation || Allocation->Faulted || Allocation->Request || !Request || Request->Targets.empty()) return false;
    auto &Resident = *Allocation;
    Resident.TotalTexels = 0;
    for (const auto &Target : Request->Targets) Resident.TotalTexels += Target.Surfaces.size();
    Resident.HarvestedTexels = 0;
    Resident.StoredFailure.clear();

    const auto Refuse = [&](std::string Failure)
    {
        Resident.StoredFailure = std::move(Failure);
        DiagnosticJournal::Append("BakeRasterization", "request refused: " + Resident.StoredFailure);
        return true;
    };
    if (Resident.TotalTexels == 0) return Refuse("Bake request holds no texels.");
    const VkDeviceSize TriangleBytes = VkDeviceSize(Request->Triangles.Triangles.size()) * sizeof(BakeTriangleSlot);
    const VkDeviceSize ExtentBytes = VkDeviceSize(Request->Triangles.Extents.size()) * sizeof(BakeExtentSlot);
    if (TriangleBytes > Resident.MaxStorageBufferRange)
        return Refuse("High-mesh triangle buffer (" + std::to_string(TriangleBytes) + " bytes) exceeds this device's " +
            std::to_string(Resident.MaxStorageBufferRange) + "-byte storage range.");
    if (ExtentBytes > Resident.MaxStorageBufferRange)
        return Refuse("High-mesh extent buffer (" + std::to_string(ExtentBytes) + " bytes) exceeds this device's " +
            std::to_string(Resident.MaxStorageBufferRange) + "-byte storage range.");

    Resident.OutcomeFields.assign(Request->Targets.size(), {});
    for (size_t Index = 0; Index < Request->Targets.size(); ++Index)
        Resident.OutcomeFields[Index].resize(Request->Targets[Index].Surfaces.size());

    // 📝 Empty high mesh: every query misses by construction, so the miss fields are written directly and the
    // bake completes without a dispatch (see WriteMissField) — identical outcomes, no zero-sized buffers.
    if (Request->Triangles.Triangles.empty())
    {
        for (size_t Target = 0; Target < Request->Targets.size(); ++Target)
            for (size_t Texel = 0; Texel < Request->Targets[Target].Surfaces.size(); ++Texel)
                Resident.OutcomeFields[Target][Texel] = WriteMissField(Request->Targets[Target].Surfaces[Texel]);
        Resident.HarvestedTexels = Resident.TotalTexels;
        Resident.Completed = true;
        Resident.Request = std::move(Request);
        return true;
    }

    Resident.NextTarget = Resident.InFlightTarget = 0;
    Resident.NextFirst = Resident.InFlightFirst = Resident.InFlightCount = 0;
    Resident.TrianglesUploaded = Resident.BandInFlight = Resident.Completed = false;
    Resident.Request = std::move(Request);
    return true;
}

bool BakeRasterization::ConstructCommands(VkCommandBuffer Commands, uint64_t CompletedSerial, uint64_t SubmissionSerial)
{
    if (!Allocation || Allocation->Faulted || !Allocation->Request || Allocation->Completed) return false;
    auto &Resident = *Allocation;
    if (Resident.Recorded) throw std::runtime_error("Bake band recording was not confirmed by its submission.");

    // ① Harvest the in-flight band once its submission retires; its fields become host-readable here.
    if (Resident.BandInFlight)
    {
        if (Resident.LastSubmission > CompletedSerial) return false;
        void *Mapped = nullptr;
        ConfirmBakeResult(Resident.MapMemory(Resident.Access.Device, Resident.FieldMemory, 0,
            VkDeviceSize(Resident.InFlightCount) * sizeof(BakeTexelField), 0, &Mapped));
        std::memcpy(Resident.OutcomeFields[Resident.InFlightTarget].data() + Resident.InFlightFirst, Mapped,
            size_t(Resident.InFlightCount) * sizeof(BakeTexelField));
        Resident.UnmapMemory(Resident.Access.Device, Resident.FieldMemory);
        Resident.HarvestedTexels += Resident.InFlightCount;
        Resident.BandInFlight = false;
        Resident.InFlightCount = 0;
        if (Resident.NextTarget >= Resident.Request->Targets.size())
        {
            Resident.Completed = true;
            return false;
        }
    }

    // ② Nothing may be rewritten while an earlier submission could still read it.
    if (!Commands || Resident.LastSubmission > CompletedSerial || SubmissionSerial <= CompletedSerial)
        throw std::runtime_error("Bake band is not retired or recording serial is invalid.");

    const BakeFieldTarget &Target = Resident.Request->Targets[Resident.NextTarget];
    const uint32_t TargetTexels = uint32_t(Target.Surfaces.size());
    const uint32_t BandCount = std::min(BandTexels, TargetTexels - Resident.NextFirst);

    if (!Resident.TrianglesUploaded)
    {
        Resident.EnsureResidence();
        const auto &Triangles = Resident.Request->Triangles.Triangles;
        const auto &Extents = Resident.Request->Triangles.Extents;
        const VkDeviceSize TriangleBytes = VkDeviceSize(Triangles.size()) * sizeof(BakeTriangleSlot);
        Resident.Upload(Resident.StagingMemory, 0, Triangles.data(), Triangles.size() * sizeof(BakeTriangleSlot));
        Resident.Upload(Resident.StagingMemory, TriangleBytes, Extents.data(), Extents.size() * sizeof(BakeExtentSlot));
        Resident.BufferBarrier(Commands, Resident.Staging, VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        const VkBufferCopy TriangleCopy{0, 0, TriangleBytes};
        Resident.CmdCopyBuffer(Commands, Resident.Staging, Resident.Triangles, 1, &TriangleCopy);
        const VkBufferCopy ExtentCopy{TriangleBytes, 0, VkDeviceSize(Extents.size()) * sizeof(BakeExtentSlot)};
        Resident.CmdCopyBuffer(Commands, Resident.Staging, Resident.Extents, 1, &ExtentCopy);
        Resident.BufferBarrier(Commands, Resident.Triangles, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        Resident.BufferBarrier(Commands, Resident.Extents, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

    Resident.Upload(Resident.SurfaceMemory, 0, Target.Surfaces.data() + Resident.NextFirst,
        size_t(BandCount) * sizeof(BakeTexelSurface));
    Resident.BufferBarrier(Commands, Resident.Surfaces, VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    Resident.CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, Resident.Pipeline);
    Resident.CmdBindDescriptorSets(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, Resident.Layout, 0, 1, &Resident.Descriptor, 0, nullptr);
    BakeFieldUniforms Uniforms = Resident.Request->Uniforms;
    Uniforms.TexelCount = BandCount;
    Resident.CmdPushConstants(Commands, Resident.Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, uint32_t(sizeof(Uniforms)), &Uniforms);
    Resident.CmdDispatch(Commands, (BandCount + 63) / 64, 1, 1);
    Resident.BufferBarrier(Commands, Resident.Fields, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT);

    Resident.TrianglesUploaded = true;
    Resident.InFlightTarget = Resident.NextTarget;
    Resident.InFlightFirst = Resident.NextFirst;
    Resident.InFlightCount = BandCount;
    Resident.NextFirst += BandCount;
    if (Resident.NextFirst >= TargetTexels) { ++Resident.NextTarget; Resident.NextFirst = 0; }
    Resident.BandInFlight = true;
    Resident.Recorded = true;
    Resident.PendingSubmission = SubmissionSerial;
    ++Resident.Dispatches;
    return true;
}

void BakeRasterization::ConfirmSubmission() noexcept
{
    if (!Allocation || !Allocation->Recorded) return;
    Allocation->LastSubmission = Allocation->PendingSubmission;
    Allocation->PendingSubmission = 0;
    Allocation->Recorded = false;
}

void BakeRasterization::ConfigureFaulted(std::string Failure) noexcept
{
    if (!Allocation) return;
    Allocation->Faulted = true;
    if (Allocation->Request && Allocation->StoredFailure.empty()) Allocation->StoredFailure = std::move(Failure);
    Allocation->BandInFlight = false;
    Allocation->Recorded = false;
}

BakeGpuOutcome BakeRasterization::QueryOutcome()
{
    BakeGpuOutcome Outcome;
    if (!Allocation) { Outcome.Failed = true; Outcome.Failure = "Bake rasterization is not initialized."; return Outcome; }
    auto &Resident = *Allocation;
    Outcome.DoneTexels = Resident.HarvestedTexels;
    Outcome.TotalTexels = Resident.TotalTexels;
    if (!Resident.StoredFailure.empty())
    {
        Outcome.Complete = true;
        Outcome.Failed = true;
        Outcome.Failure = std::move(Resident.StoredFailure);
        Resident.StoredFailure.clear();
        Resident.Request.reset();
        Resident.OutcomeFields.clear();
        Resident.Completed = false;
        return Outcome;
    }
    if (Resident.Completed)
    {
        Outcome.Complete = true;
        Outcome.Fields = std::move(Resident.OutcomeFields);
        Resident.OutcomeFields.clear();
        Resident.Request.reset();
        Resident.Completed = false;
        Resident.TrianglesUploaded = false;
        return Outcome;
    }
    return Outcome;
}

bool BakeRasterization::QueryReady() const noexcept { return Allocation && !Allocation->Faulted; }
const std::string &BakeRasterization::QueryDeviceName() const noexcept { return DeviceName; }
uint64_t BakeRasterization::QueryDispatchCount() const noexcept { return Allocation ? Allocation->Dispatches : 0; }
void BakeRasterization::Finalize() noexcept
{
    Allocation.reset();
    DeviceName.clear();
}
} // namespace Frontier
