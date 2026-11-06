//============================================================================================================================================
//                                                            CABINOVERLAY.WGSL
//============================================================================================================================================
// 🧩 What the deferred raster cannot carry: see-through glazing, the HMI's two screens and the mirrors' pictures, drawn over it.

//! include "FrameDescriptor.wgsl"
//! include "SkyProjection.wgsl"
//! include "EffectRecords.wgsl"
//! include "VehicleVisual.wgsl"

struct OverlayRecord
{
    Tablet   : vec4f,   // x luminance, y brightness [0, 1], z garage preview shown (1), w drag chip over well (1)
    Preview  : vec4f,   // xy top-left, zw bottom-right of the preview well in tablet UV
    Corner   : vec4f,   // xy the well's corner radius in UV, z cluster luminance, w drag pointer U
    Glass    : vec4f,   // x transmission at normal incidence, y mirror reflectance, z mirrors live (1), w drag pointer V
    Stage    : vec4f,   // x inspection view (0..3), y holoAmount [0..1], z scanX, w clock [s]
}

@group(0) @binding(1) var<storage, read> Occupants    : array<OccupantRecord>;
@group(0) @binding(2) var<uniform> Overlay            : OverlayRecord;
@group(0) @binding(3) var<uniform> Effect             : EffectRecord;
@group(0) @binding(4) var<storage, read> VehicleState : VehicleRecord;
@group(0) @binding(5) var TabletTexture               : texture_2d<f32>;
@group(0) @binding(6) var ClusterTexture              : texture_2d<f32>;
@group(0) @binding(7) var PreviewTexture              : texture_2d<f32>;
@group(0) @binding(8) var MirrorTexture               : texture_2d_array<f32>;
@group(0) @binding(9) var LinearSampler               : sampler;
@group(0) @binding(10) var SkyView                    : texture_2d<f32>;
@group(0) @binding(11) var SkySampler                 : sampler;

@group(0) @binding(12) var<storage, read> DisplayAdaptation : array<f32>;

const KindGlass   : u32 = 0u;
const KindTablet  : u32 = 1u;
const KindCluster : u32 = 2u;
const KindMirror  : u32 = 3u;

struct OverlayVarying
{
    @builtin(position) Position             : vec4f,
    @location(0) Normal                     : vec3f,
    @location(1) Coordinates                : vec2f,
    @location(2) WorldPosition              : vec3f,
    @location(3) BodyLocal                  : vec3f,
    @location(4) @interpolate(flat) Kind    : u32,
    @location(5) @interpolate(flat) Layer   : u32,
}

@vertex
fn OverlayVertex(@location(0) Position: vec3f, @location(1) Kind: u32, @location(2) Normal: vec3f, @location(3) Layer: f32,
                 @location(4) Coordinates: vec2f) -> OverlayVarying
{
    let Body = Occupants[0];
    var Placed = Position;
    if (Kind == KindTablet) { Placed = PlaceTablet(Position); }
    var TurnedNormal = Normal;
    if (Kind == KindCluster) {
        let Turned = TurnSteeringWheel(Position, Normal);
        Placed = Turned.Position;
        TurnedNormal = Turned.Normal;
    }
    let Lifted = Placed + TurnedNormal * select(0.0015, 0.0, Kind == KindGlass);
    let World = Body.LocalToWorld * vec4f(Lifted, 1.0);
    var Varying: OverlayVarying;
    Varying.Position = Frame.ViewProjection * World;
    Varying.Normal = (Body.LocalToWorld * vec4f(TurnedNormal, 0.0)).xyz;
    Varying.Coordinates = Coordinates;
    Varying.WorldPosition = World.xyz;
    Varying.BodyLocal = Lifted;
    Varying.Kind = Kind;
    Varying.Layer = u32(Layer + 0.5);
    return Varying;
}

fn ClassifyMasked(BodyLocal: vec3f) -> bool
{
    if (Effect.MaskMode == 0u)
    {
        return false;
    }
    if (Effect.MaskMode == 3u)
    {
        return true;
    }
    let Unit = (BodyLocal - Effect.BodyMinimum.xyz) / max(Effect.BodyMaximum.xyz - Effect.BodyMinimum.xyz, vec3f(1.0e-3));
    let Elapsed = Effect.Clock - Effect.MaskStart;
    let Seed = u32(Effect.Emitters[0].Centre.w);
    if (Effect.MaskMode == 1u)
    {
        return Elapsed >= ResolveDerezRelease(Unit, Seed, Effect.Style, 1.0);
    }
    return Elapsed < ResolveRezArrival(Unit, Seed, Effect.Style, 1.0);
}

fn SampleSky(Direction: vec3f) -> vec3f
{
    return textureSampleLevel(SkyView, SkySampler, ProjectSkyCoordinates(Direction), 0.0).rgb * Frame.SkyScale + Frame.NightSkyRadiance;
}

// 📐 Rounded-rectangle coverage of the preview well in tablet UV (aspect-correct corner radii).
fn ResolveWellCoverage(Coordinates: vec2f) -> f32
{
    let Low = Overlay.Preview.xy;
    let High = Overlay.Preview.zw;
    if (any(Coordinates < Low) || any(Coordinates > High))
    {
        return 0.0;
    }
    let Radius = Overlay.Corner.xy;
    let Inner = clamp(Coordinates, Low + Radius, High - Radius);
    let Offset = (Coordinates - Inner) / max(Radius, vec2f(1.0e-5));
    return select(1.0, 0.0, dot(Offset, Offset) > 1.0);
}

// 🎨 Reference studio backdrop (App.tsx + Scene.tsx): dark studio gradient, overhead softbox glow, subtle 56 px grid & floor contact shadow.
fn ResolveStudioStageBackdrop(Inside: vec2f) -> vec3f
{
    let TopCol = vec3f(0.015, 0.020, 0.030);
    let MidCol = vec3f(0.0055, 0.0075, 0.012);
    let BotCol = vec3f(0.0018, 0.0024, 0.004);
    var Stage  = select(mix(MidCol, BotCol, (Inside.y - 0.56) / 0.44), mix(TopCol, MidCol, Inside.y / 0.56), Inside.y < 0.56);

    // Overhead studio spotlight pool
    let PoolOffset = (Inside - vec2f(0.50, 0.36)) / vec2f(0.48, 0.34);
    Stage += vec3f(0.026, 0.036, 0.054) * exp(-dot(PoolOffset, PoolOffset) * 1.65);

    // Subtle 56 px architectural grid (App.tsx)
    let Pixels = Inside * vec2f(640.0, 400.0);
    let GridCell = abs(fract(Pixels / 44.0) - 0.5) * 44.0;
    let GridLine = 1.0 - smoothstep(0.0, 1.15, min(GridCell.x, GridCell.y));
    Stage += vec3f(0.012, 0.016, 0.022) * GridLine * (1.0 - smoothstep(0.15, 0.85, length(Inside - vec2f(0.5, 0.45))));

    // Floor horizon sheen + soft contact shadow beneath the vehicle
    let FloorGlow = exp(-pow((Inside.y - 0.64) * 7.0, 2.0)) * (1.0 - smoothstep(0.15, 0.52, abs(Inside.x - 0.5)));
    Stage += vec3f(0.010, 0.014, 0.020) * FloorGlow;
    let ShadowOffset = (Inside - vec2f(0.50, 0.72)) / vec2f(0.32, 0.095);
    let ContactShadow = exp(-dot(ShadowOffset, ShadowOffset) * 1.9);
    Stage *= 1.0 - 0.68 * ContactShadow;

    // Gentle studio vignette
    let Vignette = smoothstep(0.42, 0.92, length((Inside - vec2f(0.5, 0.46)) * vec2f(1.0, 1.15)));
    Stage *= 1.0 - 0.48 * Vignette;
    return Stage;
}

// 🎨 Reference X-Ray HUD overlay (App.tsx): dot matrix, sweeping scan line and cyan corner targeting brackets.
fn ApplyScanHudOverlay(Base: vec3f, Inside: vec2f) -> vec3f
{
    let HoloAmt = clamp(Overlay.Stage.y, 0.0, 1.0);
    if (HoloAmt < 0.01)
    {
        return Base;
    }
    var Result = Base;
    let Pixels = Inside * vec2f(640.0, 400.0);
    let Cyan   = vec3f(0.32, 0.80, 0.98);

    // Medical-scan dot matrix (24 px pitch, radially masked)
    let DotDist = length((fract(Pixels / 20.0) - 0.5) * 20.0);
    let Dot = (1.0 - smoothstep(0.8, 1.8, DotDist)) * (1.0 - smoothstep(0.18, 0.68, length(Inside - vec2f(0.5, 0.48))));
    Result += Cyan * (0.045 * Dot * HoloAmt);

    // Sweeping horizontal scan line in X-Ray Scan mode (View == 3)
    if (Overlay.Stage.x > 2.5)
    {
        let SweepY = 0.10 + 0.80 * (0.5 - 0.5 * cos(Overlay.Stage.w * 0.90));
        let Line   = exp(-pow((Inside.y - SweepY) * 130.0, 2.0));
        let Span   = smoothstep(0.03, 0.26, Inside.x) * smoothstep(0.03, 0.26, 1.0 - Inside.x);
        Result    += Cyan * (0.28 * Line * Span * HoloAmt);
    }

    // Corner HUD targeting brackets (inset 20 px, 18 px arms, 1.8 px stroke)
    let Margin = vec2f(20.0, 18.0);
    let MirrorP = min(Pixels - Margin, vec2f(640.0, 400.0) - Margin - Pixels);
    let InArm = MirrorP.x >= 0.0 && MirrorP.y >= 0.0 && MirrorP.x <= 18.0 && MirrorP.y <= 18.0
             && (MirrorP.x <= 1.8 || MirrorP.y <= 1.8);
    if (InArm)
    {
        Result = mix(Result, Cyan * 0.65, 0.72 * HoloAmt);
    }
    return Result;
}

@fragment
fn OverlayFragment(Varying: OverlayVarying) -> @location(0) vec4f
{
    if (ClassifyMasked(Varying.BodyLocal))
    {
        discard;
    }
    let View = normalize(Frame.CameraPosition - Varying.WorldPosition);
    var Normal = normalize(Varying.Normal);
    if (dot(Normal, View) < 0.0)
    {
        Normal = -Normal;
    }
    let NdotV = clamp(dot(Normal, View), 0.0, 1.0);
    let Fresnel = 0.04 + 0.96 * pow(1.0 - NdotV, 5.0);
    let Reflected = reflect(-View, Normal);
    var Reflection = SampleSky(Reflected) * Fresnel;
    let Glint = pow(max(dot(Reflected, Frame.SunDirection), 0.0), 900.0) * 60.0;
    Reflection += Frame.SunIlluminance * (Glint * Fresnel);

    if (Varying.Kind == KindGlass)
    {
        if (VehicleState.Cabin.y < 0.5)
        {
            discard;
        }
        var GlassOpacity = 1.0;
        if (StudioPreviewPass == 1u)
        {
            let HoloAmt = clamp(Overlay.Stage.y, 0.0, 1.0);
            if (HoloAmt > 0.004)
            {
                let ViewMode = Overlay.Stage.x;
                GlassOpacity = 1.0 - HoloAmt * 0.85;
                if (ViewMode > 0.5 && ViewMode < 2.5)
                {
                    GlassOpacity = 1.0 - HoloAmt;
                }
                else if (ViewMode > 2.5)
                {
                    GlassOpacity = 1.0 - smoothstep(-0.9, -0.05, Varying.BodyLocal.x - Overlay.Stage.z) * HoloAmt;
                }
                if (GlassOpacity <= 0.03)
                {
                    discard;
                }
            }
        }
        let Transmission = Overlay.Glass.x * (1.0 - Fresnel);
        return vec4f(Reflection * GlassOpacity, (1.0 - Transmission) * GlassOpacity);
    }
    if (Varying.Kind == KindTablet)
    {
        var Picture = textureSampleLevel(TabletTexture, LinearSampler, Varying.Coordinates, 0.0).rgb;
        if (Overlay.Tablet.z > 0.5)
        {
            var Well = ResolveWellCoverage(Varying.Coordinates);
            if (Overlay.Tablet.w > 0.5)
            {
                let Span = max(Overlay.Preview.zw - Overlay.Preview.xy, vec2f(1.0e-5));
                let PanelExtent = vec2f(640.0, 400.0) / Span;
                let P = abs((Varying.Coordinates - vec2f(Overlay.Corner.w, Overlay.Glass.w)) * PanelExtent);
                let Q = P - vec2f(64.0, 0.0);
                let Distance = length(max(Q, vec2f(0.0))) + min(max(Q.x, Q.y), 0.0) - 22.0;
                Well *= smoothstep(-1.0, 1.0, Distance);
            }
            if (Well > 0.0)
            {
                let Inside = (Varying.Coordinates - Overlay.Preview.xy) / max(Overlay.Preview.zw - Overlay.Preview.xy, vec2f(1.0e-5));
                let CarSample = textureSampleLevel(PreviewTexture, LinearSampler, Inside, 0.0);
                let Backdrop = select(vec3f(0.0), ResolveStudioStageBackdrop(Inside), CarSample.a < 0.999);
                let Composed = ApplyScanHudOverlay(CarSample.rgb + Backdrop * (1.0 - CarSample.a), Inside);
                Picture = mix(Picture, Composed, Well);
            }
        }
        return vec4f(ScreenRadiance(Picture * Overlay.Tablet.y), 1.0);
    }
    if (Varying.Kind == KindCluster)
    {
        let Texel = textureSampleLevel(ClusterTexture, LinearSampler, Varying.Coordinates, 0.0);
        return vec4f(ScreenRadiance(Texel.rgb * Texel.a * Overlay.Tablet.y), 1.0);
    }
    if (Overlay.Glass.z < 0.5 && FeatureEnabled(Frame, FeatureRayTracing) && FeatureEnabled(Frame, FeatureReflection)
        && FeatureEnabled(Frame, FeatureRayTracedReflections))
    {
        discard;
    }
    var Mirrored = SampleSky(Reflected);
    if (Overlay.Glass.z > 0.5)
    {
        Mirrored = textureSampleLevel(MirrorTexture, LinearSampler, Varying.Coordinates, Varying.Layer, 0.0).rgb;
    }
    return vec4f(Mirrored * Overlay.Glass.y, 1.0);
}

// Invert the same Narkowicz ACES fit used by DisplayPresent.wgsl.
fn ScreenRadiance(LinearDisplay: vec3f) -> vec3f {
    let Y = clamp(LinearDisplay, vec3f(0.0), vec3f(0.99));
    let A = 2.51 - 2.43 * Y;
    let B = 0.03 - 0.59 * Y;
    let X = (-B + sqrt(B * B + 0.56 * A * Y)) / (2.0 * A);
    let Exposure = select(1.0, DisplayAdaptation[0], DisplayAdaptation[0] > 0.0);
    return X / Exposure;
}
