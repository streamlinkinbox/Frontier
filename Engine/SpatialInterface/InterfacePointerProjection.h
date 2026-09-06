//============================================================================================================================================
//                                                  INTERFACEPOINTERPROJECTION.H
//============================================================================================================================================
// 🧩 P2 — projects a world-space ray onto the interface's figures and reports which one it struck, in that
//    figure's own plane coordinates. This is the geometry half of interaction and nothing else: it answers
//    "which figure, and where on it", never "what does that mean".
//
//    Engine ⇄ project seam (CLAUDE.md §6): the engine knows a figure is a rounded rectangle with a half extent and
//    a world placement, so it can intersect a ray with it. It does not know that one rectangle is a volume knob
//    and another is a mute button — the project reads the ordinal and decides. That is why this returns an
//    ordinal and a local coordinate rather than an action.
//
//    Only figures with PointerTarget set are considered. That flag has been reserved in InterfaceFigure since P0
//    for exactly this, so no layout changes and no GPU slot moves: interaction is a CPU-side query over data the
//    renderer already composes.
//
//    Method: each figure is a flat rounded rectangle lying in its own local XY plane. Transform the ray into that
//    plane with the inverse of the figure's world placement (a rotation with uniform scale, so the inverse is the
//    transpose over the scale), intersect with Z = 0, then test the hit against the rounded-rectangle extent. The
//    nearest hit along the ray wins, so a knob lifted off its bed is picked before the bed behind it.

#pragma once

#include "InterfaceStructure.h"
#include "InterfaceSequence.h"

#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    POINTER RAY
//------------------------------------------------------------------------------------------------------------------------
// A world-space ray. The project builds this from the cursor and the camera; the engine never touches input.

struct PointerRay
{
    float OriginX = 0.0f, OriginY = 0.0f, OriginZ = 0.0f;         // [m]
    float DirectionX = 0.0f, DirectionY = 1.0f, DirectionZ = 0.0f; // [-] expected unit length
    float Reach = 100.0f;                                          // [m] ignore hits beyond this
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   POINTER CONTACT
//------------------------------------------------------------------------------------------------------------------------
// What the ray struck. Valid = false means nothing was hit and every other field is meaningless.

struct PointerContact
{
    bool     Valid    = false;
    uint32_t Ordinal  = 0u;        // [-]  figure struck, index into InterfaceStructure
    float    Distance = 0.0f;      // [m]  along the ray

    // Hit position in the figure's own plane, metres from its centre. A caller normalises this itself — the
    //    engine will not decide that -HalfWidth..+HalfWidth means 0..1 for a slider, because for a knob it means
    //    an angle and for a button it means nothing at all.
    float    LocalX   = 0.0f;      // [m]
    float    LocalY   = 0.0f;      // [m]

    // The same position as a fraction of the figure's extent, in [-1, +1]. Provided because every caller would
    //    otherwise divide by the half extent, and doing it here keeps the sign convention in one place.
    float    FractionX = 0.0f;     // [-]
    float    FractionY = 0.0f;     // [-]
};

//------------------------------------------------------------------------------------------------------------------------
//                                             INTERFACE POINTER PROJECTION
//------------------------------------------------------------------------------------------------------------------------

class InterfacePointerProjection
{
public:
    // Nearest PointerTarget figure along the ray. Composition must have run for this frame — the placements come
    //    from the sequence, so a stale sequence gives stale answers rather than wrong ones.
    [[nodiscard]] static PointerContact Project(const InterfaceStructure& Structure,
                                                const InterfaceSequence&  Composition,
                                                const PointerRay&         Ray) noexcept;

    // Ray through a viewport pixel, for a pinhole camera matching the one the renderer uses. Supplied here so the
    //    project does not re-derive the projection convention and drift from CameraClipConfiguration.
    //
    //    PixelX / PixelY are in [0, Width) × [0, Height) with the origin top-left, matching Vulkan's framebuffer
    //    convention and therefore the cursor position a window reports.
    [[nodiscard]] static PointerRay ConstructViewportRay(float PixelX, float PixelY,
                                                         uint32_t Width, uint32_t Height,
                                                         const float EyeOrigin[3],
                                                         const float Forward[3],
                                                         const float Right[3],
                                                         const float Up[3],
                                                         float TanHalfFieldOfView,
                                                         float AspectRatio) noexcept;
};

} // namespace Frontier
