//============================================================================================================================================
//                                                        TRANSIENTSLOTS.H
//============================================================================================================================================
// 🧩 One-shot voices spawned by throttle events — lift-off backfire thud, overrun pop, anti-lag pop, blow-off flutter, the low
//    blow-off thump and the supercharger intake bark (row A2½) — ported from the AudioEditor's `TransientSlots`
//    (Tools/AudioEditor/index.html, <script id="dsp">). Fixed capacity, no allocation: a spawn that finds no free slot is counted
//    as dropped (the editor's `dropped` readout; the proofs hold it at 0 on the limiter sequence with 32 slots). Spawn takes the
//    editor's arguments in the editor's order so the call sites in AcousticIntegrator read the same as the JavaScript, and
//    Advance draws its noise from the caller's Xorshift in the same statement order (that order is part of the ±1 × 10⁻⁶
//    identity contract).

#pragma once

#include "SignalSections.h"

#include <cmath>
#include <cstdint>

namespace Frontier {

enum class TransientCategory : uint8_t
{
    Backfire = 0,   // triangle at 100 → 40 Hz through a 2 kHz one-pole, with a noise burst
    Pop      = 1,   // triangle 90–150 → 45 Hz, 2.5 kHz one-pole, noise burst
    Antilag  = 2,   // square 80–120 → 30 Hz, 3 kHz one-pole
    Blowoff  = 3,   // high-passed noise sweeping 3 kHz → 1 kHz with a flutter at turbo.flutter_hz (14 Hz until row A2½)
    Thump    = 4,   // low sine thud under the blow-off, thump_hz → 0.8 × thump_hz (the Agera's wastegate "whump")
    Bark     = 5    // first-order band of noise (one-pole high at f0 into the one-pole low), the supercharger bypass valve snapping shut
};

template<uint32_t Capacity>
class TransientSlots
{
public:
    explicit TransientSlots(double SampleRate = 48000.0) noexcept : Fs(SampleRate) { }

    void Prepare(double SampleRate) noexcept
    {
        Fs = SampleRate;
        for (uint32_t I = 0u; I < Capacity; ++I) Active[I] = 0u;
        Dropped = 0u; Spawned = 0u;
    }

    // (type, delay [s], duration [s], f0 [Hz], f1 [Hz], gain, attack [s], noise gain, one-pole corner [Hz], flutter [Hz]) — the editor's
    //    order; the flutter defaults to the editor's `flutterHz === undefined ? 14.0` for every call that does not pass it.
    void Spawn(TransientCategory Type, double Delay, double Dur, double F0, double F1, double Gain, double Attack, double NoiseGain, double LowHz, double FlutterHz = 14.0) noexcept
    {
        int32_t Slot = -1;
        for (uint32_t I = 0u; I < Capacity; ++I) if (!Active[I]) { Slot = int32_t(I); break; }
        if (Slot < 0) { ++Dropped; return; }
        const uint32_t S = uint32_t(Slot);
        Active[S] = 1u; Category[S] = Type; T[S] = 0.0; DelayLeft[S] = Delay; Duration[S] = Dur;
        Start[S] = F0; End[S] = F1; Level[S] = Gain; AttackS[S] = Attack;
        Decay[S] = std::log(std::max(Gain, 0.01) / 0.001) / std::max(0.001, Dur - Attack);
        Phase[S] = 0.0; Noise[S] = NoiseGain; Flutter[S] = FlutterHz;
        Low[S].SetCutoff(Fs, LowHz); Low[S].Y = 0.0; High[S].Low.Y = 0.0;
        if (Type == TransientCategory::Bark) High[S].SetCutoff(Fs, F0);   // bark: one-pole high at f0 into the one-pole low at LowHz = a first-order band
        ++Spawned;
    }

    // Summed transient sample; Rng supplies the noise (consumed only by active, started slots, in slot order).
    [[nodiscard]] double Advance(Xorshift& Rng) noexcept
    {
        double Out = 0.0;
        const double Dt = 1.0 / Fs;
        for (uint32_t I = 0u; I < Capacity; ++I)
        {
            if (!Active[I]) continue;
            if (DelayLeft[I] > 0.0) { DelayLeft[I] -= Dt; continue; }
            const double Time = T[I], Dur = Duration[I];
            const TransientCategory Type = Category[I];
            if (Time >= Dur) { Active[I] = 0u; continue; }
            const double Frac = Time / Dur, Attack = AttackS[I];
            const double Env = Time < Attack ? Time / Attack : std::exp(-(Time - Attack) * Decay[I]);
            if (Type == TransientCategory::Blowoff)
            {
                const double Hz = Start[I] * std::pow(End[I] / Start[I], Frac);
                High[I].SetCutoff(Fs, Hz);
                const double Flut = 0.5 + 0.5 * std::sin(TwoPi * Flutter[I] * Time);
                Out += High[I].Advance(Rng.Signed()) * Level[I] * Env * Flut;
            }
            else if (Type == TransientCategory::Thump)
            {
                // low sine thud under the blow-off (the Agera's wastegate "whump"): f0 → f1 sweep, the standard envelope
                const double Hz = Start[I] * std::pow(End[I] / Start[I], Frac);
                double Ph = Phase[I] + Hz * Dt; if (Ph >= 1.0) Ph -= 1.0; Phase[I] = Ph;
                Out += std::sin(TwoPi * Ph) * Level[I] * Env;
            }
            else if (Type == TransientCategory::Bark)
            {
                // band-limited noise burst (the supercharger bypass valve snapping shut on a throttle stab)
                Out += Low[I].Advance(High[I].Advance(Rng.Signed())) * Level[I] * Env;
            }
            else
            {
                const double Hz = Start[I] * std::pow(End[I] / Start[I], Frac);
                double Ph = Phase[I] + Hz * Dt; if (Ph >= 1.0) Ph -= 1.0; Phase[I] = Ph;
                const double Wave = Type == TransientCategory::Antilag ? (Ph < 0.5 ? 1.0 : -1.0) : 4.0 * std::fabs(Ph - 0.5) - 1.0;
                Out += Low[I].Advance(Wave * Level[I] * Env);
                if (Noise[I] > 0.0)
                {
                    const double NoiseEnv = Time < 0.002 ? Time / 0.002 : std::exp(-(Time - 0.002) * 70.0);   // RevSim: 2 ms attack, gone by 100 ms
                    Out += Rng.Signed() * Noise[I] * NoiseEnv;
                }
            }
            T[I] = Time + Dt;
        }
        return Out;
    }

    [[nodiscard]] uint32_t QueryDropped() const noexcept { return Dropped; }
    [[nodiscard]] uint32_t QuerySpawned() const noexcept { return Spawned; }

private:
    double            Fs;                     // [Hz]
    uint8_t           Active[Capacity]    = {};
    TransientCategory Category[Capacity]  = {};
    double            T[Capacity]         = {};   // [s]  time since the slot started sounding
    double            DelayLeft[Capacity] = {};   // [s]
    double            Duration[Capacity]  = {};   // [s]
    double            Start[Capacity]     = {};   // [Hz]
    double            End[Capacity]       = {};   // [Hz]
    double            Level[Capacity]     = {};   // [-]
    double            AttackS[Capacity]   = {};   // [s]
    double            Decay[Capacity]     = {};   // [1/s]
    double            Phase[Capacity]     = {};   // [-]  0 … 1
    double            Noise[Capacity]     = {};   // [-]
    double            Flutter[Capacity]   = {};   // [Hz] blow-off flutter rate
    OnePole           Low[Capacity];
    OnePoleHigh       High[Capacity];
    uint32_t          Dropped = 0u;
    uint32_t          Spawned = 0u;
};

} // namespace Frontier
