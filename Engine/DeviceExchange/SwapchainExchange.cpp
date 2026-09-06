//============================================================================================================================================
//                                                     SWAPCHAINEXCHANGE.CPP
//============================================================================================================================================
// 🧩 Vulkan instance, surface, device, swapchain and recording-slot transport across the hardware vendor edge.

#include <vulkan/vulkan.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <thorvg.h>

#include "SwapchainExchange.h"
#include "../ContentInterchange/MaterialIndex.h"
#include "../ContentInterchange/TextureIndex.h"
#include "../GeometricRaster/TraversalIndex.h"
#include "../GeometricRaster/SceneStructure.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#if defined(_WIN32)
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   include <windows.h>
#elif defined(__APPLE__)
#   include <mach-o/dyld.h>
#else
#   include <unistd.h>
#endif

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                           CONSTANTS AND INTERNAL LIMITS
//------------------------------------------------------------------------------------------------------------------------

static constexpr uint32_t kCycleSlotCount  = 2u;
static constexpr uint32_t kLocalGroupSizeX = 16u;
static constexpr uint32_t kLocalGroupSizeY = 16u;

//------------------------------------------------------------------------------------------------------------------------
//                                              VULKAN RECORD DEFINITION
//------------------------------------------------------------------------------------------------------------------------

struct SwapchainExchange::VulkanRecord
{
    // ── Instance and surface ──────────────────────────────────────────────────────────────────────────────────────────
    VkInstance               Instance              = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT DebugMessenger        = VK_NULL_HANDLE;
    VkSurfaceKHR             Surface               = VK_NULL_HANDLE;

    // ── Physical and logical device ───────────────────────────────────────────────────────────────────────────────────
    VkPhysicalDevice                  PhysicalDevice  = VK_NULL_HANDLE;
    VkDevice                          Device          = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties  MemoryProperties{};
    uint32_t                          GraphicsFamily  = 0u;
    uint32_t                          ComputeFamily   = 0u;
    VkQueue                           GraphicsQueue   = VK_NULL_HANDLE;
    VkQueue                           ComputeQueue    = VK_NULL_HANDLE;

    // ── Swapchain ─────────────────────────────────────────────────────────────────────────────────────────────────────
    VkSwapchainKHR           Swapchain             = VK_NULL_HANDLE;
    VkFormat                 SwapchainFormat       = VK_FORMAT_UNDEFINED;
    VkExtent2D               SwapchainExtent       = {};
    std::vector<VkImage>     SwapchainImages;
    std::vector<VkImageView> SwapchainImageViews;

    // ── Storage image (compute writes; blit to swapchain) ────────────────────────────────────────────────────────────
    VkImage                  StorageImage          = VK_NULL_HANDLE;
    VkDeviceMemory           StorageMemory         = VK_NULL_HANDLE;
    VkImageView              StorageImageView      = VK_NULL_HANDLE;

    // ── History image (linear HDR running mean for temporal accumulation; rgba32f, .a = sample count) ────────────────
    VkImage                  HistoryImage          = VK_NULL_HANDLE;
    VkDeviceMemory           HistoryMemory         = VK_NULL_HANDLE;
    VkImageView              HistoryImageView      = VK_NULL_HANDLE;
    bool                     HistoryInitialised    = false;             // [-]  layout transitioned to GENERAL once

    // ── Scene SSBO geometry and materials ────────────────────────────────────────────────────────────────────────────
    VkBuffer                 TriangleBuffer        = VK_NULL_HANDLE;
    VkDeviceMemory           TriangleMemory        = VK_NULL_HANDLE;
    VkBuffer                 MaterialBuffer        = VK_NULL_HANDLE;   // R4a: MaterialRecord[] (binding 2)
    VkDeviceMemory           MaterialMemory        = VK_NULL_HANDLE;
    VkBuffer                 SlabBuffer            = VK_NULL_HANDLE;   // R4a: MaterialSlabRecord[] (binding 10)
    VkDeviceMemory           SlabMemory            = VK_NULL_HANDLE;
    // R4a bindless textures (binding 15 since R4b, sampler2D[], partially bound): one image + view per resident texture
    struct ResidentTexture { VkImage Image = VK_NULL_HANDLE; VkDeviceMemory Memory = VK_NULL_HANDLE; VkImageView View = VK_NULL_HANDLE; };
    std::vector<ResidentTexture> Textures;
    VkSampler                TextureSampler        = VK_NULL_HANDLE;
    ResidentTexture          ShadingTables[2];                       // R4b: 0 = GGX energy (A, B, E_avg), 1 = LTC sheen (aInv, bInv, R) — RGBA32F 32×32
    VkSampler                TableSampler          = VK_NULL_HANDLE; // linear, clamp-to-edge, no mips
    bool                     DescriptorIndexing    = false;   // runtimeDescriptorArray + partially bound granted by the driver
    VkBuffer                 TraversalNodeBuffer   = VK_NULL_HANDLE;   // R3 CWBVH nodes (binding 8)
    VkDeviceMemory           TraversalNodeMemory   = VK_NULL_HANDLE;
    VkBuffer                 TraversalLeafBuffer   = VK_NULL_HANDLE;   // R3 CWBVH triangles (binding 9)
    VkDeviceMemory           TraversalLeafMemory   = VK_NULL_HANDLE;
    // R6 temporal reservoirs: two W×H×64 B SSBOs (bindings 16/17), ping-ponged per presented frame. Record layout
    //    (std430, mirrors GpuReservoir in ReSTIRViewport.slang): Sample(xyz point, w WeightSum) · Counts(M, light,
    //    Visible, Age) · UvDepth(uv, W, view depth) · Normal(xyz geometric normal, w stride guard).
    struct ReservoirBufferRecord { float Sample[4]; uint32_t Counts[4]; float UvDepth[4]; float Normal[4]; };
    static_assert(sizeof(ReservoirBufferRecord) == 64u, "GpuReservoir stride must be 64 B (matches the shader)");
    VkBuffer                 ReservoirBuffers[2]   = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDeviceMemory           ReservoirMemories[2]  = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDeviceSize             ReservoirBytes        = 0u;   // [B] per buffer (W×H×64)
    bool                     ReservoirParity       = false;   // [-]  false: 0 = prev / 1 = curr; flipped per frame
    bool                     ReservoirsInitialised = false;   // [-]  zero-filled once before first dispatch
    uint32_t                 TriangleCount         = 0u;
    uint32_t                 MaterialCount         = 0u;

    // ── Compute pipeline ──────────────────────────────────────────────────────────────────────────────────────────────
    VkDescriptorSetLayout    ComputeDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool         ComputeDescriptorPool   = VK_NULL_HANDLE;
    VkDescriptorSet          ComputeDescriptorSet    = VK_NULL_HANDLE;
    VkPipelineLayout         ComputePipelineLayout   = VK_NULL_HANDLE;
    VkPipeline               ComputePipeline         = VK_NULL_HANDLE;

    // ── Command recording ─────────────────────────────────────────────────────────────────────────────────────────────
    VkCommandPool                ComputeCommandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> ComputeCommands;

    // ── ImGui render pass and framebuffers ───────────────────────────────────────────────────────────────────────────
    VkDescriptorPool         ImGuiDescriptorPool   = VK_NULL_HANDLE;
    VkRenderPass             ImGuiRenderPass       = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> ImGuiFramebuffers;

    // ── Cycle slots (one fence + two semaphores per slot) ────────────────────────────────────────────────────────────
    std::array<VkSemaphore, kCycleSlotCount> AcquireSemaphores = {};
    std::vector<VkSemaphore>                ReleaseSemaphores;   // [-] per-image render-complete semaphore
    std::array<VkFence,     kCycleSlotCount> CycleFences       = {};
    std::vector<VkFence>                     ImageOrdinalFences;  // [-]  per-image in-flight fence pointer
    uint32_t                                 ActiveSlot         = 0u;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  VALIDATION CALLBACK
//------------------------------------------------------------------------------------------------------------------------

static VKAPI_ATTR VkBool32 VKAPI_CALL ValidationCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* CallbackData,
    void*) noexcept
{
    std::cerr << "[SwapchainExchange] Validation: " << CallbackData->pMessage << "\n";
    return VK_FALSE;
}

//------------------------------------------------------------------------------------------------------------------------
//                                               SPIRV LOADER
//------------------------------------------------------------------------------------------------------------------------

static std::filesystem::path QueryExecutableDirectory()
{
#if defined(_WIN32)
    wchar_t Buffer[MAX_PATH]{};
    const DWORD Length = GetModuleFileNameW(nullptr, Buffer, MAX_PATH);
    if (Length == 0u) return {};
    return std::filesystem::path(Buffer).parent_path();
#elif defined(__APPLE__)
    char     Buffer[4096]{};
    uint32_t Size = sizeof(Buffer);
    if (_NSGetExecutablePath(Buffer, &Size) != 0) return {};
    return std::filesystem::path(Buffer).parent_path();
#else
    char Buffer[4096]{};
    const ssize_t Length = readlink("/proc/self/exe", Buffer, sizeof(Buffer) - 1u);
    if (Length <= 0) return {};
    return std::filesystem::path(std::string(Buffer, static_cast<size_t>(Length))).parent_path();
#endif
}

// Resolves a repository-relative asset path. Order of preference:
//    ① relative to the current working directory (running from the repository root)
//    ② next to the executable, then walking up its parents (double-clicking the .exe in Build\Output\...\Binary)
static std::filesystem::path ResolveAssetPath(const std::string& RelativePath)
{
    std::error_code Error;

    if (std::filesystem::exists(RelativePath, Error)) return RelativePath;

    std::filesystem::path Probe = QueryExecutableDirectory();
    for (int Depth = 0; Depth < 12 && !Probe.empty(); ++Depth)
    {
        const std::filesystem::path Candidate = Probe / RelativePath;
        if (std::filesystem::exists(Candidate, Error)) return Candidate;

        const std::filesystem::path Parent = Probe.parent_path();
        if (Parent == Probe) break;
        Probe = Parent;
    }

    return RelativePath;
}

static std::vector<uint32_t> LoadSpirv(const std::string& RelativePath)
{
    const std::filesystem::path Path = ResolveAssetPath(RelativePath);

    std::ifstream File(Path, std::ios::binary | std::ios::ate);
    if (!File.is_open())
    {
        std::cerr << "[SwapchainExchange] Cannot open SPIR-V: " << RelativePath
                  << " (searched the working directory and the executable's parent folders). "
                  << "Run the build script so the shader is lowered, or launch from the repository root.\n";
        return {};
    }
    const std::streamsize ByteCount = File.tellg();
    if (ByteCount < 4 || (ByteCount % 4) != 0)
    {
        std::cerr << "[SwapchainExchange] SPIR-V file is malformed: " << Path.string() << "\n";
        return {};
    }
    std::vector<uint32_t> Spirv(static_cast<size_t>(ByteCount) / 4u);
    File.seekg(0);
    File.read(reinterpret_cast<char*>(Spirv.data()), ByteCount);
    std::cerr << "[SwapchainExchange] Loaded SPIR-V: " << Path.string() << "\n";
    return Spirv;
}

//------------------------------------------------------------------------------------------------------------------------
//                                         BUFFER ALLOCATION HELPER
//------------------------------------------------------------------------------------------------------------------------

static void AllocateBuffer(
    VkDevice                           Device,
    VkPhysicalDeviceMemoryProperties&  MemoryProperties,
    VkDeviceSize                       ByteCount,
    VkBufferUsageFlags                 UsageFlags,
    uint32_t                           MemoryFlags,
    VkBuffer&                          OutBuffer,
    VkDeviceMemory&                    OutMemory) noexcept
{
    VkBufferCreateInfo BufferInfo{};
    BufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    BufferInfo.size        = ByteCount;
    BufferInfo.usage       = UsageFlags;
    BufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    (void)vkCreateBuffer(Device, &BufferInfo, nullptr, &OutBuffer);

    VkMemoryRequirements Requirements{};
    vkGetBufferMemoryRequirements(Device, OutBuffer, &Requirements);

    VkMemoryAllocateInfo AllocateInfo{};
    AllocateInfo.sType          = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    AllocateInfo.allocationSize = Requirements.size;
    for (uint32_t Index = 0u; Index < MemoryProperties.memoryTypeCount; ++Index)
    {
        if ((Requirements.memoryTypeBits & (1u << Index)) &&
            (MemoryProperties.memoryTypes[Index].propertyFlags & MemoryFlags) ==
             static_cast<VkMemoryPropertyFlags>(MemoryFlags))
        {
            AllocateInfo.memoryTypeIndex = Index;
            break;
        }
    }
    (void)vkAllocateMemory(Device, &AllocateInfo, nullptr, &OutMemory);
    vkBindBufferMemory(Device, OutBuffer, OutMemory, 0);
}

//============================================================================================================================================
//                                                     LIFECYCLE
//============================================================================================================================================

SwapchainExchange::SwapchainExchange(const SwapchainConfiguration& InitialConfiguration) noexcept
    : Vulkan(new VulkanRecord{})
    , GlfwWindow(nullptr)
    , Configuration(InitialConfiguration)
    , ResizePending(false)
    , Pacing(PresentPacingCategory::VerticalSyncOn)
    , ResolvedPresentMode(static_cast<uint32_t>(VK_PRESENT_MODE_FIFO_KHR))
    , FullscreenActive(false)
    , WindowedX(0), WindowedY(0), WindowedW(0), WindowedH(0)
    , ForwardInput(nullptr)
    , PreviousCursorX(0.0)
    , PreviousCursorY(0.0)
    , CursorInitialised(false)
    , PendingInputReset(false)
{
}

SwapchainExchange::~SwapchainExchange() noexcept
{
    Retire();
    delete Vulkan;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       BRING
//------------------------------------------------------------------------------------------------------------------------

static void OnGlfwError(int Code, const char* Description) noexcept
{
    std::cerr << "[SwapchainExchange] GLFW error " << Code << ": " << (Description ? Description : "") << "\n";
}

bool SwapchainExchange::Bring() noexcept
{
    glfwSetErrorCallback(OnGlfwError);

    if (!glfwInit())
    {
        std::cerr << "[SwapchainExchange] glfwInit failed.\n";
        return false;
    }

    if (!glfwVulkanSupported())
    {
        std::cerr << "[SwapchainExchange] glfwVulkanSupported() returned false - no Vulkan loader/ICD found (vulkan-1.dll missing or no Vulkan-capable driver).\n";
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE,  GLFW_TRUE);

    GlfwWindow = glfwCreateWindow(
        static_cast<int>(Configuration.Width),
        static_cast<int>(Configuration.Height),
        Configuration.Title ? Configuration.Title : "Frontier",
        nullptr, nullptr);

    if (!GlfwWindow)
    {
        std::cerr << "[SwapchainExchange] glfwCreateWindow failed.\n";
        return false;
    }

    glfwSetWindowUserPointer      (GlfwWindow, this);
    glfwSetKeyCallback            (GlfwWindow, OnKey);
    glfwSetMouseButtonCallback    (GlfwWindow, OnMouseButton);
    glfwSetCursorPosCallback      (GlfwWindow, OnCursorMove);
    glfwSetScrollCallback         (GlfwWindow, OnScroll);
    glfwSetFramebufferSizeCallback(GlfwWindow, OnFramebuffer);
    glfwSetWindowFocusCallback    (GlfwWindow, OnFocus);

    std::cerr << "[SwapchainExchange] Window created: " << Configuration.Width << "x" << Configuration.Height << "\n";

    tvg::Initializer::init(0u);

    // Each stage reports its own failure reason to stderr; the name here tells the reader which one stopped.
    struct Stage { const char* Name; bool (SwapchainExchange::*Fn)() noexcept; };
    const Stage Stages[] =
    {
        { "BringInstance",         &SwapchainExchange::BringInstance         },
        { "BringSurface",          &SwapchainExchange::BringSurface          },
        { "BringPhysicalDevice",   &SwapchainExchange::BringPhysicalDevice   },
        { "BringLogicalDevice",    &SwapchainExchange::BringLogicalDevice    },
        { "BringSwapchain",        &SwapchainExchange::BringSwapchain        },
        { "BringStorageImage",     &SwapchainExchange::BringStorageImage     },
        { "BringCommandRecording", &SwapchainExchange::BringCommandRecording },
        { "BringComputePipeline",  &SwapchainExchange::BringComputePipeline  },
        { "BringDescriptorSet",    &SwapchainExchange::BringDescriptorSet    },
        { "BringCycleSlots",       &SwapchainExchange::BringCycleSlots       },
        { "BringImGui",            &SwapchainExchange::BringImGui            },
        { "BringVisibility",       &SwapchainExchange::BringVisibility       },
    };

    for (const Stage& Current : Stages)
    {
        if (!(this->*Current.Fn)())
        {
            std::cerr << "[SwapchainExchange] Bring-up stopped at stage " << Current.Name << ".\n";
            return false;
        }
    }

    std::cerr << "[SwapchainExchange] Bring-up complete.\n";
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       RETIRE
//------------------------------------------------------------------------------------------------------------------------

void SwapchainExchange::Retire() noexcept
{
    if (!Vulkan || !Vulkan->Device) return;

    vkDeviceWaitIdle(Vulkan->Device);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    for (auto& Framebuffer : Vulkan->ImGuiFramebuffers)
        if (Framebuffer) vkDestroyFramebuffer(Vulkan->Device, Framebuffer, nullptr);
    if (Vulkan->ImGuiRenderPass)     vkDestroyRenderPass     (Vulkan->Device, Vulkan->ImGuiRenderPass,     nullptr);
    if (Vulkan->ImGuiDescriptorPool) vkDestroyDescriptorPool (Vulkan->Device, Vulkan->ImGuiDescriptorPool, nullptr);

    Visibility.Retire();
    RetireSwapchain();

    if (Vulkan->TriangleBuffer)  vkDestroyBuffer (Vulkan->Device, Vulkan->TriangleBuffer, nullptr);
    if (Vulkan->TriangleMemory)  vkFreeMemory    (Vulkan->Device, Vulkan->TriangleMemory, nullptr);
    if (Vulkan->MaterialBuffer)  vkDestroyBuffer (Vulkan->Device, Vulkan->MaterialBuffer, nullptr);
    if (Vulkan->MaterialMemory)  vkFreeMemory    (Vulkan->Device, Vulkan->MaterialMemory, nullptr);
    if (Vulkan->SlabBuffer)      vkDestroyBuffer (Vulkan->Device, Vulkan->SlabBuffer, nullptr);
    if (Vulkan->SlabMemory)      vkFreeMemory    (Vulkan->Device, Vulkan->SlabMemory, nullptr);
    DestroyTextures();
    if (Vulkan->TextureSampler)  vkDestroySampler(Vulkan->Device, Vulkan->TextureSampler, nullptr);
    for (VulkanRecord::ResidentTexture& T : Vulkan->ShadingTables)
    {
        if (T.View)   vkDestroyImageView(Vulkan->Device, T.View, nullptr);
        if (T.Image)  vkDestroyImage    (Vulkan->Device, T.Image, nullptr);
        if (T.Memory) vkFreeMemory      (Vulkan->Device, T.Memory, nullptr);
    }
    if (Vulkan->TableSampler)    vkDestroySampler(Vulkan->Device, Vulkan->TableSampler, nullptr);
    if (Vulkan->TraversalNodeBuffer) vkDestroyBuffer(Vulkan->Device, Vulkan->TraversalNodeBuffer, nullptr);
    if (Vulkan->TraversalNodeMemory) vkFreeMemory   (Vulkan->Device, Vulkan->TraversalNodeMemory, nullptr);
    if (Vulkan->TraversalLeafBuffer) vkDestroyBuffer(Vulkan->Device, Vulkan->TraversalLeafBuffer, nullptr);
    if (Vulkan->TraversalLeafMemory) vkFreeMemory   (Vulkan->Device, Vulkan->TraversalLeafMemory, nullptr);

    for (uint32_t Slot = 0u; Slot < kCycleSlotCount; ++Slot)
    {
        if (Vulkan->AcquireSemaphores[Slot]) vkDestroySemaphore(Vulkan->Device, Vulkan->AcquireSemaphores[Slot], nullptr);
        if (Vulkan->CycleFences[Slot])       vkDestroyFence    (Vulkan->Device, Vulkan->CycleFences[Slot],       nullptr);
    }
    for (VkSemaphore S : Vulkan->ReleaseSemaphores)
    {
        if (S) vkDestroySemaphore(Vulkan->Device, S, nullptr);
    }
    Vulkan->ReleaseSemaphores.clear();

    if (Vulkan->ComputeCommandPool)    vkDestroyCommandPool       (Vulkan->Device, Vulkan->ComputeCommandPool,    nullptr);
    if (Vulkan->ComputePipeline)       vkDestroyPipeline          (Vulkan->Device, Vulkan->ComputePipeline,       nullptr);
    if (Vulkan->ComputePipelineLayout) vkDestroyPipelineLayout    (Vulkan->Device, Vulkan->ComputePipelineLayout, nullptr);
    if (Vulkan->ComputeDescriptorPool) vkDestroyDescriptorPool    (Vulkan->Device, Vulkan->ComputeDescriptorPool, nullptr);
    if (Vulkan->ComputeDescriptorLayout) vkDestroyDescriptorSetLayout(Vulkan->Device, Vulkan->ComputeDescriptorLayout, nullptr);

    if (Vulkan->Device)   vkDestroyDevice             (Vulkan->Device,             nullptr);
    if (Vulkan->Surface)  vkDestroySurfaceKHR          (Vulkan->Instance, Vulkan->Surface, nullptr);
    if (Vulkan->DebugMessenger)
    {
        auto DestroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(Vulkan->Instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (DestroyMessenger) DestroyMessenger(Vulkan->Instance, Vulkan->DebugMessenger, nullptr);
        Vulkan->DebugMessenger = VK_NULL_HANDLE;
    }
    if (Vulkan->Instance) vkDestroyInstance            (Vulkan->Instance,           nullptr);

    tvg::Initializer::term();

    if (GlfwWindow) glfwDestroyWindow(GlfwWindow);
    glfwTerminate();
    GlfwWindow = nullptr;
}

//------------------------------------------------------------------------------------------------------------------------
//                                               RETIRE SWAPCHAIN  (inner)
//------------------------------------------------------------------------------------------------------------------------

void SwapchainExchange::RetireSwapchain() noexcept
{
    if (Vulkan->StorageImageView)  vkDestroyImageView(Vulkan->Device, Vulkan->StorageImageView,  nullptr);
    if (Vulkan->StorageImage)      vkDestroyImage    (Vulkan->Device, Vulkan->StorageImage,      nullptr);
    if (Vulkan->StorageMemory)     vkFreeMemory      (Vulkan->Device, Vulkan->StorageMemory,     nullptr);
    Vulkan->StorageImageView = VK_NULL_HANDLE;
    Vulkan->StorageImage     = VK_NULL_HANDLE;
    Vulkan->StorageMemory    = VK_NULL_HANDLE;

    if (Vulkan->HistoryImageView)  vkDestroyImageView(Vulkan->Device, Vulkan->HistoryImageView,  nullptr);
    if (Vulkan->HistoryImage)      vkDestroyImage    (Vulkan->Device, Vulkan->HistoryImage,      nullptr);
    if (Vulkan->HistoryMemory)     vkFreeMemory      (Vulkan->Device, Vulkan->HistoryMemory,     nullptr);
    Vulkan->HistoryImageView   = VK_NULL_HANDLE;
    Vulkan->HistoryImage       = VK_NULL_HANDLE;
    Vulkan->HistoryMemory      = VK_NULL_HANDLE;
    Vulkan->HistoryInitialised = false;

    for (uint32_t I = 0u; I < 2u; ++I)   // R6 temporal reservoirs (size-dependent, like storage/history)
    {
        if (Vulkan->ReservoirBuffers[I])  vkDestroyBuffer(Vulkan->Device, Vulkan->ReservoirBuffers[I], nullptr);
        if (Vulkan->ReservoirMemories[I]) vkFreeMemory   (Vulkan->Device, Vulkan->ReservoirMemories[I], nullptr);
        Vulkan->ReservoirBuffers[I]  = VK_NULL_HANDLE;
        Vulkan->ReservoirMemories[I] = VK_NULL_HANDLE;
    }
    Vulkan->ReservoirsInitialised = false;

    for (auto& ImageView : Vulkan->SwapchainImageViews)
        if (ImageView) vkDestroyImageView(Vulkan->Device, ImageView, nullptr);
    Vulkan->SwapchainImageViews.clear();

    for (VkSemaphore S : Vulkan->ReleaseSemaphores)
        if (S) vkDestroySemaphore(Vulkan->Device, S, nullptr);
    Vulkan->ReleaseSemaphores.clear();

    if (Vulkan->Swapchain) vkDestroySwapchainKHR(Vulkan->Device, Vulkan->Swapchain, nullptr);
    Vulkan->Swapchain = VK_NULL_HANDLE;
}

//============================================================================================================================================
//                                                   BRING-UP STAGES
//============================================================================================================================================

bool SwapchainExchange::BringInstance() noexcept
{
    VkApplicationInfo ApplicationInfo{};
    ApplicationInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ApplicationInfo.pApplicationName   = Configuration.Title;
    ApplicationInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    ApplicationInfo.pEngineName        = "Frontier";
    ApplicationInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    ApplicationInfo.apiVersion         = VK_API_VERSION_1_2;

    uint32_t     GlfwExtensionCount = 0u;
    const char** GlfwExtensions     = glfwGetRequiredInstanceExtensions(&GlfwExtensionCount);

    std::vector<const char*> Extensions(GlfwExtensions, GlfwExtensions + GlfwExtensionCount);
    std::vector<const char*> Layers;

    if (Configuration.ValidationEnabled)
    {
        Extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        Layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    VkInstanceCreateInfo InstanceInfo{};
    InstanceInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    InstanceInfo.pApplicationInfo        = &ApplicationInfo;
    InstanceInfo.enabledExtensionCount   = static_cast<uint32_t>(Extensions.size());
    InstanceInfo.ppEnabledExtensionNames = Extensions.data();
    InstanceInfo.enabledLayerCount       = static_cast<uint32_t>(Layers.size());
    InstanceInfo.ppEnabledLayerNames     = Layers.data();

    if (GlfwExtensions == nullptr || GlfwExtensionCount == 0u)
    {
        std::cerr << "[SwapchainExchange] glfwGetRequiredInstanceExtensions returned nothing - Vulkan surface extensions unavailable.\n";
        return false;
    }

    const VkResult InstanceResult = vkCreateInstance(&InstanceInfo, nullptr, &Vulkan->Instance);
    if (InstanceResult != VK_SUCCESS)
    {
        std::cerr << "[SwapchainExchange] vkCreateInstance failed (VkResult " << static_cast<int>(InstanceResult) << ").\n";
        return false;
    }

    if (Configuration.ValidationEnabled)
    {
        auto CreateMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(Vulkan->Instance, "vkCreateDebugUtilsMessengerEXT"));

        if (CreateMessenger)
        {
            VkDebugUtilsMessengerCreateInfoEXT MessengerInfo{};
            MessengerInfo.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            MessengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                                          | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            MessengerInfo.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                                          | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            MessengerInfo.pfnUserCallback = ValidationCallback;
            CreateMessenger(Vulkan->Instance, &MessengerInfo, nullptr, &Vulkan->DebugMessenger);
        }
    }

    return true;
}

bool SwapchainExchange::BringSurface() noexcept
{
    if (glfwCreateWindowSurface(Vulkan->Instance, GlfwWindow, nullptr, &Vulkan->Surface) != VK_SUCCESS)
    {
        std::cerr << "[SwapchainExchange] glfwCreateWindowSurface failed.\n";
        return false;
    }
    return true;
}

bool SwapchainExchange::BringPhysicalDevice() noexcept
{
    uint32_t DeviceCount = 0u;
    vkEnumeratePhysicalDevices(Vulkan->Instance, &DeviceCount, nullptr);
    if (DeviceCount == 0u)
    {
        std::cerr << "[SwapchainExchange] No Vulkan physical devices found.\n";
        return false;
    }

    std::vector<VkPhysicalDevice> Devices(DeviceCount);
    vkEnumeratePhysicalDevices(Vulkan->Instance, &DeviceCount, Devices.data());

    // Pick the first device that owns a queue family able to do graphics + compute + present on our surface.
    //    Discrete GPUs are preferred over integrated ones when both qualify.
    VkPhysicalDevice ChosenDevice = VK_NULL_HANDLE;
    uint32_t         ChosenFamily = 0u;
    bool             ChosenIsDiscrete = false;
    std::string      ChosenName;

    for (const auto& Candidate : Devices)
    {
        VkPhysicalDeviceProperties Properties{};
        vkGetPhysicalDeviceProperties(Candidate, &Properties);

        uint32_t FamilyCount = 0u;
        vkGetPhysicalDeviceQueueFamilyProperties(Candidate, &FamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> Families(FamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(Candidate, &FamilyCount, Families.data());

        for (uint32_t Index = 0u; Index < FamilyCount; ++Index)
        {
            constexpr VkQueueFlags Wanted = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            if ((Families[Index].queueFlags & Wanted) != Wanted) continue;

            VkBool32 PresentCapable = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(Candidate, Index, Vulkan->Surface, &PresentCapable);
            if (!PresentCapable) continue;

            const bool IsDiscrete = Properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
            if (ChosenDevice == VK_NULL_HANDLE || (IsDiscrete && !ChosenIsDiscrete))
            {
                ChosenDevice     = Candidate;
                ChosenFamily     = Index;
                ChosenIsDiscrete = IsDiscrete;
                ChosenName       = Properties.deviceName;
            }
            break;
        }
    }

    if (ChosenDevice == VK_NULL_HANDLE)
    {
        std::cerr << "[SwapchainExchange] No Vulkan device exposes a graphics+compute queue family that can present to the window.\n";
        return false;
    }

    Vulkan->PhysicalDevice = ChosenDevice;
    // One family drives everything: the command pool, the submit queue and the present queue must agree,
    //    otherwise command buffers recorded from a compute-only pool would be submitted to a graphics queue.
    Vulkan->GraphicsFamily = ChosenFamily;
    Vulkan->ComputeFamily  = ChosenFamily;

    vkGetPhysicalDeviceMemoryProperties(Vulkan->PhysicalDevice, &Vulkan->MemoryProperties);

    std::cerr << "[SwapchainExchange] Using GPU: " << ChosenName << " (queue family " << ChosenFamily << ")\n";

    // Ray-tracing tier (extension-first probe; see RayTracingCapabilitySet.h for why the feature struct alone is not trusted).
    Capabilities = RayTracingCapabilitySet::Probe(Vulkan->PhysicalDevice);
    const RayTracingTierCategory Supported = Capabilities.QuerySupportedTier();
    const RayTracingTierCategory Resolved  = Capabilities.ResolveTier(RayTracingRequest);
    std::cerr << "[SwapchainExchange] Ray tracing: supported = " << RayTracingCapabilitySet::TierName(Supported)
              << ", requested = " << RayTracingCapabilitySet::RequestName(RayTracingRequest)
              << ", using = " << RayTracingCapabilitySet::TierName(Resolved)
              << "  [AS ext " << Capabilities.AccelerationStructureExtension << " feat " << Capabilities.AccelerationStructureFeature
              << " | RQ ext " << Capabilities.RayQueryExtension << " feat " << Capabilities.RayQueryFeature
              << " | RP ext " << Capabilities.RayTracingPipelineExtension
              << " | BDA " << Capabilities.BufferDeviceAddress << " | bindless " << Capabilities.DescriptorIndexing
              << " | subgroup " << Capabilities.SubgroupSize << "]  driver: " << Capabilities.DriverInfo << "\n";
    if (RayTracingRequest != RayTracingRequestCategory::Auto
        && static_cast<uint32_t>(Resolved) < static_cast<uint32_t>(RayTracingRequest) - 1u)
        std::cerr << "[SwapchainExchange] Requested ray-tracing tier is not available on this device; downgraded to "
                  << RayTracingCapabilitySet::TierName(Resolved) << ".\n";
    return true;
}

bool SwapchainExchange::BringLogicalDevice() noexcept
{
    float Priority = 1.0f;

    std::vector<VkDeviceQueueCreateInfo> QueueInfoList;
    auto AddQueueFamily = [&](uint32_t Family)
    {
        for (const auto& Existing : QueueInfoList)
            if (Existing.queueFamilyIndex == Family) return;

        VkDeviceQueueCreateInfo QueueInfo{};
        QueueInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        QueueInfo.queueFamilyIndex = Family;
        QueueInfo.queueCount       = 1u;
        QueueInfo.pQueuePriorities = &Priority;
        QueueInfoList.push_back(QueueInfo);
    };

    AddQueueFamily(Vulkan->GraphicsFamily);
    AddQueueFamily(Vulkan->ComputeFamily);

    const char* DeviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkPhysicalDeviceFeatures DeviceFeatures{};
    DeviceFeatures.shaderStorageImageWriteWithoutFormat = VK_TRUE;

    // R2: vkCmdDrawIndexedIndirectCount is a Vulkan 1.2 core feature (drawIndirectCount) — requested only when offered;
    //    without it the raster issues a fixed-count indirect draw over zero-sized commands (same image, more CP work).
    VkPhysicalDeviceVulkan12Features Supported12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceFeatures2        Supported2 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &Supported12 };
    vkGetPhysicalDeviceFeatures2(Vulkan->PhysicalDevice, &Supported2);
    VkPhysicalDeviceVulkan12Features Enabled12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    Enabled12.drawIndirectCount = Supported12.drawIndirectCount;
    DrawIndirectCountSupported  = Supported12.drawIndirectCount == VK_TRUE;
    // R4a: bindless texture table — Vulkan 1.2 descriptor indexing (core on every Vulkan 1.2 driver incl. Pascal).
    //    Requested only when offered; without it the material table uploads but textures stay off (logged once).
    Vulkan->DescriptorIndexing = Supported12.runtimeDescriptorArray && Supported12.descriptorBindingPartiallyBound
                              && Supported12.shaderSampledImageArrayNonUniformIndexing && Supported12.descriptorBindingVariableDescriptorCount;
    Enabled12.runtimeDescriptorArray                    = Supported12.runtimeDescriptorArray;
    Enabled12.descriptorBindingPartiallyBound           = Supported12.descriptorBindingPartiallyBound;
    Enabled12.shaderSampledImageArrayNonUniformIndexing = Supported12.shaderSampledImageArrayNonUniformIndexing;
    Enabled12.descriptorBindingVariableDescriptorCount  = Supported12.descriptorBindingVariableDescriptorCount;
    Enabled12.descriptorBindingSampledImageUpdateAfterBind   = Supported12.descriptorBindingSampledImageUpdateAfterBind;
    Enabled12.descriptorBindingStorageBufferUpdateAfterBind  = Supported12.descriptorBindingStorageBufferUpdateAfterBind;
    Enabled12.descriptorBindingStorageImageUpdateAfterBind   = Supported12.descriptorBindingStorageImageUpdateAfterBind;
    Enabled12.descriptorBindingUniformBufferUpdateAfterBind  = Supported12.descriptorBindingUniformBufferUpdateAfterBind;
    Enabled12.descriptorBindingUpdateUnusedWhilePending     = Supported12.descriptorBindingUpdateUnusedWhilePending;
    if (!Vulkan->DescriptorIndexing) std::cerr << "[SwapchainExchange] descriptor indexing not offered - textures disabled (materials keep their constants).\n";
    DeviceFeatures.multiDrawIndirect = Supported2.features.multiDrawIndirect;
    DeviceFeatures.geometryShader    = Supported2.features.geometryShader;
    DeviceFeatures.shaderStorageImageWriteWithoutFormat = Supported2.features.shaderStorageImageWriteWithoutFormat;

    VkDeviceCreateInfo DeviceInfo{};
    DeviceInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    DeviceInfo.pNext                   = &Enabled12;
    DeviceInfo.queueCreateInfoCount    = static_cast<uint32_t>(QueueInfoList.size());
    DeviceInfo.pQueueCreateInfos       = QueueInfoList.data();
    DeviceInfo.enabledExtensionCount   = 1u;
    DeviceInfo.ppEnabledExtensionNames = DeviceExtensions;
    DeviceInfo.pEnabledFeatures        = &DeviceFeatures;

    const VkResult DeviceResult = vkCreateDevice(Vulkan->PhysicalDevice, &DeviceInfo, nullptr, &Vulkan->Device);
    if (DeviceResult != VK_SUCCESS)
    {
        std::cerr << "[SwapchainExchange] vkCreateDevice failed (VkResult " << static_cast<int>(DeviceResult) << ").\n";
        return false;
    }

    vkGetDeviceQueue(Vulkan->Device, Vulkan->GraphicsFamily, 0u, &Vulkan->GraphicsQueue);
    vkGetDeviceQueue(Vulkan->Device, Vulkan->ComputeFamily,  0u, &Vulkan->ComputeQueue);
    return true;
}

bool SwapchainExchange::BringSwapchain() noexcept
{
    VkSurfaceCapabilitiesKHR SurfaceCapabilities{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(Vulkan->PhysicalDevice, Vulkan->Surface, &SurfaceCapabilities);

    uint32_t FormatCount = 0u;
    vkGetPhysicalDeviceSurfaceFormatsKHR(Vulkan->PhysicalDevice, Vulkan->Surface, &FormatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> SurfaceFormats(FormatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(Vulkan->PhysicalDevice, Vulkan->Surface, &FormatCount, SurfaceFormats.data());

    if (SurfaceFormats.empty())
    {
        std::cerr << "[SwapchainExchange] Surface reports no supported formats.\n";
        return false;
    }

    VkSurfaceFormatKHR ChosenFormat = SurfaceFormats[0];
    for (const auto& Candidate : SurfaceFormats)
    {
        if (Candidate.format     == VK_FORMAT_B8G8R8A8_UNORM &&
            Candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            ChosenFormat = Candidate;
            break;
        }
    }

    Vulkan->SwapchainFormat = ChosenFormat.format;

    if (SurfaceCapabilities.currentExtent.width != UINT32_MAX)
    {
        Vulkan->SwapchainExtent = SurfaceCapabilities.currentExtent;
    }
    else
    {
        int FramebufferW = 0, FramebufferH = 0;
        glfwGetFramebufferSize(GlfwWindow, &FramebufferW, &FramebufferH);
        Vulkan->SwapchainExtent.width  = std::clamp(
            static_cast<uint32_t>(FramebufferW),
            SurfaceCapabilities.minImageExtent.width,
            SurfaceCapabilities.maxImageExtent.width);
        Vulkan->SwapchainExtent.height = std::clamp(
            static_cast<uint32_t>(FramebufferH),
            SurfaceCapabilities.minImageExtent.height,
            SurfaceCapabilities.maxImageExtent.height);
    }

    Configuration.Width  = Vulkan->SwapchainExtent.width;
    Configuration.Height = Vulkan->SwapchainExtent.height;

    uint32_t ImageCount = SurfaceCapabilities.minImageCount + 1u;
    if (SurfaceCapabilities.maxImageCount > 0u)
        ImageCount = std::min(ImageCount, SurfaceCapabilities.maxImageCount);

    VkSwapchainCreateInfoKHR SwapchainInfo{};
    SwapchainInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    SwapchainInfo.surface          = Vulkan->Surface;
    SwapchainInfo.minImageCount    = ImageCount;
    SwapchainInfo.imageFormat      = ChosenFormat.format;
    SwapchainInfo.imageColorSpace  = ChosenFormat.colorSpace;
    SwapchainInfo.imageExtent      = Vulkan->SwapchainExtent;
    SwapchainInfo.imageArrayLayers = 1u;
    // The compute pass writes the private storage image; swapchain images only receive the blit
    //    and the ImGui colour pass, so STORAGE usage (not universally supported) is not requested.
    SwapchainInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                                   | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    if ((SurfaceCapabilities.supportedUsageFlags & SwapchainInfo.imageUsage) != SwapchainInfo.imageUsage)
    {
        std::cerr << "[SwapchainExchange] Surface does not support COLOR_ATTACHMENT | TRANSFER_DST swapchain usage.\n";
        return false;
    }

    if (Vulkan->SwapchainExtent.width == 0u || Vulkan->SwapchainExtent.height == 0u)
    {
        std::cerr << "[SwapchainExchange] Swapchain extent is zero (window minimised?).\n";
        return false;
    }

    uint32_t SharedFamilies[] = { Vulkan->GraphicsFamily, Vulkan->ComputeFamily };
    if (Vulkan->GraphicsFamily != Vulkan->ComputeFamily)
    {
        SwapchainInfo.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        SwapchainInfo.queueFamilyIndexCount = 2u;
        SwapchainInfo.pQueueFamilyIndices   = SharedFamilies;
    }
    else
    {
        SwapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    SwapchainInfo.preTransform   = SurfaceCapabilities.currentTransform;
    SwapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ResolvedPresentMode          = ResolvePresentMode();
    SwapchainInfo.presentMode    = static_cast<VkPresentModeKHR>(ResolvedPresentMode);
    SwapchainInfo.clipped        = VK_TRUE;

    const VkResult SwapchainResult = vkCreateSwapchainKHR(Vulkan->Device, &SwapchainInfo, nullptr, &Vulkan->Swapchain);
    if (SwapchainResult != VK_SUCCESS)
    {
        std::cerr << "[SwapchainExchange] vkCreateSwapchainKHR failed (VkResult " << static_cast<int>(SwapchainResult) << ").\n";
        return false;
    }

    uint32_t ActualImageCount = 0u;
    (void)vkGetSwapchainImagesKHR(Vulkan->Device, Vulkan->Swapchain, &ActualImageCount, nullptr);
    Vulkan->SwapchainImages.resize(ActualImageCount);
    (void)vkGetSwapchainImagesKHR(Vulkan->Device, Vulkan->Swapchain, &ActualImageCount, Vulkan->SwapchainImages.data());

    Vulkan->SwapchainImageViews.resize(ActualImageCount);
    for (uint32_t Index = 0u; Index < ActualImageCount; ++Index)
    {
        VkImageViewCreateInfo ImageViewInfo{};
        ImageViewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ImageViewInfo.image                           = Vulkan->SwapchainImages[Index];
        ImageViewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        ImageViewInfo.format                          = Vulkan->SwapchainFormat;
        ImageViewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        ImageViewInfo.subresourceRange.baseMipLevel   = 0u;
        ImageViewInfo.subresourceRange.levelCount     = 1u;
        ImageViewInfo.subresourceRange.baseArrayLayer = 0u;
        ImageViewInfo.subresourceRange.layerCount     = 1u;
        (void)vkCreateImageView(Vulkan->Device, &ImageViewInfo, nullptr, &Vulkan->SwapchainImageViews[Index]);
    }

    Vulkan->ImageOrdinalFences.assign(ActualImageCount, VK_NULL_HANDLE);
    for (VkSemaphore S : Vulkan->ReleaseSemaphores)
        if (S) vkDestroySemaphore(Vulkan->Device, S, nullptr);
    Vulkan->ReleaseSemaphores.assign(ActualImageCount, VK_NULL_HANDLE);
    for (uint32_t Index = 0u; Index < ActualImageCount; ++Index)
    {
        VkSemaphoreCreateInfo SemaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        (void)vkCreateSemaphore(Vulkan->Device, &SemaphoreInfo, nullptr, &Vulkan->ReleaseSemaphores[Index]);
    }
    return true;
}

static bool CreateStorageImage(VkDevice Device, const VkPhysicalDeviceMemoryProperties& MemoryProperties,
                               VkFormat Format, VkExtent2D Extent, VkImageUsageFlags Usage,
                               VkImage& OutImage, VkDeviceMemory& OutMemory, VkImageView& OutView, const char* Label) noexcept
{
    VkImageCreateInfo ImageInfo{};
    ImageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ImageInfo.imageType     = VK_IMAGE_TYPE_2D;
    ImageInfo.format        = Format;
    ImageInfo.extent        = { Extent.width, Extent.height, 1u };
    ImageInfo.mipLevels     = 1u;
    ImageInfo.arrayLayers   = 1u;
    ImageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    ImageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ImageInfo.usage         = Usage;
    ImageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(Device, &ImageInfo, nullptr, &OutImage) != VK_SUCCESS)
    {
        std::cerr << "[SwapchainExchange] vkCreateImage (" << Label << ") failed.\n";
        return false;
    }

    VkMemoryRequirements Requirements{};
    vkGetImageMemoryRequirements(Device, OutImage, &Requirements);

    VkMemoryAllocateInfo AllocateInfo{};
    AllocateInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    AllocateInfo.allocationSize  = Requirements.size;
    AllocateInfo.memoryTypeIndex = 0u;
    for (uint32_t Index = 0u; Index < MemoryProperties.memoryTypeCount; ++Index)
    {
        if ((Requirements.memoryTypeBits & (1u << Index)) &&
            (MemoryProperties.memoryTypes[Index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        {
            AllocateInfo.memoryTypeIndex = Index;
            break;
        }
    }
    if (vkAllocateMemory(Device, &AllocateInfo, nullptr, &OutMemory) != VK_SUCCESS)
    {
        std::cerr << "[SwapchainExchange] vkAllocateMemory (" << Label << ") failed.\n";
        return false;
    }
    vkBindImageMemory(Device, OutImage, OutMemory, 0);

    VkImageViewCreateInfo ViewInfo{};
    ViewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ViewInfo.image                           = OutImage;
    ViewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    ViewInfo.format                          = Format;
    ViewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    ViewInfo.subresourceRange.levelCount     = 1u;
    ViewInfo.subresourceRange.layerCount     = 1u;
    if (vkCreateImageView(Device, &ViewInfo, nullptr, &OutView) != VK_SUCCESS)
    {
        std::cerr << "[SwapchainExchange] vkCreateImageView (" << Label << ") failed.\n";
        return false;
    }
    return true;
}

bool SwapchainExchange::BringStorageImage() noexcept
{
    const VkExtent2D Extent{ Configuration.Width, Configuration.Height };

    // ① Presentation image — the compute pass writes tone-mapped 8-bit colour, blitted to the swapchain.
    // COLOR_ATTACHMENT_BIT is what lets the SpatialInterface overlay draw its figures straight onto the resolved
    //    scene image (it begins its own render pass against this view) before the blit to the swapchain.
    if (!CreateStorageImage(Vulkan->Device, Vulkan->MemoryProperties, VK_FORMAT_R8G8B8A8_UNORM, Extent,
                            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                            Vulkan->StorageImage, Vulkan->StorageMemory, Vulkan->StorageImageView, "storage image"))
        return false;

    // ② History image — linear HDR running mean, persists across frames (temporal accumulation).
    if (!CreateStorageImage(Vulkan->Device, Vulkan->MemoryProperties, VK_FORMAT_R32G32B32A32_SFLOAT, Extent,
                            VK_IMAGE_USAGE_STORAGE_BIT,
                            Vulkan->HistoryImage, Vulkan->HistoryMemory, Vulkan->HistoryImageView, "history image"))
        return false;

    // ③ R6 temporal reservoirs — two full-extent 64 B/px SSBOs (bindings 16/17), device-local, zeroed on first dispatch.
    {
        Vulkan->ReservoirBytes =
            static_cast<VkDeviceSize>(Extent.width) * static_cast<VkDeviceSize>(Extent.height) * sizeof(VulkanRecord::ReservoirBufferRecord);
        for (uint32_t I = 0u; I < 2u; ++I)
            AllocateBuffer(Vulkan->Device, Vulkan->MemoryProperties, Vulkan->ReservoirBytes,
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           Vulkan->ReservoirBuffers[I], Vulkan->ReservoirMemories[I]);
        Vulkan->ReservoirParity       = false;
        Vulkan->ReservoirsInitialised = false;
        std::cerr << "[SwapchainExchange] Reservoirs: 2 x " << (Vulkan->ReservoirBytes >> 20u) << " MB (64 B/px temporal DI state).\n";
    }

    Vulkan->HistoryInitialised = false;
    return true;
}

bool SwapchainExchange::BringCommandRecording() noexcept
{
    VkCommandPoolCreateInfo PoolInfo{};
    PoolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    PoolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    PoolInfo.queueFamilyIndex = Vulkan->ComputeFamily;

    if (vkCreateCommandPool(Vulkan->Device, &PoolInfo, nullptr, &Vulkan->ComputeCommandPool) != VK_SUCCESS)
    {
        std::cerr << "[SwapchainExchange] vkCreateCommandPool failed.\n";
        return false;
    }

    const uint32_t ImageCount = static_cast<uint32_t>(Vulkan->SwapchainImages.size());
    Vulkan->ComputeCommands.resize(ImageCount);

    VkCommandBufferAllocateInfo AllocateInfo{};
    AllocateInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    AllocateInfo.commandPool        = Vulkan->ComputeCommandPool;
    AllocateInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    AllocateInfo.commandBufferCount = ImageCount;
    (void)vkAllocateCommandBuffers(Vulkan->Device, &AllocateInfo, Vulkan->ComputeCommands.data());

    return true;
}

bool SwapchainExchange::BringComputePipeline() noexcept
{
    // ① Descriptor set layout — 0: output image, 1: triangle SSBO, 2: material SSBO, 3: history image,
    //    R2: 4: surface image, 5: normal image, 6: instance SSBO, 7: luminaire SSBO
    //    R3: 8: CWBVH node SSBO, 9: CWBVH triangle SSBO
    //    R4a: 10: material slab SSBO
    //    R4b: 11: vertex SSBO, 12: index SSBO, 13: GGX energy LUT, 14: LTC sheen LUT
    //    R6: 15: motion sampler, 16: prev-reservoir SSBO, 17: curr-reservoir SSBO, 18: sampler2D Textures[] (bindless, partially bound, variable count — must be last)
    std::array<VkDescriptorSetLayoutBinding, kComputeBindingCount> LayoutBindings{};
    LayoutBindings[0].binding         = 0u;
    LayoutBindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    LayoutBindings[0].descriptorCount = 1u;
    LayoutBindings[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    LayoutBindings[1].binding         = 1u;
    LayoutBindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    LayoutBindings[1].descriptorCount = 1u;
    LayoutBindings[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    LayoutBindings[2].binding         = 2u;
    LayoutBindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    LayoutBindings[2].descriptorCount = 1u;
    LayoutBindings[2].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    LayoutBindings[3].binding         = 3u;
    LayoutBindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    LayoutBindings[3].descriptorCount = 1u;
    LayoutBindings[3].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    for (uint32_t B = 4u; B < kComputeBindingCount - 1u; ++B)
    {
        LayoutBindings[B].binding         = B;
        LayoutBindings[B].descriptorType  = B < 6u ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : (B == 13u || B == 14u || B == 15u) ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        LayoutBindings[B].descriptorCount = 1u;
        LayoutBindings[B].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    const uint32_t TextureBinding = kComputeBindingCount - 1u;   // 15
    LayoutBindings[TextureBinding].binding         = TextureBinding;
    LayoutBindings[TextureBinding].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    LayoutBindings[TextureBinding].descriptorCount = Vulkan->DescriptorIndexing ? kTextureSlotCapacity : 1u;
    LayoutBindings[TextureBinding].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    std::array<VkDescriptorBindingFlags, kComputeBindingCount> BindingFlags{};
    if (Vulkan->DescriptorIndexing)
    {
        BindingFlags[TextureBinding] = static_cast<VkDescriptorBindingFlags>(VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT);
        BindingFlags[16u]            = static_cast<VkDescriptorBindingFlags>(VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT);
        BindingFlags[17u]            = static_cast<VkDescriptorBindingFlags>(VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT);
    }
    VkDescriptorSetLayoutBindingFlagsCreateInfo FlagsInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
    FlagsInfo.bindingCount  = kComputeBindingCount;
    FlagsInfo.pBindingFlags = BindingFlags.data();

    VkDescriptorSetLayoutCreateInfo LayoutInfo{};
    LayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    LayoutInfo.pNext        = &FlagsInfo;
    LayoutInfo.flags        = Vulkan->DescriptorIndexing ? static_cast<VkDescriptorSetLayoutCreateFlags>(VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT) : 0u;
    LayoutInfo.bindingCount = kComputeBindingCount;
    LayoutInfo.pBindings    = LayoutBindings.data();
    (void)vkCreateDescriptorSetLayout(Vulkan->Device, &LayoutInfo, nullptr, &Vulkan->ComputeDescriptorLayout);

    // ② Push constant range — matches DispatchConfiguration exactly (96 bytes, static_assert in ReSTIRIntegrator.h)
    VkPushConstantRange PushRange{};
    PushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    PushRange.offset     = 0u;
    PushRange.size       = static_cast<uint32_t>(sizeof(DispatchConfiguration));

    VkPipelineLayoutCreateInfo PipelineLayoutInfo{};
    PipelineLayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    PipelineLayoutInfo.setLayoutCount         = 1u;
    PipelineLayoutInfo.pSetLayouts            = &Vulkan->ComputeDescriptorLayout;
    PipelineLayoutInfo.pushConstantRangeCount = 1u;
    PipelineLayoutInfo.pPushConstantRanges    = &PushRange;
    (void)vkCreatePipelineLayout(Vulkan->Device, &PipelineLayoutInfo, nullptr, &Vulkan->ComputePipelineLayout);

    // ③ Load SPIR-V — expected at Shaders/ReSTIRViewport.spv relative to working directory
    const std::vector<uint32_t> Spirv = LoadSpirv("Engine/Shaders/ReSTIRViewport.spv");
    if (Spirv.empty()) return false;

    VkShaderModuleCreateInfo ShaderModuleInfo{};
    ShaderModuleInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ShaderModuleInfo.codeSize = Spirv.size() * 4u;
    ShaderModuleInfo.pCode    = Spirv.data();
    VkShaderModule ShaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(Vulkan->Device, &ShaderModuleInfo, nullptr, &ShaderModule) != VK_SUCCESS)
    {
        std::cerr << "[SwapchainExchange] vkCreateShaderModule failed - the SPIR-V blob is invalid.\n";
        return false;
    }

    VkComputePipelineCreateInfo ComputeInfo{};
    ComputeInfo.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ComputeInfo.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ComputeInfo.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    ComputeInfo.stage.module = ShaderModule;
    ComputeInfo.stage.pName  = "main";
    ComputeInfo.layout       = Vulkan->ComputePipelineLayout;

    const VkResult PipelineResult = vkCreateComputePipelines(
        Vulkan->Device, VK_NULL_HANDLE, 1u, &ComputeInfo, nullptr, &Vulkan->ComputePipeline);
    vkDestroyShaderModule(Vulkan->Device, ShaderModule, nullptr);

    if (PipelineResult != VK_SUCCESS)
    {
        std::cerr << "[SwapchainExchange] vkCreateComputePipelines failed (VkResult " << static_cast<int>(PipelineResult) << ").\n";
        return false;
    }
    return true;
}

bool SwapchainExchange::BringDescriptorSet() noexcept
{
    std::array<VkDescriptorPoolSize, 3u> PoolSizes{};
    PoolSizes[0].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    PoolSizes[0].descriptorCount = 4u;
    PoolSizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSizes[1].descriptorCount = kComputeBindingCount - 8u;   // 11 storage buffers (1, 2, 6-12, 16-17)
    PoolSizes[2].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    PoolSizes[2].descriptorCount = 3u + (Vulkan->DescriptorIndexing ? kTextureSlotCapacity : 1u);   // R6: two LUTs + motion + the bindless table

    VkDescriptorPoolCreateInfo PoolInfo{};
    PoolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    PoolInfo.flags         = Vulkan->DescriptorIndexing ? static_cast<VkDescriptorPoolCreateFlags>(VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT) : 0u;
    PoolInfo.maxSets       = 1u;
    PoolInfo.poolSizeCount = 3u;
    PoolInfo.pPoolSizes    = PoolSizes.data();
    (void)vkCreateDescriptorPool(Vulkan->Device, &PoolInfo, nullptr, &Vulkan->ComputeDescriptorPool);

    const uint32_t VariableCount = Vulkan->DescriptorIndexing ? kTextureSlotCapacity : 1u;
    VkDescriptorSetVariableDescriptorCountAllocateInfo VariableInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO };
    VariableInfo.descriptorSetCount = 1u;
    VariableInfo.pDescriptorCounts  = &VariableCount;
    VkDescriptorSetAllocateInfo AllocateInfo{};
    AllocateInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    AllocateInfo.pNext              = &VariableInfo;
    AllocateInfo.descriptorPool     = Vulkan->ComputeDescriptorPool;
    AllocateInfo.descriptorSetCount = 1u;
    AllocateInfo.pSetLayouts        = &Vulkan->ComputeDescriptorLayout;
    if (vkAllocateDescriptorSets(Vulkan->Device, &AllocateInfo, &Vulkan->ComputeDescriptorSet) != VK_SUCCESS)
    {
        std::cerr << "[SwapchainExchange] vkAllocateDescriptorSets failed.\n";
        return false;
    }

    // The scene SSBOs do not exist yet (UploadTriangles / UploadRadiance run after Bring()).
    //    WriteDescriptorSet() only writes the bindings whose resources exist - writing a VK_NULL_HANDLE
    //    buffer into a descriptor is invalid and crashes most drivers when validation is off.
    WriteDescriptorSet();
    return true;
}

void SwapchainExchange::WriteDescriptorSet() noexcept
{
    if (!Vulkan->ComputeDescriptorSet) return;

    VkDescriptorImageInfo ImageInfo{};
    ImageInfo.imageView   = Vulkan->StorageImageView;
    ImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorBufferInfo TriangleBufferInfo{};
    TriangleBufferInfo.buffer = Vulkan->TriangleBuffer;
    TriangleBufferInfo.offset = 0u;
    TriangleBufferInfo.range  = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo MaterialBufferInfo{};
    MaterialBufferInfo.buffer = Vulkan->MaterialBuffer;
    MaterialBufferInfo.offset = 0u;
    MaterialBufferInfo.range  = VK_WHOLE_SIZE;

    VkDescriptorImageInfo HistoryInfo{};
    HistoryInfo.imageView   = Vulkan->HistoryImageView;
    HistoryInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo SurfaceInfo{ VK_NULL_HANDLE, static_cast<VkImageView>(Visibility.QuerySurfaceView()), VK_IMAGE_LAYOUT_GENERAL };
    VkDescriptorImageInfo NormalInfo { VK_NULL_HANDLE, static_cast<VkImageView>(Visibility.QueryNormalView()),  VK_IMAGE_LAYOUT_GENERAL };
    VkDescriptorBufferInfo InstanceInfo { static_cast<VkBuffer>(Visibility.QueryInstanceBuffer()),  0u, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo LuminaireInfo{ static_cast<VkBuffer>(Visibility.QueryLuminaireBuffer()), 0u, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo NodeInfo     { Vulkan->TraversalNodeBuffer, 0u, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo LeafInfo     { Vulkan->TraversalLeafBuffer, 0u, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo SlabInfo     { Vulkan->SlabBuffer, 0u, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo VertexInfo   { static_cast<VkBuffer>(Visibility.QueryVertexBuffer()), 0u, VK_WHOLE_SIZE };   // R4b
    VkDescriptorBufferInfo IndexInfo    { static_cast<VkBuffer>(Visibility.QueryIndexBuffer()),  0u, VK_WHOLE_SIZE };   // R4b
    VkDescriptorImageInfo  EnergyInfo   { Vulkan->TableSampler, Vulkan->ShadingTables[0].View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo  SheenInfo    { Vulkan->TableSampler, Vulkan->ShadingTables[1].View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorImageInfo  MotionInfo   { Vulkan->TableSampler, static_cast<VkImageView>(Visibility.QueryMotionView()), VK_IMAGE_LAYOUT_GENERAL };   // R6: texelFetch only; linear sampler harmless
    // R6: prev = the buffer last frame wrote, curr = the one this frame writes (parity flips per presented frame).
    const uint32_t PrevSlot = Vulkan->ReservoirParity ? 1u : 0u;
    VkDescriptorBufferInfo PrevReservoirInfo{ Vulkan->ReservoirBuffers[PrevSlot],      0u, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo CurrReservoirInfo{ Vulkan->ReservoirBuffers[PrevSlot ^ 1u], 0u, VK_WHOLE_SIZE };

    std::array<VkWriteDescriptorSet, kComputeBindingCount> Writes{};
    uint32_t WriteCount = 0u;

    if (Vulkan->StorageImageView)
    {
        VkWriteDescriptorSet& Write = Writes[WriteCount++];
        Write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Write.dstSet          = Vulkan->ComputeDescriptorSet;
        Write.dstBinding      = 0u;
        Write.descriptorCount = 1u;
        Write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        Write.pImageInfo      = &ImageInfo;
    }

    if (Vulkan->TriangleBuffer)
    {
        VkWriteDescriptorSet& Write = Writes[WriteCount++];
        Write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Write.dstSet          = Vulkan->ComputeDescriptorSet;
        Write.dstBinding      = 1u;
        Write.descriptorCount = 1u;
        Write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Write.pBufferInfo     = &TriangleBufferInfo;
    }

    if (Vulkan->MaterialBuffer)
    {
        VkWriteDescriptorSet& Write = Writes[WriteCount++];
        Write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Write.dstSet          = Vulkan->ComputeDescriptorSet;
        Write.dstBinding      = 2u;
        Write.descriptorCount = 1u;
        Write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Write.pBufferInfo     = &MaterialBufferInfo;
    }

    if (Vulkan->HistoryImageView)
    {
        VkWriteDescriptorSet& Write = Writes[WriteCount++];
        Write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Write.dstSet          = Vulkan->ComputeDescriptorSet;
        Write.dstBinding      = 3u;
        Write.descriptorCount = 1u;
        Write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        Write.pImageInfo      = &HistoryInfo;
    }

    // R2 bindings — written once the visibility targets / scene exist.
    const auto WriteImage = [&](uint32_t Binding, const VkDescriptorImageInfo& Info)
    {
        if (!Info.imageView) return;
        VkWriteDescriptorSet& Write = Writes[WriteCount++];
        Write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; Write.dstSet = Vulkan->ComputeDescriptorSet; Write.dstBinding = Binding;
        Write.descriptorCount = 1u; Write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; Write.pImageInfo = &Info;
    };
    const auto WriteBuffer = [&](uint32_t Binding, const VkDescriptorBufferInfo& Info)
    {
        if (!Info.buffer) return;
        VkWriteDescriptorSet& Write = Writes[WriteCount++];
        Write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; Write.dstSet = Vulkan->ComputeDescriptorSet; Write.dstBinding = Binding;
        Write.descriptorCount = 1u; Write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; Write.pBufferInfo = &Info;
    };
    WriteImage (4u, SurfaceInfo);
    WriteImage (5u, NormalInfo);
    WriteBuffer(6u, InstanceInfo);
    WriteBuffer(7u, LuminaireInfo);
    WriteBuffer(8u, NodeInfo);
    WriteBuffer(9u, LeafInfo);
    WriteBuffer(10u, SlabInfo);
    WriteBuffer(11u, VertexInfo);
    WriteBuffer(12u, IndexInfo);
    const auto WriteSampled = [&](uint32_t Binding, const VkDescriptorImageInfo& Info)
    {
        if (!Info.imageView || !Info.sampler) return;
        VkWriteDescriptorSet& Write = Writes[WriteCount++];
        Write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; Write.dstSet = Vulkan->ComputeDescriptorSet; Write.dstBinding = Binding;
        Write.descriptorCount = 1u; Write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; Write.pImageInfo = &Info;
    };
    WriteSampled(13u, EnergyInfo);
    WriteSampled(14u, SheenInfo);
    WriteSampled(15u, MotionInfo);          // R6: skipped until the motion target + table sampler exist
    WriteBuffer(16u, PrevReservoirInfo);    // R6: skipped until the reservoir SSBOs exist
    WriteBuffer(17u, CurrReservoirInfo);

    // R4a: the texture table. Written in one go (partially bound: slots past the resident count stay undefined and are
    //    never indexed — the material records only reference resident slots).
    std::vector<VkDescriptorImageInfo> TextureInfos;
    VkWriteDescriptorSet TextureWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    if (Vulkan->DescriptorIndexing && Vulkan->TextureSampler && !Vulkan->Textures.empty())
    {
        TextureInfos.reserve(Vulkan->Textures.size());
        for (const VulkanRecord::ResidentTexture& T : Vulkan->Textures)
            TextureInfos.push_back(VkDescriptorImageInfo{ Vulkan->TextureSampler, T.View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
        TextureWrite.dstSet          = Vulkan->ComputeDescriptorSet;
        TextureWrite.dstBinding      = kComputeBindingCount - 1u;
        TextureWrite.dstArrayElement = 0u;
        TextureWrite.descriptorCount = static_cast<uint32_t>(TextureInfos.size());
        TextureWrite.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        TextureWrite.pImageInfo      = TextureInfos.data();
    }

    if (WriteCount > 0u)
        vkUpdateDescriptorSets(Vulkan->Device, WriteCount, Writes.data(), 0u, nullptr);
    if (TextureWrite.descriptorCount > 0u)
        vkUpdateDescriptorSets(Vulkan->Device, 1u, &TextureWrite, 0u, nullptr);
}

bool SwapchainExchange::BringCycleSlots() noexcept
{
    VkSemaphoreCreateInfo SemaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo     FenceInfo    { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    FenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t Slot = 0u; Slot < kCycleSlotCount; ++Slot)
    {
        (void)vkCreateSemaphore(Vulkan->Device, &SemaphoreInfo, nullptr, &Vulkan->AcquireSemaphores[Slot]);
        vkCreateFence    (Vulkan->Device, &FenceInfo,     nullptr, &Vulkan->CycleFences[Slot]);
    }
    return true;
}

bool SwapchainExchange::BringImGui() noexcept
{
    // ① ImGui descriptor pool — include SAMPLER and SAMPLED_IMAGE for ImGui font/texture uploads
    std::array<VkDescriptorPoolSize, 6u> ImGuiPoolSizes = {{
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16u },
        { VK_DESCRIPTOR_TYPE_SAMPLER,                16u },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          16u },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          16u },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         16u },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         16u }
    }};
    VkDescriptorPoolCreateInfo ImGuiPoolInfo{};
    ImGuiPoolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ImGuiPoolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    ImGuiPoolInfo.maxSets       = 64u;
    ImGuiPoolInfo.poolSizeCount = static_cast<uint32_t>(ImGuiPoolSizes.size());
    ImGuiPoolInfo.pPoolSizes    = ImGuiPoolSizes.data();
    (void)vkCreateDescriptorPool(Vulkan->Device, &ImGuiPoolInfo, nullptr, &Vulkan->ImGuiDescriptorPool);

    // ② Render pass — loads compute output, ImGui renders on top, transitions to PRESENT
    VkAttachmentDescription ColourAttachment{};
    ColourAttachment.format         = Vulkan->SwapchainFormat;
    ColourAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    ColourAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
    ColourAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    ColourAttachment.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ColourAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ColourReference{ 0u, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription  Subpass{};
    Subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    Subpass.colorAttachmentCount = 1u;
    Subpass.pColorAttachments    = &ColourReference;

    VkSubpassDependency Dependency{};
    Dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
    Dependency.dstSubpass    = 0u;
    Dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    Dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    Dependency.srcAccessMask = 0u;
    Dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo RenderPassInfo{};
    RenderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    RenderPassInfo.attachmentCount = 1u;
    RenderPassInfo.pAttachments    = &ColourAttachment;
    RenderPassInfo.subpassCount    = 1u;
    RenderPassInfo.pSubpasses      = &Subpass;
    RenderPassInfo.dependencyCount = 1u;
    RenderPassInfo.pDependencies   = &Dependency;
    (void)vkCreateRenderPass(Vulkan->Device, &RenderPassInfo, nullptr, &Vulkan->ImGuiRenderPass);

    // ③ Framebuffers
    const uint32_t ImageCount = static_cast<uint32_t>(Vulkan->SwapchainImages.size());
    Vulkan->ImGuiFramebuffers.resize(ImageCount);
    for (uint32_t Index = 0u; Index < ImageCount; ++Index)
    {
        VkFramebufferCreateInfo FramebufferInfo{};
        FramebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        FramebufferInfo.renderPass      = Vulkan->ImGuiRenderPass;
        FramebufferInfo.attachmentCount = 1u;
        FramebufferInfo.pAttachments    = &Vulkan->SwapchainImageViews[Index];
        FramebufferInfo.width           = Configuration.Width;
        FramebufferInfo.height          = Configuration.Height;
        FramebufferInfo.layers          = 1u;
        (void)vkCreateFramebuffer(Vulkan->Device, &FramebufferInfo, nullptr, &Vulkan->ImGuiFramebuffers[Index]);
    }

    // ④ ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
#ifdef IMGUI_HAS_DOCK
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;  // 💡 docking branch only
#endif // IMGUI_HAS_DOCK
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(GlfwWindow, true);

    ImGui_ImplVulkan_InitInfo ImGuiVulkanInfo{};
    ImGuiVulkanInfo.Instance       = Vulkan->Instance;
    ImGuiVulkanInfo.PhysicalDevice = Vulkan->PhysicalDevice;
    ImGuiVulkanInfo.Device         = Vulkan->Device;
    ImGuiVulkanInfo.QueueFamily    = Vulkan->GraphicsFamily;
    ImGuiVulkanInfo.Queue          = Vulkan->GraphicsQueue;
    ImGuiVulkanInfo.DescriptorPool = Vulkan->ImGuiDescriptorPool;
    ImGuiVulkanInfo.PipelineInfoMain.RenderPass   = Vulkan->ImGuiRenderPass;  // 💡 moved from InitInfo root in ImGui 1.93
    ImGuiVulkanInfo.PipelineInfoMain.MSAASamples  = VK_SAMPLE_COUNT_1_BIT;    // 💡 moved from InitInfo root in ImGui 1.93
    ImGuiVulkanInfo.MinImageCount  = 2u;
    ImGuiVulkanInfo.ImageCount     = ImageCount;
    ImGui_ImplVulkan_Init(&ImGuiVulkanInfo);

    // ⑤ Font upload — automatic since ImGui 1.80; ImGui_ImplVulkan_NewFrame() uploads on first call.
    // 💡 ImGui_ImplVulkan_CreateFontsTexture() was removed in ImGui 1.93 (2025-06-11).
    //    The backend now owns font atlas upload internally via ImGuiBackendFlags_RendererHasTextures.

    return true;
}

//============================================================================================================================================
//                                               SCENE UPLOAD
//============================================================================================================================================

void SwapchainExchange::UploadTriangles(const std::vector<TriangleIndex>& Triangles) noexcept
{
    if (!Vulkan->Device) return;

    if (Vulkan->TriangleBuffer || Vulkan->TriangleMemory) vkDeviceWaitIdle(Vulkan->Device);
    if (Vulkan->TriangleBuffer) vkDestroyBuffer(Vulkan->Device, Vulkan->TriangleBuffer, nullptr);
    if (Vulkan->TriangleMemory) vkFreeMemory   (Vulkan->Device, Vulkan->TriangleMemory, nullptr);
    Vulkan->TriangleBuffer = VK_NULL_HANDLE;
    Vulkan->TriangleMemory = VK_NULL_HANDLE;

    Vulkan->TriangleCount      = static_cast<uint32_t>(Triangles.size());
    // A zero-sized buffer is invalid; keep at least one record so the SSBO binding is always valid.
    const VkDeviceSize ByteCount = std::max<VkDeviceSize>(Triangles.size(), 1u) * sizeof(TriangleIndex);
    constexpr uint32_t HostVisible = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                   | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    AllocateBuffer(Vulkan->Device, Vulkan->MemoryProperties, ByteCount,
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, HostVisible,
                   Vulkan->TriangleBuffer, Vulkan->TriangleMemory);

    void* Mapped = nullptr;
    (void)vkMapMemory(Vulkan->Device, Vulkan->TriangleMemory, 0u, ByteCount, 0u, &Mapped);
    if (Mapped)
    {
        std::memset(Mapped, 0, static_cast<size_t>(ByteCount));
        if (!Triangles.empty())
            std::memcpy(Mapped, Triangles.data(), Triangles.size() * sizeof(TriangleIndex));
        vkUnmapMemory(Vulkan->Device, Vulkan->TriangleMemory);
    }

    WriteDescriptorSet();
}

void SwapchainExchange::UploadMaterials(const MaterialIndex& Materials) noexcept
{
    if (!Vulkan->Device) return;

    if (Vulkan->MaterialBuffer || Vulkan->SlabBuffer) vkDeviceWaitIdle(Vulkan->Device);
    if (Vulkan->MaterialBuffer) vkDestroyBuffer(Vulkan->Device, Vulkan->MaterialBuffer, nullptr);
    if (Vulkan->MaterialMemory) vkFreeMemory   (Vulkan->Device, Vulkan->MaterialMemory, nullptr);
    if (Vulkan->SlabBuffer)     vkDestroyBuffer(Vulkan->Device, Vulkan->SlabBuffer, nullptr);
    if (Vulkan->SlabMemory)     vkFreeMemory   (Vulkan->Device, Vulkan->SlabMemory, nullptr);
    Vulkan->MaterialBuffer = Vulkan->SlabBuffer = VK_NULL_HANDLE;
    Vulkan->MaterialMemory = Vulkan->SlabMemory = VK_NULL_HANDLE;

    constexpr uint32_t HostVisible = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    const auto Upload = [&](const void* Source, size_t Bytes, VkBuffer& Buffer, VkDeviceMemory& Memory)
    {
        const VkDeviceSize ByteCount = std::max<VkDeviceSize>(Bytes, 16u);   // a zero-sized buffer is invalid
        AllocateBuffer(Vulkan->Device, Vulkan->MemoryProperties, ByteCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, HostVisible, Buffer, Memory);
        void* Mapped = nullptr;
        (void)vkMapMemory(Vulkan->Device, Memory, 0u, ByteCount, 0u, &Mapped);
        if (Mapped)
        {
            std::memset(Mapped, 0, static_cast<size_t>(ByteCount));
            if (Bytes) std::memcpy(Mapped, Source, Bytes);
            vkUnmapMemory(Vulkan->Device, Memory);
        }
    };
    Vulkan->MaterialCount = static_cast<uint32_t>(Materials.QueryRecords().size());
    Upload(Materials.QueryRecords().data(),     Materials.QueryRecords().size()     * sizeof(MaterialRecord),     Vulkan->MaterialBuffer, Vulkan->MaterialMemory);
    Upload(Materials.QuerySlabRecords().data(), Materials.QuerySlabRecords().size() * sizeof(MaterialSlabRecord), Vulkan->SlabBuffer,     Vulkan->SlabMemory);

    WriteDescriptorSet();
}

//------------------------------------------------------------------------------------------------------------------------
//                                          R4a: BINDLESS TEXTURE UPLOAD
//------------------------------------------------------------------------------------------------------------------------
// One VK_IMAGE_TILING_OPTIMAL image per texture, every mip level copied from one host-visible staging buffer through a
//    single one-time compute-queue submission (the compute family owns transfer capability by Vulkan guarantee).

void SwapchainExchange::DestroyTextures() noexcept
{
    if (!Vulkan->Device) return;
    for (VulkanRecord::ResidentTexture& T : Vulkan->Textures)
    {
        if (T.View)   vkDestroyImageView(Vulkan->Device, T.View, nullptr);
        if (T.Image)  vkDestroyImage    (Vulkan->Device, T.Image, nullptr);
        if (T.Memory) vkFreeMemory      (Vulkan->Device, T.Memory, nullptr);
    }
    Vulkan->Textures.clear();
}

void SwapchainExchange::UploadTextures(const TextureIndex& Textures) noexcept
{
    if (!Vulkan->Device || !Vulkan->DescriptorIndexing) return;
    vkDeviceWaitIdle(Vulkan->Device);
    DestroyTextures();

    if (!Vulkan->TextureSampler)
    {
        VkSamplerCreateInfo SamplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        SamplerInfo.magFilter = SamplerInfo.minFilter = VK_FILTER_LINEAR;
        SamplerInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        SamplerInfo.addressModeU = SamplerInfo.addressModeV = SamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        SamplerInfo.maxLod       = VK_LOD_CLAMP_NONE;
        SamplerInfo.anisotropyEnable = VK_FALSE;   // ⚠️ anisotropic filtering waits for R4b (feature request + config key)
        (void)vkCreateSampler(Vulkan->Device, &SamplerInfo, nullptr, &Vulkan->TextureSampler);
    }

    const std::vector<TextureDescriptor>& Source = Textures.QueryTextures();
    const uint32_t Count = static_cast<uint32_t>(std::min<size_t>(Source.size(), kTextureSlotCapacity));
    if (Count == 0u) { WriteDescriptorSet(); return; }
    if (Source.size() > kTextureSlotCapacity)
        std::cerr << "[SwapchainExchange] " << Source.size() << " textures exceed the " << kTextureSlotCapacity << "-slot table - the rest are not resident.\n";

    // ① Staging buffer with every texture's full mip chain back to back.
    VkDeviceSize StagingBytes = 0u;
    for (uint32_t I = 0u; I < Count; ++I) StagingBytes += Source[I].Texels.size();
    VkBuffer Staging = VK_NULL_HANDLE; VkDeviceMemory StagingMemory = VK_NULL_HANDLE;
    AllocateBuffer(Vulkan->Device, Vulkan->MemoryProperties, std::max<VkDeviceSize>(StagingBytes, 16u), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, Staging, StagingMemory);
    std::vector<VkDeviceSize> TextureOffset(Count);
    {
        void* Mapped = nullptr;
        (void)vkMapMemory(Vulkan->Device, StagingMemory, 0u, VK_WHOLE_SIZE, 0u, &Mapped);
        VkDeviceSize Cursor = 0u;
        for (uint32_t I = 0u; I < Count; ++I)
        {
            TextureOffset[I] = Cursor;
            if (Mapped && !Source[I].Texels.empty()) std::memcpy(static_cast<uint8_t*>(Mapped) + Cursor, Source[I].Texels.data(), Source[I].Texels.size());
            Cursor += Source[I].Texels.size();
        }
        if (Mapped) vkUnmapMemory(Vulkan->Device, StagingMemory);
    }

    // ② Images + views.
    Vulkan->Textures.resize(Count);
    for (uint32_t I = 0u; I < Count; ++I)
    {
        const TextureDescriptor& T = Source[I];
        VulkanRecord::ResidentTexture& R = Vulkan->Textures[I];
        const VkFormat Format = T.Encoding == TextureEncoding::Srgb8 ? VK_FORMAT_R8G8B8A8_SRGB : T.Encoding == TextureEncoding::Linear8 ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R16G16B16A16_SFLOAT;
        VkImageCreateInfo ImageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        ImageInfo.imageType = VK_IMAGE_TYPE_2D; ImageInfo.format = Format;
        ImageInfo.extent = { T.Width, T.Height, 1u }; ImageInfo.mipLevels = T.LevelCount; ImageInfo.arrayLayers = 1u;
        ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT; ImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        ImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(Vulkan->Device, &ImageInfo, nullptr, &R.Image) != VK_SUCCESS) { R.Image = VK_NULL_HANDLE; continue; }
        VkMemoryRequirements Requirements{};
        vkGetImageMemoryRequirements(Vulkan->Device, R.Image, &Requirements);
        VkMemoryAllocateInfo Allocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        Allocate.allocationSize = Requirements.size;
        for (uint32_t M = 0u; M < Vulkan->MemoryProperties.memoryTypeCount; ++M)
            if ((Requirements.memoryTypeBits & (1u << M)) && (Vulkan->MemoryProperties.memoryTypes[M].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) { Allocate.memoryTypeIndex = M; break; }
        (void)vkAllocateMemory(Vulkan->Device, &Allocate, nullptr, &R.Memory);
        (void)vkBindImageMemory(Vulkan->Device, R.Image, R.Memory, 0u);
        VkImageViewCreateInfo ViewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        ViewInfo.image = R.Image; ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; ViewInfo.format = Format;
        ViewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, T.LevelCount, 0u, 1u };
        (void)vkCreateImageView(Vulkan->Device, &ViewInfo, nullptr, &R.View);
    }

    // ③ One-time copy: UNDEFINED → TRANSFER_DST, per-level buffer→image copies, → SHADER_READ_ONLY.
    VkCommandBufferAllocateInfo CommandInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    CommandInfo.commandPool = Vulkan->ComputeCommandPool; CommandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; CommandInfo.commandBufferCount = 1u;
    VkCommandBuffer Command = VK_NULL_HANDLE;
    (void)vkAllocateCommandBuffers(Vulkan->Device, &CommandInfo, &Command);
    VkCommandBufferBeginInfo Begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    (void)vkBeginCommandBuffer(Command, &Begin);
    for (uint32_t I = 0u; I < Count; ++I)
    {
        const TextureDescriptor& T = Source[I];
        VulkanRecord::ResidentTexture& R = Vulkan->Textures[I];
        if (!R.Image) continue;
        VkImageMemoryBarrier ToTransfer{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        ToTransfer.srcAccessMask = 0u; ToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        ToTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; ToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        ToTransfer.srcQueueFamilyIndex = ToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ToTransfer.image = R.Image; ToTransfer.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, T.LevelCount, 0u, 1u };
        vkCmdPipelineBarrier(Command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &ToTransfer);

        std::vector<VkBufferImageCopy> Copies(T.LevelCount);
        for (uint32_t L = 0u; L < T.LevelCount; ++L)
        {
            VkBufferImageCopy& C = Copies[L];
            C.bufferOffset = TextureOffset[I] + T.LevelOffsets[L];
            C.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, L, 0u, 1u };
            C.imageExtent = { std::max(1u, T.Width >> L), std::max(1u, T.Height >> L), 1u };
        }
        vkCmdCopyBufferToImage(Command, Staging, R.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, T.LevelCount, Copies.data());

        VkImageMemoryBarrier ToShader = ToTransfer;
        ToShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; ToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        ToShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; ToShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(Command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &ToShader);
    }
    (void)vkEndCommandBuffer(Command);
    VkSubmitInfo Submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    Submit.commandBufferCount = 1u; Submit.pCommandBuffers = &Command;
    (void)vkQueueSubmit(Vulkan->ComputeQueue, 1u, &Submit, VK_NULL_HANDLE);
    (void)vkQueueWaitIdle(Vulkan->ComputeQueue);
    vkFreeCommandBuffers(Vulkan->Device, Vulkan->ComputeCommandPool, 1u, &Command);
    vkDestroyBuffer(Vulkan->Device, Staging, nullptr);
    vkFreeMemory(Vulkan->Device, StagingMemory, nullptr);

    std::cerr << "[SwapchainExchange] Textures: " << Count << " resident (" << (StagingBytes >> 20) << " MB) in the bindless table.\n";
    WriteDescriptorSet();
}

void SwapchainExchange::UploadShadingTables(const float* Energy, const float* Sheen, uint32_t Resolution) noexcept
{
    // DeviceExchange must not include DisplayPresentation (it is the layer below it) — the caller bakes with
    //    ShadingTableCodec and hands over the two RGBA32F planes.
    if (!Vulkan->Device || Vulkan->ShadingTables[0].View || !Energy || !Sheen || Resolution == 0u) return;
    const uint32_t N = Resolution;
    const float* Source[2] = { Energy, Sheen };

    if (!Vulkan->TableSampler)
    {
        VkSamplerCreateInfo SamplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        SamplerInfo.magFilter = SamplerInfo.minFilter = VK_FILTER_LINEAR;
        SamplerInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        SamplerInfo.addressModeU = SamplerInfo.addressModeV = SamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        SamplerInfo.maxLod       = 0.0f;
        (void)vkCreateSampler(Vulkan->Device, &SamplerInfo, nullptr, &Vulkan->TableSampler);
    }

    const VkDeviceSize TableBytes = static_cast<VkDeviceSize>(N) * N * 4u * sizeof(float);
    VkBuffer Staging = VK_NULL_HANDLE; VkDeviceMemory StagingMemory = VK_NULL_HANDLE;
    AllocateBuffer(Vulkan->Device, Vulkan->MemoryProperties, TableBytes * 2u, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, Staging, StagingMemory);
    {
        void* Mapped = nullptr;
        (void)vkMapMemory(Vulkan->Device, StagingMemory, 0u, VK_WHOLE_SIZE, 0u, &Mapped);
        if (Mapped)
        {
            std::memcpy(static_cast<uint8_t*>(Mapped),              Source[0], static_cast<size_t>(TableBytes));
            std::memcpy(static_cast<uint8_t*>(Mapped) + TableBytes, Source[1], static_cast<size_t>(TableBytes));
            vkUnmapMemory(Vulkan->Device, StagingMemory);
        }
    }

    VkCommandBufferAllocateInfo CommandInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    CommandInfo.commandPool = Vulkan->ComputeCommandPool; CommandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; CommandInfo.commandBufferCount = 1u;
    VkCommandBuffer Command = VK_NULL_HANDLE;
    (void)vkAllocateCommandBuffers(Vulkan->Device, &CommandInfo, &Command);
    VkCommandBufferBeginInfo Begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    (void)vkBeginCommandBuffer(Command, &Begin);

    for (uint32_t I = 0u; I < 2u; ++I)
    {
        VulkanRecord::ResidentTexture& R = Vulkan->ShadingTables[I];
        VkImageCreateInfo ImageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        ImageInfo.imageType = VK_IMAGE_TYPE_2D; ImageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        ImageInfo.extent = { N, N, 1u }; ImageInfo.mipLevels = 1u; ImageInfo.arrayLayers = 1u;
        ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT; ImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        ImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(Vulkan->Device, &ImageInfo, nullptr, &R.Image) != VK_SUCCESS) { R.Image = VK_NULL_HANDLE; continue; }
        VkMemoryRequirements Requirements{};
        vkGetImageMemoryRequirements(Vulkan->Device, R.Image, &Requirements);
        VkMemoryAllocateInfo Allocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        Allocate.allocationSize = Requirements.size;
        for (uint32_t M = 0u; M < Vulkan->MemoryProperties.memoryTypeCount; ++M)
            if ((Requirements.memoryTypeBits & (1u << M)) && (Vulkan->MemoryProperties.memoryTypes[M].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) { Allocate.memoryTypeIndex = M; break; }
        (void)vkAllocateMemory(Vulkan->Device, &Allocate, nullptr, &R.Memory);
        (void)vkBindImageMemory(Vulkan->Device, R.Image, R.Memory, 0u);
        VkImageViewCreateInfo ViewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        ViewInfo.image = R.Image; ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; ViewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        ViewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
        (void)vkCreateImageView(Vulkan->Device, &ViewInfo, nullptr, &R.View);

        VkImageMemoryBarrier ToTransfer{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        ToTransfer.srcAccessMask = 0u; ToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        ToTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; ToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        ToTransfer.srcQueueFamilyIndex = ToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ToTransfer.image = R.Image; ToTransfer.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
        vkCmdPipelineBarrier(Command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &ToTransfer);
        VkBufferImageCopy Copy{};
        Copy.bufferOffset = TableBytes * I;
        Copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
        Copy.imageExtent = { N, N, 1u };
        vkCmdCopyBufferToImage(Command, Staging, R.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &Copy);
        VkImageMemoryBarrier ToShader = ToTransfer;
        ToShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; ToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        ToShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; ToShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(Command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &ToShader);
    }
    (void)vkEndCommandBuffer(Command);
    VkSubmitInfo Submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    Submit.commandBufferCount = 1u; Submit.pCommandBuffers = &Command;
    (void)vkQueueSubmit(Vulkan->ComputeQueue, 1u, &Submit, VK_NULL_HANDLE);
    (void)vkQueueWaitIdle(Vulkan->ComputeQueue);
    vkFreeCommandBuffers(Vulkan->Device, Vulkan->ComputeCommandPool, 1u, &Command);
    vkDestroyBuffer(Vulkan->Device, Staging, nullptr);
    vkFreeMemory(Vulkan->Device, StagingMemory, nullptr);
    std::cerr << "[SwapchainExchange] Shading tables: GGX energy + LTC sheen, 2 x " << N << "x" << N << " RGBA32F resident (bindings 13/14).\n";
}

void* SwapchainExchange::SwapReservoirParity() noexcept
{
    if (!Vulkan->Device || !Vulkan->ComputeDescriptorSet) return nullptr;
    if (!Vulkan->ReservoirBuffers[0u] || !Vulkan->ReservoirBuffers[1u]) return nullptr;
    Vulkan->ReservoirParity = !Vulkan->ReservoirParity;
    // Rewrite only bindings 16/17 (the full WriteDescriptorSet also writes them — same values, harmless).
    const uint32_t PrevSlot = Vulkan->ReservoirParity ? 1u : 0u;
    VkDescriptorBufferInfo Infos[2] =
    {
        { Vulkan->ReservoirBuffers[PrevSlot],      0u, VK_WHOLE_SIZE },
        { Vulkan->ReservoirBuffers[PrevSlot ^ 1u], 0u, VK_WHOLE_SIZE }
    };
    VkWriteDescriptorSet Writes[2] = {};
    for (uint32_t I = 0u; I < 2u; ++I)
    {
        Writes[I].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[I].dstSet          = Vulkan->ComputeDescriptorSet;
        Writes[I].dstBinding      = 16u + I;
        Writes[I].descriptorCount = 1u;
        Writes[I].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Writes[I].pBufferInfo     = &Infos[I];
    }
    vkUpdateDescriptorSets(Vulkan->Device, 2u, Writes, 0u, nullptr);
    return Vulkan->ReservoirBuffers[PrevSlot];
}

void SwapchainExchange::UploadTraversal(const TraversalIndex& Traversal) noexcept
{
    if (!Vulkan->Device || !Traversal.IsReady()) return;
    vkDeviceWaitIdle(Vulkan->Device);
    if (Vulkan->TraversalNodeBuffer) vkDestroyBuffer(Vulkan->Device, Vulkan->TraversalNodeBuffer, nullptr);
    if (Vulkan->TraversalNodeMemory) vkFreeMemory   (Vulkan->Device, Vulkan->TraversalNodeMemory, nullptr);
    if (Vulkan->TraversalLeafBuffer) vkDestroyBuffer(Vulkan->Device, Vulkan->TraversalLeafBuffer, nullptr);
    if (Vulkan->TraversalLeafMemory) vkFreeMemory   (Vulkan->Device, Vulkan->TraversalLeafMemory, nullptr);
    Vulkan->TraversalNodeBuffer = Vulkan->TraversalLeafBuffer = VK_NULL_HANDLE;
    Vulkan->TraversalNodeMemory = Vulkan->TraversalLeafMemory = VK_NULL_HANDLE;

    constexpr uint32_t HostVisible = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    const auto Upload = [&](const std::vector<float>& Blob, VkBuffer& Buffer, VkDeviceMemory& Memory)
    {
        const VkDeviceSize ByteCount = static_cast<VkDeviceSize>(Blob.size()) * sizeof(float);
        AllocateBuffer(Vulkan->Device, Vulkan->MemoryProperties, ByteCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, HostVisible, Buffer, Memory);
        void* Mapped = nullptr;
        (void)vkMapMemory(Vulkan->Device, Memory, 0u, ByteCount, 0u, &Mapped);
        if (Mapped) { std::memcpy(Mapped, Blob.data(), static_cast<size_t>(ByteCount)); vkUnmapMemory(Vulkan->Device, Memory); }
    };
    Upload(Traversal.QueryNodeBlob(), Vulkan->TraversalNodeBuffer, Vulkan->TraversalNodeMemory);
    Upload(Traversal.QueryLeafBlob(), Vulkan->TraversalLeafBuffer, Vulkan->TraversalLeafMemory);
    // Remember what was allocated so RefreshTraversal can refuse a blob that no longer fits instead of truncating.
    TraversalNodeCapacity = static_cast<VkDeviceSize>(Traversal.QueryNodeBlob().size()) * sizeof(float);
    TraversalLeafCapacity = static_cast<VkDeviceSize>(Traversal.QueryLeafBlob().size()) * sizeof(float);
    TraversalResident = true;
    WriteDescriptorSet();
}

bool SwapchainExchange::RefreshTraversal(const TraversalIndex& Traversal, const std::vector<TriangleIndex>& Facets) noexcept
{
    // D5 per-frame path. Unlike UploadTraversal this must NOT reallocate: no vkDeviceWaitIdle, no descriptor
    //    rewrite, because the VkBuffer handles are unchanged. It only succeeds while the refitted blobs still fit
    //    the allocations made at load — a refit preserves topology, so in practice they do, but a grown blob is
    //    refused rather than truncated.
    if (!Vulkan || !Vulkan->Device || !TraversalResident) return false;
    if (!Vulkan->TraversalNodeBuffer || !Vulkan->TraversalLeafBuffer) return false;

    const auto Refresh = [&](const std::vector<float>& Blob, VkDeviceMemory Memory, VkDeviceSize Capacity) -> bool
    {
        const VkDeviceSize ByteCount = static_cast<VkDeviceSize>(Blob.size()) * sizeof(float);
        if (ByteCount == 0u || ByteCount > Capacity) return false;
        void* Mapped = nullptr;
        if (vkMapMemory(Vulkan->Device, Memory, 0u, ByteCount, 0u, &Mapped) != VK_SUCCESS || Mapped == nullptr) return false;
        std::memcpy(Mapped, Blob.data(), static_cast<size_t>(ByteCount));
        vkUnmapMemory(Vulkan->Device, Memory);
        return true;
    };

    if (!Refresh(Traversal.QueryNodeBlob(), Vulkan->TraversalNodeMemory, TraversalNodeCapacity)) return false;
    if (!Refresh(Traversal.QueryLeafBlob(), Vulkan->TraversalLeafMemory, TraversalLeafCapacity)) return false;

    // The kernel resolves a hit's material and normal from Triangles[], so the flat triangles must move with the
    //    acceleration structure or shading would read the body's old position.
    UploadTriangles(Facets);
    return true;
}

void SwapchainExchange::UploadScene(const SceneStructure& Scene, const TraversalIndex& Traversal, const TextureIndex* Textures) noexcept
{
    if (!Vulkan->Device) return;
    Visibility.UploadScene(Scene);
    if (Textures) UploadTextures(*Textures);        // R4a: bindless table (binding 15) — before the descriptor writes below
    UploadTriangles(Scene.QueryFlatTriangles());   // kernel: material / normal lookup by CWBVH primitive index
    UploadMaterials(Scene.QueryMaterials());       // R4a: MaterialRecord + MaterialSlabRecord (bindings 2, 10)
    UploadTraversal(Traversal);                    // R3: CWBVH node + triangle blobs (bindings 8-9)

    // R4b: the raster's alpha-mask test borrows the slab SSBO and the bindless table (VisibilityRaster.frag bindings 6 / 7).
    if (Vulkan->DescriptorIndexing)
    {
        std::vector<const void*> Views; Views.reserve(Vulkan->Textures.size());
        for (const VulkanRecord::ResidentTexture& T : Vulkan->Textures) Views.push_back(T.View);
        Visibility.AssignRasterMaterials(Vulkan->SlabBuffer, Vulkan->TextureSampler, Views.data(), static_cast<uint32_t>(Views.size()));
    }
}

bool SwapchainExchange::BringVisibility() noexcept
{
    if (!Visibility.Bring(Vulkan->Device, Vulkan->PhysicalDevice, kCycleSlotCount, DrawIndirectCountSupported, Vulkan->DescriptorIndexing ? kTextureSlotCapacity : 0u)) return false;
    if (!Visibility.Resize(Configuration.Width, Configuration.Height, Vulkan->StorageImageView)) return false;
    WriteDescriptorSet();
    return true;
}

//============================================================================================================================================
//                                           RECORD AND PRESENT
//============================================================================================================================================

void SwapchainExchange::RecordAndPresent(const DispatchConfiguration& Dispatch) noexcept
{
    const uint32_t ActiveSlot = Vulkan->ActiveSlot;

    vkWaitForFences(Vulkan->Device, 1u, &Vulkan->CycleFences[ActiveSlot], VK_TRUE, UINT64_MAX);

    uint32_t ImageOrdinal = 0u;
    const VkResult AcquireResult = vkAcquireNextImageKHR(
        Vulkan->Device, Vulkan->Swapchain, UINT64_MAX,
        Vulkan->AcquireSemaphores[ActiveSlot], VK_NULL_HANDLE, &ImageOrdinal);

    if (AcquireResult == VK_ERROR_OUT_OF_DATE_KHR || ResizePending)
    {
        ResizePending = false;
        (void)RebuildSwapchain();
        return;
    }

    if (AcquireResult != VK_SUCCESS && AcquireResult != VK_SUBOPTIMAL_KHR)
    {
        std::cerr << "[SwapchainExchange] vkAcquireNextImageKHR failed (VkResult " << static_cast<int>(AcquireResult) << ").\n";
        return;
    }

    if (!Vulkan->TriangleBuffer || !Vulkan->MaterialBuffer || !TraversalResident || !Visibility.IsReady() || !VisibilityFrameValid)
    {
        // Descriptors for bindings 1/2/4-7 are unwritten until the scene is uploaded; dispatching now would be UB.
        std::cerr << "[SwapchainExchange] RecordAndPresent called before UploadScene / AssignVisibilityFrame - frame skipped.\n";
        return;
    }

    if (Vulkan->ImageOrdinalFences[ImageOrdinal] != VK_NULL_HANDLE)
        vkWaitForFences(Vulkan->Device, 1u, &Vulkan->ImageOrdinalFences[ImageOrdinal], VK_TRUE, UINT64_MAX);
    Vulkan->ImageOrdinalFences[ImageOrdinal] = Vulkan->CycleFences[ActiveSlot];

    void* PrevReservoirs = SwapReservoirParity();   // R6: prev = last frame's curr before recording the new frame
    Visibility.AssignReservoirView(PrevReservoirs);   // R6 row 3: resolve binding 13 follows the kernel's prev buffer (M/W/Age views)
    RecordComputeCommands(ImageOrdinal, Dispatch);

    vkResetFences(Vulkan->Device, 1u, &Vulkan->CycleFences[ActiveSlot]);

    // The first touch of the acquired image is the blit (transfer stage), then the ImGui colour pass.
    VkPipelineStageFlags WaitStage = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo Submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    Submit.waitSemaphoreCount   = 1u;
    Submit.pWaitSemaphores      = &Vulkan->AcquireSemaphores[ActiveSlot];
    Submit.pWaitDstStageMask    = &WaitStage;
    Submit.commandBufferCount   = 1u;
    Submit.pCommandBuffers      = &Vulkan->ComputeCommands[ImageOrdinal];
    Submit.signalSemaphoreCount = 1u;
    Submit.pSignalSemaphores    = &Vulkan->ReleaseSemaphores[ImageOrdinal];
    (void)vkQueueSubmit(Vulkan->GraphicsQueue, 1u, &Submit, Vulkan->CycleFences[ActiveSlot]);

    VkPresentInfoKHR PresentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    PresentInfo.waitSemaphoreCount = 1u;
    PresentInfo.pWaitSemaphores    = &Vulkan->ReleaseSemaphores[ImageOrdinal];
    PresentInfo.swapchainCount     = 1u;
    PresentInfo.pSwapchains        = &Vulkan->Swapchain;
    PresentInfo.pImageIndices      = &ImageOrdinal;

    const VkResult PresentResult = vkQueuePresentKHR(Vulkan->GraphicsQueue, &PresentInfo);
    if (PresentResult == VK_ERROR_OUT_OF_DATE_KHR || PresentResult == VK_SUBOPTIMAL_KHR || ResizePending)
    {
        ResizePending = false;
        (void)RebuildSwapchain();
    }

    Vulkan->ActiveSlot = (ActiveSlot + 1u) % kCycleSlotCount;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           RECORD COMPUTE COMMANDS
//------------------------------------------------------------------------------------------------------------------------

void SwapchainExchange::RecordComputeCommands(uint32_t ImageOrdinal, const DispatchConfiguration& Dispatch) noexcept
{
    VkCommandBuffer Command = Vulkan->ComputeCommands[ImageOrdinal];

    VkCommandBufferBeginInfo BeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    (void)vkBeginCommandBuffer(Command, &BeginInfo);

    // ① Storage image → GENERAL for compute write
    {
        VkImageMemoryBarrier Barrier{};
        Barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        Barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
        Barrier.newLayout                       = VK_IMAGE_LAYOUT_GENERAL;
        Barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        Barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        Barrier.image                           = Vulkan->StorageImage;
        Barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        Barrier.subresourceRange.levelCount     = 1u;
        Barrier.subresourceRange.layerCount     = 1u;
        Barrier.srcAccessMask                   = 0u;
        Barrier.dstAccessMask                   = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(Command,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0u, 0u, nullptr, 0u, nullptr, 1u, &Barrier);
    }

    // ①b History image → GENERAL; first use transitions from UNDEFINED, later uses order the previous frame's writes
    {
        VkImageMemoryBarrier Barrier{};
        Barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        Barrier.oldLayout                       = Vulkan->HistoryInitialised ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
        Barrier.newLayout                       = VK_IMAGE_LAYOUT_GENERAL;
        Barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        Barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        Barrier.image                           = Vulkan->HistoryImage;
        Barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        Barrier.subresourceRange.levelCount     = 1u;
        Barrier.subresourceRange.layerCount     = 1u;
        Barrier.srcAccessMask                   = Vulkan->HistoryInitialised ? static_cast<VkAccessFlags>(VK_ACCESS_SHADER_WRITE_BIT) : static_cast<VkAccessFlags>(0u);
        Barrier.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(Command,
            Vulkan->HistoryInitialised ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0u, 0u, nullptr, 0u, nullptr, 1u, &Barrier);
        Vulkan->HistoryInitialised = true;
    }

    // ①c R6 reservoirs: zero-fill once, then order the previous frame's writes before this frame's access.
    if (Vulkan->ReservoirBuffers[0u] && Vulkan->ReservoirBuffers[1u] && Vulkan->ReservoirBytes > 0u)
    {
        if (!Vulkan->ReservoirsInitialised)
        {
            for (uint32_t I = 0u; I < 2u; ++I)
                vkCmdFillBuffer(Command, Vulkan->ReservoirBuffers[I], 0u, Vulkan->ReservoirBytes, 0u);
            VkBufferMemoryBarrier FillBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
            FillBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            FillBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            FillBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            FillBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            FillBarrier.buffer = Vulkan->ReservoirBuffers[0u];
            FillBarrier.offset = 0u;
            FillBarrier.size   = Vulkan->ReservoirBytes;
            // Both buffers are filled together; one barrier per buffer (same parameters, different handle).
            VkBufferMemoryBarrier FillBarriers[2] = { FillBarrier, FillBarrier };
            FillBarriers[1u].buffer = Vulkan->ReservoirBuffers[1u];
            vkCmdPipelineBarrier(Command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0u, 0u, nullptr, 2u, FillBarriers, 0u, nullptr);
            Vulkan->ReservoirsInitialised = true;
        }
        else
        {
            // Same-queue frames execute in submission order; this orders last frame's curr-writes (now prev)
            //    before this frame's prev-reads and curr-writes.
            VkBufferMemoryBarrier Barriers[2] = {};
            for (uint32_t I = 0u; I < 2u; ++I)
            {
                Barriers[I].sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                Barriers[I].srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
                Barriers[I].dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                Barriers[I].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                Barriers[I].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                Barriers[I].buffer              = Vulkan->ReservoirBuffers[I];
                Barriers[I].offset              = 0u;
                Barriers[I].size                = Vulkan->ReservoirBytes;
            }
            vkCmdPipelineBarrier(Command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0u, 0u, nullptr, 2u, Barriers, 0u, nullptr);
        }
    }

    // Render scale: every pass only covers Dispatch.ViewportWidth × ViewportHeight (the top-left sub-rectangle of
    //    the full-size targets); the blit below stretches that region over the whole swapchain image.
    const uint32_t RenderWidth  = std::clamp(Dispatch.ViewportWidth,  1u, Configuration.Width);
    const uint32_t RenderHeight = std::clamp(Dispatch.ViewportHeight, 1u, Configuration.Height);

    // ①c R2 front end: cull → visibility raster → HiZ → cull → raster → surface resolve (writes bindings 4/5 for the
    //    kernel; in a debug view it writes the presentation image directly and the kernel is skipped).
    VisibilityFrameConfiguration Frame = VisibilityFrame;
    Frame.RenderWidth  = RenderWidth;
    Frame.RenderHeight = RenderHeight;
    Visibility.RecordFrame(Command, Vulkan->ActiveSlot, Frame);

    if (Frame.DebugView == DebugViewCategory::Off)
    {
        // ② Dispatch ReSTIR compute
        vkCmdBindPipeline(Command, VK_PIPELINE_BIND_POINT_COMPUTE, Vulkan->ComputePipeline);
        vkCmdBindDescriptorSets(Command, VK_PIPELINE_BIND_POINT_COMPUTE,
            Vulkan->ComputePipelineLayout, 0u, 1u, &Vulkan->ComputeDescriptorSet, 0u, nullptr);
        vkCmdPushConstants(Command, Vulkan->ComputePipelineLayout,
            VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(DispatchConfiguration), &Dispatch);
        const uint32_t GroupX = (RenderWidth  + kLocalGroupSizeX - 1u) / kLocalGroupSizeX;
        const uint32_t GroupY = (RenderHeight + kLocalGroupSizeY - 1u) / kLocalGroupSizeY;
        vkCmdDispatch(Command, GroupX, GroupY, 1u);
    }
    Visibility.RecordKernelEnd(Command, Vulkan->ActiveSlot);

    // ②b Project overlay (SpatialInterface) — draws world-space figures onto the resolved scene before the blit, so
    //     the panel is part of the presented image rather than a screen-space sticker on top of it.
    //     The overlay begins its own render pass expecting COLOR_ATTACHMENT_OPTIMAL, so the image is transitioned in
    //     and back out; without the round trip the following blit would read an image in the wrong layout.
    if (Overlay)
    {
        VkImageMemoryBarrier ToAttachment{};
        ToAttachment.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        ToAttachment.oldLayout                   = VK_IMAGE_LAYOUT_GENERAL;
        ToAttachment.newLayout                   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ToAttachment.image                       = Vulkan->StorageImage;
        ToAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ToAttachment.subresourceRange.levelCount = 1u;
        ToAttachment.subresourceRange.layerCount = 1u;
        ToAttachment.srcAccessMask               = VK_ACCESS_SHADER_WRITE_BIT;
        ToAttachment.dstAccessMask               = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(Command,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0u, 0u, nullptr, 0u, nullptr, 1u, &ToAttachment);

        Overlay(static_cast<void*>(Command), Vulkan->ActiveSlot);

        VkImageMemoryBarrier ToGeneral = ToAttachment;
        ToGeneral.oldLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ToGeneral.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
        ToGeneral.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        ToGeneral.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(Command,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0u, 0u, nullptr, 0u, nullptr, 1u, &ToGeneral);
    }

    // ③ Storage image → TRANSFER_SRC for blit
    {
        VkImageMemoryBarrier Barrier{};
        Barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        Barrier.oldLayout                       = VK_IMAGE_LAYOUT_GENERAL;
        Barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        Barrier.image                           = Vulkan->StorageImage;
        Barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        Barrier.subresourceRange.levelCount     = 1u;
        Barrier.subresourceRange.layerCount     = 1u;
        Barrier.srcAccessMask                   = VK_ACCESS_SHADER_WRITE_BIT;
        Barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(Command,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0u, 0u, nullptr, 0u, nullptr, 1u, &Barrier);
    }

    // ④ Swapchain image → TRANSFER_DST
    {
        VkImageMemoryBarrier Barrier{};
        Barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        Barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
        Barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        Barrier.image                           = Vulkan->SwapchainImages[ImageOrdinal];
        Barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        Barrier.subresourceRange.levelCount     = 1u;
        Barrier.subresourceRange.layerCount     = 1u;
        Barrier.srcAccessMask                   = 0u;
        Barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(Command,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0u, 0u, nullptr, 0u, nullptr, 1u, &Barrier);
    }

    // ⑤ Blit storage → swapchain
    VkImageBlit BlitRegion{};
    BlitRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
    BlitRegion.srcOffsets[0]  = { 0, 0, 0 };
    BlitRegion.srcOffsets[1]  = { static_cast<int32_t>(RenderWidth), static_cast<int32_t>(RenderHeight), 1 };
    BlitRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
    BlitRegion.dstOffsets[0]  = { 0, 0, 0 };
    BlitRegion.dstOffsets[1]  = { static_cast<int32_t>(Configuration.Width), static_cast<int32_t>(Configuration.Height), 1 };
    const bool Upscaling = RenderWidth != Configuration.Width || RenderHeight != Configuration.Height;
    vkCmdBlitImage(Command,
        Vulkan->StorageImage,                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        Vulkan->SwapchainImages[ImageOrdinal],   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1u, &BlitRegion, Upscaling ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);

    // ⑥ Swapchain image → COLOR_ATTACHMENT_OPTIMAL for ImGui
    {
        VkImageMemoryBarrier Barrier{};
        Barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        Barrier.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        Barrier.newLayout                       = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        Barrier.image                           = Vulkan->SwapchainImages[ImageOrdinal];
        Barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        Barrier.subresourceRange.levelCount     = 1u;
        Barrier.subresourceRange.layerCount     = 1u;
        Barrier.srcAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
        Barrier.dstAccessMask                   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(Command,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0u, 0u, nullptr, 0u, nullptr, 1u, &Barrier);
    }

    // ⑦ ImGui render pass
    VkClearValue ClearValue{};
    ClearValue.color = {{ 0.0f, 0.0f, 0.0f, 0.0f }};

    VkRenderPassBeginInfo RenderPassBegin{};
    RenderPassBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    RenderPassBegin.renderPass        = Vulkan->ImGuiRenderPass;
    RenderPassBegin.framebuffer       = Vulkan->ImGuiFramebuffers[ImageOrdinal];
    RenderPassBegin.renderArea.extent = Vulkan->SwapchainExtent;
    RenderPassBegin.clearValueCount   = 1u;
    RenderPassBegin.pClearValues      = &ClearValue;
    vkCmdBeginRenderPass(Command, &RenderPassBegin, VK_SUBPASS_CONTENTS_INLINE);
    if (ImDrawData* DrawData = ImGui::GetDrawData())
        ImGui_ImplVulkan_RenderDrawData(DrawData, Command);
    vkCmdEndRenderPass(Command);

    (void)vkEndCommandBuffer(Command);
}

//============================================================================================================================================
//                                             DISPLAY SETTINGS (present pacing · fullscreen)
//============================================================================================================================================

//============================================================================================================================================
//                                        DEVICE SEAM (handles an overlay needs to record)
//============================================================================================================================================
// Deliberately thin: these hand back handles this class already owns so a project-side overlay can build its own
//    resources against the same device and targets. There is no depth target in this renderer — the compute path
//    resolves into a colour storage image — so QueryDepthView/Format report "none" and an overlay draws depthless.

void*    SwapchainExchange::QueryDevice()         const noexcept { return Vulkan ? static_cast<void*>(Vulkan->Device)           : nullptr; }
void*    SwapchainExchange::QueryPhysicalDevice() const noexcept { return Vulkan ? static_cast<void*>(Vulkan->PhysicalDevice)   : nullptr; }
void*    SwapchainExchange::QueryColourView()     const noexcept { return Vulkan ? static_cast<void*>(Vulkan->StorageImageView) : nullptr; }
void*    SwapchainExchange::QueryDepthView()      const noexcept { return nullptr; }
uint32_t SwapchainExchange::QueryColourFormat()   const noexcept { return static_cast<uint32_t>(VK_FORMAT_R8G8B8A8_UNORM); }
uint32_t SwapchainExchange::QueryDepthFormat()    const noexcept { return static_cast<uint32_t>(VK_FORMAT_UNDEFINED); }
uint32_t SwapchainExchange::QueryCycleSlotCount() const noexcept { return kCycleSlotCount; }
uint32_t SwapchainExchange::QueryCycleSlot()      const noexcept { return Vulkan ? Vulkan->ActiveSlot : 0u; }

uint32_t SwapchainExchange::ResolvePresentMode() const noexcept
{
    uint32_t Count = 0u;
    vkGetPhysicalDeviceSurfacePresentModesKHR(Vulkan->PhysicalDevice, Vulkan->Surface, &Count, nullptr);
    std::vector<VkPresentModeKHR> Modes(Count);
    if (Count > 0u) vkGetPhysicalDeviceSurfacePresentModesKHR(Vulkan->PhysicalDevice, Vulkan->Surface, &Count, Modes.data());
    const auto Supported = [&](VkPresentModeKHR M) { for (VkPresentModeKHR X : Modes) if (X == M) return true; return false; };

    switch (Pacing)
    {
        case PresentPacingCategory::VerticalSyncOff:
            if (Supported(VK_PRESENT_MODE_IMMEDIATE_KHR)) return VK_PRESENT_MODE_IMMEDIATE_KHR;
            if (Supported(VK_PRESENT_MODE_MAILBOX_KHR))   return VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        case PresentPacingCategory::VerticalSyncAdaptive:
            if (Supported(VK_PRESENT_MODE_FIFO_RELAXED_KHR)) return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
            break;
        case PresentPacingCategory::VerticalSyncOn:
            break;
    }
    return VK_PRESENT_MODE_FIFO_KHR;   // mandated by the spec, always present
}

const char* SwapchainExchange::QueryPresentModeName() const noexcept
{
    switch (static_cast<VkPresentModeKHR>(ResolvedPresentMode))
    {
        case VK_PRESENT_MODE_IMMEDIATE_KHR:    return "IMMEDIATE";
        case VK_PRESENT_MODE_MAILBOX_KHR:      return "MAILBOX";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED";
        default:                               return "FIFO";
    }
}

void SwapchainExchange::AssignPresentPacing(PresentPacingCategory Desired) noexcept
{
    if (Pacing == Desired) return;
    Pacing = Desired;
    ResizePending = true;   // rebuild at the next present with the new mode
}

void SwapchainExchange::AssignFullscreen(bool Desired) noexcept
{
    if (!GlfwWindow || FullscreenActive == Desired) return;
    if (Desired)
    {
        GLFWmonitor* Monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* Mode = Monitor ? glfwGetVideoMode(Monitor) : nullptr;
        if (!Mode) return;
        glfwGetWindowPos (GlfwWindow, &WindowedX, &WindowedY);
        glfwGetWindowSize(GlfwWindow, &WindowedW, &WindowedH);
        glfwSetWindowMonitor(GlfwWindow, Monitor, 0, 0, Mode->width, Mode->height, Mode->refreshRate);
    }
    else
    {
        if (WindowedW <= 0 || WindowedH <= 0) { WindowedW = static_cast<int>(Configuration.Width); WindowedH = static_cast<int>(Configuration.Height); WindowedX = WindowedY = 64; }
        glfwSetWindowMonitor(GlfwWindow, nullptr, WindowedX, WindowedY, WindowedW, WindowedH, GLFW_DONT_CARE);
    }
    FullscreenActive = Desired;
    ResizePending = true;   // the framebuffer callback also fires; a redundant rebuild is harmless
}

//============================================================================================================================================
//                                             SWAPCHAIN REBUILD (on resize)
//============================================================================================================================================

bool SwapchainExchange::RebuildSwapchain() noexcept
{
    int FramebufferW = 0, FramebufferH = 0;
    glfwGetFramebufferSize(GlfwWindow, &FramebufferW, &FramebufferH);
    while (FramebufferW == 0 || FramebufferH == 0)
    {
        glfwGetFramebufferSize(GlfwWindow, &FramebufferW, &FramebufferH);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(Vulkan->Device);

    for (auto& Framebuffer : Vulkan->ImGuiFramebuffers)
        vkDestroyFramebuffer(Vulkan->Device, Framebuffer, nullptr);
    Vulkan->ImGuiFramebuffers.clear();

    RetireSwapchain();

    if (!BringSwapchain() || !BringStorageImage()) return false;
    if (!Visibility.Resize(Configuration.Width, Configuration.Height, Vulkan->StorageImageView)) return false;

    // Every view handed out by the device seam has just been destroyed and recreated. Bumping the generation is how
    //    an overlay learns it must re-Resize; without it, it would keep rendering into a stale VkImageView.
    ++TargetGeneration;

    WriteDescriptorSet();

    const uint32_t ImageCount = static_cast<uint32_t>(Vulkan->SwapchainImages.size());
    Vulkan->ImGuiFramebuffers.resize(ImageCount);
    for (uint32_t Index = 0u; Index < ImageCount; ++Index)
    {
        VkFramebufferCreateInfo FramebufferInfo{};
        FramebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        FramebufferInfo.renderPass      = Vulkan->ImGuiRenderPass;
        FramebufferInfo.attachmentCount = 1u;
        FramebufferInfo.pAttachments    = &Vulkan->SwapchainImageViews[Index];
        FramebufferInfo.width           = Configuration.Width;
        FramebufferInfo.height          = Configuration.Height;
        FramebufferInfo.layers          = 1u;
        (void)vkCreateFramebuffer(Vulkan->Device, &FramebufferInfo, nullptr, &Vulkan->ImGuiFramebuffers[Index]);
    }
    return true;
}

//============================================================================================================================================
//                                               POLL AND CLOSE
//============================================================================================================================================

bool SwapchainExchange::CloseRequested() const noexcept
{
    return GlfwWindow && glfwWindowShouldClose(GlfwWindow);
}

void SwapchainExchange::PollInput(InputExchange& TargetInput) noexcept
{
    ForwardInput = &TargetInput;
    TargetInput.ResetCursorDelta();
    TargetInput.ResetMouseScroll();
    if (PendingInputReset)
    {
        TargetInput.ReleaseAllInputs();
        PendingInputReset = false;
    }
    glfwPollEvents();
    ForwardInput = nullptr;
}

//============================================================================================================================================
//                                               MEMORY TYPE RESOLUTION
//============================================================================================================================================

uint32_t SwapchainExchange::ResolveMemoryType(uint32_t TypeMask, uint32_t PropertyMask) const noexcept
{
    for (uint32_t Index = 0u; Index < Vulkan->MemoryProperties.memoryTypeCount; ++Index)
    {
        if ((TypeMask & (1u << Index)) &&
            (Vulkan->MemoryProperties.memoryTypes[Index].propertyFlags & PropertyMask) ==
             static_cast<VkMemoryPropertyFlags>(PropertyMask))
        {
            return Index;
        }
    }
    return 0u;
}

//============================================================================================================================================
//                                                 GLFW CALLBACKS
//============================================================================================================================================

void SwapchainExchange::OnKey(GLFWwindow* Window, int Key, int, int Action, int) noexcept
{
    auto* Self = static_cast<SwapchainExchange*>(glfwGetWindowUserPointer(Window));
    if (!Self || !Self->ForwardInput) return;

    const bool Pressed = (Action == GLFW_PRESS || Action == GLFW_REPEAT);

    // Text fields in the overlay own the keyboard while focused; releases always pass so nothing sticks.
    if (Pressed && ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard) return;

    auto MapKey = [&](int GlfwKey, VirtualKeyCategory EngineKey)
    {
        if (Key == GlfwKey) Self->ForwardInput->AssignKeyState(EngineKey, Pressed);
    };

    MapKey(GLFW_KEY_W,           VirtualKeyCategory::KeyW);
    MapKey(GLFW_KEY_A,           VirtualKeyCategory::KeyA);
    MapKey(GLFW_KEY_S,           VirtualKeyCategory::KeyS);
    MapKey(GLFW_KEY_D,           VirtualKeyCategory::KeyD);
    MapKey(GLFW_KEY_Q,           VirtualKeyCategory::KeyQ);
    MapKey(GLFW_KEY_E,           VirtualKeyCategory::KeyE);
    MapKey(GLFW_KEY_LEFT_SHIFT,  VirtualKeyCategory::KeyLeftShift);
    MapKey(GLFW_KEY_RIGHT_SHIFT, VirtualKeyCategory::KeyRightShift);
    MapKey(GLFW_KEY_ESCAPE,      VirtualKeyCategory::KeyEscape);

    if (Key == GLFW_KEY_ESCAPE && Action == GLFW_PRESS)
        glfwSetWindowShouldClose(Window, GLFW_TRUE);
}

void SwapchainExchange::OnMouseButton(GLFWwindow* Window, int Button, int Action, int) noexcept
{
    auto* Self = static_cast<SwapchainExchange*>(glfwGetWindowUserPointer(Window));
    if (!Self || !Self->ForwardInput) return;

    const bool Pressed = (Action == GLFW_PRESS);

    // A press that lands on the overlay belongs to the overlay; releases always pass so nothing sticks.
    if (Pressed && ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) return;

    if (Button == GLFW_MOUSE_BUTTON_RIGHT)
    {
        Self->ForwardInput->AssignMouseButton(MouseButtonCategory::ButtonRight, Pressed);
        glfwSetInputMode(Window, GLFW_CURSOR, Pressed ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        Self->CursorInitialised = false;
    }
    if (Button == GLFW_MOUSE_BUTTON_LEFT)
        Self->ForwardInput->AssignMouseButton(MouseButtonCategory::ButtonLeft,  Pressed);
    if (Button == GLFW_MOUSE_BUTTON_MIDDLE)
        Self->ForwardInput->AssignMouseButton(MouseButtonCategory::ButtonMiddle, Pressed);
}

void SwapchainExchange::OnCursorMove(GLFWwindow* Window, double X, double Y) noexcept
{
    auto* Self = static_cast<SwapchainExchange*>(glfwGetWindowUserPointer(Window));
    if (!Self || !Self->ForwardInput) return;

    if (!Self->CursorInitialised)
    {
        Self->PreviousCursorX  = X;
        Self->PreviousCursorY  = Y;
        Self->CursorInitialised = true;
    }

    const float Δx = static_cast<float>(X - Self->PreviousCursorX);
    const float Δy = static_cast<float>(Y - Self->PreviousCursorY);
    Self->PreviousCursorX = X;
    Self->PreviousCursorY = Y;

    Self->ForwardInput->AssignCursorDelta(Δx, Δy);
    Self->ForwardInput->AssignCursorPosition(static_cast<float>(X), static_cast<float>(Y));
}

void SwapchainExchange::OnScroll(GLFWwindow* Window, double, double OffsetY) noexcept
{
    auto* Self = static_cast<SwapchainExchange*>(glfwGetWindowUserPointer(Window));
    if (!Self || !Self->ForwardInput) return;
    if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) return;   // wheel over the overlay scrolls it, not the camera
    Self->ForwardInput->AssignMouseScroll(static_cast<float>(OffsetY));
}

void SwapchainExchange::OnFocus(GLFWwindow* Window, int Focused) noexcept
{
    auto* Self = static_cast<SwapchainExchange*>(glfwGetWindowUserPointer(Window));
    if (!Self) return;

    // Losing focus while a key or button is held means GLFW will never deliver the release; clear
    //    everything so the camera does not keep flying / steering when the user Alt-Tabs back.
    if (!Focused)
    {
        Self->PendingInputReset = true;
        glfwSetInputMode(Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        Self->CursorInitialised = false;
    }
}

void SwapchainExchange::OnFramebuffer(GLFWwindow* Window, int, int) noexcept
{
    auto* Self = static_cast<SwapchainExchange*>(glfwGetWindowUserPointer(Window));
    if (Self) Self->SignalResize();
}

} // namespace Frontier
