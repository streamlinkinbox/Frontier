# Project-Zero: GPU fluid test modes and startup/RAM diagnosis

2026-09-25. **Implemented GPU compute extraction, CPU mirror, PC test entry points and logs. Hardware GPU execution and full Windows build/runtime remain unverified here.** Foam and splash remain deferred.

## Rebuild and run on Windows

From the repository root in a Visual Studio x64 developer terminal, with your existing Vulkan SDK/dependencies:

```powershell
.\Projects\Project-Zero\Build\ToolchainSequence.ps1 -Rebuild -FluidOpenMP
$env:OMP_NUM_THREADS = '2'
```

The build compiles the new extraction and preview shaders and copies them beside the binary. Vulkan **1.2** is required. The optional MSVC OpenMP runtime must be available. Start with two CPU threads; more is not automatically faster for a small simulation.

### A. Interactive simulation preview, inside the Project-Zero executable

```powershell
.\Build\Project-Zero.exe --fluid-preview
```

Controls: **Space** pause, **R** reset, **S** stir, **P** toggle pouring, **1–4** water/milk/honey/chocolate, **Esc** quit.

This is a **separate fluid test window/mode**, entered before the normal scene/editor loads. It advances the existing CPU particle solver/PCA, runs the new GPU mesh extractor, and displays the existing GPU projected-particle water renderer. **The displayed water is not the extracted mesh**, and this does not add live fluid entities/BLAS updates to the main ReSTIR viewport. The distinction is printed at startup.

Logs: `Build/FluidTests/preview-<timestamp>.csv`. Columns include simulation step count, CPU physics/PCA/preparation, GPU-extraction wall time, total update time, GPU timestamp stages, index count and overflow. `update_wall_ms` excludes drawing/presentation and is **not total frame time**. A displayed update can contain zero, one or two physics steps. Initialization cost is logged separately. The preview window is fixed-size; unsupported swapchain capabilities fail explicitly, and full resize/minimize/cross-vendor behavior remains unverified.

### B. Reproducible GPU extraction test and CPU parity check

```powershell
.\Build\Project-Zero.exe --fluid-gpu-test --fluid-steps 120 --fluid-verify --fluid-output Build/FluidTests/gpu-pc 2>&1 | Tee-Object fluid-gpu-console.txt
```

Runs a deterministic basin at 1/60 second per step; no pouring in this benchmark. Writes:
- `performance.csv`: per-step CPU times, GPU stage timestamps, submit/wait, complete step wall time, index count and overflow.
- `summary.txt`: mode, selected device, allocated buffer bytes, setup time, post-warm-up median/p95, final-state parity result or failure.
- `last-frame.obj`: compact exported mesh, rotated from fluid Y-up to Frontier Z-up.

The first five steps are marked as warm-up and excluded from the summary percentiles. GPU setup, final readback, CPU validation and OBJ export are excluded from frame times. With `--fluid-verify`, **the final GPU state** is compared with the CPU mirror: scalar field, triangle edge IDs independent of atomic output order, vertex positions and normals. An error exits nonzero. This is not a claim that every intermediate frame is verified.

The device list is printed to the console. Use `--fluid-device 1` to select a different enumerated GPU. Software devices, if any, are identified by their Vulkan device type; their timings must not be called hardware-GPU benchmarks. Use `--fluid-shader <path>` only if deliberately overriding the built shader location.

Each default run gets a timestamped directory; `--fluid-output` chooses a fixed directory and **overwrites** its named outputs on repeat runs.

### C. Run the same extraction layout entirely on the CPU

```powershell
.\Build\Project-Zero.exe --fluid-cpu-test --fluid-steps 120 --fluid-output Build/FluidTests/cpu-pc 2>&1 | Tee-Object fluid-cpu-console.txt
```

This does not create a Vulkan device; GPU timing columns are `NA`. The Project-Zero executable itself still depends on the normal Vulkan loader/runtime libraries. The dependency-free CPU mirror CTest is also available through `cmake --preset fluid-cpu`, build and CTest.

To inspect the exported mesh in the **normal static scene renderer** afterward:

```powershell
.\Build\Project-Zero.exe --scene Build/FluidTests/gpu-pc/last-frame.obj
```

OBJ does not carry our water material or solver state: this opens geometry, not a live simulation or automatically configured refractive-water material.

## GPU implementation scope

`GpuSurfaceExtractor` borrows a Vulkan device/compute queue and owns reusable device-local field, vertex and index buffers. It executes three compute stages:
1. Anisotropic field evaluation using CPU-prepared brick-to-kernel lists and coefficients.
2. Interpolated vertices/normals in deterministic lattice-edge slots; one writer per edge, no vertex-welding race.
3. Marching-cubes index emission using a bounded atomic counter and explicit overflow reporting. Overflowed output is rejected rather than silently exported.

The triangle winding is outward, consistent with negative field-gradient normals. A CPU regression test caught and corrected the imported lookup table's inward convention for this new extractor.

GPU output stays resident during updates. Only an 8-byte status is read back each update. **The prototype synchronously waits for each extraction submission**: it is not an asynchronous multi-frame renderer integration. Full readback is explicit for final validation/export. Uploads, including a small lookup table, are currently performed each update; further upload/pipeline optimization remains possible.

Timestamp queries use the queue family's valid-bit width and device timestamp period, including wraparound masking. Unsupported timestamps are reported `NA`, not fabricated zero-duration results. Capability checks cover compute workgroup limits, buffer ranges, eight storage-buffer bindings and push constants. Unsupported devices/shader errors fail visibly rather than silently falling back to CPU extraction.

This first GPU extractor emits a **raw indexed marching-cubes mesh with gradient normals**. It does **not** implement the older CPU extractor's Laplacian smoothing/volume-restoration pass. Therefore GPU time versus the earlier CPU mesher is **not an equal-quality speedup comparison**. Use the new CPU layout mirror for stage parity. The GPU/mirror grid is 65×65×45 (Y reaches 4.32 m), rather than the earlier 65×48×45 grid that clipped high fluid. Fixed horizontal bounds are still a limitation. Candidate bounds assume the orthogonal PCA kernels produced by our reconstruction.

**The particle solver and PCA remain CPU-side.** This change ports surface extraction, not the entire physics solver. No GPU performance result is claimed until a PC log is available.

## Verification performed here

- Serial Release CPU suite: **7/7 pass**.
- Parallel Release suite: **7/7 pass**, including the final expanded mirror fixtures.
- Parallel Debug UndefinedBehaviorSanitizer suite: **7/7 pass**, with the expanded mirror subsequently rerun successfully.
- CPU mirror tests: exhaustive single-kernel field comparison, real-fluid sampled exhaustive field comparison, valid edge indexing, unit normals, closed ellipsoid topology, outward winding, capacity overflow, empty field and high-Y coverage.
- All four extraction/preview shaders compiled to SPIR-V for Vulkan 1.2 using locally built Khronos glslang 16.3.0 (`275822a` source).
- New Vulkan C++ translation units, embedded preview, GameExecution and allocation telemetry syntax-checked against real Vulkan/GLFW/dependency headers.
- Build-source parity: 105 CMake Project-Zero translation units included in the 110-entry direct Windows source batch. Shader parity: both build paths lower the same **22 shaders**.
- No actual GPU dispatch, driver validation-layer run, GPU/CPU parity result, or Windows full link/run is claimed.

## Why Project-Zero might use around 4 GB

**The cause on your PC is not identified yet.** Distinguish process working set, process private committed memory and GPU/shared memory in Task Manager; they are different measurements.

Verified code facts:
- `ShadingTableCodec::Bake()` really runs on CPU at each normal startup. Its two 32×32 RGBA32F tables total **32,768 bytes (32 KiB)**. A standalone Release probe here took **285.952 ms**; that is not a timing prediction for your PC. These tables alone cannot explain a 4 GB footprint. [Probe log](FluidEvidence/shading-lut-probe.txt).
- Four ReSTIR reservoir buffers are allocated at **80 bytes per pixel each**: about **281 MiB at 1280×720**, or **2.47 GiB at 3840×2160**, before the other render targets. These are device-local requests; on integrated/shared-memory GPUs they can consume system memory, but on a discrete GPU their size is not automatically process RAM.
- Decoded CPU texture mip chains remain in `TextureIndex`. Upload creates another staging buffer containing all resident mip chains, then GPU images. Those can coexist during upload. As an example, one 2048² RGBA8 full mip chain is about **21.3 MiB**; sixteen are about **341 MiB per copy**. Half-float textures take twice that. The default texture-edge limit is 2048, though your saved configuration can differ.
- Scene vertices, indices, flattened triangles, acceleration data and animation mirrors also coexist. BVH construction and texture decoding run concurrently, so their temporary peaks can overlap.
- The icon atlas contains 151 tiles, with 24–96 px artwork and gutters: about **0.48 MiB at 1×** or **6.1 MiB at 4× per atlas**, before cached tile pixels, SVG parser/rasterizer and GPU copies. At most two atlases are retained during replacement. This does not establish zero SVG cost, but the atlas itself is not a plausible 4 GB allocation.
- Driver pipeline compilation/caching, render targets, fonts and first-frame sky/UI work can contribute. The new logs are designed to distinguish those phases rather than assume the answer.

No texture quality, render buffers or material LUT accuracy were reduced to hide the memory use.

## Capture startup timing and RAM on your PC

Launch the normal application (no fluid mode flag):

```powershell
.\Build\Project-Zero.exe 2>&1 | Tee-Object project-zero-startup-console.txt
```

Every launch writes `Build/Diagnostics/startup-<timestamp>.csv` with:
- elapsed milliseconds since the logger was created at main entry;
- phase duration (`-1` means a milestone/payload row, not a timed phase);
- process RSS/working set and peak working set;
- Windows private committed bytes (`-1` where unavailable);
- selected payload byte counts.

Stages cover built-in scene readiness, scene decoding, texture decoding, BVH construction, Vulkan bring-up, shading LUT bake, scene upload, fonts, Control Centre/UI initialization, frame-loop readiness, return from the first presentation call and after 120 frames. Texture and geometry payloads and animation-mirror capacity are recorded separately. Concurrent worker memory measurements are process-wide, **not exclusive per-worker attribution**; overlapping phase durations must not be added together.

Console `[Allocation]` rows report buffer/image allocation requests and memory flags plus texture staging payload. They are **not a live allocation total** (resources can be released/replaced, and other engine allocation sites are not fully covered).

**How long will it load?** There is no honest fixed prediction without your CPU, GPU/driver, scene, settings and cold/warm-cache measurements. `FirstPresentReturned.since_main_ms` gives the measured startup-to-first-presentation-call time for your run; it is not proof that scanout finished. `After120Frames` distinguishes a temporary startup spike from retained memory. Run twice and compare cold/warm behavior. Please return both startup files plus GPU-test `summary.txt`/`performance.csv` so the next fix targets the actual bottleneck.
