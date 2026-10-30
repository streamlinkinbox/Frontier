/*============================================================================================================================================
                                                          PATTERNTHUMBNAILRASTER.H
============================================================================================================================================*/
// 🧩 Gallery thumbnails are SVG documents rasterized by ThorVG into atlas-ready RGBA pixels;
// the GPU uploads each page once. The encoding mirrors the coverage shader's geometry exactly.

#pragma once
#include "UI/TextureStudio/PatternWorkspacePanel.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{
// 📝 Encodes the visible layer stack as one SVG over the 512 tile, background baked in.
[[nodiscard]] std::string EncodePatternThumbnailSvg(const VectorAuthoringDocument &Document, uint32_t BackgroundAbgr);
// 📝 ThorVG raster of that SVG at Extent×Extent (ABGR8888S straight alpha, anti-aliased).
[[nodiscard]] std::vector<uint8_t> RasterizePatternThumbnailSvg(
    const VectorAuthoringDocument &Document, uint32_t Extent, uint32_t BackgroundAbgr);
// 🖨️ Stamp-library raster: the same document encoding with NO baked background rect, so everything
// outside the artwork stays transparent and the decal can composite over a painted surface.
[[nodiscard]] std::string EncodeStampDocumentSvg(const VectorAuthoringDocument &Document);
[[nodiscard]] std::vector<uint8_t> RasterizeStampDocument(const VectorAuthoringDocument &Document, uint32_t Extent);

struct StampSurfaceMaps
{
    uint32_t Extent = 0;
    std::vector<uint8_t> ColourRgba;
    std::vector<float> Coverage;
    std::vector<float> Height;
    std::vector<float> NormalScale;
    std::vector<float> NormalX, NormalY, NormalZ;
    std::vector<float> Roughness, Metalness, Opacity, Occlusion, Transmission, Clearcoat;
    std::vector<float> RoughnessMask, MetalnessMask, HeightMask, NormalMask, OcclusionMask, TransmissionMask, ClearcoatMask;
};
[[nodiscard]] StampSurfaceMaps EvaluateStampSurfaceMaps(const VectorAuthoringDocument &Document, uint32_t Extent);
// 📝 Encodes just one layer, cropped to its own tight extent (never the full tile) — the layer-row
// preview thumbnail asked of the Pattern workspace, mirroring the selection hitbox's own bounds.
[[nodiscard]] std::string EncodePatternLayerThumbnailSvg(
    const VectorAuthoringDocument &Document, uint32_t Token, uint32_t BackgroundAbgr);
// 📝 ThorVG raster of a single layer's cropped thumbnail SVG at Extent×Extent.
[[nodiscard]] std::vector<uint8_t> RasterizePatternLayerThumbnail(
    const VectorAuthoringDocument &Document, uint32_t Token, uint32_t Extent, uint32_t BackgroundAbgr);
// 📝 Blits square thumbs into a Columns×Columns grid with replicated 1px gutters, matching MaterialThumbnailAtlas slots.
[[nodiscard]] std::vector<uint8_t> BlitPatternThumbnailAtlas(
    const std::vector<std::vector<uint8_t>> &Thumbs, uint32_t TileExtent, uint32_t Columns, uint32_t BackgroundAbgr);
}
