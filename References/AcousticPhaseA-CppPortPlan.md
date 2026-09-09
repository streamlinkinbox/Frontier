# Acoustic Phase A — C++ port plan (row A2): the AudioEditor voice into `Engine/PlatformInterchange` + Project-Dyno

Plan only — no code until you say go. Companion to `AcousticPhaseA-Plan.md` (§5 row A2) and `AcousticPhaseA-VoicingReport.md`
(§5 rev-3 list, §10 measured idle targets). Written so it works **whichever voice ships first**: rev 2 as it stands today, or rev 3
after the LaFerrari work. The port is of a *contract* (TOML schema + integrator + proofs), not of a snapshot.

## 0. Summary

* The whole voice lives in one JavaScript text today (`Tools/AudioEditor/index.html`, `<script id="dsp">`, 1 009 lines): a TOML
  schema of 77 fields in 7 sections, `AcousticIntegrator` (≈ 410 lines of per-sample DSP), five small DSP pieces, `TransientSlots`,
  `DynoSequence` (+ `overrun`, `limiter`), `FreeRevPowertrain`, `scriptedRecord`. It runs identically in the page and in the
  AudioWorklet (proof: max |Δ| 3 × 10⁻⁸). The C++ port makes it run identically a third place: the engine.
* The A1 transport is already the shape the port needs: `SignalIntegrator::Render(float*, frames)` on the realtime thread,
  `RelayQueue<PowertrainRecord>` for demand, fixed 64-frame slices, `AudioExchange::RenderOffline` for bit-identical WAVs,
  `WaveCodec`, `DiagnosticMetrics`, `ToolchainSequence.{sh,ps1}`. `CrankClickIntegrator` is the template: the port is a second
  `SignalIntegrator` behind the same `Attach()`.
* **Acceptance is numeric, not "sounds the same"**: the same TOML, the same pull, the same seed → the C++ WAV and the JavaScript
  render agree to **±1 × 10⁻⁶ per sample in float64 mode** (the integrators are written in doubles on both sides and every
  transcendental is `std::` / `Math.` — identical to the last ulp is not guaranteed across libm and V8, so the bar is 10⁻⁶) and to
  **±0.1 dB per order** after the float32 output. If the order diagrams differ by more than that, the port is wrong, not "different".
* Six rows, each a commit with proofs (§4). Two of them (P0 harness, P5 editor hand-over) are tooling; P1–P4 are the code. Roughly
  1 300 lines of C++ against 1 009 of JavaScript, plus ≈ 500 of proof harness.
* Things that are **not** 1:1 and are decided here (§3): the random generator (bit-exact xorshift32, seeded per render), the sheet
  builders (rebuilt every 128 *samples* counted from the first sample, not per slice — already the JS rule), the TOML reader
  (tomlpp with the schema as a sheet, defaults from the same sheet), the crank angle in **double** with the same `6·rpm·dt` step,
  meters and scope capture kept (the editor and Project-Zero's `[audio]` page read them), listener presets kept as a C++ sheet.

## 1. What gets ported — inventory of the JavaScript, with its C++ home

| JS piece (index.html `#dsp`) | Lines | C++ home | Notes |
|---|---|---|---|
| `ACOUSTIC_SCHEMA` (77 fields: section, key, default, unit, note, [min, max, step]) | 86 | `Engine/PlatformInterchange/AcousticStructure.h/.cpp` — `struct AcousticStructure { Vehicle, Combustion, Voice, Exhaust, Mechanical, Turbo, Mix }` + a `constexpr` field sheet used by Load / Save / Describe | one sheet, three uses: defaults, TOML load with per-field fallback, TOML save with unit comments. The slider ranges ship too (`AcousticFieldRange`) — Project-Zero's `[audio]` page reuses them |
| `parseToml`, `serialiseToml`, `structureFromToml`, `structureDefaults`, `structureClone` | 130 | same files, via **tomlpp** (`ExternalPackages/tomlpp`, pin `1e8829b7`, already a submodule) — `Reader` pattern from `Engine/DisplayPresentation/ConfigurationRegistry.cpp` | the JS has its own 60-line TOML parser because a browser has none; C++ uses the real library. Round-trip proof: the three shipped TOMLs load → save → load with zero field drift |
| `Xorshift` (xorshift32, `uniform / signed / gauss`) | 16 | `Engine/PlatformInterchange/AcousticIntegrator.h` (private, header-only) | **bit-exact**: `uint32_t` shifts, `x * 2.3283064365386963e-10`, gauss = sum of four uniforms − 2, × √3. Same seed `0x5EED1234` default |
| `OnePole`, `OnePoleHigh`, `BiquadSection` (bandpass / highpass RBJ), `CombLine` (feedback comb, power-of-two ring) | 70 | `Engine/PlatformInterchange/SignalSections.h` (header-only, `struct`s, no virtuals) | doubles for state and coefficients, same formulas incl. the `clamp(hz, 1, 0.45·fs)` and `max(0.05, q)` guards |
| `TransientSlots` (32 one-shot voices: backfire, pop, anti-lag, blow-off) | 65 | `Engine/PlatformInterchange/TransientSlots.h/.cpp` | fixed arrays, no allocation; `Spawn(...)` mirrors the JS argument order exactly so the call sites read the same |
| `LISTENER_PRESETS` (trackside / chase / cockpit) | 6 | `AcousticIntegrator.h` — `enum class ListenerPreset` + `constexpr` sheet | Project-Zero later replaces the preset with a real listener; the fields stay |
| `AcousticIntegrator` — constructor sheets, `assignStructure`, `assignDemand`, `rebuildSheet`, `fireEvent`, `render` (9 numbered stages), `takeMeters`, `takeScope` | 410 | `Engine/PlatformInterchange/AcousticIntegrator.h/.cpp` — `class AcousticIntegrator final : public SignalIntegrator` | the port proper; §2 has the stage-by-stage map. Public surface: `AssignStructure`, `AssignDemand(PowertrainRecord)`, `AssignListener`, `AssignPureTone`, `Prepare`, `Render`, `TakeMeters`, `TakeScope`, `QueryFiringCount / PopCount / ClipCount / Boost / Spool` |
| `DynoSequence` (7 pulls, keyframes with idle/redline substitution) | 40 | `Projects/Project-Dyno/Source/DynoSequence.cpp` — **exists** with 5 pulls; gains `overrun` + `limiter` and the `(RedlineRpm, IdleRpm)` substitution the JS has | keyframe sheets must match the JS to the number — they are copied into a proof that diffs them |
| `FreeRevPowertrain` (torque shape, friction, inertia, idle governor, rev limiter, overrun cut) | 30 | `Projects/Project-Dyno/Source/FreeRevPowertrain.h/.cpp` | Project-Dyno's live mode gets a throttle key (Space / `--free`) like the editor; Project-Zero will replace it with its physics |
| `scriptedRecord` (load from throttle, idle load from friction, 14 Hz limiter chop) | 9 | `DynoSequence.cpp` — `PowertrainRecord DynoSequence::QueryRecord()` gains the same rule (needs the vehicle's idle / redline / friction → `Select(Name, const AcousticStructure&)`) | today's C++ `QueryRecord` returns rpm + throttle only; load is what the voice actually follows |
| `AcousticWorklet` (message plumbing) | 80 | — not ported; the C++ equivalent is `AudioExchange` + `RelayQueue`, done in A1 | |
| Editor app (`window.FrontierAudioEditor`, canvases, sliders) | ≈ 1 100 | — stays HTML; P5 adds the **file feed** contract so it can display a C++ `--render` WAV next to its own | |

Not in the JS but required by the C++ side: **`AcousticProof`** (Scratchpad harness: radix-2 FFT, Hann order sheets, palette PNG
via `stb_image_write` — the A1½ node proof rewritten in C++ so both sides produce the same numbers from the same code shape), and
a **JSON/text dump of the JS render** (`Scratchpad/AcousticEditorRender.js --dump`) for the identity diff.

## 2. The integrator, stage by stage (JS `render()` → C++ `Render()`)

Same order, same names, per sample; the C++ loops over `FrameCount` frames and writes interleaved `Output[2k]`, `Output[2k+1]`.

| # | JS stage | C++ | Watch-outs |
|---|---|---|---|
| 1 | demand smoothing: `rpmS` one-pole (τ 4 ms) with a ±50 000 rpm/s step clamp; throttle τ 10 ms; load τ 5 ms; `fuelOn = loadS > 0.02`; `load = fuelOn ? throttle : 0` | identical, doubles; demand comes from `RelayQueue<PowertrainRecord>::Take` at slice entry (A1 rule) — **not** mid-slice, so slice invariance holds (the JS `assignDemand` is likewise called between `render` calls) | the JS `assignDemand(rpm, throttle, load, boost)` takes four scalars; C++ takes the `PowertrainRecord` — `Boost` is read-only display in rev 2 |
| 2 | per-128-sample constants: `if (sampleIndex % 128 == 0) rebuildSheet(rpm, load)` — thump pitch, decay, noise rate, crackle / ring / rasp / howl gains, drive, 24 harmonic amps → 512-point body sheet, valve HP retune with a 0.2 s lag per rebuild, turbo rush band | identical; `sampleIndex` is a `uint64_t` counted from `Prepare()`; the body sheet rebuild is 512 × 24 sines every 2.67 ms (≈ 12 k `sin` calls) — fine on the realtime thread (≈ 60 µs), same cost the worklet pays | rebuild is counted in **samples since start**, not per slice, so 1 / 37 / 64 / 256-frame slices are byte-identical (already proven in JS; the C++ proof repeats it) |
| 3 | crank clock: `theta += 6·rpm·dt` [deg]; events at exact sub-sample fractions (`while theta ≥ evNext[e]`); cycle index; cam modulation | identical in double; `Theta` never wraps (the JS does not either) — at 9 000 rpm it grows 54 000 °/s, 10⁹ ° after ≈ 5 h, still exact in double | `fireEvent` for a FIRE recomputes `evNext` for **all** firing events from a shared walk — keep the loop, it is the RevSim "walk" the report wants gone in rev 3 (then it becomes a no-op behind `jitter_pct = 0`) |
| 4 | per-cylinder voices: pulse sheet (512-pt, linear read), body sheet, `exp(−t·decay)` envelope, crackle noise, valve ring, rasp AM, howl; parity/bank pan (`cSame / cOther`) | identical; `voiceT / voiceAmp / voiceActive` as fixed arrays of `MAX_CYLINDERS + 1`; `std::exp` per active voice per sample (≤ 12 at idle, ≈ 21 at 9 000 rpm on the V12 — the report's "too many overlapping voices" finding, unchanged by the port) | the JS reads `pulseSheet[i1]` with `i1 = (i0 + 1) & 511` — same wrap in C++ |
| 5 | throttle-edge transients (backfire arm/lift, turbo anti-lag / blow-off, overrun crackle with `popBoost`) | identical; `TransientSlots::Spawn` with the same 9 arguments; `rng.uniform() < rate·dt` per sample | the RNG **call order** must match the JS exactly (uniform for misfire, gauss for amplitude, uniform for the walk step, uniform for pops …) or the identity proof fails — the port keeps the JS statement order inside each stage |
| 6 | turbo: spool lag toward `rpm/redline · load`, whine sine, rush band-pass on white noise | identical | |
| 7 | mechanical: clatter (two band-passes + ring on crank-locked impulses), gear whine (order × rpm, Nyquist fade), hybrid motor tone | identical | |
| 8 | mix: per-channel `tanh(sum · drive)` → buses → `output_gain` → `CombLine` → valve `BiquadSection` HP → ±1 clip (counted) → listener `OnePole` LP (bypassed at trackside) | identical; `std::tanh` in double | the clip counter counts left only (JS quirk) — kept, it is a meter not audio |
| 9 | meters (12 mean-square accumulators) + crank-locked scope capture (one 720° cycle of output + head pulses, armed by the host) | kept: `TakeMeters(float[12])` and `TakeScope(...)` are what the editor's signal-chain diagram and scope show, and what Project-Zero's `[audio]` page will show | scope capture is 16 384 floats — allocated in `Prepare`, never in `Render` |

Pure mode (`pureTone`) is kept as `AssignPureTone(bool)`: it is what the order-structure proofs use.

## 3. Decisions taken in this plan (⚠️ where they deviate from "1:1")

1. **Doubles throughout, float at the edge.** The JS runs in float64; the port does too, converting to `float` only when writing
   `Output`. That is what makes the ±10⁻⁶ identity possible. Cost is nil on x64.
2. **Bit-exact RNG, seeded per render.** `Xorshift` is ported at the bit level (`uint32_t`); `Prepare()` reseeds from the
   structure's seed field or the default. Live playback uses the default seed — two runs of the same pull are identical, which is
   a feature for A/B listening and a non-issue for gameplay (Project-Zero can seed from its clock).
3. **RNG call order is part of the contract.** Every stage consumes randomness in the JS statement order. A comment block at the
   top of `Render()` lists the order; the identity proof is the enforcement.
4. **Schema sheet drives everything.** ⚠️ Instead of hand-written `Load` per field (the `ConfigurationRegistry` style), the 77 fields
   live in one `constexpr` sheet (`section, key, offset, kind, default, unit, min, max, step`) and `Load / Save / Describe / Apply`
   iterate it. Why: the editor's inspector, the TOML writer, Project-Zero's `[audio]` page and the C++ loader must never disagree on
   a field — one sheet, four consumers. The JS already works this way (`ACOUSTIC_SCHEMA`).
5. **Section names as-is.** `[vehicle] [combustion] [voice] [exhaust] [mechanical] [turbo] [mix]` — the shipped TOMLs are the
   interface; the C++ reads them unchanged. Field names stay `snake_case` in TOML, `PascalCase` in C++ (`pitch_hz` →
   `Voice.PitchHz`); the sheet holds both.
6. **Rev-3 readiness without rev-3 code.** ⚠️ The port adds nothing rev-3 specific, but three seams are left where the report's §5
   items land, so rev 3 is a change inside the integrator, not a second port: (a) event angles come from a per-cylinder sheet
   (`FiringAngle[cyl]`) — rev 3 fills it from `bank_angle_deg` instead of `j · 720/N`; (b) the pan is computed per cylinder from
   `BankOfCylinder` — rev 3 sets it bank-true by data; (c) the voice envelope is a function `VoiceKernel(t, rpm)` — rev 3 swaps the
   seconds-based `exp` for a crank-degree kernel. Unknown TOML keys are **warned and ignored**, missing keys take defaults, so a
   rev-3 TOML loads in a rev-2 build and vice versa.
7. **Slice size.** `AudioExchange` hands 64-frame slices; the JS worklet renders 128. Identity holds because nothing in the
   integrator depends on the slice length (proven 1/37/64/256 in A1½; re-proven in C++). No change.
8. **No allocation, no locks in `Render`.** All arrays are fixed (`MAX_CYLINDERS = 16`, 32 transient slots, 2 048-sample comb
   rings, 16 384-sample scope). `AssignStructure` from the main thread publishes a **new structure through a second `RelayQueue`**
   (⚠️ the JS mutates in place from the worklet message handler — fine in a single-threaded worklet, not in C++); the realtime side
   takes it at slice entry and calls the same `ApplyStructure` the constructor uses. Cylinder-count changes rebuild the event
   sheet at slice entry, as the JS does.
9. **`DynoSequence` load rule moves into C++.** Today C++'s `QueryRecord()` has `Load = 0`; the voice follows load, so the C++
   dyno must produce the JS `scriptedRecord` load (throttle, idle load from friction, limiter chop at 14 Hz). Same numbers.
10. **stb_image_write for proof PNGs.** Already a submodule (`ExternalPackages/stb`); the node proofs write palette PNGs by hand,
    the C++ proof writes RGB PNGs through stb — pixel content is the same order sheet, the encoder differs, which is fine (the
    comparison is on numbers in the log, the PNG is for eyes).

## 4. Rows (one commit each on this branch; proofs in `Scratchpad/`; hardware acceptance on your GTX 1060 Windows box)

| Row | Scope | Sandbox proof | Your acceptance |
|---|---|---|---|
| **P0** JS reference dump + C++ proof scaffold | `Scratchpad/AcousticEditorRender.js --dump <car> <pull> <seed> <seconds>` writes `Scratchpad/Reference/<car>_<pull>.f64` (raw float64 L/R) + `.json` (meters, firing / pop / clip counts, structure echo); `Scratchpad/AcousticProof.cpp` (FFT, order sheet, band levels, centroid, PNG, `.f64` diff) built by `ToolchainSequence.sh --proof` | `AcousticProof --self` : FFT round trip, order reader on a synthetic comb = expected ±0.01 dB | none (tooling) |
| **P1** `AcousticStructure` + `SignalSections` + `TransientSlots` | schema sheet, tomlpp Load / Save / Describe, defaults, unknown-key warnings; header-only DSP pieces; transient slots | `Scratchpad/AcousticStructureTest.log`: the 3 shipped TOMLs load with 0 warnings; load → save → load byte-stable; every field's default equals the JS default (dump compares `structureDefaults()` against `Describe()`); DSP pieces vs JS on 4 096-sample impulse / noise runs ±1 × 10⁻¹² (pure arithmetic — must be tighter than 10⁻⁶) | none |
| **P2** `AcousticIntegrator` — stages 1–4 + 8 (pure mode) | crank clock, events, voices, sheets, pan, output chain in pure mode; `SignalIntegrator` contract; structure relay | `Scratchpad/AcousticIdentityTest.log`: pure-mode render vs the P0 dump for 3 cars × `steady` / `pull` × seed 0x5EED1234: **max \|Δ\| ≤ 1 × 10⁻⁶** per sample (float64), order diagram ±0.1 dB; 1 / 37 / 64 / 256-frame slices byte-identical; firing count = ∫ rpm · N / 120 ± 1 | none yet |
| **P3** `AcousticIntegrator` — stages 5–7 + 9 (full chain) | transients, turbo, mechanical, meters, scope, listener presets, comb / valve / clip / LP | identity log extended to full chain, all 7 pulls, 3 cars: max \|Δ\| ≤ 10⁻⁶; meters ±10⁻⁶; pop / clip / firing counts equal; `TransientSlots` never drop on `limiter`; ±1 clip held; < −30 dB past 16 kHz in pure mode (A1½ bar) | none yet |
| **P4** Project-Dyno wiring | `--car <Porsche918Spyder\|FerrariLaFerrari\|NissanGtrNismo\|path.toml>`, `--listener`, `--pure`, `--seed`, `--free` (Space = throttle, `FreeRevPowertrain`); `DynoSequence` `overrun` + `limiter` + load rule; `[Audio]` line gains firing / pop / clip counters and boost; `--render` writes the same WAV the device plays; `CMakeLists.txt` + both `ToolchainSequence` scripts list the new sources; **`CrankClickIntegrator` stays** behind `--clicks` as the transport self-test | `Scratchpad/ProjectDynoRender.log`: `--render` for 3 cars × `pull` at 48 kHz → order sheets + PNGs (`Scratchpad/ProjectDyno_<Car>_Pull.png`); WAV ⇄ JS dump identity after float32 quantisation (≤ 1 LSB of 24-bit); null-device 60 s run: 0 overloads, peak callback µs logged | **`ToolchainSequence.ps1 -Run -- --car FerrariLaFerrari --pull pull`** on your box: same sound as the editor's LaFerrari `pull` (A/B: `--render` WAV dropped onto the editor's file feed shows the same spectrogram); `--free` + Space revs like the editor; `[Audio]` peak µs < 500, overloads 0 |
| **P5** editor hand-over | editor's file feed shows the C++ WAV's order diagram **over** its own (the A4 overlay, first use); `Export TOML` output is byte-identical to `AcousticStructure::Save` for the same structure (comment text included) so files round-trip between tools; `Diagnostics/AudioEditor_07_CppOverlay.png` | `Scratchpad/AcousticEditorBrowser.log` gains: overlay ±0.1 dB per order for the 3 cars; TOML export ⇄ C++ save diff empty | open the editor, drop a C++ `--render` WAV, see two order diagrams coincide |

Estimated size: P1 ≈ 450 lines, P2 ≈ 350, P3 ≈ 300, P4 ≈ 200 (+ script edits), P0 / P5 ≈ 500 of proofs and editor glue.

## 5. File layout after A2

```
Engine/PlatformInterchange/
    AudioExchange.h/.cpp            (A1, unchanged)
    WaveCodec.h/.cpp                (A1, unchanged)
    MiniaudioTranslation.cpp        (A1, unchanged)
    AcousticStructure.h/.cpp        P1  schema sheet · TOML load/save · defaults · describe
    SignalSections.h                P1  OnePole · OnePoleHigh · BiquadSection · CombLine · Xorshift
    TransientSlots.h/.cpp           P1  one-shot voices
    AcousticIntegrator.h/.cpp       P2–P3  the voice (SignalIntegrator)
Projects/Project-Dyno/Source/
    GameExecution.cpp               P4  --car / --listener / --pure / --seed / --free / --clicks
    DynoSequence.h/.cpp             P4  + overrun · limiter · load rule
    FreeRevPowertrain.h/.cpp        P4  free-rev stand-in for physics
    CrankClickIntegrator.h/.cpp     (A1, kept as self-test)
Scratchpad/
    AcousticProof.cpp               P0  FFT · order sheet · PNG · f64 diff (built with --proof)
    AcousticEditorRender.js         P0  + --dump
    Reference/*.f64 *.json          P0  JS reference renders (git-ignored: regenerate with --dump; the .json is committed)
    AcousticStructureTest.log · AcousticIdentityTest.log · ProjectDynoRender.log · ProjectDyno_<Car>_Pull.png
```

Naming follows the locked list (`AcousticStructure`, `AcousticIntegrator`, `DynoSequence`, `PowertrainRecord`, `SignalIntegrator`,
`RelayQueue`); new names: `SignalSections`, `TransientSlots`, `FreeRevPowertrain`, `AcousticProof` — none on the banned list
(`CLAUDE.md` §3). Units in comments `[Hz] [rpm] [°] [s] [bar] [-]`, Allman, 4 spaces, 142/122 banners.

## 6. Risks and how each is closed

| Risk | Closed by |
|---|---|
| libm vs V8 transcendental differences accumulate through the feedback comb and the RNG-driven branches → drift beyond 10⁻⁶ | measured, not assumed: P2 reports the max \|Δ\| and where it first exceeds 10⁻⁷; if a branch flips (`rng.uniform() < p` on a value differing at 10⁻¹⁶), the two renders diverge from that sample — the proof reports the first divergence sample so it is visible. Fallback: compare **order diagrams ±0.1 dB and counts**, which is the audible contract anyway |
| The body-sheet rebuild (12 k sines every 128 samples) on the realtime thread | ≈ 60 µs at −O2, within the 1.33 ms slice; `[Audio]` peak µs shows it on your box. If it ever matters: precomputed sine sheet (also what rev 3 would do) |
| `AssignStructure` from the main thread while `Render` runs | second `RelayQueue<AcousticStructure>`; the structure is 71 numbers + two small arrays — a copy per slice entry is cheap, and only when a new one was published |
| Rev 3 lands while the port is under way and both change the integrator | the port is done against the **dump**: whichever JS is shipped when P0 runs is the reference; if rev 3 ships first, P0 dumps rev 3 and the port is of rev 3 — the row list does not change, the stage sheet (§2) gains rows for the intake voice and the crank-degree kernel |
| Windows: MSVC `std::exp / tanh` differ from glibc → identity fails on your box but passes here | the identity proof runs in the sandbox against the dump; on Windows the acceptance is order diagram ±0.1 dB via the P5 overlay, which is the audible bar |
| tomlpp compile time / C++20 needs | already built into `ConfigurationRegistry`; same flags |

## 7. What I need from you before "go"

1. **Order confirmation**: the report's §9 sequence is A4 lane + rev-3 core → LaFerrari rev 3 → 918 → GT-R → **then** A2 (this plan).
   You said "tomorrow" for that call; this plan holds either way (§3.6, §6 row 4). If A2 goes first, it ports rev 2 and rev 3
   later changes both sides through the same identity proof.
2. **Seed policy** for live play (fixed default vs clock) — default fixed, per §3.2.
3. **Meters in the `[Audio]` line** every 2 s: firing / pop / clip counts and boost — yes unless you want it quieter.
4. Nothing else: no new packages, no repo access, no recordings needed for this row.

---

## Merge note (integrated into the Slate interface branch)

Counted at integration time rather than taken from the prose: the editor's `ACOUSTIC_SCHEMA` and all three shipped
car archives carry **77 fields in 7 sections** — vehicle 13, combustion 10, voice 21, exhaust 7, mechanical 7,
turbo 14, mix 5. The plan said 71; the references above are corrected. It matters because §3.6 sizes the C++ field
sheet from that number, and a sheet six entries short would load six fields as their defaults and never say so.

Verified on integration: all three TOMLs parse and share an identical key set; all four `Scratchpad/Acoustic*.js`
modules parse; the editor is 154 KB. The DSP itself is not verified here — it needs a browser with an
AudioContext, so the identity proof the plan describes (§P0) remains the first real check of the port.
