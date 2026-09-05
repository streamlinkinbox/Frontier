# 🔊 Phase A — Vehicle acoustics (plan; awaiting approval — no code until "go A1")

Sequence 1 = engine audio on the main loop + a third project that only does audio (rows A1–A2).
Sequence 2 = HTML AudioEditor/visualiser + three vehicles, one at a time, tuned until they stop sounding
synthetic (rows A3–A7). The 3D-UI plan (P0 gauge spike …) is parked; its RPM gauge will later bind to the
same `PowertrainRecord` that drives the audio.

## 0. Scope line

**In:** miniaudio device transport (`AudioExchange`), procedural powertrain synthesis (`AcousticStructure`
+ `AcousticIntegrator`), `Projects/Project-Dyno` (windowless dyno cell: scripted pulls, offline WAV render),
`Tools/AudioEditor/index.html` (single file, zero deps, AudioWorklet port of the same DSP, scopes, order
diagram, TOML round-trip), three car descriptors under `Content/AudioArchives/`.
**Out (explicit):** world placement / doppler / N-vehicle mixing (Phase B), tyre-wind-surface layers (B),
cabin occlusion + convolution reverbs (B), EOS voice (`VoiceExchange`, separate), sample-based layering
(all three cars are fully procedural — samples are a fallback, not the plan).

## 1. Where it lives (merge = copy)

```
Engine/PlatformInterchange/
    MiniaudioTranslation.cpp        ← the one TU with MINIAUDIO_IMPLEMENTATION (same drill as UfbxTranslation.cpp)
    AudioExchange.h/.cpp            ← role Exchange: ma_device C-ABI boundary, realtime callback, metrics
    AcousticStructure.h/.cpp        ← role Structure: a powertrain's sound-path topology (TOML in/out)   [already in CMake]
    AcousticIntegrator.h/.cpp       ← role Integrator: advances crank + waveguides, renders sample blocks [already in CMake]
Projects/Project-Dyno/
    Build/ToolchainSequence.ps1 / .sh   ← no Vulkan, no slangc; links miniaudio TU + PlatformInterchange
    Content/AudioArchives/Porsche918Spyder.toml · FerrariLaFerrari.toml · NissanGTRNismo.toml
    Source/GameExecution.cpp        ← main loop (below) + `--render <wav> --car <toml> --pull <name>`
    Source/DynoSequence.h/.cpp      ← role Sequence: scripted rpm/throttle timelines (idle, sweep, WOT pull, overrun, blips)
Tools/AudioEditor/index.html        ← Sequence 2
ExternalPackages/miniaudio @9634bedb (v0.11.25) · tomlpp @1e8829b7   ← add to .gitmodules (listed in CLAUDE.md, missing there)
```

Main loop contract ("Tick"): every host in Slate already exposes `Advance(Δτ)`; audio follows suit.

```cpp
Frontier::AudioExchange      Audio;                                   // device + realtime thread
Frontier::AcousticStructure  Car   = Frontier::AcousticStructure::Load(".../Porsche918Spyder.toml");
Frontier::AcousticIntegrator Powertrain(Car);                         // synthesis (thread-agnostic Render)
Audio.Open({ 48000 /*[Hz]*/, 2 /*ch*/, 256 /*frames per period*/ });
Audio.Attach(Powertrain);                                             // callback → Powertrain.Render(out, frames)
while (running)
{
    Pull.Advance(Δτ);                                                 // DynoSequence → rpm, throttle
    Powertrain.Command(Pull.QueryRecord());                           // main thread: post PowertrainRecord (lock-free)
    Audio.Advance(Δτ);                                                // publish commands, drain AudioMetrics, device loss
}
```

Merging into Project-Zero later is three lines in `GameExecution.cpp` (construct, `Open`, `Advance(Δτ)` next to
`Notifications.Advance(Δτ)`). The realtime thread never touches anything but the integrator; the main thread
never blocks on audio. Zero allocations after `Open` (everything pre-sized), denormals flushed, rpm ramped
linearly across each block (no zipper), `ma_backend_null` when no device (sandbox), `--render` bypasses the
device and calls the same `Render` in a loop — so the offline WAV is bit-identical to what the device would play.

`PowertrainRecord { Rpm [rpm], Throttle [-], Load [-], Gear, ClutchEngaged, Boost [bar] }` is the only thing
the game will ever have to feed. Physics can produce it; the dyno scripts it.

## 2. Why the earlier attempts sounded "digital", and the design that doesn't

| Digital tell | Cause | What this design does instead |
|---|---|---|
| Buzzy, organ-like | perfectly periodic pulses | cycle-to-cycle variation: ±3–8 % amplitude, ±0.5–1.5° timing per firing; random misfires on overrun/fuel-cut |
| Fizz / warble that tracks rpm | pulse onsets quantised to samples, naive saw/click pulses alias | **analytic pulse kernels evaluated at exact sub-sample time** — `p(t) = A·(t/τ)^k·e^{k(1−t/τ)}` (smooth ⇒ band-limited by construction); a second, faster kernel scaled by load is the WOT "bark" |
| Static formants | fixed EQ | **digital waveguides** for intake + exhaust: primaries → collector junction → cat/muffler resonators → tailpipe; speed of sound `c = 20.05·√T` follows gas temperature (≈ 560–690 m/s hot, rises with load), fractional-delay interpolated |
| No texture | pure tonal synthesis | turbulent **flow noise gated by the pulse envelope** and comb-filtered by the tailpipe (this *is* the rasp); valvetrain ticks (2/cyl/cycle, jittered), injector clicks, order-locked gear/chain whine |
| Instant, weightless response | rpm slewed by a constant | rotational inertia `J·ω̇ = τ_engine(rpm, throttle) − τ_load`; light flywheel on the 918, turbo lag on the GT-R, rev-hang |
| Same sound at all loads | one amplitude knob | pulse amplitude **and shape** follow load; overrun = weak combustion + unburnt-fuel **pops** (random impulses into the hot tailpipe with decay — the crackle) |
| Flat, centred | mono | banks to L/R (the 918's top pipes are literally left/right), listener presets trackside / cockpit / chase |
| Clean at 130 dB SPL | linear mix | mild `tanh` saturation on the exhaust bus — real tailpipe pressure is nonlinear; then a limiter |

Signal chain (per vehicle, ~100 flop/sample — < 1 % of a core at 48 kHz):

```
crank clock θ(t) ─► per-cylinder events at exact firing angles (bank angle + crank pins + firing order; uneven intervals kept)
   ├─ combustion → exhaust-valve blowdown pulse ─► EXHAUST: bank primaries ─► collector ─► [turbine] ─► cat/muffler ─► tailpipe ─► radiation HP
   ├─ intake-valve suction pulse (inverted, softer) ─► INTAKE: runner + plenum Helmholtz (honk ∝ throttle) + throttle-body flow noise
   ├─ MECHANICAL bed: valvetrain, injectors, chain/gear mesh orders, DCT whine (GT-R), inverter carrier (918 / LaFerrari hybrids)
   ├─ FORCED INDUCTION (GT-R only): shaft-speed integrator (first-order lag) → compressor whine harmonics, wastegate flutter, recirc hiss on lift
   └─ BODY: 2–3 low resonators (60–120 Hz cabin/panel boom) ─► listener mix ─► soft-clip ─► limiter ─► stereo out
```

Reference designs (approach, not code): AngeTheGreat's open-source engine-sim (combustion + exhaust
waveguides + convolution — proof that this class of synthesis sounds real); Baldan, Delle Monache &
Rocchesso, *Physically informed car engine sound synthesis* (2015).

## 3. The three vehicles (physical starting points — ≈ means tune / verify against recordings)

Four-stroke: firing frequency = rpm/60 × N/2, so the dominant "engine order" is N/2.

| | Porsche 918 Spyder | Ferrari LaFerrari | Nissan GT-R Nismo (R35) |
|---|---|---|---|
| Engine | 4,593 cc 90° V8, **flat-plane crank**, NA, dry sump | F140FE 6,262 cc **65° V12**, NA, 13.5:1 | VR38DETT 3,799 cc 60° V6, **twin IHI turbos** |
| Bore × stroke | 95 × 81 mm | 94 × 75.2 mm | 95.5 × 88.4 mm |
| Peak / redline | 608 PS @ 8,700 · **9,150 rpm** | 800 PS @ 9,000 · **9,250 rpm** | 600 PS @ 6,800 · 7,000 (cut ≈ 7,100) |
| Firing | even 90°, banks alternate L-R ⇒ each bank = an inline-4 (2nd order per bank, 4th overall); no half-orders ⇒ no burble, pure wail | even 60° (crank-pin offset compensates 65°), banks alternate ⇒ each bank = an inline-6 (3rd per bank, 6th overall); Ferrari order 1-7-5-11-3-9-6-12-2-8-4-10 (numbering scheme ≈) | 1-2-3-4-5-6 (Nissan numbering R 1-3-5 / L 2-4-6 ⇒ alternating, even 120°); 3rd order overall, 1.5 per bank |
| Dominant order at peak | 4th: 580 Hz @ 8,700 | 6th: 900 Hz @ 9,000 | 3rd: 340 Hz @ 6,800 |
| Exhaust path | **top pipes** behind the heads: total ≈ 0.8 m, primaries ≈ 0.35 m ⇒ almost no low-pass, extremely bright, F1-like | equal-length hydroformed headers ≈ 0.8 m, 6-2-1 per bank, total ≈ 2.8 m ⇒ very clean harmonic stack | bank → turbine → downpipe → merge → twin rear mufflers, total ≈ 3.8 m, Nismo titanium ⇒ turbine low-passes the pulses, hollow drone 3–4k |
| Intake character | moderate; short trumpets | **dominant at high rpm** — plenums right behind the cabin, variable-length runners (honk changes with rpm) | muffled; compressor whine + recirc hiss on lift are the signature |
| Idle | ≈ 900 rpm | ≈ 850 rpm | ≈ 650 rpm |
| Extras | light flywheel ⇒ very fast rev response; sharp lift-off crackles; e-motor whine below ≈ 20 km/h, engine start clatter when it cuts in | HY-KERS assist (faint inverter under the V12); intake resonance shift with runner length | GR6 dual-clutch transaxle whine + clunks; turbo shaft ≈ 150 k rpm ⇒ audible whistle 4–12 kHz; wastegate flutter; boost ≈ 1.3 bar |
| Proposed order | **1st** — cleanest physics (two I4 banks), exposes every pulse-shape flaw; proves the core | **2nd** — adds 12-cyl bank angle, intake dominance, harmonic purity | **3rd** — adds the whole forced-induction + DCT layer on top of a proven core |

(You listed the GT-R first; I recommend 918 → LaFerrari → GT-R because the NA cars are the hardest to fake and
the turbo layer is purely additive. Your call.)

## 4. Sequence 2 — AudioEditor (`Tools/AudioEditor/index.html`)

Single file, zero dependencies, Frontier tokens (Inter, blue/black/white/dark-grey from `app/index.html`,
components per Slate `References/UIComponents.html`). The DSP runs in an **AudioWorklet as a 1:1 port of
`AcousticIntegrator`** — the car TOML is the single source of truth; the editor exports it, the C++ loads it.
That is what makes tuning fast: you hear it in the live preview, then the C++ mirrors it and we prove identity.

Views:
1. **Dyno strip** — car picker, throttle slider, RPM dial (our gauge), scripted pulls (idle · sweep · WOT pull ·
   overrun · gear blips), record → WAV, listener preset.
2. **Signal-flow diagram** — the chain in §2 for the selected car, live level meters on every edge; click a block
   → its TOML fields in the inspector. This is the "visualise the audio we are creating" surface.
3. **Scopes** — oscilloscope **crank-locked** (triggered on cylinder 1 TDC so one cycle stands still and the pulse
   shape is readable); log spectrum with engine-order markers (0.5, 1, 1.5, 2 … × rpm); spectrogram; **order
   diagram** (level of each order vs rpm — the standard NVH view for matching a synth to a recording).
4. **Reference lane** — drop a recording (mp3/wav), aligned spectrogram + order diagram over the synth's.
5. **Device panel** — sample rate, block, worklet load %, glitches: mirrors `AudioMetrics` field-for-field so a
   later WebSocket bridge from the C++ is a drop-in.

## 5. Rows (one commit each, proofs + hardware acceptance + ⚠️ deviations per row)

| Row | Scope | Sandbox proof | Your acceptance (Windows) |
|---|---|---|---|
| **A1** transport | `.gitmodules` pins (miniaudio 9634bedb, tomlpp 1e8829b7), `MiniaudioTranslation.cpp` (`MA_NO_ENGINE`, `MA_NO_RESOURCE_MANAGER`, `MA_NO_NODE_GRAPH`), `AudioExchange` (open/close, callback, SPSC command mailbox, `AudioMetrics`, device-loss reopen, null backend, `RenderToWave`), `Project-Dyno` skeleton + `ToolchainSequence.ps1/.sh` + CMake target, plays a sine sweep | builds with g++ 12; null backend opens; `--render` writes a 5 s sweep WAV; `-Wall -Wextra` clean | hear the sweep; F-key-free; callback µs < 500, zero glitches over 60 s |
| **A2** synthesis skeleton | `AcousticStructure` TOML load/save, `AcousticIntegrator` v1 (crank clock, analytic pulses with jitter, exhaust + intake waveguides, flow noise, mechanical bed, inertia), `DynoSequence` pulls, three first-cut TOMLs | offline WOT pulls for all three cars → `Scratchpad/` WAVs + spectrogram PNGs (C++ harness, radix-2 FFT, `stb_image_write`); order-diagram sanity: dominant order = N/2 within 1 Hz; no energy above Nyquist/2 in a silent-pulse test (aliasing guard) | first listen: "an engine, wrong car" is the bar |
| **A3** editor design | static HTML: all five views with mock data, tokens, layout, resize | screenshots in `Diagnostics/AudioEditor_*.png` | look approved before wiring |
| **A4** editor live | AudioWorklet port, scopes, order diagram, TOML import/export, reference lane, WAV record | worklet renders the A2 pull and its order diagram matches the C++ render within ±1 dB per order | audible in the live preview |
| **A5** car 1 (918) | tune in editor → back-port constants/DSP deltas to C++ → identity proof | C++ vs worklet order-diagram match ±1 dB; reference-lane match report | you sign off by ear |
| **A6** car 2 (LaFerrari) | same loop; adds runner-length resonance shift, intake dominance | same | same |
| **A7** car 3 (GT-R) | same loop; adds turbo shaft integrator, whine, wastegate, recirc, DCT orders | same | same |
| **A8** phase note | `References/AcousticPhaseA.md`, per-car parameter sheets, Phase B scope (placement, doppler, tyres, cabin) | — | — |

## 6. Vocabulary (banned-word substitutions — DSP is a minefield for CLAUDE.md §3)

`Filter` → `Resonator` / `BiquadSection` / `OnePoleLag` · `Buffer` → `Ring` / `Extent` · `Source` → `Generator` /
`Emitter` · `Flow` → `Discharge` (exhaust) / `Induction` (intake) · `Blend` → `Mix` · `Map` (Campbell map) →
`OrderDiagram` · `Table` → `Sheet` · `Node` (junction) → `Junction` · `Pipeline` → `Chain` · `History` → `Trace` ·
`Tick` → `Advance(Δτ)` (existing convention). Units annotated `[Hz] [rpm] [m] [s] [Pa] [bar] [-]`.

## 7. Decisions needed before "go A1"

1. **Repo placement** — build here in `streamlinkinbox/Frontier` at Slate-identical paths, merge by copy later
   (this session cannot push to Slate). OK, or would you rather re-point a session at Slate first?
2. **Names** — `Project-Dyno`; `AudioExchange` / `AcousticStructure` / `AcousticIntegrator` (the last two are
   already in Slate's CMake). Alternatives welcome; §6 vocabulary otherwise applies.
3. **Car order** — 918 → LaFerrari → GT-R (recommended) or GT-R first as you listed.
4. **Editor architecture** — DSP defined once, worklet port in the browser so tuning is audible in the preview,
   C++ mirrors with an order-diagram identity proof. (Alternative: C++-only + WAV drops into the editor — slower loop.)
5. **Reference recordings** — will you supply clips (idle / steady 4k / WOT pull / overrun per car) for the
   reference lane, or do we judge by ear + order-signature checks only?

⚠️ Pre-existing: Slate's `CMakeLists.txt` also lists `VoiceExchange.cpp` and `OnlineInterchange.cpp`, which do not
exist — the Linux CMake target is already unbuildable. A1 fixes the two acoustic entries; the other two stay
out of scope (flagged, not touched).
