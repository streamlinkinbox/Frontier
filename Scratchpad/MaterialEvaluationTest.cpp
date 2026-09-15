// R4b row 1 proof harness: compiles Engine/Shaders/MaterialEvaluation.slang as C++ (GlslShim.h, FRONTIER_CPU_PORT) and runs
// furnace / reciprocity / importance-sampling checks against the ShadingTableCodec bake.
// Build (from repo root):
//   sed -E 's/\.(xyz|xy|yz|xz)\b([^(])/.\1()\2/g' Engine/Shaders/MaterialEvaluation.slang > /tmp/MaterialEvaluation.port.inc
//   g++ -std=c++20 -O2 -I Scratchpad -I Engine Scratchpad/MaterialEvaluationTest.cpp Engine/DisplayPresentation/ShadingTableCodec.cpp -o /tmp/met && /tmp/met
#include "GlslShim.h"
#include "DisplayPresentation/ShadingTableCodec.h"
#include <cstdio>
#include <random>
#include <string>
#define FRONTIER_CPU_PORT
static Frontier::ShadingTableSet gTables;
vec3  FetchEnergy(float mu, float alpha) { float o[3]; Frontier::ShadingTableCodec::SampleEnergy(gTables, mu, alpha, o); return vec3(o[0], o[1], o[2]); }
static float Ess(float mu, float alpha) { vec3 e = FetchEnergy(mu, alpha); return e.x + e.y; }
vec3  FetchSheen(float mu, float alpha)  { float o[3]; Frontier::ShadingTableCodec::SampleSheen(gTables, mu, alpha, o); return vec3(o[0], o[1], o[2]); }
#include "/tmp/MaterialEvaluation.port.inc"

static int gFail = 0;
static void Check(const char* name, double got, double want, double tol) {
    bool ok = std::fabs(got - want) <= tol; if (!ok) ++gFail;
    std::printf("  %-58s %9.5f  (want %.4f +-%.4f)  %s\n", name, got, want, tol, ok ? "PASS" : "FAIL");
}
static ShadingRecord Default() {
    ShadingRecord m{}; m.BaseColor = vec3(0.8f); m.Metalness = 0; m.DiffuseRoughness = 0; m.SpecularWeight = 1; m.SpecularColor = vec3(1);
    m.SpecularRoughness = 0.3f; m.SpecularAnisotropy = 0; m.SpecularIor = 1.5f; m.ThinFilmWeight = 0; m.ThinFilmThickness = 0.5f; m.ThinFilmIor = 1.4f;
    m.HazinessWeight = 0; m.HazinessRoughness = 0.5f; m.CoatWeight = 0; m.CoatColor = vec3(1); m.CoatRoughness = 0; m.CoatIor = 1.6f; m.CoatDarkening = 1;
    m.FuzzWeight = 0; m.FuzzColor = vec3(1); m.FuzzRoughness = 0.5f; m.Emission = vec3(0); return m; }
static vec3 Dir(float ct, float phi) { float st = std::sqrt(std::max(0.f, 1 - ct * ct)); return vec3(st * std::cos(phi), st * std::sin(phi), ct); }

// Hemispherical albedo by uniform-hemisphere quadrature (deterministic stratified grid).
static vec3 Albedo(const ShadingRecord& m, vec3 wo, int n = 256) {
    ResolvedLayers L = ResolveLayers(m, wo); vec3 sum(0);
    for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) {
        float ct = (i + 0.5f) / n, phi = 2 * kPi * (j + 0.5f) / n; vec3 wi = Dir(ct, phi);
        sum += EvaluateBsdf(m, L, wo, wi) * ct; }
    return sum * (2 * kPi / (n * n));
}
// Raw single-scatter GGX albedo with F=1 from the slang's own D/G2 (no compensation) — for the "loss at α=1" column.
static float GgxRawAlbedo(vec3 wo, float r, int n = 512) {
    vec2 a = AnisotropicAlpha(r, 0); double sum = 0;
    for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) {
        float ct = (i + 0.5f) / n, phi = 2 * kPi * (j + 0.5f) / n; vec3 wi = Dir(ct, phi); vec3 h = normalize(wo + wi);
        sum += GgxD(h, a) * GgxG2(wo, wi, a) / (4 * wo.z * ct) * ct; }
    return float(sum * 2 * kPi / (n * n));
}

int main() {
    gTables = Frontier::ShadingTableCodec::Bake(4096);
    std::printf("ShadingTableCodec::Bake  Energy 32x32 RGBA32F (A, B, E_avg), Sheen 32x32 RGBA32F\n");
    std::printf("  E_ss(mu, alpha): mu=0.98 a=0.02 -> %.4f | mu=0.5 a=0.25 -> %.4f | mu=0.5 a=1 -> %.4f | mu=0.1 a=1 -> %.4f | E_avg(a=1) %.4f\n",
        Ess(0.98f, 0.02f), Ess(0.5f, 0.25f), Ess(0.5f, 1.f), Ess(0.1f, 1.f), FetchEnergy(0.5f, 1.f).z);
    Check("LUT E_ss(mu=0.5, a=1) vs raw slang GGX quadrature", Ess(0.5f, 1.f), GgxRawAlbedo(Dir(0.5f, 0), 1.f), 0.01);
    Check("LUT E_ss(mu=0.15, a=0.49) vs raw slang GGX quadrature", Ess(0.15f, 0.49f), GgxRawAlbedo(Dir(0.15f, 0), 0.7f), 0.01);

    const float mus[3] = {0.95f, 0.5f, 0.15f};
    std::printf("\n[1] EON furnace (specular off): white base -> 1.0 within 0.5%%; base 0.8 -> paper's closed-form E_EON within 0.5%%\n");
    std::printf("    (for rho < 1 and r > 0 EON is darker than rho by design: multiple scattering absorbs, rho_ms ~ rho^2)\n");
    for (float rd : {0.0f, 0.5f, 1.0f}) for (float mu : mus) {
        ShadingRecord m = Default(); m.SpecularWeight = 0; m.DiffuseRoughness = rd; m.BaseColor = vec3(1); vec3 a = Albedo(m, Dir(mu, 0.3f));
        char n[96]; std::snprintf(n, 96, "EON white r=%.1f mu=%.2f  albedo", rd, mu); Check(n, a.x, 1.0, 0.005); }
    for (float rd : {0.5f, 1.0f}) for (float mu : mus) {
        ShadingRecord m = Default(); m.SpecularWeight = 0; m.DiffuseRoughness = rd; vec3 a = Albedo(m, Dir(mu, 0.3f));
        char n[96]; std::snprintf(n, 96, "EON 0.8 r=%.1f mu=%.2f  albedo vs E_EON", rd, mu); Check(n, a.x, EonAlbedo(vec3(0.8f), rd, mu).x, 0.004); }

    std::printf("\n[2] GGX white furnace (metal, F0 = 1): compensated == 1.0 within 1%%; raw single-scatter shown for loss\n");
    for (float r : {0.3f, 0.7f, 1.0f}) for (float mu : mus) {
        ShadingRecord m = Default(); m.Metalness = 1; m.BaseColor = vec3(1); m.SpecularRoughness = r; vec3 wo = Dir(mu, 0.3f);
        vec3 a = Albedo(m, wo); char n[96]; std::snprintf(n, 96, "metal F0=1 r=%.1f mu=%.2f  (raw %.3f)", r, mu, GgxRawAlbedo(wo, r)); Check(n, a.x, 1.0, 0.01); }
    { ShadingRecord m = Default(); m.Metalness = 1; m.BaseColor = vec3(1); m.SpecularRoughness = 1; m.SpecularAnisotropy = 0.8f;
      Check("metal F0=1 r=1.0 aniso=0.8 mu=0.5 (aniso path, sqrt(ax*ay) LUT approx)", Albedo(m, Dir(0.5f, 0.7f)).x, 1.0, 0.2);
      m.SpecularAnisotropy = 0.5f; Check("metal F0=1 r=1.0 aniso=0.5 mu=0.5 (aniso path)", Albedo(m, Dir(0.5f, 0.7f)).x, 1.0, 0.05); }

    std::printf("\n[3] Dielectric + diffuse (default OpenPBR: 0.8 base, ior 1.5, r 0.3): albedo <= 1, >= diffuse-only\n");
    for (float mu : mus) { ShadingRecord m = Default(); vec3 a = Albedo(m, Dir(mu, 0.3f));
        std::printf("  mu=%.2f  albedo = (%.4f %.4f %.4f)  %s\n", mu, a.x, a.y, a.z, (a.x <= 1.0f && a.x >= 0.79f) ? "PASS" : "FAIL"); gFail += !(a.x <= 1.0f && a.x >= 0.79f); }
    { ShadingRecord m = Default(); m.BaseColor = vec3(1); vec3 a = Albedo(m, Dir(0.5f, 0.3f)); Check("white dielectric+diffuse mu=0.5 (<= 1, ~1)", a.x, 1.0, 0.03); }
    { ShadingRecord m = Default(); m.ThinFilmWeight = 1; vec3 a = Albedo(m, Dir(0.5f, 0.3f)); std::printf("  thin film 0.5um on default: (%.4f %.4f %.4f) %s\n", a.x, a.y, a.z, std::max(a.x, std::max(a.y, a.z)) <= 1.0f ? "PASS" : "FAIL"); gFail += std::max(a.x, std::max(a.y, a.z)) > 1.0f; }
    { ShadingRecord m = Default(); m.HazinessWeight = 0.5f; m.HazinessRoughness = 0.8f; vec3 a = Albedo(m, Dir(0.5f, 0.3f)); Check("haziness 0.5/0.8 on default mu=0.5 (<= 1)", std::min(a.x, 1.0f), a.x, 0.0); }

    std::printf("\n[4] Fuzz furnace (white fuzz, weight 1, black base): albedo <= 1; fuzz over white diffuse ~<= 1\n");
    for (float rf : {0.2f, 0.5f, 1.0f}) for (float mu : mus) {
        ShadingRecord m = Default(); m.SpecularWeight = 0; m.BaseColor = vec3(0); m.FuzzWeight = 1; m.FuzzRoughness = rf; vec3 wo = Dir(mu, 0.3f);
        float a = Albedo(m, wo).x, tbl = SheenAlbedo(rf, mu); char n[96]; std::snprintf(n, 96, "fuzz r=%.1f mu=%.2f albedo (table R %.3f)", rf, mu, tbl);
        Check(n, a, tbl, 0.01 + 0.02 * tbl); }
    for (float mu : mus) { ShadingRecord m = Default(); m.SpecularWeight = 0; m.BaseColor = vec3(1); m.FuzzWeight = 1; m.FuzzRoughness = 0.5f;
        float a = Albedo(m, Dir(mu, 0.3f)).x; char n[96]; std::snprintf(n, 96, "fuzz 0.5 over white diffuse mu=%.2f (<= 1.01)", mu); Check(n, std::min(a, 1.01f), a, 0.0); }

    std::printf("\n[5] Coat furnace (clear coat ior 1.6 over white metal / white diffuse): coat + base <= 1\n");
    for (float rc : {0.0f, 0.3f}) for (float mu : mus) {
        ShadingRecord m = Default(); m.Metalness = 1; m.BaseColor = vec3(1); m.CoatWeight = 1; m.CoatRoughness = rc; m.SpecularRoughness = 0.5f;
        float a = Albedo(m, Dir(mu, 0.3f)).x; char n[96]; std::snprintf(n, 96, "coat r=%.1f over white metal mu=%.2f (<= 1.02)", rc, mu); Check(n, std::min(a, 1.02f), a, 0.0); }
    for (float mu : mus) { ShadingRecord m = Default(); m.SpecularWeight = 0; m.BaseColor = vec3(1); m.CoatWeight = 1;
        float a = Albedo(m, Dir(mu, 0.3f)).x; char n[96]; std::snprintf(n, 96, "clear coat over white diffuse mu=%.2f (<= 1.01, darkening on)", mu); Check(n, std::min(a, 1.01f), a, 0.0); }
    { ShadingRecord m = Default(); m.SpecularWeight = 0; m.BaseColor = vec3(0.8f); m.CoatWeight = 1; m.CoatColor = vec3(0.5f, 0.2f, 0.1f);
      vec3 a = Albedo(m, Dir(0.5f, 0.3f)); std::printf("  tinted coat (0.5,0.2,0.1) over 0.8 diffuse mu=0.5: (%.4f %.4f %.4f) — must be tinted & <= 1  %s\n", a.x, a.y, a.z, (a.x > a.y && a.y > a.z && a.x <= 1) ? "PASS" : "FAIL"); gFail += !(a.x > a.y && a.y > a.z && a.x <= 1); }

    std::printf("\n[6] Reciprocity f(wo,wi) == f(wi,wo), 10000 pairs per lobe set (relative error; layered forms are non-reciprocal by design -> reported only)\n");
    { std::mt19937 rng(7); std::uniform_real_distribution<float> U(0, 1);
      auto Run = [&](const char* name, ShadingRecord m, bool strict) { double worst = 0; int bad = 0;
        for (int i = 0; i < 10000; ++i) { vec3 wo = Dir(0.02f + 0.98f * U(rng), 2 * kPi * U(rng)), wi = Dir(0.02f + 0.98f * U(rng), 2 * kPi * U(rng));
            vec3 a = EvaluateBsdf(m, ResolveLayers(m, wo), wo, wi), b = EvaluateBsdf(m, ResolveLayers(m, wi), wi, wo);
            double rel = std::fabs(a.x - b.x) / std::max(1e-4f, std::max(a.x, b.x)); worst = std::max(worst, rel); if (rel > 1e-3) ++bad; }
        std::printf("  %-50s worst rel %.2e, pairs > 1e-3: %d  %s\n", name, worst, bad, strict ? (bad == 0 ? "PASS" : "FAIL") : "(info)"); if (strict && bad) ++gFail; };
      ShadingRecord eon = Default(); eon.SpecularWeight = 0; eon.DiffuseRoughness = 0.7f; Run("EON r=0.7 (diffuse only)", eon, true);
      ShadingRecord met = Default(); met.Metalness = 1; met.SpecularRoughness = 0.4f; met.SpecularAnisotropy = 0.5f; Run("metal GGX aniso (single lobe)", met, true);
      ShadingRecord fz = Default(); fz.SpecularWeight = 0; fz.BaseColor = vec3(0); fz.FuzzWeight = 1; Run("fuzz only (LTC, table-fit)", fz, false);
      ShadingRecord all = Default(); all.CoatWeight = 1; all.FuzzWeight = 0.3f; all.ThinFilmWeight = 0.5f; Run("full stack (albedo-scaling layers)", all, false); }

    std::printf("\n[7] Importance sampling: E[f cos / pdf] over 200k samples == quadrature albedo within 1%%; pdf integrates to ~1\n");
    { std::mt19937 rng(11); std::uniform_real_distribution<float> U(0, 1);
      auto Run = [&](const char* name, ShadingRecord m, float mu) { vec3 wo = Dir(mu, 0.3f); ResolvedLayers L = ResolveLayers(m, wo);
        double est = 0; int N = 200000, zero = 0; for (int i = 0; i < N; ++i) { vec4 s = SampleBsdf(m, L, wo, vec3(U(rng), U(rng), U(rng)));
            if (s.w <= 0) { ++zero; continue; } vec3 wi = s.xyz(); est += EvaluateBsdf(m, L, wo, wi).x * wi.z / s.w; }
        est /= N; float ref = Albedo(m, wo).x;
        int n = 256; double pint = 0; for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) pint += PdfBsdf(m, L, wo, Dir((i + .5f) / n, 2 * kPi * (j + .5f) / n)); pint *= 2 * kPi / (n * n);
        char nm[96]; std::snprintf(nm, 96, "%s mu=%.2f  (pdf int %.3f, failed %.2f%%)", name, mu, pint, 100.0 * zero / N); Check(nm, est, ref, 0.01 * std::max(ref, 0.2f)); };
      ShadingRecord eon = Default(); eon.SpecularWeight = 0; eon.DiffuseRoughness = 1; Run("EON r=1 CLTC", eon, 0.5f); Run("EON r=1 CLTC", eon, 0.1f);
      ShadingRecord met = Default(); met.Metalness = 1; met.SpecularRoughness = 0.5f; met.SpecularAnisotropy = 0.6f; Run("metal aniso VNDF", met, 0.5f);
      ShadingRecord def = Default(); Run("default dielectric+EON", def, 0.7f); Run("default dielectric+EON", def, 0.15f);
      ShadingRecord hz = Default(); hz.HazinessWeight = 0.5f; hz.HazinessRoughness = 0.9f; Run("haziness", hz, 0.5f);
      ShadingRecord fz = Default(); fz.FuzzWeight = 0.6f; fz.FuzzRoughness = 0.4f; Run("fuzz over default", fz, 0.5f);
      ShadingRecord all = Default(); all.CoatWeight = 1; all.CoatRoughness = 0.2f; all.FuzzWeight = 0.3f; all.ThinFilmWeight = 1; all.HazinessWeight = 0.3f; Run("full stack", all, 0.4f); }

    std::printf("\n%s (%d failures)\n", gFail ? "FAILED" : "ALL PASS", gFail); return gFail ? 1 : 0;
}
