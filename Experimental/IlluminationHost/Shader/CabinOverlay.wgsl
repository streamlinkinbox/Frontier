//============================================================================================================================================
//                                                            CABINOVERLAY.WGSL
//============================================================================================================================================
// 🧩 What the deferred raster cannot carry: see-through glazing, the HMI's two screens and the mirrors' pictures, drawn over it.

// 📝 Drawn into the recombined radiance before the temporal antialiasing, depth-tested (read only) against the opaque
//    surface, premultiplied over what is there:
//
//    | Kind    | Geometry                                  | What it adds                                             |
//    |---------|-------------------------------------------|----------------------------------------------------------|
//    | glass   | the body's glazing facets                 | the sky's Fresnel reflection and a sun glint; lets 70 %   |
//    |         |                                           | of the cabin (or the world, from inside) through          |
//    | tablet  | the centre-stack screen, 1.5 mm proud      | the TabletPanel launcher (its canvas), with the Garage's  |
//    |         |                                           | live car composited into its preview well                |
//    | cluster | the binnacle's hexagon                     | the TabletPanel cluster (its canvas, alpha to the outline)|
//    | mirror  | each mirror's glass                        | the reflected view ForwardRaster rendered for it          |
//
//    The screens are self-lit (Luminance, in the scene's radiance units where sunlit white is about 5 — DriveCabin-
//    Layout's 3 and 3.5), so they read the same in a dark cabin and at noon; their cover glass still reflects the sky.
//    The derez mask applies here too: glass, screens and mirrors come apart with the car.

//! include "FrameDescriptor.wgsl"
//! include "SkyProjection.wgsl"
//! include "EffectRecords.wgsl"
//! include "TyreCarcass.wgsl"

struct OverlayRecord
{
    Tablet            : vec4f,   // x luminance, y brightness [0, 1], z garage preview shown (1), w unused
    Preview           : vec4f,   // xy top-left, zw bottom-right of the preview well in tablet UV
    Corner            : vec4f,   // xy the well's corner radius in UV, z cluster luminance, w unused
    Glass             : vec4f,   // x transmission at normal incidence, y mirror reflectance, z mirrors live (1), w unused
    PaintRippleCenter : vec4f,   // xyz [m] body-local origin, w radius [m]
    PaintRippleColor  : vec4f,   // rgb previous coat albedo, w active (0 or 1)
    RimRippleCenter   : vec4f,   // xyz [m] body-local origin, w radius [m]
    RimRippleColor    : vec4f,   // rgb previous rim albedo, w active (0 or 1)
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
    // 📝 The two screens are cockpit fixtures: they scale and shift with it (the glazing and the mirrors stay with the
    //    shell). Screens and mirrors sit 1.5 mm proud of their cover facets so they win the depth test against them.
    let Placed = select(Position, PlaceCockpit(Position), Kind == KindTablet || Kind == KindCluster);
    let Lifted = Placed + Normal * select(0.0015, 0.0, Kind == KindGlass);
    let World = Body.LocalToWorld * vec4f(Lifted, 1.0);
    var Varying: OverlayVarying;
    Varying.Position = Frame.ViewProjection * World;
    Varying.Normal = (Body.LocalToWorld * vec4f(Normal, 0.0)).xyz;
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
    // 📝 A sun glint on the glass: a narrow lobe of the sun's illuminance.
    let Glint = pow(max(dot(Reflected, Frame.SunDirection), 0.0), 900.0) * 60.0;
    Reflection += Frame.SunIlluminance * (Glint * Fresnel);

    if (Varying.Kind == KindGlass)
    {
        if (VehicleState.Cabin.y < 0.5)
        {
            discard;
        }
        let Transmission = Overlay.Glass.x * (1.0 - Fresnel);
        return vec4f(Reflection, 1.0 - Transmission);
    }
    if (Varying.Kind == KindTablet)
    {
        var Picture = textureSampleLevel(TabletTexture, LinearSampler, Varying.Coordinates, 0.0).rgb;
        if (Overlay.Tablet.z > 0.5)
        {
            let Well = ResolveWellCoverage(Varying.Coordinates);
            if (Well > 0.0)
            {
                let Inside = (Varying.Coordinates - Overlay.Preview.xy) / max(Overlay.Preview.zw - Overlay.Preview.xy, vec2f(1.0e-5));
                let PreviewRgb = textureSampleLevel(PreviewTexture, LinearSampler, Inside, 0.0).rgb;
                // 📝 Any 2D HUD callout cards, drop prompts or drag ghosts drawn on the tablet canvas over the #080b10
                //    well background composite cleanly over the 3D studio preview.
                let KeyDiff = length(Picture - vec3f(0.0024, 0.0033, 0.0048));
                let UiMask  = smoothstep(0.003, 0.018, KeyDiff);
                Picture = mix(Picture, mix(PreviewRgb, Picture, UiMask), Well);
            }
        }
        let Emitted = Picture * Overlay.Tablet.x * Overlay.Tablet.y * Frame.EmissionScale;
        return vec4f(Emitted * (1.0 - Fresnel) + Reflection, 1.0);
    }
    if (Varying.Kind == KindCluster)
    {
        let Texel = textureSampleLevel(ClusterTexture, LinearSampler, Varying.Coordinates, 0.0);
        let Emitted = Texel.rgb * Texel.a * Overlay.Corner.z * Overlay.Tablet.y * Frame.EmissionScale;
        return vec4f(Emitted * (1.0 - Fresnel) + Reflection, 1.0);
    }
    // 📝 Mirror: the reflected view, or the sky alone until a mirror has been rendered.
    var Mirrored = SampleSky(Reflected);
    if (Overlay.Glass.z > 0.5)
    {
        Mirrored = textureSampleLevel(MirrorTexture, LinearSampler, Varying.Coordinates, Varying.Layer, 0.0).rgb;
    }
    return vec4f(Mirrored * Overlay.Glass.y, 1.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                      VEHICLE HOLOGRAM / WIREFRAME / RIPPLE OVERLAY
//------------------------------------------------------------------------------------------------------------------------

struct VehicleHoloVarying
{
    @builtin(position) Position            : vec4f,
    @location(0) Normal                    : vec3f,
    @location(1) @interpolate(flat) Family : u32,
    @location(2) WorldPosition             : vec3f,
    @location(3) BodyLocal                 : vec3f,
    @location(4) Barycentric               : vec3f,
}

@vertex
fn VehicleHoloVertex(@location(0) Position: vec3f, @location(1) Family: u32, @location(2) Normal: vec3f,
                     @location(3) OccupantOffset: u32, @builtin(vertex_index) VertexIndex: u32,
                     @builtin(instance_index) OccupantIndex: u32) -> VehicleHoloVarying
{
    let Index = OccupantIndex + OccupantOffset;
    let Occupant = Occupants[Index];
    var Moved: DeformedVertex;
    Moved.Position = Position;
    Moved.Normal = Normal;
    if (Index >= 1u && Index < 5u)
    {
        Moved = DeformTyre(Index - 1u, Position, Normal);
    }
    else if (Index == 0u)
    {
        Moved = DeformBodyVertex(Family, Position, Normal);
    }
    let WorldNormal = normalize((Occupant.LocalToWorld * vec4f(Moved.Normal, 0.0)).xyz);
    let World = Occupant.LocalToWorld * vec4f(Moved.Position, 1.0);
    let LiftedWorld = vec4f(World.xyz + WorldNormal * 0.0015, 1.0);
    let Corner = VertexIndex % 3u;
    var Varying: VehicleHoloVarying;
    Varying.Position      = Frame.ViewProjection * LiftedWorld;
    Varying.Normal        = WorldNormal;
    Varying.Family        = select(Family, Occupant.MaterialOverride, Occupant.MaterialOverride != NoMaterialOverride);
    Varying.WorldPosition = World.xyz;
    Varying.BodyLocal     = (Occupants[0].WorldToLocal * World).xyz;
    Varying.Barycentric   = select(select(vec3f(0.0, 0.0, 1.0), vec3f(0.0, 1.0, 0.0), Corner == 1u), vec3f(1.0, 0.0, 0.0), Corner == 0u);
    return Varying;
}

@fragment
fn VehicleHoloFragment(Varying: VehicleHoloVarying) -> @location(0) vec4f
{
    let BarycentricWidth = fwidth(Varying.Barycentric);
    let EdgeStep = smoothstep(vec3f(0.0), BarycentricWidth * 1.35, Varying.Barycentric);
    let WireEdge = 1.0 - min(EdgeStep.x, min(EdgeStep.y, EdgeStep.z));

    if (ClassifyMasked(Varying.BodyLocal))
    {
        discard;
    }

    let ViewDirection = normalize(Frame.CameraPosition - Varying.WorldPosition);
    var Normal = normalize(Varying.Normal);
    if (dot(Normal, ViewDirection) < 0.0)
    {
        Normal = -Normal;
    }

    var OutColor = vec3f(0.0);
    var OutAlpha = 0.0;

    // 📝 Expanding 3D spherical material ripple wave on the real vehicle in the proving ground.
    if (Varying.Family == 6u && Overlay.PaintRippleColor.w > 0.5)
    {
        let Dist = length(Varying.BodyLocal - Overlay.PaintRippleCenter.xyz);
        let Radius = Overlay.PaintRippleCenter.w;
        let KeepOld = smoothstep(Radius - 0.16, Radius + 0.16, Dist);
        let WaveRing = exp(-pow((Dist - Radius) * 9.0, 2.0));
        let Fresnel = 0.04 + 0.96 * pow(1.0 - clamp(dot(Normal, ViewDirection), 0.0, 1.0), 5.0);
        let OldShade = Overlay.PaintRippleColor.rgb * (Frame.SunIlluminance * max(dot(Normal, Frame.SunDirection), 0.08) * 0.25 + vec3f(0.18));
        OutColor += OldShade * KeepOld * 0.72 + vec3f(0.28, 0.78, 1.0) * WaveRing * (1.8 + Fresnel * 1.2) * Frame.EmissionScale;
        OutAlpha = max(OutAlpha, clamp(KeepOld * 0.72 + WaveRing * 0.85, 0.0, 0.92));
    }
    else if (Varying.Family == 10u && Overlay.RimRippleColor.w > 0.5)
    {
        let Dist = length(Varying.BodyLocal - Overlay.RimRippleCenter.xyz);
        let Radius = Overlay.RimRippleCenter.w;
        let KeepOld = smoothstep(Radius - 0.12, Radius + 0.12, Dist);
        let WaveRing = exp(-pow((Dist - Radius) * 11.0, 2.0));
        let OldShade = Overlay.RimRippleColor.rgb * (Frame.SunIlluminance * max(dot(Normal, Frame.SunDirection), 0.10) * 0.25 + vec3f(0.18));
        OutColor += OldShade * KeepOld * 0.72 + vec3f(0.35, 0.82, 1.0) * WaveRing * 2.0 * Frame.EmissionScale;
        OutAlpha = max(OutAlpha, clamp(KeepOld * 0.72 + WaveRing * 0.85, 0.0, 0.92));
    }

    // 📝 X-Ray Scan / Ghost-Wireframe view on the real vehicle.
    let HoloAmount = VehicleState.Axis.w;
    if (HoloAmount > 0.001)
    {
        let ScanX = VehicleState.Column.w;
        let ScanD = Varying.BodyLocal.x - ScanX;
        let ScanRegion = smoothstep(-0.90, -0.05, ScanD) * HoloAmount;
        if (ScanRegion > 0.002)
        {
            let ExteriorShell = Varying.Family == 6u || Varying.Family == 8u || Varying.Family == TyreFamily || Varying.Family == 12u;
            let Fresnel    = pow(1.0 - abs(dot(Normal, ViewDirection)), 2.3);
            let ScanLines  = smoothstep(0.72, 1.0, sin(Varying.BodyLocal.z * 85.0) * 0.5 + 0.5);
            let WaveEdge   = exp(-pow((ScanD + 0.18) * 4.2, 2.0));
            let Wire       = WireEdge * VehicleState.CabinShift.w;
            let HoloBase   = select(vec3f(0.56, 0.85, 0.95), vec3f(0.22, 0.74, 0.98), !ExteriorShell);
            let HoloAccent = vec3f(0.22, 0.74, 0.98);
            let RadianceScale = max(Frame.EmissionScale * 1.35, 0.65);
            let HoloColor  = (HoloBase * (0.28 + Fresnel * 0.85 + ScanLines * 0.28)
                           + HoloAccent * (WaveEdge * 1.65 + Wire * 0.98)) * RadianceScale;
            let HoloAlpha  = clamp((0.12 + Fresnel * 0.36 + ScanLines * 0.08 + WaveEdge * 0.58 + Wire * 0.62) * ScanRegion, 0.0, 0.90);
            OutColor += HoloColor * HoloAlpha;
            OutAlpha = clamp(OutAlpha + HoloAlpha * (1.0 - OutAlpha), 0.0, 0.94);
        }
    }

    if (OutAlpha <= 0.001)
    {
        discard;
    }
    return vec4f(OutColor, OutAlpha);
}
