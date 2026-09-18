//============================================================================================================================================
//                                                    INTERFACEVECTORCODEC.H
//============================================================================================================================================
// 🧩 P4 — offline SVG conversion. Turns an SVG path into interface figures, so artwork authored in a vector tool
//    becomes panel geometry without a texture, a runtime parser, or a rasteriser in the frame.
//
//    The conversion that makes this cheap: a stroked polyline segment IS a rounded rectangle. Centre it on the
//    segment's midpoint, roll it to the segment's angle, give it half the segment length plus the stroke radius
//    as its half width and the radius as its half height, and the existing Surface figure draws a capsule. So P4
//    adds NO category, NO shader code and NO change to the 112-byte GPU slot — it reuses what P0 already ships,
//    and the batcher still emits one draw for the whole icon.
//
//    OFFLINE is the operative word, and it is a deliberate constraint rather than a limitation to apologise for.
//    Parsing SVG text is string work: allocation, floating-point scanning, error handling. None of that belongs in
//    a frame. Convert at build time or at level load, keep the figures, and the render loop never sees a character
//    of XML. `Tools/InterfaceVectorConversion` drives this over a directory of icons; the runtime never calls it.
//
//    Scope, stated up front: polylines and the curves GlyphSpace already flattens, stroked with a uniform width.
//    Filled regions are not produced — a filled star would need triangulation, which is a different problem with
//    a different answer (Clipper2/earcut are already vendored for exactly that, when it is wanted). Icons in the
//    lucide/feather idiom, which is what this panel uses, are strokes and convert exactly.

#pragma once

#include "InterfaceStructure.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                   VECTOR PLACEMENT
//------------------------------------------------------------------------------------------------------------------------

struct VectorPlacement
{
    float       OriginX     = 0.0f;     // [m]  icon centre in the ancestor's plane
    float       OriginY     = 0.0f;     // [m]
    float       OriginZ     = 0.0f;     // [m]  lift out of the plane
    float       Extent      = 0.024f;   // [m]  the viewBox maps to this square
    float       ViewBox     = 24.0f;    // [-]  source units per side (lucide and feather are 24)
    float       StrokeWidth = 0.0022f;  // [m]  full width; the capsule radius is half this
    PaletteSlot Palette     = PaletteSlot::Marking;
    uint32_t    TintOverride = 0u;      // [-]  RGBA8; 0 = use the palette slot
    uint32_t    OrderingRank = 0u;      // [-]  carried onto every segment so an icon sorts as one unit

    // A segment shorter than this is dropped. Flattening a curve emits many tiny steps, and a capsule shorter than
    //    its own stroke width is a dot the eye cannot distinguish from its neighbours — it costs an instance and
    //    contributes nothing. Expressed as a fraction of the stroke width.
    float       MinimumSegment = 0.35f; // [-]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 CONVERSION METRICS
//------------------------------------------------------------------------------------------------------------------------
// Reported so a build step can see what an icon actually cost before it is committed to. An icon that converts to
//    four hundred figures is a mistake worth catching at conversion time rather than in a frame budget.

struct VectorConversionMetrics
{
    uint32_t ContourCount = 0u;   // [cnt] sub-paths found
    uint32_t PointCount   = 0u;   // [cnt] flattened points
    uint32_t FigureCount  = 0u;   // [cnt] figures emitted
    uint32_t DroppedCount = 0u;   // [cnt] segments below MinimumSegment
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 INTERFACE VECTOR CODEC
//------------------------------------------------------------------------------------------------------------------------

class InterfaceVectorCodec
{
public:
    // Appends one figure per surviving segment beneath Ancestor. Returns the metrics; FigureCount is zero for an
    //    empty or unparseable path rather than being an error, because a build step converting a directory should
    //    report a bad icon and carry on rather than abort the batch.
    static VectorConversionMetrics Compose(InterfaceStructure& Structure, uint32_t Ancestor,
                                           std::string_view SvgPath, const VectorPlacement& Placement) noexcept;

    // What the path would cost, without creating anything. Lets a caller reject an over-complex icon up front.
    [[nodiscard]] static VectorConversionMetrics Measure(std::string_view SvgPath,
                                                         const VectorPlacement& Placement) noexcept;
};

} // namespace Frontier
