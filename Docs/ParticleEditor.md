# Particle Editor

`Experimental/ParticleEditor/index.html` is a standalone particle editor written in HTML, JavaScript and WebGPU. Its UI follows the layout of `Experimental/ProjectZeroEditor/index.html`: top bar, outliner on the left, viewport in the centre, and inspector on the right, with a status bar along the bottom.

Open it through any static HTTP server. Opening the file directly with `file://` will not load the scripts in every browser. A browser with WebGPU enabled is required.

```
cd Experimental/ParticleEditor && python3 -m http.server 8080
# then open http://localhost:8080/
```

Query parameters:

- `?scene=none` starts with no particle systems.
- `?scene=id,id,...` starts with the listed presets only (IDs come from `js/presets.js`).

## What is in the editor

| System | Kind | Notes |
| --- | --- | --- |
| Sparks | Ballistic, GPU-simulated | Streak-rendered. Emitted by the **Strike** button and by lightning. Affected by the wind field. |
| Lightning and thunder | Procedural bolts | **Strike** creates a bolt with a flash, a thunder cue (**Sound**) and a spark burst at the impact point. |
| Lightning web | Lightning, procedural | Branching arcs between scene objects. Every visible particle system's origin is a node, linked to its nearest neighbours and redrawn on an interval. Arcs are drawn only; they do not strike or light particles. Turn on in the **Lightning web** card. |
| Leaves | Wind-driven, GPU-simulated | Leaf or paper sprites (selectable). They take their velocity from the wind field at their position.
| Tornado debris | Wind-driven, GPU-simulated | Paper debris lifted from a ring around a vortex. Adding it enables the **Tornado** wind component at its origin. |
| Sandstorm | Wind-driven, GPU-simulated | Dense dust (up to about 6,000 grains) fired from the upwind edge. Adding it sets **Prevailing wind** to 8 m/s at bearing 70°. |
| Rain streaks | Precipitation, GPU-simulated | Streaked drops that fall through the wind field and rebound a little off the floor. |
| Hail | Precipitation, GPU-simulated | Ice pellets that drop fast and bounce high off the floor before settling. |
| Snow | Precipitation, GPU-simulated | Flakes that drift through the wind field and land with a soft rebound. |
| Black hole | VFX (kind 5), GPU-simulated, plus screen-space lensing | A glowing accretion disc that is pulled in by an attractor, swirls inward and is swallowed at the swallow radius. The background is bent around the horizon by a full-screen lens pass, and a photon ring sits outside it. Lensing is a screen-space approximation, not a ray-traced metric. Tune it in the **Black hole** card. |
| Heat shimmer | VFX (kind 0), GPU-simulated, plus screen-space distortion | A faint rising plume that bends the scene behind it with a noise offset. Place it over a fire or an exhaust. The bending is an approximation: it shifts the sampled image, it does not refract real geometry. Tune it in the **Heat shimmer** card. |
| Magnetic field lines | Light fibres (analytic) | Light trails drawn along the field lines of a magnetic dipole (path shape **Dipole field line**, axis in the plane of the lines). Sparks ride the lines. They are analytic, so they do not move with the field. Pair with **Magnetic particles**. |
| Magnetic particles | Particles (kind 0), GPU-simulated | Particles steered by a dipole force field: they are pushed along the local field direction and damped across it, so they circulate along the field lines. This is a steering approximation, not an integrated Lorentz force. Tune it in the **Magnetic dipole** card. |
| Bubbles | Particles (kind 0), GPU-simulated | Transparent spheres (shape **Bubble**) with a fresnel rim and a small highlight. They rise on buoyancy, sway (flutter) and grow slightly. |
| Foam | Particles (kind 0), GPU-simulated | Many small bubbles that bunch up and bob near the floor, pushed by the wind. |
| Glitch dissolve | Transition (kind 5), GPU-simulated | The derez path with a **Glitch (blocky)** release wave. Release times come from hashed cells on a 7×5×7 grid plus a rising sweep, so the break-up is blocky rather than smooth. After release, each cube jumps in 12 steps a second. |

Precipitation uses the regular particle kind, not the debris kind. Debris pins to the floor, while the other kinds bounce with the preset's `bounce` coefficient, so rain, hail and snow set a bounce value.
| Embers | Additive VFX | Glowing embers lifted on buoyancy and bent by the wind. |
| Cherry petals | Wind-driven, GPU-simulated | Petals that tumble and follow the wind closely. | |
| Sand grains | Ground, GPU-simulated | Sand kicked up from the floor in short hops (saltation) and carried by the wind. |
| Ash flakes | VFX, GPU-simulated | Grey ash lifted by a fire's heat, drifting and settling onto the ground. |
| Small debris | Ground, GPU-simulated | Twigs, bark and wood chips thrown up and clattering off the ground. Small pieces only; stone-sized rubble is out of scope. |
| Countermeasure flares | Aircraft, GPU-simulated | IR decoy flares dispensed by a jet to break missile lock: white-hot bursts every ~0.9 s from the aircraft origin, falling and burning out behind it. |
| Pollen & dust motes | Ambient, GPU-simulated | Slow glowing motes drifting on the wind. Light shafts are not modelled. |
| Fireflies | Swarm (kind 6), GPU-simulated | Glowing points that wander and pulse (pulse frequency and depth are preset parameters). |
| Insect swarm | Swarm (kind 6), GPU-simulated | Gnat-like flock: separation, alignment and cohesion, plus wander. Brute-force neighbour search, so keep capacity at a few hundred. |
| Water mist | VFX, GPU-simulated | Fine droplets blown off a waterfall or spray: short life, high drag. |
| Splash rings | VFX, GPU-simulated | Expanding ripple rings on the floor. Placed at random ground points, not at each rain or hail impact (no impact events yet). |
| Glass shards | Streak, GPU-simulated | Sharp bright fragments from a broken window. They bounce and glint. |
| Volcanic ash & lapilli | Sphere, GPU-simulated | Hot, heavy ejecta thrown high by a vent: faster and bigger than ash flakes. |
| Dandelion seeds | Wind-driven, GPU-simulated | Very light seeds that float and flutter in the wind. |
| Feathers | Wind-driven, GPU-simulated | Light feathers that drift, spin and sway. |
| Confetti & streamers | Wind-driven, GPU-simulated | Paper pieces that tumble with strong wind coupling. |
| Steam vent | VFX, GPU-simulated | Hot steam rising from a vent and expanding as it cools. |
| Fireworks | VFX, GPU-simulated | Timed bursts (every ~2.6 s, randomised) fired from random points in the sky. Trails are streak-shaped burst particles, not separate rising shells. |
| Light streaks | Light fibre (kind 7), analytic | Bezier fibres with a bright head sliding along each one and a fading tail, plus head sparks. Ported from the Strand Editor. Not simulated, so wind does not move them. |
| Ember ribbons | Light fibre (kind 7), analytic | Rippling sheets of light across a sheet width. The pulse runs outward from the root. |
| Weave trails | Light fibre (kind 7), analytic | Trails that comet along a weaving path, each leaving a tail that pulses outward along the path. |
| Trim trail (stadium) | Light fibre (kind 7), analytic | Trails running round a flat stadium loop, like a light guide following dashboard trim. |
| Derez cube | Transition (kind 5), GPU-simulated | A grid of cubes covers the object's surface (1,536 cells: 16×16 per face). Cubes hold in place, then release in a wave from one corner: each either bursts outward or falls under gravity. A cube that hits the floor bursts once into eight 2×2×2 children. Children are procedural (analytic motion, fading over a set life), not simulated particles, and they do not collide with anything. |
| Cube to coins | Transition (kind 5), GPU-simulated | The same surface grid (384 cells) with gold coins instead of cubes. Coins are released once, burst or fall, and bounce on the floor (restitution 0.35). They never split. Coin resting behaviour is approximate. |
| Melt away | Transition (kind 5), GPU-simulated | The object melts: surface cubes release in patches across the surface (Release wave: Melt), and the pieces drift off on the wind (high wind coupling). Pieces that land stay on the floor and slide with the wind. |
| Dissolve to nothing | Transition (kind 5), GPU-simulated | The object dissolves: surface pieces release in patches (melt wave), drift on the wind, shrink to zero and fade out. |
| Dissolve into butterflies | Transition (kind 5), GPU-simulated | The object melts in patches; each released piece becomes a flapping butterfly that rises (buoyancy), drifts on the wind, wanders (flutter) and fades. The butterfly is a two-pair silhouette with wings that open and close, not a detailed model. Cubes hide once released, so the switch is an instant pop. |
| Reverse derez | Transition (kind 5), analytic flight | Cubes fly in from a shell around the object (Start distance), snap onto their surface cell in a wave from one corner, and hold. Flight is scripted (eased from the shell to the slot), not simulated. |
| Voxel explosion | Transition (kind 5), GPU-simulated | A solid sphere of cubes holds briefly, then every cube bursts radially and tumbles on its own axis. Cubes are sampled randomly inside the sphere, so they can overlap; there is no lattice. No floor breaking. |
| Coin stack | Transition (kind 5), analytic drops | Coins drop one by one into a column on the floor, land, settle with a small damped bounce and stay stacked. Scripted drops, not physics: coins do not collide with each other. |
| Coin fountain | Particles (kind 0), GPU-simulated | Gold coins fired upward from a point, arcing over and bouncing onto the floor. They scatter into a loose heap; particles do not collide, so there is no true pile. |

Swarm particles (kind 6) read their neighbours from a snapshot taken after emission, so the flocking step is race-free. Fireworks use the same burst path as the Burst button, driven by a timer.
| Atoms (LJ gas) | Molecular, GPU-simulated | Lennard-Jones pairs on a spatial-hash grid, Langevin thermostat, reflecting box walls. |
| Chemicals A+B→C | Molecular, GPU-simulated | Same as atoms, with stochastic A + B → C reactions on contact and C → A or B dissociation. |

Particles are stored in one GPU buffer per system, with 80 bytes per particle. Simulation and rendering both stay on the GPU. The CPU only receives the small statistics described under *Readback costs* below.

## Light fibre colour

Light fibres use one of three colour modes, set in the **Colour** card:

- **Solid**: every fibre uses the first stop.
- **Ramp along fibre**: stops blend by position (0 to 1) along each fibre's length.
- **Palette per strand**: each strand takes one stop, picked by a per-strand hash (so the mix is pseudo-random, not weighted).

Up to 8 stops; each has a colour and (in ramp mode) a position. The accent colour is separate.

## Transition systems

Transition systems (kind 5 with a transition block) reform the object each cycle: `burstEvery` sets the cycle and `burstCount` equals the capacity, so every cycle replaces all particles. The hold time is `base + spread × t`, with `t` the normalised position from the min corner of the box. Derez cubes that hit the floor are removed from the particle count and replaced by children, so the alive count falls faster than the particle lifetimes suggest.

## Force fields and black holes

Force fields are invisible regions that act on every GPU particle, including debris and transitions. Add them from **Environment → Force fields**, which opens the **Force fields** inspector (up to 8 fields):

- **Attractor**: pulls particles toward its centre, with an optional **swirl** (sideways push, which gives spiral infall) and a **swallow radius** inside which particles are removed.
- **Repulsor**: pushes particles away from its centre.
- **Reverse gravity**: lifts particles upward (strength in m/s²). It is timed: **Start**, **Duration** and **Period** set a repeating window, and period 0 means once. Radius 0 means the field applies everywhere.

Radius sets the falloff: strength is `(1 − r/R)²` inside radius R, so the pull is strongest at the centre and reaches zero at the edge.

Black hole systems add their own attractor at their origin. Their lensing is a second, screen-space step: each frame the canvas is copied and a full-screen pass bends the background around the projected horizon, with a black disc inside the horizon and a photon ring outside it. The bend is a weak-field mapping (`r − E²/r`), not a ray trace, and it fades with distance from the black hole. Particles are not lensed; they are drawn normally.

Limitations: force fields act on particles, not on rigid meshes. Particles do not collide with each other. The accretion disc comes from the swirl term, not from a Keplerian emitter.

## Heat shimmer, magnetic fields and bubbles

- **Heat shimmer** (`heatShimmer`: radius, strength, frequency, rise speed) adds a second screen-space pass. Inside its radius the sampled UV is offset by a noise field that rises over time and fades toward the edge. There is no black core. It works on any system, and it runs in the same copy-and-lens pass as the black hole.
- **Magnetic dipole** force (type 3): the axis is world Z through the system origin. The drive term pushes along the local field direction, and the guide term damps velocity across it. Both fade with the radius. The 1/r³ magnitude is not used, only the direction. Particles near the centre are softened so the direction does not flip violently.
- **Bubble** is particle shape 6: a billboard with a fresnel rim, a highlight and a near-clear centre. Buoyancy and flutter come from the existing parameters.

Limitations: the heat shimmer distorts the frame, not the particles behind it. The magnetic particles steer toward the local field direction, so they follow the lines qualitatively but are not an exact trajectory.

## Wind field

The wind field is a 3D grid of velocities, 24 × 12 × 24 voxels over a 12 × 8 × 12 m domain. It is carried over from the reference editor and is evaluated each step by every wind-driven system.

Sources (all linear superposition, as in the reference):

- **Prevailing wind**: directional, with strength, bearing, radius and centre position.
- **Passing gust**: travelling gust bands.
- **Turbulence**: scaled noise, controlled by the *Turbulence* slider.
- **Swirl**: a divergence-free curl-noise field, controlled by the *Swirl* slider (m/s). Because it is a curl, it turns the grid over on itself without creating sources or sinks, so arrows visibly swirl while the net flow stays intact.
- **Tornado** (a wind component, type 2): a vortex with a tangential velocity, inflow, and an updraft. Systems such as Tornado debris read it through the grid like any other wind.

The field is visualised as a 3D grid rather than a single plane. Each arrow is one voxel:

- **Direction** is the local wind direction.
- **Colour** encodes speed: blue is calm, cyan moderate, yellow fast, red at or above the full-scale value.
- **Length** follows speed, up to one voxel.

The *Arrow full scale* slider sets the speed that maps to full colour and length. *Show grid arrows*, *Show floor* and *Show domain box* toggle the overlay layers. The arrows are a debugging view of the forces that drive particles and are not part of the final visual.

## Readback costs

The editor keeps simulation state on the GPU. CPU readback is limited to the data the UI actually needs, and every readback is asynchronous: the frame is recorded and submitted without waiting for the map to complete. A result therefore arrives one or more frames after it was requested.

| Readback | Size | Frequency | Used for |
| --- | --- | --- | --- |
| Molecular statistics (alive count, species counts A/B/C, kinetic energy sum) | 32 bytes per molecular system | At most one in flight per system, so about once per frame | Status bar, live counts, temperature readout |
| Wind probe (one voxel, rgba16float) | 8 bytes | On request, while the viewport probe is enabled | Probe readout in the viewport |
| Full particle buffer | 80 bytes × capacity (for example 1,024 atoms = 80 KiB; 65,536 = 5 MiB) | Only when **Benchmark full readback** is pressed | Measurement only; the editor never uses this data |

Why the statistics are cheap:

- A reduction pass on the GPU sums the counts and energy into a fixed 32-byte buffer. The CPU never reads per-particle data during normal use.
- Energy is accumulated as integers (`v² × 1000`) so that atomic adds are exact. The integer sum is safe up to about 65,000 atoms at the maximum speed cap before it overflows a `u32`.

Measured here (headless Chrome with SwiftShader, a CPU software WebGPU implementation, so these numbers are not representative of real hardware):

- The status-bar readback time reached about 790 ms. This value is the time from `mapAsync` to its resolution, which includes queued GPU work. Software emulation makes that queue very slow, so it does not predict real-GPU latency.
- The temperature readout is `<v²>/3` and settled at 0.59–0.60 for a thermostat set to T = 0.6, which is the expected value.

What to expect on real hardware: a 32-byte map adds about one frame of latency. Use **Benchmark full readback** on the target GPU to measure the full-buffer cost before relying on it. Measured cost is not published in this document.

Rule of thumb: keep per-frame readback to the 32-byte statistics. Anything bigger should be requested on demand, asynchronously, and at a low rate.

## Simulation notes

- **Molecular step size.** The molecular step uses a fixed `simDt` (default 0.004). Each frame runs `steps = clamp(round(dt / simDt), 1, 8)` sub-steps, and the sub-step is `min(dt / steps, 1.25 × simDt)`. This keeps the Lennard-Jones integration stable when the frame rate drops and `dt` grows.
- **Thermostat noise must differ per sub-step.** The random seed mixes the particle index, the frame seed and the particle's current position. Using only the frame seed made every sub-step reuse the same noise and pushed the atoms about 2.4× too hot.
- **Wind texture usage.** The wind grid is a 3D texture. It uses `GPUTextureUsage` flags, with separate sampled and storage views.

## Files

- `index.html`: markup and element IDs, loading the stylesheet and the five scripts in order.
- `ParticleEditor.css`: dark theme, DM Sans fonts from `fonts/`.
- `js/presets.js`: system presets.
- `js/shaders.js`: WGSL for simulation, reduction and rendering.
- `js/engine.js`: WebGPU device, buffers, pipelines, readbacks.
- `js/lightning.js`: bolt generation and thunder cues.
- `js/app.js`: editor state, UI, frame loop.

## Where it came from, and what was changed on the way in

The editor was merged from `eosclient0001-rgb/Frontier @ arena/003c3e5f-frontier` (17 commits, unrelated
history). The merge was clean apart from `README.md`, which was an add/add conflict and was kept as this
repository's own.

Six things were changed on arrival. Each is a defect rather than a difference of taste:

| | Finding | What was done |
| --- | --- | --- |
| ❌ | A 118 KB copy of DM Sans sat in `Experimental/ParticleEditor/fonts/`, byte-identical to `EngineContent/Fonts/SunReference/`. | Deleted; the `@font-face` rules now point at the shared set, as `Experimental/Fluid` already does. |
| ❌ | `statStage.mapAsync` could resolve after the system it belonged to had been destroyed, calling `getMappedRange()` on a dead buffer. Removing a system while a readback was in flight crashed the page. | `SystemGPU.gone`, set in `destroy()`; the fulfilment handler returns early. |
| ❌ | The frame loop kept encoding GPU work in a background tab. | `frame()` returns early on `document.hidden` and resets `lastT`, so time does not jump when the tab comes back. |
| ❌ | Nothing could get a scene in or out — no save, no load, no export, no `localStorage`. 52 presets could be tuned and nothing left the page. | `js/handoff.js`, below. |
| ❌ | No tests of any kind. | `CheckHandoff.mjs`, 24 checks, run in CI. |
| ❌ | No single commit point for a parameter change: every inspector row writes straight into `sys.p` from its own closure, so nothing can observe an edit. | `js/edits.js` — the funnel, placed at the six row helpers rather than at the parameter. The handoff subscribes; the 500 ms poll is gone. |

Three further findings were recorded and not acted on, because each is a rewrite rather than a repair:

- ❌ **WebGPU only.** `engine.js` throws when `navigator.gpu` is absent. `Experimental/Fluid` ships both a
  WebGL2 and a WebGPU engine behind one interface; this page has no fallback at all, so on a browser
  without WebGPU the gas emitter's drawer shows a boot banner rather than a viewport.
- ❌ **No build step.** Three IIFEs on a `window.PE` global with hand-maintained cache-busting query
  strings (`?v=20261008j`). Adding a file means editing `index.html`, and forgetting to bump the string
  serves a stale script.
- ❌ **One-letter identifiers throughout** (`d`, `U`, `o`, `s`, `f`, `el`, `$`). This will have to be
  undone when the page is ported to C++, where `AgenticInstuctions/SKILL-Naming.md` applies.
*(The unconditional per-system molecular lattice was the fourth such finding and is now fixed: only kinds
3 and 4 allocate it, and the rest share a 16-byte stub, because the bind group layout still demands the
binding. `kind` comes from the preset and is never edited, so the decision is made once in the
`SystemGPU` constructor.)*

What the page already got right, and should keep: device loss is handled (`d.lost.then` → a banner rather
than a frozen canvas); boot is wrapped so a failure explains itself; DPR is capped at 2 with a
`ResizeObserver`; readbacks are single-flight behind `statPending`/`probePending`; `innerHTML` is only ever
assigned `""`, so there is no injection surface; and the engine/app split with documented uniform slot maps
(`PE.SYS_SLOT`, `PE.GLOB_OFF`) is genuinely clear.

## The edit funnel

`js/edits.js` is the one place an edit passes through. The obvious shape for this is
`setParam(sys, key, value)` called by all thirty-two inspector closures — but that means editing
thirty-two closures, and the thirty-third one written next month will forget. Every one of them is
already reached through six row helpers (`rangeRow`, `vecRow`, `colorRow`, `selectRow`, `checkRow`, and
the action `button`), so the funnel sits **at the widget layer instead**: six call sites, and a new row is
observable by construction, because the only way to draw a row is to call one of the six.

- `For(sys)` — rows capture their system in a closure, which the funnel cannot see, so `renderInspector`
  names it once at the top.
- `Subscribe(fn)` → returns the unsubscribe. A watcher that cannot leave is a leak the first time the
  drawer is opened twice.
- `Quietly(work)` — runs without announcing. This is what stops a host's arriving message from being
  re-announced as an edit and posted straight back; without it the two windows talk to each other forever.
- `Announce(what, value)` / `Through(what, set)`.

A throwing watcher is logged and stepped over, because a subscriber's bug must not freeze a slider
mid-drag.

What this deliberately does **not** catch: a write made in code rather than by a person. That is the right
side to err on — the funnel reports authoring, and a programmatic write is the caller's own business to
report. The handoff keeps a 2 s sweep behind its subscription for exactly those.

## The handoff — how the gas emitter card opens this page

`js/handoff.js` is the whole contract, and it is four functions:

- `Describe(state)` — the selected system as a plain message, or `null` if nothing is selected.
- `Admit(p, Settings)` — writes known keys into a system's parameters and returns how many it took. A key
  the system does not have is ignored; a number arriving as a string or a NaN, a three-vector of the wrong
  length, and a boolean arriving as `1` are all refused. A message is untrusted input, not a merge source.
- `Writable(p)` — only scalars, strings and numeric vectors cross. GPU handles, functions and arrays of
  objects do not, because `postMessage` would either throw or clone the page's live state.
- `Install(state, Host)` — listens for `particle-system`, posts `particle-system-changed`, announces
  `particle-editor-ready` on boot, and subscribes to the edit funnel — coalescing a drag to one post per
  90 ms, with a 2 s sweep behind it for changes that never passed through a row. **With no parent frame
  it installs nothing at all**, so the standalone page is unaffected.

In the editor, `GasEmitterInspector` grew an `Open ParticleEditor` button and `ParticleEditor` in
`GasPanel.jsx` is the drawer behind it — the same shape as the domain's `Open FluidEditor` and `GasEditor`,
hosting this page in an iframe rather than reimplementing it. Natively, `GasCards::GasCardSubject::Emitter`
swaps the Open band's title and subtitle, and `InspectorPanel` raises `ConsumeParticleEditorRequest()`
instead of `ConsumeFluidEditorRequest()` for an emitter row.

**Only four readings cross** (`GasEmitterToParticles` in `GasSpecification.js`): Position → `origin`,
Radius → `radius`, Rise Speed → `buoyancy`, Swirl → `swirl`. Smoke, Fuel and Temperature do not, and this
is deliberate: those are fields a fluid solver integrates, and a particle system has no fields. A made-up
conversion would be worse than none, because it would look as though it worked.

## Force fields — the umbrella wind turned out to be one of

`js/forcefields.js` is the design reference; `CheckForceFields.mjs` holds it to 45 checks. Nothing is
renamed by it. `Engine/DisplayPresentation/WindField.h` keeps its name and its job — it is the
atmospheric model (Ekman shear and veer, gust envelope, curl turbulence), it is well tested, and it
becomes the solver *behind* the flow kinds rather than being replaced by them.

**The distinction that matters is not what a field is called, it is what a field returns.** Three
contributions, and every field is exactly one:

| Contribution | Returns | Applied as | Members |
| --- | --- | --- | --- |
| **Flow** | velocity, m/s | `v += (F(p) − v)·k·dt` — relaxed toward, at the *receiver's* coupling | prevailing wind, gust front, tornado, blast outflow, current |
| **Accelerate** | acceleration, m/s² | `v += F(p)·dt` — coupling has no say | gravity, attractor, repulsor, lift, magnetic dipole, orbit |
| **Damp** | rate, 1/s | `v *= exp(−F(p)·dt)` | drag volume |

Eight of the twelve kinds are not new. Four are the wind components `buildWind` already sums into the
lattice and four are the force types `fieldAccel` already evaluates per particle — and they sort cleanly,
all four winds into Flow and all four forces into Accelerate. The split fell out of the existing code; it
was not imposed on it. That is the evidence the umbrella is a description rather than a wish.

### Why gravity cannot just be another wind

This is the trap the rename invites, so it is asserted numerically rather than claimed in a comment:

| | |
| --- | --- |
| ✔️ | As an **acceleration**, gravity changes a leaf's and a hailstone's velocity by *the same* amount in one step. That is the whole content of Galileo's claim and is not negotiable. |
| ❌ | As a **flow** of the same number, the leaf falls more than fifty times faster than the hailstone, because a flow is relaxed toward at the receiver's own coupling. |
| ❌ | And a receiver with **no wind coupling ignores it entirely** — smuggled-in gravity reaches nothing the wind does not already move. |
| ✔️ | Conversely, ten seconds in an 8 m/s wind leaves a receiver at 8 m/s and no faster; ten seconds of the same number as an acceleration is past 70 m/s and still climbing. One bucket could not have produced both. |

It pays for itself on performance too. Flow fields sum into one 3D velocity texture per frame and sample
in O(1) however many there are — which is exactly what `buildWind` already does. Acceleration fields
cannot: the useful ones are unbounded, and a bounded box would clip them. `Bakeable()` makes that
queryable so the engine does not re-derive it and the two cannot disagree.

### What the checks found

🔴 **The absence of wind is not a wind of zero.** Relaxing toward a flow of `[0,0,0]` is a brake, so an
empty field list silently slowed everything to a stop — a receiver in a place no wind reaches would be
stopped by the *mere absence* of wind, invisibly. `Resolve` now reports `Flowing`, decided by reach and
the time window rather than by the sampled magnitude, and `Advance` relaxes only when something is
genuinely there. Air that is *deliberately* still does still slow you down; empty space does not.

### Who responds

Per the same rule the gas colliders follow, the responding set is data. A field names the channels it
acts on (empty = everything) and a receiver names the channels it is in. "This room is on the Moon, but
only for the rubble" is authored, never compiled in.

### The migration — two lists became one

Wind components and force fields were two arrays, two shapes, two panels. They are now one authored list,
`state.forces`, and the two panels became one grouped by contribution. The wind panel keeps what is
genuinely the lattice's own — scale, turbulence, swirl, arrows, the domain box — because those are
settings of the sampling grid, not fields anyone placed in it.

**The discipline that makes this a refactor rather than a rewrite:** the shaders did not change, and the
payloads they receive are asserted equal to the ones the two hand-written structures produced.
`defaultWind().components` is deliberately still in `presets.js`, unread by the editor, because
`CheckForceFields.mjs` asserts `DefaultForces()` against *that array* — field by field, bit for bit,
bearings to within 1e-9 of a degree. Delete it and the migration loses its only witness.

Two traps the checks caught, both of which would have read as physics bugs:

| | |
| --- | --- |
| 🔴 | **Gravity must pack with radius 0.** The shader reads 0 as *everywhere*; packing the authored 8 m would have made gravity a small ball. |
| 🔴 | **A disabled flow field keeps its lattice slot.** `buildWind` reads a fixed count and tests each entry's own flag, so dropping a disabled one shifts every field behind it into the wrong slot. Acceleration fields are the opposite — only awake ones are packed. |

Gravity reaches the existing shader as a downward `Lift`, the one force type that is already a fixed
world axis. That is a mapping, not a merge: it is authored as gravity, it stays `Accelerate`, and when
the shader grows a type of its own exactly one line changes.

### The native port

`Engine/VolumetricDynamics/ForceFieldSet.h` sits *above* `Engine/DisplayPresentation/WindField.h`, which
keeps its name and its job — the atmospheric model and its carefully split cheap/expensive sampling
interface — and becomes the solver behind the flow kinds.

| Check | Count |
| --- | --- |
| `CheckForceFields.mjs` — the taxonomy and the migration's equality | 91 |
| `CheckForcePanel.mjs` — the one list drawn into a real DOM | 44 (38 without jsdom) |
| `NativeForceFields.cpp` — the same arithmetic in C++, run twice, byte for byte | 64 |
| `ForceFieldParity.py` — the port against the browser, kind by kind | 67 |

The native proof deliberately makes the *same claims with the same numbers* as the browser one. Two files
agreeing on prose is worth nothing; two files agreeing on arithmetic is what makes it a port.

### The native editor UI

The panel itself followed the taxonomy across, by the same route the gas card took.

`Engine/Editor/ForceFieldCardSurface.h` is the native Force Fields panel. It owns **no painters**: every
pill, switch, select, note and card shell is `GasCards`', because the stylesheet is shared and two
painters that resemble each other drift apart. What the file owns is the *stack* — the order, the
grouping, the heights, and the hit regions.

Four things the panel says out loud, each of them structural rather than something a painter remembers:

1. **The grouping is read from `FactsOf(Kind).Give`**, not from a hand-written list, so a kind added to
   the taxonomy cannot go missing from the panel. An empty group paints no heading at all.
2. **A field that reaches everywhere draws no centre and no radius.** Not greyed out — absent. The row
   planner reads the *field*, so an authored override behaves the way the kind's default does.
3. **A kind with no shader path is listed and says so.** A control that silently does nothing is worse
   than one that admits it.
4. **The cost card counts what sums into the lattice against what is evaluated per receiver**, read
   through `ForceBakeable` so the panel and the engine cannot disagree about which fields are cheap.

Routing: `EditorSheetAppearance::ForceFields` → `RowPanel::ForceField` → `InspectorPanel::RecordForceFields()`.
🔴 Unlike a gas domain, the fields are **one set for the scene, not a slot per instance** — a field is a
thing in the world, not a property of whichever row is selected, exactly as the browser's single outliner
row has it. The outliner line counts by contribution (`3 flow · 2 accel · 1 damp · 2 black hole`), because
six fields is not a useful figure and those three numbers are.

| Check | Count |
| --- | --- |
| `NativeForceCard.cpp` — the panel drawn from real ImGui draw commands, with captures | 136 |
| `NativeForceRoute.cpp` — the shipped inspector opening it, clicked not inspected | 48 |
| `ForceFieldPanelParity.py` — the native panel against the browser panel, row for row | 381 |

Captures: `Exhibits/Gallery/ForceCardNative/` (empty, flow only, flow and acceleration, with damping) and
`Exhibits/Gallery/ForceFieldRoute/InspectorRoute.png` (the shipped inspector's own frame, ident strip and
all).

`ForceFieldPanelParity.py` is the sibling of `ForceFieldParity.py`: that one holds the two *taxonomies*
level, this one holds the two *panels* level — which rows a card shows for a given kind, in what order,
with what label, over what span, to how many decimals, in what unit, and which sentences it says. Like its
sibling it **parses** both sides rather than running them, so it needs neither node nor a compiler.

### Still open

- A field's `Acts` channels have no UI yet — the plumbing is there on both sides, the editor has no
  control for it.
- `Reach.Cone` and `Reach.Box` are declared and not yet sampled; `Drag` and `Current` have no shader
  path, which `Packable()` reports and the panel states on the card rather than hiding.
- The 52 presets still describe their wind links in the lattice's old language (type number, bearing,
  x/z). One adapter in `app.js` translates it. Rewriting the presets is a separate job.
- Per-system attractors and magnetic dipoles are still attached to their system rather than being
  entries in the list, which is correct — they move with the system and die with it — but it means two
  places create acceleration entries. The native header says so out loud; the native panel does not yet
  *count* them, because nothing in the editor owns a particle system to count.
- The native panel's name field, centre triple and falloff select are **drawn but not yet editable** —
  the toggle, the strength track, add and remove are wired. A draw-list card has no text input, so the
  name needs the editor's own field widget rather than an invisible button over a rectangle.
- No row in the native editor's feed carries `EditorSheetAppearance::ForceFields` yet: the branch, the
  panel and the routing are proven, but `EditorFeedSequence::BuildSheet` still has to emit it — the same
  gap gas has.

## WebGL2 — closed, will not fix

The page requires WebGPU and will keep requiring it. This is not an oversight to be fixed later:

- WebGL2 **has no compute shaders**. The whole simulation is compute passes over storage buffers.
- It **has no atomics**. The molecular kinds bin themselves with `atomicAdd` into the neighbour lattice;
  there is no equivalent, approximate or otherwise.
- A WebGL2 path would therefore be a *second, lesser simulator* — transform-feedback ballistics with no
  molecular kinds, no lensing and no stats readback — not a second backend behind one interface.

`Experimental/Fluid` ships both only because its solver is texture ping-pong by nature, which WebGL2 does
natively. The comparison does not transfer. A browser without WebGPU gets the boot banner, and the gas
emitter's drawer says so in plain words rather than showing an empty rectangle.

## Identifier naming — deferred to the port, on purpose

The one-letter identifiers (`d`, `U`, `o`, `s`, `f`, `el`, `$`) stay for now. A rename across ~4,700
lines is protected by very little here — the only executable tests are the handoff, force field and
structural checks — so the diff would be large, unreviewable and unguarded. It is cheaper and safer to
rename each file as it is ported to C++, where `AgenticInstuctions/SKILL-Naming.md` applies and a parity
check exists to catch a mistake.
