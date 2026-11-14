/*============================================================================================================================================
                                                       PARTICLEVULKANEXECUTOR.CPP
============================================================================================================================================*/
// 🧩 Vulkan 1.3 Compute + Raster execution routed through GeometricRaster::VulkanInterchange (hardware ICD or SwiftShader)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Execution/ParticleVulkanExecutor.h"

#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <variant>

namespace Frontier::ParticleSpace::Execution
{

namespace
{

struct DispatchPushParams
{
    uint32_t SlotOffset;
    uint32_t SlotCount;
    uint32_t ForceSourceCount;
    uint32_t SpawnCursor;
};
static_assert(sizeof(DispatchPushParams) == 16u);

[[nodiscard]] std::filesystem::path ResolveParticleShaderDir(const std::filesystem::path& ExplicitDir)
{
    if (!ExplicitDir.empty() && std::filesystem::exists(ExplicitDir / "ParticleBillboardRasterization.vert.spv"))
    {
        return ExplicitDir;
    }
    const std::array<std::filesystem::path, 4u> Candidates {
#ifdef FRONTIER_COMPILED_SHADER_DIR
        std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR),
        std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR) / "Particles",
#endif
        std::filesystem::current_path() / "CompiledShaders",
        std::filesystem::current_path() / "build/CompiledShaders",
    };
    for (const auto& Dir : Candidates)
    {
        if (std::filesystem::exists(Dir / "ParticleBillboardRasterization.vert.spv"))
        {
            return Dir;
        }
    }
    return ExplicitDir;
}

void BuildVulkanViewProjectionZUp(
    Mathematics::Axis3D Eye,
    Mathematics::Axis3D Target,
    float               FovDeg,
    float               Aspect,
    GlobalParticleUniform& OutUniform) noexcept
{
    float Fx = Target.X - Eye.X;
    float Fy = Target.Y - Eye.Y;
    float Fz = Target.Z - Eye.Z;
    float FLen = std::sqrt(std::max(1.0e-6f, Fx * Fx + Fy * Fy + Fz * Fz));
    Fx /= FLen;
    Fy /= FLen;
    Fz /= FLen;

    // Right = normalize(cross(Forward, WorldUp(0,0,1)))
    float Rx = Fy * 1.0f - Fz * 0.0f;
    float Ry = Fz * 0.0f - Fx * 1.0f;
    float Rz = Fx * 0.0f - Fy * 0.0f;
    float RLen = std::sqrt(std::max(1.0e-6f, Rx * Rx + Ry * Ry + Rz * Rz));
    if (RLen < 1.0e-4f)
    {
        Rx = 1.0f; Ry = 0.0f; Rz = 0.0f;
    }
    else
    {
        Rx /= RLen; Ry /= RLen; Rz /= RLen;
    }

    // Up = cross(Right, Forward)
    const float Ux = Ry * Fz - Rz * Fy;
    const float Uy = Rz * Fx - Rx * Fz;
    const float Uz = Rx * Fy - Ry * Fx;

    const float HalfFovRad = std::clamp(FovDeg, 10.0f, 120.0f) * (3.14159265f / 360.0f);
    const float TanHalf    = std::tan(HalfFovRad);
    const float ScaleX     = 1.0f / (TanHalf * std::max(0.1f, Aspect));
    const float ScaleY     = 1.0f / TanHalf;
    constexpr float NearZ  = 0.1f;
    constexpr float FarZ   = 250.0f;
    const float A          = FarZ / (FarZ - NearZ);
    const float B          = -(FarZ * NearZ) / (FarZ - NearZ);

    const float DotRE = Rx * Eye.X + Ry * Eye.Y + Rz * Eye.Z;
    const float DotUE = Ux * Eye.X + Uy * Eye.Y + Uz * Eye.Z;
    const float DotFE = Fx * Eye.X + Fy * Eye.Y + Fz * Eye.Z;

    // Row-major matrix transforming column vector float4(x, y, z, 1) -> Vulkan clip space (Y-down, Z in [0,1])
    OutUniform.ViewProjection[0]  = Rx * ScaleX;
    OutUniform.ViewProjection[1]  = Ry * ScaleX;
    OutUniform.ViewProjection[2]  = Rz * ScaleX;
    OutUniform.ViewProjection[3]  = -DotRE * ScaleX;

    OutUniform.ViewProjection[4]  = -Ux * ScaleY;
    OutUniform.ViewProjection[5]  = -Uy * ScaleY;
    OutUniform.ViewProjection[6]  = -Uz * ScaleY;
    OutUniform.ViewProjection[7]  = DotUE * ScaleY;

    OutUniform.ViewProjection[8]  = Fx * A;
    OutUniform.ViewProjection[9]  = Fy * A;
    OutUniform.ViewProjection[10] = Fz * A;
    OutUniform.ViewProjection[11] = -DotFE * A + B;

    OutUniform.ViewProjection[12] = Fx;
    OutUniform.ViewProjection[13] = Fy;
    OutUniform.ViewProjection[14] = Fz;
    OutUniform.ViewProjection[15] = -DotFE;

    OutUniform.CameraRight[0] = Rx;
    OutUniform.CameraRight[1] = Ry;
    OutUniform.CameraRight[2] = Rz;
    OutUniform.CameraUp[0]    = Ux;
    OutUniform.CameraUp[1]    = Uy;
    OutUniform.CameraUp[2]    = Uz;
    OutUniform.CameraPosition[0] = Eye.X;
    OutUniform.CameraPosition[1] = Eye.Y;
    OutUniform.CameraPosition[2] = Eye.Z;
    OutUniform.CameraPosition[3] = HalfFovRad * 2.0f;
}

[[nodiscard]] bool RigUsesDirectCpuSync(const ParticleRig& Rig) noexcept
{
    return std::holds_alternative<ClothGridOriginSpec>(Rig.Origin) ||
           std::holds_alternative<BotanicalRootOriginSpec>(Rig.Origin) ||
           std::holds_alternative<CanopySkeletonOriginSpec>(Rig.Origin) ||
           std::holds_alternative<SpiderwebOriginSpec>(Rig.Origin) ||
           std::holds_alternative<SurfaceAnchorOriginSpec>(Rig.Origin) ||
           std::holds_alternative<FireworkPatternOriginSpec>(Rig.Origin) ||
           (Rig.Dynamics.Ferrofluid.FerroMode != 0u) ||
           (Rig.Dynamics.Fracture.CrackMode != 0u) ||
           (Rig.Dynamics.WaterSplash.SplashMode != 0u) ||
           (Rig.Dynamics.EffectorGrid.Active) ||
           (Rig.Dynamics.Shield.Active) ||
           (Rig.Dynamics.Pyro.FlareSalvoMode != 0u) ||
           (Rig.Dynamics.Pyro.BeamModeActive > 0.0f) ||
           (Rig.Dynamics.Transition.Mode != TransitionDerezMode::Off) ||
           (Rig.Dynamics.Botanical.SeedHeadMode != 0u);
}

void PackForceSourcesForGpu(
    const SimulationSpace::ForceFields::ForceFieldAccumulator& Accumulator,
    double                                                     TimeSeconds,
    std::vector<SimulationSpace::ForceFields::ForceSourceShaderEntry>& OutEntries)
{
    OutEntries.clear();
    const auto Sources = Accumulator.QuerySources();
    const std::size_t Count = std::min<std::size_t>(Sources.size(), MaxActiveForceUniformSlots);
    OutEntries.reserve(Count);

    for (std::size_t I = 0u; I < Count; ++I)
    {
        const auto& Src = Sources[I];
        SimulationSpace::ForceFields::ForceSourceShaderEntry Gpu{};
        Gpu.CentreX          = Src.EvaluatedCentreMeters.X;
        Gpu.CentreY          = Src.EvaluatedCentreMeters.Y;
        Gpu.CentreZ          = Src.EvaluatedCentreMeters.Z;
        Gpu.RadiusMeters     = Src.RadiusMeters;
        Gpu.DirectionX       = 1.0f;
        Gpu.DirectionY       = 0.0f;
        Gpu.DirectionZ       = 0.0f;
        Gpu.Strength         = Src.Strength;
        Gpu.TangentLeftX     = 0.0f;
        Gpu.TangentLeftY     = 1.0f;
        Gpu.TangentLeftZ     = 0.0f;
        Gpu.FalloffExponent  = Src.FalloffExponent;
        Gpu.UpX              = 0.0f;
        Gpu.UpY              = 0.0f;
        Gpu.UpZ              = 1.0f;
        Gpu.CoreRadiusMeters = Src.CoreRadiusMeters;
        Gpu.HalfExtentX      = Src.HalfExtentsMeters.X;
        Gpu.HalfExtentY      = Src.HalfExtentsMeters.Y;
        Gpu.HalfExtentZ      = Src.HalfExtentsMeters.Z;
        Gpu.TurbulenceScaleM = Src.TurbulenceScaleMeters;
        Gpu.FrequencyHertz   = Src.FrequencyHertz;
        Gpu.SwirlRatio       = Src.SwirlRatio;
        Gpu.BuoyancyRatio    = Src.BuoyancyDensityRatio;
        Gpu.DragCoefficient  = Src.DragCoefficient;
        Gpu.ModelId          = static_cast<uint32_t>(Src.Model);
        Gpu.StreamProfileId  = static_cast<uint32_t>(Src.StreamProfile);
        Gpu.AttenuationId    = static_cast<uint32_t>(Src.Attenuation);
        Gpu.EnvelopeId       = static_cast<uint32_t>(Src.Envelope);
        Gpu.DurationId       = static_cast<uint32_t>(Src.DurationMode);
        Gpu.OctaveCount      = Src.OctaveCount;
        Gpu.Seed             = Src.Seed;
        Gpu.FlagMask         = Src.Enabled ? 1u : 0u;
        Gpu.ImpulsePhase01   = 0.0f;
        Gpu.TimeSeconds      = static_cast<float>(TimeSeconds);
        OutEntries.push_back(Gpu);
    }
}

} // namespace

ParticleVulkanExecutor::~ParticleVulkanExecutor()
{
    TeardownExecutor();
}

bool ParticleVulkanExecutor::InitializeExecutor(
    GeometricRaster::VulkanInterchange* BoundDevice,
    const ParticleDocument&             Document,
    const std::filesystem::path&        ShaderDirectory)
{
    TeardownExecutor();
    DeviceOwner = BoundDevice;
    if (DeviceOwner == nullptr || !DeviceOwner->Ready())
    {
        return false;
    }

    const std::filesystem::path ShaderDir = ResolveParticleShaderDir(ShaderDirectory);
    if (!CreateGpuExecutables(ShaderDir))
    {
        TeardownExecutor();
        return false;
    }

    // Allocate persistent host-visible buffers for immediate compute + graphics execution
    AllocatedParticleSlots = std::max(8192u, Document.QueryTotalAllocatedSlots());
    AllocatedStrandVerts   = 4096u * MaxRibbonStrandVertices;

    const VkMemoryPropertyFlags HostVis = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    GlobalUniformBuf = DeviceOwner->AllocateBuffer(sizeof(GlobalParticleUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, HostVis);
    RigUniformBuf    = DeviceOwner->AllocateBuffer(sizeof(RigParticleUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, HostVis);
    ColourLutBuf     = DeviceOwner->AllocateBuffer(sizeof(ColourSpace::ColourRampLut64), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, HostVis);
    ParticleSlotBuf  = DeviceOwner->AllocateBuffer(
        static_cast<uint64_t>(AllocatedParticleSlots) * sizeof(ParticleSlot),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        HostVis);
    StrandVertexBuf  = DeviceOwner->AllocateBuffer(
        static_cast<uint64_t>(AllocatedStrandVerts) * sizeof(StrandVertex),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        HostVis);
    ForceSourceBuf   = DeviceOwner->AllocateBuffer(
        MaxActiveForceUniformSlots * sizeof(SimulationSpace::ForceFields::ForceSourceShaderEntry),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        HostVis);
    ForceWaypointBuf = DeviceOwner->AllocateBuffer(256ull * 16ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, HostVis);
    TelemetryBuf     = DeviceOwner->AllocateBuffer(sizeof(ParticleTelemetryCounters), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, HostVis);

    if (GlobalUniformBuf.BufferHandle == VK_NULL_HANDLE || RigUniformBuf.BufferHandle == VK_NULL_HANDLE ||
        ColourLutBuf.BufferHandle == VK_NULL_HANDLE || ParticleSlotBuf.BufferHandle == VK_NULL_HANDLE ||
        StrandVertexBuf.BufferHandle == VK_NULL_HANDLE || ForceSourceBuf.BufferHandle == VK_NULL_HANDLE ||
        ForceWaypointBuf.BufferHandle == VK_NULL_HANDLE || TelemetryBuf.BufferHandle == VK_NULL_HANDLE)
    {
        TeardownExecutor();
        return false;
    }

    // Update descriptor set bindings 0..7
    const std::array<VkDescriptorBufferInfo, 8u> BufInfos {{
        { GlobalUniformBuf.BufferHandle, 0ull, sizeof(GlobalParticleUniform) },
        { RigUniformBuf.BufferHandle,    0ull, sizeof(RigParticleUniform) },
        { ColourLutBuf.BufferHandle,     0ull, sizeof(ColourSpace::ColourRampLut64) },
        { ParticleSlotBuf.BufferHandle,  0ull, VK_WHOLE_SIZE },
        { StrandVertexBuf.BufferHandle,  0ull, VK_WHOLE_SIZE },
        { ForceSourceBuf.BufferHandle,   0ull, VK_WHOLE_SIZE },
        { ForceWaypointBuf.BufferHandle, 0ull, VK_WHOLE_SIZE },
        { TelemetryBuf.BufferHandle,     0ull, sizeof(ParticleTelemetryCounters) },
    }};
    const std::array<VkDescriptorType, 8u> DescTypes {{
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    }};

    std::array<VkWriteDescriptorSet, 8u> Writes{};
    for (uint32_t I = 0u; I < 8u; ++I)
    {
        Writes[I].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[I].dstSet          = UnifiedDescriptorSlot;
        Writes[I].dstBinding      = I;
        Writes[I].descriptorCount = 1u;
        Writes[I].descriptorType  = DescTypes[I];
        Writes[I].pBufferInfo     = &BufInfos[I];
    }
    vkUpdateDescriptorSets(DeviceOwner->AccessDevice(), 8u, Writes.data(), 0u, nullptr);

    if (!ResetDocument(Document))
    {
        TeardownExecutor();
        return false;
    }

    Initialized = true;
    return true;
}

void ParticleVulkanExecutor::TeardownExecutor() noexcept
{
    if (DeviceOwner != nullptr && DeviceOwner->Ready())
    {
        VkDevice Dev = DeviceOwner->AccessDevice();
        vkDeviceWaitIdle(Dev);

        DestroyRenderTargets();

        for (RasterExecutableUnit& Unit : RasterUnits)
        {
            if (Unit.Executable != VK_NULL_HANDLE) vkDestroyPipeline(Dev, Unit.Executable, nullptr);
            if (Unit.VertexBinary != VK_NULL_HANDLE) vkDestroyShaderModule(Dev, Unit.VertexBinary, nullptr);
            if (Unit.FragmentBinary != VK_NULL_HANDLE) vkDestroyShaderModule(Dev, Unit.FragmentBinary, nullptr);
            Unit = RasterExecutableUnit{};
        }
        for (uint32_t I = 0u; I < 4u; ++I)
        {
            if (ComputeUnits[I] != VK_NULL_HANDLE) vkDestroyPipeline(Dev, ComputeUnits[I], nullptr);
            if (ComputeBinaries[I] != VK_NULL_HANDLE) vkDestroyShaderModule(Dev, ComputeBinaries[I], nullptr);
            ComputeUnits[I] = VK_NULL_HANDLE;
            ComputeBinaries[I] = VK_NULL_HANDLE;
        }
        if (OffscreenTargetScope != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(Dev, OffscreenTargetScope, nullptr);
            OffscreenTargetScope = VK_NULL_HANDLE;
        }
        if (UnifiedLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(Dev, UnifiedLayout, nullptr);
            UnifiedLayout = VK_NULL_HANDLE;
        }
        if (UnifiedDescriptorBank != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(Dev, UnifiedDescriptorBank, nullptr);
            UnifiedDescriptorBank = VK_NULL_HANDLE;
            UnifiedDescriptorSlot = VK_NULL_HANDLE;
        }
        if (UnifiedSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(Dev, UnifiedSetLayout, nullptr);
            UnifiedSetLayout = VK_NULL_HANDLE;
        }

        DeviceOwner->ReleaseBuffer(GlobalUniformBuf);
        DeviceOwner->ReleaseBuffer(RigUniformBuf);
        DeviceOwner->ReleaseBuffer(ColourLutBuf);
        DeviceOwner->ReleaseBuffer(ParticleSlotBuf);
        DeviceOwner->ReleaseBuffer(StrandVertexBuf);
        DeviceOwner->ReleaseBuffer(ForceSourceBuf);
        DeviceOwner->ReleaseBuffer(ForceWaypointBuf);
        DeviceOwner->ReleaseBuffer(TelemetryBuf);
        DeviceOwner->ReleaseBuffer(StagingReadbackBuf);
    }

    DeviceOwner            = nullptr;
    AllocatedParticleSlots = 0u;
    AllocatedStrandVerts   = 0u;
    Initialized            = false;
}

bool ParticleVulkanExecutor::CreateGpuExecutables(const std::filesystem::path& ShaderDir)
{
    VkDevice Dev = DeviceOwner->AccessDevice();

    // 1. Unified descriptor set layout (0..7)
    const std::array<VkDescriptorSetLayoutBinding, 8u> Bindings {{
        { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_ALL, nullptr },
        { 1u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_ALL, nullptr },
        { 2u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_ALL, nullptr },
        { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_ALL, nullptr },
        { 4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_ALL, nullptr },
        { 5u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_ALL, nullptr },
        { 6u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_ALL, nullptr },
        { 7u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_ALL, nullptr },
    }};
    VkDescriptorSetLayoutCreateInfo LayoutInfo{};
    LayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    LayoutInfo.bindingCount = static_cast<uint32_t>(Bindings.size());
    LayoutInfo.pBindings    = Bindings.data();
    if (vkCreateDescriptorSetLayout(Dev, &LayoutInfo, nullptr, &UnifiedSetLayout) != VK_SUCCESS)
    {
        return false;
    }

    const std::array<VkDescriptorPoolSize, 2u> DescriptorCapacities {{
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4u },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8u },
    }};
    VkDescriptorPoolCreateInfo DescriptorBankInfo{};
    DescriptorBankInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    DescriptorBankInfo.maxSets       = 1u;
    DescriptorBankInfo.poolSizeCount = static_cast<uint32_t>(DescriptorCapacities.size());
    DescriptorBankInfo.pPoolSizes    = DescriptorCapacities.data();
    if (vkCreateDescriptorPool(Dev, &DescriptorBankInfo, nullptr, &UnifiedDescriptorBank) != VK_SUCCESS)
    {
        return false;
    }

    VkDescriptorSetAllocateInfo AllocInfo{};
    AllocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    AllocInfo.descriptorPool     = UnifiedDescriptorBank;
    AllocInfo.descriptorSetCount = 1u;
    AllocInfo.pSetLayouts        = &UnifiedSetLayout;
    if (vkAllocateDescriptorSets(Dev, &AllocInfo, &UnifiedDescriptorSlot) != VK_SUCCESS)
    {
        return false;
    }

    VkPushConstantRange PushRange{};
    PushRange.stageFlags = VK_SHADER_STAGE_ALL;
    PushRange.offset     = 0u;
    PushRange.size       = sizeof(DispatchPushParams);

    VkPipelineLayoutCreateInfo PipeLayoutInfo{};
    PipeLayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    PipeLayoutInfo.setLayoutCount         = 1u;
    PipeLayoutInfo.pSetLayouts            = &UnifiedSetLayout;
    PipeLayoutInfo.pushConstantRangeCount = 1u;
    PipeLayoutInfo.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Dev, &PipeLayoutInfo, nullptr, &UnifiedLayout) != VK_SUCCESS)
    {
        return false;
    }

    // 2. Compute executables (4)
    const std::array<const char*, 4u> ComputeSpvs {{
        "ParticleReleaseCompute.comp.spv",
        "ParticleAdvectCompute.comp.spv",
        "ParticleSpatialGridCompute.comp.spv",
        "ParticleReductionCompute.comp.spv",
    }};
    for (uint32_t I = 0u; I < 4u; ++I)
    {
        ComputeBinaries[I] = DeviceOwner->LoadShaderBinary(ShaderDir / ComputeSpvs[I]);
        if (ComputeBinaries[I] == VK_NULL_HANDLE)
        {
            return false;
        }
        VkComputePipelineCreateInfo CpInfo{};
        CpInfo.sType       = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        CpInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        CpInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        CpInfo.stage.module = ComputeBinaries[I];
        CpInfo.stage.pName = "ComputeEntry";
        CpInfo.layout      = UnifiedLayout;
        if (vkCreateComputePipelines(Dev, VK_NULL_HANDLE, 1u, &CpInfo, nullptr, &ComputeUnits[I]) != VK_SUCCESS)
        {
            return false;
        }
    }

    // 3. Offscreen RenderPass (RGBA8 color + D32 depth)
    std::array<VkAttachmentDescription, 2u> Attachments{};
    Attachments[0].format         = VK_FORMAT_R8G8B8A8_UNORM;
    Attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    Attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    Attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    Attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Attachments[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Attachments[0].finalLayout    = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    Attachments[1].format         = VK_FORMAT_D32_SFLOAT;
    Attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    Attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    Attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    Attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    Attachments[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    Attachments[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference ColorRef{ 0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference DepthRef{ 1u, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription Subpass{};
    Subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    Subpass.colorAttachmentCount    = 1u;
    Subpass.pColorAttachments       = &ColorRef;
    Subpass.pDepthStencilAttachment = &DepthRef;

    std::array<VkSubpassDependency, 2u> Deps{};
    Deps[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
    Deps[0].dstSubpass      = 0u;
    Deps[0].srcStageMask    = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    Deps[0].dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    Deps[0].srcAccessMask   = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    Deps[0].dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    Deps[1].srcSubpass      = 0u;
    Deps[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
    Deps[1].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    Deps[1].dstStageMask    = VK_PIPELINE_STAGE_TRANSFER_BIT;
    Deps[1].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    Deps[1].dstAccessMask   = VK_ACCESS_TRANSFER_READ_BIT;

    VkRenderPassCreateInfo RpInfo{};
    RpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    RpInfo.attachmentCount = static_cast<uint32_t>(Attachments.size());
    RpInfo.pAttachments    = Attachments.data();
    RpInfo.subpassCount    = 1u;
    RpInfo.pSubpasses      = &Subpass;
    RpInfo.dependencyCount = static_cast<uint32_t>(Deps.size());
    RpInfo.pDependencies   = Deps.data();
    if (vkCreateRenderPass(Dev, &RpInfo, nullptr, &OffscreenTargetScope) != VK_SUCCESS)
    {
        return false;
    }

    // 4. Graphics raster executables (9 slots across 8 shader pairs)
    struct RasterShaderPairDesc
    {
        const char*        VertSpv;
        const char*        FragSpv;
        VkPrimitiveTopology Topology;
        bool               AdditiveBlend;
        bool               DepthWrite;
    };
    const std::array<RasterShaderPairDesc, 9u> PairDescs {{
        { "ParticleBillboardRasterization.vert.spv",      "ParticleBillboardRasterization.frag.spv",      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true,  false }, // BillboardAdditive
        { "ParticleBillboardRasterization.vert.spv",      "ParticleBillboardRasterization.frag.spv",      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, false, false }, // BillboardAlpha
        { "ParticleRibbonRasterization.vert.spv",         "ParticleRibbonRasterization.frag.spv",         VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, false, true  }, // Ribbon
        { "ParticleSolidRasterization.vert.spv",          "ParticleSolidRasterization.frag.spv",          VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, false, true  }, // Solid
        { "ParticleWingedRasterization.vert.spv",         "ParticleWingedRasterization.frag.spv",         VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, false, true  }, // Winged
        { "ParticleLineArcRasterization.vert.spv",        "ParticleLineArcRasterization.frag.spv",        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true,  false }, // LineArc
        { "ParticleLensRasterization.vert.spv",           "ParticleLensRasterization.frag.spv",           VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, false, false }, // Lens
        { "ParticleSurfaceBackingRasterization.vert.spv", "ParticleSurfaceBackingRasterization.frag.spv", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, false, true  }, // SurfaceBacking
        { "ParticleOverlayRasterization.vert.spv",        "ParticleOverlayRasterization.frag.spv",        VK_PRIMITIVE_TOPOLOGY_LINE_LIST,     false, true  }, // OverlayGrid
    }};

    for (uint32_t I = 0u; I < 9u; ++I)
    {
        RasterExecutableUnit& Unit = RasterUnits[I];
        Unit.VertexBinary   = DeviceOwner->LoadShaderBinary(ShaderDir / PairDescs[I].VertSpv);
        Unit.FragmentBinary = DeviceOwner->LoadShaderBinary(ShaderDir / PairDescs[I].FragSpv);
        if (Unit.VertexBinary == VK_NULL_HANDLE || Unit.FragmentBinary == VK_NULL_HANDLE)
        {
            return false;
        }

        std::array<VkPipelineShaderStageCreateInfo, 2u> Stages{};
        Stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        Stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        Stages[0].module = Unit.VertexBinary;
        Stages[0].pName  = "VertexEntry";

        Stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        Stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        Stages[1].module = Unit.FragmentBinary;
        Stages[1].pName  = "FragmentEntry";

        VkPipelineVertexInputStateCreateInfo VertInput{};
        VertInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo InputAsm{};
        InputAsm.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        InputAsm.topology = PairDescs[I].Topology;

        VkPipelineViewportStateCreateInfo ViewportDesc{};
        ViewportDesc.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        ViewportDesc.viewportCount = 1u;
        ViewportDesc.scissorCount  = 1u;

        VkPipelineRasterizationStateCreateInfo RasterDesc{};
        RasterDesc.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        RasterDesc.polygonMode = VK_POLYGON_MODE_FILL;
        RasterDesc.cullMode    = VK_CULL_MODE_NONE;
        RasterDesc.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        RasterDesc.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo Msaa{};
        Msaa.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        Msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo DepthStencilDesc{};
        DepthStencilDesc.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        DepthStencilDesc.depthTestEnable  = VK_TRUE;
        DepthStencilDesc.depthWriteEnable = PairDescs[I].DepthWrite ? VK_TRUE : VK_FALSE;
        DepthStencilDesc.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPipelineColorBlendAttachmentState BlendAtt{};
        BlendAtt.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        BlendAtt.blendEnable         = VK_TRUE;
        BlendAtt.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        BlendAtt.dstColorBlendFactor = PairDescs[I].AdditiveBlend ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        BlendAtt.colorBlendOp        = VK_BLEND_OP_ADD;
        BlendAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        BlendAtt.dstAlphaBlendFactor = PairDescs[I].AdditiveBlend ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        BlendAtt.alphaBlendOp        = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo ColorBlendDesc{};
        ColorBlendDesc.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        ColorBlendDesc.attachmentCount = 1u;
        ColorBlendDesc.pAttachments    = &BlendAtt;

        const std::array<VkDynamicState, 2u> DynStates {{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR }};
        VkPipelineDynamicStateCreateInfo DynInfo{};
        DynInfo.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        DynInfo.dynamicStateCount = static_cast<uint32_t>(DynStates.size());
        DynInfo.pDynamicStates    = DynStates.data();

        VkGraphicsPipelineCreateInfo GpInfo{};
        GpInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        GpInfo.stageCount          = static_cast<uint32_t>(Stages.size());
        GpInfo.pStages             = Stages.data();
        GpInfo.pVertexInputState   = &VertInput;
        GpInfo.pInputAssemblyState = &InputAsm;
        GpInfo.pViewportState      = &ViewportDesc;
        GpInfo.pRasterizationState = &RasterDesc;
        GpInfo.pMultisampleState   = &Msaa;
        GpInfo.pDepthStencilState  = &DepthStencilDesc;
        GpInfo.pColorBlendState    = &ColorBlendDesc;
        GpInfo.pDynamicState       = &DynInfo;
        GpInfo.layout              = UnifiedLayout;
        GpInfo.renderPass          = OffscreenTargetScope;
        if (vkCreateGraphicsPipelines(Dev, VK_NULL_HANDLE, 1u, &GpInfo, nullptr, &Unit.Executable) != VK_SUCCESS)
        {
            return false;
        }
    }

    return true;
}

void ParticleVulkanExecutor::DestroyRenderTargets() noexcept
{
    if (DeviceOwner == nullptr || !DeviceOwner->Ready())
    {
        return;
    }
    VkDevice Dev = DeviceOwner->AccessDevice();
    if (OffscreenFramebuffer != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(Dev, OffscreenFramebuffer, nullptr);
        OffscreenFramebuffer = VK_NULL_HANDLE;
    }
    DeviceOwner->ReleaseImage2D(ColorTargetImg);
    DeviceOwner->ReleaseImage2D(DepthTargetImg);
    TargetWidthPx  = 0u;
    TargetHeightPx = 0u;
}

bool ParticleVulkanExecutor::EnsureRenderTargets(uint32_t WidthPx, uint32_t HeightPx)
{
    if (TargetWidthPx == WidthPx && TargetHeightPx == HeightPx && OffscreenFramebuffer != VK_NULL_HANDLE)
    {
        return true;
    }

    DestroyRenderTargets();
    ColorTargetImg = DeviceOwner->AllocateImage2D(
        WidthPx,
        HeightPx,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
    DepthTargetImg = DeviceOwner->AllocateImage2D(
        WidthPx,
        HeightPx,
        VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);
    if (ColorTargetImg.ViewHandle == VK_NULL_HANDLE || DepthTargetImg.ViewHandle == VK_NULL_HANDLE)
    {
        DestroyRenderTargets();
        return false;
    }

    const std::array<VkImageView, 2u> Views {{ ColorTargetImg.ViewHandle, DepthTargetImg.ViewHandle }};
    VkFramebufferCreateInfo FbInfo{};
    FbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    FbInfo.renderPass      = OffscreenTargetScope;
    FbInfo.attachmentCount = static_cast<uint32_t>(Views.size());
    FbInfo.pAttachments    = Views.data();
    FbInfo.width           = WidthPx;
    FbInfo.height          = HeightPx;
    FbInfo.layers          = 1u;
    if (vkCreateFramebuffer(DeviceOwner->AccessDevice(), &FbInfo, nullptr, &OffscreenFramebuffer) != VK_SUCCESS)
    {
        DestroyRenderTargets();
        return false;
    }

    const uint64_t ReadbackBytes = static_cast<uint64_t>(WidthPx) * HeightPx * 4ull;
    if (StagingReadbackBuf.ByteCapacity < ReadbackBytes)
    {
        DeviceOwner->ReleaseBuffer(StagingReadbackBuf);
        StagingReadbackBuf = DeviceOwner->AllocateBuffer(
            ReadbackBytes,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (StagingReadbackBuf.BufferHandle == VK_NULL_HANDLE)
        {
            DestroyRenderTargets();
            return false;
        }
    }

    TargetWidthPx  = WidthPx;
    TargetHeightPx = HeightPx;
    return true;
}

bool ParticleVulkanExecutor::ResetDocument(const ParticleDocument& Document)
{
    ElapsedSeconds = 0.0;
    StepCount      = 0ull;
    MetricsCollector.Reset();
    SoftwareParity.Initialize(Document);

    if (ParticleSlotBuf.HostAddress != nullptr)
    {
        auto* Dst = static_cast<ParticleSlot*>(ParticleSlotBuf.HostAddress);
        for (uint32_t I = 0u; I < AllocatedParticleSlots; ++I)
        {
            Dst[I] = ParticleSlot{};
            Dst[I].PositionAge[3]      = 1000.0f;
            Dst[I].VelocityLifetime[3] = 1.0f;
        }
    }
    return true;
}

bool ParticleVulkanExecutor::AdvanceRenderStep(
    const ParticleDocument&           Document,
    float                             DeltaSeconds,
    const ParticleRenderViewportSpec& ViewportSpec)
{
    if (!Initialized || DeviceOwner == nullptr || !DeviceOwner->Ready())
    {
        return false;
    }
    if (!EnsureRenderTargets(std::max(64u, ViewportSpec.WidthPx), std::max(64u, ViewportSpec.HeightPx)))
    {
        return false;
    }

    const auto T0 = std::chrono::steady_clock::now();
    const float Dt = Document.PlaybackPaused ? 0.0f : std::clamp(DeltaSeconds * Document.TimeScale, 1.0e-4f, 0.1f);
    if (!Document.PlaybackPaused)
    {
        ElapsedSeconds += static_cast<double>(Dt);
        ++StepCount;
        SoftwareParity.Step(Document, DeltaSeconds, nullptr);
    }

    // 1. Update GlobalParticleUniform
    GlobalParticleUniform GlobalU{};
    BuildVulkanViewProjectionZUp(
        Document.ViewportCamera.EyePositionMeters,
        Document.ViewportCamera.TargetMeters,
        Document.ViewportCamera.FieldOfViewDeg,
        static_cast<float>(TargetWidthPx) / static_cast<float>(TargetHeightPx),
        GlobalU);
    GlobalU.TimingMetrics[0] = Dt;
    GlobalU.TimingMetrics[1] = static_cast<float>(ElapsedSeconds);
    GlobalU.TimingMetrics[2] = Document.TimeScale;
    GlobalU.TimingMetrics[3] = static_cast<float>(StepCount);
    GlobalU.ColliderBallPose[0] = Document.Collider.BallPositionMeters.X;
    GlobalU.ColliderBallPose[1] = Document.Collider.BallPositionMeters.Y;
    GlobalU.ColliderBallPose[2] = Document.Collider.BallPositionMeters.Z;
    GlobalU.ColliderBallPose[3] = Document.Collider.BallRadiusMeters;
    GlobalU.ColliderBallVelocity[0] = Document.Collider.BallVelocityMps.X;
    GlobalU.ColliderBallVelocity[1] = Document.Collider.BallVelocityMps.Y;
    GlobalU.ColliderBallVelocity[2] = Document.Collider.BallVelocityMps.Z;
    GlobalU.ColliderBallVelocity[3] = Document.Collider.ImpactImpulseScale;
    std::memcpy(GlobalUniformBuf.HostAddress, &GlobalU, sizeof(GlobalParticleUniform));

    // Upload coupled ForceSources
    std::vector<SimulationSpace::ForceFields::ForceSourceShaderEntry> GpuForces;
    PackForceSourcesForGpu(SoftwareParity.QueryForceAccumulator(), ElapsedSeconds, GpuForces);
    if (!GpuForces.empty())
    {
        std::memcpy(
            ForceSourceBuf.HostAddress,
            GpuForces.data(),
            GpuForces.size() * sizeof(SimulationSpace::ForceFields::ForceSourceShaderEntry));
    }

    // Reset telemetry buffer before reduction
    ParticleTelemetryCounters InitTel{};
    InitTel.BoundsMinFixedX = 0xFFFFFFFFu;
    InitTel.BoundsMinFixedY = 0xFFFFFFFFu;
    InitTel.BoundsMinFixedZ = 0xFFFFFFFFu;
    std::memcpy(TelemetryBuf.HostAddress, &InitTel, sizeof(ParticleTelemetryCounters));

    // Identify active rigs (first enabled rig drives compute slots + draw in current pass; multi-rig draws all)
    struct RigDrawEntry
    {
        const ParticleRig* Rig        = nullptr;
        uint32_t           SlotOffset = 0u;
        uint32_t           SlotCount  = 0u;
        uint32_t           StrandCount= 0u;
        uint32_t           StrandVerts= 0u;
        bool               CpuSynced  = false;
    };
    std::vector<RigDrawEntry> ActiveRigs;
    uint32_t RunningOffset = 0u;
    uint32_t TotalStrandVertOffset = 0u;

    bool HasSolo = false;
    for (const ParticleRig& R : Document.Rigs)
    {
        if (R.Solo)
        {
            HasSolo = true;
            break;
        }
    }

    for (const ParticleRig& Rig : Document.Rigs)
    {
        if (!Rig.Enabled || Rig.Quarantined || (HasSolo && !Rig.Solo))
        {
            continue;
        }
        const uint32_t Slots = std::min<uint32_t>(Rig.Sizing.CapacitySlots, AllocatedParticleSlots - RunningOffset);
        if (Slots == 0u)
        {
            break;
        }

        RigDrawEntry Entry{};
        Entry.Rig        = &Rig;
        Entry.SlotOffset = RunningOffset;
        Entry.SlotCount  = Slots;
        Entry.CpuSynced  = RigUsesDirectCpuSync(Rig);

        if (const auto* Work = SoftwareParity.FindWorkspace(Rig.RigId))
        {
            if (Entry.CpuSynced && !Work->Slots.empty())
            {
                const uint32_t CopyCount = std::min<uint32_t>(Slots, static_cast<uint32_t>(Work->Slots.size()));
                std::memcpy(
                    static_cast<ParticleSlot*>(ParticleSlotBuf.HostAddress) + RunningOffset,
                    Work->Slots.data(),
                    CopyCount * sizeof(ParticleSlot));
            }
            if (!Work->StrandVertices.empty() && TotalStrandVertOffset + Work->StrandVertices.size() <= AllocatedStrandVerts)
            {
                std::memcpy(
                    static_cast<StrandVertex*>(StrandVertexBuf.HostAddress) + TotalStrandVertOffset,
                    Work->StrandVertices.data(),
                    Work->StrandVertices.size() * sizeof(StrandVertex));
                Entry.StrandCount = static_cast<uint32_t>(Work->StrandAnchors.size());
                Entry.StrandVerts = Entry.StrandCount > 0u
                                        ? static_cast<uint32_t>(Work->StrandVertices.size() / Entry.StrandCount)
                                        : 0u;
                TotalStrandVertOffset += static_cast<uint32_t>(Work->StrandVertices.size());
            }
        }
        ActiveRigs.push_back(Entry);
        RunningOffset += Slots;
    }

    // Upload primary rig uniform + LUT before command buffer execution
    if (!ActiveRigs.empty())
    {
        const RigDrawEntry& Primary = ActiveRigs[0];
        RigParticleUniform RigU = Primary.Rig->PackGpuUniform(Primary.SlotOffset, Primary.SlotCount);
        RigU.ReleaseBudget[3] = (!Document.PlaybackPaused && !Primary.CpuSynced)
                                    ? std::max(1.0f, std::floor(Primary.Rig->Sizing.ReleaseRatePerSec * Dt))
                                    : 0.0f;
        if (Primary.StrandVerts > 0u)
        {
            RigU.StrandAndBlend[0] = static_cast<float>(Primary.StrandVerts);
        }
        std::memcpy(RigUniformBuf.HostAddress, &RigU, sizeof(RigParticleUniform));
        std::memcpy(ColourLutBuf.HostAddress, Primary.Rig->BakedColourLut.Samples.data(), sizeof(ColourSpace::ColourRampLut64));
    }

    // 2. Record Vulkan Compute + Graphics CommandBuffer
    VkCommandBuffer Cmd = DeviceOwner->BeginSingleTimeCommands();
    if (Cmd == VK_NULL_HANDLE)
    {
        return false;
    }

    for (const RigDrawEntry& Entry : ActiveRigs)
    {
        const DispatchPushParams Push{
            Entry.SlotOffset,
            Entry.SlotCount,
            static_cast<uint32_t>(GpuForces.size()),
            static_cast<uint32_t>(StepCount * 16u),
        };

        if (!Document.PlaybackPaused && !Entry.CpuSynced)
        {
            vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, UnifiedLayout, 0u, 1u, &UnifiedDescriptorSlot, 0u, nullptr);
            vkCmdPushConstants(Cmd, UnifiedLayout, VK_SHADER_STAGE_ALL, 0u, sizeof(Push), &Push);

            // Release
            vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ComputeUnits[0]);
            vkCmdDispatch(Cmd, std::max(1u, (Entry.SlotCount + 63u) / 64u), 1u, 1u);

            VkMemoryBarrier MemBar{};
            MemBar.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            MemBar.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            MemBar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 1u, &MemBar, 0u, nullptr, 0u, nullptr);

            // Advect or SpatialGrid
            if (Entry.Rig->Dynamics.Boids.Active || Entry.Rig->Dynamics.Molecular.Active)
            {
                vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ComputeUnits[2]);
            }
            else
            {
                vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ComputeUnits[1]);
            }
            vkCmdDispatch(Cmd, std::max(1u, (Entry.SlotCount + 63u) / 64u), 1u, 1u);
            vkCmdPipelineBarrier(Cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 1u, &MemBar, 0u, nullptr, 0u, nullptr);
        }

        // Reduction telemetry
        vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, UnifiedLayout, 0u, 1u, &UnifiedDescriptorSlot, 0u, nullptr);
        vkCmdPushConstants(Cmd, UnifiedLayout, VK_SHADER_STAGE_ALL, 0u, sizeof(Push), &Push);
        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ComputeUnits[3]);
        vkCmdDispatch(Cmd, std::max(1u, (Entry.SlotCount + 63u) / 64u), 1u, 1u);
    }

    // Barrier: Compute writes -> Vertex shader reads
    VkMemoryBarrier ComputeToVert{};
    ComputeToVert.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    ComputeToVert.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    ComputeToVert.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(
        Cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        0u,
        1u,
        &ComputeToVert,
        0u,
        nullptr,
        0u,
        nullptr);

    // 3. Begin Offscreen Graphics RenderPass
    std::array<VkClearValue, 2u> Clears{};
    Clears[0].color        = { { 0.045f, 0.052f, 0.082f, 1.0f } };
    Clears[1].depthStencil = { 1.0f, 0u };

    VkRenderPassBeginInfo RpBegin{};
    RpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    RpBegin.renderPass        = OffscreenTargetScope;
    RpBegin.framebuffer       = OffscreenFramebuffer;
    RpBegin.renderArea.extent = { TargetWidthPx, TargetHeightPx };
    RpBegin.clearValueCount   = static_cast<uint32_t>(Clears.size());
    RpBegin.pClearValues      = Clears.data();
    vkCmdBeginRenderPass(Cmd, &RpBegin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport Vp{ 0.0f, 0.0f, static_cast<float>(TargetWidthPx), static_cast<float>(TargetHeightPx), 0.0f, 1.0f };
    VkRect2D   Scissor{ { 0, 0 }, { TargetWidthPx, TargetHeightPx } };
    vkCmdSetViewport(Cmd, 0u, 1u, &Vp);
    vkCmdSetScissor(Cmd, 0u, 1u, &Scissor);

    vkCmdBindDescriptorSets(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, UnifiedLayout, 0u, 1u, &UnifiedDescriptorSlot, 0u, nullptr);

    // Overlay grid
    if (ViewportSpec.RenderFloorGrid)
    {
        const DispatchPushParams GridPush{ 0u, 84u, 0u, 0u };
        vkCmdPushConstants(Cmd, UnifiedLayout, VK_SHADER_STAGE_ALL, 0u, sizeof(GridPush), &GridPush);
        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, RasterUnits[static_cast<uint8_t>(RasterProgramSlot::OverlayGrid)].Executable);
        vkCmdDraw(Cmd, 84u, 1u, 0u, 0u);
    }

    // Chrome collider ball
    if (ViewportSpec.RenderColliderBall && Document.Collider.BallRadiusMeters > 0.01f)
    {
        const DispatchPushParams BallPush{ 0u, 1u, 0u, 0u };
        vkCmdPushConstants(Cmd, UnifiedLayout, VK_SHADER_STAGE_ALL, 0u, sizeof(BallPush), &BallPush);
        vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, RasterUnits[static_cast<uint8_t>(RasterProgramSlot::SurfaceBacking)].Executable);
        vkCmdDraw(Cmd, 6u, 1u, 0u, 0u);
    }

    // Visual carriers per active rig
    for (const RigDrawEntry& Entry : ActiveRigs)
    {
        const DispatchPushParams DrawPush{ Entry.SlotOffset, Entry.SlotCount, 0u, 0u };
        vkCmdPushConstants(Cmd, UnifiedLayout, VK_SHADER_STAGE_ALL, 0u, sizeof(DrawPush), &DrawPush);

        if (Entry.StrandCount > 0u && Entry.StrandVerts >= 2u)
        {
            const DispatchPushParams StrandPush{ 0u, Entry.StrandCount, 0u, 0u };
            vkCmdPushConstants(Cmd, UnifiedLayout, VK_SHADER_STAGE_ALL, 0u, sizeof(StrandPush), &StrandPush);
            vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, RasterUnits[static_cast<uint8_t>(RasterProgramSlot::Ribbon)].Executable);
            vkCmdDraw(Cmd, (Entry.StrandVerts - 1u) * 6u, Entry.StrandCount, 0u, 0u);
            continue;
        }

        std::visit([&](const auto& Carrier)
        {
            using CarrierT = std::decay_t<decltype(Carrier)>;
            if constexpr (std::is_same_v<CarrierT, OrientedSolidCarrier>)
            {
                vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, RasterUnits[static_cast<uint8_t>(RasterProgramSlot::Solid)].Executable);
                vkCmdDraw(Cmd, 36u, Entry.SlotCount, 0u, 0u);
            }
            else if constexpr (std::is_same_v<CarrierT, WingedCreatureCarrier>)
            {
                vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, RasterUnits[static_cast<uint8_t>(RasterProgramSlot::Winged)].Executable);
                vkCmdDraw(Cmd, 6u, Entry.SlotCount, 0u, 0u);
            }
            else if constexpr (std::is_same_v<CarrierT, DielectricBeamCarrier>)
            {
                vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, RasterUnits[static_cast<uint8_t>(RasterProgramSlot::LineArc)].Executable);
                if (Entry.SlotCount > 1u)
                {
                    vkCmdDraw(Cmd, (Entry.SlotCount - 1u) * 6u, 1u, 0u, 0u);
                }
            }
            else if constexpr (std::is_same_v<CarrierT, RefractionLensCarrier>)
            {
                vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, RasterUnits[static_cast<uint8_t>(RasterProgramSlot::Lens)].Executable);
                vkCmdDraw(Cmd, 6u, 1u, 0u, 0u);
            }
            else if constexpr (std::is_same_v<CarrierT, SpriteBillboardCarrier>)
            {
                const RasterProgramSlot Slot = (Carrier.BlendEquation == ColourSpace::OpticalBlendEquation::Additive)
                                                   ? RasterProgramSlot::BillboardAdditive
                                                   : RasterProgramSlot::BillboardAlpha;
                vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, RasterUnits[static_cast<uint8_t>(Slot)].Executable);
                vkCmdDraw(Cmd, 6u, Entry.SlotCount, 0u, 0u);
            }
            else
            {
                vkCmdBindPipeline(Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, RasterUnits[static_cast<uint8_t>(RasterProgramSlot::BillboardAdditive)].Executable);
                vkCmdDraw(Cmd, 6u, Entry.SlotCount, 0u, 0u);
            }
        }, Entry.Rig->Carrier);
    }

    vkCmdEndRenderPass(Cmd);

    if (ViewportSpec.CapturePixels)
    {
        VkImageMemoryBarrier ImgToXfer{};
        ImgToXfer.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        ImgToXfer.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        ImgToXfer.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
        ImgToXfer.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        ImgToXfer.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        ImgToXfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ImgToXfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ImgToXfer.image               = ColorTargetImg.ImageHandle;
        ImgToXfer.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
        vkCmdPipelineBarrier(
            Cmd,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0u, 0u, nullptr, 0u, nullptr, 1u, &ImgToXfer);

        VkBufferImageCopy CopyRegion{};
        CopyRegion.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
        CopyRegion.imageExtent      = { TargetWidthPx, TargetHeightPx, 1u };
        vkCmdCopyImageToBuffer(
            Cmd,
            ColorTargetImg.ImageHandle,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            StagingReadbackBuf.BufferHandle,
            1u,
            &CopyRegion);

        VkBufferMemoryBarrier BufToHost{};
        BufToHost.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        BufToHost.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
        BufToHost.dstAccessMask       = VK_ACCESS_HOST_READ_BIT;
        BufToHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        BufToHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        BufToHost.buffer              = StagingReadbackBuf.BufferHandle;
        BufToHost.offset              = 0ull;
        BufToHost.size                = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(
            Cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT,
            0u, 0u, nullptr, 1u, &BufToHost, 0u, nullptr);
    }

    const uint64_t Tick = DeviceOwner->DispatchCommandBufferAsync(Cmd);
    const bool Ok = (Tick != 0ull) && DeviceOwner->AwaitTimelineTick(Tick);
    DeviceOwner->FreeCommandBuffer(Cmd);
    if (!Ok)
    {
        return false;
    }

    if (ViewportSpec.CapturePixels && StagingReadbackBuf.HostAddress != nullptr)
    {
        const std::size_t TotalBytes = static_cast<std::size_t>(TargetWidthPx) * TargetHeightPx * 4u;
        CapturedRgbaPixels.resize(TotalBytes);
        std::memcpy(CapturedRgbaPixels.data(), StagingReadbackBuf.HostAddress, TotalBytes);
    }

    ParticleTelemetryCounters GpuTel{};
    std::memcpy(&GpuTel, TelemetryBuf.HostAddress, sizeof(ParticleTelemetryCounters));
    if (!ActiveRigs.empty())
    {
        for (const RigDrawEntry& E : ActiveRigs)
        {
            GpuTel.ActiveStrandCount += E.StrandCount;
        }
    }

    const auto T1 = std::chrono::steady_clock::now();
    const double Ms = std::chrono::duration<double, std::milli>(T1 - T0).count();
    MetricsCollector.IngestCounters(GpuTel, Ms, true);
    return true;
}

ParticleSlot ParticleVulkanExecutor::InspectGpuSlot(uint32_t Index) const noexcept
{
    if (ParticleSlotBuf.HostAddress == nullptr || Index >= AllocatedParticleSlots)
    {
        return ParticleSlot{};
    }
    return static_cast<const ParticleSlot*>(ParticleSlotBuf.HostAddress)[Index];
}

} // namespace Frontier::ParticleSpace::Execution
