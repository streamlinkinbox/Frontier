# Frontier Fluid — real-time WebGPU water for racing games

Engine-agnostic WebGPU module that **simulates** water and mud on the GPU and renders it: sea and
swell breaking on a beach, rivers flowing down a carved path, puddles and mud pits that cars drive
through, spray, caustics and buoyancy.

Nothing is faked. Every wave comes out of the solver (there is no analytic wave mesh anywhere,
near or far), spray particles carry and return real water volume, and caustics are traced through
the simulated surface. The world is simulated twice at once — a coarse grid for the whole visible
bay and a fine grid around the car — and the two are coupled in both directions, so the sea you
see far away is the same water that runs through the fine domain. Rendering uses a
**camera-adaptive water mesh**: about one simulation cell per vertex where you look, growing
coarser with distance, like a Nanite-style resolution pyramid.

The demo also has **terrain sculpting** (raise / lower / smooth / flatten / paint) that edits the
golive heightfield the solver runs on, so carving a channel immediately fills with water.

```
engine/fluid/
  src/                 the module (vanilla ES modules, no dependencies, no build step)
    FluidSim.js        shallow-water + mud solver, object coupling, queries, stats
    FluidParticles.js  spray / mist / rain particles coupled to the grid
    DomainLink.js      two-way nesting between a fine playable grid and a coarse far-field grid
    FluidRenderer.js   adaptive water surface, caustics, particle rendering, ground-shader hooks
    Buoyancy.js        RigidBody, BuoyancyProbe, FluidQueryBatch (CPU side)
    RiverPath.js       carve a river (or a track) along a spline, fill it, add the inflow
    shaders/           all WGSL (sim, particles, link, render)
  demo/                drivable test world (stand-in for your engine) + terrain sculpting
  test/                GPU numerical tests, headless render + browser tests
```

## Run the demo

Run the bundled dev server and open the root URL. It redirects to the demo:

```bash
node engine/fluid/demo/serve.mjs 8080    # or: cd engine/fluid && npm run demo
# open http://localhost:8080/            (?q=low|medium|high|ultra)
```

The server sends `Cache-Control: no-store` and puts every build under `/b/<build>/`. After an edit,
every module has a new URL, so neither the browser nor a proxy can keep serving stale JavaScript.
The build id is shown in the menu's subtitle. Any static server works too, but plain ones such as
`python3 -m http.server` send no cache headers, so you may see old code after an update until you
hard-reload (Ctrl+Shift+R).

Needs a WebGPU browser: Chrome / Edge 113+ (desktop), Safari 26, Firefox 141+ (Windows).

**Camera: Unreal Engine viewport controls.** Grabbing the view with the right or middle mouse
button (or with `Alt`) takes over from the car camera and puts you in the free camera. Press `C` or `F8` to go back to the car.

| Camera (free / Unreal) | |
|---|---|
| `RMB` drag | look around |
| `RMB` + `W A S D` | fly forward / left / back / right (also arrows and numpad `8 4 2 6`) |
| `RMB` + `Q` / `E` | down / up in world space (also `PgDn` / `PgUp`, numpad `7` / `9`) |
| `RMB` + `F` / `R` | down / up in camera space |
| `RMB` + `Z` / `C` | zoom out / in (FOV); snaps back when `RMB` is released (numpad `1` / `3`) |
| `RMB` (or `LMB`) + wheel | camera speed 1–8 (0.5 → 64 m/s); `Shift` while flying = ×3 |
| `LMB` drag | move forward / back on the ground plane and turn left / right |
| `MMB` drag, `LMB`+`RMB` drag | pan up / down / left / right |
| wheel | dolly forward / back (car cameras: zoom) |
| `Alt` + `LMB` / `RMB` / `MMB` drag | orbit / dolly / track around the pivot: whatever is under the screen centre, or the last focused object |
| `LMB` click | select the car or a prop (orange outline) · `Esc` deselect · `Delete` removes a prop |
| `F` | focus: frame the selection, or the car |
| `C` · `F8` | cycle chase → low → orbit → free · toggle free ↔ car camera |

The free camera can't go below the terrain or water surface, and it stays near the world. The camera
is engine-agnostic: `demo/EditorCamera.js` holds the camera maths with no DOM or GPU code,
`demo/input.js` maps mouse and keys to it, and `demo/picking.js` handles ray picking against
bodies and the heightfield.

| Driving & world | |
|---|---|
| `W A S D` / arrows | drive (AWD, ABS-less, handbrake on `Space`). Not while `RMB` is held, when they fly the camera |
| `R` | reset / flip the car upright |
| `1`–`5` | beach · river ford · mud pit · rapids · puddles |
| `X` / `B` | drop crates / drop a 2-tonne cannonball (free camera: where you look) |
| `M` | sculpt mode: the left mouse button paints the terrain instead of moving the camera |
| `U` | undo the last sculpt stroke |
| `T` `V` `P` | rain · debug view (depth / speed / mud / normals) · pause |
| `H` · `G` · `?` | hide / show the menu · game view (hide all UI, as in UE) · controls sheet |
| Gamepad | RT throttle, LT brake, A handbrake, left stick steer |

The panel also has live sliders for swell height and period, river flow and wind, a Camera section
with the mode, fly speed, FOV and optional mouse lock, and a **Terrain sculpting** section
(see below). `?ui=hidden` starts with the menu hidden and
`?ui=game` starts in game view. The menu state, camera speed and FOV are remembered between visits.

## What is simulated

| Requirement | How |
|---|---|
| Sea, shallow water, puddles, mud | One GPU shallow-water (Saint-Venant) solver over a heightfield bed, wet/dry fronts, per-cell sediment concentration with Bingham-plastic rheology — the same equations everywhere, no special-cased "ocean" layer |
| No fake waves | Swell is fed into the coarse grid through its seaward edge (a relaxation zone with physical amplitude/period/direction) and then propagates, shoals, refracts, steepens and breaks as bores purely in the solver. Wakes, bow waves, splashes, wind gust forcing — all solver dynamics. Nothing is a moving sine mesh |
| Two resolutions, one world | The visible bay runs on a coarse 1400 m grid (≈3.7 m cells) while the 160 m playable area runs on the fine grid (0.16–0.63 m cells). `DomainLink` copies the coarse state into the fine border each step and averages the fine state back into the coarse cells it covers, so both stay one continuous body of water |
| Nanite-style water resolution | The fine surface is drawn with one draw call from a camera-warped grid: ~1 vertex per simulation cell near the camera, cells growing cubically with distance. No LOD seams, no cracks; the far side of the same mesh is a few times coarser, and the coarse grid takes over beyond the playable area |
| Terrain sculpting | Raise / lower / smooth / flatten / paint brushes write the solver's own bed (`FluidSim.applyBed`), keeping the water *surface* in place so raising ground squeezes water aside and digging fills with water. Undo, reset, brush-size and strength controls |
| River along a path | `RiverPath` carves a channel along a spline into the bed and adds a volume source at the head; the water flows downhill under gravity, speeds up over the rapids, pools, crosses fords and enters the sea |
| Interactive vehicles | Car body + wheels are rasterised into the grid every frame as solid volumes: they push water away (bow wave, wake, trough behind each tyre), drag it along, leave persistent ruts in mud (yield stress) and get pushed back (buoyancy, drag, current) |
| Splash & spray | GPU particles emitted where the simulated surface breaks (Ihmsen et al. potentials: wave crest, kinetic energy) and by wheel / bow emitters. Spray removes water from the grid and returns it where it lands. Soft, velocity-stretched droplets with forward scattering. There is **no foam** — no foam particles and no foam shading — and no volumetric splash solver: broken water is shown by the simulated bore plus spray |
| Caustics | Sun rays refracted through the simulated surface onto the bed; intensity = change of projected triangle area, with absorption and chromatic dispersion |
| Buoyancy | Async GPU point queries (pressure head, depth, flow, mud) feed CPU rigid bodies (Archimedes + quadratic/linear drag + density of mud) |

## Physics

### Shallow-water solver (`shaders/sim.wgsl.js`)
Staggered (MAC) grid: depth `h` and mud concentration `c` at cell centres, `u`/`v` on faces.
Per frame the step is sub-stepped to the gravity-wave CFL (`dt ≤ 0.4·dx/(√(g·h_max)+2.5)`,
even number of sub-steps, ping-pong buffers):

1. **Momentum advection** — Stelling & Duinmeijer (2003) momentum-conservative upwind scheme.
   Correct bore / hydraulic-jump speeds (breaking surf, rapids) and correct wet-front speeds
   (dam-break test matches Ritter's analytic solution).
2. **Velocity update** — pressure gradient of the effective head
   `η* = B + h + k·s(objects) + p_wind/ρg`, wet/dry via hydrostatic reconstruction (Audusse et
   al.), implicit Manning bed friction, **Bingham mud** (O'Brien & Julien 1988: yield stress
   `τy = a·(e^{bCv}−1)`, viscosity `μ = a'·(e^{b'Cv}−1)`), wind shear stress, no-slip drag
   towards moving bodies, inflow momentum, sponge / wave-maker relaxation.
3. **Outflow limiter** — scales a cell's outgoing fluxes so it can never go negative.
4. **Height update** — conservative finite-volume fluxes (same upwind face depth as the
   momentum step) for water and sediment, sediment mixing, sources/drains, rain, infiltration
   of thin films, boundary relaxation.

Mass is conserved to float precision (tests: relative error ~1e-7 over thousands of steps), a
lake at rest over rough terrain with islands stays at rest (`|u| < 3e-6 m/s`).

### Object coupling
Each primitive (box / sphere / cylinder — wheels are cylinders) is ray-cast vertically per cell
to get its occupied interval `[z0, z1]`. The submerged thickness `s` raises the pressure head;
for bodies piercing the surface the head acts like a stiff lid (`k` up to 60, limited by the
local gravity-wave CFL), so water is expelled from under the hull/tyre and the head under the
body equals the surrounding level — exactly what buoyancy needs. Test: a floating box pushes the
water under it down to its hull bottom and reports a head within 1 cm of the free surface.

### Particles (`shaders/particles.wgsl.js`)
Pool of up to 512k particles (48 B each) with a GPU dead-list allocator and indirect draw.
Kinds: spray, mist, rain. Ballistic droplets with size-dependent drag relative to the wind;
volume exchange through fixed-point atomics so splashes genuinely move water (test: total volume
returns to 2e-6 after all spray has landed).

### Two nested domains (`DomainLink.js`, `shaders/link.wgsl.js`)
A shallow-water grid fine enough for tyre ruts is far too fine for a 1.4 km view. Instead:

1. **Coarse grid** — 384² cells of 3.65 m over the whole visible bay (1400 m). Two open edges,
   one wall, and a seaward edge where the swell relaxation zone lives. Lower resolution, lower
   Manning friction, no particle emission.
2. **Fine grid** — 256²–1024² over the 160 m playable square, driven by the coarse grid.
3. Every frame: `coarse.step` → `link.edge` → `fine.step` → `link.interior`.

`link.edge` bilinearly samples the coarse depth, mud and velocity along the fine border, so waves
and river flow enter the playable area instead of reflecting off its edge. `link.interior`
supersamples the fine grid 4×4 into every coarse cell that the fine patch covers, so the coarse
sea carries exactly the water (and momentum) the fine patch holds — including river water leaving
the playable area and car displacement. Both kernels are one dispatch over their own grid.

### Terrain sculpting (`demo/TerrainEditor.js`)
The brush works on the same CPU heightfield the solver uses, then pushes it to the GPU:

| Tool | Effect |
|---|---|
| Raise / Lower | moves the bed up or down under the brush with a smooth falloff |
| Smooth | relaxes the bed towards its neighbourhood average |
| Flatten | levels the ground to the height where the stroke started |
| Paint | writes the material mask (grass / dirt track / gravel / sand / mud) the terrain shader reads |

`FluidSim.applyBed` runs a tiny compute pass that keeps the **water surface** (`bed + depth`) fixed
per cell and adjusts the depth to the new bed — raising ground squeezes water aside, digging lets
water flow in. Edits are also mirrored into the coarse bed under the playable rectangle, so the
two resolutions never disagree, and the high-resolution terrain mesh is rebuilt (throttled to
~8 Hz while dragging). Eight undo steps; *Reset terrain* restores the generated world.

### Rendering (`shaders/render.wgsl.js`)
Two water renderers, both reading the same solver textures:

* **Fine water** (`adaptive: true`) — one index buffer, one draw call. A regular grid in
  normalised space is warped with `x = c + L·(a·q + (1−a)·q³)` around the camera, with `a` chosen
  from the camera height and the mesh size so the centre spacing is about one simulation cell.
  Vertices outside the playable rectangle collapse onto its border. The surface texture is
  sampled bilinearly, so vertices closer together than a cell do not show facets.
* **Coarse water** — a plain grid over the far-field simulation, drawn first. It is the fallback
  under the fine mesh; the fine layer fades in over the last 6 m of its domain, so the two layers
  cross-fade at the nesting border instead of compositing twice or leaving a gap.

Per pixel: shoreline from the true water thickness, screen-space refraction along the refracted
ray, Beer–Lambert absorption + single scattering (water and suspended sediment), Fresnel sky
reflection, GGX sun glints, opaque wet mud with micro-relief, fog and shadows. Spray and mist are
soft, velocity-stretched droplet clusters with forward scattering and a soft-particle depth fade.

## Integrating into your engine

```js
import { FluidSim, FluidRenderer, FluidQueryBatch, BuoyancyProbe, RiverPath } from './engine/fluid/src/index.js';

// 1. simulation domain (e.g. 160 m around a track section at 0.31 m resolution)
const sim = new FluidSim(device, { nx: 512, ny: 512, cellSize: 0.3125, origin: [-80, -80], maxParticles: 262144 });
sim.setBed(terrainHeights);                    // Float32Array nx*ny, row-major (j*nx+i), metres
const river = new RiverPath(points, { width: 8, depth: 1.1, elevation: [4.4, -1.4] });
river.carve(terrainHeights, 512, 512, 0.3125, [-80, -80]);   // optional: carve before setBed
sim.setWater(initialDepth, initialMud);        // e.g. flood-fill the sea, fill puddles, river.fill()
sim.setBoundaries({ south: 'open', west: 'open', east: 'open', north: 'open' });   // driven by the coarse grid
river.addSource(sim, { flow: 2.6 /* m³/s */ });

// optional far field: a coarse grid for the rest of the visible world, coupled both ways
const coarse = new FluidSim(device, { nx: 384, ny: 384, cellSize: 3.65, origin: [-700, -700] });
coarse.setBed(coarseHeights);
coarse.setBoundaries({ south: 'ocean', west: 'open', east: 'open', north: 'wall' });
coarse.setWaves([{ amplitude: 0.3, period: 7.5, direction: Math.PI / 2 }]);   // seaward edge only
const link = new DomainLink(sim, coarse);

// 2. rendering (your HDR color target must be filterable, e.g. rgba16float; reverse-Z depth)
const water = new FluidRenderer(sim, { colorFormat: 'rgba16float', depthFormat: 'depth32float',
                                       adaptive: true, lodN: 384, caustics: true });
const waterFar = new FluidRenderer(coarse, { colorFormat: 'rgba16float', depthFormat: 'depth32float' });
water.setSceneTextures(sceneColorCopyView, sceneDepthView, shadowMapView /* optional */, [width, height]);

// 3. every frame
sim.setPrimitives([...car.primitives(), ...props]);   // displacement + drag
sim.emit({ position, velocity, rate: 3000, kind: 'spray', fromWater: true });   // wheel spray etc.
queries.submit();                                   // FluidQueryBatch of BuoyancyProbes
const enc = device.createCommandEncoder();
coarse.step(enc, dt); link.edge(enc);                // far field, then the fine border
sim.step(enc, dt);                                   // all sim + particle work
link.interior(enc);                                  // fine state back into the coarse grid
water.updateView({ viewProj, invViewProj, camPos, near, width, height, camRight, camUp, camFwd,
                   sunDir, sunColor, skyZenith, skyHorizon, fogDensity, shadowMat, shadowsOn: true });
water.renderCaustics(enc);                           // before your opaque pass
//   ...your opaque pass (terrain/material shaders call fluidGround(worldPos), see below)...
enc.copyTextureToTexture({ texture: sceneColor }, { texture: sceneColorCopy }, size);
const pass = enc.beginRenderPass({ colorAttachments: [{ view: sceneColorView, loadOp: 'load', storeOp: 'store' }],
                                   depthStencilAttachment: { view: sceneDepthView, depthReadOnly: true } });
waterFar.drawWater(pass); water.drawWater(pass); water.drawParticles(pass); pass.end();
device.queue.submit([enc.finish()]);
sim.afterSubmit();                                   // kicks off async query / stats readback
```

**Ground & object shaders** — add `FluidRenderer.groundWGSL(group, firstBinding)` to your WGSL,
`water.groundLayoutEntries(first)` to your bind group layout and `water.groundBindEntries(first)`
to the bind group, then:

```wgsl
let fg = fluidGround(worldPos);   // .light (caustics × absorption for sunlight), .wet, .mud, .depth, .surface
albedo *= mix(1.0, 0.5, fg.wet);  // wet darkening, mud stains via fg.mud
sun *= fg.light;                  // caustics under water
```

**Buoyancy** — wrap physics bodies in `BuoyancyProbe`s (sample grid over the hull with its
displaced volume), register them in a `FluidQueryBatch`, call `batch.submit()` before
`sim.step`, and each physics sub-step call `probe.apply(data, offset)` with
`batch.resultsFor(probe)`. Results are 1–3 frames old; probes query at positions predicted by
that latency. Query layout per point: `[head, depth, velX, velZ, mud, bed, surface, dη/dt]`.

**Sculpting** — call `sim.applyBed(heights)` with a new `Float32Array` whenever the terrain changes;
it keeps the water surface where it was. `FluidRenderer` has no terrain of its own, so also rebuild
whatever terrain mesh you draw from the same heights (`demo/SceneRenderer.js` shows a near-field
mesh built from the fine grid and a skirt that meets the far terrain).

**Vehicles** — see `demo/vehicle.js` for a complete example: hull probe, per-wheel water/mud
queries (aquaplaning, drag, mud resistance and grip), primitives for the solver and spray /
mud-roost emitters that only fire where there is real water.

## Tuning

| Parameter | Where | Notes |
|---|---|---|
| `manning` | `sim.params` | bed roughness: 0.012 asphalt … 0.035 natural channel |
| `mudYieldA/B`, `mudViscA/B`, `mudCvMax` | `sim.params` | Bingham rheology; `c = 1` ↔ `Cv = mudCvMax` (default ≈ 410 Pa yield) |
| `kObjMax`, `objCouple` | `sim.params` | lid stiffness of piercing bodies, no-slip drag rate |
| `spongeWidth`, `spongeRate` | `sim.params` | open-boundary absorption |
| `infiltration`, `filmDepth`, `dryTime` | `sim.params` | how splashes soak in and wet ground dries |
| `emitScale`, `ta/wc/ke Min/Max` | `sim.particles.params` | how readily breaking water makes spray |
| `sprayVolume` | `sim.particles.params` | water carried per spray particle (mass exchange) |
| `absorb`, `scatter`, `mudAbsorb`, `mudScatter` | `renderer.settings` | water / sediment optics (1/m) |
| `adaptive`, `lodN` | `new FluidRenderer(...)` | camera-adaptive mesh on/off and vertices per side (`320` demo default, `384` at high quality) |
| `meshStep`, `caustics`, `particles` | `new FluidRenderer(...)` | vertex stride as a multiple of a cell, caustics pass, particle pass |
| `radius`, `strength`, `tool`, `material` | `demo/TerrainEditor.js` | brush size (m), brush speed, active tool, paint material |

## Performance

Workload per quality preset of the demo (160 m domain):

| Preset | Fine cells | Cell size | Coarse cells | Water mesh | Particles | Sub-steps @60 Hz |
|---|---|---|---|---|---|---|
| low | 256² | 0.63 m | 384² | 320² + coarse | 131k | 2 |
| medium | 512² | 0.31 m | 384² | 320² + coarse | 262k | 2 |
| high | 768² | 0.21 m | 384² | 384² + coarse | 393k | 2–4 |
| ultra | 1024² | 0.16 m | 384² | 384² + coarse | 524k | 4 |

Each sub-step is 4 compute dispatches over the fine grid; the coarse grid costs one fixed extra
step per frame; particles cost one dispatch over the pool. The water surface is one draw call of
~100k triangles regardless of distance (the coarse mesh is a second, cheap one). Sculpting adds a
small bed-edit compute pass per stroke plus a terrain mesh rebuild at ~8 Hz. Everything stays on
the GPU — the only readbacks are small async ones (buoyancy queries, stats). It has **not yet been
profiled on real GPU hardware** (development was done headless on a software rasteriser), so
measure on your target devices; the grid resolutions, sub-step cap (`maxSubsteps`), particle pool,
`lodN` and caustics resolution are the main knobs.

## Tests

```bash
cd engine/fluid && npm install
npm test             # vehicle + editor-camera checks (CPU), WGSL validation, GPU numerical tests
npm run test:render  # headless render of the demo to test/out/*.png
npm run render:shots # named water views (bay, coast, ford, rapids, aerial) to test/out/*
npm run test:sculpt  # terrain sculpting: bed edits, surface preservation, undo, reset, paint
npm run test:browser # demo page in headless Chromium + WebGPU: real mouse/keyboard input
                     # (menu hide, game view, RMB fly, pan, orbit, dolly, focus, select), screenshots
```

Runs on machines without a GPU: the launcher falls back to SwiftShader (CPU Vulkan). Current
results: 30/30 solver checks — Ritter dam break, √(gh) wave speed, lake at rest with wet/dry
islands, run-up, Bingham equilibrium of a mud mound, floating-box buoyancy, moving-wheel
displacement, exact source volume, particle volume exchange, ocean boundary stability —
plus 18/18 sculpting checks (surface preserved to <1e-4 m, volume conserved, undo/reset exact,
no WebGPU validation errors) and 26/26 browser UI checks.

## Limitations / next steps

* **2.5D**: the water is a heightfield, so no overturning waves, tunnels or water under
  overhangs; breaking is represented by bores + spray (standard for real-time).
* **Long-wave physics**: the shallow-water equations are non-dispersive; swell and surf are
  right, but short deep-water chop below the grid scale is not simulated (and not faked).
* **Fixed domains**: each `FluidSim` is a fixed rectangle. The demo nests exactly two (coarse sea
  + fine playable area); a camera-following scrolling fine domain is the natural next step, and
  several could coexist for different track sections.
* Particles collide with the bed / water surface, not with car geometry.
* No underwater camera rendering (above-water views only).

## References

Stelling & Duinmeijer 2003 (momentum-conservative staggered scheme) · Audusse et al. 2004
(hydrostatic reconstruction) · O'Brien & Julien 1988 (mudflow rheology) · Ihmsen et al. 2012
(spray potentials) · Chentanez & Müller 2010 (heightfield water with rigid-body coupling) ·
Ritter 1892 (dam break) · Evan Wallace 2011 (WebGL caustics) · Horne & Dreher 1963 (aquaplaning).
