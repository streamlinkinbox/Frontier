/*============================================================================================================================================
                                                         PATTERNTHUMBNAILRASTER.CPP
============================================================================================================================================*/
// 🧩 Gallery thumbnails are real SVG documents rasterized by ThorVG: one vector encoding of the
// authoring document, anti-aliased at whatever extent the atlas asks for. No home-grown scanline
// fill remains; primitive half-extents match the coverage shader (Bounds.zw * .5).

#include "UI/TextureStudio/PatternThumbnailRaster.h"
#include "Engine/ContentInterchange/VectorAuthoringEditing.h"
#include "Engine/ContentInterchange/VectorGradientStops.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <thorvg.h>

namespace Frontier
{
namespace
{
struct ReleasePaint
{
    void operator()(tvg::Picture *Picture) const { tvg::Paint::rel(Picture); }
};

void EnsureThorVg()
{
    static const bool Ready = tvg::Initializer::init(0) == tvg::Result::Success;
    if (!Ready) throw std::runtime_error("ThorVG runtime initialization failed.");
}

std::string Number(float Value)
{
    char Buffer[32]{};
    std::snprintf(Buffer, sizeof(Buffer), "%.3f", double(Value));
    return Buffer;
}

std::string ColourLiteral(const std::array<float, 4> &Colour)
{
    const auto Channel = [](float Value) { return int(std::lround(std::clamp(Value, 0.f, 1.f) * 255)); };
    char Buffer[32]{};
    std::snprintf(Buffer, sizeof(Buffer), "rgb(%d,%d,%d)", Channel(Colour[0]), Channel(Colour[1]), Channel(Colour[2]));
    return Buffer;
}

// 📝 Linear/radial gradient fills are a real <linearGradient>/<radialGradient> def (gradientUnits=
// "userSpaceOnUse", so its coordinates live in the same absolute tile-space as everything else this file
// emits), its x1/y1/x2/y2 (or cx/cy/r) derived from the shape's own GradientAxis A/B control points via
// the same rotate-then-translate placement as every other primitive below - so what the gallery thumbnail
// shows is the exact axis the user dragged, not a fixed corner-to-corner guess. Returns the "url(#id)"
// reference to use as fill, or an empty string if this instruction isn't a gradient fill.
std::string EmitPatternGradientDef(const VectorCoverageInstruction &Source, uint32_t &DefSerial, float Opacity,
    const std::function<std::array<float, 2>(float, float)> &Place, std::string &Defs, bool Stroke = false)
{
    const uint32_t Category = uint32_t(Source.Transform[3]);
    if (Category != 1 && Category != 2) return {};
    // The shader measures gradients in bounds-normalized coordinates. Transform that
    // coordinate system to tile space for SVG too, including non-square and rotated shapes.
    const auto Origin = Place(0, 0);
    const auto X = Place(Source.Bounds[2], 0), Y = Place(0, Source.Bounds[3]);
    const std::string Matrix = "matrix(" + Number(X[0] - Origin[0]) + " " + Number(X[1] - Origin[1]) + " " +
        Number(Y[0] - Origin[0]) + " " + Number(Y[1] - Origin[1]) + " " + Number(Origin[0]) + " " + Number(Origin[1]) + ")";
    const std::string Id = (Stroke ? "pattern-stroke-gradient-" : "pattern-gradient-") + std::to_string(DefSerial++);
    const auto &Axis = Source.GradientAxis;
    std::string Def;
    if (Category == 1)
        Def = "<linearGradient id=\"" + Id + "\" gradientUnits=\"userSpaceOnUse\" gradientTransform=\"" + Matrix +
              "\" x1=\"" + Number(Axis[0]) + "\" y1=\"" + Number(Axis[1]) + "\" x2=\"" + Number(Axis[2]) + "\" y2=\"" + Number(Axis[3]) + "\">";
    else
    {
        const float Radius = std::max(std::hypot(Axis[2] - Axis[0], Axis[3] - Axis[1]), .0001f);
        Def = "<radialGradient id=\"" + Id + "\" gradientUnits=\"userSpaceOnUse\" gradientTransform=\"" + Matrix +
              "\" cx=\"" + Number(Axis[0]) + "\" cy=\"" + Number(Axis[1]) + "\" r=\"" + Number(Radius) + "\">";
    }
    const auto Stops = QueryVectorGradientStops(Source);
    const uint32_t Mode = uint32_t(std::clamp(std::lround(Source.ShapeParameters[2]), 0l, 4l));
    if (Mode == 0)
    {
        for (const auto &Stop : Stops)
            Def += "<stop offset=\"" + Number(std::clamp(Stop.Position, 0.f, 1.f) * 100.f) + "%\" stop-color=\"" +
                   ColourLiteral(Stop.Colour) + "\" stop-opacity=\"" + Number(std::clamp(Stop.Colour[3] * Opacity, 0.f, 1.f)) + "\"/>";
    }
    else
    {
        constexpr int SampleCount = 24;
        for (int Step = 0; Step <= SampleCount; ++Step)
        {
            const float Fraction = float(Step) / float(SampleCount);
            const auto Sampled = EvaluateVectorGradientColour(Stops, Fraction, Mode);
            Def += "<stop offset=\"" + Number(Fraction * 100.f) + "%\" stop-color=\"" + ColourLiteral(Sampled) +
                   "\" stop-opacity=\"" + Number(std::clamp(Sampled[3] * Opacity, 0.f, 1.f)) + "\"/>";
        }
    }
    Def += Category == 1 ? "</linearGradient>" : "</radialGradient>";
    Defs += Def;
    return "url(#" + Id + ")";
}

std::string PatternInstructionStyle(const VectorCoverageInstruction &Source, float Opacity, const std::string &GradientFill,
    const std::string &GradientStroke, bool ForceNoFill = false, bool SuppressStroke = false)
{
    std::string Text;
    if (Source.Paint[3] != 0 && !ForceNoFill)
    {
        if (!GradientFill.empty()) Text += " fill=\"" + GradientFill + "\"";
        else Text += " fill=\"" + ColourLiteral(Source.Colour) + "\" fill-opacity=\"" + Number(std::clamp(Source.Colour[3] * Opacity, 0.f, 1.f)) + "\"";
    }
    else
        Text += " fill=\"none\"";
    // 📝 Closed+filled cubic contours read their independent Paint[0] outline width - shared rule in
    // Engine/Render/VectorCoverage.h (QueryVectorCoverageStrokeWidth), mirrored by the GPU shaders.
    const float StrokeWidth = SuppressStroke ? 0.f : QueryVectorCoverageStrokeWidth(Source);
    if (StrokeWidth > 0)
    {
        const bool Explicit = (Source.Topology[3] & VectorCoverageStrokeColourExplicit) != 0;
        const auto &Line = Explicit ? Source.StrokeColour : Source.Colour;
        if (!GradientStroke.empty()) Text += " stroke=\"" + GradientStroke + "\"";
        else Text += " stroke=\"" + ColourLiteral(Line) + "\" stroke-opacity=\"" + Number(std::clamp(Line[3] * Opacity, 0.f, 1.f)) + "\"";
        Text += " stroke-width=\"" + Number(StrokeWidth) +
                "\" stroke-linejoin=\"round\" stroke-linecap=\"round\"";
    }
    return Text;
}

std::string EncodeLeafShapeElement(const VectorAuthoringDocument &Document, const VectorCoverageInstruction &Source, const std::string &Paint)
{
    const uint32_t Category = Source.Topology[0];
    const float Rotation = Source.Transform[0];
    const float Sine = std::sin(Rotation), Cosine = std::cos(Rotation);
    const auto Place = [&](float LocalX, float LocalY)
    {
        return std::array<float, 2>{Source.Bounds[0] + Cosine * LocalX - Sine * LocalY,
            Source.Bounds[1] + Sine * LocalX + Cosine * LocalY};
    };
    const float HalfX = Source.Bounds[2] * .5f, HalfY = Source.Bounds[3] * .5f;
    if (Category == 0)
    {
        const float Radius = std::min(Source.ShapeParameters[0], std::min(HalfX, HalfY));
        return "<rect x=\"" + Number(Source.Bounds[0] - HalfX) + "\" y=\"" + Number(Source.Bounds[1] - HalfY) +
            "\" width=\"" + Number(Source.Bounds[2]) + "\" height=\"" + Number(Source.Bounds[3]) +
            "\" rx=\"" + Number(Radius) + "\" transform=\"rotate(" + Number(Rotation * 57.2957795131f) + " " +
            Number(Source.Bounds[0]) + " " + Number(Source.Bounds[1]) + ")\"" + Paint + "/>";
    }
    if (Category == 1)
    {
        return "<ellipse cx=\"" + Number(Source.Bounds[0]) + "\" cy=\"" + Number(Source.Bounds[1]) + "\" rx=\"" +
               Number(HalfX) + "\" ry=\"" + Number(HalfY) + "\" transform=\"rotate(" +
               Number(Rotation * 57.2957795131f) + " " + Number(Source.Bounds[0]) + " " +
               Number(Source.Bounds[1]) + ")\"" + Paint + "/>";
    }
    if (Category <= 5)
    {
        uint32_t Count = Category == 2 ? 4 : Category == 3 ? 3 : uint32_t(std::max(Source.Paint[2], 3.f));
        if (Category == 5) Count *= 2;
        std::string Text = "<polygon points=\"";
        for (uint32_t Corner = 0; Corner < Count; ++Corner)
        {
            const float Angle = -1.57079632679f + 6.28318530718f * float(Corner) / float(Count);
            const float Radius = Category == 5 && Corner % 2 == 1 ? Source.Paint[1] : 1.f;
            const auto Point = Place(std::cos(Angle) * HalfX * Radius, std::sin(Angle) * HalfY * Radius);
            Text += Number(Point[0]) + "," + Number(Point[1]) + " ";
        }
        Text += "\"" + Paint + "/>";
        return Text;
    }
    const uint32_t Start = Source.Topology[1], Count = Source.Topology[2];
    if (Count == 0 || uint64_t(Start) + Count > Document.Segments.size()) return {};
    const auto Cage = [&](float X, float Y) { return Place(X * Source.Bounds[2], Y * Source.Bounds[3]); };
    std::string Path = "<path d=\"";
    bool Pen = false;
    std::array<float, 2> Last{0, 0}, First{0, 0};
    for (uint32_t Offset = 0; Offset < Count; ++Offset)
    {
        const auto &Segment = Document.Segments[Start + Offset];
        const auto A = Cage(Segment.StartControl[0], Segment.StartControl[1]);
        const auto B = Cage(Segment.StartControl[2], Segment.StartControl[3]);
        const auto C = Cage(Segment.ControlEnd[0], Segment.ControlEnd[1]);
        const auto D = Cage(Segment.ControlEnd[2], Segment.ControlEnd[3]);
        if (!Pen || std::hypot(Last[0] - A[0], Last[1] - A[1]) > 1e-3f)
        {
            Path += "M" + Number(A[0]) + " " + Number(A[1]) + " ";
            if (!Pen) First = A;
            Pen = true;
        }
        Path += "C" + Number(B[0]) + " " + Number(B[1]) + " " + Number(C[0]) + " " + Number(C[1]) + " " +
                Number(D[0]) + " " + Number(D[1]) + " ";
        Last = D;
    }
    if (Pen && std::hypot(Last[0] - First[0], Last[1] - First[1]) <= 1.5f) Path += "Z";
    Path += "\"" + Paint + "/>";
    return Path;
}

// 📝 Shared recursive emitter: appends one instruction (and, for Boolean/composite roots, both
// children) to Svg in absolute tile-space coordinates. Used both for the whole-document gallery
// encoding and the single-layer row-thumbnail encoding below, so the two never drift apart.
void EmitPatternInstruction(const VectorAuthoringDocument &Document, uint32_t Root, uint32_t Depth, std::string &Svg, std::string &Defs, uint32_t &DefSerial)
{
    if (Root >= Document.Instructions.size() || Depth > 16) return;
    const auto &Source = Document.Instructions[Root];
    const uint32_t Category = Source.Topology[0];
    if (Category > 6)
    {
        if (Category == 29)
        {
            Svg += "<g opacity=\"" + Number(Source.Transform[2]) + "\">";
            EmitPatternInstruction(Document, Source.Topology[1], Depth + 1, Svg, Defs, DefSerial);
            Svg += "</g>";
            return;
        }
        // 📝 Boolean/composite roots emit both children in order, matching card paint order.
        EmitPatternInstruction(Document, Source.Topology[1], Depth + 1, Svg, Defs, DefSerial);
        if (Source.Topology[0] != 17 && Source.Topology[0] != 24 && Source.Topology[0] != 25)
            EmitPatternInstruction(Document, Source.Topology[2], Depth + 1, Svg, Defs, DefSerial);
        return;
    }
    const float Opacity = Source.Transform[2];
    if (!(Opacity > 0) || !std::isfinite(Opacity)) return;
    const float Rotation = Source.Transform[0];
    const float Sine = std::sin(Rotation), Cosine = std::cos(Rotation);
    const auto Place = [&](float LocalX, float LocalY)
    {
        return std::array<float, 2>{Source.Bounds[0] + Cosine * LocalX - Sine * LocalY,
            Source.Bounds[1] + Sine * LocalX + Cosine * LocalY};
    };
    const auto PlaceUnrotated = [&](float LocalX, float LocalY)
    {
        return std::array<float, 2>{Source.Bounds[0] + LocalX, Source.Bounds[1] + LocalY};
    };
    const std::function<std::array<float, 2>(float, float)> GradientPlace = (Category == 1 || Category == 0) ? std::function<std::array<float, 2>(float, float)>(PlaceUnrotated) : std::function<std::array<float, 2>(float, float)>(Place);
    const std::string GradientStroke = EmitPatternGradientDef(QueryVectorStrokeGradientSource(Source), DefSerial, Opacity, GradientPlace, Defs, true);
    if (uint32_t(Source.Transform[3]) == 3 && Source.Paint[3] != 0)
    {
        const std::string ClipShape = EncodeLeafShapeElement(Document, Source, "");
        if (!ClipShape.empty())
        {
            const std::string ClipId = "pattern-clip-" + std::to_string(DefSerial++);
            Defs += "<clipPath id=\"" + ClipId + "\">" + ClipShape + "</clipPath>";
            const auto FreeformPoints = QueryVectorFreeformPoints(Source);
            const float Sharpness = Source.FreeformParameters[1];
            const uint32_t FillMode = uint32_t(std::clamp(std::lround(Source.ShapeParameters[2]), 0l, 4l));
            constexpr int Subdivisions = 20;
            const float HalfX = Source.Bounds[2] * .5f, HalfY = Source.Bounds[3] * .5f;
            const float CellW = Source.Bounds[2] / float(Subdivisions), CellH = Source.Bounds[3] / float(Subdivisions);
            const float OverlapW = CellW * 1.18f, OverlapH = CellH * 1.18f;
            Svg += "<g clip-path=\"url(#" + ClipId + ")\"><g transform=\"translate(" + Number(Source.Bounds[0]) + " " +
                   Number(Source.Bounds[1]) + ") rotate(" + Number(Rotation * 57.2957795131f) + ")\">";
            for (int Row = 0; Row < Subdivisions; ++Row)
            {
                const float NormY = -.5f + (float(Row) + .5f) / float(Subdivisions);
                const float TopY = -HalfY + float(Row) * CellH - (OverlapH - CellH) * .5f;
                for (int Column = 0; Column < Subdivisions; ++Column)
                {
                    const float NormX = -.5f + (float(Column) + .5f) / float(Subdivisions);
                    const float LeftX = -HalfX + float(Column) * CellW - (OverlapW - CellW) * .5f;
                    const auto CellColour = EvaluateVectorFreeformGradient2D(FreeformPoints, {NormX, NormY}, Sharpness, FillMode);
                    Svg += "<rect x=\"" + Number(LeftX) + "\" y=\"" + Number(TopY) + "\" width=\"" + Number(OverlapW) +
                           "\" height=\"" + Number(OverlapH) + "\" fill=\"" + ColourLiteral(CellColour) +
                           "\" fill-opacity=\"" + Number(std::clamp(CellColour[3] * Opacity, 0.f, 1.f)) + "\"/>";
                }
            }
            Svg += "</g></g>";
        }
        if (QueryVectorCoverageStrokeWidth(Source) > 0.f)
        {
            const std::string StrokeOnly = PatternInstructionStyle(Source, Opacity, {}, GradientStroke, true, false);
            Svg += EncodeLeafShapeElement(Document, Source, StrokeOnly);
        }
        return;
    }
    const std::string GradientFill = EmitPatternGradientDef(Source, DefSerial, Opacity, GradientPlace, Defs);
    const std::string Paint = PatternInstructionStyle(Source, Opacity, GradientFill, GradientStroke);
    Svg += EncodeLeafShapeElement(Document, Source, Paint);
}
} // namespace

std::string EncodePatternThumbnailSvg(const VectorAuthoringDocument &Document, uint32_t BackgroundAbgr)
{
    const float Extent = std::clamp(Document.CanvasExtent, 64.f, 4096.f);
    std::string Svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " + Number(Extent) + " " + Number(Extent) + "\">";
    {
        char Buffer[128]{};
        std::snprintf(Buffer, sizeof(Buffer), "<rect width=\"%.3f\" height=\"%.3f\" fill=\"rgb(%u,%u,%u)\"/>",
            double(Extent), double(Extent), BackgroundAbgr & 255, (BackgroundAbgr >> 8) & 255, (BackgroundAbgr >> 16) & 255);
        Svg += Buffer;
    }
    std::string Defs, Body;
    uint32_t DefSerial = 0;
    for (const auto &Layer : Document.Layers)
    {
        if (!QueryVectorAuthoringVisibility(Document, Layer)) continue;
        Body += "<g opacity=\"" + Number(Layer.ExpressionOpacity) + "\">";
        EmitPatternInstruction(Document, Layer.Root, 0, Body, Defs, DefSerial);
        Body += "</g>";
    }
    if (!Defs.empty()) Svg += "<defs>" + Defs + "</defs>";
    Svg += Body;
    Svg += "</svg>";
    return Svg;
}

std::string EncodePatternLayerThumbnailSvg(const VectorAuthoringDocument &Document, uint32_t Token, uint32_t BackgroundAbgr)
{
    const auto Position = std::find_if(Document.Layers.begin(), Document.Layers.end(),
        [Token](const auto &Layer) { return Layer.Token == Token; });
    if (Position == Document.Layers.end()) throw std::runtime_error("Pattern layer thumbnail needs an authenticated layer token.");
    // 📝 Crop to the layer's own tight extent (the same bounds the selection hitbox hugs), never the
    // full 512 tile, so a small motif reads clearly instead of shrinking to a speck in a tile-sized frame.
    const std::array<uint32_t, 1> Selection{Token};
    const auto Extent = QueryVectorSelectionExtent(Document, Selection);
    const float Span = std::max({Extent.Bounds[2], Extent.Bounds[3], 1.f}) * 1.25f;
    const float Left = Extent.Bounds[0] - Span * .5f, Top = Extent.Bounds[1] - Span * .5f;
    char ViewBox[96]{};
    std::snprintf(ViewBox, sizeof(ViewBox), "%.3f %.3f %.3f %.3f", double(Left), double(Top), double(Span), double(Span));
    std::string Svg = std::string("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"") + ViewBox + "\">";
    {
        char Buffer[192]{};
        std::snprintf(Buffer, sizeof(Buffer), "<rect x=\"%.3f\" y=\"%.3f\" width=\"%.3f\" height=\"%.3f\" fill=\"rgb(%u,%u,%u)\"/>",
            double(Left), double(Top), double(Span), double(Span),
            BackgroundAbgr & 255, (BackgroundAbgr >> 8) & 255, (BackgroundAbgr >> 16) & 255);
        Svg += Buffer;
    }
    std::string Defs, Body;
    uint32_t DefSerial = 0;
    if (QueryVectorAuthoringVisibility(Document, *Position))
    {
        Body += "<g opacity=\"" + Number(Position->ExpressionOpacity) + "\">";
        EmitPatternInstruction(Document, Position->Root, 0, Body, Defs, DefSerial);
        Body += "</g>";
    }
    if (!Defs.empty()) Svg += "<defs>" + Defs + "</defs>";
    Svg += Body;
    Svg += "</svg>";
    return Svg;
}


std::vector<uint8_t> RasterizePatternThumbnailSvg(
    const VectorAuthoringDocument &Document, uint32_t Extent, uint32_t BackgroundAbgr)
{
    if (!Extent || Extent > 1024) throw std::runtime_error("Thumbnail raster extent is unbounded.");
    const std::string Svg = EncodePatternThumbnailSvg(Document, BackgroundAbgr);
    EnsureThorVg();
    std::vector<uint8_t> Pixels(size_t(Extent) * Extent * 4, 0);
    std::unique_ptr<tvg::SwCanvas> Canvas(tvg::SwCanvas::gen());
    std::unique_ptr<tvg::Picture, ReleasePaint> Picture(tvg::Picture::gen());
    if (!Canvas || !Picture) throw std::runtime_error("ThorVG thumbnail canvas allocation failed.");
    bool Ok = Picture->load(Svg.data(), uint32_t(Svg.size()), "svg", nullptr, true) == tvg::Result::Success;
    if (Ok) Ok = Picture->size(float(Extent), float(Extent)) == tvg::Result::Success;
    if (Ok) Ok = Canvas->target(reinterpret_cast<uint32_t *>(Pixels.data()), Extent, Extent, Extent,
                     tvg::ColorSpace::ABGR8888S) == tvg::Result::Success;
    if (Ok)
    {
        Ok = Canvas->add(Picture.get()) == tvg::Result::Success;
        if (Ok) Picture.release();
    }
    if (Ok) Ok = Canvas->draw(true) == tvg::Result::Success;
    if (Ok) Ok = Canvas->sync() == tvg::Result::Success;
    if (!Ok) throw std::runtime_error("ThorVG could not rasterize the pattern thumbnail SVG.");
    return Pixels;
}

std::vector<uint8_t> RasterizePatternLayerThumbnail(
    const VectorAuthoringDocument &Document, uint32_t Token, uint32_t Extent, uint32_t BackgroundAbgr)
{
    if (!Extent || Extent > 1024) throw std::runtime_error("Layer thumbnail raster extent is unbounded.");
    const std::string Svg = EncodePatternLayerThumbnailSvg(Document, Token, BackgroundAbgr);
    EnsureThorVg();
    std::vector<uint8_t> Pixels(size_t(Extent) * Extent * 4, 0);
    std::unique_ptr<tvg::SwCanvas> Canvas(tvg::SwCanvas::gen());
    std::unique_ptr<tvg::Picture, ReleasePaint> Picture(tvg::Picture::gen());
    if (!Canvas || !Picture) throw std::runtime_error("ThorVG layer-thumbnail canvas allocation failed.");
    bool Ok = Picture->load(Svg.data(), uint32_t(Svg.size()), "svg", nullptr, true) == tvg::Result::Success;
    if (Ok) Ok = Picture->size(float(Extent), float(Extent)) == tvg::Result::Success;
    if (Ok) Ok = Canvas->target(reinterpret_cast<uint32_t *>(Pixels.data()), Extent, Extent, Extent,
                     tvg::ColorSpace::ABGR8888S) == tvg::Result::Success;
    if (Ok)
    {
        Ok = Canvas->add(Picture.get()) == tvg::Result::Success;
        if (Ok) Picture.release();
    }
    if (Ok) Ok = Canvas->draw(true) == tvg::Result::Success;
    if (Ok) Ok = Canvas->sync() == tvg::Result::Success;
    if (!Ok) throw std::runtime_error("ThorVG could not rasterize the pattern layer thumbnail SVG.");
    return Pixels;
}

std::vector<uint8_t> BlitPatternThumbnailAtlas(
    const std::vector<std::vector<uint8_t>> &Thumbs, uint32_t TileExtent, uint32_t Columns, uint32_t BackgroundAbgr)
{
    if (!TileExtent || TileExtent > 512 || !Columns || Columns > 16 || Thumbs.size() > size_t(Columns) * Columns)
        throw std::runtime_error("Atlas blit exceeds bounded grid capacity.");
    for (const auto &Thumb : Thumbs)
        if (Thumb.size() != size_t(TileExtent) * TileExtent * 4)
            throw std::runtime_error("Atlas thumb does not match the tile extent.");
    const uint32_t Stride = TileExtent + 2, AtlasExtent = Stride * Columns;
    std::vector<uint8_t> Atlas(size_t(AtlasExtent) * AtlasExtent * 4);
    for (size_t Pixel = 0; Pixel < Atlas.size(); Pixel += 4)
    {
        Atlas[Pixel] = uint8_t(BackgroundAbgr & 255);
        Atlas[Pixel + 1] = uint8_t((BackgroundAbgr >> 8) & 255);
        Atlas[Pixel + 2] = uint8_t((BackgroundAbgr >> 16) & 255);
        Atlas[Pixel + 3] = 255;
    }
    for (size_t Slot = 0; Slot < Thumbs.size(); ++Slot)
    {
        const auto &Thumb = Thumbs[Slot];
        const uint32_t Left = uint32_t(Slot % Columns) * Stride + 1, Top = uint32_t(Slot / Columns) * Stride + 1;
        for (uint32_t Row = 0; Row < TileExtent; ++Row)
        {
            const uint8_t *Source = &Thumb[size_t(Row) * TileExtent * 4];
            uint8_t *Target = &Atlas[(size_t(Top + Row) * AtlasExtent + Left) * 4];
            std::copy_n(Source, size_t(TileExtent) * 4, Target);
            // 📝 Replicated gutters keep bilinear sampling inside the tile, mirroring the GPU atlas copies.
            std::copy_n(Source, 4, Target - 4);
            std::copy_n(Source + (size_t(TileExtent) - 1) * 4, 4, Target + size_t(TileExtent) * 4);
        }
        const uint8_t *FirstRow = &Atlas[(size_t(Top) * AtlasExtent + Left - 1) * 4];
        uint8_t *TopGutter = &Atlas[(size_t(Top - 1) * AtlasExtent + Left - 1) * 4];
        std::copy_n(FirstRow, size_t(TileExtent + 2) * 4, TopGutter);
        const uint8_t *LastRow = &Atlas[(size_t(Top + TileExtent - 1) * AtlasExtent + Left - 1) * 4];
        uint8_t *BottomGutter = &Atlas[(size_t(Top + TileExtent) * AtlasExtent + Left - 1) * 4];
        std::copy_n(LastRow, size_t(TileExtent + 2) * 4, BottomGutter);
    }
    return Atlas;
}

std::string EncodeStampDocumentSvg(const VectorAuthoringDocument &Document)
{
    // 🖨️ EncodePatternThumbnailSvg minus the opaque background rect: stamp artwork must keep a
    // transparent surround so Texture Paint can composite it as a decal.
    const auto Evaluated = ConstructEvaluatedVectorAuthoringDocument(Document);
    const float Extent = std::clamp(Evaluated.CanvasExtent, 64.f, 4096.f);
    std::string Svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " + Number(Extent) + " " + Number(Extent) + "\">";
    std::string Defs, Body;
    uint32_t DefSerial = 0;
    for (const auto &Layer : Evaluated.Layers)
    {
        if (!QueryVectorAuthoringVisibility(Evaluated, Layer)) continue;
        Body += "<g opacity=\"" + Number(Layer.ExpressionOpacity) + "\">";
        EmitPatternInstruction(Evaluated, Layer.Root, 0, Body, Defs, DefSerial);
        Body += "</g>";
    }
    if (!Defs.empty()) Svg += "<defs>" + Defs + "</defs>";
    Svg += Body;
    Svg += "</svg>";
    return Svg;
}

std::vector<uint8_t> RasterizeStampDocument(const VectorAuthoringDocument &Document, uint32_t Extent)
{
    if (!Extent || Extent > 2048) throw std::runtime_error("Stamp raster extent is unbounded.");
    const std::string Svg = EncodeStampDocumentSvg(Document);
    EnsureThorVg();
    std::vector<uint8_t> Pixels(size_t(Extent) * Extent * 4, 0);
    std::unique_ptr<tvg::SwCanvas> Canvas(tvg::SwCanvas::gen());
    std::unique_ptr<tvg::Picture, ReleasePaint> Picture(tvg::Picture::gen());
    if (!Canvas || !Picture) throw std::runtime_error("ThorVG stamp canvas allocation failed.");
    bool Ok = Picture->load(Svg.data(), uint32_t(Svg.size()), "svg", nullptr, true) == tvg::Result::Success;
    if (Ok) Ok = Picture->size(float(Extent), float(Extent)) == tvg::Result::Success;
    if (Ok) Ok = Canvas->target(reinterpret_cast<uint32_t *>(Pixels.data()), Extent, Extent, Extent,
                     tvg::ColorSpace::ABGR8888S) == tvg::Result::Success;
    if (Ok)
    {
        Ok = Canvas->add(Picture.get()) == tvg::Result::Success;
        if (Ok) Picture.release();
    }
    if (Ok) Ok = Canvas->draw(true) == tvg::Result::Success;
    if (Ok) Ok = Canvas->sync() == tvg::Result::Success;
    if (!Ok) throw std::runtime_error("ThorVG could not rasterize the stamp document SVG.");
    return Pixels;
}

namespace
{
struct CachedStampLayerRaster
{
    uint32_t Extent = 0;
    std::string Svg;
    std::vector<uint8_t> Pixels;
    uint32_t FirstRow = 0;
    uint32_t EndRow = 0;
};

std::shared_ptr<const CachedStampLayerRaster> RasterizeCachedStampSvg(const std::string &Svg, uint32_t Extent)
{
    static std::mutex CacheMutex;
    static std::list<std::shared_ptr<CachedStampLayerRaster>> CacheEntries;
    {
        std::scoped_lock Lock(CacheMutex);
        for (auto Iterator = CacheEntries.begin(); Iterator != CacheEntries.end(); ++Iterator)
        {
            if ((*Iterator)->Extent == Extent && (*Iterator)->Svg == Svg)
            {
                CacheEntries.splice(CacheEntries.begin(), CacheEntries, Iterator);
                return *Iterator;
            }
        }
    }
    EnsureThorVg();
    auto Entry = std::make_shared<CachedStampLayerRaster>();
    Entry->Extent = Extent;
    Entry->Svg = Svg;
    Entry->Pixels.assign(size_t(Extent) * size_t(Extent) * 4u, 0);
    std::unique_ptr<tvg::SwCanvas> Canvas(tvg::SwCanvas::gen());
    std::unique_ptr<tvg::Picture, ReleasePaint> Picture(tvg::Picture::gen());
    if (!Canvas || !Picture) throw std::runtime_error("ThorVG stamp canvas allocation failed.");
    bool Ok = Picture->load(Svg.data(), uint32_t(Svg.size()), "svg", nullptr, true) == tvg::Result::Success;
    if (Ok) Ok = Picture->size(float(Extent), float(Extent)) == tvg::Result::Success;
    if (Ok) Ok = Canvas->target(reinterpret_cast<uint32_t *>(Entry->Pixels.data()), Extent, Extent, Extent,
                     tvg::ColorSpace::ABGR8888S) == tvg::Result::Success;
    if (Ok)
    {
        Ok = Canvas->add(Picture.get()) == tvg::Result::Success;
        if (Ok) Picture.release();
    }
    if (Ok) Ok = Canvas->draw(true) == tvg::Result::Success;
    if (Ok) Ok = Canvas->sync() == tvg::Result::Success;
    if (!Ok) throw std::runtime_error("ThorVG could not rasterize the stamp document SVG.");

    uint32_t FirstRow = Extent, EndRow = 0;
    for (uint32_t Row = 0; Row < Extent; ++Row)
    {
        const uint8_t *RowPtr = Entry->Pixels.data() + size_t(Row) * size_t(Extent) * 4u;
        bool NonEmpty = false;
        for (uint32_t Column = 0; Column < Extent; ++Column)
            if (RowPtr[size_t(Column) * 4u + 3u] != 0) { NonEmpty = true; break; }
        if (NonEmpty)
        {
            FirstRow = std::min(FirstRow, Row);
            EndRow = Row + 1u;
        }
    }
    Entry->FirstRow = FirstRow < EndRow ? FirstRow : 0u;
    Entry->EndRow = FirstRow < EndRow ? EndRow : 0u;

    {
        std::scoped_lock Lock(CacheMutex);
        CacheEntries.push_front(Entry);
        while (CacheEntries.size() > 64u) CacheEntries.pop_back();
    }
    return Entry;
}
} // namespace

StampSurfaceMaps EvaluateStampSurfaceMaps(const VectorAuthoringDocument &Document, uint32_t Extent)
{
    if (!Extent || Extent > 2048) throw std::runtime_error("Stamp surface map extent is unbounded.");
    const auto Evaluated = ConstructEvaluatedVectorAuthoringDocument(Document);
    const size_t Count = size_t(Extent) * size_t(Extent);

    StampSurfaceMaps Maps;
    Maps.Extent = Extent;
    Maps.ColourRgba.assign(Count * 4, 0);
    Maps.Coverage.assign(Count, 0.f);
    Maps.Height.assign(Count, 0.f);
    Maps.NormalScale.assign(Count, 1.f);
    Maps.NormalX.assign(Count, 0.f);
    Maps.NormalY.assign(Count, 0.f);
    Maps.NormalZ.assign(Count, 1.f);
    Maps.Roughness.assign(Count, .7f);
    Maps.Metalness.assign(Count, 0.f);
    Maps.Opacity.assign(Count, 0.f);
    Maps.Occlusion.assign(Count, 1.f);
    Maps.Transmission.assign(Count, 0.f);
    Maps.Clearcoat.assign(Count, 0.f);
    Maps.RoughnessMask.assign(Count, 0.f);
    Maps.MetalnessMask.assign(Count, 0.f);
    Maps.HeightMask.assign(Count, 0.f);
    Maps.NormalMask.assign(Count, 0.f);
    Maps.OcclusionMask.assign(Count, 0.f);
    Maps.TransmissionMask.assign(Count, 0.f);
    Maps.ClearcoatMask.assign(Count, 0.f);

    std::vector<std::array<float, 4>> ColourAccum(Count, {0.f, 0.f, 0.f, 0.f});
    for (size_t Index = 0; Index < Evaluated.Layers.size(); ++Index)
    {
        const auto &Layer = Evaluated.Layers[Index];
        if (!QueryVectorAuthoringVisibility(Evaluated, Layer)) continue;
        const auto &Channels = Layer.SurfaceChannels;
        bool AnyChannel = false;
        for (const auto &Entry : Channels)
            if (Entry.Enabled) AnyChannel = true;
        if (!AnyChannel) continue;

        VectorAuthoringDocument Solo;
        Solo.Stamp = Evaluated.Stamp;
        Solo.CanvasExtent = Evaluated.CanvasExtent;
        Solo.Instructions = Evaluated.Instructions;
        Solo.Segments = Evaluated.Segments;
        Solo.TokenClock = Evaluated.TokenClock;
        auto SoloLayer = Layer;
        SoloLayer.Visible = true;
        SoloLayer.EnclosureToken = 0;
        Solo.Layers.push_back(std::move(SoloLayer));

        const auto CachedSolo = RasterizeCachedStampSvg(EncodeStampDocumentSvg(Solo), Extent);
        const auto &Raster = CachedSolo->Pixels;
        if (Raster.size() != Count * 4 || CachedSolo->FirstRow >= CachedSolo->EndRow) continue;

        std::shared_ptr<const CachedStampLayerRaster> CachedMask;
        if (Layer.Mask && Layer.MaskEnabled)
        {
            VectorAuthoringDocument MaskDoc;
            MaskDoc.Stamp = Evaluated.Stamp;
            MaskDoc.CanvasExtent = Evaluated.CanvasExtent;
            MaskDoc.Instructions = Evaluated.Instructions;
            MaskDoc.Segments = Evaluated.Segments;
            const uint32_t MaskRoot = uint32_t(MaskDoc.Instructions.size());
            MaskDoc.Instructions.push_back(*Layer.Mask);
            VectorAuthoringLayer MaskLayer;
            MaskLayer.Token = 1;
            MaskLayer.Root = MaskRoot;
            MaskLayer.Visible = true;
            MaskDoc.TokenClock = 1;
            MaskDoc.Layers.push_back(std::move(MaskLayer));
            CachedMask = RasterizeCachedStampSvg(EncodeStampDocumentSvg(MaskDoc), Extent);
        }
        const uint8_t *MaskData = (CachedMask && CachedMask->Pixels.size() == Count * 4) ? CachedMask->Pixels.data() : nullptr;

        const auto &ColourCh = Channels[size_t(VectorAuthoringSurfaceChannel::Colour)];
        const auto &HeightCh = Channels[size_t(VectorAuthoringSurfaceChannel::Height)];
        const auto &NormalCh = Channels[size_t(VectorAuthoringSurfaceChannel::Normal)];
        const auto &RoughCh = Channels[size_t(VectorAuthoringSurfaceChannel::Roughness)];
        const auto &MetalCh = Channels[size_t(VectorAuthoringSurfaceChannel::Metalness)];
        const auto &OpacityCh = Channels[size_t(VectorAuthoringSurfaceChannel::Opacity)];
        const auto &OcclusionCh = Channels[size_t(VectorAuthoringSurfaceChannel::Occlusion)];
        const auto &TransmissionCh = Channels[size_t(VectorAuthoringSurfaceChannel::Transmission)];
        const auto &ClearcoatCh = Channels[size_t(VectorAuthoringSurfaceChannel::Clearcoat)];

        const float ColourStrength = std::clamp(ColourCh.Strength, 0.f, 1.f);
        const float SignedHeight = std::clamp(Layer.SurfaceRelief * std::clamp(HeightCh.Strength, 0.f, 1.f), -1.f, 1.f);
        const float NormalStrength = std::clamp(NormalCh.Strength, 0.f, 4.f);
        const float RoughStrength = std::clamp(RoughCh.Strength, 0.f, 1.f);
        const float MetalStrength = std::clamp(MetalCh.Strength, 0.f, 1.f);
        const float OpacityScale = OpacityCh.Enabled ? std::clamp(OpacityCh.Strength, 0.f, 1.f) : 1.f;
        const float AoTarget = 1.f - std::clamp(OcclusionCh.Strength, 0.f, 1.f);
        const float TransmissionStrength = std::clamp(TransmissionCh.Strength, 0.f, 1.f);
        const float ClearcoatStrength = std::clamp(ClearcoatCh.Strength, 0.f, 1.f);

        const size_t StartTexel = size_t(CachedSolo->FirstRow) * size_t(Extent);
        const size_t EndTexel = size_t(CachedSolo->EndRow) * size_t(Extent);
        for (size_t Texel = StartTexel; Texel < EndTexel; ++Texel)
        {
            float RawAlpha = Raster[Texel * 4 + 3] / 255.f;
            if (RawAlpha <= 0.f) continue;
            if (MaskData)
            {
                float MaskFactor = MaskData[Texel * 4 + 3] / 255.f;
                if (Layer.MaskLuminance)
                {
                    const float Lum = (.2126f * MaskData[Texel * 4 + 0] + .7152f * MaskData[Texel * 4 + 1] + .0722f * MaskData[Texel * 4 + 2]) / 255.f;
                    MaskFactor *= Lum;
                }
                if (Layer.MaskInverted) MaskFactor = 1.f - MaskFactor;
                RawAlpha *= std::clamp(MaskFactor, 0.f, 1.f);
            }
            const float Alpha = RawAlpha * OpacityScale;
            if (Alpha <= .002f) continue;

            Maps.Coverage[Texel] = Alpha + Maps.Coverage[Texel] * (1.f - Alpha);
            if (OpacityCh.Enabled)
                Maps.Opacity[Texel] = Alpha + Maps.Opacity[Texel] * (1.f - Alpha);

            if (ColourCh.Enabled)
            {
                const float ColourAlpha = Alpha * ColourStrength;
                if (ColourAlpha > .001f)
                {
                    auto &Dst = ColourAccum[Texel];
                    const float SrcR = Raster[Texel * 4 + 0] / 255.f;
                    const float SrcG = Raster[Texel * 4 + 1] / 255.f;
                    const float SrcB = Raster[Texel * 4 + 2] / 255.f;
                    const float OutA = ColourAlpha + Dst[3] * (1.f - ColourAlpha);
                    if (OutA > 1e-5f)
                    {
                        Dst[0] = (SrcR * ColourAlpha + Dst[0] * Dst[3] * (1.f - ColourAlpha)) / OutA;
                        Dst[1] = (SrcG * ColourAlpha + Dst[1] * Dst[3] * (1.f - ColourAlpha)) / OutA;
                        Dst[2] = (SrcB * ColourAlpha + Dst[2] * Dst[3] * (1.f - ColourAlpha)) / OutA;
                        Dst[3] = OutA;
                    }
                }
            }
            if (HeightCh.Enabled)
            {
                Maps.Height[Texel] = std::clamp(Maps.Height[Texel] + SignedHeight * Alpha, -1.f, 1.f);
                Maps.HeightMask[Texel] = Alpha + Maps.HeightMask[Texel] * (1.f - Alpha);
            }
            if (NormalCh.Enabled)
            {
                Maps.NormalScale[Texel] += (NormalStrength - Maps.NormalScale[Texel]) * Alpha;
                Maps.NormalMask[Texel] = Alpha + Maps.NormalMask[Texel] * (1.f - Alpha);
            }
            else if (HeightCh.Enabled && Maps.NormalMask[Texel] <= 1e-4f)
            {
                Maps.NormalScale[Texel] = 0.f;
            }
            if (RoughCh.Enabled)
            {
                const float OutMask = Alpha + Maps.RoughnessMask[Texel] * (1.f - Alpha);
                Maps.Roughness[Texel] = Maps.RoughnessMask[Texel] > 1e-5f
                    ? (RoughStrength * Alpha + Maps.Roughness[Texel] * Maps.RoughnessMask[Texel] * (1.f - Alpha)) / OutMask
                    : RoughStrength;
                Maps.RoughnessMask[Texel] = OutMask;
            }
            if (MetalCh.Enabled)
            {
                const float OutMask = Alpha + Maps.MetalnessMask[Texel] * (1.f - Alpha);
                Maps.Metalness[Texel] = Maps.MetalnessMask[Texel] > 1e-5f
                    ? (MetalStrength * Alpha + Maps.Metalness[Texel] * Maps.MetalnessMask[Texel] * (1.f - Alpha)) / OutMask
                    : MetalStrength;
                Maps.MetalnessMask[Texel] = OutMask;
            }
            if (OcclusionCh.Enabled)
            {
                const float OutMask = Alpha + Maps.OcclusionMask[Texel] * (1.f - Alpha);
                Maps.Occlusion[Texel] = Maps.OcclusionMask[Texel] > 1e-5f
                    ? (AoTarget * Alpha + Maps.Occlusion[Texel] * Maps.OcclusionMask[Texel] * (1.f - Alpha)) / OutMask
                    : AoTarget;
                Maps.OcclusionMask[Texel] = OutMask;
            }
            if (TransmissionCh.Enabled)
            {
                const float OutMask = Alpha + Maps.TransmissionMask[Texel] * (1.f - Alpha);
                Maps.Transmission[Texel] = Maps.TransmissionMask[Texel] > 1e-5f
                    ? (TransmissionStrength * Alpha + Maps.Transmission[Texel] * Maps.TransmissionMask[Texel] * (1.f - Alpha)) / OutMask
                    : TransmissionStrength;
                Maps.TransmissionMask[Texel] = OutMask;
            }
            if (ClearcoatCh.Enabled)
            {
                const float OutMask = Alpha + Maps.ClearcoatMask[Texel] * (1.f - Alpha);
                Maps.Clearcoat[Texel] = Maps.ClearcoatMask[Texel] > 1e-5f
                    ? (ClearcoatStrength * Alpha + Maps.Clearcoat[Texel] * Maps.ClearcoatMask[Texel] * (1.f - Alpha)) / OutMask
                    : ClearcoatStrength;
                Maps.ClearcoatMask[Texel] = OutMask;
            }
        }
    }

    const int Span = int(Extent);
    const auto HeightAt = [&](int X, int Y) {
        return Maps.Height[size_t(std::clamp(Y, 0, Span - 1)) * size_t(Extent) + size_t(std::clamp(X, 0, Span - 1))];
    };
    const float ReliefResolution = float(Extent) / 128.f;
    const auto ProcessRowRange = [&](int RowBegin, int RowEnd)
    {
        for (int Y = RowBegin; Y < RowEnd; ++Y)
        {
            for (int X = 0; X < Span; ++X)
            {
                const size_t Texel = size_t(Y) * size_t(Extent) + size_t(X);
                const auto &Col = ColourAccum[Texel];
                Maps.ColourRgba[Texel * 4 + 0] = uint8_t(std::round(std::clamp(Col[0], 0.f, 1.f) * 255.f));
                Maps.ColourRgba[Texel * 4 + 1] = uint8_t(std::round(std::clamp(Col[1], 0.f, 1.f) * 255.f));
                Maps.ColourRgba[Texel * 4 + 2] = uint8_t(std::round(std::clamp(Col[2], 0.f, 1.f) * 255.f));
                Maps.ColourRgba[Texel * 4 + 3] = uint8_t(std::round(std::clamp(Col[3], 0.f, 1.f) * 255.f));
                const float SlopeScale = 2.f * std::clamp(Maps.NormalScale[Texel], 0.f, 4.f) * ReliefResolution;
                float Nx = -(HeightAt(X + 1, Y) - HeightAt(X - 1, Y)) * SlopeScale;
                float Ny = -(HeightAt(X, Y + 1) - HeightAt(X, Y - 1)) * SlopeScale;
                float Nz = 1.f;
                const float InvLen = 1.f / std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz);
                Maps.NormalX[Texel] = Nx * InvLen;
                Maps.NormalY[Texel] = Ny * InvLen;
                Maps.NormalZ[Texel] = Nz * InvLen;
            }
        }
    };
    const unsigned WorkerCount = Extent >= 256u ? std::clamp(std::thread::hardware_concurrency(), 1u, 8u) : 1u;
    if (WorkerCount <= 1u)
        ProcessRowRange(0, Span);
    else
    {
        std::vector<std::thread> Workers;
        Workers.reserve(WorkerCount - 1u);
        for (unsigned Worker = 0; Worker < WorkerCount; ++Worker)
        {
            const int RowBegin = int((uint64_t(Span) * Worker) / WorkerCount);
            const int RowEnd = int((uint64_t(Span) * (Worker + 1u)) / WorkerCount);
            if (Worker + 1u == WorkerCount) ProcessRowRange(RowBegin, RowEnd);
            else Workers.emplace_back(ProcessRowRange, RowBegin, RowEnd);
        }
        for (auto &Worker : Workers) Worker.join();
    }
    return Maps;
}
} // namespace Frontier
