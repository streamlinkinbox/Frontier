//============================================================================================================================================
//                                                            FORWARDRASTER.WGSL
//============================================================================================================================================
// 🧩 A small lit forward pass over the scene's facets: the mirrors' reflected views and the HMI Garage's studio portrait of the car.

// 📝 The main view is deferred and globally illuminated — far too much for three side views and a 640 × 400 preview.
//    This pass draws the same vertex records (the same instanced runs, the same tyre deformation, steering wheel and
//    derez mask) with direct light only:
//
//    | Mode   | Light                                                                                              |
//    |--------|----------------------------------------------------------------------------------------------------|
//    | scene  | the sun through the cascaded shadow depth, the sky view averaged over the normal's hemisphere, a    |
//    |        | sun-lit ground bounce from below, authored emission — what a mirror shows of the grounds           |
//    | studio | a key, a fill and a rim light fixed to the camera, a dim ambient, a soft floor glow — the car as a  |
//    |        | configurator shows it, its own camera and lighting (Docs/CarHmiReport.md)                           |
//
//    A mirror's view is the driver's eye reflected in the mirror's plane, through an off-axis frustum fitted to the
//    glass; its near plane is the mirror itself, so nothing behind the glass leaks into it.

//! include "FrameDescriptor.wgsl"
//! include "SurfaceReflectance.wgsl"
//! include "SkyProjection.wgsl"
//! include "EffectRecords.wgsl"
//! include "TyreCarcass.wgsl"
//! include "CabinVolume.wgsl"

struct ForwardView
{
    ViewProjection     : mat4x4f,
    Camera             : vec4f,   // xyz [m] eye, w mode: 0 scene, 1 studio
    Key                : vec4f,   // xyz studio key light direction (toward the light), w its intensity
    Fill               : vec4f,   // xyz studio fill direction, w intensity
    Rim                : vec4f,   // xyz studio rim direction, w intensity; scene: x hides the car (derezzed)
    ScanState          : vec4f,   // x: ScanX [m], y: HoloAmount [0..1], z: WireframeWeight [0..1], w: PassMode (0 normal, 1 solid/core, 2 holo shell)
    PaintRippleCenter  : vec4f,   // xyz [m] body-local origin, w radius [m]
    PaintRippleColor   : vec4f,   // rgb previous coat albedo, w active (0 or 1)
    PaintRippleSurface : vec4f,   // x: prev roughness, y: prev metalness, z: prev coatWeight, w: prev coatRoughness
    RimRippleCenter    : vec4f,   // xyz [m] body-local origin, w radius [m]
    RimRippleColor     : vec4f,   // rgb previous rim albedo, w active (0 or 1)
}

struct CascadeReceiver
{
    LightViewProjection : array<mat4x4f, 3>,
    Splits              : vec4f,
    TexelWorld          : vec4f,
}

@group(0) @binding(1) var<uniform> View                 : ForwardView;
@group(0) @binding(2) var<storage, read> Occupants      : array<OccupantRecord>;
@group(0) @binding(3) var<storage, read> Materials      : array<MaterialRecord>;
@group(0) @binding(4) var<storage, read> VehicleState   : VehicleRecord;
@group(0) @binding(5) var<uniform> Effect               : EffectRecord;
@group(0) @binding(6) var ShadowDepth                   : texture_depth_2d_array;
@group(0) @binding(7) var ShadowSampler                 : sampler_comparison;
@group(0) @binding(8) var<uniform> Receiver             : CascadeReceiver;
@group(0) @binding(9) var SkyView                       : texture_2d<f32>;
@group(0) @binding(10) var SkySampler                   : sampler;

struct ForwardVarying
{
    @builtin(position) Position             : vec4f,
    @location(0) Normal                     : vec3f,
    @location(1) @interpolate(flat) Family  : u32,
    @location(2) WorldPosition              : vec3f,
    @location(3) @interpolate(flat) Vehicle : u32,
    @location(4) BodyLocal                  : vec3f,
    @location(5) Barycentric                : vec3f,
}

@vertex
fn ForwardVertex(@location(0) Position: vec3f, @location(1) Family: u32, @location(2) Normal: vec3f,
                 @location(3) OccupantOffset: u32, @builtin(vertex_index) VertexIndex: u32,
                 @builtin(instance_index) OccupantIndex: u32) -> ForwardVarying
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
    let World = Occupant.LocalToWorld * vec4f(Moved.Position, 1.0);
    let Corner = VertexIndex % 3u;
    var Varying: ForwardVarying;
    Varying.Position = View.ViewProjection * World;
    Varying.Normal = (Occupant.LocalToWorld * vec4f(Moved.Normal, 0.0)).xyz;
    Varying.Family = select(Family, Occupant.MaterialOverride, Occupant.MaterialOverride != NoMaterialOverride);
    Varying.WorldPosition = World.xyz;
    Varying.Vehicle = select(0u, 1u, Index < 5u);
    Varying.BodyLocal = (Occupants[0].WorldToLocal * World).xyz;
    Varying.Barycentric = select(select(vec3f(0.0, 0.0, 1.0), vec3f(0.0, 1.0, 0.0), Corner == 1u), vec3f(1.0, 0.0, 0.0), Corner == 0u);
    return Varying;
}

fn ResolveSunVisibility(Position: vec3f, Normal: vec3f) -> f32
{
    let ViewDistance = ResolveViewDepth(Position);
    let Count = u32(Receiver.Splits.w);
    if (Count == 0u || ViewDistance < 0.0 || ViewDistance > Receiver.Splits[Count - 1u])
    {
        return 1.0;
    }
    var Cascade = 0u;
    while (Cascade + 1u < Count && ViewDistance > Receiver.Splits[Cascade])
    {
        Cascade++;
    }
    let Offset = Position + Normal * (1.5 * Receiver.TexelWorld[Cascade]);
    let Clip = Receiver.LightViewProjection[Cascade] * vec4f(Offset, 1.0);
    let Ndc = Clip.xyz / Clip.w;
    let Coordinates = vec2f(Ndc.x * 0.5 + 0.5, 0.5 - Ndc.y * 0.5);
    if (any(Coordinates < vec2f(0.0)) || any(Coordinates > vec2f(1.0)) || Ndc.z > 1.0)
    {
        return 1.0;
    }
    return textureSampleCompareLevel(ShadowDepth, ShadowSampler, Coordinates, Cascade, Ndc.z);
}

// 📝 The derez mask as the surface raster applies it: released cells are gone, hidden is all gone.
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

@fragment
fn ForwardFragment(Varying: ForwardVarying) -> @location(0) vec4f
{
    let BarycentricWidth = fwidth(Varying.Barycentric);
    let EdgeStep = smoothstep(vec3f(0.0), BarycentricWidth * 1.35, Varying.Barycentric);
    let WireEdge = 1.0 - min(EdgeStep.x, min(EdgeStep.y, EdgeStep.z));

    let ViewDirection = normalize(View.Camera.xyz - Varying.WorldPosition);
    var Normal = normalize(Varying.Normal);
    if (dot(Normal, ViewDirection) < 0.0)
    {
        Normal = -Normal;
    }

    if (Varying.Vehicle == 1u)
    {
        // 📝 A mirror never sees mirror glass (each mirror's own lies on its near plane); see-through glazing is gone.
        if ((Varying.Family == VehicleGlassFamily && VehicleState.Cabin.y > 0.5) || Varying.Family == MirrorGlassFamily)
        {
            discard;
        }
        if (ClassifyMasked(Varying.BodyLocal))
        {
            discard;
        }
    }

    let HoloAmount = View.ScanState.y;
    let ScanX      = View.ScanState.x;
    let ScanD      = Varying.BodyLocal.x - ScanX;
    let SolidCut   = smoothstep(0.05, -0.75, ScanD);
    let SolidAlpha = mix(1.0, SolidCut, HoloAmount);
    let ScanRegion = smoothstep(-0.90, -0.05, ScanD) * HoloAmount;
    let PassMode   = View.ScanState.w;
    let ExteriorShell = Varying.Family == 6u || Varying.Family == 8u || Varying.Family == TyreFamily || Varying.Family == 12u;

    if (Varying.Vehicle == 1u && HoloAmount > 0.001)
    {
        if (PassMode > 1.5)
        {
            // 📝 Pass 2 (studio preview): translucent holographic ghost + barycentric wireframe shell over the interior.
            if (ScanRegion <= 0.002)
            {
                discard;
            }
            let Fresnel    = pow(1.0 - abs(dot(Normal, ViewDirection)), 2.3);
            let ScanLines  = smoothstep(0.72, 1.0, sin(Varying.BodyLocal.z * 85.0) * 0.5 + 0.5);
            let WaveEdge   = exp(-pow((ScanD + 0.18) * 4.2, 2.0));
            let Wire       = WireEdge * View.ScanState.z;
            let HoloBase   = select(vec3f(0.56, 0.85, 0.95), vec3f(0.22, 0.74, 0.98), !ExteriorShell);
            let HoloAccent = vec3f(0.22, 0.74, 0.98);
            let Color      = HoloBase * (0.28 + Fresnel * 0.85 + ScanLines * 0.28)
                           + HoloAccent * (WaveEdge * 1.55 + Wire * 0.95);
            let Alpha      = clamp((0.12 + Fresnel * 0.36 + ScanLines * 0.08 + WaveEdge * 0.58 + Wire * 0.62) * ScanRegion, 0.0, 0.92);
            return vec4f(Color * Alpha, Alpha);
        }
        else if (PassMode > 0.5 && ExteriorShell && SolidAlpha < 0.5)
        {
            // 📝 Pass 1 (studio preview): peel away the opaque exterior shell behind the scan curtain so the internal
            //    cockpit, hubs and brake assemblies remain visible inside the ghosted car.
            discard;
        }
    }

    var Record = Materials[Varying.Family];
    var RippleGlow = vec3f(0.0);
    if (Varying.Vehicle == 1u)
    {
        if (Varying.Family == 6u && View.PaintRippleColor.w > 0.5)
        {
            let Dist = length(Varying.BodyLocal - View.PaintRippleCenter.xyz);
            let Radius = View.PaintRippleCenter.w;
            let KeepOld = smoothstep(Radius - 0.16, Radius + 0.16, Dist);
            Record.Albedo        = mix(Record.Albedo, View.PaintRippleColor.rgb, KeepOld);
            Record.Roughness     = mix(Record.Roughness, View.PaintRippleSurface.x, KeepOld);
            Record.Metalness     = mix(Record.Metalness, View.PaintRippleSurface.y, KeepOld);
            Record.CoatWeight    = mix(Record.CoatWeight, View.PaintRippleSurface.z, KeepOld);
            Record.CoatRoughness = mix(Record.CoatRoughness, View.PaintRippleSurface.w, KeepOld);
            let WaveRing = exp(-pow((Dist - Radius) * 9.0, 2.0));
            RippleGlow = vec3f(0.28, 0.78, 1.0) * WaveRing * 0.85;
        }
        else if (Varying.Family == 10u && View.RimRippleColor.w > 0.5)
        {
            let Dist = length(Varying.BodyLocal - View.RimRippleCenter.xyz);
            let Radius = View.RimRippleCenter.w;
            let KeepOld = smoothstep(Radius - 0.12, Radius + 0.12, Dist);
            Record.Albedo = mix(Record.Albedo, View.RimRippleColor.rgb, KeepOld);
            let WaveRing = exp(-pow((Dist - Radius) * 11.0, 2.0));
            RippleGlow = vec3f(0.35, 0.82, 1.0) * WaveRing * 0.90;
        }
    }

    let Surface = ProjectSurfaceSample(Record);
    let Albedo = ResolveDiffuseAlbedo(Surface);
    var Radiance = Surface.Emission * Frame.EmissionScale + RippleGlow;
    if (View.Camera.w > 0.5)
    {
        // 📝 Studio: three lights fixed to the camera, a dim cool ambient, a warm glow from the floor.
        Radiance += ResolveDirectReflectance(Surface, Normal, ViewDirection, normalize(View.Key.xyz)) * View.Key.w;
        Radiance += ResolveDirectReflectance(Surface, Normal, ViewDirection, normalize(View.Fill.xyz)) * View.Fill.w;
        Radiance += ResolveDirectReflectance(Surface, Normal, ViewDirection, normalize(View.Rim.xyz)) * View.Rim.w;
        Radiance += Albedo * (vec3f(0.10, 0.12, 0.14) * (0.6 + 0.4 * Normal.z) + vec3f(0.05, 0.045, 0.04) * max(-Normal.z, 0.0));
        let Reflected = reflect(-ViewDirection, Normal);
        Radiance += ResolveSpecularColour(Surface) * 0.08 * smoothstep(-0.2, 0.6, Reflected.z) * (1.0 - Surface.Roughness);
        if (Varying.Vehicle == 1u && HoloAmount > 0.001)
        {
            let WaveEdge = exp(-pow((ScanD + 0.18) * 4.2, 2.0)) * HoloAmount;
            Radiance += vec3f(0.22, 0.74, 0.98) * WaveEdge * 1.35;
            if (!ExteriorShell && ScanRegion > 0.01)
            {
                let CoreRim = pow(1.0 - abs(dot(Normal, ViewDirection)), 1.6);
                let CoreHolo = vec3f(0.12, 0.42, 0.64) * (0.35 + 0.65 * CoreRim) + vec3f(0.22, 0.74, 0.98) * WireEdge * 0.35;
                Radiance = mix(Radiance, Radiance * 0.55 + CoreHolo, ScanRegion * 0.82);
            }
        }
        // 📝 The preview is a picture on a screen (display-referred, 8-bit): a soft shoulder keeps the coat's highlights.
        return vec4f(Radiance * 1.5 / (vec3f(1.0) + Radiance * 1.5), 1.0);
    }
    // 📝 Scene: the sun with its cascaded shadow, the sky over the normal's hemisphere, the ground's bounce.
    let Visibility = ResolveSunVisibility(Varying.WorldPosition, Normal);
    Radiance += Frame.SunIlluminance * Visibility * ResolveDirectReflectance(Surface, Normal, ViewDirection, Frame.SunDirection);
    let SkyDirection = normalize(vec3f(Normal.xy, max(Normal.z, 0.0) + 0.35));
    let Sky = textureSampleLevel(SkyView, SkySampler, ProjectSkyCoordinates(SkyDirection), 0.0).rgb * Frame.SkyScale + Frame.NightSkyRadiance;
    let Bounce = Frame.SunIlluminance * (max(Frame.SunDirection.z, 0.0) * 0.18 * InversePi);
    let Enclosure = select(1.0, mix(1.0, 0.2, ResolveCabinShare(Varying.BodyLocal)), Varying.Vehicle == 1u);
    Radiance += Albedo * (Sky * (0.55 + 0.45 * Normal.z) + Bounce * (0.5 - 0.5 * Normal.z)) * Enclosure;
    let Reflected = reflect(-ViewDirection, Normal);
    let Glossy = textureSampleLevel(SkyView, SkySampler, ProjectSkyCoordinates(normalize(vec3f(Reflected.xy, max(Reflected.z, 0.02)))), 0.0).rgb * Frame.SkyScale;
    Radiance += ResolveSpecularColour(Surface) * Glossy * (1.0 - Surface.Roughness) * 0.6;
    return vec4f(Radiance, 1.0);
}
