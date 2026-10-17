//============================================================================================================================================
//                                                           PATHREFLECTANCE.WGSL
//============================================================================================================================================
// 🧩 The path tracer's view of the one reflectance model: evaluation split into diffuse and specular, importance sampling
//    and the pdf of the sampled mixture, so BSDF and light sampling can be combined by multiple importance sampling.

// 📝 Same layers as ResolveDirectReflectance (SurfaceReflectance.wgsl): Lambert under Fresnel, GGX specular with
//    height-correlated Smith, a clear coat on top. Sampling draws the lobe by its approximate albedo, the diffuse
//    direction by the cosine, the specular directions from the GGX visible-normal distribution (Heitz 2018). Every
//    value here is f·cos; the Diffuse half of a split is per unit diffuse albedo so the resolved signal can be
//    demodulated and remodulated at display, as the card path's irradiance is.

//! include "TraceRecords.wgsl"

struct ReflectanceSplit
{
    Diffuse  : vec3f,   // [-] f_d·cos per unit diffuse albedo ρ_d = albedo·(1 − metalness)
    Specular : vec3f,   // [-] f_s·cos including the coat
}

struct LobeWeights
{
    Diffuse  : f32,
    Specular : f32,
    Coat     : f32,
}

struct ReflectanceSample
{
    Direction : vec3f,
    Pdf       : f32,     // [1/sr] mixture pdf of Direction
    Value     : vec3f,   // [-]    f·cos
    Lobe      : u32,     // 0 diffuse, 1 specular, 2 coat
    Roughness : f32,     // [-]    roughness of the lobe drawn (diffuse counts as 1)
}

const LobeDiffuse  : u32 = 0u;
const LobeSpecular : u32 = 1u;
const LobeCoat     : u32 = 2u;

fn EvaluateReflectanceSplit(Surface: SurfaceSample, Normal: vec3f, View: vec3f, Light: vec3f) -> ReflectanceSplit
{
    var Split: ReflectanceSplit;
    Split.Diffuse = vec3f(0.0);
    Split.Specular = vec3f(0.0);
    let NdotL = dot(Normal, Light);
    let NdotV = max(dot(Normal, View), 1.0e-4);
    if (NdotL <= 0.0)
    {
        return Split;
    }
    let Half  = normalize(Light + View);
    let NdotH = max(dot(Normal, Half), 0.0);
    let VdotH = max(dot(View, Half), 0.0);
    let F0 = ResolveSpecularColour(Surface);
    let Fresnel = ResolveFresnel(F0, VdotH);
    var Specular = Fresnel * ResolveGgxDistribution(NdotH, Surface.Roughness) * ResolveSmithVisibility(NdotL, NdotV, Surface.Roughness) * NdotL;
    var Diffuse = (vec3f(1.0) - Fresnel) * (InversePi * NdotL);
    if (Surface.CoatWeight > 0.0)
    {
        let CoatFresnel = ResolveFresnel(vec3f(0.04), VdotH).x * Surface.CoatWeight;
        let Coat = CoatFresnel * ResolveGgxDistribution(NdotH, Surface.CoatRoughness)
                 * ResolveSmithVisibility(NdotL, NdotV, Surface.CoatRoughness) * NdotL;
        Diffuse = Diffuse * (1.0 - CoatFresnel);
        Specular = Specular * (1.0 - CoatFresnel) + vec3f(Coat);
    }
    Split.Diffuse = Diffuse;
    Split.Specular = Specular;
    return Split;
}

fn EvaluateReflectance(Surface: SurfaceSample, Normal: vec3f, View: vec3f, Light: vec3f) -> vec3f
{
    let Split = EvaluateReflectanceSplit(Surface, Normal, View, Light);
    return ResolveDiffuseAlbedo(Surface) * Split.Diffuse + Split.Specular;
}

// 📝 Lobe selection by approximate directional albedo; a floor keeps every present lobe reachable.
fn ResolveLobeWeights(Surface: SurfaceSample, NdotV: f32, AllowSpecular: bool) -> LobeWeights
{
    var Weights: LobeWeights;
    let Diffuse = ResolveLuminance(ResolveDiffuseAlbedo(Surface));
    let SpecularFresnel = ResolveLuminance(ResolveFresnel(ResolveSpecularColour(Surface), NdotV));
    let CoatFresnel = ResolveFresnel(vec3f(0.04), NdotV).x * Surface.CoatWeight;
    var WeightDiffuse  = Diffuse * (1.0 - CoatFresnel);
    var WeightSpecular = SpecularFresnel * (1.0 - CoatFresnel) + Trace.SpecularLobeFloor;
    var WeightCoat     = CoatFresnel;
    if (!AllowSpecular)
    {
        WeightSpecular = 0.0;
        WeightCoat = 0.0;
    }
    let Total = WeightDiffuse + WeightSpecular + WeightCoat;
    if (Total <= 1.0e-6)
    {
        Weights.Diffuse = 1.0;
        Weights.Specular = 0.0;
        Weights.Coat = 0.0;
        return Weights;
    }
    Weights.Diffuse  = WeightDiffuse / Total;
    Weights.Specular = WeightSpecular / Total;
    Weights.Coat     = WeightCoat / Total;
    return Weights;
}

// 📐 Smith G1 for GGX with α = roughness².
fn ResolveSmithMasking(NdotV: f32, Roughness: f32) -> f32
{
    let α = Roughness * Roughness;
    let α2 = α * α;
    return 2.0 * NdotV / max(NdotV + sqrt(α2 + (1.0 - α2) * NdotV * NdotV), 1.0e-6);
}

// 📐 pdf of the reflected direction under visible-normal sampling: G1(v)·D(h) / (4·n·v).
fn ResolveGgxPdf(Normal: vec3f, View: vec3f, Light: vec3f, Roughness: f32) -> f32
{
    let NdotV = max(dot(Normal, View), 1.0e-4);
    let Half = normalize(View + Light);
    let NdotH = max(dot(Normal, Half), 0.0);
    return ResolveSmithMasking(NdotV, Roughness) * ResolveGgxDistribution(NdotH, Roughness) / (4.0 * NdotV);
}

fn PdfReflectance(Surface: SurfaceSample, Normal: vec3f, View: vec3f, Light: vec3f, AllowSpecular: bool) -> f32
{
    let NdotL = dot(Normal, Light);
    if (NdotL <= 0.0)
    {
        return 0.0;
    }
    let NdotV = max(dot(Normal, View), 1.0e-4);
    let Weights = ResolveLobeWeights(Surface, NdotV, AllowSpecular);
    var Pdf = Weights.Diffuse * NdotL * InversePi;
    if (Weights.Specular > 0.0)
    {
        Pdf += Weights.Specular * ResolveGgxPdf(Normal, View, Light, Surface.Roughness);
    }
    if (Weights.Coat > 0.0)
    {
        Pdf += Weights.Coat * ResolveGgxPdf(Normal, View, Light, Surface.CoatRoughness);
    }
    return Pdf;
}

// 📐 GGX visible-normal sampling (Heitz 2018) in the tangent frame.
fn SampleGgxVisibleNormal(TangentView: vec3f, Roughness: f32, Pair: vec2f) -> vec3f
{
    let α = Roughness * Roughness;
    let Vh = normalize(vec3f(α * TangentView.x, α * TangentView.y, TangentView.z));
    let LengthSquared = Vh.x * Vh.x + Vh.y * Vh.y;
    let T1 = select(vec3f(1.0, 0.0, 0.0), vec3f(-Vh.y, Vh.x, 0.0) / sqrt(max(LengthSquared, 1.0e-12)), LengthSquared > 1.0e-12);
    let T2 = cross(Vh, T1);
    let Radius = sqrt(Pair.x);
    let φ = 2.0 * Pi * Pair.y;
    let t1 = Radius * cos(φ);
    var t2 = Radius * sin(φ);
    let s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(max(0.0, 1.0 - t1 * t1)) + s * t2;
    let Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;
    return normalize(vec3f(α * Nh.x, α * Nh.y, max(0.0, Nh.z)));
}

fn SampleReflectance(Surface: SurfaceSample, Normal: vec3f, View: vec3f, Random: vec3f, AllowSpecular: bool) -> ReflectanceSample
{
    var Sample: ReflectanceSample;
    Sample.Pdf = 0.0;
    Sample.Value = vec3f(0.0);
    Sample.Direction = Normal;
    Sample.Lobe = LobeDiffuse;
    Sample.Roughness = 1.0;
    let NdotV = max(dot(Normal, View), 1.0e-4);
    let Weights = ResolveLobeWeights(Surface, NdotV, AllowSpecular);
    let Frame3 = ConstructTangentFrame(Normal);
    var Direction: vec3f;
    if (Random.x < Weights.Diffuse)
    {
        Direction = Frame3 * ProjectCosineHemisphere(Random.yz);
        Sample.Lobe = LobeDiffuse;
        Sample.Roughness = 1.0;
    }
    else
    {
        let Coat = Random.x >= Weights.Diffuse + Weights.Specular;
        let Roughness = select(Surface.Roughness, Surface.CoatRoughness, Coat);
        let TangentView = transpose(Frame3) * View;
        let Half = Frame3 * SampleGgxVisibleNormal(TangentView, Roughness, Random.yz);
        Direction = reflect(-View, Half);
        Sample.Lobe = select(LobeSpecular, LobeCoat, Coat);
        Sample.Roughness = Roughness;
    }
    if (dot(Direction, Normal) <= 1.0e-4)
    {
        return Sample;
    }
    Sample.Direction = normalize(Direction);
    Sample.Pdf = PdfReflectance(Surface, Normal, View, Sample.Direction, AllowSpecular);
    Sample.Value = EvaluateReflectance(Surface, Normal, View, Sample.Direction);
    return Sample;
}

// 📝 Whether a surface may host a reconnection vertex: its reflectance is mostly carried by lobes at least as rough as
//    the threshold, so a slightly different incoming direction changes its response smoothly.
fn ResolveReconnectable(Surface: SurfaceSample, NdotV: f32) -> bool
{
    let Weights = ResolveLobeWeights(Surface, NdotV, true);
    var Rough = Weights.Diffuse;
    if (Surface.Roughness >= Trace.ReconnectionRoughness)
    {
        Rough += Weights.Specular;
    }
    if (Surface.CoatRoughness >= Trace.ReconnectionRoughness)
    {
        Rough += Weights.Coat;
    }
    return Rough >= 0.5;
}
