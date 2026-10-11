//============================================================================================================================================
//                                          AUTOMOTIVE MATERIAL PROFILES
//============================================================================================================================================
// Shared authoring profiles for the standalone automotive material scene. These are deliberately built out of the
// same ShadingRecord fields consumed by MaterialEvaluation.slang: the CPU exhibit and the GPU include see the exact
// same profile bytes and the exact same OpenPBR lobe evaluator. They are not wired into Project-Zero's material
// records yet; the preview scene is the review gate before that integration.
//
// Current preview milestone: clearcoat paint with dense analytic metallic flakes, carbon/resin, red/clear optics,
// brushed alloy, heat-tinted titanium, rubber, and Alcantara. UV texture sampling, water, and automotive host
// serialization remain later milestones. The functions below are intentionally texture-free so a CPU render can be
// compared bit-for-bit against the same constants on the GPU.

#ifndef FRONTIER_AUTOMOTIVE_MATERIAL_PROFILES
#define FRONTIER_AUTOMOTIVE_MATERIAL_PROFILES

ShadingRecord AutomotiveDefaults()
{
    ShadingRecord M;
#ifdef FRONTIER_AUTOMOTIVE_SHOWCASE
    M.AutomotiveData=vec4(0.0);M.AutomotiveDensity=0.0;M.AutomotiveFootprint=0.0;
#endif
    M.BaseColor = vec3(0.5);
    M.Metalness = 0.0;
    M.DiffuseRoughness = 0.5;
    M.SpecularWeight = 1.0;
    M.SpecularColor = vec3(1.0);
    M.SpecularRoughness = 0.5;
    M.SpecularAnisotropy = 0.0;
    M.AnisotropyAngle = 0.0;
    M.SpecularIor = 1.5;
    M.ThinFilmWeight = 0.0;
    M.ThinFilmThickness = 0.5;
    M.ThinFilmIor = 1.4;
    M.HazinessWeight = 0.0;
    M.HazinessRoughness = 0.5;
    M.CoatWeight = 0.0;
    M.CoatColor = vec3(1.0);
    M.CoatRoughness = 0.0;
    M.CoatAnisotropy = 0.0;
    M.CoatIor = 1.5;
    M.CoatDarkening = 1.0;
    M.CoatTangent = vec3(1.0, 0.0, 0.0);
    M.CoatNormal = vec3(0.0, 0.0, 1.0);
    M.FuzzWeight = 0.0;
    M.FuzzColor = vec3(1.0);
    M.FuzzRoughness = 0.5;
    M.Emission = vec3(0.0);
    M.TransmissionWeight = 0.0;
    M.TransmissionColor = vec3(1.0);
    M.TransmissionDepth = 0.0;
    M.TransmissionThickness = 0.0;
    M.Selection = kReflectanceStandard;
    M.SssWeight = 0.0;
    M.SssColor = vec3(1.0);
    M.SssRadius = 0.0;
    M.SssRadiusScale = vec3(1.0);
    M.SssThickness = 0.0;
    return M;
}

// A compact flop helper for later per-hit/profile parameterisation. It is shared here so the future texture/UV
// integration cannot invent a second colour-shift curve. mu is the view cosine; edgeColor is approached at grazing.
vec3 AutomotiveFlopColor(vec3 faceColor, vec3 edgeColor, float mu)
{
    float grazing = pow(1.0 - clamp(mu, 0.0, 1.0), 5.0);
    return mix(faceColor, edgeColor, grazing);
}

// Cauchy dispersion at wavelengthNm. B and C are authored around the 589.3 nm reference line so the supplied
// IorD remains the measured yellow-green index. This is a transport parameter, not an RGB tint: the preview traces
// each display channel with its wavelength-specific IOR through the existing Fresnel/Snell/TIR path.
float AutomotiveCauchyIor(float iorD, float cauchyB, float cauchyC, float wavelengthNm)
{
    float lambdaUm = max(wavelengthNm * 0.001f, 0.25f);
    float referenceUm = 0.5893f;
    float a = iorD - cauchyB / (referenceUm * referenceUm) - cauchyC /
              (referenceUm * referenceUm * referenceUm * referenceUm);
    return max(1.0001f, a + cauchyB / (lambdaUm * lambdaUm) +
               cauchyC / (lambdaUm * lambdaUm * lambdaUm * lambdaUm));
}

// Deterministic, texture-free microflake signal. Three incommensurate bands avoid a visible grid while remaining
// identical in the CPU port and the GPU shader. The signal is deliberately a material-detail term, not a light
// source: it only modulates the shared coat/base lobes and their resolved microfacet normal.
float AutomotiveFlakeSignal(vec3 position, vec3 normal, float density, float scale, float jitter)
{
    float h0 = abs(float(sin(dot(position, vec3(12.9898, 78.233, 37.719)) * scale + jitter * 17.17)));
    float h1 = abs(float(sin(dot(position, vec3(39.3468, 11.135, 83.155)) * (scale * 1.731) + jitter * 29.31)));
    float h2 = abs(float(sin(dot(position, vec3(91.17, 17.31, 43.77)) * (scale * 0.617) + jitter * 47.03)));
    float sparkle = smoothstep(1.0 - density, 1.0 - density + 0.13, h0)
                  * smoothstep(1.0 - density * 0.70, 1.0 - density * 0.70 + 0.14, h1);
    sparkle = max(sparkle, smoothstep(0.91, 0.995, h2) * 0.72);
    float grazing = 1.0 - clamp(dot(normalize(normal), vec3(0.0, 0.0, 1.0)), 0.0, 1.0);
    return clamp(sparkle * (0.65 + 0.35 * grazing), 0.0, 1.0);
}

// The flakes are tiny metallic facets under the clearcoat. This fallback perturbs the resolved shading normal with a
// bounded, deterministic orientation rather than painting an emissive dot. A future UV normal map can replace this
// function without changing the clearcoat or Fresnel code below.
vec3 AutomotiveApplyTriCoatFlakeNormal(vec3 normal, vec3 position, float density, float scale, float jitter)
{
    float signal = AutomotiveFlakeSignal(position, normal, density, scale, jitter);
    vec3 helper = abs(normal.z) < 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(helper, normal));
    vec3 bitangent = cross(normal, tangent);
    float tx = float(sin(dot(position, vec3(73.17, 19.31, 41.77)) * scale + jitter * 13.0));
    float ty = float(sin(dot(position, vec3(17.23, 61.91, 29.41)) * (scale * 1.37) + jitter * 23.0));
    float tilt = signal * 0.22;
    return normalize(normal + tangent * (tx * tilt) + bitangent * (ty * tilt));
}

// UV-backed review detail. These analytic test patterns stand in for texture reads until the host texture binding is
// integrated: both CPU and GPU still consume the same UV coordinates, phase, thresholds, and lobe modulation. The
// carbon pattern is a dual-weave warp/weft, while the tire pattern uses staggered longitudinal ribs and cross grooves.
float AutomotiveCarbonWeave(vec2 uv)
{
    float Tau = 6.28318530718f;
    float warp = 0.5f + 0.5f * float(sin(uv.x * Tau * 34.0f + 0.22f * sin(uv.y * Tau * 3.0f)));
    float weft = 0.5f + 0.5f * float(sin(uv.y * Tau * 34.0f + 0.22f * sin(uv.x * Tau * 3.0f)));
    float diagonal = 0.5f + 0.5f * float(sin((uv.x + uv.y) * Tau * 17.0f));
    return clamp(0.50f * max(pow(warp, 3.0f), pow(weft, 3.0f)) + 0.16f * diagonal, 0.0f, 1.0f);
}

float AutomotiveTireTread(vec2 uv)
{
    float Tau = 6.28318530718f;
    float rib = abs(float(sin(uv.x * Tau * 11.0f + 0.35f * sin(uv.y * Tau * 2.0f))));
    float crossGroove = abs(float(sin(uv.y * Tau * 5.5f + uv.x * Tau * 2.0f)));
    return clamp(smoothstep(0.28f, 0.72f, rib) * (0.72f + 0.28f * crossGroove), 0.0f, 1.0f);
}

vec3 AutomotiveUvFrameNormal(vec3 normal, vec2 uv, float pattern, float amplitude)
{
    vec3 helper = abs(normal.z) < 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(helper, normal));
    vec3 bitangent = cross(normal, tangent);
    float Tau = 6.28318530718;
    float hx = pattern < 0.5
        ? sin(uv.x * Tau * 34.0 + 0.18 * sin(uv.y * Tau * 3.0))
        : sin(uv.x * Tau * 11.0 + 0.35 * sin(uv.y * Tau * 2.0));
    float hy = pattern < 0.5
        ? sin(uv.y * Tau * 34.0 + 0.18 * sin(uv.x * Tau * 3.0))
        : sin(uv.y * Tau * 5.5 + uv.x * Tau * 2.0);
    return normalize(normal + tangent * (hx * amplitude) + bitangent * (hy * amplitude));
}

ShadingRecord AutomotiveApplyCarbonUvDetail(ShadingRecord M, vec2 uv)
{
    float weave = AutomotiveCarbonWeave(uv);
    M.BaseColor = mix(vec3(0.008, 0.010, 0.013), vec3(0.055, 0.068, 0.082), weave * 0.72);
    M.SpecularRoughness = mix(0.30, 0.18, weave);
    M.SpecularAnisotropy = mix(0.58, 0.88, weave);
    M.CoatRoughness = mix(0.10, 0.055, weave);
    return M;
}

ShadingRecord AutomotiveApplyTireUvDetail(ShadingRecord M, vec2 uv)
{
    float tread = AutomotiveTireTread(uv);
    M.BaseColor = mix(vec3(0.009, 0.010, 0.011), vec3(0.030, 0.034, 0.038), tread);
    M.DiffuseRoughness = mix(0.98, 0.82, tread);
    M.SpecularWeight = mix(0.12, 0.28, tread);
    M.SpecularRoughness = mix(0.88, 0.66, tread);
    return M;
}

// Apply the automotive paint's analytic detail after the material record has been resolved. The caller supplies the
// view direction for the flop term. A later host integration can replace position with UV-backed flakes while keeping
// this exact fallback and the same lobe fields.
ShadingRecord AutomotiveApplyTriCoatFlakes(ShadingRecord M, vec3 position, vec3 normal, vec3 viewDirection,
                                           vec3 edgeColor, float density, float scale, float jitter)
{
    if (M.Metalness <= 0.0 || M.CoatWeight <= 0.0) return M;
    float mu = clamp(dot(normalize(normal), normalize(viewDirection)), 0.0, 1.0);
    M.BaseColor = AutomotiveFlopColor(M.BaseColor, edgeColor, mu);
    float signal = AutomotiveFlakeSignal(position, normal, density, scale, jitter);
    // The flake is a bright microfacet population, not an emissive decal: it raises the shared metallic F82 tint and
    // locally narrows both the base and clearcoat roughness so a direct-light sample produces visible silver/blue
    // sparkle at finite preview spp. Mixing toward the flake colour makes the effect readable at the review distance,
    // while the underlying clearcoat still carries the broad reflection.
    vec3 flakeColor = mix(edgeColor, vec3(1.0), 0.55);
    M.BaseColor = mix(M.BaseColor, flakeColor, signal * 0.74);
    M.SpecularColor = mix(M.SpecularColor, vec3(1.0), signal * 0.92);
    M.SpecularRoughness = mix(M.SpecularRoughness, max(0.012, M.SpecularRoughness * 0.20), signal * 0.92);
    M.CoatRoughness = mix(M.CoatRoughness, max(0.012, M.CoatRoughness * 0.45), signal * 0.35);
    return M;
}

ShadingRecord AutomotiveTriCoat(vec3 pigment, float roughness, float coatRoughness, float coatIor)
{
    ShadingRecord M = AutomotiveDefaults();
    M.BaseColor = pigment;
    M.Metalness = 0.92;
    M.SpecularColor = vec3(0.92, 0.95, 1.0);
    M.SpecularRoughness = roughness;
    M.SpecularWeight = 1.0;
    M.CoatWeight = 1.0;
    M.CoatColor = vec3(0.98, 0.99, 1.0);
    M.CoatRoughness = coatRoughness;
    M.CoatIor = coatIor;
    M.CoatDarkening = 0.9;
    M.Selection = kReflectanceClearCoated;
    return M;
}

ShadingRecord AutomotiveCarbonResin()
{
    ShadingRecord M = AutomotiveDefaults();
    M.BaseColor = vec3(0.012, 0.016, 0.020);
    M.Metalness = 0.35;
    M.SpecularColor = vec3(0.72, 0.78, 0.84);
    M.SpecularRoughness = 0.24;
    M.SpecularAnisotropy = 0.72;
    M.AnisotropyAngle = 0.78539816339;
    M.CoatWeight = 1.0;
    M.CoatColor = vec3(0.96, 0.98, 1.0);
    M.CoatRoughness = 0.08;
    M.CoatIor = 1.52;
    M.Selection = kReflectanceClearCoated;
    return M;
}

ShadingRecord AutomotiveClearHeadlight(float ior, float roughness)
{
    ShadingRecord M = AutomotiveDefaults();
    M.BaseColor = vec3(0.0);
    M.SpecularIor = ior;
    M.SpecularRoughness = roughness;
    M.TransmissionWeight = 1.0;
    M.TransmissionColor = vec3(0.94, 0.98, 1.0);
    M.TransmissionDepth = 4.0;
    M.TransmissionThickness = 0.22;
    M.Selection = kReflectanceTransmissive;
    return M;
}

ShadingRecord AutomotiveRedTailLens(float ior, float roughness)
{
    ShadingRecord M = AutomotiveDefaults();
    M.BaseColor = vec3(0.0);
    M.SpecularIor = ior;
    M.SpecularRoughness = roughness;
    M.TransmissionWeight = 1.0;
    M.TransmissionColor = vec3(0.40, 0.008, 0.004);
    M.TransmissionDepth = 0.70;
    M.TransmissionThickness = 0.16;
    M.Selection = kReflectanceTransmissive;
    return M;
}

ShadingRecord AutomotiveBrushedAlloy(vec3 tint, float roughness, float anisotropy, float angle)
{
    ShadingRecord M = AutomotiveDefaults();
    M.BaseColor = tint;
    M.Metalness = 1.0;
    M.SpecularColor = vec3(0.90, 0.93, 0.98);
    M.SpecularRoughness = roughness;
    M.SpecularAnisotropy = anisotropy;
    M.AnisotropyAngle = angle;
    M.Selection = kReflectanceAnisotropic;
    return M;
}

ShadingRecord AutomotiveHeatTitanium(float roughness, float filmThickness)
{
    ShadingRecord M = AutomotiveDefaults();
    M.BaseColor = vec3(0.48, 0.52, 0.58);
    M.Metalness = 1.0;
    M.SpecularColor = vec3(0.75, 0.82, 0.92);
    M.SpecularRoughness = roughness;
    M.ThinFilmWeight = 0.78;
    M.ThinFilmThickness = filmThickness;
    M.ThinFilmIor = 1.72;
    M.Selection = kReflectanceStandard;
    return M;
}

ShadingRecord AutomotiveTireRubber()
{
    ShadingRecord M = AutomotiveDefaults();
    M.BaseColor = vec3(0.018, 0.020, 0.022);
    M.Metalness = 0.0;
    M.DiffuseRoughness = 0.86;
    M.SpecularWeight = 0.22;
    M.SpecularColor = vec3(0.18, 0.19, 0.20);
    M.SpecularRoughness = 0.78;
    M.Selection = kReflectanceStandard;
    return M;
}

ShadingRecord AutomotiveAlcantara()
{
    ShadingRecord M = AutomotiveDefaults();
    M.BaseColor = vec3(0.075, 0.082, 0.090);
    M.DiffuseRoughness = 0.92;
    M.SpecularWeight = 0.08;
    M.SpecularRoughness = 0.72;
    M.FuzzWeight = 0.72;
    M.FuzzColor = vec3(0.16, 0.18, 0.20);
    M.FuzzRoughness = 0.86;
    M.Selection = kReflectanceCloth;
    return M;
}

#endif // FRONTIER_AUTOMOTIVE_MATERIAL_PROFILES
