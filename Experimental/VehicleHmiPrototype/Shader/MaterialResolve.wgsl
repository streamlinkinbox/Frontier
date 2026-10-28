//============================================================================================================================================
//                                                          MATERIALRESOLVE.WGSL
//============================================================================================================================================
// 🧩 Phase-3 deferred resolve: visibility identity + barycentrics -> world position / UV / normal -> material.
//    A second entry refracts glass pixels through their front (entry) and peeled back (exit) layers and
//    gathers the resolved opaque image, with Beer absorption and Schlick Fresnel sky reflection.

//! include "FrameDescriptor.wgsl"

@group(0) @binding(1) var<storage, read> Occupants  : array<OccupantRecord>;
@group(0) @binding(2) var<storage, read> CornerPos  : array<vec4f>;   // xyz local position, w = family bitcast to f32
@group(0) @binding(3) var<storage, read> CornerData : array<vec4f>;   // xy UV, zw unused
@group(0) @binding(4) var OpaqueVisTex              : texture_2d<u32>;
@group(0) @binding(5) var OpaqueBaryTex             : texture_2d<f32>;
@group(0) @binding(6) var OpaqueResolved            : texture_storage_2d<rgba16float, write>;
@group(0) @binding(7) var GlassVisTex               : texture_2d<u32>;
@group(0) @binding(8) var GlassBaryTex              : texture_2d<f32>;
@group(0) @binding(9) var GlassBackVisTex           : texture_2d<u32>;
@group(0) @binding(10) var GlassBackBaryTex         : texture_2d<f32>;
@group(0) @binding(11) var FinalResolved            : texture_storage_2d<rgba16float, write>;
@group(0) @binding(12) var OpaqueResolvedRead       : texture_2d<f32>;
@group(0) @binding(13) var HiZTexture               : texture_2d<f32>;

const SkyColour = vec3f(0.36, 0.48, 0.66);
const LightDirection = vec3f(0.375, 0.625, 0.5);
const GlassEta = 1.52;

fn FamilyColour(Family : u32) -> vec3f
{
    switch (Family % 8u)
    {
        case 0u: { return vec3f(0.62, 0.65, 0.70); }
        case 1u: { return vec3f(0.55, 0.48, 0.38); }
        case 2u: { return vec3f(0.72, 0.74, 0.78); }
        case 3u: { return vec3f(0.30, 0.34, 0.40); }
        case 4u: { return vec3f(0.66, 0.30, 0.24); }
        case 5u: { return vec3f(0.24, 0.44, 0.62); }
        case 6u: { return vec3f(0.28, 0.56, 0.36); }
        default: { return vec3f(0.70, 0.58, 0.26); }
    }
}

struct SurfacePoint
{
    Valid   : bool,
    World   : vec3f,
    Uv      : vec2f,
    Normal  : vec3f,
    Family  : u32,
}

fn ResolvePoint(VisTex : texture_2d<u32>, BaryTex : texture_2d<f32>, Coord : vec2i) -> SurfacePoint
{
    var Point : SurfacePoint;
    let Id = textureLoad(VisTex, Coord, 0);
    if (Id.x == 0u) { return Point; }
    let TriangleFirst = Id.x - 1u;
    let Occupant = Occupants[Id.y];
    let Bary = textureLoad(BaryTex, Coord, 0).xy;
    let Weights = vec3f(1.0 - Bary.x - Bary.y, Bary.x, Bary.y);
    var World = vec3f(0.0);
    var Uv = vec2f(0.0);
    var Corners = array<vec3f, 3>();
    var Family = 0u;
    for (var C = 0u; C < 3u; C = C + 1u)
    {
        let Corner = CornerPos[TriangleFirst + C];
        Corners[C] = (Occupant.LocalToWorld * vec4f(Corner.xyz, 1.0)).xyz;
        World = World + Corners[C] * Weights[C];
        Uv = Uv + CornerData[TriangleFirst + C].xy * Weights[C];
        Family = bitcast<u32>(Corner.w);
    }
    Point.Valid = true;
    Point.World = World;
    Point.Uv = Uv;
    Point.Normal = normalize(cross(Corners[1] - Corners[0], Corners[2] - Corners[0]));
    Point.Family = Family;
    return Point;
}

fn ShadeOpaque(Point : SurfacePoint) -> vec3f
{
    var Colour = FamilyColour(Point.Family);
    if (any(Point.Uv != vec2f(0.0)))
    {
        let Checker = select(0.45, 1.0, (i32(floor(Point.Uv.x * 24.0)) + i32(floor(Point.Uv.y * 24.0))) % 2 == 0);
        Colour = Colour * Checker;
    }
    let Diffuse = max(dot(Point.Normal, normalize(LightDirection)), 0.0);
    return Colour * (0.32 + Diffuse);
}

@compute @workgroup_size(8, 8)
fn ResolveOpaqueVisibility(@builtin(global_invocation_id) ID : vec3u)
{
    let Extent = textureDimensions(OpaqueResolved);
    if (ID.x >= Extent.x || ID.y >= Extent.y) { return; }
    let Coord = vec2i(ID.xy);
    let Point = ResolvePoint(OpaqueVisTex, OpaqueBaryTex, Coord);
    let Colour = select(SkyColour, ShadeOpaque(Point), Point.Valid);
    textureStore(OpaqueResolved, Coord, vec4f(Colour, 1.0));
}

fn RefractDirection(ViewDir : vec3f, Normal : vec3f, EtaRatio : f32) -> vec3f
{
    let CosI = -dot(ViewDir, Normal);
    let K = 1.0 - EtaRatio * EtaRatio * (1.0 - CosI * CosI);
    if (K < 0.0) { return vec3f(0.0); }
    return EtaRatio * ViewDir + (EtaRatio * CosI - sqrt(K)) * Normal;
}

@compute @workgroup_size(8, 8)
fn ResolveRefractionGather(@builtin(global_invocation_id) ID : vec3u)
{
    let Extent = textureDimensions(FinalResolved);
    if (ID.x >= Extent.x || ID.y >= Extent.y) { return; }
    let Coord = vec2i(ID.xy);
    var Colour = textureLoad(OpaqueResolvedRead, Coord, 0).xyz;
    let Front = ResolvePoint(GlassVisTex, GlassBaryTex, Coord);
    if (Front.Valid)
    {
        let Back = ResolvePoint(GlassBackVisTex, GlassBackBaryTex, Coord);
        var ViewDir = Front.World - Frame.CameraPosition;
        let ViewLength = length(ViewDir);
        ViewDir = ViewDir / max(ViewLength, 1.0e-6);
        var Normal = Front.Normal;
        if (dot(Normal, ViewDir) > 0.0) { Normal = -Normal; }
        let Inside = RefractDirection(ViewDir, Normal, 1.0 / GlassEta);
        var Gathered = SkyColour;
        var Thickness = 0.25;
        if (any(Inside != vec3f(0.0)))
        {
            var ExitPoint = Front.World + Inside * Thickness;
            var Outside = Inside;
            if (Back.Valid)
            {
                Thickness = length(Back.World - Front.World);
                ExitPoint = Back.World;
                var ExitNormal = Back.Normal;
                if (dot(ExitNormal, Inside) < 0.0) { ExitNormal = -ExitNormal; }
                let Outgoing = RefractDirection(Inside, -ExitNormal, GlassEta);
                if (any(Outgoing != vec3f(0.0))) { Outside = Outgoing; }
            }
            let SampleWorld = ExitPoint + Outside * 2.0;
            let Clip = Frame.UnjitteredViewProjection * vec4f(SampleWorld, 1.0);
            if (Clip.w > 1.0e-4)
            {
                let Ndc = Clip.xy / Clip.w;
                let SampleCoord = vec2i(vec2f(Ndc.x * 0.5 + 0.5, 0.5 - Ndc.y * 0.5) * vec2f(Extent));
                if (all(SampleCoord >= vec2i(0)) && all(SampleCoord < vec2i(Extent)))
                {
                    Gathered = textureLoad(OpaqueResolvedRead, SampleCoord, 0).xyz;
                }
            }
        }
        let Absorb = exp(vec3f(-0.22, -0.10, -0.16) * Thickness);
        let CosView = clamp(abs(dot(Normal, ViewDir)), 0.0, 1.0);
        let F0 = pow((GlassEta - 1.0) / (GlassEta + 1.0), 2.0);
        let Fresnel = F0 + (1.0 - F0) * pow(1.0 - CosView, 5.0);
        Colour = Gathered * Absorb * (1.0 - Fresnel) + SkyColour * Fresnel;
    }
    textureStore(FinalResolved, Coord, vec4f(Colour, 1.0));
}

//------------------------------------------------------------------------------------------------------------------------
// Debug views for the display inspection list (written into the inspection target, presented un-tonemapped).
//------------------------------------------------------------------------------------------------------------------------

fn DebugHashColour(N : u32) -> vec3f
{
    var X = N * 2654435761u;
    X = (X ^ (X >> 13u)) * 1274126177u;
    X = X ^ (X >> 16u);
    return vec3f(f32((X >> 16u) & 255u), f32((X >> 8u) & 255u), f32(X & 255u)) / 255.0 * 0.85 + vec3f(0.12);
}

// 📝 Visibility buffer IDs: every cluster-triangle gets a stable hash colour; glass front tints cyan,
//    glass back tints magenta so the two refraction layers can be told apart at a glance.
@compute @workgroup_size(8, 8)
fn DebugVisibilityIds(@builtin(global_invocation_id) ID : vec3u)
{
    let Extent = textureDimensions(FinalResolved);
    if (ID.x >= Extent.x || ID.y >= Extent.y) { return; }
    let Coord = vec2i(ID.xy);
    let Opaque = textureLoad(OpaqueVisTex, Coord, 0);
    let Front = textureLoad(GlassVisTex, Coord, 0);
    let Back = textureLoad(GlassBackVisTex, Coord, 0);
    var Colour = vec3f(0.10, 0.12, 0.20);   // visibly non-black: an empty buffer reads as "no coverage", never as a dead view
    if (Opaque.x != 0u) { Colour = DebugHashColour(Opaque.x); }
    if (Front.x != 0u) { Colour = DebugHashColour(Front.x) * vec3f(0.35, 1.0, 1.0); }
    if (Back.x != 0u && Front.x == 0u) { Colour = DebugHashColour(Back.x) * vec3f(1.0, 0.35, 1.0); }
    textureStore(FinalResolved, Coord, vec4f(Colour, 1.0));
}

// 📝 Glass layers: front layer green, back layer red, overlap (two-layer refraction pixels) yellow.
@compute @workgroup_size(8, 8)
fn DebugGlassLayers(@builtin(global_invocation_id) ID : vec3u)
{
    let Extent = textureDimensions(FinalResolved);
    if (ID.x >= Extent.x || ID.y >= Extent.y) { return; }
    let Coord = vec2i(ID.xy);
    let Front = textureLoad(GlassVisTex, Coord, 0);
    let Back = textureLoad(GlassBackVisTex, Coord, 0);
    var Colour = textureLoad(OpaqueResolvedRead, Coord, 0).xyz * 0.35;
    if (Front.x != 0u && Back.x != 0u) { Colour = vec3f(0.95, 0.85, 0.25); }
    else if (Front.x != 0u) { Colour = vec3f(0.25, 0.9, 0.45); }
    else if (Back.x != 0u) { Colour = vec3f(0.95, 0.3, 0.3); }
    textureStore(FinalResolved, Coord, vec4f(Colour, 1.0));
}

// 📝 Deferred resolve output, copied verbatim (binding 12 carries the final resolved texture in this group).
@compute @workgroup_size(8, 8)
fn DebugCopyResolved(@builtin(global_invocation_id) ID : vec3u)
{
    let Extent = textureDimensions(FinalResolved);
    if (ID.x >= Extent.x || ID.y >= Extent.y) { return; }
    let Coord = vec2i(ID.xy);
    textureStore(FinalResolved, Coord, vec4f(textureLoad(OpaqueResolvedRead, Coord, 0).xyz, 1.0));
}

// 📝 Hi-Z pyramid (reversed-Z): near occluders bright, far/clear dark.
@compute @workgroup_size(8, 8)
fn DebugHiZView(@builtin(global_invocation_id) ID : vec3u)
{
    let Extent = textureDimensions(FinalResolved);
    if (ID.x >= Extent.x || ID.y >= Extent.y) { return; }
    let Coord = vec2i(ID.xy);
    let Depth = textureLoad(HiZTexture, Coord, 0).x;
    textureStore(FinalResolved, Coord, vec4f(vec3f(pow(Depth, 0.25)), 1.0));
}

// 📝 LOD levels: which DAG level the cut picked per pixel, read from the level stored in corner-data z
//    (build-time, streamed with the page payload, so it always matches the triangle actually drawn).
//    Ordered palette: level 0 (finest leaves) is warm red, each coarser level shifts towards blue.
//    On screen the fine→coarse transition band must move as the camera dollies: near geometry stays
//    red/orange, distant geometry cools to blue — that moving band IS the continuous LOD cut working.
//    Flat geometry may stay one colour at every distance: its simplification error is zero, so the
//    coarsest level is already pixel-exact and the cut correctly never refines it.
@compute @workgroup_size(8, 8)
fn DebugLodLevels(@builtin(global_invocation_id) ID : vec3u)
{
    let Extent = textureDimensions(FinalResolved);
    if (ID.x >= Extent.x || ID.y >= Extent.y) { return; }
    let Coord = vec2i(ID.xy);
    let Opaque = textureLoad(OpaqueVisTex, Coord, 0);
    let Front = textureLoad(GlassVisTex, Coord, 0);
    var Colour = vec3f(0.10, 0.12, 0.20);
    var Vis = Opaque;
    if (Vis.x == 0u) { Vis = Front; }
    if (Vis.x != 0u)
    {
        let Level = CornerData[Vis.x - 1u].z;
        Colour = 0.55 + 0.45 * cos(6.2831853 * (Level * 0.14) + vec3f(0.0, 0.33, 0.67));
        if (Front.x != 0u && Opaque.x == 0u) { Colour = Colour * vec3f(0.35, 1.0, 1.0); }   // glass keeps the cyan tell
    }
    textureStore(FinalResolved, Coord, vec4f(Colour, 1.0));
}
