/*==========================================================================================================================================
                                             TYREPROJECTIONVULKANVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Execute the Engine SPIR-V unchanged through Vulkan and compare each tyre with the Engine CPU solver.

#include "Engine/Physics/TyreContinuum.h"
#include "Engine/Physics/TyreProjectionDevice.h"
#include "Engine/Physics/TyreProjectionVulkan.h"
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
void Confirm(VkResult Status, const char *Operation)
{
    if (Status != VK_SUCCESS) throw std::runtime_error(std::string(Operation) + " failed: " + std::to_string(Status));
}

struct Buffer
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
static_assert(sizeof(SurfaceProbe) == 64 && sizeof(SurfaceResult) == 32);

struct VulkanFixture
{
    VkInstance Instance = VK_NULL_HANDLE;
    VkPhysicalDevice Physical = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    VkQueue Queue = VK_NULL_HANDLE;
    std::uint32_t QueueFamily = 0;
    VkPhysicalDeviceProperties Properties{};
    VkDescriptorSetLayout DescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet Descriptors = VK_NULL_HANDLE;
    VkDescriptorSet SurfaceDescriptors = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkPipeline ResetPipeline = VK_NULL_HANDLE;
    VkPipeline AdvancePipeline = VK_NULL_HANDLE;
    VkPipeline SurfacePipeline = VK_NULL_HANDLE;
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    VkCommandBuffer Commands = VK_NULL_HANDLE;
    Buffer CommandBuffer, ParticleBuffer, ResultBuffer, SurfaceProbeBuffer, SurfaceResultBuffer;
    std::uint32_t SurfaceCount = 0;

    ~VulkanFixture()
    {
        if (Device) vkDeviceWaitIdle(Device);
        if (Device && CommandPool) vkDestroyCommandPool(Device, CommandPool, nullptr);
        if (Device && ResetPipeline) vkDestroyPipeline(Device, ResetPipeline, nullptr);
        if (Device && AdvancePipeline) vkDestroyPipeline(Device, AdvancePipeline, nullptr);
        if (Device && SurfacePipeline) vkDestroyPipeline(Device, SurfacePipeline, nullptr);
        if (Device && PipelineLayout) vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
        if (Device && DescriptorPool) vkDestroyDescriptorPool(Device, DescriptorPool, nullptr);
        if (Device && DescriptorLayout) vkDestroyDescriptorSetLayout(Device, DescriptorLayout, nullptr);
        for (Buffer *Item : {&CommandBuffer, &ParticleBuffer, &ResultBuffer, &SurfaceProbeBuffer, &SurfaceResultBuffer})
        {
            if (Device && Item->Mapped) vkUnmapMemory(Device, Item->Memory);
            if (Device && Item->Handle) vkDestroyBuffer(Device, Item->Handle, nullptr);
            if (Device && Item->Memory) vkFreeMemory(Device, Item->Memory, nullptr);
        }
        if (Device) vkDestroyDevice(Device, nullptr);
        if (Instance) vkDestroyInstance(Instance, nullptr);
    }

    void InitializeDevice()
    {
        VkApplicationInfo App{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        App.pApplicationName = "Frontier tyre projection validation";
        App.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo InstanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        InstanceInfo.pApplicationInfo = &App;
        Confirm(vkCreateInstance(&InstanceInfo, nullptr, &Instance), "vkCreateInstance");
        std::uint32_t Count = 0;
        Confirm(vkEnumeratePhysicalDevices(Instance, &Count, nullptr), "vkEnumeratePhysicalDevices count");
        if (!Count) throw std::runtime_error("No Vulkan device");
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
        if (!Found) throw std::runtime_error("No compute queue");
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

    void InitializeBuffer(Buffer &Target, VkDeviceSize Size)
    {
        VkBufferCreateInfo Info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        Info.size = Size;
        Info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        Info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        Confirm(vkCreateBuffer(Device, &Info, nullptr, &Target.Handle), "vkCreateBuffer");
        VkMemoryRequirements Requirements{};
        vkGetBufferMemoryRequirements(Device, Target.Handle, &Requirements);
        VkPhysicalDeviceMemoryProperties Types{};
        vkGetPhysicalDeviceMemoryProperties(Physical, &Types);
        std::uint32_t Chosen = Types.memoryTypeCount;
        const auto Flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (std::uint32_t I = 0; I < Types.memoryTypeCount; ++I)
            if ((Requirements.memoryTypeBits & (1u << I)) && (Types.memoryTypes[I].propertyFlags & Flags) == Flags)
            { Chosen = I; break; }
        if (Chosen == Types.memoryTypeCount) throw std::runtime_error("No coherent host-visible Vulkan memory");
        VkMemoryAllocateInfo Allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        Allocation.allocationSize = Requirements.size;
        Allocation.memoryTypeIndex = Chosen;
        Confirm(vkAllocateMemory(Device, &Allocation, nullptr, &Target.Memory), "vkAllocateMemory");
        Confirm(vkBindBufferMemory(Device, Target.Handle, Target.Memory, 0), "vkBindBufferMemory");
        Confirm(vkMapMemory(Device, Target.Memory, 0, Size, 0, &Target.Mapped), "vkMapMemory");
        std::memset(Target.Mapped, 0, static_cast<std::size_t>(Size));
    }

    VkPipeline InitializePipeline(const char *Path)
    {
        std::ifstream Input(Path, std::ios::binary | std::ios::ate);
        if (!Input) throw std::runtime_error(std::string("Cannot read shader ") + Path);
        const auto Bytes = Input.tellg();
        if (Bytes <= 0 || Bytes % 4 != 0) throw std::runtime_error("Invalid SPIR-V byte count");
        std::vector<std::uint32_t> Words(static_cast<std::size_t>(Bytes) / 4);
        Input.seekg(0);
        if (!Input.read(reinterpret_cast<char *>(Words.data()), Bytes)) throw std::runtime_error("SPIR-V read failed");
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
                             const char *SurfacePath, std::uint32_t ProbeCount)
    {
        using namespace Frontier;
        InitializeBuffer(CommandBuffer, sizeof(TyreProjectionCommand) * Count);
        InitializeBuffer(ParticleBuffer, sizeof(TyreProjectionParticle) * TyreContinuumParticleCount * Count);
        InitializeBuffer(ResultBuffer, sizeof(TyreProjectionResult) * Count);
        SurfaceCount = ProbeCount;
        InitializeBuffer(SurfaceProbeBuffer, sizeof(SurfaceProbe) * ProbeCount);
        InitializeBuffer(SurfaceResultBuffer, sizeof(SurfaceResult) * ProbeCount);
        VkDescriptorSetLayoutBinding Bindings[3]{};
        for (std::uint32_t I = 0; I < 3; ++I)
        {
            Bindings[I].binding = I;
            Bindings[I].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            Bindings[I].descriptorCount = 1;
            Bindings[I].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo LayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        LayoutInfo.bindingCount = 3;
        LayoutInfo.pBindings = Bindings;
        Confirm(vkCreateDescriptorSetLayout(Device, &LayoutInfo, nullptr, &DescriptorLayout), "vkCreateDescriptorSetLayout");
        VkPipelineLayoutCreateInfo PipelineInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        PipelineInfo.setLayoutCount = 1;
        PipelineInfo.pSetLayouts = &DescriptorLayout;
        const VkPushConstantRange TyreExtent{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(std::uint32_t)};
        PipelineInfo.pushConstantRangeCount = 1;
        PipelineInfo.pPushConstantRanges = &TyreExtent;
        Confirm(vkCreatePipelineLayout(Device, &PipelineInfo, nullptr, &PipelineLayout), "vkCreatePipelineLayout");
        VkDescriptorPoolSize PoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6};
        VkDescriptorPoolCreateInfo PoolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        PoolInfo.maxSets = 2;
        PoolInfo.poolSizeCount = 1;
        PoolInfo.pPoolSizes = &PoolSize;
        Confirm(vkCreateDescriptorPool(Device, &PoolInfo, nullptr, &DescriptorPool), "vkCreateDescriptorPool");
        VkDescriptorSetLayout Layouts[2] = {DescriptorLayout, DescriptorLayout};
        VkDescriptorSet Sets[2]{};
        VkDescriptorSetAllocateInfo Allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        Allocate.descriptorPool = DescriptorPool;
        Allocate.descriptorSetCount = 2;
        Allocate.pSetLayouts = Layouts;
        Confirm(vkAllocateDescriptorSets(Device, &Allocate, Sets), "vkAllocateDescriptorSets");
        Descriptors = Sets[0];
        SurfaceDescriptors = Sets[1];
        VkDescriptorBufferInfo BufferInfo[6]{};
        VkWriteDescriptorSet Writes[6]{};
        const VkBuffer Handles[6] = {CommandBuffer.Handle, ParticleBuffer.Handle, ResultBuffer.Handle,
                                     ParticleBuffer.Handle, SurfaceProbeBuffer.Handle, SurfaceResultBuffer.Handle};
        for (std::uint32_t I = 0; I < 6; ++I)
        {
            BufferInfo[I] = {Handles[I], 0, VK_WHOLE_SIZE};
            Writes[I].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            Writes[I].dstSet = Sets[I / 3];
            Writes[I].dstBinding = I % 3;
            Writes[I].descriptorCount = 1;
            Writes[I].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            Writes[I].pBufferInfo = &BufferInfo[I];
        }
        vkUpdateDescriptorSets(Device, 6, Writes, 0, nullptr);
        ResetPipeline = InitializePipeline(ResetPath);
        AdvancePipeline = InitializePipeline(AdvancePath);
        SurfacePipeline = InitializePipeline(SurfacePath);
        VkCommandPoolCreateInfo Pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        Pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        Pool.queueFamilyIndex = QueueFamily;
        Confirm(vkCreateCommandPool(Device, &Pool, nullptr, &CommandPool), "vkCreateCommandPool");
        VkCommandBufferAllocateInfo CommandsInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        CommandsInfo.commandPool = CommandPool;
        CommandsInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        CommandsInfo.commandBufferCount = 1;
        Confirm(vkAllocateCommandBuffers(Device, &CommandsInfo, &Commands), "vkAllocateCommandBuffers");
    }

    Frontier::TyreProjectionVulkanAccess QueryAccess() const
    {
        Frontier::TyreProjectionVulkanAccess Access;
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

    void Execute(Frontier::TyreProjectionOperation Operation, std::uint32_t Count, bool IncludeSurface = false)
    {
        VkCommandBufferBeginInfo Begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Confirm(vkBeginCommandBuffer(Commands, &Begin), "vkBeginCommandBuffer");
        Frontier::RecordTyreProjection(Commands, QueryAccess(), Operation, Count);
        if (IncludeSurface)
        {
            VkMemoryBarrier ParticleBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            ParticleBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            ParticleBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &ParticleBarrier, 0, nullptr, 0, nullptr);
            vkCmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, SurfacePipeline);
            vkCmdBindDescriptorSets(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, PipelineLayout, 0, 1, &SurfaceDescriptors, 0, nullptr);
            vkCmdDispatch(Commands, SurfaceCount / 64 + (SurfaceCount % 64 != 0 ? 1 : 0), 1, 1);
        }
        VkMemoryBarrier Barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        Barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &Barrier, 0, nullptr, 0, nullptr);
        Confirm(vkEndCommandBuffer(Commands), "vkEndCommandBuffer");
        VkSubmitInfo Submission{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        Submission.commandBufferCount = 1;
        Submission.pCommandBuffers = &Commands;
        Confirm(vkQueueSubmit(Queue, 1, &Submission, VK_NULL_HANDLE), "vkQueueSubmit");
        Confirm(vkQueueWaitIdle(Queue), "vkQueueWaitIdle");
        Confirm(vkResetCommandBuffer(Commands, 0), "vkResetCommandBuffer");
    }
};

Frontier::TyreContinuumInput Expand(const Frontier::TyreProjectionCommand &Command)
{
    Frontier::TyreContinuumInput Input;
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

void CheckRejected(const Frontier::TyreContinuumInput &Input, double Seconds, std::uint32_t Substeps)
{
    try { (void)Frontier::CompileTyreProjectionCommand(Input, Seconds, Substeps); }
    catch (const std::invalid_argument &) { return; }
    throw std::runtime_error("Invalid CPU-side command was accepted");
}

std::vector<SurfaceProbe> ConstructSurfaceProbes(std::uint32_t Tyres)
{
    constexpr int SamplesPerCase = 2048;
    constexpr double Pi = 3.14159265358979323846;
    struct SurfaceCase { float Angle, Deflection, Pressure, Shift; std::uint32_t Tyre, Model, Flags; };
    const SurfaceCase Cases[] = {
        {0.0f, 0.035f, 220000.0f, 0.02f, 0, 0, 1},
        {0.5f, 0.060f, 0.0f, -0.04f, 0, 0, 1},
        {-2.1f, 0.014f, 350000.0f, 0.01f, 0, 0, 1},
        {0.0f, 0.000011f, 220000.0f, 0.0f, 0, 0, 1},
        {1.2f, 0.0f, 220000.0f, 0.02f, 0, 0, 1},
        {0.0f, 0.035f, 220000.0f, 0.02f, 0, 0, 3},
        {0.0f, 0.072f, 350000.0f, 0.0f, 11, 1, 1},
        {0.0f, 0.072f, 0.0f, 0.0f, 2, 1, 1},
        {0.0f, 0.0f, 350000.0f, 0.0f, 11, 1, 1},
        {0.0f, 0.072f, 350000.0f, 0.0f, 11, 1, 3},
        {0.0f, 0.072f, 350000.0f, 0.0f, Tyres, 1, 1},
        {0.0f, 0.072f, 350000.0f, 0.0f, 11, 1, 0}};
    const double Radii[8] = {0.32, 0.3447, 0.345, 0.37, 0.40, 0.45, 0.49, 0.50855};
    std::vector<SurfaceProbe> Probes;
    Probes.reserve(sizeof(Cases) / sizeof(Cases[0]) * SamplesPerCase);
    for (const auto &Case : Cases)
        for (int I = 0; I < SamplesPerCase; ++I)
        {
            const double Angle = 2.0 * Pi * ((I + 0.371) / SamplesPerCase) - Pi;
            const double Radius = Radii[I % 8];
            const double Nx = std::cos(Angle), Ny = 0.15 * std::sin(3.0 * Angle), Nz = std::sin(Angle);
            const double Magnitude = std::sqrt(Nx*Nx + Ny*Ny + Nz*Nz);
            SurfaceProbe Probe;
            Probe.Position[0] = static_cast<float>(Radius * std::cos(Angle));
            Probe.Position[1] = static_cast<float>(-0.13 + 0.26 * ((I * 37) % SamplesPerCase) / (SamplesPerCase - 1));
            Probe.Position[2] = static_cast<float>(Radius * std::sin(Angle));
            Probe.Normal[0] = static_cast<float>(Nx / Magnitude);
            Probe.Normal[1] = static_cast<float>(Ny / Magnitude);
            Probe.Normal[2] = static_cast<float>(Nz / Magnitude);
            Probe.Deformation[0] = Case.Angle;
            Probe.Deformation[1] = Case.Deflection;
            Probe.Deformation[2] = Case.Pressure;
            Probe.Deformation[3] = Case.Shift;
            Probe.Controls[0] = Case.Tyre;
            Probe.Controls[1] = Case.Model;
            Probe.Controls[2] = I % 13 == 0 ? 0 : Case.Flags;
            Probes.push_back(Probe);
        }
    return Probes;
}

void ValidateSurface(const std::vector<SurfaceProbe> &Probes, const SurfaceResult *Actual,
                     const Frontier::TyreProjectionParticle *Particles, std::uint32_t Tyres)
{
    using namespace Frontier;
    std::vector<TyreContinuumPositions> Rest(Tyres), Local(Tyres);
    for (std::uint32_t Tyre = 0; Tyre < Tyres; ++Tyre)
        for (int N = 0; N < TyreContinuumParticleCount; ++N)
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                const auto &Particle = Particles[Tyre * TyreContinuumParticleCount + N];
                Rest[Tyre][N*3+Axis] = Particle.RestInverseMass[Axis];
                Local[Tyre][N*3+Axis] = Particle.Local[Axis];
            }
    double WorstPosition[2]{}, WorstNormal[2]{}, LargestDisplacement[2]{};
    std::size_t Guarded = 0;
    for (std::size_t I = 0; I < Probes.size(); ++I)
    {
        const auto &Probe = Probes[I];
        const bool Surface = (Probe.Controls[2] & 1) != 0;
        const bool Shredded = (Probe.Controls[2] & 2) != 0;
        TyreDeformationRecord Deformation;
        Deformation.ContactAngle = Probe.Deformation[0];
        Deformation.Deflection = Probe.Deformation[1];
        Deformation.GaugePressure = Probe.Deformation[2];
        Deformation.LateralDistortion = Probe.Deformation[3];
        Deformation.Shredded = Shredded;
        float Position[3], Normal[3];
        if (Probe.Controls[1] == 0)
            ResolveDeformedWheelVertex(Deformation, Surface, Probe.Position, Probe.Normal, Position, Normal);
        else if (Probe.Controls[0] < Tyres)
            ResolveContinuumWheelVertex(Deformation, Surface, Rest[Probe.Controls[0]], Local[Probe.Controls[0]],
                                       Probe.Position, Probe.Normal, Position, Normal);
        else
        {
            for (int Axis = 0; Axis < 3; ++Axis) { Position[Axis] = Probe.Position[Axis]; Normal[Axis] = Probe.Normal[Axis]; }
        }
        const std::uint32_t Model = Probe.Controls[1];
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            if (!std::isfinite(Actual[I].Position[Axis]) || !std::isfinite(Actual[I].Normal[Axis]))
                throw std::runtime_error("Nonfinite surface result");
            WorstPosition[Model] = std::max(WorstPosition[Model], std::abs(double(Actual[I].Position[Axis]) - Position[Axis]));
            WorstNormal[Model] = std::max(WorstNormal[Model], std::abs(double(Actual[I].Normal[Axis]) - Normal[Axis]));
            if (Surface && !Shredded && Deformation.Deflection > 0.00001f && Probe.Controls[0] < Tyres)
                LargestDisplacement[Model] = std::max(LargestDisplacement[Model],
                                                       std::abs(double(Actual[I].Position[Axis]) - Probe.Position[Axis]));
            if (!Surface || Deformation.Deflection <= 0.00001f || (Model == 1 && Probe.Controls[0] >= Tyres))
            {
                if (!Shredded && (Actual[I].Position[Axis] != Probe.Position[Axis] || Actual[I].Normal[Axis] != Probe.Normal[Axis]))
                    throw std::runtime_error("Surface pass-through guard failed");
                ++Guarded;
            }
            if (Surface && Shredded && Actual[I].Position[Axis] != 0.0f)
                throw std::runtime_error("Shredded surface did not collapse");
        }
    }
    std::printf("TYRE_SURFACE_DEVICE probes=%zu cases=12 gpu_resident_particles=1 same_command_buffer=1 guards=%zu "
                "spring_position_m=%.9g spring_normal=%.9g continuum_position_m=%.9g continuum_normal=%.9g "
                "spring_motion_m=%.9g continuum_motion_m=%.9g\n", Probes.size(), Guarded,
                WorstPosition[0], WorstNormal[0], WorstPosition[1], WorstNormal[1], LargestDisplacement[0], LargestDisplacement[1]);
    for (int Model = 0; Model < 2; ++Model)
        if (WorstPosition[Model] > 0.0003 || WorstNormal[Model] > 0.0003 || LargestDisplacement[Model] < 0.005)
            throw std::runtime_error("GPU surface differs from Engine CPU projection or was not deformed");
}

int Run(int Count, char **Arguments)
{
    using namespace Frontier;
    const bool Membrane = Count == 7 && std::string(Arguments[6]) == "membrane";
    if (Count != 6 && !Membrane)
        throw std::runtime_error("Expected reset.spv advance.spv surface.spv commands.bin output.bin [membrane]");
    std::ifstream File(Arguments[4], std::ios::binary);
    if (!File) throw std::runtime_error("Cannot read fixture commands");
    std::uint32_t Tyres = 0, Steps = 0;
    File.read(reinterpret_cast<char *>(&Tyres), 4);
    File.read(reinterpret_cast<char *>(&Steps), 4);
    if (!File || Tyres < 12 || Tyres > 128 || Steps == 0 || Steps > 64)
        throw std::runtime_error("Invalid fixture dimensions");
    std::vector<TyreProjectionCommand> All(static_cast<std::size_t>(Tyres) * Steps);
    if (!File.read(reinterpret_cast<char *>(All.data()), static_cast<std::streamsize>(All.size() * sizeof(All[0]))) ||
        File.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("Invalid fixture command bytes");
    std::ofstream Output(Arguments[5], std::ios::binary);
    if (!Output) throw std::runtime_error("Cannot write fixture output");
    const auto SurfaceProbes = ConstructSurfaceProbes(Tyres);

    VulkanFixture Device;
    Device.InitializeDevice();
    Device.InitializeResources(Tyres, Arguments[1], Arguments[2], Arguments[3], static_cast<std::uint32_t>(SurfaceProbes.size()));
    std::memcpy(Device.SurfaceProbeBuffer.Mapped, SurfaceProbes.data(), SurfaceProbes.size() * sizeof(SurfaceProbes[0]));
    auto *MappedCommands = static_cast<TyreProjectionCommand *>(Device.CommandBuffer.Mapped);
    auto *Particles = static_cast<TyreProjectionParticle *>(Device.ParticleBuffer.Mapped);
    auto *Results = static_cast<TyreProjectionResult *>(Device.ResultBuffer.Mapped);
    std::memcpy(MappedCommands, All.data(), sizeof(All[0]) * Tyres);
    Device.Execute(TyreProjectionOperation::Reset, Tyres);
    std::vector<TyreCarcassProjection> Cpu(Membrane ? 0 : Tyres);
    if (!Membrane)
        for (std::uint32_t I = 0; I < Tyres; ++I) Cpu[I].Reset(Expand(All[I]));
    double WorstLocal = 0.0, WorstWorld = 0.0, WorstForce = 0.0;
    std::size_t Pinned = 0;
    for (std::uint32_t Step = 0; Step < Steps; ++Step)
    {
        const auto *Commands = All.data() + static_cast<std::size_t>(Step) * Tyres;
        for (std::uint32_t I = 0; I < Tyres; ++I)
        {
            const auto Packed = CompileTyreProjectionCommand(Expand(Commands[I]), Commands[I].PressureLateralSeconds[2],
                                                             Commands[I].IterationCounts[0]);
            if (std::memcmp(&Packed, &Commands[I], sizeof(Packed)) != 0)
                throw std::runtime_error("Fixture bytes differ from the Engine command ABI");
        }
        std::memcpy(MappedCommands, Commands, sizeof(Commands[0]) * Tyres);
        Device.Execute(TyreProjectionOperation::Advance, Tyres, Step + 1 == Steps);
        for (std::uint32_t I = 0; I < Tyres; ++I)
        {
            const auto Input = Expand(Commands[I]);
            const double Force = Membrane ? Input.Deflection * (25000.0 + 0.88 * Input.Pressure) :
                Cpu[I].Advance(Input, Commands[I].PressureLateralSeconds[2], Commands[I].IterationCounts[0]);
            if (Results[I].ForceStatus[1] != 1.0f) throw std::runtime_error("Valid GPU command rejected");
            WorstForce = std::max(WorstForce, std::abs(Force - Results[I].ForceStatus[0]));
            for (int N = 0; N < TyreContinuumParticleCount; ++N)
            {
                const auto &Particle = Particles[I * TyreContinuumParticleCount + N];
                for (int Axis = 0; Axis < 3; ++Axis)
                {
                    if (!std::isfinite(Particle.Local[Axis]) || !std::isfinite(Particle.World[Axis]))
                        throw std::runtime_error("Nonfinite GPU particle");
                    if (!Membrane)
                    {
                        WorstLocal = std::max(WorstLocal, std::abs(double(Particle.Local[Axis]) - Cpu[I].QueryLocalPositions()[N*3+Axis]));
                        WorstWorld = std::max(WorstWorld, std::abs(double(Particle.World[Axis]) - Cpu[I].QueryWorldPositions()[N*3+Axis]));
                    }
                    if (N % 5 == 0 || N % 5 == 4)
                    {
                        if (Particle.Local[Axis] != Particle.RestInverseMass[Axis])
                            throw std::runtime_error("Rim bead moved");
                        ++Pinned;
                    }
                }
            }
        }
        Output.write(reinterpret_cast<const char *>(Particles), sizeof(*Particles) * TyreContinuumParticleCount * Tyres);
        Output.write(reinterpret_cast<const char *>(Results), sizeof(*Results) * Tyres);
    }
    if (!Output) throw std::runtime_error("GPU output write failed");
    if (Membrane)
        std::printf("TYRE_MEMBRANE_FORCE tyres=%u steps=%u particles=%zu max_force_n=%.9g pinned_components=%zu cpu_membrane=0\n",
                    Tyres, Steps, std::size_t(Tyres) * Steps * TyreContinuumParticleCount, WorstForce, Pinned);
    else
        std::printf("TYRE_DEVICE_CPU tyres=%u steps=%u particles=%zu max_local_m=%.9g max_world_m=%.9g max_force_n=%.9g pinned_components=%zu\n",
                    Tyres, Steps, std::size_t(Tyres) * Steps * TyreContinuumParticleCount, WorstLocal, WorstWorld, WorstForce, Pinned);
    if ((!Membrane && (WorstLocal > 0.0003 || WorstWorld > 0.0003)) || WorstForce > 0.02)
        throw std::runtime_error("GPU differs from CPU force or projection beyond tolerance");
    ValidateSurface(SurfaceProbes, static_cast<const SurfaceResult *>(Device.SurfaceResultBuffer.Mapped), Particles, Tyres);

    // 📝 A partial final group must not process other tyres merely because their descriptor ranges are larger.
    if (Tyres > 1)
    {
        const std::vector<TyreProjectionParticle> TailParticles(Particles + TyreContinuumParticleCount,
                                                                 Particles + Tyres * TyreContinuumParticleCount);
        const std::vector<TyreProjectionResult> TailResults(Results + 1, Results + Tyres);
        MappedCommands[0] = All[1];
        Device.Execute(TyreProjectionOperation::Reset, 1);
        if (Results[0].ForceStatus[0] != 0.0f || Results[0].ForceStatus[1] != 1.0f ||
            std::memcmp(TailParticles.data(), Particles + TyreContinuumParticleCount,
                        TailParticles.size() * sizeof(TailParticles[0])) != 0 ||
            std::memcmp(TailResults.data(), Results + 1, TailResults.size() * sizeof(TailResults[0])) != 0)
            throw std::runtime_error("Partial reset modified an unrequested tyre");
        Device.Execute(TyreProjectionOperation::Advance, 1);
        if (Results[0].ForceStatus[0] <= 0.0f || Results[0].ForceStatus[1] != 1.0f ||
            std::memcmp(TailParticles.data(), Particles + TyreContinuumParticleCount,
                        TailParticles.size() * sizeof(TailParticles[0])) != 0 ||
            std::memcmp(TailResults.data(), Results + 1, TailResults.size() * sizeof(TailResults[0])) != 0)
            throw std::runtime_error("Partial advance modified an unrequested tyre");
        std::printf("TYRE_DEVICE_COUNT_BOUND requested=1 bound=%u reset_and_advance=1 untouched_tail=1\n", Tyres);
    }

    // 📝 Failed shader commands must not mutate the first tyre's state.
    std::vector<TyreProjectionParticle> Saved(Particles, Particles + TyreContinuumParticleCount);
    MappedCommands[0].IterationCounts[0] = 0;
    Device.Execute(TyreProjectionOperation::Advance, Tyres);
    if (Results[0].ForceStatus[1] != 0.0f ||
        std::memcmp(Saved.data(), Particles, Saved.size() * sizeof(Saved[0])) != 0)
        throw std::runtime_error("Invalid advance changed tyre state");
    MappedCommands[0].IterationCounts[0] = 2;
    MappedCommands[0].PressureLateralSeconds[0] = std::numeric_limits<float>::quiet_NaN();
    Device.Execute(TyreProjectionOperation::Reset, Tyres);
    if (Results[0].ForceStatus[1] != 0.0f ||
        std::memcmp(Saved.data(), Particles, Saved.size() * sizeof(Saved[0])) != 0)
        throw std::runtime_error("Invalid reset changed tyre state");

    MappedCommands[0] = All[0];
    Device.Execute(TyreProjectionOperation::Reset, Tyres);
    for (std::uint32_t I = 0; I < Tyres; ++I)
    {
        if (Results[I].ForceStatus[0] != 0.0f || Results[I].ForceStatus[1] != 1.0f)
            throw std::runtime_error("Reset did not clear the force");
        for (int N = 0; N < TyreContinuumParticleCount; ++N)
        {
            const auto &Particle = Particles[I * TyreContinuumParticleCount + N];
            for (int Axis = 0; Axis < 3; ++Axis)
                if (Particle.Local[Axis] != Particle.RestInverseMass[Axis] ||
                    Particle.Previous[Axis] != Particle.RestInverseMass[Axis] || Particle.Velocity[Axis] != 0.0f)
                    throw std::runtime_error("Reset did not restore particle state");
        }
    }
    const std::vector<TyreProjectionParticle> AllSaved(Particles, Particles + Tyres * TyreContinuumParticleCount);
    const std::vector<TyreProjectionResult> ResultsSaved(Results, Results + Tyres);
    VkDescriptorBufferInfo ShortBuffer{Device.ParticleBuffer.Handle, 0,
                                      sizeof(TyreProjectionParticle) * (TyreContinuumParticleCount - 1)};
    VkWriteDescriptorSet ShortWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    ShortWrite.dstSet = Device.Descriptors;
    ShortWrite.dstBinding = 1;
    ShortWrite.descriptorCount = 1;
    ShortWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ShortWrite.pBufferInfo = &ShortBuffer;
    vkUpdateDescriptorSets(Device.Device, 1, &ShortWrite, 0, nullptr);
    Device.Execute(TyreProjectionOperation::Advance, Tyres);
    if (std::memcmp(AllSaved.data(), Particles, AllSaved.size() * sizeof(AllSaved[0])) != 0 ||
        std::memcmp(ResultsSaved.data(), Results, ResultsSaved.size() * sizeof(ResultsSaved[0])) != 0)
        throw std::runtime_error("Undersized particle binding was not rejected");

    TyreContinuumInput Invalid;
    CheckRejected(Invalid, 0.0, 2);
    CheckRejected(Invalid, 1.0/120.0, 0);
    CheckRejected(Invalid, 1.0/120.0, 33);
    Invalid.Pressure = std::numeric_limits<double>::infinity();
    CheckRejected(Invalid, 1.0/120.0, 2);
    Invalid.Pressure = 220000.0;
    Invalid.Orientation[3] = 0.5;
    CheckRejected(Invalid, 1.0/120.0, 2);
    auto Access = Device.QueryAccess();
    Access.MaxGroupCountX = 0;
    bool Rejected = false;
    try { RecordTyreProjection(Device.Commands, Access, TyreProjectionOperation::Advance, 1); }
    catch (const std::invalid_argument &) { Rejected = true; }
    if (!Rejected) throw std::runtime_error("Dispatch group limit not enforced");
    Access = Device.QueryAccess();
    Access.PushConstants = nullptr;
    Rejected = false;
    try { RecordTyreProjection(Device.Commands, Access, TyreProjectionOperation::Advance, 1); }
    catch (const std::invalid_argument &) { Rejected = true; }
    if (!Rejected) throw std::runtime_error("Missing count push-constant entry was accepted");
    RecordTyreProjection(VK_NULL_HANDLE, {}, TyreProjectionOperation::Reset, 0);
    std::printf("TYRE_DEVICE_GUARDS invalid_gpu_advance=1 invalid_gpu_reset=1 valid_reset=1 short_particle_buffer=1 invalid_cpu_commands=5 dispatch_limit=1 missing_push=1 zero_count=1\n");
    return 0;
}
} // namespace

int main(int Count, char **Arguments)
{
    try { return Run(Count, Arguments); }
    catch (const std::exception &Error) { std::fprintf(stderr, "FAIL %s\n", Error.what()); return 1; }
}
