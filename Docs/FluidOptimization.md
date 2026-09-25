# Flux CPU optimization — measured results and remaining GPU work

2026-09-25. This completes the first CPU optimization pass and stage instrumentation. **GPU-resident extraction and GPU timestamp measurements are NOT implemented in this change.** Project-Zero still loads a pond snapshot; no live engine water integration was added here.

## Changes

- Spatial cell index for particle neighbours, boundary neighbours and PCA neighbourhoods. Exact support-radius filtering and sorted particle IDs preserve accumulation order and existing 128/96 neighbour caps. The index is rebuilt from current positions; no stale neighbour reuse across pressure passes.
- Brick-to-kernel candidate lists and cached anisotropic evaluation coefficients.
- Kernel-major field accumulation restricted to each kernel's support AABB, rather than scanning all kernels for every grid sample. Shared brick samples keep the same ascending kernel accumulation order. Inverse evaluation-matrix bounds cover the field calculation; dirty invalidation still follows the existing generated orthogonal-kernel contract.
- Dense lattice-edge vertex welding instead of a hash map; sorted topology edge counts; contiguous CSR adjacency instead of per-vertex dynamic lists.
- Timings for indexing, field evaluation, triangle generation, assembly and a smoothing subset (adjacency construction, smoothing, volume correction and normals). **The smoothing subset is included in assembly time: do not add it twice.**
- Retained exhaustive neighbour/field modes as correctness oracles; added optimization equivalence CTest.

Not changed: particle count, extraction resolution, pressure iterations, viscosity model, mesh smoothing parameters or physical simulation timestep. This is a CPU implementation, not a GPU estimate. No unsafe fast-math flags were added.

## Controlled comparison

Compiled baseline sources from commit `85bf863` with GCC 12.2, `-O3 -DNDEBUG -std=c++20`; optimized build uses the same compiler/Release configuration. Ran each benchmark five times on the same sandbox. Table values are medians of the five process runs (each solver/PCA entry itself averages the iterations in Benchmark.cpp). Baseline runs preceded optimized runs; this is not randomized hardware-laboratory testing.

Workload: **1,440 particles**, **65 × 48 × 45 samples**, spacing **0.07**, **192 changed bricks**, identical benchmark sequence. CPU: sandbox Xeon @ 2.60 GHz. These routines are not explicitly multithreaded.

| Stage | Baseline median | Optimized median | Approximate speedup |
|---|---:|---:|---:|
| Particle solver step | 31.65 ms | 17.05 ms | 1.86× |
| PCA reconstruction | 6.93 ms | 4.87 ms | 1.42× |
| Initial extraction | 3720.27 ms | 19.98 ms | 186× |
| Changed extraction | 2591.80 ms | 18.05 ms | 144× |
| Unchanged extraction | 0.0129 ms | 0.0127 ms | Essentially unchanged |
| Ripple step (unchanged implementation) | 0.192 ms | 0.194 ms | Essentially unchanged |

Optimized changed-extraction breakdown (independent medians, so rounding may not sum exactly):

| Stage | Time |
|---|---:|
| Dirty detection / candidate preparation | 1.00 ms |
| Scalar field | 7.63 ms |
| Triangle generation | 2.06 ms |
| Assembly, topology diagnostics and smoothing | 7.38 ms |
| ↳ Smoothing subset already included above | 2.85 ms |

[All five baseline and optimized measurements](FluidEvidence/optimization-benchmark.json).

The solver + PCA + changed extraction remains approximately **40 ms of serial CPU work**, before rendering/upload. This is a major improvement but not a 60 FPS full-engine result. Dense distributions can still approach quadratic neighbour work; the spatial index is not a universal linear-time guarantee. All existing solver fidelity, fixed-domain clipping and GPU/vendor compatibility limitations remain.

## Correctness checks

- **6/6 Release CTests passed**, 7.97 seconds.
- **6/6 Debug UndefinedBehaviorSanitizer CTests passed**, 52.62 seconds.
- Spatial queries/order compared with brute force at 2,003 seeded sample positions, including negative coordinates and support-radius boundaries.
- Twelve solver steps compared with exhaustive particle and boundary searches, checking positions and velocities.
- PCA centres, axes and weights compared against exhaustive neighbourhood reconstruction.
- Mesh indices, positions, normals and topology diagnostics compared against exhaustive field evaluation for a full build, an incremental displacement and particle removal. Tolerances: positions 0.0002, normals 0.002.
- Separately compiled the original `85bf863` extractor/solver/PCA and exported its 12-step mesh. Comparison with optimized output had **identical face indices**, with maximum printed position/normal vector difference **1.005e-5**. This also covers the changed welding/adjacency path, which the in-process field oracle shares.
- CMake/direct Windows build-source parity gate passed. Newly changed code has not been executed under MSVC or in a Vulkan viewport.

These are deterministic fixtures, not proof of equivalence for every possible scene or degenerate input. The existing fixed vertical extraction extent (max Y=3.13 versus particle bound Y=3.70) is unchanged.

## Reproduce

```sh
cmake --preset fluid-cpu
cmake --build --preset fluid-cpu
ctest --preset fluid-cpu
build/fluid-cpu/Projects/Project-Fluid/Project-Fluid-Benchmark
```

The benchmark now prints individual extraction stages. The new optimization test intentionally runs the slow exhaustive oracle; its runtime is not the optimized runtime.

## GPU status / next stage

The sandbox has no exposed `/dev/dri` or NVIDIA device, no installed Vulkan ICD directory and no available Vulkan shader compiler/runtime tools. **No hardware GPU timing can be obtained here.** CPU timings must not be relabelled as GPU timings, and the existing standalone screen-space Vulkan renderer is not GPU mesh extraction.

The GPU extraction port remains pending, not silently marked complete. It should:

1. Upload/cache evaluation coefficients and compact brick-to-kernel offsets/IDs (initially CPU-prepared if necessary).
2. Evaluate uniquely owned field samples in compute, using local candidates; avoid races at shared brick borders.
3. Count/scan/write marching-cubes output into bounded GPU buffers, with overflow reporting.
4. Generate stable edge IDs, normals and the required smoothing/volume correction without CPU mesh readback in the rendering path. Dropping smoothing is not quality-equivalent.
5. Keep the mesh GPU-resident; use correct storage/indirect/vertex or acceleration-build barriers.
6. Place Vulkan timestamp queries around actual compute stages, check `timestampValidBits`, apply `timestampPeriod`, handle valid-bit wraparound and separate warm-up from repeat measurements. Read timestamp results after completion, rather than timing command submission on the CPU.
7. Validate sampled GPU field/mesh output against this CPU reference before claiming parity or speedup. Measure rendering and BLAS/TLAS work separately.

This pass implements priorities 1–3 and the first solver/PCA improvements from priority 5. **Priority 4 is outstanding.**
