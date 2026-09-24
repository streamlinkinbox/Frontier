//============================================================================================================================================
//                                   📦 Exhibits/Workbench/Traversal/BlasDeviceRun.cpp — D9b
//============================================================================================================================================
// 🧩 THE GPU RUN. Everything else in D9 was built to be checkable without a device, and this is the part that cannot be:
//    it creates a Vulkan device, uploads the M10 level's soup, runs `BlasBuild.slang` through the dispatch plan, and
//    compares the device's node blob, leaf blob and level table with the CPU mirror's — byte for byte. It also runs the
//    refit (`BlasRefit.slang`) over a deformed soup and checks that path the same way.
//
// 🔑 WHY THIS LIVES IN Tools/Exhibits AND NOT IN THE ENGINE: it is a run, not a feature. The engine's own shaders and
//    pipelines are created by SwapchainExchange; this program exists to answer one question — does the device produce
//    the mirror's bytes — and to answer it on whatever machine has a GPU, including ones that never build the engine.
//
// ⚠️ Built and compiled in a sandbox with no GPU and no libvulkan: the pipeline objects it drives
//    (Engine/GeometricRaster/BlasBuildPipeline.{h,cpp}) are compile-checked, and this file is compile-checked against the
//    Vulkan-Headers, but THE RUN ITSELF HAS NOT HAPPENED. Running it is the last item on D9, and it needs real hardware:
//
//        g++ -std=c++20 -O2 -DFRONTIER_CPU_PORT -IEngine -IEngine/GeometricRaster -IEngine/DeviceExchange
//            -IEngine/ContentInterchange -I<vulkan headers> -I<tinybvh>
//            Exhibits/Workbench/Traversal/BlasDeviceRun.cpp Engine/GeometricRaster/BlasBuildMirror.cpp
//            Engine/GeometricRaster/BlasDevicePayload.cpp Engine/GeometricRaster/BlasBuildPipeline.cpp
//            Engine/GeometricRaster/InstanceAcceleration.cpp Engine/GeometricRaster/TraversalIndex.cpp
//            Engine/ContentInterchange/MaterialSwatchStructure.cpp Engine/DeviceExchange/OrientationClassifier.cpp
//            -o blas_device_run -ldl
//    (-Wno-missing-field-initializers: Vulkan's structs are initialised sType-first, which is the idiom every Vulkan
//     sample uses and which -Wextra reports once per struct.)
//
//    or `bash Exhibits/Workbench/Traversal/RunBlasDevice.sh`, which does the above with the repository's include paths and
//    builds the SPIR-V first if the .spv files are missing.
//
// Exit codes: 0 the device matches the mirror · 1 a mismatch · 2 no Vulkan device (printed as SKIP, not as a pass).
#include "BlasBuildPipeline.h"
#include "../../../Engine/ContentInterchange/MaterialSwatchStructure.h"

#include <dlfcn.h>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <functional>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Frontier;
using Frontier::PackBlasSoup;

namespace
{
    int Failures = 0;

    void Say(const char* Format, ...)
    {
        va_list Arguments;
        va_start(Arguments, Format);
        std::printf("  ");
        std::vprintf(Format, Arguments);
        std::printf("\n");
        va_end(Arguments);
    }

    void Pass(const char* Format, ...)
    {
        std::printf("  PASS  ");
        va_list Arguments;
        va_start(Arguments, Format);
        std::vprintf(Format, Arguments);
        va_end(Arguments);
        std::printf("\n");
    }

    void Fail(const char* Format, ...)
    {
        ++Failures;
        std::printf("  FAIL  ");
        va_list Arguments;
        va_start(Arguments, Format);
        std::vprintf(Format, Arguments);
        va_end(Arguments);
        std::printf("\n");
    }

    // ── a minimal Vulkan loader. dlopen rather than a link-time dependency, because a machine can have a GPU driver and
    //    no -lvulkan in its dev packages, and because this file must still COMPILE where neither exists.
    struct Loader
    {
        void* Handle = nullptr;
        PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;
        PFN_vkGetDeviceProcAddr GetDeviceProcAddr = nullptr;

        PFN_vkCreateInstance CreateInstance = nullptr;
        PFN_vkDestroyInstance DestroyInstance = nullptr;
        PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices = nullptr;
        PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties = nullptr;
        PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties = nullptr;
        PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties = nullptr;
        PFN_vkCreateDevice CreateDevice = nullptr;
        PFN_vkDestroyDevice DestroyDevice = nullptr;
        PFN_vkGetDeviceQueue GetDeviceQueue = nullptr;

        bool Open(std::string& OutError)
        {
            const char* Names[] = { "libvulkan.so.1", "libvulkan.so", "vulkan-1.dll", "libvulkan.dylib" };
            for (const char* Name : Names)
            {
                Handle = dlopen(Name, RTLD_NOW | RTLD_LOCAL);
                if (Handle) break;
            }
            if (!Handle)
            {
                OutError = "no Vulkan loader found (tried libvulkan.so.1, libvulkan.so, vulkan-1.dll, libvulkan.dylib)";
                return false;
            }
            GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(Handle, "vkGetInstanceProcAddr"));
            if (!GetInstanceProcAddr) { OutError = "vkGetInstanceProcAddr is missing from the loader"; return false; }
            const auto Load = [&](const char* Name) { return GetInstanceProcAddr(nullptr, Name); };
            CreateInstance                        = reinterpret_cast<PFN_vkCreateInstance>(Load("vkCreateInstance"));
            DestroyInstance                       = reinterpret_cast<PFN_vkDestroyInstance>(Load("vkDestroyInstance"));
            EnumeratePhysicalDevices              = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(Load("vkEnumeratePhysicalDevices"));
            GetPhysicalDeviceProperties           = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(Load("vkGetPhysicalDeviceProperties"));
            GetPhysicalDeviceMemoryProperties     = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(Load("vkGetPhysicalDeviceMemoryProperties"));
            GetPhysicalDeviceQueueFamilyProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(Load("vkGetPhysicalDeviceQueueFamilyProperties"));
            CreateDevice                          = reinterpret_cast<PFN_vkCreateDevice>(Load("vkCreateDevice"));
            DestroyDevice                         = reinterpret_cast<PFN_vkDestroyDevice>(Load("vkDestroyDevice"));
            GetDeviceQueue                        = reinterpret_cast<PFN_vkGetDeviceQueue>(Load("vkGetDeviceQueue"));
            if (!CreateInstance || !EnumeratePhysicalDevices || !CreateDevice)
            {
                OutError = "the loader is missing core entry points";
                return false;
            }
            return true;
        }

        void Close()
        {
            if (Handle) dlclose(Handle);
            Handle = nullptr;
        }
    };

    VulkanSourced SourcedFrom(PFN_vkGetDeviceProcAddr GetDeviceProcAddr)
    {
        VulkanSourced Out;
        const auto Load = [&](const char* Name) { return GetDeviceProcAddr(nullptr, Name); };
        Out.GetDeviceProcAddr              = GetDeviceProcAddr;
        Out.CreateShaderModule             = reinterpret_cast<PFN_vkCreateShaderModule>(Load("vkCreateShaderModule"));
        Out.DestroyShaderModule            = reinterpret_cast<PFN_vkDestroyShaderModule>(Load("vkDestroyShaderModule"));
        Out.CreateDescriptorSetLayout      = reinterpret_cast<PFN_vkCreateDescriptorSetLayout>(Load("vkCreateDescriptorSetLayout"));
        Out.DestroyDescriptorSetLayout     = reinterpret_cast<PFN_vkDestroyDescriptorSetLayout>(Load("vkDestroyDescriptorSetLayout"));
        Out.CreatePipelineLayout           = reinterpret_cast<PFN_vkCreatePipelineLayout>(Load("vkCreatePipelineLayout"));
        Out.DestroyPipelineLayout          = reinterpret_cast<PFN_vkDestroyPipelineLayout>(Load("vkDestroyPipelineLayout"));
        Out.CreateComputePipelines         = reinterpret_cast<PFN_vkCreateComputePipelines>(Load("vkCreateComputePipelines"));
        Out.DestroyPipeline                = reinterpret_cast<PFN_vkDestroyPipeline>(Load("vkDestroyPipeline"));
        Out.CreateDescriptorPool           = reinterpret_cast<PFN_vkCreateDescriptorPool>(Load("vkCreateDescriptorPool"));
        Out.DestroyDescriptorPool          = reinterpret_cast<PFN_vkDestroyDescriptorPool>(Load("vkDestroyDescriptorPool"));
        Out.AllocateDescriptorSets         = reinterpret_cast<PFN_vkAllocateDescriptorSets>(Load("vkAllocateDescriptorSets"));
        Out.UpdateDescriptorSets           = reinterpret_cast<PFN_vkUpdateDescriptorSets>(Load("vkUpdateDescriptorSets"));
        Out.AllocateMemory                 = reinterpret_cast<PFN_vkAllocateMemory>(Load("vkAllocateMemory"));
        Out.FreeMemory                     = reinterpret_cast<PFN_vkFreeMemory>(Load("vkFreeMemory"));
        Out.CreateBuffer                   = reinterpret_cast<PFN_vkCreateBuffer>(Load("vkCreateBuffer"));
        Out.DestroyBuffer                  = reinterpret_cast<PFN_vkDestroyBuffer>(Load("vkDestroyBuffer"));
        Out.GetBufferMemoryRequirements    = reinterpret_cast<PFN_vkGetBufferMemoryRequirements>(Load("vkGetBufferMemoryRequirements"));
        Out.BindBufferMemory               = reinterpret_cast<PFN_vkBindBufferMemory>(Load("vkBindBufferMemory"));
        Out.MapMemory                      = reinterpret_cast<PFN_vkMapMemory>(Load("vkMapMemory"));
        Out.UnmapMemory                    = reinterpret_cast<PFN_vkUnmapMemory>(Load("vkUnmapMemory"));
        Out.FlushMappedMemoryRanges        = reinterpret_cast<PFN_vkFlushMappedMemoryRanges>(Load("vkFlushMappedMemoryRanges"));
        Out.InvalidateMappedMemoryRanges   = reinterpret_cast<PFN_vkInvalidateMappedMemoryRanges>(Load("vkInvalidateMappedMemoryRanges"));
        Out.CreateCommandPool              = reinterpret_cast<PFN_vkCreateCommandPool>(Load("vkCreateCommandPool"));
        Out.DestroyCommandPool             = reinterpret_cast<PFN_vkDestroyCommandPool>(Load("vkDestroyCommandPool"));
        Out.AllocateCommandBuffers         = reinterpret_cast<PFN_vkAllocateCommandBuffers>(Load("vkAllocateCommandBuffers"));
        Out.ResetCommandBuffer             = reinterpret_cast<PFN_vkResetCommandBuffer>(Load("vkResetCommandBuffer"));
        Out.BeginCommandBuffer             = reinterpret_cast<PFN_vkBeginCommandBuffer>(Load("vkBeginCommandBuffer"));
        Out.EndCommandBuffer               = reinterpret_cast<PFN_vkEndCommandBuffer>(Load("vkEndCommandBuffer"));
        Out.CmdBindPipeline                = reinterpret_cast<PFN_vkCmdBindPipeline>(Load("vkCmdBindPipeline"));
        Out.CmdBindDescriptorSets          = reinterpret_cast<PFN_vkCmdBindDescriptorSets>(Load("vkCmdBindDescriptorSets"));
        Out.CmdPushConstants               = reinterpret_cast<PFN_vkCmdPushConstants>(Load("vkCmdPushConstants"));
        Out.CmdDispatch                    = reinterpret_cast<PFN_vkCmdDispatch>(Load("vkCmdDispatch"));
        Out.CmdPipelineBarrier             = reinterpret_cast<PFN_vkCmdPipelineBarrier>(Load("vkCmdPipelineBarrier"));
        Out.CmdWriteTimestamp              = reinterpret_cast<PFN_vkCmdWriteTimestamp>(Load("vkCmdWriteTimestamp"));
        Out.QueueSubmit                    = reinterpret_cast<PFN_vkQueueSubmit>(Load("vkQueueSubmit"));
        Out.QueueWaitIdle                  = reinterpret_cast<PFN_vkQueueWaitIdle>(Load("vkQueueWaitIdle"));
        Out.CreateFence                    = reinterpret_cast<PFN_vkCreateFence>(Load("vkCreateFence"));
        Out.DestroyFence                   = reinterpret_cast<PFN_vkDestroyFence>(Load("vkDestroyFence"));
        Out.WaitForFences                  = reinterpret_cast<PFN_vkWaitForFences>(Load("vkWaitForFences"));
        Out.ResetFences                    = reinterpret_cast<PFN_vkResetFences>(Load("vkResetFences"));
        Out.CreateQueryPool                = reinterpret_cast<PFN_vkCreateQueryPool>(Load("vkCreateQueryPool"));
        Out.DestroyQueryPool               = reinterpret_cast<PFN_vkDestroyQueryPool>(Load("vkDestroyQueryPool"));
        Out.GetQueryPoolResults            = reinterpret_cast<PFN_vkGetQueryPoolResults>(Load("vkGetQueryPoolResults"));
        Out.GetPhysicalDeviceProperties    = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(Load("vkGetPhysicalDeviceProperties"));
        Out.GetPhysicalDeviceMemoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(Load("vkGetPhysicalDeviceMemoryProperties"));
        return Out;
    }

    struct SceneBounds { float Min[3]; float Max[3]; };

    SceneBounds Measure(const std::vector<TriangleIndex>& Tris)
    {
        SceneBounds B{ { 1.0e30f, 1.0e30f, 1.0e30f }, { -1.0e30f, -1.0e30f, -1.0e30f } };
        for (const TriangleIndex& T : Tris)
        {
            const float X[3][3] = {
                { T.VertexAlphaX, T.VertexAlphaY, T.VertexAlphaZ },
                { T.VertexBetaX,  T.VertexBetaY,  T.VertexBetaZ  },
                { T.VertexGammaX, T.VertexGammaY, T.VertexGammaZ }
            };
            for (int V = 0; V < 3; ++V)
                for (int C = 0; C < 3; ++C)
                {
                    B.Min[C] = std::min(B.Min[C], X[V][C]);
                    B.Max[C] = std::max(B.Max[C], X[V][C]);
                }
        }
        return B;
    }

    // The deformed soup the refit path is checked with: the same level, pushed along a smooth wave — a DEFORMATION, not a
    //    topology change, which is exactly the contract `BlasRefit` has. (A topology change is what the build covers.)
    std::vector<TriangleIndex> Wave(const std::vector<TriangleIndex>& Rest, float Amplitude, float Wavelength)
    {
        std::vector<TriangleIndex> Out = Rest;
        const SceneBounds B = Measure(Rest);
        const auto Shift = [&](float& X, float& Y, float& Z)
        {
            const float U = (X - B.Min[0]) / std::max(1.0e-6f, B.Max[0] - B.Min[0]);
            const float V = (Y - B.Min[1]) / std::max(1.0e-6f, B.Max[1] - B.Min[1]);
            Z += Amplitude * std::sin(6.2831853f * (U + 2.0f * V) / std::max(1.0e-6f, Wavelength));
        };
        for (TriangleIndex& T : Out)
        {
            Shift(T.VertexAlphaX, T.VertexAlphaY, T.VertexAlphaZ);
            Shift(T.VertexBetaX,  T.VertexBetaY,  T.VertexBetaZ);
            Shift(T.VertexGammaX, T.VertexGammaY, T.VertexGammaZ);
        }
        return Out;
    }
} // namespace

int main(int ArgumentCount, char** Arguments)
{
    std::string ShaderDir;
    bool DoRefit = false;
    float Amplitude = 0.05f;
    for (int I = 1; I < ArgumentCount; ++I)
    {
        const std::string Argument = Arguments[I];
        if (Argument == "--shaders" && I + 1 < ArgumentCount) ShaderDir = Arguments[++I];
        else if (Argument == "--refit") DoRefit = true;
        else if (Argument == "--amplitude" && I + 1 < ArgumentCount) Amplitude = std::strtof(Arguments[++I], nullptr);
        else if (Argument == "--help")
        {
            std::printf("BlasDeviceRun — run D9's build/refit kernels on a Vulkan device and check them against the CPU mirror\n"
                        "  --shaders <dir>   where BlasBuild.spv / BlasRefit.spv live (default: Engine/Shaders)\n"
                        "  --refit           also run the refit path over a deformed soup\n"
                        "  --amplitude <m>   the deformation's height (default 0.05)\n");
            return 0;
        }
    }

    std::printf("[blas-device] D9 build/refit on a Vulkan device, verified against the CPU mirror\n");

    // ── the level, and the mirror's answer, BEFORE any Vulkan object exists: the comparison the run is for ──────────
    MaterialSwatchStructure Library;
    Library.Construct();
    const std::vector<TriangleIndex>& Soup = Library.QueryTriangles();
    if (Soup.empty()) { std::printf("[blas-device] RED — the level builder produced no triangles\n"); return 1; }
    const SceneBounds Bounds = Measure(Soup);
    std::printf("[blas-device] M10 soup: %zu triangles · AABB [%.2f %.2f %.2f] .. [%.2f %.2f %.2f]\n", Soup.size(),
                static_cast<double>(Bounds.Min[0]), static_cast<double>(Bounds.Min[1]), static_cast<double>(Bounds.Min[2]),
                static_cast<double>(Bounds.Max[0]), static_cast<double>(Bounds.Max[1]), static_cast<double>(Bounds.Max[2]));

    BlasBuildMirrorMetrics MirrorMetrics;
    std::vector<float> MirrorNodes, MirrorLeaves;
    std::vector<uint16_t> MirrorLevels;
    uint32_t MirrorMaxLevel = 0u;
    const auto MirrorStart = std::chrono::steady_clock::now();
    if (!BlasBuildMirror::BuildHPloc(Soup, MirrorNodes, MirrorLeaves, MirrorMetrics))
    {
        std::printf("[blas-device] RED — the mirror refused the level\n");
        return 1;
    }
    BlasBuildMirror::LevelsOf(MirrorNodes, 0u, MirrorMetrics.NodeBlocks, MirrorLevels, MirrorMaxLevel);
    const double MirrorMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - MirrorStart).count();
    Say("the mirror's answer: %u nodes · %u leaf records · %u levels (%u cell(s)) · %.1f ms on the CPU",
        MirrorMetrics.NodeCount, MirrorMetrics.TriangleCount, MirrorMaxLevel + 1u,
        BlasBuildLevelCap(static_cast<uint32_t>(Soup.size())), MirrorMs);

    // ── Vulkan ──────────────────────────────────────────────────────────────────────────────────────────────────────
    Loader Loader_;
    std::string Error;
    if (!Loader_.Open(Error))
    {
        std::printf("[blas-device] SKIP — %s. This program needs a Vulkan device; it has been compiled and gated without one.\n",
                    Error.c_str());
        return 2;
    }

    VkApplicationInfo Application{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    Application.pApplicationName = "Frontier BLAS device run";
    Application.apiVersion       = VK_API_VERSION_1_1;
    VkInstanceCreateInfo InstanceInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    InstanceInfo.pApplicationInfo = &Application;
    VkInstance Instance = VK_NULL_HANDLE;
    if (Loader_.CreateInstance(&InstanceInfo, nullptr, &Instance) != VK_SUCCESS)
    {
        std::printf("[blas-device] SKIP — vkCreateInstance failed (no driver?)\n");
        return 2;
    }

    uint32_t DeviceCount = 0u;
    Loader_.EnumeratePhysicalDevices(Instance, &DeviceCount, nullptr);
    std::vector<VkPhysicalDevice> Devices(DeviceCount);
    if (DeviceCount) Loader_.EnumeratePhysicalDevices(Instance, &DeviceCount, Devices.data());
    if (Devices.empty())
    {
        std::printf("[blas-device] SKIP — no physical device\n");
        Loader_.DestroyInstance(Instance, nullptr);
        return 2;
    }

    // Discrete GPUs first: the question this run answers is about the format and the algorithm, and an integrated part
    //    would answer it too, but the reported timings should be the ones a build would actually be dispatched on.
    VkPhysicalDevice Physical = Devices[0];
    int BestScore = -1;
    for (VkPhysicalDevice Candidate : Devices)
    {
        VkPhysicalDeviceProperties Properties{};
        Loader_.GetPhysicalDeviceProperties(Candidate, &Properties);
        const int Score = Properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3
                        : Properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
        if (Score > BestScore) { BestScore = Score; Physical = Candidate; }
    }
    VkPhysicalDeviceProperties PhysicalProperties{};
    Loader_.GetPhysicalDeviceProperties(Physical, &PhysicalProperties);
    Say("device: %s (API %u.%u.%u)", PhysicalProperties.deviceName,
        VK_VERSION_MAJOR(PhysicalProperties.apiVersion), VK_VERSION_MINOR(PhysicalProperties.apiVersion),
        VK_VERSION_PATCH(PhysicalProperties.apiVersion));

    uint32_t FamilyCount = 0u;
    Loader_.GetPhysicalDeviceQueueFamilyProperties(Physical, &FamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> Families(FamilyCount);
    if (FamilyCount) Loader_.GetPhysicalDeviceQueueFamilyProperties(Physical, &FamilyCount, Families.data());
    uint32_t ComputeFamily = UINT32_MAX;
    for (uint32_t Index = 0u; Index < FamilyCount; ++Index)
    {
        if ((Families[Index].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0u) continue;
        // Same preference the engine's device selection uses: a family that is ONLY compute is the quiet one.
        const bool General = (Families[Index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u;
        if (ComputeFamily == UINT32_MAX || !General) ComputeFamily = Index;
        if (!General) break;
    }
    if (ComputeFamily == UINT32_MAX)
    {
        std::printf("[blas-device] SKIP — no compute queue family\n");
        Loader_.DestroyInstance(Instance, nullptr);
        return 2;
    }

    const float Priority = 1.0f;
    VkDeviceQueueCreateInfo QueueInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    QueueInfo.queueFamilyIndex = ComputeFamily;
    QueueInfo.queueCount       = 1u;
    QueueInfo.pQueuePriorities = &Priority;
    VkDeviceCreateInfo DeviceInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    DeviceInfo.queueCreateInfoCount = 1u;
    DeviceInfo.pQueueCreateInfos    = &QueueInfo;
    VkDevice Device = VK_NULL_HANDLE;
    if (Loader_.CreateDevice(Physical, &DeviceInfo, nullptr, &Device) != VK_SUCCESS)
    {
        std::printf("[blas-device] SKIP — vkCreateDevice failed\n");
        Loader_.DestroyInstance(Instance, nullptr);
        return 2;
    }
    VkQueue Queue = VK_NULL_HANDLE;
    Loader_.GetDeviceQueue(Device, ComputeFamily, 0u, &Queue);

    const VulkanSourced Api = SourcedFrom(Loader_.GetDeviceProcAddr);
    if (!Api.Complete())
    {
        std::printf("[blas-device] SKIP — the device entry points are incomplete\n");
        Loader_.DestroyDevice(Device, nullptr);
        Loader_.DestroyInstance(Instance, nullptr);
        return 2;
    }

    BlasBuildPipeline Pipeline;
    if (!Pipeline.Build(Device, Api, ShaderDir, Error))
    {
        std::printf("[blas-device] RED — %s\n", Error.c_str());
        Loader_.DestroyDevice(Device, nullptr);
        Loader_.DestroyInstance(Instance, nullptr);
        return 1;
    }

    // ── command pool, command buffer, fence ────────────────────────────────────────────────────────────────────────
    VkCommandPoolCreateInfo PoolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    PoolInfo.queueFamilyIndex = ComputeFamily;
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    Api.CreateCommandPool(Device, &PoolInfo, nullptr, &CommandPool);
    VkCommandBufferAllocateInfo CommandInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    CommandInfo.commandPool        = CommandPool;
    CommandInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    CommandInfo.commandBufferCount = 1u;
    VkCommandBuffer Command = VK_NULL_HANDLE;
    Api.AllocateCommandBuffers(Device, &CommandInfo, &Command);
    VkFenceCreateInfo FenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence Fence = VK_NULL_HANDLE;
    Api.CreateFence(Device, &FenceInfo, nullptr, &Fence);

    // ① THE BUILD — upload, record the plan's dispatches, submit, wait, and compare against the mirror's bytes.
    BlasDeviceJob Job;
    if (!Pipeline.CreateJob(Physical, Device, Api, Soup, Bounds.Min, Bounds.Max, MirrorNodes, MirrorLeaves, MirrorMetrics,
                            MirrorLevels, Job, Error))
    {
        std::printf("[blas-device] RED — job creation: %s\n", Error.c_str());
        return 1;
    }
    Say("arena: %u node slots (bound = triangles / 3 — a cap, not a reservation) · the mirror filled %u of them",
        Job.NodeSlots, MirrorMetrics.NodeCount);

    // ⚠️ The recording happens INSIDE this lambda, after BeginCommandBuffer: passing a pre-recorded buffer in would
    //    record into a command buffer that is not in the recording state, which is invalid usage and — the first time it
    //    ran on a real device — would have been the whole run's failure, on a machine nobody could debug it on.
    auto SubmitAndWait = [&](const std::function<bool()>& Record) -> bool
    {
        Api.ResetCommandBuffer(Command, 0u);
        VkCommandBufferBeginInfo Begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (Api.BeginCommandBuffer(Command, &Begin) != VK_SUCCESS) return false;
        if (!Record()) return false;
        if (Api.EndCommandBuffer(Command) != VK_SUCCESS) return false;
        VkSubmitInfo Submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        Submit.commandBufferCount = 1u;
        Submit.pCommandBuffers    = &Command;
        Api.ResetFences(Device, 1u, &Fence);
        if (Api.QueueSubmit(Queue, 1u, &Submit, Fence) != VK_SUCCESS) return false;
        if (Api.WaitForFences(Device, 1u, &Fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) return false;
        return true;
    };

    const std::chrono::steady_clock::time_point BuildStart = std::chrono::steady_clock::now();
    const bool Recorded = SubmitAndWait([&] { return Pipeline.RecordBuild(Command, Job, true, Error); });
    const double BuildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - BuildStart).count();
    if (!Recorded)
    {
        std::printf("[blas-device] RED — recording the build: %s\n", Error.c_str());
        return 1;
    }
    Say("build: %u dispatches · %.2f ms (records, submits and waits — the number a frame would pay includes the barriers, "
        "not the fence)", Pipeline.BuildDispatches(), BuildMs);

    std::string Report;
    const bool BuildMatches = Pipeline.Verify(Device, Api, Job, Report, Error);
    if (BuildMatches) Pass("① the DEVICE's build equals the MIRROR's build: %s", Report.c_str());
    else              Fail("① the device's build differs from the mirror's: %s (%s)", Report.c_str(), Error.c_str());

    // ② THE REFIT — the deformed soup through the same blob, then against a mirror refit of the same deformation. The
    //    comparison is not "did it change" but "did it change to the mirror's bytes": a refit that wrote nothing would
    //    otherwise pass a laxer check.
    if (DoRefit)
    {
        const std::vector<TriangleIndex> Deformed = Wave(Soup, Amplitude, 1.0f);
        std::vector<float> RefitNodes = MirrorNodes;   // the device's own post-build blob is overwritten in place
        std::vector<float> RefitLeaves = MirrorLeaves;
        std::vector<uint16_t> RefitLevels;
        uint32_t RefitMaxLevel = 0u;
        BlasBuildMirror::LevelsOf(RefitNodes, 0u, MirrorMetrics.NodeBlocks, RefitLevels, RefitMaxLevel);
        // (LevelsOf's last parameter is the deepest level present; the refit reads the table, not the number, but the
        //    dispatch loop's count comes from it — see BuildBlasRefitPlan.)

        // The mirror's refit of the same deformation, over a COPY of the device's built blob: the level-ordered path is
        //    the one BlasRefit.slang transcribes (§⑨e proves it identical to D8's descending sweep), so this comparison
        //    is against the kernel's own algorithm, not against a different one.
        std::vector<float> MirrorRefitNodes = RefitNodes, MirrorRefitLeaves = RefitLeaves;
        BlasBuildMirrorMetrics RefitMetrics = MirrorMetrics;
        const auto MirrorRefitStart = std::chrono::steady_clock::now();
        const bool MirrorRefitOk = BlasBuildMirror::RefitLevelOrder(MirrorRefitNodes, 0u, MirrorRefitLeaves, 0u, Deformed,
                                                                    RefitLevels, RefitMetrics);
        const double MirrorRefitMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - MirrorRefitStart).count();

        // The device's refit: the deformed soup replaces the soup buffer, then the plan's dispatches run over the blob
        //    the build just wrote.
        std::vector<float> DeformedSoup;
        if (!PackBlasSoup(Deformed, DeformedSoup))
        {
            Fail("② could not pack the deformed soup");
        }
        else if (!Pipeline.UploadSoup(Device, Job, DeformedSoup, Error))
        {
            Fail("② the deformed soup upload failed: %s", Error.c_str());
        }
        else
        {
            const std::chrono::steady_clock::time_point RefitStart = std::chrono::steady_clock::now();
            const bool RefitRecorded = SubmitAndWait([&] { return Pipeline.RecordRefit(Command, Job, RefitMaxLevel, true, Error); });
            const double RefitMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - RefitStart).count();
            if (!RefitRecorded) Fail("② recording the refit: %s", Error.c_str());
            else
            {
                Say("refit: %u dispatches · %.2f ms on the device · %.2f ms on the CPU mirror", Pipeline.RefitDispatches(),
                    RefitMs, MirrorRefitMs);
                BlasDeviceJob RefitJob = Job;
                RefitJob.ExpectedNodes  = MirrorRefitNodes;
                RefitJob.ExpectedLeaves = MirrorRefitLeaves;
                RefitJob.MirrorMetrics  = MirrorMetrics;   // the refit keeps the topology, so the node count is the same
                std::string RefitReport;
                const bool RefitMatches = Pipeline.Verify(Device, Api, RefitJob, RefitReport, Error);
                if (!MirrorRefitOk) Fail("② the mirror's own refit refused the deformation");
                else if (RefitMatches) Pass("② the DEVICE's refit equals the MIRROR's refit: %s", RefitReport.c_str());
                else Fail("② the device's refit differs from the mirror's: %s (%s)", RefitReport.c_str(), Error.c_str());
            }
        }
    }

    // ── teardown ────────────────────────────────────────────────────────────────────────────────────────────────────
    Pipeline.DestroyJob(Device, Job);
    Api.DestroyFence(Device, Fence, nullptr);
    Api.DestroyCommandPool(Device, CommandPool, nullptr);
    Pipeline.Destroy(Device);
    Loader_.DestroyDevice(Device, nullptr);
    Loader_.DestroyInstance(Instance, nullptr);
    Loader_.Close();

    if (Failures == 0)
    {
        std::printf("[blas-device] GREEN — the device and the CPU mirror agree on every byte they were asked about\n");
        return 0;
    }
    std::printf("[blas-device] RED — %d failure(s)\n", Failures);
    return 1;
}
