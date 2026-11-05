# Combined prototype: plan, provenance, results, known issues

*Branch `arena/aef49291-frontier`, app in `Experimental/VehicleHmiPrototype/`. Written 2026-10-20.*

This is the plan and the record for the single combined WebGPU prototype built from five scattered branches. The first half says what was asked, what each branch had and what was
chosen, and why. The second half says what was verified and how, what the numbers are, and (just as important) what is **not** known. How to run it is in
[README.md](README.md).

## 1. The brief as a checklist

| # | Requirement (the user's words, condensed) | Where it stands | Where it lives |
|---|---|---|---|
| 1 | **One** final app with everything; correctness and performance decide between competing implementations | Done. One app, no duplicate implementations of a feature | this directory |
| 2 | **Clustered lighting**, in two systems: over **emissive surfaces**, and over **actual lights** (point / spot / IES) | Done. Both exist, share one 16 × 16 × 4 world grid and one uniform binding | `LightClusterSpace.js/.wgsl`, `EmissiveLightSpace.js` |
| 3 | **XPBD tyre physics** ("I believe some have it already") | Done, **ported from Slate `ca517016`**. None of the five branch heads had it (see §2) | `TyreCarcassSolver.js`, `Shader/Tyre*.wgsl`, `Archive/TyreLattice.bin` |
| 4 | **ReSTIR GI** | Done (f5's triangle ReSTIR GI path tracer, with s1's screen-trace hand-off) | `RayTraversal.wgsl`, `ReservoirTemporal.wgsl`, … |
| 5 | **Ray tracing**: "there's no RTX, it's software BVH" | Done and renamed everywhere: a two-level BVH traversed in WGSL compute. UI, docs, comments and tests say BVH; `?renderer=rtx` survives only as an alias, and a test fails if a label says RTX again | `RayTracingSpace.js`, `RendererNames.js` |
| 6 | **Screen traces**, "the last missing Lumen pillar": a nearest-depth hierarchy walk first, then hand-off to the distance field | Done (s1's implementation) | `ScreenTraversal.wgsl`, `NearestDepth*.wgsl/.js` |
| 7 | **Real IES files** | Done for the roadway, area, flood and spot profiles (four measured files, source and licence recorded). The three automotive beams are still authored: none was found under a permissive licence | `Profiles/` |
| 8 | **Prototype first, C++ later**; run the tests myself; use GitHub Actions toggles when unsure which option is faster | Prototype complete. Tests run locally and on Actions. Toggle matrix built and run. C++ notes in §9 | `.github/workflows/`, `Tools/` |

## 2. The five branches, what they had, what was taken

Compared file by file against f4's tree, the closest thing to a common base (33 files are identical in all five trees), and read where they overlapped.

| Branch | Head | Distinct contribution (files not in f4) | Decision |
|---|---|---|---|
| **s1** `SultanAladin/Slate` `arena/d984ba1b-slate` | `c552cb26` | screen traces + nearest-depth hierarchy; **world light grid** (`u32` cell masks); IOR + refraction; the benchmark matrix; a `TriangleHierarchy` | **Taken**: screen traces, light grid, refraction, benchmark matrix. **Dropped**: `TriangleHierarchy` (f5's BVH supersedes it) |
| **s2** `arena/b55d93c0-slate` | `8a700498` | its own screen traces; a **reflection-only** BVH tracer; `TimingLog`; the materials garden | Nothing needed from it: its `RayTracingSpace.js` is the same TLAS + binned-SAH BLAS builder as f5's (22 of ~1060 lines differ) and its traversal is 442 lines of reflections against f5's 859 lines of multi-bounce path tracing, NEE and ReSTIR. Its light clusters (index lists) lost to s1's mask grid (§4). **Not ported**: materials garden, `TimingLog` (listed in §8) |
| **s3** `arena/c6cba543-slate` | `dd045df6` | a smaller exhibit tree (67 files) with a third screen-trace variant and a `ControlPanel` | Reviewed; s1's screen traces were better (§4) |
| **f4** `streamlinkinbox/Frontier` `arena/85b411ac-frontier` | `dfc92a02` | the common base. Its **unlisted commits** hold the IES parser and files (`f2aa20a6`, branch `734ca2e5`), the emissive-table ideas (`b8821f14`), triangle ReSTIR GI with executed tests (`5ec20b4f`), an XPBD attempt marked unstable (`7c483f80`) | **Taken**: the IES parser (extended, §5); the emissive-table *ideas* (redesigned, §3). The XPBD attempt was not used |
| **f5** `c7egoist/Frontier` `arena/21530081-frontier` | `21c553b` | the **software BVH path tracer** and ReSTIR GI path tracer | **The base** of the combined app |
| Slate (not among the five heads) | `ca517016` (commits `8e799250`, `b77c30fa`, `465b55a2`) | the **XPBD tyre engine** and its preview | **Ported** (§3) |

**What existed where** (checked by file and grep in the five branch heads, not from memory):

| Feature | s1 | s2 | s3 | f4 head | f5 head | Where the combined app got it |
|---|---|---|---|---|---|---|
| Clustered **real lights** (an actual cell → lights assignment) | mask grid (`LightClusterSpace`) | index lists (`ClusterCells` / `ClusterIndices`, assigned per frame) | none | **name only**: `ClusteredLights.wgsl` evaluates analytic beams and `DirectRadiance` loops over all lights | same as f4 | s1's mask grid |
| **IES** (LM-63 parser and files) | none | none | none | none (analytic beams whose comments say "IES") | none | f4 `f2aa20a6` (unlisted commit), extended |
| **Emissive-surface** light sampling | none | none | none | none | none | designed here from the ideas in f4 `b8821f14` |
| **XPBD tyres** | comments only | comments only | comments only | comments only | comments only | Slate `ca517016` |
| **Screen traces** | yes | yes | yes | none | none | s1's |
| **Software BVH** tracer | `TriangleHierarchy` (no TLAS/BLAS) | reflections only | none | none | full GI path tracer | f5's |

(The XPBD comments are "there is no XPBD carcass here" in `TyreThermalSolver.js` and "Frontier's tyre is an XPBD soft-body" in `VehicleSolver.js`, which describes the engine the Drive port came from.)

So the second clustered-light system the brief asked about (over *real* lights) existed only in s1 and s2; in f4 and f5 the name was all there was. The "XPBD is already in some
branches" expectation does not hold for the five heads: it appears only in comments.

**XPBD, specifically.** A search of the five branch heads found XPBD only in comments. The implementation exists in the Slate repository at `ca517016` and, unstable, in f4's
`7c483f80`. The solver here is the Slate one: the lattice is exported from the C++ engine (`Tools/TyreLattice/`; the engine's default lattice is byte-identical to the preview's), the
six WGSL kernels are Slate's, and the host coupling is new (§3).

## 3. How the combined app is put together

**Base.** f5's tree. s1's renderer features were merged onto it by a three-way merge of about 40 hunks (automatic merges of the variants conflicted in 13, 10 and 21 files, so none
was attempted), then the missing features were ported.

**Per frame.** On the CPU: the vehicle, the XPBD readings, the lights (the grid's masks) and the emissive cell lists are updated and uploaded. On the GPU, in order: XPBD tyre
carcass → sky view → fragments → distance union → smoke transport → shadow cascades → card relighting → G-buffer → depth pyramid (the nearest-depth hierarchy) → direct radiance
(analytic lights with IES photometry; primary emissive NEE) → ReSTIR resampling (candidates are traced screen-first, then through the BVH or the distance field, with emissive NEE and
the partition in the BVH renderer; temporal and spatial reuse) → reflections → smoke march → resolve → present.

**System 1, real lights.** Up to 32 point, spot, flood and street lights. A 16 × 16 × 4 grid holds a `u32` mask per cell (bit *i* = light *i* reaches this cell). The cull is exact: with
`?lightgrid=0` every light is tested and the picture must not change (a test). Each light may carry an IES profile: an 8-bit atlas, 37 azimuths × 91 polar angles per profile.

**System 2, emissive surfaces.** Emissive triangles are grouped into clusters of at most 8 per facet set (recursive median split). Every cluster *instance* (a cluster on a placed
occupant) is binned into the same grid; a cell keeps the **eight strongest** by power / (d² + r²) at the cell centre. The ray tracer's next-event estimation samples exactly that list
with exact area pdfs. The estimator is unbiased by construction: the light technique takes weight 1 − w_b where it can draw a point, and the BSDF technique keeps w_b **only** where the hit
triangle is listed in the cell of the vertex it is seen from (tested exactly with BVH triangle indices); every other emitter keeps weight 1. The binning is incremental (§6).

**Layout (the contract for any port).** Everything for both light systems lives in one `LightClusterRecords` uniform of **60,992 B** (limit 65,536): a header, 32 lights and the masks
(`array<vec4u, 256>`) in the first 6,176 B; the IES atlas at 6,176 (1,474 `vec4u` = 23,584 B); the emissive triangles at 29,760 (160 × 48 B); the emissive clusters at 37,440 (224 × 32 B);
the cells at 44,608 (1,024 × 16 B: eight `u16` cluster ids, `0x7FFF` = empty). The grid covers x −60…156, y −60…60, z −2…28 m (216 × 120 × 30 m). A table triangle's `A.w` is its BVH
triangle index. Frame record 528 B
(`Features` at u32 index 103, bit 8192 = BVH; `EmissionScale` at f32 index 111; `Features2` at u32 index 129: screen traversal 1, screen indirect 2, screen reflection 4, light grid 8,
emissive lights 16, IES profiles 32); occupant record 336 B (`Roles` at u32 index 67; role 64 = emissive cluster).

**XPBD.** One 3-D carcass lattice per tyre (rings, segments, cords; tiles are independent copies), six compute kernels, 32 substeps (16 is not converged; the engine refuses 96
segments), about 416 dispatches a frame. The carcass is loaded by the contact patch and inflated by pressure (α = V₀ / (γ p_abs)); the host decodes fixed-point loads and the patch
extent, forms a secant stiffness from load and deflection, and feeds the vehicle a law clamped to 0.35–3× the analytic stiffness. A bad reading (NaN, zero, absurd loads, stale frames)
can bend a tyre but never break the car (tests). Measured (SwiftShader, 220 kPa, rolling radius 0.50855 m):

| deflection (mm) | 0 | 1 | 2 | 4 | 6 | 8 | 10 | 14 | 20 | 30 |
|---|---|---|---|---|---|---|---|---|---|---|
| load (N) | 137 | 233 | 330 | 545 | 830 | 1101 | 1446 | 2303 | 3573 | 6308 |

Pressure matters strongly: at 12 mm, 50 kPa gives 314 N and 350 kPa gives 4,375 N. The full app at rest sums the four loads to 1.000 × the car's weight with 17.9–18.5 mm of
deflection (secant about 175 kN/m, ride height 0.508 m); driving shows weight transfer with the readings within ±5 % of the weight.

## 4. Where branches competed, and what decided it

- **Screen traces (s1 vs s2 vs s3).** s1's: a separate `r16float` identity target; three gates (master, indirect, reflection); step budgets 64 / 48 / 32; and the distance-field walk
  resumes at `Strike.Travel − Bias` so the two halves of the trace do not leave a gap or double-count. Chosen on reading the three side by side, then checked with executed frames
  (§5): enabling it must change pixels and must stay close to the pure SDF and pure BVH frames.
- **World light cluster (s1 mask grid vs s2 index lists).** The masks are constant-size (no per-cell offset table or index list), so the whole structure fits one uniform binding next to
  the lights, and exactness can be tested directly (grid on against grid off). WebGPU's default limit of 8 storage buffers per stage ruled out the storage-buffer alternative: moving
  the lights to a storage buffer made the temporal pipeline's layout invalid (9 buffers).
- **BVH (s2 vs f5).** Same builder; f5's traversal is a superset (above).
- **Emissive cell lists (my first design vs what shipped).** The first version took the idea of a per-cell candidate list from f4's emissive table and flagged any cell with more than 8
  candidates "incomplete", turning emissive sampling off there. In the real scene (62 cluster instances, reach √(P/5·10⁻⁴) covering the whole pad) **1,024 of 1,024 cells overflow**, so that
  version was off everywhere; a census of the real scene showed it and it was abandoned. What shipped keeps the top 8 per cell and gets unbiasedness from the exact membership test instead of
  an "incomplete" flag (§3). (f4's own table was not copied: only its idea was used.)
- **XPBD.** The Slate engine was chosen over f4's unstable attempt because its lattice could be exported byte-identically from the C++ engine and its WGSL kernels (as Slate's preview
  shipped them) run as they are.

## 5. What was verified, and how

Everything below was run by me; `RUN_GPU=1` tests run the WGSL for real, on CPU cores, through SwiftShader (the sandbox and GitHub's runners have no GPU).

| Check | What it proves | Where | Result |
|---|---|---|---|
| **Unit tests**, CPU only (`npm test`) | partition, layouts, cell lists, incremental binning == rebuild, the analytic-irradiance claim, real-scene invariants, IES parser and provenance, light-grid structure, renderer names, tooling | local; Actions | local: **104 tests, 95 pass, 0 fail, 9 skipped** (the opt-in executed ones). Actions (`a503c27`): 91 passed, 0 failed, 9 skipped in 18 s (before the grid tests were added) |
| **Null-backend validation**: the whole app in **20 URL modes**, then `validate`, `:screen`, `:effects`, `:slate`, `:orbit`, `:emissive`, and `test:stability` | every WGSL module, layout, pipeline and command is valid exactly as Chrome would see it | local; Actions | **20 of 20 modes** and every script: 0 errors, 0 warnings (Actions: 58 s). The emissive hook: `Failures: []`, 98 of 98 table triangles are their BVH triangles, **1,024 of 1,024 cell lists equal an independent recomputation**, 600 of 600 samples valid and listed |
| **Executed: emissive sampler vs its CPU twin** | the shader samples what the unit tests verified | SwiftShader; Actions | 3,000 queries: **3,000 pick the same cluster and triangle**; worst position error 4.2e-6, worst pdf error 1.2e-6, radiance exact; 0 usable-cell mismatches and 0 listed-membership mismatches (195 listed, 2,805 not) |
| **Executed: IES lookup**, real profiles loaded | the shader's atlas lookup is the CPU reference | SwiftShader; Actions | 2,400 directions over every light type: worst error **2.5e-4**, mean 2.3e-6, all finite |
| **Executed: XPBD** | the six kernels behave physically | SwiftShader; Actions | load rises with deflection and with pressure (table in §3); in the full app the four loads sum to the car's weight and the tyres deflect plausibly |
| **Executed frames**, whole app, 16 frames of 192 × 108 | no green ground through the old z-fight frames; the light grid is an exact cull and the test would catch one that is not; screen traces are sane and close to the reference; emissive clusters add no energy | SwiftShader; Actions | see below |
| **Mutation checks** | the tests can fail | local | incremental cell lists: reversed tie-break, stale static cache and uncounted candidates are each caught with a specific message. Light grid: halving every reach is caught by the executed frames (below) and by the structure test |
| **GitHub Actions** | all of the above on fresh machines | Actions | first run (`5191504`) **green**: unit 18 s, validation 58 s, executed GPU job 25 min 20 s (shader parity 6 min, full-app frames 19 min). The second run (`a503c27`) is **green** too: 5 of 5 executed full-app tests (17 min 51 s), 31 of 31 unit and executed shader-parity tests with the GPU variants enabled, validation, and the benchmark workflow. |

**The executed frames, in numbers.**

- *Exact cull.* At night, with analytic tyres, the clean grid against the all-lights frame: mean abs **0.0067** / 255, 19 pixels moved by 4 or more steps, none by 16. The same grid with every reach
  **halved**: mean abs **0.5221**, 700 / 314 / 72 pixels moved by 4 / 8 / 16 or more. The test asserts both, so it cannot pass vacuously (a daylight version barely could: halved reach moved only
  989 pixels by at most 7 steps, against a clean floor of 82 and 3).
- *Night emissive ON/OFF.* The structure was live in the ON run (62 instances, 98 triangles) and off in the OFF run; the frame's mean luminance agreed to 0.4 % (73.17 with the structure, 73.46 without) and every region's mean to within 1.0 / 255, with mixed signs.
  This is a smoke-and-bias check (§7, item 4).
- *Screen traces.* On the SDF renderer 9,458 of 20,736 pixels change (largest change 30 steps, mean abs 0.388 / 255; the test requires under 6); on the BVH renderer 19,987 pixels change (largest 83,
  mean abs 1.297; requires under 10). Enabling them must change pixels (they do) and must stay close to the reference (they do); the frames stay finite and tarmac-grey.
- *Reproducibility across machines.* On the BVH renderer the sandbox and GitHub's runner agree almost exactly: screen traces on vs off differ in 19,986 pixels here and 19,987 there (mean abs 1.297 on
  both), and the night emissive pair gives the same frame means (73.17 and 73.46) on both. The SDF frames carry the tyre-readback noise of §7, item 5.

**Where the tests earned their keep** (each of these was found by a measurement, not by reading):

1. The first emissive design (a cell with more than 8 candidates switches the feature off) was off in **1,024 of 1,024** real cells: found by a census of the real scene, replaced by top-8 lists and an exact membership test.
2. Re-binning the emissive structure cost **9 ms every frame while driving**: found by the CPU microbenchmark, fixed (0.3 ms) and proven bit-identical.
3. The atlas scaled profiles to the *file's* peak, but the roadway file's peak falls between grid points, so its strongest texel was only **96 %** of it: found by the real-file test; the atlas now scales to the strongest texel.
4. The f4 parser could not read **real** IES files (90–270 grids, grids ending at 357.5° or 358.8°, downlights measured to 90°): found by loading them; the parser now reads all three.
5. The old single 480 m grass quad **won the depth test at certain frames**: found by the executed tests, fixed by tiling.
6. An exact-cull test that passed in daylight had **no teeth**: found by deliberately breaking the grid; the test now runs at night and renders a broken grid on purpose.
7. A *failed-looking* test that was really the OOM killer (exit 1, no output) on a 4 GB machine: found by checking the log's emptiness; renders are now limited (`GPU_JOBS`).
8. Dawn's limit of 8 storage buffers per stage rejected the storage-buffer light layout: found at bring-up; the lights are a uniform.


## 6. Performance: what is known and what is not

Two kinds of measurement, which must not be confused. 🔴 **Neither was taken on a GPU**: the sandbox and GitHub's runners have none.

**1. CPU-side costs (valid anywhere): JavaScript doing what it does in the browser every frame.** `Tools/CpuBenchHooks.mjs`, on the real scene (62 emissive cluster instances, 1,024 cells):

| What | Before | After | GitHub runner (AMD EPYC 7763, 4 cores) |
|---|---:|---:|---:|
| Emissive cell lists, per frame while the car drives | **9.0 ms** | **0.3 ms** | 0.34 ms |
| Emissive cell lists, full rebuild | 9.0 ms | 2.6 ms | 2.96 ms |
| Emissive cell lists, frame where nothing moved | 0.04 ms | 0.04 ms | 0.06 ms |
| Lighting system + world light grid, per frame | n/a | 0.75 ms | 0.67 ms |
| IES atlas from the seven files (start-up, once) | n/a | 16.8 ms | 17.3 ms |

The first row is the one that mattered: the first working version re-binned all 62 cluster instances into 1,024 cells whenever the car moved, which is every frame while driving; at 9 ms (33 ms
on a loaded machine) that was more than half a 60 Hz frame of CPU time. It was caught by this benchmark, not by a test, and fixed by caching the unmoving instances' lists and merging only the
moving ones, with the result proven bit-identical to a from-scratch rebuild (§5).

**2. Toggle benchmarks: the shader code running on CPU cores (SwiftShader), so a measure of RELATIVE shader work, not of GPU time.** One GitHub job per toggle; each runs the baseline and the
variant interleaved on one runner (A B B A), 7 measured frames of 192 × 108 per run. "Noise" is the disagreement between repeats of the same query; a verdict needs the change to leave both a 5 %
band and twice the noise (and is called *marginal* when it is less than 1.5 bands out). Run `39387086558`, commit `a503c27`:

| Toggle (baseline → variant) | Queries | ms per frame | Change | Noise | Verdict |
|---|---|---|---:|---:|---|
| Emissive clusters, day | emissive=0 → emissive=1 | 4446 → 4966 | +11.7 % | ±2.3 % | **slower** |
| Emissive clusters, night | preset=Night&emissive=0 → preset=Night&emissive=1 | 4399 → 4930 | +12.1 % | ±2.5 % | **slower** |
| IES photometric profiles | ies=0 → ies=1 | 4296 → 4074 | −5.2 % | ±2.4 % | **faster (marginal)** |
| World light grid (vs testing every light) | lightgrid=0 → lightgrid=1 | 5910 → 4844 | −18.0 % | ±2.4 % | **faster** |
| BVH path tracer vs SDF renderer | renderer=sdf → renderer=bvh | 3142 → 4935 | +57.1 % | ±6.0 % | **slower** |
| Screen traces on the BVH renderer | renderer=bvh → renderer=bvh&screentraces=1 | 2176 → 2159 | −0.8 % | ±8.1 % | **no measurable difference** |
| Screen traces on the SDF renderer | renderer=sdf → renderer=sdf&screentraces=1 | 3106 → 3162 | +1.8 % | ±5.0 % | **no measurable difference** |
| XPBD 16 substeps vs 32 | tyresubsteps=32 → tyresubsteps=16 | 4836 → 4556 | −5.8 % | ±2.3 % | **faster (marginal)** |
| XPBD 8 substeps vs 32 | tyresubsteps=32 → tyresubsteps=8 | 5003 → 4383 | −12.4 % | ±1.5 % | **faster** |
| XPBD tyre carcass (32 substeps) vs analytic tyres | tyres=analytic → tyres=xpbd | 4080 → 4963 | +21.6 % | ±1.9 % | **slower** |

(Machine: AMD EPYC 7763 × 4 cores, Node 22, SwiftShader from `@sparticuz/chromium@153.0.0`. Verdicts recomputed from the run's own numbers under the final "marginal" rule; the run itself predates that label.)

What this does and does not say:

- **The world light grid is a clear win: 18 % faster** than testing every light, so it stays on by default.
- **The BVH path tracer costs 57 % more than the distance-field renderer** here. It is the default because the brief asks for the software ray tracer; `?renderer=sdf` is the cheap mode.
- **Screen traces: no measurable difference on either renderer** (−0.8 % ± 8.1 % on BVH, +1.8 % ± 5.0 % on SDF). On this evidence they neither cost nor save frame time in this scene on a software
  rasteriser. Their value is the quality of what they find (exact on-screen hits), which a timing cannot show. They are off by default, as in s1; whether to make them the default is a decision for you (§8).
- **Emissive clusters cost about 12 % (day and night alike)** and, in this scene, move few pixels (§7, item 4): the cost is the extra sampling and shadow rays, paid whether or not the emitters
  matter. They are on by default because the brief asks for the system; `?emissive=0` is the switch. A real-GPU measurement should decide whether to keep them on.
- **XPBD tyres cost about 22 % at 32 substeps** against the analytic tyre. Fewer substeps are cheaper (16: −5.8 %, marginal; 8: −12.4 %), but 16 is not converged, so the default stays 32.
- **IES profiles: no cost** (−5.2 %, marginal: a lead, not a finding).
- **Runners differ by up to 2×**: the default BVH configuration measured 2,159–2,176 ms per frame in the screen-trace job and 4,844–4,935 ms in two others (different VMs). That is why only
  within-job comparisons are used, and why the rounds can be raised (`gh workflow run vehicle-hmi-bench.yml -f rounds=4`) before acting on a borderline row.


## 7. Known issues and limits

1. **No measurement on a real GPU or in a real browser.** The sandbox and the CI runners have neither. The picture was judged on SwiftShader frames only; "does it look right in
   Chrome" and every GPU timing are open. The in-app **Hardware log** card (Render page, *Capture matrix*) captures 120 frames per configuration on the machine it runs on (SDF, BVH, each with screen traces, the grid off, emissive clusters off): that is how to take the real numbers.
2. **Inherited radiometric inconsistencies, deliberately unchanged:** f5's path-tracer NEE multiplies sun and lights by π while the reflection tracer divides by π; the analytic
   lights apply the cosine twice (`EvaluateLightArrival` and `ResolveDirectReflectance`). Fixing them changes the look of the whole app, so they should be a separate, reviewed change.
3. **Emissive NEE is top-8 per cell.** In the real scene every cell has more than 8 candidates, so emitters outside a cell's eight strongest are found by BSDF hits at weight 1:
   unbiased but noisier. The uniform has about 4.5 KB left, so the lists cannot grow much without a storage buffer. BVH renderer only.
4. **In this scene the emissive structure moves few pixels.** The analytic street lamps dominate the lighting, so the full-app ON/OFF test is a smoke-and-bias check (the mean light
   must not change anywhere), not a variance test. The estimator's correctness rests on the unit tests and the 3,000-query shader parity test.
5. **Frame equality has a noise floor, and its biggest source is the XPBD tyres.** The null backend is bit-reproducible (three runs, identical hashes over all 2,074 occupants and the
   vehicle pose), but on SwiftShader the tyre loads come back through asynchronous readbacks that land at different moments, which varies the car's pose and ripples through reprojection
   at thin silhouettes. Two *identical* night runs differ by a mean of 0.100/255 (max 103) with the XPBD tyres and 0.011 (max 12) with analytic ones; in daylight, by tens of pixels by one step.
   So the executed tests assert to a floor, not to bit equality, and a test about something other than the tyres (the light grid) renders with `?tyres=analytic`. This is a property of slow
   adapters and A/B comparison, not a defect found in the car, but it is why the frame tests carry tolerance bands.
6. **XPBD cost is high**: about 416 dispatches a frame. Its GPU cost is unmeasured; on SwiftShader it is seconds per frame. 16 substeps is not converged, which is why 32 is the default.
7. **The `Area` IES slot is unused** by any built-in light type; the three **automotive** beams are authored, not measured.
8. **A pre-existing bug in the f4 and f5 bases** was found and fixed on the way: a single 480 m grass quad lost the depth test under TAA jitter at certain frames ("green ground").
   It is tiled into 20 m cells in `LevelStructure.js`; a test guards it.
9. **IES profiles are normalised to their peak, not to their flux.** A light's own intensity supplies the scale, so swapping a profile for another changes the light's total power: the
   real roadway file has only 0.43 of its peak straight down (its peak is 69° out, along the road), so a street lamp's pool under the pole is dimmer than the authored profile's was
   while its throw is longer. Light intensities were not recalibrated for the real profiles; if a street looks too dark or bright, scale `Intensity` per profile by flux.
10. IES files' licences: the *repository* licences are permissive and recorded, but the files are manufacturers' published data without a licence of their own (`Profiles/README.md`).

## 8. Not done, and decisions for you

**Not done.** s2's materials garden and `TimingLog` were not ported. No browser, real-GPU or visual review beyond SwiftShader frames. No C++ port, by instruction: WebGPU prototype first.

**Decisions I did not make for you, with the evidence that bears on each:**

1. **Screen traces are off by default** (`?screentraces=1` or the Inspector switch), as in s1. On the software rasteriser they cost nothing measurable and save nothing measurable (§6), so the case for
   switching them on by default is quality, which a timing cannot show. The executed frames say they stay sane and close to the reference.
2. **Emissive clusters are on by default**, because the brief asks for the system. They cost about 12 % on the software rasteriser and, in this scene, move few pixels. If a real GPU says the
   cost matters, `?emissive=0` (or the Inspector switch) is the answer; a cheaper default is a design change (fewer samples, or gating by a cell's strongest emitter).
3. **The XPBD carcass is on by default at 32 substeps** (about 22 % of the software frame). 16 substeps are cheaper but not converged.
4. **The three automotive IES beams are still authored.** If you have measured headlamp files (or accept a different licence), they drop in at the same names; update `Profiles/PROVENANCE.json`.
   Light intensities were not recalibrated for the real profiles (§7, item 9).
5. **The inherited π and double-cosine inconsistencies** (§7, item 2) change the look of everything when fixed, so they should be their own reviewed change.
6. **A real-GPU, real-browser pass**: the Render page's *Hardware log* card captures the configuration matrix on whatever machine runs it.

## 9. Notes for the C++ port

- **Keep the contract, not the JavaScript.** The binary layouts in §3 are the interface. Port the records first. What is asserted today: the light record's size is at most 64 KiB and the
  emissive offsets land where the shader reads them (`EmissiveLights.test.mjs`); the IES atlas size agrees with the shader's constants (`Photometry.test.mjs`). What is *not* a unit test:
  the frame record (528 B, `FrameRecordExtent` in the host) and the occupant record (336 B) are host constants whose mismatch with the WGSL structs shows up as a binding-size validation
  error in the null-backend bring-up (a buffer too small is caught; one too large is not), so a C++ port should add explicit size checks.
- **The shaders need no translation to start.** Dawn (C++) and wgpu-native consume the WGSL unchanged, so the first C++ milestone can run every shader and every parity probe as is.
- **The oracles are small and exact; port them as golden vectors.** `Tools/EmissiveReference.mjs` (sampler, partition, `IsListedEmitter`), `Source/Photometry.js` (`SampleIes`, atlas
  packing), `EmissiveLightSpace.js` (cell lists), `TyreCarcassSolver.js`'s host coupling. Dump their inputs and outputs to files and have the C++ tests read them. Tie rules matter:
  cell lists order by importance descending, then cluster id ascending, in doubles, using the same expressions (including `** 2`).
- **The XPBD engine is already C++.** `Tools/TyreLattice/ExportTyreLattice.cpp` builds the lattice with the engine, so a C++ build can link the engine directly and compare its CPU
  result with the WGSL kernels' (today the kernels are checked physically, not against the engine bit for bit).
- **Tests worth porting verbatim as properties:** the light grid is an exact cull; incremental cell lists equal a from-scratch rebuild; the partition reproduces the analytic
  irradiance; the atlas's strongest texel is 255; the four tyre loads sum to the car's weight; the executed frames agree to the noise floor.
- **Traps found here:** Dawn's default of 8 storage buffers per stage; the 64 KiB uniform binding; `Frame.Features2` bits; WebGPU has no hardware ray tracing (the BVH is a plain
  compute traversal); an NaN or stale carcass reading must degrade to the analytic tyre, never to a broken car; SwiftShader timings are CPU emulation and must not drive tuning.
