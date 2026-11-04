# Virtualized geometry (Nanite-like) — report and port plan

Status: **ported and improved** (2026-11-02, branch `arena/88b72faa-frontier`). P1–P6 are done with bit-exact
parity, I1–I5 are done and measured, and the fast preview exists as `AnalyticIllumination`. See §8 for the outcome.
Sections 1–7 are the report that preceded the port (`650e631`); their "today" describes the branch before it.
Measurements: `Exhibits/VirtualizedGeometry/` and `Exhibits/AnalyticIllumination/`.

## 1. Verdict

| | 🅰 VehicleHMI demo | 🅱 `5ec53579` | 🅲 this branch (C++) |
|---|---|---|---|
| Code | `VirtualizedGeometry` (JS + WGSL) | `ClusterNesting` + `VisibilityCull` | `ScreenSpaceClusterRefinement`, `QuadricTierBuild`, `FacetTierBuild`, `PreviewRaster` |
| Source | SultanAladin `6afb6f0`, `Experimental/VehicleHmiPrototype` | streamlinkinbox `56059b3` | `2d55bd2` sync (from egoistm1inbox `9ce908bc`) |
| What it is | complete pipeline: build → GPU cut and cull → visibility raster → resolve → streaming | sphere tree over full-detail leaves; no simplified levels | CPU port of the demo's build and runtime, with deviations and extras |
| In this branch | only its earlier lossless cluster cull (live) | ✅ byte-identical copy, unused | ✅ exhibit-only, not in the editor |
| Verdict | 🏆 **the reference to port 1:1** | ❌ nothing to take | ⚠️ keep the useful pieces, replace build and runtime |

For this feature `5ec53579` has nothing that this branch lacks: its `ClusterNesting` and `VisibilityCull` are
identical bytes here. The "cluster refinement" already in the branch is 🅲, a partial CPU port that never reached the
viewport.

## 2. What the branch holds today

| Piece | Origin | Used by | Status |
|---|---|---|---|
| `GeometryClusterCull.slang` + `ClusteredGeometry` (`SurfaceProjection.cpp`) | demo's lossless clustering (`GEOMETRY_CLUSTERING.md`), ported with the GI | live viewport | ✅ frustum cull of 128-facet clusters, static sets ≥ 256 facets, one indirect draw per cluster |
| `ClusterNesting`, `VisibilityCull` | `5ec53579` | `Exhibits/GeometryPipeline`; atmosphere headers include it for `SpatialVector` only | 💤 |
| `FacetTierBuild`, `QuadricTierBuild`, `ScreenSpaceClusterRefinement` | `2d55bd2` | `Exhibits/ClusterDiagnostic` | 💤 CPU only |
| `PreviewRaster` (+ `PreviewRaster.slang`) | `2d55bd2` | `Exhibits/ClusterDiagnostic`, Editor/Development builds | 💤 no editor entry point |
| Render settings → Buffer index 10–14 ("Clusters: …") | demo labels | `DisplayRecombination.slang` | ❌ they copy the "Inspection radiance" target, which nothing writes (index 6, the distance-field inspection, reads the same empty target) |

## 3. Feature comparison

| Capability | 🅰 demo | 🅱 `5ec53579` | 🅲 branch C++ |
|---|---|---|---|
| Simplified LOD levels (quadric DAG, locked group borders) | ✅ | ❌ stand-in error, no simplified geometry | ✅ |
| Crack-free cut from per-cluster errors (GPU-ready) | ✅ group-coherent errors: no defect at any of 48 thresholds | — | ❌ double cover at 30 of 48; relies on a serial group walk |
| UV seams | ✅ simplified along the seam, never smeared | — | ⚠️ seam vertices pinned: vessel stalls at 236 triangles (demo 92) |
| Authored creases | ✅ exact at every level (line quadrics, along-curve collapses) | — | ❌ dropped after the leaf tier |
| Accuracy at equal triangle count | ✅ fine levels (terrain 0.06 % vs 1.03 %) · ⚠️ coarse terrain 9.3 % | — | ✅ coarse terrain 2.4 % · ⚠️ fine levels |
| Build speed | ⚠️ JS, main thread: ≈ 0.3–0.7 s per corpus surface | ✅ trivial | ✅ ≈ 20–70 ms (7–14× faster) |
| GPU cut + frustum + normal cone | ✅ WGSL, one thread per cluster | ❌ CPU | ❌ CPU |
| Occlusion | ✅ two-phase Hi-Z from real depth, last refresh + disocclusion retest | ✅ conservative CPU pyramid (unwired) | ⚠️ single-refresh ¼-res CPU tiles sampled at centres: can cull visible clusters |
| Visibility raster + deferred material resolve + two-layer glass | ✅ (resolve not yet presented) | ❌ | ⚠️ preview compute rasterizer in an exhibit |
| Streaming | ✅ 64 KB pages, pinned roots, LRU, GPU slot arena | ❌ | ⚠️ CPU page bookkeeping only |
| Debug views | ✅ 5 inspection modes + magenta fallback + overlay | ❌ | ⚠️ 10 preview modes, exhibit only |
| Tests | ✅ 22 (cut completeness at 6 thresholds, two-phase Hi-Z, glass, pages) | ✅ invariants | ⚠️ threshold checks, no cut-completeness test |
| Not in the demo | — | — | ⚠️ material tiers (Full / Toksvig / Proxy), distance curve, glass boost of a different form |

## 4. Measured comparison (summary of `Exhibits/VirtualizedGeometry`)

Same input for both builders: the demo's corpus with its own evidence options.

| Surface | demo: levels, leaf → coarsest | C++: tiers, leaf → coarsest | Better |
|---|---|---|---|
| Sphere (5120) | 7, → 80 (1 root cluster) | 7, → 92 (1) | 🤝 tie (differences ≤ 0.25 % Hausdorff) |
| Glass sphere (5120) | 5, → 352 (25) | 11, → 370 (4) | 🅰 at 640 triangles (0.56 % vs 0.80 %); both ≈ 20 % at the coarsest |
| Heightfield cliff (8192) | 7, → 254 (11) | 10, → 133 (2) | 🅰 fine (0.06 % vs 1.03 % at 4096) · 🅲 coarse (2.4 % vs 9.3 % at 254) |
| UV vessel (2976) | 6, → 92 (1) | 9, → 236 (2) | 🅰 (0.91 % vs 2.18 % at 374; creases and seams kept) |

Deviation is the two-sided Hausdorff distance to the source as a percentage of its bounding-box diagonal. The
demo's 22 unit tests pass in this sandbox (Node.js 22).

## 5. Can it be improved?

Yes, in two steps. Porting first and improving afterwards keeps every later change measurable against a known
reference, which is the rule that worked for the GI.

### 5.1 Port 1:1 (same method as `IlluminationPort.md`)

| Step | Demo source | C++ / Slang destination | Parity check |
|---|---|---|---|
| P1 🧱 build | `GeometryHierarchy.js` (1312 lines): weld, scored clusters (≤ 128 facets, ≤ 256 vertices), grouping 8 → 32, `SimplifyGroup`, group-coherent `ParentError`/`ParentSphere`, validator | `Frontier/Engine/Geometry/GeometryProcessing/` (replaces the `QuadricTierBuild` simplifier and the `FacetTierBuild` partition) | level counts and errors equal to the demo on the 4 corpus surfaces |
| P2 ⚙️ CPU runtime | `GeometryHierarchyRuntime.js` (835): cut, frustum / cone, Hi-Z, pages, residency, slot allocator | same folder; replaces `ScreenSpaceClusterRefinement`'s cut | the 22 demo tests ported as a CTest |
| P3 🖥️ GPU | `VirtualizedGeometry.js` (606) + `VirtualizedGeometryCull`, `HiZPyramid`, `HiZDownsample`, `VisibilityRaster` WGSL | host in `Rendering/GlobalIllumination/`, `.slang` in `Shaders/GlobalIllumination/`; `SurfaceProjection` runs phase 1 → Hi-Z → phase 2 for eligible static sets, as `?hierarchy=1` does | identical indirect commands and depth on SwiftShader |
| P4 🔍 inspection | `MaterialResolve.wgsl` debug entries, `ClusterDebugFallback.wgsl` | Buffer index 10–14 become real views | captures against the demo |
| P5 💾 streaming | `PageSlotAllocator`, resident bits | GPU slot arena | residency fills root-first |
| P6 🧪 exhibit | `Tools/GeometryHierarchyProof.mjs`, `VisibilityResolve*.mjs` | `Exhibits/VirtualizedGeometry` grows into the parity exhibit | — |

Scope: about 3000 lines of JS and 650 lines of WGSL. Shadows, ray tracing, distance fields and cards keep
full-detail geometry, as in the demo.

### 5.2 Improve after parity

| # | Improvement | Impact | Changes the image? |
|---|---|---|---|
| I1 | ⚡ `vkCmdDrawIndexedIndirectCount` with compacted commands: one draw call per phase instead of one per cluster (the per-cluster loop is a WebGPU limit) | 🟢 CPU submission cost | no |
| I2 | 🧵 build at import, multithreaded per group, stored beside the `.facetspace` | 🟢 no runtime build; the demo builds on the main thread | no |
| I3 | 📐 measured group deviation instead of the additive bound (the demo's cliff root claims 47.8 units on a 40-unit terrain) | 🟢 fewer triangles at equal error | yes, needs approval |
| I4 | 🔺 aspect-ratio term for terrain: 217 of the demo's 254 coarsest cliff triangles are needles | 🟡 coarse terrain (9.3 % at 254 triangles; the C++ build reaches 2.4 %) | yes, needs approval |
| I5 | 🪟 class borders collapse along the border curve instead of locking | 🟡 fixes the ≈ 20 % coarsest glass level in both builders | yes, needs approval |
| I6 | 🌑 shadow rasterization through the same cut | 🟡 shadow cost on dense content | yes |
| I7 | 🧩 `VK_EXT_mesh_shader` or software path for sub-pixel triangles | 🔴 large; only for very dense content | no |

### 5.3 Decisions for the owner

1. **Replace or keep 🅲?** Recommended: replace the build and runtime with the 1:1 port. Keep `PreviewRaster` as an
   Editor/Development tool (it can also host the fast preview below). Move the material tiers and distance curve to
   "after parity, opt-in", because they are not in the demo and change the image.
2. **`ClusterNesting` / `VisibilityCull`:** retire once `SpatialVector` / `BoundingSphere` move to a geometry types
   header. This is low priority.
3. **Order:** P1–P4 make it visible in the editor; P5 matters only when content outgrows memory.

## 6. Expected benefit in Everest today

The proving ground has 20,878 unique triangles in 504 instances. A cluster DAG will not make the current scene
faster, and the demo claims no FPS gain either. The payoff comes with dense content: scanned or sculpted
assets, detailed terrain, or the vehicle when it returns. The lossless cluster frustum cull already runs.

## 7. The "Fast Blinn" render (simple sky, debug)

There is **no switch for it** in this branch, in `5ec53579`, or in the demo. What exists:

| Candidate | Where | Status |
|---|---|---|
| `BlinnRenderer`: CPU Lambert + hemisphere + Blinn-Phong + planar shadow, gradient sky, low resolution | egoistm1inbox `9ce908bc`, `Frontier/Engine/Rendering/BlinnRenderer.{h,cpp}` | only a validation suite calls it; never synced into this line |
| `SceneRender.cpp`: CPU Lambert + hemisphere + Blinn sheen, planar shadow | `5ec53579`, `Exhibits/VehiclePhysics/Code/` | writes exhibit images only |
| `PreviewRaster`: GPU compute rasterizer, gradient sky, GGX (not Blinn), 5 fast shadow modes, 10 view modes | this branch, `GeometricRaster/` | Editor/Development only; run by `Exhibits/ClusterDiagnostic`, no editor entry point |
| Lightest settings of the real renderer | Control Centre → Quick Controls → Render settings | Indirect GI off, Quality Low, GPU path GTX, Buffer index 1 "Direct sun only"; still the full pipeline and the physical sky |

The demo's own renderer choice is RTX (default) or GTX (`?renderer=gtx`); it has no Blinn mode. A real fast preview
is a small, separate task: an Editor/Development-only viewport renderer (Blinn-Phong + hemisphere fill + one
shadow map + gradient sky lit by the level's Sun), selected from Render settings and the viewport menu, with
GI, clouds and fog skipped.

## 8. Outcome (2026-11-02)

The owner asked for the 1:1 port in a `VirtualizedGeometry` folder, the five improvements each measured before and
after, and the fast preview under a technical name, with no controls yet.

| Step | State | Where | Evidence |
|---|---|---|---|
| P1 🧱 build | ✅ bit-exact on 10 surfaces | `Rendering/VirtualizedGeometry/NestingConstruction` | `NestingParityValidation` CTest |
| P2 ⚙️ CPU runtime | ✅ the demo's 22 tests | `NestingSelection`, `VisibilityResolve` | `VirtualizedGeometryValidation` CTest |
| P3 🖥️ GPU | ✅ takeover of eligible static sets as `?hierarchy=1` does | `NestingProjection`, `Shaders/VirtualizedGeometry/` (6 units) | `VirtualizedGeometryCapture` |
| P4 🔍 inspection | ✅ Buffer index 10–14 show real cluster data | `MaterialResolve` entries, `ClusterDebugFallback` | 5 views, each > 8 colours |
| P5 💾 streaming | ✅ every page resident after 4 refreshes | the slot arena in `NestingProjection` | capture |
| P6 🧪 exhibit | ✅ | `Exhibits/VirtualizedGeometry/` | README |
| I1 ⚡ one call per phase and class | ✅ 6452 → 10 indirect calls, 0.631 → 0.003 ms recording per refresh | `NestingProjection`, two `…Compacted` cull entries, `DeviceExchange` | capture, cluster identities identical |
| I2 🧵 build at import | ✅ 1.25 MB `.nestingspace`, loaded in 2.9 ms; the editor never builds | `NestingArchive`, `NestingArchiveExport` | `NestingArchiveValidation` CTest |
| I3 📐 measured deviation | ✅ claimed = measured at the coarsest tier (demo 9–15×) | `NestingConstruction` | `NestingRefinementMeasure` |
| I4 🔺 needle guard | ✅ cliff coarsest 217 → 0 needles, 9.38 → 1.43 % | `NestingConstruction` | same |
| I5 🪟 class borders | ✅ glass sphere coarsest 20.8 → 3.06 %, 25 → 3 clusters | `NestingConstruction` | same |
| Fast preview | ✅ `AnalyticIllumination`, levelspace key `analytic_illumination`, 146× faster than the GI on SwiftShader | `Rendering/AnalyticIllumination/`, `Shaders/AnalyticIllumination/` | `AnalyticIlluminationCapture` |

Decisions taken under §5.3: the port replaces nothing yet. The former C++ tier build (🅲) and `PreviewRaster` remain
for their exhibits, and `ClusterNesting` / `VisibilityCull` remain for `SpatialVector`. The editor loads the refined
archive (I3+I4+I5); `virtualized_geometry` stays off by default, as in the demo.

Open findings, not fixed:

1. The proving ground's set is about 142 disconnected props. Adjacency-only grouping leaves each prop its own cluster
   on every tier, so the refined build ends in tiers of about 142 clusters that barely reduce (194 → 146 triangles over
   the last four tiers). It holds 1613 records against the demo nesting's 650, though it reaches 146 coarsest triangles
   against 1952. With I1 the extra records cost only cull threads. Proximity grouping, or a minimum-yield stop, is the
   next candidate (`Results/NestingSurvey.txt`).
2. The demo's cull binds a single-level Hi-Z, so its 4-corner test is not conservative and the downsample chain is
   unused. Ported 1:1; a mip-selecting test is a candidate fix.
3. SwiftShader offers `multiDrawIndirect` but not `drawIndirectCount`, so the count path ran only through review and
   compilation here. Desktop GPUs take that path, and it needs a check on a real device.

