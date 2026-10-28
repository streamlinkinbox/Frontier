//============================================================================================================================================
//                                                        DISPLAYRECOMBINATION.WGSL
//============================================================================================================================================
// 🧩 Recombines direct, indirect diffuse, glossy reflection and emission per pixel, or substitutes an inspection signal.

// 📝 The indirect term arrives demodulated (irradiance E) and is modulated here by the diffuse albedo, so texture-free
//    surfaces keep their edges through the wavelet. Specular indirect uses the split-sum response F0·A + B over the
//    traced reflection; past the reflection cutoff it falls back to E/π, Lumen's rough-reflection reuse. A clear coat
//    reflects the same radiance with its own response and attenuates the layer beneath by its Fresnel transmittance.

//! include "FrameDescriptor.wgsl"
//! include "SurfaceReflectance.wgsl"
//! include "PackingCodec.wgsl"
//! include "CabinVolume.wgsl"
//! include "FogMedium.wgsl"

@group(0) @binding(1) var<storage, read> Materials : array<MaterialRecord>;
@group(0) @binding(2) var SurfaceAttributes        : texture_2d<f32>;
@group(0) @binding(3) var SurfaceDepth             : texture_depth_2d;
@group(0) @binding(4) var DirectRadiance           : texture_2d<f32>;
@group(0) @binding(5) var IndirectIrradiance       : texture_2d<f32>;
@group(0) @binding(6) var ReflectionRadiance       : texture_2d<f32>;
@group(0) @binding(7) var ResampledIrradiance      : texture_2d<f32>;
@group(0) @binding(8) var InspectionRadiance       : texture_2d<f32>;
@group(0) @binding(9) var CardFinal                : texture_2d<f32>;
@group(0) @binding(10) var RecombinedTarget         : texture_storage_2d<rgba16float, write>;
@group(0) @binding(11) var<storage, read> Occupants : array<OccupantRecord>;   // [0]: the car body, for its cabin

@group(0) @binding(12) var TyrePlume : texture_2d<f32>;
@group(0) @binding(13) var PlumeSampler : sampler;
fn CompositePlume(Colour: vec3f, Pixel: vec2i) -> vec3f {
    if ((Frame.Features & 4096u) == 0u || Frame.InspectionMode != 0u) { return Colour; }
    let Sample = textureSampleLevel(TyrePlume, PlumeSampler, (vec2f(Pixel) + 0.5) * Frame.InverseScreenExtent, 0.0);
    return Colour * Sample.a + Sample.rgb;
}

// 📝 Of the traced indirect light the enclosed cabin keeps (CabinVolume.wgsl). Its surfaces lie inside the body's own
//    distance volume, so their rays "hit" at once and gather the sunlit exterior's cards — several times the open road's
//    irradiance. A twentieth of that is about what windows let in; the sun through the glass is direct and untouched.
const CabinIndirectShare : f32 = 0.05;

const InspectionFinal      : u32 = 0u;
const InspectionDirect     : u32 = 1u;
const InspectionDiffuse    : u32 = 2u;
const InspectionIrradiance : u32 = 3u;
const InspectionResampled  : u32 = 4u;
const InspectionReflection : u32 = 5u;
const InspectionDistance   : u32 = 6u;
const InspectionCards      : u32 = 7u;
const InspectionNormal     : u32 = 8u;
const InspectionAlbedo     : u32 = 9u;

@compute @workgroup_size(8, 8)
fn ResolveRecombination(@builtin(global_invocation_id) Invocation: vec3u)
{
    let Pixel = vec2i(Invocation.xy);
    if (any(vec2f(Pixel) >= Frame.ScreenExtent))
    {
        return;
    }
    let Direct = textureLoad(DirectRadiance, Pixel, 0);
    if (Frame.InspectionMode == InspectionDistance || Frame.InspectionMode >= 10u)
    {
        textureStore(RecombinedTarget, Pixel, vec4f(textureLoad(InspectionRadiance, Pixel, 0).rgb, 1.0));
        return;
    }
    if (Frame.InspectionMode == InspectionCards)
    {
        let Extent = vec2f(textureDimensions(CardFinal));
        let Scale = min(Frame.ScreenExtent.x, Frame.ScreenExtent.y) / Extent.x;
        let Texel = vec2i(vec2f(Pixel) / Scale);
        var Colour = vec3f(0.04);
        if (all(Texel < vec2i(Extent)))
        {
            Colour = textureLoad(CardFinal, Texel, 0).rgb;
        }
        textureStore(RecombinedTarget, Pixel, vec4f(Colour, 1.0));
        return;
    }
    let Depth = textureLoad(SurfaceDepth, Pixel, 0);
    if (Depth <= 0.0)
    {
        textureStore(RecombinedTarget, Pixel, vec4f(CompositePlume(Direct.rgb, Pixel), 1.0));
        return;
    }
    let Attributes = textureLoad(SurfaceAttributes, Pixel, 0);
    let Normal = DecodeOctahedral(Attributes.xy);
    let Surface = ProjectSurfaceSample(Materials[u32(Attributes.z + 0.5)]);
    let Position = ReconstructWorldPosition(vec2f(Pixel) + 0.5, Depth);
    let View = normalize(Frame.CameraPosition - Position);
    let NdotV = max(dot(Normal, View), 1.0e-3);

    // 📝 Inside the car's cabin: SDF volumes need 0.05 to counter self-hit, whereas RTX traces through real window openings.
    let TargetCabinShare = select(CabinIndirectShare, 0.14, FeatureEnabled(Frame, FeatureRayTracing));
    let Enclosure = mix(1.0, TargetCabinShare, ResolveCabinShare((Occupants[0].WorldToLocal * vec4f(Position, 1.0)).xyz));
    var Irradiance = vec3f(0.0);
    if (FeatureEnabled(Frame, FeatureIndirect))
    {
        Irradiance = textureLoad(IndirectIrradiance, Pixel, 0).rgb * Frame.IndirectScale * Enclosure;
    }
    let Diffuse = ResolveDiffuseAlbedo(Surface) * InversePi * Irradiance;

    let GlossRoughness   = select(Surface.Roughness, Surface.CoatRoughness, Surface.CoatWeight > 0.0);
    let Reflection       = textureLoad(ReflectionRadiance, Pixel, 0);
    var Glossy           = Irradiance * InversePi;
    let UseRtReflections = FeatureEnabled(Frame, FeatureRayTracing) && FeatureEnabled(Frame, FeatureRayTracedReflections);
    let Cutoff           = select(0.50, ResolveReflectionRoughness(Frame), UseRtReflections);
    let FadeStart        = max(0.05, Cutoff - 0.18);
    if (FeatureEnabled(Frame, FeatureReflection))
    {
        let ReflScale = select(1.0, Frame.IndirectScale, FeatureEnabled(Frame, FeatureIndirect)) * ResolveReflectionScale(Frame);
        Glossy = mix(Reflection.rgb * ReflScale * Enclosure, Glossy, smoothstep(FadeStart, Cutoff, GlossRoughness));
    }
    let LayerResponse = ResolveEnvironmentResponse(NdotV, Surface.Roughness);
    let LayerGlossy = select(Glossy, mix(Glossy, Irradiance * InversePi, smoothstep(FadeStart - 0.05, Cutoff, Surface.Roughness)), Surface.CoatWeight > 0.0);
    var Indirect = Diffuse + LayerGlossy * (ResolveSpecularColour(Surface) * LayerResponse.x + LayerResponse.y);
    var Specular = LayerGlossy * (ResolveSpecularColour(Surface) * LayerResponse.x + LayerResponse.y);
    if (Surface.CoatWeight > 0.0)
    {
        let CoatResponse = ResolveEnvironmentResponse(NdotV, Surface.CoatRoughness);
        let CoatReflectance = (0.04 * CoatResponse.x + CoatResponse.y) * Surface.CoatWeight;
        Indirect = Indirect * (1.0 - CoatReflectance) + Glossy * CoatReflectance;
        Specular = Specular * (1.0 - CoatReflectance) + Glossy * CoatReflectance;
    }
    // 📝 Attributes.w carries 1 + glow: a derezzing car's cells and cubes glow in their own facet's colour.
    let Emission = (Surface.Emission + Surface.Albedo * max(Attributes.w - 1.0, 0.0)) * Frame.EmissionScale;

    var Colour = Direct.rgb + Indirect + Emission;
    // Closed-form exponential column, geometry only: the sky is not fogged twice.
    if (Frame.InspectionMode == 0u && (Frame.MoonFog.z>0.0 || Frame.Aerial.y>0.0)) {
        let T=FogTransmission(Frame.CameraPosition,Position,Frame.MoonFog.z,Frame.MoonFog.w,Frame.Aerial.y,Frame.Aerial.z,Frame.FogTint.w);
        let Forward = pow(max(0.0,dot(-View,Frame.SolarDisc.xyz)),8.0)*Frame.Aerial.x;
        let FogLight = Frame.NightSkyRadiance + Frame.SunIlluminance*(Frame.FogTint.rgb*0.12+Forward*0.04);
        Colour = Colour*T+FogLight*(1.0-T);
    }
    switch (Frame.InspectionMode)
    {
        case 1u: { Colour = Direct.rgb + Emission; }
        case 2u: { Colour = Diffuse; }
        case 3u: { Colour = Irradiance * InversePi; }
        case 4u:
        {
            let TracePixel = min(Pixel / i32(Frame.TraceScale), vec2i(Frame.TraceExtent) - vec2i(1));
            Colour = textureLoad(ResampledIrradiance, TracePixel, 0).rgb * InversePi;
        }
        case 5u: { Colour = Specular; }
        case 8u: { Colour = Normal * 0.5 + 0.5; }
        case 9u: { Colour = Surface.Albedo; }
        default: { }
    }
    textureStore(RecombinedTarget, Pixel, vec4f(CompositePlume(Colour, Pixel), 1.0));
}
