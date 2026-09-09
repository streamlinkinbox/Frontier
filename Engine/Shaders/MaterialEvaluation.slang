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
//
// Conventions: local shading frame, +Z = shading normal, wo = towards the viewer, wi = towards the light; both point
//    away from the surface. Every Evaluate* returns f (no cosine). Sample* returns wi and the pdf of the whole mixture.
//    Colours linear Rec.709. Not evaluated here (R4b scope line): transmission, subsurface, dispersion, glints.

#ifndef FRONTIER_MATERIAL_EVALUATION
#define FRONTIER_MATERIAL_EVALUATION

#ifndef FRONTIER_CPU_PORT
layout(binding = 13) uniform sampler2D EnergyLut;   // x = μ = cosθ, y = α : (A, B, E_avg) — E_ss(μ, α) = F0·A + B (split-sum), E_avg(α) hemispherical mean
layout(binding = 14) uniform sampler2D SheenLut;    // x = cosθ,    y = α : (aInv, bInv, R) — LtcSheenTable.h
vec3  FetchEnergy(float mu, float alpha)     { return textureLod(EnergyLut, vec2(mu, alpha), 0.0).xyz; }
vec3  FetchSheen (float mu, float alpha)     { return textureLod(SheenLut,  vec2(mu, alpha), 0.0).xyz; }
#endif

const float kPi         = 3.14159265358979;
const float kInvPi      = 0.31830988618379;
const float kMinAlpha   = 0.0025;                // [-] roughness² floor: keeps D/VNDF finite; mirror-like below this
const float kMinCos     = 1.0e-4;

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
    float SpecularAnisotropy; // specular_roughness_anisotropy
    float SpecularIor;        // specular_ior
    float ThinFilmWeight;     // thin_film_weight
    float ThinFilmThickness;  // thin_film_thickness [µm]
    float ThinFilmIor;        // thin_film_ior
    float HazinessWeight;     // slate_haziness_weight
    float HazinessRoughness;  // slate_haziness_roughness
    float CoatWeight;         // coat_weight
    vec3  CoatColor;          // coat_color (T² at normal incidence)
    float CoatRoughness;      // coat_roughness
    float CoatIor;            // coat_ior
    float CoatDarkening;      // coat_darkening
    float FuzzWeight;         // fuzz_weight
    vec3  FuzzColor;          // fuzz_color
    float FuzzRoughness;      // fuzz_roughness
    vec3  Emission;           // emission_luminance × emission_color [nit]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    FRESNEL
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
    float Diffuse;      // … EON
};

struct ResolvedLayers   // per-(record, wo) constants shared by Evaluate / Sample / Pdf
{
    vec2  SpecularAlpha;      // after coat roughening
    vec2  HazeAlpha;
    vec2  CoatAlpha;
    float SpecularEta;        // modulated by specular_weight, coat presence
    float CoatEta;
    vec3  MetalF0;
    vec3  DielectricF0;       // (η−1)²/(η+1)² × specular_color
    float CoatF0;
    float CoatFresnelO;       // F_coat(μo)
    float CoatAlbedoO;        // directional albedo of the coat lobe (for the albedo-scaling layer)
    vec3  CoatAbsorptionO;    // T^(1/μto) for the outgoing leg
    vec3  BaseDarkening;      // lerp(1, Δ, C·δ)
    float FuzzAlbedoO;        // R(α, μo) × weight
    vec3  DielectricAlbedoO;  // E_spec(μo) for the diffuse albedo scaling
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

ResolvedLayers ResolveLayers(ShadingRecord m, vec3 wo)
{
    ResolvedLayers L;
    float muO = max(wo.z, kMinCos);
    float c = m.CoatWeight;

    // Coat: IOR relative to air; base sees lerp(n_b/n_a, n_b/n_c, C) — OpenPBR eq. 60 with the TIR inversion (eq. 73).
    L.CoatEta = max(m.CoatIor, 1.0001);
    float etaBaseAir  = max(m.SpecularIor, 1.0001);
    float etaBaseCoat = etaBaseAir / L.CoatEta;
    if (etaBaseCoat < 1.0) etaBaseCoat = 1.0 / etaBaseCoat;
    float etaBase = mix(etaBaseAir, etaBaseCoat, c);
    L.SpecularEta = ModulatedIor(etaBase, m.SpecularWeight);
    float f0Dielectric = (L.SpecularEta - 1.0) / (L.SpecularEta + 1.0); f0Dielectric *= f0Dielectric;
    L.DielectricF0 = m.SpecularColor * f0Dielectric;
    L.MetalF0 = m.BaseColor;
    L.CoatF0 = (L.CoatEta - 1.0) / (L.CoatEta + 1.0); L.CoatF0 *= L.CoatF0;

    // Roughness after coat roughening (eq. 72) — base only; the coat keeps its own.
    float rSpec = CoatRoughen(m.SpecularRoughness, m.CoatRoughness, c);
    L.SpecularAlpha = AnisotropicAlpha(rSpec, m.SpecularAnisotropy);
    L.HazeAlpha     = AnisotropicAlpha(CoatRoughen(max(m.HazinessRoughness, m.SpecularRoughness), m.CoatRoughness, c), m.SpecularAnisotropy);
    L.CoatAlpha     = AnisotropicAlpha(m.CoatRoughness, 0.0);

    // Coat Fresnel / albedo for the layer operator (§3.10 albedo scaling, non-reciprocal form).
    L.CoatFresnelO = FresnelDielectric(muO, L.CoatEta);
    L.CoatAlbedoO = min(c * GgxDirectionalAlbedo(vec3(L.CoatF0), muO, L.CoatAlpha).x, c);
    L.CoatAbsorptionO = mix(vec3(1.0), CoatAbsorption(m.CoatColor, muO, L.CoatEta), c);

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

    // Dielectric specular albedo (for the diffuse albedo scaling, eq. 42): split-sum single scatter + Kulla–Conty term.
    vec3 eMain = GgxDirectionalAlbedo(L.DielectricF0, muO, L.SpecularAlpha);
    if (m.HazinessWeight > 0.0) eMain = mix(eMain, GgxDirectionalAlbedo(L.DielectricF0, muO, L.HazeAlpha), m.HazinessWeight);
    L.DielectricAlbedoO = min(vec3(1.0), eMain);

    // Lobe selection probabilities from the same albedo estimates (never zero for an enabled lobe).
    float underFuzz = 1.0 - L.FuzzAlbedoO;
    float underCoat = underFuzz * (1.0 - L.CoatAlbedoO);
    float lumMetal  = dot(L.MetalF0, vec3(0.2126, 0.7152, 0.0722));
    float lumDiel   = dot(L.DielectricAlbedoO, vec3(0.2126, 0.7152, 0.0722));
    float specMass  = underCoat * mix(lumDiel, min(1.0, lumMetal + 0.04), m.Metalness);
    float diffMass  = underCoat * (1.0 - m.Metalness) * (1.0 - lumDiel) * dot(EonAlbedo(m.BaseColor, m.DiffuseRoughness, muO), vec3(0.2126, 0.7152, 0.0722));
    LobeWeights w;
    w.Fuzz = L.FuzzAlbedoO; w.Coat = underFuzz * L.CoatAlbedoO; w.Specular = max(specMass, 0.02 * underCoat); w.Diffuse = max(diffMass, (1.0 - m.Metalness) * 0.02 * underCoat);
    float total = w.Fuzz + w.Coat + w.Specular + w.Diffuse;
    w.Fuzz /= total; w.Coat /= total; w.Specular /= total; w.Diffuse /= total;
    L.Weights = w;
    return L;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    EVALUATE
//------------------------------------------------------------------------------------------------------------------------

// Base specular (dielectric + metal mix, thin film, haziness) — f only.
vec3 EvaluateBaseSpecular(ShadingRecord m, ResolvedLayers L, vec3 wo, vec3 wi)
{
    vec3 h = normalize(wo + wi);
    float muH = max(dot(wo, h), 0.0);

    // Fresnel of the two substrates.
    float fD = FresnelDielectric(muH, L.SpecularEta);
    vec3  fDielectric = m.SpecularColor * fD;
    vec3  fMetal = m.SpecularWeight * FresnelF82(L.MetalF0, m.SpecularColor, muH);
    if (m.ThinFilmWeight > 0.0)
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
    vec3 f0 = mix(L.DielectricF0, m.SpecularWeight * L.MetalF0, m.Metalness);
    float dMain = GgxD(h, L.SpecularAlpha) * GgxG2(wo, wi, L.SpecularAlpha) / (4.0 * max(wo.z, kMinCos) * max(wi.z, kMinCos));
    vec3 spec = f * dMain + GgxMultiScatter(f0, wo.z, wi.z, L.SpecularAlpha);
    if (m.HazinessWeight > 0.0)
    {
        float dHaze = GgxD(h, L.HazeAlpha) * GgxG2(wo, wi, L.HazeAlpha) / (4.0 * max(wo.z, kMinCos) * max(wi.z, kMinCos));
        spec = mix(spec, f * dHaze + GgxMultiScatter(f0, wo.z, wi.z, L.HazeAlpha), m.HazinessWeight);
    }
    return spec;
}

vec3 EvaluateBsdf(ShadingRecord m, ResolvedLayers L, vec3 wo, vec3 wi)
{
    if (wo.z <= 0.0 || wi.z <= 0.0) return vec3(0.0);
    float muI = wi.z;

    // Bottom: diffuse, scaled by what the dielectric interface lets through (eq. 42, non-reciprocal albedo scaling).
    vec3 diffuse = (1.0 - m.Metalness) * (vec3(1.0) - L.DielectricAlbedoO) * EonEvaluate(m.BaseColor, m.DiffuseRoughness, wi, wo);
    vec3 base = diffuse + EvaluateBaseSpecular(m, L, wo, wi);

    // Coat layer on top of the base.
    if (m.CoatWeight > 0.0)
    {
        vec3 h = normalize(wo + wi);
        float fC = FresnelDielectric(max(dot(wo, h), 0.0), L.CoatEta);
        float dC = GgxD(h, L.CoatAlpha) * GgxG2(wo, wi, L.CoatAlpha) / (4.0 * max(wo.z, kMinCos) * max(muI, kMinCos));
        vec3 coat = vec3(fC * dC) + GgxMultiScatter(vec3(L.CoatF0), wo.z, muI, L.CoatAlpha);
        vec3 absorptionI = CoatAbsorption(m.CoatColor, muI, L.CoatEta);
        vec3 under = base * L.BaseDarkening * (1.0 - L.CoatAlbedoO) * mix(vec3(1.0), L.CoatAbsorptionO * absorptionI, m.CoatWeight);
        base = m.CoatWeight * coat + under;
    }

    // Fuzz on top of the coat.
    if (m.FuzzWeight > 0.0)
    {
        vec3 fuzz = m.FuzzWeight * SheenEvaluate(m.FuzzColor, clamp(m.FuzzRoughness, 0.0, 1.0), wo, wi);
        base = fuzz + base * (1.0 - L.FuzzAlbedoO);
    }
    return base;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SAMPLE / PDF
//------------------------------------------------------------------------------------------------------------------------

float PdfBsdf(ShadingRecord m, ResolvedLayers L, vec3 wo, vec3 wi)
{
    if (wo.z <= 0.0 || wi.z <= 0.0) return 0.0;
    vec3 h = normalize(wo + wi);
    float jac = 1.0 / (4.0 * max(dot(wo, h), kMinCos));
    float pdf = L.Weights.Diffuse * EonPdfCltc(wo, wi, m.DiffuseRoughness);
    float pSpec = GgxVndfPdf(wo, h, L.SpecularAlpha) * jac;
    if (m.HazinessWeight > 0.0) pSpec = mix(pSpec, GgxVndfPdf(wo, h, L.HazeAlpha) * jac, m.HazinessWeight);
    pdf += L.Weights.Specular * pSpec;
    if (L.Weights.Coat > 0.0) pdf += L.Weights.Coat * GgxVndfPdf(wo, h, L.CoatAlpha) * jac;
    if (L.Weights.Fuzz > 0.0) pdf += L.Weights.Fuzz * SheenPdf(clamp(m.FuzzRoughness, 0.0, 1.0), wo, wi);
    return pdf;
}

// u = 3 uniforms (lobe pick + 2D). Returns wi in .xyz, pdf in .w (0 = failed sample).
vec4 SampleBsdf(ShadingRecord m, ResolvedLayers L, vec3 wo, vec3 u)
{
    vec3 wi;
    float pick = u.x;
    if (pick < L.Weights.Fuzz)
    {
        wi = SheenSample(clamp(m.FuzzRoughness, 0.0, 1.0), wo, u.yz).xyz;
    }
    else if (pick < L.Weights.Fuzz + L.Weights.Coat)
    {
        vec3 h = SampleGgxVndf(wo, L.CoatAlpha, u.yz);
        wi = reflect(-wo, h);
    }
    else if (pick < L.Weights.Fuzz + L.Weights.Coat + L.Weights.Specular)
    {
        float hazePick = (pick - L.Weights.Fuzz - L.Weights.Coat) / max(L.Weights.Specular, 1.0e-6);
        vec3 h = SampleGgxVndf(wo, hazePick < m.HazinessWeight ? L.HazeAlpha : L.SpecularAlpha, u.yz);
        wi = reflect(-wo, h);
    }
    else
    {
        wi = EonSampleCltc(wo, m.DiffuseRoughness, u.y, u.z).xyz;
    }
    if (wi.z <= 0.0) return vec4(0.0);
    return vec4(wi, PdfBsdf(m, L, wo, wi));
}

#endif // FRONTIER_MATERIAL_EVALUATION
