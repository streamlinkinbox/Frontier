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
#include <string>
#include <vector>
#include <functional>
#include <array>

struct GLFWwindow;

namespace Frontier {

class SceneStructure;
class TraversalIndex;   // GeometricRaster/TraversalIndex.h (R3 CWBVH)
class TextureIndex;     // ContentInterchange/TextureIndex.h (R4a)
// R7 à-trous levels. Five doublings reach an 81x81 pixel footprint (1+2+4+8+16 taps either side of centre) for
//    5 x 25 taps instead of 6561 — the whole point of the "with holes" formulation.
static constexpr uint32_t kDenoiseLevelCount    = 5u;   // CEILING: descriptor sets and images are allocated for
                                                       //    this many levels. R10 #8: how many actually DISPATCH is
                                                       //    tier-keyed (DispatchConfiguration::DenoiseLevelCount),
                                                       //    because the last level buys 5-9% less error for a full
                                                       //    screen dispatch, two barriers and ~1.8M taps.

// A6b/A7c adaptive-exposure metering. These mirror LuminanceReduce.slang; the gate checks they still agree.
//    A HISTOGRAM rather than a running sum, because a percentile of the frame's own distribution has no
//    absolute constant in it and therefore behaves identically at noon and at midnight. The fixed 1e-2 cd/m²
//    floor it replaced could not: below it the meter went blind, and a night sky is entirely below it.
static constexpr uint32_t kLuminanceHistogramBins = 256u;
static constexpr float    kLuminanceLog2Low       = -30.0f;   // 1e-9 cd/m², below anything the renderer makes
static constexpr float    kLuminanceLog2High      =  30.0f;   // 1e9 cd/m², above the sun's own disc
// 🔴 The exposure is anchored to the frame's MEDIAN and averaged over everything within a few stops of it.
//    It used to be a percentile window — the middle 75 % — and a percentile cannot tell a bright outlier from a
//    bright subject, because both are just "the top of the distribution".
//
//    That distinction is the whole problem. A Cornell frame with the roof oculus in shot is two populations: a
//    room near 1 cd/m² and a hole showing sky at thousands. Walking about changes how much of the frame the
//    hole covers, the bright mode slid into and out of the average, and the reading swung 2 to 4.7 stops as the
//    camera moved. Exposure is global, so the SKY pumped along with the room — which is the tell, because a sky
//    whose brightness depends on where the camera stands is not a sky problem at all.
//
//    Widening the percentile fixed the pumping and broke the outdoor scene instead: sunlit ground is 41 % of
//    that frame and 2.3 stops above the sky, so trimming enough to exclude a hole in a roof also excluded the
//    ground, and a landscape metered as its own sky.
//
//    A distance in STOPS separates them where an area fraction cannot. The median says where the scene is;
//    anything more than six stops from it is a light source in shot rather than part of the scene, whatever
//    fraction of the frame it happens to cover. Measured across all three cases: pumping 0.00 stops, framing
//    spread better than the original percentile, and the landscape metered at 3075 rather than 1909.
//
//    ⚠️ Six is bounded on both sides. Three to eight all measure identically, so it is not a tuned number; at
//    twelve the oculus sky comes back inside the window and the pumping returns at 2.08 stops.
static constexpr float    kLuminanceMedianStops   = 6.0f;
static constexpr uint32_t kLuminanceHistogramBytes = kLuminanceHistogramBins * 4u;

static constexpr uint32_t kComputeBindingCount  = 26u;    // compute set 0: 0 out · 1 tris · 2 materials · 3 history · 4 surface · 5 normal · 6 instances · 7 luminaires · 8/9 CWBVH · 10 slabs · 11 vertices · 12 indices · 13 energy LUT · 14 sheen LUT · 15 motion · 16 prev reservoir · 17 curr reservoir · 18 history normal+depth (R7a) · 19 luminance moments (R7) · 20 denoise input (R7) · 21 sky record · 22/24 retired holes · 25 Textures[] (variable-count binding MUST stay last — Vulkan requires it on the highest binding number)
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

//------------------------------------------------------------------------------------------------------------------------
//                          TRIANGLE SPAN RECORD  (CPU only — object identity over a soup)
//
// Mechanism: a TriangleIndex soup carries no object identity — Floor, Ceiling and Back Wall share one material
//    and would decode as one instance. The builders record one span per Append call (name + dynamic flag authored
//    at the call site); SceneCodec::Encode turns each span into a named glTF node, so the decode carries one
//    placement per scene object and the outliner walks the live scene instead of a parallel hand-typed table.
//    Never uploaded; dies with TriangleIndex in R5.
//------------------------------------------------------------------------------------------------------------------------

struct TriangleSpanRecord
{
    uint32_t    FirstTriangle = 0u;      // [idx] first triangle of the object in the builder's soup
    uint32_t    TriangleCount = 0u;      // [cnt]
    std::string Name;                    // [-]   display name ("Tall Box")
    bool        Dynamic = false;         // [-]   the object moves (--animate / physics drive it)
};

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

    // ── Reserve ──────────────────────────────────────────────────────────────────────────────────────────────────
    // ⚠️ Push constants stay within 128 bytes, which is Vulkan's GUARANTEED MINIMUM. Growing past it works on a
    //    card offering 256 and fails on one offering only the guarantee — a defect that appears on someone
    //    else's machine. Anything that does not fit goes into a uniform buffer rather than growing this block.
    float    ColourSaturation;                                     // [-] A7d: 1 in daylight, 0 under starlight
    uint32_t SpatialTapCount;                                      // [-] spatial-reuse neighbours per pixel (0 = cross off); taken from the reserve, block still 128 B
    uint32_t DenoiseLevelCount;                                    // [-] a-trous levels to dispatch, 1..kDenoiseLevelCount (tier-keyed); also from the reserve
    uint32_t PushReserve[5];                                       // [-] keeps the block 128 B and 16-B aligned
};

// Bits of DispatchConfiguration::FeatureFlags — mirror kFeature* in ReSTIRViewport.slang.
enum DispatchFeature : uint32_t
{
    DispatchFeatureGlobalIllumination = 1u << 0,
    DispatchFeatureAntiAliasing       = 1u << 1,
    DispatchFeatureAmbientFloor       = 1u << 2,   // debug fill light (R0: off by default)
    DispatchFeatureTemporalReuse      = 1u << 3,   // R6 row 2: temporal reservoir reuse
    DispatchFeatureSpatialReuse       = 1u << 4,   // R6 row 3: spatial neighbour reuse
    DispatchFeatureAliasPick          = 1u << 5,   // R6 row 3: Walker-alias light pick (off = uniform, R0 identity)
    DispatchFeatureTemporalReprojection = 1u << 6, // R7a: reproject the running mean through the R2 motion vectors
    DispatchFeatureDenoise            = 1u << 7,   // R7:  à-trous filter runs; the kernel defers the tone map to it
};

// Mirrors `layout(push_constant) uniform ReSTIRConstants` in Engine/Shaders/ReSTIRViewport.slang.
//    vec3 + float pairs pack to 16 bytes each (4 × 16) followed by 8 uints (32) = 96, plus the reserve tail (32).
//    128 bytes is the minimum every Vulkan implementation must offer, so this is now exactly full: anything
//    further has to become a uniform buffer.
static_assert(sizeof(DispatchConfiguration) == 128u, "DispatchConfiguration must match the shader push-constant block (128 bytes)");

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
    // Escape no longer closes the window from the key callback: a text field needs it to abandon an edit, and the
    //    callback cannot see whether one is open. The host asks for the close instead, once it knows nothing is
    //    holding the keyboard.
    void                        RequestClose() noexcept;

    void                        UploadTriangles   (const std::vector<TriangleIndex>&   Facets)    noexcept;
    void                        UploadMaterials(const MaterialIndex& Materials) noexcept;   // R4a: bindings 2 (headers) + 10 (slabs)

    // R2: the whole level becomes resident (vertices · indices · instances · clusters · materials · luminaires) and the
    //    interim kernel's flat triangle / material SSBOs are taken from the same SceneStructure — one upload, one truth.
    void                        UploadScene(const SceneStructure& Scene, const TraversalIndex& Traversal, const TextureIndex* Textures = nullptr) noexcept;

    // D3: per-frame instance transform refresh (no reallocation, no device stall). See
    //    VisibilityExchange::RefreshInstances. Returns false if the count no longer matches the resident scene.
    [[nodiscard]] bool          RefreshInstances(const InstanceRecord* Rows, uint32_t Count) noexcept
    { return Visibility.RefreshInstances(Rows, Count); }
    [[nodiscard]] uint32_t      QueryInstanceCount() const noexcept { return Visibility.QueryInstanceCount(); }
    void                        UploadTextures(const TextureIndex& Textures) noexcept;   // R4a bindless table → binding 18 (last since R6)
    void                        DestroyTextures() noexcept;
    void                        UploadShadingTables(const float* Energy, const float* Sheen, uint32_t Resolution) noexcept;   // R4b: two RGBA32F Resolution² planes (ShadingTableCodec bake) → bindings 13 / 14, once
    void                        UploadTraversal(const TraversalIndex& Traversal) noexcept;   // R3 CWBVH blobs → bindings 8-9

    // D5: per-frame acceleration-structure refresh after a refit. No reallocation and no device stall, so unlike
    //    UploadTraversal this is safe every frame. Also re-uploads the flat triangles, because the kernel resolves
    //    material and normal through them and they must not lag the structure. False if a blob outgrew its
    //    allocation, in which case the caller should fall back to a full UploadTraversal.
    [[nodiscard]] bool          RefreshTraversal(const TraversalIndex& Traversal, const std::vector<TriangleIndex>& Facets) noexcept;
    // Celestial sky and weather record → binding 21, safe every frame: a memcpy into the persistently mapped
    //    uniform buffer, no reallocation and no descriptor rewrite. DeviceExchange must not include
    //    DisplayPresentation (it is the layer below it), so the caller packs with SkyConstantRecord and hands over
    //    plain bytes. False when the bytes are null, the size is not 320, or the buffer does not exist yet; the
    //    previous contents stand, so a refusal degrades to a stale sky, not a tear.
    [[nodiscard]] bool          RefreshSky(const void* Bytes, uint32_t ByteCount) noexcept;
    // Celestial moon record → binding 22, safe every frame: the same arrangement as the sky record above, 288
    //    bytes packed by MoonConstantRecord/PackMoonConstants. False when the bytes are null, the size is not
    //    288, or the buffer does not exist yet; the previous contents stand, so a refusal degrades to stale
    //    moons — and zero is no moons at all (MoonControl.x = 0), which is the kernel's early-out.
    [[nodiscard]] bool          RefreshMoons(const void* Bytes, uint32_t ByteCount) noexcept;
    // Celestial post record → binding 24, safe every frame: the same arrangement as the records above, 128
    //    bytes packed by PostConstantRecord/PackPostConstants. False on null bytes, wrong size, or no buffer;
    //    the previous contents stand. Zero is everything off (stars, flare, bow), the kernel's early-out.
    [[nodiscard]] bool          RefreshPost(const void* Bytes, uint32_t ByteCount) noexcept;
    // Star tables → binding 23, once after the catalogue loads: 1 024 cells of 8 B then StarCount stars of
    //    32 B, re-pointing the binding at the reallocated buffer. Skipped (never called) when the catalogue
    //    is empty — the bring-up zeros stand. A refused upload keeps the previous tables, never a hole.
    void                        UploadStarTables(const void* CellBytes, uint32_t CellCount,
                                                 const void* StarBytes, uint32_t StarCount) noexcept;
    void*                       SwapReservoirParity() noexcept;   // R6: flip prev/curr reservoir bindings (16/17); returns the new prev buffer (null when unavailable)

    // R2 frame front end (cull → visibility raster → HiZ → resolve) recorded before the kernel each frame.
    void                        AssignVisibilityFrame(const VisibilityFrameConfiguration& Frame) noexcept { VisibilityFrame = Frame; VisibilityFrameValid = true; }

    // R10 — the GI-off shadow stage's per-frame settings (technique, map side, taps). Only consulted when Global
    //    Illumination is off: with GI on the ReSTIR kernel owns light visibility and no shadow map is rasterised.
    //    The taps themselves are placed by VisibilityExchange from the resident emitters, so the caller supplies
    //    only the tier's choices and the Control Centre's resolution override.
    void                        AssignShadowFrame(const ShadowFrameConfiguration& Shadow) noexcept { ShadowFrame = Shadow; ShadowFrameValid = true; }
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

    //--------------------------------------------------------------------------------------------------------------------
    // Device seam — the handles a compositing overlay (SpatialInterface) needs to record into this frame.
    //--------------------------------------------------------------------------------------------------------------------
    // Returned as void*/uint32_t so this header stays Vulkan-free, exactly like InterfaceExchange::Bring accepts them.
    //    A caller that wants to draw over the finished scene registers an OverlaySequence below; these accessors exist
    //    so it can Bring() and Resize() its own resources against the same device and targets.
    //
    // ⚠️ Lifetime: every handle here is owned by SwapchainExchange and is invalidated by a swapchain rebuild (resize,
    //    present-pacing change, fullscreen toggle). QueryTargetGeneration() increments on every rebuild — an overlay
    //    must compare it each frame and re-Resize when it changes, or it will render into destroyed image views.

    [[nodiscard]] void*    QueryDevice()          const noexcept;   // VkDevice
    [[nodiscard]] void*    QueryPhysicalDevice()  const noexcept;   // VkPhysicalDevice
    [[nodiscard]] void*    QueryColourView()      const noexcept;   // VkImageView of the resolved scene image (GENERAL)
    [[nodiscard]] void*    QueryDepthView()       const noexcept;   // VkImageView, or null when no depth target exists
    [[nodiscard]] uint32_t QueryColourFormat()    const noexcept;   // VkFormat of the above colour view
    [[nodiscard]] uint32_t QueryDepthFormat()     const noexcept;   // VkFormat, or VK_FORMAT_UNDEFINED when depthless
    [[nodiscard]] uint32_t QueryCycleSlotCount()  const noexcept;   // frames in flight — the overlay sizes rings to this
    [[nodiscard]] uint32_t QueryCycleSlot()       const noexcept;   // slot the frame now being recorded belongs to

    // A6b. The average LOG luminance the reduction pass measured, or a negative value when no measurement is
    //    available yet. Reads the slot the GPU has already finished with, so it never stalls: the value is one
    //    or two frames stale, which is invisible against adaptation time constants measured in seconds.
    [[nodiscard]] float QueryAverageLogLuminance() const noexcept;

    [[nodiscard]] uint32_t QueryTargetGeneration() const noexcept { return TargetGeneration; }

    //--------------------------------------------------------------------------------------------------------------------
    // Overlay seam — one callback recorded after the scene, before ImGui.
    //--------------------------------------------------------------------------------------------------------------------
    // The engine deliberately knows nothing about what is drawn: it hands back the command buffer and the slot, and the
    //    project records whatever it likes. This keeps SpatialInterface out of DeviceExchange (a project may compose
    //    figures; the engine may not know what a figure means) while still giving the overlay a place in the frame.
    //    Command is a VkCommandBuffer. Called once per presented frame; never called for a skipped frame.
    using OverlaySequence = std::function<void(void* Command, uint32_t CycleSlot)>;
    void AssignOverlaySequence(OverlaySequence Sequence) noexcept { Overlay = std::move(Sequence); }
    void ClearOverlaySequence() noexcept { Overlay = nullptr; }

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
    [[nodiscard]] bool  BringDenoisePipeline()  noexcept;   // R7: à-trous filter, its own small descriptor set
    [[nodiscard]] bool  BringLuminanceReduction() noexcept; // A6b: the average-log-luminance pass
    [[nodiscard]] bool  BringSkyRecord() noexcept;   // Celestial sky uniform buffer (binding 21) — before BringDescriptorSet, which writes it
    [[nodiscard]] bool  BringMoonRecord() noexcept;  // Celestial moon uniform buffer (binding 22) — beside the sky record, same ordering rule
    [[nodiscard]] bool  BringPostRecord() noexcept;  // Celestial post uniform buffer (binding 24) — with the records, same ordering rule
    [[nodiscard]] bool  BringStarTables() noexcept;  // Star tables storage buffer (binding 23) — cells alone at bring-up, UploadStarTables grows it
    // Rewritten on every resize: this set binds HistoryImageView, which a resize destroys and recreates.
    void                WriteLuminanceDescriptors() noexcept;
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
    static void OnCharacter   (GLFWwindow*, unsigned int Codepoint) noexcept;
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
    bool                    TraversalResident = false;
    uint64_t                TraversalNodeCapacity = 0u;   // [B] allocation size, so a refit refresh cannot overrun
    uint64_t                TraversalLeafCapacity = 0u;   // [B]   // [-]   R3 CWBVH uploaded (kernel refuses to run without it)
    VisibilityFrameConfiguration VisibilityFrame{};
    bool                    VisibilityFrameValid = false;
    ShadowFrameConfiguration ShadowFrame{};        // R10: GI-off shadow settings (tier technique + resolution override)
    bool                    ShadowFrameValid = false;

    OverlaySequence         Overlay;                       // [-]   optional per-frame overlay recorder (project-owned)
    uint32_t                TargetGeneration = 0u;         // [cnt] bumped on every swapchain rebuild; overlays re-Resize on change
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
