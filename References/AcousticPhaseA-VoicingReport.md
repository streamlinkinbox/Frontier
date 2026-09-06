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
* I could **not** measure against proper recordings in this sandbox (YouTube / archive.org are unreachable). §8 lists exactly which
  recordings to give me and how; then §3 gets a second column with the measured numbers.
* **§10 (added after the search you asked for)**: no published LaFerrari spectrogram or waveform graph is reachable — what exists
  is Ferrari's own prose and generic method pages. The one real LaFerrari-labelled audio reachable is a 2.5 s start clip plus
  two 166 ms loops in an open RC-sound project. Measured, they confirm the §2 structure (one integer-order ladder, order 6 on top,
  no half-orders) and add numbers rev 2 misses badly: heard from one side, rev 2's loudest idle line is order 3½ — a line the
  real car does not have at all — and the real idle is far darker (centroid ≈ 110–180 Hz vs 400 Hz) and sparser (10 lines vs
  25–28 within 26 dB below 1 kHz).

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

**Status (row A1¾, `Tools/AudioEditor/index.html` rev 3):** a–g and i are implemented behind `voice.crank_pulse` and switched on
for the LaFerrari only (the 918 / GT-R renders stay bit-identical to rev 2); h is left at sparse defaults. Against the §10.4
sheet the LaFerrari idle now measures (left channel, 1081 rpm): loudest order 6; structural half-orders ≤ −70 dB (rev 2: 3½ at
0 dB); order 3 −27 (rev 2 −16 … −18, real −35); orders 18 / 24 −38 / −58 (real −40 / −56); centroid 129 Hz (real 107–179);
nothing within 37 dB above 300 Hz; 6 lines within 26 dB below 1 kHz (real 10). Mean |Δ| over integer orders 1–14 vs the real
rev loop: 9.3 dB (rev 2: 7.5 on the left channel — the remaining distance is orders 1 / 8 / 12, which the loop carries at
−27 / −15 / −32 and rev 3 at −16 / −23 / −18; the loop's 8-bit floor and loop-splice sidebands set ≈ 5 dB of that). What the
idle clips cannot tell (§10.5) — everything above idle — is tuned to the geometry alone and waits for the §8 recordings.
Plan `AcousticPhaseA-Plan.md` §2b carries the field-by-field sheet.

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

## 10. Real-data check — the LaFerrari audio that is actually reachable

You said published LaFerrari spectra / wave graphs exist online. I searched for them specifically (web search, image search,
Ferrari's own site, GitHub code search, the acoustics pages that do publish car spectrograms). Result, honestly:

| Looked for | Found | Usable? |
|---|---|---|
| Published spectrogram / waterfall / order plot of a LaFerrari | none reachable. Image hits titled "LaFerrari spectrogram" are press photos and F1 thumbnails | no |
| Ferrari's own acoustic statements (812 GTS / Competizione, 12Cilindri — same F140 family, same 6-into-1 recipe) | prose only: "predominance of the first-order combustion harmonics", "high and low frequencies from the intake and exhaust respectively", resonators tuned to "the noble combustion orders" | confirms §2 qualitatively (note = combustion order, intake = the high half, exhaust = the low half); no numbers |
| YMEC (Yoshimasa Electronic) — the site that does publish car spectrograms with a worked method | F40 0–200 km/h page (fundamental 59 Hz + harmonics, running-ACF method); no LaFerrari; the WAV is unreachable | method only — it is the A4 lane's reader (§6) |
| Loudness figures | Car and Driver 812 Superfast: 66 / 91 / 75 dBA interior at idle / WOT / 70 mph | family-level targets only |
| Audio files (`laferrari` + wav/mp3/ogg on GitHub) | zero hits; one C-array sample set in an open RC engine-sound project (`TheDIYGuy999/Rc_Engine_Sound_ESP32`, `src/vehicles/sounds/LaFerrari{Start,Idle,Rev,Knock}.h`) | **yes, with caveats — measured below** |

### 10.1 What the sample set is

22 050 Hz, 8-bit PCM, mono, in C headers. No licence in that repository, so the clips are **not** committed here:
`node Scratchpad/AcousticLaFerrariReference.js --fetch` pulls them into the git-ignored `Scratchpad/LaFerrariReference/`.
Provenance from the project's own commit log and README:

| Clip | Label in the project | Length | What it is | Trust |
|---|---|---|---|---|
| `LaFerrariStart.h` | "Ferrari LaFerrari, V12" | 2.53 s | cranking → catch → flare → settle; a straight recording, not looped, not rate-shifted | **best** — the targets below lean on it |
| `LaFerrariRev.h` | "Ferrari LaFerrari, V12" | 166.1 ms loop | a running-engine loop; the README's cutting rule **rate-shifts** the rev sample until it is as long as the idle loop, so its pitch is only as real as the idle loop's | shape + relative levels |
| `LaFerrariIdle.h` | **"Jaguar XJS V12"** in the vehicle profile since v5.4 (2020-09), the same commit that replaced the file's content (the v2.6 file, labelled LaFerrari, was a different 2094-sample clip) | 166.5 ms loop | most likely a Jaguar V12 idle — a 60° V12 with the same firing rate. (A fixed ≈ 180 Hz line shows up in both this loop and the start clip: either the same car after all, or a shared 3 × 60 Hz mains harmonic from the video — undecidable at 8 bits) | V12-idle **shape control**, not LaFerrari evidence |
| `LaFerrariKnock.h` | — | 1.7 ms | the per-firing click the firmware layers on top (the same event-synth idea as rev 2) | — |

No clip carries a tachometer, so the crank speed is read out of the audio and **two readings are reported everywhere**:
**R1** = the project's cutting rule (one loop = one 720° cycle = 12 firings → 721 rpm); **R2** = the engine reading (the shortest
lag at which the loop repeats with r ≥ 0.8 is one firing or one revolution; the interpretation whose crank series carries ≥ 90 %
of the line energy wins). R2 lands on **1081 rpm** from the idle loop (its 55.5 ms period = one revolution at 18.0 Hz) and **1084 rpm**
from the rev loop (its 9.07 ms period = one firing of a V12 at 18.1 Hz) — two independent routes to the same number, and a
plausible warm-ish idle; R1's 721 rpm makes the loudest lines orders 9 and 15, which no V12 does. **R2 is the reading I trust**;
R1 is kept so the "loop = 720°" assumption is visible. The start clip needs neither.

### 10.2 What the clips measure (`Scratchpad/AcousticLaFerrariReference.log`; image `Scratchpad/AcousticLaFerrari_RealVsRev2.png`)

**Start clip** (LaFerrari-labelled, no loop, no rate shift): cranking 0–0.5 s (compression thuds 26–37 Hz, 50 Hz hum, starter
whine 1–2 kHz); catch at ≈ 0.5 s at 108–136 Hz; **flare to 145–155 Hz at 0.9–1.3 s**; then a smooth glide **141 → 117 Hz from
1.5 to 2.3 s** (≈ −300 rpm/s read as order 6), still falling toward the loops' 108 Hz when the clip ends. Companions that glide
with it sit at 1/2 × (−16 dB at the flare), 3/2 × (−2 … −12 dB, 0.9–1.2 s only) and 2 × (−10 dB) — **orders 3, 9, 12 of an
order-6 line: one integer ladder, no half-orders**. Read as order 3 (the per-bank note) the flare would be 2900 → 2370 rpm, which
no warm start does — so order 6 is the loudest line of the real car, as §2 predicted. Two lines **stay put while the pitch
glides**: ≈ 172–188 Hz and ≈ 207–223 Hz — fixed ridges, i.e. pipe/body resonances, not orders. From 1.5 s on the sound is
essentially one line with everything else ≥ 19 dB down; **nothing within 25 dB above 300 Hz** after the catch. Settled 1.3–2.5 s:
bands re 20–200 Hz = **−17 / −23 / −23 / −25 / −27 dB** (200–500 / 500–1k / 1–2k / 2–4k / 4–8k), **centroid 160 Hz**.

**Loops** (exact line spectra — a seamless loop is periodic, so 8 tiled repeats and a rectangular DFT give leak-free lines on a
6.0 Hz lattice; under R2 the half-orders fall *between* lattice lines, so a loop cannot hold them at all — which is itself a
weak hint: the author found a seamless cut at 3 revolutions = 1½ cycles, only possible if the 720°-periodic content was
negligible. The direct evidence is the start clip and the on-lattice lines between the integer orders):

| | idle loop (probably Jaguar) | rev loop (LaFerrari-labelled) |
|---|---|---|
| loudest lines | **108 Hz** and **180 Hz** (equal), 126 Hz (−4), 36 / 72 Hz (−19 / −16) | **108 Hz**, then 36 Hz (−13), 114 Hz (−12), 145 Hz (−15) |
| own period (circular ACF) | 55.5 ms, r = 0.89 (one revolution at 1081 rpm) | 9.07 ms, r = 0.85 (one firing at 1084 rpm); 27.7 ms r = 0.91 |
| orders under R2 (≈ 1082 rpm) | **6 = 10 = 0 dB**, 7 −4, 11 / 12 / 14 −15 … −16, 2 / 4 −18 / −17, **3 −35** | **6 = 0 dB**, 2 −13, 8 −15, 7 −19, 4 −24, 1 −27, **3 −34**, 9 … 12 −32 … −35 |
| lines *between* the integer orders (odd multiples of 6 Hz — on the lattice, so a real measurement) | −22 … −46 dB (mean −42) | −12 … −46 dB (mean −36; the −12 is the 114 Hz sideband of the 108 Hz line, a loop-modulation artefact) |
| bands re 20–200 Hz | −14 / −22 / −22 / −24 / −26 dB · centroid **179 Hz** | −26 / −35 / −44 / −48 / −46 dB · centroid **107 Hz** |
| lines within 26 dB below 1 kHz | 28 (highest 552 Hz) | **10** (highest 145 Hz) |

Three things agree across all three clips regardless of reading: the loudest line is **order 6 at ≈ 108 Hz** (≈ 1080 rpm), the
content is **one integer-order ladder with order 3 ≈ 35 dB down**, and the spectrum is **dark** — centroid 107–179 Hz, nothing
of note above 300 Hz.

### 10.3 Rev 2 at the same crank speed (full chain, same reader; L+R and the left channel alone)

The clips are mono, so the fair comparison is one channel — what one ear or one speaker gets. That matters here: rev 2's parity
pan puts the half-orders anti-phase between left and right, so the L+R sum hides most of them.

| | rev 2 · 1081 rpm load 0 (L+R / **left**) | rev 2 · 1081 rpm load 1 (L+R / **left**) | real rev loop (R2) | real start, settled |
|---|---|---|---|---|
| loudest order | 6 / **3½** | 6 / **3½** | 6 | 6 (117–145 Hz) |
| order 6 | 0 / −3 | 0 / −3 | 0 | 0 |
| order 3 | **−16** / −18 | −22 / −23 | **−34** | −16 (flare) … not found (settled) |
| orders 18 / 24 | **−10 / −11** | −12 / −10 | −40 / −56 | nothing within 25 dB above 300 Hz |
| half-orders ½ … 6½ (mean; worst) | −34; 6½ −19 / **−16; 3½ 0, 2½ −11** | −34; 6½ −25 / **−18; 3½ 0, 2½ −16** | −36 (R1 row) / not holdable (R2) | none found |
| bands re 20–200 Hz | −3 / −15 / −22 / −24 / **−13** | −3 / −13 / −15 / −14 / −7 | −26 / −35 / −44 / −48 / −46 | −17 / −23 / −23 / −25 / −27 |
| centroid < 8 kHz | 403 Hz | 976 Hz | 107 Hz | 160 Hz |
| peaks within 26 dB below 1 kHz | 25 | 28 | **10** | 36 (the pitch glides through the window) |
| mean \|Δ\| over orders 1–14 vs the rev loop | 7.0 / 7.5 dB | 6.5 / 5.7 dB | — | — |

So against real audio rev 2 misses on four measurable things at idle:

1. **The half-orders — in one channel they are the loudest thing.** Left alone: order 3½ is the top line, 2½ at −11, ½ / 1½ at
   −18; the real clips have none (≤ −36 dB re order 6 where a loop can hold them; unfindable in the start clip). This is the §3
   parity-pan prediction measured against the car: a listener on one side hears a lumpy 6-of-12 pattern with a 720° period, not
   a V12. §5 a (bank-true routing) removes it outright; b/d keep it from creeping back.
2. **Order 3 ≈ 15–20 dB too strong.** At 1081 rpm order 3 is 54 Hz, and the fixed `pitch_hz` = 58 Hz thump (+15 Hz rise with
   rpm) lands within a few Hz of it — the thump *is* the order-3 excess, and its harmonics at 116 / 174 Hz land next to orders
   6 and 10 and widen them. §5 c removes it (pulse in crank degrees, no fixed pitch).
3. **Orders 18 / 24 ≈ 20–45 dB too strong** and **4–8 kHz ≈ 13 dB too bright**: the pulse sheet's fixed spectrum plus the
   crackle / rasp / ring layers. Real idle has nothing within 25 dB above 300 Hz. §5 c + f + i, targets tightened below.
4. **Too many lines and too bright overall**: 25–28 peaks within 26 dB below 1 kHz vs 10; the 200–500 Hz band 10–20 dB too
   hot; centroid 2.5–4× too high. Line density is what the ear calls "digital fizz".

Measured this way the idle problem is mostly *structure* (items 1–2: the wrong lines exist), and only then *colour* (items 3–4).

### 10.4 What this changes in the §5 list (targets, not new entries)

| §5 entry | Was | Now (measured target at idle, ≈ 1080 rpm, full chain) |
|---|---|---|
| a / b / d | "half-orders below −30 dB re order 6 in pure mode" | **below −40 dB re order 6 in the full chain, measured per channel, not on L+R** (real ≤ −36 on 8-bit video audio; leave headroom for the 16-bit lane) |
| b | order 3 ≈ −17 dB | order 3 **≈ −35 dB** at idle (both loops), rising to ≈ −16 only during the start flare; order 7 / 8 present at −15 … −19 (the 55/65° timing) |
| c | "centroid rises linearly with rpm" | **centroid ≈ 110–180 Hz at idle**; bands re 20–200 Hz between −17 / −23 / −23 / −25 / −27 (start, settled) and −26 / −35 / −44 / −48 / −46 (rev loop); nothing above 300 Hz within 25 dB |
| f | exhaust formants "from the recording" | two fixed ridges at **≈ 180 Hz** and **≈ 215 Hz** (present in the start clip while the pitch glides, and as the equal-loudness 180 Hz line of the idle loop) — candidates for the first two resonators, to be confirmed on a 16-bit recording; the 2.667 ms comb goes |
| i | idle mechanics "within 6 dB of the recording" | 2–6 kHz hash **≥ 25 dB down** in these clips (8-bit, video-grade — the true figure needs a real recording; keep `clatter` low until then) |
| new: **line count** | — | ≤ 15 peaks within 26 dB below 1 kHz at steady idle (the LaFerrari-labelled rev loop has 10; the other two clips are inflated by loop modulation and the pitch glide) — a direct meter for "fizz" in the A4 lane |
| new: **start sequence** | — | catch at ≈ 108–136 Hz → flare +25 % (≈ 1450–1550 rpm) within 0.4–0.8 s → glide down at ≈ 300 rpm/s → idle; Project-Dyno's start can be read straight off the clip |

### 10.5 Limits — why §8 still stands

8-bit (floor ≈ −50 dBFS), 22 kHz, mono, unknown microphone, almost certainly lifted from a video; **idle region only** — nothing
about the 6000 / 8000 rpm intake steps, the order profile at 9000 rpm, the valve step or the overrun; the idle loop is probably
a Jaguar. So: these numbers fix the *idle* targets and prove the order-structure argument on real audio; the rest of §5 (e, g, h)
still needs the §8 recordings. If you can point me at the published spectra you had in mind (a URL, a paper title, or a screenshot
dropped into `EngineContent/AudioArchives/FerrariLaFerrari/Reference/`), I read them next — the reader is written.
