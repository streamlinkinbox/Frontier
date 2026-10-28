//============================================================================================================================================
//                                                          VISIBILITYRESOLVE.JS
//============================================================================================================================================
// 🧩 Phase 3: visibility-buffer raster (cluster + triangle identity, no material work), deferred material
//    resolve from interpolated UV/family, and a two-layer refraction resolve for transmissive clusters.
//    Transmissive geometry never enters the opaque visibility/depth buffers; it is depth-peeled into two
//    glass layers (front, back) so the resolve can refract through an entry and an exit surface.

import { MultiplyMatrices, TransformClipPoint, TransformWorldPoint } from "./GeometryHierarchyRuntime.js";

export const GlassIndexOfRefraction = 1.52;

// 📝 Visibility identity packing: 0 = background, otherwise (recordSlot * 256 + localTriangle + 1).
//    Clusters hold at most 128 triangles by construction, so 8 bits of local index cannot overflow.
export function PackVisibilityId(RecordSlot, LocalTriangle)
{
    return RecordSlot * 256 + LocalTriangle + 1;
}
export function UnpackVisibilityId(Id)
{
    if (!Id) return null;
    const Zero = Id - 1;
    return { RecordSlot: Math.floor(Zero / 256), LocalTriangle: Zero % 256 };
}

function TriangleWorldCorners(Item, Triangle)
{
    const Level = Item.Hierarchy.Levels[Item.Node.Level];
    const Corners = [];
    for (let C = 0; C < 3; ++C)
    {
        const Vertex = Level.Triangles[Triangle * 3 + C];
        const Local = [Level.Positions[Vertex * 3], Level.Positions[Vertex * 3 + 1], Level.Positions[Vertex * 3 + 2]];
        Corners.push({ Local, World: TransformWorldPoint(Item.LocalToWorld, Local),
                       Uv: Level.CornerUVs ? [Level.CornerUVs[Triangle * 6 + C * 2], Level.CornerUVs[Triangle * 6 + C * 2 + 1]] : [0, 0] });
    }
    return Corners;
}

function FaceNormal(Corners)
{
    const AB = Corners[1].World.map((V, A) => V - Corners[0].World[A]);
    const AC = Corners[2].World.map((V, A) => V - Corners[0].World[A]);
    const N = [AB[1] * AC[2] - AB[2] * AC[1], AB[2] * AC[0] - AB[0] * AC[2], AB[0] * AC[1] - AB[1] * AC[0]];
    const L = Math.hypot(N[0], N[1], N[2]) || 1;
    return [N[0] / L, N[1] / L, N[2] / L];
}

// 📝 Software visibility raster over the runtime's visible clusters. Opaque clusters depth-test into the
//    opaque visibility buffer; transmissive clusters depth-peel into front/back glass layers and never
//    touch opaque depth (the CPU mirror of the Hi-Z rule). Barycentrics are stored per layer for resolve.
export function RasterizeVisibility(Items, Width, Height, ViewProjection)
{
    const Pixels = Width * Height;
    const OpaqueVis = new Uint32Array(Pixels);
    const OpaqueDepth = new Float32Array(Pixels);            // reversed-Z: larger = nearer, 0 = clear
    const OpaqueBary = new Float32Array(Pixels * 2);
    const GlassVis = [new Uint32Array(Pixels), new Uint32Array(Pixels)];
    const GlassDepth = [new Float32Array(Pixels), new Float32Array(Pixels)];
    const GlassBary = [new Float32Array(Pixels * 2), new Float32Array(Pixels * 2)];

    // 📝 Two internal passes: opaque first, then glass depth-tested against the opaque buffer — a glass
    //    surface behind opaque geometry is occluded by it, while glass in front refracts what is behind.
    const OrderedItems = Items.map((Item, Slot) => ({ Item, Slot }))
        .sort((A, B) => ((A.Item.Node.ClassMask & 2) !== 0) - ((B.Item.Node.ClassMask & 2) !== 0));
    OrderedItems.forEach(({ Item, Slot }) =>
    {
        const M = Item.LocalToWorld ? MultiplyMatrices(ViewProjection, Item.LocalToWorld) : ViewProjection;
        const Level = Item.Hierarchy.Levels[Item.Node.Level];
        const IsGlass = (Item.Node.ClassMask & 2) !== 0;
        Item.Node.Triangles.forEach((Triangle, LocalTriangle) =>
        {
            const WorldCorners = TriangleWorldCorners(Item, Triangle);
            const S = [];
            let Valid = true;
            for (let C = 0; C < 3; ++C)
            {
                const Local = WorldCorners[C].Local;
                const Q = TransformClipPoint(M, Local);
                if (Q[3] <= 1e-4) { Valid = false; break; }
                S.push([(Q[0] / Q[3] * 0.5 + 0.5) * Width, (0.5 - Q[1] / Q[3] * 0.5) * Height, Q[2] / Q[3]]);
            }
            if (!Valid) return;
            const Area = (S[1][0] - S[0][0]) * (S[2][1] - S[0][1]) - (S[1][1] - S[0][1]) * (S[2][0] - S[0][0]);
            if (Math.abs(Area) < 1e-5) return;
            const MinX = Math.max(0, Math.floor(Math.min(S[0][0], S[1][0], S[2][0])));
            const MaxX = Math.min(Width - 1, Math.ceil(Math.max(S[0][0], S[1][0], S[2][0])));
            const MinY = Math.max(0, Math.floor(Math.min(S[0][1], S[1][1], S[2][1])));
            const MaxY = Math.min(Height - 1, Math.ceil(Math.max(S[0][1], S[1][1], S[2][1])));
            const Id = PackVisibilityId(Slot, LocalTriangle);
            for (let Y = MinY; Y <= MaxY; ++Y)
                for (let X = MinX; X <= MaxX; ++X)
                {
                    const PX = X + 0.5, PY = Y + 0.5;
                    const W0 = ((S[1][0] - PX) * (S[2][1] - PY) - (S[1][1] - PY) * (S[2][0] - PX)) / Area;
                    const W1 = ((S[2][0] - PX) * (S[0][1] - PY) - (S[2][1] - PY) * (S[0][0] - PX)) / Area;
                    const W2 = 1 - W0 - W1;
                    if (W0 < 0 || W1 < 0 || W2 < 0) continue;
                    const Z = W0 * S[0][2] + W1 * S[1][2] + W2 * S[2][2];
                    if (Z < 0 || Z > 1) continue;
                    const Pixel = Y * Width + X;
                    if (IsGlass && Z <= OpaqueDepth[Pixel]) continue;   // occluded by nearer opaque surface
                    if (!IsGlass)
                    {
                        if (Z <= OpaqueDepth[Pixel]) continue;
                        OpaqueDepth[Pixel] = Z;
                        OpaqueVis[Pixel] = Id;
                        OpaqueBary[Pixel * 2] = W1; OpaqueBary[Pixel * 2 + 1] = W2;
                    }
                    else if (Z > GlassDepth[0][Pixel])
                    {
                        // New nearest glass surface: demote the old front layer to the back layer.
                        GlassVis[1][Pixel] = GlassVis[0][Pixel];
                        GlassDepth[1][Pixel] = GlassDepth[0][Pixel];
                        GlassBary[1][Pixel * 2] = GlassBary[0][Pixel * 2];
                        GlassBary[1][Pixel * 2 + 1] = GlassBary[0][Pixel * 2 + 1];
                        GlassVis[0][Pixel] = Id;
                        GlassDepth[0][Pixel] = Z;
                        GlassBary[0][Pixel * 2] = W1; GlassBary[0][Pixel * 2 + 1] = W2;
                    }
                    else if (Z > GlassDepth[1][Pixel])
                    {
                        GlassVis[1][Pixel] = Id;
                        GlassDepth[1][Pixel] = Z;
                        GlassBary[1][Pixel * 2] = W1; GlassBary[1][Pixel * 2 + 1] = W2;
                    }
                }
        });
    });
    return { Width, Height, OpaqueVis, OpaqueDepth, OpaqueBary, GlassVis, GlassDepth, GlassBary, Items };
}

// 📝 Procedural material palette by family id (the CPU stand-in for the material strata the GPU resolve
//    samples): stable per-family base colours so resolve output is deterministic.
const FamilyPalette = [
    [0.62, 0.65, 0.70], [0.55, 0.48, 0.38], [0.72, 0.74, 0.78], [0.30, 0.34, 0.40],
    [0.66, 0.30, 0.24], [0.24, 0.44, 0.62], [0.28, 0.56, 0.36], [0.70, 0.58, 0.26]
];
export function FamilyBaseColour(Family)
{
    return FamilyPalette[Math.abs(Family) % FamilyPalette.length];
}

function ResolveSurfacePoint(Raster, Vis, Bary, Pixel)
{
    const Decoded = UnpackVisibilityId(Vis[Pixel]);
    if (!Decoded) return null;
    const Item = Raster.Items[Decoded.RecordSlot];
    if (!Item) return null;
    const Triangle = Item.Node.Triangles[Decoded.LocalTriangle];
    if (Triangle === undefined) return null;
    const Corners = TriangleWorldCorners(Item, Triangle);
    const W1 = Bary[Pixel * 2], W2 = Bary[Pixel * 2 + 1], W0 = 1 - W1 - W2;
    const World = [0, 1, 2].map((A) => W0 * Corners[0].World[A] + W1 * Corners[1].World[A] + W2 * Corners[2].World[A]);
    const Uv = [0, 1].map((A) => W0 * Corners[0].Uv[A] + W1 * Corners[1].Uv[A] + W2 * Corners[2].Uv[A]);
    const Level = Item.Hierarchy.Levels[Item.Node.Level];
    return { Item, Triangle, World, Uv, Normal: FaceNormal(Corners),
             Family: Level.Families[Triangle], ClassMask: Item.Node.ClassMask };
}

const LightDirection = (() => { const L = Math.hypot(0.45, 0.75, 0.6); return [0.45 / L, 0.75 / L, 0.6 / L]; })();
const SkyColour = [0.36, 0.48, 0.66];

function ShadeOpaque(Surface)
{
    const Base = FamilyBaseColour(Surface.Family);
    let R = Base[0], G = Base[1], B = Base[2];
    // UV checker modulation where the mesh carries UVs: deferred resolve is where UVs become material.
    if (Surface.Item.Hierarchy.HasUVs)
    {
        const Checker = (Math.floor(Surface.Uv[0] * 24) + Math.floor(Surface.Uv[1] * 24)) % 2 === 0 ? 1.0 : 0.45;
        R *= Checker; G *= Checker; B *= Checker;
    }
    const Diffuse = Math.max(0, Surface.Normal[0] * LightDirection[0] + Surface.Normal[1] * LightDirection[1] + Surface.Normal[2] * LightDirection[2]);
    const Ambient = 0.32;
    return [R * (Ambient + Diffuse), G * (Ambient + Diffuse), B * (Ambient + Diffuse)];
}

function RefractDirection(ViewDir, Normal, EtaRatio)
{
    const CosI = -(ViewDir[0] * Normal[0] + ViewDir[1] * Normal[1] + ViewDir[2] * Normal[2]);
    const K = 1 - EtaRatio * EtaRatio * (1 - CosI * CosI);
    if (K < 0) return null;   // total internal reflection
    const F = EtaRatio * CosI - Math.sqrt(K);
    return [EtaRatio * ViewDir[0] + F * Normal[0], EtaRatio * ViewDir[1] + F * Normal[1], EtaRatio * ViewDir[2] + F * Normal[2]];
}

// 📝 Deferred resolve: opaque visibility -> lit opaque colour; then each glass pixel refracts through its
//    front (entry) and back (exit) layers, gathers the resolved opaque colour at the exit projection, tints
//    by Beer absorption over the travelled thickness, and mixes a Fresnel sky reflection.
export function ResolveVisibility(Raster, Camera, Options = {})
{
    const { Width, Height } = Raster;
    const Pixels = Width * Height;
    const Eta = Options.IndexOfRefraction ?? GlassIndexOfRefraction;
    const OpaqueColor = new Float32Array(Pixels * 3);
    const FinalColor = new Float32Array(Pixels * 3);
    const Eye = Camera.Eye ?? Camera.Position ?? [0, -10, 4];

    for (let Pixel = 0; Pixel < Pixels; ++Pixel)
    {
        const Surface = ResolveSurfacePoint(Raster, Raster.OpaqueVis, Raster.OpaqueBary, Pixel);
        const Colour = Surface ? ShadeOpaque(Surface) : SkyColour;
        OpaqueColor.set(Colour, Pixel * 3);
        FinalColor.set(Colour, Pixel * 3);
    }

    // Screen projection for the refraction gather (same convention as the raster).
    const ViewProjection = Camera.ViewProjection ?? Options.ViewProjection;
    const ProjectToPixel = (World) =>
    {
        if (!ViewProjection) return null;
        const Q = TransformClipPoint(ViewProjection, World);
        if (Q[3] <= 1e-4) return null;
        const X = Math.floor((Q[0] / Q[3] * 0.5 + 0.5) * Width);
        const Y = Math.floor((0.5 - Q[1] / Q[3] * 0.5) * Height);
        if (X < 0 || X >= Width || Y < 0 || Y >= Height) return null;
        return Y * Width + X;
    };

    let GlassPixels = 0, TwoLayerPixels = 0;
    for (let Pixel = 0; Pixel < Pixels; ++Pixel)
    {
        const Front = ResolveSurfacePoint(Raster, Raster.GlassVis[0], Raster.GlassBary[0], Pixel);
        if (!Front) continue;
        ++GlassPixels;
        const Back = ResolveSurfacePoint(Raster, Raster.GlassVis[1], Raster.GlassBary[1], Pixel);
        if (Back) ++TwoLayerPixels;
        let ViewDir = Front.World.map((V, A) => V - Eye[A]);
        const ViewLength = Math.hypot(...ViewDir) || 1;
        ViewDir = ViewDir.map((V) => V / ViewLength);
        let Normal = Front.Normal;
        if (Normal[0] * ViewDir[0] + Normal[1] * ViewDir[1] + Normal[2] * ViewDir[2] > 0) Normal = Normal.map((V) => -V);
        const Inside = RefractDirection(ViewDir, Normal, 1 / Eta);
        let Gathered = SkyColour;
        let Thickness = Front.Item.Node.Sphere.Radius * 0.5;
        if (Inside)
        {
            let ExitPoint, ExitNormal = null;
            if (Back)
            {
                Thickness = Math.hypot(...Back.World.map((V, A) => V - Front.World[A]));
                ExitPoint = Back.World;
                ExitNormal = Back.Normal;
                if (ExitNormal[0] * Inside[0] + ExitNormal[1] * Inside[1] + ExitNormal[2] * Inside[2] < 0)
                    ExitNormal = ExitNormal.map((V) => -V);
            }
            else ExitPoint = Front.World.map((V, A) => V + Inside[A] * Thickness);
            const Outside = ExitNormal ? RefractDirection(Inside, ExitNormal.map((V) => -V), Eta) : Inside;
            const SamplePoint = Outside
                ? ExitPoint.map((V, A) => V + Outside[A] * 2.0)
                : ExitPoint;
            const SamplePixel = ProjectToPixel(SamplePoint);
            if (SamplePixel !== null)
                Gathered = [OpaqueColor[SamplePixel * 3], OpaqueColor[SamplePixel * 3 + 1], OpaqueColor[SamplePixel * 3 + 2]];
            else if (Outside)
                Gathered = SkyColour;
        }
        // Beer absorption over thickness (pale green glass tint) + Schlick Fresnel sky reflection.
        const Absorb = [Math.exp(-0.22 * Thickness), Math.exp(-0.10 * Thickness), Math.exp(-0.16 * Thickness)];
        const CosView = Math.min(1, Math.abs(Normal[0] * ViewDir[0] + Normal[1] * ViewDir[1] + Normal[2] * ViewDir[2]));
        const F0 = Math.pow((Eta - 1) / (Eta + 1), 2);
        const Fresnel = F0 + (1 - F0) * Math.pow(1 - CosView, 5);
        const Colour = [0, 1, 2].map((C) => Gathered[C] * Absorb[C] * (1 - Fresnel) + SkyColour[C] * Fresnel);
        FinalColor.set(Colour, Pixel * 3);
    }
    return { Width, Height, OpaqueColor, FinalColor, GlassPixels, TwoLayerPixels };
}
