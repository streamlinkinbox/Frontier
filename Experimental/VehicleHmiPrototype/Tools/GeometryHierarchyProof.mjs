//------------------------------------------------------------------------------------------------------------------------
// GEOMETRY HIERARCHY — VISUAL PROOF RENDERER
//------------------------------------------------------------------------------------------------------------------------
//
// Renders the hierarchy with a tiny deterministic software rasterizer (z-buffered, flat-shaded) so the evidence
// exists independent of any GPU: source soup, per-level meshlet-colour views, an error heatmap, and the
// opaque/transmissive class view. Output: Evidence/GeometryHierarchy/*.png + stats.json.
//
// Run: node Tools/GeometryHierarchyProof.mjs

import { existsSync, mkdirSync, readdirSync, unlinkSync, writeFileSync } from "node:fs";
import { deflateSync } from "node:zlib";
import * as HierarchyModule from "../Source/GeometryHierarchy.js";
import { ConstructSphereFacets, ConstructHeightfieldFacets, ConstructUvSeamCreasedMeshFacets } from "./GeometryHierarchyCorpus.mjs";
import { SelectHierarchyCut } from "../Source/GeometryHierarchyRuntime.js";

const { BuildGeometryHierarchy, ValidateGeometryHierarchy, CollectHierarchyLevel } = HierarchyModule;

//------------------------------------------------------------------------------------------------------------------------
// PNG ENCODER (no dependencies)
//------------------------------------------------------------------------------------------------------------------------

const CrcTable = Array.from({ length: 256 }, (_, N) =>
{
    let C = N;
    for (let K = 0; K < 8; ++K) C = C & 1 ? 0xEDB88320 ^ (C >>> 1) : C >>> 1;
    return C >>> 0;
});
function Crc32(Buffer)
{
    let Crc = 0xFFFFFFFF;
    for (const Byte of Buffer) Crc = CrcTable[(Crc ^ Byte) & 0xFF] ^ (Crc >>> 8);
    return (Crc ^ 0xFFFFFFFF) >>> 0;
}
function Chunk(Type, Data)
{
    const Length = Buffer.alloc(4); Length.writeUInt32BE(Data.length);
    const Body = Buffer.concat([Buffer.from(Type, "ascii"), Data]);
    const Crc = Buffer.alloc(4); Crc.writeUInt32BE(Crc32(Body));
    return Buffer.concat([Length, Body, Crc]);
}
function EncodePng({ Width, Height, Pixels: Rgb })
{
    const Header = Buffer.alloc(13);
    Header.writeUInt32BE(Width, 0); Header.writeUInt32BE(Height, 4);
    Header[8] = 8; Header[9] = 2;   // 8-bit RGB
    const Rows = Buffer.alloc(Height * (Width * 3 + 1));
    for (let Y = 0; Y < Height; ++Y)
        Rgb.copy(Rows, Y * (Width * 3 + 1) + 1, Y * Width * 3, (Y + 1) * Width * 3);
    return Buffer.concat([
        Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
        Chunk("IHDR", Header), Chunk("IDAT", deflateSync(Rows, { level: 6 })), Chunk("IEND", Buffer.alloc(0))]);
}

//------------------------------------------------------------------------------------------------------------------------
// SOFTWARE RASTERIZER
//------------------------------------------------------------------------------------------------------------------------

function Sub(A, B) { return [A[0] - B[0], A[1] - B[1], A[2] - B[2]]; }
function Cross(A, B) { return [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]]; }
function Dot(A, B) { return A[0] * B[0] + A[1] * B[1] + A[2] * B[2]; }
function Norm(A) { const L = Math.hypot(A[0], A[1], A[2]) || 1; return [A[0] / L, A[1] / L, A[2] / L]; }

// Renders a flat position soup (3 vertices per triangle). ColourFor(Triangle, Normal, Shade) returns [r,g,b] 0..255.
function RenderSoup(Positions, ColourFor, View, CornerUVs = null)
{
    const { Width, Height, Eye, Target, Up, FieldOfView } = View;
    const Forward = Norm(Sub(Target, Eye));
    const Right = Norm(Cross(Forward, Up));
    const TrueUp = Cross(Right, Forward);
    const Focal = 1 / Math.tan(FieldOfView / 2);
    const Aspect = Width / Height;
    const Light = Norm([0.45, 0.75, 0.6]);
    const Image = Buffer.alloc(Width * Height * 3);
    const Depth = new Float64Array(Width * Height).fill(Infinity);
    for (let Pixel = 0; Pixel < Width * Height; ++Pixel)
    {
        // Vertical gradient backdrop so silhouettes read against something.
        const T = Math.floor(Pixel / Width) / Height;
        Image[Pixel * 3] = 18 + 22 * T; Image[Pixel * 3 + 1] = 22 + 26 * T; Image[Pixel * 3 + 2] = 34 + 30 * T;
    }
    const Project = (P) =>
    {
        const D = Sub(P, Eye);
        const Z = Dot(D, Forward);
        if (Z <= 1e-6) return null;
        return [(Dot(D, Right) * Focal / Aspect / Z * 0.5 + 0.5) * Width,
                (0.5 - Dot(D, TrueUp) * Focal / Z * 0.5) * Height, Z];
    };
    const Triangles = Positions.length / 9;
    for (let Triangle = 0; Triangle < Triangles; ++Triangle)
    {
        const Corners = [0, 1, 2].map((C) => [Positions[Triangle * 9 + C * 3], Positions[Triangle * 9 + C * 3 + 1],
            Positions[Triangle * 9 + C * 3 + 2]]);
        const Normal = Norm(Cross(Sub(Corners[1], Corners[0]), Sub(Corners[2], Corners[0])));
        const Screen = Corners.map(Project);
        if (Screen.some((P) => P === null)) continue;
        const Shade = 0.35 + 0.65 * Math.abs(Dot(Normal, Light));
        const Colour = ColourFor(Triangle, Normal, Shade, null);
        const [A, B, C] = Screen;
        const Area = (B[0] - A[0]) * (C[1] - A[1]) - (B[1] - A[1]) * (C[0] - A[0]);
        if (Area === 0) continue;
        const MinX = Math.max(0, Math.floor(Math.min(A[0], B[0], C[0])));
        const MaxX = Math.min(Width - 1, Math.ceil(Math.max(A[0], B[0], C[0])));
        const MinY = Math.max(0, Math.floor(Math.min(A[1], B[1], C[1])));
        const MaxY = Math.min(Height - 1, Math.ceil(Math.max(A[1], B[1], C[1])));
        for (let Y = MinY; Y <= MaxY; ++Y)
            for (let X = MinX; X <= MaxX; ++X)
            {
                const W0 = ((B[0] - X) * (C[1] - Y) - (B[1] - Y) * (C[0] - X)) / Area;
                const W1 = ((C[0] - X) * (A[1] - Y) - (C[1] - Y) * (A[0] - X)) / Area;
                const W2 = 1 - W0 - W1;
                if (W0 < 0 || W1 < 0 || W2 < 0) continue;
                const Z = 1 / (W0 / A[2] + W1 / B[2] + W2 / C[2]);
                const Pixel = Y * Width + X;
                if (Z >= Depth[Pixel]) continue;
                Depth[Pixel] = Z;
                let PixelColour = Colour;
                if (CornerUVs)
                {
                    const Uv = [0, 1].map((Axis) =>
                        W0 * CornerUVs[Triangle * 6 + Axis] + W1 * CornerUVs[Triangle * 6 + 2 + Axis] +
                        W2 * CornerUVs[Triangle * 6 + 4 + Axis]);
                    PixelColour = ColourFor(Triangle, Normal, Shade, Uv);
                }
                Image[Pixel * 3] = PixelColour[0]; Image[Pixel * 3 + 1] = PixelColour[1]; Image[Pixel * 3 + 2] = PixelColour[2];
            }
    }
    return { Width, Height, Pixels: Image };
}


//------------------------------------------------------------------------------------------------------------------------
// GIF89a ENCODER (no dependencies): fixed 216-colour cube palette, LZW per frame, NETSCAPE loop.
//------------------------------------------------------------------------------------------------------------------------

function EncodeGif(Frames, DelayCentiseconds)
{
    const { Width, Height } = Frames[0];
    const Palette = [];
    for (let R = 0; R < 6; ++R) for (let G = 0; G < 6; ++G) for (let B = 0; B < 6; ++B)
        Palette.push(R * 51, G * 51, B * 51);
    while (Palette.length < 256 * 3) Palette.push(0, 0, 0);
    const Quantize = (Pixels) =>
    {
        const Indices = new Uint8Array(Pixels.length / 3);
        for (let I = 0; I < Indices.length; ++I)
            Indices[I] = Math.round(Pixels[I * 3] / 51) * 36 + Math.round(Pixels[I * 3 + 1] / 51) * 6 +
                Math.round(Pixels[I * 3 + 2] / 51);
        return Indices;
    };
    const CompressLzw = (Indices) =>
    {
        const MinimumCodeSize = 8;
        const Clear = 1 << MinimumCodeSize, EndOfInformation = Clear + 1;
        let Dictionary = new Map(), NextCode = EndOfInformation + 1, CodeSize = MinimumCodeSize + 1;
        const Bytes = [];
        let BitBuffer = 0, BitCount = 0;
        const Emit = (Code) =>
        {
            BitBuffer |= Code << BitCount;
            BitCount += CodeSize;
            while (BitCount >= 8) { Bytes.push(BitBuffer & 0xFF); BitBuffer >>>= 8; BitCount -= 8; }
        };
        const Reset = () => { Dictionary = new Map(); NextCode = EndOfInformation + 1; CodeSize = MinimumCodeSize + 1; };
        Emit(Clear);
        let Prefix = Indices[0];
        for (let I = 1; I < Indices.length; ++I)
        {
            const Key = Prefix * 256 + Indices[I];
            const Existing = Dictionary.get(Key);
            if (Existing !== undefined) { Prefix = Existing; continue; }
            Emit(Prefix);
            if (NextCode < 4096)
            {
                Dictionary.set(Key, NextCode++);
                if (NextCode - 1 === (1 << CodeSize) && CodeSize < 12) ++CodeSize;
            }
            else { Emit(Clear); Reset(); }
            Prefix = Indices[I];
        }
        Emit(Prefix);
        Emit(EndOfInformation);
        if (BitCount > 0) Bytes.push(BitBuffer & 0xFF);
        const Blocked = [MinimumCodeSize];
        for (let Offset = 0; Offset < Bytes.length; Offset += 255)
        {
            const Slice = Bytes.slice(Offset, Offset + 255);
            Blocked.push(Slice.length, ...Slice);
        }
        Blocked.push(0);
        return Buffer.from(Blocked);
    };
    const Parts = [Buffer.from("GIF89a", "ascii")];
    const Screen = Buffer.alloc(7);
    Screen.writeUInt16LE(Width, 0); Screen.writeUInt16LE(Height, 2);
    Screen[4] = 0xF7;   // global palette, 8-bit colour resolution, 256 entries
    Parts.push(Screen, Buffer.from(Palette));
    Parts.push(Buffer.from([0x21, 0xFF, 11, ...Buffer.from("NETSCAPE2.0", "ascii"), 3, 1, 0, 0, 0]));
    for (const Frame of Frames)
    {
        const Control = Buffer.alloc(8);
        Control[0] = 0x21; Control[1] = 0xF9; Control[2] = 4; Control[3] = 4;   // disposal: restore to background
        Control.writeUInt16LE(DelayCentiseconds, 4);
        Parts.push(Control);
        const Descriptor = Buffer.alloc(10);
        Descriptor[0] = 0x2C;
        Descriptor.writeUInt16LE(Width, 5); Descriptor.writeUInt16LE(Height, 7);
        Parts.push(Descriptor, CompressLzw(Quantize(Frame.Pixels)));
    }
    Parts.push(Buffer.from([0x3B]));
    return Buffer.concat(Parts);
}

//------------------------------------------------------------------------------------------------------------------------
// COLOUR SCHEMES
//------------------------------------------------------------------------------------------------------------------------

function HslToRgb(Hue, Saturation, Lightness)
{
    const C = (1 - Math.abs(2 * Lightness - 1)) * Saturation;
    const X = C * (1 - Math.abs((Hue / 60) % 2 - 1));
    const M = Lightness - C / 2;
    const Sector = Math.floor(Hue / 60) % 6;
    const Rgb = [[C, X, 0], [X, C, 0], [0, C, X], [0, X, C], [X, 0, C], [C, 0, X]][Sector];
    return Rgb.map((V) => Math.round((V + M) * 255));
}
const OwnerColour = (Owner, Shade) =>
{
    const [R, G, B] = HslToRgb((Owner * 137.508) % 360, 0.62, 0.55);
    return [R * Shade, G * Shade, B * Shade].map((V) => Math.min(255, Math.round(V)));
};
const ErrorColour = (Error, Maximum, Shade) =>
{
    const T = Maximum > 0 ? Math.min(1, Error / Maximum) : 0;
    const [R, G, B] = HslToRgb(240 - 240 * T, 0.85, 0.55);   // blue (exact) -> red (coarsest)
    return [R * Shade, G * Shade, B * Shade].map((V) => Math.min(255, Math.round(V)));
};
const ClassColour = (Class, Shade) => Class === 1
    ? [Math.round(70 * Shade + 40), Math.round(220 * Shade), Math.round(240 * Shade)]        // transmissive cyan
    : [Math.round(150 * Shade), Math.round(155 * Shade), Math.round(170 * Shade)];          // opaque slate

// 📝 24x24 UV checkerboard shaded by the raster light: stretching shows as elongated checks, the meridian seam
//    and cap islands show as checker discontinuities that must survive every LOD level without smearing.
const UvCheckerColour = (Uv, Shade) =>
{
    if (!Uv) return [120, 120, 120];
    const Parity = (Math.floor(Uv[0] * 24) + Math.floor(Uv[1] * 24)) % 2 === 0;
    const Base = Parity ? [226, 232, 240] : [52, 96, 168];
    return Base.map((V) => Math.min(255, Math.round(V * (0.35 + 0.65 * Shade))));
};
// 📝 Triangles touching a marked sharp edge glow red, triangles touching only a UV seam glow cyan, else slate.
const FeatureColour = (SharpBits, SeamBits, Shade) =>
{
    if (SharpBits) return [Math.round(255 * Shade), Math.round(70 * Shade), Math.round(50 * Shade)];
    if (SeamBits) return [Math.round(60 * Shade), Math.round(215 * Shade), Math.round(235 * Shade)];
    return [Math.round(140 * Shade), Math.round(148 * Shade), Math.round(162 * Shade)];
};

//------------------------------------------------------------------------------------------------------------------------
// SCENES
//------------------------------------------------------------------------------------------------------------------------

function FrameView(Positions, Azimuth, Elevation)
{
    const Minimum = [Infinity, Infinity, Infinity], Maximum = [-Infinity, -Infinity, -Infinity];
    for (let V = 0; V < Positions.length / 3; ++V)
        for (let A = 0; A < 3; ++A)
        {
            Minimum[A] = Math.min(Minimum[A], Positions[V * 3 + A]);
            Maximum[A] = Math.max(Maximum[A], Positions[V * 3 + A]);
        }
    const Center = Minimum.map((V, A) => (V + Maximum[A]) / 2);
    const Radius = Math.hypot(...Maximum.map((V, A) => V - Minimum[A])) / 2 || 1;
    const Distance = Radius * 3.1;
    const Eye = [Center[0] + Distance * Math.cos(Elevation) * Math.cos(Azimuth),
                 Center[1] + Distance * Math.sin(Elevation),
                 Center[2] + Distance * Math.cos(Elevation) * Math.sin(Azimuth)];
    return { Width: 720, Height: 540, Eye, Target: Center, Up: [0, 1, 0], FieldOfView: 0.72 };
}

const OutDirectory = new URL("../Evidence/GeometryHierarchy/", import.meta.url).pathname;
if (!existsSync(OutDirectory)) mkdirSync(OutDirectory, { recursive: true });
// 📝 Evidence must never mix runs: renders from an older builder (different level counts) look identical in a
//    listing to current ones, so stale images are deleted before every regeneration. README.md is kept.
for (const Entry of readdirSync(OutDirectory))
    if (/\.(png|gif)$/.test(Entry) || Entry === "stats.json") unlinkSync(OutDirectory + Entry);
const Stats = {};

function ProveScene(Name, Source, Options, Azimuth, Elevation, ClassOf, UvAware = false)
{
    const Hierarchy = BuildGeometryHierarchy(Source, Options);
    const Validation = ValidateGeometryHierarchy(Source, Hierarchy, Options);
    if (!Validation.Passed) throw new Error(Name + " failed validation: " + [...new Set(Validation.Failures)].join("; "));
    Stats[Name] = { Levels: Validation.Levels, SourceTriangles: Hierarchy.SourceTriangles,
        WeldedVertices: Hierarchy.WeldedVertices };
    const LevelIndices = [...new Set([0, Math.floor((Hierarchy.Levels.length - 1) / 2), Hierarchy.Levels.length - 1])];
    const LeafSoup = CollectHierarchyLevel(Hierarchy, 0);
    const View = FrameView(LeafSoup.Positions, Azimuth, Elevation);
    writeFileSync(OutDirectory + Name + "-source.png",
        EncodePng(RenderSoup(LeafSoup.Positions, (_, __, Shade) => [Math.round(180 * Shade), Math.round(184 * Shade), Math.round(196 * Shade)], View)));
    const MaxError = Math.max(...Hierarchy.Clusters.map((Node) => Node.Error), 0);
    for (const Level of LevelIndices)
    {
        const Soup = CollectHierarchyLevel(Hierarchy, Level);
        writeFileSync(OutDirectory + Name + "-level" + Level + "-meshlets.png",
            EncodePng(RenderSoup(Soup.Positions, (Triangle, _, Shade) => OwnerColour(Soup.Owners[Triangle], Shade), View)));
        writeFileSync(OutDirectory + Name + "-level" + Level + "-error.png",
            EncodePng(RenderSoup(Soup.Positions, (Triangle, _, Shade) => ErrorColour(Soup.Errors[Triangle], MaxError, Shade), View)));
        if (ClassOf)
            writeFileSync(OutDirectory + Name + "-level" + Level + "-classes.png",
                EncodePng(RenderSoup(Soup.Positions, (Triangle, _, Shade) => ClassColour(ClassOf(Soup.Families[Triangle]), Shade), View)));
        if (UvAware)
        {
            writeFileSync(OutDirectory + Name + "-level" + Level + "-uv.png",
                EncodePng(RenderSoup(Soup.Positions, (_, __, Shade, Uv) => UvCheckerColour(Uv, Shade), View, Soup.CornerUVs)));
            writeFileSync(OutDirectory + Name + "-level" + Level + "-features.png",
                EncodePng(RenderSoup(Soup.Positions,
                    (Triangle, _, Shade) => FeatureColour(Soup.SharpMask[Triangle], Soup.UvSeamMask[Triangle], Shade), View)));
        }
    }
    const RuntimeCamera = { Eye: View.Eye, Target: View.Target, Up: View.Up, FovY: View.FieldOfView,
        ScreenWidth: View.Width, ScreenHeight: View.Height, Near: 0.1, Far: 1000 };
    Stats[Name].RuntimeCuts = [64, 8, 1].map((Threshold) =>
    {
        const Cut = SelectHierarchyCut(Hierarchy, RuntimeCamera, null, { PixelErrorThreshold: Threshold });
        return { ThresholdPx: Threshold, Clusters: Cut.CutClusters.length, Triangles: Cut.CutTriangles };
    });
    const GifFrames = Hierarchy.Levels.map((_, Level) =>
    {
        const Soup = CollectHierarchyLevel(Hierarchy, Level);
        return RenderSoup(Soup.Positions, (Triangle, _, Shade) => OwnerColour(Soup.Owners[Triangle], Shade), View);
    });
    writeFileSync(OutDirectory + Name + "-levels.gif", EncodeGif(GifFrames, 80));
    console.log(Name + ": " + Hierarchy.Levels.map((L) => L.TriangleCount).join(" -> ") +
        " tris, " + Hierarchy.Levels.at(-1).Clusters.length + " root cluster(s), VALID");
}

ProveScene("sphere", ConstructSphereFacets(4, -1), {}, 0.7, 0.45, null);
ProveScene("glass-sphere", ConstructSphereFacets(4, 9), { TransmissiveFamilies: [9] }, 0.7, 0.45,
    (Family) => (Family === 9 ? 1 : 0));
ProveScene("heightfield-cliff", ConstructHeightfieldFacets(64, 40), {}, -0.8, 0.85, null);
ProveScene("uv-creased-vessel", ConstructUvSeamCreasedMeshFacets(48, 20, 6), {}, 0.9, 0.5, null, true);
writeFileSync(OutDirectory + "stats.json", JSON.stringify(Stats, null, 2) + "\n");
console.log("Evidence written to " + OutDirectory);
