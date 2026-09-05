# 🔊 Phase A — Vehicle acoustics (plan v2, anchored to Slate; awaiting approval — no code until "go A1")

Target repository: **`unassignedinbox/Slate` @ `arena/01a06c54-slate`** (tip `91072b0`, 2026-09-05). This file lives in
Slate's `References/` and follows its conventions (`CLAUDE.md`, `Session-Conventions.md`: plan first, one commit per
row, explicit-path staging, proofs in `Scratchpad/`, screenshots in `Diagnostics/`, ⚠️ deviations).

**Workflow (agreed): work on a clone, you merge.** The branch `streamlinkinbox/Frontier @ arena/01a07196-frontier` now
*is* Slate's `arena/01a06c54-slate` — same root commit `6e43918`, same 56 commits, HEAD `91072b0`, tree byte-identical —
with Phase A commits stacked on top. Nothing is force-pushed to Slate; the session never asks for access. To take a row:

```
git remote add frontier https://github.com/streamlinkinbox/Frontier.git      # once
git fetch frontier arena/01a07196-frontier
git merge --ff-only frontier/arena/01a07196-frontier                          # fast-forward while Slate's tip is 91072b0
git merge frontier/arena/01a07196-frontier                                    # or a normal merge once Slate has moved on
git submodule update --init -- ExternalPackages/miniaudio ExternalPackages/tomlpp
```

Safety: `Tools/git-hooks/pre-commit` is installed in the clone (checks the pushed tip is an ancestor of HEAD before each
commit); staging is by explicit path; every row ends with `git log --oneline -4` matching the expected tip.

Sequence 1 = engine audio on the main loop + a third project that only does audio (rows A1–A2).
Sequence 2 = HTML AudioEditor/visualiser + three vehicles, one at a time, tuned until they stop sounding synthetic
(rows A3–A7). The 3D-UI plan (P0 gauge spike …) is parked; its RPM gauge later binds to the same `PowertrainRecord`.

## 0. Scope line

**In:** miniaudio device transport (`AudioExchange`), WAV read/write (`WaveCodec`), procedural powertrain synthesis
(`AcousticStructure` + `AcousticIntegrator` — both names already sit in Slate's CMake list as missing files),
`Projects/Project-Dyno` (windowless dyno cell: scripted pulls, offline WAV render, console keys), `Tools/AudioEditor/
index.html` (single file, zero dependencies, AudioWorklet port of the same DSP, scopes, order diagram, TOML round-trip),
three powertrain descriptors as TOML.
**Out (explicit):** world placement / doppler / N-vehicle mixing (Phase B), tyre–wind–surface layers (B), cabin
occlusion + convolution reverbs (B), `[audio]` Control Centre page + config keys (B), EOS voice (`VoiceExchange`,
separate), sample-based layering (all three cars are fully procedural — samples are a fallback, not the plan),
any change to Project-Zero's build or `GameExecution.cpp` (merge is Phase B, three lines — see §1.3).

## 1. Where it lives in Slate

### 1.1 Files (all new unless marked)

```
.gitmodules                                   ← +miniaudio @9634bedb (v0.11.25) +tomlpp @1e8829b7 (adopts your existing folders)
.gitignore                                    ← +Scratchpad/*.wav  +Projects/**/Build/Output/
Engine/PlatformInterchange/                   ← new folder (CLAUDE.md §10 already reserves it: "Audio, voice, online")
    MiniaudioTranslation.cpp                  ← the one TU with MINIAUDIO_IMPLEMENTATION (twin of ContentInterchange/UfbxTranslation.cpp)
    AudioExchange.h/.cpp                      ← role Exchange: ma_device C-ABI boundary, realtime callback, AudioMetrics
    WaveCodec.h/.cpp                          ← role Codec: float ⇄ PCM16/24 WAV (offline renders; reference clips for harnesses)
    AcousticStructure.h/.cpp                  ← role Structure: a powertrain's sound-path topology; TOML in/out via tomlpp
    AcousticIntegrator.h/.cpp                 ← role Integrator: advances crank + waveguides, renders sample blocks
Projects/Project-Dyno/
    Build/ToolchainSequence.ps1               ← copy of Zero's minus Vulkan / slangc / ImGui / GLFW / ThorVG; same fail-fast source check
    Build/ToolchainSequence.sh                ← g++ ≥ 12 (sandbox proofs run this)
    Content/AudioArchives/                    ← Porsche918Spyder.toml · FerrariLaFerrari.toml · NissanGTRNismo.toml  (see §6 Q3)
    Source/GameExecution.cpp                  ← main loop (§1.3) + `--render <wav> --car <toml> --pull <name>` + console keys
    Source/DynoSequence.h/.cpp                ← role Sequence: scripted rpm/throttle timelines (idle · sweep · WOT pull · overrun · blips)
Tools/AudioEditor/index.html                  ← Sequence 2 (⚠️ CLAUDE.md §8 frames Tools/ as C++ dual-target apps; HTML by your request)
References/AcousticPhaseA-Plan.md             ← this file;  References/AcousticPhaseA.md = phase note (A8)
Scratchpad/Acoustic*Test.cpp + *.log + *.png  ← harnesses + proofs;  Diagnostics/AudioEditor_*.png ← UI proofs
```

### 1.2 Build wiring (Windows is the real build)

* `Projects/Project-Zero/Build/ToolchainSequence.ps1` is **not modified** in Phase A. Its `$EngineRelative` list
  (lines 586–631) and `Get-IncludePaths` (160–179) gain the five PlatformInterchange TUs + `/I ExternalPackages\miniaudio`
  only at merge (Phase B). Its `$SubmoduleList` already names `ExternalPackages/miniaudio` and `tomlpp` (477–490);
  once A1 adds the gitlinks, the `git submodule update --init` pass stops failing over to the directory check.
* `Projects/Project-Dyno/Build/ToolchainSequence.ps1`: `/std:c++20 /W4 /utf-8 /permissive- /MD /arch:AVX` as Zero,
  includes `Engine`, `ExternalPackages\miniaudio`, `ExternalPackages\tomlpp\include`, `ExternalPackages\stb`; links
  `ole32.lib` (WASAPI/COM) + `user32.lib`; `/SUBSYSTEM:CONSOLE`; output `Build\Output\Windows\<Config>\Binary\Project-Dyno.exe`.
* miniaudio TU defines: `MA_NO_ENGINE MA_NO_RESOURCE_MANAGER MA_NO_NODE_GRAPH MA_NO_DECODING MA_NO_ENCODING
  MA_NO_GENERATION` — device + data conversion only; WAV goes through `WaveCodec`. Backends: WASAPI (Windows), ALSA/
  PulseAudio (Linux), **`ma_backend_null`** when no device (sandbox) or `--render`.
* `CMakeLists.txt`: ⚠️ its `FRONTIER_ENGINE_SOURCES` lists ~20 files that do not exist (ByteSpace, TaskScheduler,
  PhysicalDynamics/*, VoiceExchange, WorkspaceHost, FrontierHost …); the Linux target is already unbuildable. Phase A
  adds a self-contained `Project-Dyno` target and the two acoustic entries it already lists; the rest is flagged, not touched.

### 1.3 Main-loop contract ("Tick" = Slate's `Advance(Δτ)`)

Every host in Slate exposes `Advance(Δτ)` and is called from `GameExecution.cpp`'s loop (Zero: lines 344–366, next to
`Notifications.Advance(Δτ)`). Audio follows suit:

```cpp
Frontier::AudioExchange      Audio;                                           // device + realtime thread
Frontier::AcousticStructure  Car;   Car.Load("Projects/Project-Dyno/Content/AudioArchives/Porsche918Spyder.toml", &Error);
Frontier::AcousticIntegrator Powertrain(Car);                                 // synthesis; thread-agnostic Render()
Audio.Open({ 48000 /*[Hz]*/, 2 /*channels*/, 256 /*[frames] period*/ });     // null backend when no device
Audio.Attach(Powertrain);                                                     // callback → Powertrain.Render(out, frames)
while (running)
{
    Pull.Advance(Δτ);                                                         // DynoSequence → rpm, throttle, gear
    Powertrain.AssignDemand(Pull.QueryRecord());                              // main thread → seqlock'd latest PowertrainRecord
    Audio.Advance(Δτ);                                                        // drains AudioMetrics, device-loss reopen, hot-plug
}
```

`PowertrainRecord { Rpm [rpm], Throttle [-], Load [-], Gear, ClutchEngaged, Boost [bar] }` is the only thing a game
ever feeds. Physics will produce it; the dyno scripts it. Merge into Project-Zero = construct, `Open`, `Advance(Δτ)`.

Realtime rules: zero allocations after `Open` (everything pre-sized), denormals flushed, no locks — demand crosses
threads through a seqlock (latest wins); the integrator advances on a **fixed 1 ms control substep** inside `Render`
regardless of the device's period size (same reasoning as `MotionIntegrator`'s 1/240 s substep), so `--render` output
is **bit-identical** to what the device plays for the same timeline, whatever period WASAPI actually grants.

## 2. Why the earlier attempts sounded "digital", and the design that doesn't

| Digital tell | Cause | This design |
|---|---|---|
| Buzzy, organ-like | perfectly periodic pulses | cycle-to-cycle variation: ±3–8 % amplitude, ±0.5–1.5° timing per firing; random misfires on overrun / fuel cut |
| Fizz / warble tracking rpm | pulse onsets quantised to samples; naive saw/click pulses alias | **analytic pulse kernels at exact sub-sample time** — `p(t) = A·(t/τ)^k·e^{k(1−t/τ)}` (smooth ⇒ band-limited by construction); a faster second kernel scaled by load = the WOT "bark" |
| Static formants | fixed EQ | **digital waveguides** for intake + exhaust: primaries → collector junction → cat/muffler resonators → tailpipe; `c = 20.05·√T` follows gas temperature (≈ 560–690 m/s hot, rising with load), fractional-delay interpolated |
| No texture | pure tonal synthesis | turbulent **discharge noise gated by the pulse envelope** and comb-filtered by the tailpipe (this *is* the rasp); valvetrain ticks (2/cyl/cycle, jittered); injector clicks; order-locked gear/chain whine |
| Instant, weightless response | rpm slewed by a constant | rotational inertia `J·ω̇ = τ_engine(rpm, throttle) − τ_load`; light flywheel on the 918, turbo lag on the GT-R, rev-hang |
| Same sound at all loads | one amplitude knob | pulse amplitude **and shape** follow load; overrun = weak combustion + unburnt-fuel **pops** (random impulses into the hot tailpipe with decay — the crackle) |
| Flat, centred | mono | banks to L/R (the 918's top pipes are literally left/right), listener presets trackside / cockpit / chase |
| Clean at 130 dB SPL | linear mix | mild `tanh` saturation on the exhaust bus — tailpipe pressure is nonlinear; then a limiter |

Signal chain (per vehicle, ≈ 100–200 flop/sample — < 1 % of a core at 48 kHz):

```
crank clock θ(t) ─► per-cylinder events at exact firing angles (bank angle + crank pins + firing order; uneven intervals kept)
   ├─ combustion → exhaust-valve blowdown pulse ─► EXHAUST: bank primaries ─► collector ─► [turbine] ─► cat/muffler ─► tailpipe ─► radiation HP
   ├─ intake-valve suction pulse (inverted, softer) ─► INDUCTION: runner + plenum Helmholtz (honk ∝ throttle) + throttle-body discharge noise
   ├─ MECHANICAL bed: valvetrain, injectors, chain/gear mesh orders, DCT whine (GT-R), inverter carrier (918 / LaFerrari hybrids)
   ├─ FORCED INDUCTION (GT-R): shaft-speed integrator (first-order lag) → compressor whine harmonics, wastegate flutter, recirculation hiss on lift
   └─ BODY: 2–3 low resonators (60–120 Hz cabin/panel boom) ─► listener mix ─► soft-clip ─► limiter ─► stereo out
```

DSP written once in plain `double` math with a shared xorshift PRNG and fixed operation order → the AudioWorklet port
(§4) agrees with the C++ at sample level (transcendentals aside); order-diagram agreement ±0.1 dB is the identity proof.
Reference designs (approach, not code): AngeTheGreat's open-source engine-sim (combustion + exhaust waveguides — proof
this class of synthesis sounds real); Baldan, Delle Monache & Rocchesso, *Physically informed car engine sound synthesis* (2015).

## 3. The three vehicles (physical starting points — ≈ means tune / verify against recordings)

Four-stroke: firing frequency = rpm/60 × N/2, so the dominant "engine order" is N/2.

| | Porsche 918 Spyder | Ferrari LaFerrari | Nissan GT-R Nismo (R35) |
|---|---|---|---|
| Engine | 4,593 cc 90° V8, **flat-plane crank**, NA, dry sump | F140FE 6,262 cc **65° V12**, NA, 13.5:1 | VR38DETT 3,799 cc 60° V6, **twin IHI turbos** |
| Bore × stroke | 95 × 81 mm | 94 × 75.2 mm | 95.5 × 88.4 mm |
| Peak / redline | 608 PS @ 8,700 · **9,150 rpm** | 800 PS @ 9,000 · **9,250 rpm** | 600 PS @ 6,800 · 7,000 (cut ≈ 7,100) |
| Firing | even 90°, banks alternate L-R ⇒ each bank = an inline-4 (2nd order per bank, 4th overall); no half-orders ⇒ no burble, pure wail | even 60° (crank-pin offset compensates 65°), banks alternate ⇒ each bank = an inline-6 (3rd per bank, 6th overall); F140 order per Enzo/599 manuals 1-12-5-8-3-10-6-7-2-11-4-9 | 1-2-3-4-5-6 with Nissan numbering (R 1-3-5 / L 2-4-6 ⇒ banks alternate, even 120°); 3rd order overall, 1.5 per bank |
| Dominant order at peak | 4th: 580 Hz @ 8,700 | 6th: 900 Hz @ 9,000 | 3rd: 340 Hz @ 6,800 |
| Exhaust path | **top pipes** behind the heads: total ≈ 0.8–1 m, primaries ≈ 0.35 m ⇒ almost no low-pass, extremely bright, F1-like | equal-length hydroformed headers ≈ 0.8 m, 6-2-1 per bank, total ≈ 2.8 m ⇒ very clean harmonic stack | bank → turbine → downpipe → merge → twin rear mufflers, total ≈ 3.8 m, Nismo titanium ⇒ turbine low-passes the pulses, hollow drone 3–4 k |
| Induction character | moderate; short trumpets | **dominant at high rpm** — plenums right behind the cabin, variable-length runners (honk shifts with rpm) | muffled; compressor whine + recirculation hiss on lift are the signature |
| Idle | ≈ 900–1,000 rpm | ≈ 850–1,000 rpm | ≈ 650 rpm |
| Extras | light flywheel ⇒ very fast rev response; sharp lift-off crackles; E-Power: engine off, e-motor/inverter whine, engine cuts in on demand with a start clatter | HY-KERS assist (faint inverter under the V12); induction resonance shift with runner length | GR6 dual-clutch transaxle whine + clunks; turbo shaft ≈ 150 k rpm ⇒ whistle 4–12 kHz; wastegate flutter; boost ≈ 1.0–1.1 bar |
| Proposed order | **1st** — cleanest physics (two I4 banks), exposes every pulse-shape flaw; proves the core | **2nd** — adds 12-cyl bank angle, induction dominance, harmonic purity | **3rd** — adds the whole forced-induction + DCT layer on top of a proven core |

(You listed the GT-R first; I recommend 918 → LaFerrari → GT-R because the NA cars are the hardest to fake and the
turbo layer is purely additive. Your call — §6 Q4.)

## 4. Sequence 2 — AudioEditor (`Tools/AudioEditor/index.html`)

Single file, zero dependencies, opens from disk by double-click (worklet inlined as a Blob module, so no server needed;
the sandbox serves it for the live preview). Frontier tokens (Inter, blue/black/white/dark-grey from the Frontier lobby
mock; components per Slate `References/UIComponents.html`). Two feeds, same views:

* **File feed** — drag-drop a `--render` WAV from Project-Dyno (+ its TOML) → this visualises *the engine's own output*.
* **Live feed** — the AudioWorklet port of `AcousticIntegrator` runs the same TOML in the browser → hear edits instantly;
  export TOML → C++ loads it → identity proof. (A localhost WebSocket feed straight from the running C++ is Phase B — §6 Q5.)

Views:
1. **Dyno strip** — car picker, throttle slider, RPM dial (our gauge), scripted pulls (idle · sweep · WOT pull · overrun ·
   gear blips), record → WAV, listener preset.
2. **Signal-flow diagram** — the chain in §2 for the selected car, live level meters on every edge; click a block → its
   TOML fields in the inspector. This is the "visualise the audio we are creating" surface.
3. **Scopes** — oscilloscope **crank-locked** (triggered on cylinder-1 TDC so one cycle stands still and the pulse shape is
   readable); log spectrum with engine-order markers (0.5, 1, 1.5, 2 … × rpm); spectrogram; **order diagram** (level of each
   order vs rpm — the standard NVH view for matching a synth to a recording).
4. **Reference lane** — drop a recording (mp3/wav), aligned spectrogram + order diagram over the synth's.
5. **Device panel** — sample rate, period, worklet load %, glitches: mirrors `AudioMetrics` field-for-field so the Phase B
   WebSocket feed is a drop-in.

## 5. Rows (one commit each on `arena/01a06c54-slate`; proofs + hardware acceptance + ⚠️ deviations per row)

| Row | Scope | Sandbox proof | Your acceptance (Windows, GTX 1060 box) |
|---|---|---|---|
| **A1** transport | `.gitmodules` pins, `MiniaudioTranslation.cpp`, `AudioExchange` (open/close, callback, seqlock demand, `AudioMetrics`, device-loss reopen, null backend), `WaveCodec`, `Project-Dyno` skeleton + both `ToolchainSequence` scripts + CMake target; plays a sine sweep and a crank-locked click train at scripted rpm | builds with g++ 12 `-Wall -Wextra` clean; null backend opens; `--render` writes a 5 s sweep + click train; click spacing = 60/(rpm·N/2) within one sample | hear sweep + clicks; callback max < 500 µs, zero underruns over 60 s (printed by `AudioMetrics` on exit) |
| **A2** synthesis skeleton | `AcousticStructure` TOML load/save, `AcousticIntegrator` v1 (crank clock, analytic pulses with jitter, exhaust + induction waveguides, discharge noise, mechanical bed, inertia), `DynoSequence` pulls, three first-cut TOMLs | offline WOT pulls for all three cars → `Scratchpad/*.log` + spectrogram/order PNGs (C++ harness, radix-2 FFT, `stb_image_write`); dominant order = N/2 within 1 Hz; silent-pulse aliasing guard: nothing above −90 dB past 16 kHz | first listen: "an engine, wrong car" is the bar |
| **A3** editor design | static HTML: all five views with mock feeds, tokens, layout, resize | screenshots `Diagnostics/AudioEditor_01…_ContactSheet.png` | look approved before wiring |
| **A4** editor live | worklet port, scopes, order diagram, TOML import/export, file feed, reference lane, WAV record | worklet renders the A2 pull; order diagram matches the C++ render within ±0.1 dB per order | audible in the live preview |
| **A5** car 1 (918) | tune in editor → back-port constants/DSP deltas to C++ → identity proof | C++ vs worklet ±0.1 dB per order; reference-lane match report | you sign off by ear |
| **A6** car 2 (LaFerrari) | same loop; adds runner-length resonance shift, induction dominance | same | same |
| **A7** car 3 (GT-R) | same loop; adds turbo shaft integrator, whine, wastegate, recirculation, DCT orders | same | same |
| **A8** phase note | `References/AcousticPhaseA.md`, per-car parameter sheets, Phase B scope (merge into Project-Zero, placement, doppler, tyres, cabin, `[audio]` page, WebSocket feed) | — | — |

Sandbox limits (do not re-discover): no sound device (`/dev/snd` absent), no cmake, no numpy/ffmpeg — proofs are
g++-built harnesses writing WAV/PNG/log; WAVs are git-ignored (regenerate with `--render`), PNG + log are committed.

## 6. Decisions needed before "go A1"

1. **Names** — `Project-Dyno`; `AudioExchange` / `WaveCodec` / `AcousticStructure` / `AcousticIntegrator` / `DynoSequence`
   / `PowertrainRecord` / `AudioMetrics`. Alternatives welcome; §7 vocabulary otherwise applies.
2. **Car TOML home** — `Projects/Project-Dyno/Content/AudioArchives/` (copied into the game at merge) **or**
   `EngineContent/AudioArchives/<Car>/<Car>.toml` mirroring `FontArchives` (shared by Dyno + game, nothing moves at merge).
   I lean EngineContent.
3. **Car order** — 918 → LaFerrari → GT-R (recommended) or GT-R first as you listed.
4. **Editor feeds** — file + worklet now (as written), C++ WebSocket feed in Phase B. Or pull the WebSocket feed into A4
   (+1 row: `TelemetryInterchange`, ~200 lines: handshake + framing, no library).
5. **Reference recordings** — will you supply short clips per car (idle / steady 4 k / WOT pull / overrun), or do we judge by
   ear + order-signature checks only?
6. **miniaudio pin** — plan pins `9634bedb` (v0.11.25) per CLAUDE.md; if your local `ExternalPackages\miniaudio` is another
   version, A1 pins yours instead (`git -C ExternalPackages\miniaudio rev-parse HEAD`).

Defaults if you just say **"go A1"**: names as listed, `EngineContent/AudioArchives/`, 918 first, WebSocket in Phase B,
judge by ear + order signatures until clips arrive, pin `9634bedb`.

## 7. Vocabulary (banned-word substitutions — DSP is a minefield for CLAUDE.md §3)

`Filter` → `Resonator` / `BiquadSection` / `OnePoleLag` · `Buffer` → `Ring` / `Extent` · `Source` → `Generator` / `Emitter` ·
`Flow` → `Discharge` (exhaust) / `Induction` (intake) · `Blend` → `Mix` · `Map` (Campbell map) → `OrderDiagram` · `Table` →
`Sheet` · `Node` (junction) → `Junction` · `Pipeline` → `Chain` · `History` → `Trace` · `Bridge` → `Feed` · `Tick` →
`Advance(Δτ)` (existing convention). Units annotated `[Hz] [rpm] [m] [s] [Pa] [bar] [-]`; Allman, 4 spaces, 142/122 banners.
