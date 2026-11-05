//============================================================================================================================================
//                                                           DIRECTRADIANCE.WGSL
//============================================================================================================================================
// 🧩 Direct sun radiance, tyre-fire and light-grid arrivals at every primary surface, cascaded shadow depth, and sky background.

//! include "FrameDescriptor.wgsl"
//! include "SurfaceReflectance.wgsl"
//! include "PackingCodec.wgsl"
//! include "SkyProjection.wgsl"
//! include "FireIllumination.wgsl"
//! include "DistanceTraversal.wgsl"
//! include "LightClusterSpace.wgsl"
//! include "RayTraversal.wgsl"

struct CascadeReceiver
{
    LightViewProjection : array<mat4x4f, 3>,
    Splits              : vec4f,   // [m]   xyz far distance of each cascade, w cascade count
    TexelWorld          : vec4f,   // [m]   world size of one shadow texel per cascade
}

@group(0) @binding(1) var<storage, read> Materials     : array<MaterialRecord>;
@group(0) @binding(2) var SurfaceAttributes            : texture_2d<f32>;
@group(0) @binding(3) var SurfaceDepth                 : texture_depth_2d;
@group(0) @binding(4) var ShadowDepth                  : texture_depth_2d_array;
@group(0) @binding(5) var ShadowSampler                : sampler_comparison;
@group(0) @binding(6) var<uniform> Receiver            : CascadeReceiver;
@group(0) @binding(7) var DirectSkyView                : texture_2d<f32>;
@group(0) @binding(8) var DirectSkySampler             : sampler;
@group(0) @binding(9) var DirectTarget                 : texture_storage_2d<rgba16float, write>;   // rgb radiance, w sun visibility
// 📝 SceneLights (group 3, binding 3) is declared once, by RayTraversal.wgsl: the same light records and cell masks serve this pass,
//    the ray-traced next-event estimation and the card relighting.
@group(0) @binding(10) var DirectIrradianceTarget      : texture_storage_2d<rgba16float, write>;   // rgb diffuse irradiance of fire and lights

// 📐 3×3 percentage-closer taps, each a hardware bilinear comparison, after a normal offset of 1.5 texels.
fn ResolveCascadeVisibility(Position: vec3f, Normal: vec3f) -> f32
{
    let ViewDistance = ResolveViewDepth(Position);
    let Count = u32(Receiver.Splits.w);
    var Cascade = 0u;
    while (Cascade + 1u < Count && ViewDistance > Receiver.Splits[Cascade])
    {
        Cascade++;
    }
    if (ViewDistance > Receiver.Splits[Count - 1u])
    {
        return 1.0;
    }
    let Offset = Position + Normal * (1.5 * Receiver.TexelWorld[Cascade]);
    let Clip = Receiver.LightViewProjection[Cascade] * vec4f(Offset, 1.0);
    let Ndc = Clip.xyz / Clip.w;
    let Coordinates = vec2f(Ndc.x * 0.5 + 0.5, 0.5 - Ndc.y * 0.5);
    if (any(Coordinates < vec2f(0.0)) || any(Coordinates > vec2f(1.0)) || Ndc.z > 1.0)
    {
        return 1.0;
    }
    let Texel = 1.0 / vec2f(textureDimensions(ShadowDepth).xy);
    var Lit = 0.0;
    for (var Y = -1; Y <= 1; Y++)
    {
        for (var X = -1; X <= 1; X++)
        {
            Lit += textureSampleCompareLevel(ShadowDepth, ShadowSampler, Coordinates + vec2f(f32(X), f32(Y)) * Texel, Cascade, Ndc.z);
        }
    }
    return Lit / 9.0;
}

fn ResolveBackgroundRadiance(Direction: vec3f) -> vec3f
{
    var Radiance = textureSampleLevel(DirectSkyView, DirectSkySampler, ProjectSkyCoordinates(Direction), 0.0).rgb * Frame.SkyScale
                 + Frame.NightSkyRadiance;
    // 📝 The solar disc (0.27° radius) is added only here, never to escaped rays, so it is not counted twice.
    let Cosine = dot(Direction, Frame.SunDirection);
    if (Cosine > 0.99998)
    {
        Radiance += Frame.SunIlluminance * 2500.0;
    }
    return Radiance;
}

@compute @workgroup_size(8, 8)
fn IntegrateDirectRadiance(@builtin(global_invocation_id) Invocation: vec3u)
{
    let Pixel = Invocation.xy;
    if (any(vec2f(Pixel) >= Frame.ScreenExtent))
    {
        return;
    }
    let Depth = textureLoad(SurfaceDepth, vec2i(Pixel), 0);
    let Centre = vec2f(Pixel) + 0.5;
    if (Depth <= 0.0)
    {
        let Far = ReconstructWorldPosition(Centre, 1.0e-6);
        textureStore(DirectTarget, vec2i(Pixel), vec4f(ResolveBackgroundRadiance(normalize(Far - Frame.CameraPosition)), 0.0));
        textureStore(DirectIrradianceTarget, vec2i(Pixel), vec4f(0.0));
        return;
    }
    let Position     = ReconstructWorldPosition(Centre, Depth);
    let Attributes   = textureLoad(SurfaceAttributes, vec2i(Pixel), 0);
    let Normal       = DecodeOctahedral(Attributes.xy);
    let Surface      = ProjectSurfaceSample(Materials[u32(Attributes.z + 0.5)]);
    let View         = normalize(Frame.CameraPosition - Position);
    let UseRtShadows = FeatureEnabled(Frame, FeatureRayTracing) && FeatureEnabled(Frame, FeatureRayTracedShadows);
    let RayOrigin    = Position + Normal * RayEpsilon;

    var Visibility = 1.0;
    var SunDir     = Frame.SunDirection;
    if (FeatureEnabled(Frame, FeatureDirectShadow))
    {
        if (UseRtShadows)
        {
            SunDir     = SampleSunConeDirection(Frame.SunDirection, ProjectTemporalPair(Pixel, Frame.FrameIndex, 17u));
            Visibility = TraceSceneVisibility(RayOrigin, SunDir, RayEpsilon, 400.0);
        }
        else
        {
            Visibility = ResolveCascadeVisibility(Position, Normal);
        }
    }
    var Radiance = Frame.SunIlluminance * Visibility * ResolveDirectReflectance(Surface, Normal, View, SunDir);

    // 📝 Irradiance here is diffuse (cosine-weighted, unscaled by the albedo): the display's screen exitance and the cards
    //    both need it, and the radiance above carries only the specular-inclusive response for direct shading.
    var Irradiance = vec3f(0.0);
    let Fire = ResolveFireArrival(Position, Normal);
    if (any(Fire.Illuminance > vec3f(0.0)))
    {
        let FireCosine = max(dot(Normal, Fire.Direction), 0.0);
        Radiance += Fire.Illuminance * ResolveDirectReflectance(Surface, Normal, View, Fire.Direction);
        Irradiance += Fire.Illuminance * FireCosine;
    }

    // 📝 Only the lights whose reach touches this point's grid cell are evaluated (LightClusterSpace.wgsl). Their shadow is the exact
    //    BVH visibility when the BVH renderer is active and the distance-field soft shadow otherwise.
    var Candidates = ResolveLightCandidates(Position);
    while (Candidates != 0u)
    {
        let Index = firstTrailingBit(Candidates);
        Candidates &= Candidates - 1u;
        let Light = SceneLights.Lights[Index];
        let Illuminance = EvaluateLightArrival(Light, Position, Normal);
        if (any(Illuminance > vec3f(0.0)))
        {
            let Offset      = Light.PositionType.xyz - Position;
            let LightDist   = length(Offset);
            let TowardLight = Offset / max(LightDist, 1.0e-4);
            var Shade       = 1.0;
            if (UseRtShadows)
            {
                Shade = TraceSceneVisibility(RayOrigin, TowardLight, RayEpsilon, max(LightDist - 0.08, RayEpsilon));
            }
            else if (FeatureEnabled(Frame, FeatureDirectShadow))
            {
                let Bias = ResolveSurfaceBias(Position);
                Shade = TraverseSceneShadow(Position + Normal * Bias, TowardLight, 0.5 * Bias, max(LightDist - 0.5, 0.0), 4.0);
            }
            let Lit = Illuminance * Shade;
            Irradiance += Lit;
            Radiance += Lit * ResolveDirectReflectance(Surface, Normal, View, TowardLight);
        }
    }

    // 📝 The light technique of the emissive partition at this primary vertex (BVH mode only; see EmissiveLightSpace.js and RayTraversal.wgsl). The ReSTIR candidates that start here are
    //    cosine sampled and see diffuse transport only, so this adds the emitters' DIFFUSE share: albedo/π × irradiance, with the BSDF technique's w_b taken off (the candidate's
    //    first hit carries it). Specular reflections of emitters stay with the reflection pass, which counts them in full.
    let EmissiveWords = ResolveEmissiveCell(Position);
    if (EmissiveUsable(EmissiveWords))
    {
        var EmissiveIrradiance = vec3f(0.0);
        for (var Sample = 0u; Sample < EmissiveSamplesPerVertex; Sample += 1u)
        {
            let Draw    = vec4f(ProjectTemporalPair(Pixel, Frame.FrameIndex, 21u + 4u * Sample), ProjectTemporalPair(Pixel, Frame.FrameIndex, 23u + 4u * Sample));
            let Emitter = SampleEmissiveLight(Position, EmissiveWords, Draw);
            if (!Emitter.Valid)
            {
                continue;
            }
            let Toward     = Emitter.Position - Position;
            let Dist       = length(Toward);
            let LDir       = Toward / max(Dist, 1.0e-4);
            let CosSurface = dot(Normal, LDir);
            if (CosSurface <= 1.0e-4)
            {
                continue;
            }
            let CosEmitter = abs(dot(Emitter.Normal, LDir));
            let BsdfWeight = EvaluateEmissiveBsdfWeight(CosSurface * InversePi, Dist, CosEmitter);
            let Visible    = TraceSceneVisibility(RayOrigin, LDir, RayEpsilon, max(Dist - 0.08, RayEpsilon));
            EmissiveIrradiance += Emitter.Radiance * (CosEmitter * CosSurface * (1.0 - BsdfWeight) * Visible
                                                      / (max(Dist * Dist, 1.0e-4) * max(Emitter.Pdf, 1.0e-8) * f32(EmissiveSamplesPerVertex)));
        }
        Irradiance += EmissiveIrradiance;
        Radiance   += ResolveDiffuseAlbedo(Surface) * InversePi * EmissiveIrradiance;
    }

    textureStore(DirectTarget, vec2i(Pixel), vec4f(Radiance, Visibility));
    textureStore(DirectIrradianceTarget, vec2i(Pixel), vec4f(Irradiance, 1.0));
}
