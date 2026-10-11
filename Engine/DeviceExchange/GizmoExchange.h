//============================================================================================================================================
//                                                       GIZMOEXCHANGE.H
//============================================================================================================================================
// 🧩 Draws the editor's transform gizmo on the GPU: the vertices GizmoFigures composed on the CPU (the 1:1 port
//    of References/Gizmo.html) become two draws — the triangle pieces and the corner quads' line edges — through
//    Engine/Shaders/GizmoRaster.{vert,frag}.spv, composited over the resolved scene inside the same overlay
//    bracket the spatial interface uses. Modelled directly on InterfaceExchange so the engine keeps one Vulkan
//    idiom: one graphics pipeline per topology, one host-visible vertex extent per cycle slot, push constants
//    for the view, no vertex input bindings (the SSBO is pulled by gl_VertexIndex).
//
//    Depth is deliberately NOT tested: like Blender's and the reference's gizmo, the grips must read over the
//    object they manipulate. Vulkan handles cross this header as void* so it stays SDK-free for CPU proofs.

#pragma once

#include <cstdint>
#include "../Editor/GizmoFigures.h"

namespace Frontier {

// The view the gizmo draws under: the visibility raster's own world → clip, plus the eye (specular-free shading
//    still wants the facing test) and the reference's directional light.
struct GizmoViewClip
{
    float ViewClip[16] = {};                              // column-major world → clip
    float EyeX = 0.0f, EyeY = 0.0f, EyeZ = 0.0f;
    float LightX = 0.4558f, LightY = 0.6838f, LightZ = 0.5698f;   // normalize(4, 6, 5), the reference's lamp
    uint32_t RenderWidth = 0u, RenderHeight = 0u;
};

class GizmoExchange
{
public:
    GizmoExchange() noexcept;
    ~GizmoExchange() noexcept;

    GizmoExchange(const GizmoExchange&)            = delete;
    GizmoExchange& operator=(const GizmoExchange&) = delete;

    // Device bring-up. ColourFormat is the presentation image's VkFormat; VertexCapacity bounds one slot's
    //    upload (triangles + strokes share it).
    [[nodiscard]] bool Bring(void* Device, void* PhysicalDevice, uint32_t CycleSlotCount,
                             uint32_t ColourFormat, uint32_t VertexCapacity) noexcept;
    void               Retire() noexcept;

    // (Re)aims the draw at the presentation image; call after every swapchain rebuild.
    [[nodiscard]] bool Resize(uint32_t Width, uint32_t Height, void* ColourView) noexcept;

    // Seats one frame's vertices into CycleSlot: the triangle list, then the line list. Call before the frame
    //    that RecordGizmo()s the same slot is submitted.
    void UploadVertices(const GizmoVertex* Triangles, uint32_t TriangleCount,
                        const GizmoVertex* Strokes, uint32_t StrokeCount, uint32_t CycleSlot) noexcept;

    // Records the two draws into Command (a VkCommandBuffer inside a COLOR_ATTACHMENT_OPTIMAL bracket, exactly
    //    where InterfaceExchange::RecordInterface runs). Draws nothing when the slot holds no vertices.
    void RecordGizmo(void* Command, uint32_t CycleSlot, const GizmoViewClip& View) noexcept;

    [[nodiscard]] bool IsReady() const noexcept { return Ready; }

private:
    struct VulkanRecord;
    VulkanRecord* Vulkan;

    [[nodiscard]] bool BringPipelines() noexcept;
    [[nodiscard]] bool BringDescriptorSets() noexcept;

    bool     Ready    = false;
    uint32_t Capacity = 0u;   // [vertices] per slot
    uint32_t Width    = 0u;
    uint32_t Height   = 0u;
};

} // namespace Frontier
