# Materials gallery — shaderball sheet

`ShaderballSheet_GlassClothCoat.png` (1544×512): the CC0 shaderball path-traced on the CPU with the
proven `MaterialEvaluation.slang` BSDF — thin-wall glass (rough 0.06, η 1.5), deep-red velvet cloth
(fuzz 0.65), clearcoat car paint — under a 3-softbox studio rig. 256 spp/panel, BSDF sampling + NEE
with power-heuristic MIS, ACES + gamma 2.2. Deterministic: re-running the driver below reproduces it
pixel-for-pixel.

- Harness: `Exhibits/Workbench/Materials/ShaderballExhibit.cpp` · driver: `RunShaderballExhibit.sh`
  (smoke + full render; not part of the materials gate).
- Mesh: `shaderball.obj` (15,554 tris after quad split) + `shaderball-CC0-LICENSE.txt` — CC0 1.0
  Universal, Pseudopode/UnityShaderBall, credited with thanks.
- Kept-sheet values (linear means): glass 0.1913 · cloth 0.1509 · coat 0.1434 · 0 non-finite pixels.
- `sha256 2d6ddb48…9e66e2` (2026-09-16 re-render after the below-horizon mixture polish: cloth/coat panels
  bit-identical, 0.14 % of glass pixels shifted by the recovered paths; re-rendered again for M4b — bit-identical).

`ShaderballSheet_SolidGlass.png` (1028×512): thin-vs-solid diptych — the same clear glass as the triptych's
glass panel, once as foil and once traversed as solid glass (M4b medium tracking: true enter/exit + Beer +
TIR). Rendered with `RunShaderballExhibit.sh 512 256 solid` (or `both` for both sheets).

- Kept-sheet values (linear means): thin 0.1913 · solid 0.1886 · 0 non-finite pixels.
- `sha256 b432c15389b62552faa4b523bedddfda062c69956c0073d2a7c74bfb3dcb20ac` (2026-09-16, new for M4b).

## Denoiser sheets — M9 (2026-09-17)

Seven sheets from `bash Exhibits/Workbench/Materials/RunDenoiseExhibit.sh` (defaults: 192px tiles, 2048 spp
reference). They are rendered by `DenoiseExhibit.cpp`, which drives the **shipped** à-trous filter — the real
`Engine/Shaders/AtrousDenoise.slang` compiled 1:1 as C++ and called through its own `main()` — over a CPU
path-traced scene, and the **shipped** reprojection rule (`ReprojectionMirror.h`, the same header the materials
gate's §D checks). Deterministic: same seeds, same pixels, every run; re-running the driver reproduces every sheet
bit-for-bit.

Kept-sheet hashes (2026-09-17, driver defaults): `NoiseAndEdges 877776c6…` · `IdentityAtConvergence caa61b8f…` · `FadeOut 8e3d8964…` · `AtrasLevels 92845f34…` · `EdgeStops 278e6a2c…` · `Reprojection 0663cb06…` · `StreamAB 7fa51d1b…`.

The radiance is a real one-bounce path trace (checker ground, two spheres, wall, soft area light) accumulated by
the shader's own recursion (`ResolveSurface`'s running mean + first two luminance moments), so its per-sample
noise — flat-region speckle, contact shadow, colour bleed, glossy fireflies — is the scene's, not a model of it.
The integrator is **not** the ReSTIR kernel (no reservoirs, no reuse, no BVH, one bounce): these sheets are
evidence about the filter and the reprojection rule, which is what M9 set out to prove. The GPU-side end-to-end
A/B remains on the render-verification backlog.

| Sheet | Size | What it shows |
|---|---|---|
| `DenoiseSheet_NoiseAndEdges.png` | 624×524 | the A/B in one frame: reference (2048 spp) · raw 1 spp · filtered 1 spp, then \|raw − reference\| vs \|filtered − reference\| (each auto-exposed, gain printed) and the variance-of-the-mean the filter reads (log blue→red). Mean \|error\| vs reference: 0.1170 raw → 0.0295 filtered, **74.8 % removed**. |
| `DenoiseSheet_IdentityAtConvergence.png` | 624×480 | the other half of the A/B at 2048 spp: filtered vs unfiltered differ by 0 on **4566 of 36864 surface pixels** — every pixel the shipped early-out accepted — with a 4× crop of the worst difference and the early-out mask. |
| `DenoiseSheet_FadeOut.png` | 828×1012 | raw / filtered / \|filtered − raw\| ×16 / early-out mask at 1, 16, 128 and 2048 spp: the filter's fade-out measured on a real frame — 0 % → 2 % → 5 % → 12 % of surface pixels taken out of its hands as the estimate settles. |
| `DenoiseSheet_AtrasLevels.png` | 624×480 | one noisy 1-spp frame after 0, 1, 2, 3, 4 and 5 à-trous levels (tap step 1, 2, 4, 8, 16 px): the shipped chain's own progression, including the coarse blotching the widest levels trade speckle for. |
| `DenoiseSheet_EdgeStops.png` | 828×305 | the three edge-stopping terms, on/off, same input and same chain, with a 4× crop on the deepest depth edge: with the engine's σn/σz/σl the silhouette holds and the checker stays sharp; flattened, the ball bleeds into the wall. |
| `DenoiseSheet_Reprojection.png` | 624×480 | a 64-frame camera pan (0.012 m/frame, 4 spp/frame): pre-R7a same-pixel history vs R7a reprojection, the R2 motion vectors, the disocclusion map (25.2 % of surface pixels rejected at least once after frame 0) and the filtered R7a accumulation. |
| `DenoiseSheet_StreamAB.png` | 948×669 | the §E statistics as bars — presentation MSE at 1 spp (92 % / 69 % / 91 % lower filtered on lambertian / glass-BTDF / subsurface) and the early-out acceptance curve per hold (512 / 2048 / 8192), drawn from the *same* `MeasureStream` the gate's E1–E4c checks call. |

Headline numbers, all reproducible from the driver: raw 1-spp frame error to reference falls 74.8 %; the
firefly-heavy glass stream is still being filtered at 512 and 2048 spp because it has not converged there (36×
the diffuse per-sample variance) and only past the 8192-sample hold does the shader's own test take over 79 % of
the frame; and at every hold, every accepted pixel is returned bit-identical — 0 differing pixels in all nine
cells of the gate's E4.

## Material library level — the product's own scene, CPU-rendered (2026-09-17)

`MaterialLibrary_View.png` (960×540), `MaterialLibrary_GlassRow.png`, `MaterialLibrary_Specials.png` and
`MaterialLibrary_Wide.png` (480×270): the M10 level (`--scene materials`) drawn by Project-Zero's own CPU stack — the
engine's level builder and material records, the shipped `MaterialEvaluation.slang` BSDF compiled 1:1 as C++, the
engine's atmosphere core at the product's 17.93 h staging, the `GameExecution` materials camera and the engine's
single tone map (`ColourTransfer.h`, ACES, exposure 1.05). Not the ReSTIR kernel: a CPU render affords the samples the
GPU cannot, so these are the converged images ReSTIR + the M9 denoiser estimate. Physics for physics the two agree —
same shader text, same lights, same radiance.

- Harness: `Projects/Project-Zero/Host/MaterialLevelViewport.cpp` (Makefile target `MaterialLevelViewport`) ·
  driver: `Exhibits/Workbench/Materials/RunMaterialLibraryViewport.sh [fast|full]` (builds, renders, gates on
  non-finite samples and a plausible film mean, prints the sha256 of each sheet).
- Deterministic: fixed per (pixel, sample) seeds, no time-dependent state; re-running the driver reproduces each sheet
  bit-for-bit. Kept-sheet hashes (2026-09-17, driver defaults): `View 9761cdfb…` · `GlassRow 7a4ea7b9…` ·
  `Specials fcf22e2d…` · `Wide 44007e3e…`, film means 1.65 / 1.34 / 1.62 / 1.30, 0 non-finite samples in all four.
- Deliberately absent: the sky core's panel post (vignette/flare), the fog march (the level's scenario is Clear), and
  textures (the level is constants-only by design — see the M10 section of the proofs report). Below the sky horizon
  the atmosphere's dark planet ground shows, exactly as the engine returns it.

## ReSTIR sheet — the shipped kernel's direct block, CPU-simulated (2026-09-17)

`RestirSheet_StandardTier.png` (660×1040): six panels of the M10 level answering the question "what would I see if I
ran this on my GPU" without one. The Vulkan build draws the level with `Engine/Shaders/ReSTIRViewport.slang`
(bindless tables, a BVH in buffers, storage images); the CPU viewport mirrors that kernel line for line — RIS
candidates, temporal and spatial reservoir reuse, visibility re-traced at the shading pixel, and the running mean in
`ResolveSurface` — constants included (25°/10 % validation, the 20× M clamp, the sun-pick probability, radius 4–16 px
scaled by width/1280).

| panel | what it is | RMSE vs ① (display space) |
|---|---|---|
| ① reference | brute force, 192 spp | — |
| ② brute force | 4 spp × 16 frames (the same 64 samples) | 1268.26 (0.0194) |
| ③ ReSTIR | Standard tier: 4 candidates × 16 frames, 2 spatial taps | 6696.39 (0.1022) |
| ④ ReSTIR + à-trous | the product's actual pipeline (R7 filter over the R6 reuse) | 5441.13 (0.0830) |
| ⑤ + camera excursion | ±0.70 m triangular pan, out and back; R7a reprojection on | 8828.22 (0.1347) |
| ⑥ the same, pre-R7a | both history reads at the pixel's own address | 10701.2 (0.1633) |

⚠️ **③ and ④ are not comparable with the pre-fix sheet** (2308.00 / 2272.37): the mirror now shades **one sample per
pixel per frame**, which is what the kernel's DI block does — the earlier mirror shaded once per *sample*, i.e. four
times per frame at `--spp 4`, so it was delivering 4× the information the shader delivers. The convergence claims for
the two fixed paths are argued from the taps ordering, the history-split A/B, and the bounded M (§14 of the proofs
report), not from a before/after RMSE across two different budgets. ⑤/⑥ are the excursion A/B and are within one
build: the reprojection still wins by 17 % on the closing frame.

- **The R7a A/B is the headline**: the closing frame of a ±0.70 m excursion sits back on the base pose, and the
  reprojected running mean gets there 36 % closer to the reference than the same-pixel read (6545 vs 10187), whose
  spheres are smeared into streaks. The film reports the mechanism per frame: ⑤ reprojects 98.0 % of surface pixels
  with 2.0 % disocclusion restarts and keeps 57 of its 64 samples; ⑥ reprojects 0 % and keeps 64 samples it never
  should have.
- **The spatial-reuse stall is fixed** (2026-09-17, §14 of the proofs report). Two faults, both in the merge algebra:
  the history the next frame's temporal pass read was the *post-spatial* reservoir, so a frame's spatial merges kept
  raising the M the next frame's temporal cap reasoned about (mean M 1 879 / max 4 436 at frame 16, 843 633 by frame
  20); and a tap's M cap was taken against a count the tap loop was itself growing, so tap 2 could add up to 20× what
  tap 1 had just added. With the two-dispatch split and the pre-merge cap, more taps is now strictly better at every
  budget — see the convergence sheet below.
- **One measured caveat, stated plainly**: at equal *ray* budgets ③ is still noisier than ② at these frame counts
  (240×135, N=32/64/128: 7 462 / 7 163 / 7 599 for 4 candidates + reuse against 1 687.63 / 1 461.05 / 1 460.94 for
  4 spp brute force). The causes are structural and measured, not guessed: the ReSTIR arm resolves **one** shaded
  sample per pixel per frame where the brute-force arm resolves four, ~12.7 % of shaded selections fail their
  visibility re-trace and contribute nothing, and the reuse arm's error is flat from 32 → 128 frames (7 387 → 7 223 →
  7 632), i.e. what is left is a plateau, not variance more frames can average away.
- Harness: `Projects/Project-Zero/Host/MaterialLevelViewport.cpp` · driver:
  `Exhibits/Workbench/Materials/RunRestirViewport.sh [fast|full]` (builds, renders, gates on non-finite samples,
  prints the RMSE table and the sheet's sha256).
- The plain path tracer is **kept on purpose** and is not superseded by the `--restir` mirror: it is the reference
  oracle every ReSTIR and denoiser number here is measured against (the ① panels), it is the bit-stability floor
  (AE = 1 on the 480×270 wide render), and it stays available for later cross-checks against the GPU frame.
- Kept-sheet hash (2026-09-17, `full`, post-fix): `22454e15…`. Deterministic — per (pixel, sample, frame) seeds, no
  time-dependent state.

## Convergence sheet — the two reuse paths, before and after (regenerated 2026-09-18)

`RestirConvergenceSheet.png`: eight cells of the same level at the same budget, one switch apart — the fix-evidence
companion to the product sheet above, and the A/B for both faults in §14 of the proofs report. ⚠️ The numbers below are
from the 2026-09-18 regeneration, which is the first with **D10's identity validation on by default**: every ReSTIR
figure dated 2026-09-17 in this README and in §14 of the report was measured before that rule existed and is kept as
history, not as the current reading. The direction of every comparison is unchanged; the absolute values moved.

| panel | what it is | RMSE vs ① (display space) |
|---|---|---|
| ① reference | brute force, 512 spp, one frame | — |
| ② brute force | 4 spp × 128 frames (the same 512 samples per pixel) | **0** — pixel-identical to ①, the sanity gate still holds |
| ③ ReSTIR, 0 taps | spatial reuse off — the arm that used to be the only one that converged | 6428.44 (0.0981) |
| ④ ReSTIR, 2 taps | the Standard tier's own tap count | **6181.33 (0.0943)** |
| ⑤ ReSTIR, 4 taps | spatial reuse with the fix in | **6123.37 (0.0934)** |
| ⑥ split OFF | ④ with `--restir-no-history-split` — spatial feeds temporal again | 6309.43 (0.0963) |
| ⑦ indirect, no pool | ④ with `--restir-no-gi-reuse` — the pre-pool single-sample arm | 6087.87 (0.0929) |
| ⑧ indirect pool on | ④ — ReSTIR GI-style reuse of the first-bounce vertex's NEE stratum | **6181.33 (0.0943)** |

**⑨ The floor (roadmap #7).** Everything above is against ① on the *same* seed stream — deliberate, since that is what
makes ② ≡ ① a gate. It also means each figure is a lower bound: a shared stream subtracts the noise the two arms have
in common. The sheet now re-renders the two arms that carry the headline claim on an independent stream and prints both
columns, so the size of that flattery is measured instead of estimated:

| arm | shared stream | independent stream |
|---|---|---|
| plain, 4 spp × 128 frames | 0 (identical to ① by construction) | **760.03** |
| ReSTIR, 4 candidates × 128 frames, 2 taps | 6181.33 | **6078.44** |

- **More frames are not the lever** (roadmap #2, report §14.4): the same arm soaked to 1 000 frames is flat past ~250
  frames — mean M 58.9 / max M 84, unchanged since frame 25 — while the plain arm keeps improving over the same span
  (1 017.76 at 62 frames → 734.05 at 250, −28 %). The pre-merge clamp that fixed the compounding growth is also the
  floor: with M saturating near 60 the temporal filter's memory is ~M frames, and more frames resample the clamped
  weights instead of adding information. Past this point accuracy comes from taps, candidates and indirect coverage —
  not from the clock (`Exhibits/Workbench/Materials/CheckRestirSoak.sh`).

- ② and ① are the *same* estimator sampled the same way — 4 spp × 128 frames walks exactly the 512 seeds of the
  one-frame reference — so the pair is bit-identical and doubles as the harness's own sanity gate.
- Per-frame M at frame 128 (all 240×135): ③ 63.1 (max 84) with the shaded reservoir unchanged at 63.1, ④/⑧ 63.1 with
  the shaded 178.4, ⑥ 71.3 (max 84) — the old loop runs hotter than the split, and ⑦'s direct half is identical to ⑧'s
  (the pool switch does not touch it). The pool reports on 16.0 % of surface pixels with shaded M 13.1 and 24.5 % of its
  selections occluded.

- **Read ③④⑤ left to right**: before the fix this read *backwards* (0 taps converged to 1952.52 by 128 frames while 2
  taps stalled at 4437.03 and 4 at 4660.38). It now reads forwards at every budget, and M stays bounded (mean 41.7 /
  max 64 at frame 16 → 50.4 / 84 at frame 32).
- **⑥ is why the fix is not a matter of taste**: with the history carrying the post-spatial reservoir again, M
  saturates at the clamp — 69.7 mean / 84 max by frame 8 — and stays there with 9.4 % occluded selections, against
  12.5–12.7 % for the split. The arms separate by RMSE 2 110.48 at frame 16; the first attempt at this A/B printed
  *byte-identical* PNGs because an end-of-frame buffer swap was overwriting the choice (see §14.1) — the tell that
  turned a "no difference" reading into a found bug.
- **⑧ is the indirect half's own reuse**: the first-bounce vertex's NEE stratum gets a reservoir of its own (RIS
  candidates at the vertex, temporal merge, the same taps, one visibility re-trace), and the pixel's indirect half
  partitions around it so nothing is double counted — the vertex's terminal cases (sky, emitter) are stored, and the
  deeper path continues from the vertex with the same BSDF sample. The pool exists on ~16 % of surface pixels (the
  share whose primary BSDF sample hits geometry; 41 % escape to the sky), which is why the gain is small but
  consistent: 7 387 vs 7 508 at 32 frames, 7 223 vs 7 295 at 64, 7 632 vs 7 680 at 128.
- **Kernel side (R11)**: both fixes and the pool are in `Engine/Shaders/ReSTIRViewport.slang`. The pool is
  `kFeatureGiReuse` (bit 8) — **ON by default**, with the Control Centre's *Indirect reuse (GI pool)* checkbox, and
  it rides a second 64 B/px reservoir pair at bindings **25/26** (the bindless table moved 25 → 27,
  `kComputeBindingCount` 26 → 28; the host zero-fills and ping-pongs them with the DI pair). No vertex buffer was
  needed: the receiving vertex is in registers (one dispatch) and a neighbour's *validity* is carried by M > 0.
  GPU verification is still pending — nothing here compiles SPIR-V — so those numbers remain the mirror's.
- **Dials for "more ray tracing, less ReSTIR"**: render scale (config `render_scale`, 25–100 % — more traced pixels),
  *Candidates / px* (1–32) and *Extra candidates* (0–8) up, *Spatial taps* (0–4, new slider) down, and the *Indirect
  reuse (GI pool)* checkbox off for the indirect half. Tiers: Minimal 0.5×/1 cand/0 taps · Economy 0.75×/2/1 ·
  Standard 1.0×/4/2 · High 1.0×/8/3 · Ultra 1.0×/16/4 (`FidelityClassifier`).
- **Where brute force still wins (measured, not spun)**: at the same one-sample-per-frame resolve rate, plain path
  tracing lands **1 181.57** from the 512-spp reference while the ReSTIR arm lands **5 991.04** — **5.1× further**, both
  on an INDEPENDENT seed stream. Earlier printings said 7.8× against 971.89 vs 7 553.24; those figures compared the
  arms against a reference sharing the plain arm's own seeds, which understated plain's error by 21.6 % (and slightly
  *overstated* ReSTIR's, by 2.2 % — the correlation's effect is per-arm, not a uniform discount). The direction never
  changed; the factor is now the honest one. Why, and what would change it (16 % indirect coverage, occluded-selection
  weight loss, sun-coin variance, replay + shift mapping): report §14.3.
- Harness: `Exhibits/Workbench/Materials/RunRestirConvergence.sh [fast|full]` (builds, renders, gates on non-finite
  samples, prints the RMSE table, the ⑨ floor pair and the sheet's sha256). Kept-sheet hash (2026-09-18): `b61e528e…`.
