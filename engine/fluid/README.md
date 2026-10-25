# Frontier Fluid — real-time WebGPU water for racing games

Engine-agnostic WebGPU module that **simulates** water and mud on the GPU and renders it:
ocean swell breaking on a beach, rivers flowing down a carved path, puddles and mud pits that cars
drive through, real splashes, particle spray and particle foam, caustics and buoyancy.

Nothing is faked: every wave you see comes out of the solver, foam and spray are GPU particles
carrying (and returning) real water volume, and caustics are traced through the simulated surface.

```
engine/fluid/
  src/                 the module (vanilla ES modules, no dependencies, no build step)
    FluidSim.js        shallow-water + mud solver, object coupling, queries, stats
    FluidParticles.js  spray / mist / foam / rain particles coupled to the grid
    FoamPattern.js     procedural foam micro-structure carried by the foam particles
    FluidRenderer.js   water surface, caustics, particle rendering, ground-shader hooks
    Buoyancy.js        RigidBody, BuoyancyProbe, FluidQueryBatch (CPU side)
    RiverPath.js       carve a river (or a track) along a spline, fill it, add the inflow
    shaders/           all WGSL (sim, particles, render)
  demo/                drivable test world (stand-in for your engine)
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
| `T` `V` `P` | rain · debug view (depth / speed / mud / normals) · pause |
| `H` · `G` · `?` | hide / show the menu · game view (hide all UI, as in UE) · controls sheet |
| Gamepad | RT throttle, LT brake, A handbrake, left stick steer |

The panel also has live sliders for swell height and period, river flow and wind. It also has a Camera
section with the mode, fly speed, FOV and optional mouse lock. `?ui=hidden` starts with the menu hidden and
`?ui=game` starts in game view. The menu state, camera speed and FOV are remembered between visits.

## What is simulated

| Requirement | How |
|---|---|
| Ocean, shallow water, puddles, mud | One GPU shallow-water (Saint-Venant) solver over a heightfield bed, wet/dry fronts, per-cell sediment concentration with Bingham-plastic rheology |
| No fake waves | Swell enters through an *ocean* boundary (relaxation-zone wave maker with physical amplitude/period/direction) and then propagates, shoals, refracts, steepens and breaks as bores purely in the solver. Wakes, bow waves, splashes, wind gust forcing — all solver dynamics |
| River along a path | `RiverPath` carves a channel along a spline into the bed and adds a volume source at the head; the water flows downhill under gravity, speeds up over the rapids, pools, crosses fords and enters the sea |
| Interactive vehicles | Car body + wheels are rasterised into the grid every frame as solid volumes: they push water away (bow wave, wake, trough behind each tyre), drag it along, leave persistent ruts in mud (yield stress) and get pushed back (buoyancy, drag, current) |
| Splash, spray & foam | GPU particles emitted where the simulated surface breaks (Ihmsen et al. potentials: wave crest, trapped air, kinetic energy) and by wheel / bow emitters. Spray removes water from the grid and returns it where it lands; spray hitting water leaves thin droplet foam. Foam particles are rafts of bubbles that ride the simulated current, turn with its vorticity, thin out and burst (faster in turbulence and on land). They are rendered as one continuous foam surface that carries a bubble-raft micro-structure along with the particles, not as sprites (see *Particle foam* below) |
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
Kinds: spray, mist, foam, rain. Ballistic droplets with size-dependent drag relative to the
wind; volume exchange through fixed-point atomics so splashes genuinely move water (test: total
volume returns to 2e-6 after all spray has landed).

### Particle foam (`FoamPattern.js`, `FOAM_SPLAT` / `FOAM_COMPOSITE` in `shaders/render.wgsl.js`)
The foam is the simulated foam particles. Each one is a *raft* of bubbles, 0.3 m in radius, that
spreads as it ages. Breaking waves and trapped air make full-thickness whitewater, while landing
droplets leave a thin film. Every raft is advected by the surface current and turned by the local
vorticity. Its thickness decays as its bubbles burst.

The rafts are drawn as one continuous surface, not as sprites:

1. **Splat.** Each raft is drawn as a small patch lying on the water surface. The patch adds its
   kernel weight w, w² and its pattern values into two screen-space rgba16f buffers. The opaque
   depth is used for the depth test.
2. **Lagrangian texture advection** (Yu, Neyret, Bruneton & Holzschuch 2011). Every raft carries
   its own randomly placed and rotated window of a procedural foam micro-structure, so the
   structure moves exactly with the particles. Where rafts overlap, their patterns are combined
   with the variance-preserving blend `P = ½ + Σw(Pᵢ − ½) / √Σw²`. This keeps the structure crisp
   however many rafts overlap.
3. **Coverage = P(pattern < density).** Foam is drawn where the blended pattern is lower than the
   local foam density. The pattern channels are histogram-equalised, so mean coverage equals
   density. Fresh whitewater is solid with a frothy rim. As foam thins, it opens up into lace
   along the bubble-raft walls, then breaks into fragments and specks.
4. **Shading.**
   - Reflectance runs from about 0.5 for thin foam to about 0.85 for thick fresh foam.
   - Thin foam is translucent and tinted by the water underneath.
   - Lighting uses wrapped diffuse light with relief from screen-space derivatives, the sun shadow
     map and sky light.
   - Muddy water makes tan foam.
   - Fresh foam has wet glints.
   - Water under fresh foam turns milky aquamarine from the submerged bubbles.
5. **Level of detail.** When the fine lace becomes smaller than a pixel, the threshold switches to
   the coarser marbling. When that is unresolved too, the expected coverage is drawn instead. The
   foam doesn't shimmer, and its average brightness is kept.

The micro-structure is a 512² texture generated on the CPU at start-up (about 0.4 s), with no image
assets. It holds marbling, two-scale domain-warped Voronoi raft walls of varying thickness,
fine bubbles and clumps. Settings are `foam*` and `bubbleScatter` in `renderer.settings`.

### Rendering (`shaders/render.wgsl.js`)
Surface mesh displaced by the simulated height (shoreline handled per pixel from the true water
thickness), screen-space refraction along the refracted ray, Beer–Lambert absorption + single
scattering (water and suspended sediment), Fresnel sky reflection, GGX sun glints, opaque wet mud
with micro-relief, fog, shadows. An outer ocean ring outside the domain shows the same incident
swell the wave maker injects (linear theory). Spray and mist are soft, velocity-stretched droplet
clusters with forward scattering and a soft-particle depth fade. Foam is the continuous particle
surface described above.

## Integrating into your engine

```js
import { FluidSim, FluidRenderer, FluidQueryBatch, BuoyancyProbe, RiverPath } from './engine/fluid/src/index.js';

// 1. simulation domain (e.g. 160 m around a track section at 0.31 m resolution)
const sim = new FluidSim(device, { nx: 512, ny: 512, cellSize: 0.3125, origin: [-80, -80], maxParticles: 262144 });
sim.setBed(terrainHeights);                    // Float32Array nx*ny, row-major (j*nx+i), metres
const river = new RiverPath(points, { width: 8, depth: 1.1, elevation: [4.4, -1.4] });
river.carve(terrainHeights, 512, 512, 0.3125, [-80, -80]);   // optional: carve before setBed
sim.setWater(initialDepth, initialMud);        // e.g. flood-fill the sea, fill puddles, river.fill()
sim.setBoundaries({ south: 'ocean', west: 'open', east: 'open', north: 'wall' });
sim.setWaves([{ amplitude: 0.3, period: 7.5, direction: Math.PI / 2 }]);
river.addSource(sim, { flow: 2.6 /* m³/s */ });

// 2. rendering (your HDR color target must be filterable, e.g. rgba16float; reverse-Z depth)
const water = new FluidRenderer(sim, { colorFormat: 'rgba16float', depthFormat: 'depth32float' });
water.setSceneTextures(sceneColorCopyView, sceneDepthView, shadowMapView /* optional */, [width, height]);

// 3. every frame
sim.setPrimitives([...car.primitives(), ...props]);   // displacement + drag
sim.emit({ position, velocity, rate: 3000, kind: 'spray', fromWater: true });   // wheel spray etc.
queries.submit();                                   // FluidQueryBatch of BuoyancyProbes
const enc = device.createCommandEncoder();
sim.step(enc, dt);                                   // all sim + particle work
water.updateView({ viewProj, invViewProj, camPos, near, width, height, camRight, camUp, camFwd,
                   sunDir, sunColor, skyZenith, skyHorizon, fogDensity, shadowMat, shadowsOn: true });
water.renderCaustics(enc);                           // before your opaque pass
//   ...your opaque pass (terrain/material shaders call fluidGround(worldPos), see below)...
enc.copyTextureToTexture({ texture: sceneColor }, { texture: sceneColorCopy }, size);
water.renderFoam(enc);                               // splat the foam particles (tests the opaque depth)
const pass = enc.beginRenderPass({ colorAttachments: [{ view: sceneColorView, loadOp: 'load', storeOp: 'store' }],
                                   depthStencilAttachment: { view: sceneDepthView, depthReadOnly: true } });
water.drawWater(pass); water.drawFoam(pass); water.drawParticles(pass); pass.end();
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
| `emitScale`, `ta/wc/ke Min/Max` | `sim.particles.params` | how readily breaking water makes spray & foam |
| `sprayVolume` | `sim.particles.params` | water carried per spray particle (mass exchange) |
| `absorb`, `scatter`, `mudAbsorb`, `mudScatter` | `renderer.settings` | water / sediment optics (1/m) |
| `foamLife`, `foamSize`, `foamEmitProb`, `impactFoamProb`, `impactFoamAmount` | `sim.particles.params` | how much foam is made and how long rafts last |
| `foamDensity`, `foamLace`, `foamThinning`, `foamFresh`, `foamAlbedo` | `renderer.settings` | how dense, lacy, long-lived and bright the foam looks |

## Performance

Workload per quality preset of the demo (160 m domain):

| Preset | Cells | Cell size | Particles | Sub-steps @60 Hz (≈9 m sea) |
|---|---|---|---|---|
| low | 256² | 0.63 m | 131k | 2 |
| medium | 512² | 0.31 m | 262k | 2 |
| high | 768² | 0.21 m | 393k | 2–4 |
| ultra | 1024² | 0.16 m | 524k | 4 |

Each sub-step is 4 compute dispatches over the grid; particles cost one dispatch over the pool;
the water mesh, caustic mesh and terrain are one vertex per cell. Everything stays on the GPU —
the only readbacks are small async ones (buoyancy queries, stats). It has **not yet been profiled
on real GPU hardware** (development was done headless on a software rasteriser), so measure on
your target devices; the grid resolution, sub-step cap (`maxSubsteps`), particle pool and
caustics resolution are the main knobs.

## Tests

```bash
cd engine/fluid && npm install
npm test            # vehicle + editor-camera checks (CPU), WGSL validation, GPU numerical tests
npm run test:render # headless render of the demo to test/out/*.png
npm run test:browser # demo page in headless Chromium + WebGPU: real mouse/keyboard input
                     # (menu hide, game view, RMB fly, pan, orbit, dolly, focus, select), screenshots
npm run render:foam  # foam close-ups (ford, beach, rapids) + foam-buffer statistics; with
                     # TAG=before/after and node test/compare.mjs for a side-by-side sheet
```

Runs on machines without a GPU: the launcher falls back to SwiftShader (CPU Vulkan). Current
results: 30/30 solver checks — Ritter dam break, √(gh) wave speed, lake at rest with wet/dry
islands, run-up, Bingham equilibrium of a mud mound, floating-box buoyancy, moving-wheel
displacement, exact source volume, particle volume exchange, ocean boundary stability.

## Limitations / next steps

* **2.5D**: the water is a heightfield, so no overturning waves, tunnels or water under
  overhangs; breaking is represented by bores + spray/foam particles (standard for real-time).
* **Long-wave physics**: the shallow-water equations are non-dispersive; swell and surf are
  right, but short deep-water chop below the grid scale is not simulated (and not faked).
* **One fixed domain per `FluidSim`**: place domains where the track meets water (several can
  coexist); a camera-following scrolling domain is a natural extension.
* Particles collide with the bed / water surface, not with car geometry.
* No underwater camera rendering (above-water views only).

## References

Stelling & Duinmeijer 2003 (momentum-conservative staggered scheme) · Audusse et al. 2004
(hydrostatic reconstruction) · O'Brien & Julien 1988 (mudflow rheology) · Ihmsen et al. 2012
(spray/foam potentials) · Yu, Neyret, Bruneton & Holzschuch 2011 (Lagrangian texture advection,
variance-preserving blending) · Chentanez & Müller 2010 (heightfield water with rigid-body coupling) ·
Ritter 1892 (dam break) · Evan Wallace 2011 (WebGL caustics) · Horne & Dreher 1963 (aquaplaning).
