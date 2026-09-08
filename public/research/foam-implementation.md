# Foam simulation · Frontier 0.9

**Implemented on WebGL2 and WebGPU.** Open **Environment → River surface → Foam simulation**, or use `?renderer=webgl&panel=foam`.

This is a GPU-resident, stateful effects system. It is not a full 3D two-phase water solver, a bubble-film simulation, or a claim to reproduce a particular river's measured hydrodynamics. The [research report](foam-quality-tiers.md) records the design candidates and their sources; its GTX millisecond budgets remain **unverified targets**, not measured performance of this release.

## The four tiers

| Tier | What runs | Balanced workload |
| --- | --- | --- |
| **Low** | The original stateless shoreline foam and procedural current streaks | No evolving foam map/particles |
| **Standard** | Persistent foam density and density-weighted age; obstacle-deflected driving flow; midpoint backtrace transport, bounded compression, source injection and exponential decay | 128² flow/geometry, 256² foam |
| **Ultra** | A fixed GPU surface-particle pool, advected by a bounded inertial surface-flow field; positions/lifetimes persist and the density/age map is reconstructed from cleared splats | 256² flow, 512² reconstruction, 32,768 particle slots |
| **Cinematic** | Surface foam plus ballistic spray and buoyant submerged air, with class transitions; localized whitewater emission/stirring around the camera's water focus; linear-HDR, depth-aware composition | 256² flow, 512² reconstruction, 98,304 total slots across the three classes |

New projects start on **Standard**. Projects without a foam-quality field retain **Low**, preserving their previous water appearance. Low remains available if floating-point render targets are unsupported. The UI and GPU diagnostic report show that fallback explicitly.

### Workload budget

This is independent of the foam model, and is not locked to a GPU brand:

| Budget | Standard map | Ultra map / slots | Cinematic map / total slots |
| --- | --- | --- | --- |
| Compact | 128² | 256² / 16,384 | 256² / 49,152 |
| Balanced | 256² | 512² / 32,768 | 512² / 98,304 |
| Expanded | 512² | 1024² / 65,536 | 1024² / 196,608 |

Flow-cache resolution scales with the budget, capped at 256². Expanded is for measured GPU headroom, not a promise that every RTX card can afford it. A GTX card can use WebGPU when its browser/driver supports it; an RTX card can use WebGL2. Neither path requires RT cores, Tensor cores, CUDA or native half-precision arithmetic.

Particle splat contribution is normalized by pool capacity so selecting a larger pool does not simply triple the mean amount of foam. The three-dimensional optical particles are aggregates, not literal millimeter bubbles.

## Data and simulation

- **Bank/bed cache:** samples the edited 3D SDF at the actual water level. Downward probes find the first solid **below** the water; an overhanging roof is not treated as the seabed. Geometry, wetness, bank normal and approximate lighting are cached. The SatMap D8 catchment texture is **not** used as river velocity.
- **Driving flow:** starts from the existing canyon route or compass heading and removes into-bank velocity near obstacles. Ultra adds velocity advection, damping, a bounded pressure/head correction and optional stirring. Cinematic concentrates the stronger correction/stirring in an approximately 18 m-radius focus. This is a shallow-water-inspired effects field, not a conservative flood solver.
- **Sources:** motion/energy-gated bank impact, wind/shore breakup, convergence and shear proxies. A sharp terrain feature alone is not a source, and calm zero-current/zero-wind water does not spontaneously aerate. These are visual emission models; there is no exact breaking-wave detector or Tessendorf horizontal-displacement Jacobian in the current river.
- **Standard:** backtraces with a midpoint velocity sample; dry/solid backtraces are rejected. Density undergoes bounded convergence/expansion and timestep-aware exponential decay; its weighted age is advected as well. Very small densities are cleared to avoid immortal half-float tails. This numerical transport is not exactly mass conservative.
- **Ultra:** each GPU record has an f32 position/lifetime and velocity/type. Markers move on the current procedural wave surface and obey bank/collision checks. Their map is cleared and reconstructed, not advected a second time. It does not compute foam–foam SPH forces or physical surface tension.
- **Cinematic:** spray uses gravity and drag; submerged air uses buoyancy and flow drag; particles can transition to/from surface foam. Lifetime and spatial bounds prevent unbounded accumulation. Sources are local, while existing particles can travel out of that focus and decay naturally. Surface foam outside it remains part of the capped population.
- **Appearance:** a small deterministic synthetic coverage/normal/height atlas supplies porous microstructure. It does **not** determine foam birth or movement, and it is not a photographed/scanned foam material. Both this atlas and the evolving density map have mip chains. Display samples use the world-space pixel footprint; inspection views show the base data without the decorative texture.

The actual water surface remains Frontier's procedural displaced surface. The surface-flow head variable guides motion and source behavior; it is **not a new globally simulated free surface**. In particular, this release does not add free-falling volumetric waterfalls or multi-level fluid bodies in caves.

### Implementation difference from the proposal

The research suggested a separate 128² local water patch for Cinematic. This release instead uses a **localized correction/emission region within the shared bounded 2D flow grid**. It avoids introducing a second overlapping water-domain solver. A dedicated higher-resolution local hydrodynamic patch is future work, not an implemented claim.

## Cinematic rendering

Cinematic does not draw white particles over the finished display image. The primary raymarch produces three targets:

1. Linear-HDR scene color.
2. Opaque and water hit distances, camera side, and water coverage.
3. The weighted transmitted-water contribution.

The terrain/water raymarch runs once. GPU instanced quads accumulate spray and bubble optical layers at half the raymarch target's resolution. Hit data rejects occluded fragments and inappropriate above/below-water contributions. Bubble lighting is attenuated over the underwater camera path; bubble opacity modifies the transmitted contribution rather than indiscriminately whitening reflected light. A depth-aware upsample keeps these layers away from foreground silhouettes. Filmic exposure and sRGB conversion happen **after** composition.

These are rasterized, approximate optical layers. There is no exact bent-ray tracing through every bubble, multiple scattering, bubble–bubble occlusion solve, film interference, or perfect grazing-angle reconstruction. Foam lighting uses a cached surface-light approximation. The implementation's limitation is intentionally different from claiming a production offline wet-foam renderer.

## Controls and lifecycle

- **Foam generation:** emission intensity; switching it off lets existing foam decay rather than deleting history. Low keeps the original Shore foam control instead.
- **Foam half-life:** the exponential density half-life in Standard; randomized particle-lifetime scale in Ultra/Cinematic.
- **Flow stirring:** optional bounded stirring for Ultra/Cinematic.
- **Bubble detail scale:** material/aggregate appearance, not fluid-cell resolution.
- **Airborne spray / Entrained bubbles:** Cinematic effect controls. Existing particles can finish their lifetimes when a source is disabled.
- **Pause / Restart foam:** affect only the foam history. They do not edit voxels or trigger an erosion operation.
- **Inspection:** Surface, Density, Age, Velocity, Sources, Banks & bed. These views apply to water, not the rock material. Return to Surface or select a regular shading-menu view to leave inspection.

Foam runs on a fixed 30 Hz effect clock, with two bounded flow substeps per tick and at most three effect ticks per displayed frame. Slow frames drop excess catch-up time instead of submitting unlimited work; diagnostics record the dropped time. Therefore severely overloaded hardware can show slowed foam evolution. Choose Compact/adaptive display in that situation. This is not a fabricated frame-rate counter.

Water-off, pause and original comparison freeze the live foam clock. Adaptive display resizing does not clear world-space density or particles. Terrain edits, undo and water-level changes invalidate the appropriate geometry cache; ordinary edits retain the effect history, while terrain reset/regeneration clears it. Geometry refresh is coalesced to roughly 250 ms during ordinary drawing, and an explicit capture refreshes dirty geometry.

**Original comparison uses the original geometry with Low water shading**, while retaining and freezing the live state. It does not paint current foam through old rocks, and does not pretend to have simulated a second original foam history.

Tier/budget changes intentionally restart transient foam. Projects save the controls and seed, not the potentially large particle/map cache, so reopening reseeds the effect. A project saved with paused foam reopens paused and empty until resumed. PNG export includes the current displayed effect state without advancing it. GLB remains a terrain mesh export; water/foam are not exported geometry or vertex colors.

## Memory and performance honesty

Positions and velocities are f32 GPU textures; maps are RGBA16F. The normal animation loop performs **no particle/map/volume readback for foam**. The explicit developer inspection function used in tests can read those resources on request. The UI polls CPU-side counters/allocation estimates only.

Cinematic needs additional full-resolution HDR/hit/transmission targets plus half-resolution optical targets. Its memory cost therefore depends on the raymarch resolution as well as the particle budget. The UI reports estimated allocated texture bytes, including mip levels, but not driver/pipeline overhead. Low has only tiny empty bindings and the small shared atlas; it does not retain higher-tier simulation targets after switching tiers.

The research's 0.75 / 2 / 4 ms goals are **not claimed as achieved**. Browser tests here use software Vulkan/SwiftShader for correctness, not a GTX performance benchmark. The feature must be profiled on the user's exact GPU, driver, browser, resolution and camera before assigning hardware-specific FPS claims. Expanded mode increases cost; it is not automatically selected based on a card's name.

## Code map

- `src/engine/foam/model.ts`: quality/workload caps, fixed clock, scalar reference helpers and the one-time atlas.
- `src/engine/foam/kernels.wgsl`: shared geometry, flow, density and particle-state updates.
- `src/engine/foam/shaders.ts`: shared shader assembly, particle splats and linear-HDR composition.
- `src/engine/foam/drivers.ts`: WebGL2/WebGPU render-to-texture execution, float-format capability handling, mip generation and resource disposal.
- `src/engine/foam/system.ts`: resource graph, lifetime, geometry invalidation, scheduling and diagnostics.
- `src/engine/shaders/foam-render.wgsl`: state-based surface appearance and diagnostic views.
- `src/components/FoamControls.tsx`: quality, workload, effect and inspection controls.

Both APIs use GPU render-to-texture state updates; WebGPU compute is not a requirement for this subsystem. WebGL2 uses floating-point framebuffers and instanced draws, not a CPU fallback particle simulation. The main uniform block is now **172 floats / 688 bytes**; earlier offsets remain unchanged.

## Verification

The unit suite covers fixed-rate scheduling, capped catch-up, pause, half-life, coverage bounds, bank projection, all workloads, deterministic atlas generation, old-project migration, strict settings validation, archive round trips, uniform layout, resource ownership, capture/compare behavior and disposal.

Browser tests execute both real backend implementations and check:

- Density centroid motion at a known current and decay over one simulated second.
- First-solid-below-water depth under a roof, and dry/solid bank cells.
- Unchanged voxel data after foam updates.
- Frozen history during pause, original comparison and PNG capture; restart clears it.
- Actual GPU marker populations; all three Cinematic classes; nonzero optical-layer output and finite state.
- Empty Cinematic optical layers preserve the ordinary above/below-water image within small output-quantization tolerance.
- Controls, all inspection views, workload selection, portable project import/export and mobile layout.
- An explicitly simulated lack of float-render-target support preserves a functioning Low renderer.

These checks validate the implementation, not physical equivalence to real foam or the research report's hardware timing targets.
