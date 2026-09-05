//============================================================================================================================================
//                                                    INTERFACESPECIFICATION.H
//============================================================================================================================================
// 🧩 Declarations shared by every spatial-interface translation unit: the figure categories the signed-distance shader
//    can draw, the fixed limits the instance extent is sized from, the quad topology, and the sort-key layout.
//
// This header is the engine's whole vocabulary for 3D interface geometry. It knows rounded rectangles, arcs, tick
//    rings, needles, seven-segment cells and lamps — it does not know what a tachometer is. Semantic meaning
//    (redline, gear, throttle) belongs to the project that constructs the figures (CLAUDE.md §6).
//
// Coordinate convention: a figure lives on its own local plane, +X right, +Y UP, origin at the figure centre, metres.
//    The plane is placed in the world by the figure's transform. This is deliberately NOT the top-left +Y-down
//    convention of PixelSpace: that surface composites onto the swapchain, this one lives in the scene.

#pragma once

#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                  FIGURE CATEGORY
//------------------------------------------------------------------------------------------------------------------------
// Mirrors kCategory* in Shaders/InterfaceSignedDistance.slang. Every category is evaluated by the same fragment
//    shader from the same 96-byte instance slot — a new category is a new branch there, never a new draw.

enum class InterfaceCategory : uint32_t
{
    Surface     = 0u,   // rounded rectangle — housings, cards, buttons, bar troughs, toggle beds
    Arc         = 1u,   // annular sector — meters, progress rings; ScalarAlpha = fill fraction of the sweep
    TickRing    = 2u,   // repeated radial marks around a circle; ScalarBeta = mark count
    Needle      = 3u,   // tapered pointer from the centre; ScalarAlpha = angle fraction across the sweep
    SegmentCell = 4u,   // one seven-segment digit; ScalarAlpha = digit 0..9 (10 = blank, 11 = minus)
    Lamp        = 5u,   // filled disc with a soft halo — telltales; ScalarAlpha = luminance
    Glyph       = 6u,   // P1: one stroke-font character; ScalarAlpha = ASCII code, ScalarBeta = stroke half width [m]
    Count       = 7u
};

[[nodiscard]] const char* InterfaceCategoryName(InterfaceCategory Category) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                 INTERFACE SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------

struct InterfaceSpecification
{
    // Instance extent sizing. The plan budgets ~200 live figures for a full multi-screen cockpit; the limit is set
    //    well above that so 500+ still holds without a resize (§P0 assumption 1).
    static constexpr uint32_t FigureLimit      = 1024u;         // [cnt] maximum figures in one structure
    static constexpr uint32_t DescentLimit     = 64u;           // [cnt] maximum graph depth (recursion is iterative anyway)

    // Draw shape: a 4-vertex triangle strip, no vertex extent bound at all. The vertex shader derives the corner
    //    from gl_VertexIndex, so the entire interface costs one vkCmdDraw(4, InstanceCount, 0, 0).
    static constexpr uint32_t CornerCount      = 4u;            // [cnt] vertices per figure

    // Anti-aliasing feather, in local metres per screen pixel. The fragment stage widens this by fwidth so the edge
    //    stays a pixel wide at any distance or tilt — this is why the shapes never blur like a rasterised vector.
    static constexpr float    EdgeFeather      = 1.0f;          // [px] coverage ramp width

    // The transparent sort is a stable back-to-front over view depth. Opaque figures (Opacity ≥ this) are emitted
    //    first in front-to-back order so early-Z rejects the transparents behind them.
    static constexpr float    OpaqueThreshold  = 0.999f;        // [-]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     SORT KEY
//------------------------------------------------------------------------------------------------------------------------
// Packed so a single ascending uint64 sort produces the correct submission order:
//
//    bit 63      transparency  (0 = opaque, 1 = transparent)   — opaque group always submits first
//    bits 62..40 ordering rank (23 bits, caller-assigned)      — a whole screen sorts as one layer mid-transition (⑤)
//    bits 39..8  view depth    (32 bits, float bits, inverted for the opaque group → front-to-back)
//    bits 7..0   unused        (kept zero; P2 will place the clip-group ordinal here so the batcher can group scissors)
//
// A few hundred keys per frame — the sort is trivial and happens on the CPU, never on the GPU.

[[nodiscard]] uint64_t ComposeInterfaceSortKey(bool Transparent, uint32_t OrderingRank, float ViewDepth) noexcept;

} // namespace Frontier
