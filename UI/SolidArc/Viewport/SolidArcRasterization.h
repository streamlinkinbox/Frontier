/*====================================================================================================================================
                                                       SOLIDARCRASTERIZATION.H
====================================================================================================================================*/
// 🧩 Viewport rasterization: offscreen colour + R32 identity + depth, surface / edge / grid programs, probe and readback

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "UI/SolidArc/Document/SolidArcImagePlane.h"
#include "UI/SolidArc/Document/SolidArcScene.h"
#include "UI/SolidArc/Viewport/SolidArcCamera.h"
#include "UI/SolidArc/Viewport/SolidArcDevice.h"
#include "UI/SolidArc/Viewport/SolidArcGizmo.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier::SolidArc
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Selection-state bits per face / edge (mirrored in Shaders/SolidArcProjection.slang).
constexpr uint32_t StateSelected = 1u;
constexpr uint32_t StateActive   = 2u;
constexpr uint32_t StateHovered  = 4u;

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Host mirror of SolidArcProjection (128 bytes, column-major float matrices).
struct SolidArcProjectionBlock
{
    float ClipFromWorld[16] = {};                                                            // [-]  - Projection × view
    float ViewRight[4]      = {};                                                            // [-]  - w: orthographic flag
    float ViewUp[4]         = {};                                                            // [-]  - Camera up, w: zebra stripe count (0 off)
    float ViewForward[4]    = {};                                                            // [-]  - Eye → target
    float DisplayStroke[4]  = {};                                                            // [px] - Width, height, stroke, depth bias
};
static_assert(sizeof(SolidArcProjectionBlock) == 128, "SolidArcProjection push constant must stay 128 bytes");

struct SolidArcGridBlock
{
    float PlaneFromClip[16]  = {};                                                           // [-]   - Grid coordinates (u, v, n) ← clip
    float AlongU[4]          = {};                                                           // [0-1] - Colour of the axis along u
    float AlongV[4]          = {};                                                           // [0-1] - Colour of the axis along v
    float Reserved[4]        = {};                                                           // [-]   - Padding to 128 bytes
    float ReservedSecond[4]  = {};                                                           // [-]   - Padding to 128 bytes
};
static_assert(sizeof(SolidArcGridBlock) == 128, "SolidArcGridProjection push constant must stay 128 bytes");

struct SolidArcImagePlaneBlock
{
    float ClipFromWorld[16] = {};                                                            // [-]   - Projection × view
    float Corner[4]         = {};                                                            // [len] - Crop top-left, w: opacity
    float Right[4]          = {};                                                            // [len] - Crop width span, w: mode (0 line art)
    float Down[4]           = {};                                                            // [len] - Crop height span
    float Crop[4]           = {};                                                            // [0-1] - u0, v0, u1, v1
};
static_assert(sizeof(SolidArcImagePlaneBlock) == 128, "SolidArcImagePlaneProjection push constant must stay 128 bytes");

// One image plane to draw this frame: the uploaded picture it samples and its world parallelogram.
struct SolidArcImagePlaneDraw
{
    uint32_t              Picture = 0;                                                       // [idx] - Into the last UploadPictures list
    std::array<double, 3> Corner  = {};                                                      // [len] - Crop top-left (world)
    std::array<double, 3> Right   = {};                                                      // [len] - Crop width span
    std::array<double, 3> Down    = {};                                                      // [len] - Crop height span
    std::array<float, 4>  Crop    = { 0.0f, 0.0f, 1.0f, 1.0f };                              // [0-1] - Picture uv rectangle
    float                 Opacity = 0.6f;                                                    // [0-1] - Ink opacity
    float                 Mode    = 0.0f;                                                    // [-]   - 0 line art, 1 photograph
    bool                  FrontOnlyCondition = false;                                      // [-]   - Optional one-sided reference; evaluated per viewport
};

[[nodiscard]] bool FacesSolidArcImagePlane(const SolidArcImagePlaneDraw& Draw, const OrbitCameraFrame& Frame) noexcept;

struct SolidArcViewportStyle
{
    float Background[4]   = { 0.090f, 0.106f, 0.137f, 1.0f };                                // [0-1] - SolidArc viewport #171b23
    float EdgeStroke      = 1.5f;                                                            // [px]  - Edge stroke width
    float EdgeDepthMargin = 3.0f;                                                            // [px]  - Edge pull toward the eye (world length of this many pixels)
    bool  GridEnabled     = true;                                                            // [-]   - Drawing-plane grid
    int   GridPlane       = 0;                                                               // [-]   - 0 XY (Top/Bottom), 1 XZ (Front/Back), 2 YZ (Right/Left)
    bool  EdgesEnabled    = true;                                                            // [-]   - B-rep edges
    bool  SurfacesEnabled = true;                                                            // [-]   - B-rep faces
    float ZebraStripes    = 0.0f;                                                            // [-]   - Zebra stripes over the reflection angle (0 off, V8)
    bool  XRayEnabled     = false;                                                           // [-]   - Alt+Z: ghost faces + occluded edges, picked through
    float XRayOpacity     = 0.45f;                                                           // [0-1] - Ghost face opacity α in x-ray
    bool  SectionEnabled  = false;                                                           // [-]   - Alt+S: clip at SectionPlane, cap the solids
    std::array<double, 4> SectionPlane = { 0.0, 0.0, 1.0, 0.0 };                             // [-]   - (n, w): kept side n·P + w ≥ 0
    std::vector<SolidArcImagePlaneDraw> ImagePlanes = {};                                    // [-]   - Blueprint references, depth-tested behind the model
    SolidArcGizmoGeometry Gizmo        = {};                                                 // [-]   - Transform gizmo (CPU points, drawn last on cleared depth)
};

// One sub-viewport pane inside the shared offscreen Colour / Identity / Depth targets (Single = 1 pane, Split = 2..4 panes).
struct SolidArcRasterPane
{
    int                   OffsetX = 0;                                                       // [px] - Scissor/viewport left within Colour target
    int                   OffsetY = 0;                                                       // [px] - Scissor/viewport top within Colour target
    uint32_t              Width   = 0;                                                       // [px] - Pane width
    uint32_t              Height  = 0;                                                       // [px] - Pane height
    OrbitCameraFrame      Frame   = {};                                                      // [-]  - Camera frame for this pane
    SolidArcViewportStyle Style   = {};                                                      // [-]  - Per-pane GridPlane + scaled Gizmo
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    RASTERIZATION
//------------------------------------------------------------------------------------------------------------------------

// 💡 Borrowed device; the caller owns submission and fences (as the SultanAladin/Frontier SurfaceRasterization).
//    Record() leaves the colour target in SHADER_READ_ONLY_OPTIMAL for the interface to sample.
class SolidArcRasterization
{
public:
    SolidArcRasterization() = default;
    SolidArcRasterization(const SolidArcRasterization&)            = delete;
    SolidArcRasterization& operator=(const SolidArcRasterization&) = delete;
    ~SolidArcRasterization();

    // SampleWish: multisample count for the viewport edges and silhouettes (lowered to what the device supports for all
    // three attachments; 1 = off).
    bool Create(const SolidArcDevice& Device, std::string& Refusal, uint32_t SampleWish = 4);
    [[nodiscard]] uint32_t SampleCount() const { return uint32_t(Samples); }
    void Destroy();

    // Recreates the targets when the extent changes (waits for the device first).
    void Resize(uint32_t Width, uint32_t Height);
    void UploadScene(const SolidArcScene& Scene);
    void AssignStates(const std::vector<uint32_t>& FaceStates, const std::vector<uint32_t>& EdgeStates);
    // Replaces every image-plane texture (pictures with a refusal or no pixels get a 1 × 1 placeholder). Waits for the device.
    void UploadPictures(const std::vector<SolidArcPicture>& Pictures);
    [[nodiscard]] size_t PictureCount() const { return Pictures.size(); }
    // Replaces the matcap texture array: Layers[k] becomes layer k (the surface program samples layer = face layer − 1).
    // Pictures must share one square extent; none → a 1-layer grey placeholder. Waits for the device.
    void UploadMatcaps(const std::vector<SolidArcPicture>& Layers);
    [[nodiscard]] uint32_t MatcapLayerCount() const { return MatcapLayers; }

    // Records the viewport. ProbeX / ProbeY ≥ 0 copy that identity texel; ReadbackCondition copies both whole targets.
    void Record(VkCommandBuffer Commands, const OrbitCameraFrame& Frame, const SolidArcViewportStyle& Style, bool ReadbackCondition,
                int ProbeX, int ProbeY);

    // Records 1 … 4 split-viewport panes into the shared Colour / Identity / Depth targets with per-pane viewport + scissor.
    void RecordPanes(VkCommandBuffer Commands, const std::vector<SolidArcRasterPane>& Panes, const float Background[4], bool ReadbackCondition,
                     int ProbeX, int ProbeY);

    // Valid after the recorded work completed.
    [[nodiscard]] uint32_t        ProbedIdentity() const;
    [[nodiscard]] const uint32_t* ReadbackColour() const;                                    // RGBA8, row-major, Width × Height
    [[nodiscard]] const uint32_t* ReadbackIdentity() const;                                  // R32, row-major, Width × Height

    [[nodiscard]] VkImageView ColourView() const { return Colour.View; }
    // 💡 Changes whenever the targets are rebuilt. Compare this, not ColourView(): drivers recycle handle values, so a new
    //    view can carry the destroyed view's handle and a descriptor keyed on it would keep sampling freed memory.
    [[nodiscard]] uint64_t    TargetGeneration() const { return BuiltTargets; }
    [[nodiscard]] uint32_t    Width() const { return Colour.Width; }
    [[nodiscard]] uint32_t    Height() const { return Colour.Height; }
    [[nodiscard]] uint64_t    SceneGeneration() const { return UploadedGeneration; }

private:
    void CreateTargets(uint32_t Width, uint32_t Height);
    void DestroyTargets();
    void CreatePipelines();
    void ReleasePictures();

    struct PictureTexture
    {
        SolidArcImage   Image = {};                                                          // [-] - RGBA8 sampled texture
        VkDescriptorSet Set   = VK_NULL_HANDLE;                                              // [-] - Set 1, binding 0
    };

    const SolidArcDevice* Device = nullptr;

    SolidArcImage  Colour            = {};
    SolidArcImage  Identity          = {};
    SolidArcImage  Depth             = {};                                                   // [-] - Multisampled when Samples > 1
    SolidArcImage  ColourSamples     = {};                                                   // [-] - Multisampled colour (resolved into Colour)
    SolidArcImage  IdentitySamples   = {};                                                   // [-] - Multisampled identity (sample 0 into Identity)
    VkSampleCountFlagBits Samples    = VK_SAMPLE_COUNT_1_BIT;                                // [-] - Viewport multisample count
    SolidArcBuffer ColourReadback    = {};
    SolidArcBuffer IdentityReadback  = {};
    SolidArcBuffer ProbeReadback     = {};
    SolidArcBuffer CornerBuffer      = {};
    SolidArcBuffer SegmentBuffer     = {};
    SolidArcBuffer FaceStateBuffer   = {};
    SolidArcBuffer EdgeStateBuffer   = {};
    SolidArcBuffer SectionBuffer     = {};                                                   // [-] - 2 × float4: plane, (eye, enabled)
    SolidArcBuffer GizmoBuffer       = {};                                                   // [-] - Gizmo vertices (host-visible, grows)
    uint32_t       CornerCount       = 0;
    uint32_t       SolidCornerCount  = 0;
    uint32_t       SegmentCount      = 0;
    uint32_t       SolidSegmentCount = 0;
    uint32_t       FaceCount         = 0;
    uint32_t       EdgeCount         = 0;
    uint64_t       UploadedGeneration  = UINT64_MAX;
    uint64_t       BuiltTargets        = 0;                                                  // [-] - CreateTargets count

    VkDescriptorSetLayout BufferLayout     = VK_NULL_HANDLE;
    VkDescriptorPool      DescriptorPool   = VK_NULL_HANDLE;
    VkDescriptorSet       BufferSet        = VK_NULL_HANDLE;
    VkPipelineLayout      PipelineLayout   = VK_NULL_HANDLE;
    VkPipeline            SurfacePipeline  = VK_NULL_HANDLE;
    VkPipeline            SurfaceDepthPipeline = VK_NULL_HANDLE;                             // [-] - X-ray ①: depth + identity, no colour
    VkPipeline            SurfaceGhostPipeline = VK_NULL_HANDLE;                             // [-] - X-ray ②: front fragments blended at α
    VkPipeline            CutterDepthPipeline  = VK_NULL_HANDLE;                             // [-] - Live cutter ①: biased depth + identity
    VkPipeline            CutterGhostPipeline  = VK_NULL_HANDLE;                             // [-] - Live cutter ②: biased front faces blended at α
    VkPipeline            EdgeOccludedPipeline = VK_NULL_HANDLE;                             // [-] - X-ray ③: depth GREATER, pickable
    VkPipeline            EdgePipeline     = VK_NULL_HANDLE;                                 // [-] - Soft strokes, colour only
    VkPipeline            EdgeIdentityPipeline = VK_NULL_HANDLE;                             // [-] - Stroke cores, identity only
    VkPipeline            EdgeOccludedIdentityPipeline = VK_NULL_HANDLE;                     // [-] - X-ray ③′: hidden cores, identity only
    VkPipeline            GridPipeline     = VK_NULL_HANDLE;
    VkPipeline            GizmoPipeline    = VK_NULL_HANDLE;                                 // [-] - Gizmo handles (blended, own depth)
    VkDescriptorSetLayout PictureLayout    = VK_NULL_HANDLE;
    VkDescriptorPool      PicturePool      = VK_NULL_HANDLE;
    VkSampler             PictureSampler   = VK_NULL_HANDLE;
    VkPipelineLayout      PlaneLayout      = VK_NULL_HANDLE;
    VkPipeline            PlanePipeline    = VK_NULL_HANDLE;
    std::vector<PictureTexture> Pictures   = {};
    SolidArcImage         MatcapArray      = {};                                             // [-] - Matcap layers (2D array)
    VkDescriptorPool      MatcapPool       = VK_NULL_HANDLE;                                 // [-] - Holds MatcapSet only
    VkDescriptorSet       MatcapSet        = VK_NULL_HANDLE;                                 // [-] - Set 1 of the surface program
    uint32_t              MatcapLayers     = 0;                                              // [-] - Uploaded layer count
};

} // namespace Frontier::SolidArc
