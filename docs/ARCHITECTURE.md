# Frontier Fluid — Architecture

How the simulation works, why it's built this way, and how to port/tune it.

## 1. The solver: shallow-water equations on a heightfield

State per grid cell (one `rgba16float` texture, ping-ponged):

| channel | meaning |
|---|---|
| x | water depth `d` (m) |
| y, z | horizontal velocity `u, v` (m/s) |
| w | splash / breaking energy (drives particle emission + foam) |

Static bed data in a second texture: bed height `b`, river flow target `(fx, fz)`, channel mask.
Free surface `η = b + d`. A separate texture holds advected **foam density** (x) and terrain **wetness** (y).

### Velocity update (`swe.wgsl: velocityUpdate`)

1. **Semi-Lagrangian self-advection** of momentum — backtrace along `−v·dt`, sample bilinearly.
   This is what makes wakes behind the truck stay crisp and curl around obstacles.
2. **Pressure gradient** `v −= g·dt·∇η` (central differences). A *dry-cell rule* keeps dry, higher
   neighbours from pulling water uphill: their effective `η` is clamped to ours. This kills most
   shoreline jitter.
3. **Viscosity** by Jacobi smoothing toward the 4-neighbour mean with factor
   `k = 1 − exp(−4ν·dt/dx²)`, clamped ≤ 0.92. Explicit diffusion would explode for mud
   (`ν·dt/dx² ≈ 1.6 ≫ 0.25`); the relaxation form is unconditionally stable and converges to
   "velocity = local average" for tar-like materials — exactly the mud look.
4. **Bed friction** `v *= exp(−dt·μ / max(d, 0.04))` (Manning-style: bites hardest in thin water),
   global damping, **wind stress** on deeper water, **river flow forcing** (relax `v` toward the
   baked channel velocity where the channel mask > 0).
5. Boundaries: solid walls (velocity clamped at rim cells), or ocean mode — momentum sponge +
   sinusoidal **swell injection** on the west strip.
6. Speed clamp `min(0.9·dx/dt, 28)`.

### Continuity (`continuityUpdate`)

Donor-cell (upwind) face fluxes. The face depth is limited by the **sill** — the higher of the two
beds: `d_face = max(0, η_upwind − max(b_L, b_R))`. Water can only cross a face by the amount its
surface clears the sill, which gives clean wet/dry fronts and stops numerical "leaking" uphill:
puddles stay puddles, rivers stay in their banks until they genuinely overtop.

Then: rain adds volume, evaporation removes it, ocean sponge relaxes depth toward rest level,
negatives clamp to 0, velocity zeroes in dry cells. **Breaking energy** accumulates where the
surface is steep and fast (`smoothstep(0.42…1.05, |∇η|)·|v|`), decays exponentially.

### Stability (CFL)

Explicit SWE needs `dt ≤ dx / √(g·d_max)`. The simulator runs a **fixed timestep** (`settings.timeStep`,
default 1/180 s at 256²/120 m) driven by an accumulator, so wave speed is identical at 30/60/144 Hz;
`substeps` caps catch-up work per frame. If you raise depth range or shrink `dx`, lower `timeStep`
(or raise `substeps`) accordingly. The safety nets (speed clamp, positivity clamp, Jacobi viscosity)
degrade gracefully instead of exploding if you push it.

### Interactors & sources (`interact`, once per frame)

For every grid cell, loop the (≤64) interactors. A submerged interactor:

1. **Displaces volume** — surface under the body lerps down toward the body's underside; the solver
   piles the displaced water around it (bow wave) on subsequent substeps.
2. **Drags water** — velocity lerps toward the body's horizontal velocity + radial outwash
   ∝ plunge speed & travel speed.
3. **Deposits churn energy** ∝ speed × submersion — the particle systems spend it.

Sources add/remove depth with a smooth disc profile and inject velocity (river head).
Rain drops are stochastic per-cell depth bumps (hash of cell+frame) that ring out through the
solver, and **landed splash droplets** feed small divergent impulses back in — the loop is closed:
solver → spray → ripples → solver.

## 2. Particles (`particles.wgsl`)

Two GPU pools in storage buffers, allocated by atomic ring-buffer heads (oldest recycle first,
zero CPU involvement, zero allocation after init):

- **Splash droplets** (48k): ballistic (`v.y −= g·dt`, light air drag). Emission = *rejection
  sampling* against the solver's energy field: N candidate threads probe random cells and accept
  with probability ∝ energy, so spray density tracks the physics. On hitting the surface a droplet
  writes a **landing event** (position, energy) into a lock-free buffer.
- **Foam flecks** (64k): live on the surface (`y = η + ε`), advected by the simulated velocity with
  small jitter, age 2.5–7 s (preset), grow slightly as they "merge". Emission anchors on landing
  events (45%) and on the advected foam-density field (55%).

Landing events are consumed next frame by `interact` (ripple impulses) and `foamUpdate` (foam
density), with a feedback cap of 256/frame for solver impulses (droplet ripples are sub-cell
phenomena anyway); foam emission sees all of them.

### Advected foam field (`foamField.wgsl`)

A scalar density advected semi-Lagrangian-style by the simulated surface velocity, so foam
*accumulates in the right places* — eddy lines behind wheels, convergence zones, shorelines —
instead of being a function of instantaneous depth. Sources: flow compression `max(0,−∇·v)·|v|`,
crest steepness, shore wash (thin + moving), churn/breaking energy, droplet landings.
Exponential dissipation; wetness channel saturates under water and dries over ~30 s.

## 3. Caustics (`caustics.wgsl`)

For each surface texel: refract the sun vector through the **simulated** normal (Snell, material
IOR), march the ray to the bed with 3 fixed-point iterations against the bed heightfield, and do
the same for the +x/+z neighbours. The ratio `cellArea / |cross(hitR−hit, hitT−hit)|` is the
ray-bundle focusing factor (Jacobian of the surface→bed map) — >1 focused (bright filament),
<1 spread (dim). Weighted by Fresnel transmission and `exp(−depth·0.22)` scattering fade, then
scattered with fixed-point `atomicAdd` into an accumulation buffer at the *bed texel actually hit*,
and resolved with a 3×3 tent blur + soft knee. The terrain shader multiplies its sunlight by it
underwater. Cost: 3 ray marches × sim-res² — trivially cheap, and the caustics are literally
images of your waves.

## 4. Rendering

- **Surface**: a static UV grid displaced in the vertex shader by `η` (dry cells tuck 4.5 cm under
  the bed, producing the shoreline). Normals from the heightfield gradient; optional
  flow-advected micro-detail normals (normal perturbation only). Refraction samples a copy of the
  opaque scene with normal distortion; absorption is Beer–Lambert over the *true* view-path length
  measured from a linear-depth render target (`sceneZ − waterZ`), so knee-deep shallows stay clear
  and deep water goes dark for the right reason. Fresnel blends to a half-res **planar reflection**
  (rendered with a mirrored camera; below-waterline geometry is clipped in the material shaders
  instead of oblique-frustum math). Sun glint (Blinn, roughness-shaped), through-crest scattering,
  and foam: the advected density masks a procedural bubble pattern; foam particles render on top.
- Mud is the same material with huge absorption + high roughness — refraction simply dies within
  centimetres and the surface reads as opaque wet earth.
- **Particles**: storage buffers bound directly as instanced vertex buffers. Droplets are
  billboards stretched along screen-space velocity with a sun glint; foam flecks are flat,
  self-rotating discs with procedural bubble breakup, faded by simulated age.
- **Terrain**: mesh from the same heights the sim collides with. Procedural height/slope albedo,
  wetness darkening + specular sheen from the sim, caustics underwater.

Frame order: `sim.step` (compute) → reflection (sky/terrain/props) → opaque → copy for refraction →
water + particles → ACES tonemap blit. Each solver stage is its own compute pass — passes are the
one read-after-write ordering guarantee every WebGPU implementation honours, which keeps the
texture ping-pong safe on strict validators.

## 5. CPU gameplay bridge

A 128² downsample of (η, u, v, d) is copied to a double-buffered staging ring each frame and
mapped asynchronously — `sampleWater(x,z)` is always ≤1 frame stale and never stalls the GPU.
`BuoyantBody` samples it at probe points: per-probe Archimedes force `ρ·g·V_probe·submergedFraction`
applied at the probe (so torque — rocking, righting, wave-slope alignment — emerges), plus drag
toward the local simulated current. Feed `body.toInteractor()` back into the solver and boats make
wakes, crates splash. The demo truck uses the same queries for ground/float blending, drag,
current push and "bogging" in mud (drag scaled by viscosity).

## 6. Rivers (`RiverPath.ts`)

Catmull-Rom through control points with per-point bed elevation. Baking stamps nearest-sample
info into grids, then: carves a U-channel (smooth profile, feathered banks) with **monotonic
descent enforced** (a river that can't dam itself), and writes the flow field with a parabolic
cross-channel profile (fast mid-stream). The solver's flow forcing relaxes velocity toward it
inside the channel mask — strong enough to guarantee downstream flow, weak enough that waves,
splashes and a parked truck still behave physically. `getSource()/getSink()` close the water
budget; balance their `flow × disc area` or your pond slowly wins.

## 7. Tuning guide

| Knob | Effect | Notes |
|---|---|---|
| `resolution`, `worldSize` | detail vs cost | waves need ≥ ~4 cells/wavelength; 256² @ 120 m ⇒ 0.47 m cells |
| `timeStep`, `substeps` | stability/speed | keep `dt ≤ dx/√(g·d_max)`; `substeps·timeStep ≈ 1/60` for real-time at 60 fps |
| `viscosity` | water ~1, syrup ~10, mud 40–120 | Jacobi form: cannot explode, just gets thicker |
| `bedFriction` | how fast shallows calm | rivers want low (0.1–0.2), mud 1+ |
| `absorption` | colour/depth falloff (per metre) | mud: ≥ 15 ⇒ opaque |
| `density` | buoyancy only | mud 1400–1800: crates ride high |
| `splashRate`/`foamRate` | particle emission budgets | pools recycle oldest-first, so over-driving is safe |
| `edgeWaveAmp/Freq` | ocean swell height/period (`boundaryMode=1`) | amp is injection velocity (m/s) ≈ resulting wave height in deep water |

## 8. Porting checklist (another engine / three.js / Babylon / raw)

1. Copy `src/fluid/` (TS + WGSL). Replace the `?raw` shader imports with your bundler's text loader,
   keeping the `//@@COMMON@@` / `//@@FRAME@@` chunk substitution (see `FluidSim.makeModule`,
   `Renderer.makeRenderModule`).
2. Create `FluidSim` with your `GPUDevice`, upload bed data built from your terrain
   (`setBed`), call `reset()` once into a command encoder.
3. Per frame: build `FluidInteractor[]` from your physics bodies (wheels = spheres, hulls/boats =
   columns), `FluidSource[]` for rivers/faucets, call `sim.step(...)` **before** your render
   passes, then `sim.pollReadback()` after submit.
4. Render: either reuse `WaterRenderer`/`ParticleRenderer` (they need a `Frame` uniform block +
   scene-color/linear-depth/refraction/reflection targets shaped like `engine/Renderer.ts`), or
   write your own material sampling `sim.fieldView/foamView/causticsView` — the texture layouts
   are documented at the top of `shaders/common.wgsl`.
5. Gameplay: `sampleWater()` + `BuoyantBody` (or your own probes against `cpuField`).

The WGSL is plain and self-contained; for a native engine (Dawn/wgpu/Unreal/Unity compute), the
same shaders port mechanically: textures→RWTextures, storage buffers→RWBuffers, same math.

## 9. Known limitations / roadmap

- Heightfield: no overturning wave curls, no true 3D splashes *inside* the volume, no waterfalls
  (fake the sheet, simulate the pool + spray).
- `rgba16float` state: depth precision ~8 mm at 10 m depth — fine for games; a `r32float`
  variant with manual bilinear fetch is the upgrade path if you need glassy-smooth deep ocean.
- Ocean *swell spectrum*: for open-sea shots far from shore, layer an FFT/Gerstner detail ocean
  outside the sim domain and let this solver own the interactive near field (standard production
  split). The sim's swell injection already matches that workflow at the boundary.
- Single domain, square. A follow-the-player recentring domain (with state re-projection) or
  clipmap of tiles is the natural next step for open worlds.
