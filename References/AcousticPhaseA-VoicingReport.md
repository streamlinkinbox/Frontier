# Acoustic Phase A — Voicing report: rev 2 against the real cars, and how the LaFerrari gets fixed

Report only — no code in this row. Companion to `AcousticPhaseA-Plan.md` (§2a documents the rev-2 voice this report measures).

## 0. Summary

* Rev 2 matches the RevSim reference you gave me — that was its target. The **real cars** are a different target, and against
  their known geometry the gaps are large, specific and measurable. They are the *same* gaps for all three cars; the LaFerrari
  simply suffers most because it is the fastest-firing, smoothest engine of the three, so every generic-synth habit shows.
* The headline for the LaFerrari: the real engine's note is its firing rate — **100 Hz per 1000 rpm, 900 Hz at 9000 rpm**. Rev 2's
  loudest line at 9000 rpm sits at **75 Hz (order ½)** with the firing order 33 dB down — 3.6 octaves too low. At idle and 3000 rpm
  it is an octave to an octave-and-a-half low. That is the whole "sounds like a V8, not a LaFerrari" problem in one number.
* Why every AI attempt fails on this car (§4): five structural habits — fixed-pitch voice, time-constant pulse decay, timing walk
  as the "life" ingredient, parity/left-right panning, exhaust-only voicing — each kills exactly the property that defines a V12.
* The fix (§5) is not more tuning by ear. It is (a) six physical corrections to the voice that remove the impossible content and
  restore the missing content, and (b) a measurement loop (the plan's A4 reference lane, pulled forward) that overlays a real
  recording's order diagram on the synth's and reports the per-order distance, so tuning becomes reading a meter.
* I could **not** measure against recordings in this sandbox (YouTube / archive.org are unreachable). §8 lists exactly which
  recordings to give me and how; then §3 gets a second column with the measured numbers.

## 1. What was measured, and how

| Item | Method |
|---|---|
| Rev-2 synth | the shipped `Tools/AudioEditor/index.html` DSP, rendered in node; 2 s steady at idle / 3000 / 6000 / redline × load 0 / 0.3 / 1, left channel, 32768-point Hann FFT; per-order level = loudest bin within ±2 bins of `order × rpm / 60`; pure mode for the order-structure checks in the proof, full chain for the tables below |
| Real cars | published geometry (bank angle, crank, firing order, exhaust/intake layout) → expected order structure by first principles (§2); marked **expected** — the reference lane confirms or corrects each |
| Not yet | real recordings — sandbox network blocks video hosts; see §8 |

## 2. What defines each real engine's sound (from geometry)

The one law behind all of it: a four-stroke engine fires `N/2` times per revolution, so its **note is order N/2**:

| Car | Engine | Firing note (order N/2) | Per-bank train | Bank routing to the outside | Extras that are part of the signature |
|---|---|---|---|---|---|
| **LaFerrari** | F140FE 6.3 L **65°** V12, 9250 rpm, six straight crank throws | **order 6 → 100 Hz per 1000 rpm** (100 Hz idle, 300 Hz @ 3000, 900 Hz @ 9000) | inline-6 per bank: perfectly even 120° → orders 3, 6, 9 … | 6-into-1 equal-length hydroformed headers per bank; bank merge point = the order-3 level heard outside (to read from the recording) | 65° V with shared pins ⇒ firing intervals **alternate 55° / 65°** ([Hagerty on the F140][hag]) → adds orders 9, 15, 21 between the main harmonics (≈ −8 / −4 / −2 dB re order 6) and a weak order 3 (≈ −17 dB) — the "denser than a 60° V12" silk; **variable-length intake**: length halves at 6000 rpm, grows again, minimum above 8000 rpm ([R&T][rt]) → the intake formant *steps* at 6000 and 8000 rpm; intake dominates the cockpit and the last 2000 rpm outside; valved exhaust; HY-KERS motor whine at parking speeds; busy mechanical top end at idle |
| **918 Spyder** | 4.6 L flat-plane 90° V8, 9150 rpm | **order 4 → 66.7 Hz per 1000 rpm** (580 Hz @ 8700) | inline-4 per bank: even 180° → orders 2, 4, 6, 8 | **top pipes, one per bank, left and right** → each ear/channel hears an inline-4 (strong order 2 = the flat-plane rasp); merged content would be orders 4, 8, 12 only | exhaust path ≈ 1 m → almost no low-pass, race-car bark; heavy overrun crackle; valves closed = the RevSim 300 Hz high-pass mode, Sport/Race = open (which one you want decides the low end); front + crank e-motors, inverter whine on E-drive and decel |
| **GT-R Nismo** | VR38DETT 3.8 L 60° V6, split-pin crank (even 120°), twin IHI turbos, 7100 rpm | **order 3 → 50 Hz per 1000 rpm** (150 Hz @ 3000, 350 Hz @ 7000) | inline-3 per bank: even 240° → orders 1.5, 3, 4.5 | one turbine per bank → pulses arrive at the outlets **attenuated and rounded** (turbine ≈ strong low-pass + 10–20 dB loss) → deep smooth drone, weak high harmonics | the voice is mostly **induction**: compressor shaft tone (≈ 1.5–2.5 kHz at 100–150 krpm) + broadband rush + recirculating blow-off "pssh" (faint on a stock car — **no anti-lag on a stock Nismo**); GR6 transaxle whine in-car; titanium exhaust louder but still turbo-smooth |

Order content that **cannot** come from combustion in any of them: order 1 (crank speed — that is mechanical/unbalance), and for
the V12 and flat-plane V8 anything at half orders (½, 1½, 2½ …) beyond a few percent from cylinder-to-cylinder variance.

[hag]: https://www.hagerty.com/media/automotive-history/epic-engines-v-12-ferrari-heart-soul/
[rt]: https://autos.yahoo.com/laferraris-variable-length-intake-manifold-173555247.html

## 3. Rev 2 measured against §2 — how much they differ

Left channel, full chain, load 1 unless stated. "Loudest" = the order carrying the most energy; "note" = the firing order N/2.

| Car · point | Real (expected) | Rev 2 measured | Gap |
|---|---|---|---|
| LaFerrari idle 1000 | note order 6 = 100 Hz, smooth + mechanical hash | loudest order **3.5** (58 Hz) −7.5 dB; order 6 at −14 dB | note ≈ 0.8 octave low, lumpy V8 idle |
| LaFerrari 3000 | order 6 = 300 Hz, order 12 next, order 3 ≈ −17 dB | loudest order **2.5** (125 Hz) −6.7 dB; order 6 −9.6 dB; orders 1.5 / 0.5 at −14 / −19 dB | note 1.3 octaves low; half-orders that do not exist carry the mix |
| LaFerrari 6000 (pure) | order 6 = 600 Hz | loudest order **3.5**; order 6 9 dB below it | 0.8 octave low |
| LaFerrari 9000 | order 6 = **900 Hz scream**, 12 / 18 above it, 9 / 15 / 21 in between | loudest order **½ (75 Hz)** −8.9 dB, 2.5 at −7.3 dB; **order 6 at −33 dB** | **3.6 octaves low**; the scream is absent, replaced by rumble |
| 918 3000 | order 4 = 200 Hz, order 2 per side | loudest order 8 (400 Hz); order 4 −10 dB below (RevSim valve high-pass) | one octave high — the closed-valve mode |
| 918 6000 | order 4 = 400 Hz | loudest order 4 ✓; orders 1.5 / 2.5 at −9 … −13 dB | note right, impossible half-orders under it |
| 918 8700 | order 4 = 580 Hz, 8 = 1160 Hz | loudest orders **1.5 / 3** (−10.5 / −10.7 dB); order 4 at −24 dB | note 1.4 octaves low at the top |
| GT-R 3000 | order 3 = 150 Hz, 1.5 per side | loudest **order 1** (50 Hz) −5.5 dB; order 3 at −16.7 dB | order 1 cannot come from combustion; note 1.6 octaves low |
| GT-R 6000–7000 | order 3 | order 3 loudest ✓ (−5 … −16 dB), 1.5 present ✓ | closest of the three |

Structural gaps shared by all three (these are what §5 fixes):

| Property | Real | Rev 2 | Consequence |
|---|---|---|---|
| Pulse duration | blowdown ≈ 40–60° crank, rise ≈ 10–20° → **∝ 1/rpm** (V12 @ 9000: ≈ 1 ms pulse, ≈ 1.5 overlapping) | `exp(−t·(15 + 3·rpm/1000))`: 24–66 ms in *seconds* → V12 @ 9000: **≈ 21 overlapping voices**, @ 3000: 12; V8 @ 8700: 14; V6 @ 7000: 10 | duty cycle 8–14× too long → the harmonic series smears into a fixed-pitch hum; brightness has to be faked with noise / ring layers = the "digital fizz" |
| Voice pitch | the firing rate (order N/2) | `pitch_hz` 58 / 65 / 48 Hz + 15 Hz rise — **fixed**; 58 Hz is a V12 at 580 rpm | perceived pitch sticks near 60–80 Hz and its harmonics at every rpm; all three cars sound related |
| Firing timing | cam-locked to the crank within ≈ ±0.5° (torsional twist); what varies cycle-to-cycle is **combustion pressure** (COV ≈ 2–8 %) | ±5 % **timing walk** per firing, cumulative (RevSim), clamped at ±30 % of an interval | phase noise scales with order → order 6 decorrelates (a 0.05 rev walk is 108° at order 6) → the high orders that define the V12 turn into a hump; sub-orders survive → rumble |
| Stereo | left bank → left, right bank → right (each side an even inline-6 / -4 / -3) | RevSim **cylinder-parity pan** (odd → L, even → R): each channel gets an irregular 6-of-12 pattern with a 720° period | the half-orders (½ … 3½) are *generated* here; on a V12 they are the strongest content |
| Intake | order-N/2 valve-event train through a runner/plenum resonance (LaFerrari: steps at 6000 / 8000 rpm); dominates the cockpit | `howl`: a level above `howl_rpm` on the same pulse sheet | no separate intake voice, no formant steps, cockpit = exhaust |
| Exhaust colour | fixed pipe resonances (collector, silencer, tailpipe quarter-wave — the "thump" near 60–90 Hz on a ≈ 1 m hot tailpipe) | one comb at 2.667 ms (375 Hz spacing — a WebAudio 128-frame artefact) + `tanh` + clip | the comb is not the car's; the clip's intermodulation of a dense series lands on sub-orders → more rumble |

Scorecard (✓ right · ◐ partly · ✗ wrong) — this is the honest answer to "how much do they differ":

| Trait | LaFerrari | 918 | GT-R |
|---|---|---|---|
| Note follows the firing rate at all rpm | ✗ | ◐ (right 3000–6000, wrong at redline and in valve-closed low end) | ◐ (right above 5000, order 1 at 3000) |
| Harmonic series correct (density, no impossible orders) | ✗ | ✗ | ◐ |
| Per-bank stereo image | ✗ | ✗ | ◐ |
| Pulse brightness law (∝ 1/rpm) | ✗ | ✗ | ✗ (matters least — the turbines round the pulses anyway) |
| Intake voice + rpm-dependent formant steps | ◐ | ◐ | ✓ (turbo layer present) / ◐ (BOV, no anti-lag on stock) |
| Exhaust formants of the actual car | ◐ | ◐ | ◐ |
| Overrun / transients calibrated | ◐ (probably too many pops for a LaFerrari) | ✓ (crackle-heavy is right) | ◐ |
| Extras (hybrid / gearbox whine) | ◐ | ◐ | ✗ (transaxle whine missing) |
| **Right today** | **≈ 1 of 8** | **≈ 2 of 8** | **≈ 3 of 8** |

## 4. Why the LaFerrari defeats every generic synth (including rev 2)

Each habit below is harmless-ish on a lazy V8 and fatal on this engine:

1. **"Hypercar ⇒ deep and angry" prior.** Models voice by category: low pitch, growl, rasp. The F140 is the opposite — the
   highest-pitched, smoothest, densest series of the three; its aggression is *brightness and rate*, not weight. Rev 2 carries
   its `pitch_hz` at 58 Hz — 3.6 octaves below the real note at redline.
2. **Fixed-pitch, time-constant voices.** A 24 ms wavelet retriggered every 1.1 ms cannot produce a 900 Hz note; it produces a
   60–80 Hz hum amplitude-modulated at 900 Hz. The V12 is where firing rates are highest, so the smear is worst here.
3. **Timing walk as "life".** Real engines vary *pressure* per cycle, not valve timing. A timing walk's phase noise grows with
   order — it erases order 6/12/18 first, i.e. the LaFerrari first. The V12 has the least natural sub-order content to hide
   behind, so what remains reads immediately as "V8".
4. **Parity panning.** Odd/even cylinder panning manufactures half-orders in each channel. On an even-firing V12 that is the
   loudest content rev 2 produces (orders ½, 1½, 2½, 3½). A crossplane V8 *has* those orders; a V12 does not.
5. **Exhaust-only voicing.** From 6000 rpm up, and in the cockpit at all times, a LaFerrari is largely an intake sound with
   two audible formant steps (6000 / 8000 rpm). No intake voice ⇒ no scream, and no "gear change" in the timbre on the way up.
6. **Wrong crank.** A generic V12 is voiced as 12 × 60°. The F140 fires 55/65; the extra orders 9, 15, 21 are half of what
   makes the series sound "silky-dense" rather than "organ-clean". Cheap to add — we already run arbitrary event angles.

None of this is model intelligence; it is that nobody measured. With the loop in §6 the LaFerrari becomes an engineering job.

## 5. The fix — rev 3 voice for the LaFerrari (order of audible effect)

Keep from rev 2 what your ear accepted as "not digital": `tanh` saturation (lower drive), a load-gated noise floor (crackle),
the transient slots, the clip as a safety, cycle-to-cycle variance, the dyno / free-rev feel. Change the structure:

| # | Change | Why | Expected audible effect | Verified by |
|---|---|---|---|---|
| a | **Bank-true routing** — bank 0 → left, bank 1 → right (Ferrari numbering 1–6 / 7–12; order 1-12-5-8-3-10-6-7-2-11-4-9 gives perfect alternation) — ⚠️ reverses the rev-2 parity map I adopted for RevSim-likeness | removes manufactured half-orders; each channel becomes an even inline-6 | rumble gone; the note snaps to order 6 (or 3 per side outside) | orders ½ … 3½ below −30 dB re order 6 in pure mode |
| b | **Crank-true angles** — event angles from the 65° bank / straight-pin crank: intervals 65, 55, 65, 55 … (one TOML field, `bank_angle_deg`, the crank clock does the rest) | the F140's actual pulse timing | denser series: orders 9, 15, 21 appear at ≈ −8 / −4 / −2 dB re 6; order 3 at ≈ −17 dB | order diagram shows the 3k lines at those levels |
| c | **Pulse in crank degrees** — blowdown kernel defined over θ (rise ≈ 15°, blowdown ≈ 50°, displacement tail to ≈ 200°), converted to seconds by `1/(6·rpm)` | duration ∝ 1/rpm, duty cycle constant → "same timbre, higher pitch", bright at the top without noise | the 7000–9250 rpm scream; harmonic content to 5–8 kHz from the pulse itself; idle stays clean | spectral centroid rises linearly with rpm; overlap ≈ 1.5 voices at all rpm |
| d | **Variance = pressure, not timing** — per-firing amplitude/shape COV 2–8 % (more at idle, less at WOT), timing jitter ≤ ±0.3°, no cumulative walk | physics of cycle-to-cycle variation | "life" without smearing; order 6/12/18 stay as lines | line width of order 6 < 2 Hz at steady rpm |
| e | **Intake voice** — second event train (intake valve opening, order 6) through a runner + plenum resonator whose tuned frequency **steps at 6000 and 8000 rpm** (variable trumpets), inverted softer pulses, level ∝ throttle; cockpit preset intake-heavy, trackside exhaust-heavy | the missing half of the car | the wail from 6000 up and the two timbre steps; cockpit finally sounds like a LaFerrari cockpit | two rpm-locked formant discontinuities in the spectrogram at 6000 / 8000 |
| f | **Exhaust formants of the car** — 2–3 resonators (6-into-1 collector, silencer, tailpipe quarter-wave — the physical version of RevSim's `basePitch` thump) read from the recording's rpm-independent ridges; replaces the 2.667 ms comb | colour that belongs to this pipe | the "Ferrari" ring instead of a generic comb | ridge frequencies within 5 % of the recording |
| g | **Valved exhaust step** — bypass valves open on load / rpm (threshold from the recording) | loudness + brightness step on the way up | the "opens up" moment | level / centroid step at the same rpm as the recording |
| h | **Overrun calibrated** — LaFerrari pops sparingly; `backfire_probability`, `pop_rate_hz` from the lift in the recording | rev 2 inherits RevSim's 20 % backfire / 18 Hz pops | cleaner lift, occasional crackle when hot | pop count per lift within ±50 % of the recording |
| i | **Idle mechanics** — crank-locked valvetrain / chain hash (`clatter`) + HY-KERS whine at parking speeds | the busy Ferrari idle | idle reads "Ferrari V12" before the throttle is touched | idle spectrum 2–6 kHz hash within 6 dB of the recording |

Items a–d are one change to the shared voice (they fix the 918 and GT-R too); e–i are per-car tuning against the recording.

## 6. The measurement loop (A4 reference lane, pulled forward)

Inputs: a recording (WAV) dropped on the editor. Extracted automatically:

1. **rpm trajectory** from the strongest harmonic track (order N/2 × its harmonics) → also becomes a dyno script, so the synth
   is rendered along the *same* rpm/time path.
2. **Order profile vs rpm**: orders ½ … 24 in 250 rpm bins — the recording's order diagram over the synth's, plus a per-order
   difference sheet.
3. **Formants**: rpm-independent ridges in the spectrogram → resonator frequencies / bandwidths for (f).
4. **Harmonic density and brightness law**: number of lines per kHz and spectral centroid vs rpm → checks (b) and (c).
5. **Level dynamics**: steps in loudness / centroid vs rpm → valve thresholds (g), intake steps (e).
6. **Transients**: pop count and spectrum on lift → (h).
7. **Noise floor** vs load → crackle level.

Acceptance metric per car: mean |Δ| ≤ 3 dB over orders 3 … 24 and 2000 rpm … redline (orders below 3 ≤ 6 dB, since room and
mic dominate there), formant frequencies within 5 %, loudest-order track identical, pops per lift within ±50 %. Then the listen.

Limits: drive-by recordings are useless (Doppler bends every order); phone mics lose < 80 Hz and > 12 kHz (fine — orders 3–24
survive); a static rev or a chassis-dyno pull is ideal; onboard clips give the cockpit balance.

## 7. The other two cars, same treatment

* **918** — a–d as above; restore the real map (1-5-2-6-4-8-3-7, banks 1–4 / 5–8) so each top pipe is an inline-4 — that *is*
  the flat-plane rasp; decide valve mode (Sport/Race open ⇒ drop the 300 Hz high-pass), exhaust path ≈ 1 m ⇒ very little
  low-pass, crackle stays heavy; e-motor / inverter whine from the recording (E-drive and decel).
* **GT-R Nismo** — a–d; restore 1-2-3-4-5-6 with alternating banks; put a **turbine** in the exhaust path (pulse attenuation +
  low-pass per bank — the one place a smoother, longer pulse is *right*); turbo shaft tone from a shaft-speed integrator (100–150
  krpm) rather than a fixed sine; recirculating BOV "pssh" instead of anti-lag (anti-lag off unless you want the tuned-car
  sound); GR6 transaxle whine (order ∝ output shaft) for the cockpit.

## 8. What I need from you

Recordings — WAV (or MP3; I add a decoder) at 44.1/48 kHz, ≥ 20 s each, per car if you can:

1. **Static exterior**: idle 10 s → slow rev to redline → hold → lift (near the exhaust, no Doppler).
2. **Onboard**: a full-throttle pull through two gears with a lift (cockpit balance, valve/intake steps, gearbox whine).
3. Optional: **chassis-dyno** pull (steady load, no Doppler) — the best order data there is.
4. Tell me the mode (918 Sport/Race or Normal; GT-R stock or Nismo titanium; LaFerrari normal).

How to get them to me: my sandbox reaches github.com and the npm registry but not video hosts, so commit them to Slate under
`EngineContent/AudioArchives/<Car>/Reference/` (a 30 s 48 kHz mono 16-bit WAV is ≈ 3 MB; MP3 ≈ 0.5 MB) — I fetch from the
`slate` remote. Any direct HTTPS link to the file also works.

## 9. Proposed row order (your call)

1. **A4 reference lane + rev-3 core (a–d)** in the HTML — one row, all three cars benefit immediately, and you get the overlay
   view that shows the difference as numbers.
2. **LaFerrari rev 3 (e–i)** against your recording — your pain point first (this is A6 pulled forward).
3. 918, then GT-R rows against their recordings.
4. **A2** — the C++ port of rev 3 (porting rev 2 now would mean porting twice).

If you approve, row 1 starts on your word; no code until then.
