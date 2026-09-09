# 🧩 Phase R3 — tinybvh → CWBVH software acceleration structure (Tier A)

Plan v2.2 phase R3. Authorised by the user with "continue to implement" (2026-09-04) after the material decisions.

## 1. What it does

| Piece | File | Role |
|---|---|---|
| Builder | `Engine/GeometricRaster/TraversalIndex.{h,cpp}` | tinybvh (submodule `ExternalPackages/tinybvh` @ 0e45842, MIT) binned-SAH or SBVH build → 8-wide collapse → CWBVH compression (Ylitie et al. 2017). Emits two float4 blobs: nodes (80 B) and leaf triangles (48 B: e1, e2, v0 + primitive index). Also exposes tinybvh's own CPU traversal for the harness. |
| GPU traversal | `Engine/Shaders/TraversalCWBVH.slang` | 1:1 GLSL port of tinybvh `kernels/traverse_cwbvh.cl` (non-NVIDIA path): `TraverseClosest`, `TraverseOccluded`. Stack 32, octant-inverse ordering, 24-bit node/triangle group split, Möller–Trumbore leaves. |
| Kernel | `Engine/Shaders/ReSTIRViewport.slang` | `TraceScene` / `TraceShadow` now traverse the CWBVH; the O(N) loops and `IntersectTriangle` are removed. Primitive index → `Triangles[]` for normal + material. Estimator untouched. |
| Upload | `SwapchainExchange::UploadTraversal / UploadScene(Scene, Traversal)` | Bindings 8 (nodes) and 9 (leaves). `RecordAndPresent` refuses to run without a resident index — no silent fallback to brute force. |
| Host | `GameExecution.cpp` | Builds after import (SBVH when ≤ 2 M tris), logs the stats line; debug popup shows `rays: CWBVH (Tier A)`. |
| Build | `ToolchainSequence.ps1` (`/arch:AVX2`, tinybvh include + submodule), `CMakeLists.txt` (per-file `-mavx2 -mfma`) | |

Tier B (`rayQueryEXT`) is R5; the capability set from R1 still reports the truth and nothing pretends otherwise.

## 2. Proofs (CPU harness, this sandbox — no Vulkan runtime here; GPU pixels need the user's GTX 1060)

Harness sources: `Scratchpad/TraversalIndexTest.cpp`, `Scratchpad/TraversalShaderPortTest.cpp`.

### 2.1 Build statistics

```
CornellBox  SAH tris=36      nodes=5     nodeBytes=400      leafBytes=1728      SAH=13.33 build=0.9 ms   (59.1 B/tri)
CornellBox  HQ  tris=36      nodes=4     nodeBytes=320      leafBytes=2592      SAH=13.27 build=0.2 ms   (80.9 B/tri)
Sponza      SAH tris=262267  nodes=43407 nodeBytes=3472560  leafBytes=12588816  SAH=75.47 build=270.2 ms (61.2 B/tri)
Sponza      HQ  tris=262267  nodes=47642 nodeBytes=3811360  leafBytes=18883200  SAH=67.50 build=499.4 ms (86.5 B/tri)
```
Sponza fits in 16–22 MB; build is a one-off at load (270–500 ms single-threaded in this VM).

### 2.2 Correctness — tinybvh CWBVH vs brute force (random rays inside the bounds)

```
CornellBox: 4000/4000 agree (3389 hits), max |Δt| = 4.8e-07
Sponza    :  400/400  agree ( 374 hits), max |Δt| = 1.9e-06
```

### 2.3 GLSL port vs tinybvh reference (the shader source executed as C++ with GLSL builtins shimmed)

```
CornellBox: 20000/20000 identical hit/miss, |Δt| ≤ 1e-5 rel (17092 hits)
Sponza    : 20000/20000 identical hit/miss, |Δt| ≤ 1e-5 rel (18654 hits)
```
Distances differ only by float summation order; primitive choice differs on coplanar-duplicate triangles in
Sponza's source mesh (identical t) — expected and harmless.

### 2.4 Syntax checks
All touched translation units compile with `-std=c++20 -Wall -Wextra` (0 errors, 0 warnings after fixing the
pre-existing enum/0u conditional in `RecordComputeCommands`).

## 3. Deviations / notes ⚠️

1. Traversal loop carries a 100 000-iteration guard (GPU hang protection); tinybvh's kernel has none. Cost: one
   counter increment per step.
2. `TraceScene` returns the flat-triangle face normal (as before); barycentrics from the traversal are available in
   `TraversalHit` for R4's interpolated normals/UVs.
3. The flat `Triangles[]` SSBO (48 B/tri) is still uploaded because the kernel reads material + normal from it. R4
   replaces it with the resident VertexRecord/IndexRecord buffers the raster already owns.
4. No GPU timing yet — the `kernel` ms in the F3 popup (R2 timestamps) is the number to compare before/after on the
   GTX 1060: the expected drop on Cornell is small (36 tris), on Sponza it is the difference between seconds and
   milliseconds per frame.

## 4. User-side acceptance (needs hardware)
- Cornell image identical to R2 (same estimator, same lights).
- Sponza with the emissive test light renders shadows at interactive rates; F3 popup `kernel` ms reported.
- Log line `CWBVH: … built in … ms` present in `Diagnostics/*_TelemetryReport.md`.
