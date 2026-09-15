//============================================================================================================================================
// 📦 Scratchpad/SunDirectProof.cpp — the kernel's direct-light estimator converges to the analytic answer
//============================================================================================================================================
// ReSTIRViewport answers w = p̂/p for two light kinds (mesh triangles, sun disc) in one reservoir, and the shade
//    divides by W. This proof replicates that algebra EXACTLY (same coin, same picks, same pdfs, same W) for a
//    diffuse point and checks the Monte Carlo mean against INDEPENDENT stratified quadrature — not against the
//    kernel, which would be the code agreeing with itself.
//
//    The BSDF stands in as a constant (diffuse f = albedo/π): every operation under test is linear per channel
//    and BSDF-independent (picks, pdfs, weights, W), so a constant exercises the sampling math fully. Shadowing
//    is out (all visible — TraceShadow is the traversal gates' subject). Reuse is out (the carried-p̂ identity
//    proof owns it). What is IN: the candidate pdfs, the coin probabilities, the area/solid-angle scales, and the
//    panel parity of the sun (converged NEE == 0.11·I·T·f·cos).
//
//    Deterministic: splitmix64, fixed seeds, every MC run twice. Self-contained (no engine headers), so the gate
//    builds it anywhere.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Vec3 { double X, Y, Z; };
Vec3 operator+(Vec3 A, Vec3 B) { return { A.X + B.X, A.Y + B.Y, A.Z + B.Z }; }
Vec3 operator-(Vec3 A, Vec3 B) { return { A.X - B.X, A.Y - B.Y, A.Z - B.Z }; }
Vec3 operator*(Vec3 A, double S) { return { A.X * S, A.Y * S, A.Z * S }; }
double Dot(Vec3 A, Vec3 B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
Vec3 Cross(Vec3 A, Vec3 B) { return { A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X }; }
double Length(Vec3 A) { return std::sqrt(Dot(A, A)); }
Vec3 Normalise(Vec3 A) { double L = Length(A); return L > 0.0 ? A * (1.0 / L) : Vec3{ 0.0, 0.0, 1.0 }; }

// splitmix64: deterministic across platforms (the proof asserts tolerances, but the stream must not wander).
struct SplitMix { uint64_t S; explicit SplitMix(uint64_t Seed) : S(Seed) {}
    double Next() { uint64_t Z = (S += 0x9E3779B97F4A7C15ull); Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
        Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull; return (double)(Z >> 11) * (1.0 / 9007199254740992.0); } };

// ── The kernel's constants, transcribed ──────────────────────────────────────────────────────────
constexpr double kSunAngularRadius = 0.53 * (kPi / 180.0) * 0.5;   // SkyRecords.slang's shared body
constexpr double kSunPick          = 0.5;                          // kSunPickProbability
double SunSolidAngle() { double S = std::sin(kSunAngularRadius * 0.5); return 4.0 * kPi * S * S; }

Vec3 SampleSunDirection(Vec3 SunDir, double U1, double U2)   // ReSTIRViewport.slang, verbatim logic
{
    double CosMax = std::cos(kSunAngularRadius);
    double CosT = 1.0 + (CosMax - 1.0) * U1;
    double SinT = std::sqrt(std::fmax(0.0, 1.0 - CosT * CosT));
    double Phi = 2.0 * kPi * U2;
    Vec3 Up = std::fabs(SunDir.X) < 0.9 ? Vec3{ 1.0, 0.0, 0.0 } : Vec3{ 0.0, 1.0, 0.0 };
    Vec3 T = Normalise(Cross(Up, SunDir));
    Vec3 B = Cross(SunDir, T);
    return Normalise(T * (SinT * std::cos(Phi)) + B * (SinT * std::sin(Phi)) + SunDir * CosT);
}

Vec3 SampleTriPoint(Vec3 A, Vec3 B, Vec3 C, double U1, double U2)   // SampleLightPoint, verbatim logic
{
    double R1 = std::sqrt(U1);
    return A * (1.0 - R1) + B * (R1 * (1.0 - U2)) + C * (R1 * U2);
}

struct Lamp { Vec3 A, B, C, N; double Area, Le; };
Lamp MakeLamp(Vec3 A, Vec3 B, Vec3 C, double Le)
{
    Vec3 N = Normalise(Cross(B - A, C - A));
    return { A, B, C, N, 0.5 * Length(Cross(B - A, C - A)), Le };
}

// ── References: independent stratified quadrature ────────────────────────────────────────────────
double TriQuad(const Lamp& L, Vec3 P, Vec3 N, double F, int Q)   // ∫ f·Le·cosS·cosL/d² dA
{
    double Sum = 0.0;
    for (int I = 0; I < Q; ++I) for (int J = 0; J < Q; ++J)
    {
        Vec3 Pt = SampleTriPoint(L.A, L.B, L.C, (I + 0.5) / Q, (J + 0.5) / Q);
        Vec3 ToL = Pt - P;
        double D2 = Dot(ToL, ToL), D = std::sqrt(D2);
        Vec3 LDir = ToL * (1.0 / D);
        double CosS = std::fmax(0.0, Dot(N, LDir)), CosL = std::fmax(0.0, Dot(L.N, LDir * -1.0));
        Sum += F * L.Le * CosS * CosL / D2;
    }
    return Sum * L.Area / (double)(Q * Q);
}

double DiscQuad(Vec3 SunDir, double Lsun, Vec3 N, double F, double Omega)   // ∫ f·L·cos dω
{
    const int NR = 200, NP = 720;
    double CosMax = std::cos(kSunAngularRadius), Sum = 0.0;
    Vec3 Up = std::fabs(SunDir.X) < 0.9 ? Vec3{ 1.0, 0.0, 0.0 } : Vec3{ 0.0, 1.0, 0.0 };
    Vec3 T = Normalise(Cross(Up, SunDir));
    Vec3 B = Cross(SunDir, T);
    for (int I = 0; I < NR; ++I) for (int J = 0; J < NP; ++J)
    {
        double CosT = 1.0 + (CosMax - 1.0) * (I + 0.5) / NR;
        double SinT = std::sqrt(std::fmax(0.0, 1.0 - CosT * CosT));
        double Phi = 2.0 * kPi * (J + 0.5) / NP;
        Vec3 D = Normalise(T * (SinT * std::cos(Phi)) + B * (SinT * std::sin(Phi)) + SunDir * CosT);
        Sum += F * Lsun * std::fmax(0.0, Dot(N, D));
    }
    return Sum * Omega / (double)(NR * NP);
}

// ── The estimator, transcribed ───────────────────────────────────────────────────────────────────
// One reservoir: M candidates through DrawDirectCandidate's logic, ResampleCandidate's selection, W, shade.
// Shade == p̂ at the selection (same formula, same values — the kernel's shade/target agreement), so the
//    estimate is Wsum/M exactly; the selection is still replicated for fidelity.
double ReservoirEstimate(SplitMix& Rng, int M, bool SunUp, const std::vector<Lamp>& Lamps, bool Alias,
                         Vec3 SunDir, double Lsun, double Omega, Vec3 P, Vec3 N, double F, bool OldWeights)
{
    double TotalPower = 0.0;
    for (const Lamp& L : Lamps) TotalPower += L.Area * L.Le;
    double Wsum = 0.0, SelPhat = 0.0;
    for (int I = 0; I < M; ++I)
    {
        bool Sun = SunUp && (Lamps.empty() || Rng.Next() < kSunPick);
        double W = 0.0, PHat = 0.0;
        if (Sun)
        {
            Vec3 D = SampleSunDirection(SunDir, Rng.Next(), Rng.Next());
            PHat = F * Lsun * std::fmax(0.0, Dot(N, D));
            double Pick = Lamps.empty() ? 1.0 : kSunPick;
            W = PHat * Omega / Pick;
        }
        else
        {
            size_t Z = 0;
            double PSource;
            if (Alias)   // Walker-alias equivalent: power-weighted pick, p = power/total
            {
                double X = Rng.Next() * TotalPower, Acc = 0.0;
                for (size_t K = 0; K < Lamps.size(); ++K) { Acc += Lamps[K].Area * Lamps[K].Le;
                    if (X <= Acc) { Z = K; break; } }
                PSource = Lamps[Z].Area * Lamps[Z].Le / TotalPower;
            }
            else { Z = (size_t)(Rng.Next() * Lamps.size()); if (Z >= Lamps.size()) Z = Lamps.size() - 1;
                PSource = 1.0 / (double)Lamps.size(); }
            const Lamp& L = Lamps[Z];
            Vec3 Pt = SampleTriPoint(L.A, L.B, L.C, Rng.Next(), Rng.Next());
            Vec3 ToL = Pt - P;
            double D2 = Dot(ToL, ToL);
            Vec3 LDir = ToL * (1.0 / std::sqrt(D2));
            PHat = F * L.Le * std::fmax(0.0, Dot(N, LDir)) * std::fmax(0.0, Dot(L.N, LDir * -1.0)) / D2;
            if (OldWeights) W = PHat / PSource;   // the pre-fix line: pick alone, no area
            else { double PCoin = SunUp ? 1.0 - kSunPick : 1.0; W = PHat * L.Area / (PSource * PCoin); }
        }
        Wsum += W;
        if (Rng.Next() * Wsum <= W) SelPhat = PHat;
    }
    if (Wsum <= 0.0 || SelPhat <= 0.0) return 0.0;
    return SelPhat * (Wsum / (M * SelPhat));
}

// One bounce sample: the section-4 NEE, same transcription discipline.
double BounceEstimate(SplitMix& Rng, bool SunUp, const std::vector<Lamp>& Lamps, Vec3 SunDir, double Lsun,
                      double Omega, Vec3 P, Vec3 N, double F)
{
    bool Sun = SunUp && (Lamps.empty() || Rng.Next() < kSunPick);
    if (Sun)
    {
        Vec3 D = SampleSunDirection(SunDir, Rng.Next(), Rng.Next());
        double Pick = Lamps.empty() ? 1.0 : kSunPick;
        return F * Lsun * std::fmax(0.0, Dot(N, D)) * Omega / Pick;
    }
    size_t Z = (size_t)(Rng.Next() * Lamps.size()); if (Z >= Lamps.size()) Z = Lamps.size() - 1;
    const Lamp& L = Lamps[Z];
    Vec3 Pt = SampleTriPoint(L.A, L.B, L.C, Rng.Next(), Rng.Next());
    Vec3 ToL = Pt - P;
    double D2 = Dot(ToL, ToL);
    Vec3 LDir = ToL * (1.0 / std::sqrt(D2));
    double PCoin = SunUp ? 1.0 - kSunPick : 1.0;
    // pdf = pCoin·(1/N)·(1/A): the estimate divides by the pdf, i.e. multiplies by N·A.
    return F * L.Le * std::fmax(0.0, Dot(N, LDir)) * std::fmax(0.0, Dot(L.N, LDir * -1.0))
         * L.Area * (double)Lamps.size() / (D2 * PCoin);
}

int Failures = 0;
void Expect(bool Ok, const char* Label, double Got, double Want)
{
    std::printf("  %-72s %s (got %.6g, want %.6g)\n", Label, Ok ? "PASS" : "FAIL", Got, Want);
    if (!Ok) ++Failures;
}
bool Close(double Got, double Want, double Tol) { return std::fabs(Got - Want) <= Tol * std::fabs(Want); }

} // namespace

int main()
{
    const double Omega = SunSolidAngle();
    std::printf("  solar disc: radius %.7g rad, solid angle %.7g sr\n", kSunAngularRadius, Omega);
    Expect(Close(Omega, kPi * kSunAngularRadius * kSunAngularRadius, 1e-4),
           "the half-angle Ω matches πθ² to the small-angle error", Omega, kPi * kSunAngularRadius * kSunAngularRadius);

    // ── ① The samplers ───────────────────────────────────────────────────────────────────────────
    {
        SplitMix Rng(12345);
        Vec3 Axis{ 0.0, -0.7660444431, 0.6427876097 };   // 40° elevation, southern sky
        Vec3 Mean{ 0, 0, 0 };
        double Sin2 = 0.0, Worst = 1.0;
        const int S = 1000000;
        for (int I = 0; I < S; ++I)
        {
            Vec3 D = SampleSunDirection(Axis, Rng.Next(), Rng.Next());
            Mean = Mean + D;
            double C = Dot(D, Axis);
            if (C < Worst) Worst = C;
            Sin2 += 1.0 - C * C;
        }
        Mean = Mean * (1.0 / S);
        double CosMax = std::cos(kSunAngularRadius);
        Expect(Worst >= CosMax * (1.0 - 1e-6), "a million cone samples all land inside the disc", Worst, CosMax);
        double RmsWant = std::sqrt((2.0 - CosMax - CosMax * CosMax) / 3.0);   // E[sin²] over uniform cos
        double RmsGot = std::sqrt(Sin2 / S);
        Expect(Close(RmsGot, RmsWant, 0.02), "the cone is UNIFORM, not merely contained (transverse RMS)", RmsGot, RmsWant);
        Expect(Length(Mean - Axis) < 5e-3, "the cone's mean is the axis", Length(Mean - Axis), 0.0);
    }
    const Lamp Lamp1 = MakeLamp({ -0.5, -0.4, 3.0 }, { 0.0, 0.4, 3.0 }, { 0.5, -0.4, 3.0 }, 32.0);
    const Lamp Lamp2 = MakeLamp({ 1.0, -0.5, 2.5 }, { 1.0, 2.1, 2.5 }, { 2.3, -0.5, 2.5 }, 7.0);
    Expect(Close(Lamp1.Area, 0.4, 1e-9), "lamp 1 has the Cornell triangle's area", Lamp1.Area, 0.4);
    Expect(Lamp1.N.Z < -0.999, "lamp 1 faces the room (winding side down)", Lamp1.N.Z, -1.0);
    {
        SplitMix Rng(777);
        Vec3 Mean{ 0, 0, 0 };
        const int S = 1000000;
        for (int I = 0; I < S; ++I) Mean = Mean + SampleTriPoint(Lamp1.A, Lamp1.B, Lamp1.C, Rng.Next(), Rng.Next());
        Mean = Mean * (1.0 / S);
        Vec3 Centroid = (Lamp1.A + Lamp1.B + Lamp1.C) * (1.0 / 3.0);
        Expect(Length(Mean - Centroid) < 5e-3, "the triangle sampler's mean is the centroid", Length(Mean - Centroid), 0.0);
    }

    // ── ② References ─────────────────────────────────────────────────────────────────────────────
    const Vec3 P{ 0, 0, 0 }, N{ 0, 0, 1 };
    constexpr double F = 0.8 / kPi;   // diffuse albedo 0.8
    const double Q1 = TriQuad(Lamp1, P, N, F, 700), Q1c = TriQuad(Lamp1, P, N, F, 350);
    Expect(Close(Q1, Q1c, 1e-4), "the lamp quadrature is converged (N vs N/2)", Q1, Q1c);
    const double Q2 = TriQuad(Lamp2, P, N, F, 700);
    // The sun: the panel's factor with a fixed transmittance (the march is the parity proof's subject).
    constexpr double kPanel = 0.11, kIntensity = 22.0, kTrans = 0.8;
    const double Qsun = kPanel * kIntensity * kTrans;   // 1.936
    const double Lsun = Qsun / Omega;                   // SunEmission(): L·Ω == the panel's formula
    Vec3 SunDir{ 0.0, -0.7660444431, 0.6427876097 };
    const double Qd = DiscQuad(SunDir, Lsun, N, F, Omega);
    const double Analytic = F * Qsun * Dot(N, SunDir);
    Expect(Close(Qd, Analytic, 1e-3), "the disc quadrature matches f·Q·cos (panel formula)", Qd, Analytic);
    std::printf("  references: lamp1 %.6g  lamp2 %.6g  sun %.6g (panel %.6g)\n", Q1, Q2, Qd, Analytic);

    // ── ③ Mesh-only reservoir vs quadrature, new vs old weights ──────────────────────────────────
    for (uint64_t Seed : { 1001ull, 2002ull })
    {
        SplitMix Rng(Seed);
        const int R = 200000, M = 4;
        double Acc = 0.0, AccOld = 0.0;
        std::vector<Lamp> Lamps{ Lamp1 };
        for (int I = 0; I < R; ++I) Acc += ReservoirEstimate(Rng, M, false, Lamps, false, SunDir, Lsun, Omega, P, N, F, false);
        SplitMix Rng2(Seed);
        for (int I = 0; I < R; ++I) AccOld += ReservoirEstimate(Rng2, M, false, Lamps, false, SunDir, Lsun, Omega, P, N, F, true);
        char Label[128];
        std::snprintf(Label, sizeof(Label), "mesh-only reservoir converges to quadrature (seed %llu)", (unsigned long long)Seed);
        Expect(Close(Acc / R, Q1, 0.01), Label, Acc / R, Q1);
        std::snprintf(Label, sizeof(Label), "the OLD weights converge to C/A, proving the 1/A diagnosis (seed %llu)", (unsigned long long)Seed);
        Expect(Close(AccOld / R * Lamp1.Area, Q1, 0.01), Label, AccOld / R * Lamp1.Area, Q1);
    }

    // ── ④ Sun-only reservoir vs the disc ─────────────────────────────────────────────────────────
    for (uint64_t Seed : { 3003ull, 4004ull })
    {
        SplitMix Rng(Seed);
        const int R = 200000, M = 4;
        double Acc = 0.0;
        std::vector<Lamp> None;
        for (int I = 0; I < R; ++I) Acc += ReservoirEstimate(Rng, M, true, None, false, SunDir, Lsun, Omega, P, N, F, false);
        char Label[128];
        std::snprintf(Label, sizeof(Label), "sun-only reservoir converges to the disc integral (seed %llu)", (unsigned long long)Seed);
        Expect(Close(Acc / R, Qd, 0.01), Label, Acc / R, Qd);
        std::snprintf(Label, sizeof(Label), "…and to the panel's formula f·Q·cos (seed %llu)", (unsigned long long)Seed);
        Expect(Close(Acc / R, Analytic, 0.005), Label, Acc / R, Analytic);
    }

    // ── ⑤ Mixed reservoir (coin 0.5), uniform and alias picks ────────────────────────────────────
    for (bool Alias : { false, true })
        for (uint64_t Seed : { 5005ull, 6006ull })
        {
            SplitMix Rng(Seed);
            const int R = 400000, M = 4;
            double Acc = 0.0;
            std::vector<Lamp> Lamps{ Lamp1 };
            for (int I = 0; I < R; ++I) Acc += ReservoirEstimate(Rng, M, true, Lamps, Alias, SunDir, Lsun, Omega, P, N, F, false);
            char Label[160];
            std::snprintf(Label, sizeof(Label), "mixed reservoir, %s pick, converges to lamp+sun (seed %llu)",
                          Alias ? "alias" : "uniform", (unsigned long long)Seed);
            Expect(Close(Acc / R, Q1 + Qd, 0.015), Label, Acc / R, Q1 + Qd);
        }

    // ── ⑥ Bounce single-sample: two lamps (different areas) + sun ────────────────────────────────
    for (uint64_t Seed : { 7007ull, 8008ull })
    {
        SplitMix Rng(Seed);
        const int R = 1000000;
        double Acc = 0.0;
        std::vector<Lamp> Lamps{ Lamp1, Lamp2 };
        for (int I = 0; I < R; ++I) Acc += BounceEstimate(Rng, true, Lamps, SunDir, Lsun, Omega, P, N, F);
        char Label[128];
        std::snprintf(Label, sizeof(Label), "bounce NEE converges to lamp1+lamp2+sun (seed %llu)", (unsigned long long)Seed);
        Expect(Close(Acc / R, Q1 + Q2 + Qd, 0.015), Label, Acc / R, Q1 + Q2 + Qd);
    }

    std::printf("\n  %s\n", Failures == 0 ? "ALL SUN-DIRECT ESTIMATOR CHECKS PASSED" : "SUN-DIRECT ESTIMATOR FAILURES PRESENT");
    return Failures == 0 ? 0 : 1;
}
