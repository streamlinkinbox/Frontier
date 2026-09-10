══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
  Celestial port — the step plan, with proofs
══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

Companion to `CelestialPortPlan.md`, which covers scope, architecture and the tier mapping. This document is the
ordered build sequence and, for every step, **what will be produced as evidence that it works**.

Nothing here is implemented yet. Steps are sized to be finished and gated one at a time.


① THE PROOF SPINE — WHAT WE CAN ACTUALLY PROVE, AND HOW
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Three proof mechanisms already exist in this tree and all three were verified working before this plan was
written. Nothing below invents a new kind of evidence.

**A · Headless UI proof (ImGui → CPU raster → PNG → pixel assertions).**
`Scratchpad/EditorProof.cpp` drives `EditorHost` through the engine's real tick order and rasterises the ImGui
draw lists with a dependency-free CPU rasteriser — no Vulkan, no GLFW, no window. `EditorKnobCheck.cpp` then reads
the PNG back and asserts on geometry. Confirmed running: it emits `EditorProof_{Palette,Views,Menu,Tabs,…}.png`
and reports `knobs found: 1 · middle 1214,341 size 23x24 · the knobs sit on their fractions`. **This is how the
Celestial panel gets proved**: the panel renders headlessly, sheets are committed to `Diagnostics/`, and a checker
asserts the layout numerically rather than "looks right".

  ⚠️ Prerequisite: `ExternalPackages/stb` must be initialised (`EditorKnobCheck` needs `stb_image.h` to read the
  sheet back). It is an uninitialised submodule in a fresh checkout and was one of the baseline failures; it
  fetches cleanly with `git submodule update --init ExternalPackages/stb`. **Step 0 fixes this properly** so the
  UI gate is not permanently red. `PngWriteShim.h` already covers the *write* side dependency-free.

**B · Dual render-path proof (CPU mirrors of both paths).**
The engine has two shipping paths and each has a CPU harness:
  • **GI-on / ReSTIR** — `CornellExportProof.cpp` links the real `RayTracingSolver` + `ReSTIRIntegrator` +
    `ExposureIntegrator`.
  • **GI-off / no-ray** — `ShadowTierProof.cpp` renders the real Cornell box through the real `VisibilityRaster`.
Every celestial feature must appear correctly in **both**, and each step below says what each path should show.

**C · Device proof (SwiftShader).** Shaders compile, pipelines create, dispatches execute and produce inspectable
buffers — the method established for shadows (`Scratchpad/GpuShadowDiagnosis/`). Correctness only; **never
timings** (`References/SoftwareVulkanDevice.md`).


② THE ARCHITECTURAL FINDING THAT ORDERS THE WORK
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Both render paths contain the *same* explicit hole, and the sky is what fills it:

    GI-off  VisibilityRaster.cpp:25   constexpr float kSky[3] = {0.30f, 0.42f, 0.63f};   // flat blue constant
            VisibilityRaster.cpp:570  if (TriId_[Idx] == kMiss) → paint kSky

    GI-on   ReSTIRViewport.slang:792  "A missed primary ray sees no surface, and there is no environment
                                       light, so it contributes nothing."
            ReSTIRViewport.slang:1051 "An escaped bounce ray contributes nothing: there is no environment light."

So the sky is not a decorative layer bolted on top. In GI-off it **replaces a hardcoded constant**; in GI-on it
**becomes an environment light**, which changes ReSTIR's light sampling, its reservoirs and the exposure metering
that Round 9 stripped back to frame-only. That asymmetry is the single biggest correctness risk in the port, and
it is why every step is proved on both paths separately rather than once.

Round 9's removal commit `947b8b9` lists every seam: ReSTIR sun slots and sky LUT bindings (21/22/24 — still free
below the bindless table at 25), `ExposureIntegrator` incident metering, `RenderScheduler`, `FidelityClassifier`,
the editor categories, and the build lists. **Read `git show 947b8b9` before starting each step that touches a
seam.**


③ THE STEPS
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

┌─ STEP 0 · Foundations and instruments ─────────────────────────────────────────────────────────────────────────┐
Make the ground solid before anything renders.
  • Initialise/repair `ExternalPackages/stb` so the UI proof gate runs in a fresh checkout — the gate that will
    carry every later UI proof.
  • Vendor the six 2K moon textures (`luna, ember, glacier, shard, shroud, sulfur`) from the source branch.
  • Re-open ReSTIR bindings 21–24 for sky sampling (below the bindless table at 25).
  • Extend `kTimestampCount` 16 → 20 with a **sky** span and a **volumetrics** span, in the same
    `WITH_AVAILABILITY` pattern as the shadow/ReSTIR pairs, and extend `Scratchpad/CheckGpuTimestamps.sh` to cover
    them (pool size, span closure, no direct `Stamps[]` reads).
  • Add `CelestialCriteria` fields to `FidelityCriteria` — the tier budgets from the plan's §6 table.

  **Proof**: `CheckEditorProof.sh` green from a clean checkout · `CheckGpuTimestamps.sh` green, negative-controlled
  for the two new spans (shrink the pool, drop a closing write → each must fail its own check) · file-count and
  suite baseline unchanged.
└────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

┌─ STEP 1 · Sun + Atmosphere + Sky (the analytic core) ──────────────────────────────────────────────────────────┐
`CelestialSolver` (sun/moon ephemeris from latitude/date/time), `AtmosphereModel` (Rayleigh/Mie/ozone, scale
heights, planet radius), `SkyView.slang` (analytic — **not** LUTs, per the reverted `2fe78ed`).
Wire into **both** paths: replace `kSky` in `VisibilityRaster`; feed the miss branches in `ReSTIRViewport`.

  **Proof**:
  • `Scratchpad/CelestialSolverProof.cpp` — solar position against published almanac values for several
    latitudes/dates, exit non-zero on drift. The `ShadowMatrixProof` pattern: independent arithmetic, not a
    re-run of the same code.
  • `Diagnostics/Celestial_01_TimeOfDay_{Dawn,Noon,Dusk,Night}.png` — GI-off sheets through `VisibilityRaster`.
  • `Diagnostics/Celestial_02_ReSTIR_{Noon,Dusk}.png` — the same times through the ReSTIR CPU path, so the two
    paths can be compared side by side.
  • Device: `SkyView.slang` compiles, pipeline creates, dispatch writes a plausible radiance buffer under
    SwiftShader.
  • Regression: `CheckShadowTiers.sh` and `CheckReSTIRSelection.sh` still green — the sky must not disturb shadows
    or reservoir identity.
└────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

┌─ STEP 2 · (was the panel UI — MOVED TO LAST) ──────────────────────────────────────────────────────────────────┐
The panel was originally scheduled here. It is now built after every simulation component exists, by decision:
see `References/Backlog/CelestialPanelUi.md` for the reasoning and for everything already in place for it.

In short: the panel is a projection of the entity registry, so building it early means editing every entity twice
and rebuilding widgets against parameters that are still moving. Nothing is blocked by its absence — each
component is proved headless with committed sheets and numeric gates, which is stronger evidence than a
screenshot of a slider.

**While it is deferred**, every component must still keep its parameters in one settings struct per entity, named
as the demo names them, with units in the comment — as `VisibilityRaster::CelestialSettings` and
`TwilightSettings` already do. That is what keeps the eventual panel a projection rather than a rewrite.
└────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

┌─ STEP 3 · Stars + Moons ───────────────────────────────────────────────────────────────────────────────────────┐
Depends on Step 1's celestial frame. Star pass gated on sky luminance **from the start** (measured win `da4b0d7`,
not a later optimisation). Six moon textures with phase and libration.
  **Proof**: `Diagnostics/Celestial_03_Stars_Night.png` (both paths) · limiting-magnitude hero value checked
  against the solver · a gate asserting the star pass is skipped when sky luminance exceeds the brightest star.
└────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

┌─ STEP 4 · Wind Field ──────────────────────────────────────────────────────────────────────────────────────────┐
Before every other volumetric, because clouds, local cloud, fog and precipitation all advect by its integral.
Port `windAt()` with the `6c8b1a8` structure already in place: per-step advection trig-only, swirl once per pixel
and only when turbulence > 0.
  **Proof**: `Scratchpad/WindFieldProof.cpp` — CPU/GPU twin agreement on a sampled grid, and an explicit count
  that turbulence is evaluated once per pixel rather than per march step (the exact regression `6c8b1a8` fixed)
  · `WINDROSE` widget sheet with Beaufort readout.
└────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

┌─ STEP 5 · Clouds + Fog (the largest step) ─────────────────────────────────────────────────────────────────────┐
Cloud Layer, Local Cloud, Volumetric Clouds, Height Fog, Atmospheric Fog, Local Volumetric Fog — as **one unified
march over the union interval** with shared extinction and a shared sun-shadow march (`73737b6`), coarse probe
through clear air, zero-coverage early-out. **Do not** add clear-air striding (`73b71d6`, reverted: speckled
cloud), a low-res cloud FBO with temporal reprojection (`a152901`, reverted: slower and worse), or atmosphere LUTs
(deferred to last on measured evidence AND on looks — see `References/Deferred/AtmosphereLuts.md`).
  **Proof**: cloud sheets at several coverages, both paths · a gate asserting the single-march structure (one
  march function, not per-volume duplicates — the thing `73737b6` consolidated) · timestamp `volumetrics` span
  reporting separately from `sky` · SwiftShader dispatch correctness.
└────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

┌─ STEP 5b · God rays (crepuscular shafts) ──────────────────────────────────────────────────────────────────────┐
**Not a port — new work.** Checked the source branch and the demo: no godray/shaft/crepuscular code exists in
either. So this is designed here, and the design should follow the physics the demo already uses rather than the
screen-space trick.

The demo's volumetrics already contain the exact term shafts are made of: `uniShadow(p, st)` marches 4 taps toward
the sun accumulating optical depth and returns `exp(-od)` — the sun's transmittance *at a point inside a medium*.
Shafts are that same quantity evaluated against **scene occlusion** instead of only cloud/fog density, integrated
along the view ray through the atmospheric medium. So god rays are not a new system: they are the sun-visibility
term of the existing march, sourced from the shadow maps.

That is why this sits at 5b rather than in its own phase — it reuses Step 5's unified march (one more term in a
loop that already exists) and Round 10's shadow maps (`ShadowExchange`, up to 4 taps) as the occlusion source. Two
things this repo already has, joined.

Implementation, in preference order:
  • **Analytic in-medium** (preferred): inside the unified march, replace the medium-only `uniShadow` with the
    product of medium transmittance and the shadow-map lookup already used by `ShadowSample.slang`. Physically
    consistent with the fog and cloud in-scatter, correct behind occluders, and costs one map lookup per march
    step rather than a separate pass.
  • **Radial screen-space blur**: rejected as the default. Cheap, but it fails when the sun is off-screen —
    exactly the shot people want shafts for — and it cannot be reconciled with the aerial-perspective integral.
    Only worth reconsidering as a Minimal-tier fallback, and only if measured.

Tier keying (extends the plan §6 table): shaft sample count follows the medium's own step budget — off at
Minimal, 8 at Economy, 16 at Standard, 24 at Ultra, 32 at Reference — so shafts cannot desynchronise from the
march that carries them.

Both paths: GI-off takes the shadow-map term directly; GI-on must not double-count, since ReSTIR already resolves
sun visibility per sample — there the shaft term applies to the *medium* in-scatter only, not to surface shading.
**That double-counting risk is the main thing to get right**, and it is the reason this step is proved on both
paths separately like every other.

  **Proof**:
  • `Scratchpad/GodRayProof.cpp` — a slab of uniform medium with a known occluder, integrated analytically and
    compared against the marched result; shaft radiance must fall as `exp(-σ·d)` along the shadowed segment.
  • `Diagnostics/Celestial_05b_GodRays_{Forest,CloudGap}.png` in both paths, plus a **sun-behind-camera** sheet —
    the case that would expose a screen-space shortcut if one ever crept in.
  • A gate asserting the shaft term is evaluated inside the unified march (no separate full-screen pass) and that
    GI-on applies it to medium in-scatter only.
└────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

┌─ STEP 6 · Precipitation ───────────────────────────────────────────────────────────────────────────────────────┐
Rain/drizzle/hail/snow/sleet: terminal velocity, drag, wind coupling, ground collision with restitution (hail
bounces), rest-then-vanish, splash ripples / melt. Spawns from cloud base, so it follows Step 5.
  **Proof**: `Scratchpad/PrecipitationProof.cpp` — terminal velocity against the analytic drag solution per type
  · sheets per type · particle budget honouring the tier.
└────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

┌─ STEP 7 · Terrain + Post ──────────────────────────────────────────────────────────────────────────────────────┐
Height field; then aerial perspective, rainbow, lens flare, tonemap (`ACES/Reinhard/Filmic/AgX`).
  **Proof**: rainbow geometry at the analytic 42° · tonemap curve sheets · both paths.
└────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

┌─ STEP 8 · The panel, in full ──────────────────────────────────────────────────────────────────────────────────┐
All 16 entity inspectors and all 21 widget types, built once against a settled parameter set — including the
bespoke ones: `ORB`, `GLOBE`, `PAD`, `DISC`, `PLANET`, `ORBIT2`, `WINDROSE`, `HORIZON`, `LENS`, `HIST`, `METER`,
`TRK`, `KELVIN`, `STEP`, `DROP`, `LINKNOTE`. Design tokens and registry shape are recorded in
`References/Backlog/CelestialPanelUi.md`.
  **Proof**: a sheet per entity inspector committed to `Diagnostics/`, each numerically gated through the
  `EditorProof` → `EditorKnobCheck` mechanism — knob positions against their computed fractions, token colours
  sampled at known pixels, hero readouts against the solver's own output.
└────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

┌─ STEP 9 · Tier integration and Auto ───────────────────────────────────────────────────────────────────────────┐
Bind the plan §6 mapping (7 demo tiers → our 5, Cinematic collapsed into Reference) in `FidelityClassifier`
**only**, with the panel reading it rather than restating it. Then `Auto`, last, behind its own toggle.
  **Proof**: a five-tier comparison sheet (the `ShadowTierProof` pattern) · a gate that the ladder lives in one
  place · Auto exercised against synthetic frame times, not real ones.
└────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘


④ WHAT EACH STEP SHIPS AS EVIDENCE
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Every step lands with the same four artefacts, so "done" is never a matter of opinion:

  1. **PNG sheets** in `Diagnostics/`, committed, showing the feature in **GI-off and GI-on**.
  2. **A numeric gate** in `Scratchpad/Check*.sh` — asserting values, not appearances — negative-controlled by
     breaking the thing it guards and confirming it goes red.
  3. **A CPU proof** where physics is checkable against an independent calculation (solar position, terminal
     velocity, rainbow angle, limiting magnitude).
  4. **A device check** on SwiftShader for anything that runs on the GPU: compiles, creates, dispatches, output
     inspected. Correctness only.


⑤ HONEST LIMITS, RESTATED
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
  • **No performance numbers from this sandbox.** SwiftShader cannot rank tiers or price a cloud march. The
    timestamp spans added in Step 0 are what make the first hardware run conclusive; until then every performance
    statement stays a work count.
  • **The UI proofs are structural, not aesthetic.** A committed sheet plus pixel assertions proves the panel is
    laid out and wired as specified. It cannot prove it *feels* like the demo — that needs your eye, which is why
    sheets are committed every step rather than at the end.
  • **The sky changes GI-on behaviour.** An environment light alters ReSTIR sampling and the exposure path R9
    stripped. Reservoir identity (`CheckReSTIRSelection.sh`) must be re-verified after every step that touches the
    kernel, and I expect at least one genuine surprise there.
  • **Several rounds, not one.** Step 5 alone is comparable in size to the whole shadow round.
