//------------------------------------------------------------------------------------------------------------------------
// VISIBILITY RESOLVE — VISUAL PROOF RENDERER
//------------------------------------------------------------------------------------------------------------------------
// Renders Phase 3 with the CPU reference path (same maths the WGSL programs run): visibility buffer coloured
// by cluster identity, deferred opaque resolve, and the final two-layer refraction composite for glass.
// Run: node Tools/VisibilityResolveProof.mjs   ->  Evidence/VisibilityResolve/*.png + stats.json

import { existsSync, mkdirSync, readdirSync, unlinkSync, writeFileSync } from "node:fs";
import { deflateSync } from "node:zlib";
import { BuildGeometryHierarchy } from "../Source/GeometryHierarchy.js";
import { ExecuteHierarchyRuntimeFrame } from "../Source/GeometryHierarchyRuntime.js";
import { RasterizeVisibility, ResolveVisibility, UnpackVisibilityId } from "../Source/VisibilityResolve.js";
import { ConstructSphereFacets, ConstructUvSeamCreasedMeshFacets } from "./GeometryHierarchyCorpus.mjs";

const CrcTable = Array.from({ length: 256 }, (_, N) => { let C = N; for (let K = 0; K < 8; ++K) C = C & 1 ? 0xEDB88320 ^ (C >>> 1) : C >>> 1; return C >>> 0; });
function Crc32(Buffer) { let C = 0xFFFFFFFF; for (const Byte of Buffer) C = CrcTable[(C ^ Byte) & 0xFF] ^ (C >>> 8); return (C ^ 0xFFFFFFFF) >>> 0; }
function Chunk(Type, Data) { const L = Buffer.alloc(4); L.writeUInt32BE(Data.length); const Body = Buffer.concat([Buffer.from(Type, "ascii"), Data]); const C = Buffer.alloc(4); C.writeUInt32BE(Crc32(Body)); return Buffer.concat([L, Body, C]); }
function EncodePng(Width, Height, Rgb)
{
    const Header = Buffer.alloc(13); Header.writeUInt32BE(Width, 0); Header.writeUInt32BE(Height, 4); Header[8] = 8; Header[9] = 2;
    const Rows = Buffer.alloc(Height * (Width * 3 + 1));
    for (let Y = 0; Y < Height; ++Y) Rgb.copy(Rows, Y * (Width * 3 + 1) + 1, Y * Width * 3, (Y + 1) * Width * 3);
    return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), Chunk("IHDR", Header), Chunk("IDAT", deflateSync(Rows, { level: 6 })), Chunk("IEND", Buffer.alloc(0))]);
}
function HslToRgb(H, S, L)
{
    const C = (1 - Math.abs(2 * L - 1)) * S, X = C * (1 - Math.abs((H / 60) % 2 - 1)), M = L - C / 2;
    const Rgb = [[C, X, 0], [X, C, 0], [0, C, X], [0, X, C], [X, 0, C], [C, 0, X]][Math.floor(H / 60) % 6];
    return Rgb.map((V) => Math.round((V + M) * 255));
}

const OutDirectory = new URL("../Evidence/VisibilityResolve/", import.meta.url).pathname;
if (!existsSync(OutDirectory)) mkdirSync(OutDirectory, { recursive: true });
for (const Entry of readdirSync(OutDirectory)) if (/\.(png)$/.test(Entry) || Entry === "stats.json") unlinkSync(OutDirectory + Entry);

const Width = 640, Height = 480;
const Camera = { Eye: [2.5, -7.5, 2.2], Target: [0, 0, 0], Up: [0, 0, 1], FovY: Math.PI / 3, ScreenWidth: Width, ScreenHeight: Height, Near: 0.1, Far: 200 };
const Stats = {};

function ProveScene(Name, Hierarchy, Threshold, SceneCamera = Camera)
{
    const Frame = ExecuteHierarchyRuntimeFrame([{ Hierarchy, LocalToWorld: null, Occupant: 0 }], SceneCamera, { PixelErrorThreshold: Threshold });
    const Items = Frame.VisibleClusters.map((Item) => ({ Hierarchy, Node: Item.Node, LocalToWorld: null }));
    const Raster = RasterizeVisibility(Items, Width, Height, Frame.ViewProjection);
    const Resolved = ResolveVisibility(Raster, { ...SceneCamera, ViewProjection: Frame.ViewProjection });

    const VisImage = Buffer.alloc(Width * Height * 3);
    const OpaqueImage = Buffer.alloc(Width * Height * 3);
    const FinalImage = Buffer.alloc(Width * Height * 3);
    for (let Pixel = 0; Pixel < Width * Height; ++Pixel)
    {
        const Id = Raster.OpaqueVis[Pixel] || Raster.GlassVis[0][Pixel];
        if (Id)
        {
            const Slot = UnpackVisibilityId(Id).RecordSlot;
            const [R, G, B] = HslToRgb((Slot * 137.508) % 360, 0.62, Raster.GlassVis[0][Pixel] ? 0.75 : 0.52);
            VisImage.set([R, G, B], Pixel * 3);
        }
        else VisImage.set([16, 20, 30], Pixel * 3);
        OpaqueImage.set(Resolved.OpaqueColor.subarray(Pixel * 3, Pixel * 3 + 3).map((V) => Math.min(255, Math.round(V * 255))), Pixel * 3);
        FinalImage.set(Resolved.FinalColor.subarray(Pixel * 3, Pixel * 3 + 3).map((V) => Math.min(255, Math.round(V * 255))), Pixel * 3);
    }
    writeFileSync(OutDirectory + Name + "-visbuffer.png", EncodePng(Width, Height, VisImage));
    writeFileSync(OutDirectory + Name + "-resolved.png", EncodePng(Width, Height, OpaqueImage));
    writeFileSync(OutDirectory + Name + "-final.png", EncodePng(Width, Height, FinalImage));
    Stats[Name] = { VisibleClusters: Items.length, VisibleTriangles: Frame.Stats.VisibleTriangles,
                    GlassPixels: Resolved.GlassPixels, TwoLayerPixels: Resolved.TwoLayerPixels };
    console.log(`${Name}: ${Items.length} clusters, ${Resolved.GlassPixels} glass px (${Resolved.TwoLayerPixels} two-layer)`);
}

ProveScene("glass-sphere", BuildGeometryHierarchy(ConstructSphereFacets(4, 9), { TransmissiveFamilies: [9] }), 1.5,
    { ...Camera, Eye: [1.1, -3.4, 0.9] });
ProveScene("uv-creased-vessel", BuildGeometryHierarchy(ConstructUvSeamCreasedMeshFacets(48, 20, 6)), 1.5);
writeFileSync(OutDirectory + "stats.json", JSON.stringify(Stats, null, 2) + "\n");
console.log("Evidence written to " + OutDirectory);
