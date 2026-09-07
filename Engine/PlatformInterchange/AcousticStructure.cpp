//============================================================================================================================================
//                                                      ACOUSTICSTRUCTURE.CPP
//============================================================================================================================================
// 🧩 The field sheet and its four consumers (defaults, Deserialise, Serialise, field access). See AcousticStructure.h.
//
//    Serialise is held byte-identical to the editor's `serialiseToml` (Tools/AudioEditor/index.html): same 22-column key pad,
//    same 56-column comment pad counted in UTF-16 code units the way String.prototype.padEnd counts, same number text
//    (`tomlNumber`: whole keys → the rounded integer, integral reals → one decimal, others → JavaScript's shortest text of the
//    value rounded to 6 significant digits, ties toward the larger magnitude as Number.prototype.toPrecision specifies).
//    The archives round-trip through both writers without a byte of drift — Scratchpad/AcousticStructureTest proves it.

#include "AcousticStructure.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE SHEET
//------------------------------------------------------------------------------------------------------------------------

// Generated from ACOUSTIC_SCHEMA (Tools/AudioEditor/index.html) — keep the two in step; the count is fixed by the
//    declaration and Scratchpad/AcousticStructureTest diffs every row against the page at run time.
const AcousticFieldNote AcousticStructure::Fields[AcousticFieldCount] =
{
    // [vehicle]
    { "vehicle", "name", AcousticFieldCategory::Text, offsetof(AcousticStructure, Vehicle.Name), 0.0, "Generic V8", "", "display name", 0.0, 0.0, 0.0, false },
    { "vehicle", "engine", AcousticFieldCategory::Text, offsetof(AcousticStructure, Vehicle.Engine), 0.0, "4.0 L 90° V8", "", "one-line engine description", 0.0, 0.0, 0.0, false },
    { "vehicle", "cylinder_count", AcousticFieldCategory::Whole, offsetof(AcousticStructure, Vehicle.CylinderCount), 8.0, "", "-", "number of cylinders", 1.0, 16.0, 1.0, true },
    { "vehicle", "firing_order", AcousticFieldCategory::RealList, offsetof(AcousticStructure, Vehicle.FiringOrder), 0.0, "", "-", "cylinder numbers in firing sequence; even 720/N intervals unless even_firing is off", 0.0, 0.0, 0.0, false },
    { "vehicle", "bank_of_cylinder", AcousticFieldCategory::RealList, offsetof(AcousticStructure, Vehicle.BankOfCylinder), 0.0, "", "-", "bank per cylinder number (0 = left pipe, 1 = right pipe); the pan and the per-bank trains follow it", 0.0, 0.0, 0.0, false },
    { "vehicle", "even_firing", AcousticFieldCategory::Toggle, offsetof(AcousticStructure, Vehicle.EvenFiring), 1.0, "", "-", "on = firings at even 720/N (split pins, inline, flat-plane, 90° V8); off = shared straight pins on a V: each bank fires evenly at 720/(N/2), bank 1 trails bank 0 by bank_angle_deg (F140 65° V12 → 65° / 55° alternation)", 0.0, 0.0, 0.0, false },
    { "vehicle", "bank_angle_deg", AcousticFieldCategory::Real, offsetof(AcousticStructure, Vehicle.BankAngleDeg), 90.0, "", "°", "V angle between the banks; only read when even_firing is off", 0.0, 180.0, 1.0, true },
    { "vehicle", "idle_rpm", AcousticFieldCategory::Real, offsetof(AcousticStructure, Vehicle.IdleRpm), 900.0, "", "rpm", "", 500.0, 2000.0, 10.0, true },
    { "vehicle", "redline_rpm", AcousticFieldCategory::Real, offsetof(AcousticStructure, Vehicle.RedlineRpm), 8000.0, "", "rpm", "fuel cut", 3000.0, 12000.0, 50.0, true },
    { "vehicle", "peak_power_rpm", AcousticFieldCategory::Real, offsetof(AcousticStructure, Vehicle.PeakPowerRpm), 7500.0, "", "rpm", "", 3000.0, 12000.0, 50.0, true },
    { "vehicle", "peak_torque_rpm", AcousticFieldCategory::Real, offsetof(AcousticStructure, Vehicle.PeakTorqueRpm), 5500.0, "", "rpm", "", 1500.0, 10000.0, 50.0, true },
    { "vehicle", "peak_torque_nm", AcousticFieldCategory::Real, offsetof(AcousticStructure, Vehicle.PeakTorqueNm), 500.0, "", "N·m", "", 100.0, 1500.0, 10.0, true },
    { "vehicle", "inertia_kgm2", AcousticFieldCategory::Real, offsetof(AcousticStructure, Vehicle.InertiaKgm2), 0.4, "", "kg·m²", "effective rotating inertia seen by the free-rev powertrain", 0.05, 2.0, 0.01, true },
    { "vehicle", "friction_nm", AcousticFieldCategory::Real, offsetof(AcousticStructure, Vehicle.FrictionNm), 25.0, "", "N·m", "friction torque at zero rpm", 0.0, 100.0, 1.0, true },
    { "vehicle", "friction_per_krpm_nm", AcousticFieldCategory::Real, offsetof(AcousticStructure, Vehicle.FrictionPerKrpmNm), 6.0, "", "N·m", "friction torque increase per 1000 rpm", 0.0, 30.0, 0.5, true },
    // [combustion]
    { "combustion", "evo_deg", AcousticFieldCategory::Real, offsetof(AcousticStructure, Combustion.EvoDeg), 130.0, "", "°", "voice starts this many degrees after the firing TDC (exhaust valve opening)", 0.0, 170.0, 1.0, true },
    { "combustion", "jitter_pct", AcousticFieldCategory::Real, offsetof(AcousticStructure, Combustion.JitterPct), 5.0, "", "%", "firing time walks by a uniform ±half of this share of the interval per firing, cumulative (RevSim 5 %); the walk eases back to the crank", 0.0, 15.0, 0.1, true },
    { "combustion", "idle_jitter_boost", AcousticFieldCategory::Real, offsetof(AcousticStructure, Combustion.IdleJitterBoost), 1.0, "", "-", "jitter grows by this factor toward zero load", 0.0, 6.0, 0.1, true },
    { "combustion", "jitter_amp", AcousticFieldCategory::Real, offsetof(AcousticStructure, Combustion.JitterAmp), 0.05, "", "-", "cycle-to-cycle amplitude deviation (1 σ)", 0.0, 0.3, 0.005, true },
    { "combustion", "timing_jitter_deg", AcousticFieldCategory::Real, offsetof(AcousticStructure, Combustion.TimingJitterDeg), 0.0, "", "°", "per-firing timing jitter (1 σ, crank degrees), not cumulative — rev 3 keeps ≤ 0.3°; 0 = off", 0.0, 2.0, 0.05, true },
    { "combustion", "cylinder_spread_pct", AcousticFieldCategory::Real, offsetof(AcousticStructure, Combustion.CylinderSpreadPct), 0.0, "", "%", "fixed cylinder-to-cylinder amplitude imbalance at full load (peak, % of the pulse; × (1 + idle_jitter_boost) at zero load), one pattern along the firing order repeating every revolution — puts the integer orders between the firing harmonics (1, 2, 4, 5, 7, 8 …) where a real idle has them, no half-orders", 0.0, 15.0, 0.5, true },
    { "combustion", "misfire_probability", AcousticFieldCategory::Real, offsetof(AcousticStructure, Combustion.MisfireProbability), 0.0, "", "-", "per firing at full load; ×4 toward zero load", 0.0, 0.2, 0.001, true },
    { "combustion", "backfire_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Combustion.BackfireLevel), 2.0, "", "-", "lift-off backfire thud (throttle > 0.6 → < 0.1 above 60 % redline)", 0.0, 4.0, 0.05, true },
    { "combustion", "backfire_probability", AcousticFieldCategory::Real, offsetof(AcousticStructure, Combustion.BackfireProbability), 0.2, "", "-", "chance of the thud per lift", 0.0, 1.0, 0.01, true },
    { "combustion", "pop_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Combustion.PopLevel), 0.6, "", "-", "overrun crackle amplitude", 0.0, 3.0, 0.01, true },
    { "combustion", "pop_rate_hz", AcousticFieldCategory::Real, offsetof(AcousticStructure, Combustion.PopRateHz), 25.0, "", "Hz", "overrun crackle rate at redline right after lift", 0.0, 100.0, 1.0, true },
    { "combustion", "pop_decay_s", AcousticFieldCategory::Real, offsetof(AcousticStructure, Combustion.PopDecayS), 1.2, "", "s", "overrun crackle fades with this time constant", 0.1, 5.0, 0.05, true },
    // [voice]
    { "voice", "crank_pulse", AcousticFieldCategory::Toggle, offsetof(AcousticStructure, Voice.CrankPulse), 0.0, "", "-", "rev 3: each firing plays one blowdown kernel defined over crank degrees (length ∝ 1/rpm, no fixed pitch, no body sheet); off = rev-2 thump + body voice at pitch_hz", 0.0, 0.0, 0.0, false },
    { "voice", "pulse_rise_deg", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.PulseRiseDeg), 12.0, "", "°", "crank_pulse: blowdown rise from exhaust valve opening to the discharge peak", 2.0, 40.0, 0.5, true },
    { "voice", "pulse_blowdown_deg", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.PulseBlowdownDeg), 45.0, "", "°", "crank_pulse: blowdown decay length (1/e² point)", 10.0, 120.0, 1.0, true },
    { "voice", "pulse_tail_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.PulseTailLevel), 0.25, "", "-", "crank_pulse: displacement-stroke discharge (piston velocity BDC → TDC) re the full-load blowdown", 0.0, 1.0, 0.01, true },
    { "voice", "pulse_undershoot", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.PulseUndershoot), 0.3, "", "-", "crank_pulse: share of the reverse piston velocity after TDC (valve still open through the overlap) that shows as suck-back", 0.0, 1.0, 0.01, true },
    { "voice", "pulse_sharpness", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.PulseSharpness), 1.0, "", "-", "crank_pulse: rise curvature — 1 = raised cosine over pulse_rise_deg, < 1 front-loads the rise (brighter), > 1 back-loads it (softer)", 0.25, 4.0, 0.05, true },
    { "voice", "pulse_soft_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.PulseSoftLevel), 0.25, "", "-", "crank_pulse: blowdown at zero load re full load (the voice crossfades hard ↔ soft with load)", 0.0, 1.0, 0.01, true },
    { "voice", "pulse_soft_stretch", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.PulseSoftStretch), 1.5, "", "-", "crank_pulse: zero-load rise is this many times longer (decay ½ (1 + stretch) ×) — a throttled cylinder blows down more gently", 1.0, 8.0, 0.1, true },
    { "voice", "pitch_hz", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.PitchHz), 65.0, "", "Hz", "rev-2 voice: pulse repetition inside one firing at 0 rpm (the \"thump\" pitch)", 20.0, 200.0, 0.5, true },
    { "voice", "pitch_rise_hz", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.PitchRiseHz), 15.0, "", "Hz", "added to the pitch at 8000 rpm", 0.0, 100.0, 0.5, true },
    { "voice", "decay_per_s", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.DecayPerS), 15.0, "", "1/s", "voice envelope decay at 0 rpm", 2.0, 80.0, 0.5, true },
    { "voice", "decay_per_krpm", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.DecayPerKrpm), 3.0, "", "1/s", "added decay per 1000 rpm", 0.0, 20.0, 0.1, true },
    { "voice", "thump_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.ThumpLevel), 0.8, "", "-", "pressure pulse amplitude (rev 2: the asymmetric thump sheet; crank_pulse: the kernel peak)", 0.0, 2.0, 0.01, true },
    { "voice", "body_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.BodyLevel), 0.6, "", "-", "rev-2 voice: harmonic body (24 harmonics of the pitch); unused with crank_pulse", 0.0, 2.0, 0.01, true },
    { "voice", "harmonics", AcousticFieldCategory::RealList, offsetof(AcousticStructure, Voice.Harmonics), 0.0, "", "-", "weights of harmonics 1 … n; beyond the list 0.5 / n^1.2", 0.0, 0.0, 0.0, false },
    { "voice", "resonance", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.Resonance), 0.5, "", "-", "formant climb with rpm; above 1.0 the rasp switches on", 0.0, 3.0, 0.05, true },
    { "voice", "crackle_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.CrackleLevel), 0.1, "", "-", "per-firing noise burst at zero load", 0.0, 1.0, 0.01, true },
    { "voice", "crackle_load_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.CrackleLoadLevel), 0.4, "", "-", "added at full load", 0.0, 2.0, 0.01, true },
    { "voice", "ring_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.RingLevel), 0.1, "", "-", "valve ring at zero load (5 / 6 / 7 kHz by cylinder)", 0.0, 1.0, 0.01, true },
    { "voice", "ring_load_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.RingLoadLevel), 0.2, "", "-", "added at full load", 0.0, 1.0, 0.01, true },
    { "voice", "ring_hz", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.RingHz), 5000.0, "", "Hz", "ring of cylinder 1; +1 kHz per cylinder mod 3", 1000.0, 12000.0, 50.0, true },
    { "voice", "ring_fade_rpm", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.RingFadeRpm), 4000.0, "", "rpm", "ring is gone above this speed", 500.0, 12000.0, 50.0, true },
    { "voice", "rasp_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.RaspLevel), 1.5, "", "-", "AM rasp (3 × pitch carrier, ½ × pitch modulator); needs resonance > 1", 0.0, 4.0, 0.05, true },
    { "voice", "howl_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.HowlLevel), 0.0, "", "-", "intake howl above howl_rpm", 0.0, 2.0, 0.01, true },
    { "voice", "howl_rpm", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.HowlRpm), 4000.0, "", "rpm", "", 1000.0, 12000.0, 50.0, true },
    { "voice", "howl_span_rpm", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.HowlSpanRpm), 5000.0, "", "rpm", "howl reaches full level this far above howl_rpm", 500.0, 10000.0, 50.0, true },
    { "voice", "cam_order", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.CamOrder), 4.0, "", "-", "amplitude modulation order relative to the cam (rpm / 120 Hz)", 1.0, 12.0, 1.0, true },
    { "voice", "cam_depth", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.CamDepth), 0.0, "", "-", "modulation depth (0 for flat-plane cranks)", 0.0, 0.6, 0.01, true },
    { "voice", "lift_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Voice.LiftLevel), 1.0, "", "-", "voice amplitude at zero load (fuel cut / overrun); 1 = unchanged (RevSim)", 0.0, 1.0, 0.01, true },
    // [intake]
    { "intake", "level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Intake.Level), 0.0, "", "-", "rev 3 intake voice on the induction bus: suction pulses at intake valve opening through the runner resonator; scales with throttle and with rpm / redline (mass throughput); 0 = off", 0.0, 8.0, 0.01, true },
    { "intake", "direct", AcousticFieldCategory::Real, offsetof(AcousticStructure, Intake.Direct), 0.3, "", "-", "share of the raw suction train radiated straight out of the trumpet mouth (high-passed at 2 × the runner frequency: a small mouth only radiates the top), re level", 0.0, 2.0, 0.01, true },
    { "intake", "path_m", AcousticFieldCategory::Real, offsetof(AcousticStructure, Intake.PathM), 0.0, "", "m", "extra path of the intake voice to the listener re the tailpipes (trumpets under the rear deck vs tailpipes at the bumper): a delay of path / c on the left channel, path + INTAKE_EAR_OFFSET_M on the right — the two crank-locked voices then interfere the way two separated emitters do (narrow dips that sweep with rpm, never the same order in both ears) instead of cancelling one order across the whole rev range; 0 = in phase", 0.0, 6.0, 0.05, true },
    { "intake", "runner_hz", AcousticFieldCategory::Real, offsetof(AcousticStructure, Intake.RunnerHz), 220.0, "", "Hz", "quarter-wave frequency of the long tract (c / 4L); the resonator sits here and at 3× (odd modes)", 50.0, 1000.0, 5.0, true },
    { "intake", "short_ratio", AcousticFieldCategory::Real, offsetof(AcousticStructure, Intake.ShortRatio), 2.0, "", "-", "tract length is cut to 1/ratio at step_rpm_1 (LaFerrari: halved → ×2), then eases back to 0.75 × this by step_rpm_2 (tract lengthens again)", 1.0, 4.0, 0.05, true },
    { "intake", "min_ratio", AcousticFieldCategory::Real, offsetof(AcousticStructure, Intake.MinRatio), 2.6, "", "-", "frequency ratio above step_rpm_2 (minimum tract length)", 1.0, 5.0, 0.05, true },
    { "intake", "step_rpm_1", AcousticFieldCategory::Real, offsetof(AcousticStructure, Intake.StepRpm1), 6000.0, "", "rpm", "first tract step", 1000.0, 12000.0, 50.0, true },
    { "intake", "step_rpm_2", AcousticFieldCategory::Real, offsetof(AcousticStructure, Intake.StepRpm2), 8000.0, "", "rpm", "second tract step (minimum length above it)", 1000.0, 12000.0, 50.0, true },
    { "intake", "q", AcousticFieldCategory::Real, offsetof(AcousticStructure, Intake.Q), 9.0, "", "-", "runner resonator Q (ring length of the trumpet)", 1.0, 40.0, 0.5, true },
    { "intake", "breath", AcousticFieldCategory::Real, offsetof(AcousticStructure, Intake.Breath), 0.2, "", "-", "aspiration noise through the same resonator, ∝ throttle · rpm / redline, re level", 0.0, 2.0, 0.01, true },
    // [exhaust]
    { "exhaust", "drive", AcousticFieldCategory::Real, offsetof(AcousticStructure, Exhaust.Drive), 1.0, "", "-", "multiplies the per-channel tanh drive (1 + 0.8 · load)", 0.1, 6.0, 0.05, true },
    { "exhaust", "bank_pan", AcousticFieldCategory::Real, offsetof(AcousticStructure, Exhaust.BankPan), 0.9, "", "-", "0 = both banks centred … 1 = hard left/right", 0.0, 1.0, 0.01, true },
    { "exhaust", "pan_split_hz", AcousticFieldCategory::Real, offsetof(AcousticStructure, Exhaust.PanSplitHz), 0.0, "", "Hz", "below this the two banks merge in the image (tailpipes a metre apart are one emitter at long wavelengths — the 55/65° bank offset then cancels order N/4 the way the real car does); the pan applies above it; 0 = rev-2 full-band pan", 0.0, 2000.0, 10.0, true },
    { "exhaust", "formant_a_hz", AcousticFieldCategory::Real, offsetof(AcousticStructure, Exhaust.FormantAHz), 180.0, "", "Hz", "fixed exhaust resonator A (collector / silencer ridge that stays put while the pitch moves); level 0 = off", 40.0, 4000.0, 1.0, true },
    { "exhaust", "formant_a_q", AcousticFieldCategory::Real, offsetof(AcousticStructure, Exhaust.FormantAQ), 8.0, "", "-", "", 0.5, 40.0, 0.5, true },
    { "exhaust", "formant_a_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Exhaust.FormantALevel), 0.0, "", "-", "added to the exhaust bus after the tanh", 0.0, 3.0, 0.01, true },
    { "exhaust", "formant_b_hz", AcousticFieldCategory::Real, offsetof(AcousticStructure, Exhaust.FormantBHz), 215.0, "", "Hz", "fixed exhaust resonator B; level 0 = off", 40.0, 4000.0, 1.0, true },
    { "exhaust", "formant_b_q", AcousticFieldCategory::Real, offsetof(AcousticStructure, Exhaust.FormantBQ), 8.0, "", "-", "", 0.5, 40.0, 0.5, true },
    { "exhaust", "formant_b_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Exhaust.FormantBLevel), 0.0, "", "-", "", 0.0, 3.0, 0.01, true },
    { "exhaust", "silencer_hz", AcousticFieldCategory::Real, offsetof(AcousticStructure, Exhaust.SilencerHz), 24000.0, "", "Hz", "4th-order low-pass on the exhaust bus while the bypass valve is shut (cat + silencer + tailpipe); ≥ 0.45 × sample rate = off", 100.0, 24000.0, 10.0, true },
    { "exhaust", "silencer_open_hz", AcousticFieldCategory::Real, offsetof(AcousticStructure, Exhaust.SilencerOpenHz), 24000.0, "", "Hz", "the same low-pass above valve_open_rpm (bypass open)", 100.0, 24000.0, 10.0, true },
    { "exhaust", "valve_open_gain", AcousticFieldCategory::Real, offsetof(AcousticStructure, Exhaust.ValveOpenGain), 1.0, "", "-", "exhaust bus gain above valve_open_rpm (the \"opens up\" step; 0.2 s lag like the high-pass)", 0.5, 3.0, 0.05, true },
    { "exhaust", "comb_mix", AcousticFieldCategory::Real, offsetof(AcousticStructure, Exhaust.CombMix), 1.0, "", "-", "share of the feedback comb output added to the bus (RevSim 1; 0 = comb off — rev 3 uses the formants instead)", 0.0, 1.0, 0.01, true },
    { "exhaust", "comb_ms", AcousticFieldCategory::Real, offsetof(AcousticStructure, Exhaust.CombMs), 2.667, "", "ms", "feedback comb delay (pipe colour); 2.667 ms = the 128-frame minimum a WebAudio delay in a feedback cycle is held to (what RevSim plays at 48 kHz)", 0.2, 20.0, 0.001, true },
    { "exhaust", "comb_feedback", AcousticFieldCategory::Real, offsetof(AcousticStructure, Exhaust.CombFeedback), 0.3, "", "-", "", 0.0, 0.9, 0.01, true },
    { "exhaust", "valve_hp_hz", AcousticFieldCategory::Real, offsetof(AcousticStructure, Exhaust.ValveHpHz), 10.0, "", "Hz", "high-pass below valve_open_rpm (exhaust valve shut)", 5.0, 1000.0, 5.0, true },
    { "exhaust", "valve_open_hp_hz", AcousticFieldCategory::Real, offsetof(AcousticStructure, Exhaust.ValveOpenHpHz), 10.0, "", "Hz", "high-pass above valve_open_rpm", 5.0, 1000.0, 5.0, true },
    { "exhaust", "valve_open_rpm", AcousticFieldCategory::Real, offsetof(AcousticStructure, Exhaust.ValveOpenRpm), 5000.0, "", "rpm", "", 1000.0, 12000.0, 50.0, true },
    { "exhaust", "valve_open_load", AcousticFieldCategory::Real, offsetof(AcousticStructure, Exhaust.ValveOpenLoad), 2.0, "", "-", "the bypass valve also opens above this load (above 1 = rpm only)", 0.0, 2.0, 0.05, true },
    // [mechanical]
    { "mechanical", "clatter_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Mechanical.ClatterLevel), 0.0, "", "-", "valve seating clatter (crank-locked)", 0.0, 2.0, 0.01, true },
    { "mechanical", "clatter_hz", AcousticFieldCategory::Real, offsetof(AcousticStructure, Mechanical.ClatterHz), 4500.0, "", "Hz", "clatter resonance", 1000.0, 12000.0, 50.0, true },
    { "mechanical", "whine_order", AcousticFieldCategory::Real, offsetof(AcousticStructure, Mechanical.WhineOrder), 0.0, "", "-", "gear whine order relative to crank speed (0 = none)", 0.0, 300.0, 0.5, true },
    { "mechanical", "whine_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Mechanical.WhineLevel), 0.0, "", "-", "at redline; scales with rpm", 0.0, 0.5, 0.001, true },
    { "mechanical", "motor_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Mechanical.MotorLevel), 0.0, "", "-", "hybrid drive motor tone, scales with load (0 = none)", 0.0, 0.5, 0.001, true },
    { "mechanical", "motor_hz", AcousticFieldCategory::Real, offsetof(AcousticStructure, Mechanical.MotorHz), 200.0, "", "Hz", "motor tone at zero load", 50.0, 4000.0, 10.0, true },
    { "mechanical", "motor_load_hz", AcousticFieldCategory::Real, offsetof(AcousticStructure, Mechanical.MotorLoadHz), 600.0, "", "Hz", "added at full load", 0.0, 8000.0, 10.0, true },
    // [turbo]
    { "turbo", "enabled", AcousticFieldCategory::Toggle, offsetof(AcousticStructure, Turbo.Enabled), 0.0, "", "-", "forced-induction layer", 0.0, 0.0, 0.0, false },
    { "turbo", "spool_up_s", AcousticFieldCategory::Real, offsetof(AcousticStructure, Turbo.SpoolUpS), 0.2, "", "s", "spool follows rpm / redline · load with this lag", 0.05, 3.0, 0.01, true },
    { "turbo", "spool_down_s", AcousticFieldCategory::Real, offsetof(AcousticStructure, Turbo.SpoolDownS), 0.2, "", "s", "", 0.05, 5.0, 0.01, true },
    { "turbo", "whine_hz", AcousticFieldCategory::Real, offsetof(AcousticStructure, Turbo.WhineHz), 2500.0, "", "Hz", "compressor whine at zero spool", 500.0, 8000.0, 50.0, true },
    { "turbo", "whine_span_hz", AcousticFieldCategory::Real, offsetof(AcousticStructure, Turbo.WhineSpanHz), 4000.0, "", "Hz", "added at full spool", 0.0, 16000.0, 50.0, true },
    { "turbo", "whine_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Turbo.WhineLevel), 0.4, "", "-", "at full spool", 0.0, 2.0, 0.01, true },
    { "turbo", "rush_hz", AcousticFieldCategory::Real, offsetof(AcousticStructure, Turbo.RushHz), 2000.0, "", "Hz", "air-rush band-pass centre at zero spool", 200.0, 8000.0, 50.0, true },
    { "turbo", "rush_span_hz", AcousticFieldCategory::Real, offsetof(AcousticStructure, Turbo.RushSpanHz), 6000.0, "", "Hz", "added at full spool", 0.0, 16000.0, 50.0, true },
    { "turbo", "rush_q", AcousticFieldCategory::Real, offsetof(AcousticStructure, Turbo.RushQ), 1.5, "", "-", "", 0.3, 10.0, 0.1, true },
    { "turbo", "rush_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Turbo.RushLevel), 0.4, "", "-", "at full spool", 0.0, 2.0, 0.01, true },
    { "turbo", "boost_max_bar", AcousticFieldCategory::Real, offsetof(AcousticStructure, Turbo.BoostMaxBar), 1.0, "", "bar", "readout only", 0.0, 3.0, 0.05, true },
    { "turbo", "blowoff_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Turbo.BlowoffLevel), 0.8, "", "-", "blow-off flutter on lift above 3000 rpm", 0.0, 3.0, 0.01, true },
    { "turbo", "antilag_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Turbo.AntilagLevel), 3.0, "", "-", "anti-lag pops instead of the blow-off above antilag_rpm (0 = off)", 0.0, 6.0, 0.05, true },
    { "turbo", "antilag_rpm", AcousticFieldCategory::Real, offsetof(AcousticStructure, Turbo.AntilagRpm), 4000.0, "", "rpm", "", 1000.0, 12000.0, 50.0, true },
    { "turbo", "flutter_hz", AcousticFieldCategory::Real, offsetof(AcousticStructure, Turbo.FlutterHz), 14.0, "", "Hz", "blow-off flutter rate (the recirculation valve chattering; 14 = RevSim, the Agera's wastegate chatter ≈ 25)", 4.0, 60.0, 0.5, true },
    { "turbo", "thump_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Turbo.ThumpLevel), 0.0, "", "-", "low sine thud under the blow-off on lift (the Agera's \"whump\"); 0 = off", 0.0, 3.0, 0.01, true },
    { "turbo", "thump_hz", AcousticFieldCategory::Real, offsetof(AcousticStructure, Turbo.ThumpHz), 60.0, "", "Hz", "thud pitch", 30.0, 200.0, 1.0, true },
    // [charger]
    { "charger", "enabled", AcousticFieldCategory::Toggle, offsetof(AcousticStructure, Charger.Enabled), 0.0, "", "-", "positive-displacement supercharger layer (belt-driven twin-screw / roots): rotor pulsation whine, boost ∝ rpm · load, bypass valve with the throttle shut", 0.0, 0.0, 0.0, false },
    { "charger", "drive_ratio", AcousticFieldCategory::Real, offsetof(AcousticStructure, Charger.DriveRatio), 2.36, "", "-", "rotor speed re crank speed (pulley ratio; Hellcat / Demon IHI 2.36)", 0.5, 5.0, 0.01, true },
    { "charger", "lobes", AcousticFieldCategory::Real, offsetof(AcousticStructure, Charger.Lobes), 3.0, "", "-", "lobes on the driven (male) rotor: pocket-passing order = lobes × drive_ratio re crank (IHI 3 × 5 rotors → order 7.08)", 1.0, 8.0, 1.0, true },
    { "charger", "whine_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Charger.WhineLevel), 0.3, "", "-", "pulsation whine at redline, full load; scales with rpm / redline (throughput)", 0.0, 2.0, 0.01, true },
    { "charger", "whine_harmonics", AcousticFieldCategory::RealList, offsetof(AcousticStructure, Charger.WhineHarmonics), 0.0, "", "-", "weights of the pulsation harmonics 1 … n (up to 8); beyond the list 0", 0.0, 0.0, 0.0, false },
    { "charger", "whine_drive", AcousticFieldCategory::Real, offsetof(AcousticStructure, Charger.WhineDrive), 1.0, "", "-", "tanh shaping of the summed harmonics (> 1 = the gritty, over-compressed pulse; 1 = clean)", 0.25, 8.0, 0.05, true },
    { "charger", "bypass_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Charger.BypassLevel), 0.15, "", "-", "share of the whine left with the throttle shut (the internal bypass valve open, rotors idling)", 0.0, 1.0, 0.01, true },
    { "charger", "boost_max_bar", AcousticFieldCategory::Real, offsetof(AcousticStructure, Charger.BoostMaxBar), 1.0, "", "bar", "readout only: boost = boost_max · rpm / redline · load (no spool — positive displacement)", 0.0, 3.0, 0.05, true },
    { "charger", "bark_level", AcousticFieldCategory::Real, offsetof(AcousticStructure, Charger.BarkLevel), 0.0, "", "-", "intake bark on a fast throttle stab (shut → > 80 % within 0.25 s: the bypass valve snapping shut); 0 = off", 0.0, 3.0, 0.01, true },
    { "charger", "bark_hz", AcousticFieldCategory::Real, offsetof(AcousticStructure, Charger.BarkHz), 600.0, "", "Hz", "bark band centre (first-order band bark_hz / 1.6 … × 1.6)", 100.0, 3000.0, 10.0, true },
    // [mix]
    { "mix", "exhaust", AcousticFieldCategory::Real, offsetof(AcousticStructure, Mix.Exhaust), 1.0, "", "-", "bus levels after the tanh (listener presets multiply these)", 0.0, 4.0, 0.01, true },
    { "mix", "induction", AcousticFieldCategory::Real, offsetof(AcousticStructure, Mix.Induction), 1.0, "", "-", "turbo whine + rush · rev 3 intake voice", 0.0, 4.0, 0.01, true },
    { "mix", "mechanical", AcousticFieldCategory::Real, offsetof(AcousticStructure, Mix.Mechanical), 1.0, "", "-", "clatter · gear whine · motor", 0.0, 4.0, 0.01, true },
    { "mix", "transients", AcousticFieldCategory::Real, offsetof(AcousticStructure, Mix.Transients), 1.0, "", "-", "backfire · pops · anti-lag · blow-off", 0.0, 4.0, 0.01, true },
    { "mix", "output_gain", AcousticFieldCategory::Real, offsetof(AcousticStructure, Mix.OutputGain), 1.5, "", "-", "before the comb and the ±1 clip (RevSim master 1.5)", 0.1, 8.0, 0.05, true },
};

namespace {

const AcousticFieldNote* const FieldSheet = AcousticStructure::Fields;

//------------------------------------------------------------------------------------------------------------------------
//                                                   NUMBER TEXT (JavaScript rules)
//------------------------------------------------------------------------------------------------------------------------

// JavaScript Math.round: ties toward +∞.
double RoundHalfUpJs(double X) noexcept
{
    const double R = std::floor(X);
    return (X - R) >= 0.5 ? R + 1.0 : R;
}

// Number.prototype.toString for a finite double: the shortest digit string that round-trips, laid out by the ECMAScript
//    notation rules (fixed for 10⁻⁶ ≤ |x| < 10²¹, exponential outside).
std::string JsNumberText(double V)
{
    if (V == 0.0 || !std::isfinite(V)) return "0";
    char Buf[64] = {};
    for (int Precision = 1; Precision <= 17; ++Precision)
    {
        std::snprintf(Buf, sizeof(Buf), "%.*e", Precision - 1, V);
        if (std::strtod(Buf, nullptr) == V) break;
    }
    std::string Text(Buf);
    const bool Negative = Text[0] == '-';
    if (Negative) Text.erase(0, 1);
    const size_t E = Text.find('e');
    std::string Digits = Text.substr(0, E);
    Digits.erase(std::remove(Digits.begin(), Digits.end(), '.'), Digits.end());
    while (Digits.size() > 1u && Digits.back() == '0') Digits.pop_back();
    const int N = std::atoi(Text.c_str() + E + 1) + 1;   // decimal point sits after N digits
    const int K = int(Digits.size());
    std::string Out;
    if (K <= N && N <= 21)       Out = Digits + std::string(size_t(N - K), '0');
    else if (0 < N && N <= 21)   Out = Digits.substr(0, size_t(N)) + "." + Digits.substr(size_t(N));
    else if (-6 < N && N <= 0)   Out = "0." + std::string(size_t(-N), '0') + Digits;
    else
    {
        Out = Digits.substr(0, 1);
        if (K > 1) Out += "." + Digits.substr(1);
        Out += (N - 1 >= 0) ? "e+" : "e-";
        Out += std::to_string(std::abs(N - 1));
    }
    return Negative ? "-" + Out : Out;
}

// Number(x.toPrecision(6)): the value rounded to six significant digits on its exact decimal expansion, ties toward the
//    larger magnitude, returned as the nearest double.
double ToPrecision6(double X)
{
    if (X == 0.0 || !std::isfinite(X)) return X;
    const bool Negative = X < 0.0;
    char Buf[96] = {};
    std::snprintf(Buf, sizeof(Buf), "%.39e", std::fabs(X));   // exact decimal expansion of the double (40 digits)
    std::string Text(Buf);
    const size_t E = Text.find('e');
    std::string Digits = Text.substr(0, E);
    Digits.erase(std::remove(Digits.begin(), Digits.end(), '.'), Digits.end());
    int Exponent = std::atoi(Text.c_str() + E + 1);
    bool Up = false;
    if (Digits.size() > 6u)
    {
        const char Seventh = Digits[6];
        if (Seventh > '5') Up = true;
        else if (Seventh == '5') Up = true;               // exact tie or above: toPrecision picks the larger n
        Digits.resize(6u);
    }
    if (Up)
    {
        int I = 5;
        while (I >= 0) { if (Digits[size_t(I)] == '9') { Digits[size_t(I)] = '0'; --I; } else { ++Digits[size_t(I)]; break; } }
        if (I < 0) { Digits = "100000"; ++Exponent; }
    }
    char Rounded[64] = {};
    std::snprintf(Rounded, sizeof(Rounded), "%c.%se%d", Digits[0], Digits.c_str() + 1, Exponent);
    const double V = std::strtod(Rounded, nullptr);
    return Negative ? -V : V;
}

// The editor's tomlNumber(v, wholeNumber).
std::string TomlNumber(double V, bool Whole)
{
    if (Whole) return JsNumberText(RoundHalfUpJs(V));
    if (std::isfinite(V) && V == std::floor(V))
    {
        char Buf[64] = {};
        std::snprintf(Buf, sizeof(Buf), "%.1f", V);
        return Buf;
    }
    std::string Text = JsNumberText(ToPrecision6(V));
    if (Text.find('.') == std::string::npos && Text.find('e') == std::string::npos) Text += ".0";
    return Text;
}

// String.prototype.padEnd counts UTF-16 code units: one per code point below U+10000, two above.
size_t Utf16Length(std::string_view S) noexcept
{
    size_t N = 0u;
    for (unsigned char C : S)
    {
        if ((C & 0xC0u) != 0x80u) ++N;      // every lead byte / ASCII byte starts a code point
        if ((C & 0xF8u) == 0xF0u) ++N;      // a 4-byte sequence is a surrogate pair
    }
    return N;
}

void PadEnd(std::string& S, size_t Columns) { const size_t L = Utf16Length(S); if (L < Columns) S.append(Columns - L, ' '); }

std::string QuoteText(const char* Text)
{
    std::string Out = "\"";
    for (const char* P = Text; *P; ++P)
    {
        if (*P == '\\') Out += "\\\\";
        else if (*P == '"') Out += "\\\"";
        else Out += *P;
    }
    Out += '"';
    return Out;
}

bool IsWholeKey(const char* Key) noexcept
{
    return std::strcmp(Key, "cylinder_count") == 0 || std::strcmp(Key, "firing_order") == 0 || std::strcmp(Key, "bank_of_cylinder") == 0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   FIELD POINTERS
//------------------------------------------------------------------------------------------------------------------------

template<typename T> T*       FieldAt(AcousticStructure& S, const AcousticFieldNote& F) noexcept       { return reinterpret_cast<T*>(reinterpret_cast<char*>(&S) + F.Offset); }
template<typename T> const T* FieldAt(const AcousticStructure& S, const AcousticFieldNote& F) noexcept { return reinterpret_cast<const T*>(reinterpret_cast<const char*>(&S) + F.Offset); }

void AssignText(char* Dst, std::string_view Src) noexcept
{
    size_t N = std::min(Src.size(), size_t(AcousticTextCapacity - 1u));
    while (N > 0u && (static_cast<unsigned char>(Src[N]) & 0xC0u) == 0x80u && N < Src.size()) --N;   // never cut inside a code point
    std::memcpy(Dst, Src.data(), N);
    Dst[N] = '\0';
}

// The editor's coercions: Number(val) for reals (booleans → 0 / 1, numeric strings parse), !!val for toggles.
bool LeafAsReal(const toml::node& Leaf, double& Out) noexcept
{
    if (auto V = Leaf.value<double>()) { Out = *V; return std::isfinite(Out); }
    if (auto B = Leaf.value<bool>())   { Out = *B ? 1.0 : 0.0; return true; }
    if (auto S = Leaf.value<std::string_view>())
    {
        std::string Text(*S);
        char* End = nullptr;
        const double V = std::strtod(Text.c_str(), &End);
        if (End != Text.c_str() && *End == '\0' && std::isfinite(V)) { Out = V; return true; }
    }
    return false;
}

bool LeafAsToggle(const toml::node& Leaf) noexcept
{
    if (auto B = Leaf.value<bool>())   return *B;
    if (auto V = Leaf.value<double>()) return *V != 0.0;
    if (auto S = Leaf.value<std::string_view>()) return !S->empty();
    return true;   // an array or a table is truthy in JavaScript
}

const toml::table* SectionOf(const toml::table& Root, const char* Section) noexcept
{
    // dotted sections walk nested tables (none in the current schema, kept for parity with structurePath)
    const toml::table* Cur = &Root;
    std::string_view Rest(Section);
    while (!Rest.empty())
    {
        const size_t Dot = Rest.find('.');
        const std::string_view Part = Rest.substr(0, Dot);
        const toml::node* Next = Cur->get(Part);
        if (!Next || !Next->is_table()) return nullptr;
        Cur = Next->as_table();
        Rest = Dot == std::string_view::npos ? std::string_view() : Rest.substr(Dot + 1u);
    }
    return Cur;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                   SHEET ACCESS
//------------------------------------------------------------------------------------------------------------------------

uint32_t AcousticStructure::FindField(std::string_view Section, std::string_view Key) noexcept
{
    for (uint32_t I = 0u; I < AcousticFieldCount; ++I)
        if (Section == FieldSheet[I].Section && Key == FieldSheet[I].Key) return I;
    return AcousticFieldCount;
}

double AcousticStructure::QueryReal(uint32_t Field) const noexcept
{
    if (Field >= AcousticFieldCount) return 0.0;
    const AcousticFieldNote& F = FieldSheet[Field];
    switch (F.Category)
    {
        case AcousticFieldCategory::Real:   return *FieldAt<double>(*this, F);
        case AcousticFieldCategory::Whole:  return double(*FieldAt<int32_t>(*this, F));
        case AcousticFieldCategory::Toggle: return *FieldAt<bool>(*this, F) ? 1.0 : 0.0;
        default:                            return 0.0;
    }
}

void AcousticStructure::AssignReal(uint32_t Field, double Value) noexcept
{
    if (Field >= AcousticFieldCount) return;
    const AcousticFieldNote& F = FieldSheet[Field];
    switch (F.Category)
    {
        case AcousticFieldCategory::Real:   *FieldAt<double>(*this, F)  = Value; break;
        case AcousticFieldCategory::Whole:  *FieldAt<int32_t>(*this, F) = int32_t(RoundHalfUpJs(Value)); break;
        case AcousticFieldCategory::Toggle: *FieldAt<bool>(*this, F)    = Value != 0.0; break;
        default: break;
    }
}

const char* AcousticStructure::QueryText(uint32_t Field) const noexcept
{
    if (Field >= AcousticFieldCount || FieldSheet[Field].Category != AcousticFieldCategory::Text) return "";
    return FieldAt<char>(*this, FieldSheet[Field]);
}

const AcousticRealList* AcousticStructure::QueryList(uint32_t Field) const noexcept
{
    if (Field >= AcousticFieldCount || FieldSheet[Field].Category != AcousticFieldCategory::RealList) return nullptr;
    return FieldAt<AcousticRealList>(*this, FieldSheet[Field]);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   DESERIALISE
//------------------------------------------------------------------------------------------------------------------------

bool AcousticStructure::Deserialise(std::string_view Toml, AcousticStructure& Out, std::string* Error, uint32_t* Unknown) noexcept
{
    toml::table Root;
    try { Root = toml::parse(Toml); }
    catch (const toml::parse_error& E) { if (Error) *Error = E.description(); return false; }
    catch (...) { if (Error) *Error = "TOML parse failed"; return false; }

    Out = AcousticStructure{};   // defaults from the member initialisers (== the sheet's defaults; the test proves it)
    for (uint32_t I = 0u; I < AcousticFieldCount; ++I)
    {
        const AcousticFieldNote& F = FieldSheet[I];
        const toml::table* Section = SectionOf(Root, F.Section);
        if (!Section) continue;
        const toml::node* Leaf = Section->get(F.Key);
        if (!Leaf) continue;
        switch (F.Category)
        {
            case AcousticFieldCategory::Real:
            {
                double V; if (LeafAsReal(*Leaf, V)) *FieldAt<double>(Out, F) = V;
                break;
            }
            case AcousticFieldCategory::Whole:
            {
                double V; if (LeafAsReal(*Leaf, V)) *FieldAt<int32_t>(Out, F) = int32_t(RoundHalfUpJs(V));
                break;
            }
            case AcousticFieldCategory::Toggle:
                *FieldAt<bool>(Out, F) = LeafAsToggle(*Leaf);
                break;
            case AcousticFieldCategory::Text:
            {
                if (auto S = Leaf->value<std::string_view>()) AssignText(FieldAt<char>(Out, F), *S);
                else
                {
                    std::ostringstream Stream; Stream << toml::node_view<const toml::node>(*Leaf);
                    AssignText(FieldAt<char>(Out, F), Stream.str());
                }
                break;
            }
            case AcousticFieldCategory::RealList:
            {
                const toml::array* List = Leaf->as_array();
                if (!List) break;   // the editor keeps the default when the value is not a list
                AcousticRealList& Dst = *FieldAt<AcousticRealList>(Out, F);
                Dst = AcousticRealList{};
                for (const toml::node& E : *List)
                {
                    if (Dst.Count >= AcousticRealListCapacity) break;
                    double V = 0.0; LeafAsReal(E, V);
                    Dst.Entry[Dst.Count++] = V;
                }
                break;
            }
        }
    }

    if (Unknown)
    {
        uint32_t Count = 0u;
        for (auto&& [Key, Leaf] : Root)
        {
            const std::string SectionName(Key.str());
            if (!Leaf.is_table()) { ++Count; continue; }
            bool Known = false;
            for (uint32_t I = 0u; I < AcousticFieldCount && !Known; ++I) Known = SectionName == FieldSheet[I].Section;
            if (!Known) { ++Count; continue; }
            for (auto&& [Field, Ignored] : *Leaf.as_table())
            {
                (void)Ignored;
                if (FindField(SectionName, Field.str()) == AcousticFieldCount) ++Count;
            }
        }
        *Unknown = Count;
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   SERIALISE
//------------------------------------------------------------------------------------------------------------------------

std::string AcousticStructure::Serialise(const AcousticStructure& S, const char* const* HeaderLines, uint32_t HeaderCount) noexcept
{
    std::string Out;
    for (uint32_t H = 0u; H < HeaderCount; ++H)
    {
        const char* Line = HeaderLines[H];
        if (Line && *Line) { Out += "# "; Out += Line; }
        else Out += '#';
        Out += '\n';
    }
    const char* Current = nullptr;
    for (uint32_t I = 0u; I < AcousticFieldCount; ++I)
    {
        const AcousticFieldNote& F = FieldSheet[I];
        if (!Current || std::strcmp(Current, F.Section) != 0)
        {
            Current = F.Section;
            Out += "\n[";
            Out += Current;
            Out += "]\n";
        }
        const bool Whole = IsWholeKey(F.Key);
        std::string Literal;
        switch (F.Category)
        {
            case AcousticFieldCategory::Real:   Literal = TomlNumber(*FieldAt<double>(S, F), Whole); break;
            case AcousticFieldCategory::Whole:  Literal = TomlNumber(double(*FieldAt<int32_t>(S, F)), Whole); break;
            case AcousticFieldCategory::Toggle: Literal = *FieldAt<bool>(S, F) ? "true" : "false"; break;
            case AcousticFieldCategory::Text:   Literal = QuoteText(FieldAt<char>(S, F)); break;
            case AcousticFieldCategory::RealList:
            {
                const AcousticRealList& L = *FieldAt<AcousticRealList>(S, F);
                Literal = "[";
                for (uint32_t E = 0u; E < L.Count; ++E) { if (E) Literal += ", "; Literal += TomlNumber(L.Entry[E], Whole); }
                Literal += "]";
                break;
            }
        }
        std::string Line(F.Key);
        PadEnd(Line, 22u);
        Line += " = ";
        Line += Literal;
        std::string Note;
        if (F.Unit && *F.Unit && std::strcmp(F.Unit, "-") != 0) { Note = "["; Note += F.Unit; Note += "]"; }
        if (F.Note && *F.Note) { if (!Note.empty()) Note += ' '; Note += F.Note; }
        if (!Note.empty())
        {
            Line += ' ';
            PadEnd(Line, 56u);
            Line += "# ";
            Line += Note;
        }
        Out += Line;
        Out += '\n';
    }
    return Out;   // the editor's join('\n') after a final empty entry = one trailing newline, already written above
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   FILES
//------------------------------------------------------------------------------------------------------------------------

bool AcousticStructure::Load(std::string_view Path, AcousticStructure& Out, std::string* Error, uint32_t* Unknown) noexcept
{
    std::ifstream File(std::string(Path), std::ios::binary);
    if (!File) { if (Error) *Error = "cannot open " + std::string(Path); return false; }
    std::ostringstream Stream; Stream << File.rdbuf();
    return Deserialise(Stream.str(), Out, Error, Unknown);
}

bool AcousticStructure::Save(std::string_view Path, const AcousticStructure& S, const char* const* HeaderLines, uint32_t HeaderCount, std::string* Error) noexcept
{
    std::ofstream File(std::string(Path), std::ios::binary | std::ios::trunc);
    if (!File) { if (Error) *Error = "cannot write " + std::string(Path); return false; }
    const std::string Text = Serialise(S, HeaderLines, HeaderCount);
    File.write(Text.data(), std::streamsize(Text.size()));
    return bool(File);
}

} // namespace Frontier
