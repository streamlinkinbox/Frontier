//============================================================================================================================================
//                                                   INTERFACEVECTORCODEC.CPP
//============================================================================================================================================

#include "InterfaceVectorCodec.h"

#include "../DisplayPresentation/GlyphSpace.h"

#include <algorithm>
#include <cmath>

namespace Frontier {

namespace {

// One capsule, expressed as a rounded rectangle. This is the whole trick of P4, so it is written out rather than
//    inlined: a segment from A to B, stroked with radius R, is a rectangle centred on the midpoint, rolled to the
//    segment's angle, half as long as the segment plus a radius at each end, and one radius tall.
[[nodiscard]] InterfaceFigure ComposeCapsule(float Ax, float Ay, float Bx, float By,
                                             float Radius, const VectorPlacement& Placement) noexcept
{
    const float Dx     = Bx - Ax;
    const float Dy     = By - Ay;
    const float Length = std::sqrt(Dx * Dx + Dy * Dy);

    InterfaceFigure Figure;
    Figure.Category   = InterfaceCategory::Surface;
    Figure.HalfWidth  = Length * 0.5f + Radius;
    Figure.HalfHeight = Radius;
    // A full-height corner radius is what turns the rectangle into a capsule; anything less leaves square ends
    //    that show as notches where two segments meet at an angle.
    Figure.CornerRadius = Radius;

    Figure.Placement.Origin    = PlaneOrigin{ (Ax + Bx) * 0.5f, (Ay + By) * 0.5f, Placement.OriginZ };
    Figure.Placement.RotationZ = std::atan2(Dy, Dx);

    Figure.Palette      = Placement.Palette;
    Figure.TintOverride = Placement.TintOverride;
    Figure.OrderingRank = Placement.OrderingRank;
    return Figure;
}

// Walks the flattened contours and hands each surviving segment to Emit. Measure and Compose share this so the
//    two can never disagree about how many figures a path costs — a Measure that under-reported would be worse
//    than no Measure at all.
template <typename EmitFunction>
VectorConversionMetrics WalkSegments(std::string_view SvgPath, const VectorPlacement& Placement, EmitFunction Emit)
{
    VectorConversionMetrics Metrics;
    if (SvgPath.empty()) return Metrics;

    const std::vector<GlyphSpace::Contour> Contours = GlyphSpace::Flatten(SvgPath);
    Metrics.ContourCount = static_cast<uint32_t>(Contours.size());

    // viewBox units → metres. The viewBox maps onto a square of side Extent, centred on the placement origin.
    const float Box   = Placement.ViewBox > 0.0f ? Placement.ViewBox : 24.0f;
    const float Scale = Placement.Extent / Box;
    const float Radius  = std::max(Placement.StrokeWidth * 0.5f, 1.0e-6f);
    const float Minimum = Radius * 2.0f * std::max(Placement.MinimumSegment, 0.0f);

    const auto ToLocalX = [&](float X) { return Placement.OriginX + (X - Box * 0.5f) * Scale; };
    // SVG's Y axis points DOWN; the interface plane's points UP. Flipping here rather than at the call site means
    //    an icon cannot be silently rendered upside down by a caller that forgets.
    const auto ToLocalY = [&](float Y) { return Placement.OriginY - (Y - Box * 0.5f) * Scale; };

    for (const GlyphSpace::Contour& Contour : Contours)
    {
        const size_t Count = Contour.Points.size();
        Metrics.PointCount += static_cast<uint32_t>(Count);
        if (Count < 2u) continue;

        // A closed contour needs the wrap-around segment; an open one stops at the last point.
        const size_t Steps = Contour.Closed ? Count : Count - 1u;
        for (size_t Index = 0u; Index < Steps; ++Index)
        {
            const PlanePoint& A = Contour.Points[Index];
            const PlanePoint& B = Contour.Points[(Index + 1u) % Count];

            const float Ax = ToLocalX(A.X), Ay = ToLocalY(A.Y);
            const float Bx = ToLocalX(B.X), By = ToLocalY(B.Y);
            const float Dx = Bx - Ax, Dy = By - Ay;

            if (std::sqrt(Dx * Dx + Dy * Dy) < Minimum) { ++Metrics.DroppedCount; continue; }

            Emit(Ax, Ay, Bx, By, Radius);
            ++Metrics.FigureCount;
        }
    }
    return Metrics;
}

} // namespace

VectorConversionMetrics InterfaceVectorCodec::Measure(std::string_view SvgPath,
                                                      const VectorPlacement& Placement) noexcept
{
    return WalkSegments(SvgPath, Placement, [](float, float, float, float, float) { });
}

VectorConversionMetrics InterfaceVectorCodec::Compose(InterfaceStructure& Structure, uint32_t Ancestor,
                                                      std::string_view SvgPath,
                                                      const VectorPlacement& Placement) noexcept
{
    return WalkSegments(SvgPath, Placement,
        [&](float Ax, float Ay, float Bx, float By, float Radius)
        {
            const InterfaceFigure Figure = ComposeCapsule(Ax, Ay, Bx, By, Radius, Placement);
            const uint32_t Ordinal = Structure.Construct(Figure);
            if (Ancestor != InterfaceStructure::Detached) (void)Structure.Attach(Ordinal, Ancestor);
        });
}

} // namespace Frontier
