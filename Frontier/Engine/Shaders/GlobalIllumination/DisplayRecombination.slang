//============================================================================================================================================
//                                                       DISPLAYRECOMBINATION.SLANG
//============================================================================================================================================
// 🧩 Recombines direct, indirect diffuse, glossy reflection and incandescence per pixel, or substitutes an inspection signal.

#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"

// 📝 The indirect term arrives demodulated (irradiance E) and is modulated here by the diffuse albedo, so texture-free
//    surfaces keep their edges through the wavelet. Specular indirect uses the split-sum response F0·A + B over the
//    traced reflection; past the reflection cutoff it falls back to E/π, Lumen's rough-reflection reuse. A clear coat
//    reflects the same radiance with its own response and attenuates the layer beneath by its Fresnel transmittance.

#include "RefreshDescriptor.slang"
#include "SurfaceReflectance.slang"
#include "PackingCodec.slang"
#include "CabinVolume.slang"
#include "FogMedium.slang"

  [[vk::binding(1, 0)]] StructuredBuffer<MaterialProfile> Materials;
  [[vk::binding(2, 0)]] Texture2D<float4>        SurfaceAttributes;
  [[vk::binding(3, 0)]] Texture2D<float>             SurfaceDepth;
  [[vk::binding(4, 0)]] Texture2D<float4>           DirectRadiance;
  [[vk::binding(5, 0)]] Texture2D<float4>       IndirectIrradiance;
  [[vk::binding(6, 0)]] Texture2D<float4>       ReflectionRadiance;
  [[vk::binding(7, 0)]] Texture2D<float4>      ResampledIrradiance;
  [[vk::binding(8, 0)]] Texture2D<float4>       InspectionRadiance;
  [[vk::binding(9, 0)]] Texture2D<float4>                CardFinal;
  [[vk::binding(10, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4>         RecombinedTarget;
  [[vk::binding(11, 0)]] StructuredBuffer<OccupantProfile> Occupants;   // [0]: the car body, for its cabin

  [[vk::binding(12, 0)]] Texture2D<float4> TyrePlume;
  [[vk::binding(13, 0)]] SamplerState PlumeSampler;
  // 📝 RefinementMirrorFilter: the reflection without the variance filter (alpha 1 where present), at screen resolution.
  [[vk::binding(14, 0)]] Texture2D<float4> MirrorRadiance;

// 📝 Surfaces below MirrorSharpRoughness take the unfiltered reflection, those above MirrorBlendRoughness the variance-filtered one.
static const float MirrorSharpRoughness = 0.10;
static const float MirrorBlendRoughness = 0.20;
float3 BlendPlume(float3 Colour, int2 Pixel) {
    if ((Refresh.Features & 4096u) == 0u || Refresh.InspectionMode != 0u) { return Colour; }
    let Sample = TyrePlume.SampleLevel(PlumeSampler, (float2(Pixel) + 0.5) * Refresh.InverseScreenExtent, 0.0);
    return Colour * Sample.a + Sample.rgb;
}

// 📝 Of the traced indirect light the enclosed cabin keeps (CabinVolume.slang). Its surfaces lie inside the body's own
//    distance volume, so their rays "hit" at once and gather the sunlit exterior's cards — several times the open road's
//    irradiance. A twentieth of that is about what windows let in; the sun through the glass is direct and untouched.
static const float CabinIndirectShare = 0.05;

static const uint InspectionFinal = 0u;
static const uint InspectionDirect = 1u;
static const uint InspectionDiffuse = 2u;
static const uint InspectionIrradiance = 3u;
static const uint InspectionResampled = 4u;
static const uint InspectionReflection = 5u;
static const uint InspectionDistance = 6u;
static const uint InspectionCards = 7u;
static const uint InspectionNormal = 8u;
static const uint InspectionAlbedo = 9u;

 
[shader("compute")]
[numthreads(8, 8, 1)]
void ResolveRecombination(uint3 Invocation : SV_DispatchThreadID)
{
    let Pixel = int2(Invocation.xy);
    if (any(float2(Pixel) >= Refresh.ScreenExtent))
    {
        return;
    }
    let Direct = LoadTexel(DirectRadiance, Pixel, 0);
    if (Refresh.InspectionMode == InspectionDistance || Refresh.InspectionMode >= 10u)
    {
        RecombinedTarget[Pixel] = float4(LoadTexel(InspectionRadiance, Pixel, 0).rgb, 1.0);
        return;
    }
    if (Refresh.InspectionMode == InspectionCards)
    {
        let Extent = float2(TextureExtent(CardFinal));
        let Scale = min(Refresh.ScreenExtent.x, Refresh.ScreenExtent.y) / Extent.x;
        let Texel = int2(float2(Pixel) / Scale);
        var Colour = float3(0.04);
        if (all(Texel < int2(Extent)))
        {
            Colour = LoadTexel(CardFinal, Texel, 0).rgb;
        }
        RecombinedTarget[Pixel] = float4(Colour, 1.0);
        return;
    }
    let Depth = LoadTexel(SurfaceDepth, Pixel, 0);
    if (Depth <= 0.0)
    {
        RecombinedTarget[Pixel] = float4(BlendPlume(Direct.rgb, Pixel), 1.0);
        return;
    }
    let Attributes = LoadTexel(SurfaceAttributes, Pixel, 0);
    let Normal = DecodeOctahedral(Attributes.xy);
    let Surface = ProjectSurfaceSample(Materials[ToUnsigned(Attributes.z + 0.5)]);
    let Position = ReconstructWorldPosition(float2(Pixel) + 0.5, Depth);
    let View = normalize(Refresh.CameraPosition - Position);
    let NdotV = max(dot(Normal, View), 1.0e-3);

    // 📝 Inside the car's cabin: SDF volumes need 0.05 to counter self-hit, whereas RTX traces through real window openings.
    let TargetCabinShare = SelectBranch(CabinIndirectShare, 0.14, FeatureEnabled(Refresh, FeatureRayTracing));
    let Enclosure = lerp(1.0, TargetCabinShare, ResolveCabinShare((mul(float4(Position, 1.0), Occupants[0].WorldToLocal)).xyz));
    var Irradiance = float3(0.0);
    if (FeatureEnabled(Refresh, FeatureIndirect))
    {
        Irradiance = LoadTexel(IndirectIrradiance, Pixel, 0).rgb * Refresh.IndirectScale * Enclosure;
    }
    let Diffuse = ResolveDiffuseAlbedo(Surface) * InversePi * Irradiance;

    let GlossRoughness = SelectBranch(Surface.Roughness, Surface.CoatRoughness, Surface.CoatWeight > 0.0);
    var Reflection = LoadTexel(ReflectionRadiance, Pixel, 0);
    if (RefinementEnabled(Refresh, RefinementMirrorFilter))
    {
        let Mirror = LoadTexel(MirrorRadiance, Pixel, 0);
        let MirrorShare = Mirror.a * SelectBranch(1.0 - smoothstep(MirrorSharpRoughness, MirrorBlendRoughness, SelectBranch(Surface.Roughness, Surface.CoatRoughness,
                                                                                                                        Surface.CoatWeight > 0.0)),
                                                  1.0, RefinementEnabled(Refresh, RefinementMirrorReference));
        Reflection = float4(lerp(Reflection.rgb, Mirror.rgb, MirrorShare), Reflection.a);
    }
    var Glossy = Irradiance * InversePi;
    let UseRtReflections = FeatureEnabled(Refresh, FeatureRayTracing) && FeatureEnabled(Refresh, FeatureRayTracedReflections);
    let Cutoff = SelectBranch(0.50, ResolveReflectionRoughness(Refresh), UseRtReflections);
    let FadeStart = max(0.05, Cutoff - 0.18);
    if (FeatureEnabled(Refresh, FeatureReflection))
    {
        let ReflScale = SelectBranch(1.0, Refresh.IndirectScale, FeatureEnabled(Refresh, FeatureIndirect)) * ResolveReflectionScale(Refresh);
        Glossy = lerp(Reflection.rgb * ReflScale * Enclosure, Glossy, smoothstep(FadeStart, Cutoff, GlossRoughness));
    }
    let LayerResponse = ResolveEnvironmentResponse(NdotV, Surface.Roughness);
    let LayerGlossy = SelectBranch(Glossy, lerp(Glossy, Irradiance * InversePi, smoothstep(FadeStart - 0.05, Cutoff, Surface.Roughness)), Surface.CoatWeight > 0.0);
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
    let Incandescence = (Surface.Incandescence + Surface.Albedo * max(Attributes.w - 1.0, 0.0)) * Refresh.IncandescenceScale;

    var Colour = Direct.rgb + Indirect + Incandescence;
    // Closed-form exponential column, geometry only: the sky is not fogged twice.
    if (Refresh.InspectionMode == 0u && (Refresh.MoonFog.z>0.0 || Refresh.Aerial.y>0.0)) {
        let T = FogTransmission(Refresh.CameraPosition,Position,Refresh.MoonFog.z,Refresh.MoonFog.w,Refresh.Aerial.y,Refresh.Aerial.z,Refresh.FogTint.w);
        let Forward = pow(max(0.0,dot(-View,Refresh.SolarDisc.xyz)),8.0)*Refresh.Aerial.x;
        let FogLight = Refresh.NightSkyRadiance + Refresh.SunIlluminance*(Refresh.FogTint.rgb*0.12+Forward*0.04);
        Colour = Colour*T+FogLight*(1.0-T);
    }
    switch (Refresh.InspectionMode)
    {
        case 1u: { Colour = Direct.rgb + Incandescence; } break;
        case 2u: { Colour = Diffuse; } break;
        case 3u: { Colour = Irradiance * InversePi; } break;
        case 4u:
        {
            let TracePixel = min(Pixel / ToSigned(Refresh.TraceScale), int2(Refresh.TraceExtent) - int2(1));
            Colour = LoadTexel(ResampledIrradiance, TracePixel, 0).rgb * InversePi;
        } break;
        case 5u: { Colour = Specular; } break;
        case 8u: { Colour = Normal * 0.5 + 0.5; } break;
        case 9u: { Colour = Surface.Albedo; } break;
        default: { } break;
    }
    RecombinedTarget[Pixel] = float4(BlendPlume(Colour, Pixel), 1.0);
}
