//============================================================================================================================================
//                                                              SMOKEMARCH.WGSL
//============================================================================================================================================
// 🧩 The smoke seen from the camera: a march at reduced resolution, clipped to the opaque depth and to the occupied bricks.

// 📝 Per march pixel (one per MarchScale² screen pixels): the ray is clipped to the box of non-empty bricks the last
//    tick reported and to the opaque surface behind it; inside, any brick whose flag says empty is crossed in one
//    jump. A sample reads the smoke texture, the light volume — one filtered tap for sun visibility, sky, bounce and
//    fire together — and one tap of a four-channel tiled noise: three octaves (value and cellular) folded around ½
//    that erode the 25 cm cells into wisps at the edges while a dense core keeps its mass, drifting with the wind and
//    the rise of the plume (the sibling Plume's look: finer, less cloudy than the warped billows this used to draw).
//    Sun light is scattered in three octaves (Wrenninge 2013 — each one weaker, less shadowed and less forward), the
//    cheap stand-in for the multiple scattering that makes dense smoke bright and soft rather than grey. Flames emit
//    blackbody light between their tip and core temperatures (Helland), with the Stefan–Boltzmann T⁴ brightness.
//    Integration is energy-conserving (Hillaire 2015). The start offset is a fixed per-pixel interleaved-gradient
//    value, so a paused frame does not shimmer; the composite pass upsamples with the depth recorded here.

//! include "FrameDescriptor.wgsl"
//! include "SmokeRecords.wgsl"

@group(0) @binding(1) var<uniform> View                : SmokeView;
@group(0) @binding(2) var FineLatest                   : texture_3d<f32>;
@group(0) @binding(3) var LightVolume                  : texture_3d<f32>;
@group(0) @binding(4) var DetailNoise                  : texture_3d<f32>;
@group(0) @binding(5) var RepeatSampler                : sampler;
@group(0) @binding(6) var<storage, read> BrickFlags    : array<u32>;
@group(0) @binding(7) var<storage, read> Summary       : array<u32>;
@group(0) @binding(8) var SurfaceDepth                 : texture_depth_2d;
@group(0) @binding(9) var MarchTarget                  : texture_storage_2d<rgba16float, write>;
@group(0) @binding(10) var MarchDepthTarget            : texture_storage_2d<r32float, write>;

fn ClassifyBrickOccupied(WorldBrick: vec3i) -> bool
{
    let Local = WorldBrick - ResolveWorldBrick(View.FineOrigin);
    if (any(Local < vec3i(0)) || any(Local >= vec3i(View.BrickExtent)))
    {
        return false;
    }
    return BrickFlags[View.Parity * View.BrickCount + ResolveSmokeBrickSlot(WorldBrick, View.BrickExtent)] != 0u;
}

// 📐 Ray against an axis-aligned box: (entry, exit) travel, entry > exit when missed.
fn IntersectBox(Origin: vec3f, Inverse: vec3f, Minimum: vec3f, Maximum: vec3f) -> vec2f
{
    let Near = (Minimum - Origin) * Inverse;
    let Far = (Maximum - Origin) * Inverse;
    let Low = min(Near, Far);
    let High = max(Near, Far);
    return vec2f(max(max(Low.x, Low.y), Low.z), min(min(High.x, High.y), High.z));
}

// 📐 Interleaved gradient noise (Jimenez 2014): a stable, well-distributed per-pixel offset.
fn ResolveInterleavedGradient(Pixel: vec2f) -> f32
{
    return fract(52.9829189 * fract(dot(Pixel, vec2f(0.06711056, 0.00583715))));
}

@compute @workgroup_size(8, 8)
fn MarchSmoke(@builtin(global_invocation_id) Invocation: vec3u)
{
    let Pixel = Invocation.xy;
    if (any(vec2f(Pixel) >= View.MarchExtent))
    {
        return;
    }
    let ScreenPixel = min(vec2i(Pixel * View.MarchScale + vec2u(View.MarchScale / 2u)), vec2i(Frame.ScreenExtent) - vec2i(1));
    let Centre = vec2f(ScreenPixel) + 0.5;
    let Depth = textureLoad(SurfaceDepth, ScreenPixel, 0);
    let Direction = normalize(ReconstructWorldPosition(Centre, 1.0e-6) - Frame.CameraPosition);
    var Reach = 1.0e4;
    var ViewDepth = 1.0e4;
    if (Depth > 0.0)
    {
        let Surface = ReconstructWorldPosition(Centre, Depth);
        Reach = length(Surface - Frame.CameraPosition);
        ViewDepth = ResolveViewDepth(Surface);
    }
    textureStore(MarchDepthTarget, vec2i(Pixel), vec4f(ViewDepth, 0.0, 0.0, 0.0));
    if (View.Enabled == 0u || Summary[6] == 0u)
    {
        textureStore(MarchTarget, vec2i(Pixel), vec4f(0.0, 0.0, 0.0, 1.0));
        return;
    }

    // ① clip to the occupied bricks (grown by a cell) and to the opaque surface
    let BrickWorld = f32(SmokeBrickEdge) * View.FineCell;
    let Anchor = vec3f(SmokeAnchor) * View.FineCell;
    let Lowest = vec3f(vec3i(vec3u(Summary[0], Summary[1], Summary[2])) - vec3i(SmokeSummaryBias)) * BrickWorld + Anchor - vec3f(View.FineCell);
    let Highest = vec3f(vec3i(vec3u(Summary[3], Summary[4], Summary[5])) - vec3i(SmokeSummaryBias) + vec3i(1)) * BrickWorld + Anchor + vec3f(View.FineCell);
    let Inverse = 1.0 / select(Direction, vec3f(1.0e-6), abs(Direction) < vec3f(1.0e-6));
    let Span = IntersectBox(Frame.CameraPosition, Inverse, Lowest, Highest);
    let Enter = max(Span.x, 0.0);
    let Exit = min(Span.y, Reach);
    if (Exit <= Enter)
    {
        textureStore(MarchTarget, vec2i(Pixel), vec4f(0.0, 0.0, 0.0, 1.0));
        return;
    }

    // ② march
    let StepLength = View.StepLength;
    let InverseFineSpan = 1.0 / (vec3f(View.FineExtent) * View.FineCell);
    let InverseCoarseSpan = 1.0 / (vec3f(View.FineExtent / 2u) * View.CoarseCell);
    let Cosine = dot(Direction, Frame.SunDirection);
    let Phases = vec3f(ResolveSmokePhase(Cosine, View.PhaseForward), ResolveSmokePhase(Cosine, 0.5 * View.PhaseForward),
                       ResolveSmokePhase(Cosine, 0.25 * View.PhaseForward));
    // 📝 The detail drifts downwind and rises with the plume (sampled at the position less the drift, the pattern moves
    //    with the air); the flame flickers on the noise's fourth channel and a 23 Hz beat.
    let Drift = vec3f(-View.Wind.x * 0.35, -View.Wind.y * 0.35, -View.Rise) * View.Time;
    let WarpScale = 0.37 * View.DetailScale;
    let WarpReach = View.Warp / max(View.DetailScale, 1.0e-3);
    let Beat = 0.5 + 0.5 * sin(View.Time * 23.0);
    var Travel = Enter + StepLength * ResolveInterleavedGradient(vec2f(Pixel));
    var Radiance = vec3f(0.0);
    var Transmittance = 1.0;
    for (var Step = 0u; Step < View.MarchSteps; Step++)
    {
        if (Travel >= Exit || Transmittance < 0.01)
        {
            break;
        }
        let Position = Frame.CameraPosition + Direction * Travel;
        let WorldBrick = ResolveWorldBrick(vec3i(floor(Position / View.FineCell)));
        if (!ClassifyBrickOccupied(WorldBrick))
        {
            // 📝 Empty brick: jump to its far face in one step.
            let BrickMinimum = vec3f(WorldBrick) * BrickWorld + Anchor;
            let Leave = IntersectBox(Frame.CameraPosition, Inverse, BrickMinimum, BrickMinimum + vec3f(BrickWorld)).y;
            Travel = max(Leave, Travel) + 0.5 * StepLength;
            continue;
        }
        var Thermo = textureSampleLevel(FineLatest, RepeatSampler, Position * InverseFineSpan, 0.0);
        if (Thermo.x > 1.0e-4 || Thermo.z > 1.0e-4)
        {
            // ① sub-cell detail: three octaves (billowy value + cellular noise) folded 0.5 / 0.3 / 0.2 around ½, eroding
            //    the trilinear blobs of the lattice — thin edges turn to wisps, a dense core keeps its mass. Warp (off by
            //    default) bends the lookup for the older, rounder billows.
            var Lookup = Position + Drift;
            if (View.Warp > 0.0)
            {
                Lookup += (textureSampleLevel(DetailNoise, RepeatSampler, Position * WarpScale + 0.6 * Drift, 0.0).yzw - vec3f(0.5)) * WarpReach;
            }
            let Noise = textureSampleLevel(DetailNoise, RepeatSampler, Lookup * View.DetailScale, 0.0);
            let Folded = dot(Noise.rgb, vec3f(0.5, 0.3, 0.2));
            let Strength = View.DetailStrength;
            let Thin = 1.0 - clamp(Thermo.x * 0.8, 0.0, 1.0);
            let Density = max(0.0, Thermo.x * (1.0 + Strength * (Folded - 0.5) * 1.6)
                                 - Strength * View.Erosion * 0.35 * Thin * (1.0 - Folded) * min(Thermo.x, 1.0));
            let Extinction = Density * View.Extinction;
            let Light = textureSampleLevel(LightVolume, RepeatSampler, Position * InverseCoarseSpan, 0.0);
            let Albedo = mix(View.SmokeAlbedo, View.SootAlbedo, clamp(Thermo.w / max(Thermo.x, 1.0e-4), 0.0, 1.0));
            let Grey = dot(Albedo, vec3f(0.3333));

            // ② sun in three octaves (Wrenninge): each further octave is light that scattered again, so it is weighted by
            //    the albedo once more — white rubber smoke glows through, soot keeps only its single bounce
            let Visible = max(Light.w, 0.0);
            let Octave = Phases.x * Visible + View.MultiScatter * Grey * (0.6 * Phases.y * pow(Visible, 0.25) + 0.36 * Phases.z * pow(Visible, 0.0625));
            let Powder = mix(1.0, 1.0 - exp(-2.0 * Extinction), View.Powder);
            let Scattered = Albedo * (Frame.SunIlluminance * View.SunScale * Octave * Powder + Light.rgb * View.AmbientScale) * Extinction;

            // ③ flame: blackbody between the tip and the core temperature, set by the cell's heat; Stefan–Boltzmann
            //    brightness referred to 1500 K; tongues and flicker from the noise
            let Heat = Thermo.y / (Thermo.y + View.HeatHalf);
            let Kelvin = mix(View.FlameTip, View.FlameCore, Heat) * View.FlameTemperature;
            let Ratio = Kelvin / 1500.0;
            let Flicker = mix(1.0, 0.45 + 1.1 * mix(Noise.a, Beat, 0.25), View.FlameFlicker);
            let Flame = ResolveBlackbodyColour(Kelvin) * (Ratio * Ratio * Ratio * Ratio) * min(Thermo.z, 1.5) * Flicker * View.FlameScale * Frame.EmissionScale;
            let Absorbed = 1.0 - exp(-Extinction * StepLength);
            let Source = Scattered + Flame;
            Radiance += Transmittance * select(Source * StepLength, Source * Absorbed / max(Extinction, 1.0e-5), Extinction > 1.0e-4);
            Transmittance *= 1.0 - Absorbed;
        }
        Travel += StepLength;
    }
    textureStore(MarchTarget, vec2i(Pixel), vec4f(Radiance, Transmittance));
}
