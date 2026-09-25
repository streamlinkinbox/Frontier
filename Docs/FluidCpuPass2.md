# Flux CPU optimization pass 2 — parallel stages and measured pipeline

2026-09-25. **Foam and splash are queued after optimization, not implemented here.** This is a validated CPU optimization milestone, not completion of the GPU extraction plan. GPU-resident extraction/timestamps, live main-engine fluid updates and native Windows/Vulkan runtime validation remain outstanding.

## Implemented

- Dense contiguous cell indexing for compact simulations, with a sparse hash fallback to avoid allocating huge empty grids. Queries retain sorted IDs and exact support filtering.
- Cached immutable boundary index with owned positions. Copies cannot retain references into another solver's vectors. Reset replaces the cache; toggling the obstacle invalidates/rebuilds it. This also fixes the inherited mismatch where collision toggles left old boundary samples active.
- Reused surface-tension, viscosity PCG and mesh-assembly scratch buffers.
- Optional OpenMP for particle/boundary queries, density evaluation, pressure corrections, PCA, field construction and independent mesh bricks. Pairwise force/viscosity accumulation and convergence reductions remain ordered/serial to avoid races and changed convergence decisions.
- A single owning brick writes each shared field sample. No concurrent writes to brick boundary planes. Completed field construction precedes mesh generation via the OpenMP region barrier.
- Inlined small vector operations; stable radix sorting for mesh topology checks. Topology checks, pressure iterations, particle count, smoothing and physical timestep were **not removed or reduced**.
- Solver-stage timings plus an actual simulation→PCA→mesh pipeline benchmark (five warm-up steps, twenty timed steps).
- `fluid-parallel` configure/build/test presets. Serial remains available; no mandatory OpenMP dependency was added to the existing serial preset.
- Optional direct Windows `-FluidOpenMP` flag, using `/openmp` and a separate `Release-FluidOpenMP`/`Debug-FluidOpenMP` object directory. Windows execution is unverified. Full-app CMake also applies OpenMP to Project-Zero when explicitly enabled.

## Five-run results

Same sandbox Xeon, GCC 12.2 Release, **1,440 particles**, extraction **65×48×45**, unchanged physical parameters. Five process runs each; medians below. Serial runs preceded parallel runs. OpenMP runs explicitly use **two threads**.

| Measurement | Previous pass (serial) | New serial | New OpenMP, 2 threads |
|---|---:|---:|---:|
| Solver, average of 60 steps | 17.05 ms | 13.28 ms | **8.57 ms** |
| PCA, average of 10 repeats | 4.87 ms | 4.37 ms | **2.21 ms** |
| Changed extraction, one sampled update | 18.05 ms | 13.09 ms | **10.34 ms** |
| Sum of those stage medians | 39.97 ms | 30.75 ms | **21.11 ms** |
| Actual pipeline, mean of 20 timed updates | Not measured | 27.67 ms | **18.67 ms** |

The actual pipeline covers earlier simulation states (steps 6–25), whereas the separate extraction timing samples a later state. **18.67 ms and 21.11 ms describe different measurement windows; do not treat their difference as a mysterious parallel overlap.** The pipeline stages run sequentially, with internal parallelism. These are CPU-only measurements, not application FPS or GPU speedups.

New changed-mesh stage medians at two threads:
- Candidate preparation: 1.02 ms.
- Field evaluation: 2.06 ms.
- Triangle generation: 1.05 ms.
- Assembly/topology/smoothing: 6.24 ms, of which 1.91 ms is the smoothing/normal subset (already included).

Solver timing fields prefixed `last_step_` report the last measured physics step, **not averages across all sixty steps**. Pressure timing includes its internal neighbour rebuilds and density passes; initial neighbour/density timing is separate.

[Raw five-run measurements](FluidEvidence/cpu-pass2-benchmark.json).

## Validation

- Serial Release: **6/6 CTests pass**.
- OpenMP, two threads: **6/6 CTests pass**.
- OpenMP Debug + UndefinedBehaviorSanitizer, two threads: **6/6 CTests pass** (58.83 seconds).
- OpenMP optimization equivalence test also passes with four threads.
- Added empty/sparse index, cached obstacle-toggle, solver copy/reset and milk/honey/chocolate comparisons against exhaustive searches, alongside existing water/PCA/full/incremental/removal mesh comparisons.
- Separately exported the optimized serial mesh and compared it with the original pre-optimization baseline OBJ: identical face indices, maximum printed position/normal vector difference approximately **1.005e-5**.
- Build-source list parity remains green. No Windows run, hardware GPU benchmark or ThreadSanitizer run is claimed; UBSan does not establish absence of data races.

## Run

```sh
cmake --preset fluid-parallel
cmake --build --preset fluid-parallel
ctest --preset fluid-parallel
OMP_NUM_THREADS=2 build/fluid-parallel-proof/Projects/Project-Fluid/Project-Fluid-Benchmark
```

The test preset sets two threads. Set the environment explicitly when running benchmarks; a large default OpenMP thread count can hurt small workloads.

PowerShell, after configuring/building in an x64 developer terminal:

```powershell
$env:OMP_NUM_THREADS = '2'
.\build\fluid-parallel-proof\Projects\Project-Fluid\Project-Fluid-Benchmark.exe
```

The optional full-app direct build flag is `-FluidOpenMP`; the MSVC OpenMP runtime must be available on the target system. This enables compiled CPU stages; it does not add live water to the Project-Zero snapshot bridge.

## Remaining work before calling the complete plan finished

1. GPU-resident extraction implementation, output parity checks and actual hardware timestamp measurements. This sandbox exposes no hardware GPU/Vulkan runtime; no GPU timing has been fabricated.
2. Further assembly/topology and pressure-stage work guided by the new profiles; scaling/convergence tests at larger workloads. This pass does not promise 60 FPS after rendering costs.
3. Existing extraction-domain clipping and physical-validation limitations from the integration review remain. The same grid was retained for the controlled performance comparison.
4. Then foam/splash generation, lifetime/transport and rendering, validated separately from primary fluid accuracy. Those features are explicitly deferred until the optimization stage is completed.
