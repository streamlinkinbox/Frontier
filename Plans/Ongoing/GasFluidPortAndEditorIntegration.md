# Gas Fluid — port to Frontier, bake it, budget it, and attach it to objects

Port the browser gas/pyro simulator at `Experimental/Fluid` into native C++, ship it as both a
standalone host and a Fluid Editor inside the engine editor, let any object own an emitter, and run
the whole thing inside a stated budget so a scene full of smoke still holds its frame rate.

**This file is a plan only. No code from it has been written.**

## What already exists

| Thing | Where | State |
|---|---|---|
| Browser gas simulator | `Experimental/Fluid` | Working. WebGL2 and WebGPU engines, 18 presets, scene save/load, **flipbook bake already implemented** (`src/FlipbookSequence.js`) |
| Its contract | `src/SceneSpecification.js`, `src/presets.js` | `DEFAULT_PARAMS` (84 settings), per-setting ranges, `ValidateScene`, `ConstructPresetParameters` |
| Its proof | `src/SceneMetrics.mjs` | 18 node tests, all passing |
| Six collider primitives | `OBSTACLE_TYPES` | None, sphere, vertical cylinder, horizontal cylinder, deflector slab, **tyre ring** |
| `Project-Fluid` | `Projects/Project-Fluid` | A **liquid** project. Shares a name, shares no solver |
| A global distance field | `Engine/GeometricRaster/{DistanceFieldSpace,GlobalDistanceFieldSpace,DistanceFieldBakeSolver}.cpp` | **Already in the tree and unused by any target.** This is how arbitrary meshes become gas colliders for free |
| Fracture editor port | `Engine/Editor/FractureEditorSurface.h` | The shape a page port takes here |
| Tyre slip | `Engine/PhysicalDynamics/Vehicle/TyreSlipDynamics.cpp` | Already computes the slip ratio a burnout emitter keys off |
| Shader lowering in CI | `.github/workflows/frontier-shaders.yml` | 35 shaders → SPIR-V in 53 s. The volume raymarch joins the table |

The gas solver is **new work**. Nothing in the engine advects a velocity field on a voxel grid today.

---

## 1 · Cost, measured before anything is built

Everything below depends on these numbers, so they come first.

### Live simulation — what a domain costs in VRAM

Per voxel, at half precision: velocity `RGBA16F` ping-ponged (16 B), thermo — smoke, temperature,
fuel, soot — `RGBA16F` ping-ponged (16 B), pressure ping-ponged (4 B), divergence (2 B), vorticity
(8 B), obstacle mask (1 B). **47 bytes per voxel.**

| Grid | Voxels | VRAM | Pressure solve, 22 sweeps |
|---|---|---|---|
| 32³ | 32 768 | **1.5 MB** | 0.7 M voxel-ops |
| 64³ | 262 144 | **12.3 MB** | 5.8 M |
| 96³ | 884 736 | **41.6 MB** | 19.5 M |
| 128³ | 2 097 152 | **98.6 MB** | 46.1 M |

VRAM is not the problem. **The pressure solve is**, and it scales with the cube of resolution.

### Baking the 3D volume — measured, and then dropped

A baked volume is a sequence of 3D textures. Only density and temperature survive a bake (fuel and
soot have already done their work), so `RG8` — 2 bytes per voxel.

| Grid | Per frame | 90 frames (3 s @ 30 Hz), raw | Sparse 8³ bricks, ~20 % occupancy |
|---|---|---|---|
| 64³ | 0.5 MB | 47 MB | ~9 MB |
| 128³ | 4.0 MB | 377 MB | ~56 MB |

**Decided: no 3D baking.** 56 MB for one three-second hero effect buys almost nothing over
simulating it, block compression cannot rescue it (`BC4`/`BC7` on `VK_IMAGE_TYPE_3D` is optional in
Vulkan and missing on much of the hardware), and the one case where it pays — a 64³ effect repeated
thirty times — is a case the 2D flipbook serves for a fifth of the memory and a fiftieth of the
render cost. The numbers are kept here so the decision does not get relitigated from scratch.

Every effect is therefore either **simulated live** or **played back as a 2D card**. There is no
third thing to build, bake, version or ship.

### Baking to a 2D sprite sheet — the better trade for small and distant

An 8×8 flipbook of 256² frames in a 2048² atlas:

| Contents | Uncompressed | Compressed |
|---|---|---|
| Colour + alpha `RGBA8` | 16.8 MB | **4.2 MB** `BC7`, 5.6 MB with mips |
| Motion vectors `RG8` (for frame blending) | 8.4 MB | **4.2 MB** `BC5` |
| Six-way lighting (3 extra channels) | +12.6 MB | +4.2 MB |

**~10 MB for a high-quality looping effect, shared by every instance, rendered as a camera-facing
card at the cost of two triangles.** That is 1/5 the memory of the 3D bake and roughly 1/50 the
render cost, and the browser app already knows how to produce it.

This is the right default for anything small or far away. Its limit is parallax: a card does not
hold up when you walk around it or through it.

### The rule that falls out

| | Live sim | 2D flipbook card |
|---|---|---|
| Hero, player-adjacent, unique | ✅ | |
| Mid-ground, reacting to the world | ✅ | |
| Small, numerous, or distant | | ✅ |
| Repeated identically many times | | ✅ |

---

## 2 · Budget and quality tiers

A domain is not born at a tier; it is **assigned** one each frame by distance and by what is left in
the budget, and it can be demoted mid-life.

| Tier | Grid | Pressure sweeps | **Sim rate** | Raymarch / shadow steps | VRAM | Allowed at once |
|---|---|---|---|---|---|---|
| **Hero** | 128³ | 24 | 60 Hz | 128 / 8 | 99 MB | **1** |
| **Near** | 96³ | 18 | 60 Hz | 96 / 6 | 42 MB | **2** |
| **Mid** | 64³ | 12 | **30 Hz** | 64 / 4 | 12 MB | **4** |
| **Far** | 32³ *or* a flipbook card | 8 | **15 Hz** | 32 / 2 | 1.5 MB | **8** |
| **Card** | baked 2D only | — | playback | — | shared atlas | unbounded |

Worst legal case: 99 + 2×42 + 4×12 + 8×1.5 = **243 MB of VRAM**, which is a defensible ceiling on a
modern card and a configurable one on a smaller card.

**The sim rate is not the frame rate.** The game renders every frame at whatever the display runs
at; a Mid-tier domain merely *steps* 30 times a second and its density is interpolated between the
two newest states in between — the same easing the replication runtime already does for remote
placements. This is what the request for "60 FPS near, 30 FPS far" actually means in a solver, and
it is where most of the saving comes from: halving the step rate halves the pressure solve.

### Fixed rate per tier, never a free-running one

**Decided: a 60 Hz master tick, and each tier steps on an integer divisor of it — 1, 2 or 4.** The
*tier* is reassigned dynamically; the *rate inside a tier* never varies. A rate driven by measured
frame time was considered and rejected on three counts:

1. **It would be non-deterministic**, which throws away everything §4 buys.
2. **Interpolating between two states needs a known interval.** A wandering one makes the
   in-between frames guesswork.
3. **A preset would stop meaning one thing.** Artists tune dissipation, cooling and burn against
   seconds, not against steps; a varying step makes the same preset look different on two machines
   and different from itself one second later.

**The trap, and it is a real one.** Stepping at 30 Hz instead of 60 means doubling `dt`, or the
smoke literally evolves at half speed. And doubling `dt` while applying `smokeDissipation` *per
step* halves the dissipation per second — distant smoke would linger roughly twice as long as near
smoke from the identical preset, which reads as a bug and is very hard to spot in review. So every
rate-like setting is defined **per second** and converted with the real `dt`:

```
Remaining = pow(1 − DissipationPerSecond, dt)      // not  1 − DissipationPerSecond
```

This applies to `smokeDissipation`, `coolingRate`, `velocityDamping` and `burnRate`. The CPU proof
asserts it directly: the same preset stepped at 60, 30 and 15 Hz must reach the same total smoke,
within a stated tolerance, after one simulated second. Without that check the tier system quietly
changes how every effect looks.

**One dynamic exception, and it is still deterministic.** A domain that has just taken a blast runs
at full rate for half a second regardless of distance, because a detonation at 15 Hz judders
visibly. It is deterministic because the trigger is a replicated event, not a frame-time
measurement.

**Tier changes cross-fade** over about a quarter of a second rather than switching on one frame.

`GasBudget` owns three ceilings — VRAM, per-frame milliseconds, and live domain count. When a new
domain would breach one it is demoted a tier, and if it is already at Far it plays a card instead.
A frame-time governor demotes the furthest domain first when the measured gas cost exceeds its
millisecond ceiling for several frames running. **Nothing is ever silently dropped**; the editor
shows which tier each domain is running at and why.

---

## 3 · Collision — all three levels, and the gas pushes back through the wind field

**The world pushes the gas** — three levels, all three built, each opt-in per object through a
`GasCollision` component:

1. **Primitive** — sphere, capsule, box, cylinder, or the tyre ring the simulator already has.
   Rasterised analytically into the obstacle mask each tick. Nearly free, and exact.
2. **Distance field** — sample `GlobalDistanceFieldSpace` where it covers the domain. Arbitrary
   meshes become colliders **with no new code in the solver**, because the engine already bakes this
   field and no target currently consumes it. An object opts in and its mesh is simply there.
3. **Two-way** — below. On by default for debris-weight bodies, off for anything the player stands
   on or drives.

Levels 1 and 2 compose: a primitive is cheaper and exact, so an object that has one uses it, and
anything else falls through to the distance field.

### The gas pushes the world — as a force field, not a readback

The obvious two-way coupling reads pressure back off the GPU and hands it to Jolt. That is the wrong
shape here, three times over: a 128³ `RGBA16F` readback is ~16 MB a frame and stalls the pipeline;
raw pressure gradients inject energy and can destabilise the rigid-body solver; and it is the single
thing that makes the simulation non-deterministic for multiplayer.

**Instead the gas publishes a coarse force field and Jolt samples it — through the seam the engine
already has.** `Engine/DisplayPresentation/WindField.h` already exposes
`WindField::Sample(Wind, Position, Time, OutVelocity)` and is already consumed by `Precipitation.h`
and `VolumetricMedia.h`. A gas domain becomes **another contributor to that same sample point**.

| | Pressure readback | Force-field mirror |
|---|---|---|
| Moved per frame | ~16 MB at 128³ | **32 KB at 16³, 262 KB at 32³** |
| Pipeline | Stalls, or three frames of staging | Async, latency is harmless for drag |
| Stability | Pressure gradients can add energy | Drag is dissipative, stable by construction |
| Determinism | Broken | **Intact — see §4** |
| New interface | A whole coupling path | One more contributor behind `WindField::Sample` |

The force is ordinary aerodynamic drag against the *relative* velocity:

```
F = ½ ρ Cd A · |v_gas − v_body| · (v_gas − v_body)
```

Dissipative, so it can never pump energy into a body the way a pressure gradient can. Buoyancy from
the temperature field is the same shape and rides along with it.

**Where the coarse field comes from is the whole trick** — see §4. It is not a downsample of the GPU
volume. It is its own small CPU solver, which is what makes the force deterministic.

**Honest limits.** A 32³ field cannot resolve the eddies that are visible in a 128³ render, so a leaf
will not swirl exactly where the smoke curls. For pushing debris, cloth and ragdolls that is
invisible; for anything finer, the emitter's analytic velocity is added at the sample point. And the
two fields can drift apart in appearance, which is managed by running the coarse solver from the
*same* emitters and the *same* preset so they agree in bulk even where they differ in detail.

---

## 4 · Two fields, which settles determinism as a side effect

Every gas effect runs **two** solvers from the same emitters and the same preset:

| | **Coarse field** | **Fine volume** |
|---|---|---|
| Where | CPU, worker thread | GPU |
| Grid | 32³ (1.5 MB, ~0.7 M voxel-ops) | 64³–128³ per tier |
| Fixed step, fixed sweeps, integer-seeded noise | **Yes** | No |
| Deterministic across machines | **Yes** | No, and never needs to be |
| What reads it | **Physics forces** via `WindField::Sample`; line-of-sight queries if ever wanted | The raymarch, and nothing else |
| Replicated | By cause (see below) | Never |

This one split answers three separate questions at once:

- **Two-way physics without a readback.** The force field Jolt samples is computed on the CPU, so
  there is no GPU readback at all — not a smaller one, *none*. §3's force-field mirror is simply
  this field.
- **Determinism, free.** Because the forces come from a fixed-step CPU solver with a fixed
  iteration count and integer-seeded noise, **two-way coupling is deterministic by construction**.
  The GPU volume may differ between an NVIDIA and an AMD machine; nothing that touches gameplay
  reads it.
- **Occlusion, if it is ever wanted.** The field that answers "is there smoke between these two
  points" already exists and is already authoritative.

**Multiplayer replicates the cause, never the fluid.** A fracture sends "piece 12 broke at this
transform with this energy"; a tyre sends its slip ratio. A handful of bytes each, and both already
fit the replication runtime in `Projects/Project-Networking` — `ReplicationSequence` carries exactly
this kind of small authored event. Every client then runs the same deterministic coarse field from
the same events and gets the same forces, and renders whatever fine volume its hardware manages.

**Single-player** runs the same two fields; it simply never has to agree with anyone.

The coarse solver is not free, but it is close: 32³ is 0.7 M voxel-ops against the fine field's 5.8 M
to 46 M, it runs off the render thread, and it replaces a 16 MB-per-frame readback.

---

## 5 · The port itself

### 5.1 `Engine/VolumetricDynamics` — the solver, no graphics

| File | What |
|---|---|
| `GasDomain.h/.cpp` | Grid, world bounds, thermo channels, staggered velocity, tier |
| `GasAdvection.cpp` | Semi-Lagrangian and MacCormack |
| `PressureProjection.cpp` | Jacobi divergence removal |
| `VorticityConfinement.cpp` | The curl-restoring force |
| `CombustionSequence.cpp` | Burn rate, heat, soot, expansion, cooling |
| `GasPresetLibrary.h` | **Generated** — the 85 settings and 18 presets, from `presets.js` |
| `GasBudget.h/.cpp` | The tier table, the ceilings, the governor, the per-second → per-step conversion |
| `CoarseGasField.h/.cpp` | The 32³ fixed-step CPU field: deterministic, feeds physics, never rendered |
| `GasWindContribution.h` | The coarse field behind `WindField::Sample`, plus the drag and buoyancy terms |

The browser file stays the single source of truth for tuning; the proof fails when a generated
file is stale. **Scenes cross as TOML, never JSON** — Frontier already carries toml++ and already configures
itself from TOML, so a JSON scene would be the only JSON in the build. (Same pattern as `CompileShaders.py` reading the shader table rather than copying it.)

**Proof** — `Exhibits/Workbench/GasFluid/`, a CPU mirror with no window: a closed domain conserves
smoke; divergence after projection is below a stated bound; every generated preset validates;
the tier table never exceeds its stated VRAM; **the same preset stepped at 60, 30 and 15 Hz reaches
the same total smoke after one simulated second** (the per-second conversion in §2); **the coarse
field produces bit-identical readings across two runs and across a rebuild** (determinism, which is
the only thing multiplayer rests on); and the three rules the node suite already enforces —
detonations keep ≥ 28 voxels per world unit, cold effects carry no fuel on either path, sand sinks
and fire rises.

### 5.2 Rendering

`Engine/DisplayPresentation/VolumeRaymarch.*` and one Slang shader added to the shader table, so CI
lowers it with everything else. Vulkan has real 3D textures, so the browser's tiled 2D atlas — a
WebGL2 workaround — is dropped. Keep the lighting vocabulary (`densityExtinction`, `smokeAlbedo`,
`shadowSteps`, `phaseAnisotropy`, `internalScattering`) so a scene reads the same in both.

`Frontier.exe` stays the only windowed host. The solver never touches a swapchain.

### 5.3 Standalone host

A target beside `Project-Fluid`, with `option(FRONTIER_GAS_ONLY)` next to the existing
`FRONTIER_FLUID_ONLY` so the gas work builds on a machine with no Vulkan. It loads and saves the
**same** `frontier-fluid-scene` version 1 JSON the browser writes — save in the browser, open in the
native host, compare. That is the parity test, and it is nearly free.

### 5.4 The editor

**Outliner.** `EditorInstanceCategory::Fluid` with its own glyph. Selecting it fills an
`EditorSheetAppearance::Fluid` sheet with the few settings worth having inline — preset, tier, grid,
bounds, and the handful of sliders a scene is usually nudged with — plus a live readout of the tier
the budget has actually assigned it.

**The Fluid Editor.** Everything else is a full page opened from the inspector, exactly as the
fracture card opens the fracture editor. `Engine/Editor/FluidEditorSurface.h`, drawn with the same
`ControlPanel` widgets, a **port of the browser page** rather than a new design: preset rail and its
four filters, grouped inspector, viewport with bounds box and voxel grid lines, debug channel
selector, and the flipbook bake panel.

**Proof** — `Exhibits/Workbench/FluidEditor/`, matched against the bundle the way `BundleParity.py`
already matches the Project Zero editor.

### 5.5 Emitters on other objects

A **domain** is a scene object with bounds and a budget. An **emitter** is a component on *any*
object: a transform, a shape, and a preset's emitter half. Each emitter resolves to the nearest
enclosing domain, or to an implicit one sized from its own radius. That is what lets a tyre and a
fracture both smoke in one level without either owning a 128³ grid.

| Event | What it spawns | Tier |
|---|---|---|
| A fracture piece separates | One-shot `brick_fracture_dust` at the break, scaled by piece volume | Card, or Mid if close |
| Tyre slip ratio passes threshold | Continuous `tyre_burnout` at the contact patch, rate driven by slip | Mid or Near |

Both drive one small interface — `GasEmitterSource`, answering "where, how big, how hard, which
preset, this tick" — so neither fracture nor tyre knows anything about gas.

---

## 6 · Order

### Landed

**Steps 1, 2, 3, 4 and 7 are built and proved** — `Engine/VolumetricDynamics/`, header-only, and `Exhibits/Workbench/GasFluid/`
at **PASS 198 + 48 + 47**, wired into the `proofs` job of `frontier-build.yml`.

| File | What landed |
|---|---|
| `GasQualityAllowance.h` | The five rungs, the three ceilings, the governor, and `RemainingAfter()` |
| `CoarseGasField.h` | The 32-cubed reproducible reading: emission, buoyancy, transport, 12 fixed Jacobi sweeps |
| `GasCollisionIntake.h` | Admission ① five primitives incl. the tyre ring, admission ② the distance-reading seam |
| `GasWindContribution.h` | Two-way, behind `WindField::Sample`, off until `CouplingEnabled` is set |
| `GasPresetLibrary.h` | **Generated** from `presets.js`: 85 settings, 18 presets, `--check` guards it |
| `Tools/Build/GenerateGasPresets.py` | The transcription, and the staleness guard in CI |
| `DisplayPresentation/VolumeRaymarch.h` | The lighting and integration, CPU-authoritative |
| `Shaders/GasVolumeRaymarch.slang` | Its transcription; entry 2 of 36 in both shader tables |
| `GasSceneCodec.h` + `GasSceneFields.inl` | The scene crossing, in **TOML** via toml++; the `.inl` is generated |
| `Experimental/Fluid/Samples/` | The committed parity corpus, written by the authoring tool |

Header-only throughout, so no source list changes: `WindField.h` is in none of them either, and the three
build routes enumerate `.cpp` files explicitly. The first target to consume these is step 3.

Two findings from building it rather than planning it:

- **The reproducibility claim has to be observed, not asserted.** The runner builds and runs the checks twice
  into separate directories and compares the transcripts, because a suite that only ever runs once cannot see
  the property it exists to defend.
- **The drag term needed a clamp that the plan did not anticipate.** Drag is dissipative in the continuum, but
  integrated explicitly a light body in a fast flow can be handed an impulse larger than the one that would
  equalise the two velocities, and it then oscillates with growing amplitude. Capping the impulse at exactly
  that equalising value makes overshoot impossible for any mass, interval and coefficient. Without it the
  "stable by construction" claim in §3 is false for light debris, which is precisely the content two-way
  coupling exists for.

### Remaining

1. ~~**The coarse CPU field**~~ — 32³, fixed step, deterministic, plus the budget and tier tables. No
   GPU, no window, fully provable, and it is what physics and multiplayer depend on
2. ~~Solver stages and the generated presets~~ — the presets and settings landed; the MacCormack
   and combustion stages join when the displayed volume needs them in step 3
3. ~~Volume raymarch~~ — landed. `VolumeRaymarch.h` is authoritative and the `.slang` is its
   transcription, so a disagreement is a diff rather than a mystery. Captures in `Exhibits/Gallery/GasField`
4. ~~Scene round-trip against the browser, and the standalone host~~ — landed, and stronger than planned:
   the engine reads the committed corpus and writes it back **byte for byte**, and `Project-Gas` behind
   `FRONTIER_GAS_ONLY` opens, runs and renders a scene with no window, no Vulkan and no SDK.
   `Engine/VolumetricDynamics/GasSceneResolve.h` is the seam, and running it found three defects a byte
   comparison cannot see — §6.1 below. 🚩 Sample *content* is deferred —
   `Plans/Deferred/GasSampleSceneRefinement.md`
5. ~~**2D flipbook bake**~~ — landed. `Engine/VolumetricDynamics/GasFlipbookSheet.h` is the browser's layout
   exactly (4/16/32/64 tiles, 64–512 px, 12/24/30/60 per second, 0–3 s warm-up, left to right then top to
   bottom, straight coverage in alpha), plus `TileAtElapsed()` — one float in, one index out, which is the
   whole per-instance cost of the rung — and a TOML descriptor beside the sheet rather than the page's JSON.
   `PngWriteCounterpart::WritePng` grew RGBA so the coverage survives the file. Proof
   `Exhibits/Workbench/GasFluid/NativeGasFlipbook.cpp` at **PASS 74** bakes a real camp fire through the
   shipped solver and the shipped integration, twice, and compares the two sheets byte for byte:
   `Exhibits/Gallery/GasFlipbook/CampFireSheet.png` with `CampFireSheet.toml`. 🚩 The colour in that capture
   is the known-wrong fire rendering, deferred deliberately; the sheet mechanism is what is proved here
6. ~~**Collision levels 1 and 2 — primitives, then the global distance field**~~ — landed.
   `GasObstructionConsent.h` is the opt-in as data: one record per object, **every level off by default**,
   with the cull (scene order, tail dropped, never reordered), `AdmitObstructions()` as the whole per-advance
   step, and `AdmitBoundaryMotion()` so a moving obstruction pushes the air **in front of** it rather than
   inside itself, where the solver stills it. `ConstructCouplingConsent()` makes the object's record the one
   place level ③'s switch is read from — a stale toggle in saved tuning loses to it.
   `GasSceneDistanceIntake.h` binds level ② to the real `GlobalDistanceFieldSpace`, and the proof links it
   rather than mocking it: an L-shaped wall is baked into a `DistanceFieldSpace`, registered, rasterised and
   obstructs a burning plume with no primitive anywhere. Proof
   `Exhibits/Workbench/GasFluid/NativeGasObstruction.cpp` **PASS 38**, run twice and compared, with three
   captures in `Exhibits/Gallery/GasObstruction/` (**PASS 47** with the routing rule below).

   **A moving body is asked, not rebaked.** `GlobalDistanceFieldSpace` composites placements into a world
   volume and carries a translation and a scale and **no rotation**, so a vehicle read out of it is both
   stale and unturnable. `GasRigidDistanceBody` instead carries the query **into the body's own space** by
   the inverse of its transform — `local = Basisᵀ·(world − Translation)/Scale`, `distance = sample·Scale` —
   which is what Unreal does with mesh distance fields: baked once in object space, never rebaked, rotation
   free because it is the query that rotates. A body at 30 m/s costs exactly what a parked one costs.
   `AdmitBodyMotion()` pushes air with `V + ω × r`, so a wheel spinning on the spot still drags air.
   `ReadRigidSceneDistance()` mins the moving bodies against the static composited world — a car drives
   across terrain, so they are not alternatives.

   **The routing rule.** `AssignObstructionLevel()` decides the level from the one fact that settles it —
   whether a primitive was authored. A cube, sphere, capsule, cylinder or tyre ring takes level ①; a vehicle
   body, a rock, a fractured wall or terrain takes level ②. Two caveats the table cannot state: anything that **moves** is read
   in its own space rather than out of the composited world (`PrefersLocalReading()` says so above a
   twentieth of a voxel per advance, and advises only — it rewrites nothing), and geometry **thinner than half a voxel** leaks at either level, which
   is the lattice and not the admission. 🚩 Still open: which bodies consent by default in a
   shipped scene — the answer stays in the scene, not in code
7. ~~**Two-way through `WindField::Sample`**~~ — landed early with step 1, because the coarse field made it a
   contributor and a drag term rather than a coupling system. 🚩 Which bodies consent is still unanswered and
   is deliberately left in the scene rather than guessed in code
8. Outliner row and inline inspector — fluids exist in a level
9. Fluid Editor page — fluids become editable
10. ~~**Emitter component, then the fracture and tyre hooks**~~ — landed.
    `Engine/VolumetricDynamics/GasEmitterComponent.h` is the component and the router;
    `GasGameplayEmission.h` is the two hooks, and neither the tyre solver nor the fracture runtime includes
    either of them. Proof `Exhibits/Workbench/GasFluid/NativeGasEmitters.cpp` at **PASS 60**, built and run
    twice like the rest of the directory, now **PASS 68** with the vehicle seam closed. 🚩 The fracture hook
    has nothing to call it yet — fracture in this tree is the editor UI only — so `GasFractureSeparation` is
    the shape of the call rather than a live caller. The tyre hook, by contrast, has a live one:
    `GasVehicleIntake.h` reads `Frontier::Vehicle::VehicleTelemetry` straight into a `GasTyreContact` and is
    the only file in the engine that includes both the vehicle and the gas

### 6.0 · What landed for step 10

| Piece | What it is |
|---|---|
| `GasEmitterComponent` | Carrier identity and placement, offset, spread, radius, strength, drag share, and a one-shot request count. A plain aggregate: it is saved, replicated and diffed |
| `ResolveHostExtent` | The **nearest enclosing** authored domain, ties to the lower index. Nested domains are legal, and "nearest" is what makes a room inside a street win |
| `ConstructImplicitExtent` | Six radii across, source a fifth of the way up, clamped to 1–24 m. An emitter nobody authored a domain for still runs rather than silently doing nothing |
| `GasVehicleIntake` | The adapter: one shipped `WheelTelemetry` becomes one `GasTyreContact`, patch and travel and unsigned slip and load. Nothing was added to the vehicle for it, which is the claim the hook makes about itself |
| `ResolveEmitters` | One deterministic sweep; consumes exactly one request per one-shot per call, so nine pieces in a frame are nine puffs |
| `ConstructFractureEmitter` | Radius from the **cube root** of the piece volume — eight bricks is twice the puff — strength from the parting speed, capped at Mid and carded past 12 m |
| `ConstructTyreEmitter` | Strength rises from zero at κ = 0.18 rather than switching on, scaled by load; a **disc** source that spends a fifth of its rise and inherits 35 % of the patch's travel, so the cloud is laid down the road |
| `CubeRootOf` | Exponent-seeded, four fixed Newton steps. `std::cbrt` is not promised bit-for-bit across platforms and this number scales a radius multiplayer compares |

The hooks are pure: they read the event and return a component. They do not ignite a residency, claim a rung
or touch a budget — the governor does all three after it has seen the whole level, which is what stops a wall
collapsing off-screen from evicting the fire the player is standing in front of. That is asserted, not hoped:
forty-one claims, forty of them dust, and the camp fire keeps Hero without ever being demoted.

### 6.1 · What running a scene found that reading one did not

Step 4's first half proved the file crosses byte for byte. Its second half ran what crossed, and the
distance between those two statements turned out to be three defects, none of which any structural or
byte-level check could have reported. They are recorded here because they are the argument for the
standalone host existing at all:

| Defect | What it looked like | Why nothing else saw it |
|---|---|---|
| An authored loss is the coefficient 𝑘 in exp(−𝑘·Δτ); the solver wants the fraction shed per second | Every scene ran with **no heat in it at all** — `camp_fire` authors 1.35 and `RemainingAfter` clamps anything ≥ 1 to zero | Same type, same units on the label, plausible magnitude. The file crossed, the field stayed reproducible, and the plume still rose on smoke weight |
| The authored falloff is a smoothstep ending at 1.35 radii; the injector's is a gaussian ending at 3σ | The source came out four times wider — sixty times the volume — and the first render was a flat orange wall at 100 % coverage | A radius is a radius until something renders it |
| `CoarseGasField` conserves smoke; the authoring tool vents its sides and roof unless the scene is enclosed | A continuous source filled a sealed box in two seconds | Conservation is the *correct* behaviour for the determinism checks, which is why it was never questioned |

A fourth is a gap in the format rather than a defect in the code: **a scene with no emitter and a loaded
blast is a one-shot, and nothing in the file says so.** The authoring tool knows because the preset carries
`DetonateOnLoad`, but a scene stores settings, not the preset it came from. Two of the eight committed
samples are one-shots, and advancing them without firing leaves an empty cube that passes every structural
check there is. It is inferred from the settings for now — `GasSceneRun::OneShot` — and carrying an explicit
trigger is a version-2 format question, listed in §7.

Two more differences are spent in the seam rather than being defects. The authoring emitter *asserts* a
reading each frame where the solver's injector *adds* one, so the source region is capped after injection —
without it a 3.5 K emitter reached 17 K and a 3.2 m/s one reached 80 m/s. And the obstacle numbering is
**not** the shape enum: `OBSTACLE_TYPES` and `GasColliderShape` agree on 0, 1 and 5 and disagree on
everything between, so a cast compiles and silently turns a deflector slab into a cylinder. A written table
is used instead, and the two shapes that cannot be represented at all — a cylinder lying along X, a tyre ring
standing upright — are admitted as their closest axis-aligned relative with a flag saying so.

### 6.2 · The standalone host

```
Project-Gas read    <scene.gasscene.toml>                 what this build understood, and what it did not
Project-Gas cross   <scene> [--into <path>]               writes it back and compares byte for byte
Project-Gas advance <scene> [--advances N] [--blast]      runs the coarse field and reports what is in it
Project-Gas view    <scene> --picture <p.png> [--extent N]  renders through the scene's own camera
```

`cmake -S . -B Build/GasOnly -DFRONTIER_GAS_ONLY=ON && cmake --build Build/GasOnly && ctest --test-dir
Build/GasOnly` — ctest runs `cross` over every committed sample, so adding a scene adds a test. The host
decides nothing: every conversion comes from `GasSceneResolve.h`, every reading from `GasSceneCodec.h`.
A standalone tool that quietly tunes its own copy of the effect is worse than none, because its pictures
then prove nothing about the engine.

Step 5 is the remaining real work. Step 1 moved to the front because the coarse field is load bearing
for physics and multiplayer rather than a contingency, and carrying step 7 with it cost almost nothing.

## 7 · Still open

- **Whose budget?** The ceilings are a proposal, not a measurement. Pin them to a target card before
  step 1 finishes.
- **Flipbook authoring.** Six-way lighting triples the atlas. Worth it, or is one lit sheet plus a
  normal enough for this engine's look?
- **Should a scene carry its trigger?** A one-shot is currently inferred from "no emitter, loaded blast".
  That reading is correct for all eight samples and is still an inference. A version-2 format with an
  explicit trigger — and the preset key the scene was built from — would end the guess.
- **Which bodies get two-way by default?** Debris, cloth and ragdolls clearly yes. Vehicles are the
  argument: a burnout cloud pushing the car that made it is physically real and almost certainly
  unwanted.
- **Does the coarse field need to answer line-of-sight?** It can. Whether any gameplay asks is a
  design question, and the answer only adds a query, not a solver.

## Deliberately excluded

- Liquids — `Projects/Project-Fluid` owns those.
- WebGPU — native has Vulkan and needs no second backend.
- The tiled 2D atlas for *live* volumes — it exists only because WebGL2 has no 3D textures.
