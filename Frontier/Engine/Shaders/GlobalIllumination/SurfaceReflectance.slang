//============================================================================================================================================
//                                                        SURFACEREFLECTANCE.SLANG
//============================================================================================================================================
// 🧩 The one reflectance formulation every shading site uses: Lambert diffuse, GGX specular, and the automotive clear coat.

#pragma once
#include "IntrinsicParity.slang"

// 📝 Common-domain by the direct term, the resolved indirect term and the reflection resolve so the first bounce and every
//    later one agree on what a surface reflects (Slate's IlluminationGroundwork §5 constraint, applied here).

#include "SceneLayouts.slang"

struct SurfaceSample
{
    float3 Albedo;           // [-]
    float Roughness;          // [-]  perceptual, clamped away from zero
    float Metalness;          // [-]
    float SpecularLevel;      // [-]  dielectric F0
    float CoatWeight;         // [-]
    float CoatRoughness;      // [-]
    float3 Incandescence;         // [-]
    float Transmission;       // [-]
};

SurfaceSample ProjectSurfaceSample(MaterialProfile Material)
{
    var Surface : SurfaceSample = {};
    Surface.Albedo        = Material.Albedo;
    Surface.Roughness     = clamp(Material.Roughness, 0.03, 1.0);
    Surface.Metalness     = Material.Metalness;
    Surface.SpecularLevel = Material.SpecularLevel;
    Surface.CoatWeight    = Material.CoatWeight;
    Surface.CoatRoughness = clamp(Material.CoatRoughness, 0.03, 1.0);
    Surface.Incandescence      = Material.Incandescence;
    Surface.Transmission  = Material.Transmission;
    return Surface;
}

float3 ResolveDiffuseAlbedo(SurfaceSample Surface)
{
    return Surface.Albedo * (1.0 - Surface.Metalness);
}

float3 ResolveSpecularColour(SurfaceSample Surface)
{
    return lerp(float3(Surface.SpecularLevel), Surface.Albedo, Surface.Metalness);
}

// 📝 Schlick's Fresnel.
float3 ResolveFresnel(float3 F0, float Cosine)
{
    let Complement = 1.0 - clamp(Cosine, 0.0, 1.0);
    let Fifth = Complement * Complement * Complement * Complement * Complement;
    return F0 + (float3(1.0) - F0) * Fifth;
}

// 📝 GGX / Trowbridge–Reitz distribution with α = roughness².
float ResolveGgxDistribution(float NdotH, float Roughness)
{
    let α = Roughness * Roughness;
    let α2 = α * α;
    let Denominator = NdotH * NdotH * (α2 - 1.0) + 1.0;
    return α2 / (Pi * Denominator * Denominator);
}

// 📝 Height-correlated Smith visibility V = G / (4·n·l·n·v) (Heitz 2014).
float ResolveSmithVisibility(float NdotL, float NdotV, float Roughness)
{
    let α = Roughness * Roughness;
    let α2 = α * α;
    let Lambda_V = NdotL * sqrt(NdotV * NdotV * (1.0 - α2) + α2);
    let Lambda_L = NdotV * sqrt(NdotL * NdotL * (1.0 - α2) + α2);
    return 0.5 / max(Lambda_V + Lambda_L, 1.0e-6);
}

// 📝 Karis' analytic fit of the split-sum environment response (scale, bias) for F0·A + B.
float2 ResolveEnvironmentResponse(float NdotV, float Roughness)
{
    let c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    let c1 = float4(1.0, 0.0425, 1.04, -0.04);
    let r = Roughness * c0 + c1;
    let a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}

// 📝 Radiance reflected toward V from a distant light of illuminance E along L: a pigment layer under a clear coat whose
//    Fresnel transmittance attenuates the layer beneath, as a coated automotive finish does.
float3 ResolveDirectReflectance(SurfaceSample Surface, float3 Normal, float3 View, float3 Light)
{
    let NdotL = dot(Normal, Light);
    if (NdotL <= 0.0)
    {
        return float3(0.0);
    }
    let NdotV = max(dot(Normal, View), 1.0e-4);
    let Half = normalize(Light + View);
    let NdotH = max(dot(Normal, Half), 0.0);
    let VdotH = max(dot(View, Half), 0.0);
    let F0 = ResolveSpecularColour(Surface);
    let Fresnel = ResolveFresnel(F0, VdotH);
    let Specular = Fresnel * ResolveGgxDistribution(NdotH, Surface.Roughness) * ResolveSmithVisibility(NdotL, NdotV, Surface.Roughness);
    let Diffuse = ResolveDiffuseAlbedo(Surface) * InversePi * (float3(1.0) - Fresnel);
    var Layer = (Diffuse + Specular) * NdotL;
    if (Surface.CoatWeight > 0.0)
    {
        let CoatFresnel = ResolveFresnel(float3(0.04), VdotH).x * Surface.CoatWeight;
        let Coat = CoatFresnel * ResolveGgxDistribution(NdotH, Surface.CoatRoughness)
                 * ResolveSmithVisibility(NdotL, NdotV, Surface.CoatRoughness) * NdotL;
        Layer = Layer * (1.0 - CoatFresnel) + float3(Coat);
    }
    return Layer;
}

// 📝 A distant disc light of angular radius θ (RefinementSolarDisc), after Karis 2013's representative point with Unreal's sphere-light
//    energy normalisation: diffuse toward the disc's centre; GGX toward the disc point nearest the mirror direction, scaled by
//    α² / α'² with α'² = α² + ¼·sinθ·(3α + sinθ) / (v·h + 0.001). A smooth surface shows a disc of the sun's size and energy where
//    a point light gives a needle that a jittered direction can only hit by chance.
float ResolveDiscLobe(float NdotH, float VdotH, float Alpha, float SinRadius)
{
    let Alpha2 = Alpha * Alpha;
    let Widened2 = Alpha2 + 0.25 * SinRadius * (3.0 * Alpha + SinRadius) / (VdotH + 0.001);
    let Denominator = NdotH * NdotH * (Alpha2 - 1.0) + 1.0;
    return Alpha2 / (Pi * Denominator * Denominator) * (Alpha2 / Widened2);
}

float3 ResolveDiscReflectance(SurfaceSample Surface, float3 Normal, float3 View, float3 Centre, float AngularRadius)
{
    let NdotL = dot(Normal, Centre);
    if (NdotL <= 0.0)
    {
        return float3(0.0);
    }
    let NdotV = max(dot(Normal, View), 1.0e-4);
    let Mirror = reflect(-View, Normal);
    let Along = dot(Mirror, Centre);
    let Across = Mirror - Centre * Along;
    let AcrossLength = length(Across);
    let SinRadius = sin(AngularRadius);
    var Nearest = Centre;
    if (Along >= cos(AngularRadius))
    {
        Nearest = Mirror;
    }
    else if (AcrossLength > 1.0e-6)
    {
        Nearest = normalize(Centre * cos(AngularRadius) + Across * (SinRadius / AcrossLength));
    }
    let SpecularNdotL = max(dot(Normal, Nearest), 1.0e-4);
    let Half = normalize(Nearest + View);
    let NdotH = max(dot(Normal, Half), 0.0);
    let VdotH = max(dot(View, Half), 0.0);
    let Fresnel = ResolveFresnel(ResolveSpecularColour(Surface), VdotH);
    let Alpha = Surface.Roughness * Surface.Roughness;
    let Specular = Fresnel * ResolveDiscLobe(NdotH, VdotH, Alpha, SinRadius) * ResolveSmithVisibility(SpecularNdotL, NdotV, Surface.Roughness)
                 * SpecularNdotL;
    let CentreHalf = normalize(Centre + View);
    let Diffuse = ResolveDiffuseAlbedo(Surface) * InversePi * (float3(1.0) - ResolveFresnel(ResolveSpecularColour(Surface), max(dot(View, CentreHalf), 0.0)))
                * NdotL;
    var Layer = Diffuse + Specular;
    if (Surface.CoatWeight > 0.0)
    {
        let CoatFresnel = ResolveFresnel(float3(0.04), VdotH).x * Surface.CoatWeight;
        let CoatAlpha = Surface.CoatRoughness * Surface.CoatRoughness;
        let Coat = CoatFresnel * ResolveDiscLobe(NdotH, VdotH, CoatAlpha, SinRadius)
                 * ResolveSmithVisibility(SpecularNdotL, NdotV, Surface.CoatRoughness) * SpecularNdotL;
        Layer = Layer * (1.0 - CoatFresnel) + float3(Coat);
    }
    return Layer;
}

