#pragma once
/*==========================================================================================================================================
                                                             VULKANCONTEXT.H
==========================================================================================================================================*/
// 🧩 Pure Vulkan 1.1 dynamic dispatch, headless & GLFW-swapchain device runtime, render-target & pipeline cache,
//    per-frame dynamic UBO + descriptor ring, and Dear ImGui Vulkan presenter. Zero OpenGL dependencies.

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

#include "Frontier/Engine/Rendering/Shaders/VulkanSpirvLibrary.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct GLFWwindow;
struct ImDrawData;

namespace Frontier
{

//----------------------------------------------------------------------------------------------------------------------
//                                           DYNAMIC VULKAN PROCEDURE TABLE
//----------------------------------------------------------------------------------------------------------------------

struct VulkanFunctionTable
{
    PFN_vkGetInstanceProcAddr                GetInstanceProcAddr                = nullptr;
    PFN_vkEnumerateInstanceExtensionProperties EnumerateInstanceExtensionProps  = nullptr;
    PFN_vkCreateInstance                     CreateInstance                     = nullptr;

    PFN_vkDestroyInstance                    DestroyInstance                    = nullptr;
    PFN_vkEnumeratePhysicalDevices           EnumeratePhysicalDevices           = nullptr;
    PFN_vkGetPhysicalDeviceProperties        GetPhysicalDeviceProperties        = nullptr;
    PFN_vkGetPhysicalDeviceFeatures          GetPhysicalDeviceFeatures          = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties  GetPhysicalDeviceMemoryProperties  = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilies = nullptr;
    PFN_vkCreateDevice                       CreateDevice                       = nullptr;
    PFN_vkGetDeviceProcAddr                  GetDeviceProcAddr                  = nullptr;

    // Optional KHR surface entry points (used when presenting to a GLFW window)
    PFN_vkDestroySurfaceKHR                  DestroySurfaceKHR                  = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR GetPhysicalDeviceSurfaceSupportKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR GetPhysicalDeviceSurfaceCaps  = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR GetPhysicalDeviceSurfaceFormatsKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR GetPhysicalDevicePresentModes = nullptr;

    // Device-level entry points
    PFN_vkDestroyDevice                      DestroyDevice                      = nullptr;
    PFN_vkGetDeviceQueue                     GetDeviceQueue                     = nullptr;
    PFN_vkDeviceWaitIdle                     DeviceWaitIdle                     = nullptr;
    PFN_vkQueueSubmit                        QueueSubmit                        = nullptr;
    PFN_vkQueueWaitIdle                      QueueWaitIdle                      = nullptr;

    PFN_vkAllocateMemory                     AllocateMemory                     = nullptr;
    PFN_vkFreeMemory                         FreeMemory                         = nullptr;
    PFN_vkMapMemory                          MapMemory                          = nullptr;
    PFN_vkUnmapMemory                        UnmapMemory                        = nullptr;

    PFN_vkCreateBuffer                       CreateBuffer                       = nullptr;
    PFN_vkDestroyBuffer                      DestroyBuffer                      = nullptr;
    PFN_vkGetBufferMemoryRequirements        GetBufferMemoryRequirements        = nullptr;
    PFN_vkBindBufferMemory                   BindBufferMemory                   = nullptr;

    PFN_vkCreateImage                        CreateImage                        = nullptr;
    PFN_vkDestroyImage                       DestroyImage                       = nullptr;
    PFN_vkGetImageMemoryRequirements         GetImageMemoryRequirements         = nullptr;
    PFN_vkBindImageMemory                    BindImageMemory                    = nullptr;
    PFN_vkCreateImageView                    CreateImageView                    = nullptr;
    PFN_vkDestroyImageView                   DestroyImageView                   = nullptr;
    PFN_vkCreateSampler                      CreateSampler                      = nullptr;
    PFN_vkDestroySampler                     DestroySampler                     = nullptr;

    PFN_vkCreateShaderModule                 CreateShaderModule                 = nullptr;
    PFN_vkDestroyShaderModule                DestroyShaderModule                = nullptr;
    PFN_vkCreateDescriptorSetLayout          CreateDescriptorSetLayout          = nullptr;
    PFN_vkDestroyDescriptorSetLayout         DestroyDescriptorSetLayout         = nullptr;
    PFN_vkCreateDescriptorPool               CreateDescriptorPool               = nullptr;
    PFN_vkDestroyDescriptorPool              DestroyDescriptorPool              = nullptr;
    PFN_vkAllocateDescriptorSets             AllocateDescriptorSets             = nullptr;
    PFN_vkUpdateDescriptorSets               UpdateDescriptorSets               = nullptr;

    PFN_vkCreatePipelineLayout               CreatePipelineLayout               = nullptr;
    PFN_vkDestroyPipelineLayout              DestroyPipelineLayout              = nullptr;
    PFN_vkCreateRenderPass                   CreateRenderPass                   = nullptr;
    PFN_vkDestroyRenderPass                  DestroyRenderPass                  = nullptr;
    PFN_vkCreateFramebuffer                  CreateFramebuffer                  = nullptr;
    PFN_vkDestroyFramebuffer                 DestroyFramebuffer                 = nullptr;
    PFN_vkCreateGraphicsPipelines            CreateGraphicsPipelines            = nullptr;
    PFN_vkDestroyPipeline                    DestroyPipeline                    = nullptr;

    PFN_vkCreateCommandPool                  CreateCommandPool                  = nullptr;
    PFN_vkDestroyCommandPool                 DestroyCommandPool                 = nullptr;
    PFN_vkResetCommandPool                   ResetCommandPool                   = nullptr;
    PFN_vkAllocateCommandBuffers             AllocateCommandBuffers             = nullptr;
    PFN_vkBeginCommandBuffer                 BeginCommandBuffer                 = nullptr;
    PFN_vkEndCommandBuffer                   EndCommandBuffer                   = nullptr;
    PFN_vkResetCommandBuffer                 ResetCommandBuffer                 = nullptr;

    PFN_vkCmdPipelineBarrier                 CmdPipelineBarrier                 = nullptr;
    PFN_vkCmdBeginRenderPass                 CmdBeginRenderPass                 = nullptr;
    PFN_vkCmdEndRenderPass                   CmdEndRenderPass                   = nullptr;
    PFN_vkCmdSetViewport                     CmdSetViewport                     = nullptr;
    PFN_vkCmdSetScissor                      CmdSetScissor                      = nullptr;
    PFN_vkCmdBindPipeline                    CmdBindPipeline                    = nullptr;
    PFN_vkCmdBindDescriptorSets              CmdBindDescriptorSets              = nullptr;
    PFN_vkCmdBindVertexBuffers               CmdBindVertexBuffers               = nullptr;
    PFN_vkCmdBindIndexBuffer                 CmdBindIndexBuffer                 = nullptr;
    PFN_vkCmdDraw                            CmdDraw                            = nullptr;
    PFN_vkCmdDrawIndexed                     CmdDrawIndexed                     = nullptr;
    PFN_vkCmdCopyBufferToImage               CmdCopyBufferToImage               = nullptr;
    PFN_vkCmdCopyImageToBuffer               CmdCopyImageToBuffer               = nullptr;

    PFN_vkCreateFence                        CreateFence                        = nullptr;
    PFN_vkDestroyFence                       DestroyFence                       = nullptr;
    PFN_vkWaitForFences                      WaitForFences                      = nullptr;
    PFN_vkResetFences                        ResetFences                        = nullptr;
    PFN_vkCreateSemaphore                    CreateSemaphore                    = nullptr;
    PFN_vkDestroySemaphore                   DestroySemaphore                   = nullptr;

    // Optional KHR swapchain entry points
    PFN_vkCreateSwapchainKHR                 CreateSwapchainKHR                 = nullptr;
    PFN_vkDestroySwapchainKHR                DestroySwapchainKHR                = nullptr;
    PFN_vkGetSwapchainImagesKHR              GetSwapchainImagesKHR              = nullptr;
    PFN_vkAcquireNextImageKHR                AcquireNextImageKHR                = nullptr;
    PFN_vkQueuePresentKHR                    QueuePresentKHR                    = nullptr;
};

//----------------------------------------------------------------------------------------------------------------------
//                                             GPU IMAGE & BUFFER RESOURCES
//----------------------------------------------------------------------------------------------------------------------

struct VulkanBufferResource
{
    VkBuffer       Buffer = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    void*          Mapped = nullptr;
    VkDeviceSize   Size   = 0u;
};

struct VulkanImageTarget
{
    VkImage         Image          = VK_NULL_HANDLE;
    VkDeviceMemory  Memory         = VK_NULL_HANDLE;
    VkImageView     View           = VK_NULL_HANDLE;
    VkFormat        Format         = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags Aspect      = VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageLayout   CurrentLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    int32_t         Width          = 0;
    int32_t         Height         = 0;
    bool            LinearFiltered = true;
    uint64_t        UiToken        = 0u;   // [-] - Non-zero token usable directly as ImTextureID!
};

enum class VulkanBlendFormula : uint8_t
{
    OpaqueOverwrite    = 0,   // [-] - No blending (replace attachment)
    PremultipliedAlpha = 1,   // [-] - ONE, ONE_MINUS_SRC_ALPHA (volumetric radiative transfer & embers)
    StandardAlpha      = 2,   // [-] - SRC_ALPHA, ONE_MINUS_SRC_ALPHA (ImGui & ghost/wireframe meshes)
};

enum class VulkanVertexLayout : uint8_t
{
    FullscreenProcedural = 0,   // [-] - gl_VertexIndex procedural triangle (no VBO)
    FloatSeed1           = 1,   // [-] - 1x float seed per point (PyroEmbers)
    WorldFacet9          = 2,   // [-] - vec3 pos + vec3 normal + vec3 barycentric (36 bytes)
    ImGuiVert20          = 3,   // [-] - vec2 pos + vec2 uv + uint8x4 unorm rgba (20 bytes)
};

struct VulkanPipelineSpec
{
    VulkanShaders::SpirvBinaryView VertexShader{};
    VulkanShaders::SpirvBinaryView FragmentShader{};
    VkRenderPass                   CompatibleTarget= VK_NULL_HANDLE;
    uint32_t                       ColorAttachments= 1u;
    VulkanVertexLayout             VertexFormat    = VulkanVertexLayout::FullscreenProcedural;
    VkPrimitiveTopology            Topology        = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VulkanBlendFormula             Blend           = VulkanBlendFormula::OpaqueOverwrite;
    bool                           DepthTest       = false;
    bool                           DepthWrite      = false;
    bool                           CullBackFaces   = false;
};

//----------------------------------------------------------------------------------------------------------------------
//                                                 VULKAN CONTEXT
//----------------------------------------------------------------------------------------------------------------------

class VulkanContext
{
  public:
    VulkanContext() = default;
    ~VulkanContext();

    VulkanContext(const VulkanContext&)            = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    // Headless initialization (no window surface required — runs on discrete GPU, integrated GPU, or SwiftShader/Lavapipe)
    bool InitializeHeadless(std::string& OutDiagnostic);

    // Windowed initialization using caller-supplied instance extensions (e.g. from glfwGetRequiredInstanceExtensions)
    using SurfaceFactoryFn = bool (*)(VkInstance Instance, void* UserData, VkSurfaceKHR& OutSurface);
    bool InitializeWithSurface(const char* const* InstanceExtensions, uint32_t ExtensionCount,
                               SurfaceFactoryFn SurfaceBuilder, void* UserData,
                               int InitialWidth, int InitialHeight, std::string& OutDiagnostic);

    void Shutdown() noexcept;
    void WaitIdle() const noexcept;

    // --- Resource creation & destruction ---
    bool CreateHostBuffer(VkDeviceSize ByteSize, VkBufferUsageFlags Usage, VulkanBufferResource& OutBuffer, std::string& OutDiag);
    void DestroyBuffer(VulkanBufferResource& Buf) noexcept;

    bool CreateImageTarget(int32_t Width, int32_t Height, VkFormat Format, VkImageUsageFlags ExtraUsage,
                           bool LinearFilter, VulkanImageTarget& OutTarget, std::string& OutDiag);
    void DestroyImageTarget(VulkanImageTarget& Target) noexcept;

    bool UploadRgba8Image(VulkanImageTarget& Target, const uint8_t* Pixels, size_t ByteCount, std::string& OutDiag);
    bool UploadRgba32fImage(VulkanImageTarget& Target, const float* FloatTexels, size_t FloatCount, std::string& OutDiag);
    bool DownloadRgba8Image(VulkanImageTarget& Target, std::vector<uint8_t>& OutTopDownRgba);

    VkPipeline CreateGraphicsPipeline(const VulkanPipelineSpec& Spec, std::string& OutDiag);
    void DestroyPipeline(VkPipeline& Pipeline) noexcept;

    VkFramebuffer CreateFramebuffer(VkRenderPass TargetFormat, const VulkanImageTarget* const* Attachments,
                                    uint32_t AttachmentCount, int32_t Width, int32_t Height);
    void DestroyFramebuffer(VkFramebuffer& Fbo) noexcept;

    // --- Frame & Immediate Command Recording ---
    VkCommandBuffer BeginFrameCommands();
    bool SubmitAndWaitFrameCommands();

    // Swapchain frame helpers (when initialized with InitializeWithSurface)
    bool BeginSwapchainFrame(int WindowWidth, int WindowHeight, VkCommandBuffer& OutCmd,
                             VkFramebuffer& OutSwapFbo, VkExtent2D& OutExtent);
    bool EndAndPresentSwapchainFrame(int WindowWidth, int WindowHeight);

    void TransitionImage(VkCommandBuffer Cmd, VulkanImageTarget& Target, VkImageLayout NewLayout);

    void BeginTarget(VkCommandBuffer Cmd, VkRenderPass TargetFormat, VkFramebuffer Fbo,
                     int32_t Width, int32_t Height, const std::array<float, 4>* ClearColor0 = nullptr,
                     bool ClearDepth = false);
    void EndTarget(VkCommandBuffer Cmd);

    // Allocates a dynamic UBO slice + transient descriptor set and binds Pipeline + UBO + up to 5 sampled textures!
    bool BindPipelineAndDescriptors(VkCommandBuffer Cmd, VkPipeline Pipeline,
                                    const void* UniformData, size_t UniformBytes,
                                    VulkanImageTarget* Tex0 = nullptr,
                                    VulkanImageTarget* Tex1 = nullptr,
                                    VulkanImageTarget* Tex2 = nullptr,
                                    VulkanImageTarget* Tex3 = nullptr,
                                    VulkanImageTarget* Tex4 = nullptr);

    [[nodiscard]] VulkanImageTarget* LookupUiTexture(uint64_t UiToken) noexcept;

    // --- Shared Canonical VkRenderPass Formats ---
    [[nodiscard]] VkRenderPass QueryTargetSingleRgba16f() const noexcept     { return TargetSingleRgba16f; }
    [[nodiscard]] VkRenderPass QueryTargetDualRgba16f() const noexcept       { return TargetDualRgba16f; }
    [[nodiscard]] VkRenderPass QueryTargetSingleRgba8Load() const noexcept   { return TargetSingleRgba8Load; }
    [[nodiscard]] VkRenderPass QueryTargetSingleRgba8Clear() const noexcept  { return TargetSingleRgba8Clear; }
    [[nodiscard]] VkRenderPass QueryTargetWorldMrtClear() const noexcept     { return TargetWorldMrtClear; }
    [[nodiscard]] VkRenderPass QueryTargetWorldMrtLoad() const noexcept      { return TargetWorldMrtLoad; }
    [[nodiscard]] VkRenderPass QueryTargetSwapchainClear() const noexcept    { return TargetSwapchainClear; }
    [[nodiscard]] VkFormat     QuerySwapchainFormat() const noexcept         { return SwapchainFormat; }

    [[nodiscard]] const VulkanFunctionTable& Vk() const noexcept { return Fn; }
    [[nodiscard]] VkDevice QueryDevice() const noexcept { return Device; }
    [[nodiscard]] VkInstance QueryInstance() const noexcept { return Instance; }
    [[nodiscard]] bool IsReady() const noexcept { return Initialized; }
    [[nodiscard]] const std::string& QueryDeviceSummary() const noexcept { return DeviceSummary; }

  private:
    bool LoadVulkanLibrary(std::string& OutDiagnostic);
    bool CreateDeviceAndCorePools(bool RequireSwapchain, std::string& OutDiagnostic);
    bool CreateCanonicalTargets(std::string& OutDiagnostic);
    bool RebuildSwapchain(int Width, int Height, std::string& OutDiagnostic);
    void DestroySwapchain() noexcept;
    uint32_t FindMemoryType(uint32_t TypeBits, VkMemoryPropertyFlags RequiredMask) const noexcept;
    void ResetTransientPools() noexcept;

    void*                            LibraryHandle          = nullptr;
    VulkanFunctionTable              Fn{};
    VkInstance                       Instance               = VK_NULL_HANDLE;
    VkSurfaceKHR                     Surface                = VK_NULL_HANDLE;
    VkPhysicalDevice                 PhysicalDevice         = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties       DeviceProps{};
    VkPhysicalDeviceMemoryProperties MemoryProps{};
    VkDevice                         Device                 = VK_NULL_HANDLE;
    VkQueue                          GraphicsQueue          = VK_NULL_HANDLE;
    uint32_t                         GraphicsQueueFamily    = 0u;
    std::string                      DeviceSummary          = "Vulkan";

    VkCommandPool                    CommandPool            = VK_NULL_HANDLE;
    VkCommandBuffer                  WorkCommandBuffer      = VK_NULL_HANDLE;
    VkFence                          WorkFence              = VK_NULL_HANDLE;

    VkSampler                        NearestSampler         = VK_NULL_HANDLE;
    VkSampler                        LinearSampler          = VK_NULL_HANDLE;
    VkDescriptorSetLayout            UnifiedSetLayout       = VK_NULL_HANDLE;
    VkPipelineLayout                 UnifiedPipelineLayout  = VK_NULL_HANDLE;

    // Transient per-frame dynamic UBO arena + pre-allocated descriptor set ring
    static constexpr uint32_t        MaxDrawsPerFrame       = 2560u;
    uint32_t                         UboSliceStride         = 2048u;
    VulkanBufferResource             UniformArena{};
    uint32_t                         UniformDrawCursor      = 0u;
    VkDescriptorPool                 TransientDescPool      = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet>     TransientSets{};
    uint32_t                         TransientSetCursor     = 0u;

    VulkanImageTarget                DummyBlackImage{};
    VulkanBufferResource             StagingBuffer{};
    std::vector<VulkanImageTarget*>  UiTextureRegistry{};   // index + 1 == UiToken

    // Canonical target definitions
    VkRenderPass                     TargetSingleRgba16f    = VK_NULL_HANDLE;
    VkRenderPass                     TargetDualRgba16f      = VK_NULL_HANDLE;
    VkRenderPass                     TargetSingleRgba8Load  = VK_NULL_HANDLE;
    VkRenderPass                     TargetSingleRgba8Clear = VK_NULL_HANDLE;
    VkRenderPass                     TargetWorldMrtClear    = VK_NULL_HANDLE; // RGBA8 + RGBA16F + D32_SFLOAT
    VkRenderPass                     TargetWorldMrtLoad     = VK_NULL_HANDLE;
    VkRenderPass                     TargetSwapchainClear   = VK_NULL_HANDLE;

    // Optional Swapchain state
    VkSwapchainKHR                   Swapchain              = VK_NULL_HANDLE;
    VkFormat                         SwapchainFormat        = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D                       SwapchainExtent        = { 0u, 0u };
    std::vector<VkImage>             SwapchainImages{};
    std::vector<VkImageView>         SwapchainViews{};
    std::vector<VkFramebuffer>       SwapchainFramebuffers{};
    VkSemaphore                      ImageAvailableSem      = VK_NULL_HANDLE;
    VkSemaphore                      RenderFinishedSem      = VK_NULL_HANDLE;
    uint32_t                         ActiveSwapImageIndex   = 0u;

    bool                             RecordingActive        = false;
    bool                             Initialized            = false;
};

//----------------------------------------------------------------------------------------------------------------------
//                                           DEAR IMGUI VULKAN PRESENTER
//----------------------------------------------------------------------------------------------------------------------

class VulkanImGuiPresenter
{
  public:
    VulkanImGuiPresenter() = default;
    ~VulkanImGuiPresenter() = default;

    VulkanImGuiPresenter(const VulkanImGuiPresenter&)            = delete;
    VulkanImGuiPresenter& operator=(const VulkanImGuiPresenter&) = delete;

    bool Initialize(VulkanContext& Ctx, VkRenderPass TargetFormat, std::string& OutDiagnostic);
    void Shutdown(VulkanContext& Ctx) noexcept;

    void SyncManagedTextures(VulkanContext& Ctx);
    void RecordDrawData(VulkanContext& Ctx, VkCommandBuffer Cmd, ImDrawData* DrawData, int Width, int Height);

  private:
    VkPipeline           GuiPipeline  = VK_NULL_HANDLE;
    VulkanImageTarget    FontAtlasImg{};
    VulkanBufferResource VertexBuffer{};
    VulkanBufferResource IndexBuffer{};
    bool                 Initialized  = false;
};

} // namespace Frontier
