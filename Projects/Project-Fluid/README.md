> **CPU pass 2:** [parallel optimization and measured pipeline](../../Docs/FluidCpuPass2.md). Two-thread pipeline median: 18.67 ms over the timed fixture; GPU extraction remains outstanding. Foam/splash are queued afterward.

> **CPU performance update:** see [measured optimization results](../../Docs/FluidOptimization.md). The optimized CPU path and exhaustive reference tests are included; GPU mesh extraction is not yet implemented.

> **Consolidated Frontier integration (2026-09-25):** see [Water integration and review](../../Docs/WaterIntegration.md) for current build commands, the Project-Zero snapshot bridge, measured performance and limitations. The original project documentation follows. Vulkan is now opt-in rather than auto-enabled.

# Project Fluid — Flux native port

Native C++20/Vulkan conversion of Flux 0.3 from
`eosclient0001-rgb/Frontier@c708b47926dec2e31d08b2a0bc01ab15c84983c8`.
This is the requested 3D PBF basin simulation—not the unrelated ocean prototype,
which has been removed. Project Fluid also includes **Ripple pond mode**, ported
from `eosclient0001-rgb/Frontier@35aa3778ce5d487d6567dce1a1987b6128b29a1f`.
It is an additional broad-water 2.5D solver and does not replace Flux.

## Included

- CPU 3D PBF simulation with 1,440 initial / 2,800 maximum particles
- 0.31 m support, poly6 density, adaptive balanced pressure projection
- fixed 1/60 s clock, maximum two steps per displayed frame
- visible `[-1.95,1.95] x [0.19,3.70] x [-1.25,1.25] m` collision bounds
- visible stationary sphere obstacle and non-penetration projection
- pairwise surface response, viscosity and shear-thinning material response
- water, milk, honey and chocolate presets
- pouring, stirring, pause and deterministic reset
- shared CPU/Vulkan Yu–Turk weighted covariance/PCA kernels with smoothed
  centers and bounded, volume-normalized principal axes
- authoritative CPU summed anisotropic field on dirty 8³-cell bricks
- indexed, watertight Marching Cubes mesh with gradient normals, mild Taubin
  smoothing, exact global volume restoration, and OBJ export
- Vulkan front-depth/thickness projection remains the interactive fallback until
  the extracted mesh is connected to the planned Vulkan RT BLAS/TLAS path
- Frontier `.slang` shader sources lowered to Vulkan SPIR-V
- source optical presets: base colour, absorption, opacity, roughness and IOR
- continuous refraction, Beer–Lambert attenuation, Fresnel and rough highlights
- native CPU mirror of the same depth/thickness reconstruction, scene and optics
- particle points are simulation data only; they are not the default presentation

### Ripple pond mode

- damped linear surface-wave equation on a rectangular physical grid
- reflective shoreline and circular rock masks
- CFL-limited velocity-Verlet integration and adaptive grid refinement
- bounded floating-body substeps, slope drift, buoyancy and finite bow/stern wakes
- five optical presets: clean, ocean, swamp, puddle and dirty
- deterministic perspective CPU proof drawn from the actual adaptive height mesh

The pond and Flux solve different models. Ripple represents one-valued surface
height and cannot overturn; Flux remains the mode for pouring, splashing and 3D
free surfaces.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Project-Fluid -j
./build/Projects/Project-Fluid/Project-Fluid
```

Vulkan window requirements: Vulkan SDK/loader, `glslc` or `slangc`, GLFW3 and
a C++20 compiler. The `.slang` files follow Frontier's GLSL-in-Slang convention
and are lowered to SPIR-V by the same staged build pattern as Project Zero. Without those packages, the CPU target remains available:

```bash
cmake --build build --target Project-Fluid-CPU -j
./build/Projects/Project-Fluid/Project-Fluid-CPU proof.ppm 90 water

# Ripple pond numerical test and deterministic proof
cmake --build build --target Project-Fluid-Pond-Test Project-Fluid-Pond-CPU -j
./build/Projects/Project-Fluid/Project-Fluid-Pond-Test
./build/Projects/Project-Fluid/Project-Fluid-Pond-CPU pond.ppm clean
```

## Browser near-shore laboratory

`Web/NearShore.html` is a self-contained WebGL 2 research prototype for the
beach architecture. It evolves conservative water depth and horizontal momentum
on a `256 x 144` GPU grid with Rusanov finite-volume fluxes, bathymetry source
terms, positivity-preserving wet/dry cells, bottom friction, incoming wave
forcing, breaking/foam diagnostics, and visible swash. Run any static server at
the repository root and open `/Projects/Project-Fluid/Web/NearShore.html`.
This prototype demonstrates the coastal solver; the production port remains
Vulkan compute.

## Interactive controls

| Input | Action |
|---|---|
| Space | Pause/resume fixed-step simulation |
| R | Deterministic basin reset |
| S | Stir the actual particle velocities |
| P | Toggle the initially enabled pour until the 2,800 capacity |
| 1 / 2 / 3 / 4 | Water / milk / honey / chocolate |
| Escape | Exit |

The window title reports particle and collision counts. Every 180 frames the
console reports pressure passes, measured compression, and sphere/wall contacts.
`Project-Fluid-Surface-Test` reconstructs a deterministic surface and fails on
empty geometry, invalid indices, open/non-manifold edges, or a dirty-cache miss.

## Proof and research

- [`../../Exhibits/Project-Fluid`](../../Exhibits/Project-Fluid/README.md) contains
  a native execution frame with visible bounds and sphere interaction.
- [`RESEARCH.md`](RESEARCH.md) records provenance, equations, sources, and adaptation limits.
- [`PAPER_FIDELITY.md`](PAPER_FIDELITY.md) is the acceptance checklist for the
  strict sampled-boundary, PCG, Yu–Turk mesh, dynamic-rigid, and Vulkan RT path;
  unchecked items are explicitly not claimed as implemented.
