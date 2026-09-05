//============================================================================================================================================
//                                                       PIXELSPACE.H
//============================================================================================================================================
// 🧩 Primitives in, recorded draw commands out — the one seam between engine UI hosts and the immediate-mode backend.
//    Hosts (ControlCentreHost, future panels) speak only in pixels and colours; nothing above this header names ImGui.
//
// Coordinate convention: display pixels, origin top-left, +X right, +Y DOWN. This matches the Vulkan swapchain
//    image the overlay is composited onto; it is unrelated to the world-space convention in CLAUDE.md §7.

#pragma once

#include "ThemeStructure.h"
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                     PLANE EXTENT
//------------------------------------------------------------------------------------------------------------------------

struct PlaneExtent
{
    float MinimumX = 0.0f;   // [px] leading (left) edge
    float MinimumY = 0.0f;   // [px] upper edge
    float MaximumX = 0.0f;   // [px] trailing (right) edge
    float MaximumY = 0.0f;   // [px] lower edge

    [[nodiscard]] constexpr float Width()  const noexcept { return MaximumX - MinimumX; }
    [[nodiscard]] constexpr float Height() const noexcept { return MaximumY - MinimumY; }
    [[nodiscard]] constexpr bool  Encloses(float X, float Y) const noexcept
    {
        return X >= MinimumX && X < MaximumX && Y >= MinimumY && Y < MaximumY;
    }
};

[[nodiscard]] constexpr PlaneExtent Spanning(float X, float Y, float Width, float Height) noexcept
{
    return PlaneExtent{ X, Y, X + Width, Y + Height };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      PLANE POINT
//------------------------------------------------------------------------------------------------------------------------

struct PlanePoint
{
    float X = 0.0f;          // [px]
    float Y = 0.0f;          // [px]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    LAYER SELECTION
//------------------------------------------------------------------------------------------------------------------------

enum class SurfaceLayer : uint32_t
{
    Beneath = 0u,            // behind every ImGui window (background list)
    Above   = 1u             // in front of every ImGui window (foreground list) — overlays such as the notch
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   RECORDING SURFACE
//------------------------------------------------------------------------------------------------------------------------

class PixelSpace
{
public:
    PixelSpace() noexcept;
    ~PixelSpace() noexcept = default;

    PixelSpace(const PixelSpace&)            = delete;
    PixelSpace& operator=(const PixelSpace&) = delete;

    // Must be called once per frame after ImGui::NewFrame() and before ImGui::Render(); selects the draw list.
    //    Returns false when no ImGui context exists (e.g. headless proof generation without a backend).
    //    InterfaceScale (Display → UI Scale, 1.0 = 100 %): hosts record in LOGICAL pixels — QueryDisplayWidth/Height
    //    report physical ÷ scale — and every primitive is mapped to physical pixels on emission (positions, radii,
    //    stroke widths, font sizes, clip rectangles). Pointer input must be divided by the same factor by the caller.
    bool Begin(SurfaceLayer Layer, float DisplayWidth, float DisplayHeight, float InterfaceScale = 1.0f) noexcept;
    [[nodiscard]] float QueryInterfaceScale() const noexcept { return Scale; }

    // ── Primitives ───────────────────────────────────────────────────────────────────────────────────────────────────
    void FillRectangle (const PlaneExtent& Extent, ColorQuad Colour, float Radius = 0.0f) noexcept;
    void FillRectangleBottomRounded(const PlaneExtent& Extent, ColorQuad Colour, float Radius) noexcept;   // only the two lower corners rounded
    void FillPolygon   (const PlanePoint* Points, uint32_t PointCount, ColorQuad Colour) noexcept;   // convex or concave, anti-aliased
    void StrokePolyline(const PlanePoint* Points, uint32_t PointCount, ColorQuad Colour, float Thickness, bool Closed) noexcept;
    void Text          (float X, float Y, ColorQuad Colour, const char* Utf8, float FontSizePixels = 0.0f) noexcept;

    // ── Clipping ─────────────────────────────────────────────────────────────────────────────────────────────────────
    void PushClip(const PlaneExtent& Extent) noexcept;
    void PopClip() noexcept;

    // ── Groups ───────────────────────────────────────────────────────────────────────────────────────────────────────
    // A group is every primitive recorded between BeginGroup and EndGroup. EndGroup applies, in this order, a
    //    uniform scale about (PivotX, PivotY), a translation, and an alpha multiplier — the framer-motion
    //    "opacity / x / scale" triple that Notch animates on whole pages.
    [[nodiscard]] uint32_t BeginGroup() const noexcept;
    void EndGroup(uint32_t Mark, float OffsetX, float OffsetY, float GroupScale, float PivotX, float PivotY, float Alpha) noexcept;

    // ── Typeface ─────────────────────────────────────────────────────────────────────────────────────────────────────
    // Text / MeasureText use the face on top of this stack (nullptr → the backend default font). Handles come from
    //    TypefaceRegistry::QueryHandle. The stack is per-surface and reset by Begin().
    void PushTypeface(void* FaceHandle) noexcept;
    void PopTypeface() noexcept;
    [[nodiscard]] void* QueryTypeface() const noexcept { return TypefaceDepth > 0u ? TypefaceStack[TypefaceDepth - 1u] : nullptr; }

    // ── Measurement ──────────────────────────────────────────────────────────────────────────────────────────────────
    [[nodiscard]] PlanePoint MeasureText(const char* Utf8, float FontSizePixels = 0.0f) const noexcept;
    [[nodiscard]] float      QueryDisplayWidth()  const noexcept { return DisplayWidth;  }
    [[nodiscard]] float      QueryDisplayHeight() const noexcept { return DisplayHeight; }
    [[nodiscard]] bool       IsRecording()        const noexcept { return Commands != nullptr; }

private:
    void*   Commands;        // [-]  the backend draw list for the current frame (opaque above this seam)
    float   DisplayWidth;    // [px] logical
    float   DisplayHeight;   // [px] logical
    float   Scale;           // [-]  logical → physical multiplier
    void*   TypefaceStack[8];
    uint32_t TypefaceDepth;
};

} // namespace Frontier
