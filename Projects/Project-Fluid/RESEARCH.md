# Project Fluid — Flux C++/Vulkan port research

## Provenance and correction

This project ports **Flux 0.3**, commit `c708b47926dec2e31d08b2a0bc01ab15c84983c8`
from `eosclient0001-rgb/Frontier`, branch `arena/01a0c4da-frontier`. The earlier
spectral-ocean implementation was removed because it came from the wrong source.

## Additional Ripple pond provenance

Project Fluid also carries the original Ripple pond from
`eosclient0001-rgb/Frontier`, branch `arena/01a0b20a-frontier`, commit
`35aa3778ce5d487d6567dce1a1987b6128b29a1f`. `PondWave.cpp` is the native
adaptation of that branch's `solver.mjs`; `PondBody.cpp` adapts
`floating-body.mjs`. This is deliberately a separate mode from Flux and from the
same branch's later WebGPU WCSPH/experimental fixed-iteration DFSPH volume demo.

Ripple integrates
`h_tt = c^2 laplacian(h) - damping h_t` on a fixed-world-space rectangular grid.
The default 12 x 8 m domain starts at 144 x 96 samples, uses wave speed 1.4 m/s,
damping 0.45, reflective outer/rock boundaries, symmetric exponential damping,
and velocity-Verlet steps bounded by
`min(1/90, 0.35 / (c sqrt(1/dx^2 + 1/dz^2)))`. Adaptive refinement interpolates
height and velocity without changing physical extent, under a 160,000-sample
budget. The source's +/-0.55 m safety clamp and diagnostics are preserved.

The floating duck remains the source's lightweight kinematic/buoyant coupling,
not a rigid-body pressure solve. It uses bounded 1/120 s substeps, surface-slope
drift, spring/damper heave, wet-mask containment, smooth heading, and finite bow
mound/stern depression disturbances. Thus wakes are solver state rather than
painted trails. The CPU proof samples the real adaptive mesh and its normals,
uses the five source-style optical presets, and visibly draws the domain and rock
obstacles. Limitations are explicit: a height field cannot overturn, break,
represent vertical jets, or provide the localized 3D behavior of Flux.

The branch's WebGPU WCSPH experiment uses GPU cell-linked lists, density and
pressure kernels, contacts, and screen-space reconstruction. Its optional DFSPH
path is experimental and fixed-iteration; its own notes do not establish
production incompressibility. Neither experiment has been relabeled as the
original Ripple pond or as the existing Flux solver.

---

The source is a CPU Position-Based Fluids laboratory, not an ocean FFT. The C++
port preserves its defining scene and scale: 1,440 initial particles, 2,800
capacity, a `3.90 x 3.51 x 2.50 m` basin, 0.31 m support radius, 265 reference
density, 1/60 s fixed steps, adaptive six-pass balanced PBF pressure projection,
water/milk/honey/chocolate controls, pouring, stirring, and the sampled-source
sphere obstacle location. Vulkan replaces WebGL for rendering; simulation stays
on the CPU because that is the architecture and research claim of the referenced
commit. `Project-Fluid-CPU` is the deterministic headless path used for proof.

### C++ adaptation boundaries

- The PBF density and pressure equations, under-relaxed lambda, 0.035 m correction
  limit, fixed bounds, sphere projection, source lattice, material coefficients,
  Carreau-style thinning response, pairwise capillary response and fixed-step
  policy follow the source.
- Basin and sphere density support now use the source implementation's sampled
  Akinci boundary cloud. Floor/wall samples use 0.157 m spacing, the sphere uses
  quasi-uniform Fibonacci samples, and every sample receives
  `Psi_b = rho0 / sum_k W(x_b-x_k)`. Samples contribute density, pressure
  gradients, adhesion, and stationary-boundary viscosity; analytic constraints
  remain only as a non-penetration safety net.
- The authoritative CPU proof now evaluates the sum of all bounded,
  volume-normalized PCA kernels on a 3D field. Only dirty 8³-cell bricks are
  reevaluated. Indexed Marching Cubes chunks are rebuilt for those bricks and
  welded by global grid-edge identity. Recorded proof meshes have zero open and
  zero non-manifold edges. The previous depth/thickness projection remains only
  as the interactive Vulkan fallback.
- Vulkan kernels use Frontier's `.slang` file convention and lower to SPIR-V.
  CPU and Vulkan resolves consume the source commit's base colour,
  Beer–Lambert absorption, opacity, roughness and IOR values for all four
  materials, with refraction, Fresnel and rough highlights. The explicit orange
  basin and opaque striped sphere remain visible and depth-tested so bounds and
  collision behavior remain auditable.
- Native viscosity now uses backward Euler and a symmetric radial SPH Laplacian,
  solved matrix-free with preconditioned conjugate gradients. Fluid pairs are
  assembled once, sampled stationary solids contribute positive rank-one blocks,
  and the native solver uses full 3x3 block-Jacobi inverses rather than the
  source TypeScript path's scalar diagonal preconditioner. Iteration count and
  relative residual are retained in solver diagnostics.
- Native capillarity now evaluates the complete Flux/Akinci pair model: weighted
  color-field normals, the piecewise cohesion kernel, equal/opposite pair
  scattering, density-deficiency correction, and sampled-solid adhesion.
- Mesh normals are finite differences of the summed density field, not
  screen-space depth gradients. One positive and one negative Laplacian pass
  provide mild Taubin smoothing; the mesh is then uniformly rescaled around its
  centroid to restore its exact pre-smoothing signed volume. The OBJ exporter
  writes this same indexed surface. This implements the requested Yu–Turk field
  and mesh stage but does not claim the paper's exact parameter calibration.
- The WebGL anisotropic screen-space reconstruction is not falsely relabeled as
  ReSTIR. Future ReSTIR integration consumes an immutable particle/surface
  snapshot after the fixed step, writes fluid motion vectors, and rejects
  temporal reservoirs across material/depth/normal/disocclusion failures.

---

## Original Flux 0.3 research record (verbatim)

# CPU fluid research notes — Flux 0.3

Flux is a browser-scale adaptation of published ideas, not a reproduction of the papers' complete pipelines or reported results. All simulation, surface-neighborhood analysis, and material-response evaluation run on the CPU. WebGL 2 renders the output.

## 1. Surface tension and wetting

**Reference:** Nadir Akinci, Gizem Akinci and Matthias Teschner, *Versatile Surface Tension and Adhesion for SPH Fluids*, SIGGRAPH Asia 2013.

Paper: https://cg.informatik.uni-freiburg.de/publications/2013_SIGGRAPHASIA_surfaceTensionAdhesion.pdf

### Implemented in `src/surface-tension.ts`

- The paper's piecewise cohesion kernel (eq. 2), with repulsion at short range and attraction farther apart.
- Unnormalized color-field gradient normals, using the poly6 kernel gradient. Solid samples contribute to the occupied-space estimate so a wall is not simply treated as missing fluid/air.
- The normal-difference surface-area term (eq. 3), combined with cohesion and a bounded symmetric density-deficiency correction (eqs. 4–5).
- Each fluid pair is evaluated once; equal/opposite accelerations are scattered to both particles. The old linear attraction and pressure-stage artificial-tension term have been removed.
- The compact-support adhesion kernel (eq. 7) attracts fluid toward volume-weighted solid samples.

The internal `cohesion` property is retained for compatibility with the UI code, but now controls surface tension through `gamma = 8 * cohesion`. Export calls this parameter `surfaceTension`. This is an **uncalibrated scalar**, not N/m. Wall wetting sets the adhesion coefficient independently; it is not a prescribed or measured contact angle.

Explicit capillary/adhesion acceleration is limited to 35 scene units/s² using one global scale, preserving fluid-fluid linear force cancellation under the limiter. The density-deficiency multiplier is capped at four. These are stability adaptations, not part of a proof of physical accuracy. The curvature term is not pairwise central, so the implementation does not claim angular-momentum conservation for the complete capillary model.

### Validation

`tests/interfaces.test.ts` checks kernel support and signs, continuity at the cohesion branch point, internal force cancellation, zero-force disabling, a suspended elongated drop becoming rounder without significant bulk translation, and stronger adhesion producing more spreading in a matched reduced-gravity experiment. These are numerical/qualitative checks, not Young–Laplace or contact-angle calibration.

## 2. Sampled solids and pressure projection

**Reference:** Nadir Akinci et al., *Versatile Rigid-Fluid Coupling for Incompressible SPH*, SIGGRAPH 2012.

Paper: https://cg.informatik.uni-freiburg.de/publications/2012_SIGGRAPH_rigidFluidCoupling.pdf

### Implemented in `src/boundaries.ts` and `src/physics.ts`

The floor and walls use fixed surface samples. An optional stationary sphere uses quasi-uniform Fibonacci sampling. Duplicate corner samples are removed. Each sample gets a pseudo-mass:

`Psi_b = rho0 / sum_k W(x_b - x_k)`

This weights its contribution according to local solid-sample density. The samples participate in:

- Fluid density estimation.
- The density constraint's gradient with respect to the **fluid** particle. Solids have no free pressure-solve degrees of freedom.
- Adhesion and color-field normal estimates.
- Implicit stationary-boundary viscosity, as symmetric positive rank-one additions to each fluid particle's matrix block.

The fluid/solid neighbor graphs use spatial hashing and are rebuilt during projection, rather than retaining an arbitrarily stale graph for the entire solve. A box projection and sphere signed-distance constraint provide a geometric nonpenetration safety net. The collider's 0.0785 center-clearance represents half a rest particle spacing.

The basin samples end at the visible rim, but the box safety bounds still confine particles above it; **spill-out over the rim is not modeled**. Solids are stationary and infinitely massive: forces are not integrated into rigid-body motion. This is **one-way static coupling**, not the complete two-way method in the paper or support for arbitrary meshes.

### Pressure accuracy

The solver remains PBF, not DFSPH or a pressure-Poisson solver. Its density constraint now uses the exact derivative of its poly6 density kernel for both fluid and solid contributions. The under-relaxed Jacobi update has a bounded displacement, and solid samples enter the particle's own gradient rather than being incorrectly counted as free unknowns.

| Mode | Maximum positive-density-error target | Maximum projection passes |
|---|---:|---:|
| Fast | 6% | 3 |
| Balanced | 3% | 6 |
| Precise | 1% | 12 |

At least two passes are considered. The graph is refreshed every second pass and once more for final measurement. Targets are **not guarantees**: exhausted budgets report `BUDGET LIMIT`, and both mean and peak errors are shown. The metric is `max(rho / rho0 - 1, 0)`; ordinary free-surface underdensity is not counted as compression. Truncated-neighbor overflow is reported and prevents a convergence claim. This does not measure exact total reconstructed volume or divergence error.

The initial basin lattice is pressure-relaxed at rest before velocity integration. Otherwise the initial boundary overlap can be converted into a large, nonphysical startup impulse. Reset remains deterministic.

### Validation

Tests check pseudo-mass normalization, restored density near a wall without filling remote air, compressed-lattice error reduction, the effect of a larger iteration budget, final-state diagnostics, overflow reporting, sphere nonpenetration, dissipative stationary-boundary viscosity, and a quiet basin startup. More rigorous hydrostatic and convergence studies remain future work.

## 3. Shear- and temperature-dependent viscosity

**Context:** Chocolate rheology is recipe- and processing-dependent. No single model fits all samples or all shear-rate ranges.

Open review: https://www.scielo.br/j/cta/a/Ggm9YqGn3nvLTqRVcZyYnPs/?lang=en

### Implemented in `src/rheology.ts`

The flow is a generalized Newtonian approximation. A weighted least-squares fit estimates the local velocity gradient from fluid neighbors. The strain-rate invariant is:

`gammaDot = sqrt(2 D:D), D = (grad(v) + grad(v)^T) / 2`

Full-rank affine velocity fields are reproduced by the fit, so rigid rotation does not appear as shear. Degenerate/sparse neighborhoods conservatively use zero shear instead of producing unstable large rates; the estimated rate is capped at 250/s.

A bounded Carreau response (Yasuda exponent `a=2`) supplies the apparent kinematic coefficient:

```
nu0 = 0.5 * baseViscosity²
n = 1 - 0.85 * shearThinning
nu(gammaDot,T) = nu0 * [0.12 + 0.88 * (1 + (0.6 gammaDot)²)^((n-1)/2)] * aT
aT = exp[(E/R) * (1 / T_kelvin - 1 / T_reference_kelvin)]
```

The temperature exponent is limited to [-3, 3] and apparent viscosity to at most 2 solver units. Zero base viscosity remains zero. A zero shear-thinning control recovers a temperature-dependent Newtonian coefficient.

| Material | Reference temperature | Illustrative E/R | Default thinning |
|---|---:|---:|---:|
| Water | 20°C | 1,800 K | 0 |
| Milk | 20°C | 2,200 K | 0.08 |
| Honey | 25°C | 6,500 K | 0 |
| Chocolate | 40°C | 5,000 K | 0.8 |

**These parameters are illustrative, not measured values for the named materials.** Temperature is prescribed uniformly throughout the liquid. There is no heat transport, latent heat, melting, tempering, crystallization, thermal expansion, yield-stress threshold, or thixotropic memory. Cold chocolate still remains a fluid in this model. Carreau describes continuous thinning, not a true solid-to-liquid transition.

The effective viscosity field is evaluated before each implicit viscosity solve and frozen during that linear solve. Fluid-pair coefficients use the symmetric harmonic mean of the two apparent viscosities. Boundary coupling uses the local fluid coefficient. Changing temperature or thinning while paused refreshes the diagnostic field without advancing particle motion.

### Implicit operator

**Reference:** Marcel Weiler et al., *A Physically Consistent Implicit Viscosity Solver for SPH Fluids*, Eurographics 2018.

Paper: https://dankoschier.github.io/resources/papers/WKBB18.pdf

`src/viscosity.ts` uses radial SPH velocity-difference projections, backward Euler and matrix-free preconditioned conjugate gradients. It assumes equal fluid masses and constant reference density; it uses diagonal rather than block-Jacobi preconditioning, a maximum of 18 iterations and relative residual target 1e-5. It does not include a DFSPH stage or a nonlinear viscosity iteration. The UI shows actual iteration count and residual.

Legacy XSPH is retained for comparison. It uses the new apparent-viscosity field, but has neither the same physical interpretation nor the sampled implicit boundary coupling. The controls are not calibrated to equal physical viscosity across the two algorithms.

### Validation

Tests cover the analytical two-particle backward-Euler solution, uniform translation, rigid rotation, energy dissipation, isolated fluid-operator momentum conservation, decay under timestep refinement, general-direction stationary-solid response, the Newtonian limit, monotonic shear thinning and warming response, affine-gradient reconstruction, and cold-chocolate stability with a sphere. Conservation tests apply to the isolated viscosity operator, not to the whole bounded simulation.

## 4. Surface rendering and vorticity retained from 0.2

**Yu & Turk (2010):** https://faculty.cc.gatech.edu/~turk/my_papers/sph_surfaces.pdf

CPU weighted covariance/PCA produces bounded ellipsoid axes and smoothed render centers. Physics positions are not changed. Sparse particles use small spheres; dense neighborhoods use volume-normalized ellipsoids. Reconstruction is cached while particle revision and reconstruction mode are unchanged. It adapts the paper's neighborhood-analysis idea, not its complete summed implicit field and Marching Cubes surface.

WebGL 2 shares analytic ray–ellipsoid intersections between depth and thickness passes. Optical path contributions are nominal-particle-volume weighted and clipped at opaque geometry. Six bilateral passes reconstruct a front surface. Front-interface Snell refraction, Beer–Lambert-style absorption, approximate thickness-dependent scattering, Fresnel reflection, roughness-filtered studio lighting and FXAA produce the image.

This remains screen-space rendering: overlap artifacts, transparent sorting, undersampled sheets and blobby silhouettes remain possible. Nominal splat normalization does not guarantee exact reconstructed liquid volume. There is no multiple scattering, physical caustic solve, second-interface ray tracing or path tracing.

**Macklin & Müller (2013):** https://mmacklin.com/pbf_sig_preprint.pdf

Optional vorticity confinement estimates existing curl and its magnitude gradient, adding a capped `epsilon (N × omega)` correction. It intentionally restores energy lost through numerical damping; it does not inject random noise and it is not energy-conserving. Honey/chocolate default to zero recovery. The new physical-interface terms replace the old artistic cohesion force and pressure-stage artificial-tension term.

## 5. Experiments and controls

The selector in the viewport's upper-left switches experiments:

- **Liquid basin:** 1,440 initial particles; gravity 9.81; material-default tension; pouring enabled. Enable the sphere to inspect fluid/solid interactions.
- **Wetting drop:** 227 particles; gravity **1 m/s²** and tension **0.080**; pouring disabled. Compare wetting 0 and 2, resetting each trial. Reduced gravity is deliberate to expose capillary effects at this coarse scene scale, not a claim of normal-gravity millimeter-droplet accuracy.
- **Suspended drop:** the same elongated 227-particle initialization at **zero gravity**, tension **0.080**. Compare enabled tension with zero, resetting each trial.

Experiment changes update the visible gravity/tension controls. Selecting a material afterward applies that material's tension, temperature, thinning, viscosity, wetting and vorticity defaults. Reset preserves the current experiment and controls; Reset Scene Controls returns everything to the water basin defaults.

For warm/cold chocolate, choose Chocolate and compare 20°C with 60°C after resetting at each temperature. Use the mean effective viscosity, local-range tooltip and mean shear-rate readout. These are current constitutive-model evaluations, not rheometer measurements.

Boundary-density support can be switched off for a density/pressure comparison; geometry protection, adhesion and implicit boundary viscosity remain active. Show Solid Samples displays the actual boundary points.

For surface A/B, pause and switch **Anisotropic / Spheres** on the identical particle state. Strict motion comparisons use fixed-step tests, not uncontrolled human click timing.

## 6. Performance and remaining work

The local benchmark reports frame intervals, CPU physics time and CPU reconstruction time. The physics measurement includes the added capillary, boundary and rheology work. It does not measure actual GPU execution or compare against Unreal. Targets and budgets trade CPU cost for numerical error; no fixed frame-rate guarantee is made.

Remaining priorities: calibrated surface tension/contact angles and rheology; divergence/hydrostatic validation; a consistent capillary timestep strategy; nonlinear viscosity convergence; thermal transport and phase behavior; moving/two-way solids and arbitrary SDFs; worker-based scheduling and profiling; accurate temporal surface reconstruction and secondary spray/foam. A GPU compute port is optional future work, not a dependency of these CPU improvements.
