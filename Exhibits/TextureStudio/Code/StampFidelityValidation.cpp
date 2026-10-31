/*============================================================================================================================================
                                                        STAMPFIDELITYVALIDATION.CPP
============================================================================================================================================*/
// 🖨️ A placed decal must look like the stamp it came from: this checks the whole fidelity chain
//    (authored colours -> .stamp.toml round-trip -> ThorVG stamp raster) keeps red red and blue blue,
//    at more than one extent. Regression cover for "decals stamp as a solid black silhouette".

#include "Engine/ContentInterchange/StampTextOutline.h"
#include "Engine/ContentInterchange/VectorAuthoringDocument.h"
#include "Engine/ContentInterchange/VectorAuthoringSelectionEditing.h"
#include "UI/TextureStudio/PatternThumbnailRaster.h"
#include "UI/TextureStudio/StampWorkspacePanel.h" // MeasureStampTextFitSize
#include "UI/TextureStudio/TypefaceCatalogue.h"
#include <cstdio>
#include <stdexcept>

namespace
{
using namespace Frontier;
uint32_t Checks = 0;
void Confirm(bool Condition, const char *Caption)
{
    if (!Condition) throw std::runtime_error(Caption);
    ++Checks;
}

VectorAuthoringDocument ConstructColouredStamp()
{
    VectorAuthoringDocument Document;
    Document.Stamp = true;
    Document.Name = "Fidelity probe";
    Document.TokenClock = 8;
    VectorCoverageInstruction Square, Ellipse, Path;
    Square.Bounds = {150, 150, 160, 160};
    Square.Colour = {1, 0, 0, 1}; // red rect, top-left
    Ellipse.Topology[0] = 1;
    Ellipse.Bounds = {370, 150, 160, 160};
    Ellipse.Colour = {0, .75f, 0, 1}; // green ellipse, top-right
    Path.Topology[0] = 6;            // cubic path (pen shape), bottom centre
    Path.Bounds = {256, 380, 200, 180};
    Path.Colour = {0, 0, 1, 1}; // blue
    Path.Topology[1] = 0;       // segment start
    Path.Topology[2] = 3;       // segment count
    Document.Instructions = {Square, Ellipse, Path};
    // Triangle in unit-cage space (scaled by Bounds.zw): top -> bottom-right -> bottom-left -> close.
    const auto Leg = [](float Ax, float Ay, float Dx, float Dy) {
        VectorCubicSegment Segment;
        Segment.StartControl = {Ax, Ay, Ax + (Dx - Ax) / 3, Ay + (Dy - Ay) / 3};
        Segment.ControlEnd = {Ax + (Dx - Ax) * 2 / 3, Ay + (Dy - Ay) * 2 / 3, Dx, Dy};
        return Segment;
    };
    Document.Segments = {Leg(0, -.45f, .45f, .4f), Leg(.45f, .4f, -.45f, .4f), Leg(-.45f, .4f, 0, -.45f)};
    VectorAuthoringLayer Red, Green, Blue;
    Red.Token = 1; Red.Name = "Red"; Red.Root = 0;
    Green.Token = 2; Green.Name = "Green"; Green.Root = 1;
    Blue.Token = 3; Blue.Name = "Blue"; Blue.Root = 2;
    Document.Layers = {Red, Green, Blue};
    return Document;
}

struct Histogram
{
    size_t Opaque = 0, Red = 0, Green = 0, Blue = 0, Black = 0, White = 0;
};
Histogram Analyse(const std::vector<uint8_t> &Pixels)
{
    Histogram Result;
    for (size_t At = 0; At + 3 < Pixels.size(); At += 4)
    {
        const uint8_t R = Pixels[At], G = Pixels[At + 1], B = Pixels[At + 2], A = Pixels[At + 3];
        if (A < 200) continue;
        ++Result.Opaque;
        if (R > 180 && G < 90 && B < 90) ++Result.Red;
        else if (G > 140 && R < 90 && B < 90) ++Result.Green;
        else if (B > 180 && R < 90 && G < 90) ++Result.Blue;
        else if (R < 40 && G < 40 && B < 40) ++Result.Black;
        else if (R > 215 && G > 215 && B > 215) ++Result.White;
    }
    return Result;
}
void Report(const char *Label, const Histogram &H)
{
    std::printf("%s: opaque=%zu red=%zu green=%zu blue=%zu black=%zu white=%zu\n",
        Label, H.Opaque, H.Red, H.Green, H.Blue, H.Black, H.White);
}
} // namespace

int main()
{
    try
    {
        const auto Document = ConstructColouredStamp();
        {
            VectorAuthoringDocument Stroke; Stroke.Stamp = true; Stroke.TokenClock = 1;
            VectorCoverageInstruction Rectangle;
            Rectangle.Bounds = {256, 256, 256, 256}; Rectangle.Transform[1] = 24;
            Rectangle.Paint[3] = 0; Rectangle.ShapeParameters[1] = 1;
            Rectangle.Topology[3] |= VectorCoverageStrokeColourExplicit;
            Rectangle.StrokeColour = {1, 0, 0, 1}; Rectangle.StrokeColourEnd = {0, 0, 1, 1};
            Stroke.Instructions.push_back(Rectangle);
            VectorAuthoringLayer Layer; Layer.Token = 1; Layer.Root = 0; Layer.Name = "Stroke";
            Stroke.Layers.push_back(Layer);
            const auto Pixels = RasterizeStampDocument(DecodeVectorAuthoring(EncodeVectorAuthoring(Stroke)), 256);
            const size_t Left = (size_t(64) * 256 + 80) * 4, Right = (size_t(64) * 256 + 176) * 4;
            Confirm(Pixels[Left] > Pixels[Left + 2] && Pixels[Right + 2] > Pixels[Right], "Exported stroke gradient did not vary across the ramp.");
        }
        const auto Direct = Analyse(RasterizeStampDocument(Document, 256));
        Report("direct256", Direct);
        Confirm(Direct.Red > 400, "Red rect missing from the direct stamp raster.");
        Confirm(Direct.Green > 300, "Green ellipse missing from the direct stamp raster.");
        Confirm(Direct.Blue > 300, "Blue path missing from the direct stamp raster.");

        const auto Decoded = DecodeVectorAuthoring(EncodeVectorAuthoring(Document));
        const auto RoundTrip = Analyse(RasterizeStampDocument(Decoded, 256));
        Report("roundtrip256", RoundTrip);
        Confirm(RoundTrip.Red > 400, "Red rect lost through the .stamp.toml round-trip (solid-colour decal bug).");
        Confirm(RoundTrip.Green > 300, "Green ellipse lost through the .stamp.toml round-trip.");
        Confirm(RoundTrip.Blue > 300, "Blue path lost through the .stamp.toml round-trip.");
        Confirm(RoundTrip.Black < RoundTrip.Opaque / 4, "Round-tripped stamp collapsed toward black.");

        const auto Large = Analyse(RasterizeStampDocument(Decoded, 1024));
        Report("roundtrip1024", Large);
        Confirm(Large.Red > 6000, "Red rect missing at 1024px (Texture Paint footprint extents).");

        // 🅰️ Texture Paint's Text decal synthesis, replicated step for step from
        // TexturePaintWorkspacePanel::SynthesizeTextEntry: fit the copy to the stamp square, bake
        // white glyph outlines, centre them, rasterize. The ink must be white (so brush-colour
        // tinting owns the colour), substantial and centred - never empty or a black silhouette.
        {
            const std::string Content = "WANTED";
            constexpr float Box = 470.f;
            const float PointSize = MeasureStampTextFitSize(Content, Box, Box, {});
            VectorAuthoringDocument Text;
            Text.Stamp = true;
            const uint32_t Root = AppendStampTextOutline(Text, Content, PointSize, {1.f, 1.f, 1.f, 1.f},
                "EngineContent/FontArchives/DMSans/DMSans-Regular.ttf", Box);
            VectorAuthoringLayer Layer;
            Layer.Token = ++Text.TokenClock;
            Layer.Root = Root;
            Layer.ComponentKind = VectorAuthoringComponentKind::Text;
            Layer.SourceMarkup = Content;
            Layer.TextPointSize = PointSize;
            Text.Layers.push_back(Layer);
            ReclaimVectorAuthoringSources(Text);
            const auto Bounds = QueryVectorAuthoringSelectionBounds(Text, std::array<uint32_t, 1>{Layer.Token});
            VectorAuthoringSourcePatch Centre;
            Centre.TranslationX = 256.f - Bounds[0];
            Centre.TranslationY = 256.f - Bounds[1];
            TransformVectorAuthoringSelection(Text, std::array<uint32_t, 1>{Layer.Token}, Centre);
            const auto Pixels = RasterizeStampDocument(Text, 256);
            const auto Ink = Analyse(Pixels);
            Report("text256", Ink);
            Confirm(Ink.White > 1500, "Text decal has no white glyph ink at 256px.");
            Confirm(Ink.Black < Ink.Opaque / 8, "Text decal rasterized toward black (tinting would crush it).");
            Confirm(Ink.Opaque > 2000 && Ink.Opaque < 40000, "Text decal coverage is implausible for 'WANTED'.");
            double SumX = 0, SumY = 0;
            size_t Counted = 0;
            for (int Y = 0; Y < 256; ++Y)
                for (int X = 0; X < 256; ++X)
                    if (Pixels[(size_t(Y) * 256 + X) * 4 + 3] >= 200) { SumX += X; SumY += Y; ++Counted; }
            Confirm(Counted > 0, "Text decal raster is empty.");
            const double CentroidX = SumX / Counted / 255.0, CentroidY = SumY / Counted / 255.0;
            std::printf("text256 centroid: %.3f, %.3f\n", CentroidX, CentroidY);
            Confirm(std::abs(CentroidX - .5) < .12 && std::abs(CentroidY - .5) < .16, "Text decal ink is not centred in its stamp square.");
        }

        // 🅰 Font catalogue + selectable faces (what the Text tool's right-click properties and
        // the Stamp Inspector's font picker are built on): the shipped archives must enumerate
        // several families, every listed face must actually bake glyph outlines (the glyf filter
        // keeps CFF faces out), two faces must render different ink for the same copy, an unknown
        // id must resolve to the default face, and a text layer's stored face must round-trip.
        {
            const auto &Faces = QueryTypefaceChoices({});
            Confirm(!Faces.empty(), "FontArchives enumerated no bakeable faces.");
            size_t Families = 0;
            std::string Previous;
            for (const auto &Face : Faces)
                if (Face.Family != Previous) { Previous = Face.Family; ++Families; }
            Confirm(Families >= 3, "Font catalogue covers fewer than three families.");
            const auto *Default = FindTypefaceChoice({}, DefaultTypefaceId);
            Confirm(Default != nullptr, "Default face missing from the catalogue.");
            const auto Unknown = ResolveTypefacePath({}, "Missing/Nothing-Regular");
            Confirm(Default && Unknown == Default->Path.string(), "Unknown face id did not fall back to the default face.");
            const TypefaceChoice *Other = nullptr;
            for (const auto &Face : Faces)
                if (Face.Id != DefaultTypefaceId) { Other = &Face; break; }
            Confirm(Other != nullptr, "No non-default face to compare against.");
            if (Other && Default)
            {
                const auto A = RenderTypefacePreview(Default->Path.string(), "Rag 123", 256);
                const auto B = RenderTypefacePreview(Other->Path.string(), "Rag 123", 256);
                Confirm(A.size() == 256u * 256u * 4 && B.size() == A.size(), "Typeface preview raster size wrong.");
                size_t InkA = 0, InkB = 0, Differ = 0;
                for (size_t Texel = 0; Texel < A.size() / 4; ++Texel)
                {
                    InkA += A[Texel * 4 + 3] >= 128;
                    InkB += B[Texel * 4 + 3] >= 128;
                    Differ += std::abs(int(A[Texel * 4 + 3]) - int(B[Texel * 4 + 3])) > 64;
                }
                std::printf("typeface ink: default %zu, %s %zu, differing %zu\n", InkA, Other->Id.c_str(), InkB, Differ);
                Confirm(InkA > 500 && InkB > 500, "A catalogue face baked no preview ink.");
                Confirm(Differ > 200, "Two faces rendered the same ink - font selection is cosmetic.");
                size_t Baked = 0;
                for (const auto &Face : Faces)
                {
                    const auto Pixels = RenderTypefacePreview(Face.Path.string(), "Ag", 128);
                    size_t Ink = 0;
                    for (size_t Texel = 0; Texel < Pixels.size() / 4; ++Texel) Ink += Pixels[Texel * 4 + 3] >= 128;
                    Baked += Ink > 100;
                }
                std::printf("typeface catalogue: %zu faces, %zu bake ink\n", Faces.size(), Baked);
                Confirm(Baked == Faces.size(), "A listed face cannot bake glyph outlines (filter leaked a CFF face).");
                VectorAuthoringDocument Faced;
                Faced.Stamp = true;
                const uint32_t Root = AppendStampTextOutline(Faced, "FACE", 64.f, {1.f, 1.f, 1.f, 1.f}, Other->Path.string(), 470.f);
                VectorAuthoringLayer Layer;
                Layer.Token = ++Faced.TokenClock;
                Layer.Root = Root;
                Layer.ComponentKind = VectorAuthoringComponentKind::Text;
                Layer.SourceMarkup = "FACE";
                Layer.TextPointSize = 64.f;
                Layer.TextTypeface = Other->Id;
                Faced.Layers.push_back(Layer);
                const auto Round = DecodeVectorAuthoring(EncodeVectorAuthoring(Faced));
                Confirm(Round.Layers.size() == 1 && Round.Layers[0].TextTypeface == Other->Id,
                    "Text layer's typeface id did not survive the .stamp.toml round-trip.");
            }
        }
        {
            // Verify 9-channel PBR stamp surface map evaluation (signed relief, blind emboss, AO, clearcoat, transmission).
            VectorAuthoringDocument PbrDoc;
            PbrDoc.Stamp = true;
            VectorCoverageInstruction BaseDisc{};
            BaseDisc.Topology = {1, 0, 0, 0};
            BaseDisc.Bounds = {256.f, 256.f, 340.f, 340.f};
            BaseDisc.Transform = {0.f, 0.f, 1.f, 0.f};
            BaseDisc.Colour = BaseDisc.ColourEnd = {.82f, .64f, .22f, 1.f};
            BaseDisc.Paint = {0.f, .45f, 6.f, 1.f};
            PbrDoc.Instructions.push_back(BaseDisc);
            VectorAuthoringLayer Brass;
            Brass.Token = ++PbrDoc.TokenClock;
            Brass.Root = 0;
            Brass.SurfaceRelief = 1.f;
            Brass.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Height)].Strength = .6f;
            Brass.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Roughness)].Strength = .22f;
            Brass.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Metalness)].Strength = .95f;
            Brass.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Occlusion)].Enabled = true;
            Brass.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Occlusion)].Strength = .4f;
            Brass.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Clearcoat)].Enabled = true;
            Brass.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Clearcoat)].Strength = .85f;
            PbrDoc.Layers.push_back(Brass);

            VectorCoverageInstruction DebossBar{};
            DebossBar.Topology = {0, 0, 0, 0};
            DebossBar.Bounds = {256.f, 256.f, 140.f, 140.f};
            DebossBar.Transform = {0.f, 0.f, 1.f, 0.f};
            DebossBar.Colour = DebossBar.ColourEnd = {1.f, 1.f, 1.f, 1.f};
            DebossBar.Paint = {0.f, .45f, 6.f, 1.f};
            PbrDoc.Instructions.push_back(DebossBar);
            VectorAuthoringLayer BlindDeboss;
            BlindDeboss.Token = ++PbrDoc.TokenClock;
            BlindDeboss.Root = 1;
            BlindDeboss.SurfaceRelief = -1.f;
            BlindDeboss.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Colour)].Enabled = false;
            BlindDeboss.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Roughness)].Enabled = false;
            BlindDeboss.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Metalness)].Enabled = false;
            BlindDeboss.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Height)].Strength = .9f;
            BlindDeboss.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Transmission)].Enabled = true;
            BlindDeboss.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Transmission)].Strength = .5f;
            PbrDoc.Layers.push_back(BlindDeboss);

            const auto Maps = EvaluateStampSurfaceMaps(PbrDoc, 128);
            const size_t Centre = 64 * 128 + 64;
            const size_t Rim = 64 * 128 + 30;
            Confirm(Maps.Coverage[Centre] > .98f && Maps.Coverage[Rim] > .98f, "Stamp PBR coverage missed disc or deboss.");
            Confirm(Maps.Height[Rim] > .5f && Maps.Height[Centre] < -.2f,
                "Signed SurfaceRelief did not emboss rim (+0.6) and deboss centre (-0.3).");
            Confirm(Maps.ColourRgba[Centre * 4 + 0] > 180 && Maps.ColourRgba[Centre * 4 + 2] < 90,
                "Blind-deboss layer with Colour.Enabled=false overwrote underlying brass colour.");
            Confirm(std::abs(Maps.Roughness[Rim] - .22f) < .02f && std::abs(Maps.Metalness[Rim] - .95f) < .02f,
                "Stamp PBR Roughness/Metalness maps did not evaluate layer strengths.");
            Confirm(std::abs(Maps.Occlusion[Rim] - .6f) < .02f && std::abs(Maps.Clearcoat[Rim] - .85f) < .02f &&
                std::abs(Maps.Transmission[Centre] - .5f) < .02f,
                "Stamp PBR Occlusion/Clearcoat/Transmission maps did not evaluate layer strengths.");
        }
        std::printf("StampFidelityValidation: %u checks passed\n", Checks);
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::printf("StampFidelityValidation FAILED: %s\n", Failure.what());
        return 1;
    }
}
