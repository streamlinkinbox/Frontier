# Real-Mesh GI, Solar Shadows, and Deterministic Path Tracing Plan

**Status:** Planning only — no engine algorithms have been implemented in this change.  
**Scope:** Select and validate two real-time GI candidates for GTX-class GPUs, two real-time solar-shadow candidates, and one deterministic triangle-mesh path tracer with GTX and RTX backends.  
**Geometry constraint:** Characters, vehicles, and scene occluders are represented by their actual triangle meshes. No capsule, sphere, box, or other analytic proxy is used as scene geometry.  
**SDF decision:** Do not use voxel/local SDFs as the primary GI or shadow representation for this work. Retain existing SDF code only where already needed for unrelated/offline baking or compatibility; do not expand or make it a dependency of the selected render paths.

---

## 1. Checkout and evidence status

The active checkout used for this plan contains only `README.md` at its base commit (`4a50961275d36e2503355c8176237112cd0ca9aa`). Neither `Exhibits/CleanEmissiveShadowValidation.cpp` nor `Exhibits/CleanShadowAndGiCandidates.h` exists in this working tree. The candidate paths also do not appear in the fetched `SultanAladin/Frontier-1` reference branch (`arena/39c6910b-frontier-1`, commit `d5bbb81`). The active checkout is shallow/grafted, so its local Git history cannot establish that those two files existed in an earlier commit. The numerical results quoted in the prior discussion are therefore treated as **reported results to reproduce**, not verified results from this checkout.

The external reference branch does provide useful foundations:

- `Engine/GeometricRaster/DirectLighting/SolarShadowSequence.{h,cpp}` and `Engine/Shaders/DirectLighting/` contain existing shadow modes and mesh/BVH-facing infrastructure. The header also retains legacy SDF enum/API options; their presence is not evidence that new paths should depend on SDF.
- `Engine/GeometricRaster/GlobalIllumination/DeterministicIntegrators.h` contains deterministic Owen-scrambled Sobol quadrature and GGX VNDF helpers.
- `Experimental/DeterministicPathTracer/` is a substantial deterministic WebGPU/WGSL reference implementation. It is not itself the requested native C++/Slang GTX + RTX engine implementation.
- The reference branch has `Documentation/FolderStructure.md` and the path-tracer audit/optimization plans. The current active checkout does not yet contain those files.

Before selecting a winner, restore/recreate the missing validation harness and run it against identical real-mesh scenes, camera, materials, lighting, and reference settings. Do not hard-code the previously reported RMSE values as acceptance criteria until the measurements are reproduced.

## 2. Decisions requested / proposed

| Area | Proposed finalists | Geometry and visibility | Decision rule |
|---|---|---|---|
| GTX real-time GI | **Stokes-contour emissive GI** and **surfel-cascade GI** | Both source and receiver data come from real emissive/diffuse triangles; mesh/BVH visibility where required. | Keep both only if each has a distinct quality/performance point and passes the same reference comparison. |
| Real-time solar shadows | **Mesh-surfel soft shadow** and **BVH solar-disc ray query** (with a raster PCSS/SMRT mode measured as a third challenger if useful). | Actual static, rigid, and deformed character triangles. BVH contact visibility and/or triangle-derived surfels; no capsule stand-ins or voxel SDF. | Choose the best two from quality, temporal stability, dynamic-mesh update cost, and GPU time on the target GTX. |
| Path tracing | **One deterministic triangle-mesh path tracer**, with a software wide-BVH compute backend for GTX and an optional Vulkan hardware-ray-query backend for RTX. | The same mesh/material/light scene and deterministic sample contract across backends; triangle BVH/BLAS/TLAS traversal. | RTX hardware tracing is a backend acceleration, not a different integrator or a requirement for GTX. |

The two GI candidates are the best-supported starting pair from the earlier report (Stokes contour and surfel cascade were reported nearly tied against a 128-SPP mesh/BVH emissive reference). That result still needs reproduction. The old “hybrid cone + SDF” shadow candidate is **not** carried forward because it contradicts the real-mesh/no-SDF constraint. The prior mesh-surfel result is a candidate, not yet a winner; compare it with deterministic BVH solar-disc rays and the existing raster shadow modes.

## 3. Algorithm contracts

### 3.1 GI candidate A — Stokes-contour emissive mesh GI

1. Extract emissive triangle sources from the scene's real triangle meshes, preserving material emission and transforms.
2. Build deterministic source/patch acceleration structures; integrate the visible first-bounce emissive contribution with the Stokes-contour method.
3. Add the planned diffuse second-bounce representation only after first-bounce correctness is validated. Its patches must be sampled/updated from real diffuse triangles, not analytic proxy geometry.
4. Evaluate mesh visibility using the shared triangle BVH/contact path. Any approximation must be explicit and measurable.
5. Validate energy, orientation, visibility, and moving/deforming-mesh updates against a high-sample deterministic emissive-Nee reference.

### 3.2 GI candidate B — Surfel-cascade GI

1. Generate area-weighted surfels from real emissive and diffuse mesh triangles; retain source triangle/material identity and update data when the source mesh deforms.
2. Accumulate irradiance into the cascade representation with deterministic sampling and explicit normalization.
3. Use the cascade only as a lighting cache. It must not replace triangle geometry for primary visibility or claim exact visibility where it is approximate.
4. Measure cache update cost, memory, leakage, temporal stability, and quality separately from lookup cost.
5. Compare identical outputs and update schedules with candidate A and the same reference renderer.

### 3.3 Solar shadow finalists

- **Mesh-surfel soft shadow:** stratified, area-weighted blocker disks built from actual triangles, with per-frame re-extraction/update for deformed character meshes and a short-range BVH contact test. Surfel radius/normal filtering must avoid light leaks and silhouette popping. It is an approximation; quantify its error against the ray reference.
- **BVH solar-disc ray query:** deterministic low-discrepancy samples over the finite solar disc, queried against the real triangle BVH. This is the correctness/quality candidate and can be expensive at high sample counts; use a fixed low sample budget for GTX and report the quality/runtime curve rather than calling a low-sample result “exact.”
- **Existing raster challenger:** benchmark the reference branch's real-triangle shadow-map modes (Contact PCSS, plane-aware PCSS, SMRT, and bilinear PCF) under the same scene. These are not automatically selected; they provide useful GTX performance and stability baselines. Keep legacy SDF enum values out of the new configuration path.

All visibility paths share the same normal bias policy, solar angular radius, mesh pose, and temporal test. Deforming characters are skinned/deformed triangle meshes, and their acceleration data or shadow representation is updated/refit from those triangles. No bone capsules are inserted into the scene or queried as occluders.

### 3.4 Deterministic path tracer — GTX and RTX

- **Shared integrator:** deterministic low-discrepancy sample sequence, explicit BSDF/light PDFs and MIS, fixed accumulation order, robust triangle hit semantics, and identical scene/material definitions. No stochastic Russian roulette in the determinism validation mode.
- **GTX backend:** compute-based traversal of a software wide BVH (BVH4 initially; assess compressed BVH8 only after correctness and measured benefit). Use wavefront queues only if profiling demonstrates a gain without violating deterministic accumulation.
- **RTX backend:** optional Vulkan ray-query traversal over triangle BLAS/TLAS, with refit/update support for deforming meshes. It uses the same path-integration and sample contract; hardware traversal must not change hit/material semantics.
- **Cross-backend rule:** compare both to a high-sample reference with deterministic inputs. Require exact repeatability within a backend; compare floating-point image tolerances across different GPU architectures rather than promising bit-identical cross-vendor output.
- **Scope boundary:** the existing browser WebGPU/WGSL implementation and reports are design/reference material. Port only after auditing its semantics and licensing/asset dependencies; do not assume that a proposed native `.slang`/Vulkan implementation already exists.

## 4. Proposed folder structure

This follows the Frontier reference repository's domain split. It is a target layout, not a claim that these files already exist in the active checkout.

```text
Frontier/
├── Engine/
│   ├── SpatialAcceleration/
│   │   ├── WideBoundingTree.h/.cpp                 # Existing mesh BVH, or reuse current location
│   │   ├── WideBoundingTreeBuild.h/.cpp             # Optional dedicated builder, only if justified
│   │   └── HardwareRayTracing/                      # RTX BLAS/TLAS adapter, capability-gated
│   ├── GeometricRaster/
│   │   ├── DirectLighting/
│   │   │   └── SolarShadowSequence.h/.cpp           # Register mesh-surfel and BVH-disc modes
│   │   └── GlobalIllumination/
│   │       ├── StokesContourEmissiveGiSequence.h/.cpp
│   │       ├── SurfelCascadeGiSequence.h/.cpp
│   │       └── DeterministicPathTraceSequence.h/.cpp
│   └── Shaders/
│       ├── DirectLighting/
│       │   ├── MeshSurfelShadowCompute.slang
│       │   └── SolarDiscBvhShadowCompute.slang      # If using software BVH compute
│       └── GlobalIllumination/
│           ├── StokesContourEmissiveGiCompute.slang
│           ├── SurfelCascadeGiCompute.slang
│           └── DeterministicPathTraceCompute.slang
├── Exhibits/
│   ├── WorldGiShadowPathTraceValidation.cpp         # Reproducible metrics and assertions
│   └── Images/WorldGiShadowPathTrace/               # Generated images/results; follow repo ignore policy
├── Documentation/
│   └── Plans/WorldGiShadowPathTracerPlan.md          # This plan
└── Experimental/
    └── DeterministicPathTracer/                     # Existing WebGPU reference; do not confuse with engine port
```

Reuse canonical existing files in the real target repository rather than creating duplicate BVH, sequence, or shader infrastructure. Exact class/file names should be aligned to `Documentation/FolderStructure.md`, naming rules, build conventions, and the actual checked-out source before implementation.

## 5. Validation and selection protocol

1. **Recover baseline:** locate the original GI candidate source/validation commit if available; otherwise reconstruct a small self-contained mesh scene and harness. Record source commit and build command in the report.
2. **Scene parity:** same triangle meshes, skinning/deformation pose, materials, emissive flux, solar disc, camera, resolution, bias, and exposure for all variants. Scene assets must be real meshes; no analytic proxy occluders.
3. **References:** use a high-sample deterministic triangle-BVH path/emissive-Nee reference. Report its sample count and convergence error; a 128-SPP reference is a useful baseline, not automatically ground truth.
4. **Metrics:** report linear-HDR RMSE, luminance RMSE, shadow visibility RMSE, SSIM or equivalent, worst-case leak/error, per-frame GPU/CPU time, update/build time, memory, and temporal flicker. Exclude emissive pixels only with a documented, identical mask.
5. **Dynamic validation:** render at least two materially different skinned poses and a rigid vehicle transform. Verify that triangle acceleration data, surfels, and lighting caches are updated or refit; test for stale shadows/GI.
6. **GTX/RTX coverage:** test the GTX software path and RTX ray-query path separately. Treat unsupported hardware as a clean capability fallback, not a startup failure.
7. **Decision:** keep the two best GI methods and two best shadow methods only after measured Pareto comparison. A slightly better RMSE does not win if it causes unacceptable latency, leaks, memory, or dynamic-update cost.
8. **Regression:** determinism/repeatability checks, small-scene CI validation, and saved image/JSON artifacts. Generated images and large datasets remain outside Git unless the repository explicitly tracks them.

## 6. Implementation phases (after plan approval)

- **Phase 0 — source recovery and audit:** identify the intended base repository/commit, restore the missing validation code if it exists, inspect current APIs and build targets, and reconcile the plan with actual naming/build constraints.
- **Phase 1 — shared triangle foundation:** establish/refit real-mesh BVH access for static, rigid, and deformed triangles; lock down hit, material, normal-bias, and deterministic sampling contracts.
- **Phase 2 — validation harness:** create the controlled scene, deterministic reference, image/metric output, and baseline runs before optimizing candidates.
- **Phase 3 — shadows:** implement and compare mesh-surfel and BVH solar-disc candidates; run existing raster modes as challengers; choose the top two using the protocol above.
- **Phase 4 — GTX GI:** implement Stokes-contour and surfel-cascade candidates with real triangle source extraction; measure and choose the top two.
- **Phase 5 — path tracer:** port the audited integrator to the GTX software-BVH compute path, then add the optional RTX Vulkan ray-query backend and cross-backend tests.
- **Phase 6 — integration:** wire selected sequences into the renderer, add capability/fallback settings and documentation, then run release/debug builds and validation.

No Phase 1–6 implementation is included until the plan is reviewed and the actual source base is restored or confirmed.

## 7. SDF answer

**For the selected real-mesh GI and shadow algorithms: no, do not use SDF as the primary solution.** Real triangle geometry plus BVH/raster visibility is the common representation; surfels are derived from triangles and are an explicit approximation. SDF can remain in unrelated offline baking or legacy compatibility code, but it should not be built, sampled, or required by these new paths. This avoids a parallel geometry representation and its memory, rebake/update, and boundary/leak risks. If a future measured result shows a compelling need for SDF, evaluate it as a separate optional candidate with its own mesh-reference comparison—not as an invisible dependency.
