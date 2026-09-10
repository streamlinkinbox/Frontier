══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
  Porting the Celestial system — sky, clouds, wind, weather, and the panel UI
══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

Plan only. Nothing in this document has been implemented.

**Source**: `SultanAladin/Frontier-` @ `arena/01a08682-frontier`, HEAD `6c8b1a8`.
**Reference UI**: `Diagnostics/CelestialPanel.html` (identical to `docs/index.html`, the GitHub Pages demo) —
469 KB single-file WebGL app, 3,657 lines, of which a 498-line GLSL fragment shader is the entire simulation.
**Target**: this repo, branch `arena/01a0881a-frontier`.


① THE FINDING THAT SHAPES EVERYTHING: THIS IS A PORT, NOT A MERGE
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
I checked whether the source branch could be merged or cherry-picked. It cannot, for three independent reasons,
and knowing this up front is what keeps the estimate honest.

**1. The trees are not the same engine.** The source branch has no `Engine/` subsystem layout — its directories
sit at the repository root (`DeviceExchange/`, `PhotometricIllumination/`, `VolumetricDynamics/`), which is the
pre-Slate layout this repo replaced at `7cf8e64`. Ours is `Engine/DeviceExchange/`, `Engine/DisplayPresentation/`
and so on. There is no common ancestor to merge against for these files.

**2. The C++ there is not the implementation.** `PhotometricIllumination/AtmosphereIntegrator.{h,cpp}` is 145
lines total and `Shaders/{AtmosphereVolumetrics,VolumetricCloud}.comp` are 95 lines between them. They are
skeletons against a `VolumetricDynamics/FluidSolver.h` we do not have. **The working system is the HTML demo's
GLSL** — 47,829 characters of it. Porting means reading that shader and writing Vulkan compute, not moving files.

**3. We deliberately deleted our own sky.** Round 9 (`947b8b9`, "clean-slate sun/sky removal") removed
`CelestialSolver`, `DaylightSolver`, `AtmosphereModel`, `AtmosphereScattering/Lut` slang, three gates, the editor
Sky/Sun/Moon categories, the day-cycle meter, and the sun bindings in the ReSTIR kernel — at the author's request.
That commit is not an obstacle; **it is the most valuable document we have**, because it is an exact inventory of
every seam the sky used to touch. The port re-opens those seams deliberately rather than rediscovering them.

Consequence: **estimate the work as new development with an excellent reference, not as a port.** The GLSL is the
specification and the HTML is the UI specification; both are complete and runnable, which is far better than
usual, but no line of either compiles into this engine.


② WHAT IS BEING PORTED
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
The demo's outliner defines 19 entities. The instruction was "everything in the outliner except the lights", so
`plight` (Point Light) and `slight` (Spot Light) are out. `sun` stays: its `kind` is "Directional Light", but it
is the celestial body driving the whole atmosphere, not a placeable lamp.

| # | key | name | kind | in scope |
|---|---|---|---|---|
| 1 | `atmosphere` | Atmosphere | Atmosphere | ✅ |
| 2 | `sun` | Sun | Directional Light | ✅ (celestial driver) |
| 3 | `sky` | Sky | Sky Atmosphere | ✅ |
| 4 | `stars` | Stars | Star Field | ✅ |
| 5 | `fog` | Height Fog | Volumetrics | ✅ |
| 6 | `afog` | Atmospheric Fog | Aerial Perspective | ✅ |
| 7 | `vfog` | Local Volumetric Fog | Fog Volume | ✅ |
| 8 | `clouds` | Cloud Layer | Clouds | ✅ |
| 9 | `lcloud` | Local Cloud | Cloud Volume | ✅ |
| 10 | `vclouds` | Clouds | Volumetric Clouds | ✅ |
| 11 | `wind` | Wind | Wind Field | ✅ |
| 12 | `precip` | Precipitation | Component · Clouds | ✅ |
| 13 | `moons` | Moons | Atlas | ✅ |
| 14 | `terrain` | Height Field | Terrain | ✅ |
| 15 | `camera` | Camera | Perspective Camera | ✅ (hosts the tier dropdown) |
| 16 | `post` | Post Process | Component · Camera | ✅ |
| 17 | `cine` | Cine Camera | Cinematic Camera | ⚠️ deferred — see §7 |
| 18 | `plight` | Point Light | Point Light | ❌ excluded |
| 19 | `slight` | Spot Light | Spot Light | ❌ excluded |

Sixteen entities in scope. The GLSL that implements them, ranked by size — this is the real cost distribution:

    main 132 · starField 36 · moons 30 · marchLocal 28 · dawnGlow 28 · cloudMarch 25 · atmosphere 23
    applyMedia 16 · lensFlare 15 · rainbow 13 · cloudLayer 11 · clDensity 10 · vfShapeMask 9 · ...

56 functions, 47 uniforms, 498 lines. Six 2K moon textures ship with it (`luna`, `ember`, `glacier`, `shard`,
`shroud`, `sulfur`).


③ ARCHITECTURE — WHERE EACH PIECE LANDS
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
The demo is one monolithic fragment shader because WebGL gave it no choice. We have compute, a frame graph, and a
tier system, and the port should use them rather than transliterating a 132-line `main()`.

**Simulation (CPU, authoritative state)** — `Engine/DisplayPresentation/`:

  • `CelestialSolver.{h,cpp}` — sun/moon ephemeris from latitude, date and time. Re-creates what R9 deleted.
  • `AtmosphereModel.h` — Rayleigh/Mie/ozone medium parameters, scale heights, planet radius.
  • `WindField.{h,cpp}` — the `windAt()` twin: base × altitude shear/veer × gust + curl turbulence. This is the
    one system every other volumetric reads, so it lands first among the weather entities.
  • `WeatherState.{h,cpp}` — cloud coverage, precipitation type/rate, fog densities; the block the UI edits.

**Rendering (GPU)** — `Engine/Shaders/`, dispatched from `Engine/DeviceExchange/`:

  • `SkyView.slang` — analytic Rayleigh/Mie sky. **Not LUT-based** (see §5, source commit `2fe78ed`).
  • `CloudMarch.slang` — the unified volumetric march. Source commit `73737b6` merged local fog + local cloud
    into ONE march over the union interval with a shared sun-shadow march; the port must start there, not
    rediscover it.
  • `StarField.slang` — magnitude-distributed star field, gated on sky luminance.
  • `Precipitation.slang` — particle system with terminal velocity, drag, wind coupling, ground collision.
  • `CelestialComposite.slang` — aerial perspective, rainbow, lens flare, tonemap.

**UI** — `Engine/Editor/` + `Engine/DisplayPresentation/ControlCentreHost.cpp`.

**Frame-graph placement.** The sky is background radiance for the GI-off path and an environment source for
ReSTIR. It must run **after** `SurfaceResolve` (needs depth for aerial perspective and cloud occlusion) and
**before** `ShadowResolve`/the ReSTIR dispatch. R9 left ReSTIR bindings 21–24 free below the bindless table at 25,
which is exactly where the sky-view and transmittance samplers go — the hole it left is the right shape.


④ THE UI PORT — WHAT THE DEMO ACTUALLY IS
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
This is the part most likely to be underestimated, so it is specified concretely.

The panel is **not** a set of sliders. It is a declarative registry (`const ENTITIES={...}`, 14,987 chars) where
each entity declares `name/kind/color/icon/preview/hero` and a list of sections, each section a list of typed
property widgets. There are **21 distinct widget types**:

    COL · CURVE · DISC · DROP · GAUGE · GLOBE · HIST · HORIZON · KELVIN · LENS · LINKNOTE
    METER · ORB · ORBIT2 · PAD · PLANET · SEG · STEP · TOG · TRK · WINDROSE

Several are bespoke direct-manipulation controls, not styled inputs: `ORB` is a draggable solar orbit (drag the
sun to swing azimuth and time together, drag the path for time only), `GLOBE` is a draggable latitude globe,
`WINDROSE` is a draggable wind rose with a Beaufort readout, `PAD` is a 2-axis pad (Rayleigh × Mie), `DISC` drags
the solar angular diameter on the ring and softness inside it, `PLANET` sets planet and atmosphere radius
together, `KELVIN` is a colour-temperature strip.

The design system is a CSS token set worth reproducing exactly:

    --glass: rgba(15,16,18,.74)      --g2/-g3: white .045 / .08
    --stroke: white .07              --stroke2: white .13
    --text: white .94                --t2: .56    --t3: .32
    --orange #ffb454  --green #34c759  --red #ff3b30  --yellow #e5d33a
    --ease cubic-bezier(.22,.61,.36,1)   --spring cubic-bezier(.175,.885,.32,1.15)
    font: Outfit / General Sans / Inter ; mono: JetBrains Mono

Layout: a 316 px fixed-left outliner at 28 px radius with search + filter pills + tree; a right inspector with a
hero readout per entity (Atmosphere shows live Air Mass, Stars shows naked-eye limiting magnitude); a bottom
timeline; a HUD with fps/tier/sun/moon/px.

**We already have an ImGui Control Centre with an established inspector pattern** (Round 8 moved the sliders onto
it). The port should express the registry as data — an entity/section/property table in C++ — and render it
through our existing panel chrome, implementing the 21 widgets as ImGui custom draws. **Do not attempt a
pixel-exact reproduction of a CSS/DOM design in ImGui in one pass**; match the token palette, radii and layout
proportions, and accept that the bespoke drag widgets are the bulk of the UI effort. Roughly **60% of the total
port effort is this UI**, and that is the single most important number in this document.


⑤ PERFORMANCE — THE SOURCE BRANCH ALREADY PAID FOR THESE LESSONS
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
The source history contains measured optimisations *and measured reverts*. Re-deriving them would waste a round,
and re-implementing a reverted one would actively regress the port. Recorded here in the same measure-first
spirit as `References/Deferred/`:

| source commit | change | outcome |
|---|---|---|
| `da4b0d7` | quality tiers driving render scale, cloud/local steps, light taps, star AA, precip budget; sky ambient via one 4×1 probe pass instead of 3 atmosphere evals per pixel; star pass gated on sky luminance | **kept** |
| `73737b6` | local fog + local cloud unified into ONE march over the union interval, shared extinction and sun-shadow march; cloud shell strides clear air with a coarse probe; zero-coverage early-out | **kept** |
| `73b71d6` | clear-air striding in clouds | **reverted** — probe/erosion mismatch stalled the march, dark speckled cloud |
| `4bf200d` → `a152901` | low-res cloud FBO + temporal reprojection + coverage probes | **reverted** — slower on a GTX and lower quality |
| `f5b5d3d` → `2fe78ed` | atmosphere LUTs (256×64 transmittance + 192×108 sky-view, Bruneton parameterisation) | **reverted** — no measurable speedup on a GPU-bound frame, and the analytic path looked better |
| `6c8b1a8` | wind turbulence (6 noise evals) was inside `windDisp`, called at every march step → ~1500 extra noise evals per cloud pixel; now per-step advection is trig-only and the swirl is evaluated once per pixel | **kept** |

**Do not begin by building atmosphere LUTs.** It is the obvious first optimisation, it is what the literature
recommends, and on this content it was measured and reverted. Start analytic.

The one caveat: those verdicts were measured in WebGL on a GTX-class part. Our engine is Vulkan with compute and
async potential, so a LUT could plausibly win here where it did not there — but that must be *re-measured*, and
the burden of proof sits with the LUT, not against it.

**Our own optimisations carry straight in.** The GPU timestamp scheme added at `7bc0ee5` is the instrument that
makes any of this decidable: the pool is 16 queries per slot with 12/13 the shadow span and 14/15 ReSTIR, read
`WITH_AVAILABILITY` so a stage that does not run cannot poison the figure. **The sky/cloud/weather stages should
be added as further pairs in exactly that pattern** — a `sky` and a `volumetrics` span, reported beside `shadow`
and `restir` in the diagnostic row, so the first hardware run attributes the new cost honestly instead of folding
it into `post`. Extending `kTimestampCount` and the reader is a small, well-gated change
(`Scratchpad/CheckGpuTimestamps.sh` already negative-controls pool size and span closure).


⑥ QUALITY TIERS — THE MAPPING DECISION
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
The demo ships a **7-entry** ladder (`Auto · Low · Economic · Standard · Ultra · Cinematic · Reference`); we have
**5** (`Minimal · Economy · Standard · Ultra · Reference`) in `FidelityClassifier`. The demo's full budget table:

| tier | scale | cloudSteps | lightTaps | cloudRes | starAA | starLayers | localSteps | precip | covMargin | atmoN | atmoNL |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Low | 0.60 | 16 | 3 | 0.25 | 1 | 2 | 12 | Low | 0.10 | 8 | 3 |
| Economic | 0.75 | 20 | 3 | 0.50 | 1 | 3 | 16 | Low | 0.06 | 12 | 4 |
| Standard | 1.00 | 28 | 4 | 0.50 | 1 | 3 | 28 | Medium | 0.03 | 16 | 6 |
| Ultra | 1.50 | 36 | 5 | 0.50 | 2 | 3 | 36 | High | 0.00 | 20 | 8 |
| Cinematic | 2.00 | 48 | 5 | 1.00 | 2 | 4 | 48 | High | 0.00 | 24 | 8 |
| Reference | 3.00 | 64 | 5 | 1.00 | 2 | 4 | 64 | High | 0.00 | 32 | 12 |

**Proposed mapping** — collapse `Cinematic` into `Reference`, since our Reference already means "most realistic,
no compromise", and the two differ mainly by render scale which our own scale trim covers:

| ours | takes | note |
|---|---|---|
| Minimal | Low | |
| Economy | Economic | |
| Standard | Standard | |
| Ultra | Ultra | |
| Reference | Cinematic ∪ Reference | steps from Reference (64/64, atmoN 32), scale from Cinematic (2.0) |

This follows the shadow precedent exactly: **the tier sets defaults, and an explicit dropdown overrides it
regardless of tier** — the same contract as the Round 10 shadow-resolution dropdown, gated the same way
(`CheckShadowTiers.sh` greps the *call* `WithShadowResolution(`, not the comment). The new fields extend
`FidelityCriteria` beside `ReSTIRCandidateSampleCount` and the shadow entries.

`Auto` deserves a decision rather than a default. The demo walks the ladder every 1.5 s to hold a target fps.
That is genuinely useful and genuinely a behaviour change for this engine — recommend porting it **last**, behind
its own toggle, once the timestamps are reporting real numbers, because an auto-tier that reacts to a misattributed
frame time is worse than no auto-tier.


⑦ SEQUENCING
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
Ordered so that each phase is independently verifiable and nothing is built on an unproven assumption.

  **0 · Foundations.** Vendor the six moon textures. Re-open ReSTIR bindings 21–24. Extend `kTimestampCount` with
  a sky span and a volumetrics span, and extend `CheckGpuTimestamps.sh` to cover them. Nothing renders yet; the
  instrument exists before the thing it measures.

  **1 · Sun + Atmosphere + Sky.** `CelestialSolver` and the analytic `SkyView.slang`. Gate: a headless sheet at
  fixed times of day, plus a CPU reference for the solar position (the pattern `ShadowMatrixProof` established).
  This is the phase that proves the frame-graph placement is right.

  **2 · Stars + Moons.** Depends on the celestial frame from phase 1. Star pass gated on sky luminance from the
  start — that was a measured win (`da4b0d7`), not an optimisation to add later.

  **3 · Wind Field.** Before any other volumetric, because clouds, local cloud, fog and precipitation all advect
  by its integral. Port `windAt()` with the `6c8b1a8` structure already in place: per-step advection trig-only,
  swirl once per pixel and only when turbulence > 0.

  **4 · Clouds + Fog.** The unified march from `73737b6` — one march over the union interval, shared extinction
  and sun-shadow. Cloud Layer, Local Cloud, Volumetric Clouds, Height Fog, Atmospheric Fog, Local Volumetric Fog.
  The largest single phase.

  **5 · Precipitation.** Rain/drizzle/hail/snow/sleet with terminal velocity, drag, wind coupling, ground
  collision with restitution, splash/melt. Spawns from cloud base, so it depends on phase 4.

  **6 · Terrain + Post.** Height field, then aerial perspective, rainbow, lens flare, tonemap.

  **7 · The panel UI.** Deliberately last as a *whole*, though each phase should expose its parameters through
  the existing Control Centre as it lands — otherwise nothing is testable until the end. The bespoke widgets
  (`ORB`, `GLOBE`, `WINDROSE`, `PAD`, `DISC`, `PLANET`, `KELVIN`) are the bulk.

  **8 · Auto tier**, once timestamps report real numbers.

  **Deferred: `cine` (Cine Camera).** It is a camera rig, not weather, and it pulls in gizmo interaction
  (`G/R/S`) that overlaps our existing editor gizmo work. Out of scope unless asked.


⑧ RISKS, STATED PLAINLY
──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
  • **Scale.** Sixteen entities, ~500 lines of dense GLSL, 21 bespoke widgets. This is several rounds, not one.
    Any estimate that sounds like one round is wrong.
  • **The UI is the majority of it** (~60%), and it is the part with no existing analogue in our tree — the
    simulation at least has a working reference to read.
  • **No GPU here.** Everything this round can prove is compile-and-correctness on SwiftShader, exactly as the
    shadow work did. Cloud march performance in particular is unknowable in this sandbox, and the reverted
    optimisations above are a warning that intuition about it is unreliable.
  • **R9 removed this deliberately.** Re-introducing sun/sky re-touches ReSTIR bindings, the exposure integrator
    (incident metering was deleted; exposure is frame-only), `RenderScheduler`, the editor outliner and CMake.
    Every one of those seams is listed in `947b8b9` — read that commit before starting each phase.
  • **Tier semantics drift.** Two ladders describing one concept is how they diverge; the mapping in §6 must live
    in `FidelityClassifier` only, with the panel reading it rather than restating it. The shadow round already hit
    this and the gate exists because of it.
