/*============================================================================================================================================
                                                     PARTICLEVULKANEXECUTOR.CPP
============================================================================================================================================*/
// 🧩 Headless Vulkan 1.3 Compute executor for modular SoA particle channels, ForceField coupling, XPBD strands & 32 B async reduction

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Particles/ParticleVulkanExecutor.h"

#if defined(FRONTIER_WITH_VULKAN) && (FRONTIER_WITH_VULKAN != 0)
    #include <volk.h>
    #if defined(__linux__)
        #include <cstdlib>
        #include <dlfcn.h>
    #endif
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <vector>

namespace Frontier::SimulationSpace::Particles
{

#if !defined(FRONTIER_WITH_VULKAN) || (FRONTIER_WITH_VULKAN == 0)

struct ParticleVulkanExecutor::DeviceImplementation
{
};

ParticleVulkanExecutor::ParticleVulkanExecutor() noexcept  = default;
ParticleVulkanExecutor::~ParticleVulkanExecutor() noexcept = default;

bool ParticleVulkanExecutor::Initialize(
    const std::filesystem::path&,
    const std::filesystem::path&,
    std::string& OutDiagnostic) noexcept
{
    OutDiagnostic = "Vulkan compute disabled in this headless build profile.";
    return false;
}

void        ParticleVulkanExecutor::Finalize() noexcept {}
bool        ParticleVulkanExecutor::QueryDeviceReady() const noexcept { return false; }
std::string ParticleVulkanExecutor::QueryDeviceName() const { return {}; }
void*       ParticleVulkanExecutor::QueryNativeInstance() const noexcept { return nullptr; }
void*       ParticleVulkanExecutor::QueryNativePhysicalDevice() const noexcept { return nullptr; }
void*       ParticleVulkanExecutor::QueryNativeDevice() const noexcept { return nullptr; }
void*       ParticleVulkanExecutor::QueryNativeQueue() const noexcept { return nullptr; }
uint32_t    ParticleVulkanExecutor::QueryQueueFamilyIndex() const noexcept { return 0u; }
uint32_t    ParticleVulkanExecutor::QueryAllocatedCapacity() const noexcept { return 0u; }
void*       ParticleVulkanExecutor::QueryKinematicsBufferHandle() const noexcept { return nullptr; }
void*       ParticleVulkanExecutor::QueryLifespanBufferHandle() const noexcept { return nullptr; }
void*       ParticleVulkanExecutor::QueryAppearanceBufferHandle() const noexcept { return nullptr; }
void*       ParticleVulkanExecutor::QueryStrandLinkBufferHandle() const noexcept { return nullptr; }
uint64_t    ParticleVulkanExecutor::QueryKinematicsByteSize() const noexcept { return 0u; }
uint64_t    ParticleVulkanExecutor::QueryLifespanByteSize() const noexcept { return 0u; }
uint64_t    ParticleVulkanExecutor::QueryAppearanceByteSize() const noexcept { return 0u; }
uint64_t    ParticleVulkanExecutor::QueryStrandLinkByteSize() const noexcept { return 0u; }

bool ParticleVulkanExecutor::EnsureChannelCapacity(uint32_t, bool, std::string& OutDiagnostic) noexcept
{
    OutDiagnostic = "Vulkan compute disabled in this headless build profile.";
    return false;
}

bool ParticleVulkanExecutor::UploadChannelBank(const ParticleChannelBank&, std::string& OutDiagnostic) noexcept
{
    OutDiagnostic = "Vulkan compute disabled in this headless build profile.";
    return false;
}

bool ParticleVulkanExecutor::DownloadChannelBank(ParticleChannelBank&, std::string& OutDiagnostic) noexcept
{
    OutDiagnostic = "Vulkan compute disabled in this headless build profile.";
    return false;
}

bool ParticleVulkanExecutor::DispatchStep(const ParticleGpuStepRequest&, ParticleReadbackSummary&, std::string& OutDiagnostic) noexcept
{
    OutDiagnostic = "Vulkan compute disabled in this headless build profile.";
    return false;
}

#else // FRONTIER_WITH_VULKAN

namespace
{

constexpr uint32_t WorkgroupWidth = 64u;

[[nodiscard]] bool ReadSpirvWords(const std::filesystem::path& SourcePath, std::vector<uint32_t>& OutWords)
{
    std::ifstream Reader(SourcePath, std::ios::binary | std::ios::ate);
    if (!Reader.is_open())
    {
        return false;
    }
    const std::streamsize ByteSize = Reader.tellg();
    if (ByteSize <= 0 || (ByteSize % 4) != 0)
    {
        return false;
    }
    Reader.seekg(0, std::ios::beg);
    OutWords.resize(static_cast<std::size_t>(ByteSize / 4));
    Reader.read(reinterpret_cast<char*>(OutWords.data()), ByteSize);
    return Reader.good();
}

struct HostMappedBuffer
{
    VkBuffer       Handle   = VK_NULL_HANDLE;
    VkDeviceMemory Memory   = VK_NULL_HANDLE;
    VkDeviceSize   Capacity = 0u;
    void*          Mapped   = nullptr;
};

struct ComputeExecutableUnit
{
    VkDescriptorSetLayout DescriptorLayout = VK_NULL_HANDLE;
    VkPipelineLayout      Layout           = VK_NULL_HANDLE;
    VkPipeline            Executable       = VK_NULL_HANDLE;
    VkDescriptorSet       DescriptorSlot   = VK_NULL_HANDLE;
};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                               VULKAN DEVICE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

struct ParticleVulkanExecutor::DeviceImplementation
{
    VkInstance       Instance          = VK_NULL_HANDLE;
    VkPhysicalDevice PhysicalDevice    = VK_NULL_HANDLE;
    VkDevice         Device            = VK_NULL_HANDLE;
    VkQueue          ComputeQueue      = VK_NULL_HANDLE;
    uint32_t         QueueFamilyIndex  = 0u;
    VkDescriptorPool DescriptorBank    = VK_NULL_HANDLE;
    VkCommandPool    CommandBank       = VK_NULL_HANDLE;
    VkCommandBuffer  CommandRecorder   = VK_NULL_HANDLE;
    VkFence          CompletionSignal  = VK_NULL_HANDLE;

    ComputeExecutableUnit ReleaseUnit          {};
    ComputeExecutableUnit AdvectUnit           {};
    ComputeExecutableUnit StrandUnit           {};
    ComputeExecutableUnit ReductionAccumUnit   {};
    ComputeExecutableUnit ReductionBoundsUnit  {};
    ComputeExecutableUnit ReductionFinalUnit   {};

    HostMappedBuffer UniformBuffer     {};
    HostMappedBuffer StrandUniformBuf  {};
    HostMappedBuffer StrandVelUniform  {};
    HostMappedBuffer KinematicsBuffer  {};
    HostMappedBuffer LifespanBuffer    {};
    HostMappedBuffer AppearanceBuffer  {};
    HostMappedBuffer StrandAnchorBuf   {};
    HostMappedBuffer StrandLinkBuf     {};
    HostMappedBuffer ForceSourceBuffer {};
    HostMappedBuffer WaypointBuffer    {};
    HostMappedBuffer ScratchBuffer     {};
    HostMappedBuffer SummaryBuffer     {};

    uint32_t         SlotCapacity      = 0u;
    bool             Initialized       = false;
    std::string      DeviceLabel       {};

#if defined(__linux__)
    void*            LoaderModule      = nullptr;
#endif

    [[nodiscard]] uint32_t FindHostVisibleMemory(uint32_t TypeBits) const noexcept
    {
        VkPhysicalDeviceMemoryProperties Props{};
        vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &Props);
        constexpr VkMemoryPropertyFlags Required =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t Idx = 0u; Idx < Props.memoryTypeCount; ++Idx)
        {
            if ((TypeBits & (1u << Idx)) != 0u &&
                (Props.memoryTypes[Idx].propertyFlags & Required) == Required)
            {
                return Idx;
            }
        }
        return UINT32_MAX;
    }

    void DestroyBuffer(HostMappedBuffer& Buf) noexcept
    {
        if (Device == VK_NULL_HANDLE)
        {
            return;
        }
        if (Buf.Mapped != nullptr && Buf.Memory != VK_NULL_HANDLE)
        {
            vkUnmapMemory(Device, Buf.Memory);
            Buf.Mapped = nullptr;
        }
        if (Buf.Handle != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(Device, Buf.Handle, nullptr);
            Buf.Handle = VK_NULL_HANDLE;
        }
        if (Buf.Memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(Device, Buf.Memory, nullptr);
            Buf.Memory = VK_NULL_HANDLE;
        }
        Buf.Capacity = 0u;
    }

    bool EnsureBuffer(
        HostMappedBuffer&  Buf,
        VkDeviceSize       RequiredBytes,
        VkBufferUsageFlags UsageBits) noexcept
    {
        const VkDeviceSize SafeBytes = std::max<VkDeviceSize>(RequiredBytes, 256u);
        if (Buf.Handle != VK_NULL_HANDLE && Buf.Capacity >= SafeBytes)
        {
            return true;
        }
        DestroyBuffer(Buf);

        VkBufferCreateInfo CreateInfo{};
        CreateInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        CreateInfo.size        = SafeBytes;
        CreateInfo.usage       = UsageBits;
        CreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(Device, &CreateInfo, nullptr, &Buf.Handle) != VK_SUCCESS)
        {
            return false;
        }

        VkMemoryRequirements MemReq{};
        vkGetBufferMemoryRequirements(Device, Buf.Handle, &MemReq);
        const uint32_t MemType = FindHostVisibleMemory(MemReq.memoryTypeBits);
        if (MemType == UINT32_MAX)
        {
            DestroyBuffer(Buf);
            return false;
        }

        VkMemoryAllocateInfo AllocInfo{};
        AllocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        AllocInfo.allocationSize  = MemReq.size;
        AllocInfo.memoryTypeIndex = MemType;
        if (vkAllocateMemory(Device, &AllocInfo, nullptr, &Buf.Memory) != VK_SUCCESS)
        {
            DestroyBuffer(Buf);
            return false;
        }
        if (vkBindBufferMemory(Device, Buf.Handle, Buf.Memory, 0u) != VK_SUCCESS)
        {
            DestroyBuffer(Buf);
            return false;
        }
        if (vkMapMemory(Device, Buf.Memory, 0u, SafeBytes, 0u, &Buf.Mapped) != VK_SUCCESS)
        {
            DestroyBuffer(Buf);
            return false;
        }
        std::memset(Buf.Mapped, 0, static_cast<std::size_t>(SafeBytes));
        Buf.Capacity = SafeBytes;
        return true;
    }

    bool CreateComputeUnit(
        const std::filesystem::path&           SpirvPath,
        const char*                            EntryName,
        std::span<const VkDescriptorType>      SlotTypes,
        ComputeExecutableUnit&                 OutUnit,
        std::string&                           OutDiagnostic) noexcept
    {
        std::vector<uint32_t> SpirvWords;
        if (!ReadSpirvWords(SpirvPath, SpirvWords))
        {
            OutDiagnostic = "Missing particle SPIR-V compute binary: " + SpirvPath.string();
            return false;
        }

        std::vector<VkDescriptorSetLayoutBinding> Slots(SlotTypes.size());
        for (std::size_t Idx = 0u; Idx < SlotTypes.size(); ++Idx)
        {
            Slots[Idx].binding         = static_cast<uint32_t>(Idx);
            Slots[Idx].descriptorType  = SlotTypes[Idx];
            Slots[Idx].descriptorCount = 1u;
            Slots[Idx].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        }

        VkDescriptorSetLayoutCreateInfo SetInfo{};
        SetInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        SetInfo.bindingCount = static_cast<uint32_t>(Slots.size());
        SetInfo.pBindings    = Slots.data();
        if (vkCreateDescriptorSetLayout(Device, &SetInfo, nullptr, &OutUnit.DescriptorLayout) != VK_SUCCESS)
        {
            OutDiagnostic = "Failed to create particle descriptor set layout.";
            return false;
        }

        VkPipelineLayoutCreateInfo LayoutInfo{};
        LayoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        LayoutInfo.setLayoutCount = 1u;
        LayoutInfo.pSetLayouts    = &OutUnit.DescriptorLayout;
        if (vkCreatePipelineLayout(Device, &LayoutInfo, nullptr, &OutUnit.Layout) != VK_SUCCESS)
        {
            OutDiagnostic = "Failed to create particle compute layout.";
            return false;
        }

        VkShaderModuleCreateInfo ModuleInfo{};
        ModuleInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ModuleInfo.codeSize = SpirvWords.size() * sizeof(uint32_t);
        ModuleInfo.pCode    = SpirvWords.data();
        VkShaderModule ShaderModule = VK_NULL_HANDLE;
        if (vkCreateShaderModule(Device, &ModuleInfo, nullptr, &ShaderModule) != VK_SUCCESS)
        {
            OutDiagnostic = "Failed to create SPIR-V shader module: " + SpirvPath.string();
            return false;
        }

        VkComputePipelineCreateInfo PipeInfo{};
        PipeInfo.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        PipeInfo.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        PipeInfo.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        PipeInfo.stage.module = ShaderModule;
        PipeInfo.stage.pName  = EntryName;
        PipeInfo.layout       = OutUnit.Layout;

        const VkResult Res = vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1u, &PipeInfo, nullptr, &OutUnit.Executable);
        vkDestroyShaderModule(Device, ShaderModule, nullptr);
        if (Res != VK_SUCCESS)
        {
            OutDiagnostic = "Failed to construct Vulkan compute executable: " + SpirvPath.string();
            return false;
        }

        VkDescriptorSetAllocateInfo AllocDesc{};
        AllocDesc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        AllocDesc.descriptorPool     = DescriptorBank;
        AllocDesc.descriptorSetCount = 1u;
        AllocDesc.pSetLayouts        = &OutUnit.DescriptorLayout;
        if (vkAllocateDescriptorSets(Device, &AllocDesc, &OutUnit.DescriptorSlot) != VK_SUCCESS)
        {
            OutDiagnostic = "Failed to allocate particle compute descriptor slot.";
            return false;
        }
        return true;
    }

    void DestroyComputeUnit(ComputeExecutableUnit& Unit) noexcept
    {
        if (Device == VK_NULL_HANDLE)
        {
            return;
        }
        if (Unit.Executable != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(Device, Unit.Executable, nullptr);
            Unit.Executable = VK_NULL_HANDLE;
        }
        if (Unit.Layout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(Device, Unit.Layout, nullptr);
            Unit.Layout = VK_NULL_HANDLE;
        }
        if (Unit.DescriptorLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(Device, Unit.DescriptorLayout, nullptr);
            Unit.DescriptorLayout = VK_NULL_HANDLE;
        }
        Unit.DescriptorSlot = VK_NULL_HANDLE;
    }

    void WriteDescriptorSlots(
        VkDescriptorSet                                                     TargetSet,
        std::span<const std::pair<VkDescriptorType, const HostMappedBuffer*>> Entries) noexcept
    {
        std::vector<VkDescriptorBufferInfo> Infos(Entries.size());
        std::vector<VkWriteDescriptorSet>   Writes(Entries.size());
        for (std::size_t Idx = 0u; Idx < Entries.size(); ++Idx)
        {
            Infos[Idx].buffer = Entries[Idx].second->Handle;
            Infos[Idx].offset = 0u;
            Infos[Idx].range  = Entries[Idx].second->Capacity;

            Writes[Idx].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            Writes[Idx].dstSet          = TargetSet;
            Writes[Idx].dstBinding      = static_cast<uint32_t>(Idx);
            Writes[Idx].descriptorCount = 1u;
            Writes[Idx].descriptorType  = Entries[Idx].first;
            Writes[Idx].pBufferInfo     = &Infos[Idx];
        }
        vkUpdateDescriptorSets(Device, static_cast<uint32_t>(Writes.size()), Writes.data(), 0u, nullptr);
    }

    void RefreshAllDescriptorSlots(const HostMappedBuffer& ActiveStrandUniform) noexcept
    {
        const std::array<std::pair<VkDescriptorType, const HostMappedBuffer*>, 4u> ReleaseDesc = {{
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &UniformBuffer },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &KinematicsBuffer },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &LifespanBuffer },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &AppearanceBuffer },
        }};
        WriteDescriptorSlots(ReleaseUnit.DescriptorSlot, ReleaseDesc);

        const std::array<std::pair<VkDescriptorType, const HostMappedBuffer*>, 6u> AdvectDesc = {{
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &UniformBuffer },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &KinematicsBuffer },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &LifespanBuffer },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &AppearanceBuffer },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &ForceSourceBuffer },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &WaypointBuffer },
        }};
        WriteDescriptorSlots(AdvectUnit.DescriptorSlot, AdvectDesc);

        const std::array<std::pair<VkDescriptorType, const HostMappedBuffer*>, 4u> StrandDesc = {{
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &ActiveStrandUniform },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &KinematicsBuffer },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &StrandAnchorBuf },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &StrandLinkBuf },
        }};
        WriteDescriptorSlots(StrandUnit.DescriptorSlot, StrandDesc);

        const std::array<std::pair<VkDescriptorType, const HostMappedBuffer*>, 5u> ReduceDesc = {{
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &UniformBuffer },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &KinematicsBuffer },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &LifespanBuffer },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &ScratchBuffer },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &SummaryBuffer },
        }};
        WriteDescriptorSlots(ReductionAccumUnit.DescriptorSlot, ReduceDesc);
        WriteDescriptorSlots(ReductionBoundsUnit.DescriptorSlot, ReduceDesc);
        WriteDescriptorSlots(ReductionFinalUnit.DescriptorSlot, ReduceDesc);
    }

    void InsertComputeBarrier(VkCommandBuffer Cmd) const noexcept
    {
        VkMemoryBarrier Barrier{};
        Barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(
            Cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0u,
            1u,
            &Barrier,
            0u,
            nullptr,
            0u,
            nullptr);
    }

    void Cleanup() noexcept
    {
        if (Device != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(Device);
            DestroyBuffer(UniformBuffer);
            DestroyBuffer(StrandUniformBuf);
            DestroyBuffer(StrandVelUniform);
            DestroyBuffer(KinematicsBuffer);
            DestroyBuffer(LifespanBuffer);
            DestroyBuffer(AppearanceBuffer);
            DestroyBuffer(StrandAnchorBuf);
            DestroyBuffer(StrandLinkBuf);
            DestroyBuffer(ForceSourceBuffer);
            DestroyBuffer(WaypointBuffer);
            DestroyBuffer(ScratchBuffer);
            DestroyBuffer(SummaryBuffer);

            DestroyComputeUnit(ReleaseUnit);
            DestroyComputeUnit(AdvectUnit);
            DestroyComputeUnit(StrandUnit);
            DestroyComputeUnit(ReductionAccumUnit);
            DestroyComputeUnit(ReductionBoundsUnit);
            DestroyComputeUnit(ReductionFinalUnit);

            if (CompletionSignal != VK_NULL_HANDLE) { vkDestroyFence(Device, CompletionSignal, nullptr); }
            if (CommandBank != VK_NULL_HANDLE)      { vkDestroyCommandPool(Device, CommandBank, nullptr); }
            if (DescriptorBank != VK_NULL_HANDLE)   { vkDestroyDescriptorPool(Device, DescriptorBank, nullptr); }
            vkDestroyDevice(Device, nullptr);
        }
        if (Instance != VK_NULL_HANDLE)
        {
            vkDestroyInstance(Instance, nullptr);
        }
#if defined(__linux__)
        if (LoaderModule != nullptr)
        {
            dlclose(LoaderModule);
            LoaderModule = nullptr;
        }
#endif
        Instance         = VK_NULL_HANDLE;
        PhysicalDevice   = VK_NULL_HANDLE;
        Device           = VK_NULL_HANDLE;
        ComputeQueue     = VK_NULL_HANDLE;
        DescriptorBank   = VK_NULL_HANDLE;
        CommandBank      = VK_NULL_HANDLE;
        CommandRecorder  = VK_NULL_HANDLE;
        CompletionSignal = VK_NULL_HANDLE;
        SlotCapacity     = 0u;
        Initialized      = false;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                PUBLIC LIFECYCLE & DISPATCH
//------------------------------------------------------------------------------------------------------------------------

ParticleVulkanExecutor::ParticleVulkanExecutor() noexcept = default;

ParticleVulkanExecutor::~ParticleVulkanExecutor() noexcept
{
    Finalize();
}

bool ParticleVulkanExecutor::Initialize(
    const std::filesystem::path& ShaderBinaryDir,
    const std::filesystem::path& SwiftShaderIcdDir,
    std::string&                 OutDiagnostic) noexcept
{
    Finalize();
    DeviceHandle = std::make_unique<DeviceImplementation>();

#if defined(__linux__)
    const std::filesystem::path IcdJson  = SwiftShaderIcdDir / "vk_swiftshader_icd.json";
    const std::filesystem::path LoaderSo = SwiftShaderIcdDir / "libvulkan.so.1";
    if (std::filesystem::exists(IcdJson) && std::getenv("VK_ICD_FILENAMES") == nullptr)
    {
        const std::string AbsoluteIcd = std::filesystem::weakly_canonical(IcdJson).string();
        setenv("VK_ICD_FILENAMES", AbsoluteIcd.c_str(), 0);
        setenv("VK_DRIVER_FILES", AbsoluteIcd.c_str(), 0);
    }
#else
    (void)SwiftShaderIcdDir;
#endif

    if (volkInitialize() != VK_SUCCESS)
    {
#if defined(__linux__)
        if (std::filesystem::exists(LoaderSo))
        {
            const std::string LoaderPath = std::filesystem::weakly_canonical(LoaderSo).string();
            DeviceHandle->LoaderModule   = dlopen(LoaderPath.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (DeviceHandle->LoaderModule != nullptr)
            {
                auto GetInstProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
                    dlsym(DeviceHandle->LoaderModule, "vkGetInstanceProcAddr"));
                if (GetInstProc != nullptr)
                {
                    volkInitializeCustom(GetInstProc);
                }
            }
        }
#endif
    }

    if (vkCreateInstance == nullptr)
    {
        OutDiagnostic = "Vulkan loader unavailable and SwiftShader fallback could not load.";
        Finalize();
        return false;
    }

    VkApplicationInfo AppInfo{};
    AppInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    AppInfo.pApplicationName   = "FrontierParticleCompute";
    AppInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    AppInfo.pEngineName        = "Frontier";
    AppInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    AppInfo.apiVersion         = VK_API_VERSION_1_3;

    VkInstanceCreateInfo InstInfo{};
    InstInfo.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    InstInfo.pApplicationInfo = &AppInfo;
    if (vkCreateInstance(&InstInfo, nullptr, &DeviceHandle->Instance) != VK_SUCCESS)
    {
        OutDiagnostic = "vkCreateInstance failed for ParticleVulkanExecutor.";
        Finalize();
        return false;
    }
    volkLoadInstanceOnly(DeviceHandle->Instance);

    uint32_t PhysCount = 0u;
    vkEnumeratePhysicalDevices(DeviceHandle->Instance, &PhysCount, nullptr);
    if (PhysCount == 0u)
    {
        OutDiagnostic = "No Vulkan physical devices enumerated.";
        Finalize();
        return false;
    }
    std::vector<VkPhysicalDevice> Devices(PhysCount);
    vkEnumeratePhysicalDevices(DeviceHandle->Instance, &PhysCount, Devices.data());
    DeviceHandle->PhysicalDevice = Devices.front();

    VkPhysicalDeviceProperties Props{};
    vkGetPhysicalDeviceProperties(DeviceHandle->PhysicalDevice, &Props);
    DeviceHandle->DeviceLabel = Props.deviceName;

    uint32_t FamCount = 0u;
    vkGetPhysicalDeviceQueueFamilyProperties(DeviceHandle->PhysicalDevice, &FamCount, nullptr);
    std::vector<VkQueueFamilyProperties> Families(FamCount);
    vkGetPhysicalDeviceQueueFamilyProperties(DeviceHandle->PhysicalDevice, &FamCount, Families.data());

    uint32_t ChosenFamily = UINT32_MAX;
    for (uint32_t Idx = 0u; Idx < FamCount; ++Idx)
    {
        if ((Families[Idx].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
            (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
        {
            ChosenFamily = Idx;
            break;
        }
    }
    if (ChosenFamily == UINT32_MAX)
    {
        for (uint32_t Idx = 0u; Idx < FamCount; ++Idx)
        {
            if ((Families[Idx].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0u)
            {
                ChosenFamily = Idx;
                break;
            }
        }
    }
    if (ChosenFamily == UINT32_MAX)
    {
        OutDiagnostic = "No compute-capable Vulkan queue family found.";
        Finalize();
        return false;
    }
    DeviceHandle->QueueFamilyIndex = ChosenFamily;

    const float QueuePriority = 1.0f;
    VkDeviceQueueCreateInfo QueueInfo{};
    QueueInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    QueueInfo.queueFamilyIndex = ChosenFamily;
    QueueInfo.queueCount       = 1u;
    QueueInfo.pQueuePriorities = &QueuePriority;

    VkDeviceCreateInfo DevInfo{};
    DevInfo.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    DevInfo.queueCreateInfoCount = 1u;
    DevInfo.pQueueCreateInfos    = &QueueInfo;
    if (vkCreateDevice(DeviceHandle->PhysicalDevice, &DevInfo, nullptr, &DeviceHandle->Device) != VK_SUCCESS)
    {
        OutDiagnostic = "vkCreateDevice failed for ParticleVulkanExecutor.";
        Finalize();
        return false;
    }
    volkLoadDevice(DeviceHandle->Device);
    vkGetDeviceQueue(DeviceHandle->Device, ChosenFamily, 0u, &DeviceHandle->ComputeQueue);

    // Descriptor bank sized for 6 compute units
    const std::array<VkDescriptorPoolSize, 2u> DescSizes = {{
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 12u },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 36u },
    }};
    VkDescriptorPoolCreateInfo BankInfo{};
    BankInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    BankInfo.maxSets       = 12u;
    BankInfo.poolSizeCount = static_cast<uint32_t>(DescSizes.size());
    BankInfo.pPoolSizes    = DescSizes.data();
    if (vkCreateDescriptorPool(DeviceHandle->Device, &BankInfo, nullptr, &DeviceHandle->DescriptorBank) != VK_SUCCESS)
    {
        OutDiagnostic = "Failed to create particle descriptor bank.";
        Finalize();
        return false;
    }

    VkCommandPoolCreateInfo CmdBankInfo{};
    CmdBankInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    CmdBankInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    CmdBankInfo.queueFamilyIndex = ChosenFamily;
    if (vkCreateCommandPool(DeviceHandle->Device, &CmdBankInfo, nullptr, &DeviceHandle->CommandBank) != VK_SUCCESS)
    {
        OutDiagnostic = "Failed to create particle command bank.";
        Finalize();
        return false;
    }

    VkCommandBufferAllocateInfo CmdAlloc{};
    CmdAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    CmdAlloc.commandPool        = DeviceHandle->CommandBank;
    CmdAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    CmdAlloc.commandBufferCount = 1u;
    if (vkAllocateCommandBuffers(DeviceHandle->Device, &CmdAlloc, &DeviceHandle->CommandRecorder) != VK_SUCCESS)
    {
        OutDiagnostic = "Failed to allocate particle compute command buffer.";
        Finalize();
        return false;
    }

    VkFenceCreateInfo SignalInfo{};
    SignalInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(DeviceHandle->Device, &SignalInfo, nullptr, &DeviceHandle->CompletionSignal) != VK_SUCCESS)
    {
        OutDiagnostic = "Failed to create particle completion signal.";
        Finalize();
        return false;
    }

    // Load all 6 SPIR-V 1.3 compute modules
    const std::array<VkDescriptorType, 4u> ReleaseTypes = {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    };
    const std::array<VkDescriptorType, 6u> AdvectTypes = {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    };
    const std::array<VkDescriptorType, 4u> StrandTypes = {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    };
    const std::array<VkDescriptorType, 5u> ReduceTypes = {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
    };

    if (!DeviceHandle->CreateComputeUnit(ShaderBinaryDir / "ParticleReleaseCompute.spv", "ComputeParticleRelease", ReleaseTypes, DeviceHandle->ReleaseUnit, OutDiagnostic) ||
        !DeviceHandle->CreateComputeUnit(ShaderBinaryDir / "ParticleAdvectCompute.spv", "ComputeParticleAdvect", AdvectTypes, DeviceHandle->AdvectUnit, OutDiagnostic) ||
        !DeviceHandle->CreateComputeUnit(ShaderBinaryDir / "ParticleStrandConstraintCompute.spv", "ComputeStrandConstraint", StrandTypes, DeviceHandle->StrandUnit, OutDiagnostic) ||
        !DeviceHandle->CreateComputeUnit(ShaderBinaryDir / "ParticleReductionAccumulate.spv", "ComputeParticleReductionAccumulate", ReduceTypes, DeviceHandle->ReductionAccumUnit, OutDiagnostic) ||
        !DeviceHandle->CreateComputeUnit(ShaderBinaryDir / "ParticleReductionBounds.spv", "ComputeParticleReductionBounds", ReduceTypes, DeviceHandle->ReductionBoundsUnit, OutDiagnostic) ||
        !DeviceHandle->CreateComputeUnit(ShaderBinaryDir / "ParticleReductionFinalize.spv", "ComputeParticleReductionFinalize", ReduceTypes, DeviceHandle->ReductionFinalUnit, OutDiagnostic))
    {
        Finalize();
        return false;
    }

    // Allocate constant-size buffers & initial default capacity
    const VkBufferUsageFlags UboUsage  = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    const VkBufferUsageFlags SsboUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (!DeviceHandle->EnsureBuffer(DeviceHandle->UniformBuffer, sizeof(ParticleGpuUniformBlock), UboUsage) ||
        !DeviceHandle->EnsureBuffer(DeviceHandle->StrandUniformBuf, sizeof(ParticleGpuUniformBlock), UboUsage) ||
        !DeviceHandle->EnsureBuffer(DeviceHandle->StrandVelUniform, sizeof(ParticleGpuUniformBlock), UboUsage) ||
        !DeviceHandle->EnsureBuffer(DeviceHandle->ForceSourceBuffer, sizeof(ForceFields::ForceSourceShaderEntry) * 64u, SsboUsage) ||
        !DeviceHandle->EnsureBuffer(DeviceHandle->WaypointBuffer, sizeof(float) * 4u * 256u, SsboUsage) ||
        !DeviceHandle->EnsureBuffer(DeviceHandle->ScratchBuffer, 64u, SsboUsage) ||
        !DeviceHandle->EnsureBuffer(DeviceHandle->SummaryBuffer, sizeof(ParticleReadbackSummary), SsboUsage))
    {
        OutDiagnostic = "Failed to allocate base Vulkan uniform/staging buffers.";
        Finalize();
        return false;
    }

    DeviceHandle->Initialized = true;
    if (!EnsureChannelCapacity(256u, true, OutDiagnostic))
    {
        Finalize();
        return false;
    }
    return true;
}

void ParticleVulkanExecutor::Finalize() noexcept
{
    if (DeviceHandle)
    {
        DeviceHandle->Cleanup();
        DeviceHandle.reset();
    }
}

bool ParticleVulkanExecutor::QueryDeviceReady() const noexcept
{
    return DeviceHandle && DeviceHandle->Initialized;
}

std::string ParticleVulkanExecutor::QueryDeviceName() const
{
    return DeviceHandle ? DeviceHandle->DeviceLabel : std::string{};
}

void* ParticleVulkanExecutor::QueryNativeInstance() const noexcept
{
    return DeviceHandle ? static_cast<void*>(DeviceHandle->Instance) : nullptr;
}

void* ParticleVulkanExecutor::QueryNativePhysicalDevice() const noexcept
{
    return DeviceHandle ? static_cast<void*>(DeviceHandle->PhysicalDevice) : nullptr;
}

void* ParticleVulkanExecutor::QueryNativeDevice() const noexcept
{
    return DeviceHandle ? static_cast<void*>(DeviceHandle->Device) : nullptr;
}

void* ParticleVulkanExecutor::QueryNativeQueue() const noexcept
{
    return DeviceHandle ? static_cast<void*>(DeviceHandle->ComputeQueue) : nullptr;
}

uint32_t ParticleVulkanExecutor::QueryQueueFamilyIndex() const noexcept
{
    return DeviceHandle ? DeviceHandle->QueueFamilyIndex : 0u;
}

uint32_t ParticleVulkanExecutor::QueryAllocatedCapacity() const noexcept
{
    return DeviceHandle ? DeviceHandle->SlotCapacity : 0u;
}

void* ParticleVulkanExecutor::QueryKinematicsBufferHandle() const noexcept
{
    return DeviceHandle ? static_cast<void*>(DeviceHandle->KinematicsBuffer.Handle) : nullptr;
}

void* ParticleVulkanExecutor::QueryLifespanBufferHandle() const noexcept
{
    return DeviceHandle ? static_cast<void*>(DeviceHandle->LifespanBuffer.Handle) : nullptr;
}

void* ParticleVulkanExecutor::QueryAppearanceBufferHandle() const noexcept
{
    return DeviceHandle ? static_cast<void*>(DeviceHandle->AppearanceBuffer.Handle) : nullptr;
}

void* ParticleVulkanExecutor::QueryStrandLinkBufferHandle() const noexcept
{
    return DeviceHandle ? static_cast<void*>(DeviceHandle->StrandLinkBuf.Handle) : nullptr;
}

uint64_t ParticleVulkanExecutor::QueryKinematicsByteSize() const noexcept
{
    return DeviceHandle ? static_cast<uint64_t>(DeviceHandle->KinematicsBuffer.Capacity) : 0u;
}

uint64_t ParticleVulkanExecutor::QueryLifespanByteSize() const noexcept
{
    return DeviceHandle ? static_cast<uint64_t>(DeviceHandle->LifespanBuffer.Capacity) : 0u;
}

uint64_t ParticleVulkanExecutor::QueryAppearanceByteSize() const noexcept
{
    return DeviceHandle ? static_cast<uint64_t>(DeviceHandle->AppearanceBuffer.Capacity) : 0u;
}

uint64_t ParticleVulkanExecutor::QueryStrandLinkByteSize() const noexcept
{
    return DeviceHandle ? static_cast<uint64_t>(DeviceHandle->StrandLinkBuf.Capacity) : 0u;
}

bool ParticleVulkanExecutor::EnsureChannelCapacity(
    uint32_t     SlotCapacity,
    bool         IncludeStrandChannels,
    std::string& OutDiagnostic) noexcept
{
    if (!QueryDeviceReady())
    {
        OutDiagnostic = "ParticleVulkanExecutor is not initialized.";
        return false;
    }

    const uint32_t SafeSlots = std::max(64u, SlotCapacity);
    const VkBufferUsageFlags SsboUsage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    if (!DeviceHandle->EnsureBuffer(DeviceHandle->KinematicsBuffer, static_cast<VkDeviceSize>(SafeSlots) * sizeof(KinematicsChannel), SsboUsage) ||
        !DeviceHandle->EnsureBuffer(DeviceHandle->LifespanBuffer, static_cast<VkDeviceSize>(SafeSlots) * sizeof(LifespanChannel), SsboUsage) ||
        !DeviceHandle->EnsureBuffer(DeviceHandle->AppearanceBuffer, static_cast<VkDeviceSize>(SafeSlots) * sizeof(AppearanceChannel), SsboUsage))
    {
        OutDiagnostic = "Failed to allocate primary particle channel SSBOs.";
        return false;
    }

    const uint32_t StrandSlots = IncludeStrandChannels ? SafeSlots : 16u;
    if (!DeviceHandle->EnsureBuffer(DeviceHandle->StrandAnchorBuf, static_cast<VkDeviceSize>(StrandSlots) * sizeof(StrandAnchorChannel), SsboUsage) ||
        !DeviceHandle->EnsureBuffer(DeviceHandle->StrandLinkBuf, static_cast<VkDeviceSize>(StrandSlots) * sizeof(StrandConnectivityChannel), SsboUsage))
    {
        OutDiagnostic = "Failed to allocate particle strand SSBOs.";
        return false;
    }

    DeviceHandle->SlotCapacity = std::max(DeviceHandle->SlotCapacity, SafeSlots);
    DeviceHandle->RefreshAllDescriptorSlots(DeviceHandle->StrandUniformBuf);
    return true;
}

bool ParticleVulkanExecutor::UploadChannelBank(
    const ParticleChannelBank& SourceBank,
    std::string&               OutDiagnostic) noexcept
{
    if (!QueryDeviceReady())
    {
        OutDiagnostic = "ParticleVulkanExecutor is not initialized.";
        return false;
    }

    const uint32_t Count          = std::max(SourceBank.QueryActiveCount(), SourceBank.QueryCapacity());
    const auto     StrandAnchors  = SourceBank.QueryChannel(ChannelLayouts::StrandAnchor);
    const auto     StrandLinks    = SourceBank.QueryChannel(ChannelLayouts::StrandConnectivity);
    const bool     HasStrands     = !StrandAnchors.empty() && !StrandLinks.empty();

    if (!EnsureChannelCapacity(Count, HasStrands, OutDiagnostic))
    {
        return false;
    }

    const auto Kin    = SourceBank.QueryChannel(ChannelLayouts::Kinematics);
    const auto Life   = SourceBank.QueryChannel(ChannelLayouts::Lifespan);
    const auto Appear = SourceBank.QueryChannel(ChannelLayouts::Appearance);

    if (!Kin.empty())
    {
        std::memcpy(DeviceHandle->KinematicsBuffer.Mapped, Kin.data(), Kin.size() * sizeof(KinematicsChannel));
    }
    if (!Life.empty())
    {
        std::memcpy(DeviceHandle->LifespanBuffer.Mapped, Life.data(), Life.size() * sizeof(LifespanChannel));
    }
    else
    {
        // Ensure active slots carry LivenessMask = 1 when Lifespan channel is optional (e.g., immortal strands)
        auto* DstLife = reinterpret_cast<LifespanChannel*>(DeviceHandle->LifespanBuffer.Mapped);
        for (uint32_t Idx = 0u; Idx < Count; ++Idx)
        {
            DstLife[Idx] = LifespanChannel{ 0.0f, 0.0f, (Idx + 1u) * 2654435761u, (Idx < SourceBank.QueryActiveCount()) ? 1u : 0u };
        }
    }
    if (!Appear.empty())
    {
        std::memcpy(DeviceHandle->AppearanceBuffer.Mapped, Appear.data(), Appear.size() * sizeof(AppearanceChannel));
    }
    if (HasStrands)
    {
        std::memcpy(DeviceHandle->StrandAnchorBuf.Mapped, StrandAnchors.data(), StrandAnchors.size() * sizeof(StrandAnchorChannel));
        std::memcpy(DeviceHandle->StrandLinkBuf.Mapped, StrandLinks.data(), StrandLinks.size() * sizeof(StrandConnectivityChannel));
    }
    return true;
}

bool ParticleVulkanExecutor::DownloadChannelBank(
    ParticleChannelBank& TargetBank,
    std::string&         OutDiagnostic) noexcept
{
    if (!QueryDeviceReady())
    {
        OutDiagnostic = "ParticleVulkanExecutor is not initialized.";
        return false;
    }

    auto Kin    = TargetBank.AccessChannel(ChannelLayouts::Kinematics);
    auto Life   = TargetBank.AccessChannel(ChannelLayouts::Lifespan);
    auto Appear = TargetBank.AccessChannel(ChannelLayouts::Appearance);
    auto Anchor = TargetBank.AccessChannel(ChannelLayouts::StrandAnchor);

    if (!Kin.empty())
    {
        const std::size_t CopyBytes = std::min<std::size_t>(Kin.size() * sizeof(KinematicsChannel), static_cast<std::size_t>(DeviceHandle->KinematicsBuffer.Capacity));
        std::memcpy(Kin.data(), DeviceHandle->KinematicsBuffer.Mapped, CopyBytes);
    }
    if (!Life.empty())
    {
        const std::size_t CopyBytes = std::min<std::size_t>(Life.size() * sizeof(LifespanChannel), static_cast<std::size_t>(DeviceHandle->LifespanBuffer.Capacity));
        std::memcpy(Life.data(), DeviceHandle->LifespanBuffer.Mapped, CopyBytes);
    }
    if (!Appear.empty())
    {
        const std::size_t CopyBytes = std::min<std::size_t>(Appear.size() * sizeof(AppearanceChannel), static_cast<std::size_t>(DeviceHandle->AppearanceBuffer.Capacity));
        std::memcpy(Appear.data(), DeviceHandle->AppearanceBuffer.Mapped, CopyBytes);
    }
    if (!Anchor.empty())
    {
        const std::size_t CopyBytes = std::min<std::size_t>(Anchor.size() * sizeof(StrandAnchorChannel), static_cast<std::size_t>(DeviceHandle->StrandAnchorBuf.Capacity));
        std::memcpy(Anchor.data(), DeviceHandle->StrandAnchorBuf.Mapped, CopyBytes);
    }
    return true;
}

bool ParticleVulkanExecutor::DispatchStep(
    const ParticleGpuStepRequest& Request,
    ParticleReadbackSummary&      OutSummary,
    std::string&                  OutDiagnostic) noexcept
{
    if (!QueryDeviceReady())
    {
        OutDiagnostic = "ParticleVulkanExecutor is not initialized.";
        return false;
    }

    const auto StartWall = std::chrono::steady_clock::now();

    const uint32_t NeededSlots = std::max({
        Request.Uniforms.Capacity,
        Request.Uniforms.ActiveCount,
        Request.Uniforms.ReleaseStartSlot + Request.Uniforms.ReleaseCount,
        64u
    });
    if (!EnsureChannelCapacity(NeededSlots, Request.StrandRelaxIterations > 0u, OutDiagnostic))
    {
        return false;
    }

    // Upload ForceFields sources & waypoints SSBOs
    const VkBufferUsageFlags SsboUsage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (!Request.PackedForceSources.empty())
    {
        const VkDeviceSize SrcBytes = Request.PackedForceSources.size() * sizeof(ForceFields::ForceSourceShaderEntry);
        if (!DeviceHandle->EnsureBuffer(DeviceHandle->ForceSourceBuffer, SrcBytes, SsboUsage))
        {
            OutDiagnostic = "Failed to grow ForceSource SSBO.";
            return false;
        }
        std::memcpy(DeviceHandle->ForceSourceBuffer.Mapped, Request.PackedForceSources.data(), static_cast<std::size_t>(SrcBytes));
    }
    if (!Request.PackedForceWaypoints.empty())
    {
        const VkDeviceSize WpBytes = Request.PackedForceWaypoints.size() * sizeof(float) * 4u;
        if (!DeviceHandle->EnsureBuffer(DeviceHandle->WaypointBuffer, WpBytes, SsboUsage))
        {
            OutDiagnostic = "Failed to grow ForceWaypoint SSBO.";
            return false;
        }
        auto* DstWp = reinterpret_cast<float*>(DeviceHandle->WaypointBuffer.Mapped);
        for (std::size_t Idx = 0u; Idx < Request.PackedForceWaypoints.size(); ++Idx)
        {
            DstWp[Idx * 4u + 0u] = Request.PackedForceWaypoints[Idx].X;
            DstWp[Idx * 4u + 1u] = Request.PackedForceWaypoints[Idx].Y;
            DstWp[Idx * 4u + 2u] = Request.PackedForceWaypoints[Idx].Z;
            DstWp[Idx * 4u + 3u] = 0.0f;
        }
    }

    ParticleGpuUniformBlock StepUniform = Request.Uniforms;
    StepUniform.Capacity    = DeviceHandle->SlotCapacity;
    StepUniform.SourceCount = static_cast<uint32_t>(Request.PackedForceSources.size());
    std::memcpy(DeviceHandle->UniformBuffer.Mapped, &StepUniform, sizeof(ParticleGpuUniformBlock));

    // Helper lambdas to submit command buffer batches cleanly (necessary when Strand sub-mode uniform swaps)
    auto SubmitCommands = [&](const auto& RecordFn) -> bool
    {
        vkResetCommandBuffer(DeviceHandle->CommandRecorder, 0u);
        VkCommandBufferBeginInfo BeginInfo{};
        BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(DeviceHandle->CommandRecorder, &BeginInfo) != VK_SUCCESS)
        {
            return false;
        }

        RecordFn(DeviceHandle->CommandRecorder);

        if (vkEndCommandBuffer(DeviceHandle->CommandRecorder) != VK_SUCCESS)
        {
            return false;
        }

        vkResetFences(DeviceHandle->Device, 1u, &DeviceHandle->CompletionSignal);
        VkSubmitInfo SubInfo{};
        SubInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        SubInfo.commandBufferCount = 1u;
        SubInfo.pCommandBuffers    = &DeviceHandle->CommandRecorder;
        if (vkQueueSubmit(DeviceHandle->ComputeQueue, 1u, &SubInfo, DeviceHandle->CompletionSignal) != VK_SUCCESS)
        {
            return false;
        }
        return vkWaitForFences(DeviceHandle->Device, 1u, &DeviceHandle->CompletionSignal, VK_TRUE, 15'000'000'000ull) == VK_SUCCESS;
    };

    const uint32_t ActiveGroups = std::max(1u, (StepUniform.ActiveCount + WorkgroupWidth - 1u) / WorkgroupWidth);

    // Phase A: Optional Strand Verlet Snapshot (before Advect)
    if (Request.StrandRelaxIterations > 0u && StepUniform.ActiveCount > 0u)
    {
        ParticleGpuUniformBlock SetupUniform = StepUniform;
        SetupUniform.ColliderFlags[2]        = 1u; // StrandModeVerletSetup
        std::memcpy(DeviceHandle->StrandUniformBuf.Mapped, &SetupUniform, sizeof(ParticleGpuUniformBlock));
        DeviceHandle->RefreshAllDescriptorSlots(DeviceHandle->StrandUniformBuf);

        if (!SubmitCommands([&](VkCommandBuffer Cmd) {
                vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, DeviceHandle->StrandUnit.Executable);
                vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, DeviceHandle->StrandUnit.Layout, 0u, 1u, &DeviceHandle->StrandUnit.DescriptorSlot, 0u, nullptr);
                vkCmdDispatch(Cmd, ActiveGroups, 1u, 1u);
            }))
        {
            OutDiagnostic = "Vulkan strand Verlet setup dispatch failed.";
            return false;
        }
    }

    // Phase B: Optional Release -> Advect -> Optional Strand Relax (single batch!)
    ParticleGpuUniformBlock RelaxUniform = StepUniform;
    RelaxUniform.ColliderFlags[2]        = 2u; // StrandModeRelax
    std::memcpy(DeviceHandle->StrandUniformBuf.Mapped, &RelaxUniform, sizeof(ParticleGpuUniformBlock));
    DeviceHandle->RefreshAllDescriptorSlots(DeviceHandle->StrandUniformBuf);

    if (!SubmitCommands([&](VkCommandBuffer Cmd) {
            if (StepUniform.ReleaseCount > 0u)
            {
                const uint32_t RelGroups = (StepUniform.ReleaseCount + WorkgroupWidth - 1u) / WorkgroupWidth;
                vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, DeviceHandle->ReleaseUnit.Executable);
                vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, DeviceHandle->ReleaseUnit.Layout, 0u, 1u, &DeviceHandle->ReleaseUnit.DescriptorSlot, 0u, nullptr);
                vkCmdDispatch(Cmd, RelGroups, 1u, 1u);
                DeviceHandle->InsertComputeBarrier(Cmd);
            }

            if (StepUniform.ActiveCount > 0u)
            {
                vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, DeviceHandle->AdvectUnit.Executable);
                vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, DeviceHandle->AdvectUnit.Layout, 0u, 1u, &DeviceHandle->AdvectUnit.DescriptorSlot, 0u, nullptr);
                vkCmdDispatch(Cmd, ActiveGroups, 1u, 1u);
                DeviceHandle->InsertComputeBarrier(Cmd);

                if (Request.StrandRelaxIterations > 0u)
                {
                    vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, DeviceHandle->StrandUnit.Executable);
                    vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, DeviceHandle->StrandUnit.Layout, 0u, 1u, &DeviceHandle->StrandUnit.DescriptorSlot, 0u, nullptr);
                    for (uint32_t Iter = 0u; Iter < Request.StrandRelaxIterations; ++Iter)
                    {
                        vkCmdDispatch(Cmd, ActiveGroups, 1u, 1u);
                        DeviceHandle->InsertComputeBarrier(Cmd);
                    }
                }
            }
        }))
    {
        OutDiagnostic = "Vulkan particle release/advect compute dispatch failed.";
        return false;
    }

    // Phase C: Optional Strand Velocity Derivation + 32-Byte Reduction Readback
    if (Request.StrandRelaxIterations > 0u && StepUniform.ActiveCount > 0u)
    {
        ParticleGpuUniformBlock DeriveUniform = StepUniform;
        DeriveUniform.ColliderFlags[2]        = 3u; // StrandModeDeriveVel
        std::memcpy(DeviceHandle->StrandVelUniform.Mapped, &DeriveUniform, sizeof(ParticleGpuUniformBlock));
        DeviceHandle->RefreshAllDescriptorSlots(DeviceHandle->StrandVelUniform);
    }

    if (!SubmitCommands([&](VkCommandBuffer Cmd) {
            if (Request.StrandRelaxIterations > 0u && StepUniform.ActiveCount > 0u)
            {
                vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, DeviceHandle->StrandUnit.Executable);
                vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, DeviceHandle->StrandUnit.Layout, 0u, 1u, &DeviceHandle->StrandUnit.DescriptorSlot, 0u, nullptr);
                vkCmdDispatch(Cmd, ActiveGroups, 1u, 1u);
                DeviceHandle->InsertComputeBarrier(Cmd);
            }

            // Clear 32-byte reduction scratch buffer on GPU
            vkCmdFillBuffer(Cmd, DeviceHandle->ScratchBuffer.Handle, 0u, 32u, 0u);
            DeviceHandle->InsertComputeBarrier(Cmd);

            if (StepUniform.ActiveCount > 0u)
            {
                vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, DeviceHandle->ReductionAccumUnit.Executable);
                vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, DeviceHandle->ReductionAccumUnit.Layout, 0u, 1u, &DeviceHandle->ReductionAccumUnit.DescriptorSlot, 0u, nullptr);
                vkCmdDispatch(Cmd, ActiveGroups, 1u, 1u);
                DeviceHandle->InsertComputeBarrier(Cmd);

                vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, DeviceHandle->ReductionBoundsUnit.Executable);
                vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, DeviceHandle->ReductionBoundsUnit.Layout, 0u, 1u, &DeviceHandle->ReductionBoundsUnit.DescriptorSlot, 0u, nullptr);
                vkCmdDispatch(Cmd, ActiveGroups, 1u, 1u);
                DeviceHandle->InsertComputeBarrier(Cmd);
            }

            vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, DeviceHandle->ReductionFinalUnit.Executable);
            vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, DeviceHandle->ReductionFinalUnit.Layout, 0u, 1u, &DeviceHandle->ReductionFinalUnit.DescriptorSlot, 0u, nullptr);
            vkCmdDispatch(Cmd, 1u, 1u, 1u);
        }))
    {
        OutDiagnostic = "Vulkan particle 32-byte reduction compute dispatch failed.";
        return false;
    }

    // Read ONLY the 32-byte host-visible summary!
    std::memcpy(&OutSummary, DeviceHandle->SummaryBuffer.Mapped, sizeof(ParticleReadbackSummary));
    const auto EndWall = std::chrono::steady_clock::now();
    OutSummary.TickDurationUs = static_cast<float>(
        std::chrono::duration<double, std::micro>(EndWall - StartWall).count());
    return true;
}

#endif // FRONTIER_WITH_VULKAN

} // namespace Frontier::SimulationSpace::Particles
