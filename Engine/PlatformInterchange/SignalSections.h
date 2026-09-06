//============================================================================================================================================
//                                                        SIGNALSECTIONS.H
//============================================================================================================================================
// 🧩 The small per-sample pieces the acoustic voice is built from, ported one for one from the AudioEditor's shared DSP text
//    (Tools/AudioEditor/index.html, <script id="dsp">): Xorshift (xorshift32), OnePole, OnePoleHigh, BiquadSection (RBJ
//    band-pass / high-pass / low-pass), CombLine (feedback comb). Header-only structs, doubles for coefficients and memory,
//    no virtuals, no allocation after construction.
//
//    Every formula keeps the JavaScript's association order and guards (clamp(hz, 1, 0.45·fs), max(0.05, q), the 0.95
//    feedback ceiling) because the two implementations are held to ±1 × 10⁻⁶ per sample over a whole dyno pull
//    (References/AcousticPhaseA-CppPortPlan.md §0) — a re-associated product costs one ulp and the feedback paths grow it.
//    JavaScript's Math.round (ties toward +∞) is RoundHalfUp here; `|0` truncation is a plain integer conversion.

#pragma once

#include <cmath>
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                   SHARED ARITHMETIC
//------------------------------------------------------------------------------------------------------------------------

constexpr double TwoPi = 6.283185307179586;   // [rad] the JavaScript literal (2 × the double π, exact)

[[nodiscard]] inline double ClampReal(double V, double Lo, double Hi) noexcept { return V < Lo ? Lo : (V > Hi ? Hi : V); }

// JavaScript Math.round: nearest integer, ties toward +∞ (std::round would take ties away from zero).
[[nodiscard]] inline double RoundHalfUp(double X) noexcept
{
    const double R = std::floor(X);
    return (X - R) >= 0.5 ? R + 1.0 : R;
}

// One-pole coefficient for a time constant τ [s] at rate Fs [Hz]; τ ≤ 0 → 1 (no smoothing).
[[nodiscard]] inline double LagCoefficient(double Fs, double Tau) noexcept { return Tau > 0.0 ? 1.0 - std::exp(-1.0 / (Fs * Tau)) : 1.0; }

// One-pole coefficient for a −3 dB corner [Hz]; at or above 0.45 × Fs the pole is removed (coefficient 1).
[[nodiscard]] inline double CutoffCoefficient(double Fs, double Hz) noexcept { return Hz >= Fs * 0.45 ? 1.0 : 1.0 - std::exp(-TwoPi * Hz / Fs); }

//------------------------------------------------------------------------------------------------------------------------
//                                                   XORSHIFT
//------------------------------------------------------------------------------------------------------------------------

// xorshift32, bit-exact with the editor: a zero seed becomes 0x9E3779B9; Uniform is in [0, 1) with 2⁻³² steps;
//    Gauss is the sum of four uniforms − 2, scaled by √3 (variance 1). The CALL ORDER is part of the identity contract —
//    AcousticIntegrator consumes randomness in the JavaScript statement order.
struct Xorshift
{
    uint32_t Word = 0x9E3779B9u;

    Xorshift() noexcept = default;
    explicit Xorshift(uint32_t Seed) noexcept : Word(Seed != 0u ? Seed : 0x9E3779B9u) { }

    void Reseed(uint32_t Seed) noexcept { Word = Seed != 0u ? Seed : 0x9E3779B9u; }

    [[nodiscard]] double Uniform() noexcept
    {
        uint32_t X = Word;
        X ^= X << 13;
        X ^= X >> 17;
        X ^= X << 5;
        Word = X;
        return double(X) * 2.3283064365386963e-10;
    }
    [[nodiscard]] double Signed() noexcept { return Uniform() * 2.0 - 1.0; }
    [[nodiscard]] double Gauss() noexcept
    {
        // four sequential draws — written as statements so no compiler reorders the calls
        const double A = Uniform();
        const double B = Uniform();
        const double C = Uniform();
        const double D = Uniform();
        return (A + B + C + D - 2.0) * 1.7320508075688772;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   ONE POLE
//------------------------------------------------------------------------------------------------------------------------

struct OnePole
{
    double A = 1.0;   // [-] smoothing coefficient
    double Y = 0.0;   // [-] memory

    void   SetLag(double Fs, double Tau) noexcept    { A = LagCoefficient(Fs, Tau); }
    void   SetCutoff(double Fs, double Hz) noexcept  { A = CutoffCoefficient(Fs, Hz); }
    double Advance(double X) noexcept                { Y += A * (X - Y); return Y; }
};

// First-order high-pass (DC block / blow-off sweep): y = x − lowpass(x).
struct OnePoleHigh
{
    OnePole Low;

    void   SetCutoff(double Fs, double Hz) noexcept  { Low.SetCutoff(Fs, Hz); }
    double Advance(double X) noexcept                { return X - Low.Advance(X); }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   BIQUAD SECTION
//------------------------------------------------------------------------------------------------------------------------

// RBJ cookbook sections in transposed direct form II; the coefficient formulas keep the editor's expression order.
struct BiquadSection
{
    double B0 = 1.0, B1 = 0.0, B2 = 0.0, A1 = 0.0, A2 = 0.0;   // [-] normalised coefficients
    double Z1 = 0.0, Z2 = 0.0;                                   // [-] memory

    void Bandpass(double Fs, double Hz, double Q) noexcept
    {
        const double W = TwoPi * ClampReal(Hz, 1.0, Fs * 0.45) / Fs, S = std::sin(W), C = std::cos(W);
        const double Al = S / (2.0 * std::max(0.05, Q)), A0 = 1.0 + Al;
        B0 = Al / A0; B1 = 0.0; B2 = -Al / A0; A1 = -2.0 * C / A0; A2 = (1.0 - Al) / A0;
    }
    void Highpass(double Fs, double Hz, double Q) noexcept
    {
        const double W = TwoPi * ClampReal(Hz, 1.0, Fs * 0.45) / Fs, S = std::sin(W), C = std::cos(W);
        const double Al = S / (2.0 * std::max(0.05, Q)), A0 = 1.0 + Al;
        B0 = (1.0 + C) / 2.0 / A0; B1 = -(1.0 + C) / A0; B2 = B0; A1 = -2.0 * C / A0; A2 = (1.0 - Al) / A0;
    }
    void Lowpass(double Fs, double Hz, double Q) noexcept
    {
        const double W = TwoPi * ClampReal(Hz, 1.0, Fs * 0.45) / Fs, S = std::sin(W), C = std::cos(W);
        const double Al = S / (2.0 * std::max(0.05, Q)), A0 = 1.0 + Al;
        B0 = (1.0 - C) / 2.0 / A0; B1 = (1.0 - C) / A0; B2 = B0; A1 = -2.0 * C / A0; A2 = (1.0 - Al) / A0;
    }
    double Advance(double X) noexcept
    {
        const double Y = B0 * X + Z1;
        Z1 = B1 * X - A1 * Y + Z2;
        Z2 = B2 * X - A2 * Y;
        return Y;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   COMB LINE
//------------------------------------------------------------------------------------------------------------------------

// Feedback comb: out = x + mix · d, line ← x + feedback · d (RevSim's delay + feedback gain, both paths summed = mix 1).
//    mix 0 takes the comb out of the chain (the rev-3 exhaust colour comes from the formant resonators instead).
//    Capacity is a power of two, fixed at compile time: 2048 samples = 42.7 ms at 48 kHz, above the schema's 20 ms ceiling.
template<uint32_t Capacity>
struct CombLine
{
    static_assert((Capacity & (Capacity - 1u)) == 0u, "CombLine capacity must be a power of two");

    double   Ring[Capacity] = {};
    uint32_t Head     = 0u;               // [-] write position
    uint32_t Delay    = 1u;               // [samples]
    double   Feedback = 0.0;              // [-]
    double   Mix      = 1.0;              // [-]

    void Configure(double Fs, double Ms, double FeedbackGain, double MixShare) noexcept
    {
        Delay    = uint32_t(ClampReal(RoundHalfUp(Ms * 0.001 * Fs), 1.0, double(Capacity - 1u)));
        Feedback = ClampReal(FeedbackGain, 0.0, 0.95);
        Mix      = ClampReal(MixShare, 0.0, 1.0);
    }
    void Clear() noexcept { for (double& E : Ring) E = 0.0; Head = 0u; }
    double Advance(double X) noexcept
    {
        const double D = Ring[(Head - Delay) & (Capacity - 1u)];
        Ring[Head] = X + Feedback * D;
        Head = (Head + 1u) & (Capacity - 1u);
        return X + Mix * D;
    }
};

} // namespace Frontier
