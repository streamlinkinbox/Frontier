//============================================================================================================================================
//                                                  MATERIALFURNACEPROOF.CPP
//============================================================================================================================================
// 🧩 M0 — the R4b math proofs re-seated in the verification wing: Engine/Shaders/MaterialEvaluation.slang compiled 1:1
//    as C++ (FRONTIER_CPU_PORT, see SlangCpuShim.h) against ShadingTableCodec-baked tables. White furnace (EON / GGX +
//    Kulla–Conty / fuzz / coat stack), reciprocity, and sampling consistency. Deterministic (splitmix64); MC tolerances
//    are %level, analytic ones tight.
//
//    Build (see CheckMaterialsProof.sh): g++ -std=c++20 -DFRONTIER_CPU_PORT -I Exhibits/Workbench/Materials
//        -I Engine/DisplayPresentation -I Engine/Shaders MaterialFurnaceProof.cpp
//        Engine/DisplayPresentation/ShadingTableCodec.cpp

#include "SlangCpuShim.h"
#include "ShadingTableCodec.h"

#include <cstdint>
#include <cstdio>
#include <limits>

namespace {

const Frontier::ShadingTableSet* g_Tables = nullptr;

} // namespace

inline vec3 FetchEnergy(float mu, float alpha)
{
    float Out[3] = { 0.0f, 0.0f, 0.0f };
    Frontier::ShadingTableCodec::SampleEnergy(*g_Tables, mu, alpha, Out);
    return vec3(Out[0], Out[1], Out[2]);
}

inline vec3 FetchSheen(float mu, float alpha)
{
    float Out[3] = { 0.0f, 0.0f, 0.0f };
    Frontier::ShadingTableCodec::SampleSheen(*g_Tables, mu, alpha, Out);
    return vec3(Out[0], Out[1], Out[2]);
}

inline vec4 FetchSheenFull(float mu, float alpha)   // M3: + E_charlie in .w
{
    float Out[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    Frontier::ShadingTableCodec::SampleSheenFull(*g_Tables, mu, alpha, Out);
    return vec4(Out[0], Out[1], Out[2], Out[3]);
}


#include "MaterialEvaluation.slang"

namespace {

int g_Fail = 0;

#define CHECK(Cond, ...)                                                                                        \
    do { if (!(Cond)) { ++g_Fail; std::printf("  FAIL "); std::printf(__VA_ARGS__); std::printf("\n"); }  \
         else { std::printf("  ok "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

uint64_t g_Rng = 0x9E3779B97F4A7C15ull;   // splitmix64 state (fixed seed → deterministic)

float Rand01()
{
    uint64_t z = (g_Rng += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z = z ^ (z >> 31);
    return static_cast<float>((z >> 11) * (1.0 / 9007199254740992.0));
}

// Cosine-weighted hemisphere sample about +Z (pdf = cosθ/π).
vec3 CosineSample(float u1, float u2)
{
    float r = sqrt(u1), phi = 2.0f * 3.14159265358979f * u2;
    return vec3(r * cos(phi), r * sin(phi), sqrt(1.0f - u1));
}

vec3 UniformHemisphere(float u1, float u2)
{
    float z = u1, s = sqrt(1.0f - z * z), phi = 2.0f * 3.14159265358979f * u2;
    return vec3(s * cos(phi), s * sin(phi), z);
}

ShadingRecord StandardMaterial(vec3 albedo, float roughness)
{
    ShadingRecord m;
    m.BaseColor = albedo; m.Metalness = 0.0f; m.DiffuseRoughness = 0.0f;
    m.SpecularWeight = 1.0f; m.SpecularColor = vec3(1.0f); m.SpecularRoughness = roughness;
    m.SpecularAnisotropy = 0.0f; m.AnisotropyAngle = 0.0f; m.SpecularIor = 1.5f;
    m.ThinFilmWeight = 0.0f; m.ThinFilmThickness = 0.5f; m.ThinFilmIor = 1.4f;
    m.HazinessWeight = 0.0f; m.HazinessRoughness = 0.5f;
    m.CoatWeight = 0.0f; m.CoatColor = vec3(1.0f); m.CoatRoughness = 0.0f; m.CoatAnisotropy = 0.0f;
    m.CoatIor = 1.6f; m.CoatDarkening = 1.0f;
    m.CoatTangent = vec3(1.0f, 0.0f, 0.0f); m.CoatNormal = vec3(0.0f, 0.0f, 1.0f);   // M2 identity frame
    m.FuzzWeight = 0.0f; m.FuzzColor = vec3(1.0f); m.FuzzRoughness = 0.5f;
    m.Emission = vec3(0.0f);
    m.TransmissionWeight = 0.0f; m.TransmissionColor = vec3(1.0f);   // M4: opaque defaults (bit-identical R4b)
    m.TransmissionDepth = 0.0f; m.TransmissionThickness = 0.0f;
    m.Selection = 0u;   // M3: Standard — every pre-M3 test below must be unaffected by the cloth branch
    m.SssWeight = 0.0f; m.SssColor = vec3(1.0f);   // M5: SSS off — every pre-M5 test below must be unaffected
    m.SssRadius = 0.0f; m.SssRadiusScale = vec3(0.0f); m.SssThickness = 0.0f;
    return m;
}

ShadingRecord GlassMaterial(float roughness, float ior = 1.5f)
{
    ShadingRecord m = StandardMaterial(vec3(0.0f), roughness);   // black base: diffuse is dead at weight 1 anyway
    m.SpecularIor = ior;
    m.TransmissionWeight = 1.0f;
    m.Selection = kReflectanceTransmissive;
    return m;
}

ShadingRecord SssMaterial(vec3 base, vec3 rho, float radius, vec3 scale, float thickness, float weight = 1.0f)
{
    ShadingRecord m = StandardMaterial(base, 0.5f);
    m.SssWeight = weight; m.SssColor = rho;
    m.SssRadius = radius; m.SssRadiusScale = scale; m.SssThickness = thickness;
    m.Selection = kReflectanceSubsurface;
    return m;
}

// EON at roughness 0 is bit-exact Lambert (Cornell identity); EON albedo ≈ base colour elsewhere.
void ProofEon()
{
    std::printf("[furnace] EON diffuse\n");
    for (float mu : { 0.05f, 0.3f, 0.6f, 1.0f })
    {
        vec3 e = EonAlbedo(vec3(0.5f), 0.0f, mu);
        CHECK(std::fabs(e.x - 0.5f) < 1e-4f && std::fabs(e.y - 0.5f) < 1e-4f && std::fabs(e.z - 0.5f) < 1e-4f,
              "EON r=0 == Lambert at mu=%.2f (%.5f)", mu, e.x);
    }
    for (float r : { 0.5f, 1.0f })
    {
        for (float mu : { 0.25f, 0.6f, 1.0f })
        {
            vec3 wo = vec3(sqrt(1.0f - mu * mu), 0.0f, mu);
            vec3 acc = vec3(0.0f);
            const int N = 60000;
            for (int i = 0; i < N; ++i)
            {
                vec3 wi = CosineSample(Rand01(), Rand01());
                acc += EonEvaluate(vec3(0.5f), r, wi, wo);
            }
            acc = acc * (3.14159265358979f / static_cast<float>(N));   // ∫f·cosθ dω, cosine-weighted
            vec3 analytic = EonAlbedo(vec3(0.5f), r, mu);
            CHECK(std::fabs(acc.x - analytic.x) < 0.01f, "EON furnace r=%.1f mu=%.2f num=%.4f ana=%.4f", r, mu, acc.x, analytic.x);
            // EON's energy property is E ≤ ρ (never creates light) with the MS term recycling most of the FON loss —
            // it is NOT E == ρ (normal incidence, r=1, ρ=0.5 gives 0.439: the R4b plan's "± 0.5 %" claim was wrong and
            // is corrected here). Both bounds below are analytic facts, not fitted numbers.
            CHECK(acc.x <= 0.5f * 1.005f, "EON never exceeds rho r=%.1f mu=%.2f E=%.4f", r, mu, acc.x);
            CHECK(acc.x >= 0.5f * 0.85f, "EON MS floor r=%.1f mu=%.2f E=%.4f", r, mu, acc.x);
        }
    }
}

// Compensated GGX integrates to 1 for white F0; the table's split-sum matches numeric single-scatter.
void ProofGgxFurnace()
{
    std::printf("[furnace] GGX + Kulla–Conty (white F0)\n");
    const vec3 f0 = vec3(1.0f);
    for (float rough : { 0.15f, 0.55f, 1.0f })
    {
        vec2 a = AnisotropicAlpha(rough, 0.0f);
        for (float muO : { 0.1f, 0.5f, 1.0f })
        {
            vec3 wo = vec3(sqrt(1.0f - muO * muO), 0.0f, muO);
            // Single-scatter via VNDF (cosine sampling has ruinous variance once the lobe spikes at low roughness);
            // the multiple-scatter term is smooth, so plain cosine sampling is the right estimator for it.
            float ss = 0.0f;
            const int N = 120000;
            for (int i = 0; i < N; ++i)
            {
                vec3 h = SampleGgxVndf(wo, a, vec2(Rand01(), Rand01()));
                vec3 wi = reflect(-wo, h);
                if (wi.z <= 0.0f) continue;
                float pdf = GgxVndfPdf(wo, h, a) / (4.0f * max(dot(wo, h), 1e-4f));
                float d = GgxD(h, a) * GgxG2(wo, wi, a) / (4.0f * wo.z * wi.z);
                ss += d * wi.z / max(pdf, 1e-9f);
            }
            float Ess = ss / static_cast<float>(N);
            float ms = 0.0f;
            const int M = 60000;
            for (int i = 0; i < M; ++i)
            {
                vec3 wi = CosineSample(Rand01(), Rand01());
                ms += GgxMultiScatter(f0, wo.z, wi.z, a).x;
            }
            float Ems = ms * 3.14159265358979f / static_cast<float>(M);
            vec3 e = FetchEnergy(muO, rough * rough);
            CHECK(std::fabs(Ess - (e.x + e.y)) < 0.02f, "GGX ss r=%.2f mu=%.1f num=%.4f table=%.4f",
                  rough, muO, Ess, e.x + e.y);
            CHECK(std::fabs(Ess + Ems - 1.0f) < 0.02f, "GGX compensated r=%.2f mu=%.1f E=%.4f", rough, muO, Ess + Ems);
        }
    }
}

void ProofFuzzAndCoat()
{
    std::printf("[furnace] fuzz bound + coat stack\n");
    for (float a = 0.0f; a <= 1.0f; a += 0.1f)
        for (float mu = 0.02f; mu <= 1.0f; mu += 0.1f)
            CHECK(SheenAlbedo(a, mu) <= 1.001f, "sheen albedo ≤ 1 (a=%.1f mu=%.2f R=%.4f)", a, mu, SheenAlbedo(a, mu));

    ShadingRecord m = StandardMaterial(vec3(0.5f), 0.4f);
    m.CoatWeight = 0.5f; m.CoatRoughness = 0.1f; m.FuzzWeight = 0.3f;
    for (float muO : { 0.3f, 0.8f })
    {
        vec3 wo = vec3(sqrt(1.0f - muO * muO), 0.0f, muO);
        ResolvedLayers L = ResolveLayers(m, wo);
        vec3 acc = vec3(0.0f);
        const int N = 100000;
        for (int i = 0; i < N; ++i)
        {
            vec3 wi = CosineSample(Rand01(), Rand01());
            acc += EvaluateBsdf(m, L, wo, wi);
        }
        acc = acc * (3.14159265358979f / static_cast<float>(N));
        float peak = acc.x > acc.y ? (acc.x > acc.z ? acc.x : acc.z) : (acc.y > acc.z ? acc.y : acc.z);
        CHECK(peak <= 1.01f, "coat+fuzz+base stack albedo ≤ 1 (mu=%.1f E=%.4f)", muO, peak);
    }
}

void ProofReciprocity()
{
    std::printf("[furnace] reciprocity (EON / GGX / Kulla–Conty; coat stack excluded by design)\n");
    const int N = 5000;
    float worstEon = 0.0f, worstGgx = 0.0f, worstMs = 0.0f;
    vec2 a = AnisotropicAlpha(0.4f, 0.0f);
    for (int i = 0; i < N; ++i)
    {
        vec3 wi = UniformHemisphere(Rand01(), Rand01());
        vec3 wo = UniformHemisphere(Rand01(), Rand01());
        vec3 e1 = EonEvaluate(vec3(0.6f), 0.7f, wi, wo), e2 = EonEvaluate(vec3(0.6f), 0.7f, wo, wi);
        worstEon = max(worstEon, std::fabs(e1.x - e2.x) / max(e1.x, 1e-3f));
        vec3 h = normalize(wo + wi);
        float g1 = GgxD(h, a) * GgxG2(wo, wi, a) / (4.0f * wo.z * wi.z);
        float g2 = GgxD(h, a) * GgxG2(wi, wo, a) / (4.0f * wi.z * wo.z);
        worstGgx = max(worstGgx, std::fabs(g1 - g2) / max(g1, 1e-3f));
        vec3 ms1 = GgxMultiScatter(vec3(0.9f), wo.z, wi.z, a), ms2 = GgxMultiScatter(vec3(0.9f), wi.z, wo.z, a);
        worstMs = max(worstMs, std::fabs(ms1.x - ms2.x) / max(ms1.x, 1e-3f));
    }
    CHECK(worstEon < 1e-3f, "EON reciprocal (worst %.2e)", worstEon);
    CHECK(worstGgx < 1e-3f, "GGX single-scatter reciprocal (worst %.2e)", worstGgx);
    CHECK(worstMs < 1e-3f, "Kulla–Conty reciprocal (worst %.2e)", worstMs);
}

void ProofSampling()
{
    std::printf("[furnace] sampling consistency E[f·cosθ/pdf]\n");
    // VNDF vs the table single-scatter.
    {
        vec2 a = AnisotropicAlpha(0.35f, 0.0f);
        vec3 wo = normalize(vec3(0.3f, 0.2f, 0.9f));
        vec3 e = FetchEnergy(wo.z, 0.35f * 0.35f);
        float acc = 0.0f;
        const int N = 150000;
        for (int i = 0; i < N; ++i)
        {
            vec3 h = SampleGgxVndf(wo, a, vec2(Rand01(), Rand01()));
            vec3 wi = reflect(-wo, h);
            if (wi.z <= 0.0f) continue;
            float pdf = GgxVndfPdf(wo, h, a) / (4.0f * max(dot(wo, h), 1e-4f));
            float d = GgxD(h, a) * GgxG2(wo, wi, a) / (4.0f * wo.z * wi.z);
            acc += d * wi.z / max(pdf, 1e-9f);
        }
        acc /= static_cast<float>(N);
        CHECK(std::fabs(acc - (e.x + e.y)) / max(e.x + e.y, 1e-3f) < 0.03f,
              "VNDF E[f·cos/pdf]=%.4f table=%.4f", acc, e.x + e.y);
    }
    // CLTC vs the EON analytic albedo.
    {
        vec3 wo = normalize(vec3(0.4f, 0.1f, 0.9f));
        vec3 ana = EonAlbedo(vec3(0.5f), 0.6f, wo.z);
        float acc = 0.0f;
        const int N = 150000;
        for (int i = 0; i < N; ++i)
        {
            vec4 s = EonSampleCltc(wo, 0.6f, Rand01(), Rand01());
            vec3 wi = s.xyz;
            vec3 f = EonEvaluate(vec3(0.5f), 0.6f, wi, wo);
            acc += f.x * wi.z / max(s.w, 1e-9f);
        }
        acc /= static_cast<float>(N);
        CHECK(std::fabs(acc - ana.x) / ana.x < 0.03f, "CLTC E[f·cos/pdf]=%.4f ana=%.4f", acc, ana.x);
    }
    // Full mixture vs its own numeric furnace.
    {
        ShadingRecord m = StandardMaterial(vec3(0.5f), 0.4f);
        vec3 wo = normalize(vec3(0.3f, 0.25f, 0.9f));
        ResolvedLayers L = ResolveLayers(m, wo);
        float ref = 0.0f;
        const int N0 = 120000;
        for (int i = 0; i < N0; ++i)
        {
            vec3 wi = CosineSample(Rand01(), Rand01());
            ref += EvaluateBsdf(m, L, wo, wi).x;
        }
        ref *= 3.14159265358979f / static_cast<float>(N0);
        float acc = 0.0f;
        const int N = 150000;
        for (int i = 0; i < N; ++i)
        {
            vec4 s = SampleBsdf(m, L, wo, vec4(Rand01(), Rand01(), Rand01(), Rand01()));
            if (s.w <= 0.0f) continue;
            vec3 wi = s.xyz;
            acc += EvaluateBsdf(m, L, wo, wi).x * wi.z / s.w;
        }
        acc /= static_cast<float>(N);
        CHECK(std::fabs(acc - ref) / max(ref, 1e-3f) < 0.03f, "mixture E[f·cos/pdf]=%.4f furnace=%.4f", acc, ref);
    }
}

// M2: lobe frames — rotation equivariance, normal-incidence energy invariance, tilted-coat energy + sampling.
void ProofAnisotropyFrames()
{
    std::printf("[furnace] M2 aniso rotation + coat frame\n");
    const float kHarnessPi = 3.14159265358979f;
    auto RotZ = [](vec3 v, float a) {
        float c = cos(a), s = sin(a);
        return vec3(c * v.x - s * v.y, s * v.x + c * v.y, v.z);
    };

    // ① Equivariance: rotating the material and both vectors together changes nothing (aniso 0.7, θ = 0.6).
    {
        ShadingRecord m0 = StandardMaterial(vec3(0.5f), 0.35f);
        m0.SpecularAnisotropy = 0.7f; m0.AnisotropyAngle = 0.0f;
        ShadingRecord m1 = m0; m1.AnisotropyAngle = 0.6f;
        float worst = 0.0f;
        for (int i = 0; i < 2000; ++i)
        {
            vec3 wo = UniformHemisphere(Rand01(), Rand01());
            vec3 wi = UniformHemisphere(Rand01(), Rand01());
            ResolvedLayers l0 = ResolveLayers(m0, wo);
            vec3 rwo = RotZ(wo, 0.6f), rwi = RotZ(wi, 0.6f);
            ResolvedLayers l1 = ResolveLayers(m1, rwo);
            vec3 f0 = EvaluateBsdf(m0, l0, wo, wi), f1 = EvaluateBsdf(m1, l1, rwo, rwi);
            float d = std::fabs(f0.x - f1.x) + std::fabs(f0.y - f1.y) + std::fabs(f0.z - f1.z);
            float r = std::fabs(f0.x) + std::fabs(f0.y) + std::fabs(f0.z);
            worst = max(worst, d / max(r, 1e-3f));
        }
        CHECK(worst < 1e-3f, "aniso equivariance (worst %.2e)", worst);
    }

    // ② Normal-incidence energy is rotation-invariant (exact by change of variables; tolerance is pure MC noise).
    {
        float e[3] = { 0.0f, 0.0f, 0.0f };
        const float kAngles[3] = { 0.0f, kHarnessPi / 4.0f, kHarnessPi / 2.0f };
        for (int k = 0; k < 3; ++k)
        {
            ShadingRecord m = StandardMaterial(vec3(0.5f), 0.35f);
            m.SpecularAnisotropy = 0.7f; m.AnisotropyAngle = kAngles[k];
            vec3 wo = vec3(0.0f, 0.0f, 1.0f);
            ResolvedLayers L = ResolveLayers(m, wo);
            float acc = 0.0f;
            const int N = 200000;
            for (int i = 0; i < N; ++i)
            {
                vec3 wi = CosineSample(Rand01(), Rand01());
                acc += EvaluateBsdf(m, L, wo, wi).x;
            }
            e[k] = acc * kHarnessPi / static_cast<float>(N);
        }
        CHECK(std::fabs(e[1] - e[0]) / e[0] < 0.015f, "aniso energy θ=45° (%.4f vs %.4f)", e[1], e[0]);
        CHECK(std::fabs(e[2] - e[0]) / e[0] < 0.015f, "aniso energy θ=90° (%.4f vs %.4f)", e[2], e[0]);
    }

    // ③ Sampling consistency with rotation (validates the transpose-back + lobe-space pdfs).
    {
        ShadingRecord m = StandardMaterial(vec3(0.5f), 0.4f);
        m.SpecularAnisotropy = 0.7f; m.AnisotropyAngle = 0.5f;
        vec3 wo = normalize(vec3(0.3f, 0.25f, 0.9f));
        ResolvedLayers L = ResolveLayers(m, wo);
        float ref = 0.0f;
        const int N0 = 120000;
        for (int i = 0; i < N0; ++i) ref += EvaluateBsdf(m, L, wo, CosineSample(Rand01(), Rand01())).x;
        ref *= kHarnessPi / static_cast<float>(N0);
        float acc = 0.0f;
        const int N = 150000;
        for (int i = 0; i < N; ++i)
        {
            vec4 s = SampleBsdf(m, L, wo, vec4(Rand01(), Rand01(), Rand01(), Rand01()));
            if (s.w <= 0.0f) continue;
            vec3 wi = s.xyz;
            acc += EvaluateBsdf(m, L, wo, wi).x * wi.z / s.w;
        }
        acc /= static_cast<float>(N);
        CHECK(std::fabs(acc - ref) / ref < 0.03f, "rotated mixture E[f·cos/pdf]=%.4f furnace=%.4f", acc, ref);
    }

    // ④ Reciprocity survives rotation (dielectric + metal), tested on the isolated specular lobe: the full stack's
    // diffuse × (1 − E(μo)) albedo scaling is non-reciprocal BY DESIGN (OpenPBR §3.10), so the stack as a whole
    // is not — and must not be — asserted reciprocal here.
    for (float metal : { 0.0f, 1.0f })
    {
        ShadingRecord m = StandardMaterial(vec3(0.6f), 0.4f);
        m.Metalness = metal; m.SpecularAnisotropy = 0.6f; m.AnisotropyAngle = 0.5f;
        float worst = 0.0f;
        for (int i = 0; i < 2000; ++i)
        {
            vec3 wi = UniformHemisphere(Rand01(), Rand01());
            vec3 wo = UniformHemisphere(Rand01(), Rand01());
            ResolvedLayers lo = ResolveLayers(m, wo), li = ResolveLayers(m, wi);
            vec3 f1 = EvaluateBaseSpecular(m, lo, wo, wi), f2 = EvaluateBaseSpecular(m, li, wi, wo);
            float d = std::fabs(f1.x - f2.x);
            worst = max(worst, d / max(f1.x, 1e-3f));
        }
        CHECK(worst < 1e-3f, "rotated specular reciprocity metal=%.0f (worst %.2e)", metal, worst);
    }

    // ⑤ Tilted-coat stack stays energy-safe (20° tilt about x; plain + coat-aniso configs).
    for (float coatAniso : { 0.0f, 0.5f })
    {
        ShadingRecord m = StandardMaterial(vec3(0.5f), 0.4f);
        m.CoatWeight = 0.6f; m.CoatRoughness = 0.15f; m.CoatAnisotropy = coatAniso;
        m.CoatTangent = vec3(1.0f, 0.0f, 0.0f);
        m.CoatNormal = vec3(0.0f, 0.34202014f, 0.93969261f);   // 20° about x
        for (float muO : { 0.5f, 1.0f })
        {
            vec3 wo = vec3(sqrt(1.0f - muO * muO), 0.0f, muO);
            ResolvedLayers L = ResolveLayers(m, wo);
            float acc = 0.0f;
            const int N = 100000;
            for (int i = 0; i < N; ++i) acc += EvaluateBsdf(m, L, wo, CosineSample(Rand01(), Rand01())).x;
            acc *= kHarnessPi / static_cast<float>(N);
            CHECK(acc <= 1.01f, "tilted coat albedo ≤ 1 (aniso=%.1f mu=%.1f E=%.4f)", coatAniso, muO, acc);
        }
    }

    // ⑥ Coat continuity: a 2° tilt barely moves the furnace (a wrong basis would jump).
    {
        ShadingRecord m0 = StandardMaterial(vec3(0.5f), 0.4f);
        m0.CoatWeight = 0.6f; m0.CoatRoughness = 0.15f;
        ShadingRecord m1 = m0;
        m1.CoatNormal = vec3(0.0f, 0.03489950f, 0.99939083f);   // 2° about x
        vec3 wo = normalize(vec3(0.0f, 0.5f, 0.7f));
        float e0 = 0.0f, e1 = 0.0f;
        const int N = 120000;
        ResolvedLayers l0 = ResolveLayers(m0, wo), l1 = ResolveLayers(m1, wo);
        for (int i = 0; i < N; ++i)
        {
            vec3 wi = CosineSample(Rand01(), Rand01());
            e0 += EvaluateBsdf(m0, l0, wo, wi).x;
            e1 += EvaluateBsdf(m1, l1, wo, wi).x;
        }
        e0 *= kHarnessPi / static_cast<float>(N);
        e1 *= kHarnessPi / static_cast<float>(N);
        CHECK(std::fabs(e1 - e0) / e0 < 0.03f, "coat continuity 2° (%.4f vs %.4f)", e1, e0);
    }

    // ⑦ Tilted-coat sampling consistency (validates coat sample-back + pdf guard; wo off-grazing keeps the
    // below-coat-surface missing-mass bias ~1e-5, far inside the tolerance).
    {
        ShadingRecord m = StandardMaterial(vec3(0.5f), 0.4f);
        m.CoatWeight = 0.6f; m.CoatRoughness = 0.15f;
        m.CoatTangent = vec3(1.0f, 0.0f, 0.0f);
        m.CoatNormal = vec3(0.0f, 0.25881905f, 0.96592583f);   // 15° about x
        vec3 wo = normalize(vec3(0.2f, 0.2f, 0.8f));
        ResolvedLayers L = ResolveLayers(m, wo);
        float ref = 0.0f;
        const int N0 = 120000;
        for (int i = 0; i < N0; ++i) ref += EvaluateBsdf(m, L, wo, CosineSample(Rand01(), Rand01())).x;
        ref *= kHarnessPi / static_cast<float>(N0);
        float acc = 0.0f;
        const int N = 150000;
        for (int i = 0; i < N; ++i)
        {
            vec4 s = SampleBsdf(m, L, wo, vec4(Rand01(), Rand01(), Rand01(), Rand01()));
            if (s.w <= 0.0f) continue;
            vec3 wi = s.xyz;
            acc += EvaluateBsdf(m, L, wo, wi).x * wi.z / s.w;
        }
        acc /= static_cast<float>(N);
        CHECK(std::fabs(acc - ref) / ref < 0.035f, "tilted-coat mixture E[f·cos/pdf]=%.4f furnace=%.4f", acc, ref);
    }
}

// M3: the SheenLut .w bake (Charlie albedo) is range-safe and the cloth rescale can never leak — per texel AND
// (by the same argument) per bilinear interpolation, since both clamp inputs stay in range under lerp.
void ProofSheenTable()
{
    std::printf("[furnace] M3 Charlie bake + rescale safety\n");
    const uint32_t N = Frontier::ShadingTableSet::kResolution;
    float maxEc = 0.0f, maxR = 0.0f, maxRescaled = 0.0f;
    int hi = 0, lo = 0;
    for (uint32_t i = 0; i < N * N; ++i)
    {
        const float* T = &g_Tables->Sheen[i * 4u];
        CHECK(T[3] >= 0.0f && T[3] <= 1.0f, "E_c in [0,1] (texel %u: %.4f)", i, T[3]);
        CHECK(T[2] >= 0.0f && T[2] <= 1.0f, "LTC R in [0,1] (texel %u: %.4f)", i, T[2]);
        maxEc = max(maxEc, T[3]); maxR = max(maxR, T[2]);
        float rescale = T[3] / max(T[2], 1e-4f);
        rescale = rescale < 0.5f ? 0.5f : (rescale > 2.0f ? 2.0f : rescale);
        if (rescale >= 2.0f) ++hi; else if (rescale <= 0.5f) ++lo;
        maxRescaled = max(maxRescaled, rescale * T[2]);
        CHECK(rescale * T[2] <= 1.0f + 1e-6f, "rescale·R <= 1 (texel %u)", i);
    }
    std::printf("    max E_c=%.4f max R=%.4f max rescale·R=%.4f clamps hi=%d lo=%d\n", maxEc, maxR, maxRescaled, hi, lo);
}

// M3: the consumption table itself, compiled 1:1 from MaterialEvaluation.slang — bit C of the mask = channel C.
// Base/opacity/emission bypass Consumes via never-gating (see the .slang preamble); their presence in Cloth's arm
// declares Sultan-18 §3 membership, not fetch behaviour. Ch 8/9 were locked false until M4/M5 wired them (the flip
// is a deliberate test change, not drift); ch 15 (unassigned) reads false.
void ProofConsumesTable()
{
    std::printf("[furnace] M3 consumption matrix (8 selections)\n");
    const uint32_t kExpect[8] = {
        (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 14),   // Standard: metal/rough/spec/normal/AO
        (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 13) | (1u << 14),   // + aniso dir
        (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5) | (1u << 10) | (1u << 14),   // + coat/coat-normal
        (1u << 0) | (1u << 2) | (1u << 4) | (1u << 7) | (1u << 11) | (1u << 14),   // Cloth: Sultan-18 §3 {1,3,5,6,8,14,15}
        (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 9) | (1u << 14),   // Subsurface (9 WIRED M5)
        (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 8) | (1u << 14),   // Transmissive (8 WIRED M4)
        0u,   // EmissiveOnly
        0u,   // Unlit
    };
    for (uint32_t s = 0; s < 8u; ++s)
        for (uint32_t c = 0; c < 16u; ++c)
            CHECK(ReflectanceConsumes(s, c) == ((kExpect[s] >> c & 1u) != 0u),
                  "Consumes(sel=%u, ch=%u)", s, c);
}

// M3: the Cloth path — EON + LTC sheen (primary, albedo-corrected) + weak dielectric GGX (F0 ≈ 4 %).
void ProofCloth()
{
    std::printf("[furnace] M3 cloth path\n");
    const float kHarnessPi = 3.14159265358979f;
    auto ClothMaterial = [](vec3 albedo, float diffRough, float fuzzRough, float specRough = 0.5f) {
        ShadingRecord m = StandardMaterial(albedo, specRough);
        m.Selection = kReflectanceCloth;
        m.SpecularWeight = 0.0f; m.DiffuseRoughness = diffRough;
        m.FuzzWeight = 1.0f; m.FuzzColor = vec3(1.0f); m.FuzzRoughness = fuzzRough;
        return m;
    };

    // ① Cloth furnace ≤ 1 (white velvet + felt: the max-energy configs; EON ≤ ρ and the sheen layer scales the
    // stack below it, so this holds BY CONSTRUCTION — the proof guards the rescale + weak-lobe plumbing).
    for (float fuzzRough : { 0.35f, 0.8f })
        for (float muO : { 0.3f, 1.0f })
        {
            ShadingRecord m = ClothMaterial(vec3(1.0f), 1.0f, fuzzRough);
            vec3 wo = vec3(sqrt(1.0f - muO * muO), 0.0f, muO);
            ResolvedLayers L = ResolveLayers(m, wo);
            float acc = 0.0f;
            const int N = 80000;
            for (int i = 0; i < N; ++i) acc += EvaluateBsdf(m, L, wo, CosineSample(Rand01(), Rand01())).x;
            acc *= kHarnessPi / static_cast<float>(N);
            CHECK(acc <= 1.01f, "cloth albedo ≤ 1 (fuzzR=%.2f mu=%.1f E=%.4f)", fuzzRough, muO, acc);
        }

    // ② Retroreflective ordering, component + stack (θi = θo = 60°). Config: rough EON (its own retro term helps),
    // mid sheen (unclamped rescale), BROAD weak lobe — a mirror-smooth weak lobe forward-peaks by construction (silk
    // streaks are physical), so the ordering is asserted for the broad-weak cloth-typical case only. The plan's ">1
    // directionally" is permission, not mandate: ours stay < 0.35 (broad LTC + EON), and the binding bound is ①.
    {
        ShadingRecord m = ClothMaterial(vec3(1.0f), 1.0f, 0.65f, 1.0f);
        vec3 wo = vec3(0.86602540f, 0.0f, 0.5f);
        ResolvedLayers L = ResolveLayers(m, wo);
        vec3 wiRetro = wo, wiSide = vec3(0.0f, 0.86602540f, 0.5f), wiFwd = vec3(-0.86602540f, 0.0f, 0.5f);
        float sRetro = SheenEvaluate(vec3(1.0f), 0.65f, wo, wiRetro).x;
        float sSide  = SheenEvaluate(vec3(1.0f), 0.65f, wo, wiSide).x;
        float sFwd   = SheenEvaluate(vec3(1.0f), 0.65f, wo, wiFwd).x;
        CHECK(sRetro > sSide && sSide > sFwd, "sheen lobe retro-ordered (%.4f > %.4f > %.4f)", sRetro, sSide, sFwd);
        float eRetro = EvaluateBsdf(m, L, wo, wiRetro).x * 0.5f;
        float eSide  = EvaluateBsdf(m, L, wo, wiSide).x * 0.5f;
        float eFwd   = EvaluateBsdf(m, L, wo, wiFwd).x * 0.5f;
        std::printf("    retro=%.4f side=%.4f fwd=%.4f (stack f·cosθ)\n", eRetro, eSide, eFwd);
        CHECK(eRetro > eSide && eRetro > eFwd, "cloth stack retro-dominant");
    }

    // ②b The velvet signature (analytic, no MC): the sheen layer's weight rises steeply toward grazing — the rim
    // takes over from the diffuse, which is what reads as velvet. Compositional, so exact from the table.
    for (float fuzzRough : { 0.35f, 0.65f, 0.8f })
    {
        ShadingRecord m = ClothMaterial(vec3(1.0f), 1.0f, fuzzRough);
        float wGrazing = ResolveLayers(m, vec3(0.99498744f, 0.0f, 0.1f)).FuzzAlbedoO;
        float wNormal  = ResolveLayers(m, vec3(0.43588990f, 0.0f, 0.9f)).FuzzAlbedoO;
        std::printf("    fuzzR=%.2f sheen weight grazing=%.4f normal=%.4f (×%.1f)\n",
                    fuzzRough, wGrazing, wNormal, wGrazing / wNormal);
        CHECK(wGrazing > 2.5f * wNormal, "velvet signature (fuzzR=%.2f)", fuzzRough);
    }

    // ③ Sampling consistency of the cloth mixture (EON + rescaled LTC + weak VNDF).
    {
        ShadingRecord m = ClothMaterial(vec3(0.7f), 0.8f, 0.4f);
        vec3 wo = normalize(vec3(0.3f, 0.25f, 0.9f));
        ResolvedLayers L = ResolveLayers(m, wo);
        float ref = 0.0f;
        const int N0 = 120000;
        for (int i = 0; i < N0; ++i) ref += EvaluateBsdf(m, L, wo, CosineSample(Rand01(), Rand01())).x;
        ref *= kHarnessPi / static_cast<float>(N0);
        float acc = 0.0f;
        const int N = 150000;
        for (int i = 0; i < N; ++i)
        {
            vec4 s = SampleBsdf(m, L, wo, vec4(Rand01(), Rand01(), Rand01(), Rand01()));
            if (s.w <= 0.0f) continue;
            vec3 wi = s.xyz;
            acc += EvaluateBsdf(m, L, wo, wi).x * wi.z / s.w;
        }
        acc /= static_cast<float>(N);
        CHECK(std::fabs(acc - ref) / ref < 0.03f, "cloth mixture E[f·cos/pdf]=%.4f furnace=%.4f", acc, ref);
    }

    // ④ Per-lobe reciprocity (the full stack is non-reciprocal by design — same OpenPBR §3.10 scaling as M2 ④).
    {
        ShadingRecord m = ClothMaterial(vec3(0.6f), 0.7f, 0.5f);
        float worstGgx = 0.0f, worstEon = 0.0f;
        for (int i = 0; i < 2000; ++i)
        {
            vec3 wi = UniformHemisphere(Rand01(), Rand01());
            vec3 wo = UniformHemisphere(Rand01(), Rand01());
            ResolvedLayers lo = ResolveLayers(m, wo), li = ResolveLayers(m, wi);
            vec3 g1 = EvaluateBaseSpecular(m, lo, wo, wi), g2 = EvaluateBaseSpecular(m, li, wi, wo);
            worstGgx = max(worstGgx, std::fabs(g1.x - g2.x) / max(g1.x, 1e-3f));
            vec3 e1 = EonEvaluate(vec3(0.6f), 0.7f, wi, wo), e2 = EonEvaluate(vec3(0.6f), 0.7f, wo, wi);
            worstEon = max(worstEon, std::fabs(e1.x - e2.x) / max(e1.x, 1e-3f));
        }
        CHECK(worstGgx < 1e-3f, "weak-GGX reciprocal (worst %.2e)", worstGgx);
        CHECK(worstEon < 1e-3f, "EON reciprocal (worst %.2e)", worstEon);
        // Sheen: the TRUE Charlie D·V is reciprocal, but the LTC fetches coeffs at μo only — asymmetric by
        // construction (pre-existing, untouched by M3), worst at grazing pairs. Fixed Fibonacci grid (not the
        // shared RNG stream): worst-of over random pairs would drift whenever any earlier test is edited.
        auto FibDir = [](int i, int n) {
            float mu = 1.0f - (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
            float phi = 6.28318530718f * static_cast<float>(i) * 0.61803398875f;
            float s = sqrt(1.0f - mu * mu);
            return vec3(s * cos(phi), s * sin(phi), mu);
        };
        float worstSheen = 0.0f;
        const int kFibN = 24;
        for (int i = 0; i < kFibN; ++i)
            for (int j = 0; j < kFibN; ++j)
            {
                if (i == j) continue;   // self-pairs are trivially reciprocal
                vec3 wo = FibDir(i, kFibN), wi = FibDir(j, kFibN);
                vec3 s1 = SheenEvaluate(vec3(1.0f), 0.5f, wo, wi), s2 = SheenEvaluate(vec3(1.0f), 0.5f, wi, wo);
                worstSheen = max(worstSheen, std::fabs(s1.x - s2.x) / max(s1.x, 1e-3f));
            }
        std::printf("    sheen reciprocity worst (Fibonacci 24²): %.3f\n", worstSheen);
        CHECK(worstSheen < 1.6f, "LTC sheen asymmetry bounded (fit artifact, grazing-driven; grid-worst 1.24)");
    }

    // ⑤ The weak lobe's F0 is the true dielectric 0.04 (η = 1.5) — and the forcing is selection-gated (weight-0
    // non-cloth still collapses to eta 1 / F0 0, exactly as before M3).
    {
        ShadingRecord m = ClothMaterial(vec3(0.5f), 0.5f, 0.5f);
        ResolvedLayers L = ResolveLayers(m, vec3(0.0f, 0.0f, 1.0f));
        CHECK(std::fabs(L.SpecularEta - 1.5f) < 1e-6f, "cloth eta unmodulated (%.7f)", L.SpecularEta);
        CHECK(std::fabs(L.DielectricF0.x - 0.04f) < 1e-6f, "cloth F0 = 0.04 (%.7f)", L.DielectricF0.x);
        ShadingRecord s = StandardMaterial(vec3(0.5f), 0.5f);
        s.SpecularWeight = 0.0f;   // Selection 0, weight 0: pre-M3 behaviour bit-preserved
        ResolvedLayers Ls = ResolveLayers(s, vec3(0.0f, 0.0f, 1.0f));
        CHECK(Ls.SpecularEta == 1.0f && Ls.DielectricF0.x == 0.0f, "non-cloth weight-0 still collapses");
        CHECK(Ls.SheenRescale == 1.0f, "non-cloth rescale exactly 1");
    }

    // ⑥ Rescale end-to-end: layer weight matches the baked texel, and the sheen (eval, pdf) pair integrates to it —
    // in BOTH regimes (0.35: hi-clamped, 0.8: unclamped-lo). The ratio is near-constant per sample, hence the 0.5 %.
    for (float fuzzRough : { 0.35f, 0.8f })
    {
        ShadingRecord m = ClothMaterial(vec3(0.7f), 0.8f, fuzzRough);
        vec3 wo = vec3(0.8f, 0.0f, 0.6f);
        ResolvedLayers L = ResolveLayers(m, wo);
        float full[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        Frontier::ShadingTableCodec::SampleSheenFull(*g_Tables, 0.6f, fuzzRough, full);
        float expect = full[3] / max(full[2], 1e-4f);
        expect = expect < 0.5f ? 0.5f : (expect > 2.0f ? 2.0f : expect);
        CHECK(std::fabs(L.SheenRescale - expect) < 1e-6f, "rescale matches bake (fuzzR=%.2f: %.6f)", fuzzRough, L.SheenRescale);
        CHECK(std::fabs(L.FuzzAlbedoO - expect * full[2]) < 1e-6f, "layer weight = F·rescale·R (%.6f)", L.FuzzAlbedoO);
        CHECK(L.FuzzAlbedoO <= 1.0f, "sheen layer weight ≤ 1");
        float acc = 0.0f;
        const int N = 50000;
        for (int i = 0; i < N; ++i)
        {
            vec4 s = SheenSample(fuzzRough, wo, vec2(Rand01(), Rand01()));
            vec3 wi = s.xyz;
            acc += L.SheenRescale * SheenEvaluate(vec3(1.0f), fuzzRough, wo, wi).x * wi.z / s.w;
        }
        acc /= static_cast<float>(N);
        CHECK(std::fabs(acc - expect * full[2]) / (expect * full[2]) < 0.005f,
              "sheen E[f·cos/pdf]=%.5f albedo=%.5f (fuzzR=%.2f)", acc, expect * full[2], fuzzRough);
    }

    // ⑦ Anisotropy is ignored under Cloth (specular-aniso + angle forced out): isotropic alpha, identity basis,
    // and bit-identical eval to the unrotated record.
    {
        ShadingRecord m = ClothMaterial(vec3(0.5f), 0.5f, 0.5f);
        m.SpecularAnisotropy = 0.7f; m.AnisotropyAngle = 0.5f;
        ShadingRecord m0 = ClothMaterial(vec3(0.5f), 0.5f, 0.5f);
        vec3 wo = normalize(vec3(0.2f, 0.3f, 0.9f));
        ResolvedLayers L = ResolveLayers(m, wo);
        CHECK(L.SpecularAlpha.x == L.SpecularAlpha.y, "cloth specular alpha isotropic");
        float worstBasis = 0.0f;
        for (int i = 0; i < 100; ++i)
        {
            vec3 v = UniformHemisphere(Rand01(), Rand01());
            vec3 t = L.AnisoBasis * v;
            worstBasis = max(worstBasis, std::fabs(t.x - v.x) + std::fabs(t.y - v.y) + std::fabs(t.z - v.z));
        }
        CHECK(worstBasis < 1e-6f, "cloth aniso basis identity (worst %.2e)", worstBasis);
        ResolvedLayers L0 = ResolveLayers(m0, wo);
        bool identical = true;
        for (int i = 0; i < 100; ++i)
        {
            vec3 wi = UniformHemisphere(Rand01(), Rand01());
            vec3 f1 = EvaluateBsdf(m, L, wo, wi), f0 = EvaluateBsdf(m0, L0, wo, wi);
            identical = identical && (f1.x == f0.x && f1.y == f0.y && f1.z == f0.z);
        }
        CHECK(identical, "aniso+angle bit-inert under Cloth");
    }

    // ⑧ The branch is live: identical weights shade differently by selection alone.
    {
        ShadingRecord m = ClothMaterial(vec3(0.5f), 0.5f, 0.5f);
        ShadingRecord s = m; s.Selection = 0u;
        vec3 wo = normalize(vec3(0.2f, 0.2f, 0.9f));
        vec3 wi = normalize(vec3(-0.3f, 0.1f, 0.8f));
        vec3 fc = EvaluateBsdf(m, ResolveLayers(m, wo), wo, wi);
        vec3 fs = EvaluateBsdf(s, ResolveLayers(s, wo), wo, wi);
        float d = std::fabs(fc.x - fs.x) + std::fabs(fc.y - fs.y) + std::fabs(fc.z - fs.z);
        float r = std::fabs(fs.x) + std::fabs(fs.y) + std::fabs(fs.z);
        CHECK(d / max(r, 1e-6f) > 1e-3f, "cloth ≠ standard by selection (rel %.2e)", d / max(r, 1e-6f));
    }
}

// M4: Walter-2007 single-interface transmission — η² reciprocity + the exact R/T split (thick form, furnace-only).
// M4b: one slab-walk path through two parallel planes (entry z = 0, exit z = −depth): VNDF-sampled interfaces with
// exact Fresnel splits, Beer over the true interior segments, the FULL internal series (internal-R re-walks, TIR
// bounces continue, 64-segment cap absorbs). Below-horizon R crosses the plane into the next medium — the exhibit's
// uniform rule (wi.z < 0 ⟺ medium transition). Returns the R/T weights; trapped accumulates cap-absorbed weight.
void SlabWalk(vec3 wo, vec2 a, float ior, vec3 sigma, float depth, vec3& Rout, vec3& Tout, vec3& trappedW)
{
    Rout = vec3(0.0f); Tout = vec3(0.0f); trappedW = vec3(0.0f);
    vec3 w(1.0f);
    int plane = 0, side = 0;   // plane: 0 entry / 1 exit; side: 0 air / 1 glass. Start: (entry, air).
    vec3 wl = wo;              // local wo in the incident-side frame (+z = incident normal)
    for (int seg = 0; seg < 64; ++seg)
    {
        float etaO = (side == 0) ? 1.0f : ior;
        float etaI = (side == 0) ? ior : 1.0f;
        vec3 h = SampleGgxVndf(wl, a, vec2(Rand01(), Rand01()));
        float F = FresnelDielectric(std::fabs(dot(wl, h)), etaI / etaO);
        float dvis = GgxVndfPdf(wl, h, a);
        bool takeT = Rand01() < 1.0f - F;
        vec4 refr = takeT ? RefractDielectric(-wl, h, etaO / etaI) : vec4(0.0f);
        if (takeT && refr.w < 0.0f) takeT = false;   // TIR paranoia (F = 1 already killed these)
        vec3 wi;
        float pw;
        if (takeT)
        {
            wi = refr.xyz;
            float dnom = etaO * dot(wl, h) + etaI * dot(wi, h);
            float jac = (etaI * etaI) * std::fabs(dot(wi, h)) / (dnom * dnom + 1e-12f);
            float pdf = (1.0f - F) * dvis * jac;
            vec3 f = TransmissionEvaluateSingle(wl, wi, a, etaO, etaI);
            pw = f.x * std::fabs(wi.z) / std::max(pdf, 1e-12f);
        }
        else
        {
            wi = reflect(-wl, h);
            float pdf = F * dvis / (4.0f * std::max(dot(wl, h), 1e-4f));
            float d = F * GgxD(h, a) * GgxG2(wl, wi, a) / (4.0f * wl.z * std::max(std::fabs(wi.z), 1e-4f));
            pw = d * std::fabs(wi.z) / std::max(pdf, 1e-12f);
        }
        w = w * pw;
        if (wi.z >= 0.0f)
        {
            if (side == 0) { Rout = Rout + w; return; }   // R out to air (entry, first or only hit)
            // Interior reflection: travel across the slab to the other plane (glass segment — Beer it).
            w = w * exp(-sigma * (depth / std::max(std::fabs(wi.z), 1e-4f)));
            plane = 1 - plane;
            // Arrival reframe: exit-glass is identity-up, entry-glass is z-flipped — either way the two flips
            // cancel and wl = (−wi.x, −wi.y, +wi.z) (verified by hand for both arrival planes; the slab
            // analytic below arbitrates: a sign error here fails R AND T at oblique incidence).
            wl = vec3(-wi.x, -wi.y, wi.z);
        }
        else
        {
            if (side == 1)   // crosses out of the glass: forward (exit plane) = T, backward (entry plane) = R
            {
                if (plane == 1) Tout = Tout + w; else Rout = Rout + w;
                return;
            }
            // Entry from air: travel into the slab to the exit plane (glass segment — Beer it).
            w = w * exp(-sigma * (depth / std::max(std::fabs(wi.z), 1e-4f)));
            plane = 1; side = 1;
            wl = vec3(-wi.x, -wi.y, -wi.z);
        }
    }
    trappedW = trappedW + w;   // cap: TIR-trapped (clear-rough only — smooth asserts exact 0)
}

// Closed-form smooth-slab R/T: entry-T (1−F1) → Beer B → exit-T (1−F2), internal round-trips ×F1·F2·B² each
// (F1 = F2 by Stokes). Clear: T + R = 1 exactly; tinted: both chromatic through B (R carries Beer²).
void SlabAnalytic(float muO, float ior, vec3 sigma, float depth, vec3& Rwant, vec3& Twant)
{
    float F1 = FresnelDielectric(muO, ior);
    float muIn = sqrt(std::max(1.0f - (1.0f - muO * muO) / (ior * ior), 1e-6f));
    vec3 B = exp(-sigma * (depth / muIn));
    vec3 denom = vec3(1.0f) - vec3(F1 * F1) * B * B;
    Twant = vec3((1.0f - F1) * (1.0f - F1)) * B / denom;
    Rwant = vec3(F1) + vec3((1.0f - F1) * (1.0f - F1) * F1) * B * B / denom;
}

void ProofTransmissionSolid()
{
    std::printf("[furnace] M4b solid glass (single-interface entry/exit + slab walk + Beer)\n");
    const float ior = 1.5f;
    // ①a Entry sampler: SampleBsdf solid-outside through the REAL mixture (lobe pick + R/T split) vs the block-②
    // numeric integral. EON-above samples fill the underside-R the VNDF hand-rolled estimator misses (~+0.7 %),
    // so E sits ~0.007 above ②'s E and the gap shrinks by the same — same bounds, independent sampler.
    for (float muO : { 0.5f, 1.0f })
    {
        ShadingRecord m = GlassMaterial(0.5f);
        vec3 wo = vec3(std::sqrt(1.0f - muO * muO), 0.0f, muO);
        ResolvedLayers Lr = ResolveLayers(m, wo);
        Lr.SolidInterface = true; Lr.IncidentIor = 1.0f;   // tracer context: solid boundary, ray in air
        vec3 eTab = FetchEnergy(muO, 0.25f);
        float Ess = eTab.x + eTab.y;
        float acc = 0.0f;
        const int N = 200000;
        for (int i = 0; i < N; ++i)
        {
            vec4 S = SampleBsdf(m, Lr, wo, vec4(Rand01(), Rand01(), Rand01(), Rand01()));
            if (S.w <= 0.0f) continue;
            vec3 f = EvaluateBsdf(m, Lr, wo, S.xyz);
            acc += f.x * std::fabs(S.z) / S.w;
        }
        float E = acc / static_cast<float>(N);
        CHECK(E <= 1.005f, "solid-entry ceiling (mu=%.1f E=%.4f)", muO, E);
        CHECK(E >= Ess - 0.02f, "solid-entry floor (mu=%.1f E=%.4f Ess=%.4f)", muO, E, Ess);
        float numR = 0.0f, numT = 0.0f;
        const int N0 = 400000;
        vec2 a = AnisotropicAlpha(0.5f, 0.0f);
        for (int i = 0; i < N0; ++i)
        {
            vec3 up = CosineSample(Rand01(), Rand01());
            vec3 hr = normalize(wo + up);
            float fr = FresnelDielectric(std::fabs(dot(wo, hr)), ior);
            numR += fr * GgxD(hr, a) * GgxG2(wo, up, a) / (4.0f * wo.z * up.z);
            vec3 dn = -CosineSample(Rand01(), Rand01());
            numT += TransmissionEvaluateSingle(wo, dn, a, 1.0f, ior).x;
        }
        float Enum = 3.14159265358979f * (numR + numT) / static_cast<float>(N0);
        float gap = Enum - E;
        CHECK(gap > 0.01f && gap < 0.09f, "solid-entry gap = ② gap (mu=%.1f gap=%.4f)", muO, gap);
    }
    // ① Exit sampler: SampleBsdf solid-inside. TIR keeps most energy in R (F = 1 kills the T-branch exactly);
    // smooth anchors are tight-principled (G → 1, F exact: R + T = 1 ± 0.005); rough checks bound + document.
    for (float muO : { 0.5f, 1.0f })
    {
        ShadingRecord m = GlassMaterial(0.5f);
        vec3 wo = vec3(std::sqrt(1.0f - muO * muO), 0.0f, muO);   // inside frame: +z = interior normal
        ResolvedLayers Lr = ResolveLayers(m, wo);
        Lr.SolidInterface = true; Lr.IncidentIor = Lr.SpecularEta;   // tracer context: ray inside the glass
        float acc = 0.0f;
        const int N = 200000;
        for (int i = 0; i < N; ++i)
        {
            vec4 S = SampleBsdf(m, Lr, wo, vec4(Rand01(), Rand01(), Rand01(), Rand01()));
            if (S.w <= 0.0f) continue;
            vec3 f = EvaluateBsdf(m, Lr, wo, S.xyz);
            acc += f.x * std::fabs(S.z) / S.w;
        }
        float E = acc / static_cast<float>(N);
        CHECK(E <= 1.005f, "solid-exit ceiling (mu=%.1f E=%.4f)", muO, E);
        if (muO < 0.75f)   // TIR regime: E tracks the F0 = 1 single-scatter albedo (measured 0.8238 vs Ess 0.8579)
        {
            vec3 eTab = FetchEnergy(muO, 0.25f);
            float Ess = eTab.x + eTab.y;
            CHECK(E <= Ess + 0.01f, "solid-exit TIR ceiling (mu=%.1f E=%.4f Ess=%.4f)", muO, E, Ess);
            CHECK(E >= Ess - 0.05f, "solid-exit TIR floor (mu=%.1f E=%.4f Ess=%.4f)", muO, E, Ess);
        }
        else
            CHECK(E >= 0.90f, "solid-exit floor (mu=%.1f E=%.4f)", muO, E);
        float numR = 0.0f, numT = 0.0f;
        const int N0 = 400000;
        vec2 a = AnisotropicAlpha(0.5f, 0.0f);
        for (int i = 0; i < N0; ++i)
        {
            vec3 up = CosineSample(Rand01(), Rand01());
            vec3 hr = normalize(wo + up);
            float fr = FresnelDielectric(std::fabs(dot(wo, hr)), 1.0f / ior);   // relative eta: TIR-aware
            numR += fr * GgxD(hr, a) * GgxG2(wo, up, a) / (4.0f * wo.z * up.z);
            vec3 dn = -CosineSample(Rand01(), Rand01());
            numT += TransmissionEvaluateSingle(wo, dn, a, ior, 1.0f).x;
        }
        float Enum = 3.14159265358979f * (numR + numT) / static_cast<float>(N0);
        float gap = Enum - E;
        CHECK(gap > 0.005f && gap < 0.15f, "solid-exit gap bounded (mu=%.1f gap=%.4f Enum=%.4f)", muO, gap, Enum);
    }
    for (float muO : { 0.5f, 1.0f })   // smooth exit anchors: μ=0.5 is all-TIR, μ=1.0 sub-critical — both MUST be 1
    {
        ShadingRecord m = GlassMaterial(0.05f);
        vec3 wo = vec3(std::sqrt(1.0f - muO * muO), 0.0f, muO);
        ResolvedLayers Lr = ResolveLayers(m, wo);
        Lr.SolidInterface = true; Lr.IncidentIor = Lr.SpecularEta;
        float acc = 0.0f;
        const int N = 100000;
        for (int i = 0; i < N; ++i)
        {
            vec4 S = SampleBsdf(m, Lr, wo, vec4(Rand01(), Rand01(), Rand01(), Rand01()));
            if (S.w <= 0.0f) continue;
            vec3 f = EvaluateBsdf(m, Lr, wo, S.xyz);
            acc += f.x * std::fabs(S.z) / S.w;
        }
        float E = acc / static_cast<float>(N);
        CHECK(std::fabs(E - 1.0f) < 0.02f, "solid-exit smooth anchor (mu=%.1f E=%.4f)", muO, E);
    }
    // ② Slab analytic: smooth unit slab (VNDF walk vs closed form incl. the internal series). Clear: R + T = 1;
    // tinted: per-channel Beer (R carries Beer² through the internal round-trip). Trapped is ~1e-4, not 0: rare
    // 4°-facets (≈1e-4 of VNDF draws at r = 0.05) land beyond-critical and TIR-loop (smooth jitter can't randomise
    // them out within 64 segments — the guided-mode analogue; an infinite slab has no sides to leak them).
    for (float muO : { 0.5f, 1.0f })
    {
        for (int tinted : { 0, 1 })
        {
            ShadingRecord m = GlassMaterial(0.05f);
            if (tinted) { m.TransmissionColor = vec3(0.5f, 0.7f, 0.9f); m.TransmissionDepth = 1.0f; }
            vec3 wo = vec3(std::sqrt(1.0f - muO * muO), 0.0f, muO);
            ResolvedLayers Lr = ResolveLayers(m, wo);   // σ via the resolve (tests the −ln/depth mapping too)
            vec2 a = AnisotropicAlpha(0.05f, 0.0f);
            vec3 R(0.0f), T(0.0f), trap(0.0f);
            const int N = 100000;
            for (int i = 0; i < N; ++i)
            {
                vec3 r, t, tr;
                SlabWalk(wo, a, ior, Lr.TransmissionSigma, 1.0f, r, t, tr);
                R = R + r; T = T + t; trap = trap + tr;
            }
            R = R / float(N); T = T / float(N);
            vec3 Rw, Tw;
            SlabAnalytic(muO, ior, Lr.TransmissionSigma, 1.0f, Rw, Tw);
            if (tinted)
            {
                CHECK(std::fabs(T.x - Tw.x) < 0.01f && std::fabs(T.y - Tw.y) < 0.01f && std::fabs(T.z - Tw.z) < 0.01f,
                      "slab T tinted (mu=%.1f T=%.3f/%.3f/%.3f want=%.3f/%.3f/%.3f)", muO, T.x, T.y, T.z, Tw.x, Tw.y, Tw.z);
                CHECK(std::fabs(R.x - Rw.x) < 0.01f && std::fabs(R.y - Rw.y) < 0.01f && std::fabs(R.z - Rw.z) < 0.01f,
                      "slab R tinted (mu=%.1f R=%.3f/%.3f/%.3f want=%.3f/%.3f/%.3f)", muO, R.x, R.y, R.z, Rw.x, Rw.y, Rw.z);
            }
            else
            {
                CHECK(std::fabs(T.x - Tw.x) < 0.01f, "slab T clear (mu=%.1f T=%.4f want=%.4f)", muO, T.x, Tw.x);
                CHECK(std::fabs(R.x - Rw.x) < 0.01f, "slab R clear (mu=%.1f R=%.4f want=%.4f)", muO, R.x, Rw.x);
            }
            float trapLum = (trap.x + trap.y + trap.z) / (3.0f * static_cast<float>(N));
            CHECK(trapLum < 2e-3f, "slab walk terminates (mu=%.1f tint=%d trapped=%.1e)", muO, tinted, trapLum);
        }
    }
    // ③ Beer sweep: tinted unit slab at normal incidence, attenuation distance × {0.5, 1, 2} — the T(σ) curve.
    for (float depth : { 0.5f, 1.0f, 2.0f })
    {
        ShadingRecord m = GlassMaterial(0.05f);
        m.TransmissionColor = vec3(0.5f, 0.7f, 0.9f); m.TransmissionDepth = depth;
        vec3 wo = vec3(0.0f, 0.0f, 1.0f);
        ResolvedLayers Lr = ResolveLayers(m, wo);
        vec2 a = AnisotropicAlpha(0.05f, 0.0f);
        vec3 R(0.0f), T(0.0f), trap(0.0f);
        const int N = 100000;
        for (int i = 0; i < N; ++i)
        {
            vec3 r, t, tr;
            SlabWalk(wo, a, ior, Lr.TransmissionSigma, 1.0f, r, t, tr);
            R = R + r; T = T + t; trap = trap + tr;
        }
        T = T / float(N);
        vec3 Rw, Tw;
        SlabAnalytic(1.0f, ior, Lr.TransmissionSigma, 1.0f, Rw, Tw);
        float lum = (T.x + T.y + T.z) / 3.0f, lumW = (Tw.x + Tw.y + Tw.z) / 3.0f;
        CHECK(std::fabs(lum - lumW) < 0.008f, "slab Beer sweep (depth=%.1f T=%.4f want=%.4f)", depth, lum, lumW);
    }
    // ④ Rough-slab bounds: R + T ≤ 1 is rigorous (nested ≤1 lobe integrals × Beer ≤ 1, exclusive R/T per path);
    // the floor is empirical-loose: TIR-loop shadowing (~6 % at oblique) + reachable-only entry/exit drops
    // (~7 % + ~6 %, the ② phenomenon on both faces) + missing compound (~4 %) + trapped (< 2 %) ≈ 19 % total
    // (measured E = 0.7834 at mu = 0.5, inside the arithmetic). Trapped stays small (facets randomise TIR loops
    // out within a few bounces).
    for (float muO : { 0.5f, 1.0f })
    {
        ShadingRecord m = GlassMaterial(0.5f);
        vec3 wo = vec3(std::sqrt(1.0f - muO * muO), 0.0f, muO);
        ResolvedLayers Lr = ResolveLayers(m, wo);
        vec2 a = AnisotropicAlpha(0.5f, 0.0f);
        vec3 R(0.0f), T(0.0f), trap(0.0f);
        const int N = 200000;
        for (int i = 0; i < N; ++i)
        {
            vec3 r, t, tr;
            SlabWalk(wo, a, ior, Lr.TransmissionSigma, 1.0f, r, t, tr);
            R = R + r; T = T + t; trap = trap + tr;
        }
        float E = (R.x + T.x) / float(N), trapLum = (trap.x + trap.y + trap.z) / (3.0f * float(N));
        CHECK(E <= 1.005f, "rough-slab ceiling (mu=%.1f E=%.4f)", muO, E);
        CHECK(E >= 0.75f, "rough-slab floor (mu=%.1f E=%.4f)", muO, E);
        CHECK(trapLum < 0.02f, "rough-slab trapped (mu=%.1f trapped=%.4f)", muO, trapLum);
    }
    // ⑤ f/p identity on fixed Snell pairs (no MC): f·|cos|/p = G₁(wi)·(1−F_eval)/(1−F_branch). Entry: the eval and
    // branch Fresnel are the same rare-side call (Stokes-equal to float noise); exit: conditioned (rare-side) vs
    // direct (dense-side) — Stokes-equal to ~1e-3 near-TIR (the η² tolerance's source), exact elsewhere.
    {
        vec2 a = AnisotropicAlpha(0.35f, 0.0f);
        int tested = 0;
        for (vec3 wo : { normalize(vec3(0.0f, 0.0f, 1.0f)), normalize(vec3(0.4f, 0.2f, 0.9f)) })
        {
            for (vec3 raw : { vec3(0.05f, 0.02f, 1.0f), vec3(0.3f, -0.2f, 1.0f), vec3(-0.25f, 0.35f, 1.0f) })
            {
                vec3 m = normalize(raw);
                vec4 rr = RefractDielectric(-wo, m, 1.0f / ior);
                if (rr.w < 0.0f) continue;   // air → glass never TIRs; paranoia like the sampler
                vec3 wi = rr.xyz;
                float Fb = FresnelDielectric(std::fabs(dot(wo, m)), ior);
                float Fe = TransmissionFresnel(std::fabs(dot(wo, m)), std::fabs(dot(wi, m)), 1.0f, ior);
                vec3 f = TransmissionEvaluateSingle(wo, wi, a, 1.0f, ior);
                float p = TransmissionPdfSingle(wo, wi, a, 1.0f, ior, 1.0f - Fb);
                float wGot = f.x * std::fabs(wi.z) / std::max(p, 1e-12f);
                float wWant = GgxG1(wi, a) * (1.0f - Fe) / std::max(1.0f - Fb, 1e-6f);
                CHECK(std::fabs(wGot - wWant) / std::max(wWant, 1e-6f) < 1e-5f,
                      "single-entry f/p got=%.6f want=%.6f", wGot, wWant);
                ++tested;
            }
        }
        CHECK(tested == 6, "single-entry f/p ran on all 6 pairs (%d)", tested);
    }
    {
        vec2 a = AnisotropicAlpha(0.35f, 0.0f);
        int tested = 0, skipped = 0;
        for (vec3 wo : { normalize(vec3(0.0f, 0.0f, 1.0f)), normalize(vec3(0.7f, 0.1f, 0.7f)) })
        {
            for (vec3 raw : { vec3(0.05f, 0.02f, 1.0f), vec3(0.3f, -0.2f, 1.0f), vec3(-0.25f, 0.35f, 1.0f), vec3(0.9f, 0.0f, 0.45f) })
            {
                vec3 m = normalize(raw);
                vec4 rr = RefractDielectric(-wo, m, ior / 1.0f);
                if (rr.w < 0.0f) { ++skipped; continue; }   // beyond-critical: T = 0 exactly, no pair to test
                vec3 wi = rr.xyz;
                float Fb = FresnelDielectric(std::fabs(dot(wo, m)), 1.0f / ior);
                float Fe = TransmissionFresnel(std::fabs(dot(wo, m)), std::fabs(dot(wi, m)), ior, 1.0f);
                vec3 f = TransmissionEvaluateSingle(wo, wi, a, ior, 1.0f);
                float p = TransmissionPdfSingle(wo, wi, a, ior, 1.0f, 1.0f - Fb);
                float wGot = f.x * std::fabs(wi.z) / std::max(p, 1e-12f);
                float wWant = GgxG1(wi, a) * (1.0f - Fe) / std::max(1.0f - Fb, 1e-6f);
                CHECK(std::fabs(wGot - wWant) / std::max(wWant, 1e-6f) < 2e-3f,
                      "single-exit f/p got=%.6f want=%.6f", wGot, wWant);
                ++tested;
            }
        }
        CHECK(tested >= 5, "single-exit f/p ran (tested=%d skipped=%d)", tested, skipped);
    }
}

void ProofTransmissionSingle()
{
    std::printf("[furnace] M4 single-interface BTDF (eta2 reciprocity + R+T=1)\n");
    // ① η² reciprocity: f(wo→wi)/ηi² is invariant under the full exchange (wo↔wi, ηo↔ηi) — arbitrates the η² numerator.
    // The formula is only valid over Snell-consistent pairs, so TIR pairs (fwd = 0) and near-zero-lobe pairs are
    // skipped: beyond-critical internal rays correspond to no real air-side ray (evanescent), not to a violation.
    for (float ior : { 1.1f, 1.5f, 2.0f })
    {
        float worst = 0.0f;
        int tested = 0, skipped = 0;
        const int N = 5000;
        for (int i = 0; i < N; ++i)
        {
            vec3 wo = UniformHemisphere(Rand01(), Rand01());
            if (wo.z < 0.05f) { ++skipped; continue; }
            vec3 wiB = UniformHemisphere(Rand01(), Rand01()); wiB.z = -wiB.z;
            if (wiB.z > -0.05f) { ++skipped; continue; }
            vec2 a = AnisotropicAlpha(0.15f + 0.7f * Rand01(), 0.4f * Rand01() - 0.2f);
            vec3 fwd = TransmissionEvaluateSingle(wo, wiB, a, 1.0f, ior);
            if (fwd.x <= 0.0f) { ++skipped; continue; }   // TIR-side pair: unreachable, not a violation
            vec4 hv = TransmissionHalfVector(wo, wiB, 1.0f, ior);
            float fSide = FresnelDielectric(std::fabs(dot(wiB, hv.xyz)), 1.0f / ior);
            if (fSide > 0.999f) { ++skipped; continue; }   // near-TIR: (1−F) float cancellation, not physics
            vec3 woS = vec3(-wiB.x, -wiB.y, -wiB.z);
            vec3 wiS = vec3(-wo.x, -wo.y, -wo.z);
            vec3 bwd = TransmissionEvaluateSingle(woS, wiS, a, ior, 1.0f);
            float lhs = fwd.x / (ior * ior), rhs = bwd.x;
            float denom = std::fabs(lhs) + std::fabs(rhs);
            if (denom < 1e-6f) { ++skipped; continue; }
            worst = std::max(worst, std::fabs(lhs - rhs) / denom);
            ++tested;
        }
        CHECK(tested > 500, "eta2 reciprocity ran (ior=%.1f tested=%d skipped=%d)", ior, tested, skipped);
        CHECK(worst < 2e-3f, "eta2 reciprocity ior=%.1f worst-rel-diff=%.2e", ior, worst);
    }
    // ② Macro closure through the branch estimator (VNDF facet + microfacet-Fresnel R/T pick). The micro-split is
    // exact BY CONSTRUCTION (P(R|m) + P(T|m) = 1 per facet); the macro statement is the rigorous two-sided bound
    // E_ss − 0.02 ≤ R_ss+T_ss ≤ 1.005. The ceiling is hard (branch weights ≤ 1, VNDF normalised); the floor is
    // empirical but principled — transmitted rays bend toward the normal and shadow LESS than reflected ones, so T
    // systematically exceeds its (1−F)·E_ss share (measured +4–12 % at rough-oblique, → 0 smooth).
    // REACHABLE vs FULL (T_ms termination note): the branch estimator integrates over VNDF-reachable wi only
    // (p_T = 0 where wo·ht(wo,wi) < 0 — opposed facets carry no VNDF mass). The D·G lobe form still assigns ~6.8 %
    // there (opposed-facet T + underside-facet R — the η²-required |wo·m|·|wi·m| symmetry FORBIDS gating it out:
    // any wo·m > 0 gate breaks the η² exchange, and a μo-dependent normaliser breaks it too). So rendering energy
    // is environment-dependent: BSDF-only paths see E ≈ 0.96, while a furnace environment (NEE covering unreachable
    // wi with MIS weight exactly 1) sees E_num ≈ 1.03. A uniform second lobe provably cannot close both (it adds to
    // the over-end 4× faster than to the under-end — reachable fraction ≈ 0.23); SS-only is minimax-optimal
    // (spread [0.959, 1.028], centre 0.993). The numeric bounds below pin the invisible-facet mass so any drift fails.
    for (float rough : { 0.15f, 0.5f })
    {
        vec2 a = AnisotropicAlpha(rough, 0.0f);
        for (float muO : { 0.5f, 1.0f })
        {
            vec3 wo = vec3(std::sqrt(1.0f - muO * muO), 0.0f, muO);
            const float ior = 1.5f;
            vec3 eTab = FetchEnergy(muO, rough * rough);
            float Ess = eTab.x + eTab.y;
            float acc = 0.0f;
            const int N = 200000;
            for (int i = 0; i < N; ++i)
            {
                vec3 h = SampleGgxVndf(wo, a, vec2(Rand01(), Rand01()));
                float F = FresnelDielectric(std::fabs(dot(wo, h)), ior);
                float dvis = GgxVndfPdf(wo, h, a);
                if (Rand01() < 1.0f - F)   // T-branch: refract into glass (air → glass never TIRs)
                {
                    vec4 r = RefractDielectric(-wo, h, 1.0f / ior);
                    vec3 wi = r.xyz;
                    float dnom = dot(wo, h) + ior * dot(wi, h);
                    float jac = (ior * ior) * std::fabs(dot(wi, h)) / (dnom * dnom + 1e-12f);
                    float pdf = (1.0f - F) * dvis * jac;
                    vec3 f = TransmissionEvaluateSingle(wo, wi, a, 1.0f, ior);
                    acc += f.x * std::fabs(wi.z) / std::max(pdf, 1e-12f);
                }
                else   // R-branch (below-horizon reflects contribute 0 — kept, not skipped: unbiased)
                {
                    vec3 wi = reflect(-wo, h);
                    if (wi.z <= 0.0f) continue;
                    float pdf = F * dvis / (4.0f * std::max(dot(wo, h), 1e-4f));
                    float d = F * GgxD(h, a) * GgxG2(wo, wi, a) / (4.0f * wo.z * wi.z);
                    acc += d * wi.z / std::max(pdf, 1e-12f);
                }
            }
            float E = acc / static_cast<float>(N);
            CHECK(E <= 1.005f, "R_ss+T_ss ceiling (r=%.2f mu=%.1f E=%.4f)", rough, muO, E);
            CHECK(E >= Ess - 0.02f, "R_ss+T_ss floor (r=%.2f mu=%.1f E=%.4f Ess=%.4f)", rough, muO, E, Ess);
            if (rough > 0.3f)   // broad lobe: the cosine furnace is quiet; pins the invisible-facet over-closure
            {
                float numR = 0.0f, numT = 0.0f;
                const int N0 = 400000;
                for (int i = 0; i < N0; ++i)
                {
                    vec3 up = CosineSample(Rand01(), Rand01());
                    vec3 hr = normalize(wo + up);
                    float fr = FresnelDielectric(std::fabs(dot(wo, hr)), ior);
                    numR += fr * GgxD(hr, a) * GgxG2(wo, up, a) / (4.0f * wo.z * up.z);
                    vec3 dn = -CosineSample(Rand01(), Rand01());
                    numT += TransmissionEvaluateSingle(wo, dn, a, 1.0f, ior).x;
                }
                float Enum = 3.14159265358979f * (numR + numT) / static_cast<float>(N0);
                float gap = Enum - E;
                CHECK(Enum > 1.005f && Enum < 1.06f,
                      "single numeric over-closure (r=%.2f mu=%.1f E=%.4f)", rough, muO, Enum);
                CHECK(gap > 0.01f && gap < 0.10f,
                      "invisible-facet mass bounded (r=%.2f mu=%.1f gap=%.4f)", rough, muO, gap);
            }
        }
    }
    // ②c Eta-sweep: the single-interface lobe carries no baked tables, so off-eta behaviour is bounded, not tight.
    for (float ior : { 1.1f, 2.0f })
    {
        vec2 a = AnisotropicAlpha(0.5f, 0.0f);
        vec3 wo = vec3(std::sqrt(1.0f - 0.25f), 0.0f, 0.5f);
        float acc = 0.0f;
        const int N = 100000;
        for (int i = 0; i < N; ++i)
        {
            vec3 h = SampleGgxVndf(wo, a, vec2(Rand01(), Rand01()));
            float F = FresnelDielectric(std::fabs(dot(wo, h)), ior);
            float dvis = GgxVndfPdf(wo, h, a);
            if (Rand01() < 1.0f - F)
            {
                vec4 r = RefractDielectric(-wo, h, 1.0f / ior);
                vec3 wi = r.xyz;
                float dnom = dot(wo, h) + ior * dot(wi, h);
                float jac = (ior * ior) * std::fabs(dot(wi, h)) / (dnom * dnom + 1e-12f);
                float pdf = (1.0f - F) * dvis * jac;
                vec3 f = TransmissionEvaluateSingle(wo, wi, a, 1.0f, ior);
                acc += f.x * std::fabs(wi.z) / std::max(pdf, 1e-12f);
            }
            else
            {
                vec3 wi = reflect(-wo, h);
                if (wi.z <= 0.0f) continue;
                float pdf = F * dvis / (4.0f * std::max(dot(wo, h), 1e-4f));
                float d = F * GgxD(h, a) * GgxG2(wo, wi, a) / (4.0f * wo.z * wi.z);
                acc += d * wi.z / std::max(pdf, 1e-12f);
            }
        }
        float E = acc / static_cast<float>(N);
        CHECK(E <= 1.01f, "off-eta ceiling (ior=%.1f E=%.4f)", ior, E);
        CHECK(std::fabs(E - 1.0f) < 0.05f, "off-eta closure (ior=%.1f E=%.4f)", ior, E);
    }
    // ②b Smooth absolute anchor: at roughness 0.05 the lobe is delta-ish (G → 1, micro-Fresnel → macro), so the
    // branch estimator MUST return 1 — no tables, no approximations, the strongest scale check on Single.
    {
        vec2 a = AnisotropicAlpha(0.05f, 0.0f);
        for (float muO : { 0.5f, 1.0f })
        {
            vec3 wo = vec3(std::sqrt(1.0f - muO * muO), 0.0f, muO);
            const float ior = 1.5f;
            float acc = 0.0f;
            const int N = 200000;
            for (int i = 0; i < N; ++i)
            {
                vec3 h = SampleGgxVndf(wo, a, vec2(Rand01(), Rand01()));
                float F = FresnelDielectric(std::fabs(dot(wo, h)), ior);
                float dvis = GgxVndfPdf(wo, h, a);
                if (Rand01() < 1.0f - F)
                {
                    vec4 r = RefractDielectric(-wo, h, 1.0f / ior);
                    vec3 wi = r.xyz;
                    float dnom = dot(wo, h) + ior * dot(wi, h);
                    float jac = (ior * ior) * std::fabs(dot(wi, h)) / (dnom * dnom + 1e-12f);
                    float pdf = (1.0f - F) * dvis * jac;
                    vec3 f = TransmissionEvaluateSingle(wo, wi, a, 1.0f, ior);
                    acc += f.x * std::fabs(wi.z) / std::max(pdf, 1e-12f);
                }
                else
                {
                    vec3 wi = reflect(-wo, h);
                    if (wi.z <= 0.0f) continue;
                    float pdf = F * dvis / (4.0f * std::max(dot(wo, h), 1e-4f));
                    float d = F * GgxD(h, a) * GgxG2(wo, wi, a) / (4.0f * wo.z * wi.z);
                    acc += d * wi.z / std::max(pdf, 1e-12f);
                }
            }
            float E = acc / static_cast<float>(N);
            CHECK(std::fabs(E - 1.0f) < 0.02f, "single smooth anchor (mu=%.1f E=%.4f)", muO, E);
        }
    }
    // ③ Smooth limit: T-samples cluster at the Snell direction.
    {
        vec2 a = AnisotropicAlpha(0.08f, 0.0f);
        vec3 wo = normalize(vec3(0.3f, 0.15f, 0.9f));
        vec3 snell = RefractDielectric(-wo, vec3(0.0f, 0.0f, 1.0f), 1.0f / 1.5f).xyz;
        int nT = 0, okT = 0;
        const int N = 20000;
        for (int i = 0; i < N; ++i)
        {
            vec3 h = SampleGgxVndf(wo, a, vec2(Rand01(), Rand01()));
            float F = FresnelDielectric(std::fabs(dot(wo, h)), 1.5f);
            if (Rand01() >= 1.0f - F) continue;
            vec3 wi = RefractDielectric(-wo, h, 1.0f / 1.5f).xyz;
            ++nT;
            if (dot(wi, snell) > 0.996f) ++okT;   // within 5° of Snell
        }
        CHECK(nT > N / 2, "T-branch dominates smooth glass (nT=%d/%d)", nT, N);
        CHECK(static_cast<float>(okT) / std::max(nT, 1) > 0.9f, "smooth T clusters at Snell (%d/%d)", okT, nT);
    }
}

// M4: the thin-wall compound (tilted entry + flat exit) — sampling consistency, Beer, smooth limit, weight sweep.
void ProofTransmissionThin()
{
    std::printf("[furnace] M4 thin-wall compound (sampling + Beer + smooth limit)\n");
    // ① Full-mixture sampling anchored at E_ss × macro-split. The Ess-split theory is essentially EXACT smooth-ish
    // (r = 0.15: matched to 4 digits) but undercounts at r = 0.5 — transmitted rays bend toward the normal and shadow
    // less than the E_ss-per-lobe factorisation assumes (same direction as the single-interface excess, +4–12 %).
    // So: TIGHT split-compare at r = 0.15, rigorous floor/ceiling at r = 0.5, and the cosine-furnace cross-check only
    // where the lobe is broad enough to integrate quietly (r = 0.5; at r = 0.15 a sharp lobe makes it ±10 % noise).
    for (float rough : { 0.15f, 0.5f })
    {
        ShadingRecord m = GlassMaterial(rough);
        vec3 wo = normalize(vec3(0.3f, 0.2f, 0.9f));
        ResolvedLayers L = ResolveLayers(m, wo);
        vec3 eTab = FetchEnergy(wo.z, rough * rough);
        float Ess = eTab.x + eTab.y;
        float fMacro = FresnelDielectric(wo.z, 1.5f);
        float sinI = std::sqrt(std::max(1.0f - wo.z * wo.z, 0.0f));
        float cosT = std::sqrt(std::max(1.0f - sinI * sinI / 2.25f, 0.0f));
        float fExit = FresnelDielectric(cosT, 1.0f / 1.5f);
        float expect = Ess * (fMacro + (1.0f - fMacro) * (1.0f - fExit));
        float acc = 0.0f;
        int rej = 0;
        const int N = 150000;
        for (int i = 0; i < N; ++i)
        {
            vec4 s = SampleBsdf(m, L, wo, vec4(Rand01(), Rand01(), Rand01(), Rand01()));
            if (s.w <= 0.0f) { ++rej; continue; }   // exit-TIR rejection: trapped = absorbed, energy-consistent
            vec3 wi = s.xyz;
            acc += EvaluateBsdf(m, L, wo, wi).x * std::fabs(wi.z) / s.w;
        }
        acc /= static_cast<float>(N);
        if (rough < 0.3f)
            CHECK(std::fabs(acc - expect) < 0.035f,
                  "thin-wall E[f·cos/p]=%.4f Ess-split=%.4f (r=%.2f)", acc, expect, rough);
        CHECK(acc <= 1.005f, "thin-wall sampler ceiling (r=%.2f E=%.4f)", rough, acc);
        CHECK(acc >= expect - 0.02f, "thin-wall sampler floor (r=%.2f E=%.4f split=%.4f)", rough, acc, expect);
        CHECK(static_cast<float>(rej) / N < 0.20f, "exit-TIR trap rate sane (r=%.2f rej=%.3f)", rough, rej / (float)N);
        if (rough > 0.3f)   // broad lobe only: cosine-furnace cross-check, ceiling, trap-loss floor
        {
            float refR = 0.0f, refT = 0.0f;
            const int N0 = 200000;
            for (int i = 0; i < N0; ++i)
            {
                vec3 up = CosineSample(Rand01(), Rand01());
                refR += EvaluateBsdf(m, L, wo, up).x;
                vec3 dn = -CosineSample(Rand01(), Rand01());
                refT += EvaluateBsdf(m, L, wo, dn).x;
            }
            float ref = 3.14159265358979f * (refR + refT) / static_cast<float>(N0);
            CHECK(std::fabs(acc - ref) / std::max(ref, 1e-3f) < 0.06f,
                  "thin-wall numeric cross-check sampler=%.4f furnace=%.4f (r=%.2f)", acc, ref, rough);
            CHECK(ref <= 1.02f, "thin-wall numeric never exceeds 1 (r=%.2f E=%.4f)", rough, ref);
            // The floor is NOT 1: exit-reflected light ((1−F0)·F0 ≈ 3.8 % at normal incidence) bounces inside the wall
            // and the single-interface model absorbs it instead of re-emitting it — a documented model limit, invisible
            // at display-glass energies (92 % vs 96 % transmission).
            CHECK(ref >= 0.88f, "thin-wall trap-loss floor (r=%.2f E=%.4f)", rough, ref);
        }
    }
    // ①a Smooth absolute anchor: at roughness 0.08 the wall is a macro Fresnel sandwich (G → 1, micro → macro), so
    // the full-mixture sampler MUST return F(μo) + (1−F(μo))(1−F_x) — no tables, the strongest scale check on thin-wall.
    {
        ShadingRecord m = GlassMaterial(0.08f);
        vec3 wo = normalize(vec3(0.3f, 0.2f, 0.9f));
        ResolvedLayers L = ResolveLayers(m, wo);
        float fMacro = FresnelDielectric(wo.z, 1.5f);
        float sinI = std::sqrt(std::max(1.0f - wo.z * wo.z, 0.0f));
        float cosT = std::sqrt(std::max(1.0f - sinI * sinI / 2.25f, 0.0f));
        float split = fMacro + (1.0f - fMacro) * (1.0f - FresnelDielectric(cosT, 1.0f / 1.5f));
        float acc = 0.0f;
        const int N = 150000;
        for (int i = 0; i < N; ++i)
        {
            vec4 s = SampleBsdf(m, L, wo, vec4(Rand01(), Rand01(), Rand01(), Rand01()));
            if (s.w <= 0.0f) continue;
            vec3 wi = s.xyz;
            acc += EvaluateBsdf(m, L, wo, wi).x * std::fabs(wi.z) / s.w;
        }
        acc /= static_cast<float>(N);
        CHECK(std::fabs(acc - split) < 0.035f, "thin-wall smooth anchor E=%.4f split=%.4f", acc, split);
    }
    // ①c Below-horizon mixture: transmissive keeps R/EON/coat samples landing below (valid T-region paths with
    // f = f_T > 0) instead of rejecting them; the pdf carries the full mixture there so sampling density matches
    // exactly. Rejection was unbiased (the T-sampler covers all below-wi) but wasteful — this recovers the paths.
    // Opaque stays bit-identical (0 below, reject below).
    {
        vec3 wo = normalize(vec3(0.3f, 0.2f, 0.9f));
        {   // opaque regression: pdf exactly 0 below, sampler never keeps below
            ShadingRecord m = StandardMaterial(vec3(0.5f), 0.5f);
            ResolvedLayers L = ResolveLayers(m, wo);
            bool allZero = true;
            for (int i = 0; i < 200; ++i)
                if (PdfBsdf(m, L, wo, -CosineSample(Rand01(), Rand01())) != 0.0f) allZero = false;
            CHECK(allZero, "opaque pdf exactly 0 below");
            int keptBelow = 0;
            for (int i = 0; i < 20000; ++i)
            {
                vec4 s = SampleBsdf(m, L, wo, vec4(Rand01(), Rand01(), Rand01(), Rand01()));
                if (s.w > 0.0f && s.z < 0.0f) ++keptBelow;
            }
            CHECK(keptBelow == 0, "opaque sampler rejects below (%d kept)", keptBelow);
        }
        {   // transmissive: extra R+D mass below, finite everywhere (incl. wi = −wo), never under the T-arm
            ShadingRecord m = GlassMaterial(0.5f);
            ResolvedLayers L = ResolveLayers(m, wo);
            float extraMax = 0.0f, extraMin = 1e30f;
            bool finite = true;
            for (int i = 0; i < 2000; ++i)
            {
                vec3 wi = -CosineSample(Rand01(), Rand01());
                float p = PdfBsdf(m, L, wo, wi);
                if (!(p >= 0.0f) || !(p <= 1e9f)) finite = false;
                float extra = p - L.Weights.Specular * PdfTransmission(m, L, wo, wi);
                extraMax = std::max(extraMax, extra);
                extraMin = std::min(extraMin, extra);
            }
            float pBack = PdfBsdf(m, L, wo, -wo);   // the degenerate half-vector: old code NaN'd here
            if (!(pBack >= 0.0f) || !(pBack <= 1e9f)) finite = false;
            CHECK(finite, "below-horizon pdf finite everywhere (p(-wo)=%.4f)", pBack);
            CHECK(extraMax > 0.0f, "below-horizon pdf carries R+D mass (max extra=%.5f)", extraMax);
            CHECK(extraMin > -1e-5f, "below-horizon pdf never under the T-arm (min extra=%.2e)", extraMin);
            // targeted R-lands-below sample: fix facet/branch uniforms until reflect lands below on the R-branch
            vec2 a = AnisotropicAlpha(0.5f, 0.0f);
            bool found = false;
            vec4 kept = vec4(0.0f);
            for (int t = 0; t < 5000 && !found; ++t)
            {
                vec2 u2 = vec2(Rand01(), Rand01());
                vec3 h = SampleGgxVndf(wo, a, u2);
                if (reflect(-wo, h).z >= 0.0f) continue;
                float F = FresnelDielectric(std::fabs(dot(wo, h)), 1.5f);
                float uw = 1.0f - 0.5f * (1.0f - F);   // mid-R-branch: u.w ≥ P(T|m) = 1−F
                float pick = L.Weights.Fuzz + L.Weights.Coat + 0.5f * L.Weights.Specular;
                vec4 q = SampleBsdf(m, L, wo, vec4(pick, u2.x, u2.y, uw));
                if (q.w > 0.0f && q.z < 0.0f) { found = true; kept = q; }
            }
            CHECK(found, "R-branch below-horizon sample kept");
            if (found)
            {
                float q = PdfBsdf(m, L, wo, kept.xyz);
                float f = EvaluateBsdf(m, L, wo, kept.xyz).x;
                CHECK(std::fabs(kept.w - q) / std::max(q, 1e-6f) < 1e-4f,
                      "kept below-R pdf consistent (s.w=%.5f pdf=%.5f)", kept.w, q);
                CHECK(f > 0.0f, "kept below-R evaluates f_T > 0 (f=%.5f)", f);
            }
        }
    }
    // ①b Direct f/p identity on fixed pairs (no MC): f·|cos|/p = (G₁(−d1) + MS_entry·cos_w/(η²·p))·(1−F_x)·Beer —
    // the SS part (separable entry-G₁(wo) cancels D_vis; /η² and the exit Jacobian reconcile through étendue) plus
    {
        vec2 a = AnisotropicAlpha(0.35f, 0.0f);
        vec3 sigma = vec3(0.4f, 0.2f, 0.1f);
        int tested = 0;
        for (vec3 wo : { normalize(vec3(0.0f, 0.0f, 1.0f)), normalize(vec3(0.4f, 0.2f, 0.9f)) })
        {
            for (vec3 raw : { vec3(0.05f, 0.02f, 1.0f), vec3(0.3f, -0.2f, 1.0f), vec3(-0.25f, 0.35f, 1.0f) })
            {
                vec3 facet = normalize(raw);
                vec4 entry = RefractDielectric(-wo, facet, 1.0f / 1.5f);
                if (entry.w < 0.0f) continue;
                vec4 exit = RefractDielectric(entry.xyz, vec3(0.0f, 0.0f, 1.0f), 1.5f);
                if (exit.w < 0.0f) continue;
                vec3 wi = exit.xyz;
                vec3 d1 = entry.xyz;
                float f = TransmissionThinWallEvaluate(wo, wi, a, 1.5f, sigma, 0.5f).x;
                float pp = TransmissionThinWallPdf(wo, wi, a, 1.5f, 1.0f);
                float g1t = GgxG1(-d1, a);
                float fx = FresnelDielectric(std::fabs(d1.z), 1.0f / 1.5f);
                float beer = std::exp(-sigma.x * (0.5f / std::fabs(d1.z)));
                float wGot = f * std::fabs(wi.z) / std::max(pp, 1e-12f);
                float wWant = g1t * (1.0f - fx) * beer;
                CHECK(std::fabs(wGot - wWant) / std::max(wWant, 1e-6f) < 1e-3f,
                      "thin-wall f/p identity got=%.5f want=%.5f", wGot, wWant);
                ++tested;
            }
        }
        CHECK(tested == 6, "f/p identity ran on all 6 pairs (%d)", tested);
    }
    // ② Smooth limit: T-samples go antipodal (wi ≈ −wo — the exit face unbends the entry refraction exactly).
    {
        ShadingRecord m = GlassMaterial(0.05f);
        vec3 wo = normalize(vec3(0.2f, 0.1f, 1.0f));
        ResolvedLayers L = ResolveLayers(m, wo);
        vec3 antipode = -wo;
        int nT = 0, okT = 0;
        const int N = 20000;
        for (int i = 0; i < N; ++i)
        {
            vec4 s = SampleBsdf(m, L, wo, vec4(Rand01(), Rand01(), Rand01(), Rand01()));
            if (s.w <= 0.0f) continue;
            vec3 wi = s.xyz;
            if (wi.z >= 0.0f) continue;
            ++nT;
            if (dot(wi, antipode) > 0.9994f) ++okT;   // within 2° of antipodal
        }
        CHECK(nT > N / 2, "smooth thin glass transmits (nT=%d/%d)", nT, N);
        CHECK(static_cast<float>(okT) / std::max(nT, 1) > 0.9f, "smooth T is antipodal (%d/%d)", okT, nT);
    }
    // ③ Beer-vs-depth: normal incidence, per-channel T-ONLY throughput ratios track exp(−σ·t) (the untinted R lobe
    // would dilute the ratio, so below-hemisphere samples accumulate separately); the absolute clear-glass throughput
    // is E_ss × [F0 + (1−F0)²] (entry reflection + double-transmitted — the exit reflection is the §① loss).
    {
        const float F0 = 0.04f;   // ((1.5 − 1) / (1.5 + 1))²
        vec3 color = vec3(0.2f, 0.5f, 0.8f);
        float sigma[3] = { -std::log(0.2f), -std::log(0.5f), -std::log(0.8f) };   // depth = 1 m
        vec3 eTab = FetchEnergy(1.0f, 0.01f);
        float Ess = eTab.x + eTab.y;
        float baseT[3] = { 0.0f, 0.0f, 0.0f };
        for (float t : { 0.0f, 0.5f, 1.0f, 2.0f })
        {
            ShadingRecord m = GlassMaterial(0.1f);
            m.TransmissionColor = color; m.TransmissionDepth = 1.0f; m.TransmissionThickness = t;
            vec3 wo = vec3(0.0f, 0.0f, 1.0f);
            ResolvedLayers L = ResolveLayers(m, wo);
            float acc = 0.0f, accT[3] = { 0.0f, 0.0f, 0.0f };
            int nT = 0;
            const int N = 120000;
            for (int i = 0; i < N; ++i)
            {
                vec4 s = SampleBsdf(m, L, wo, vec4(Rand01(), Rand01(), Rand01(), Rand01()));
                if (s.w <= 0.0f) continue;
                vec3 wi = s.xyz;
                vec3 f = EvaluateBsdf(m, L, wo, wi);
                acc += f.x * std::fabs(wi.z) / s.w;
                if (wi.z >= 0.0f) continue;
                accT[0] += f.x * std::fabs(wi.z) / s.w;
                accT[1] += f.y * std::fabs(wi.z) / s.w;
                accT[2] += f.z * std::fabs(wi.z) / s.w;
                ++nT;
            }
            acc /= N; accT[0] /= N; accT[1] /= N; accT[2] /= N;
            if (t == 0.0f)
            {
                baseT[0] = accT[0]; baseT[1] = accT[1]; baseT[2] = accT[2];
                float expect = Ess * (F0 + (1.0f - F0) * (1.0f - F0));
                CHECK(std::fabs(acc - expect) < 0.04f, "clear-glass throughput %.4f vs %.4f", acc, expect);
            }
            else
            {
                for (int c = 0; c < 3; ++c)
                {
                    float ratio = accT[c] / std::max(baseT[c], 1e-6f);
                    float expect = std::exp(-sigma[c] * t);
                    CHECK(std::fabs(ratio - expect) < 0.02f + 0.1f * expect,
                          "Beer ch=%d t=%.1f ratio=%.4f exp=%.4f (nT=%d)", c, t, ratio, expect, nT);
                }
            }
        }
    }
    // ④ specular_weight sweep: energy holds at every weight (numeric loose — the grazing-J spike inflates cosine
    // variance — plus the tight sampler check, where J cancels); weight 0 = invisible film, measured via the sampler
    // (cosine sampling cannot see a delta lobe: straight-through + Beer).
    for (float sw : { 0.0f, 0.25f, 0.5f, 1.0f })
    {
        ShadingRecord m = GlassMaterial(0.3f);
        m.SpecularWeight = sw;
        vec3 wo = normalize(vec3(0.2f, 0.0f, 1.0f));
        ResolvedLayers L = ResolveLayers(m, wo);
        float ref = 0.0f;
        const int N0 = 200000;
        for (int i = 0; i < N0; ++i)
        {
            ref += EvaluateBsdf(m, L, wo, CosineSample(Rand01(), Rand01())).x;
            ref += EvaluateBsdf(m, L, wo, -CosineSample(Rand01(), Rand01())).x;
        }
        ref *= 3.14159265358979f / static_cast<float>(N0);
        // Gross-guard ceiling only: at r = 0.3 the lobe peak (~30×) makes cosine-numeric ±5 % noise; the sampler
        // check below is the tight one.
        CHECK(ref <= 1.06f, "weight-sweep numeric energy (sw=%.2f E=%.4f)", sw, ref);
        float acc = 0.0f;
        const int N = 150000;
        for (int i = 0; i < N; ++i)
        {
            vec4 s = SampleBsdf(m, L, wo, vec4(Rand01(), Rand01(), Rand01(), Rand01()));
            if (s.w <= 0.0f) continue;
            vec3 wi = s.xyz;
            acc += EvaluateBsdf(m, L, wo, wi).x * std::fabs(wi.z) / s.w;
        }
        acc /= static_cast<float>(N);
        CHECK(acc <= 1.01f, "weight-sweep sampler energy (sw=%.2f E=%.4f)", sw, acc);
        if (sw == 0.0f)
        {
            CHECK(std::fabs(acc - 1.0f) < 0.03f, "weight-0 film is invisible (E=%.4f)", acc);
            int nT = 0, okT = 0;
            for (int i = 0; i < 20000; ++i)
            {
                vec4 s = SampleBsdf(m, L, wo, vec4(Rand01(), Rand01(), Rand01(), Rand01()));
                if (s.w <= 0.0f) continue;
                vec3 wi = s.xyz;
                if (wi.z >= 0.0f) continue;
                ++nT;
                if (dot(wi, -wo) > 0.9999f) ++okT;
            }
            CHECK(nT > 10000, "weight-0 film transmits (nT=%d)", nT);
            CHECK(static_cast<float>(okT) / std::max(nT, 1) > 0.9f, "weight-0 T is straight-through (%d/%d)", okT, nT);
        }
    }
    // ④b transmit_weight sweep: partial transmission blends diffuse + KC×(1−wT) + T_ss across the wT range.
    // Ceiling-only (energy VARIES by design: tw = 0 is a dark dielectric, tw = 1 closes near 1); tw = 0 asserts
    // dark in BOTH estimators (T dead in eval AND sample — a leak in either trips one of them).
    for (float tw : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        ShadingRecord m = GlassMaterial(0.3f);
        m.TransmissionWeight = tw;
        vec3 wo = normalize(vec3(0.2f, 0.0f, 1.0f));
        ResolvedLayers L = ResolveLayers(m, wo);
        float ref = 0.0f;
        const int N0 = 100000;
        for (int i = 0; i < N0; ++i)
        {
            ref += EvaluateBsdf(m, L, wo, CosineSample(Rand01(), Rand01())).x;
            ref += EvaluateBsdf(m, L, wo, -CosineSample(Rand01(), Rand01())).x;
        }
        ref *= 3.14159265358979f / static_cast<float>(N0);
        // Wall-level invisible-entry-mass: the full-mixture numeric sees the opposed-facet entry mass (~+6 %
        // through the exit composition — the ② note composed through the wall), so tw = 1 over-closes ≈ 1.07.
        CHECK(ref <= 1.10f, "tw-sweep numeric energy (tw=%.2f E=%.4f)", tw, ref);
        float acc = 0.0f;
        const int N = 100000;
        for (int i = 0; i < N; ++i)
        {
            vec4 s = SampleBsdf(m, L, wo, vec4(Rand01(), Rand01(), Rand01(), Rand01()));
            if (s.w <= 0.0f) continue;
            vec3 wi = s.xyz;
            acc += EvaluateBsdf(m, L, wo, wi).x * std::fabs(wi.z) / s.w;
        }
        acc /= static_cast<float>(N);
        CHECK(acc <= 1.01f, "tw-sweep sampler energy (tw=%.2f E=%.4f)", tw, acc);
        if (tw == 0.0f)
        {
            CHECK(acc < 0.15f, "tw-0 sampler dark (E=%.4f)", acc);
            CHECK(ref < 0.15f, "tw-0 numeric dark (E=%.4f)", ref);
        }
    }
    // ⑤ R4b regression: the 4th uniform is dead when opaque (bitwise), and metal kills T exactly.
    {
        ShadingRecord m = StandardMaterial(vec3(0.5f), 0.4f);
        vec3 wo = normalize(vec3(0.3f, 0.25f, 0.9f));
        ResolvedLayers L = ResolveLayers(m, wo);
        bool identical = true;
        for (int i = 0; i < 2000; ++i)
        {
            float x = Rand01(), y = Rand01(), z = Rand01();
            vec4 s0 = SampleBsdf(m, L, wo, vec4(x, y, z, 0.0f));
            vec4 s1 = SampleBsdf(m, L, wo, vec4(x, y, z, 0.999f));
            identical = identical && s0.x == s1.x && s0.y == s1.y && s0.z == s1.z && s0.w == s1.w;
        }
        CHECK(identical, "u.w dead when opaque (2000/2000 bitwise identical)");
        ShadingRecord mt = m; mt.Metalness = 1.0f; mt.TransmissionWeight = 1.0f;
        ResolvedLayers Lt = ResolveLayers(mt, wo);
        CHECK(Lt.TransmitMix == 0.0f, "metal kills the transmit mix");
        CHECK(EvaluateBsdf(mt, Lt, wo, vec3(0.1f, 0.1f, -0.9f)).x == 0.0f, "metal transmits nothing");
    }
}

// M5 v2: Christensen–Burley dipole — full lobe (Sample/Pdf dipole branch), view-dependent by design (exit
// transmission), metals exempt. The normalisation (N = 6/5π) is proved, not assumed: a uniform below-hemisphere
// backlight must close EXACTLY per view (E(wo) = mix·ρ·T·T_exit(μo)), the front-glow value at μi = −1, μo = 1 is
// asserted to 6 digits by direct eval, and the sampler + pdf + MIS close against the same integral (⑨⑩).
void ProofSss()
{
    std::printf("[furnace] M5 subsurface dipole (transport, exit-shape, branch, consistency, MIS)\n");
    const float kHarnessPi = 3.14159265358979f;
    const float kNaN = std::numeric_limits<float>::quiet_NaN();

    {   // ① Dipole-transport unit checks (direct, no MC): C-B CCDF per-channel, opaque at r ≤ 0, foil at t = 0, null on miss.
        vec3 t = SssDipoleTransport(0.5f, 1.0f, vec3(1.0f, 0.5f, 0.25f));
        float w0 = 0.25f * std::exp(-0.5f) + 0.75f * std::exp(-0.5f / 3.0f);   // d = 1: independent std::exp, ±1e-6
        float w1 = 0.25f * std::exp(-1.0f) + 0.75f * std::exp(-1.0f / 3.0f);   // d = 0.5
        float w2 = 0.25f * std::exp(-2.0f) + 0.75f * std::exp(-2.0f / 3.0f);   // d = 0.25
        CHECK(std::fabs(t.x - w0) < 1e-6f && std::fabs(t.y - w1) < 1e-6f &&
              std::fabs(t.z - w2) < 1e-6f, "dipole CCDF per-channel d (%.6f %.6f %.6f)", t.x, t.y, t.z);
        vec3 t0 = SssDipoleTransport(0.0f, 0.0f, vec3(1.0f));   // r = 0 AND t = 0: no transport (the 0/0 guard, no NaN)
        CHECK(t0.x == 0.0f && t0.y == 0.0f && t0.z == 0.0f, "dipole opaque at r=0 (0/0 guarded)");
        vec3 tf = SssDipoleTransport(0.0f, 1.0f, vec3(1.0f));   // foil: t = 0, r > 0 → .25+.75 = 1 bitwise (exact halves)
        CHECK(tf.x == 1.0f && tf.y == 1.0f && tf.z == 1.0f, "dipole foil at t=0 (bitwise 1)");
        vec3 tm = SssDipoleTransport(1e30f, 1.0f, vec3(1.0f));   // missed chord: t = +∞ → exactly 0
        CHECK(tm.x == 0.0f && tm.y == 0.0f && tm.z == 0.0f, "dipole miss attenuates to 0");
        vec3 tn = SssDipoleTransport(1.0f, 1.0f, vec3(1.0f, 0.0f, -1.0f));   // sick scale channels: guarded per channel
        CHECK(std::fabs(tn.x - (0.25f * std::exp(-1.0f) + 0.75f * std::exp(-1.0f / 3.0f))) < 1e-6f &&
              tn.y == 0.0f && tn.z == 0.0f, "dipole scale<=0 opaque");
    }

    {   // ② Front-glow analytic value: wi = (0,0,−1) (B = 1), wo = (0,0,1), f = mix·ρ·T·6/(5π)·T_exit(1) — direct-eval, ±2e-6.
        ShadingRecord m = SssMaterial(vec3(0.5f), vec3(1.0f, 0.5f, 0.25f), 2.0f, vec3(1.0f), 1.0f, 0.75f);
        vec3 wo = vec3(0.0f, 0.0f, 1.0f);
        ResolvedLayers L = ResolveLayers(m, wo);
        vec3 f = EvaluateBsdf(m, L, wo, vec3(0.0f, 0.0f, -1.0f));
        float trans = 0.25f * std::exp(-0.5f) + 0.75f * std::exp(-0.5f / 3.0f);   // t/d = 1/2, scale 1
        float f0 = (1.5f - 1.0f) / (1.5f + 1.0f); f0 *= f0;   // hand-F0(1.5) = 0.04 — independent of shared Fresnel
        vec3 want = 0.75f * vec3(1.0f, 0.5f, 0.25f) * trans * (6.0f / (5.0f * kHarnessPi)) * (1.0f - f0);
        CHECK(std::fabs(f.x - want.x) < 2e-6f && std::fabs(f.y - want.y) < 2e-6f && std::fabs(f.z - want.z) < 2e-6f,
              "front-glow f = mix·ρ·T·6/(5π)·T_exit (%.6f vs %.6f)", f.x, want.x);
    }

    {   // ③ View-DEPENDENCE (v2 deliberately supersedes the v1 view-independence lock): wo enters ONLY through
        // the exit transmission — f(μo)/f(1) = T_exit(μo)/T_exit(1) with T_exit from the shared (proven) Fresnel,
        // and every off-normal view differs (the bare mat has no coat/fuzz exit scales, so nothing else varies).
        ShadingRecord m = SssMaterial(vec3(0.5f), vec3(0.9f, 0.4f, 0.3f), 1.5f, vec3(1.0f, 0.37f, 0.3f), 0.8f, 0.6f);
        vec3 wi = normalize(vec3(0.3f, -0.2f, -0.9f));
        vec3 woRef = vec3(0.0f, 0.0f, 1.0f);
        vec3 ref = EvaluateBsdf(m, ResolveLayers(m, woRef), woRef, wi);
        float exitRef = 1.0f - FresnelDielectric(1.0f, 1.5f);
        bool shaped = true, varied = true;
        for (int a = 0; a < 12; ++a)
            for (int p = 0; p < 24; ++p)
            {
                float mu = 0.05f + 0.95f * (static_cast<float>(a) + 0.5f) / 12.0f;
                float phi = 2.0f * kHarnessPi * (static_cast<float>(p) + 0.5f) / 24.0f;
                float s = std::sqrt(1.0f - mu * mu);
                vec3 wo = vec3(s * std::cos(phi), s * std::sin(phi), mu);
                vec3 f = EvaluateBsdf(m, ResolveLayers(m, wo), wo, wi);
                float want = (1.0f - FresnelDielectric(mu, 1.5f)) / exitRef;
                shaped = shaped && std::fabs(f.x / ref.x - want) < 1e-5f &&
                         std::fabs(f.y / ref.y - want) < 1e-5f && std::fabs(f.z / ref.z - want) < 1e-5f;
                varied = varied && (f.x != ref.x || f.y != ref.y || f.z != ref.z);
            }
        CHECK(shaped, "SSS follows the exit-transmission shape (288/288)");
        CHECK(varied, "SSS view-dependent (288/288 differ off-normal — v1 lock superseded)");
    }

    {   // ④ Uniform-backlight closure: cosine-below MC, E(wo) == mix·ρ·T·T_exit(μo) ±0.005 (per-view exact close).
        ShadingRecord m = SssMaterial(vec3(0.5f), vec3(0.8f, 0.4f, 0.2f), 1.0f, vec3(1.0f, 0.5f, 0.25f), 0.7f, 0.9f);
        vec3 wo = vec3(0.0f, 0.0f, 1.0f);
        ResolvedLayers L = ResolveLayers(m, wo);
        vec3 acc = vec3(0.0f);
        const int N = 200000;
        for (int i = 0; i < N; ++i)   // pdf = |cos|/π below → E[f·|cos|/p] = π·mean(f)
        {
            vec3 wi = -CosineSample(Rand01(), Rand01());
            acc = acc + EvaluateBsdf(m, L, wo, wi);
        }
        vec3 E = acc * (kHarnessPi / static_cast<float>(N));
        vec3 trans = vec3(0.25f * std::exp(-0.7f) + 0.75f * std::exp(-0.7f / 3.0f),
                          0.25f * std::exp(-1.4f) + 0.75f * std::exp(-1.4f / 3.0f),
                          0.25f * std::exp(-2.8f) + 0.75f * std::exp(-2.8f / 3.0f));
        vec3 want = 0.9f * vec3(0.8f, 0.4f, 0.2f) * trans * 0.96f;   // T_exit(1) = 1 − F0(1.5) = 0.96
        CHECK(std::fabs(E.x - want.x) < 0.005f && std::fabs(E.y - want.y) < 0.005f && std::fabs(E.z - want.z) < 0.005f,
              "uniform-backlight E = mix·ρ·T·T_exit (%.4f %.4f %.4f)", E.x, E.y, E.z);
    }

    {   // ⑤ Gates, all bitwise: w = 0 ⟹ exactly 0 below (NaN-poisoned SSS fields prove the branch-gate never
        // touches them — a multiply-gate would propagate NaN); metal kills the mix exactly.
        ShadingRecord m = StandardMaterial(vec3(0.5f), 0.4f);
        m.SssColor = vec3(kNaN); m.SssRadius = kNaN; m.SssRadiusScale = vec3(kNaN); m.SssThickness = kNaN;
        vec3 wo = vec3(0.0f, 0.0f, 1.0f);
        ResolvedLayers L = ResolveLayers(m, wo);
        CHECK(L.SssMix == 0.0f, "SSS mix 0 at weight 0");
        bool zero = true;
        for (int i = 0; i < 2000; ++i)
        {
            vec3 f = EvaluateBsdf(m, L, wo, -CosineSample(Rand01(), Rand01()));
            zero = zero && f.x == 0.0f && f.y == 0.0f && f.z == 0.0f;
        }
        CHECK(zero, "SSS-off below-branch exactly 0 (2000/2000, NaN-poisoned fields untouched)");
        bool finite = true;   // above: the partition ×(1−0) = ×1.0 is exact, NaN cannot leak into the R stack
        for (int i = 0; i < 2000; ++i)
        {
            vec3 f = EvaluateBsdf(m, L, wo, CosineSample(Rand01(), Rand01()));
            finite = finite && std::isfinite(f.x) && std::isfinite(f.y) && std::isfinite(f.z);
        }
        CHECK(finite, "SSS-off above stack finite under NaN-poison");
        ShadingRecord mm = SssMaterial(vec3(0.5f), vec3(1.0f), 1.0f, vec3(1.0f), 1.0f, 1.0f);
        mm.Metalness = 1.0f;
        ResolvedLayers Lm = ResolveLayers(mm, wo);
        CHECK(Lm.SssMix == 0.0f, "metal kills the SSS mix");
        CHECK(EvaluateBsdf(mm, Lm, wo, vec3(0.1f, 0.1f, -0.9f)).x == 0.0f, "metal scatters nothing below");
    }

    {   // ⑥ Partition: diffuse loses exactly (1 − w) — (f0 − f1) vs w·diffuse0, specular cancels bitwise.
        ShadingRecord m0 = StandardMaterial(vec3(0.5f), 0.4f);
        ShadingRecord m1 = SssMaterial(vec3(0.5f), vec3(1.0f), 1.0f, vec3(1.0f), 1.0f, 0.3f);
        m1.SpecularRoughness = 0.4f;   // match m0 — SssMaterial defaults to 0.5, the partition pair must be twins
        vec3 wo = vec3(0.0f, 0.0f, 1.0f);
        ResolvedLayers L0 = ResolveLayers(m0, wo), L1 = ResolveLayers(m1, wo);
        CHECK(L0.DielectricAlbedoO.x == L1.DielectricAlbedoO.x && L0.DielectricAlbedoO.y == L1.DielectricAlbedoO.y &&
              L0.DielectricAlbedoO.z == L1.DielectricAlbedoO.z, "dielectric albedo SSS-blind (bitwise)");
        bool parted = true;
        for (int i = 0; i < 200; ++i)
        {
            vec3 wi = CosineSample(Rand01(), Rand01());
            vec3 f0 = EvaluateBsdf(m0, L0, wo, wi), f1 = EvaluateBsdf(m1, L1, wo, wi);
            vec3 d0 = (vec3(1.0f) - L0.DielectricAlbedoO) * EonEvaluate(m0.BaseColor, m0.DiffuseRoughness, wi, wo);
            vec3 delta = f0 - f1, want = 0.3f * d0;
            parted = parted && std::fabs(delta.x - want.x) < 1e-5f &&
                     std::fabs(delta.y - want.y) < 1e-5f && std::fabs(delta.z - want.z) < 1e-5f;
        }
        CHECK(parted, "diffuse ×(1−w) partition (200/200)");
        ShadingRecord m0m = m0, m1m = m1;   // metal pair: diffuse dead both sides, specular must be bitwise equal
        m0m.Metalness = 1.0f; m1m.Metalness = 1.0f;
        ResolvedLayers L0m = ResolveLayers(m0m, wo), L1m = ResolveLayers(m1m, wo);
        bool specSame = true;
        for (int i = 0; i < 200; ++i)
        {
            vec3 wi = CosineSample(Rand01(), Rand01());
            vec3 f0 = EvaluateBsdf(m0m, L0m, wo, wi), f1 = EvaluateBsdf(m1m, L1m, wo, wi);
            specSame = specSame && f0.x == f1.x && f0.y == f1.y && f0.z == f1.z;
        }
        CHECK(specSame, "specular SSS-blind above (200/200 bitwise identical)");
    }

    {   // ⑦ The shared exit stack: coated SSS dims by the coat pass (white coat ⟹ pure (1 − CoatAlbedoO) scale).
        ShadingRecord bare = SssMaterial(vec3(0.5f), vec3(0.9f, 0.5f, 0.35f), 1.0f, vec3(1.0f), 0.6f, 0.8f);
        ShadingRecord coat = bare;
        coat.CoatWeight = 1.0f;
        vec3 wo = vec3(0.0f, 0.0f, 1.0f);
        ResolvedLayers Lb = ResolveLayers(bare, wo), Lc = ResolveLayers(coat, wo);
        CHECK(Lc.CoatAlbedoO > 0.01f && Lc.CoatAlbedoO < 0.2f, "coat scale non-vacuous (%.4f)", Lc.CoatAlbedoO);
        bool scaled = true;
        for (int i = 0; i < 200; ++i)
        {
            vec3 wi = -CosineSample(Rand01(), Rand01());
            vec3 fb = EvaluateBsdf(bare, Lb, wo, wi), fc = EvaluateBsdf(coat, Lc, wo, wi);
            vec3 want = fb * (1.0f - Lc.CoatAlbedoO) * Lc.CoatAbsorptionO;   // mix(1, A, 1) = A (white: ≈1)
            scaled = scaled && std::fabs(fc.x - want.x) < 1e-5f &&
                     std::fabs(fc.y - want.y) < 1e-5f && std::fabs(fc.z - want.z) < 1e-5f;
        }
        CHECK(scaled, "coated SSS takes the exit scale (200/200)");
    }

    {   // ⑧ The dipole branch (v2 supersedes the v1 never-below lock): pure-SSS lands below with frequency ≈ wSss,
        // the pdf below reads EXACTLY wSss·|cos|/π (bitwise — the shared kInvPi both sides), SSS-off still reads
        // exactly 0 below, and mixed T+SSS keeps wSss = 0 (exclusivity — the T arm owns below).
        ShadingRecord m = SssMaterial(vec3(0.5f), vec3(1.0f), 1.0f, vec3(1.0f), 1.0f, 1.0f);
        vec3 wo = vec3(0.0f, 0.0f, 1.0f);
        ResolvedLayers L = ResolveLayers(m, wo);
        CHECK(L.Weights.Sss > 0.02f, "dipole mass positive (%.4f)", L.Weights.Sss);
        const int NB = 20000;
        int below = 0;
        for (int i = 0; i < NB; ++i)
        {
            vec4 S = SampleBsdf(m, L, wo, vec4(Rand01(), Rand01(), Rand01(), Rand01()));
            if (S.w > 0.0f && S.z < 0.0f) ++below;
        }
        float freq = static_cast<float>(below) / static_cast<float>(NB);
        float sig = std::sqrt(L.Weights.Sss * (1.0f - L.Weights.Sss) / static_cast<float>(NB));
        CHECK(std::fabs(freq - L.Weights.Sss) < 4.0f * sig + 0.002f, "dipole lands below at wSss (%.4f vs %.4f)",
              freq, L.Weights.Sss);
        bool pdfExact = true;
        for (int i = 0; i < 2000; ++i)
        {
            vec3 wi = -CosineSample(Rand01(), Rand01());
            pdfExact = pdfExact && PdfBsdf(m, L, wo, wi) == L.Weights.Sss * (-wi.z) * kInvPi;
        }
        CHECK(pdfExact, "dipole pdf = wSss·|cos|/π below (2000/2000 bitwise)");
        ShadingRecord mo = StandardMaterial(vec3(0.5f), 0.4f);   // SSS-off negative (the v1 lock, kept)
        ResolvedLayers Lo = ResolveLayers(mo, wo);
        bool pdfZero = true;
        for (int i = 0; i < 2000; ++i)
            pdfZero = pdfZero && PdfBsdf(mo, Lo, wo, -CosineSample(Rand01(), Rand01())) == 0.0f;
        CHECK(pdfZero, "SSS-off pdf 0 below (2000/2000 bitwise)");
        bool noneBelow = true;
        for (int i = 0; i < NB; ++i)
        {
            vec4 S = SampleBsdf(mo, Lo, wo, vec4(Rand01(), Rand01(), Rand01(), Rand01()));
            noneBelow = noneBelow && (S.w <= 0.0f || S.z > 0.0f);
        }
        CHECK(noneBelow, "SSS-off sampler never lands below (20000/20000)");
        ShadingRecord mx = GlassMaterial(0.4f);   // mixed T+SSS: exclusivity — T owns below, dipole mass exactly 0
        mx.SssWeight = 1.0f; mx.SssColor = vec3(1.0f); mx.SssRadius = 1.0f;
        mx.SssRadiusScale = vec3(1.0f); mx.SssThickness = 1.0f;
        ResolvedLayers Lx = ResolveLayers(mx, wo);
        CHECK(Lx.Weights.Sss == 0.0f, "mixed T+SSS dipole mass 0 (exclusivity, bitwise)");
        int mixedBelow = 0;
        for (int i = 0; i < NB; ++i)
        {
            vec4 S = SampleBsdf(mx, Lx, wo, vec4(Rand01(), Rand01(), Rand01(), Rand01()));
            if (S.w > 0.0f && S.z < 0.0f) ++mixedBelow;
        }
        CHECK(mixedBelow > 10, "mixed below-samples are T-real (%d/20000)", mixedBelow);
    }

    {   // ⑨ Dipole sampling consistency: E[f·|cos|/p] over the REAL sampler = the ④ closure (branch + pdf agree).
        ShadingRecord m = SssMaterial(vec3(0.5f), vec3(0.8f, 0.4f, 0.2f), 1.0f, vec3(1.0f, 0.5f, 0.25f), 0.7f, 0.9f);
        vec3 wo = vec3(0.0f, 0.0f, 1.0f);
        ResolvedLayers L = ResolveLayers(m, wo);
        vec3 acc = vec3(0.0f);
        const int N = 200000;
        for (int i = 0; i < N; ++i)
        {
            vec4 S = SampleBsdf(m, L, wo, vec4(Rand01(), Rand01(), Rand01(), Rand01()));
            if (S.w <= 0.0f || S.z >= 0.0f) continue;   // above-stratum owns ≥ 0 (partition — 0-contribution)
            vec3 f = EvaluateBsdf(m, L, wo, S.xyz);
            acc = acc + f * (-S.z) / std::max(S.w, 1e-12f);
        }
        vec3 E = acc / static_cast<float>(N);   // /N over ALL draws (above + killed + below) — the ∫_below estimator
        vec3 trans = vec3(0.25f * std::exp(-0.7f) + 0.75f * std::exp(-0.7f / 3.0f),
                          0.25f * std::exp(-1.4f) + 0.75f * std::exp(-1.4f / 3.0f),
                          0.25f * std::exp(-2.8f) + 0.75f * std::exp(-2.8f / 3.0f));
        vec3 want = 0.9f * vec3(0.8f, 0.4f, 0.2f) * trans * 0.96f;   // the ④ want (same mat, same view)
        CHECK(std::fabs(E.x - want.x) < 0.01f && std::fabs(E.y - want.y) < 0.01f && std::fabs(E.z - want.z) < 0.01f,
              "dipole E[f·|cos|/p] closes (%.4f %.4f %.4f)", E.x, E.y, E.z);
    }

    {   // ⑩ Two-stratum MIS closure (the maths the exhibit's DirectMISsss implements): light-stratum (cosine-below,
        // pdfA = |cos|/π) + BSDF-stratum (the dipole branch) under the balance heuristic reproduce the ④ closure.
        ShadingRecord m = SssMaterial(vec3(0.5f), vec3(0.8f, 0.4f, 0.2f), 1.0f, vec3(1.0f, 0.5f, 0.25f), 0.7f, 0.9f);
        vec3 wo = vec3(0.0f, 0.0f, 1.0f);
        ResolvedLayers L = ResolveLayers(m, wo);
        vec3 accA = vec3(0.0f), accB = vec3(0.0f);
        const int N = 100000;
        for (int i = 0; i < N; ++i)
        {
            vec3 wi = -CosineSample(Rand01(), Rand01());
            float pdfA = (-wi.z) / kHarnessPi, pdfB = PdfBsdf(m, L, wo, wi);
            float wA = (pdfA * pdfA) / (pdfA * pdfA + pdfB * pdfB + 1e-12f);
            accA = accA + EvaluateBsdf(m, L, wo, wi) * (-wi.z) * wA / std::max(pdfA, 1e-12f);
        }
        for (int i = 0; i < N; ++i)
        {
            vec4 S = SampleBsdf(m, L, wo, vec4(Rand01(), Rand01(), Rand01(), Rand01()));
            if (S.w <= 0.0f || S.z >= 0.0f) continue;
            float pdfA = (-S.z) / kHarnessPi;
            float wB = (S.w * S.w) / (S.w * S.w + pdfA * pdfA + 1e-12f);
            accB = accB + EvaluateBsdf(m, L, wo, S.xyz) * (-S.z) * wB / std::max(S.w, 1e-12f);
        }
        vec3 E = (accA + accB) / static_cast<float>(N);   // MIS sum: (1/N)Σ_A w·f/p + (1/N)Σ_B w·f/p (no /2 — each stratum estimates its weight-fraction, not the whole)
        vec3 trans = vec3(0.25f * std::exp(-0.7f) + 0.75f * std::exp(-0.7f / 3.0f),
                          0.25f * std::exp(-1.4f) + 0.75f * std::exp(-1.4f / 3.0f),
                          0.25f * std::exp(-2.8f) + 0.75f * std::exp(-2.8f / 3.0f));
        vec3 want = 0.9f * vec3(0.8f, 0.4f, 0.2f) * trans * 0.96f;   // the ④ want (same mat, same view)
        CHECK(std::fabs(E.x - want.x) < 0.012f && std::fabs(E.y - want.y) < 0.012f && std::fabs(E.z - want.z) < 0.012f,
              "dipole MIS two-stratum closes (%.4f %.4f %.4f)", E.x, E.y, E.z);
    }
}

} // namespace

int main()
{
    Frontier::ShadingTableSet Tables = Frontier::ShadingTableCodec::Bake(1024u);   // production bakes 4096; 1024 proves the maths
    g_Tables = &Tables;
    std::printf("[furnace] tables baked (32×32, 1024 spp/cell)\n");
    ProofEon();
    ProofGgxFurnace();
    ProofFuzzAndCoat();
    ProofReciprocity();
    ProofSampling();
    ProofAnisotropyFrames();
    ProofSheenTable();
    ProofConsumesTable();
    ProofCloth();
    ProofTransmissionSingle();
    ProofTransmissionSolid();
    ProofTransmissionThin();
    ProofSss();
    std::printf(g_Fail == 0 ? "MATERIAL FURNACE: PASS\n" : "MATERIAL FURNACE: FAIL (%d)\n", g_Fail);
    return g_Fail == 0 ? 0 : 1;
}
