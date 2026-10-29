/*==========================================================================================================================================
                                                     LIGHTCOMPUTEVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Execute shared Slang light selection and visibility in Vulkan, then
// compare actual readbacks to native references.

#define VK_NO_PROTOTYPES
#include "LightComputeFormat.h"
#include "Engine/Rendering/PathReplayDomain.h"
#include "Engine/Rendering/PathReplayVulkan.h"
#include "Engine/Rendering/PathReplaySubmissionSequence.h"
#include "Engine/Rendering/PathReplayRayQuery.h"
#include "Engine/Rendering/RadiantFacetDistribution.h"
#include "Engine/Rendering/MediumRadiancePayload.h"
#include "Engine/Rendering/SolarOpticsPayload.h"
#include "Engine/Rendering/SolarDiscFilter.h"
#include "Engine/Render/MaterialPreviewGeometry.h"
#include "Engine/Render/ScatteringSequence.h"
#include "Engine/Render/PaneThickness.h"
#include "Engine/Render/DielectricRefraction.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <vulkan/vulkan.h>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif
using namespace Frontier;
namespace
{
void Require(bool Condition, const char* Message)
{
    if (!Condition)
        throw std::runtime_error(Message);
}
void Confirm(VkResult Result, const std::string& Operation = "unspecified call")
{
    if (Result != VK_SUCCESS)
        throw std::runtime_error("Vulkan error " + std::to_string(Result) + " at " + Operation);
}
#define LightVulkanFunctions(F)                                                                              \
    F(DestroyInstance)                                                                                       \
    F(GetPhysicalDeviceProperties)                                                                           \
    F(GetPhysicalDeviceMemoryProperties)                                                                     \
    F(GetPhysicalDeviceQueueFamilyProperties)                                                                \
    F(EnumeratePhysicalDevices)                                                                              \
    F(EnumerateDeviceExtensionProperties)                                                                    \
    F(CreateDevice)                                                                                          \
    F(DestroyDevice)                                                                                         \
    F(GetDeviceQueue)                                                                                        \
    F(DeviceWaitIdle)                                                                                        \
    F(CreateBuffer)                                                                                          \
    F(DestroyBuffer)                                                                                         \
    F(GetBufferMemoryRequirements)                                                                           \
    F(AllocateMemory)                                                                                        \
    F(FreeMemory)                                                                                            \
    F(BindBufferMemory)                                                                                      \
    F(MapMemory)                                                                                             \
    F(UnmapMemory)                                                                                           \
    F(CreateDescriptorSetLayout)                                                                             \
    F(DestroyDescriptorSetLayout)                                                                            \
    F(CreateDescriptorPool) F(DestroyDescriptorPool) F(AllocateDescriptorSets) F(UpdateDescriptorSets)       \
        F(CreatePipelineLayout) F(DestroyPipelineLayout) F(CreateShaderModule) F(DestroyShaderModule)        \
            F(CreateComputePipelines) F(DestroyPipeline) F(CreateCommandPool) F(DestroyCommandPool)          \
                F(AllocateCommandBuffers) F(BeginCommandBuffer) F(EndCommandBuffer) F(ResetCommandPool)      \
                    F(CmdBindPipeline) F(CmdBindDescriptorSets) F(CmdPushConstants) F(CmdDispatch)           \
                        F(CmdPipelineBarrier) F(QueueSubmit) F(CreateFence) F(DestroyFence) F(WaitForFences) \
                            F(ResetFences)
struct BufferSpan
{
    VkBuffer Buffer = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    void* Address = nullptr;
    size_t Size = 0;
    size_t AllocationSize = 0;
};
using ComputeExtent = LightComputeFootprint;
struct CanonicalUnposedFacet
{
    int32_t Token = 0, Material = 0;
    double CentroidX = 0;
    std::array<double, 9> Positions{}, Normals{};
    std::array<float, 4> Albedo{}, Optical{}, Emissive{};
};
inline CanonicalUnposedFacet UnposeRigidFacet(const LightComputeFacet& F, double Theta, double OffsetX,
                                              double Threshold)
{
    CanonicalUnposedFacet R;
    R.Token = int32_t(std::lround(F.OpticalToken[3]));
    R.Material = F.Emissive[3] > 0 ? int32_t(std::lround(F.Emissive[3])) : 0;
    R.Albedo = F.AlbedoRoughness;
    R.Optical = F.OpticalToken;
    R.Emissive = F.Emissive;
    bool Rigid = Threshold > 0 && double(F.OpticalToken[3]) > Threshold;
    double C = std::cos(Theta), S = std::sin(Theta);
    const std::array<float, 4>* Corners[3] = {&F.A, &F.B, &F.C};
    const std::array<float, 4>* VertexNormals[3] = {&F.NormalA, &F.NormalB, &F.NormalC};
    for (uint32_t I = 0; I < 3; ++I)
    {
        double Px = (*Corners[I])[0], Py = (*Corners[I])[1], Pz = (*Corners[I])[2];
        double Nx = (*VertexNormals[I])[0], Ny = (*VertexNormals[I])[1], Nz = (*VertexNormals[I])[2];
        if (Rigid)
        {
            double Dx = Px - OffsetX;
            Px = C * Dx + S * Py;
            Py = -S * Dx + C * Py;
            double Rx = C * Nx + S * Ny;
            Ny = -S * Nx + C * Ny;
            Nx = Rx;
        }
        R.Positions[I * 3 + 0] = Px;
        R.Positions[I * 3 + 1] = Py;
        R.Positions[I * 3 + 2] = Pz;
        R.Normals[I * 3 + 0] = Nx;
        R.Normals[I * 3 + 1] = Ny;
        R.Normals[I * 3 + 2] = Nz;
    }
    R.CentroidX = (R.Positions[0] + R.Positions[3] + R.Positions[6]) / 3.0;
    return R;
}
inline bool ConfirmRigidPoseContinuation(const std::vector<LightComputeFacet>& Previous,
                                         const std::vector<LightComputeFacet>& Current,
                                         const std::array<float, 4>& PreviousPose,
                                         const std::array<float, 4>& CurrentPose,
                                         const std::array<float, 4>& ReportedPrevious,
                                         const std::array<float, 4>& Rotations)
{
    if (Previous.empty() || Previous.size() != Current.size())
        return false;
    double PrevTheta = PreviousPose[0], PrevOffsetX = PreviousPose[1], PrevThreshold = PreviousPose[2];
    double Theta = CurrentPose[0], OffsetX = CurrentPose[1], Threshold = CurrentPose[2];
    if (!(Threshold > 0) || std::abs(Threshold - PrevThreshold) > 1e-5 ||
        std::abs(double(ReportedPrevious[0]) - PrevTheta) > 1e-5 ||
        std::abs(double(ReportedPrevious[1]) - PrevOffsetX) > 1e-5 ||
        std::abs(double(ReportedPrevious[2]) - PrevThreshold) > 1e-5 ||
        std::abs(double(Rotations[0]) - std::cos(Theta)) > 1e-4 ||
        std::abs(double(Rotations[1]) - std::sin(Theta)) > 1e-4 ||
        std::abs(double(Rotations[2]) - std::cos(PrevTheta)) > 1e-4 ||
        std::abs(double(Rotations[3]) - std::sin(PrevTheta)) > 1e-4)
        return false;
    std::vector<CanonicalUnposedFacet> A, B;
    A.reserve(Previous.size());
    B.reserve(Current.size());
    for (const auto& F : Previous)
        A.push_back(UnposeRigidFacet(F, PrevTheta, PrevOffsetX, PrevThreshold));
    for (const auto& F : Current)
        B.push_back(UnposeRigidFacet(F, Theta, OffsetX, Threshold));
    auto Order = [](const CanonicalUnposedFacet& L, const CanonicalUnposedFacet& R)
    {
        if (L.Token != R.Token)
            return L.Token < R.Token;
        if (L.Material != R.Material)
            return L.Material < R.Material;
        return L.CentroidX < R.CentroidX;
    };
    std::sort(A.begin(), A.end(), Order);
    std::sort(B.begin(), B.end(), Order);
    std::vector<uint8_t> Matched(B.size(), 0);
    size_t WindowStart = 0;
    for (size_t I = 0; I < A.size(); ++I)
    {
        while (WindowStart < B.size() &&
               (B[WindowStart].Token < A[I].Token ||
                (B[WindowStart].Token == A[I].Token && B[WindowStart].Material < A[I].Material) ||
                (B[WindowStart].Token == A[I].Token && B[WindowStart].Material == A[I].Material &&
                 B[WindowStart].CentroidX < A[I].CentroidX - 2e-3)))
            ++WindowStart;
        bool Found = false;
        for (size_t J = WindowStart; J < B.size(); ++J)
        {
            if (B[J].Token != A[I].Token || B[J].Material != A[I].Material ||
                B[J].CentroidX > A[I].CentroidX + 2e-3)
                break;
            if (Matched[J])
                continue;
            double PositionGap = 0, NormalGap = 0, ParameterGap = 0;
            for (uint32_t K = 0; K < 9; ++K)
            {
                PositionGap = std::max(PositionGap, std::abs(A[I].Positions[K] - B[J].Positions[K]));
                NormalGap = std::max(NormalGap, std::abs(A[I].Normals[K] - B[J].Normals[K]));
            }
            for (uint32_t K = 0; K < 4; ++K)
            {
                ParameterGap = std::max(ParameterGap, double(std::abs(A[I].Albedo[K] - B[J].Albedo[K])));
                ParameterGap = std::max(ParameterGap, double(std::abs(A[I].Optical[K] - B[J].Optical[K])));
            }
            for (uint32_t K = 0; K < 3; ++K)
                ParameterGap = std::max(ParameterGap, double(std::abs(A[I].Emissive[K] - B[J].Emissive[K])));
            if (PositionGap < 5e-4 && NormalGap < 2e-3 && ParameterGap < 1e-5)
            {
                Matched[J] = 1;
                Found = true;
                break;
            }
        }
        if (!Found)
            return false;
    }
    return true;
}
#include "RectangleDomainValidation.h"
PathReplaySceneView ReplayDomainView(const LightComputeArchive& Scene)
{
    return {Scene.Receivers, Scene.Surfaces, Scene.Visibility.Enclosures, Scene.Visibility.Corners,
            Scene.Angles, Scene.Camera, Scene.Lights.size()};
}
struct LightComputeSubmission
{
#ifdef _WIN32
    HMODULE Library = nullptr;
#else
    void* Library = nullptr;
#endif
    PFN_vkGetInstanceProcAddr Resolve = nullptr;
#define Declare(Name) PFN_vk##Name Name = nullptr;
    LightVulkanFunctions(Declare)
#undef Declare
        VkInstance Instance = VK_NULL_HANDLE;
    VkPhysicalDevice Physical = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    VkQueue Queue = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties Properties{};
    VkPhysicalDeviceMemoryProperties MemoryProperties{};
    VkCommandPool Pool = VK_NULL_HANDLE;
    VkCommandBuffer Commands = VK_NULL_HANDLE;
    VkFence Completion = VK_NULL_HANDLE;
    VkDescriptorSetLayout DescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool Descriptors = VK_NULL_HANDLE;
    VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkPipeline CellPipeline = VK_NULL_HANDLE, ReceiverPipeline = VK_NULL_HANDLE,
               PrimaryPipeline = VK_NULL_HANDLE, TransportPipeline = VK_NULL_HANDLE;
    std::array<BufferSpan, 8> Buffers{};
    std::array<VkPipeline, 4> HistoryPipelines{};
    std::array<VkPipeline, 3> TransportReconstruction{};
    VkPipeline CloudPipeline = VK_NULL_HANDLE, FogPipeline = VK_NULL_HANDLE, ExitancePyramidPipeline = VK_NULL_HANDLE;
    VkPipeline SolarVisibilityPipeline = VK_NULL_HANDLE, SolarOpticsPipeline = VK_NULL_HANDLE;
    VkPipeline DiffuseGeneratePipeline = VK_NULL_HANDLE, DiffuseResolvePipeline = VK_NULL_HANDLE;
    VkPipeline RectangleGeneratePipeline = VK_NULL_HANDLE, RectangleResolvePipeline = VK_NULL_HANDLE;
    VkPipeline ReplayGeneratePipeline = VK_NULL_HANDLE, ReplayResolvePipeline = VK_NULL_HANDLE;
    VkPipeline ReplayGatherReconstructionPipeline = VK_NULL_HANDLE, ReplayFinalizeReconstructionPipeline = VK_NULL_HANDLE;
    VkPipeline ReplayGatherSampleHistoryPipeline = VK_NULL_HANDLE, ReplayFinalizeSampleHistoryPipeline = VK_NULL_HANDLE;
    VkPipeline ReplayGatherVarianceHistoryPipeline = VK_NULL_HANDLE,
               ReplayIntegrateVarianceMomentsPipeline = VK_NULL_HANDLE,
               ReplayFilterVarianceWaveletPipeline = VK_NULL_HANDLE;
    VkPipeline ReplayProbeRegionPipeline = VK_NULL_HANDLE, ReplayTailAuditPipeline = VK_NULL_HANDLE;
    bool PendingOrdinaryHistoryReset = false;
    bool ReplayLastReset = true, ReplayLastReconstructionReset = true, ReplayReconstructionActive = false;
    bool ReplayLastSampleHistoryReset = true, ReplaySampleHistoryActive = false;
    bool ReplayLastVarianceFilterReset = true, ReplayVarianceFilterActive = false;
    bool ReplayPreviousTemporal = false, ReplayPreviousSpatial = false, ReplayPreviousDiffuseCamera = false;
    bool ReplayPreviousSpatialDisk = false, ReplayPreviousSampleHistory = false;
    bool ReplayPreviousCompactStorage = false, ReplayPreviousPowerMis = false, ReplayPreviousAuthoredMaterials = false;
    uint32_t ReplayLastHistoryCap = 1, ReplayFacetStart = 0, ReplayFacetCapacity = 0;
    PathReplayHistoryDomain ReplayHistoryDomain;
    PathReplaySubmissionSequence ReplaySequence;
    PathReplaySequenceRecording ReplayLastSequence{};
    RectangleDomain PreviousRectangle{};
    bool RectangleHistory = false, LastRectangleReset = true;
    std::vector<LightComputeFacet> DiffuseFacets;
    std::vector<TriangleVisibilitySpace::FloatEnclosure> DiffuseEnclosures;
    uint32_t DiffuseBounces = 0;
    bool LastDiffuseReset = true;
    bool TransportHistoryValid = false, TransportDirty = true;
    bool TransportGeometryRevision = true, RigidMotionHistory = false;
    bool PriorExitanceReady = false, IgnoreLightingRevisionForPriorExitance = false, RigidPoseRecorded = false;
    std::array<float, 4> PreviousRigidPose{};
    std::vector<std::array<float, 4>> EmissionPalette;
    std::vector<LightComputeShape> PreviousLights;
    std::vector<float> PreviousAngles;
    std::vector<LightComputeFacet> TransportFacets;
    uint32_t HistorySequence = 0, ReceiverCount = 0, ReceiverWidth = 0, ReceiverHeight = 0;
    uint32_t PreviousCloudOffset = UINT32_MAX, PreviousFogOffset = UINT32_MAX, PreviousDistanceOffset = UINT32_MAX;
    bool HistoryDirty = true;
    uint32_t Family = 0;
    bool RayQueryExtensions = false;
    PathReplayRayQueryDeviceCapabilities RayQueryCapabilities{};
    PathReplayAccelerationSelection AccelerationSelection{};
    template <class T> T Lookup(const char* Name)
    {
        auto P = Resolve(Instance, Name);
        Require(P != nullptr, Name);
        return reinterpret_cast<T>(P);
    }
    explicit LightComputeSubmission(const char* Driver)
    {
        try
        {
#ifdef _WIN32
            Library = LoadLibraryA(Driver);
            if (Library)
                Resolve = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
                    GetProcAddress(Library, "vkGetInstanceProcAddr"));
#else
            Library = dlopen(Driver, RTLD_NOW | RTLD_LOCAL);
            if (Library)
                Resolve =
                    reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(Library, "vkGetInstanceProcAddr"));
#endif
            Require(Resolve != nullptr, "Explicit Vulkan library could not load");
            VkApplicationInfo Application{};
            Application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            Application.pApplicationName = "Frontier light compute validation";
            Application.apiVersion = VK_API_VERSION_1_1;
            VkInstanceCreateInfo Creation{};
            Creation.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            Creation.pApplicationInfo = &Application;
            Confirm(Lookup<PFN_vkCreateInstance>("vkCreateInstance")(&Creation, nullptr, &Instance));
#define Load(Name) Name = Lookup<PFN_vk##Name>("vk" #Name);
            LightVulkanFunctions(Load)
#undef Load
                uint32_t Count = 0;
            Confirm(EnumeratePhysicalDevices(Instance, &Count, nullptr));
            Require(Count > 0, "No Vulkan physical device enumerated");
            std::vector<VkPhysicalDevice> Devices(Count);
            Confirm(EnumeratePhysicalDevices(Instance, &Count, Devices.data()));
            for (auto Candidate : Devices)
            {
                uint32_t Families = 0;
                GetPhysicalDeviceQueueFamilyProperties(Candidate, &Families, nullptr);
                std::vector<VkQueueFamilyProperties> Queues(Families);
                GetPhysicalDeviceQueueFamilyProperties(Candidate, &Families, Queues.data());
                for (uint32_t I = 0; I < Families; ++I)
                    if (Queues[I].queueFlags & VK_QUEUE_COMPUTE_BIT)
                    {
                        Physical = Candidate;
                        Family = I;
                        break;
                    }
                if (Physical)
                    break;
            }
            Require(Physical != VK_NULL_HANDLE, "No compute-capable queue");
            GetPhysicalDeviceProperties(Physical, &Properties);
            GetPhysicalDeviceMemoryProperties(Physical, &MemoryProperties);
            Require(Properties.apiVersion >= VK_API_VERSION_1_1, "Vulkan 1.1 device required for SPIR-V 1.3");
            Require(Properties.limits.maxDescriptorSetStorageBuffers >= 8 &&
                        Properties.limits.maxPerStageDescriptorStorageBuffers >= 8,
                    "Device has fewer than eight compute storage buffers");
            uint32_t Extensions = 0;
            Confirm(EnumerateDeviceExtensionProperties(Physical, nullptr, &Extensions, nullptr));
            std::vector<VkExtensionProperties> ExtensionNames(Extensions);
            Confirm(
                EnumerateDeviceExtensionProperties(Physical, nullptr, &Extensions, ExtensionNames.data()));
            RayQueryCapabilities = InspectPathReplayRayQueryExtensions(
                ExtensionNames.data(), ExtensionNames.size(), Properties.apiVersion);
            RayQueryExtensions = RayQueryCapabilities.AccelerationStructureExtension &&
                                 RayQueryCapabilities.RayQueryExtension;
            AccelerationSelection = SelectPathReplayAccelerationTier(RayQueryCapabilities, true);
            float Priority = 1;
            VkDeviceQueueCreateInfo QueueCreation{};
            QueueCreation.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            QueueCreation.queueFamilyIndex = Family;
            QueueCreation.queueCount = 1;
            QueueCreation.pQueuePriorities = &Priority;
            VkDeviceCreateInfo DeviceCreation{};
            DeviceCreation.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            DeviceCreation.queueCreateInfoCount = 1;
            DeviceCreation.pQueueCreateInfos = &QueueCreation;
            Confirm(CreateDevice(Physical, &DeviceCreation, nullptr, &Device));
            GetDeviceQueue(Device, Family, 0, &Queue);
            VkCommandPoolCreateInfo PoolCreation{};
            PoolCreation.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            PoolCreation.queueFamilyIndex = Family;
            Confirm(CreateCommandPool(Device, &PoolCreation, nullptr, &Pool));
            VkCommandBufferAllocateInfo Allocation{};
            Allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            Allocation.commandPool = Pool;
            Allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            Allocation.commandBufferCount = 1;
            Confirm(AllocateCommandBuffers(Device, &Allocation, &Commands));
            VkFenceCreateInfo FenceCreation{};
            FenceCreation.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            Confirm(CreateFence(Device, &FenceCreation, nullptr, &Completion));
        }
        catch (...)
        {
            Release();
            throw;
        }
    }
    ~LightComputeSubmission() { Release(); }
    LightComputeSubmission(const LightComputeSubmission&) = delete;
    LightComputeSubmission& operator=(const LightComputeSubmission&) = delete;
    void Release()
    {
        if (Device)
        {
            DeviceWaitIdle(Device);
            for (auto Pipeline : HistoryPipelines)
                if (Pipeline)
                    DestroyPipeline(Device, Pipeline, nullptr);
            if (ReplayTailAuditPipeline) DestroyPipeline(Device, ReplayTailAuditPipeline, nullptr);
            if (ReplayProbeRegionPipeline) DestroyPipeline(Device, ReplayProbeRegionPipeline, nullptr);
            if (ReplayFilterVarianceWaveletPipeline) DestroyPipeline(Device, ReplayFilterVarianceWaveletPipeline, nullptr);
            if (ReplayIntegrateVarianceMomentsPipeline)
                DestroyPipeline(Device, ReplayIntegrateVarianceMomentsPipeline, nullptr);
            if (ReplayGatherVarianceHistoryPipeline) DestroyPipeline(Device, ReplayGatherVarianceHistoryPipeline, nullptr);
            if (ReplayFinalizeSampleHistoryPipeline) DestroyPipeline(Device, ReplayFinalizeSampleHistoryPipeline, nullptr);
            if (ReplayGatherSampleHistoryPipeline) DestroyPipeline(Device, ReplayGatherSampleHistoryPipeline, nullptr);
            if (ReplayFinalizeReconstructionPipeline) DestroyPipeline(Device, ReplayFinalizeReconstructionPipeline, nullptr);
            if (ReplayGatherReconstructionPipeline) DestroyPipeline(Device, ReplayGatherReconstructionPipeline, nullptr);
            if (ReplayResolvePipeline) DestroyPipeline(Device, ReplayResolvePipeline, nullptr);
            if (ReplayGeneratePipeline) DestroyPipeline(Device, ReplayGeneratePipeline, nullptr);
            if (RectangleResolvePipeline) DestroyPipeline(Device, RectangleResolvePipeline, nullptr);
            if (RectangleGeneratePipeline) DestroyPipeline(Device, RectangleGeneratePipeline, nullptr);
            if (DiffuseResolvePipeline) DestroyPipeline(Device, DiffuseResolvePipeline, nullptr);
            if (DiffuseGeneratePipeline) DestroyPipeline(Device, DiffuseGeneratePipeline, nullptr);
            if (SolarOpticsPipeline) DestroyPipeline(Device, SolarOpticsPipeline, nullptr);
            if (SolarVisibilityPipeline) DestroyPipeline(Device, SolarVisibilityPipeline, nullptr);
            if (FogPipeline)
                DestroyPipeline(Device, FogPipeline, nullptr);
            if (ExitancePyramidPipeline)
                DestroyPipeline(Device, ExitancePyramidPipeline, nullptr);
            if (CloudPipeline)
                DestroyPipeline(Device, CloudPipeline, nullptr);
            for (auto Pipeline : TransportReconstruction)
                if (Pipeline)
                    DestroyPipeline(Device, Pipeline, nullptr);
            if (TransportPipeline)
                DestroyPipeline(Device, TransportPipeline, nullptr);
            if (PrimaryPipeline)
                DestroyPipeline(Device, PrimaryPipeline, nullptr);
            if (CellPipeline)
                DestroyPipeline(Device, CellPipeline, nullptr);
            if (ReceiverPipeline)
                DestroyPipeline(Device, ReceiverPipeline, nullptr);
            if (PipelineLayout)
                DestroyPipelineLayout(Device, PipelineLayout, nullptr);
            if (Descriptors)
                DestroyDescriptorPool(Device, Descriptors, nullptr);
            if (DescriptorLayout)
                DestroyDescriptorSetLayout(Device, DescriptorLayout, nullptr);
            for (auto& B : Buffers)
            {
                if (B.Address)
                    UnmapMemory(Device, B.Memory);
                if (B.Buffer)
                    DestroyBuffer(Device, B.Buffer, nullptr);
                if (B.Memory)
                    FreeMemory(Device, B.Memory, nullptr);
            }
            if (Completion)
                DestroyFence(Device, Completion, nullptr);
            if (Pool)
                DestroyCommandPool(Device, Pool, nullptr);
            DestroyDevice(Device, nullptr);
        }
        if (Instance && DestroyInstance)
            DestroyInstance(Instance, nullptr);
#ifdef _WIN32
        if (Library)
            FreeLibrary(Library);
#else
        if (Library)
            dlclose(Library);
#endif
        Device = VK_NULL_HANDLE;
        Instance = VK_NULL_HANDLE;
        Library = nullptr;
    }
    void ConstructBuffer(uint32_t Index, const void* Bytes, size_t Size)
    {
        auto& B = Buffers[Index];
        B.Size = std::max(size_t(16), Size);
        Require(B.Size <= Properties.limits.maxStorageBufferRange, "Storage buffer exceeds device limit");
        VkBufferCreateInfo Creation{};
        Creation.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        Creation.size = B.Size;
        Creation.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        Creation.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        Confirm(CreateBuffer(Device, &Creation, nullptr, &B.Buffer));
        VkMemoryRequirements Requirements{};
        GetBufferMemoryRequirements(Device, B.Buffer, &Requirements);
        uint32_t Type = UINT32_MAX;
        for (uint32_t I = 0; I < MemoryProperties.memoryTypeCount; ++I)
            if ((Requirements.memoryTypeBits & (1u << I)) &&
                (MemoryProperties.memoryTypes[I].propertyFlags &
                 (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                    (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
            {
                Type = I;
                break;
            }
        Require(Type != UINT32_MAX, "Host-coherent storage memory is unavailable");
        VkMemoryAllocateInfo Allocation{};
        Allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        Allocation.allocationSize = Requirements.size;
        Allocation.memoryTypeIndex = Type;
        B.AllocationSize = size_t(Requirements.size);
        Confirm(AllocateMemory(Device, &Allocation, nullptr, &B.Memory),
                "AllocateMemory storage=" + std::to_string(Index) + " bytes=" + std::to_string(Requirements.size));
        Confirm(BindBufferMemory(Device, B.Buffer, B.Memory, 0));
        Confirm(MapMemory(Device, B.Memory, 0, B.Size, 0, &B.Address), "MapMemory storage=" + std::to_string(Index));
        std::memset(B.Address, 0, B.Size);
        if (Size && Bytes)
            std::memcpy(B.Address, Bytes, Size);
    }
    template <class T> void ConstructBuffer(uint32_t Index, const std::vector<T>& Sequence)
    {
        ConstructBuffer(Index, Sequence.data(), Sequence.size() * sizeof(T));
    }
    VkPipeline ConstructPipeline(const std::string& Path)
    {
        std::ifstream Input(Path, std::ios::binary | std::ios::ate);
        Require(bool(Input), "SPIR-V file unavailable");
        auto Length = Input.tellg();
        Require(Length > 0 && Length < 64 * 1024 * 1024 && Length % 4 == 0, "Invalid SPIR-V extent");
        std::vector<uint32_t> Words(size_t(Length) / 4);
        Input.seekg(0);
        Input.read(reinterpret_cast<char*>(Words.data()), Length);
        Require(bool(Input) && Words[0] == 0x07230203, "Invalid SPIR-V payload");
        VkShaderModuleCreateInfo Creation{};
        Creation.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        Creation.codeSize = size_t(Length);
        Creation.pCode = Words.data();
        VkShaderModule Shader = VK_NULL_HANDLE;
        Confirm(CreateShaderModule(Device, &Creation, nullptr, &Shader));
        VkComputePipelineCreateInfo Pipeline{};
        Pipeline.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        Pipeline.layout = PipelineLayout;
        Pipeline.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        Pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        Pipeline.stage.module = Shader;
        Pipeline.stage.pName = "main";
        VkPipeline Result = VK_NULL_HANDLE;
        auto Status = CreateComputePipelines(Device, VK_NULL_HANDLE, 1, &Pipeline, nullptr, &Result);
        DestroyShaderModule(Device, Shader, nullptr);
        Confirm(Status, "CreateComputePipelines " + Path);
        return Result;
    }
    void Prepare(const LightComputeArchive& Scene, const std::string& Shaders, bool SolarOnly = false,
                 bool DirectOnly = false, bool DiffuseOnly = false, bool RectangleOnly = false,
                 bool ReplayOnly = false, bool MediumOnly = false, bool CompactReplay = false)
    {
        Require(int(SolarOnly) + int(DirectOnly) + int(DiffuseOnly) + int(RectangleOnly) + int(ReplayOnly) + int(MediumOnly) <= 1,
                "Compute preparation modes conflict");
        Require(!CompactReplay || ReplayOnly, "Compact replay buffer preparation requires ReplayOnly mode");
        for (const auto& B : Buffers)
            Require(B.Buffer == VK_NULL_HANDLE, "Compute buffers are already prepared");
        const bool HasReplayGather = ReplayOnly && std::filesystem::exists(Shaders + "/ReplayGatherReconstruction.spv");
        const bool HasReplayFinalize = ReplayOnly && std::filesystem::exists(Shaders + "/ReplayFinalizeReconstruction.spv");
        const bool HasSampleGather = ReplayOnly && std::filesystem::exists(Shaders + "/ReplayGatherSampleHistory.spv");
        const bool HasSampleFinalize = ReplayOnly && std::filesystem::exists(Shaders + "/ReplayFinalizeSampleHistory.spv");
        const bool HasVarianceGather = ReplayOnly && std::filesystem::exists(Shaders + "/ReplayGatherVarianceHistory.spv");
        const bool HasVarianceIntegrate =
            ReplayOnly && std::filesystem::exists(Shaders + "/ReplayIntegrateVarianceMoments.spv");
        const bool HasVarianceWavelet =
            ReplayOnly && std::filesystem::exists(Shaders + "/ReplayFilterVarianceWavelet.spv");
        Require(HasReplayGather == HasReplayFinalize, "Replay reconstruction needs both compute stages");
        Require(HasSampleGather == HasSampleFinalize, "Replay sample history needs both compute stages");
        Require(HasVarianceGather == HasVarianceIntegrate && HasVarianceGather == HasVarianceWavelet,
                "Replay variance filter needs all three compute stages");
        Require(!CompactReplay || !HasReplayGather, "Compact replay storage excludes legacy colour reconstruction");
        ConstructBuffer(0, Scene.Lights);
        ConstructBuffer(1, Scene.Receivers);
        // 📝 Poison the uploaded oracle channel: shader output must not depend on
        // native direct-light answers.
        std::array<float, 4> Poison;
        Poison.fill(std::numeric_limits<float>::quiet_NaN());
        for (size_t I = 0; I < Scene.Receivers.size(); ++I)
            std::memcpy(static_cast<char*>(Buffers[1].Address) + I * sizeof(LightComputeReceiver) +
                            offsetof(LightComputeReceiver, ExpectedDirect),
                        Poison.data(), sizeof(Poison));
        ConstructBuffer(2, Scene.Visibility.Enclosures);
        ConstructBuffer(3, PackedSurfaces(Scene));
        auto ProfileSamples = ProjectedAngles(Scene);
        if (Scene.Camera[10][2] > 0)
            ProfileSamples.resize(ProfileSamples.size() + Scene.Surfaces.size() * 4);
        ProfileSamples.resize(ProfileSamples.size() + SolarOpticsPayloadFloats);
        if (ReplayOnly)
        {
            Require(Scene.Surfaces.size() <= (UINT32_MAX - 1u) / 4u && ProfileSamples.size() <= UINT32_MAX &&
                        1 + Scene.Surfaces.size() * 4 <= UINT32_MAX - ProfileSamples.size(),
                    "Replay source distribution exceeds addressable storage");
            ReplayFacetStart = uint32_t(ProfileSamples.size());
            ReplayFacetCapacity = uint32_t(1 + Scene.Surfaces.size() * 4);
            ProfileSamples.resize(ProfileSamples.size() + ReplayFacetCapacity);
        }
        ConstructBuffer(4, ProfileSamples);
        ConstructBuffer(5, nullptr, 256 * 17 * 4);
        const size_t OutputRecords = CompactReplay
            ? size_t(PathReplayRequiredCompactOutputRecords(uint32_t(Scene.Receivers.size()),
                                                            HasSampleGather, HasVarianceGather))
            : (Scene.Receivers.size() * (HasVarianceGather ? 46 :
                (HasSampleGather ? 40 : (HasReplayGather ? 36 :
                ((DiffuseOnly || RectangleOnly || ReplayOnly) ? 32 : 25)))) + 13889 + SolarSourceCount);
        ConstructBuffer(6, nullptr, OutputRecords * 16);
        ConstructBuffer(7, nullptr, 96);
        std::array<VkDescriptorSetLayoutBinding, 8> Slots{};
        for (uint32_t I = 0; I < 8; ++I)
        {
            Slots[I].binding = I;
            Slots[I].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            Slots[I].descriptorCount = 1;
            Slots[I].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo Layout{};
        Layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        Layout.bindingCount = 8;
        Layout.pBindings = Slots.data();
        Confirm(CreateDescriptorSetLayout(Device, &Layout, nullptr, &DescriptorLayout));
        VkPushConstantRange Push{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputeExtent)};
        VkPipelineLayoutCreateInfo PipelineCreation{};
        PipelineCreation.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        PipelineCreation.setLayoutCount = 1;
        PipelineCreation.pSetLayouts = &DescriptorLayout;
        PipelineCreation.pushConstantRangeCount = 1;
        PipelineCreation.pPushConstantRanges = &Push;
        Confirm(CreatePipelineLayout(Device, &PipelineCreation, nullptr, &PipelineLayout));
        VkDescriptorPoolSize PoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8};
        VkDescriptorPoolCreateInfo DescriptorCreation{};
        DescriptorCreation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        DescriptorCreation.maxSets = 1;
        DescriptorCreation.poolSizeCount = 1;
        DescriptorCreation.pPoolSizes = &PoolSize;
        Confirm(CreateDescriptorPool(Device, &DescriptorCreation, nullptr, &Descriptors));
        VkDescriptorSetAllocateInfo Allocation{};
        Allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        Allocation.descriptorPool = Descriptors;
        Allocation.descriptorSetCount = 1;
        Allocation.pSetLayouts = &DescriptorLayout;
        Confirm(AllocateDescriptorSets(Device, &Allocation, &DescriptorSet));
        std::array<VkDescriptorBufferInfo, 8> Spans{};
        std::array<VkWriteDescriptorSet, 8> Writes{};
        for (uint32_t I = 0; I < 8; ++I)
        {
            Spans[I] = {Buffers[I].Buffer, 0, Buffers[I].Size};
            Writes[I].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            Writes[I].dstSet = DescriptorSet;
            Writes[I].dstBinding = I;
            Writes[I].descriptorCount = 1;
            Writes[I].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            Writes[I].pBufferInfo = &Spans[I];
        }
        UpdateDescriptorSets(Device, 8, Writes.data(), 0, nullptr);
        if (ReplayOnly)
        {
            ReplayGeneratePipeline = ConstructPipeline(Shaders + "/ReplayGenerate.spv");
            ReplayResolvePipeline = ConstructPipeline(Shaders + "/ReplayResolve.spv");
            if (HasReplayGather)
            {
                ReplayGatherReconstructionPipeline = ConstructPipeline(Shaders + "/ReplayGatherReconstruction.spv");
                ReplayFinalizeReconstructionPipeline = ConstructPipeline(Shaders + "/ReplayFinalizeReconstruction.spv");
            }
            if (HasSampleGather)
            {
                ReplayGatherSampleHistoryPipeline = ConstructPipeline(Shaders + "/ReplayGatherSampleHistory.spv");
                ReplayFinalizeSampleHistoryPipeline = ConstructPipeline(Shaders + "/ReplayFinalizeSampleHistory.spv");
            }
            if (HasVarianceGather)
            {
                ReplayGatherVarianceHistoryPipeline = ConstructPipeline(Shaders + "/ReplayGatherVarianceHistory.spv");
                ReplayIntegrateVarianceMomentsPipeline =
                    ConstructPipeline(Shaders + "/ReplayIntegrateVarianceMoments.spv");
                ReplayFilterVarianceWaveletPipeline = ConstructPipeline(Shaders + "/ReplayFilterVarianceWavelet.spv");
            }
            if (std::filesystem::exists(Shaders + "/ReplayProbeRegion.spv"))
                ReplayProbeRegionPipeline = ConstructPipeline(Shaders + "/ReplayProbeRegion.spv");
            if (std::filesystem::exists(Shaders + "/ReplayTailAudit.spv"))
                ReplayTailAuditPipeline = ConstructPipeline(Shaders + "/ReplayTailAudit.spv");
        }
        if (RectangleOnly)
        {
            RectangleGeneratePipeline = ConstructPipeline(Shaders + "/RectangleGenerate.spv");
            RectangleResolvePipeline = ConstructPipeline(Shaders + "/RectangleResolve.spv");
        }
        if (DiffuseOnly)
        {
            DiffuseGeneratePipeline = ConstructPipeline(Shaders + "/DiffuseGenerate.spv");
            DiffuseResolvePipeline = ConstructPipeline(Shaders + "/DiffuseResolve.spv");
        }
        if (!SolarOnly && !DirectOnly && !DiffuseOnly && !RectangleOnly && !ReplayOnly && !MediumOnly)
        {
            const char* TransportNames[] = {"TransportTemporal", "TransportSpatial", "TransportResolve"};
            for (uint32_t I = 0; I < 3; ++I)
                TransportReconstruction[I] = ConstructPipeline(Shaders + "/" + TransportNames[I] + ".spv");
            CloudPipeline = ConstructPipeline(Shaders + "/Clouds.spv");
            FogPipeline = ConstructPipeline(Shaders + "/Fog.spv");
            if (std::filesystem::exists(Shaders + "/ExitancePyramid.spv"))
                ExitancePyramidPipeline = ConstructPipeline(Shaders + "/ExitancePyramid.spv");
            TransportPipeline = ConstructPipeline(Shaders + "/Transport.spv");
        }
        if (MediumOnly)
        {
            CloudPipeline = ConstructPipeline(Shaders + "/Clouds.spv");
            FogPipeline = ConstructPipeline(Shaders + "/Fog.spv");
        }
        if (!SolarOnly && !DiffuseOnly && !RectangleOnly && !ReplayOnly && !MediumOnly)
        {
            const char* Names[] = {"Reuse", "Reconstruct", "Filter", "Finalize"};
            for (uint32_t I = 0; I < 4; ++I)
                HistoryPipelines[I] = ConstructPipeline(Shaders + "/" + Names[I] + ".spv");
        }
        if (!DirectOnly && !DiffuseOnly && !RectangleOnly && !ReplayOnly && !MediumOnly && std::filesystem::exists(Shaders + "/SolarVisibility.spv") &&
            std::filesystem::exists(Shaders + "/SolarOptics.spv"))
        {
            SolarVisibilityPipeline = ConstructPipeline(Shaders + "/SolarVisibility.spv");
            SolarOpticsPipeline = ConstructPipeline(Shaders + "/SolarOptics.spv");
        }
        if (!DirectOnly && !DiffuseOnly && !RectangleOnly) PrimaryPipeline = ConstructPipeline(Shaders + "/Primary.spv");
        if (!DiffuseOnly && !RectangleOnly && !ReplayOnly)
        {
            CellPipeline = ConstructPipeline(Shaders + "/Cells.spv");
            ReceiverPipeline = ConstructPipeline(Shaders + "/Receivers.spv");
        }
        ReceiverCount = uint32_t(Scene.Receivers.size());
        Upload(Scene);
    }
    std::vector<LightComputeFacet> PackedSurfaces(const LightComputeArchive& Scene) const
    {
        if (Scene.Surfaces.size() == Scene.Visibility.Corners.size())
            return Scene.Surfaces;
        std::vector<LightComputeFacet> Packed;
        for (auto C : Scene.Visibility.Corners)
        {
            LightComputeFacet F;
            F.A = C.A;
            F.B = C.B;
            F.C = C.C;
            Packed.push_back(F);
        }
        return Packed;
    }
    std::vector<float> ProjectedAngles(const LightComputeArchive& Scene) const
    {
        auto Samples = Scene.Angles;
        for (const auto& V : Scene.Camera)
            Samples.insert(Samples.end(), V.begin(), V.end());
        return Samples;
    }
    void Upload(const LightComputeArchive& Scene, bool ReplayMaterialRevision = false)
    {
        Require(Scene.Receivers.size() == ReceiverCount,
                "History receiver extent changed; recreate submission");
        ReceiverWidth = Scene.Width;
        ReceiverHeight = Scene.Height;
        auto Copy = [&](uint32_t Index, const void* Bytes, size_t Size)
        {
            Require(Size <= Buffers[Index].Size, "Scene exceeds prepared buffer span");
            if (Size)
                std::memcpy(Buffers[Index].Address, Bytes, Size);
        };
        Copy(0, Scene.Lights.data(), Scene.Lights.size() * sizeof(LightComputeShape));
        Copy(1, Scene.Receivers.data(), Scene.Receivers.size() * sizeof(LightComputeReceiver));
        Copy(2, Scene.Visibility.Enclosures.data(),
             Scene.Visibility.Enclosures.size() * sizeof(TriangleVisibilitySpace::FloatEnclosure));
        auto Surfaces = PackedSurfaces(Scene);
        Copy(3, Surfaces.data(), Surfaces.size() * sizeof(LightComputeFacet));
        // 📝 Authored-scene indirect history must notice moved reflectors, occluders and emitters with unchanged radiance/tokens.
        if (TransportGeometryRevision && (TransportFacets.size() != Surfaces.size() ||
            (!Surfaces.empty() && std::memcmp(TransportFacets.data(), Surfaces.data(), Surfaces.size() * sizeof(LightComputeFacet)) != 0)))
        {
            if (RigidMotionHistory && RigidPoseRecorded &&
                ConfirmRigidPoseContinuation(TransportFacets, Surfaces, PreviousRigidPose, Scene.Camera[8],
                                             Scene.Camera[9], Scene.Camera[11]))
            {
                TransportFacets = Surfaces;
            }
            else
            {
                // 📝 Preserve the retained compact diffuse path's moving-history policy; this revision guards authored transport.
                auto Authored = [](const LightComputeFacet& F) { return F.Emissive[3] > 0; };
                if (std::any_of(Surfaces.begin(), Surfaces.end(), Authored) ||
                    std::any_of(TransportFacets.begin(), TransportFacets.end(), Authored)) TransportDirty = true;
                TransportFacets = Surfaces;
            }
        }
        PreviousRigidPose = Scene.Camera[8];
        RigidPoseRecorded = true;
        auto Samples = ProjectedAngles(Scene);
        Copy(4, Samples.data(), Samples.size() * 4);
        std::array<float, 4> Poison;
        Poison.fill(std::numeric_limits<float>::quiet_NaN());
        for (size_t I = 0; I < Scene.Receivers.size(); ++I)
            std::memcpy(static_cast<char*>(Buffers[1].Address) + I * sizeof(LightComputeReceiver) +
                            offsetof(LightComputeReceiver, ExpectedDirect),
                        Poison.data(), 16);
        std::vector<std::array<float, 4>> Palette;
        for (const auto& F : Scene.Surfaces)
            Palette.push_back({F.OpticalToken[3], F.Emissive[0], F.Emissive[1], F.Emissive[2]});
        std::sort(Palette.begin(), Palette.end());
        Palette.erase(std::unique(Palette.begin(), Palette.end()), Palette.end());
        if (Palette != EmissionPalette)
        {
            TransportDirty = true;
            EmissionPalette = std::move(Palette);
        }
        size_t RevisionAngles = Scene.Angles.size();
        if (Scene.Camera[10][2] > 0)
        {
            double Offset = Scene.Camera[10][2];
            Require(std::isfinite(Offset) && Offset < Scene.Angles.size() && std::floor(Offset) == Offset,
                    "Invalid emissive CDF offset");
            RevisionAngles = size_t(Offset);
            ValidateRadiantFacetDistribution(Scene.Angles, RevisionAngles, Scene.Surfaces.size());
        }
        // 📝 Moving emissive geometry changes its CDF, not the analytic reservoirs' light/profile revision.
        bool LightsChanged = PreviousLights.size() != Scene.Lights.size();
        bool AnglesChanged = PreviousAngles.size() != RevisionAngles;
        if (!LightsChanged && !Scene.Lights.empty())
            LightsChanged = std::memcmp(PreviousLights.data(), Scene.Lights.data(),
                                        Scene.Lights.size() * sizeof(LightComputeShape)) != 0;
        if (!AnglesChanged && RevisionAngles != 0)
            AnglesChanged = std::memcmp(PreviousAngles.data(), Scene.Angles.data(), RevisionAngles * sizeof(float)) != 0;
        if (LightsChanged || AnglesChanged)
        {
            PreviousLights = Scene.Lights;
            PreviousAngles.assign(Scene.Angles.begin(), Scene.Angles.begin() + RevisionAngles);
        }
        // 📝 Replay retraces material changes; ordinary pipelines still require their original hard reset.
        if (ReplayMaterialRevision && AnglesChanged) PendingOrdinaryHistoryReset = true;
        if (LightsChanged || (!ReplayMaterialRevision && (AnglesChanged || PendingOrdinaryHistoryReset)))
        {
            HistoryDirty = true;
            if (!ReplayMaterialRevision) PendingOrdinaryHistoryReset = false;
        }
    }
    // Replays sampled random sequences against the current diffuse triangle scene and optional primary rays.
    std::vector<std::array<float, 4>> ExecuteReplay(const LightComputeArchive& Scene, uint32_t Bounces, uint32_t Seed,
                                                    bool Temporal, bool Spatial, bool ResetHistory = false,
                                                    const std::array<float, 3>& Environment = {0,0,0},
                                                    float PositiveFloor = .01f, bool PrimaryVisibility = false,
                                                    bool FullHistoryForAudit = false, bool RoughOpaque = false,
                                                    bool SampleFacets = false, bool DoubleCountForAudit = false,
                                                    bool StrictMotionForAudit = false, uint32_t StackLimit = 64,
                                                    bool BalanceSampling = false, bool TexturedDiffuse = false,
                                                    bool ImportanceRough = false, bool PowerWeightedFacets = false,
                                                    bool UniformFacetMixture = false, bool ProbeRegionForAudit = false,
                                                    bool TailForAudit = false, bool ReconstructReplay = false,
                                                    uint32_t InitialCandidates = 1, bool ExplicitOneForAudit = false,
                                                    bool DiffuseCameraReprojection = false, bool SampleHistory = false,
                                                    bool SpatialDisk = false, bool VarianceFilter = false,
                                                    bool OrderedTraversal = false, bool PowerMis = false,
                                                    bool CompactStorage = false, bool AuthoredMaterials = false,
                                                    bool Sequenced = false, uint64_t StorageRevision = 1u,
                                                    bool AllowCameraOnly = false, bool InterruptBeforeSubmitForAudit = false)
    {
        Require(!InterruptBeforeSubmitForAudit || Sequenced, "Submission interruption audit requires sequencing");
        Require(!Sequenced || (!ProbeRegionForAudit && !TailForAudit && !FullHistoryForAudit),
                "Replay submission sequencing excludes audit-only pipelines and full-history overrides");
        Require(!SpatialDisk || Spatial, "Spatial disk requires spatial reservoir reuse");
        Require(!PowerMis || (BalanceSampling && SampleFacets && !DoubleCountForAudit),
                "Power MIS requires balanced explicit facet sampling");
        Require(!CompactStorage || !ReconstructReplay,
                "Compact replay storage excludes legacy colour reconstruction");
        Require(!AuthoredMaterials || (RoughOpaque && ImportanceRough && !SampleHistory &&
                                       !DiffuseCameraReprojection && !DoubleCountForAudit),
                "Authored PBR replay requires rough-opaque multi-lobe importance sampling");
        Require(!VarianceFilter || (!ReconstructReplay && !DoubleCountForAudit && !ProbeRegionForAudit &&
                                    !TailForAudit && ReplayGatherVarianceHistoryPipeline &&
                                    ReplayIntegrateVarianceMomentsPipeline && ReplayFilterVarianceWaveletPipeline),
                "Replay variance filter requires its three pipelines and excludes legacy colour reconstruction");
        Require(!DiffuseCameraReprojection || (ReconstructReplay && PrimaryVisibility && !RoughOpaque && !TexturedDiffuse),
                "Diffuse camera reprojection requires reconstruction and untextured diffuse primary rays");
        Require(!SampleHistory || (PrimaryVisibility && !RoughOpaque && !TexturedDiffuse && !ReconstructReplay &&
                    !DoubleCountForAudit && InitialCandidates == 1 && !ProbeRegionForAudit &&
                    !TailForAudit && ReplayGatherSampleHistoryPipeline && ReplayFinalizeSampleHistoryPipeline),
                "Sample history requires untextured diffuse primary rays and one fresh candidate");
        Require(!ExplicitOneForAudit || InitialCandidates == 1, "Explicit-one audit requires one initial candidate");
        Require(!ReconstructReplay || (!ProbeRegionForAudit && !TailForAudit && ReplayGatherReconstructionPipeline &&
                                       ReplayFinalizeReconstructionPipeline),
                "Replay colour reconstruction requires its own pipelines and excludes audit-only stages");
        Require(!TailForAudit || (PrimaryVisibility && Spatial && !Temporal && Bounces == 4 && InitialCandidates == 1 &&
                                  ProbeRegionForAudit && ReplayTailAuditPipeline && Scene.Width == 64 && Scene.Height == 64),
                "Replay tail audit requires one initial candidate and its four-hit 64x64 spatial probe pipeline");
        Require(!ProbeRegionForAudit || (PrimaryVisibility && ReplayProbeRegionPipeline &&
                                        Scene.Width == 64 && Scene.Height == 64),
                "Replay probe-region audit requires its primary pipeline and 64x64 extent");
        Require(ReplayGeneratePipeline && ReplayResolvePipeline && (!PrimaryVisibility || PrimaryPipeline) &&
                    Scene.Width > 0 && Scene.Height > 0 && size_t(Scene.Width) * Scene.Height == Scene.Receivers.size() &&
                    Bounces > 0 && Bounces <= 8 && InitialCandidates >= 1 &&
                    InitialCandidates <= PathReplayMaximumInitialCandidates &&
                    std::isfinite(PositiveFloor) && PositiveFloor >= 1e-4f &&
                    PositiveFloor <= 10 && StackLimit >= 1 && StackLimit <= 64 && Scene.Lights.empty() &&
                    Scene.Surfaces.size() == Scene.Visibility.Corners.size(),
                "Invalid replay extent, domain or diffuse scene");
        for (float C : Environment) Require(std::isfinite(C) && C >= 0 && C <= 256, "Invalid replay environment");
        ValidatePathReplayDomain(ReplayDomainView(Scene),
                       {PrimaryVisibility, RoughOpaque, TexturedDiffuse, ImportanceRough,
                        DiffuseCameraReprojection || SampleHistory, VarianceFilter, false, AuthoredMaterials});
        Require(!DoubleCountForAudit || SampleFacets, "Double-count control requires explicit source sampling");
        Require(!BalanceSampling || (SampleFacets && !DoubleCountForAudit),
                "Balanced sampling requires facet sampling without the double-count control");
        Require(!PowerWeightedFacets || SampleFacets, "Weighted sampling requires explicit facet sampling");
        Require(!UniformFacetMixture || PowerWeightedFacets, "Uniform/power mixture requires the weighted CDF");
        const auto FacetIndices = SampleFacets ? CompilePathReplayFacetSources(Scene.Surfaces) : std::vector<float>{};
        const auto FacetSources = PowerWeightedFacets ? CompileRadiantFacetDistribution(Scene.Surfaces) : FacetIndices;
        const uint32_t SourceCount = PowerWeightedFacets ? uint32_t(FacetSources[0]) : uint32_t(FacetIndices.size());
        Require(SourceCount == FacetIndices.size(), "Weighted and replay source domains disagree");
        Require(FacetSources.size() <= ReplayFacetCapacity && ProjectedAngles(Scene).size() <= ReplayFacetStart,
                "Replay facet-source distribution exceeds the prepared storage span");
        const PathReplayHistorySpecification ReplaySpecification{Bounces, PositiveFloor, Environment, PrimaryVisibility,
            RoughOpaque, SampleFacets, DoubleCountForAudit, StrictMotionForAudit, BalanceSampling, TexturedDiffuse,
            ImportanceRough, PowerWeightedFacets, UniformFacetMixture, InitialCandidates, PowerMis, CompactStorage,
            AuthoredMaterials};
        // 📝 Reservoir replay can retain capped history across changes; radiance accumulation cannot.
        const auto ReplayDecision = ReplayHistoryDomain.Resolve(ReplayDomainView(Scene), ReplaySpecification,
                                                                FullHistoryForAudit);
        ReplayLastHistoryCap = ReplayDecision.MaximumHistory;
        const bool ReusePolicyChanged = HistorySequence != 0 &&
            (ReplayPreviousTemporal != Temporal || ReplayPreviousSpatial != Spatial ||
             ReplayPreviousSpatialDisk != SpatialDisk || ReplaySampleHistoryActive != SampleHistory);
        bool Reset = ResetHistory || Scene.Width != ReceiverWidth || Scene.Height != ReceiverHeight ||
                     ReplayPreviousCompactStorage != CompactStorage || ReplayPreviousPowerMis != PowerMis ||
                     ReplayPreviousAuthoredMaterials != AuthoredMaterials || ReusePolicyChanged;
        Upload(Scene, TexturedDiffuse || AuthoredMaterials);
        if (!PrimaryVisibility && AuthoredMaterials)
            for (size_t I = 0; I < Scene.Receivers.size(); ++I)
                if (std::isfinite(Scene.Receivers[I].ExpectedDirect[3]) && Scene.Receivers[I].ExpectedDirect[3] > 0)
                {
                    const float Address = Scene.Receivers[I].ExpectedDirect[3];
                    std::memcpy(static_cast<char*>(Buffers[1].Address) + I * sizeof(LightComputeReceiver) +
                                    offsetof(LightComputeReceiver, ExpectedDirect) + 3 * sizeof(float),
                                &Address, sizeof(float));
                }
        if (!FacetSources.empty())
            std::memcpy(static_cast<float*>(Buffers[4].Address) + ReplayFacetStart,
                        FacetSources.data(), FacetSources.size() * sizeof(float));
        ReplayLastReset = Reset || HistoryDirty;
        const bool CameraTransition =
            (DiffuseCameraReprojection || SampleHistory || (VarianceFilter && PrimaryVisibility)) &&
            ReplayDecision.CameraOnly;
        ReplayLastReconstructionReset = ReplayLastReset || (ReplayDecision.Changed && !CameraTransition) ||
                                        !ReplayReconstructionActive || ReplayPreviousTemporal != Temporal ||
                                        ReplayPreviousSpatial != Spatial ||
                                        ReplayPreviousSpatialDisk != SpatialDisk ||
                                        ReplayPreviousDiffuseCamera != DiffuseCameraReprojection;
        ReplayLastSampleHistoryReset = ReplayLastReset || (ReplayDecision.Changed && !CameraTransition) ||
                                       !ReplaySampleHistoryActive;
        ReplayLastVarianceFilterReset = ReplayLastReset || (ReplayDecision.Changed && !CameraTransition) ||
                                        !ReplayVarianceFilterActive || ReplayPreviousTemporal != Temporal ||
                                        ReplayPreviousSpatial != Spatial ||
                                        ReplayPreviousSpatialDisk != SpatialDisk ||
                                        ReplayPreviousSampleHistory != SampleHistory;
        ComputeExtent Extent;
        Extent.Dimensions[0] = uint32_t(Scene.Receivers.size());
        Extent.Grid[3] = uint32_t(Scene.Visibility.Enclosures.size());
        Extent.Sampling = {Seed, InitialCandidates == 1 && !ExplicitOneForAudit ? 0u : InitialCandidates,
                           uint32_t(Scene.Visibility.Corners.size()), StackLimit};
        Extent.History = {Scene.Width, Scene.Height, ReplayLastReset ? 0u : HistorySequence, ReplayLastReset ? 1u : 0u};
        Extent.Reconstruction = {Temporal ? 1u : 0u, Spatial ? 1u : 0u, SourceCount, ReplayLastHistoryCap};
        const uint32_t ReplayFlags = (RoughOpaque ? PathReplayRoughOpaque : 0u) |
                                     (SampleFacets ? PathReplaySampleFacets : 0u) |
                                     (DoubleCountForAudit ? PathReplayAuditDoubleCount : 0u) |
                                     (StrictMotionForAudit ? PathReplayAuditStrictMotion : 0u) |
                                     (BalanceSampling ? PathReplayBalanceSampling : 0u) |
                                     (TexturedDiffuse ? PathReplayTexturedDiffuse : 0u) |
                                     (ImportanceRough ? PathReplayImportanceRough : 0u) |
                                     (PowerWeightedFacets ? PathReplayPowerWeightedFacets : 0u) |
                                     (UniformFacetMixture ? PathReplayUniformFacetMixture : 0u) |
                                     (ReconstructReplay ? PathReplayReconstruct : 0u) |
                                     (ReconstructReplay && ReplayLastReconstructionReset ? PathReplayResetReconstruction : 0u) |
                                     (DiffuseCameraReprojection && CameraTransition && !ReplayLastReconstructionReset ?
                                          PathReplayDiffuseCameraReprojection : 0u) |
                                     (SampleHistory ? PathReplaySampleHistory : 0u) |
                                     (SampleHistory && ReplayLastSampleHistoryReset ? PathReplayResetSampleHistory : 0u) |
                                     (SpatialDisk ? PathReplaySpatialDisk : 0u) |
                                     (VarianceFilter ? PathReplayVarianceFilter : 0u) |
                                     (VarianceFilter && ReplayLastVarianceFilterReset ? PathReplayResetVarianceFilter : 0u) |
                                     (OrderedTraversal ? PathReplayOrderedTraversal : 0u) |
                                     (PowerMis ? PathReplayPowerMis : 0u) |
                                     (CompactStorage ? PathReplayCompactStorage : 0u) |
                                     (AuthoredMaterials ? PathReplayAuthoredMaterials : 0u);
        Extent.Primary = {PrimaryVisibility ? 1u : 0u, uint32_t(Scene.Angles.size()), Bounces, ReplayFlags};
        Extent.Minimum = {Environment[0], Environment[1], Environment[2], 0};
        Extent.Maximum = {PositiveFloor, 0, 0, 0};
        std::memcpy(&Extent.Maximum[1], &ReplayFacetStart, sizeof(ReplayFacetStart));
        if (Sequenced)
        {
            Extent.History[2] = 0;
            Extent.History[3] = ResetHistory ? 1u : 0u;
            Extent.Reconstruction[3] = 64u;
            Extent.Primary[3] &= ~(PathReplayResetReconstruction | PathReplayResetSampleHistory |
                                   PathReplayResetVarianceFilter);
            if (DiffuseCameraReprojection) Extent.Primary[3] |= PathReplayDiffuseCameraReprojection;
        }
        std::memset(Buffers[7].Address, 0, 96);
        Confirm(ResetCommandPool(Device, Pool, 0));
        Confirm(ResetFences(Device, 1, &Completion));
        VkCommandBufferBeginInfo Begin{};
        Begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Confirm(BeginCommandBuffer(Commands, &Begin));
        VkMemoryBarrier Barrier{};
        Barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        Barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &Barrier, 0, nullptr, 0, nullptr);
        if (!ProbeRegionForAudit && !TailForAudit)
        {
            const PathReplayVulkanAccess Access{PipelineLayout, DescriptorSet, PrimaryPipeline, ReplayGeneratePipeline,
                ReplayResolvePipeline, CmdBindPipeline, CmdBindDescriptorSets, CmdPushConstants, CmdDispatch,
                CmdPipelineBarrier, Properties.limits.maxComputeWorkGroupCount[0], Properties.limits.maxPushConstantsSize,
                ReplayGatherReconstructionPipeline, ReplayFinalizeReconstructionPipeline,
                ReplayGatherSampleHistoryPipeline, ReplayFinalizeSampleHistoryPipeline,
                ReplayGatherVarianceHistoryPipeline, ReplayIntegrateVarianceMomentsPipeline,
                ReplayFilterVarianceWaveletPipeline};
            const PathReplayVulkanBuffers Spans{Buffers[1].Size / sizeof(LightComputeReceiver),
                Buffers[2].Size / sizeof(TriangleVisibilitySpace::FloatEnclosure), Buffers[3].Size / sizeof(LightComputeFacet),
                Buffers[4].Size / sizeof(float), Buffers[6].Size / sizeof(std::array<float, 4>),
                Buffers[7].Size / sizeof(uint32_t)};
            if (Sequenced)
            {
                ReplayLastSequence = ReplaySequence.RecordReplay(Commands, Access, Spans, ReplayDomainView(Scene), Extent,
                                                           StorageRevision, AllowCameraOnly);
                Extent = ReplayLastSequence.Extent;
            }
            else RecordPathReplay(Commands, Access, Spans, Extent);
        }
        else
        {
            // 📝 Audit-only receiver masking/tail passes remain outside the production recorder.
            CmdBindDescriptorSets(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, PipelineLayout, 0, 1, &DescriptorSet, 0, nullptr);
            CmdPushConstants(Commands, PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Extent), &Extent);
            auto Dispatch = [&](VkPipeline Pipeline)
            {
                CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
                CmdDispatch(Commands, (Extent.Dimensions[0] + 63) / 64, 1, 1);
                Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   0, 1, &Barrier, 0, nullptr, 0, nullptr);
            };
            if (PrimaryVisibility) Dispatch(PrimaryPipeline);
            if (ProbeRegionForAudit) Dispatch(ReplayProbeRegionPipeline);
            Dispatch(ReplayGeneratePipeline);
            Dispatch(ReplayResolvePipeline);
            if (TailForAudit) Dispatch(ReplayTailAuditPipeline);
        }
        Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        Barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                           0, 1, &Barrier, 0, nullptr, 0, nullptr);
        try
        {
            Confirm(EndCommandBuffer(Commands));
            if (InterruptBeforeSubmitForAudit) throw std::runtime_error("Replay submission interrupted for audit");
            VkSubmitInfo Submission{};
            Submission.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            Submission.commandBufferCount = 1;
            Submission.pCommandBuffers = &Commands;
            Confirm(QueueSubmit(Queue, 1, &Submission, Completion));
            Confirm(WaitForFences(Device, 1, &Completion, VK_TRUE, 600000000000ull));
            if (Sequenced) ReplaySequence.Finalize(ReplayLastSequence.Submission);
        }
        catch (...)
        {
            if (Sequenced && ReplaySequence.IsPending()) ReplaySequence.Abandon(ReplayLastSequence.Submission);
            throw;
        }
        std::vector<std::array<float, 4>> Result(Scene.Receivers.size());
        std::memcpy(Result.data(), Buffers[6].Address, Result.size() * 16);
        if (Sequenced)
        {
            ReplayLastReset = ReplayLastSequence.Reset;
            ReplayLastSampleHistoryReset = (ReplayLastSequence.Extent.Primary[3] & PathReplayResetSampleHistory) != 0;
            ReplayLastVarianceFilterReset = (ReplayLastSequence.Extent.Primary[3] & PathReplayResetVarianceFilter) != 0;
        }
        HistorySequence = ReplayLastReset ? 1 : HistorySequence + 1;
        HistoryDirty = false;
        ReplayReconstructionActive = ReconstructReplay;
        ReplaySampleHistoryActive = SampleHistory;
        ReplayVarianceFilterActive = VarianceFilter;
        ReplayPreviousTemporal = Temporal;
        ReplayPreviousSpatial = Spatial;
        ReplayPreviousSpatialDisk = SpatialDisk;
        ReplayPreviousSampleHistory = SampleHistory;
        ReplayPreviousDiffuseCamera = DiffuseCameraReprojection;
        ReplayPreviousCompactStorage = CompactStorage;
        ReplayPreviousPowerMis = PowerMis;
        ReplayPreviousAuthoredMaterials = AuthoredMaterials;
        return Result;
    }
    // One translating, unobstructed rectangular source; other geometry or changing emission is rejected/reset.
    std::vector<std::array<float, 4>> ExecuteRectangle(const LightComputeArchive& Scene, uint32_t Seed,
                                                        bool Temporal, bool Spatial, bool ResetHistory = false,
                                                        bool OmitJacobianForAudit = false)
    {
        Require(RectangleGeneratePipeline && RectangleResolvePipeline && Scene.Width > 0 && Scene.Height > 0 &&
                    size_t(Scene.Width) * Scene.Height == Scene.Receivers.size(), "Invalid rectangle experiment");
        const RectangleDomain Current = ValidateRectangleDomain(Scene);
        bool Reset = ResetHistory || !RectangleHistory || Scene.Width != ReceiverWidth || Scene.Height != ReceiverHeight ||
                     !CompatibleRectangles(PreviousRectangle, Current);
        if (!Reset)
            for (const auto& R : Scene.Receivers)
                if (R.PositionHit[3] != 0 && R.MotionPositionToken[2] + .01f > PreviousRectangle.Height)
                    Reset = true;
        const std::array<float, 3> Translation = Reset ? std::array<float,3>{0,0,0} :
            std::array<float,3>{Current.MinX - PreviousRectangle.MinX, Current.MinY - PreviousRectangle.MinY,
                                Current.Height - PreviousRectangle.Height};
        PreviousRectangle = Current;
        RectangleHistory = true;
        Upload(Scene);
        LastRectangleReset = Reset || HistoryDirty;
        ComputeExtent Extent;
        Extent.Dimensions[0] = uint32_t(Scene.Receivers.size());
        Extent.Grid[3] = uint32_t(Scene.Visibility.Enclosures.size());
        Extent.Sampling = {Seed, 0, uint32_t(Scene.Visibility.Corners.size()), 64};
        Extent.History = {Scene.Width, Scene.Height, LastRectangleReset ? 0u : HistorySequence, LastRectangleReset ? 1u : 0u};
        Extent.Reconstruction = {Temporal ? 1u : 0u, Spatial ? 1u : 0u, OmitJacobianForAudit ? 1u : 0u, 0};
        Extent.Minimum = {Translation[0], Translation[1], Translation[2], 0};
        std::memset(Buffers[7].Address, 0, 96);
        Confirm(ResetCommandPool(Device, Pool, 0));
        Confirm(ResetFences(Device, 1, &Completion));
        VkCommandBufferBeginInfo Begin{};
        Begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Confirm(BeginCommandBuffer(Commands, &Begin));
        VkMemoryBarrier Barrier{};
        Barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        Barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &Barrier, 0, nullptr, 0, nullptr);
        CmdBindDescriptorSets(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, PipelineLayout, 0, 1, &DescriptorSet, 0, nullptr);
        CmdPushConstants(Commands, PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Extent), &Extent);
        CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, RectangleGeneratePipeline);
        CmdDispatch(Commands, (Extent.Dimensions[0] + 63) / 64, 1, 1);
        Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           0, 1, &Barrier, 0, nullptr, 0, nullptr);
        CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, RectangleResolvePipeline);
        CmdDispatch(Commands, (Extent.Dimensions[0] + 63) / 64, 1, 1);
        Barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                           0, 1, &Barrier, 0, nullptr, 0, nullptr);
        Confirm(EndCommandBuffer(Commands));
        VkSubmitInfo Submission{};
        Submission.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        Submission.commandBufferCount = 1;
        Submission.pCommandBuffers = &Commands;
        Confirm(QueueSubmit(Queue, 1, &Submission, Completion));
        Confirm(WaitForFences(Device, 1, &Completion, VK_TRUE, 600000000000ull));
        std::vector<std::array<float, 4>> Result(Scene.Receivers.size());
        std::memcpy(Result.data(), Buffers[6].Address, Result.size() * 16);
        HistorySequence = LastRectangleReset ? 1 : HistorySequence + 1;
        HistoryDirty = false;
        return Result;
    }
    // Identical-domain diffuse experiment; any triangle or bounce-budget revision invalidates history.
    std::vector<std::array<float, 4>> ExecuteDiffuse(const LightComputeArchive& Scene, uint32_t Bounces,
                                                      uint32_t Seed, bool Temporal, bool Spatial,
                                                      bool ResetHistory = false)
    {
        Require(DiffuseGeneratePipeline && DiffuseResolvePipeline && Scene.Width > 0 && Scene.Height > 0 &&
                    size_t(Scene.Width) * Scene.Height == Scene.Receivers.size() && Bounces > 0 && Bounces <= 8,
                "Invalid diffuse experiment or unavailable pipelines");
        auto Facets = PackedSurfaces(Scene);
        for (const auto& F : Facets)
            Require(F.Emissive[3] == 0 && F.OpticalToken[0] == 0,
                    "Diffuse experiment requires untextured nonmetallic triangles");
        bool Changed = DiffuseFacets.size() != Facets.size() ||
                       DiffuseEnclosures.size() != Scene.Visibility.Enclosures.size();
        if (!Changed)
            Changed = (!Facets.empty() && std::memcmp(DiffuseFacets.data(), Facets.data(),
                          Facets.size() * sizeof(LightComputeFacet)) != 0) ||
                      (!DiffuseEnclosures.empty() && std::memcmp(DiffuseEnclosures.data(), Scene.Visibility.Enclosures.data(),
                          DiffuseEnclosures.size() * sizeof(TriangleVisibilitySpace::FloatEnclosure)) != 0);
        DiffuseFacets = std::move(Facets);
        DiffuseEnclosures = Scene.Visibility.Enclosures;
        Upload(Scene);
        LastDiffuseReset = HistoryDirty || ResetHistory || Changed || DiffuseBounces != Bounces;
        DiffuseBounces = Bounces;
        ComputeExtent Extent;
        Extent.Dimensions[0] = uint32_t(Scene.Receivers.size());
        Extent.Grid[3] = uint32_t(Scene.Visibility.Enclosures.size());
        Extent.Sampling = {Seed, 0, uint32_t(Scene.Visibility.Corners.size()), 64};
        Extent.History = {Scene.Width, Scene.Height, LastDiffuseReset ? 0u : HistorySequence, LastDiffuseReset ? 1u : 0u};
        Extent.Reconstruction = {Temporal ? 1u : 0u, Spatial ? 1u : 0u, 0, 0};
        Extent.Primary[2] = Bounces;
        std::memset(Buffers[7].Address, 0, 96);
        Confirm(ResetCommandPool(Device, Pool, 0));
        Confirm(ResetFences(Device, 1, &Completion));
        VkCommandBufferBeginInfo Begin{};
        Begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Confirm(BeginCommandBuffer(Commands, &Begin));
        VkMemoryBarrier Barrier{};
        Barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        Barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &Barrier, 0, nullptr, 0, nullptr);
        CmdBindDescriptorSets(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, PipelineLayout, 0, 1, &DescriptorSet, 0, nullptr);
        CmdPushConstants(Commands, PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Extent), &Extent);
        CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, DiffuseGeneratePipeline);
        CmdDispatch(Commands, (Extent.Dimensions[0] + 63) / 64, 1, 1);
        Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           0, 1, &Barrier, 0, nullptr, 0, nullptr);
        CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, DiffuseResolvePipeline);
        CmdDispatch(Commands, (Extent.Dimensions[0] + 63) / 64, 1, 1);
        Barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                           0, 1, &Barrier, 0, nullptr, 0, nullptr);
        Confirm(EndCommandBuffer(Commands));
        VkSubmitInfo Submission{};
        Submission.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        Submission.commandBufferCount = 1;
        Submission.pCommandBuffers = &Commands;
        Confirm(QueueSubmit(Queue, 1, &Submission, Completion));
        Confirm(WaitForFences(Device, 1, &Completion, VK_TRUE, 600000000000ull));
        std::vector<std::array<float, 4>> Result(Scene.Receivers.size());
        std::memcpy(Result.data(), Buffers[6].Address, Result.size() * 16);
        HistorySequence = LastDiffuseReset ? 1 : HistorySequence + 1;
        HistoryDirty = false;
        return Result;
    }
    std::vector<std::array<float, 4>>
    Execute(const LightComputeArchive& Scene, uint32_t Mode, uint32_t Capacity = 16, uint32_t Seed = 1,
            uint32_t StackCapacity = 64, uint32_t Reconstruction = 7, bool ResetHistory = false,
            bool PrimaryVisibility = false, uint32_t TransportSamples = 0, bool Environment = false,
            bool EmissiveSampling = false, bool ReconstructTransport = false,
            uint32_t CloudOffset = UINT32_MAX, uint32_t FogOffset = UINT32_MAX, bool ResolveFog = true,
            bool PriorExitance = false, uint32_t DistanceOffset = UINT32_MAX,
            const SolarOpticsParameters* SolarOptics = nullptr)
    {
        Require(!SolarOptics || (PrimaryVisibility && SolarVisibilityPipeline && SolarOpticsPipeline),
                "Solar optics requires primary visibility and both solar programs");
        Require(Mode != 3 || HistoryPipelines[0], "History shaders were not prepared");
        Require(!PrimaryVisibility || PrimaryPipeline, "Primary visibility shader was not prepared");
        Require(TransportSamples == 0 || TransportPipeline, "Transport shader was not prepared");
        Require(Capacity >= 1 && Capacity <= 16 && Mode <= 3 && StackCapacity >= 1 && StackCapacity <= 64,
                "Unsupported compute extent selection");
        Require((Scene.Receivers.size() + 63) / 64 <= Properties.limits.maxComputeWorkGroupCount[0],
                "Receiver dispatch exceeds device group limit");
        Require(uint64_t(Scene.Receivers.size()) * Scene.Lights.size() < UINT32_MAX,
                "Compute counter extent exceeds uint32");
        Require(!PriorExitance ||
                    (Mode == 3 && PrimaryVisibility && ExitancePyramidPipeline != VK_NULL_HANDLE),
                "Prior-screen exitance requires Mode 3 primary visibility and ExitancePyramid.spv");
        if (DistanceOffset != UINT32_MAX)
        {
            Require(PrimaryVisibility && size_t(DistanceOffset) + 16 <= Scene.Angles.size(),
                    "DistanceFieldSpace requires primary visibility and valid header offset");
            const float* H = Scene.Angles.data() + DistanceOffset;
            for (uint32_t I = 0; I < 16; ++I)
                Require(std::isfinite(H[I]), "DistanceFieldSpace header is non-finite");
            Require(H[4] > H[0] && H[5] > H[1] && H[6] > H[2] && H[3] > 0 && H[7] > 0 &&
                        H[8] >= 2 && H[8] <= 256 && std::floor(H[8]) == H[8] &&
                        H[9] >= 2 && H[9] <= 256 && std::floor(H[9]) == H[9] &&
                        H[10] >= 2 && H[10] <= 256 && std::floor(H[10]) == H[10] &&
                        (H[15] == 1.0f || H[15] == 2.0f),
                    "DistanceFieldSpace header bounds or resolution are invalid");
            size_t Count = size_t(H[8]) * size_t(H[9]) * size_t(H[10]);
            Require(Count <= 2097152 && size_t(DistanceOffset) + 16 + Count <= Scene.Angles.size(),
                    "DistanceFieldSpace sample span exceeds uploaded angles");
            for (size_t I = 0; I < Count; ++I)
                Require(std::isfinite(H[16 + I]) && H[16 + I] >= 0,
                        "DistanceFieldSpace contains invalid signed distance");
        }
        Require(!EmissiveSampling || (PrimaryVisibility && TransportSamples > 0 &&
                                      std::isfinite(Scene.Camera[10][2]) && Scene.Camera[10][2] > 0),
                "Area emission requires primary transport and a valid distribution");
        Require(!ReconstructTransport || (Mode == 3 && PrimaryVisibility && TransportSamples > 0),
                "Transport reconstruction requires primary history mode");
        if (CloudOffset != UINT32_MAX)
        {
            Require(CloudPipeline != VK_NULL_HANDLE, "Cloud shader was not prepared");
            Require(PrimaryVisibility && Environment && Scene.Camera[10][1] >= 1 &&
                        Scene.Camera[10][1] <= 48 && size_t(CloudOffset) + 28 <= Scene.Angles.size(),
                    "Invalid cloud cube extent or parameters");
            ValidateCloudRadiancePayload(Scene.Angles.data() + CloudOffset, CloudRadianceFloatCount);
        }
        if (FogOffset != UINT32_MAX)
        {
            Require(!ResolveFog || FogPipeline != VK_NULL_HANDLE, "Fog shader was not prepared");
            Require(PrimaryVisibility && size_t(FogOffset) + 16 <= Scene.Angles.size(), "Invalid fog parameter span");
            const float* P = Scene.Angles.data() + FogOffset;
            ValidateFogRadiancePayload(P, FogRadianceFloatCount);
            if (P[15] > 0)
            {
                float Address = Scene.Camera[10][2];
                Require(std::isfinite(Address) && Address > 0 && Address < Scene.Angles.size() &&
                            std::floor(Address) == Address, "Fog emitter CDF address is invalid");
                ValidateRadiantFacetDistribution(Scene.Angles, size_t(Address), Scene.Surfaces.size());
            }
        }
        Upload(Scene);
        if (SolarOptics)
        {
            const auto Payload = CompileSolarOpticsPayload(*SolarOptics);
            const size_t Offset = (Scene.Angles.size() + Scene.Camera.size() * 4) * sizeof(float);
            Require(Offset + Payload.size() * sizeof(float) <= Buffers[4].Size, "Solar payload exceeds storage");
            std::memcpy(static_cast<char*>(Buffers[4].Address) + Offset, Payload.data(), Payload.size() * sizeof(float));
        }
        // 📝 Feature selection is outside the angle prefix; changing it must invalidate both histories too.
        if (CloudOffset != PreviousCloudOffset || FogOffset != PreviousFogOffset ||
            DistanceOffset != PreviousDistanceOffset)
        {
            HistoryDirty = true;
            TransportDirty = true;
            PreviousCloudOffset = CloudOffset;
            PreviousFogOffset = FogOffset;
            PreviousDistanceOffset = DistanceOffset;
        }
        ComputeExtent Extent;
        uint32_t FogAddress = FogOffset != UINT32_MAX ? FogOffset + 1 : 0;
        uint32_t DistanceAddress = DistanceOffset != UINT32_MAX ? DistanceOffset + 1 : 0;
        std::memcpy(&Extent.Minimum[3], &FogAddress, 4);
        std::memcpy(&Extent.Maximum[3], &DistanceAddress, 4);
        Extent.Dimensions = {uint32_t(Scene.Receivers.size()), uint32_t(Scene.Lights.size()), Capacity, Mode};
        Extent.Grid[3] = uint32_t(Scene.Visibility.Enclosures.size());
        Extent.Sampling = {Seed, 8, uint32_t(Scene.Visibility.Corners.size()), StackCapacity};
        bool Reset = HistoryDirty || ResetHistory;
        bool SamplePriorExitance =
            PriorExitance && PriorExitanceReady &&
            (IgnoreLightingRevisionForPriorExitance || (!Reset && !TransportDirty));
        Extent.History = {Scene.Width, Scene.Height, Reset ? 0 : HistorySequence, Reset ? 1u : 0u};
        Extent.Reconstruction = {Reconstruction & 1 ? 1u : 0u, Reconstruction & 2 ? 1u : 0u,
                                 Reconstruction & 4 ? 1u : 0u, 0};
        Extent.Primary = {CloudOffset != UINT32_MAX ? CloudOffset + 1
                          : PrimaryVisibility       ? 1u
                                                    : 0u,
                          uint32_t(Scene.Angles.size()), TransportSamples,
                          (Environment ? 1u : 0u) | (EmissiveSampling ? 2u : 0u) |
                              (ReconstructTransport ? 4u : 0u) |
                              ((!TransportHistoryValid || TransportDirty) ? 8u : 0u) |
                              (CloudOffset != UINT32_MAX ? 16u : 0u) |
                              (std::any_of(Scene.Surfaces.begin(), Scene.Surfaces.end(),
                                  [](const auto& F) { return F.Emissive[3] > 0; }) ? 32u : 0u) |
                              (SamplePriorExitance ? 64u : 0u)};
        if (PrimaryVisibility)
        {
            // 📝 No uploaded CPU surface channel may supply primary geometry or
            // material information.
            std::vector<LightComputeReceiver> Poisoned(Scene.Receivers.size());
            for (auto& R : Poisoned)
            {
                std::array<float, 36> Poison;
                Poison.fill(std::numeric_limits<float>::quiet_NaN());
                std::memcpy(static_cast<void*>(&R), Poison.data(), sizeof(R));
            }
            std::memcpy(Buffers[1].Address, Poisoned.data(), Poisoned.size() * sizeof(LightComputeReceiver));
        }
        std::memset(Buffers[7].Address, 0, 96);
        Confirm(ResetCommandPool(Device, Pool, 0));
        Confirm(ResetFences(Device, 1, &Completion));
        VkCommandBufferBeginInfo Begin{};
        Begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Confirm(BeginCommandBuffer(Commands, &Begin));
        VkMemoryBarrier Barrier{};
        Barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        Barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &Barrier, 0, nullptr, 0, nullptr);
        CmdBindDescriptorSets(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, PipelineLayout, 0, 1, &DescriptorSet,
                              0, nullptr);
        CmdPushConstants(Commands, PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Extent), &Extent);
        if (CloudOffset != UINT32_MAX)
        {
            CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, CloudPipeline);
            uint32_t Texels = 6 * uint32_t(Scene.Camera[10][1]) * uint32_t(Scene.Camera[10][1]);
            CmdDispatch(Commands, (Texels + 63) / 64, 1, 1);
            Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &Barrier, 0, nullptr, 0, nullptr);
        }
        CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, CellPipeline);
        CmdDispatch(Commands, 4, 1, 1);
        Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &Barrier, 0, nullptr, 0, nullptr);
        if (PrimaryVisibility)
        {
            CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, PrimaryPipeline);
            CmdDispatch(Commands, (Extent.Dimensions[0] + 63) / 64, 1, 1);
            Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &Barrier, 0, nullptr, 0, nullptr);
        }
        if (PrimaryVisibility && TransportSamples)
        {
            Require(TransportSamples <= uint32_t(Scene.Camera[9][3]),
                    "Transport sample budget exceeds uploaded directions");
            CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, TransportPipeline);
            CmdDispatch(Commands, (Extent.Dimensions[0] + 63) / 64, 1, 1);
            CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &Barrier, 0, nullptr, 0, nullptr);
        }
        if (ReconstructTransport)
            for (auto Pipeline : TransportReconstruction)
            {
                CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
                CmdDispatch(Commands, (Extent.Dimensions[0] + 63) / 64, 1, 1);
                CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &Barrier, 0, nullptr, 0,
                                   nullptr);
            }
        CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, ReceiverPipeline);
        CmdDispatch(Commands, (Extent.Dimensions[0] + 63) / 64, 1, 1);
        if (Mode == 3)
        {
            auto Dispatch = [&](VkPipeline Pipeline)
            {
                VkMemoryBarrier Dependency{};
                Dependency.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                Dependency.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                Dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &Dependency, 0, nullptr, 0,
                                   nullptr);
                CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline);
                CmdDispatch(Commands, (Extent.Dimensions[0] + 63) / 64, 1, 1);
            };
            Dispatch(HistoryPipelines[0]);
            Dispatch(HistoryPipelines[1]);
            for (uint32_t I = 0; I < 2; ++I)
            {
                Extent.Reconstruction[3] = I;
                CmdPushConstants(Commands, PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Extent),
                                 &Extent);
                Dispatch(HistoryPipelines[2]);
            }
            Dispatch(HistoryPipelines[3]);
            if (PriorExitance)
            {
                for (uint32_t Level = 0; Level <= 5; ++Level)
                {
                    VkMemoryBarrier Dependency{};
                    Dependency.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                    Dependency.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                    Dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                    CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &Dependency, 0, nullptr, 0,
                                       nullptr);
                    Extent.Reconstruction[3] = Level;
                    CmdPushConstants(Commands, PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Extent),
                                     &Extent);
                    CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, ExitancePyramidPipeline);
                    uint32_t MipWidth = Scene.Width, MipHeight = Scene.Height;
                    for (uint32_t Tier = 0; Tier < Level; ++Tier)
                    {
                        MipWidth = std::max(1u, (MipWidth + 1u) >> 1u);
                        MipHeight = std::max(1u, (MipHeight + 1u) >> 1u);
                    }
                    CmdDispatch(Commands, (MipWidth * MipHeight + 63) / 64, 1, 1);
                }
            }
        }
        if (FogOffset != UINT32_MAX && ResolveFog)
        {
            Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &Barrier, 0, nullptr, 0, nullptr);
            CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, FogPipeline);
            CmdDispatch(Commands, (Extent.Dimensions[0] + 63) / 64, 1, 1);
        }
        if (SolarOptics)
        {
            Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &Barrier, 0, nullptr, 0, nullptr);
            CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, SolarVisibilityPipeline);
            CmdDispatch(Commands, 1, 1, 1);
            CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &Barrier, 0, nullptr, 0, nullptr);
            CmdBindPipeline(Commands, VK_PIPELINE_BIND_POINT_COMPUTE, SolarOpticsPipeline);
            CmdDispatch(Commands, (Extent.Dimensions[0] + 63) / 64, 1, 1);
        }
        Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        Barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                           &Barrier, 0, nullptr, 0, nullptr);
        Confirm(EndCommandBuffer(Commands));
        VkSubmitInfo Submission{};
        Submission.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        Submission.commandBufferCount = 1;
        Submission.pCommandBuffers = &Commands;
        Confirm(QueueSubmit(Queue, 1, &Submission, Completion));
        Confirm(WaitForFences(Device, 1, &Completion, VK_TRUE, 600000000000ull));
        std::vector<std::array<float, 4>> Result(Scene.Receivers.size());
        std::memcpy(Result.data(), Buffers[6].Address, Result.size() * 16);
        if (Mode == 3)
        {
            HistorySequence = Reset ? 1 : HistorySequence + 1;
            HistoryDirty = false;
        }
        if (Mode == 3)
        {
            TransportHistoryValid = ReconstructTransport;
            TransportDirty = false;
            PriorExitanceReady = PriorExitance;
        }
        else
            PriorExitanceReady = false;
        return Result;
    }
    std::vector<std::array<float, 4>> ExitanceMip(uint32_t Level) const
    {
        Require(Level <= 5 && ReceiverWidth > 0 && ReceiverHeight > 0, "Invalid ExitancePyramid mip request");
        size_t Start = size_t(ReceiverCount) * 21 + 13825;
        uint32_t W = ReceiverWidth, H = ReceiverHeight;
        for (uint32_t I = 0; I < Level; ++I)
        {
            Start += size_t(W) * H;
            // 📝 Ceiling condensation: edge texels keep a partial 2x2 block, exactly as ExitancePyramid writes them.
            W = std::max(1u, (W + 1u) >> 1u);
            H = std::max(1u, (H + 1u) >> 1u);
        }
        std::vector<std::array<float, 4>> Mip(size_t(W) * H);
        std::memcpy(Mip.data(), static_cast<const char*>(Buffers[6].Address) + Start * 16, Mip.size() * 16);
        return Mip;
    }
    std::vector<std::array<float, 4>> DistanceDiagnostics() const
    {
        std::vector<std::array<float, 4>> R(ReceiverCount);
        size_t Start = size_t(ReceiverCount) * 23 + 13889;
        std::memcpy(R.data(), static_cast<const char*>(Buffers[6].Address) + Start * 16, R.size() * 16);
        return R;
    }
    std::array<uint32_t, 24> ExtendedStatistics() const
    {
        std::array<uint32_t, 24> Counts{};
        std::memcpy(Counts.data(), Buffers[7].Address, 96);
        return Counts;
    }
    std::array<float, 3> SolarVisibility() const
    {
        std::array<float, 3> Visibility{};
        std::memcpy(Visibility.data(), static_cast<const char*>(Buffers[7].Address) + 20 * sizeof(uint32_t),
                    Visibility.size() * sizeof(float));
        return Visibility;
    }
    std::vector<std::array<float, 4>> TransportSlab(uint32_t Slot) const
    {
        Require(Slot >= 16 && Slot <= 20, "Invalid transport slab");
        std::vector<std::array<float, 4>> R(ReceiverCount);
        std::memcpy(R.data(),
                    static_cast<const char*>(Buffers[6].Address) + size_t(ReceiverCount) * 16 * Slot,
                    R.size() * 16);
        return R;
    }
    std::vector<std::array<float, 4>> CloudCube(uint32_t Size) const
    {
        std::vector<std::array<float, 4>> R(6 * Size * Size);
        std::memcpy(R.data(), static_cast<const char*>(Buffers[6].Address) + size_t(ReceiverCount) * 16 * 21,
                    R.size() * 16);
        return R;
    }
    float CloudSun(uint32_t Size) const
    {
        float T = 0;
        std::memcpy(&T,
                    static_cast<const char*>(Buffers[6].Address) +
                        (size_t(ReceiverCount) * 21 + 6 * Size * Size) * 16,
                    4);
        return T;
    }
    std::array<uint32_t, 16> HistoryStatistics() const
    {
        std::array<uint32_t, 16> Counts{};
        std::memcpy(Counts.data(), Buffers[7].Address, 64);
        return Counts;
    }
    std::vector<std::array<float, 4>> HistoryLinks() const
    {
        std::vector<std::array<float, 4>> Links(ReceiverCount);
        std::memcpy(Links.data(),
                    static_cast<const char*>(Buffers[6].Address) + size_t(ReceiverCount) * 16 * 13,
                    ReceiverCount * 16);
        return Links;
    }
    std::vector<LightComputeReceiver> PrimaryReceivers() const
    {
        std::vector<LightComputeReceiver> R(ReceiverCount);
        std::memcpy(R.data(), Buffers[1].Address, R.size() * sizeof(LightComputeReceiver));
        return R;
    }
    uint64_t OutputBufferBytes() const { return Buffers[6].Size; }
    uint64_t OutputAllocationBytes() const { return Buffers[6].AllocationSize; }
    uint64_t TotalAllocatedBytes() const
    {
        uint64_t Sum = 0;
        for (const auto& B : Buffers) Sum += B.AllocationSize;
        return Sum;
    }
    void RebindOutputStorage(bool CompactReplay, bool HasSampleGather, bool HasVarianceGather)
    {
        DeviceWaitIdle(Device);
        auto& B = Buffers[6];
        if (B.Address) UnmapMemory(Device, B.Memory);
        if (B.Buffer) DestroyBuffer(Device, B.Buffer, nullptr);
        if (B.Memory) FreeMemory(Device, B.Memory, nullptr);
        B = {};
        const size_t OutputRecords = CompactReplay
            ? size_t(PathReplayRequiredCompactOutputRecords(ReceiverCount, HasSampleGather, HasVarianceGather))
            : (size_t(ReceiverCount) * (HasVarianceGather ? 46u : (HasSampleGather ? 40u : 32u)) + 13889u + SolarSourceCount);
        ConstructBuffer(6, nullptr, OutputRecords * 16u);
        VkDescriptorBufferInfo Span{Buffers[6].Buffer, 0, Buffers[6].Size};
        VkWriteDescriptorSet Write{};
        Write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Write.dstSet = DescriptorSet;
        Write.dstBinding = 6;
        Write.descriptorCount = 1;
        Write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Write.pBufferInfo = &Span;
        UpdateDescriptorSets(Device, 1, &Write, 0, nullptr);
        ReplayHistoryDomain = PathReplayHistoryDomain{};
        ReplaySequence.Invalidate();
    }
    std::vector<std::array<float, 4>> ReadReplayLogicalSlab(uint32_t Slot, bool CompactStorage) const
    {
        std::vector<std::array<float, 4>> Slab(ReceiverCount);
        for (uint32_t Pixel = 0; Pixel < ReceiverCount; ++Pixel)
        {
            const uint64_t Record = CompactStorage
                ? PathReplayCompactSlot(Slot, ReceiverCount, Pixel)
                : (uint64_t(ReceiverCount) * (25u + Slot) + PathReplayFixedOutputRecords + Pixel);
            Require(Record != UINT64_MAX && (Record + 1u) * 16u <= Buffers[6].Size,
                    "Replay logical slab readback exceeds OutputColour buffer");
            std::memcpy(&Slab[Pixel], static_cast<const char*>(Buffers[6].Address) + Record * 16u, 16u);
        }
        return Slab;
    }
    std::vector<std::array<float, 4>> PrimaryStrikes() const
    {
        std::vector<std::array<float, 4>> R(ReceiverCount);
        std::memcpy(R.data(), static_cast<const char*>(Buffers[6].Address) + size_t(ReceiverCount) * 16 * 15,
                    R.size() * 16);
        return R;
    }
    std::array<uint32_t, 4> Statistics() const
    {
        std::array<uint32_t, 4> Counts{};
        std::memcpy(Counts.data(), Buffers[7].Address, 16);
        return Counts;
    }
};
void ValidateFreshSampling(const char* Driver, const std::string& Shaders)
{
    ReferenceScene Scene;
    for (uint32_t I = 0; I < 97; ++I)
    {
        ReferenceLight L;
        L.Vector = {double(int(I % 7) - 3), double(int(I % 5) - 2), 3. + I % 3};
        L.Radiance = {1. + I % 3, 2. + I % 7, 1. + I % 5};
        L.Reach = 12;
        if (I % 3 == 1)
        {
            L.Kind = ReferenceLight::Kind::Spot;
            L.InnerCosine = .9;
            L.OuterCosine = .1;
        }
        if (I % 3 == 2)
        {
            std::istringstream Text("IESNA:LM-63-2002\nTILT=NONE\n1 -1 1 3 3 1 2 0 0 "
                                    "0\n1 1 10\n0 45 90\n0 "
                                    "180 360\n100 80 0 100 20 0 100 80 0");
            L.Photometry = std::make_shared<CandelaDistribution>(CandelaDistribution::Read(Text));
        }
        Scene.Lights.push_back(L);
    }
    ReferenceImage Image;
    Image.Width = 32;
    Image.Height = 16;
    Image.Pixels.resize(512);
    Image.SurfaceSamples.resize(512);
    double Expected = 0;
    for (uint32_t I = 0; I < 512; ++I)
    {
        auto& R = Image.SurfaceSamples[I];
        R.Hit = true;
        R.Position = {double(int(I % 32) - 16) / 8, double(int(I / 32) - 8) / 8, 0};
        R.Normal = {0, 0, 1};
        R.View = {0, 0, 1};
        R.Material.Albedo = {.4, .6, .7};
        for (const auto& L : Scene.Lights)
        {
            SpatialVector Direction;
            double Distance;
            auto C = ReferenceLightArrival(L, R.Position, Direction, Distance);
            auto F = EvaluateReferenceBrdf(R.Material, R.Normal, R.View, Direction);
            double Cosine = std::max(Direction.Z, 0.);
            R.Direct.R += F.R * C.R * Cosine;
            R.Direct.G += F.G * C.G * Cosine;
            R.Direct.B += F.B * C.B * Cosine;
        }
        Image.Pixels[I] = R.Direct;
        Expected += .2126 * R.Direct.R + .7152 * R.Direct.G + .0722 * R.Direct.B;
    }
    auto Archive = PackLightCompute(Scene, Image);
    for (auto& R : Archive.Receivers)
        R.MotionPixel[2] = .25f;
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Archive, Shaders);
    auto All = Compute.Execute(Archive, 0);
    auto Local = Compute.Execute(Archive, 1);
    Require(All == Local, "Dense 97-light Vulkan cell dropped illumination");
    Require(Compute.Statistics()[1] == 512, "Dense cells did not use exhaustive fallback");
    double Maximum = 0;
    for (size_t I = 0; I < All.size(); ++I)
        for (uint32_t C = 0; C < 3; ++C)
            Maximum = std::max(Maximum, std::abs(double(All[I][C]) - Archive.Receivers[I].ExpectedDirect[C]));
    Require(Maximum < 2e-4, "Synthetic shader/profile response differs from native reference");
    double Sum = 0, Squares = 0;
    const uint32_t Repetitions = 256;
    for (uint32_t Seed = 1; Seed <= Repetitions; ++Seed)
    {
        auto Pixels = Compute.Execute(Archive, 2, 16, Seed);
        double Energy = 0;
        for (auto P : Pixels)
            Energy += P[3];
        Sum += Energy;
        Squares += Energy * Energy;
    }
    double Mean = Sum / Repetitions,
           Error = std::sqrt(std::max(0., Squares / Repetitions - Mean * Mean) / Repetitions);
    Require(std::abs(Mean - Expected) < 6 * Error + 1e-4, "Fresh Vulkan RIS differs from exhaustive energy");
    std::cout << "SAMPLING lights=97 receivers=512 seeds=" << Repetitions << " mean=" << Mean
              << " native_exact=" << Expected << " standard_error=" << Error << " native_max_abs=" << Maximum
              << "\n";
    double ReuseSum = 0, ReuseSquares = 0;
    const uint32_t Chains = 64;
    for (uint32_t Chain = 1; Chain <= Chains; ++Chain)
    {
        std::vector<std::array<float, 4>> Samples;
        for (uint32_t Step = 0; Step < 4; ++Step)
            Samples = Compute.Execute(Archive, 3, 16, Chain * 101 + Step, 64, 3, Step == 0);
        double Energy = 0;
        for (auto P : Samples)
            Energy += P[3];
        ReuseSum += Energy;
        ReuseSquares += Energy * Energy;
    }
    double ReuseMean = ReuseSum / Chains,
           ReuseError = std::sqrt(std::max(0., ReuseSquares / Chains - ReuseMean * ReuseMean) / Chains);
    Require(std::abs(ReuseMean - Expected) < 6 * ReuseError + 1e-4,
            "Reused unfiltered reservoirs differ from exact energy");
    std::cout << "REUSE_ENERGY chains=" << Chains << " steps=4 mean=" << ReuseMean
              << " native_exact=" << Expected << " standard_error=" << ReuseError << "\n";
    auto ChangedMaterial = Archive;
    for (size_t I = 0; I < ChangedMaterial.Receivers.size() / 2; ++I)
        ChangedMaterial.Receivers[I].AlbedoIor[0] += .2f;
    (void)Compute.Execute(ChangedMaterial, 3, 16, 901);
    auto Rejections = Compute.HistoryStatistics();
    Require(Rejections[4] == 256 && Rejections[5] == 256,
            "Material change did not reject exactly affected pixels");
    auto BadNormal = ChangedMaterial;
    for (auto& R : BadNormal.Receivers)
        R.MotionNormal = {0, 0, -1, 0};
    (void)Compute.Execute(BadNormal, 3, 16, 902);
    Require(Compute.HistoryStatistics()[4] == 0, "Normal mismatch retained history");
    auto InvalidMotion = ChangedMaterial;
    for (auto& R : InvalidMotion.Receivers)
        R.MotionPixel[3] = 0;
    auto Cut = Compute.Execute(InvalidMotion, 3, 16, 903, 64, 0);
    Require(Compute.HistoryStatistics()[4] == 0, "Camera cut retained history");
    auto Cold = Compute.Execute(InvalidMotion, 3, 16, 903, 64, 0, true);
    Require(Cut == Cold, "Rejected camera history differs from a cold restart");
    auto BadPosition = ChangedMaterial;
    for (auto& R : BadPosition.Receivers)
        R.MotionPositionToken[2] += 10;
    (void)Compute.Execute(BadPosition, 3, 16, 904);
    Require(Compute.HistoryStatistics()[4] == 0, "Disoccluded world position retained history");
    std::cout << "REJECTION material_accepted=256 material_rejected=256 "
                 "normal_rejected=512 "
                 "camera_cut_rejected=512 position_rejected=512 "
                 "cold_restart_exact_match=1\n";
    std::vector<std::array<SpatialVector, 3>> Caster;
    for (uint32_t I = 0; I < 5; ++I)
    {
        Caster.push_back({SpatialVector{-20, -20, 1}, SpatialVector{20, -20, 1}, SpatialVector{20, 20, 1}});
        Caster.push_back({SpatialVector{-20, -20, 1}, SpatialVector{20, 20, 1}, SpatialVector{-20, 20, 1}});
    }
    TriangleVisibilitySpace Visibility;
    Visibility.Build(Caster);
    Archive.Visibility = Visibility.PackFloat32();
    for (auto& R : Archive.Receivers)
        R.ExpectedDirect = {};
    LightComputeSubmission Shadow(Driver);
    Shadow.Prepare(Archive, Shaders);
    auto NormalStack = Shadow.Execute(Archive, 0), SmallStack = Shadow.Execute(Archive, 0, 16, 1, 1);
    Require(NormalStack == SmallStack && Shadow.Statistics()[3] > 0, "Enclosure stack fallback failed");
    for (auto P : SmallStack)
        Require(P[0] == 0 && P[1] == 0 && P[2] == 0, "Offscreen caster failed to occlude synthetic lights");
    std::cout << "VISIBILITY offscreen_caster_facets=10 forced_stack_fallbacks=" << Shadow.Statistics()[3]
              << " exhaustive_fallback_matches=1\n";
}

void WriteImage(const std::string& Path, const std::vector<std::array<float, 4>>& Pixels, uint32_t Width,
                uint32_t Height, double Exposure = .45)
{
    std::ofstream Output(Path, std::ios::binary);
    Output.exceptions(std::ios::failbit | std::ios::badbit);
    Output << "P6\n" << Width << ' ' << Height << "\n255\n";
    ReferenceImage Image;
    Image.Width = Width;
    Image.Height = Height;
    Image.Pixels.reserve(Pixels.size());
    for (auto P : Pixels)
    {
        Require(std::isfinite(P[0]) && std::isfinite(P[1]) && std::isfinite(P[2]), "Nonfinite Vulkan colour");
        Image.Pixels.push_back({P[0], P[1], P[2]});
    }
    std::vector<uint8_t> Rgb;
    EncodeDisplayRgb8(Image, Exposure, Rgb);
    Output.write(reinterpret_cast<const char*>(Rgb.data()), std::streamsize(Rgb.size()));
}
#include "LightHistoryValidation.h"
#include "LightPrimaryValidation.h"
#include "LightPrimaryMotion.h"
#include "LightSurfaceTransport.h"
#include "EmissiveFacetQuadrature.h"
#include "TransportReconstruction.h"
#include "CloudCubeValidation.h"
#include "CloudShadowValidation.h"
#include "FogScatteringValidation.h"
#include "FogFacetValidation.h"
#include "MediumRadianceGpuValidation.h"
#include "MaterialSceneValidation.h"
#include "MaterialEnergyGpu.h"
#include "RoughReflectionValidation.h"
#include "PoleShadowValidation.h"
#include "SpecularHistoryValidation.h"
#include "DielectricBoundaryValidation.h"
#include "FaintPathValidation.h"
#include "VehicleGlazingScene.h"
#include "VehicleGlazingValidation.h"
#include "GlazingMotionValidation.h"
#include "SolidDielectricValidation.h"
#include "MirrorDielectricValidation.h"
#include "RoughDielectricValidation.h"
#include "WindowDielectricValidation.h"
#include "DiffuseDielectricValidation.h"
#include "PaneTransmittanceValidation.h"
#include "PriorExitanceValidation.h"
#include "UnifiedSceneValidation.h"
#include "SolarOpticsValidation.h"
#include "SolarStarburstValidation.h"
#include "ClusterSpotIesValidation.h"
#include "SolarCaptureGpu.h"
#include "DirectSpatialGpuValidation.h"
#include "StaticDiffuseGpuValidation.h"
#include "RectangleReconnectionGpuValidation.h"
#include "PathReplayCapture.h"
#include "PathReplayGpuValidation.h"
#include "ReplayRoomReference.h"
#include "RoughReplayValidation.h"
#include "ReplayFacetSamplingValidation.h"
#include "ReplayFacetCapture.h"
#include "ReplayMotionValidation.h"
#include "ReplayMotionCapture.h"
#include "ReplayEndpointValidation.h"
#include "ReplayBalanceValidation.h"
#include "ReplayBalanceCapture.h"
#include "ReplayTextureValidation.h"
#include "ReplayTextureRoomValidation.h"
#include "ReplayTextureCapture.h"
#include "ReplayTextureMotionValidation.h"
#include "RoughTextureReplayValidation.h"
#include "RoughTextureMotionValidation.h"
#include "ReplayImportanceValidation.h"
#include "ReplayImportanceMotionValidation.h"
#include "ReplayImportanceParityValidation.h"
#include "ReplayFacetPowerValidation.h"
#include "ReplayFacetMixtureValidation.h"
#include "ReplayFacetMixtureSpectrumValidation.h"
#include "ReplayFacetMixtureConvergenceValidation.h"
#include "ReplayFacetMixtureCapture.h"
#include "ReplayFacetMixtureMotionValidation.h"
#include "ReplayFacetMixtureMotionRepeat.h"
#include "ReplayRigidVarianceRepeat.h"
#include "ReplayRigidTailAudit.h"
#include "ReplayFacetMixtureMotionCapture.h"
#include "RoughTextureReplayCapture.h"
#include "ReplayTextureMotionCapture.h"
#include "ReplayReconstructionValidation.h"
#include "ReplayCandidateValidation.h"
#include "ReplayCameraReprojectionValidation.h"
#include "ReplayDynamicCameraValidation.h"
#include "ReplaySampleHistoryValidation.h"
#include "ReplayVarianceFilterGpuValidation.h"
#include "ReplayRayAccelerationGpuValidation.h"
#include "ReplayPbrReflectionGpuValidation.h"
#include "ReplayTransmissionValidation.h"
#include "ReplaySegmentValidation.h"
#include "ReplaySubmissionSequenceGpuValidation.h"
#include "ReplayMaterialSamplingValidation.h"
#include "ReplayMaterialPathValidation.h"
#include "ReplayMaterialReuseValidation.h"
} // namespace
int main(int Count, char** Arguments)
{
    try
    {
        if (Count == 4 && std::string(Arguments[3]) == "--replay-material-reuse")
        {
            ValidateReplayMaterialReuse(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-material-path")
        {
            ValidateReplayMaterialPath(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-material-sampling")
        {
            ValidateReplayMaterialSampling(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-submission-sequence")
        {
            ValidateReplaySubmissionSequenceGpu(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 4 && (std::string(Arguments[3]) == "--replay-transmission" ||
                           std::string(Arguments[3]) == "--replay-transmission-baseline"))
        {
            ValidateReplayTransmission(Arguments[1], Arguments[2],
                                       std::string(Arguments[3]) == "--replay-transmission-baseline");
            return 0;
        }
        if ((Count == 4 || Count == 5) && std::string(Arguments[3]) == "--replay-pbr-reflection")
        {
            ValidateReplayPbrReflectionAll(Arguments[1], Arguments[2], Count == 5 ? Arguments[4] : "");
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-ray-acceleration")
        {
            ValidateReplayRayAccelerationAll(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--capture-replay-ray-acceleration")
        {
            CaptureReplayRayAccelerationComparison(Arguments[1], Arguments[2], Arguments[4]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-variance-filter")
        {
            ValidateReplayVarianceFilterAll(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--capture-replay-variance-filter")
        {
            CaptureReplayVarianceFilterComparison(Arguments[1], Arguments[2], Arguments[4]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--medium-radiance")
        {
            ValidateMediumRadianceGpu(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-camera-reprojection")
        {
            ValidateReplayCameraReprojectionAll(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-dynamic-camera")
        {
            ValidateReplayDynamicCamera(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-sample-history")
        {
            ValidateReplaySampleHistory(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-spatial-disk")
        {
            ValidateReplaySpatialDiskAll(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--capture-replay-spatial-disk")
        {
            CaptureReplaySpatialDiskComparison(Arguments[1], Arguments[2], Arguments[4]);
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--capture-replay-sample-history")
        {
            CaptureReplaySampleHistory(Arguments[1], Arguments[2], Arguments[4]);
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--capture-replay-camera")
        {
            CaptureReplayCameraReprojection(Arguments[1], Arguments[2], Arguments[4]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-candidates")
        {
            ValidateReplayCandidates(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--replay-candidates-parity")
        {
            ValidateReplayFacetMixtureParity(Arguments[1], Arguments[2], Arguments[4]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-reconstruction")
        {
            ValidateReplayReconstruction(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--capture-replay-reconstruction")
        {
            CaptureReplayReconstruction(Arguments[1], Arguments[2], Arguments[4]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-facet-mixture")
        {
            ValidateReplayFacetMixture(Arguments[1], Arguments[2]);
            ValidateReplayFacetMixtureSpectrum(Arguments[1], Arguments[2]);
            ValidateReplayFacetMixtureRoom(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-facet-mixture-spectrum")
        {
            ValidateReplayFacetMixtureSpectrum(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-facet-mixture-convergence")
        {
            ValidateReplayFacetMixtureConvergence(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-facet-mixture-motion")
        {
            ValidateReplayFacetMixtureMotion(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-facet-mixture-motion-smoke")
        {
            ValidateReplayFacetMixtureMotion(Arguments[1], Arguments[2], 12000, 8);
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--replay-facet-mixture-motion-repeat")
        {
            ValidateReplayFacetMixtureMotionRepeat(Arguments[1], Arguments[2], Arguments[4]);
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--replay-facet-mixture-motion-repeat-smoke")
        {
            ValidateReplayFacetMixtureMotionRepeat(Arguments[1], Arguments[2], Arguments[4], 2, 8, 12000);
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--replay-rigid-tail-audit")
        {
            ValidateReplayRigidTailAudit(Arguments[1], Arguments[2], Arguments[4]);
            return 0;
        }
        if (Count == 7 && std::string(Arguments[3]) == "--replay-rigid-variance-repeat")
        {
            ValidateReplayRigidVarianceRepeat(Arguments[1], Arguments[2], Arguments[4], Arguments[5], Arguments[6]);
            return 0;
        }
        if (Count == 7 && std::string(Arguments[3]) == "--replay-rigid-variance-repeat-smoke")
        {
            ValidateReplayRigidVarianceRepeat(Arguments[1], Arguments[2], Arguments[4], Arguments[5], Arguments[6],
                                               2, 8, 10000);
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--replay-facet-mixture-parity")
        {
            ValidateReplayFacetMixtureParity(Arguments[1], Arguments[2], Arguments[4]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-facet-power")
        {
            ValidateReplayPowerFacets(Arguments[1], Arguments[2]);
            ValidateReplayPowerRoom(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--replay-facet-power-parity")
        {
            ValidateReplayPowerDefaultParity(Arguments[1], Arguments[2], Arguments[4]);
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--replay-importance-parity")
        {
            ValidateReplayImportanceDefaultParity(Arguments[1], Arguments[2], Arguments[4]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-importance-motion")
        {
            ValidateReplayImportanceMotion(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-importance")
        {
            ValidateReplayImportanceMass();
            ValidateReplayImportanceEnvironment(Arguments[1], Arguments[2]);
            ValidateReplayImportanceFacet(Arguments[1], Arguments[2]);
            ValidateReplayImportanceRoom(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--rough-texture-fresh-floor")
        {
            ValidateRoughTextureFreshFloor(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--rough-texture-motion")
        {
            ValidateRoughTextureMotion(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--capture-replay-default-parity")
        {
            ValidateReplayCaptureDefaultParity(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--capture-replay-mixture")
        {
            CaptureReplayFacetMixture(Arguments[1], Arguments[2], Arguments[4]);
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--capture-replay-mixture-motion")
        {
            CaptureReplayFacetMixtureMotion(Arguments[1], Arguments[2], Arguments[4]);
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--capture-rough-texture")
        {
            CaptureRoughTextureReplay(Arguments[1], Arguments[2], Arguments[4]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--rough-texture-replay")
        {
            ValidateReplayTextureGuards();
            ValidateRoughTextureQuadrature(Arguments[1], Arguments[2]);
            ValidateRoughTextureRoom(Arguments[1], Arguments[2]);
            ValidateRoughTexturePrimary(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--cluster-spot-ies")
        {
            ValidateClusterSpotIes(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--solar-starburst")
        {
            ValidateSolarStarburst(Arguments[1], Arguments[2], Arguments[4]);
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--capture-texture-motion")
        {
            CaptureReplayTextureMotion(Arguments[1], Arguments[2], Arguments[4]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-texture-motion")
        {
            ValidateReplayTextureMotion(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--capture-replay-texture")
        {
            CaptureReplayTexture(Arguments[1], Arguments[2], Arguments[4]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-texture-room")
        {
            ValidateReplayTextureRoom(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-texture")
        {
            ValidateReplayTextureGuards();
            ValidateReplayTextureAnalytic(Arguments[1], Arguments[2]);
            ValidateReplayTextureSecondaryFiltering(Arguments[1], Arguments[2]);
            ValidateReplayTexturePrimary(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--capture-replay-balance")
        {
            CaptureReplayBalance(Arguments[1], Arguments[2], Arguments[4]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-balance")
        {
            ValidateReplayBalance(Arguments[1], Arguments[2]);
            ValidateReplayBalanceRoom(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-endpoint")
        {
            ValidateReplayEndpoint(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--capture-replay-motion")
        {
            CaptureReplayMotion(Arguments[1], Arguments[2], Arguments[4]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-motion")
        {
            ValidateReplayMotion(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--capture-facet-replay")
        {
            CaptureReplayFacetRooms(Arguments[1], Arguments[2], Arguments[4]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--facet-replay")
        {
            ValidateReplayFacetSampling(Arguments[1], Arguments[2]);
            ValidateReplayFacetRoom(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--rough-replay")
        {
            ValidateRoughReplay(Arguments[1], Arguments[2]);
            ValidateRoughReplayRoom(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--replay-room-reference")
        {
            ValidateReplayRoomReference(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--capture-path-replay")
        {
            CaptureReplayRooms(Arguments[1], Arguments[2], Arguments[4]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--path-replay")
        {
            ValidatePathReplayGpu(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--rectangle-reconnection")
        {
            ValidateRectangleReconnectionGpu(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--solar-capture")
        {
            CaptureSolarOptics(Arguments[1], Arguments[2], Arguments[4]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--static-diffuse")
        {
            ValidateStaticDiffuseGpu(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 4 && (std::string(Arguments[3]) == "--direct-spatial" ||
                           std::string(Arguments[3]) == "--direct-spatial-observe"))
        {
            ValidateDirectSpatialGpu(Arguments[1], Arguments[2], std::string(Arguments[3]) == "--direct-spatial");
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--solar-optics")
        {
            ValidateSolarOptics(Arguments[1], Arguments[2]);
            return 0;
        }
        if (Count == 6 && (std::string(Arguments[3]) == "--prior-exitance" ||
                           std::string(Arguments[3]) == "--prior-exitance-native" ||
                           std::string(Arguments[3]) == "--prior-exitance-original"))
        {
            const std::string Mode = Arguments[3];
            ValidatePriorExitance(Arguments[1], Arguments[2], Arguments[4], Arguments[5],
                                  Mode == "--prior-exitance-native",
                                  Mode == "--prior-exitance-original");
            return 0;
        }
        if (Count == 7 && (std::string(Arguments[3]) == "--unified-scene" ||
                           std::string(Arguments[3]) == "--unified-scene-native"))
        {
            ValidateUnifiedScene(Arguments[1], Arguments[2], Arguments[4], Arguments[5], Arguments[6],
                                 std::string(Arguments[3]) == "--unified-scene-native");
            return 0;
        }
        if (Count == 6 && (std::string(Arguments[3]) == "--pane-transmittance" ||
                           std::string(Arguments[3]) == "--pane-transmittance-native" ||
                           std::string(Arguments[3]) == "--pane-transmittance-original"))
        {
            const std::string Mode = Arguments[3];
            ValidatePaneTransmittance(Arguments[1], Arguments[2], Arguments[4], Arguments[5],
                                      Mode == "--pane-transmittance-native",
                                      Mode == "--pane-transmittance-original");
            return 0;
        }
        if (Count == 7 && (std::string(Arguments[3]) == "--glazing-motion" ||
                           std::string(Arguments[3]) == "--glazing-motion-native"))
        {
            ValidateGlazingMotion(Arguments[1],Arguments[2],Arguments[4],Arguments[5],Arguments[6],
                                  std::string(Arguments[3]) == "--glazing-motion-native");
            return 0;
        }
        if (Count == 7 && (std::string(Arguments[3]) == "--vehicle-glazing" ||
                           std::string(Arguments[3]) == "--vehicle-glazing-probe"))
        {
            ValidateVehicleGlazing(Arguments[1],Arguments[2],LightComputeArchive::Load(Arguments[4]),Arguments[5],Arguments[6],
                                   std::string(Arguments[3]) == "--vehicle-glazing-probe");
            return 0;
        }
        if (Count == 6 && std::string(Arguments[3]) == "--saved-dielectric")
        {
            ValidateSolidDielectric(Arguments[1],Arguments[2],Arguments[4],Arguments[5]);
            return 0;
        }
        if (Count == 5 && (std::string(Arguments[3]) == "--diffuse-dielectric" || std::string(Arguments[3]) == "--diffuse-dielectric-original"))
        {
            ValidateDiffuseDielectric(Arguments[1],Arguments[2],Arguments[4],std::string(Arguments[3]) == "--diffuse-dielectric-original");
            return 0;
        }
        if (Count == 5 && (std::string(Arguments[3]) == "--window-dielectric" || std::string(Arguments[3]) == "--window-dielectric-original"))
        {
            ValidateWindowDielectric(Arguments[1],Arguments[2],Arguments[4],std::string(Arguments[3]) == "--window-dielectric-original");
            return 0;
        }
        if (Count == 5 && (std::string(Arguments[3]) == "--rough-dielectric" || std::string(Arguments[3]) == "--rough-dielectric-original"))
        {
            ValidateRoughDielectric(Arguments[1],Arguments[2],Arguments[4],std::string(Arguments[3]) == "--rough-dielectric-original");
            return 0;
        }
        if (Count == 5 && (std::string(Arguments[3]) == "--mirror-dielectric" || std::string(Arguments[3]) == "--mirror-dielectric-original"))
        {
            ValidateMirrorDielectric(Arguments[1],Arguments[2],Arguments[4],std::string(Arguments[3]) == "--mirror-dielectric-original");
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--solid-dielectric")
        {
            ValidateSolidDielectric(Arguments[1],Arguments[2],Arguments[4]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--faint-path")
        {
            ValidateFaintPath(Arguments[1],Arguments[2]);
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--dielectric-boundaries")
        {
            ValidateDielectricBoundaries(Arguments[1],Arguments[2]);
            return 0;
        }
        if (Count == 5 && (std::string(Arguments[3]) == "--specular-history" || std::string(Arguments[3]) == "--specular-history-original"))
        {
            ValidateSpecularHistory(Arguments[1],Arguments[2],Arguments[4],std::string(Arguments[3]) == "--specular-history-original");
            return 0;
        }
        if (Count == 5 && std::string(Arguments[3]) == "--pole-shadows")
        {
            ValidatePoleShadows(Arguments[1],Arguments[2],Arguments[4]);
            return 0;
        }
        if (Count == 5 && (std::string(Arguments[3]) == "--rough-reflections" ||
                           std::string(Arguments[3]) == "--rough-reflections-sharp"))
        {
            ValidateRoughReflections(Arguments[1],Arguments[2],Arguments[4],std::string(Arguments[3]) == "--rough-reflections-sharp");
            return 0;
        }
        if (Count == 4 && (std::string(Arguments[3]) == "--material-energy" ||
                           std::string(Arguments[3]) == "--material-energy-original"))
        {
            ValidateMaterialEnergyGpu(Arguments[1],Arguments[2],std::string(Arguments[3]) == "--material-energy-original");
            return 0;
        }
        if (Count == 4 && std::string(Arguments[3]) == "--sampling-only")
        {
            ValidateFreshSampling(Arguments[1], Arguments[2]);
            return 0;
        }
        Require(Count == 5 || Count == 6 || Count == 7,
                "Usage: LightComputeValidation driver-library shader-directory "
                "input-archive output-directory");
        auto Scene = LightComputeArchive::Load(Arguments[3]);
        std::filesystem::create_directories(Arguments[4]);
        LightComputeSubmission Compute(Arguments[1]);
        std::cout << "DEVICE name=" << Compute.Properties.deviceName
                  << " type=" << Compute.Properties.deviceType << " api=" << Compute.Properties.apiVersion
                  << " ray_query_extensions=" << Compute.RayQueryExtensions
                  << " ray_query_executed=0 triangle_bvh_executed=1\n";
        if (Count == 6 && std::string(Arguments[5]) == "--material-scene")
        {
            ValidateMaterialScene(Arguments[1], Arguments[2], Scene, Arguments[4]);
            return 0;
        }
        if (Count == 6 && std::string(Arguments[5]) == "--primary-only")
        {
            Compute.Prepare(Scene, Arguments[2]);
            ValidatePrimaryClipping(Arguments[1], Arguments[2], Arguments[4]);
            ValidatePrimary(Compute, Scene, Arguments[4]);
            return 0;
        }
        if (Count == 7 && std::string(Arguments[5]) == "--primary-motion")
        {
            ValidatePrimaryMotion(Arguments[1], Arguments[2], Arguments[6], Arguments[4]);
            return 0;
        }
        if (Count == 6 && std::string(Arguments[5]) == "--transport-only")
        {
            ValidateSurfaceTransport(Arguments[1], Arguments[2], Scene, Arguments[4]);
            return 0;
        }
        if (Count == 7 && std::string(Arguments[5]) == "--transport-motion")
        {
            ValidateTransportMotion(Arguments[1], Arguments[2], Arguments[6], Arguments[4]);
            return 0;
        }
        if (Count == 6 && std::string(Arguments[5]) == "--emissive-only")
        {
            ValidateEmissiveQuadrature(Arguments[1], Arguments[2], Scene, Arguments[4]);
            return 0;
        }
        if (Count == 7 && std::string(Arguments[5]) == "--emissive-motion")
        {
            ValidateTransportMotion(Arguments[1], Arguments[2], Arguments[6], Arguments[4],
                                    &EmissiveQuadrature::AppendDistribution);
            return 0;
        }
        if (Count == 7 && std::string(Arguments[5]) == "--transport-reconstruction")
        {
            ValidateTransportReconstruction(Arguments[1], Arguments[2], Arguments[6], Arguments[4]);
            return 0;
        }
        if (Count == 7 && std::string(Arguments[5]) == "--transport-reconstruction-compact-motion")
        {
            ValidateTransportReconstruction(Arguments[1],Arguments[2],Arguments[6],Arguments[4],false,true);
            return 0;
        }
        if (Count == 7 && std::string(Arguments[5]) == "--transport-reconstruction-checks")
        {
            ValidateTransportReconstruction(Arguments[1], Arguments[2], Arguments[6], Arguments[4], false);
            return 0;
        }
        if (Count == 6 && (std::string(Arguments[5]) == "--cloud-budget" ||
                           std::string(Arguments[5]) == "--cloud-preview"))
        {
            ValidateFogScattering(Arguments[1], Arguments[2], Scene, Arguments[4], true, false, false, true,
                                  std::string(Arguments[5]) == "--cloud-budget");
            return 0;
        }
        if (Count == 6 && (std::string(Arguments[5]) == "--fog-facets" ||
                           std::string(Arguments[5]) == "--fog-facet-checks"))
        {
            ValidateFogFacets(Arguments[1], Arguments[2], Scene, Arguments[4],
                              std::string(Arguments[5]) == "--fog-facets");
            return 0;
        }
        if (Count == 6 && (std::string(Arguments[5]) == "--fog-sampling" ||
                           std::string(Arguments[5]) == "--fog-sampling-checks"))
        {
            ValidateFogScattering(Arguments[1], Arguments[2], Scene, Arguments[4],
                                  std::string(Arguments[5]) == "--fog-sampling", false, true);
            return 0;
        }
        if (Count == 6 && (std::string(Arguments[5]) == "--fog-rays" ||
                           std::string(Arguments[5]) == "--fog-checks" || std::string(Arguments[5]) == "--fog-sky"))
        {
            ValidateFogScattering(Arguments[1], Arguments[2], Scene, Arguments[4],
                                  std::string(Arguments[5]) != "--fog-checks", std::string(Arguments[5]) == "--fog-sky");
            return 0;
        }
        if (Count == 7 && std::string(Arguments[5]) == "--cloud-shadow-captures")
        {
            std::string Text = Arguments[6];
            Require(Text.size() == 1 && Text[0] >= '1' && Text[0] <= '3', "Invalid first cloud capture index");
            ValidateCloudShadows(Arguments[1], Arguments[2], Scene, Arguments[4], true, uint32_t(Text[0] - '0'));
            return 0;
        }
        if (Count == 6 && (std::string(Arguments[5]) == "--cloud-shadows" ||
                           std::string(Arguments[5]) == "--cloud-shadow-checks"))
        {
            ValidateCloudShadows(Arguments[1], Arguments[2], Scene, Arguments[4],
                                 std::string(Arguments[5]) == "--cloud-shadows");
            return 0;
        }
        if (Count == 6 && std::string(Arguments[5]) == "--clouds-only")
        {
            ValidateCloudCube(Arguments[1], Arguments[2], Scene, Arguments[4]);
            return 0;
        }
        Compute.Prepare(Scene, Arguments[2]);
        auto All = Compute.Execute(Scene, 0);
        auto GlobalCounts = Compute.Statistics();
        auto Local = Compute.Execute(Scene, 1);
        auto LocalCounts = Compute.Statistics();
        Require(All == Local, "Vulkan clustered output differs from Vulkan all-light evaluation");
        auto Overflow = Compute.Execute(Scene, 1, 1);
        Require(Overflow == All, "Vulkan overflow fallback changed output");
        double Absolute = 0, Maximum = 0;
        uint64_t Large = 0;
        double Energy = 0;
        for (size_t I = 0; I < All.size(); ++I)
            for (uint32_t Channel = 0; Channel < 3; ++Channel)
            {
                auto& R = Scene.Receivers[I];
                double Expected = R.ResidualSpecular[Channel] + R.ExpectedDirect[Channel];
                double Delta = std::abs(All[I][Channel] - Expected);
                Absolute += Delta;
                Maximum = std::max(Maximum, Delta);
                Large += Delta > 1e-3;
                Energy += R.ExpectedDirect[Channel] * (Channel == 0 ? .2126 : Channel == 1 ? .7152 : .0722);
                Require(std::isfinite(All[I][Channel]), "Nonfinite Vulkan readback");
            }
        std::cout << "COMPARE receivers=" << All.size() << " facets=" << Scene.Visibility.Corners.size()
                  << " all_visits=" << GlobalCounts[0] << " local_visits=" << LocalCounts[0]
                  << " fallbacks=" << LocalCounts[1] << " shadow_rays=" << LocalCounts[2]
                  << " stack_fallbacks=" << LocalCounts[3]
                  << " cpu_mean_abs_rgb=" << Absolute / (All.size() * 3) << " cpu_max_abs_rgb=" << Maximum
                  << " channels_over_1e-3=" << Large << "\n";
        Require(Maximum < .001 && Absolute / (All.size() * 3) < .001,
                "Mean Vulkan/native colour error exceeds acceptance");
        WriteImage(std::string(Arguments[4]) + "/All.ppm", All, Scene.Width, Scene.Height);
        WriteImage(std::string(Arguments[4]) + "/Clustered.ppm", Local, Scene.Width, Scene.Height);
        ValidateFreshSampling(Arguments[1], Arguments[2]);
        ValidateHistory(Arguments[1], Arguments[2], Scene, Arguments[4], Count == 6 ? Arguments[5] : "");
        auto Ris = Compute.Execute(Scene, 2);
        WriteImage(std::string(Arguments[4]) + "/FreshRis.ppm", Ris, Scene.Width, Scene.Height);
        double Sum = 0;
        for (auto P : Ris)
            Sum += P[3];
        std::cout << "RIS first_seed_luminance_sum=" << Sum << " native_exact_luminance_sum=" << Energy
                  << " fresh_ris_temporal_reuse=0\nPASS Vulkan "
                     "all-light/cluster/overflow equality and native "
                     "image comparison\n";
    }
    catch (const std::exception& Error)
    {
        std::cerr << Error.what() << '\n';
        return 1;
    }
}
