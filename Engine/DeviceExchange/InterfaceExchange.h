//============================================================================================================================================
//                                                      INTERFACEEXCHANGE.H
//============================================================================================================================================
// 🧩 Vulkan side of the spatial interface — layer ② (GPU half). Owns one graphics state object, one host-visible
//    instance extent per cycle slot, and records exactly one draw for the whole interface:
//
//        vkCmdDraw(Command, 4, InstanceCount, 0, 0)
//
//    No vertex extent is bound at all: the vertex stage derives the quad corner from gl_VertexIndex and reads the
//    96-byte slot from an SSBO indexed by gl_InstanceIndex. The draw count is therefore independent of the figure
//    count — nine figures and nine hundred cost the same single call, which is the whole architectural claim.
//
// Recorded into the presentation colour target after the path kernel has resolved the scene, so the interface
//    composites over the lit image with premultiplied alpha. Depth is tested (so a panel is correctly occluded by
//    world geometry) but not written (so the sorted transparents composite correctly among themselves).
//
// Every Vulkan handle lives in the .cpp-only VulkanRecord, so this header stays Vulkan-free for the layers above —
//    the same rule VisibilityExchange follows.

#pragma once

#if defined(_MSC_VER)
    #pragma warning(disable: 4324)
#endif

#include <cstdint>

namespace Frontier {

struct InterfaceInstanceFigure;

//------------------------------------------------------------------------------------------------------------------------
//                                                  VIEW CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------
// The world → clip transform the raster uses. Supplied by the caller already composed (the same reverse-Z infinite
//    projection with the Vulkan Y flip that GeometricRaster/ClipProjection.h builds for the visibility raster), so
//    this exchange never duplicates the projection convention.

struct InterfaceViewClip
{
    float    ViewClip[16];        // [-]  column-major world → clip
    float    EyeX = 0.0f;         // [m]
    float    EyeY = 0.0f;         // [m]
    float    EyeZ = 0.0f;         // [m]
    uint32_t RenderWidth  = 0u;   // [px]
    uint32_t RenderHeight = 0u;   // [px]

    // ⑦ Irradiance reaching the panel, so albedo-side elements sit correctly in the room's light. Defaults to 1.0
    //    (fully lit), which reproduces the old behaviour exactly for any caller that does not set it.
    float    AmbientRed   = 1.0f; // [-]
    float    AmbientGreen = 1.0f; // [-]
    float    AmbientBlue  = 1.0f; // [-]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       METRICS
//------------------------------------------------------------------------------------------------------------------------

struct InterfaceDrawMetrics
{
    uint32_t InstanceCount = 0u;    // [cnt] figures submitted last frame
    uint32_t DrawCount     = 0u;    // [cnt] the acceptance number — 1 while anything is drawn
    uint32_t UploadBytes   = 0u;    // [B]   instance extent written last frame
    float    Milliseconds  = 0.0f;  // [ms]  GPU time between the bracketing timestamps
    bool     Valid         = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  INTERFACE EXCHANGE
//------------------------------------------------------------------------------------------------------------------------

class InterfaceExchange
{
public:
    InterfaceExchange() noexcept;
    ~InterfaceExchange() noexcept;

    InterfaceExchange(const InterfaceExchange&)            = delete;
    InterfaceExchange& operator=(const InterfaceExchange&) = delete;

    // Handles are opaque (VkDevice, VkPhysicalDevice) so the header stays Vulkan-free. ColourFormat / DepthFormat
    //    are VkFormat values; they must match the targets RecordInterface writes into.
    [[nodiscard]] bool Bring(void* Device, void* PhysicalDevice, uint32_t CycleSlotCount,
                             uint32_t ColourFormat, uint32_t DepthFormat, uint32_t FigureCapacity = 1024u) noexcept;
    void               Retire() noexcept;

    // (Re)binds the targets the interface composites onto. ColourView is the resolved scene image (GENERAL layout);
    //    DepthView may be null, in which case the interface draws without a depth test and reports that choice.
    [[nodiscard]] bool Resize(uint32_t Width, uint32_t Height, void* ColourView, void* DepthView) noexcept;

    // Copies the sequence's instance span into this slot's host-visible extent. Call once per frame, before
    //    RecordInterface, after the slot's fence has been waited on.
    void UploadInstances(const InterfaceInstanceFigure* Instances, uint32_t Count, uint32_t CycleSlot) noexcept;

    // Records the single draw into Command (a VkCommandBuffer).
    void RecordInterface(void* Command, uint32_t CycleSlot, const InterfaceViewClip& View) noexcept;

    [[nodiscard]] const InterfaceDrawMetrics& QueryMetrics() const noexcept { return Metrics; }
    [[nodiscard]] bool  IsReady()          const noexcept { return Ready; }
    [[nodiscard]] bool  IsDepthTested()    const noexcept { return DepthTested; }
    [[nodiscard]] uint32_t QueryCapacity() const noexcept { return Capacity; }

    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    struct VulkanRecord;
    VulkanRecord* Vulkan;

    [[nodiscard]] bool BringPipeline() noexcept;
    [[nodiscard]] bool BringDescriptorSets() noexcept;
    void               WriteDescriptorSets() noexcept;
    void               WriteViewConstants(uint32_t CycleSlot, const InterfaceViewClip& View) noexcept;

    InterfaceDrawMetrics Metrics;
    bool     Ready        = false;
    bool     DepthTested  = false;
    uint32_t Capacity     = 0u;
    uint32_t Width        = 0u;
    uint32_t Height       = 0u;
    uint32_t PendingCount = 0u;
};

template<>
inline uint32_t InterfaceExchange::Convert<uint32_t>() const noexcept
{
    return Metrics.InstanceCount;
}

} // namespace Frontier
