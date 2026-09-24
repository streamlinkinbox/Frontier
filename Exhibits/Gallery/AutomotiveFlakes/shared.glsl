#define FRONTIER_CPU_PORT 1
vec3 FetchEnergy(float a,float b){return vec3(0.0);}
vec3 FetchSheen(float a,float b){return vec3(0.0);}
vec4 FetchSheenFull(float a,float b){return vec4(0.0);}
//============================================================================================================================================
//                                                    MATERIALEVALUATION.SLANG
//============================================================================================================================================
// 🧩 OpenPBR lobe set for one resolved slab (Tier A). Include-only; also compiled as C++ by Scratchpad/MaterialEvaluationTest.cpp
//    (FRONTIER_CPU_PORT) so the white-furnace / reciprocity / sampling proofs run on exactly this text.
//
// Lobes (OpenPBR §5 order, bottom-up, §3.10 albedo scaling between layers):
//    diffuse   EON — Portsmouth, Kutz, Hill 2025 (arXiv 2410.18026v3 Listing 1–3, verbatim maths; CLTC sampling)
//    specular  GGX · Smith height-correlated · Dupuy–Benyoub 2023 spherical-cap VNDF · Kulla–Conty 2017 / Turquin 2019
//              multiple-scattering compensation from the E(μ,α) table on EnergyLut
//    metal     same GGX with F82-tint Fresnel (Kutz, Hašan, Edmondson 2021; OpenPBR §5.3)
//    thin film Belcour & Barla 2017 (2-term Airy series, Gaussian CIE sensitivity fit) modulating the base Fresnel
//    haziness  second GGX at slate_haziness_roughness mixed by slate_haziness_weight (Barla, Pacanowski, Vangorp 2018)
//    coat      dielectric GGX on top; base darkened per OpenPBR §5.6 (K = lerp(Ks, Kr, r_b)), roughened (r⁴ + 2 r_c⁴)^¼,
//              view-dependent absorption T^(1/μti + 1/μto)
//    fuzz      Zeltner, Burley, Chiang 2022 LTC sheen (table on SheenLut: aInv, bInv, R by [α][cosθ])
//    transmit  Walter 2007 GGX BTDF sampled through microfacet-Fresnel branch selection (exact R/T split), thin-walled
//              by default via the tilted-entry + flat-exit compound (KHR_materials_volume semantics: Beer over the
//              nominal thickness); solid mode (M4b) evaluates the bare single interface per boundary while the tracer
//              walks the medium (true enter/exit + Beer over true segments; IncidentIor tracks the incident medium).
//
// Conventions: local shading frame, +Z = shading normal, wo = towards the viewer, wi = towards the light; both point
//    away from the surface. Every Evaluate* returns f (no cosine). Sample* returns wi and the pdf of the whole mixture.
//    Colours linear Rec.709. Not evaluated here: subsurface (M5), dispersion (M4c), glints (M6).

#ifndef FRONTIER_MATERIAL_EVALUATION
#define FRONTIER_MATERIAL_EVALUATION

#ifndef FRONTIER_CPU_PORT
layout(binding = 13) uniform sampler2D EnergyLut;   // x = μ = cosθ, y = α : (A, B, E_avg) — E_ss(μ, α) = F0·A + B (split-sum), E_avg(α) hemispherical mean
layout(binding = 14) uniform sampler2D SheenLut;    // x = cosθ,    y = α : (aInv, bInv, R) — LtcSheenTable.h
vec3  FetchEnergy(float mu, float alpha)     { return textureLod(EnergyLut, vec2(mu, alpha), 0.0).xyz; }
vec3  FetchSheen (float mu, float alpha)     { return textureLod(SheenLut,  vec2(mu, alpha), 0.0).xyz; }
vec4  FetchSheenFull(float mu, float alpha)  { return textureLod(SheenLut,  vec2(mu, alpha), 0.0); }   // M3: + E_charlie in .w
#endif

const float kPi         = 3.14159265358979;
const float kInvPi      = 0.31830988618379;
const float kMinAlpha   = 0.0025;                // [-] roughness² floor: keeps D/VNDF finite; mirror-like below this
const float kMinCos     = 1.0e-4;

//------------------------------------------------------------------------------------------------------------------------
//                                          SELECTION + CHANNEL CONSUMPTION (M1, moved M3)
//------------------------------------------------------------------------------------------------------------------------
// M3: this table lives HERE (not in ReSTIRViewport.slang) so the CPU port compiles and tests it 1:1 — the kernel uses
// it through #include "MaterialEvaluation.slang", so there is exactly one source of truth. Canonical selection values:
// SceneRecords.slang kMaterialReflectance* (GPU) = MaterialIndex.h MaterialReflectance (CPU).
const uint kChannelBaseColor = 0u, kChannelMetalness = 1u, kChannelRoughness = 2u, kChannelSpecularColor = 3u, kChannelNormal = 4u,
           kChannelCoatNormal = 5u, kChannelEmission = 6u, kChannelOpacity = 7u, kChannelTransmission = 8u,
           kChannelSubsurface = 9u, kChannelCoat = 10u, kChannelFuzz = 11u,
           kChannelThinFilm = 12u, kChannelAnisotropy = 13u, kChannelOcclusion = 14u;

// M1: reflectance selection mirror (canonical: SceneRecords.slang kMaterialReflectance*). Sultan-18 §3's eight
// shading selections, derived per material by MaterialIndex::DeriveReflectance from exact-zero weights.
const uint kReflectanceStandard = 0u, kReflectanceAnisotropic = 1u, kReflectanceClearCoated = 2u, kReflectanceCloth = 3u,
           kReflectanceSubsurface = 4u, kReflectanceTransmissive = 5u, kReflectanceEmissiveOnly = 6u, kReflectanceUnlit = 7u;

// Sultan-18 §9: a selection samples only the channels it consumes. Emission (6) and opacity (7) are NEVER gated — they
// feed the emission short-circuit and cutout, not just shading — and neither is base colour (Unlit reads it). Every
// other fetch in ResolveMaterial runs iff (constant weight > 0 || Consumes), and since selection is derived from
// exact-zero weights, the gate provably skips fetches only where the factor is the identity anyway (bound-but-zero
// textures). Cloth = Sultan-18 §3 {1,3,5,6,8,14,15} (M3: opacity retained, so cutout works on cloth).
bool ReflectanceConsumes(uint selection, uint channel)
{
    if (selection == kReflectanceUnlit || selection == kReflectanceEmissiveOnly) return false;
    if (selection == kReflectanceCloth)
        return channel == kChannelBaseColor || channel == kChannelRoughness || channel == kChannelNormal ||
               channel == kChannelOpacity || channel == kChannelFuzz || channel == kChannelOcclusion;
    if (channel == kChannelMetalness || channel == kChannelRoughness || channel == kChannelSpecularColor ||
        channel == kChannelNormal || channel == kChannelOcclusion)
        return true;   // Standard set, shared by every reflective selection
    if (channel == kChannelCoat) return selection == kReflectanceClearCoated;
    if (channel == kChannelAnisotropy) return selection == kReflectanceAnisotropic;
    if (channel == kChannelCoatNormal) return selection == kReflectanceClearCoated;
    if (channel == kChannelTransmission) return selection == kReflectanceTransmissive;   // M4 (DeriveReflectance: weight > 0 ⟹ Transmissive, so the gate is behaviour-free)
    if (channel == kChannelSubsurface) return selection == kReflectanceSubsurface;     // M5 (weight > 0 ⟹ Subsurface — same proof)
    return false;      // thin-film (12): extension, weight-gated only
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SHADING RECORD
//------------------------------------------------------------------------------------------------------------------------
// Everything the lobes need, already resolved from MaterialRecord / MaterialSlabRecord + textures by ResolveMaterial.

struct ShadingRecord
{
    vec3  BaseColor;          // base_weight × base_color (× texture)
    float Metalness;          // base_metalness
    float DiffuseRoughness;   // base_diffuse_roughness (EON r)
    float SpecularWeight;     // specular_weight (ξs)
    vec3  SpecularColor;      // specular_color (dielectric tint / F82 edge tint)
    float SpecularRoughness;  // specular_roughness r  (α = r²)
    float SpecularAnisotropy; // specular_roughness_anisotropy (signed: negative flips the major axis)
    float AnisotropyAngle;    // M2: rotation of the aniso axes vs local tangent [rad] (texture direction + rotation)
    float SpecularIor;        // specular_ior
    float ThinFilmWeight;     // thin_film_weight
    float ThinFilmThickness;  // thin_film_thickness [µm]
    float ThinFilmIor;        // thin_film_ior
    float HazinessWeight;     // slate_haziness_weight
    float HazinessRoughness;  // slate_haziness_roughness
    float CoatWeight;         // coat_weight
    vec3  CoatColor;          // coat_color (T² at normal incidence)
    float CoatRoughness;      // coat_roughness
    float CoatAnisotropy;     // M2: coat_roughness_anisotropy (same parametrisation, coat-frame axes)
    float CoatIor;            // coat_ior
    float CoatDarkening;      // coat_darkening
    vec3  CoatTangent;        // M2: coat frame in LOCAL coords (orthonormal; identity (1,0,0) when unbound)
    vec3  CoatNormal;         // M2: coat normal in local coords (identity (0,0,1) when unbound)
    float FuzzWeight;         // fuzz_weight
    vec3  FuzzColor;          // fuzz_color
    float FuzzRoughness;      // fuzz_roughness
    vec3  Emission;           // emission_luminance × emission_color [nit]
    float TransmissionWeight; // M4: transmission_weight (exact-zero gate: 0 = opaque)
    vec3  TransmissionColor;  // M4: attenuation colour (KHR volume attenuation_color; white = untinted)
    float TransmissionDepth;  // M4: attenuation distance [m] (0 = no absorption)
    float TransmissionThickness; // M4: KHR volume thickness [m] — Beer path of the thin-wall slab (0 = foil); solid mode traverses true geometry and only uses it as the no-exit fallback
    uint  Selection;          // M3: kReflectance* (drives the per-selection shading path; stack-local, NOT a layout field)
    float SssWeight;          // M5: subsurface_weight (exact-zero gate: 0 = no SSS arm, branch-skipped)
    vec3  SssColor;           // M5: subsurface_color ρ (single-scatter albedo tint of the wrap term)
    float SssRadius;          // M5: subsurface_radius r [m] (mean free path scale; ≤ 0 = opaque, no transport)
    vec3  SssRadiusScale;     // M5: subsurface_radius_scale.rgb (per-channel MFP multiplier; skin ≈ (1, 0.37, 0.3))
    float SssThickness;       // M5: geometric chord t [m] at THIS hit (tracer-side, M4b-style; +∞ on miss = Beer 0)
};

// Standalone automotive profiles are included after the shared record definition so both the GPU shader and the
// FRONTIER_CPU_PORT build expose the same material constructors. Project-Zero does not serialize these profiles yet;
// the separate automotive scene is the review gate before that host integration.
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


//------------------------------------------------------------------------------------------------------------------------
//                                                  FRESNEL
//------------------------------------------------------------------------------------------------------------------------

// Exact unpolarised dielectric Fresnel for relative IOR eta = n_transmitted / n_incident.
float FresnelDielectric(float mu, float eta)
{
    mu = clamp(mu, 0.0, 1.0);
    float sinT2 = (1.0 - mu * mu) / (eta * eta);
    if (sinT2 >= 1.0) return 1.0;                                     // total internal reflection
    float muT = sqrt(1.0 - sinT2);
    float rs = (mu - eta * muT) / (mu + eta * muT);
    float rp = (eta * mu - muT) / (eta * mu + muT);
    return 0.5 * (rs * rs + rp * rp);
}

// OpenPBR eq. 25–26: specular_weight modulates F0 by lowering / raising the IOR ratio.
float ModulatedIor(float eta, float weight)
{
    float f0  = (eta - 1.0) / (eta + 1.0); f0 *= f0;
    float eps = sqrt(clamp(weight * f0, 0.0, 0.9999)) * (eta >= 1.0 ? 1.0 : -1.0);
    return (1.0 + eps) / (1.0 - eps);
}

vec3 FresnelSchlick(vec3 f0, float mu)
{
    float m = clamp(1.0 - mu, 0.0, 1.0);
    float m2 = m * m;
    return f0 + (vec3(1.0) - f0) * (m2 * m2 * m);
}

// OpenPBR §5.3 F82-tint: F(μ̄ = 1/7) is pulled down to specular_color × Schlick(μ̄).
vec3 FresnelF82(vec3 f0, vec3 edgeTint, float mu)
{
    const float muBar = 1.0 / 7.0;
    const float denom = muBar * 0.396823;                              // μ̄ (1 − μ̄)⁶
    vec3 fBar = FresnelSchlick(f0, muBar);
    vec3 a    = (fBar - edgeTint * fBar) / denom;
    float m   = clamp(1.0 - mu, 0.0, 1.0);
    float m2 = m * m; float m6 = m2 * m2 * m2;
    return FresnelSchlick(f0, mu) - a * mu * m6;
}

// Average (hemispherical) Fresnel of the exact dielectric curve — d'Eon's fit, used by coat darkening and Kulla–Conty.
float FresnelDielectricAverage(float eta)
{
    if (eta >= 1.0) return (eta - 1.0) / (4.08567 + 1.00071 * eta);
    float e2 = eta * eta, e3 = e2 * eta;
    return 0.997118 + 0.1014 * eta - 0.965241 * e2 - 0.130607 * e3;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              THIN FILM (Belcour & Barla 2017)
//------------------------------------------------------------------------------------------------------------------------
// Two-term Airy expansion with the CIE XYZ Gaussian sensitivity fit of the paper; per-polarisation top interface, base
//    interface taken as the unpolarised base reflectance with the dielectric phase rule (π when n_base > n_film).
//    ⚠️ Deviation: for metals the base phase is 0 and the reflectance is the F82 curve (no complex-IOR inversion).

vec3 ThinFilmSensitivity(float opd, vec3 shift)   // opd [nm]
{
    float phase = 2.0 * kPi * opd * 1.0e-9;
    vec3 val = vec3(5.4856e-13, 4.4201e-13, 5.2481e-13);
    vec3 pos = vec3(1.6810e+06, 1.7953e+06, 2.2084e+06);
    vec3 var = vec3(4.3278e+09, 9.3046e+09, 6.6121e+09);
    vec3 xyz = val * sqrt(2.0 * kPi * var) * cos(pos * phase + shift) * exp(-var * phase * phase);
    xyz.x += 9.7470e-14 * sqrt(2.0 * kPi * 4.5282e+09) * cos(2.2399e+06 * phase + shift.x) * exp(-4.5282e+09 * phase * phase);
    return xyz / 1.0685e-7;
}

vec3 XyzToLinearRec709(vec3 xyz)
{
    return vec3( 3.2404542 * xyz.x - 1.5371385 * xyz.y - 0.4985314 * xyz.z,
                -0.9692660 * xyz.x + 1.8760108 * xyz.y + 0.0415560 * xyz.z,
                 0.0556434 * xyz.x - 0.2040259 * xyz.y + 1.0572252 * xyz.z);
}

// baseReflectance = what the substrate would reflect at the refracted angle without the film.
vec3 ThinFilmReflectance(float mu, float filmIor, float thicknessMicrons, vec3 baseReflectance, bool basePhasePi)
{
    float dinc = thicknessMicrons * 1000.0;                            // nm
    float eta2 = mix(1.0, filmIor, smoothstep(0.0, 0.03, thicknessMicrons));   // film → air as it vanishes
    float sinT2 = (1.0 - mu * mu) / (eta2 * eta2);
    if (sinT2 >= 1.0) return vec3(1.0);
    float muT = sqrt(1.0 - sinT2);
    float r12 = FresnelDielectric(mu, eta2);
    float t121 = 1.0 - r12;
    float phi21 = kPi;                                                 // reflection inside the film off the top (n2 > n1)
    vec3  r23 = clamp(baseReflectance, vec3(0.0), vec3(1.0));
    float phi23 = basePhasePi ? kPi : 0.0;
    float opd = 2.0 * eta2 * dinc * muT;
    vec3 phi2 = vec3(phi21 + phi23);
    vec3 r123 = r12 * r23;
    vec3 rr   = sqrt(r123);
    vec3 rs   = t121 * t121 * r23 / (vec3(1.0) - r123);
    vec3 c0   = vec3(r12) + rs;
    vec3 i    = c0;
    vec3 cm   = rs - vec3(t121);
    for (int m = 1; m <= 2; ++m)
    {
        cm *= rr;
        vec3 sm = 2.0 * ThinFilmSensitivity(float(m) * opd, float(m) * phi2);
        i += cm * sm;
    }
    return clamp(XyzToLinearRec709(i), vec3(0.0), vec3(1.0));
}

//------------------------------------------------------------------------------------------------------------------------
//                                              GGX (anisotropic, height-correlated Smith)
//------------------------------------------------------------------------------------------------------------------------
// OpenPBR §4: α_t = r² (1 + a) / √(1 + a²)·…  — we use the Kulla–Conty parametrisation α_t = r²·√(2/(1+(1−a)²)), α_b = (1−a)·α_t.

vec2 AnisotropicAlpha(float roughness, float anisotropy)
{
    float alpha = max(roughness * roughness, kMinAlpha);
    float at = alpha * sqrt(2.0 / (1.0 + (1.0 - anisotropy) * (1.0 - anisotropy)));
    return vec2(max(at, kMinAlpha), max((1.0 - anisotropy) * at, kMinAlpha));
}

float GgxD(vec3 h, vec2 a)
{
    float hx = h.x / a.x, hy = h.y / a.y;
    float d = hx * hx + hy * hy + h.z * h.z;
    return 1.0 / (kPi * a.x * a.y * d * d);
}

float GgxLambda(vec3 w, vec2 a)
{
    float ax = w.x * a.x, ay = w.y * a.y;
    return 0.5 * (-1.0 + sqrt(1.0 + (ax * ax + ay * ay) / max(w.z * w.z, 1.0e-12)));
}

float GgxG1(vec3 w, vec2 a) { return 1.0 / (1.0 + GgxLambda(w, a)); }
float GgxG2(vec3 wo, vec3 wi, vec2 a) { return 1.0 / (1.0 + GgxLambda(wo, a) + GgxLambda(wi, a)); }

// Dupuy & Benyoub 2023, "Sampling Visible GGX Normals with Spherical Caps" (listing 3): exact VNDF sample.
vec3 SampleGgxVndf(vec3 wo, vec2 a, vec2 u)
{
    vec3 wh = normalize(vec3(a.x * wo.x, a.y * wo.y, wo.z));           // stretch to the unit hemisphere
    float phi = 2.0 * kPi * u.x;
    float z = (1.0 - u.y) * (1.0 + wh.z) - wh.z;                       // spherical cap: z ∈ [−wh.z, 1]
    float s = sqrt(max(0.0, 1.0 - z * z));
    vec3 c = vec3(s * cos(phi), s * sin(phi), z);
    vec3 m = c + wh;                                                   // half-vector in the stretched space
    return normalize(vec3(a.x * m.x, a.y * m.y, max(0.0, m.z)));      // unstretch
}

float GgxVndfPdf(vec3 wo, vec3 h, vec2 a)
{
    float g1 = GgxG1(wo, a);
    return g1 * max(0.0, dot(wo, h)) * GgxD(h, a) / max(wo.z, kMinCos);
}

// Kulla–Conty 2017 multiple-scattering term (reciprocal, closes the white furnace exactly for F0 = 1):
//    f_ms = F_ms · (1 − E(μo)) (1 − E(μi)) / (π (1 − E_avg)),  F_ms = F_avg² E_avg / (1 − F_avg (1 − E_avg)),  F_avg = F0 + (F90 − F0)/21 (Schlick).
//    F90 = saturate(50 F0) (Lagarde 2014) so a vanishing F0 (specular_weight → 0, η → 1) removes the lobe entirely.
//    Anisotropy uses α = √(αx αy) for the table lookup (standard approximation; loses accuracy above anisotropy ≈ 0.6).
float GgxF90(vec3 f0) { return clamp(50.0 * dot(f0, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0); }

vec3 GgxMultiScatterFresnel(vec3 f0, float eAvg)
{
    vec3 fAvg = f0 + (vec3(GgxF90(f0)) - f0) * (1.0 / 21.0);
    return fAvg * fAvg * eAvg / max(vec3(1.0) - fAvg * (1.0 - eAvg), vec3(1.0e-4));
}

vec3 GgxMultiScatter(vec3 f0, float muO, float muI, vec2 a)   // f_ms (not cosine weighted)
{
    float alpha = clamp(sqrt(a.x * a.y), 0.0, 1.0);
    vec3 eo = FetchEnergy(clamp(muO, 0.0, 1.0), alpha);
    vec3 ei = FetchEnergy(clamp(muI, 0.0, 1.0), alpha);
    float eO = eo.x + eo.y, eI = ei.x + ei.y, eAvg = eo.z;
    return GgxMultiScatterFresnel(f0, eAvg) * ((1.0 - eO) * (1.0 - eI) / (kPi * max(1.0 - eAvg, 1.0e-4)));
}

// Directional albedo of the GGX lobe (single + multiple scattering): F0·A + B + F_ms (1 − E).  Exact for Schlick Fresnel.
vec3 GgxDirectionalAlbedo(vec3 f0, float muO, vec2 a)
{
    float alpha = clamp(sqrt(a.x * a.y), 0.0, 1.0);
    vec3 e = FetchEnergy(clamp(muO, 0.0, 1.0), alpha);
    float eSs = e.x + e.y;
    return f0 * e.x + vec3(e.y * GgxF90(f0)) + GgxMultiScatterFresnel(f0, e.z) * (1.0 - eSs);
}

//------------------------------------------------------------------------------------------------------------------------
//                                          TRANSMISSION (M4: Walter-2007 GGX BTDF)
//------------------------------------------------------------------------------------------------------------------------
// Single-interface microfacet transmission + the thin-wall compound (rough entry, flat exit). Directions are unit
// outward-pointing local vectors; media IORs ride along explicitly so the η² reciprocity proof can swap them.
// Achromatic throughout — absorption (Beer) is applied by the caller from ResolvedLayers. No `out` params anywhere:
// like the rest of this file, multi-value returns ride in vec4s so the CPU port compiles.

// Snell refraction with an explicit TIR flag. I = incident TRAVEL direction (unit, dot(I, N) < 0), N = interface
// normal (unit, opposing I), eta = n_incident / n_transmitted. xyz = transmitted travel direction (unit when w ≥ 0,
// meaningless when w < 0 — the caller MUST check); w = k = 1 − η²(1 − cos²θi), negative ⟺ total internal reflection.
vec4 RefractDielectric(vec3 I, vec3 N, float eta)
{
    float cosI = dot(I, N);
    float k = 1.0 - eta * eta * (1.0 - cosI * cosI);
    if (k < 0.0) return vec4(0.0, 0.0, 0.0, k);
    return vec4(I * eta - N * (eta * cosI + sqrt(max(k, 0.0))), k);
}

// M4: conditioned dielectric Fresnel for transmission. FresnelDielectric evaluated from the DENSE side goes
// ill-conditioned near-TIR (μT = sqrt of a cancelled subtraction → 1e-3 absolute F noise → 10 % η² violation),
// so the VALUE always comes from the RARE side (entering denser: μT ≥ sqrt(1 − 1/eta²) > 0, bounded away from 0)
// while TIR is detected from the dense side by comparison only (exact, no sqrt). Equal to FresnelDielectric
// wherever the latter is well-conditioned (Stokes), exact at TIR — and bit-identical under media swap, which is
// what makes the η² proof tight. muO/muI = |cos| on wo's / wi's side.
float TransmissionFresnel(float muO, float muI, float etaO, float etaI)
{
    float eta = (etaO <= etaI) ? (etaI / etaO) : (etaO / etaI);   // η_dense / η_rare ≥ 1
    float muRare = (etaO <= etaI) ? muO : muI;
    float muDense = (etaO <= etaI) ? muI : muO;
    if ((1.0 - muDense * muDense) * eta * eta >= 1.0) return 1.0;   // dense-side TIR, comparison-only
    return FresnelDielectric(muRare, eta);
}

// Walter-2007 §5 transmission half-vector: ht = −(ηo·wo + ηi·wi)/‖…‖, z-flipped positive (D is z-symmetric and
// every signed use enters squared, so the flip is free). w = ‖…‖² before normalisation; w ≈ 0 (an antipodal
// η-balanced pair — only reachable at ηo = ηi, where the interface vanishes anyway) ⟺ degenerate, and every
// caller returns 0 there.
vec4 TransmissionHalfVector(vec3 wo, vec3 wi, float etaO, float etaI)
{
    vec3 h = etaO * wo + etaI * wi;
    float l2 = dot(h, h);
    if (l2 < 1.0e-12) return vec4(0.0, 0.0, 1.0, 0.0);
    vec3 ht = -(h / sqrt(l2));
    if (ht.z < 0.0) ht = -ht;
    return vec4(ht, l2);
}

// EXACT single-interface BTDF (Walter-2007 eq. 21): f_t = |wo·m||wi·m|/(|coso||cosi|) · ηi²(1−F)·G₂·D/d² with
// d = ηo(wo·m) + ηi(wi·m) and m = transmission half-vector. Masking is SEPARABLE Smith (G₁(wo)·G₁(wi), Walter-2007),
// not the height-correlated form shared with reflection: opposite-side rays traverse independent height ranges
// ([h,∞) above vs (−∞,h] below), so the joint probability factorises — height-correlation assumes same-side bundles
// and over-counts T by ~8 % at rough-oblique (furnace-measured). GxLambda reads w.z², so G₁ extends below unchanged.
// Single-scatter macro closure is R_ss+T_ss = E_ss(F0 = 1) — the missing 1 − E_ss is multiple scattering, which exits
// glass mostly as T (unmodelled T_ms gap, ≤ 4 % at rough-oblique, < 1 % typical); the R-side Kulla–Conty term is
// scaled by (1 − wT) so it never double-counts it. F is TransmissionFresnel (conditioned, TIR-exact): TIR pairs —
// beyond-critical internal rays — get F = 1, T = 0. etaO/etaI = IOR of wo's / wi's medium.
// NOTE: no |ηo − ηi| early-out — etaO = etaI = 1 is the valid straight-through film limit (specular_weight 0),
// and exact-degenerate (antipodal + equal-eta) pairs are measure-zero; the hv.w guard below catches them.
vec3 TransmissionEvaluateSingle(vec3 wo, vec3 wi, vec2 a, float etaO, float etaI)
{
    vec4 hv = TransmissionHalfVector(wo, wi, etaO, etaI);
    if (hv.w < 1.0e-10) return vec3(0.0);
    vec3 m = hv.xyz;
    float cosO = max(abs(wo.z), kMinCos), cosI = max(abs(wi.z), kMinCos);
    float woM = abs(dot(wo, m)), wiM = abs(dot(wi, m));
    float dnom = etaO * dot(wo, m) + etaI * dot(wi, m);
    float F = TransmissionFresnel(woM, wiM, etaO, etaI);
    float d = GgxD(m, a);
    float g = GgxG1(wo, a) * GgxG1(wi, a);
    float j = (etaI * etaI) / (dnom * dnom + 1.0e-12);
    return vec3(woM * wiM / (cosO * cosI) * (1.0 - F) * g * d * j);
}

// Pdf of wi under T-branch VNDF sampling: p = branchProb · D_vis(m) · ηi²|wi·m|/d². branchProb = P(T | m) is passed
// IN (computed identically at sample and pdf time — see TransmissionBranchProb), so f·|cos|/p collapses to G₂/G₁
// whenever the branch and eval Fresnel agree (Stokes: equal for Snell-paired rays to float precision).
float TransmissionPdfSingle(vec3 wo, vec3 wi, vec2 a, float etaO, float etaI, float branchProb)
{
    if (branchProb <= 0.0) return 0.0;
    vec4 hv = TransmissionHalfVector(wo, wi, etaO, etaI);
    if (hv.w < 1.0e-10) return vec3(0.0).x;
    vec3 m = hv.xyz;
    float dnom = etaO * dot(wo, m) + etaI * dot(wi, m);
    float jac = (etaI * etaI) * abs(dot(wi, m)) / (dnom * dnom + 1.0e-12);
    return branchProb * GgxVndfPdf(wo, m, a) * jac;
}

// Branch probability P(T | m) — the exact Fresnel split, gated by the transmit mix: a sampled facet reflects with
// probability 1 − P and transmits with P. Sample and Pdf MUST call this same function: the pdf identity needs the
// sampling distribution, not an independent estimate.
float TransmissionBranchProb(float transmitMix, float fresnel)
{
    return transmitMix * (1.0 - fresnel);
}

// Thin-wall compound (tilted rough entry + flat exit — KHR_materials_volume semantics with a real refracted lobe):
// entry refraction at the VNDF facet bends the ray into the wall, the flat exit face unbends it toward
// straight-through (EXACTLY straight-through in the smooth limit). INVERSION (eval/pdf): the internal ray d1 is
// recovered by refracting −wi back through the flat exit face; exit-TIR ⟺ the pair is unreachable ⟺ f = p = 0 (the
// light is trapped in the wall — absorbed, so rejection sampling stays energy-consistent).
// f_thin = f_entry(wo, −d1) · (1 − F_exit) · Beer(σ·thickness/|d1.z|) / η²: power conservation across the exit —
// power multiplies by (1−F_exit)·Beer with no eta and no cosine (Fresnel/Beer are power ratios; the exit Jacobian
// cancels against the radiance→power measure cosines), leaving only the glass→air radiance drop 1/η². p_thin =
// p_entry · J with J = |dd1/dwi| = |wi.z|/(η²|d1.z|) (Snell solid-angle, étendue-checked: η²·cosD1·dd1 = cos_w·dwi —
// pdfs are densities and transform WITH the Jacobian), so the sampling weight is G₁(−d1)·(1−F_exit)·Beer ≤ 1 (the
// separable entry-G₁(wo) cancels against D_vis).
vec3 TransmissionThinWallEvaluate(vec3 wo, vec3 wi, vec2 a, float eta, vec3 sigma, float thickness)
{
    if (wi.z >= 0.0 || wo.z <= 0.0) return vec3(0.0);
    float etaC = max(eta, 1.0001);   // specular_weight → 0 collapses η to 1 (tinted film): keep the lobe finite
    vec4 back = RefractDielectric(-wi, vec3(0.0, 0.0, -1.0), 1.0 / etaC);   // inverse exit: −wi (up-travel) air → glass
    if (back.w < 0.0) return vec3(0.0);
    vec3 d1 = -back.xyz;   // internal travel direction (down into the wall — the glass-side OUTWARD direction)
    float cosD1 = max(abs(d1.z), kMinCos);
    vec3 fEntry = TransmissionEvaluateSingle(wo, d1, a, 1.0, etaC);
    float fExit = FresnelDielectric(cosD1, 1.0 / etaC);
    vec3 beer = exp(-sigma * (max(thickness, 0.0) / cosD1));
    return fEntry * ((1.0 - fExit) / (etaC * etaC)) * beer;
}

float TransmissionThinWallPdf(vec3 wo, vec3 wi, vec2 a, float eta, float transmitMix)
{
    if (wi.z >= 0.0 || wo.z <= 0.0 || transmitMix <= 0.0) return 0.0;
    float etaC = max(eta, 1.0001);
    vec4 back = RefractDielectric(-wi, vec3(0.0, 0.0, -1.0), 1.0 / etaC);
    if (back.w < 0.0) return 0.0;
    vec3 d1 = -back.xyz;
    vec4 hv = TransmissionHalfVector(wo, d1, 1.0, etaC);   // d1 (down-outward), NOT −d1: ht takes both sides outward
    if (hv.w < 1.0e-10) return 0.0;
    float F = FresnelDielectric(abs(dot(wo, hv.xyz)), etaC);   // air side, like the sampler — NOT the eval side
    float branchProb = TransmissionBranchProb(transmitMix, F);
    if (branchProb <= 0.0) return 0.0;
    float cosD1 = max(abs(d1.z), kMinCos), cosI = max(abs(wi.z), kMinCos);
    float jac = cosI / (etaC * etaC * cosD1);
    return TransmissionPdfSingle(wo, d1, a, 1.0, etaC, branchProb) * jac;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   EON DIFFUSE (Listing 1–3)
//------------------------------------------------------------------------------------------------------------------------

const float kEonConstant1 = 0.5 - 2.0 / (3.0 * kPi);
const float kEonConstant2 = 2.0 / 3.0 - 28.0 / (15.0 * kPi);

float EonAlbedoFon(float mu, float r)   // E_FON_approx
{
    float mucomp = 1.0 - mu;
    float goverPi = mucomp * (0.0571085289 + mucomp * (0.491881867 + mucomp * (-0.332181442 + mucomp * 0.0714429953)));
    return (1.0 + r * goverPi) / (1.0 + kEonConstant1 * r);
}

vec3 EonEvaluate(vec3 rho, float r, vec3 wi, vec3 wo)
{
    float muI = wi.z, muO = wo.z;
    float s = dot(wi, wo) - muI * muO;
    float sOverT = s > 0.0 ? s / max(muI, muO) : s;
    float af = 1.0 / (1.0 + kEonConstant1 * r);
    vec3 fss = (rho * kInvPi) * af * (1.0 + r * sOverT);
    float efo = EonAlbedoFon(muO, r), efi = EonAlbedoFon(muI, r);
    float avgEf = af * (1.0 + kEonConstant2 * r);
    vec3 rhoMs = (rho * rho) * avgEf / (vec3(1.0) - rho * (1.0 - avgEf));
    const float eps = 1.0e-7;
    vec3 fms = (rhoMs * kInvPi) * max(eps, 1.0 - efo) * max(eps, 1.0 - efi) / max(eps, 1.0 - avgEf);
    return fss + fms;
}

vec3 EonAlbedo(vec3 rho, float r, float mu)   // E_EON directional albedo
{
    float af = 1.0 / (1.0 + kEonConstant1 * r);
    float ef = EonAlbedoFon(mu, r);
    float avgEf = af * (1.0 + kEonConstant2 * r);
    vec3 rhoMs = (rho * rho) * avgEf / (vec3(1.0) - rho * (1.0 - avgEf));
    return rho * ef + rhoMs * (1.0 - ef);
}

vec4 EonLtcCoefficients(float mu, float r)   // (a, b, c, d) of Listing 2
{
    vec4 k;
    k.x = 1.0 + r * (0.303392 + (-0.518982 + 0.111709 * mu) * mu + (-0.276266 + 0.335918 * mu) * r);
    k.y = r * (-1.16407 + 1.15859 * mu + (0.150815 - 0.150105 * mu) * r) / (mu * mu * mu - 1.43545);
    k.z = 1.0 + r * (0.20013 + (-0.506373 + 0.261777 * mu) * mu);
    k.w = r * (0.540852 + (-1.01625 + 0.475392 * mu) * mu) / (-1.0743 + (0.0725628 + mu) * mu);
    return k;
}

mat3 OrthonormalBasisLtc(vec3 w)
{
    float lenSqr = dot(w.xy, w.xy);
    vec3 x = lenSqr > 0.0 ? vec3(w.x, w.y, 0.0) * inversesqrt(lenSqr) : vec3(1.0, 0.0, 0.0);
    vec3 y = vec3(-x.y, x.x, 0.0);
    return mat3(x, y, vec3(0.0, 0.0, 1.0));
}

vec4 EonSampleCltc(vec3 wo, float r, float u1, float u2)   // xyz = wi (local), w = pdf
{
    vec4 k = EonLtcCoefficients(wo.z, r); float a = k.x, b = k.y, c = k.z, d = k.w;
    float rad = sqrt(u1); float phi = 2.0 * kPi * u2;
    float x = rad * cos(phi); float y = rad * sin(phi);
    float vz = 1.0 / sqrt(d * d + 1.0);
    float s = 0.5 * (1.0 + vz);
    x = -mix(sqrt(1.0 - y * y), x, s);
    vec3 wh = vec3(x, y, sqrt(max(1.0 - (x * x + y * y), 0.0)));
    float pdfWh = wh.z / (kPi * s);
    vec3 wi = vec3(a * wh.x + b * wh.z, c * wh.y, d * wh.x + wh.z);
    float len = length(wi);
    float detM = c * (a - b * d);
    float pdfWi = pdfWh * len * len * len / detM;
    mat3 fromLtc = OrthonormalBasisLtc(wo);
    wi = normalize(fromLtc * wi);
    return vec4(wi, pdfWi);
}

float EonPdfCltc(vec3 wo, vec3 wi, float r)
{
    mat3 toLtc = transpose(OrthonormalBasisLtc(wo));
    vec3 w = toLtc * wi;
    vec4 k = EonLtcCoefficients(wo.z, r); float a = k.x, b = k.y, c = k.z, d = k.w;
    float detM = c * (a - b * d);
    vec3 wh = vec3(c * (w.x - b * w.z), (a - b * d) * w.y, -c * (d * w.x - a * w.z));
    float lenSqr = dot(wh, wh);
    float vz = 1.0 / sqrt(d * d + 1.0);
    float s = 0.5 * (1.0 + vz);
    return detM * detM / (lenSqr * lenSqr) * max(wh.z, 0.0) / (kPi * s);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              LTC SHEEN (Zeltner, Burley, Chiang 2022)
//------------------------------------------------------------------------------------------------------------------------
// Table entry (aInv, bInv, R) at [α][cosθo]. Cosine-weighted BRDF value = R · D_o(M⁻¹ωi/‖‖)·|M⁻¹|/‖M⁻¹ωi‖³ (pbrt-v3 reference).

float SheenLtcEvaluateCos(vec3 wi, vec3 coeffs)   // returns f · cosθi
{
    float aInv = coeffs.x, bInv = coeffs.y;
    vec3 w = vec3(aInv * wi.x + bInv * wi.z, aInv * wi.y, wi.z);
    float len = length(w);
    if (len <= 0.0) return 0.0;
    w /= len;
    float jacobian = (aInv * aInv) / (len * len * len);
    return max(w.z, 0.0) * kInvPi * jacobian;
}

// f (no cosine); wo and wi in the local frame.
vec3 SheenEvaluate(vec3 fuzzColor, float alpha, vec3 wo, vec3 wi)
{
    if (wo.z <= 0.0 || wi.z <= 0.0) return vec3(0.0);
    mat3 toLtc = transpose(OrthonormalBasisLtc(wo));
    vec3 w = toLtc * wi;
    vec3 coeffs = FetchSheen(wo.z, alpha);
    float value = SheenLtcEvaluateCos(w, coeffs);
    return fuzzColor * coeffs.z * value / max(wi.z, kMinCos);
}

float SheenAlbedo(float alpha, float mu) { return FetchSheen(clamp(mu, 0.0, 1.0), alpha).z; }

vec4 SheenSample(float alpha, vec3 wo, vec2 u)   // xyz = wi, w = pdf (= cosine-weighted LTC density)
{
    vec3 coeffs = FetchSheen(wo.z, alpha);
    float rad = sqrt(u.x); float phi = 2.0 * kPi * u.y;
    vec3 wh = vec3(rad * cos(phi), rad * sin(phi), sqrt(max(0.0, 1.0 - u.x)));
    float aInv = coeffs.x, bInv = coeffs.y;
    vec3 w = normalize(vec3(wh.x / aInv - wh.z * bInv / aInv, wh.y / aInv, wh.z));
    float pdf = SheenLtcEvaluateCos(w, coeffs);
    mat3 fromLtc = OrthonormalBasisLtc(wo);
    return vec4(normalize(fromLtc * w), pdf);
}

float SheenPdf(float alpha, vec3 wo, vec3 wi)
{
    mat3 toLtc = transpose(OrthonormalBasisLtc(wo));
    return SheenLtcEvaluateCos(toLtc * wi, FetchSheen(wo.z, alpha));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 LAYER BOOK-KEEPING
//------------------------------------------------------------------------------------------------------------------------

struct LobeWeights
{
    float Fuzz;         // probability mass of the fuzz lobe
    float Coat;         // … coat GGX
    float Specular;     // … base GGX (dielectric + metal, incl. haziness)
    float Sss;          // M5 v2: … the dipole branch (pure-SSS below-sampling; 0 for opaque AND mixed — exclusivity)
    float Diffuse;      // … EON
};

struct ResolvedLayers   // per-(record, wo) constants shared by Evaluate / Sample / Pdf
{
    vec2  SpecularAlpha;      // after coat roughening
    vec2  HazeAlpha;
    vec2  CoatAlpha;
    mat3  AnisoBasis;         // M2: local → aniso space (z-rotation; exact identity at angle 0)
    mat3  CoatBasis;          // M2: local → coat space (orthonormal; exact identity when unbound)
    float SpecularEta;        // modulated by specular_weight, coat presence
    float CoatEta;
    vec3  MetalF0;
    vec3  DielectricF0;       // (η−1)²/(η+1)² × specular_color
    float CoatF0;
    float CoatFresnelO;       // F_coat(μo)
    float CoatAlbedoO;        // directional albedo of the coat lobe (for the albedo-scaling layer)
    vec3  CoatAbsorptionO;    // T^(1/μto) for the outgoing leg
    vec3  BaseDarkening;      // lerp(1, Δ, C·δ)
    float FuzzAlbedoO;        // R(α, μo) × weight (× SheenRescale under Cloth)
    float SheenRescale;       // M3: E_charlie/R albedo correction (Cloth only; 1.0 otherwise — non-cloth bit-identical)
    vec3  DielectricAlbedoO;  // E_spec(μo) for the diffuse albedo scaling
    vec3  TransmissionSigma;  // M4: σ = −ln(color)/depth (0 when depth ≤ 0 or opaque)
    bool  SolidInterface;     // M4b: bare single interface at this boundary (tracer walks the medium; Beer is tracer-side)
    float IncidentIor;        // M4b: IOR of the incident-side medium (1.0 air outside; SpecularEta inside solid glass)
    float TransmitMix;        // M4: transmission_weight·(1 − metalness) — the T-branch gate
    float TransmitMacroT;     // M4: (1 − F_avg(η))·Beer(thickness) — macro transmittance (sampling mass)
    float SssMix;             // M5: subsurface_weight·(1 − metalness) — the SSS arm gate + diffuse partition factor
    LobeWeights Weights;
};

float CoatRoughen(float r, float rc, float c)
{
    float r4 = r * r * r * r, rc4 = rc * rc * rc * rc;
    return mix(r, min(1.0, pow(r4 + 2.0 * rc4, 0.25)), c);
}

vec3 CoatAbsorption(vec3 coatColor, float mu, float eta)   // T^(1/μt) for one leg, T = sqrt(coat_color)
{
    float sinT2 = (1.0 - mu * mu) / (eta * eta);
    float muT = sqrt(max(1.0e-4, 1.0 - sinT2));
    return pow(max(coatColor, vec3(1.0e-6)), vec3(0.5 / muT));
}

// M5 v2: Christensen–Burley dipole slab transport — the normalised profile's complementary CDF at the chord:
// T(t) = ¼e^{−t/d} + ¾e^{−t/3d} (1 − cdf(r) of R(r) = (e^{−r/d} + e^{−r/3d})/(8πdr); the profile integrates to 1
// over the plane, so its CCDF is 1 at t = 0 (foil-transparent) and 0 at t = +∞). Per-channel diffusion length
// d = r·s. d ≤ 0 is opaque (no transport without a scattering length, even at t = 0: the r = 0 limit is the
// Lambert surface, not the foil). A missed chord (tracer found no exit) passes t = +∞ and attenuates to
// exactly 0 — the ray crossed the whole volume. Lives up here: the resolve needs it for the dipole mass.
vec3 SssDipoleTransport(float thickness, float radius, vec3 scale)
{
    vec3 dd = radius * scale;
    vec3 trans;
    trans.x = dd.x > 0.0 ? 0.25 * exp(-thickness / dd.x) + 0.75 * exp(-thickness / dd.x / 3.0) : 0.0;
    trans.y = dd.y > 0.0 ? 0.25 * exp(-thickness / dd.y) + 0.75 * exp(-thickness / dd.y / 3.0) : 0.0;
    trans.z = dd.z > 0.0 ? 0.25 * exp(-thickness / dd.z) + 0.75 * exp(-thickness / dd.z / 3.0) : 0.0;
    return trans;
}

ResolvedLayers ResolveLayers(ShadingRecord m, vec3 wo)
{
    ResolvedLayers L;
    float muO = max(wo.z, kMinCos);
    float c = m.CoatWeight;
    // M3: the Cloth path is parameter forcing, not a second evaluator — EON diffuse + LTC sheen (primary) + a weak
    // dielectric GGX (F0 = specular_color × f0(η) ≈ 4 %, the "low weight"), GGX-aniso suppressed by selection. Coat /
    // haze / metal / specular-weight are structurally 0 here (DeriveReflectance excludes them from Cloth), so their
    // guarded blocks skip on their own; only the eta modulation, the aniso frame and the sheen albedo need forcing.
    bool cloth = (m.Selection == kReflectanceCloth);

    // Coat: IOR relative to air; base sees lerp(n_b/n_a, n_b/n_c, C) — OpenPBR eq. 60 with the TIR inversion (eq. 73).
    L.CoatEta = max(m.CoatIor, 1.0001);
    float etaBaseAir  = max(m.SpecularIor, 1.0001);
    float etaBaseCoat = etaBaseAir / L.CoatEta;
    if (etaBaseCoat < 1.0) etaBaseCoat = 1.0 / etaBaseCoat;
    float etaBase = mix(etaBaseAir, etaBaseCoat, c);
    // Cloth keeps the unmodulated dielectric eta (weight 0 would collapse it to 1.0): the weak lobe's F0 is the true
    // dielectric f0(η) ≈ 0.04 × specular_color, MS-compensated like any GGX (Sultan-18 §9: wherever GGX is).
    L.SpecularEta = cloth ? etaBase : ModulatedIor(etaBase, m.SpecularWeight);
    float f0Dielectric = (L.SpecularEta - 1.0) / (L.SpecularEta + 1.0); f0Dielectric *= f0Dielectric;
    L.DielectricF0 = m.SpecularColor * f0Dielectric;
    L.MetalF0 = m.BaseColor;
    L.CoatF0 = (L.CoatEta - 1.0) / (L.CoatEta + 1.0); L.CoatF0 *= L.CoatF0;

    // Roughness after coat roughening (eq. 72) — base only; the coat keeps its own.
    float rSpec = CoatRoughen(m.SpecularRoughness, m.CoatRoughness, c);
    float anisoEff = cloth ? 0.0 : m.SpecularAnisotropy;   // Cloth doesn't consume ch 9/10: the weak lobe is isotropic
    L.SpecularAlpha = AnisotropicAlpha(rSpec, anisoEff);
    L.HazeAlpha     = AnisotropicAlpha(CoatRoughen(max(m.HazinessRoughness, m.SpecularRoughness), m.CoatRoughness, c), anisoEff);
    L.CoatAlpha     = AnisotropicAlpha(m.CoatRoughness, m.CoatAnisotropy);

    // M2: lobe frames. The aniso basis is a pure z-rotation; the coat basis is built from the resolve-provided
    // orthonormal pair (bitangent = N × T). Both are exact identities in the default case, so pre-M2 scenes are bit-stable.
    float angEff = cloth ? 0.0 : m.AnisotropyAngle;   // exact identity (cos0 = 1, sin0 = +0), like the default case
    float ca = cos(angEff), sa = sin(angEff);
    L.AnisoBasis = mat3(vec3(ca, -sa, 0.0), vec3(sa, ca, 0.0), vec3(0.0, 0.0, 1.0));
    vec3 coatBitangent = cross(m.CoatNormal, m.CoatTangent);
    L.CoatBasis = mat3(vec3(m.CoatTangent.x, coatBitangent.x, m.CoatNormal.x),
                       vec3(m.CoatTangent.y, coatBitangent.y, m.CoatNormal.y),
                       vec3(m.CoatTangent.z, coatBitangent.z, m.CoatNormal.z));

    // Coat Fresnel / albedo for the layer operator (§3.10 albedo scaling, non-reciprocal form) — in coat space, since
    // the coat layer is traversed along the coat normal. Identical to base space for the identity frame.
    float muOC = clamp((L.CoatBasis * wo).z, kMinCos, 1.0);
    L.CoatFresnelO = FresnelDielectric(muOC, L.CoatEta);
    L.CoatAlbedoO = min(c * GgxDirectionalAlbedo(vec3(L.CoatF0), muOC, L.CoatAlpha).x, c);
    L.CoatAbsorptionO = mix(vec3(1.0), CoatAbsorption(m.CoatColor, muOC, L.CoatEta), c);

    // Coat darkening (eq. 63–68): Δ = (1 − K) / (1 − Eb K), K = lerp(Ks, Kr, r_b).
    float ef  = FresnelDielectricAverage(L.CoatEta);
    float kr  = 1.0 - (1.0 - ef) / (L.CoatEta * L.CoatEta);
    float ks  = L.CoatFresnelO;
    float xiF = clamp(m.SpecularWeight * f0Dielectric, 0.0, 1.0);
    float rd  = mix(1.0, m.SpecularRoughness, xiF);
    float rb  = mix(rd, m.SpecularRoughness, m.Metalness);
    float k   = mix(ks, kr, rb);
    vec3  eb  = mix(m.BaseColor * (1.0 - f0Dielectric) + vec3(f0Dielectric), m.BaseColor, m.Metalness);   // normal-incidence base albedo estimate
    vec3  delta = (1.0 - k) / max(vec3(1.0) - eb * k, vec3(1.0e-4));
    L.BaseDarkening = mix(vec3(1.0), delta, c * m.CoatDarkening);

    // Fuzz on top of everything: albedo R(α, μo) scales the layers below by (1 − F·R).
    L.FuzzAlbedoO = m.FuzzWeight * SheenAlbedo(clamp(m.FuzzRoughness, 0.0, 1.0), muO);
    // Cloth: pull the sheen lobe's albedo toward the true Charlie value E_c (.w). The LTC fit diverges from Charlie
    // by up to ~80× where it collapses (low α: R → 0 while E_c stays O(1)), so the correction is clamped to [0.5, 2] —
    // always TOWARD E_c, never past it — and the layer below scales by the same rescaled value the lobe evaluates to,
    // which keeps rescale·R ≤ 1 (hence the stack ≤ 1) for every texel AND every bilinear interpolation of them.
    L.SheenRescale = 1.0;
    if (cloth && m.FuzzWeight > 0.0)
    {
        vec4 sheenTex = FetchSheenFull(muO, clamp(m.FuzzRoughness, 0.0, 1.0));
        L.SheenRescale = clamp(sheenTex.w / max(sheenTex.z, 1.0e-4), 0.5, 2.0);
        L.FuzzAlbedoO = m.FuzzWeight * L.SheenRescale * sheenTex.z;
    }

    // Dielectric specular albedo (for the diffuse albedo scaling, eq. 42): split-sum single scatter + Kulla–Conty term.
    vec3 eMain = GgxDirectionalAlbedo(L.DielectricF0, muO, L.SpecularAlpha);
    if (m.HazinessWeight > 0.0) eMain = mix(eMain, GgxDirectionalAlbedo(L.DielectricF0, muO, L.HazeAlpha), m.HazinessWeight);
    L.DielectricAlbedoO = min(vec3(1.0), eMain);

    // M4: transmission resolve. σ from the KHR volume pair (attenuation colour AT attenuation distance); the thin
    // wall's macro transmittance (mean-σ Beer over the nominal thickness, Fresnel-averaged) sizes the T sampling
    // mass. specular_weight needs NO extra gate: it already modulates η (ModulatedIor), so weight → 0 collapses the
    // interface to a straight-through tinted film — physically coherent, no special case.
    L.TransmitMix = m.TransmissionWeight * (1.0 - m.Metalness);
    L.SssMix = m.SssWeight * (1.0 - m.Metalness);   // M5: metals don't scatter below (mirror of the T gate)
    L.SolidInterface = false;   // M4b: tracers opt in per hit (foil is the safe default for stack-less callers)
    L.IncidentIor = 1.0;
    L.TransmissionSigma = vec3(0.0);
    if (m.TransmissionWeight > 0.0 && m.TransmissionDepth > 0.0)
        L.TransmissionSigma = -log(clamp(m.TransmissionColor, vec3(1.0e-6), vec3(1.0))) / m.TransmissionDepth;
    float beerMacro = exp(-(L.TransmissionSigma.x + L.TransmissionSigma.y + L.TransmissionSigma.z) * (1.0 / 3.0) * max(m.TransmissionThickness, 0.0));
    L.TransmitMacroT = (1.0 - FresnelDielectricAverage(L.SpecularEta)) * beerMacro;

    // Lobe selection probabilities from the same albedo estimates (never zero for an enabled lobe).
    float underFuzz = 1.0 - L.FuzzAlbedoO;
    float underCoat = underFuzz * (1.0 - L.CoatAlbedoO);
    float lumMetal  = dot(L.MetalF0, vec3(0.2126, 0.7152, 0.0722));
    float lumDiel   = dot(L.DielectricAlbedoO, vec3(0.2126, 0.7152, 0.0722));
    float transMass = underCoat * L.TransmitMix * L.TransmitMacroT;   // M4: without its own mass, glass is never sampled (diffuse vanishes at weight 1, R mass ≈ F0)
    float specMass  = underCoat * mix(lumDiel, min(1.0, lumMetal + 0.04), m.Metalness) + transMass;
    float diffMass  = underCoat * (1.0 - m.Metalness) * (1.0 - L.TransmitMix) * (1.0 - L.SssMix) * (1.0 - lumDiel) * dot(EonAlbedo(m.BaseColor, m.DiffuseRoughness, muO), vec3(0.2126, 0.7152, 0.0722));   // M5: sampling mass follows the eval partition (SssMix 0 ⟹ ×1.0, bit-identical)
    // M5 v2: the dipole branch owns below-sampling for pure-SSS mats — mixed T+SSS keeps the T arm owning below
    // (the dipole eval rides along in f, no flag needed: exact-zero TransmitMix is the switch). Mass ∝ the
    // transmitted energy (mix·ρ·T, luminance); the 0.02 floor (SSS-on only — 0 at SssMix 0 keeps opaque
    // bit-identical) keeps the pdf > 0 below whenever f > 0 below.
    bool sssSolo = L.SssMix > 0.0 && L.TransmitMix <= 0.0;
    vec3 sssTrans = SssDipoleTransport(m.SssThickness, m.SssRadius, m.SssRadiusScale);
    float sssMass = sssSolo ? underCoat * L.SssMix * dot(m.SssColor * sssTrans, vec3(0.2126, 0.7152, 0.0722)) : 0.0;
    LobeWeights w;
    w.Fuzz = L.FuzzAlbedoO; w.Coat = underFuzz * L.CoatAlbedoO; w.Specular = max(specMass, 0.02 * underCoat); w.Diffuse = max(diffMass, (1.0 - m.Metalness) * 0.02 * underCoat);
    w.Sss = sssSolo ? max(sssMass, 0.02 * underCoat) : 0.0;
    float total = w.Fuzz + w.Coat + w.Specular + w.Sss + w.Diffuse;
    w.Fuzz /= total; w.Coat /= total; w.Specular /= total; w.Sss /= total; w.Diffuse /= total;
    L.Weights = w;
    return L;
}

//------------------------------------------------------------------------------------------------------------------------
// M4b: media pair for the T interface (lives here: after ResolvedLayers for the CPU port, before its first
// use in EvaluateBaseSpecular). Foil (default): air outside, SpecularEta within — etaI/etaO = SpecularEta/1.0
// is bitwise SpecularEta (IEEE x/1.0 = x), so every legacy call below is bit-identical. Solid: the tracer sets
// IncidentIor per hit (1.0 outside, SpecularEta inside) and SolidInterface = true; the transmitted side is the
// other one (nesting is v1-out: a transmissive hit from inside another medium shades R-only, tracer-side).
// v1 scope: entry/exit interfaces are bare glass (proven); coat / fuzz / diffuse layer terms ride along from the
// outer resolve and are approximate at interior hits (the solid exhibits are bare glass).
vec2 TransmitEtas(ResolvedLayers L)
{
    float etaO = L.IncidentIor;
    float etaI = (etaO > 1.0) ? 1.0 : L.SpecularEta;
    return vec2(etaO, etaI);
}

//                                                    EVALUATE
//------------------------------------------------------------------------------------------------------------------------

// Base specular (dielectric + metal mix, thin film, haziness) — f only.
vec3 EvaluateBaseSpecular(ShadingRecord m, ResolvedLayers L, vec3 wo, vec3 wi)
{
    vec3 woA = L.AnisoBasis * wo, wiA = L.AnisoBasis * wi;   // M2: the specular lobe lives in aniso space
    vec3 h = normalize(woA + wiA);
    float muH = max(dot(woA, h), 0.0);

    // Fresnel of the two substrates. M4b: R follows the incident medium (etaI/etaO — TIR-aware from inside glass;
    // foil: /1.0 is bitwise-neutral, so opaque + thin-wall are bit-identical).
    vec2 etasT = TransmitEtas(L);
    float fD = FresnelDielectric(muH, etasT.y / etasT.x);
    vec3  fDielectric = m.SpecularColor * fD;
    vec3  fMetal = m.SpecularWeight * FresnelF82(L.MetalF0, m.SpecularColor, muH);
    if (m.ThinFilmWeight > 0.0 && etasT.x <= 1.0)   // M4b: films live on outer surfaces (foil: etaO = 1, gate inert)
    {
        float etaFilm = max(m.ThinFilmIor, 1.0001);
        float sinT2 = (1.0 - muH * muH) / (etaFilm * etaFilm);
        float muFilm = sqrt(max(0.0, 1.0 - sinT2));
        vec3 baseD = m.SpecularColor * vec3(FresnelDielectric(muFilm, L.SpecularEta / etaFilm));
        vec3 baseM = m.SpecularWeight * FresnelF82(L.MetalF0, m.SpecularColor, muFilm);
        fDielectric = mix(fDielectric, ThinFilmReflectance(muH, etaFilm, m.ThinFilmThickness, baseD, L.SpecularEta > etaFilm), m.ThinFilmWeight);
        fMetal      = mix(fMetal,      ThinFilmReflectance(muH, etaFilm, m.ThinFilmThickness, baseM, false),                  m.ThinFilmWeight);
    }
    vec3 f = mix(fDielectric, fMetal, m.Metalness);

    // Main GGX + haze GGX (mixed by weight), each single-scatter lobe plus its Kulla–Conty multiple-scatter term.
    // M4: the MS addend scales by (1 − wT) — multiple-scattered energy inside glass exits through T instead of R, so
    // keeping the full R-side compensation would double-count it (the R+T=1 proof arbitrates; ×1 when opaque).
    vec3 f0 = mix(L.DielectricF0, m.SpecularWeight * L.MetalF0, m.Metalness);
    vec3 msScale = vec3(1.0 - L.TransmitMix);
    float dMain = GgxD(h, L.SpecularAlpha) * GgxG2(woA, wiA, L.SpecularAlpha) / (4.0 * max(woA.z, kMinCos) * max(wiA.z, kMinCos));
    vec3 spec = f * dMain + msScale * GgxMultiScatter(f0, woA.z, wiA.z, L.SpecularAlpha);
    if (m.HazinessWeight > 0.0)
    {
        float dHaze = GgxD(h, L.HazeAlpha) * GgxG2(woA, wiA, L.HazeAlpha) / (4.0 * max(woA.z, kMinCos) * max(wiA.z, kMinCos));
        spec = mix(spec, f * dHaze + msScale * GgxMultiScatter(f0, woA.z, wiA.z, L.HazeAlpha), m.HazinessWeight);
    }
    return spec;
}

// M4: full T contribution (f, no cosine) — transmitMix-scaled, haze-mixed. Called with wi.z < 0 only. Foil: the
// thin-wall compound (entry+exit at one point, Beer over the nominal thickness). Solid (M4b): the bare single
// interface — NO Beer here (the tracer applies it over the true interior segments; doubling it would double-count).
vec3 EvaluateTransmission(ShadingRecord m, ResolvedLayers L, vec3 wo, vec3 wi)
{
    if (L.SolidInterface)
    {
        vec2 etas = TransmitEtas(L);
        vec3 ts = TransmissionEvaluateSingle(wo, wi, L.SpecularAlpha, etas.x, etas.y);
        if (m.HazinessWeight > 0.0)
            ts = mix(ts, TransmissionEvaluateSingle(wo, wi, L.HazeAlpha, etas.x, etas.y), m.HazinessWeight);
        return L.TransmitMix * ts;
    }
    vec3 t = TransmissionThinWallEvaluate(wo, wi, L.SpecularAlpha, L.SpecularEta, L.TransmissionSigma, m.TransmissionThickness);
    if (m.HazinessWeight > 0.0)
        t = mix(t, TransmissionThinWallEvaluate(wo, wi, L.HazeAlpha, L.SpecularEta, L.TransmissionSigma, m.TransmissionThickness), m.HazinessWeight);
    return L.TransmitMix * t;
}

// M4: T-branch lobe pdf (unscaled — PdfBsdf multiplies by Weights.Specular, mirroring the R arm).
float PdfTransmission(ShadingRecord m, ResolvedLayers L, vec3 wo, vec3 wi)
{
    if (L.SolidInterface)   // M4b: single-interface T density (branch F at the inverted facet, relative eta — the
    {                       // sampler evaluates the same function at the same facet, so f·|cos|/p collapses exactly)
        vec2 etas = TransmitEtas(L);
        vec4 hv = TransmissionHalfVector(wo, wi, etas.x, etas.y);
        if (hv.w < 1.0e-10) return 0.0;
        float branchProb = TransmissionBranchProb(L.TransmitMix, FresnelDielectric(abs(dot(wo, hv.xyz)), etas.y / etas.x));
        float ps = TransmissionPdfSingle(wo, wi, L.SpecularAlpha, etas.x, etas.y, branchProb);
        if (m.HazinessWeight > 0.0)
            ps = mix(ps, TransmissionPdfSingle(wo, wi, L.HazeAlpha, etas.x, etas.y, branchProb), m.HazinessWeight);
        return ps;
    }
    float p = TransmissionThinWallPdf(wo, wi, L.SpecularAlpha, L.SpecularEta, L.TransmitMix);
    if (m.HazinessWeight > 0.0)
        p = mix(p, TransmissionThinWallPdf(wo, wi, L.HazeAlpha, L.SpecularEta, L.TransmitMix), m.HazinessWeight);
    return p;
}

// M5 v2: dipole backlight — view-DEPENDENT (the v1 wrap's view-independence lock is deliberately superseded).
// Dipole CCDF transport at the chord × entry wrap B(μi) = (1−μi)/2 × view-dependent exit transmission
// T(μo) = 1 − F(μo, η) (oblique exits reflect more, transmit less — the slab-correct exit through the base
// interface) × N = 6/5π: a uniform below-hemisphere backlight closes EXACTLY per view,
// E(wo) = mix·ρ·T·T_exit(μo) (the furnace asserts this, plus the front-glow value at μi = −1, μo = 1).
// Non-reciprocal like the coat albedo-scaling (excluded from the reciprocity proofs with it).
vec3 EvaluateSubsurface(ShadingRecord m, ResolvedLayers L, vec3 wo, vec3 wi)
{
    if (L.SssMix <= 0.0) return vec3(0.0);
    vec3 trans = SssDipoleTransport(m.SssThickness, m.SssRadius, m.SssRadiusScale);
    float B = 0.5 * (1.0 - wi.z);
    float exitT = 1.0 - FresnelDielectric(wo.z, m.SpecularIor);
    return L.SssMix * m.SssColor * trans * (B * (6.0 / 5.0) * kInvPi) * exitT;
}

vec3 EvaluateBsdf(ShadingRecord m, ResolvedLayers L, vec3 wo, vec3 wi)
{
    if (wo.z <= 0.0) return vec3(0.0);
    // M4: the below-horizon arm — lights behind the surface. M5: the SSS wrap joins it (same gate, same exit
    // stack — both leave through the coat/fuzz above the substrate, so the layer scales apply once to the sum).
    if (wi.z <= 0.0)
    {
        if (L.TransmitMix <= 0.0 && L.SssMix <= 0.0) return vec3(0.0);   // exact-zero gates: opaque is bit-identical
        vec3 below = vec3(0.0);
        if (L.TransmitMix > 0.0) below = EvaluateTransmission(m, L, wo, wi);
        if (L.SssMix > 0.0) below = below + EvaluateSubsurface(m, L, wo, wi);
        if (m.CoatWeight > 0.0)   // a single coat pass on exit (no darkening: Δ belongs to the base albedo)
            below = below * (1.0 - L.CoatAlbedoO) * mix(vec3(1.0), L.CoatAbsorptionO, m.CoatWeight);
        if (m.FuzzWeight > 0.0)   // the fuzz lobe itself is 0 below (SheenEvaluate gates); only the layer scales
            below = below * (1.0 - L.FuzzAlbedoO);
        return below;
    }

    // Bottom: diffuse, scaled by what the dielectric interface lets through (eq. 42, non-reciprocal albedo scaling).
    // M4: the (1 − wT) is the macro half of the exact split — transmitted light replaces diffuse (KHR behaviour).
    // M5: the (1 − wS) is the same for subsurface — scattered light replaces diffuse (OpenPBR behaviour).
    vec3 diffuse = (1.0 - m.Metalness) * (1.0 - L.TransmitMix) * (1.0 - L.SssMix) * (vec3(1.0) - L.DielectricAlbedoO) * EonEvaluate(m.BaseColor, m.DiffuseRoughness, wi, wo);
    vec3 base = diffuse + EvaluateBaseSpecular(m, L, wo, wi);

    // Coat layer on top of the base, evaluated in the coat frame (M2). A tilted coat frame can push either leg below
    // its own surface — the specular coat lobe is undefined there (zero), while the base still shows through, scaled.
    if (m.CoatWeight > 0.0)
    {
        vec3 woC = L.CoatBasis * wo, wiC = L.CoatBasis * wi;
        vec3 coat = vec3(0.0);
        if (woC.z > 0.0 && wiC.z > 0.0)
        {
            vec3 h = normalize(woC + wiC);
            float fC = FresnelDielectric(max(dot(woC, h), 0.0), L.CoatEta);
            float dC = GgxD(h, L.CoatAlpha) * GgxG2(woC, wiC, L.CoatAlpha) / (4.0 * max(woC.z, kMinCos) * max(wiC.z, kMinCos));
            coat = vec3(fC * dC) + GgxMultiScatter(vec3(L.CoatF0), woC.z, wiC.z, L.CoatAlpha);
        }
        vec3 absorptionI = CoatAbsorption(m.CoatColor, max(wiC.z, 0.0), L.CoatEta);
        vec3 under = base * L.BaseDarkening * (1.0 - L.CoatAlbedoO) * mix(vec3(1.0), L.CoatAbsorptionO * absorptionI, m.CoatWeight);
        base = m.CoatWeight * coat + under;
    }

    // Fuzz on top of the coat.
    if (m.FuzzWeight > 0.0)
    {
        vec3 fuzz = m.FuzzWeight * L.SheenRescale * SheenEvaluate(m.FuzzColor, clamp(m.FuzzRoughness, 0.0, 1.0), wo, wi);
        base = fuzz + base * (1.0 - L.FuzzAlbedoO);
    }
    return base;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SAMPLE / PDF
//------------------------------------------------------------------------------------------------------------------------

// M5 v2: cosine-weighted below-hemisphere sample (the dipole branch's sampler; pdf = |cosθ|/π — exactly the
// density PdfBsdf's SSS arm returns). Mirrors the harness CosineSample negated.
vec3 SampleCosineBelow(vec2 u)
{
    float r = sqrt(u.x);
    float phi = 2.0 * kPi * u.y;
    return vec3(r * cos(phi), r * sin(phi), -sqrt(max(0.0, 1.0 - u.x)));
}

float PdfBsdf(ShadingRecord m, ResolvedLayers L, vec3 wo, vec3 wi)
{
    if (wo.z <= 0.0) return 0.0;
    if (wi.z <= 0.0 && L.TransmitMix <= 0.0)   // below with no T arm: the dipole density, or nothing (opaque)
    {
        if (L.SssMix <= 0.0) return 0.0;   // opaque: nothing below (bit-identical)
        return L.Weights.Sss * max(-wi.z, 0.0) * kInvPi;   // M5 v2: SSS-arm-only — every other branch's
        // below-landing is killed by the sampler, so counting their mass here would bias the estimator LOW
        // (realised density ≠ reported density on the support). Mixed mats never land here (T arm owns below).
    }
    // Transmissive below the horizon: the FULL mixture. The sampler keeps R-branch samples landing below, EON
    // CLTC leakage, and tilted-coat samples (valid T-region paths with f = f_T > 0), so the pdf must carry their
    // mass to match the sampling density exactly. (Keeping vs rejecting is variance, not bias: the T-sampler alone
    // already covers the full below-hemisphere, so rejection merely wastes samples — but the pdf must mirror the
    // sampler either way, and the kept paths are free energy.)
    float pdf = L.Weights.Diffuse * EonPdfCltc(wo, wi, m.DiffuseRoughness);
    // Rotations preserve solid angle, so each lobe's pdf is evaluated in its own space (M2).
    vec3 woA = L.AnisoBasis * wo, wiA = L.AnisoBasis * wi;
    vec3 h = normalize(woA + wiA);
    float pSpec = 0.0;
    if (dot(woA, h) > 0.0)   // degenerate half-vector (wi ≈ −wo, reachable only below) carries no R density;
    {                        // above the horizon wo·h > 0 strictly, so this gate never fires there (bit-identical)
        float jac = 1.0 / (4.0 * max(dot(woA, h), kMinCos));
        pSpec = GgxVndfPdf(woA, h, L.SpecularAlpha) * jac;
        if (m.HazinessWeight > 0.0) pSpec = mix(pSpec, GgxVndfPdf(woA, h, L.HazeAlpha) * jac, m.HazinessWeight);
    }
    vec2 etasR = TransmitEtas(L);   // M4b: R-branch split follows the incident medium (foil: bitwise-neutral /1.0)
    pSpec *= 1.0 - TransmissionBranchProb(L.TransmitMix, FresnelDielectric(abs(dot(woA, h)), etasR.y / etasR.x));   // M4: R-branch probability (×1 when opaque)
    pdf += L.Weights.Specular * pSpec;
    if (wi.z <= 0.0 && L.Weights.Specular > 0.0)   // the T-arm adds its mass below (0 above by its own gate)
        pdf += L.Weights.Specular * PdfTransmission(m, L, wo, wi);
    if (L.Weights.Coat > 0.0)
    {
        vec3 woC = L.CoatBasis * wo, wiC = L.CoatBasis * wi;
        if (woC.z > 0.0 && wiC.z > 0.0)
        {
            vec3 hC = normalize(woC + wiC);
            pdf += L.Weights.Coat * GgxVndfPdf(woC, hC, L.CoatAlpha) / (4.0 * max(dot(woC, hC), kMinCos));
        }
    }
    if (L.Weights.Fuzz > 0.0) pdf += L.Weights.Fuzz * SheenPdf(clamp(m.FuzzRoughness, 0.0, 1.0), wo, wi);
    return pdf;
}

// u = 4 uniforms (lobe pick + 2D + the R/T branch pick). Returns wi in .xyz, pdf in .w (0 = failed sample).
vec4 SampleBsdf(ShadingRecord m, ResolvedLayers L, vec3 wo, vec4 u)
{
    vec3 wi;
    bool transmitted = false;
    bool sssBelow = false;   // M5 v2: the dipole branch (below-sampling for pure SSS — exempt from the kill rule)
    float pick = u.x;
    vec2 u2 = vec2(u.y, u.z);
    if (pick < L.Weights.Fuzz)
    {
        wi = SheenSample(clamp(m.FuzzRoughness, 0.0, 1.0), wo, u2).xyz;
    }
    else if (pick < L.Weights.Fuzz + L.Weights.Coat)
    {
        vec3 woC = L.CoatBasis * wo;   // M2: sample in coat space, return to base space (transpose = inverse)
        vec3 hC = SampleGgxVndf(woC, L.CoatAlpha, u2);
        wi = transpose(L.CoatBasis) * reflect(-woC, hC);
    }
    else if (pick < L.Weights.Fuzz + L.Weights.Coat + L.Weights.Specular)
    {
        float hazePick = (pick - L.Weights.Fuzz - L.Weights.Coat) / max(L.Weights.Specular, 1.0e-6);
        vec3 woA = L.AnisoBasis * wo;   // M2: sample in aniso space, return to base space
        vec3 hA = SampleGgxVndf(woA, hazePick < m.HazinessWeight ? L.HazeAlpha : L.SpecularAlpha, u2);
        // M4: microfacet-Fresnel branch selection (exact split). The aniso basis is a z-rotation, so the flat exit
        // normal survives it as +z and the double refraction runs in aniso space directly. M4b: solid mode branches
        // on the relative eta (TIR-aware: F = 1 kills the T-branch exactly) and takes ONE refraction step — the
        // tracer walks the medium from there. Foil keeps the guarded etaC path bitwise-identically.
        vec2 etasS = TransmitEtas(L);
        float etaC = max(L.SpecularEta, 1.0001);
        float fR = FresnelDielectric(abs(dot(woA, hA)), L.SolidInterface ? etasS.y / etasS.x : etaC);
        if (L.TransmitMix > 0.0 && u.w < TransmissionBranchProb(L.TransmitMix, fR))
        {
            if (L.SolidInterface)
            {
                vec4 step = RefractDielectric(-woA, hA, etasS.x / etasS.y);
                if (step.w < 0.0) return vec4(0.0);   // TIR (paranoia: the branch already killed these with F = 1)
                wi = transpose(L.AnisoBasis) * step.xyz;
                transmitted = true;
            }
            else
            {
                vec4 entry = RefractDielectric(-woA, hA, 1.0 / etaC);
                if (entry.w < 0.0) return vec4(0.0);   // unreachable air → glass (kept: guards η < 1 callers)
                vec4 exit = RefractDielectric(entry.xyz, vec3(0.0, 0.0, 1.0), etaC);
                if (exit.w < 0.0) return vec4(0.0);    // exit-TIR: trapped in the wall = absorbed (rejection is exact here)
                wi = transpose(L.AnisoBasis) * exit.xyz;
                transmitted = true;
            }
        }
        else
        {
            wi = transpose(L.AnisoBasis) * reflect(-woA, hA);
        }
    }
    else if (pick < L.Weights.Fuzz + L.Weights.Coat + L.Weights.Specular + L.Weights.Sss)
    {
        wi = SampleCosineBelow(u2);   // M5 v2: dipole branch — cosine-weighted below (the BSDF-sampled twin of
        sssBelow = true;              // the exhibit's light-sampled NEE-below; the two meet in MIS)
    }
    else
    {
        wi = EonSampleCltc(wo, m.DiffuseRoughness, u.y, u.z).xyz;
    }
    if (!transmitted && !sssBelow && wi.z <= 0.0 && L.TransmitMix <= 0.0) return vec4(0.0);   // opaque only:
    // transmissive keeps below-horizon R/EON/coat samples (valid T-region paths: f = f_T > 0, pdf = full mixture
    // > 0); M5 v2 makes the gate selection-aware — the dipole branch survives below for pure-SSS mats
    if (transmitted && wi.z >= 0.0) return vec4(0.0);   // paranoia: exit refraction guarantees T·N = −√k < 0
    return vec4(wi, PdfBsdf(m, L, wo, wi));
}

#endif // FRONTIER_MATERIAL_EVALUATION

// Automotive paint v2: finite, object/UV-locked flakes beneath an independent coat.
// Include after MaterialEvaluation.slang (uses its existing thin-film Fresnel).
// Original finite-cell approximation, NOT an implementation of DB23 binomial glints.
// All directions in the unperturbed material tangent frame; +Z is the surface normal.
// UV coordinates and derivatives are in metres. Returns BRDF, without incident cosine.
#ifndef FRONTIER_AUTOMOTIVE_FLAKE_PAINT
#define FRONTIER_AUTOMOTIVE_FLAKE_PAINT
#ifdef FRONTIER_NATIVE_FLAKES
#define AP_INPUT(T) const T&
#else
#define AP_INPUT(T) T
#endif
struct AutomotivePaintParameters {
    vec3 Pigment;
    vec3 FlakeReflectance;
    float Density;           // expected flakes per cell, [0,16]; extra independent populations above 1
    float DiameterMm;        // largest in-plane flake diameter
    float NormalSpread;     // RMS slope spread, NOT clearcoat roughness
    float FlakeRoughness;    // Beckmann slope width of an individual flake
    float CoatRoughness;    // GGX perceptual roughness
    float CoatWeight;
    vec3 CoatTint;           // artistic coating tint, independent of pigment and flake palette
    float CoatTintStrength;
    float PearlWeight;
    float FilmThicknessNm;
    float FilmIor;
    uint Seed;
};
// Up to eight weighted colour families, each with a linear-RGB endpoint range.
// Count=0 (or all weights zero) retains the legacy FlakeReflectance fallback.
struct AutomotiveFlakePalette {
    int Count;
    vec3 Minimum[8];
    vec3 Maximum[8];
    float Weight[8];
};
AutomotiveFlakePalette AutomotivePaletteDefaults() {
    AutomotiveFlakePalette palette;palette.Count=0;
    for(int i=0;i<8;++i){palette.Minimum[i]=vec3(0.72,0.77,0.82);palette.Maximum[i]=palette.Minimum[i];palette.Weight[i]=1.0;}
    return palette;
}
AutomotiveFlakePalette AutomotiveRgbPalette() {
    AutomotiveFlakePalette p=AutomotivePaletteDefaults();p.Count=3;
    p.Minimum[0]=vec3(0.42,0.006,0.004);p.Maximum[0]=vec3(0.95,0.055,0.018);
    p.Minimum[1]=vec3(0.004,0.24,0.025);p.Maximum[1]=vec3(0.035,0.8,0.15);
    p.Minimum[2]=vec3(0.003,0.025,0.35);p.Maximum[2]=vec3(0.025,0.18,0.95);
    return p;
}
struct AutomotiveFlakeSurface {
    uint FacetKey;
    vec3 Colour;
    vec3 MeanColour;
    vec2 Slope;
    float Weight;
    float MeanWeight;
    float Unresolved;
    float Alpha;
    float PopulationAlpha;
};
float APRange(float x,float lo,float hi) { return !(x>=lo)?lo:min(x,hi); }
AutomotivePaintParameters AutomotivePaintDefaults() {
    AutomotivePaintParameters p;
    p.Pigment=vec3(0.014,0.045,0.13);p.FlakeReflectance=vec3(0.72,0.77,0.82);
    p.Density=0.8;p.DiameterMm=0.35;p.NormalSpread=0.24;p.FlakeRoughness=0.065;
    p.CoatRoughness=0.23;p.CoatWeight=1.0;p.CoatTint=vec3(1.0);p.CoatTintStrength=0.0;p.PearlWeight=0.0;p.FilmThicknessNm=420.0;p.FilmIor=1.48;p.Seed=17u;
    return p;
}
uint APHash(uint x) { x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; return x^(x>>16); }
float APRandom(uint x) { return float(APHash(x)>>8)*(1.0/16777216.0); }
AutomotiveFlakeSurface AutomotivePrepareFlakes(AP_INPUT(AutomotivePaintParameters) p,vec2 uv,vec2 dx,vec2 dy) {
    AutomotiveFlakeSurface s;
    const float radius=0.36;
    float frequency=2.0*radius/(APRange(p.DiameterMm,0.02,2.0)*0.001);
    if(!(abs(uv.x)<1000.0&&abs(uv.y)<1000.0))uv=vec2(0.0);
    vec2 q=uv*frequency;float cx=float(floor(q.x)),cy=float(floor(q.y));
    float density=APRange(p.Density,0.0,16.0);
    float footprint=max(length(dx),length(dy))*frequency;
    footprint=APRange(footprint,0.0,10000.0);
    float aa=max(0.0001,min(0.5,footprint*0.6));
    s.Weight=0.0;uint key=0u;
    // Full-cell jitter with a fixed 3x3 neighbourhood, rather than visible centred dots.
    // Overlapping flakes select the topmost (largest coverage) facet; no additive energy.
    for(int layer=0;layer<16;++layer){
      float occupancy=clamp(density-float(layer),0.0,1.0);
      if(occupancy<=0.0)break;
      for(int oy=-1;oy<=1;++oy)for(int ox=-1;ox<=1;++ox){
        float nx=cx+float(ox),ny=cy+float(oy);
        uint ix=uint(nx-float(floor(nx/65536.0))*65536.0),iy=uint(ny-float(floor(ny/65536.0))*65536.0);
        uint candidate=APHash(ix^APHash(iy)^p.Seed);
        if(layer>0)candidate=APHash(candidate^APHash(uint(layer)*0x9e3779b9u));
        vec2 centre=vec2(nx,ny)+vec2(APRandom(candidate+1u),APRandom(candidate+2u));
        vec2 local=q-centre;
        float angle=6.28318530718*APRandom(candidate+3u),c=cos(angle),sn=sin(angle);
        vec2 ell=vec2(c*local.x+sn*local.y,(-sn*local.x+c*local.y)/0.65);
        float occupied=APRandom(candidate)<occupancy?1.0:0.0;
        float coverage=occupied*(1.0-smoothstep(radius-aa,radius+aa,length(ell)));
        if(coverage>s.Weight){s.Weight=coverage;key=candidate;}
    }
    }
    s.FacetKey=key;s.Colour=clamp(p.FlakeReflectance,vec3(0.0),vec3(0.98));s.MeanColour=s.Colour;
    // Poisson coverage approximation for overlaps, not an exact footprint integral.
    s.MeanWeight=1.0-exp(-density*3.14159265359*radius*radius*0.65);
    s.Unresolved=smoothstep(0.35,1.5,footprint);
    float spread=APRange(p.NormalSpread,0.0,0.7);
    float radial=spread*sqrt(-float(log(max(0.000001,APRandom(key+4u)))));
    float azimuth=6.28318530718*APRandom(key+5u);
    s.Slope=radial*vec2(cos(azimuth),sin(azimuth));
    s.Alpha=APRange(p.FlakeRoughness,0.025,0.5);
    s.PopulationAlpha=sqrt(s.Alpha*s.Alpha+spread*spread);
    return s;
}
AutomotiveFlakeSurface AutomotiveApplyFlakePalette(AutomotiveFlakeSurface s,AP_INPUT(AutomotiveFlakePalette) palette) {
    int count=int(APRange(float(palette.Count),0.0,8.0));
    float total=0.0;vec3 average=vec3(0.0);
    for(int i=0;i<8;++i){if(i>=count)break;
        float weight=APRange(palette.Weight[i],0.0,1000.0);
        vec3 lo=clamp(palette.Minimum[i],vec3(0.0),vec3(0.98)),hi=clamp(palette.Maximum[i],vec3(0.0),vec3(0.98));
        total+=weight;average+=(lo+hi)*(0.5*weight);
    }
    if(total<=0.0)return s;
    s.MeanColour=average/total;
    float pick=min(APRandom(s.FacetKey+0xa511e9b3u),0.999999)*total;
    float cumulative=0.0;
    for(int i=0;i<8;++i){if(i>=count)break;
        cumulative+=APRange(palette.Weight[i],0.0,1000.0);
        if(pick<cumulative){
            s.Colour=mix(clamp(palette.Minimum[i],vec3(0.0),vec3(0.98)),clamp(palette.Maximum[i],vec3(0.0),vec3(0.98)),APRandom(s.FacetKey+0x63d83595u));
            break;
        }
    }
    return s;
}
vec3 AutomotiveCoatTint(AP_INPUT(AutomotivePaintParameters) p) {
    return mix(vec3(1.0),clamp(p.CoatTint,vec3(0.0),vec3(1.0)),APRange(p.CoatTintStrength,0.0,1.0));
}
vec3 AutomotiveFlakeFresnel(AP_INPUT(AutomotivePaintParameters) p,vec3 colour,float vh) {
    vec3 f=FresnelSchlick(colour,vh);
    float pearl=APRange(p.PearlWeight,0.0,1.0);
    if(pearl>0.0)f=mix(f,ThinFilmReflectance(vh,APRange(p.FilmIor,1.01,2.5),APRange(p.FilmThicknessNm,0.0,1500.0)*0.001,f,false),pearl);
    return f;
}
float APBeckmann(vec3 h,vec2 centre,float alpha) {
    if(!(h.z>0.0001))return 0.0;
    vec2 d=vec2(h.x,h.y)/h.z-centre;
    float z2=h.z*h.z;
    return exp(-dot(d,d)/(alpha*alpha))/(3.14159265359*alpha*alpha*z2*z2);
}
float APSmithG1(float cosine,float alpha) {
    float c2=cosine*cosine;
    return 2.0*cosine/(cosine+sqrt(alpha*alpha+(1.0-alpha*alpha)*c2));
}
float APGgx(vec3 h,float alpha) {
    float a2=alpha*alpha,t=h.z*h.z*(a2-1.0)+1.0;
    return a2/(3.14159265359*t*t);
}
vec3 AutomotiveEvaluateCoat(AP_INPUT(AutomotivePaintParameters) p,vec3 v,vec3 l) {
    if(!(v.z>0.0001&&l.z>0.0001))return vec3(0.0);
    vec3 sum=v+l;float len=length(sum);if(!(len>0.0001&&len<3.0))return vec3(0.0);
    vec3 h=sum/len;float r=APRange(p.CoatRoughness,0.06,0.7),a=r*r;
    float f=FresnelDielectric(clamp(dot(v,h),0.0,1.0),1.5);
    return AutomotiveCoatTint(p)*(APRange(p.CoatWeight,0.0,1.0)*f*APGgx(h,a)*APSmithG1(v.z,a)*APSmithG1(l.z,a)/(4.0*v.z*l.z));
}
vec3 AutomotiveEvaluatePaint(AP_INPUT(AutomotivePaintParameters) p,AP_INPUT(AutomotiveFlakeSurface) s,vec3 v,vec3 l) {
    if(!(v.z>0.0001&&l.z>0.0001))return vec3(0.0);
    vec3 sum=v+l;float len=length(sum);if(!(len>0.0001&&len<3.0))return vec3(0.0);
    vec3 h=sum/len;float vh=clamp(dot(v,h),0.0,1.0);
    float coverage=mix(s.Weight,s.MeanWeight,s.Unresolved);
    float discrete=s.Weight*APBeckmann(h,s.Slope,s.Alpha);
    float population=s.MeanWeight*APBeckmann(h,vec2(0.0),s.PopulationAlpha);

    // Macro masking is a bounded approximation for the tilted, finite flake population.
    float masking=APSmithG1(v.z,s.PopulationAlpha)*APSmithG1(l.z,s.PopulationAlpha);
    vec3 resolvedF=AutomotiveFlakeFresnel(p,s.Colour,vh),populationF=AutomotiveFlakeFresnel(p,s.MeanColour,vh);
    vec3 flakes=mix(resolvedF*discrete,populationF*population,s.Unresolved)*(masking/(4.0*v.z*l.z));
    vec3 base=(1.0-coverage)*clamp(p.Pigment,vec3(0.0),vec3(0.95))*0.31830988618+flakes;
    float coat=APRange(p.CoatWeight,0.0,1.0);
    float transmission=(1.0-coat*FresnelDielectric(v.z,1.5))*(1.0-coat*FresnelDielectric(l.z,1.5));
    // Thin-layer two-interface attenuation. No refracted-direction/multiple-bounce solver claim.
    // Artistic attenuation/tinted reflection; not a spectral dye/Beer-Lambert coating model.
    vec3 tint=mix(vec3(1.0),AutomotiveCoatTint(p),coat);
    return base*transmission*tint+AutomotiveEvaluateCoat(p,v,l);
}
#endif

// Shared CPU/GLSL review scene: a 140 x 90 mm curved, ellipsoidal paint swatch.
struct APPaintHit { float Hit; vec3 Position; vec3 Normal; vec3 View; vec2 UV; };
APPaintHit APIntersect(vec2 pixel,vec2 resolution,float yaw,float distance) {
    APPaintHit h;h.Hit=0.0;h.Position=vec3(0.0);h.Normal=vec3(0.0,0.0,1.0);h.View=h.Normal;h.UV=vec2(0.0);
    vec2 ndc=vec2((pixel.x/resolution.x*2.0-1.0)*resolution.x/resolution.y,pixel.y/resolution.y*2.0-1.0);
    vec3 eye=distance*vec3(sin(yaw),0.15,cos(yaw));
    vec3 forward=normalize(-eye),right=normalize(cross(forward,vec3(0.0,1.0,0.0))),up=cross(right,forward);
    vec3 ray=normalize(forward+right*(ndc.x*0.34)+up*(ndc.y*0.34));
    vec3 radius=vec3(0.070,0.045,0.025),o=eye/radius,d=ray/radius;
    float a=dot(d,d),b=dot(o,d),c=dot(o,o)-1.0,disc=b*b-a*c;
    if(disc<0.0)return h;
    float t=(-b-sqrt(disc))/a;if(t<=0.0)return h;
    h.Hit=1.0;h.Position=eye+ray*t;h.Normal=normalize(h.Position/(radius*radius));h.View=-ray;
    h.UV=vec2(h.Position.x,h.Position.y);return h;
}
vec3 APSceneLocal(vec3 x,vec3 n,vec3 t,vec3 b) { return vec3(dot(x,t),dot(x,b),dot(x,n)); }
vec3 APStudio(vec2 pixel,vec2 resolution,AP_INPUT(AutomotivePaintParameters) p,float phase,float yaw,float distance,AP_INPUT(AutomotiveFlakePalette) palette) {
    APPaintHit hit=APIntersect(pixel,resolution,yaw,distance);
    if(hit.Hit==0.0){float vignette=1.0-length(vec2(pixel.x/resolution.x-0.5,pixel.y/resolution.y-0.5));return vec3(0.014,0.018,0.024)*max(0.3,vignette);}
    APPaintHit hx=APIntersect(pixel+vec2(1.0,0.0),resolution,yaw,distance),hy=APIntersect(pixel+vec2(0.0,1.0),resolution,yaw,distance);
    vec2 dx=hx.Hit>0.0?hx.UV-hit.UV:vec2(0.01),dy=hy.Hit>0.0?hy.UV-hit.UV:vec2(0.01);
    AutomotiveFlakeSurface s=AutomotiveApplyFlakePalette(AutomotivePrepareFlakes(p,hit.UV,dx,dy),palette);
    vec3 n=hit.Normal,t=normalize(vec3(1.0,0.0,0.0)-n*n.x),b=cross(n,t);
    vec3 v=APSceneLocal(hit.View,n,t,b);
    float coverage=mix(s.Weight,s.MeanWeight,s.Unresolved);
    // A labelled diffuse ambient fill; glossy illumination below comes from the studio lights.
    vec3 colour=p.Pigment*(0.16*(1.0-coverage))*(1.0-FresnelDielectric(v.z,1.5))*mix(vec3(1.0),AutomotiveCoatTint(p),APRange(p.CoatWeight,0.0,1.0));
    for(int box=0;box<2;++box)for(int y=0;y<12;++y)for(int x=0;x<8;++x){
        float u=(float(x)+0.5)/8.0-0.5,w=(float(y)+0.5)/12.0-0.5;
        vec3 light=box==0?vec3(-0.38+u*1.05,0.52+w*0.30,1.15):vec3(0.8+u*0.25,-0.15+w*1.05,0.85);
        float angle=phase*(box==0?1.0:-0.4);
        light=vec3(cos(angle)*light.x+sin(angle)*light.z,light.y,-sin(angle)*light.x+cos(angle)*light.z);
        vec3 direction=normalize(light);
        vec3 l=APSceneLocal(direction,n,t,b);
        vec3 radiance=box==0?vec3(46.0,40.0,32.0):vec3(12.0,17.0,26.0);
        // Quadrature over the rectangular source's projected solid angle.
        float area=box==0?0.315:0.2625;
        float omega=area*abs(direction.z)/(dot(light,light)*96.0);
        colour+=AutomotiveEvaluatePaint(p,s,v,l)*radiance*(max(0.0,l.z)*omega);
    }
    return colour;
}
vec3 APTonemap(vec3 x) {
    x=x*1.6;
    x=clamp((x*(2.51*x+vec3(0.03)))/(x*(2.43*x+vec3(0.59))+vec3(0.14)),vec3(0.0),vec3(1.0));
    return pow(x,vec3(1.0/2.2));
}
