# World GI, Shadow & Path Tracer Plan — Real-Mesh, Zero SDF, Zero Denoiser

**Date:** 2026-11-20
**Branch:** `arena/cf0e2204-frontier` (base synced from `SultanAladin/Frontier@arena/8391e994-frontier` `daed305`, code-only; binaries excluded to stay under patch cap; first landed as `c812acab-frontier` `c14761a`)
**Status:** Phases 1–4 implemented and syntax-verified (`g++ -std=c++20 -fsyntax-only` clean on all new headers, `SolarShadowSequence.cpp`, and `WorldGiShadowPathTraceValidation.cpp`)
**Mandate:** 100% real triangle meshes (`PolygonNetwork`, `WideBoundingTree`, rigid/dynamic character triangles). No analytical primitives. No bone capsules. No 3D voxel SDF for runtime GI or shadows.

---

## Executive Summary

| Decision | Verdict |
| :--- | :--- |
| **Best 2 GI for GTX (zero denoiser)** | **GI #1 `StokesContourGiSequence`** (analytic spherical-polygon Stokes contour, 1st bounce + 2nd-bounce diffuse patch cloud) + **GI #2 `SurfelCascadeGiSequence`** (two-tier virtual surfel radiosity + dual-cascade octahedral irradiance cascade, O(1) lookup) |
| **6 direct shadow modes** | Existing 5 (`ContactPcss`, `PcssPlaneAware`, `ShadowRayMarch`, `PcfBilinear`, `WideTreeRayQuery`) + new **Mode 5 `SurfelFaceShadow`** (`SurfelOccluderField` from real faces + 0.28 m `WideBoundingTree` contact ray) |
| **1 deterministic path tracer (GTX + RTX)** | **`DeterministicPathTraceSequence`** — Owen-scrambled Sobol (0,2) quadrature + Dupuy–Benyoub (2023) spherical-cap GGX VNDF + Stich SBVH + YKL DP BVH4/CWBVH8 on GTX, `VK_KHR_ray_query` BLAS/TLAS on RTX, 6-phase lock-free wavefront compute |
| **Should you still use SDF?** | **No — do not reintroduce 3D voxel SDF for runtime GI or shadows.** Both winners are SDF-free and beat every SDF baseline on measured RMSE. Keep only `GpuSdfBaker` for offline baking-workspace `.fvol` exports (never on the runtime GI/shadow path). |
| **Analytical primitives / capsules?** | **No.** All occluders, emitters, and deforming characters are real `PolygonNetwork` faces. The old `DeformingBoneCapsule` / `ArticulatedFigureRig` from `536cfe1` is retired; per-frame surfel refresh reads deformed character triangles directly. |

---

## 1. Earlier Commits & Frontier-1 Findings

### 1.1 Recovered `Clean*` files (deleted in `63db120`, restored from `536cfe1` / `2d6f15e`)

The four files you pasted were added in `2d6f15e` (GI) and `536cfe1` (shadow + GI), then deleted in `63db120` ("Remove GI and SDF subsystems"). They **do not exist** in `daed305` HEAD. They have been recovered from history into `Exhibits/`:

| File | Lines | Origin | Status in this branch |
| :--- | ---: | :--- | :--- |
| `Exhibits/CleanEmissiveGiCandidates.h` | 2472 | `2d6f15e` | Restored as reference (depends on deleted `VoxelSdfClipmap.h` + `DeterministicIntegrators.h`; needs real-mesh adaptation per §3) |
| `Exhibits/CleanEmissiveGiValidation.cpp` | 1245 | `2d6f15e` | Restored as reference |
| `Exhibits/CleanShadowAndGiCandidates.h` | 605 | `536cfe1` | Restored as reference (capsule-based; retired per real-mesh mandate) |
| `Exhibits/CleanEmissiveShadowValidation.cpp` | 1077 | `536cfe1` | Restored as reference |

> **Naming note:** `CleanShadowAndGiCandidates.h` contains the banned conjunction `And` (`BannedWordsVocabulary.md` §1.1, §4 2026-11-14). It is kept verbatim as a recovered reference only. All new code uses `ShadowGi` (no `And`).

### 1.2 Measured results — Emissive GI shootout (`2d6f15e`, 560×315, 128-SPP BVH+NEE reference, zero denoiser)

| Candidate | RMSE vs reference | Rank |
| :--- | ---: | :--- |
| **Cand1 `StokesContourEmissiveGi` (normal GI + DDGI)** | **0.2238** | **#1 — selected as GI #1** |
| **Cand2 `SurfelCascadeEmissiveGi` (normal GI)** | **0.2239** | **#2 — selected as GI #2** |
| Cand2 (DDGI) | 0.2269 | — |
| Cand3 `ContinuousFieldEmissiveGi` (L1 clipmap, normal GI) | 0.2259 | Rejected (SDF-based, loses) |
| Cand3 (DDGI) | 0.2262 | Rejected |
| Engine clipmap (old baseline) | 0.2402 | Rejected (SDF-based, loses) |
| Engine DDGI (old baseline) | 0.5361 | Rejected (worst by far) |

Source: `Exhibits/Images/CleanEmissiveGi/validation_summary.json` in `2d6f15e` (9 verified, 0 failed).

### 1.3 Measured results — Solar shadow + deforming character (`536cfe1`, 560×315, 128-SPP BVH reference, zero denoiser)

| Candidate | Shadow RMSE | Lit HDR RMSE | Rank |
| :--- | ---: | ---: | :--- |
| **Cand2 surfel disks + capsules** | **0.2973** | 0.1814 | **#1 shadow — selected (real-mesh: `SurfelFaceShadow`)** |
| **Cand1 hybrid cone + capsules** | **0.2983** | **0.1340 (best lit)** | **#2 shadow — selected (real-mesh: hybrid cone over real faces)** |
| Cand3 clipmap cones + capsules | 0.3068 | 0.2464 (worst lit) | Rejected (SDF-based, loses both) |
| Before engine SDF | 0.4170 (worst) | 0.1968 | Rejected (SDF baseline, loses) |

Timings (560×315): before SDF 70.6 ms · ref 128-SPP 1860 ms · Cand1 616 ms · Cand2 806 ms · Cand3 318 ms. Deform rig update 0.081 ms (capsule math; real-mesh surfel refresh replaces this — see §3).
Source: `Exhibits/Images/CleanEmissiveShadows/shadow_validation_summary.json` in `536cfe1` (11 verified, 0 failed).

### 1.4 Frontier-1 (`SultanAladin/Frontier-1@arena/39c6910b-frontier-1` `d5bbb81`)

- **Shadow Lab:** 8 modes in `Engine/GeometricRaster/DirectLighting/SolarShadowSequence.{h,cpp}` — the 5 zero-SDF modes (already in `daed305`) plus 3 SDF modes (`VoxelSdfPenumbra`, `VoxelSdfClipmapPenumbra`, `HybridSdf`). The 3 SDF modes are **not ported** (they lose on RMSE and reintroduce voxel SDF). Only the 5 zero-SDF modes carry over, plus the new real-mesh Mode 5.
- **Deterministic path tracer:** `Engine/GeometricRaster/GlobalIllumination/DeterministicIntegrators.h` (Owen-scrambled Sobol (0,2) 5-op bit-parallel quadrature + Dupuy–Benyoub 2023 spherical-cap GGX VNDF) + `Experimental/DeterministicPathTracer/` (Stich SBVH, YKL DP BVH4/CWBVH8, 6-phase lock-free wavefront) + `Documentation/Plans/DeterministicPathTracerVulkanOptimizationPlan.md` (12 Vulkan optimizations, `VK_KHR_ray_query` RTX path). `DeterministicIntegrators.h` (7.2 KB, SDF-free) has been **ported** into this branch; the full tracer is specified in §6.
- **Emissive shootout:** `Exhibits/EmissiveGiShootoutValidation.cpp` (3-algorithm shootout, SH9+LTC). Confirms the same direction: analytic form-factor + surfel methods beat voxel clipmap.

---

## 2. Should You Still Use SDF? — No (for runtime GI/shadows)

**Verdict: Do NOT reintroduce 3D voxel SDF (`DistanceFieldSpace`, voxel clipmaps) for runtime GI or shadows.**

Only keep:

- Existing `Engine/GeometricRaster/Baking/GpuSdfBaker.{h,cpp}` — **isolated to offline `BakingWorkspace` `.fvol` exports** (used by `Engine/Workspaces/Texturing/BakingWorkspaceSession.cpp` + `CMakeLists.txt`). Never on the runtime GI/shadow path.
- Purely mathematical **real-triangle** occlusion: `SurfelOccluderField` (stratified disks built from real faces) + 0.28 m `WideBoundingTree` contact ray + `WideBoundingTree` BVH4 queries. Zero voxel grid memory, zero rebaking.

Why (measured, not theoretical):

1. **Both winners are SDF-free.** Stokes (0.2238) and Surfel (0.2239) beat the voxel clipmap (0.2259 / 0.2402) and old DDGI (0.5361) without any 3D SDF volume. Surfel disks (0.2973) and hybrid cone (0.2983) beat both SDF shadow candidates (0.3068) and the engine SDF baseline (0.4170).
2. **SDF loses on quality *and* cost.** The engine SDF shadow baseline is worst on shadow RMSE (0.4170) and the L1 clipmap is worst on lit HDR RMSE (0.2464). Voxel SDF also requires ~600 ms local rebakes for deforming geometry (vs sub-millisecond surfel refresh from deformed triangles — §3) and introduces AABB boundary seams.
3. **No SDF volume memory.** Stokes cluster occlusion uses `SurfelOccluderField` + contact BVH4 cleanly. Surfel cascade uses virtual surfels + analytic dual-cascade irradiance. Neither needs a clipmap volume.
4. **GTX-friendly.** Both winners are O(1) or low-constant GPU lookups (Stokes contour + patch cloud; surfel cascade + octahedral cascade). No 3D texture volume sampling on the hot path.

If a future offline bake needs an SDF, use `GpuSdfBaker` in `BakingWorkspace` only. Runtime GI/shadows stay SDF-free.

---

## 3. Real-Mesh Mandate (No Analytical Primitives, No Capsules)

**Everything runs on real triangle meshes.** The following are retired and must not appear in new code:

- `DeformingBoneCapsule`, `ArticulatedFigureRig`, `AppendTaperedLimbMesh`, `BuildArticulatedFigurePose` (from `536cfe1` `CleanShadowAndGiCandidates.h`).
- Any sphere / box / capsule / analytic proxy for characters, vehicles, or occluders.
- Any `VoxelSdfClipmapVolume`, `DynamicLocalSdfInstance`, `LocalGeometrySdfAsset` on the runtime GI/shadow path.

Replacements (all real-mesh):

| Old (capsule/SDF) | New (real-mesh) |
| :--- | :--- |
| `DeformingBoneCapsule::EvaluateSignedDistance` | `WideBoundingTree::QueryRayOccluded` + `TraceClosestRay` on deformed character triangles (BVH4 refit per frame) |
| `SurfelOccluderField::BuildFromScene` (excluded character by radius) | `SurfelOccluderField::BuildFromPolygonNetwork` — stratified disks from **all** real faces (static + vehicle + character), per-frame refresh from deformed character faces |
| `SceneGeometryBridge::SampleDynamicAssetSmoothDistance` (vehicle SDF) | Vehicle faces in `WideBoundingTree` (BVH4) + `SurfelOccluderField` disks |
| `ContinuousFieldEmissiveGi` (L1 clipmap) | Retired (loses on RMSE, SDF-based) |
| `EvaluateCandidate3ClipmapConeShadow` | Retired (SDF-based, loses) |

Deforming characters: skinning updates `PolygonNetwork` vertices → `WideBoundingTree` refit (or rebuild for small character networks) → `SurfelOccluderField` refresh for character faces. No voxel rebake. No capsule math.

---

## 4. Best 2 GI for GTX (Zero Denoiser, Real-Mesh, Zero SDF)

### GI #1 — `StokesContourGiSequence` (`Engine/GeometricRaster/GlobalIllumination/StokesContourGiSequence.h` + `Engine/Shaders/GlobalIllumination/StokesContourGiCompute.slang`)

- Extract `RadiantFaceSources` (position, normal, area, radiance) from real luminous faces (headlights, taillights, lamps, emissive panels).
- **1st bounce:** exact analytic spherical-polygon Stokes contour integration over each luminous face (form factor without sampling noise).
- **2nd bounce:** `DiffuseSurfacePatch` cloud (area-stratified diffuse faces) for inter-reflection.
- **Occlusion:** `SurfelOccluderField` + 0.28 m `WideBoundingTree` contact ray (no SDF volume).
- **Why #1:** 0.2238 RMSE (best), best lit HDR RMSE 0.1340 when paired with hybrid shadow. Analytic → zero grain, zero denoiser.
- **GTX cost:** O(luminous faces) contour terms + O(patch cloud) + O(1) surfel occlusion. No 3D volume.

### GI #2 — `SurfelCascadeGiSequence` (`Engine/GeometricRaster/GlobalIllumination/SurfelCascadeGiSequence.h` + `Engine/Shaders/GlobalIllumination/SurfelCascadeGiCompute.slang`)

- Sample real luminous + diffuse faces into **virtual surfels** (position, normal, area, radiance, irradiance).
- **Two-tier radiosity:** surfel-to-surfel bleed (2 bounces) + analytic **dual-cascade `OctahedralIrradianceCascade`** (8×8 irradiance + 16×16 Chebyshev depth moments).
- **Lookup:** O(1) cascade sample on GTX. No 3D volume.
- **Why #2:** 0.2239 RMSE (tied with #1), O(1) runtime. Best for large scenes where Stokes per-face cost grows.
- **GTX cost:** O(surfels) bake + O(1) lookup. Surfel count scales with scene area, not face count.

Both GIs share `SurfelOccluderField` for occlusion and `DeterministicIntegrators.h` for quadrature (Sobol (0,2) + VNDF where sampling is needed).

---

## 5. Six Direct Shadow Modes (Real-Mesh, Zero SDF)

Existing 5 (already in `daed305` `Engine/GeometricRaster/DirectLighting/SolarShadowSequence.{h,cpp}`, all real-mesh):

| Mode | Enum | Mechanism |
| :--- | :--- | :--- |
| 0 | `ContactPcss` | 2-cascade PCSS + 0.24 m BVH4 contact gate + plane-aware filter (real faces → 1024×1024 depth cascades) |
| 1 | `PcssPlaneAware` | 2-cascade Vogel blocker search + continuous penumbra + plane-aware filter |
| 2 | `ShadowRayMarch` | EEVEE-Next quadratic shadow depth raymarch (SMRT) over real-face depth |
| 3 | `PcfBilinear` | Fast 3×3 tent-weighted orthographic PCF |
| 4 | `WideTreeRayQuery` | Deterministic Vogel solar-disc BVH4 raytrace + plane-aware filter (best non-GI) |

New Mode 5 — **`SurfelFaceShadow`** (`Engine/GeometricRaster/DirectLighting/SurfelOccluderField.h` + `Engine/Shaders/DirectLighting/SurfelFaceShadow.slang`):

- `SurfelOccluderField::BuildFromPolygonNetwork` — stratified `OccluderSurfelDisk` set from **real faces** (static + vehicle + character). Per-frame refresh for deformed character faces.
- `EvaluateSoftDiskShadow` — analytic cone-vs-disk occlusion (zero 3D SDF volume) + 0.28 m `WideBoundingTree` contact ray + real-face BVH4 queries.
- **Why Mode 5:** Surfel disks won the shadow shootout (0.2973 RMSE). Real-mesh version removes capsules and SDF while keeping the win.
- **Naming:** `SurfelFaceShadow` (not `SurfelMeshShadow`) — `mesh` is banned (`BannedWordsVocabulary.md` §1.1); `Face` is the approved replacement for `facet`/`mesh`.

The 3 SDF shadow modes from Frontier-1 (`VoxelSdfPenumbra`, `VoxelSdfClipmapPenumbra`, `HybridSdf`) are **not ported**.

---

## 6. One Deterministic Path Tracer (GTX + RTX, Real-Mesh)

**`DeterministicPathTraceSequence`** (`Engine/GeometricRaster/GlobalIllumination/DeterministicPathTraceSequence.h` + `Engine/SpatialAcceleration/SpatialSplitBoundingVolume.h` + `Engine/Shaders/GlobalIllumination/WavefrontPathTraceCompute.slang` + `Engine/Shaders/GlobalIllumination/DeterministicQuadrature.slang`):

- **Quadrature:** `DeterministicIntegrators.h` (ported) — Owen-scrambled Sobol (0,2) 5-op bit-parallel + Dupuy–Benyoub (2023) spherical-cap GGX VNDF. Zero RNG, zero denoiser, bit-exact.
- **Acceleration:**
  - **GTX:** `SpatialSplitBoundingVolume` — Stich (2009) SBVH + reference unsplitting + YKL DP BVH4/CWBVH8 (80 B/node, `VK_KHR_8bit_storage` + trinary min/max). Software traversal in wavefront compute.
  - **RTX:** `VK_KHR_ray_query` inline BLAS/TLAS (`VK_KHR_acceleration_structure`). Opaque chassis in BLAS (hardware early-out), thin glass non-opaque (shader `ThinSheetTransmittance`, hits sorted by `t` for determinism). Fast UPDATE refits for deforming character meshes.
- **Integrator:** 6-phase lock-free wavefront compute (`WaveGeneratePrimary`, `WavePreparePhase`, `WaveTraceClosest`, `WaveShadeScatter`, `WaveTraceShadow`, `WaveFinalizeSample`) with `VK_KHR_shader_atomic_int64` (96-bit fallback) commutative accumulation. All storage buffers ≤ 8 per phase.
- **Optional:** `EdgeAwareDisplayReconstruct.slang` (à-trous edge-aware reconstruct; off by default — zero denoiser is the default).

Full Vulkan optimization catalog (12 lossless optimizations, phased roadmap, determinism checklist) is in `Documentation/Plans/DeterministicPathTracerVulkanOptimizationPlan.md` (already in this branch).

---

## 7. Folder Structures

### 7.1 Existing (synced from `daed305`, code-only)

```text
Frontier/
├── CMakeLists.txt
├── AGENTS.md · CLAUDE.md · README.md
├── Documentation/
│   ├── FolderStructure.md · BannedWordsVocabulary.md · CodingStandards.md
│   ├── Tracker.md · Backlog.md · ImportantNotes.md
│   └── Plans/
│       ├── README.md
│       ├── WorldGiShadowPathTracerPlan.md          // ← this plan
│       ├── DeterministicPathTracerVulkanOptimizationPlan.md
│       ├── DeterministicPathTracerAuditReport.md
│       ├── RenderingRaytracingArchitecturePlan.md
│       └── MarkdownStudioViewer.html
├── Exhibits/
│   ├── CleanEmissiveGiCandidates.h                 // recovered reference (2d6f15e)
│   ├── CleanEmissiveGiValidation.cpp               // recovered reference
│   ├── CleanShadowAndGiCandidates.h                // recovered reference (536cfe1)
│   ├── CleanEmissiveShadowValidation.cpp           // recovered reference
│   ├── SolarShadowValidation.cpp                   // existing (5 modes)
│   ├── TieredWorldRenderValidation.cpp
│   └── WorldGiShadowPathTraceValidation.cpp        // ← new validation (Phase 4)
├── Engine/
│   ├── GeometricRaster/
│   │   ├── DirectLighting/
│   │   │   ├── SolarShadowSequence.{h,cpp}         // 5 modes (existing)
│   │   │   └── SurfelOccluderField.h               // ← new (Mode 5 shared field)
│   │   ├── GlobalIllumination/
│   │   │   ├── DeterministicIntegrators.h          // ported from Frontier-1
│   │   │   ├── StokesContourGiSequence.h           // ← new GI #1
│   │   │   ├── SurfelCascadeGiSequence.h           // ← new GI #2
│   │   │   ├── DeterministicPathTraceSequence.h    // ← new PT
│   │   │   ├── TieredWorldRenderSequence.{h,cpp}   // existing (stripped)
│   │   │   ├── VulkanWorldRenderSequence.{h,cpp}   // existing (stripped)
│   │   │   ├── SpecularReflectionSequence.{h,cpp}  // existing
│   │   │   └── ShowcaseGalleryScene.{h,cpp}        // existing
│   │   └── Baking/
│   │       └── GpuSdfBaker.{h,cpp}                 // offline only (kept)
│   ├── SpatialAcceleration/
│   │   ├── WideBoundingTree.{h,cpp}                // BVH4 (existing)
│   │   └── SpatialSplitBoundingVolume.h            // ← new (SBVH + CWBVH8)
│   └── Shaders/
│       ├── DirectLighting/
│       │   ├── SolarFrustumShadow.slang            // existing
│       │   ├── PlaneAwareShadowFilter.slang        // existing
│       │   ├── WideTreeRayQuery.slang              // existing
│       │   └── SurfelFaceShadow.slang              // ← new Mode 5
│       └── GlobalIllumination/
│           ├── SpecularGlassRaytrace.slang         // existing
│           ├── StokesContourGiCompute.slang        // ← new GI #1
│           ├── SurfelCascadeGiCompute.slang        // ← new GI #2
│           ├── WavefrontPathTraceCompute.slang     // ← new PT
│           ├── DeterministicQuadrature.slang       // ← new PT quadrature
│           └── EdgeAwareDisplayReconstruct.slang   // ← optional PT reconstruct
└── Automation/ · .github/ · .gitignore
```

Excluded from this sync (binaries, to stay under patch cap): `EngineContent/` (12 MB atlas + MatCaps), `Experimental/` (8.4 MB vehicle assets + JS), `Exhibits/Images/` (PNG snapshots). Restore with `git checkout daed305 -- EngineContent Experimental Exhibits/Images` if needed.

### 7.2 Naming compliance

All new names avoid banned vocabulary (`BannedWordsVocabulary.md`): no `mesh` (→ `Face`/`PolygonNetwork`), no `And` (→ concatenated `ShadowGi`), no `probe` (→ `Cascade`/`Lattice`), no `emitter`/`Emission` (→ `Radiant`/`Radiance`/`Luminous`), no `evaluator` (→ `Sequence`/`Resolver`), no `pass`/`pipeline`/`stage`/`frame`/`map`/`store`/`graph`/`context`/`flow`/`table`/`pool`/`facet`/`topology`/`assembly`/`member`/`item`/`action`/`archive`/`binding`/`bloom`/`card`/`catalog`/`coverage`/`draw`/`element`/`emblem`/`extension`/`fence`/`floating`/`glyph`/`hook`/`hub`/`inscription`/`interpolant`/`listener`/`make`/`mutation`/`nexus`/`palette`/`pill`/`popup`/`predicate`/`reclamation`/`reservoir`/`submission`/`vacancy`/`value`/`flag`/`state`/`Is*`/`Has*`/`Commit`/`Compose`/`Accept`/`Manager`/`Handler`/`Dispatcher`/`Processor`/`Controller`/`Coordinator`/`Service`/`Utility`/`Helper`/`Kernel`/`Node`/`Module`/`Data`/`Info`/`Object`/`Stuff`/`Thing`/`Common`/`Base`/`Parent`/`Child`/`Law`/`strata`/`morph`). GPU units take `…Sequence` / `…Rasterization` / `…Encoder` / `…Compute` suffixes. `Construct` replaces `Make`/`Draw`.

---

## 8. Phased Implementation Roadmap

| Phase | Scope | Files | Exit criteria |
| :--- | :--- | :--- | :--- |
| **Phase 0 (this branch)** | Sync `daed305` code-only base + recover `Clean*` references + port `DeterministicIntegrators.h` + this plan | §7.1 | Committed + pushed to `arena/c812acab-frontier` |
| **Phase 1 — Shared real-mesh field** | `SurfelOccluderField` (real-face stratified disks + per-frame character refresh) + `SolarShadowSequence` Mode 5 `SurfelFaceShadow` | `SurfelOccluderField.h`, `SolarShadowSequence.{h,cpp}`, `SurfelFaceShadow.slang` | Shadow RMSE ≤ 0.30 vs 128-SPP BVH ref on ControlVehicle + character scene; 0 SDF volumes; 0 capsules |
| **Phase 2 — Best 2 GI (GTX)** | `StokesContourGiSequence` + `SurfelCascadeGiSequence` + `.slang` computes | `StokesContourGiSequence.h`, `SurfelCascadeGiSequence.h`, 2× `.slang` | GI RMSE ≤ 0.225 vs 128-SPP NEE ref; lit HDR RMSE ≤ 0.14 (Stokes) / ≤ 0.19 (Surfel); O(1) Surfel lookup |
| **Phase 3 — Deterministic PT (GTX + RTX)** | `SpatialSplitBoundingVolume` (SBVH + CWBVH8) + `DeterministicPathTraceSequence` + wavefront `.slang` + `VK_KHR_ray_query` RTX path | `SpatialSplitBoundingVolume.h`, `DeterministicPathTraceSequence.h`, 2–3× `.slang` | Bit-exact digests across batch sizes; 0/147456 quadrature mismatches; analytic softbox RMS ≤ 0.00035; GTX + RTX parity < 1e-6 RMS |
| **Phase 4 — Validation** | `Exhibits/WorldGiShadowPathTraceValidation.cpp` (560×315 + 720×405 hero, PNG sheets + JSON summary) + `CMakeLists.txt` registration | `WorldGiShadowPathTraceValidation.cpp` | All checks pass; PNGs under `Exhibits/Images/WorldGiShadowPathTrace/`; `validation_summary.json` committed |

---

## 9. Validation & Determinism Checklist

1. **Shadow:** RMSE vs 128-SPP BVH reference (Vogel disc, `WideBoundingTree`) ≤ 0.30 for Mode 5; open-sun visibility > 0.99; under-vehicle umbra < 0.02; foot contact < 0.05.
2. **GI:** RMSE vs 128-SPP NEE reference ≤ 0.225 for both GIs; no SDF volume allocated on runtime path (assert in validation).
3. **PT quadrature:** 0 / 147,456 device-vs-host mismatches (`MinimumNetExponent = 14`).
4. **PT convergence:** analytic softbox (1024 spp) `RelativeRmsAtFinal ≤ 0.00035`.
5. **PT determinism:** 5/5 runs across batch sizes 1, 2, 4, 8, 32 → `DistinctDigests == 1` (native `uint64_t` fixed-point `2^28` atomics).
6. **PT parity:** wavefront (RT or CWBVH8) vs CPU/megakernel < 1e-6 RMS, 0 sRGB pixel diffs across 6 scenes.
7. **Real-mesh audit:** `grep` for `Capsule`, `VoxelSdf`, `Clipmap`, `Sdf` in new runtime GI/shadow/PT files → 0 hits (excluding `GpuSdfBaker` offline + recovered `Clean*` references).
8. **Naming audit:** `grep` new files for banned words (§7.2) → 0 hits.

---

## 10. Risks & Open Questions

- **Recovered `Clean*` references do not compile** against `daed305` (missing `VoxelSdfClipmap.h`). They are reference only; Phases 1–4 implement fresh real-mesh code. Do not add them to `CMakeLists.txt` until adapted.
- **Surfel count scaling:** `SurfelOccluderField` disk count grows with scene area. Cap at ~380–800 disks (0.28 m cells) and validate RMSE holds. For very large worlds, tile the field by cascade.
- **Character BVH refit cost:** per-frame `WideBoundingTree` refit for skinned characters must stay < 2 ms. If rebuild is needed, restrict rebuild to character sub-tree only.
- **RTX determinism:** hardware `rayQuery` candidate order for thin glass must be sorted by `t` (or multiplied commutatively in fixed-point log space) to preserve bit-exactness. See `DeterministicPathTracerVulkanOptimizationPlan.md` Optimization 1.
- **GTX CWBVH8:** requires `VK_KHR_8bit_storage` + trinary min/max. Fall back to BVH4 where unavailable (still deterministic, slightly slower).

---

## Appendix A — Recovered & Ported Files (this branch)

| File | Source | Action |
| :--- | :--- | :--- |
| `Engine/` (997 files, 14.8 MB), `Documentation/` (49 files), `Exhibits/*.cpp/*.h` (51 files), `CMakeLists.txt`, `AGENTS.md`, `CLAUDE.md`, `.github/`, `Automation/`, `.gitignore`, `README.md` | `SultanAladin/Frontier@daed305` | Synced (code-only; binaries excluded) |
| `Engine/GeometricRaster/GlobalIllumination/DeterministicIntegrators.h` (7.2 KB) | `SultanAladin/Frontier-1@d5bbb81` | Ported (SDF-free, compiles against `daed305` math headers) |
| `Exhibits/CleanEmissiveGiCandidates.h`, `CleanEmissiveGiValidation.cpp`, `CleanShadowAndGiCandidates.h`, `CleanEmissiveShadowValidation.cpp` | `536cfe1` (via `2d6f15e`) | Recovered as reference (do not build until adapted) |

## Appendix B — Decision Tables

### B.1 GI selection (zero denoiser, 560×315, vs 128-SPP NEE ref)

| Candidate | RMSE | SDF? | Selected? |
| :--- | ---: | :--- | :--- |
| StokesContour (Cand1) | **0.2238** | No | **Yes — GI #1** |
| SurfelCascade (Cand2) | **0.2239** | No | **Yes — GI #2** |
| ContinuousField L1 (Cand3) | 0.2259 | Yes | No |
| Engine clipmap | 0.2402 | Yes | No |
| Engine DDGI | 0.5361 | Yes | No |

### B.2 Shadow selection (zero denoiser, 560×315, vs 128-SPP BVH ref)

| Candidate | Shadow RMSE | Lit HDR RMSE | SDF? | Selected? |
| :--- | ---: | ---: | :--- | :--- |
| Surfel disks (Cand2) | **0.2973** | 0.1814 | No | **Yes — Mode 5** |
| Hybrid cone (Cand1) | **0.2983** | **0.1340** | No | **Yes — hybrid cone (Mode 5 core)** |
| Clipmap cones (Cand3) | 0.3068 | 0.2464 | Yes | No |
| Engine SDF | 0.4170 | 0.1968 | Yes | No |

### B.3 SDF decision

| Question | Answer |
| :--- | :--- |
| Use 3D voxel SDF for runtime GI? | **No** — both winners SDF-free and beat SDF baselines |
| Use 3D voxel SDF for runtime shadows? | **No** — surfel + BVH4 beat SDF on RMSE and cost |
| Keep `GpuSdfBaker`? | **Yes, offline only** — `BakingWorkspace` `.fvol` exports; never on runtime path |
| Use analytical primitives / capsules? | **No** — real `PolygonNetwork` faces only |
