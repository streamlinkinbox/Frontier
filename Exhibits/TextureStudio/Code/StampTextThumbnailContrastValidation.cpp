/*============================================================================================================================================
                                               STAMPTEXTTHUMBNAILCONTRASTVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Headless CPU-only regression test for the reported "Stamp layer-stack row never shows a real
//    preview" bug. Reproduces the real panel pipeline end to end on the CPU (AppendStampTextOutline ->
//    EncodePatternLayerThumbnailSvg -> RasterizePatternLayerThumbnail, the exact functions
//    StampWorkspacePanel::RefreshLayerAtlas calls), using the Text tool's own real default fill
//    ({.08,.08,.08,1}, ~rgb(20,20,20)) and the panel's own current row-preview background, then
//    counts pixels that visibly differ from that background. The old near-black "gallery card" well
//    (~rgb(20,23,24)) put that default ink at effectively zero contrast against its own thumbnail --
//    a seemingly blank/missing row preview that was actually rasterizing correctly, just invisibly.

#include "UI/TextureStudio/PatternThumbnailRaster.h"
#include "Engine/ContentInterchange/StampTextOutline.h"
#include <iostream>
#include <stdexcept>
#include <string>

using namespace Frontier;

namespace
{
void Require(bool Condition, const std::string &Message)
{
    if (!Condition) throw std::runtime_error(Message);
}

size_t CountInkPixels(const std::vector<uint8_t> &Pixels, std::array<uint8_t, 3> Background)
{
    size_t Ink = 0;
    for (size_t Pixel = 0; Pixel < Pixels.size(); Pixel += 4)
    {
        const int Delta = std::abs(int(Pixels[Pixel]) - int(Background[0])) + std::abs(int(Pixels[Pixel + 1]) - int(Background[1])) +
            std::abs(int(Pixels[Pixel + 2]) - int(Background[2]));
        if (Delta > 10) ++Ink;
    }
    return Ink;
}
}

int main()
{
    try
    {
        VectorAuthoringDocument Document;
        Document.Stamp = true;
        // 📝 The Text tool's own real default ink colour (see StampWorkspacePanel::CommitTextComposer).
        const uint32_t Root = AppendStampTextOutline(Document, "asdasd", 146.f, {.08f, .08f, .08f, 1},
            "EngineContent/FontArchives/DMSans/DMSans-Regular.ttf");
        VectorAuthoringLayer Layer;
        Layer.Token = ++Document.TokenClock;
        Layer.Root = Root;
        Layer.Name = "asdasd";
        Layer.ComponentKind = VectorAuthoringComponentKind::Text;
        Document.Layers.push_back(Layer);

        // 📝 The OLD per-layer row-preview well both panels' RefreshLayerAtlas used to pass (a dark
        // gallery-grid colour meant for browsing many library cards, not for a single document's
        // own authored ink).
        constexpr uint32_t OldCardWell = uint32_t(255) << 24 | uint32_t(24) << 16 | uint32_t(23) << 8 | uint32_t(20);
        const auto OldPixels = RasterizePatternLayerThumbnail(Document, Layer.Token, 64, OldCardWell);
        const size_t OldInk = CountInkPixels(OldPixels, {20, 23, 24});
        std::cout << "OLD dark gallery-card well: " << OldInk << " / " << (OldPixels.size() / 4) << " visibly non-background pixels\n";
        Require(OldInk < 20, "Expected the old dark well to reproduce near-zero visible contrast against the Text tool's own default dark ink "
            "(sanity check on the repro itself, not the fix).");

        // 📝 The FIXED well: the real 2D viewport's own board colour (Spectrum(0xede7d7), see
        // ConstructCanvas in both StampWorkspacePanel.cpp and PatternWorkspacePanel.cpp).
        constexpr uint32_t FixedCardWell = uint32_t(255) << 24 | uint32_t(215) << 16 | uint32_t(231) << 8 | uint32_t(237);
        const auto FixedPixels = RasterizePatternLayerThumbnail(Document, Layer.Token, 64, FixedCardWell);
        const size_t FixedInk = CountInkPixels(FixedPixels, {237, 231, 215});
        std::cout << "NEW board-matched well:     " << FixedInk << " / " << (FixedPixels.size() / 4) << " visibly non-background pixels\n";
        Require(FixedInk > 200, "Expected the fixed, board-matched well to make the Text tool's own default ink clearly visible in its "
            "row preview -- otherwise the reported 'row preview never shows anything' bug is still present.");

        std::cout << "StampTextThumbnailContrastValidation passed.\n";
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "StampTextThumbnailContrastValidation failed: " << Failure.what() << "\n";
        return 1;
    }
    return 0;
}
