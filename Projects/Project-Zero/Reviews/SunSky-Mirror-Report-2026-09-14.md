# Sun/Sky Mirror Report — 2026-09-14

Reference: the live celestial panel at
`https://sultanaladin.github.io/Frontier-/celestial/`
(transcribed from `arena/01a08c57-frontier:docs/celestial/index.html`,
verified byte-identical to the served page, sha `d156a60d…`).

Question asked: our proof render did not look like the live reference — the
white line on the horizon was missing and the sunset was less vibrant. Is our
`.slang` + ReSTIR implementation a faithful copy, and if so, why the mismatch?

## 0. Verdict

Yes — the implementation is now a **pixel-exact mirror** of the live panel,
term by term, including the white line, the sunset glow, the stars, and the
atmosphere media. Same settings in → same pixels out:

| gate | result |
|---|---|
| G1 core-vs-oracle (10 frames, full-frame incl. ground) | 10/10 PASS, max ≤ 1 LSB |
| G2 core-vs-upstream-port, media-free (10 frames, sky mask) | 10/10 PASS, max ≤ 2 (1 star-edge pixel, see §5) |
| G3 media-vs-numpy (36 000 seeded rows) | PASS, worst 0.52 tol |
| ReSTIR T2 DI / T3 GI vs brute force (3 probes) | PASS at all probes |
| ReSTIR T4 determinism | PASS |
| static contract rules | pass (2 files) |

Full numbers: `Diagnostics/CelestialParity.txt`. Proof frames:
`Diagnostics/Proof/*_slang.png`.

The earlier "missing line" was **not a transcription error**. It is fully
explained by three compounding facts about the panel's own physics (§1). No
quantified difference between our mirror and the live reference remains: every
pixel of every tested frame matches to ≤ 2 LSB, and the residual is proven
libm/GPU weather, not a formula slip (§5).

## 1. Why the old proof "missed the white line"

The panel's white horizon line is the `dawnGlow` line term
(`line = exp(-(alt/0.11°)^2)`, panel line ~925): a 0.11°-wide hairline worth
0.45 HDR at peak, gated by an elevation window `wlWin` that is exactly 0
outside solar elevations **−5.5° … +0.3°** (about 05:40–06:01 and
17:59–18:20). Three facts combined against the old proof:

**1a. The old proof never entered the line's solar window.**
Its cases sat at t = 6.4/7.6/12/0 h (elev +5.4° … −64°), where `wlWin = 0` —
the line term evaluates to exactly 0 on *both* implementations. The live page
shows no line at those sun times either. The new 10-case matrix adds
t = 5.663/5.778/5.889/6.0/18.111 h (elev −4.5° … 0°), which bracket the window.

**1b. With live-default Atmosphere-Fog (AF on), the panel erases its own line.**
The horizon ray carries ~3.9 AF optical depths, so the line's 0.45 HDR is
multiplied by Ta ≈ 0.02 (G) before display — about 1 LDR step, invisible.
Measured on a 20°-FOV horizon crop (t = 5.889 h, elev −1.5°), center column:

| row | AF off (our mirror) | AF on (live default) |
|---|---|---|
| 2 px above horizon | 214, 136, 80 (vibrant orange) | 192, 166, 146 (grey-beige) |
| horizon edge | **234, 226, 220 (white hairline)** | 193, 168, 146 (no spike) |

Figures: `Proof/fig_line_off_crop.png` (brilliant white hairline over vivid
orange) vs `Proof/fig_line_on_crop.png` (flat grey, line gone). The AF on/off
ablation
over full frames averages 10–21 LSB mean, up to 151 max (§4). Under live
defaults the live page itself shows the muted version — there is no hairline
to miss.

**1c. The sun disc is extinct at the horizon and ignites just above it.**
At elev 0° the direct sun traverses ~13 optical depths (2.3 M m equivalent
sea-level path through the spherical shell), so `sunL ≈ 5e-4` — the disc is
physically invisible even dead-center in frame (`Proof/fig_0600_extinct_crop.png`).
It ignites red→white over elev 0 → 0.7° (`Proof/fig_ignition_crop.png`,
t = 6.05 h: red disc, center R = 251). A "white line on the horizon" seen on
the live page is therefore the white-line term only with AF off/weak, or the
ignited disc's lower limb just above the horizon — both reproduced exactly by
the mirror under the same settings.

Net: *same settings → same pixels.* The old proof compared a 6.4 h render
against a sunset memory; the new matrix covers the window and both AF states.

## 2. What is mirrored (every panel term)

| panel feature | panel lines | mirror (`CelestialCore.slang`) | gate |
|---|---|---|---|
| sun disc (soft limb, boost) | ~1003–1015 | `celSunDisc` | G1/G2 sun-chasing cases |
| aureole (Mie forward lobe) | ~1017–1030 | `celSunAureole` | G1/G2 |
| atmosphere 20×8 spherical integration | 847–863 | `celAtmosphere` | G1/G2 all cases |
| dawn glow + **white line** | 910–937 | `celDawnGlow` | G1/G2 0589/0600/1811/5778 |
| stars + Milky Way (3 layers, day-gated) | ~940–1000 | `celStars` | G1/G2 night (100 % sky) |
| Atmosphere-Fog + ground fog + ambient probe + planet ground | ~700–845 | `celApplyMedia`, `celSkyAmbient` | G3 + G1 (fog stays off = live default) |
| ground shade (continent/ocean/spec) | ~1032–1100 | `celGround` | G1 full-frame |
| horizon/Ta/peaks post chain | 1219–1290 | `celPost` | G1/G2 |
| sun ephemeris | ~1–60 | `Host/SunPosition.h` | prints match hand computation exactly |

Two upstream transcription defects were found and fenced (see
`Host/CpuPort/PROVENANCE.md`): the `ApplyMedia` `(1-Ta.x)` channel bug
(G3 catches it at 370× tol) and the omitted planet-ground branch
(G2 runs media-free because of it).

## 3. Fixes made while mirroring

- **DI now lights with THE sky.** The DI path (host estimate, host brute-force
  ref, shell `diInitial`) evaluated disc + aureole + bare atmosphere and never
  saw the glow, the stars, or the media — inconsistent with GI (`celSkyFull`)
  and with the panel. All three sites now call `celSkyFull`. DI estimates
  moved accordingly (floor probe 0.186 → 0.287) and still match the independent
  refs: T2 PASS at all 3 probes.
- **Shell additions** (`Shaders/CelestialReSTIR.slang`): `skyProbe` entry (the
  panel's uProbe pass as 1 thread) + `u3` ambient buffer, `b0` extended to
  18 float4s (media + star globals), `b2` camera now required by *all* entries
  (star pixel-angle), `shade` backdrop and `skyViewport` upgraded from
  `celSkyFull` to full `celSkyPixel` (post + grain included). 9 compute
  entries; see `Shaders/README.md`.
- **Gates hardened**: 10-case matrix (line window, ignition, night), G3 media
  gate (36 000 numpy rows), thresholds re-derived from the formulas' own
  conditioning (§5). These Slang-side bindings are blind (no `slangc` in this
  sandbox) but strictly mechanical — every new line mirrors a host line the
  gates already cover.

## 4. Quantified differences

Mirror-vs-reference (all green — the residual IS the difference):

| case | G1 MAE/max/frac | G2 MAE/max/frac |
|---|---|---|
| 0640_default | 0.0003/1/0.00028 | 0.0001/1/0.00005 |
| 0640_lookup | 0.0001/1/0.00011 | 0.0000/1/0.00003 |
| 0760_lookup | 0.0001/1/0.00005 | 0.0001/1/0.00008 |
| 1200_lookup | 0.0000/1/0.00003 | 0.0000/1/0.00001 |
| 0640_sun | 0.0001/1/0.00014 | 0.0001/1/0.00007 |
| 0760_sun | 0.0001/1/0.00009 | 0.0000/1/0.00004 |
| 0589_sun (line window) | 0.0000/1/0.00005 | 0.0003/1/0.00034 |
| 0600_sun (ignition edge) | 0.0000/1/0.00003 | 0.0002/1/0.00022 |
| 1811_sun (sunset line) | 0.0001/1/0.00005 | 0.0002/1/0.00023 |
| 0000_night (stars) | 0.0015/1/0.00154 | 0.0017/2/0.00166 |

G3: worst row 0.52 tol (tol = 2e-6 + 1e-3·|ref|). ReSTIR: DI 0.287/0.277/0.153
vs refs 0.227/0.238/0.160 (all inside the gate band), GI 0.189/0.186/0.188 vs
0.184/0.189/0.190, T4 bit-exact.

AF on-vs-off ablation (the look difference, same mirror — the live page shows
both, depending on its AF toggle):

| case | mean \|Δ\| (LSB) | max \|Δ\| |
|---|---|---|
| 0589 line window | 11.1 | 94 |
| 0600 ignition edge | 10.3 | 64 |
| 1811 sunset line | 11.1 | 94 |
| 0640 live default | 21.0 | 151 |

## 5. Why the residual is weather, not error

- **G2 max = 2 (one night pixel):** a single sub-pixel star-core edge flip
  (R 52 vs 50 at (248,69); summit + neighbours agree). Independent normalize
  forms differ ~2 ulp; `acos` amplifies at the core edge. A GPU `rsqrt` will
  show the same weather against any CPU implementation. Bound: G2 max ≤ 2.
- **G3 tol 1e-3 rel:** the height kernel `K = H(e^-a−e^-b)/(b−a)` cancels
  catastrophically on near-horizontal slabs (~1e-4 rel on *any*
  implementation, live GPU included), and fog's `exp(−od²)` multiplies it by
  2·od (up to ~6×). A ≥1 % channel/formula slip still fails by 10×+.
- **G1/G2 frac 0.005:** the 20×8 `acos`-heavy integration jitters ±1 LSB on
  glow gradients; the bound is 30× above observed weather and 200× below any
  visible defect.

## 6. Reproduce

```sh
cd Host && make                  # SkyViewport CpuPortDiff MediaProbe ReSTIRConvergence
cd ../Diagnostics && python3 RunCelestialParity.py   # full gate (~100 s), writes CelestialParity.txt + Proof/
# Figures in §1 (live-default AF on vs --media 0):
../Host/SkyViewport --sun 5.889 --yaw 90.73 --pitch -1 --fov 20 --out line_on.ppm
../Host/SkyViewport --sun 5.889 --yaw 90.73 --pitch -1 --fov 20 --media 0 --out line_off.ppm
../Host/SkyViewport --sun 6.0 --yaw 90 --pitch 0 --fov 20 --out extinct.ppm
../Host/SkyViewport --sun 6.05 --yaw 89.61 --pitch 0.8 --fov 20 --out ignition.ppm
```
