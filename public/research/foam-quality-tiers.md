# Frontier — realistic foam on GTX, scalable to RTX

**Research and engineering recommendation · 8 September 2026**  
**Application inspected:** Frontier 0.8.0, commit `a615ffc`

> **Implementation update:** Frontier 0.9 now includes the three tiers. See [implementation status and differences](foam-implementation.md). The original research and unverified performance targets below are retained as design context.
**Scope:** three candidates for **Standard, Ultra and Cinematic**. The existing effect is the **Low baseline**. **This report does not implement the new tiers.**

## Executive decision

**Build a persistent foam system, not a more complicated white-noise shader.**

| Tier | Recommended candidate | Visible improvement over Low | Decision |
| --- | --- | --- | --- |
| **Low — existing** | Stateless shoreline mask and advected procedural streaks | Current appearance; no persistent foam population | Preserve as the fallback |
| **Standard — candidate 1** | **Eulerian foam-density map: emit, advect, accumulate and decay** | Foam survives its birth event, travels downstream, leaves fading trails and can accumulate at convergence zones | **Implement first. Best benefit-to-cost starting point** |
| **Ultra — candidate 2** | **GPU surface particles, reconstructed into a foam density/age map** | Sharper patches and filaments, breakup around rocks, coherent motion without repeatedly blurring every feature | **Recommended high-quality default after Standard** |
| **Cinematic — candidate 3** | **Localized hybrid whitewater: surface foam + spray + submerged bubbles**, driven by a small water-flow patch | Vertical splashes, rising entrained air, foam formation on resurfacing and more convincing close-up thickness | **Prototype after depth/compositing support; keep it local and capped** |

These are three algorithmic upgrades, not simply three particle-count presets. They can share sources, collision data, appearance controls and a low-cost distant representation.

**Important cost qualification:** I cannot honestly guarantee that actual foam simulation will be cheaper than the existing stateless effect. The goal is **substantially better realism at a small, bounded incremental cost**. Some current per-pixel work can potentially be replaced by cached data, but whether that pays for the simulation must be measured. No GTX or RTX benchmark of these proposed Frontier tiers has been run.

### Proposed GTX performance gates — not measured results

Reference workload: **GTX 1060 6 GB**, 1920 × 1080, native render resolution, the current 96 × 96 m terrain, and a demanding water-heavy camera. Also test GTX 1650 4 GB separately; do not assume the two cards perform identically.

| Tier | Initial prototype limit | Update cadence | Proposed p95 incremental GPU ceiling vs Low | Planned extra working-set ceiling |
| --- | --- | --- | --- | --- |
| Standard | 256² foam map; 128² driving-flow cache | 30 Hz foam, 60 Hz display | **0.75 ms/frame** | **8 MiB** |
| Ultra | 512² reconstruction; **32,768** live surface particles; 256² driving-flow field | 30 Hz simulation, interpolated display | **2.0 ms/frame** | **24 MiB** |
| Cinematic | 512² surface reconstruction; **98,304 total** secondary particles; one 128² local flow patch; half-resolution spray/bubble layer | 30 Hz base simulation; bounded substeps | **4.0 ms/frame** | **64 MiB** |

The ceilings include the tier's sources, flow work, particle/map updates, reconstruction, added depth/compositing work and rendering—not just its fastest kernel. Measure update frames as well as amortized averages. These are **accept/reject budgets**, not estimates of achieved speed or promises of 60 fps. Cinematic should initially target a **30 fps GTX viewing mode**; a 60 fps target requires enough headroom in the existing terrain/water renderer. RTX can increase quality within the same measured budget.

---

## 1. What Frontier actually has today

This audit is against the pushed source, not an assumed Unity or Unreal water system.

- [`currentStreak()` and `waterWaves()`](https://github.com/streamlinkinbox/Frontier/blob/a615ffc5b5c6ffb4187851bb40a679eaf5f937fe/src/engine/shaders/render.wgsl#L1-L60) use a seeded canyon route and procedural wave/noise coordinates. The route is not an obstacle-solving river-velocity simulation.
- [`shadeWater()`](https://github.com/streamlinkinbox/Frontier/blob/a615ffc5b5c6ffb4187851bb40a679eaf5f937fe/src/engine/shaders/render.wgsl#L215-L268) estimates bank proximity from the terrain SDF, combines a crest-like phase with noise, and blends a lit foam/streak color. There is **no persistent foam density, age, particle population or air-entrainment state**. “Low” is the report's name for this existing effect; there is not yet a Low/Standard/Ultra/Cinematic selector.
- The existing water already includes traced reflection/refraction, illumination, turbidity and boundary guards. Preserve those; foam should not reintroduce glowing bank outlines or rays passing through solid rock.
- The SatMap [D8 map](https://github.com/streamlinkinbox/Frontier/blob/a615ffc5b5c6ffb4187851bb40a679eaf5f937fe/src/engine/satmaps/terrainMaps.ts#L17-L106) is **top-surface drainage/contributing area**. It is not water velocity in meters per second, water depth, or a vector field for foam transport. The erosion volume/fluxes are also not a calibrated surface-water momentum solver.
- Both backends currently render the scene as a full-screen raymarch into an owned **color target**. There is no ready-made terrain/water depth buffer to make an additional spray pass correctly disappear behind cliffs. See [WebGLBackend](https://github.com/streamlinkinbox/Frontier/blob/a615ffc5b5c6ffb4187851bb40a679eaf5f937fe/src/engine/WebGLBackend.ts) and [WebGPUBackend](https://github.com/streamlinkinbox/Frontier/blob/a615ffc5b5c6ffb4187851bb40a679eaf5f937fe/src/engine/WebGPUBackend.ts).

**Consequence:** persistent history is the first missing ingredient; an obstacle-aware velocity field is the second. Adding detailed foam texture to the current centerline flow alone will not produce convincing recirculation behind a boulder.

## 2. Evidence: what is demonstrated, and what is not

| Research/reference | Relevant evidence | What it does **not** establish |
| --- | --- | --- |
| **Crest 4 foam implementation and documentation** | Previous foam is backtraced through a flow texture, faded, and supplemented by wave/shore sources. Documentation specifies a default 30 updates/second and independently controlled generation/fade. The inspected public implementation is MIT-licensed. [2](https://crest.readthedocs.io/en/latest/user/water-appearance.html) | No isolated GTX timing for Frontier, and no automatic river solver supplied merely by porting the foam update |
| **Chentanez & Müller, 2010 — Real-time Simulation of Large Bodies of Water with Small Scale Details** | A shallow-water velocity/height field coupled to simple spray, splash and foam particles; native CUDA results measured on **GTX 480**. Foam is advected and projected onto the water surface. [3](https://matthias-research.github.io/pages/publications/hfFluid.pdf) | Not WebGL/WebGPU code; not a directly transferable browser benchmark; the paper also identifies conservation and stability limitations |
| **Saalfrank/OMYOG, 2026 coastal renderer** | The developer describes replacing an advected foam texture with GPU particles splatted into a density map to improve patch structure and avoid a soft/blobby result; the article describes up to 300K foam particles. [2](https://80.lv/articles/real-time-shoreline-simulation-in-custom-c-coastal-renderer) | The reported approximately 250 fps is a native **RTX 4090 Laptop** scene measurement, not an isolated foam benchmark or proof of GTX performance; do not assume it measures the later foam revision under identical conditions |
| **Ihmsen et al., 2012 — Unified Spray, Foam and Bubbles** | One-way secondary particles with physically motivated emission, type-specific motion and dissolution. Diffuse-to-diffuse interaction forces are omitted. However, the original method still uses relationships to the underlying fluid particles. [7](https://link.springer.com/article/10.1007/s00371-012-0697-9) | Its original large examples are **not demonstrated at interactive runtime**. The stated 50 fps is the animation timestep; the paper reports minutes of computation for sequences |
| **Thürey et al., 2007 — Bubbles and Foam within a Shallow Water Framework** | Demonstrates coupled bubbles, vortices and SPH surface-tension-driven foam clustering at interactive rates on small examples. Its larger example uses 654 bubbles and 1,129 foam particles at a minimum 18.9 fps on a Core 2 Duo/GeForce 7900 system. [1](https://cgl.ethz.ch/Downloads/Publications/Papers/2007/Thue07a/Thue07a.pdf) | This is not evidence that a full SPH foam-neighbor solver with hundreds of thousands of particles is cheap |
| **Dupuy & Bruneton, 2012 — Ocean Whitecaps** | Mip-filterable whitecap coverage, demonstrated below 10 ms for the **whole ocean frame** at 1280 × 720 on GeForce 560 Ti. The authors explicitly identify lack of foam decay as future work. [1](https://inria.hal.science/hal-00967078v1/document) | A whitecap-generation model is not a persistent river-foam simulation |
| **Wretborn, Flynn & Stomakhin, 2022 — Guided Bubbles and Wet Foam** | Two-way bubble/Euler-flow coupling, local re-simulation, and a surface-manifold-constrained SPH wet-foam model provide a useful high-fidelity reference. [6](https://dl.acm.org/doi/10.1145/3528223.3530059) | No GTX-browser real-time performance is established by the abstract/official material consulted; do not sell this entire solver as a cheap Cinematic switch |

**Concrete native-GPU precedent:** Chentanez & Müller's WaterFG example reports **7.97 ms total frame time**, including rendering; its HF, generation and particle-update columns are **0.75, 0.82 and 0.44 ms** respectively on GTX 480. Those three columns do not include every rendering cost. The performance table does not specify a viewport resolution. This supports the *choice of a hybrid architecture*, not Frontier's proposed timing ceilings. [3](https://matthias-research.github.io/pages/publications/hfFluid.pdf)

**Offline-performance trap:** Ihmsen's Wave example lists 28 minutes of diffuse computation for 1,000 frames—approximately **1.68 seconds per frame**, calculated from its table, before adding the primary simulation and final rendering. Its efficient secondary-effect ideas are useful; its original workload is not our real-time budget. [7](https://link.springer.com/article/10.1007/s00371-012-0697-9)

---

## 3. Candidate 1 — Standard: persistent advected foam map

### Mechanism

Keep a small, world-aligned 2D texture containing foam density and density-weighted age. At a fixed timestep:

1. Backtrace each wet cell through the driving velocity field; sample previous foam.
2. Add foam from energetic breaking/impact/entrainment sources.
3. Apply lifetime decay and bounded convergence/dispersion behavior.
4. Remove or redistribute foam where the water domain becomes dry or solid.
5. Shade the resulting coverage on the existing water surface, with a filtered detail atlas.

Crest is a particularly useful implementation reference: its update explicitly reads previous state at `worldPosition - deltaTime * velocity`, fades it, and adds source terms. [2](https://crest.readthedocs.io/en/latest/user/water-appearance.html) The exact inspected code is [UpdateFoam.compute at `db0658f`](https://github.com/wave-harmonic/crest/blob/db0658ff0b2e93e4a9e28cc2867509658b0ecc00/crest/Assets/Crest/Crest/Shaders/Resources/UpdateFoam.compute).

A conceptual model for projected areal foam density `q` is:

```text
∂q/∂t + ∇·(q u) = source − q / lifetime
coverage = 1 − exp(−opticalScale × q)
```

For a semi-Lagrangian implementation, convergence needs an explicit treatment if `q` is to accumulate as a density; simply backtracing a scalar does not supply that term. Use exponential, timestep-aware decay. This first implementation is a visual transport approximation—not a claim of conservative multiphase fluid mechanics.

### Why it improves Low

A foam trail remains after the generating crest has moved on. Its movement is driven by a velocity field rather than a fresh per-pixel phase. Density and age provide separate control over fresh foam, old streaks and breakup. This is actual evolving state, even though the water driving it can initially remain a simplified field.

### Why it is the cheapest useful upgrade

The simulation is proportional to a **256² grid**, not to every viewport pixel or millions of fluid particles. Source calculation, advection and fade should be fused where feasible. Start with a stable backtrace; if numerical blur is unacceptable, test a **limited MacCormack** variant, accounting for its extra intermediate work. GPU Gems documents both semi-Lagrangian smoothing and the need to limit the correction. [2](https://developer.nvidia.com/gpugems/gpugems3/part-v-physics-simulation/chapter-30-real-time-simulation-and-rendering-3d-fluids)

**Where a saving versus Low might come from:** replace the old foam-only breakup/streak evaluations with the transported-map lookup; do not simply run both at full strength. In a hypothetical 1080p/60 fps shot with 25% water coverage, there are about **31.10 million water-pixel evaluations/second**, versus **1.97 million cell updates/second** for one 256²/30 Hz pass. This is only a sampling-frequency comparison: the map update is not equally expensive, final shading still runs per pixel, and Frontier's wave intersection/reflection/refraction may dominate. Preserve the shared shoreline-ripple and ray-boundary logic. Measure the actual net frame-time change rather than converting these counts into a speedup claim.

Use a real/artist-authored foam coverage/height atlas to supply sub-cell appearance, not a higher-resolution fluid simulation just to draw tiny bubbles. At 256² across 96 m, cells are **37.5 cm** wide: the simulation does not resolve individual millimeter bubbles.

### Important limits

- It remains a surface representation: no spray flying above water and no independently rising bubbles below it.
- Repeated resampling tends to blur fine structures. Higher resolution alone is not always the best fix.
- A centerline-driven field will not invent correct eddies. Begin with cached no-penetration/obstacle deflection, then improve the driving flow if needed; label the initial field as kinematic.
- Detail UV blending can pulse or stretch. The River Editor practitioner write-up discusses dual-rest coordinates and that trade-off; Yu et al. offer a more sophisticated Lagrangian texture-advection reference. Neither should be mistaken for foam generation physics. [9](https://80.lv/articles/river-editor-water-simulation-in-real-time) [5](https://inria.hal.science/inria-00536064/file/IEEE_TVCG.pdf)

**Recommendation:** implement this first and compare moving footage—not just attractive still frames—against Low.

## 4. Candidate 2 — Ultra: surface particles → density map

### Mechanism

Simulate a capped population of foam markers on the water surface. Each stores position, age/lifetime, represented amount, radius and an appearance seed. Advect with the flow; constrain to the current water surface; enforce solid boundaries. Reconstruct coverage, weighted age and optional thickness into a texture using small GPU splats.

This follows the surface-foam transport idea demonstrated by Chentanez & Müller. A recent independent DirectX 11 implementation specifically reports improved structure after moving from advected foam textures to particle-derived density maps. [3](https://matthias-research.github.io/pages/publications/hfFluid.pdf) [2](https://80.lv/articles/real-time-shoreline-simulation-in-custom-c-coastal-renderer)

### Why it improves Standard

Particle positions preserve the motion of individual foam features without repeatedly interpolating the whole density image. Localized birth patterns can stretch into filaments and split around rocks. A controlled distribution of particle sizes/lifetimes avoids identical white dots.

**Critical distinction:** advected marker particles do not automatically create physical surface-tension clustering. Convergent flow and localized sources create patches; genuine wet-foam cohesion requires additional modeling. Do not add an all-pairs SPH solver to Ultra. If needed, test a small grid-density-based cohesion/repulsion approximation, explicitly labeled as such. The 2007 shallow-water foam paper shows why surface tension is a separate physical ingredient. [1](https://cgl.ethz.ch/Downloads/Publications/Papers/2007/Thue07a/Thue07a.pdf)

### Cost controls

- Start at **32,768 live surface particles**, with a fixed pool and spatial emission quotas.
- Update particles entirely on the GPU. No per-frame JavaScript particle loop or particle readback.
- Splat into a **512² water-space map**, then sample it in the water shader. This avoids displaying every overlapping foam sprite directly at full screen resolution.
- Limit splat footprint and tile coverage. The dangerous cost is **overdraw**, not just the number of particle updates.
- Keep a coarse distant map; use particles where their detail is visible. Crossfade at the ownership boundary.
- Give water motion a real local velocity evolution if believable recirculation is required. A small shallow-water/inertial flow field is more useful here than simply doubling particle count. Such a field can represent horizontal swirling motion that a vertical-wave-only model cannot. [3](https://matthias-research.github.io/pages/publications/hfFluid.pdf)

**Avoid double transport:** within the particle-owned region, reconstruct into a cleared scratch map. Do not also advect yesterday's particle density at full weight; that duplicates history and can make foam move too quickly or accumulate incorrectly. Transfer density into the far representation once at handoff.

### Limits and recommendation

Ultra still primarily represents **floating surface foam**. Very close bubble-wall structure and vertical splash sheets remain approximations. It is the best candidate for the normal high-quality river setting, but depends on the shared obstacle/flow work—not merely on a particle renderer.

## 5. Candidate 3 — Cinematic: localized hybrid whitewater

### Mechanism

Add a bounded secondary-particle system around rapids, impacts and the camera's important water region:

- **Spray:** airborne droplets with ballistic motion and drag; impacts can seed foam/entrained air.
- **Foam:** surface-constrained markers with age, thickness and breakup.
- **Bubbles:** submerged air markers with buoyant rise and drag toward the water velocity; reaching the free surface can convert them into foam.

Use the **Ihmsen spray/foam/bubble taxonomy** and physically motivated source/lifetime ideas, but adapt them to grid-sampled fields instead of importing its original particle-fluid neighbor searches. Combine that with the **shallow-water + secondary-particle architecture** demonstrated by Chentanez & Müller. This is a proposed hybrid adaptation, not either paper reproduced verbatim. [7](https://link.springer.com/article/10.1007/s00371-012-0697-9) [3](https://matthias-research.github.io/pages/publications/hfFluid.pdf)

Start with one **16 × 16 m, 128² local water-flow patch** and at most **98,304 particles total across all three classes**. The 12.5 cm local grid still does not resolve actual bubble films. Keep the base tier outside the active patch. Add hysteresis to class transitions to prevent particles flickering between foam and bubbles at the moving surface.

### Why it improves Ultra

The view gains vertical separation: spray leaves the surface, submerged air rises, and new surface foam has a plausible origin. Foam appears thicker through density-dependent coverage and lighting, rather than by increasing a uniform white blend. These features matter most in low, near-water and underwater cameras.

### What keeps it feasible on GTX

- No RT cores, Tensor cores or CUDA dependency in the shipping browser path.
- A small 2.5D driving patch, **not a full-world 3D FLIP/SPH/two-phase solve**.
- One-way secondary coupling in the first version. Do not claim displacement of the primary water by foam/bubbles or globally conserved water/air mass.
- Cheap texture-based velocity/normal lookup; bounded collision substeps near obstacles.
- Half-resolution spray/bubble accumulation with depth-aware upsampling; bounded footprint and emission rate.
- Bubble optical-depth/lighting approximation rather than raymarching a high-resolution foam volume along every water ray.
- Full physical wet-foam interactions, film drainage, Plateau-law topology and multi-layer foam stacks remain outside the initial tier. Wētā's coupled wet-foam work is a visual/modeling reference, not the proposed runtime dependency. [6](https://dl.acm.org/doi/10.1145/3528223.3530059)

### The real integration cost

Cinematic is **not just an extra shader parameter**. Frontier must provide:

1. **Opaque hit distance** for cliff/terrain occlusion.
2. **Water-interface distance and side information** for above/below-water ordering.
3. A composition stage in which surface foam modifies the water interface, bubbles affect the **transmitted** contribution, and spray is composited in air with correct occlusion.

Reuse hit data from the main raymarch instead of repeating the entire terrain trace for each particle. Do not draw white bubble billboards on top of an already-composited water image: that loses refraction/attenuation and produces halos. A screen-space shallow-bubble layer is an approximation and needs grazing-angle/underwater tests; accurate arbitrary-path refraction through a 3D bubble population is outside the cheap first version.

**Recommendation:** only proceed after Ultra passes its GTX budget and the hit-buffer/compositing prototype works. On slow GTX systems, reduce the active patch/particle/overdraw budgets or fall back to Ultra; do not silently replace the simulation with more noise and keep the Cinematic label.

---

## 6. Shared rules that decide whether the result looks real

### Use appropriate sources

- Gate generation with **water motion/energy and air-entry or impact conditions**. Shallow water or a nearby bank is not sufficient on its own: a motionless pond should not acquire a permanent uniform white outline. Crest's documentation explicitly distinguishes a shallow-depth threshold from actual distance to shore. [2](https://crest.readthedocs.io/en/latest/user/water-appearance.html)
- Vorticity helps shape existing patches; it is **not by itself proof of aeration**. Avoid producing unlimited foam inside every calm swirling pool.
- Use **water-surface** curvature/strain for breaking proxies, not the terrain-curvature mask made for SatMaps. Sediment should influence turbidity separately; sediment is not foam.
- Cache wetness, bank clearance and seabed information from the SDF near the **actual water level**. The uppermost terrain height can be a roof or bridge. Find the first solid below a wet water point rather than treating that roof as the riverbed.
- The current wave function supplies height and slopes, but no horizontal displacement field. A Tessendorf-style breaking Jacobian requires that horizontal deformation; with none, `det(I + ∂D/∂x)` is 1. A steep-normal test may be an artistic source proxy, but it is not that Jacobian. [1](https://inria.hal.science/hal-00967078v1/document)

### Appearance is not the simulation

Use a filtered foam coverage/height atlas for porous breakup; mix bubble sizes, density and age. Match the material to the current light/shadow and attenuate submerged contributions. Avoid oversized repeated Voronoi cells, camera-attached detail, glowing white foam in shadow and over-bright additive accumulation. Prefilter distant coverage rather than letting a hard threshold twinkle; the ocean-whitecap paper is useful specifically for this scale/filtering problem. [1](https://inria.hal.science/hal-00967078v1/document)

### Keep history stable

Foam evolution should use a fixed effect timestep independent of erosion iterations and viewport FPS. Pause/resume must not inject one huge timestep. Adaptive display resolution and camera movement must not clear or rescale the world-space simulation. Terrain edits/water-level changes invalidate affected wet/obstacle cells; seed changes and reset clear/reseed state deliberately. Compare mode needs consistent original-geometry masks/history, not live foam passing through the original rocks. PNG capture must include the same foam state as the displayed frame.

If a local shallow-water solver is added, enforce its stability restriction using cell spacing, velocity and gravity-wave speed—not a blanket “30 Hz is always stable.” Bound substeps, report saturation and lower the quality/rate of forcing rather than allowing runaway work. Initial project persistence can save parameters and seed; exact foam-history restoration would need a separately designed/versioned state cache, not a large particle dump in the JSON header.

## 7. WebGL2 implementation path and RTX scaling

Backend and GPU family are separate choices: a GTX card can use WebGPU when its browser/driver exposes it, and an RTX card can use WebGL2. Feature detection and measured headroom—not the product name—should select the path.

| Concern | WebGL2 path (GTX or RTX) | WebGPU path (GTX or RTX) |
| --- | --- | --- |
| Foam-map update | Ping-pong render-to-texture fragment passes | Compute to storage textures or equivalent render passes |
| Particle state | Transform feedback or texture-backed fixed-pool updates; instanced quads for splats | Storage buffers, compute compaction and indirect drawing |
| Formats | Prefer tested RGBA16F targets; query float-render-target support and framebuffer completeness | RGBA16float maps, f32 simulation math by default |
| Float accumulation | Probe actual blending behavior; do not assume renderability implies arbitrary 32-bit float blending | Prefer supported 16-bit float blending; query optional capabilities before using other formats |
| CPU traffic | Dispatch and parameter changes only; no continuous particle/map readback | Same; GPU-generated active counts avoid CPU synchronization |
| Scaling | Reduce density resolution, particle cap, patch area and overdraw independently | Increase those limits based on measured headroom, not the “RTX” label |

WebGL2 transform feedback can record shader outputs into buffers. Float color rendering is an extension capability; **32-bit floating-point blending is a separate capability**, and support for `EXT_color_buffer_float` does not imply `EXT_float_blend`. If the required path is unavailable, use an explicitly tested lower-precision compatibility path or retain Low, rather than failing renderer startup. [1](https://developer.mozilla.org/en-US/docs/Web/API/EXT_float_blend) See also the MDN [float-render-target specification notes](https://developer.mozilla.org/en-US/docs/Web/API/EXT_color_buffer_float) and [transform-feedback API](https://developer.mozilla.org/en-US/docs/Web/API/WebGL2RenderingContext/transformFeedbackVaryings).

**RTX scaling suggestion:** begin by allowing 512² Standard state, 65–131K Ultra particles, or 131–196K total Cinematic secondary particles and a larger local patch. Increase one axis at a time. These are experimental limits, not guaranteed settings. Half-precision storage can save bandwidth without relying on fast half-precision arithmetic; `shader-f16`, subgroups and timestamp queries must be capability-gated. The WebGPU specification lists them as optional capabilities. [3](https://www.w3.org/TR/webgpu/)

## 8. Memory and workload arithmetic

The following are **calculated allocations**, not measured VRAM use. They assume 8 bytes per RGBA16F texel and 64 bytes per particle state; allocator overhead, extra temporary resources and the existing app are additional.

| Allocation | Calculation | Result |
| --- | --- | --- |
| Standard foam ping-pong | 2 × 256² × 8 | **1.00 MiB** |
| 512² reconstruction pair | 2 × 512² × 8 | **4.00 MiB** |
| 1024² reconstruction pair | 2 × 1024² × 8 | **16.00 MiB** |
| Ultra particle ping-pong | 2 × 32,768 × 64 | **4.00 MiB** |
| Cinematic particle ping-pong | 2 × 98,304 × 64 | **12.00 MiB** |
| Local 128² state pair | 2 × 128² × 8 | **0.25 MiB**, excluding solver scratch/bathymetry |
| Two full-1080p R32F hit maps | 2 × 1920 × 1080 × 4 | **15.82 MiB** |
| Two half-1080p RGBA16F effect targets | 2 × 960 × 540 × 8 | **7.91 MiB** |

A 256² map at 30 Hz visits about **1.97 million cells/second**; 512² visits 7.86 million; 1024² visits 31.46 million. These are cell visits for one pass, **not operation counts or GPU timings**. Doubling both dimensions quadruples each full-grid pass, before extra filtering or splat overdraw.

The 8/24/64 MiB ceilings allow room for sources, driving-flow/obstacle fields, atlases and scratch. Cinematic's hit buffers are a material part of its cost; do not hide them outside the tier's budget. Share compatible existing attachments where possible, but none are assumed free in the calculations above.

## 9. What I would not choose as a primary candidate

- **More scrolling noise or a more detailed foam bitmap alone:** useful as appearance input, but it does not add foam lifetime, transport or a population. It would repeat the current limitation.
- **An FFT/Jacobian-only ocean upgrade:** useful for open-sea whitecaps, not sufficient for a rock-filled river, persistence or air entrainment. Borrow its source/filtering ideas where the required deformation data exists.
- **A complete FLIP/SPH or two-phase bubble-film solver across the 96 m scene:** too large an architectural and computational commitment for this GTX-first request. The one-way secondary-effects simplification is deliberate.
- **The full Wētā wet-foam solver labeled “cheap Cinematic”:** unsupported by GTX-browser evidence. A visually inspired, clearly simplified local model is a different claim.
- **A huge raymarched 3D foam volume:** Frontier already has expensive water/terrain rays. Start with density maps and bounded splats; profile before adding another volume traversal.
- **Claiming native CUDA/DX11 timings as browser performance:** neither the GTX 480 paper nor the RTX 4090 Laptop demo establishes WebGL/WebGPU timings for this app.

## 10. Implementation order and proof required

1. **Instrument Low.** Separate foam/streak work from the existing water reflection/refraction and terrain cost. Hold camera, seed, water settings and render resolution fixed.
2. **Shared surface-data contract.** Provide water velocity, wet mask, bank clearance, appropriate seabed depth and source intensity. Reuse the SDF; do not reuse D8 catchment as velocity.
3. **Standard prototype.** Add fixed-step persistent density/age, boundary handling, generation/fade controls and debug maps. Measure before adding corrected advection.
4. **Ultra prototype.** Add fixed-pool surface particles and transient reconstruction. Improve local driving flow and test particle/map handoff. Keep surface foam in the existing water shading path.
5. **Cinematic prototype.** Add hit buffers/compositing first, then a bounded local water patch and three secondary states. Treat correct underwater ordering as a release gate.

### Acceptance tests

- **Persistence:** stop an emitter; its existing patch travels and decays rather than vanishing or respawning from a global noise phase.
- **Time consistency:** equivalent simulated time at 30/60/120 display fps produces comparable density and lifetime. Source-off stationary decay follows the selected half-life within numerical tolerance.
- **Transport:** a diagnostic uniform 1 m/s flow moves a patch centroid approximately 1 m in one simulated second. Account separately for boundary loss, decay and generation.
- **Obstacle interaction:** foam does not cross a solid boulder/roof; downstream splitting is visible. Only claim physical recirculation after a velocity solver actually supplies it.
- **Quiet water:** no unlimited emission from low-energy shallows or a calm vortex.
- **Cinematic state transitions:** spray impact/bubble rise/foam conversion are coherent; no rapid class flicker, airborne surface foam, or underwater white overlays through rock.
- **Visual stability:** no looping seam, UV reset pulse, excessive grid blur, distant sparkle, or camera-linked swimming. Include a two-minute looping-flow test.
- **Application regressions:** sculpt/undo/reset/compare, water-level changes, adaptive resolution, hidden-tab resume, context loss, saved parameters and owned PNG capture still work.

### Hardware measurement protocol

Test a real GTX 1060 6 GB, GTX 1650 4 GB and GTX 1660-class device where available, then an RTX 2060/3060-class device. Record exact model, VRAM, driver, browser, backend and CPU. Use the same seeds and cameras at 1080p; include a 1440p fill-rate stress case and a near-surface high-overdraw shot. Run calm water, a fast bend/boulder, an overhang, sculpting during flow, and a near/underwater view.

Warm up, record at least 60 seconds per case, and report median/p95/p99 GPU time, simulation-update spikes, CPU submission time, active particles, splat pixel workload and peak allocations. Compare each tier to Low **with adaptive resolution locked**, then test the adaptive mode separately. A synthetic software-GPU browser run can validate correctness; it cannot validate GTX/RTX performance.

Use asynchronous WebGL timer queries or optional WebGPU timestamp queries where available, discarding invalid/disjoint results and accounting for timer precision. Without those, report coarse end-to-end frame measurements as such—not invented sub-millisecond kernel timings. [3](https://www.w3.org/TR/webgpu/) [3](https://registry.khronos.org/webgl/extensions/EXT_disjoint_timer_query_webgl2/)

## 11. Source access and licensing notes

- **Crest:** public code and both root/subdirectory MIT notices inspected at commit `db0658ff0b2e93e4a9e28cc2867509658b0ecc00`. Retain the notices if code is adapted. This statement does not license separate paid Crest products or unrelated assets.
- **Chentanez & Müller, Thürey et al., Ihmsen et al.:** primary PDFs consulted, including their performance tables. [3](https://matthias-research.github.io/pages/publications/hfFluid.pdf) [1](https://cgl.ethz.ch/Downloads/Publications/Papers/2007/Thue07a/Thue07a.pdf) [1](https://cg.informatik.uni-freiburg.de/publications/2012_CGI_sprayFoamBubbles.pdf)
- **Wētā:** abstract and official publication page consulted. The official PDF could not be retrieved in this session; no detailed runtime claim is inferred. [6](https://dl.acm.org/doi/10.1145/3528223.3530059) [2](https://www.wetafx.co.nz/videos/guided-bubbles-and-wet-foam-for-realistic-whitewater-simulation)
- **Saalfrank/OMYOG:** developer description embedded in the 80 Level article; this is practitioner-reported evidence, not an independently reproduced benchmark. [2](https://80.lv/articles/real-time-shoreline-simulation-in-custom-c-coastal-renderer)
- **Whitecaps source:** the public repository's API did not identify a license. A downloadable paper or visible repository is not by itself permission to redistribute all code/assets. Use the method as a reference and verify licensing before copying. [7](https://github.com/jdupuy/whitecaps)
- No third-party foam code, video frames or texture packs were copied into Frontier for this report.

## Bottom line

**Standard first: persistent transported density. Ultra next: GPU surface particles plus better local flow. Cinematic last: a capped local whitewater hybrid with correct depth and underwater composition.** All three can be designed without RTX-only hardware. The biggest realism gain comes from *where foam forms, how it moves, how long it lives, and whether it interacts correctly with water and rock*—not from adding another layer of noise.
