/*============================================================================================================================================
                                                      PATTERNTHUMBNAILATLASVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Prove the gallery card atlas bakes faithfully: documents encode to SVG with coverage-shader
// geometry (half-extents, not the old 2x tessellation), ThorVG rasterizes them anti-aliased, array
// placements and motif-array presets land on their lattices, and the panel atlas stays revision-stable.

#include "UI/TextureStudio/PatternThumbnailRaster.h"
#include "UI/TextureStudio/PatternWorkspacePanel.h"
#include "Engine/ContentInterchange/PatternLibraryPresets.h"
#include "Engine/ContentInterchange/VectorAuthoringArrayPlacement.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>

using namespace Frontier;

namespace
{
uint32_t Assertions = 0;

void Confirm(bool Condition, const char *Caption)
{
    if (!Condition) throw std::runtime_error(Caption);
    ++Assertions;
}

std::array<uint8_t, 4> Texel(const std::vector<uint8_t> &Pixels, uint32_t Extent, uint32_t X, uint32_t Y)
{
    const size_t Offset = (size_t(Y) * Extent + X) * 4;
    return {Pixels[Offset], Pixels[Offset + 1], Pixels[Offset + 2], Pixels[Offset + 3]};
}

bool Near(uint8_t Actual, int Expected) { return std::abs(int(Actual) - Expected) <= 2; }

constexpr uint32_t PackAbgr(uint8_t Red, uint8_t Green, uint8_t Blue, uint8_t Alpha)
{
    return uint32_t(Alpha) << 24 | uint32_t(Blue) << 16 | uint32_t(Green) << 8 | uint32_t(Red);
}
}

int main()
{
    try
    {
        constexpr uint32_t CardWell = PackAbgr(20, 23, 24, 255);
        // Array placements: grid lattice centred on the origin with a linear scale envelope.
        {
            VectorAuthoringArraySpecification Grid;
            Grid.Category = VectorAuthoringArrayCategory::Grid;
            Grid.Columns = 3;
            Grid.Rows = 2;
            Grid.StepX = 10;
            Grid.StepY = 20;
            Grid.ScaleProfile = VectorAuthoringScaleProfile::Linear;
            Grid.StartScale = 100;
            Grid.EndScale = 200;
            const auto Placements = ConstructVectorAuthoringArrayPlacements(Grid, 100, 100);
            Confirm(Placements.size() == 6, "Grid array population changed.");
            Confirm(Placements[0].X == 90 && Placements[0].Y == 90 && Placements[5].X == 110 && Placements[5].Y == 110,
                "Grid array lattice is not centred on the origin.");
            Confirm(std::abs(Placements[0].Scale - 1.f) < 1e-6f && std::abs(Placements[5].Scale - 2.f) < 1e-6f,
                "Linear scale envelope endpoints moved.");
            bool Monotone = true;
            for (size_t Copy = 1; Copy < Placements.size(); ++Copy)
                Monotone = Monotone && Placements[Copy].Scale > Placements[Copy - 1].Scale;
            Confirm(Monotone, "Linear scale envelope is not monotone.");
            VectorAuthoringArraySpecification Circle;
            Circle.Category = VectorAuthoringArrayCategory::Circular;
            Circle.Count = 4;
            Circle.CentreX = 256;
            Circle.CentreY = 256;
            Circle.Radius = 50;
            Circle.Sweep = 360;
            const auto Ring = ConstructVectorAuthoringArrayPlacements(Circle, 0, 0);
            Confirm(Ring.size() == 4 && std::abs(Ring[0].X - 306) < 1e-4f && std::abs(Ring[0].Y - 256) < 1e-4f,
                "Circular array first placement moved.");
            Confirm(std::abs(Ring[3].X - 256) < 1e-3f && std::abs(Ring[3].Y - 206) < 1e-3f,
                "Full-sweep circular array duplicated its seam placement.");
        }
        // Presets are one motif expanded by the array generator: layer counts, lattice centres, inks.
        {
            const auto Centres = [](const VectorAuthoringDocument &Document)
            {
                std::set<std::pair<int, int>> Points;
                for (const auto &Layer : Document.Layers)
                {
                    const auto &Instruction = Document.Instructions[Layer.Root];
                    Points.emplace(int(std::lround(Instruction.Bounds[0])), int(std::lround(Instruction.Bounds[1])));
                }
                return Points;
            };
            const auto Polka = ConstructPatternLibraryPreset(0);
            Confirm(Polka.Layers.size() == 16, "Polka preset is not a 4x4 motif array.");
            const auto Dots = Centres(Polka);
            Confirm(Dots.count({64, 64}) && Dots.count({448, 448}) && Dots.size() == 16, "Polka lattice centres moved.");
            const auto InkAt = [&](const VectorAuthoringDocument &Document, int X, int Y)
            {
                for (const auto &Layer : Document.Layers)
                {
                    const auto &Instruction = Document.Instructions[Layer.Root];
                    if (int(std::lround(Instruction.Bounds[0])) == X && int(std::lround(Instruction.Bounds[1])) == Y)
                        return Instruction.Colour;
                }
                throw std::runtime_error("Preset motif missing.");
            };
            Confirm(InkAt(Polka, 64, 64)[0] > .8f && InkAt(Polka, 192, 64)[0] < .4f,
                "Polka checkerboard ink alternation broke.");
            Confirm(ConstructPatternLibraryPreset(1).Layers.size() == 18, "Half Drop preset population changed.");
            Confirm(ConstructPatternLibraryPreset(2).Layers.size() == 4, "Stripes preset population changed.");
            const auto Checker = ConstructPatternLibraryPreset(3);
            Confirm(Checker.Layers.size() == 32, "Checker preset is not two 4x4 sublattices.");
            const auto Cells = Centres(Checker);
            Confirm(Cells.count({32, 32}) && Cells.count({96, 96}) && !Cells.count({96, 32}),
                "Checker sublattices interleave incorrectly.");
            Confirm(ConstructPatternLibraryPreset(4).Layers.size() == 16, "Diamond preset population changed.");
            Confirm(ConstructPatternLibraryPreset(5).Layers.size() == 16, "Starfield preset population changed.");
        }
        // SVG encoding mirrors the coverage shader: full Bounds halve to radii (the old card
        // tessellation drew every primitive at 2x), and ThorVG anti-aliases the result.
        {
            const auto Polka = ConstructPatternLibraryPreset(0);
            const std::string Svg = EncodePatternThumbnailSvg(Polka, CardWell);
            Confirm(Svg.find("<ellipse") != std::string::npos && Svg.find("rx=\"44.000\"") != std::string::npos,
                "SVG ellipse radius is not the halved Bounds extent.");
            const std::vector<uint8_t> Tile = RasterizePatternThumbnailSvg(Polka, 512, CardWell);
            Confirm(Tile.size() == size_t(512) * 512 * 4, "Thumbnail payload size changed.");
            const auto Centre = Texel(Tile, 512, 64, 64);
            Confirm(Near(Centre[0], 220) && Near(Centre[1], 118) && Near(Centre[2], 90) && Centre[3] == 255,
                "Dot centre colour changed.");
            const auto Outside = Texel(Tile, 512, 114, 64);
            Confirm(Outside[0] == 20 && Outside[1] == 23 && Outside[2] == 24,
                "Dot painted past its true radius (2x tessellation regression).");
            bool Antialiased = false;
            for (uint32_t X = 100; X <= 116 && !Antialiased; ++X)
            {
                const auto Edge = Texel(Tile, 512, X, 64);
                Antialiased = Edge[0] > 24 && Edge[0] < 216;
            }
            Confirm(Antialiased, "ThorVG thumbnail edge is not anti-aliased.");
            bool Rejected = false;
            try { (void)RasterizePatternThumbnailSvg(Polka, 0, CardWell); }
            catch (const std::exception &) { Rejected = true; }
            Confirm(Rejected, "Thumbnail raster accepted a zero extent.");
        }
        // Layer-row thumbnails: one motif cropped to its own tight extent, never the full 512 tile —
        // the same hugging-bounds contract the selection hitbox relies on, so a small dot still reads
        // clearly inside a ~34px row preview instead of shrinking to a speck.
        {
            const auto Polka = ConstructPatternLibraryPreset(0);
            const uint32_t Token = Polka.Layers.front().Token;
            const std::string LayerSvg = EncodePatternLayerThumbnailSvg(Polka, Token, CardWell);
            Confirm(LayerSvg.find("viewBox=\"0 0 512 512\"") == std::string::npos,
                "Layer thumbnail still frames the full canvas tile.");
            Confirm(LayerSvg.find("<ellipse") != std::string::npos, "Layer thumbnail dropped the motif geometry.");
            const std::vector<uint8_t> LayerTile = RasterizePatternLayerThumbnail(Polka, Token, 64, CardWell);
            Confirm(LayerTile.size() == size_t(64) * 64 * 4, "Layer thumbnail payload size changed.");
            const auto LayerCentre = Texel(LayerTile, 64, 32, 32);
            Confirm(Near(LayerCentre[0], 220) && Near(LayerCentre[1], 118) && Near(LayerCentre[2], 90),
                "Layer thumbnail centre does not hold the motif's own ink.");
            // 📝 At 64px cropped tightly to an 88-unit-wide motif, the dot should cover most of the row
            // preview; the old whole-tile framing would leave only a few-pixel speck near the centre.
            size_t MotifPixels = 0;
            for (uint32_t Y = 0; Y < 64; ++Y)
                for (uint32_t X = 0; X < 64; ++X)
                {
                    const auto Texelled = Texel(LayerTile, 64, X, Y);
                    if (Texelled[0] > 150) ++MotifPixels;
                }
            Confirm(MotifPixels > 64 * 64 / 4, "Layer thumbnail did not crop tightly to its own motif.");
            bool Rejected = false;
            try { (void)EncodePatternLayerThumbnailSvg(Polka, 0xffffffff, CardWell); }
            catch (const std::exception &) { Rejected = true; }
            Confirm(Rejected, "Layer thumbnail accepted an unauthenticated token.");
        }
        // Page blit: slot offsets, replicated gutters, baked background for empty slots.
        const std::vector<uint8_t> Blank = RasterizePatternThumbnailSvg(VectorAuthoringDocument{}, 256, CardWell);
        std::vector<uint8_t> Slot0 = Blank, Slot23 = Blank;
        Slot0[0] = 1; Slot0[1] = 2; Slot0[2] = 3; Slot0[3] = 255;
        const size_t Far = (size_t(255) * 256 + 255) * 4;
        Slot0[Far] = 4; Slot0[Far + 1] = 5; Slot0[Far + 2] = 6; Slot0[Far + 3] = 255;
        Slot23[0] = 7; Slot23[1] = 8; Slot23[2] = 9; Slot23[3] = 255;
        std::vector<std::vector<uint8_t>> Tiles(24, Blank);
        Tiles[0] = Slot0;
        Tiles[23] = Slot23;
        const std::vector<uint8_t> Atlas = BlitPatternThumbnailAtlas(Tiles, 256, 6, CardWell);
        Confirm(Atlas.size() == size_t(1548) * 1548 * 4, "Atlas payload size changed.");
        const auto Slot0Near = Texel(Atlas, 1548, 1, 1);
        Confirm(Slot0Near[0] == 1 && Slot0Near[1] == 2 && Slot0Near[2] == 3 && Slot0Near[3] == 255, "Atlas slot 0 offset changed.");
        const auto Slot0Far = Texel(Atlas, 1548, 256, 256);
        Confirm(Slot0Far[0] == 4 && Slot0Far[1] == 5 && Slot0Far[2] == 6 && Slot0Far[3] == 255, "Atlas slot 0 far corner changed.");
        const auto Gutter = Texel(Atlas, 1548, 0, 1);
        Confirm(Gutter == Slot0Near, "Atlas gutter edge is not replicated.");
        const auto Corner = Texel(Atlas, 1548, 0, 0);
        Confirm(Corner == Slot0Near, "Atlas gutter corner is not replicated.");
        const auto Last = Texel(Atlas, 1548, 5 * 258 + 1, 3 * 258 + 1);
        Confirm(Last[0] == 7 && Last[1] == 8 && Last[2] == 9 && Last[3] == 255, "Atlas slot 23 offset changed.");
        const auto Empty = Texel(Atlas, 1548, 258 + 11, 11);
        Confirm(Empty[0] == 20 && Empty[1] == 23 && Empty[2] == 24 && Empty[3] == 255, "Atlas empty slot lost its baked background.");
        bool Rejected = false;
        try
        {
            std::vector<std::vector<uint8_t>> Ragged = Tiles;
            Ragged[5].resize(100);
            (void)BlitPatternThumbnailAtlas(Ragged, 256, 6, CardWell);
        }
        catch (const std::exception &) { Rejected = true; }
        Confirm(Rejected, "Atlas blit accepted a ragged thumb.");
        Rejected = false;
        try { (void)BlitPatternThumbnailAtlas(std::vector<std::vector<uint8_t>>(37, Blank), 256, 6, CardWell); }
        catch (const std::exception &) { Rejected = true; }
        Confirm(Rejected, "Atlas blit exceeded its grid capacity.");
        // Headless panel: the open document's own layer-row thumbnail atlas bakes once per edit
        // revision; redundant queries never re-bake, and an edit (or clearing the document) advances it.
        {
            PatternWorkspacePanel RowWorkspace;
            RowWorkspace.ConfigureDocument(ConstructPatternLibraryPreset(0));
            Confirm(PatternWorkspacePanel::QueryLayerAtlasExtent() == (64 + 2) * 12, "Layer atlas extent is not the settled 792.");
            const uint64_t FirstLayerAtlas = RowWorkspace.QueryLayerAtlasRevision();
            Confirm(FirstLayerAtlas > 0, "Layer atlas revision never advanced past zero for a populated document.");
            const auto &LayerPixels = RowWorkspace.QueryLayerAtlasPixels();
            Confirm(LayerPixels.size() == size_t(792) * 792 * 4, "Layer atlas payload size changed.");
            Confirm(RowWorkspace.QueryLayerAtlasRevision() == FirstLayerAtlas, "Layer atlas revision moved without a document edit.");
            bool LayerArtwork = false;
            for (uint32_t Y = 1; Y <= 64 && !LayerArtwork; ++Y)
                for (uint32_t X = 1; X <= 64; ++X)
                {
                    const auto Card = Texel(LayerPixels, 792, X, Y);
                    if (Card[0] != 20 || Card[1] != 23 || Card[2] != 24 || Card[3] != 255) { LayerArtwork = true; break; }
                }
            Confirm(LayerArtwork, "Layer atlas lead slot holds no baked artwork for the first layer.");
            RowWorkspace.ConfigureDocument({});
            Confirm(RowWorkspace.QueryLayerAtlasRevision() == FirstLayerAtlas,
                "Clearing to an empty document should not advance a revision that never re-bakes an empty atlas.");
            Confirm(RowWorkspace.QueryLayerAtlasPixels().empty(), "Empty document should leave the layer atlas empty.");
        }
        // Headless panel: one library-wide atlas baked once; filters and page flips only re-point
        // card UVs, so the revision (and GPU upload) never moves after the first bake.
        PatternWorkspacePanel Workspace;
        Confirm(Workspace.ConfigurePresetDirectory("EngineContent/PatternArchives/TextureStudio"), "Atlas panel rejected the pattern library.");
        Confirm(PatternWorkspacePanel::QueryPatternAtlasExtent() == 1560, "Atlas extent is not the settled 1560.");
        const uint64_t First = Workspace.QueryPatternAtlasRevision();
        Confirm(First > 0, "Atlas revision never advanced past zero.");
        const auto &Pixels = Workspace.QueryPatternAtlasPixels();
        Confirm(Pixels.size() == size_t(1560) * 1560 * 4, "Atlas page payload size changed.");
        Confirm(Workspace.QueryPatternAtlasRevision() == First, "Atlas revision moved without new bakes.");
        bool Artwork = false;
        for (uint32_t Y = 1; Y <= 128 && !Artwork; ++Y)
            for (uint32_t X = 1; X <= 128; ++X)
            {
                const auto Card = Texel(Pixels, 1560, X, Y);
                if (Card[0] != 20 || Card[1] != 23 || Card[2] != 24 || Card[3] != 255) { Artwork = true; break; }
            }
        Confirm(Artwork, "Atlas lead tile holds no baked artwork.");
        Confirm(Texel(Pixels, 1560, 0, 1) == Texel(Pixels, 1560, 1, 1), "Atlas page gutter is not replicated.");
        size_t ArtworkTiles = 0;
        for (size_t Slot = 0; Slot < 144; ++Slot)
        {
            const auto Card = Texel(Pixels, 1560, uint32_t((Slot % 12) * 130 + 65), uint32_t((Slot / 12) * 130 + 65));
            if (Card[0] != 20 || Card[1] != 23 || Card[2] != 24 || Card[3] != 255) ++ArtworkTiles;
        }
        Confirm(ArtworkTiles > 24, "Atlas holds no more than one page of baked artwork.");
        Workspace.ConfigureLibraryFilter("Geometric constructions", "", true);
        Confirm(Workspace.QueryPatternAtlasRevision() == First, "Filter change rebaked the settled atlas.");
        for (size_t Page = 0; Page < 5; ++Page)
        {
            Workspace.ConfigureLibraryPage(Page);
            Confirm(Workspace.QueryPatternAtlasRevision() == First, "Page flip rebaked the settled atlas.");
        }
        Workspace.ConfigureLibraryPage(0);
        Workspace.ConfigureLibraryFilter("All patterns", "", true);
        Confirm(Workspace.QueryPatternAtlasRevision() == First, "Filter reset rebaked the settled atlas.");
        std::cout << "{\"result\":\"passed\",\"checks\":" << Assertions << "}\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cout << "{\"result\":\"failed\",\"error\":\"" << Failure.what() << "\"}\n";
        return 1;
    }
}
