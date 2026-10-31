/*============================================================================================================================================
                                       TEXTUREPAINTWORKSPACEPANELCONSTRUCTIONVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Construction coverage for the Texture Paint workspace native port:
//    (1) TexturePaintWorkspacePanel constructs/Initializes/Activates cleanly, exactly as
//        TextureStudioApplication does unconditionally at startup (mirrors the Baking/Stamp
//        construction validations' startup-crash guard).
//    (2) the embedded document model matches Alloy's default project shell: document name
//        "Teapot study", three layers with the paint layer "Surface detail" selected on top,
//        snapshot history seeded, and the teapot topology (the viewport's paint/pick data)
//        really built.

#include "UI/TextureStudio/TexturePaintWorkspacePanel.h"
#include "UI/TextureStudio/PatternThumbnailRaster.h"
#include "UI/TextureStudio/StampLibrary.h"
#include "Engine/ContentInterchange/StampVectorImport.h"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool Condition, const char *Message)
{
    if (!Condition) throw std::runtime_error(Message);
}

uint64_t PixelDigest(const std::vector<uint8_t> &Pixels) noexcept
{
    uint64_t Hash = 1469598103934665603ull;
    for (size_t Index = 0; Index < Pixels.size(); Index += 7)
    {
        Hash ^= Pixels[Index];
        Hash *= 1099511628211ull;
    }
    return Hash;
}
} // namespace

int main()
{
    using namespace Frontier;
    try
    {
        TexturePaintWorkspacePanel Panel;
        Require(Panel.Initialize(), "TexturePaintWorkspacePanel::Initialize must succeed");
        Require(Panel.Activate(), "TexturePaintWorkspacePanel::Activate must succeed");

        Require(Panel.QueryDocumentName() == "Teapot study", "document must be named Teapot study");
        Require(Panel.QueryLayerCount() == 3, "default document must have three layers");
        Require(Panel.QuerySelectedLayerName() == "Surface detail", "top paint layer must start selected");
        Require(Panel.QueryHistoryCount() >= 1, "history must start seeded with an initial revision");
        Require(Panel.QueryTriangleCount() > 0, "teapot topology must build triangles");
        Require(Panel.QueryMaterialAssetCount() == 0, "no material assets before the app configures them");

        // 1. Baseline multi-channel PBR composite: Albedo, ORM, Tangent Normal, Emissive all 1028x1028x4.
        Panel.ForceCompositePreview();
        const size_t ExpectedBytes = size_t(TexturePaintWorkspacePanel::PaintCanvasExtent) *
            size_t(TexturePaintWorkspacePanel::PaintCanvasExtent) * 4;
        Require(Panel.QueryPaintCanvasPixels().size() == ExpectedBytes, "Albedo canvas must be 1028x1028 RGBA");
        Require(Panel.QueryPaintOrmPixels().size() == ExpectedBytes, "ORM canvas must be 1028x1028 RGBA");
        Require(Panel.QueryPaintNormalPixels().size() == ExpectedBytes, "Normal canvas must be 1028x1028 RGBA");
        Require(Panel.QueryPaintEmissivePixels().size() == ExpectedBytes, "Emissive canvas must be 1028x1028 RGBA");
        const uint64_t BaseAlbedoHash = PixelDigest(Panel.QueryPaintCanvasPixels());
        const uint64_t BaseOrmHash = PixelDigest(Panel.QueryPaintOrmPixels());
        const uint64_t BaseNormalHash = PixelDigest(Panel.QueryPaintNormalPixels());

        // 2. Material layer with mineral flakes ("Alpine Mica Schist & Lichen") updates Albedo, ORM, and Tangent Normal.
        Panel.AuthorAddMaterialLayer("Alpine Mica Schist & Lichen");
        Panel.ForceCompositePreview();
        const uint64_t MatAlbedoHash = PixelDigest(Panel.QueryPaintCanvasPixels());
        const uint64_t MatOrmHash = PixelDigest(Panel.QueryPaintOrmPixels());
        const uint64_t MatNormalHash = PixelDigest(Panel.QueryPaintNormalPixels());
        Require(MatAlbedoHash != BaseAlbedoHash, "Material layer must update composited Albedo");
        Require(MatOrmHash != BaseOrmHash, "Material layer must update composited ORM");
        Require(MatNormalHash != BaseNormalHash, "Flaked rock material layer must perturb composited Tangent Normals");

        // 3. Mesh-aware generator layer ("MetalEdgeWear" on metalness) modulates ORM across convex ridges.
        Panel.AuthorAddGeneratorLayer("metalness", "MetalEdgeWear", {1.f, 1.f, 1.f, 1.f}, .95f);
        Panel.ForceCompositePreview();
        const uint64_t GenOrmHash = PixelDigest(Panel.QueryPaintOrmPixels());
        Require(GenOrmHash != MatOrmHash, "MetalEdgeWear generator layer must modulate composited Metalness in ORM");

        // 4. Gradient fill layer updates Albedo across surface/UV coordinates.
        Panel.AuthorAddGradientFillLayer({.18f, .32f, .52f, .45f}, {.86f, .64f, .32f, .45f}, 45.f, 2);
        Panel.ForceCompositePreview();
        const uint64_t GradAlbedoHash = PixelDigest(Panel.QueryPaintCanvasPixels());
        Require(GradAlbedoHash != MatAlbedoHash, "Gradient fill layer must update composited Albedo");

        // 5. Multi-channel digital brush stroke deposits Albedo, Metallic/Roughness ORM, and Height-derived Normals.
        Panel.AuthorPaintDabStroke(.25f, .5f, .75f, .5f, {.92f, .74f, .26f, 1.f}, .06f, .12f, .95f, .75f);
        Panel.ForceCompositePreview();
        Require(PixelDigest(Panel.QueryPaintCanvasPixels()) != GradAlbedoHash, "Digital paint stroke must update Albedo");
        Require(PixelDigest(Panel.QueryPaintOrmPixels()) != GenOrmHash, "Digital paint stroke must deposit Roughness/Metalness into ORM");
        Require(PixelDigest(Panel.QueryPaintNormalPixels()) != MatNormalHash, "Digital paint height relief must derive Tangent Normal bevels");

        // 6. SVG/vector .stamp.toml 9-channel PBR decal projection onto the Teapot.
        const auto TempStampDir = std::filesystem::temp_directory_path() / "frontier_stage6_stamp_test";
        std::error_code CleanupFailure;
        std::filesystem::remove_all(TempStampDir, CleanupFailure);
        std::filesystem::create_directories(TempStampDir, CleanupFailure);
        {
            VectorAuthoringDocument StampDoc;
            StampDoc.Stamp = true;
            StampDoc.Name = "Embossed Brass Seal";
            const char *Svg =
                "<svg viewBox=\"0 0 100 100\">"
                "<defs><linearGradient id=\"g\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\">"
                "<stop offset=\"0%\" stop-color=\"#f5d061\"/><stop offset=\"100%\" stop-color=\"#b87a1e\"/>"
                "</linearGradient></defs>"
                "<circle cx=\"50\" cy=\"50\" r=\"38\" fill=\"url(#g)\" stroke=\"#fff2b2\" stroke-width=\"6\"/>"
                "</svg>";
            const uint32_t Root = AppendStampVectorImport(StampDoc, Svg);
            Require(!StampDoc.Instructions.empty() && Root < StampDoc.Instructions.size(), "SVG stamp import must succeed");
            VectorAuthoringLayer StampLayer;
            StampLayer.Token = ++StampDoc.TokenClock;
            StampLayer.Root = Root;
            StampLayer.Name = "Brass Seal";
            StampLayer.ComponentKind = VectorAuthoringComponentKind::ImportedVector;
            StampLayer.SourceMarkup = Svg;
            StampLayer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Colour)] = {true, 1.f};
            StampLayer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Height)] = {true, .85f};
            StampLayer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Normal)] = {true, 1.8f};
            StampLayer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Metalness)] = {true, .92f};
            StampLayer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Roughness)] = {true, .16f};
            StampLayer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Clearcoat)] = {true, .75f};
            StampDoc.Layers.push_back(std::move(StampLayer));

            const auto Maps = EvaluateStampSurfaceMaps(StampDoc, 256);
            std::string SavedId, SaveDiag;
            Require(StampLibrary::SaveRaster(TempStampDir, "Embossed Brass Seal", 256, 256,
                Maps.ColourRgba, EncodeVectorAuthoring(StampDoc), SavedId, SaveDiag),
                "StampLibrary::SaveRaster with .stamp.toml sidecar must succeed");

            const uint64_t PreStampOrm = PixelDigest(Panel.QueryPaintOrmPixels());
            const uint64_t PreStampNormal = PixelDigest(Panel.QueryPaintNormalPixels());
            Panel.ConfigureStampLibraryDirectory(TempStampDir);
            Panel.AuthorPlaceStampAtUv(SavedId, .52f, .52f, .22f);
            Panel.ForceCompositePreview();
            Require(PixelDigest(Panel.QueryPaintOrmPixels()) != PreStampOrm,
                "Placed .stamp.toml decal must composite spatial Roughness/Metalness into Teapot ORM");
            Require(PixelDigest(Panel.QueryPaintNormalPixels()) != PreStampNormal,
                "Placed .stamp.toml decal must composite spatial embossed Normals onto Teapot");
        }
        std::filesystem::remove_all(TempStampDir, CleanupFailure);
        Require(Panel.Finalize(), "TexturePaintWorkspacePanel::Finalize must succeed");
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "TexturePaintWorkspacePanelConstructionValidation failed: " << Failure.what() << "\n";
        return 1;
    }
    std::cout << "TexturePaintWorkspacePanelConstructionValidation passed.\n";
    return 0;
}
