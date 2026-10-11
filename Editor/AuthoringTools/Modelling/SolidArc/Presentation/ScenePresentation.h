//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Presentation/ScenePresentation.h — Kernel geometry → vertex streams, plus the standard viewport dressing
//============================================================================================================================================
// Pure conversion: NurbsCurve → SegmentStream (adaptive tessellation), NurbsSurface → SurfaceStream (+ iso-curve
//    SegmentStream), control polygons / nets → PointStream + SegmentStream. Also the world triad and a fit support
//    that runs the canonical step order: lattice → surfaces → curves → points → overlay.
#pragma once

#include "RasterExchange.h"
#include "Kernel/SurfaceSpecification.h"

namespace Frontier
{

struct ScenePresentation
{
    [[nodiscard]] static SegmentStream CurveSegments(const NurbsCurve& Curve, double ChordTolerance = 1e-3) noexcept;
    [[nodiscard]] static SurfaceStream SurfaceTriangles(const NurbsSurface& Surface, double ChordTolerance = 1e-3) noexcept;
    [[nodiscard]] static SegmentStream SurfaceIsoCurves(const NurbsSurface& Surface, int CountU, int CountV, double ChordTolerance = 2e-2) noexcept;
    [[nodiscard]] static SegmentStream ControlPolygon(const NurbsCurve& Curve) noexcept;
    [[nodiscard]] static PointStream   ControlPoints(const NurbsCurve& Curve, PointGlyph Glyph = PointGlyph::Square) noexcept;
    [[nodiscard]] static SegmentStream ControlNet(const NurbsSurface& Surface) noexcept;
    [[nodiscard]] static PointStream   ControlPoints(const NurbsSurface& Surface, PointGlyph Glyph = PointGlyph::Square) noexcept;

    // World triad at the origin: X red, Y green, Z blue, each `Length` long, drawn in the overlay segment.
    static void DrawTriad(RasterExchange& Raster, double Length) noexcept;

    // Phase 19: an Empty is a transform handle with no geometry. We draw a small triad at the Empty's
    //    position (X red, Y green, Z blue) so the user can see and select it.
    static void DrawEmpty(RasterExchange& Raster, Vec3 Position, double Size = 0.15) noexcept;

    [[nodiscard]] static DrawRecord Tinted(float R, float G, float B, float A = 1.0f) noexcept
    {
        DrawRecord D; D.Tint[0] = R; D.Tint[1] = G; D.Tint[2] = B; D.Tint[3] = A; return D;
    }
};

} // namespace Frontier
