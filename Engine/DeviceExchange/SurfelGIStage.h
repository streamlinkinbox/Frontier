//============================================================================================================================================
// 📦 Engine/DeviceExchange/SurfelGIStage.h — host owner of the surfel-GI compute stage (Thread E, RenderPath == 1)
//============================================================================================================================================
// Self-contained companion to SwapchainExchange. It owns:
//    • three storage buffers (SSBOs) — the surfel field, the grid head table, the per-surfel next-links;
//    • the CPU mirror of the surfel field + the HOST-SIDE hash-grid build that fills the two link buffers;
//    • three compute pipelines built from the delivered SPIR-V — SurfelIrradianceUpdate, SurfelCommit, SurfelGIResolve;
//    • the recording of the three vkCmdDispatch calls (update → commit → resolve) with the barriers between them;
//    • per-frame surfel logging (spawned / live / recycled / coverage), as the task requires.
//
// Everything device-specific is passed in through SurfelStageInit so this file compiles against the engine's existing
//    Vulkan objects without reaching into SwapchainExchange internals. The stage owns its short-lived descriptor pool:
//    it must never consume the ReSTIR pool's one descriptor set or leave dangling CWBVH bindings after a scene reload.
//
// The binding contract MUST match the shaders in ../Shaders:
//    Update  : b0 Surfels(rw)  b1 GridHead(ro)  b2 GridNext(ro)  b8 CwbvhNodes  b9 CwbvhTris   push SurfelUpdateConstants
//    Commit  : b0 Surfels(rw)                                                                   push {uvec4 Counts}
//    Resolve : b0 Output(w) b1 Surface(ro) b2 Normal(ro) b3 Albedo(ro) b4 MaterialAux(ro)
//              b8 CwbvhNodes b9 CwbvhTris  b10 Surfels(ro) b11 GridHead(ro) b12 GridNext(ro)  push ResolveConstants
//------------------------------------------------------------------------------------------------------------------------

#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{
    // 80 B — mirrors `struct GpuSurfel` in SurfelIrradianceUpdate.slang / SurfelCommit.slang (std430).
    struct GpuSurfel
    {
        float PositionRadius[4];   // xyz world position, w world-space radius
        float NormalAge[4];        // xyz unit normal, w age (frames)
        float Albedo[4];           // xyz diffuse albedo, w alive flag (>0.5)
        float Irradiance[4];       // xyz committed E
        float IrradianceNew[4];    // xyz Jacobi target
    };
    static_assert(sizeof(GpuSurfel) == 80, "GpuSurfel must match the std430 shader record (80 B)");

    // A primary-surface sample the host bins into surfels. Feed one per screen tile (from a coarse SurfaceImage
    //    readback) or per scene-geometry sample; if none are supplied the field simply ages and holds.
    struct SurfaceSample
    {
        float Position[3];
        float Normal[3];
        float Albedo[3];
    };

    struct SurfelStageInit
    {
        VkDevice                         Device            = VK_NULL_HANDLE;
        VkPhysicalDeviceMemoryProperties MemoryProperties  {};

        // Shared acceleration structure blobs — the SAME buffers the ReSTIR kernel binds at 8/9.
        VkBuffer                         CwbvhNodeBuffer   = VK_NULL_HANDLE;
        VkBuffer                         CwbvhLeafBuffer   = VK_NULL_HANDLE;

        // G-buffer + present views (GENERAL layout storage images produced by VisibilityRaster/SurfaceResolve).
        VkImageView                      OutputImageView   = VK_NULL_HANDLE;   // rgba8   (present target)
        VkImageView                      SurfaceImageView  = VK_NULL_HANDLE;   // rgba32f (world pos + vis id)
        VkImageView                      NormalImageView   = VK_NULL_HANDLE;   // rgba16f

        // ── The scene, as the ReSTIR kernel sees it ─────────────────────────────────────────────────────────────
        // Both surfel passes now decode the real hit triangle and evaluate the real OpenPBR slab, so they need the
        //    same six scene SSBOs, the same two shading LUTs and the same bindless texture table the kernel binds.
        //    These are BORROWED handles — SwapchainExchange owns them and rebinds the stage whenever they change.
        //    Before this they were absent, and the passes approximated the material instead of reading it.
        VkBuffer                         TriangleBuffer    = VK_NULL_HANDLE;   // GpuTriangle[]     — flat primitives
        VkBuffer                         MaterialBuffer    = VK_NULL_HANDLE;   // GpuMaterial[]     — slab headers
        VkBuffer                         InstanceBuffer    = VK_NULL_HANDLE;   // GpuInstance[]     — transforms + slab index
        VkBuffer                         SlabBuffer        = VK_NULL_HANDLE;   // GpuMaterialSlab[] — the OpenPBR parameters
        VkBuffer                         VertexBuffer      = VK_NULL_HANDLE;   // GpuVertex[]       — normals, tangents, UVs
        VkBuffer                         IndexBuffer       = VK_NULL_HANDLE;   // uint[]            — per-instance indices
        VkSampler                        TableSampler      = VK_NULL_HANDLE;   // clamped bilinear for the two LUTs
        VkImageView                      EnergyLutView     = VK_NULL_HANDLE;   // binding 13 — Kulla–Conty E(μ,α)
        VkImageView                      SheenLutView      = VK_NULL_HANDLE;   // binding 14 — LTC sheen table
        VkSampler                        TextureSampler    = VK_NULL_HANDLE;   // repeat trilinear for the bindless table
        const VkImageView*               TextureViews      = nullptr;          // binding 31 — the bindless table itself
        uint32_t                         TextureCount      = 0u;               // [-] entries in TextureViews
        uint32_t                         TextureCapacity   = 0u;               // [-] 0 ⇒ no descriptor indexing on this device

        uint32_t                         MaxSurfels        = 262144u;          // pool ceiling (SPAWN_BUDGET grows toward it)
        uint32_t                         GridHashSize      = 131072u;          // hash table cells (prime-ish, > live surfels)
        float                            GridCellSize      = 0.09f;            // world cell size (≈ RMAX; scene-scaled by caller)
        std::string                      SpirvDirectory    = "Engine/Shaders"; // where SurfelIrradianceUpdate.spv etc. live
    };

    // Per-frame push payload the caller fills from its scene/camera/sky (units match the shaders).
    struct SurfelFrameParams
    {
        float    SunDirection[3] = {0, 1, 0};   // TO the sun, unit
        float    SunRadiance     = 0.0f;
        float    SunColour[3]    = {1, 1, 1};
        float    SkyAmbient[3]   = {0, 0, 0};
        float    GridOrigin[3]   = {0, 0, 0};   // world origin of the hash grid (min corner of the live AABB)
        float    CameraEye[3]    = {0, 0, 0};
        float    Exposure        = 1.0f;
        float    FireflyClamp    = 8.0f;
        float    AgeCap          = 64.0f;
        uint32_t RayCount        = 8u;
        uint32_t FrameIndex      = 0u;
        uint32_t FeatureFlags    = 0u;          // DispatchConfiguration::FeatureFlags (kFeature* mirror)
        uint32_t ReflectionMode  = 1u;          // 0 Off / 1 Sky / 2 Raytraced
        uint32_t RenderWidth     = 0u;
        uint32_t RenderHeight    = 0u;
    };

    class SurfelGIStage
    {
    public:
        bool Bring(const SurfelStageInit& Init) noexcept;               // allocate SSBOs + build pipelines/sets
        void Destroy() noexcept;

        // Host-side topology maintenance: spawn/recycle from supplied surface samples up to the budget and rebuild
        //    the CPU hash grid. The GPU owns age and irradiance once resident, so steady frames do not re-upload the
        //    seed field or reset its running mean. Logs spawned/live/recycled/coverage; call before RecordFrame().
        void UpdateField(const std::vector<SurfaceSample>& Samples, const SurfelFrameParams& Params) noexcept;

        // Record update → commit → resolve into an already-begun command buffer, with the two compute→compute
        //    barriers. Bind order and push blocks match the shaders. Returns false without writing when the field
        //    has no live samples, so the caller can retain its normal renderer as a safe fallback.
        [[nodiscard]] bool RecordFrame(VkCommandBuffer Command, const SurfelFrameParams& Params) noexcept;

        [[nodiscard]] bool IsReady() const noexcept { return I.Device != VK_NULL_HANDLE && UpdatePipeline != VK_NULL_HANDLE && ResolvePipeline != VK_NULL_HANDLE; }
        uint32_t LiveSurfelCount() const noexcept { return LiveCount; }

    private:
        bool CreateBuffers() noexcept;
        bool CreatePipelines() noexcept;
        bool WriteDescriptors() noexcept;
        void UploadBuffers() noexcept;                                  // CPU topology mirror → staging SSBOs, only after a spawn/recycle

        SurfelStageInit          I{};
        // SSBOs (device local) + host-visible staging mirrors.
        VkBuffer                 SurfelBuffer = VK_NULL_HANDLE, SurfelStage = VK_NULL_HANDLE;
        VkBuffer                 HeadBuffer   = VK_NULL_HANDLE, HeadStage   = VK_NULL_HANDLE;
        VkBuffer                 NextBuffer   = VK_NULL_HANDLE, NextStage   = VK_NULL_HANDLE;
        VkDeviceMemory           SurfelMem = VK_NULL_HANDLE, SurfelStageMem = VK_NULL_HANDLE;
        VkDeviceMemory           HeadMem   = VK_NULL_HANDLE, HeadStageMem   = VK_NULL_HANDLE;
        VkDeviceMemory           NextMem   = VK_NULL_HANDLE, NextStageMem   = VK_NULL_HANDLE;

        VkDescriptorPool         DescriptorPool = VK_NULL_HANDLE;             // private: 3 sets / 11 SSBO / 5 storage-image descriptors
        VkDescriptorSetLayout    UpdateLayout = VK_NULL_HANDLE, CommitLayout = VK_NULL_HANDLE, ResolveLayout = VK_NULL_HANDLE;
        VkPipelineLayout         UpdatePipeLayout = VK_NULL_HANDLE, CommitPipeLayout = VK_NULL_HANDLE, ResolvePipeLayout = VK_NULL_HANDLE;
        VkPipeline               UpdatePipeline = VK_NULL_HANDLE, CommitPipeline = VK_NULL_HANDLE, ResolvePipeline = VK_NULL_HANDLE;
        VkDescriptorSet          UpdateSet = VK_NULL_HANDLE, CommitSet = VK_NULL_HANDLE, ResolveSet = VK_NULL_HANDLE;

        // CPU mirror of the field + grid.
        std::vector<GpuSurfel>   HostSurfels;
        std::vector<int32_t>     HostGridHead;                          // per-cell first surfel (+1; 0 = empty)
        std::vector<int32_t>     HostGridNext;                          // per-surfel next in chain (+1; 0 = end)
        uint32_t                 LiveCount = 0u;
        uint64_t                 FrameCounter = 0u;
        bool                     FieldUploadPending = false;             // never overwrite GPU irradiance once the stable field is resident
    };
}
