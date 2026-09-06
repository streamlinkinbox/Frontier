//============================================================================================================================================
//                                                      VISIBILITYEXCHANGE.H
//============================================================================================================================================
// 🧩 Resident scene on the GPU + the R2 frame front end: two-phase cluster cull (frustum · cone · HiZ) → indirect
//    visibility raster (D32 reverse-Z · R32_UINT visibility id · RG16F motion) → HiZ pyramid → surface resolve (thin
//    G-buffer the path kernel reads instead of tracing primary rays). Owned and driven by SwapchainExchange; every
//    Vulkan handle lives in the .cpp-only VulkanRecord so this header stays Vulkan-free for the presentation layer.
//
// Frame order (RecordFrame):
//    Cull(phase 1: last frame's visible set) → Raster(clear) → HiZ build → Cull(phase 2: all, HiZ tested) → Raster(load)
//    → SurfaceResolve → [kernel, recorded by SwapchainExchange] — see References/RestirPhaseR2-ResidentSceneVisibilityRaster.md.
//
// Coordinates: world RH Z-up (CLAUDE.md §7); the single Vulkan Y flip and the reverse-Z infinite projection live in
//    GeometricRaster/ClipProjection.h and are documented there.

#pragma once

#if defined(_MSC_VER)
    #pragma warning(disable: 4324)
#endif

#include "OrientationClassifier.h"
#include "../GeometricRaster/ClipProjection.h"
#include <cstdint>
#include <vector>

namespace Frontier {

class SceneStructure;
struct InstanceRecord;   // GeometricRaster/SceneStructure.h — 160 B std430 mirror (World + PreviousWorld + ranges)

//------------------------------------------------------------------------------------------------------------------------
//                                                     DEBUG VIEW
//------------------------------------------------------------------------------------------------------------------------
// Mirrors kDebug* in Shaders/SurfaceResolve.slang. Cycled by the debug popup (F3) and persisted as [render] debug_view.

enum class DebugViewCategory : uint32_t
{
    Off          = 0,
    Depth        = 1,
    Visibility   = 2,
    Motion       = 3,
    Cluster      = 4,
    HiZ          = 5,
    Albedo       = 6,
    Normal       = 7,
    Roughness    = 8,    // R4b
    Metalness    = 9,    // R4b
    ShadingNormal = 10,  // R4b: interpolated vertex normal
    ReservoirM   = 11,  // R6 row 3: reservoir sample count M (white ramp, saturates at 256)
    ReservoirW   = 12,  // R6 row 3: reservoir unbiased weight W (1−exp(−W·k) heat ramp)
    ReservoirAge = 13,  // R6 row 3: reservoir age in frames (ramp, saturates at 16)
    Count        = 14
};

[[nodiscard]] const char* DebugViewName(DebugViewCategory View) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                  FRAME CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

struct VisibilityFrameConfiguration
{
    CameraClipConfiguration Camera;             // [-]  eye + basis + FoV (near distance from CameraProjection)
    uint32_t                RenderWidth;        // [px] rendered region (top-left of the full-size targets)
    uint32_t                RenderHeight;       // [px]
    float                   JitterX;            // [px] sub-pixel jitter in [0,1) (0.5 = pixel centre)
    float                   JitterY;            // [px]
    uint32_t                FrameIndex;         // [-]
    DebugViewCategory       DebugView;          // [-]  ≠ Off → the resolve writes the presentation image, kernel skipped
    bool                    OcclusionCulling;   // [-]  HiZ test on (off = frustum + cone only; proof 4 toggles this)
    bool                    ConeCulling;        // [-]  normal-cone test (default off: the kernel shades both faces)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     TELEMETRY
//------------------------------------------------------------------------------------------------------------------------
// Read back from the frame that used the same cycle slot two frames ago (no stall).

struct VisibilityTelemetry
{
    uint32_t ClusterTotal      = 0u;    // [cnt] clusters tested in phase 2
    uint32_t FrustumPassed     = 0u;    // [cnt]
    uint32_t ConePassed        = 0u;    // [cnt]
    uint32_t OcclusionPassed   = 0u;    // [cnt] visible after HiZ
    uint32_t PhaseOneDraws     = 0u;    // [cnt] clusters re-drawn from last frame's set
    uint32_t PhaseTwoDraws     = 0u;    // [cnt] newly visible clusters
    uint32_t TrianglesDrawn    = 0u;    // [cnt] both phases
    float    CullMilliseconds     = 0.0f;
    float    RasterMilliseconds   = 0.0f;
    float    HiZMilliseconds      = 0.0f;
    float    ResolveMilliseconds  = 0.0f;
    float    KernelMilliseconds   = 0.0f;
    bool     Valid                = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  VISIBILITY EXCHANGE
//------------------------------------------------------------------------------------------------------------------------

class VisibilityExchange
{
public:
    VisibilityExchange() noexcept;
    ~VisibilityExchange() noexcept;

    VisibilityExchange(const VisibilityExchange&)            = delete;
    VisibilityExchange& operator=(const VisibilityExchange&) = delete;

    // Handles are opaque here (VkDevice, VkPhysicalDevice, VkQueue, VkCommandPool) so the header stays Vulkan-free.
    // TextureSlotCapacity > 0 enables the bindless table in the raster set (descriptor indexing granted); 0 = alpha mask off.
    [[nodiscard]] bool  Bring(void* Device, void* PhysicalDevice, uint32_t CycleSlotCount, bool DrawIndirectCount, uint32_t TextureSlotCapacity = 0u) noexcept;
    void                Retire() noexcept;

    // (Re)creates the render-size targets; PresentationView is the swapchain's rgba8 storage image view (debug views).
    [[nodiscard]] bool  Resize(uint32_t Width, uint32_t Height, void* PresentationView) noexcept;

    // Uploads every SceneStructure buffer (host-visible; a staging path is R7 work). Safe to call again with a new scene.
    void                UploadScene(const SceneStructure& Scene) noexcept;

    // D3 — refresh instance transforms in place, once per frame, for moving bodies.
    //
    //    UploadScene reallocates every scene buffer behind a vkDeviceWaitIdle. That is correct at load time and
    //    ruinous per frame: it stalls the whole device. This writes only the InstanceRecord rows into the existing
    //    host-visible, persistently-mapped allocation — no reallocation, no stall, no descriptor rewrite, because
    //    the VkBuffer handle never changes.
    //
    //    ⚠️ The count must match the resident scene. A caller that grows or shrinks the instance list has changed
    //    the scene, not moved it, and must go through UploadScene. Refusal (false) rather than a silent partial
    //    write, since a short write would leave stale transforms that look like physics glitches.
    //
    //    Memory is HOST_COHERENT, so no explicit flush is needed; the write must still land before the frame that
    //    reads it is submitted, which is why the caller does this before RecordAndPresent.
    [[nodiscard]] bool  RefreshInstances(const InstanceRecord* Rows, uint32_t Count) noexcept;

    // Instances resident after the last UploadScene — the bound RefreshInstances must match.
    [[nodiscard]] uint32_t QueryInstanceCount() const noexcept { return InstanceCount; }

    // R4b alpha mask in the raster: the kernel's slab SSBO (VkBuffer) and bindless table (VkSampler + VkImageView[]) are
    //    borrowed into raster bindings 6 / 7. Call after UploadScene / UploadTextures; the fragment stage reads them.
    void                AssignRasterMaterials(void* SlabBuffer, void* Sampler, const void* const* Views, uint32_t ViewCount) noexcept;

    // R6 row 3: the kernel's prev-frame reservoir buffer (VkBuffer) borrowed into resolve binding 13 for the
    //    M / W / Age debug views. Called once per frame with the same buffer the kernel reads as binding 16
    //    (SwapchainExchange::RecordAndPresent, right after the parity swap); RecordFrame writes binding 13
    //    before the resolve dispatch. Null clears the binding (views then read zeros — never dispatched unbound
    //    because RecordFrame skips the write while null).
    void                AssignReservoirView(void* PrevReservoirBuffer) noexcept;

    // Records cull → raster → HiZ → cull → raster → resolve into Command (a VkCommandBuffer). Call once per frame after
    //    the slot's fence has been waited on; the same slot's previous telemetry is read back first.
    void                RecordFrame(void* Command, uint32_t CycleSlot, const VisibilityFrameConfiguration& Frame) noexcept;

    // Kernel timing bracket (timestamps written into this slot's query pool).
    void                RecordKernelBegin(void* Command, uint32_t CycleSlot) noexcept;
    void                RecordKernelEnd(void* Command, uint32_t CycleSlot) noexcept;

    // Resources the interim kernel binds (VkImageView / VkBuffer as void*; GENERAL layout images).
    [[nodiscard]] void* QuerySurfaceView()     const noexcept;
    [[nodiscard]] void* QueryNormalView()      const noexcept;
    [[nodiscard]] void* QueryMotionView()      const noexcept;   // R6: RG16F motion (CurrentUv − PreviousUv, [0,1]) for temporal back-projection
    [[nodiscard]] void* QueryLuminaireBuffer() const noexcept;
    [[nodiscard]] void* QueryInstanceBuffer()  const noexcept;
    [[nodiscard]] void* QueryFlatTriangleBuffer() const noexcept;
    [[nodiscard]] void* QueryMaterialBuffer()  const noexcept;
    [[nodiscard]] void* QueryVertexBuffer()    const noexcept;   // R4b: kernel binding 11 (VertexRecord[])
    [[nodiscard]] void* QueryIndexBuffer()     const noexcept;   // R4b: kernel binding 12 (uint[])
    [[nodiscard]] uint32_t QueryLuminaireCount() const noexcept { return LuminaireCount; }
    [[nodiscard]] uint32_t QueryTriangleCount()  const noexcept { return TriangleCount; }
    [[nodiscard]] uint32_t QueryClusterCount()   const noexcept { return ClusterCount; }
    [[nodiscard]] const VisibilityTelemetry& QueryTelemetry() const noexcept { return Telemetry; }
    [[nodiscard]] bool  IsReady() const noexcept { return Ready && ClusterCount > 0u; }

    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    struct VulkanRecord;
    VulkanRecord*        Vulkan;

    [[nodiscard]] bool   BringPipelines() noexcept;
    [[nodiscard]] bool   BringDescriptorSets() noexcept;
    void                 RetireTargets() noexcept;
    void                 WriteDescriptorSets() noexcept;
    void                 WriteFrameConstants(uint32_t CycleSlot, uint32_t Phase, const VisibilityFrameConfiguration& Frame) noexcept;
    void                 ReadTelemetry(uint32_t CycleSlot) noexcept;

    VisibilityTelemetry  Telemetry;
    Matrix4x4            PreviousViewClip;      // [-]  last frame's unjittered world → clip
    bool                 PreviousValid   = false;
    bool                 Ready           = false;
    uint32_t             TriangleCount   = 0u;
    uint32_t             InstanceCount   = 0u;   // [cnt] resident instances; RefreshInstances must match this
    uint32_t             ClusterCount    = 0u;
    uint32_t             LuminaireCount  = 0u;
    uint32_t             Width           = 0u;
    uint32_t             Height          = 0u;
};

template<>
inline uint32_t VisibilityExchange::Convert<uint32_t>() const noexcept
{
    return ClusterCount;
}

} // namespace Frontier
