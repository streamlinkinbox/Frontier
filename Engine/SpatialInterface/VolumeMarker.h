//============================================================================================================================================
// 📦 Engine/SpatialInterface/VolumeMarker.h — a clickable billboard for things that have no body
//============================================================================================================================================
// Celestial port, step 5. A Local Cloud and a Local Volumetric Fog have a position and no surface. At low density
//    there is nothing on screen to click, and even at high density the thing you would click is a puff of noise
//    that moves with the wind — so selecting and dragging the volume itself is not possible.
//
// The editor's answer, and the reference demo's, is a proxy: a small screen-facing icon pinned to the volume's
//    centre. You select the marker, and the gizmo that appears drives the volume's transform. This header owns
//    the marker's projection and hit-testing; the gizmo itself is the editor's existing one.
//
//    ⚠️ Screen-space size, not world-space. A billboard scaled in world units vanishes when you fly away from it,
//    which for a marker is exactly backwards: the further you are, the more you need it to find the volume. The
//    marker holds a constant pixel size at any distance, the way the demo's gizmo does ("L=oz*CAM.tanH*.16,
//    constant on-screen size").
//
//    The icons are SVG. ThorVG is already vendored and already rasterises the Control Centre's iconography, so
//    the marker uses the same path rather than inventing a second icon system — see ControlKit.

#pragma once

#include <cstdint>
#include <cmath>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                     MARKERS
//------------------------------------------------------------------------------------------------------------------------

// Which glyph the marker shows. The name is the entity it stands for, so the editor never has to map a volume
//    kind onto an icon at the call site.
enum class VolumeMarkerCategory : uint32_t
{
    LocalCloud = 0u,   // a small cumulus glyph
    LocalFog   = 1u,   // stacked horizontal wisps
    WindField  = 2u,   // the wind rose, for the global wind's origin
};

struct VolumeMarker
{
    VolumeMarkerCategory Category = VolumeMarkerCategory::LocalCloud;
    float    World[3]   = { 0.0f, 0.0f, 0.0f };   // [m] the volume's centre, Z-up
    uint32_t Identifier = 0u;                     // which entity this marker belongs to
    bool     Selected   = false;
    bool     Visible    = true;
};

// Where a marker landed on screen, and whether it is in front of the camera at all.
struct MarkerProjection
{
    float    X = 0.0f, Y = 0.0f;   // [px] screen position of the marker's centre
    float    Depth = 0.0f;         // [m] distance along the view direction
    float    Radius = 0.0f;        // [px] hit radius — constant, see the note above
    bool     OnScreen = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PROJECTION
//------------------------------------------------------------------------------------------------------------------------

class VolumeMarkerProjection
{
public:
    // [px] the marker's on-screen size, held constant regardless of distance.
    static constexpr float kMarkerRadiusPixels = 14.0f;

    // Projects a world point through a camera basis. Kept free of any matrix type so the editor, the proofs and
    //    the eventual panel can all call it without agreeing on a maths library first.
    static MarkerProjection Project(const float World[3],
                                    const float Eye[3], const float Forward[3],
                                    const float Right[3], const float Up[3],
                                    float FovYRadians, uint32_t Width, uint32_t Height) noexcept
    {
        MarkerProjection Out{};
        const float Delta[3] = { World[0] - Eye[0], World[1] - Eye[1], World[2] - Eye[2] };
        const float Depth = Delta[0] * Forward[0] + Delta[1] * Forward[1] + Delta[2] * Forward[2];
        // Behind the camera, or so close the projection is meaningless.
        if (Depth <= 0.01f) return Out;

        const float SideOffset = Delta[0] * Right[0] + Delta[1] * Right[1] + Delta[2] * Right[2];
        const float UpOffset   = Delta[0] * Up[0]    + Delta[1] * Up[1]    + Delta[2] * Up[2];

        const float TanHalf = std::tan(FovYRadians * 0.5f);
        const float Aspect  = static_cast<float>(Width) / static_cast<float>(Height);

        const float Ndc[2] = { (SideOffset / Depth) / (TanHalf * Aspect), (UpOffset / Depth) / TanHalf };
        Out.X = (Ndc[0] * 0.5f + 0.5f) * static_cast<float>(Width);
        Out.Y = (1.0f - (Ndc[1] * 0.5f + 0.5f)) * static_cast<float>(Height);
        Out.Depth = Depth;
        Out.Radius = kMarkerRadiusPixels;
        // A marker just off the edge is still worth reporting: its gizmo may reach into view.
        Out.OnScreen = Out.X > -Out.Radius && Out.Y > -Out.Radius
                    && Out.X < static_cast<float>(Width) + Out.Radius
                    && Out.Y < static_cast<float>(Height) + Out.Radius;
        return Out;
    }

    // Which marker a click selects. Nearest to the camera wins among those under the pointer, so a marker in
    //    front is picked ahead of one behind it rather than by list order.
    static int32_t Pick(const VolumeMarker* Markers, uint32_t Count,
                        const float Eye[3], const float Forward[3],
                        const float Right[3], const float Up[3],
                        float FovYRadians, uint32_t Width, uint32_t Height,
                        float PointerX, float PointerY) noexcept
    {
        int32_t Best = -1;
        float   Nearest = 1e30f;
        for (uint32_t I = 0; I < Count; ++I)
        {
            if (!Markers[I].Visible) continue;
            const MarkerProjection P = Project(Markers[I].World, Eye, Forward, Right, Up, FovYRadians, Width, Height);
            if (!P.OnScreen) continue;
            const float Dx = PointerX - P.X, Dy = PointerY - P.Y;
            if (Dx * Dx + Dy * Dy > P.Radius * P.Radius) continue;
            if (P.Depth < Nearest) { Nearest = P.Depth; Best = static_cast<int32_t>(I); }
        }
        return Best;
    }

    // Dragging. A marker has no surface to slide along, so the drag happens on the plane through the volume's
    //    centre facing the camera — which is what makes the motion follow the pointer exactly rather than
    //    sliding away as the angle changes.
    static void DragToPointer(const float StartWorld[3],
                              const float Eye[3], const float Forward[3],
                              const float Right[3], const float Up[3],
                              float FovYRadians, uint32_t Width, uint32_t Height,
                              float PointerX, float PointerY, float OutWorld[3]) noexcept
    {
        const float Delta[3] = { StartWorld[0] - Eye[0], StartWorld[1] - Eye[1], StartWorld[2] - Eye[2] };
        const float Depth = Delta[0] * Forward[0] + Delta[1] * Forward[1] + Delta[2] * Forward[2];
        if (Depth <= 0.01f)
        {
            for (int C = 0; C < 3; ++C) OutWorld[C] = StartWorld[C];
            return;
        }
        const float TanHalf = std::tan(FovYRadians * 0.5f);
        const float Aspect  = static_cast<float>(Width) / static_cast<float>(Height);
        const float Ndc[2] = { (PointerX / static_cast<float>(Width)) * 2.0f - 1.0f,
                               1.0f - (PointerY / static_cast<float>(Height)) * 2.0f };
        const float SideOffset = Ndc[0] * TanHalf * Aspect * Depth;
        const float UpOffset   = Ndc[1] * TanHalf * Depth;
        for (int C = 0; C < 3; ++C)
            OutWorld[C] = Eye[C] + Forward[C] * Depth + Right[C] * SideOffset + Up[C] * UpOffset;
    }

    // The marker's glyph as an SVG path, rasterised by the same ThorVG the Control Centre already uses. Returned
    //    as source rather than pixels so the caller picks the size and tint.
    static const char* GlyphPath(VolumeMarkerCategory Category) noexcept
    {
        switch (Category)
        {
            case VolumeMarkerCategory::LocalFog:
                // Three stacked wisps.
                return "M3 9h12M5 12h11M4 15h13";
            case VolumeMarkerCategory::WindField:
                // A compass rose arrow.
                return "M10 2l3 8h-6zM10 18V10";
            case VolumeMarkerCategory::LocalCloud:
            default:
                // A cumulus outline.
                return "M6 14a3 3 0 010-6 4 4 0 017-2 3.5 3.5 0 011 6.9z";
        }
    }
};

} // namespace Frontier
