/*==========================================================================================================================================
                                                TYRESOLIDVULKANVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Execute Engine Hex8 Slang as direct Vulkan SPIR-V, then compare every particle with the CPU solid.
#include "Engine/Physics/TyreProjectionDevice.h"
#include "Engine/Physics/TyreProjectionVulkan.h"
#include "Engine/Physics/TyreSolidDevice.h"
#include "Engine/Physics/TyreSurfaceProjection.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace Frontier;
void Require(bool Condition, const char *Message)
{
    if (!Condition) throw std::runtime_error(Message);
}
void Confirm(VkResult Status, const char *Operation)
{
    if (Status != VK_SUCCESS) throw std::runtime_error(std::string(Operation) + " failed: " + std::to_string(Status));
}
struct VulkanBuffer
{
    VkBuffer Handle = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    void *Mapped = nullptr;
};
struct alignas(16) SurfaceProbe
{
    float Position[4]{}, Normal[4]{}, Deformation[4]{};
    std::uint32_t Controls[4]{};
};
struct alignas(16) SurfaceResult { float Position[4]{}, Normal[4]{}; };
struct alignas(16) TyreStressProbe { float F[3][4]{}, Moduli[4]{}; };
struct alignas(16) TyreStressSample { float P[3][4]{}; };
static_assert(sizeof(SurfaceProbe) == 64 && sizeof(SurfaceResult) == 32);
static_assert(sizeof(TyreStressProbe) == 64 && sizeof(TyreStressSample) == 48);
constexpr std::uint32_t SurfaceCount = 4096, StressCount = 64;
struct SolidVulkanValidation
{
    VkInstance Instance = VK_NULL_HANDLE;
    VkPhysicalDevice Physical = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    VkQueue Queue = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties Properties{};
    VkDescriptorSetLayout DescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet Descriptors = VK_NULL_HANDLE, SurfaceDescriptors = VK_NULL_HANDLE, StressDescriptors = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkPipeline ResetPipeline = VK_NULL_HANDLE, AdvancePipeline = VK_NULL_HANDLE, SurfacePipeline = VK_NULL_HANDLE,
               StressPipeline = VK_NULL_HANDLE;
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    VkCommandBuffer Commands = VK_NULL_HANDLE;
    VulkanBuffer CommandBuffer, ParticleBuffer, ResultBuffer, CellBuffer, SurfaceProbeBuffer, SurfaceResultBuffer,
                 StressProbeBuffer, StressSampleBuffer;
    std::uint32_t QueueFamily = 0;

    ~SolidVulkanValidation()
    {
        if (Device) vkDeviceWaitIdle(Device);
        if (Device && CommandPool) vkDestroyCommandPool(Device, CommandPool, nullptr);
        if (Device && ResetPipeline) vkDestroyPipeline(Device, ResetPipeline, nullptr);
        if (Device && AdvancePipeline) vkDestroyPipeline(Device, AdvancePipeline, nullptr);
        if (Device && SurfacePipeline) vkDestroyPipeline(Device, SurfacePipeline, nullptr);
        if (Device && StressPipeline) vkDestroyPipeline(Device, StressPipeline, nullptr);
        if (Device && PipelineLayout) vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
        if (Device && DescriptorPool) vkDestroyDescriptorPool(Device, DescriptorPool, nullptr);
        if (Device && DescriptorLayout) vkDestroyDescriptorSetLayout(Device, DescriptorLayout, nullptr);
        for (VulkanBuffer *Region : {&CommandBuffer, &ParticleBuffer, &ResultBuffer, &CellBuffer,
                                     &SurfaceProbeBuffer, &SurfaceResultBuffer, &StressProbeBuffer, &StressSampleBuffer})
        {
            if (Device && Region->Mapped) vkUnmapMemory(Device, Region->Memory);
            if (Device && Region->Handle) vkDestroyBuffer(Device, Region->Handle, nullptr);
            if (Device && Region->Memory) vkFreeMemory(Device, Region->Memory, nullptr);
        }
        if (Device) vkDestroyDevice(Device, nullptr);
        if (Instance) vkDestroyInstance(Instance, nullptr);
    }

    void InitializeDevice()
    {
        VkApplicationInfo App{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        App.pApplicationName = "Frontier Hex8 tyre validation";
        App.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo InstanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        InstanceInfo.pApplicationInfo = &App;
        Confirm(vkCreateInstance(&InstanceInfo, nullptr, &Instance), "vkCreateInstance");
        std::uint32_t Count = 0;
        Confirm(vkEnumeratePhysicalDevices(Instance, &Count, nullptr), "vkEnumeratePhysicalDevices count");
        Require(Count != 0, "No Vulkan device");
        std::vector<VkPhysicalDevice> Devices(Count);
        Confirm(vkEnumeratePhysicalDevices(Instance, &Count, Devices.data()), "vkEnumeratePhysicalDevices");
        Physical = Devices[0];
        vkGetPhysicalDeviceProperties(Physical, &Properties);
        std::uint32_t FamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(Physical, &FamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> Families(FamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(Physical, &FamilyCount, Families.data());
        bool Found = false;
        for (std::uint32_t I = 0; I < FamilyCount; ++I)
            if (Families[I].queueCount && (Families[I].queueFlags & VK_QUEUE_COMPUTE_BIT))
            { QueueFamily = I; Found = true; break; }
        Require(Found, "No Vulkan compute queue");
        const float Priority = 1.0f;
        VkDeviceQueueCreateInfo QueueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        QueueInfo.queueFamilyIndex = QueueFamily;
        QueueInfo.queueCount = 1;
        QueueInfo.pQueuePriorities = &Priority;
        VkDeviceCreateInfo DeviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        DeviceInfo.queueCreateInfoCount = 1;
        DeviceInfo.pQueueCreateInfos = &QueueInfo;
        Confirm(vkCreateDevice(Physical, &DeviceInfo, nullptr, &Device), "vkCreateDevice");
        vkGetDeviceQueue(Device, QueueFamily, 0, &Queue);
        std::printf("VULKAN device=%s type=%u api=%u.%u.%u\n", Properties.deviceName,
                    unsigned(Properties.deviceType), VK_VERSION_MAJOR(Properties.apiVersion),
                    VK_VERSION_MINOR(Properties.apiVersion), VK_VERSION_PATCH(Properties.apiVersion));
    }

    void InitializeBuffer(VulkanBuffer &Region, VkDeviceSize Size)
    {
        VkBufferCreateInfo Info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        Info.size = Size;
        Info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        Info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        Confirm(vkCreateBuffer(Device, &Info, nullptr, &Region.Handle), "vkCreateBuffer");
        VkMemoryRequirements Requirements{};
        vkGetBufferMemoryRequirements(Device, Region.Handle, &Requirements);
        VkPhysicalDeviceMemoryProperties Types{};
        vkGetPhysicalDeviceMemoryProperties(Physical, &Types);
        std::uint32_t Chosen = Types.memoryTypeCount;
        const auto Flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (std::uint32_t I = 0; I < Types.memoryTypeCount; ++I)
            if ((Requirements.memoryTypeBits & (1u << I)) && (Types.memoryTypes[I].propertyFlags & Flags) == Flags)
            { Chosen = I; break; }
        Require(Chosen != Types.memoryTypeCount, "No coherent host-visible Vulkan memory");
        VkMemoryAllocateInfo Allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        Allocation.allocationSize = Requirements.size;
        Allocation.memoryTypeIndex = Chosen;
        Confirm(vkAllocateMemory(Device, &Allocation, nullptr, &Region.Memory), "vkAllocateMemory");
        Confirm(vkBindBufferMemory(Device, Region.Handle, Region.Memory, 0), "vkBindBufferMemory");
        Confirm(vkMapMemory(Device, Region.Memory, 0, Size, 0, &Region.Mapped), "vkMapMemory");
        std::memset(Region.Mapped, 0, static_cast<std::size_t>(Size));
    }

    VkPipeline InitializePipeline(const char *Path)
    {
        std::ifstream Input(Path, std::ios::binary | std::ios::ate);
        Require(bool(Input), "Cannot read Hex8 shader");
        const auto Bytes = Input.tellg();
        Require(Bytes > 0 && Bytes % 4 == 0, "Invalid Hex8 SPIR-V byte count");
        std::vector<std::uint32_t> Words(static_cast<std::size_t>(Bytes) / 4);
        Input.seekg(0);
        Require(bool(Input.read(reinterpret_cast<char *>(Words.data()), Bytes)), "Cannot read Hex8 SPIR-V");
        VkShaderModuleCreateInfo ModuleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ModuleInfo.codeSize = static_cast<std::size_t>(Bytes);
        ModuleInfo.pCode = Words.data();
        VkShaderModule Module = VK_NULL_HANDLE;
        Confirm(vkCreateShaderModule(Device, &ModuleInfo, nullptr, &Module), "vkCreateShaderModule");
        VkPipelineShaderStageCreateInfo Stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        Stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        Stage.module = Module;
        Stage.pName = "main";
        VkComputePipelineCreateInfo Info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        Info.stage = Stage;
        Info.layout = PipelineLayout;
        VkPipeline Pipeline = VK_NULL_HANDLE;
        const VkResult Status = vkCreateComputePipelines(Device, VK_NULL_HANDLE, 1, &Info, nullptr, &Pipeline);
        vkDestroyShaderModule(Device, Module, nullptr);
        Confirm(Status, "vkCreateComputePipelines");
        return Pipeline;
    }

    void InitializeResources(std::uint32_t Count, const char *ResetPath, const char *AdvancePath,
                             const char *SurfacePath, const char *StressPath, const TyreSolidCellAtlas &Cells)
    {
        InitializeBuffer(CommandBuffer, sizeof(TyreProjectionCommand) * Count);
        InitializeBuffer(ParticleBuffer, sizeof(TyreProjectionParticle) * TyreContinuumParticleCount * Count);
        InitializeBuffer(ResultBuffer, sizeof(TyreSolidDeviceResult) * Count);
        InitializeBuffer(CellBuffer, sizeof(Cells));
        InitializeBuffer(SurfaceProbeBuffer, sizeof(SurfaceProbe) * SurfaceCount);
        InitializeBuffer(SurfaceResultBuffer, sizeof(SurfaceResult) * SurfaceCount);
        InitializeBuffer(StressProbeBuffer, sizeof(TyreStressProbe) * StressCount);
        InitializeBuffer(StressSampleBuffer, sizeof(TyreStressSample) * StressCount);
        std::memcpy(CellBuffer.Mapped, Cells.data(), sizeof(Cells));
        VkDescriptorSetLayoutBinding Bindings[4]{};
        for (std::uint32_t I = 0; I < 4; ++I)
        {
            Bindings[I].binding = I;
            Bindings[I].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            Bindings[I].descriptorCount = 1;
            Bindings[I].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo LayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        LayoutInfo.bindingCount = 4;
        LayoutInfo.pBindings = Bindings;
        Confirm(vkCreateDescriptorSetLayout(Device, &LayoutInfo, nullptr, &DescriptorLayout), "vkCreateDescriptorSetLayout");
        VkPipelineLayoutCreateInfo PipelineInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        PipelineInfo.setLayoutCount = 1;
        PipelineInfo.pSetLayouts = &DescriptorLayout;
        const VkPushConstantRange TyreExtent{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(std::uint32_t)};
        PipelineInfo.pushConstantRangeCount = 1;
        PipelineInfo.pPushConstantRanges = &TyreExtent;
        Confirm(vkCreatePipelineLayout(Device, &PipelineInfo, nullptr, &PipelineLayout), "vkCreatePipelineLayout");
        VkDescriptorPoolSize PoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 12};
        VkDescriptorPoolCreateInfo PoolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        PoolInfo.maxSets = 3;
        PoolInfo.poolSizeCount = 1;
        PoolInfo.pPoolSizes = &PoolSize;
        Confirm(vkCreateDescriptorPool(Device, &PoolInfo, nullptr, &DescriptorPool), "vkCreateDescriptorPool");
        VkDescriptorSetLayout Layouts[3] = {DescriptorLayout, DescriptorLayout, DescriptorLayout};
        VkDescriptorSet Sets[3]{};
        VkDescriptorSetAllocateInfo Allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        Allocate.descriptorPool = DescriptorPool;
        Allocate.descriptorSetCount = 3;
        Allocate.pSetLayouts = Layouts;
        Confirm(vkAllocateDescriptorSets(Device, &Allocate, Sets), "vkAllocateDescriptorSets");
        Descriptors = Sets[0];
        SurfaceDescriptors = Sets[1];
        StressDescriptors = Sets[2];
        const VkDescriptorBufferInfo Buffers[12] = {{CommandBuffer.Handle, 0, VK_WHOLE_SIZE},
            {ParticleBuffer.Handle, 0, VK_WHOLE_SIZE}, {ResultBuffer.Handle, 0, VK_WHOLE_SIZE},
            {CellBuffer.Handle, 0, VK_WHOLE_SIZE}, {ParticleBuffer.Handle, 0, VK_WHOLE_SIZE},
            {SurfaceProbeBuffer.Handle, 0, VK_WHOLE_SIZE}, {SurfaceResultBuffer.Handle, 0, VK_WHOLE_SIZE},
            {CellBuffer.Handle, 0, VK_WHOLE_SIZE}, {StressProbeBuffer.Handle, 0, VK_WHOLE_SIZE},
            {StressSampleBuffer.Handle, 0, VK_WHOLE_SIZE}, {ResultBuffer.Handle, 0, VK_WHOLE_SIZE},
            {CellBuffer.Handle, 0, VK_WHOLE_SIZE}};
        VkWriteDescriptorSet Writes[12]{};
        for (std::uint32_t I = 0; I < 12; ++I)
        {
            Writes[I].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            Writes[I].dstSet = Sets[I / 4];
            Writes[I].dstBinding = I % 4;
            Writes[I].descriptorCount = 1;
            Writes[I].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            Writes[I].pBufferInfo = &Buffers[I];
        }
        vkUpdateDescriptorSets(Device, 12, Writes, 0, nullptr);
        ResetPipeline = InitializePipeline(ResetPath);
        AdvancePipeline = InitializePipeline(AdvancePath);
        SurfacePipeline = InitializePipeline(SurfacePath);
        StressPipeline = InitializePipeline(StressPath);
        VkCommandPoolCreateInfo Pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        Pool.queueFamilyIndex = QueueFamily;
        Pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        Confirm(vkCreateCommandPool(Device, &Pool, nullptr, &CommandPool), "vkCreateCommandPool");
        VkCommandBufferAllocateInfo CommandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        CommandInfo.commandPool = CommandPool;
        CommandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        CommandInfo.commandBufferCount = 1;
        Confirm(vkAllocateCommandBuffers(Device, &CommandInfo, &Commands), "vkAllocateCommandBuffers");
    }

    void EvaluateStress()
    {
        Confirm(vkResetCommandBuffer(Commands, 0), "vkResetCommandBuffer stress");
        VkCommandBufferBeginInfo Begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Confirm(vkBeginCommandBuffer(Commands, &Begin), "vkBeginCommandBuffer stress");
        vkCmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, StressPipeline);
        vkCmdBindDescriptorSets(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, PipelineLayout, 0, 1, &StressDescriptors, 0, nullptr);
        vkCmdDispatch(Commands, 1, 1, 1);
        VkMemoryBarrier Download{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        Download.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        Download.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                             0, 1, &Download, 0, nullptr, 0, nullptr);
        Confirm(vkEndCommandBuffer(Commands), "vkEndCommandBuffer stress");
        VkSubmitInfo Submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        Submit.commandBufferCount = 1;
        Submit.pCommandBuffers = &Commands;
        Confirm(vkQueueSubmit(Queue, 1, &Submit, VK_NULL_HANDLE), "vkQueueSubmit stress");
        Confirm(vkQueueWaitIdle(Queue), "vkQueueWaitIdle stress");
    }

    TyreProjectionVulkanAccess QueryAccess() const
    {
        TyreProjectionVulkanAccess Access;
        Access.PipelineLayout = PipelineLayout;
        Access.ResetPipeline = ResetPipeline;
        Access.AdvancePipeline = AdvancePipeline;
        Access.Descriptors = Descriptors;
        Access.BindPipeline = vkCmdBindPipeline;
        Access.BindDescriptors = vkCmdBindDescriptorSets;
        Access.PushConstants = vkCmdPushConstants;
        Access.Dispatch = vkCmdDispatch;
        Access.MaxGroupCountX = Properties.limits.maxComputeWorkGroupCount[0];
        return Access;
    }

    void Execute(std::uint32_t Count, bool ResetFirst, bool IncludeSurface = false)
    {
        Confirm(vkResetCommandBuffer(Commands, 0), "vkResetCommandBuffer");
        VkCommandBufferBeginInfo Begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Confirm(vkBeginCommandBuffer(Commands, &Begin), "vkBeginCommandBuffer");
        const auto Access = QueryAccess();
        if (ResetFirst)
        {
            RecordTyreProjection(Commands, Access, TyreProjectionOperation::Reset, Count);
            VkMemoryBarrier Barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 1, &Barrier, 0, nullptr, 0, nullptr);
        }
        RecordTyreProjection(Commands, Access, TyreProjectionOperation::Advance, Count);
        if (IncludeSurface)
        {
            VkMemoryBarrier SurfaceBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            SurfaceBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            SurfaceBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 1, &SurfaceBarrier, 0, nullptr, 0, nullptr);
            vkCmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, SurfacePipeline);
            vkCmdBindDescriptorSets(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, PipelineLayout, 0, 1, &SurfaceDescriptors, 0, nullptr);
            vkCmdDispatch(Commands, SurfaceCount / 64, 1, 1);
        }
        VkMemoryBarrier Download{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        Download.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        Download.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                             0, 1, &Download, 0, nullptr, 0, nullptr);
        Confirm(vkEndCommandBuffer(Commands), "vkEndCommandBuffer");
        VkSubmitInfo Submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        Submit.commandBufferCount = 1;
        Submit.pCommandBuffers = &Commands;
        Confirm(vkQueueSubmit(Queue, 1, &Submit, VK_NULL_HANDLE), "vkQueueSubmit");
        Confirm(vkQueueWaitIdle(Queue), "vkQueueWaitIdle");
    }
};

TyreContinuumInput ReadInput(const TyreProjectionCommand &Command)
{
    TyreContinuumInput Input;
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        Input.Centre[Axis] = Command.Centre[Axis];
        Input.GroundNormal[Axis] = Command.GroundNormalDeflection[Axis];
    }
    for (int Axis = 0; Axis < 4; ++Axis) Input.Orientation[Axis] = Command.Orientation[Axis];
    Input.Deflection = Command.GroundNormalDeflection[3];
    Input.Pressure = Command.PressureLateralSeconds[0];
    Input.LateralForce = Command.PressureLateralSeconds[1];
    return Input;
}

double Determinant(const double F[3][3])
{
    return F[0][0] * (F[1][1]*F[2][2] - F[1][2]*F[2][1]) -
           F[0][1] * (F[1][0]*F[2][2] - F[1][2]*F[2][0]) +
           F[0][2] * (F[1][0]*F[2][1] - F[1][1]*F[2][0]);
}

double SolidEnergy(const double F[3][3], double Mu, double Lambda)
{
    const double J = Determinant(F);
    Require(J > 0.30 && J < 2.20, "Stress fixture left the unclamped positive-J domain");
    double Squared = 0.0;
    for (int Row = 0; Row < 3; ++Row)
        for (int Axis = 0; Axis < 3; ++Axis) Squared += F[Row][Axis] * F[Row][Axis];
    return 0.5 * Mu * (Squared - 3.0) - Mu * std::log(J) + 0.5 * Lambda * (J - 1.0) * (J - 1.0);
}

std::vector<TyreStressProbe> ConstructStressProbes()
{
    std::vector<TyreStressProbe> Probes(StressCount);
    constexpr float Pressures[] = {0.0f, 30000.0f, 220000.0f, 350000.0f};
    for (std::uint32_t I = 0; I < StressCount; ++I)
    {
        auto &Probe = Probes[I];
        const double PressureRatio = std::max(0.08, double(Pressures[I % 4]) / 220000.0);
        const double Young = 12000.0 * (0.32 + 0.68 * PressureRatio), Nu = 0.44;
        Probe.Moduli[0] = float(Young / (2.0 * (1.0 + Nu)));
        Probe.Moduli[1] = float(Young * Nu / ((1.0 + Nu) * (1.0 - 2.0*Nu)));
        for (int Row = 0; Row < 3; ++Row)
            for (int Axis = 0; Axis < 3; ++Axis)
                Probe.F[Row][Axis] = float((Row == Axis ? 1.0 : 0.0) +
                    (I < 5 ? 0.0 : 0.12 * std::sin((I + 1.0) * (Row + 1.0) * (Axis + 2.0) * 0.31)));
    }
    const double Angle = 0.71;
    Probes[1].F[0][0] = float(std::cos(Angle)); Probes[1].F[0][1] = float(-std::sin(Angle));
    Probes[1].F[1][0] = float(std::sin(Angle)); Probes[1].F[1][1] = float(std::cos(Angle));
    for (int Axis = 0; Axis < 3; ++Axis) Probes[2].F[Axis][Axis] = 1.20f;
    Probes[3].F[0][0] = 0.70f;
    Probes[4].F[0][1] = 0.35f;
    return Probes;
}

void ValidateStress(const std::vector<TyreStressProbe> &Probes, const TyreStressSample *Samples)
{
    constexpr double Step = 0.00001;
    double WorstStress = 0.0, WorstJacobian = 0.0, WorstScaled = 0.0, RigidStress = 0.0;
    for (std::size_t I = 0; I < Probes.size(); ++I)
    {
        double F[3][3]{};
        for (int Row = 0; Row < 3; ++Row)
            for (int Axis = 0; Axis < 3; ++Axis) F[Row][Axis] = Probes[I].F[Row][Axis];
        const double Mu = Probes[I].Moduli[0], Lambda = Probes[I].Moduli[1];
        WorstJacobian = std::max(WorstJacobian, std::abs(double(Samples[I].P[0][3]) - Determinant(F)));
        for (int Row = 0; Row < 3; ++Row)
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                F[Row][Axis] += Step;
                const double Plus = SolidEnergy(F, Mu, Lambda);
                F[Row][Axis] -= 2.0 * Step;
                const double Minus = SolidEnergy(F, Mu, Lambda);
                F[Row][Axis] += Step;
                const double Expected = (Plus - Minus) / (2.0 * Step);
                const double Actual = Samples[I].P[Row][Axis];
                Require(std::isfinite(Actual), "Nonfinite solid stress sample");
                const double Error = std::abs(Actual - Expected);
                WorstStress = std::max(WorstStress, Error);
                WorstScaled = std::max(WorstScaled, Error / (1.0 + std::abs(Expected)));
                if (I <= 1) RigidStress = std::max(RigidStress, std::abs(Actual));
                Require(Error < 0.08 + 0.00001 * std::abs(Expected), "GPU stress disagrees with numerical energy gradient");
            }
    }
    std::printf("TYRE_SOLID_CONSTITUTIVE probes=%zu independent_energy_gradient=1 max_abs=%.9g max_scaled=%.9g "
                "max_jacobian=%.9g rigid_stress=%.9g positive_j_unclamped=1\n",
                Probes.size(), WorstStress, WorstScaled, WorstJacobian, RigidStress);
    Require(WorstJacobian < 0.000002 && RigidStress < 0.02, "Solid identity or rigid rotation failed");
}

std::vector<SurfaceProbe> ConstructSurfaceProbes(std::uint32_t Tyres)
{
    Require(Tyres >= 3, "Surface fixture requires three tyres");
    constexpr double Pi = 3.14159265358979323846;
    std::vector<SurfaceProbe> Probes(SurfaceCount);
    for (std::uint32_t I = 0; I < SurfaceCount; ++I)
    {
        const double Angle = 2.0 * Pi * ((I % 2048 + 0.371) / 2048.0) - Pi;
        const double Radius = 0.36 + 0.15 * ((I * 17) % 2048) / 2047.0;
        const double Nx = std::cos(Angle), Ny = 0.15 * std::sin(3.0 * Angle), Nz = std::sin(Angle);
        const double Magnitude = std::sqrt(Nx*Nx + Ny*Ny + Nz*Nz);
        auto &Probe = Probes[I];
        Probe.Position[0] = float(Radius * std::cos(Angle));
        Probe.Position[1] = float(-0.13 + 0.26 * ((I * 37) % 2048) / 2047.0);
        Probe.Position[2] = float(Radius * std::sin(Angle));
        Probe.Normal[0] = float(Nx / Magnitude);
        Probe.Normal[1] = float(Ny / Magnitude);
        Probe.Normal[2] = float(Nz / Magnitude);
        Probe.Deformation[1] = 0.072f;
        Probe.Controls[0] = I < 2048 ? 1 : 2;
        Probe.Controls[1] = 1;
        Probe.Controls[2] = I % 13 == 0 ? 0 : (I % 17 == 0 ? 3 : 1);
    }
    return Probes;
}

void ValidateSurface(const std::vector<SurfaceProbe> &Probes, const SurfaceResult *Actual,
                     const TyreProjectionParticle *Particles, std::uint32_t Tyres)
{
    std::vector<TyreContinuumPositions> Rest(Tyres), Local(Tyres);
    for (std::uint32_t Tyre = 0; Tyre < Tyres; ++Tyre)
        for (int N = 0; N < TyreContinuumParticleCount; ++N)
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                const auto &Particle = Particles[Tyre * TyreContinuumParticleCount + N];
                Rest[Tyre][N*3+Axis] = Particle.RestInverseMass[Axis];
                Local[Tyre][N*3+Axis] = Particle.Local[Axis];
            }
    double WorstPosition = 0.0, WorstNormal = 0.0, LargestMotion = 0.0;
    std::uint32_t Guarded = 0;
    for (std::size_t I = 0; I < Probes.size(); ++I)
    {
        const auto &Probe = Probes[I];
        TyreDeformationRecord Deformation;
        Deformation.Deflection = Probe.Deformation[1];
        Deformation.Shredded = (Probe.Controls[2] & 2) != 0;
        const bool TyreSurface = (Probe.Controls[2] & 1) != 0;
        const std::uint32_t Tyre = Probe.Controls[0];
        float Position[3], Normal[3];
        ResolveContinuumWheelVertex(Deformation, TyreSurface, Rest[Tyre], Local[Tyre],
                                    Probe.Position, Probe.Normal, Position, Normal);
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            Require(std::isfinite(Actual[I].Position[Axis]) && std::isfinite(Actual[I].Normal[Axis]),
                    "Nonfinite solid surface result");
            WorstPosition = std::max(WorstPosition, std::abs(double(Actual[I].Position[Axis]) - Position[Axis]));
            WorstNormal = std::max(WorstNormal, std::abs(double(Actual[I].Normal[Axis]) - Normal[Axis]));
            if (!Deformation.Shredded && TyreSurface)
                LargestMotion = std::max(LargestMotion, std::abs(double(Actual[I].Position[Axis]) - Probe.Position[Axis]));
        }
        if (Deformation.Shredded || !TyreSurface) ++Guarded;
    }
    std::printf("TYRE_SOLID_SURFACE probes=%zu gpu_resident_particles=1 same_command_buffer=1 guards=%u "
                "max_position_m=%.9g max_normal=%.9g largest_motion_m=%.9g\n",
                Probes.size(), Guarded, WorstPosition, WorstNormal, LargestMotion);
    Require(WorstPosition < 0.0003 && WorstNormal < 0.0003 && LargestMotion > 0.005,
            "GPU solid surface differs from CPU projection or lacks deformation");
}

int Run(int Count, char **Arguments)
{
    Require(Count == 7, "Expected reset, advance, surface and stress SPIR-V, command input and readback output");
    std::ifstream Input(Arguments[5], std::ios::binary);
    Require(bool(Input), "Cannot read tyre commands");
    std::uint32_t Tyres = 0, Steps = 0;
    Input.read(reinterpret_cast<char *>(&Tyres), sizeof(Tyres));
    Input.read(reinterpret_cast<char *>(&Steps), sizeof(Steps));
    Require(bool(Input) && Tyres > 0 && Tyres <= 64 && Steps > 0 && Steps <= 32, "Invalid tyre command dimensions");
    std::vector<TyreProjectionCommand> Commands(std::size_t(Tyres) * Steps);
    Input.read(reinterpret_cast<char *>(Commands.data()), static_cast<std::streamsize>(Commands.size() * sizeof(Commands[0])));
    Require(bool(Input) && Input.peek() == std::char_traits<char>::eof(), "Unexpected tyre command byte count");
    for (const auto &Command : Commands)
        (void)CompileTyreProjectionCommand(ReadInput(Command), Command.PressureLateralSeconds[2], Command.IterationCounts[0]);

    std::vector<TyreCarcassSolid> References(Tyres);
    const TyreSolidCellAtlas Cells = CompileTyreSolidCells(References[0]);
    auto InvalidCells = Cells;
    int Rejections = 0;
    const auto Reject = [&](const TyreSolidCellFootprint *Footprints, std::size_t CellCount)
    {
        try { ValidateTyreSolidCells(Footprints, CellCount); }
        catch (const std::invalid_argument &) { ++Rejections; return; }
        throw std::runtime_error("Invalid solid tyre cells accepted");
    };
    Reject(nullptr, Cells.size());
    Reject(Cells.data(), Cells.size() - 1);
    InvalidCells[0].Corners[0] = TyreContinuumParticleCount;
    Reject(InvalidCells.data(), InvalidCells.size());
    InvalidCells = Cells;
    InvalidCells[7].GradientsVolume[1][1] = std::numeric_limits<float>::quiet_NaN();
    Reject(InvalidCells.data(), InvalidCells.size());
    InvalidCells = Cells;
    InvalidCells[8].GradientsVolume[0][3] = 0.0f;
    Reject(InvalidCells.data(), InvalidCells.size());
    InvalidCells = Cells;
    InvalidCells[9].GradientsVolume[2][3] = 1.0f;
    Reject(InvalidCells.data(), InvalidCells.size());
    std::printf("TYRE_SOLID_CELL_GUARDS rejected=%d valid_cells=%zu\n", Rejections, Cells.size());

    SolidVulkanValidation Device;
    Device.InitializeDevice();
    Device.InitializeResources(Tyres, Arguments[1], Arguments[2], Arguments[3], Arguments[4], Cells);
    const auto StressProbes = ConstructStressProbes();
    std::memcpy(Device.StressProbeBuffer.Mapped, StressProbes.data(), StressProbes.size() * sizeof(StressProbes[0]));
    Device.EvaluateStress();
    ValidateStress(StressProbes, static_cast<const TyreStressSample *>(Device.StressSampleBuffer.Mapped));
    const auto SurfaceProbes = ConstructSurfaceProbes(Tyres);
    std::memcpy(Device.SurfaceProbeBuffer.Mapped, SurfaceProbes.data(), SurfaceProbes.size() * sizeof(SurfaceProbes[0]));
    auto *GpuCommands = static_cast<TyreProjectionCommand *>(Device.CommandBuffer.Mapped);
    auto *Particles = static_cast<TyreProjectionParticle *>(Device.ParticleBuffer.Mapped);
    auto *Results = static_cast<TyreSolidDeviceResult *>(Device.ResultBuffer.Mapped);
    std::ofstream Output(Arguments[6], std::ios::binary);
    Require(bool(Output), "Cannot create tyre readback");
    double WorstLocal = 0, WorstWorld = 0, WorstForce = 0, WorstJacobian = 0, WorstStress = 0;
    std::uint64_t Pinned = 0;
    for (std::uint32_t Step = 0; Step < Steps; ++Step)
    {
        std::memcpy(GpuCommands, Commands.data() + std::size_t(Step) * Tyres, sizeof(*GpuCommands) * Tyres);
        Device.Execute(Tyres, Step == 0, Step + 1 == Steps);
        for (std::uint32_t Tyre = 0; Tyre < Tyres; ++Tyre)
        {
            const auto &Command = GpuCommands[Tyre];
            const TyreContinuumInput Source = ReadInput(Command);
            auto &Reference = References[Tyre];
            Reference.Advance(Source, Command.PressureLateralSeconds[2], int(Command.IterationCounts[0]));
            const auto &Result = Results[Tyre];
            Require(Result.ForceStatusJacobianStress[1] == 1.0f, "GPU rejected valid solid command");
            Require(Result.ContactCount[0] == std::uint32_t(Reference.QueryContactCount()), "GPU solid contact count mismatch");
            for (float Component : Result.ForceStatusJacobianStress) Require(std::isfinite(Component), "Nonfinite solid result");
            WorstForce = std::max(WorstForce, std::abs(double(Result.ForceStatusJacobianStress[0]) - Reference.QueryVerticalForce()));
            WorstJacobian = std::max(WorstJacobian, std::abs(double(Result.ForceStatusJacobianStress[2]) - Reference.QueryMeanJacobian()));
            WorstStress = std::max(WorstStress, std::abs(double(Result.ForceStatusJacobianStress[3]) - Reference.QueryMeanStress()));
            for (int N = 0; N < TyreContinuumParticleCount; ++N)
            {
                const auto &Particle = Particles[Tyre * TyreContinuumParticleCount + N];
                for (int Axis = 0; Axis < 3; ++Axis)
                {
                    Require(std::isfinite(Particle.Local[Axis]) && std::isfinite(Particle.World[Axis]), "Nonfinite solid particle");
                    WorstLocal = std::max(WorstLocal, std::abs(double(Particle.Local[Axis]) - Reference.QueryLocalPositions()[N * 3 + Axis]));
                    WorstWorld = std::max(WorstWorld, std::abs(double(Particle.World[Axis]) - Reference.QueryWorldPositions()[N * 3 + Axis]));
                    if (N % 5 == 0 || N % 5 == 4)
                    {
                        Require(Particle.Local[Axis] == Particle.RestInverseMass[Axis], "Pinned tyre particle moved");
                        ++Pinned;
                    }
                }
            }
        }
        Output.write(reinterpret_cast<const char *>(Particles), std::streamsize(sizeof(*Particles) * Tyres * TyreContinuumParticleCount));
        Output.write(reinterpret_cast<const char *>(Results), std::streamsize(sizeof(*Results) * Tyres));
        Require(bool(Output), "Tyre readback write failed");
    }
    std::printf("TYRE_SOLID_CPU tyres=%u steps=%u particle_snapshots=%u max_local_m=%.9g max_world_m=%.9g "
                "max_force_n=%.9g max_jacobian=%.9g max_stress=%.9g pinned_components=%llu contacts_exact=1\n",
                Tyres, Steps, Tyres * Steps * TyreContinuumParticleCount, WorstLocal, WorstWorld, WorstForce,
                WorstJacobian, WorstStress, static_cast<unsigned long long>(Pinned));
    Require(WorstLocal < 0.0003 && WorstWorld < 0.0003 && WorstForce < 0.02 && WorstJacobian < 0.001 && WorstStress < 1.0,
            "GPU solid disagrees with CPU reference beyond test limits");
    ValidateSurface(SurfaceProbes, static_cast<const SurfaceResult *>(Device.SurfaceResultBuffer.Mapped), Particles, Tyres);
    Output.write(static_cast<const char *>(Device.SurfaceResultBuffer.Mapped), sizeof(SurfaceResult) * SurfaceCount);
    Output.write(static_cast<const char *>(Device.StressSampleBuffer.Mapped), sizeof(TyreStressSample) * StressCount);
    Require(bool(Output), "Solid surface and stress readback write failed");

    const auto SavedCommand = GpuCommands[0];
    const std::vector<TyreProjectionParticle> Saved(Particles, Particles + TyreContinuumParticleCount);
    const std::vector<TyreProjectionParticle> TailParticles(Particles + TyreContinuumParticleCount,
                                                            Particles + Tyres * TyreContinuumParticleCount);
    const std::vector<TyreSolidDeviceResult> TailResults(Results + 1, Results + Tyres);
    GpuCommands[0].Orientation[3] = 0.0f;
    Device.Execute(1, false);
    Require(Results[0].ForceStatusJacobianStress[1] == 0.0f &&
            std::memcmp(Saved.data(), Particles, Saved.size() * sizeof(Saved[0])) == 0, "Invalid GPU solid command mutated particles");
    GpuCommands[0] = SavedCommand;
    auto *GpuCells = static_cast<TyreSolidCellFootprint *>(Device.CellBuffer.Mapped);
    GpuCells[0].Corners[0] = TyreContinuumParticleCount;
    Device.Execute(1, false);
    Require(Results[0].ForceStatusJacobianStress[1] == 0.0f &&
            std::memcmp(Saved.data(), Particles, Saved.size() * sizeof(Saved[0])) == 0, "Invalid GPU solid cell mutated particles");
    std::memcpy(GpuCells, Cells.data(), sizeof(Cells));
    GpuCells[3].GradientsVolume[1][1] = std::numeric_limits<float>::quiet_NaN();
    Device.Execute(1, false);
    Require(Results[0].ForceStatusJacobianStress[1] == 0.0f &&
            std::memcmp(Saved.data(), Particles, Saved.size() * sizeof(Saved[0])) == 0, "Nonfinite GPU solid cell mutated particles");
    std::memcpy(GpuCells, Cells.data(), sizeof(Cells));
    Device.Execute(1, false);
    Require(Results[0].ForceStatusJacobianStress[1] == 1.0f &&
            std::memcmp(TailParticles.data(), Particles + TyreContinuumParticleCount,
                        TailParticles.size() * sizeof(TailParticles[0])) == 0 &&
            std::memcmp(TailResults.data(), Results + 1, TailResults.size() * sizeof(TailResults[0])) == 0,
            "Partial solid dispatch modified an unrequested tyre");
    const std::vector<TyreProjectionParticle> AllParticles(Particles, Particles + Tyres * TyreContinuumParticleCount);
    const std::vector<TyreSolidDeviceResult> AllResults(Results, Results + Tyres);
    VkDescriptorBufferInfo ShortBuffer{Device.CellBuffer.Handle, 0, sizeof(TyreSolidCellFootprint) * (TyreContinuumCellCount - 1)};
    VkWriteDescriptorSet ShortWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    ShortWrite.dstSet = Device.Descriptors;
    ShortWrite.dstBinding = 3;
    ShortWrite.descriptorCount = 1;
    ShortWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ShortWrite.pBufferInfo = &ShortBuffer;
    vkUpdateDescriptorSets(Device.Device, 1, &ShortWrite, 0, nullptr);
    Device.Execute(Tyres, false);
    Require(std::memcmp(AllParticles.data(), Particles, AllParticles.size() * sizeof(AllParticles[0])) == 0 &&
            std::memcmp(AllResults.data(), Results, AllResults.size() * sizeof(AllResults[0])) == 0,
            "Short solid reference-cell buffer mutated state");
    std::printf("TYRE_SOLID_DEVICE_GUARDS invalid_command=1 invalid_corner=1 nonfinite_cell=1 short_cells=1 "
                "count_bound=1 untouched_particles=1 same_command_reset_advance=1\n");
    return 0;
}
} // namespace

int main(int Count, char **Arguments)
{
    try { return Run(Count, Arguments); }
    catch (const std::exception &Error) { std::fprintf(stderr, "FAIL %s\n", Error.what()); return 1; }
}
