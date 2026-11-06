//============================================================================================================================================
//                                                         SURFACEREFLECTANCE.WGSL
//============================================================================================================================================
// 🧩 The one reflectance model every shading site uses: Lambert diffuse, GGX specular, and the automotive clear coat.

// 📝 Shared by the direct term, the resolved indirect term and the reflection resolve so the first bounce and every
//    later one agree on what a surface reflects (Slate's IlluminationGroundwork §5 constraint, applied here).

//! include "SceneRecords.wgsl"

struct SurfaceSample
{
    Albedo        : vec3f,   // [-]
    Roughness     : f32,     // [-]  perceptual, clamped away from zero
    Metalness     : f32,     // [-]
    SpecularLevel : f32,     // [-]  dielectric F0
    CoatWeight    : f32,     // [-]
    CoatRoughness : f32,     // [-]
    Emission      : vec3f,   // [-]
    Transmission  : f32,     // [-]
}

fn ProjectSurfaceSample(Material: MaterialRecord) -> SurfaceSample
{
    var Surface: SurfaceSample;
    Surface.Albedo        = Material.Albedo;
    Surface.Roughness     = clamp(Material.Roughness, 0.03, 1.0);
    Surface.Metalness     = Material.Metalness;
    Surface.SpecularLevel = Material.SpecularLevel;
    Surface.CoatWeight    = Material.CoatWeight;
    Surface.CoatRoughness = clamp(Material.CoatRoughness, 0.03, 1.0);
    Surface.Emission      = Material.Emission;
    Surface.Transmission  = Material.Transmission;
    return Surface;
}

fn ResolveDiffuseAlbedo(Surface: SurfaceSample) -> vec3f
{
    return Surface.Albedo * (1.0 - Surface.Metalness);
}

fn ResolveSpecularColour(Surface: SurfaceSample) -> vec3f
{
    return mix(vec3f(Surface.SpecularLevel), Surface.Albedo, Surface.Metalness);
}

// 📐 Schlick's Fresnel.
fn ResolveFresnel(F0: vec3f, Cosine: f32) -> vec3f
{
    let Complement = 1.0 - clamp(Cosine, 0.0, 1.0);
    let Fifth = Complement * Complement * Complement * Complement * Complement;
    return F0 + (vec3f(1.0) - F0) * Fifth;
}

// 📐 GGX / Trowbridge–Reitz distribution with α = roughness².
fn ResolveGgxDistribution(NdotH: f32, Roughness: f32) -> f32
{
    let α = Roughness * Roughness;
    let α2 = α * α;
    let Denominator = NdotH * NdotH * (α2 - 1.0) + 1.0;
    return α2 / (Pi * Denominator * Denominator);
}

// 📐 Height-correlated Smith visibility V = G / (4·n·l·n·v) (Heitz 2014).
fn ResolveSmithVisibility(NdotL: f32, NdotV: f32, Roughness: f32) -> f32
{
    let α = Roughness * Roughness;
    let α2 = α * α;
    let Lambda_V = NdotL * sqrt(NdotV * NdotV * (1.0 - α2) + α2);
    let Lambda_L = NdotV * sqrt(NdotL * NdotL * (1.0 - α2) + α2);
    return 0.5 / max(Lambda_V + Lambda_L, 1.0e-6);
}

// 📐 Karis' analytic fit of the split-sum environment response (scale, bias) for F0·A + B.
fn ResolveEnvironmentResponse(NdotV: f32, Roughness: f32) -> vec2f
{
    let c0 = vec4f(-1.0, -0.0275, -0.572, 0.022);
    let c1 = vec4f(1.0, 0.0425, 1.04, -0.04);
    let r = Roughness * c0 + c1;
    let a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    return vec2f(-1.04, 1.04) * a004 + r.zw;
}

// 📝 Radiance reflected toward V from a distant light of illuminance E along L: a pigment layer under a clear coat whose
//    Fresnel transmittance attenuates the layer beneath, as a coated automotive finish does.
fn ResolveDirectReflectance(Surface: SurfaceSample, Normal: vec3f, View: vec3f, Light: vec3f) -> vec3f
{
    let NdotL = dot(Normal, Light);
    if (NdotL <= 0.0)
    {
        return vec3f(0.0);
    }
    let NdotV = max(dot(Normal, View), 1.0e-4);
    let Half  = normalize(Light + View);
    let NdotH = max(dot(Normal, Half), 0.0);
    let VdotH = max(dot(View, Half), 0.0);
    let F0 = ResolveSpecularColour(Surface);
    let Fresnel = ResolveFresnel(F0, VdotH);
    let Specular = Fresnel * ResolveGgxDistribution(NdotH, Surface.Roughness) * ResolveSmithVisibility(NdotL, NdotV, Surface.Roughness);
    let Diffuse = ResolveDiffuseAlbedo(Surface) * InversePi * (vec3f(1.0) - Fresnel);
    var Layer = (Diffuse + Specular) * NdotL;
    if (Surface.CoatWeight > 0.0)
    {
        let CoatFresnel = ResolveFresnel(vec3f(0.04), VdotH).x * Surface.CoatWeight;
        let Coat = CoatFresnel * ResolveGgxDistribution(NdotH, Surface.CoatRoughness)
                 * ResolveSmithVisibility(NdotL, NdotV, Surface.CoatRoughness) * NdotL;
        Layer = Layer * (1.0 - CoatFresnel) + vec3f(Coat);
    }
    return Layer;
}

const BodyCoatFamilyIndex      : u32 = 6u;
const HubFamilyIndex           : u32 = 10u;
const BodyCoatWaveTargetIndex  : u32 = 107u;
const BodyCoatWaveControlIndex : u32 = 108u;
const WheelWaveTargetIndex     : u32 = 109u;
const WheelWaveControlIndex    : u32 = 110u;

fn ResolveWheelWaveCoverage(BodyLocal: vec3f, CentreAndRadius: vec4f) -> f32
{
    if (CentreAndRadius.w <= 0.001)
    {
        return 0.0;
    }
    let Distance = distance(BodyLocal, CentreAndRadius.xyz);
    return 1.0 - smoothstep(CentreAndRadius.w - 0.45, CentreAndRadius.w, Distance);
}

// 📐 Paint application wave coverage:
//    Mode 1 = side-to-side sweep across the vehicle (SweepFront travels +3.30 m → −3.85 m);
//    Mode 2 = radial outward 3D sphere from the exact dropped point on the vehicle.
fn ResolvePaintWaveCoverage(BodyLocal: vec3f, CentreAndMetric: vec4f, Mode: f32) -> f32
{
    if (Mode < 0.5)
    {
        return 0.0;
    }
    if (Mode < 1.5)
    {
        let Signed = (BodyLocal.x - 0.08 * abs(BodyLocal.y)) - CentreAndMetric.w;
        return smoothstep(-0.55, 0.0, Signed);
    }
    if (CentreAndMetric.w <= 0.001)
    {
        return 0.0;
    }
    let Distance = distance(BodyLocal, CentreAndMetric.xyz);
    return 1.0 - smoothstep(CentreAndMetric.w - 0.55, CentreAndMetric.w, Distance);
}

fn ResolvePaintWaveBand(BodyLocal: vec3f, CentreAndMetric: vec4f, Mode: f32) -> f32
{
    if (Mode < 0.5)
    {
        return 0.0;
    }
    if (Mode < 1.5)
    {
        let Signed = (BodyLocal.x - 0.08 * abs(BodyLocal.y)) - CentreAndMetric.w;
        return exp(-pow((Signed + 0.16) * 5.2, 2.0));
    }
    if (CentreAndMetric.w <= 0.001)
    {
        return 0.0;
    }
    let Distance = distance(BodyLocal, CentreAndMetric.xyz);
    return exp(-pow((Distance - (CentreAndMetric.w - 0.18)) * 5.2, 2.0));
}

fn BlendSurfaceSample(Base: SurfaceSample, Target: SurfaceSample, Coverage: f32) -> SurfaceSample
{
    if (Coverage <= 0.0)
    {
        return Base;
    }
    var Blended = Base;
    Blended.Albedo        = mix(Base.Albedo, Target.Albedo, Coverage);
    Blended.Roughness     = mix(Base.Roughness, Target.Roughness, Coverage);
    Blended.Metalness     = mix(Base.Metalness, Target.Metalness, Coverage);
    Blended.CoatWeight    = mix(Base.CoatWeight, Target.CoatWeight, Coverage);
    Blended.CoatRoughness = mix(Base.CoatRoughness, Target.CoatRoughness, Coverage);
    Blended.Emission      = mix(Base.Emission, Target.Emission, Coverage);
    return Blended;
}

