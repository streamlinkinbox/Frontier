//============================================================================================================================================
//                                                      SWAPCHAINEXCHANGE.H
//============================================================================================================================================
// 🧩 Vulkan instance, surface, device, swapchain and recording-slot transport across the hardware vendor edge.

#pragma once

#if defined(_MSC_VER)
    #pragma warning(disable: 4324)
#endif

#include "InputExchange.h"
#include "RayTracingCapabilitySet.h"
#include "OrientationClassifier.h"
#include "VisibilityExchange.h"
#include <cstdint>
#include <vector>
#include <array>

struct GLFWwindow;

namespace Frontier {

class SceneStructure;
class TraversalIndex;   // GeometricRaster/TraversalIndex.h (R3 CWBVH)
class TextureIndex;     // ContentInterchange/TextureIndex.h (R4a)
static constexpr uint32_t kComputeBindingCount  = 19u;    // compute set 0: 0 out · 1 tris · 2 materials · 3 history · 4 surface · 5 normal · 6 instances · 7 luminaires · 8/9 CWBVH · 10 slabs · 11 vertices · 12 indices · 13 energy LUT · 14 sheen LUT · 15 motion · 16 prev reservoir · 17 curr reservoir · 18 Textures[] (R6; variable-count binding stays last)
static constexpr uint32_t kTextureSlotCapacity  = 1024u;  // bindless sampler2D[] size (variable-count binding; Pascal maxPerStageDescriptorSamplers ≥ 4000)
class MaterialIndex;    // ContentInterchange/MaterialIndex.h (R4a)

//------------------------------------------------------------------------------------------------------------------------
//                                              SWAPCHAIN CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

// Presentation pacing requested by the Control Centre Display tab; the swapchain maps it onto what the surface supports.
enum class PresentPacingCategory : uint32_t
{
    VerticalSyncOff      = 0,   // IMMEDIATE (tearing allowed) → MAILBOX → FIFO fallback
    VerticalSyncOn       = 1,   // FIFO (always available)
    VerticalSyncAdaptive = 2,   // FIFO_RELAXED → FIFO fallback
};

struct SwapchainConfiguration
{
    uint32_t    Width;                          // [px]  surface horizontal resolution
    uint32_t    Height;                         // [px]  surface vertical resolution
    const char* Title;                          // [-]   window title string
    bool        ValidationEnabled;              // [-]   Vulkan validation layer activation
};

//------------------------------------------------------------------------------------------------------------------------
//                             FACET STRUCTURE  (GPU SSBO — triangle geometry topology)
//
// Mechanism: three world-space vertex positions, material slot, triangle slot and the three vertex UVs, packed as a
//    contiguous 64-byte SSBO slot addressed by the CWBVH primitive index (R3). R4a replaced the stored face normal
//    with the UVs — the kernel derives the normal from the edges — so texture lookup at secondary hits needs no
//    vertex/index indirection. 🚧 R5 deletes this buffer in favour of VertexRecord/index/instance.
//------------------------------------------------------------------------------------------------------------------------

struct TriangleIndex
{
    float    VertexAlphaX,  VertexAlphaY,  VertexAlphaZ;   // [m]   vertex α world position
    float    MaterialSlot;                                   // [-]   material index (uint reinterpreted)
    float    VertexBetaX,   VertexBetaY,   VertexBetaZ;    // [m]   vertex β world position
    float    TextureGammaU;                                  // [uv]  γ u   (R4a: replaced TriangleSlot — the slot IS the array index)
    float    VertexGammaX,  VertexGammaY,  VertexGammaZ;   // [m]   vertex γ world position
    float    TextureGammaV;                                  // [uv]  γ v
    float    TextureAlphaU, TextureAlphaV;                   // [uv]  α
    float    TextureBetaU,  TextureBetaV;                    // [uv]  β
};
static_assert(sizeof(TriangleIndex) == 64u, "TriangleIndex must be 64 bytes (std430 mirror)");

// R4a: RadianceStructure (48 B material summary) is gone — materials are MaterialRecord / MaterialSlabRecord
//    (ContentInterchange/MaterialIndex.h), uploaded through UploadMaterials(const MaterialIndex&).

//------------------------------------------------------------------------------------------------------------------------
//                                    DISPATCH CONFIGURATION  (compute push constants)
//
// Mechanism: per-frame camera orientation and ReSTIR tuning scalars pushed
//    directly to the compute shader via vkCmdPushConstants — 80 bytes total.
//------------------------------------------------------------------------------------------------------------------------

struct DispatchConfiguration
{
    float    CameraOriginX,    CameraOriginY,    CameraOriginZ;  // [m]   camera world position
    float    FieldOfViewTanHalf;                                  // [-]   tan(α_FoV / 2)
    float    CameraForwardX,   CameraForwardY,   CameraForwardZ; // [-]   forward unit vector
    float    AspectRatio;                                          // [-]   width / height
    float    CameraRightX,     CameraRightY,     CameraRightZ;   // [-]   right unit vector
    float    Exposure;                                             // [-]   ACES tone-map exposure scalar
    float    CameraUpX,        CameraUpY,         CameraUpZ;     // [-]   up unit vector
    float    AmbientStrength;                                      // [-]   ambient fallback contribution
    uint32_t ViewportWidth;                                        // [px]  render width
    uint32_t ViewportHeight;                                       // [px]  render height
    uint32_t AccumulationIndex;                                    // [-]   temporal frame counter
    uint32_t ExtraCandidateCount;                                    // [-]   extra same-pixel RIS candidates (R6 row 3: renamed; true spatial reuse is the fixed kSpatialTaps cross)
    uint32_t CandidatesPerPixel;                                   // [-]   primary DI candidates per pixel
    uint32_t AlphaMaskedMaterialCount;                             // [-]   R4b: materials with MaterialFlagAlphaMask (0 = any-hit shadow rays); was TriangleCount, unused by the kernel
    uint32_t LuminaireTriangleCount;                               // [-]   emissive triangles for DI sampling
    uint32_t FeatureFlags;                                         // [bit] DispatchFeature bits
};

// Bits of DispatchConfiguration::FeatureFlags — mirror kFeature* in ReSTIRViewport.slang.
enum DispatchFeature : uint32_t
{
    DispatchFeatureGlobalIllumination = 1u << 0,
    DispatchFeatureAntiAliasing       = 1u << 1,
    DispatchFeatureAmbientFloor       = 1u << 2,   // debug fill light (R0: off by default)
    DispatchFeatureTemporalReuse      = 1u << 3,   // R6 row 2: temporal reservoir reuse
    DispatchFeatureSpatialReuse       = 1u << 4,   // R6 row 3: spatial neighbour reuse
    DispatchFeatureAliasPick          = 1u << 5    // R6 row 3: Walker-alias light pick (off = uniform, R0 identity)
};

// Mirrors `layout(push_constant) uniform ReSTIRConstants` in Engine/Shaders/ReSTIRViewport.slang.
//    vec3 + float pairs pack to 16 bytes each (4 × 16) followed by 8 uints (32) = 96 bytes.
static_assert(sizeof(DispatchConfiguration) == 96u, "DispatchConfiguration must match the shader push-constant block (96 bytes)");

//------------------------------------------------------------------------------------------------------------------------
//                                                  SWAPCHAIN EXCHANGE
//------------------------------------------------------------------------------------------------------------------------

class SwapchainExchange
{
public:
    explicit SwapchainExchange(const SwapchainConfiguration& InitialConfiguration) noexcept;
    ~SwapchainExchange() noexcept;

    SwapchainExchange(const SwapchainExchange&)            = delete;
    SwapchainExchange& operator=(const SwapchainExchange&) = delete;

    [[nodiscard]] bool  Bring()  noexcept;
    void                Retire() noexcept;

    void                        PollInput(InputExchange& TargetInput) noexcept;
    [[nodiscard]] bool          CloseRequested() const noexcept;

    void                        UploadTriangles   (const std::vector<TriangleIndex>&   Facets)    noexcept;
    void                        UploadMaterials(const MaterialIndex& Materials) noexcept;   // R4a: bindings 2 (headers) + 10 (slabs)

    // R2: the whole level becomes resident (vertices · indices · instances · clusters · materials · luminaires) and the
    //    interim kernel's flat triangle / material SSBOs are taken from the same SceneStructure — one upload, one truth.
    void                        UploadScene(const SceneStructure& Scene, const TraversalIndex& Traversal, const TextureIndex* Textures = nullptr) noexcept;
    void                        UploadTextures(const TextureIndex& Textures) noexcept;   // R4a bindless table → binding 18 (last since R6)
    void                        DestroyTextures() noexcept;
    void                        UploadShadingTables(const float* Energy, const float* Sheen, uint32_t Resolution) noexcept;   // R4b: two RGBA32F Resolution² planes (ShadingTableCodec bake) → bindings 13 / 14, once
    void                        UploadTraversal(const TraversalIndex& Traversal) noexcept;   // R3 CWBVH blobs → bindings 8-9
    void*                       SwapReservoirParity() noexcept;   // R6: flip prev/curr reservoir bindings (16/17); returns the new prev buffer (null when unavailable)

    // R2 frame front end (cull → visibility raster → HiZ → resolve) recorded before the kernel each frame.
    void                        AssignVisibilityFrame(const VisibilityFrameConfiguration& Frame) noexcept { VisibilityFrame = Frame; VisibilityFrameValid = true; }
    [[nodiscard]] const VisibilityTelemetry& QueryVisibilityTelemetry() const noexcept { return Visibility.QueryTelemetry(); }
    [[nodiscard]] uint32_t      QueryClusterCount() const noexcept { return Visibility.QueryClusterCount(); }
    [[nodiscard]] bool          QueryDrawIndirectCount() const noexcept { return DrawIndirectCountSupported; }

    void                        RecordAndPresent(const DispatchConfiguration& Dispatch) noexcept;

    void                        SignalResize() noexcept { ResizePending = true; }

    // Display settings (Step 5C). Each request is applied at the next present: pacing rebuilds the swapchain with the
    //    best supported VkPresentModeKHR; fullscreen toggles the GLFW window between the primary monitor's video mode
    //    and the remembered windowed rectangle (the resize callback then rebuilds the swapchain).
    void                        AssignPresentPacing(PresentPacingCategory Desired) noexcept;
    void                        AssignFullscreen(bool Desired) noexcept;
    [[nodiscard]] PresentPacingCategory QueryPresentPacing() const noexcept { return Pacing; }

    // Ray-tracing capability (plan v2.1 §3.4): probed once the physical device is chosen. The request comes from
    //    Slate.config.toml [render] ray_tracing_tier; the resolved tier is what the renderer must build for.
    void                        AssignRayTracingRequest(RayTracingRequestCategory Request) noexcept { RayTracingRequest = Request; }
    [[nodiscard]] const RayTracingCapabilitySet& QueryRayTracingCapabilities() const noexcept { return Capabilities; }
    [[nodiscard]] RayTracingTierCategory QueryRayTracingTier() const noexcept { return Capabilities.ResolveTier(RayTracingRequest); }
    [[nodiscard]] RayTracingRequestCategory QueryRayTracingRequest() const noexcept { return RayTracingRequest; }
    [[nodiscard]] bool          QueryFullscreen() const noexcept { return FullscreenActive; }
    [[nodiscard]] const char*   QueryPresentModeName() const noexcept;   // resolved VkPresentModeKHR, for diagnostics

    [[nodiscard]] uint32_t      QueryWidth()  const noexcept { return Configuration.Width;  }
    [[nodiscard]] uint32_t      QueryHeight() const noexcept { return Configuration.Height; }

    template<typename TargetType>
    [[nodiscard]] TargetType    Convert() const noexcept;

private:
    [[nodiscard]] bool  BringInstance()         noexcept;
    [[nodiscard]] bool  BringSurface()          noexcept;
    [[nodiscard]] bool  BringPhysicalDevice()   noexcept;
    [[nodiscard]] bool  BringLogicalDevice()    noexcept;
    [[nodiscard]] bool  BringSwapchain()        noexcept;
    [[nodiscard]] bool  BringStorageImage()     noexcept;
    [[nodiscard]] bool  BringComputePipeline()  noexcept;
    [[nodiscard]] bool  BringDescriptorSet()    noexcept;
    [[nodiscard]] bool  BringCommandRecording() noexcept;
    [[nodiscard]] bool  BringCycleSlots()       noexcept;
    [[nodiscard]] bool  BringImGui()            noexcept;
    [[nodiscard]] bool  BringVisibility()       noexcept;

    void                RetireSwapchain()       noexcept;
    [[nodiscard]] bool  RebuildSwapchain()      noexcept;
    [[nodiscard]] uint32_t ResolvePresentMode() const noexcept;   // VkPresentModeKHR as uint32_t (header stays Vulkan-free)

    void                RecordComputeCommands(uint32_t ImageOrdinal,
                                              const DispatchConfiguration& Dispatch) noexcept;
    void                WriteDescriptorSet()   noexcept;
    void                ConstructSceneBuffers() noexcept;

    [[nodiscard]] uint32_t ResolveMemoryType(uint32_t TypeMask, uint32_t PropertyMask) const noexcept;

    static void OnKey         (GLFWwindow*, int Key, int Scancode, int Action, int Mods) noexcept;
    static void OnMouseButton (GLFWwindow*, int Button, int Action, int Mods) noexcept;
    static void OnCursorMove  (GLFWwindow*, double X, double Y) noexcept;
    static void OnScroll      (GLFWwindow*, double OffsetX, double OffsetY) noexcept;
    static void OnFramebuffer (GLFWwindow*, int W, int H) noexcept;
    static void OnFocus       (GLFWwindow*, int Focused) noexcept;

    // Full Vulkan object lifetimes are owned by VulkanRecord, defined only in .cpp
    struct VulkanRecord;
    VulkanRecord*           Vulkan;             // [-]   heap-allocated Vulkan object lifetimes

    GLFWwindow*             GlfwWindow;         // [-]   GLFW window pointer
    SwapchainConfiguration  Configuration;      // [-]   runtime-tunable surface parameters
    bool                    ResizePending;       // [-]   framebuffer resize signal
    PresentPacingCategory   Pacing;              // [-]   requested pacing (default VerticalSyncOn)
    uint32_t                ResolvedPresentMode; // [-]   VkPresentModeKHR chosen at the last swapchain build
    bool                    FullscreenActive;    // [-]   window currently covers the primary monitor
    RayTracingCapabilitySet Capabilities;        // [-]   probed in BringPhysicalDevice
    VisibilityExchange      Visibility;          // [-]   R2 resident scene + cull / raster / HiZ / resolve
    bool                    TraversalResident = false;   // [-]   R3 CWBVH uploaded (kernel refuses to run without it)
    VisibilityFrameConfiguration VisibilityFrame{};
    bool                    VisibilityFrameValid = false;
    bool                    DrawIndirectCountSupported = false;   // [-] VkPhysicalDeviceVulkan12Features::drawIndirectCount
    RayTracingRequestCategory RayTracingRequest = RayTracingRequestCategory::Auto;
    int                     WindowedX, WindowedY, WindowedW, WindowedH;   // [px] rectangle to restore on leaving fullscreen

    InputExchange*          ForwardInput;        // [-]   target for GLFW callback forwarding (valid during PollInput)
    double                  PreviousCursorX;     // [px]  last known cursor horizontal position
    double                  PreviousCursorY;     // [px]  last known cursor vertical position
    bool                    CursorInitialised;   // [-]   first-movement delta suppression
    bool                    PendingInputReset;   // [-]   focus was lost; release every held key/button on next poll
};

template<>
inline bool SwapchainExchange::Convert<bool>() const noexcept
{
    return !CloseRequested();
}

template<>
inline uint32_t SwapchainExchange::Convert<uint32_t>() const noexcept
{
    return Configuration.Width;
}

} // namespace Frontier
