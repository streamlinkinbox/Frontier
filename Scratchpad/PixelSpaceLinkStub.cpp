// Link stub: GlyphSpace::Flatten is pure geometry, but GlyphSpace.cpp also carries Stroke/Fill, which call into
//    PixelSpace and therefore ImGui. The offline codec only ever calls Flatten, so the raster entry points are
//    stubbed here rather than dragging an immediate-mode UI into a headless build-time proof.
#include "DisplayPresentation/PixelSpace.h"
namespace Frontier {
void PixelSpace::StrokePolyline(const PlanePoint*, unsigned int, ColorQuad, float, bool) noexcept {}
void PixelSpace::FillPolygon(const PlanePoint*, unsigned int, ColorQuad) noexcept {}
}
