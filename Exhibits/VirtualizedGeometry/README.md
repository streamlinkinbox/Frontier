# Virtualized geometry — the demo's port, its parity and five improvements

`Frontier/Engine/Rendering/VirtualizedGeometry/` is a statement-for-statement C++ and Slang port of the VehicleHMI
demo's virtualized geometry (SultanAladin `6afb6f0`, `Experimental/VehicleHmiPrototype`). Like the GI port, it keeps
JavaScript doubles as doubles and renames only for the naming skill. Improvements I1–I5 come after parity, each an
option measured against the ported original. This exhibit holds the parity, the tests, the before/after measurements
and the GPU captures. The earlier report evidence (the demo's builder against the branch's former C++ tier build)
closes this file. The report and plan live in `Documentation/Plans/Ongoing/VirtualizedGeometry.md`.

## What is ported

| Demo source | Port | Evidence |
|---|---|---|
| `GeometryHierarchy.js` (1312 lines): weld, clusters, groups, `SimplifyGroup`, validator | `NestingConstruction.{h,cpp}`, `NestingNumerics.h` (V8's `Math.hypot`, `Math.min`/`max` rules) | `NestingParityValidation`: 10 surfaces bit-exact |
| `GeometryHierarchyRuntime.js` (835): cut, cull, pages, residency | `NestingSelection.{h,cpp}` | `VirtualizedGeometryValidation`: the demo's 22 tests |
| `VisibilityResolve.js` (268) | `VisibilityResolve.{h,cpp}` | same CTest |
| `VirtualizedGeometry.js` (606) and 6 WGSL units | `NestingProjection.{h,cpp}` and 6 Slang units in `Shaders/VirtualizedGeometry/` (20 entries; 2 are I1's) | `VirtualizedGeometryCapture` |

`NestingProjection` takes over eligible static facet sets in `SurfaceProjection` exactly where the demo's
`?hierarchy=1` does. It runs phase-1 cut and cull, the Hi-Z seed, phase 2, the visibility raster and the material
resolve. Buffer index 10–14 now show real cluster data. The levelspace keys are `virtualized_geometry` (off by default,
as in the demo) and `virtualized_stream_slots`. There are no controls yet (owner's scope).

## Tools

| Tool | Shows | Needs |
|---|---|---|
| `Code/NestingParityExport.mjs` | the demo builder's digests per section (`Results/NestingParity.txt`) and its input soups (`Surfaces/*.soup`, zlib) | Node.js 22, the demo checkout |
| `NestingParityValidation` (CTest) | the C++ build of every soup equals the digests: summary, clusters, every tier, CPU cuts at 8 distances | — |
| `VirtualizedGeometryValidation` (CTest) | the demo's 22 tests, assertion for assertion | — |
| `NestingRefinementMeasure` | I2–I5 per corpus surface: tiers, needles, claimed vs measured error, cut sizes (`Results/NestingRefinement.txt`) | — |
| `NestingArchiveExport` | I2's import step: stores `<scene>.nestingspace`; `--verify` is the `NestingArchiveValidation` CTest; `--survey` measures every profile on the scene (`Results/NestingSurvey.txt`) | — |
| `VirtualizedGeometryCapture` | the takeover in the editor's own scene viewport, offscreen, I1 before/after, the five inspection views (`Results/VirtualizedGeometryCapture.txt`) | a Vulkan device |

## Run

```sh
cmake --build <build> --target NestingParityValidation VirtualizedGeometryValidation NestingRefinementMeasure \
    NestingArchiveExport VirtualizedGeometryCapture
ctest --test-dir <build> -R "NestingParity|VirtualizedGeometryValidation|NestingArchive"
<build>/NestingRefinementMeasure > Exhibits/VirtualizedGeometry/Results/NestingRefinement.txt
<build>/NestingArchiveExport EngineContent/EnvironmentArchives/ProvingGround/ProvingGround.facetspace
<build>/NestingArchiveExport --survey EngineContent/EnvironmentArchives/ProvingGround/ProvingGround.facetspace
<build>/VirtualizedGeometryCapture <build>/ShaderArchives/GlobalIllumination <build>/ShaderArchives/Environment \
    EngineContent/EnvironmentArchives/ProvingGround EngineContent/EnvironmentArchives/CelestialMaps <out> low 480 270 6
```

The captures ran on SwiftShader (`FRONTIER_VULKAN_DRIVER` and `VK_ICD_FILENAMES` pointing at
`ExternalPackages/VulkanCompute/SwiftShader`). Regenerating the digests needs the demo files, fetched with
`git fetch --filter=blob:none --depth 1 https://github.com/SultanAladin/Frontier.git <commit>` for commit
`6afb6f00b5f25c45f7cda92c7da65b64845768f3`. Then run
`node Exhibits/VirtualizedGeometry/Code/NestingParityExport.mjs <demo folder> Exhibits/VirtualizedGeometry`. All
results were produced on 2026-11-02 (2-CPU Linux sandbox, GCC 12.2 Release, Node.js 22.22).

## Results

### Parity

| Check | Result |
|---|---|
| Builder, 10 surfaces (spheres, glass spheres, cliffs, creased vessels, a tetrahedron) | ✅ bit-exact in every section (a fresh export reproduced the stored digests); the 8192-facet cliff builds in 84 ms against 1101 ms in Node, the 5120-facet sphere in 64 ms against 910 ms |
| The demo's 22 tests (builder, runtime, visibility resolve) | ✅ 22 / 22 |
| GPU takeover with the demo's nesting (`Results/VirtualizedGeometryCaptureDemoNesting.txt`) | ✅ 650 records, every page resident after 4 refreshes, mean difference 0.013 against the lossless clusters, all 5 inspection views real |

### Improvements, each against the port

| # | Improvement | Before | After |
|---|---|---|---|
| I1 | ⚡ one call per phase and class: `vkCmdDrawIndexedIndirectCount` over compacted commands with `drawIndirectCount`, else one `vkCmdDrawIndexedIndirect` multi-draw over the records | 6452 indirect calls and 0.631 ms recording them per refresh | **10 calls, 0.003 ms**; 0 of 129,600 cluster identity pixels differ |
| I2 | 🧵 build at import on every hardware thread, stored beside the `.facetspace` | built in the viewport at load | 1613 clusters in ~200 ms at import, 1.25 MB archive, **loaded in 2.9 ms**; threads 1.2–1.4× on 2 cores, bit-identical |
| I3 | 📐 measured group deviation (two-sided, monotone) instead of the additive bound | coarsest error claimed 9–15× the measured one | claimed = measured at the coarsest tier; 1 px cut at 96 m: sphere 1280 → 78, cliff 8192 → 3582 triangles |
| I4 | 🔺 aspect-ratio guard against needles | cliff coarsest 254 triangles, 217 needles, 9.38 % | 127 triangles, 0 needles, 1.43 % (refined) |
| I5 | 🪟 class borders collapse along the border curve | glass sphere coarsest 352 triangles in 25 clusters, 20.8 % | 78 triangles in 3 clusters, 3.06 % (refined) |

I1 needs `multiDrawIndirect` (SwiftShader has it, but not `drawIndirectCount`) and falls back to the demo's loop on
devices that offer neither. Both cull entry pairs are built, and `CompactCommands` is read at every refresh, so one
capture compares both on the same resident pages. The gain is CPU recording time. An earlier run gave 0.730 → 0.004
ms, and SwiftShader records commands cheaply, so a desktop driver gains more in absolute terms. The refresh times
(8.914 → 8.763 s) differ by less than SwiftShader's run-to-run noise. Equality is checked on the cluster identity view
without jitter, because the lit image is temporal and stochastic. Its lit images differ by 0.045, against 0.029 for I1
compared with itself two refreshes later, because the jitter-free identity runs restart the temporal history. I3 costs 5–8× the demo's build time (350–960 ms serial per corpus surface), which I2 moves to
import. Per-tier tables for every surface and profile: `Results/NestingRefinement.txt`.

### The refined archive in the editor

| | Demo nesting | Refined nesting (stored archive) |
|---|---|---|
| Proving ground set 3, 3664 facets | 4 tiers, 650 clusters, coarsest 1952 triangles | 11 tiers, 1613 clusters, **coarsest 146 triangles** |
| Takeover against the lossless clusters | mean difference 0.013; refresh 8.675 s vs 9.753 s | mean difference 0.017; refresh 8.763 s vs 10.018 s |

Finding (`Results/NestingSurvey.txt`): the proving ground's set is about 142 disconnected props. The demo groups
clusters by adjacency only, so each prop stays its own cluster on every tier. With the refinements the build keeps
finding small collapses: its last tiers go 194 → 167 → 153 → 150 → 146 triangles, about 142 clusters each, and these
tiers are why there are 2.5× more records. With I1 the extra records cost one GPU thread each in the cull and no draw
calls. The demo stops only when a tier reaches 128 triangles or no group simplifies. Grouping nearby disconnected
clusters by proximity is the next improvement candidate; a minimum-yield stop would also work.

### Cull finding kept for parity

The demo's cull binds a single-level Hi-Z (mip 0, or the 1×1 dummy before the first seed). Its 4-corner test is
therefore not conservative, and the downsample chain is built but unused. The port keeps this 1:1; a mip-selecting
test is a fix candidate.

## Earlier report evidence: the demo's builder against the branch's former C++ tier build

This section predates the port. It measures the demo's `Source/GeometryHierarchy.js` under Node.js against the C++
`FacetTierBuild` + `QuadricTierBuild` through `ScreenSpaceClusterRefinement::BuildFromFacetField`, inherited with the
`2d55bd2` sync. The input is the demo's corpus (`Tools/GeometryHierarchyCorpus.mjs`) with its evidence tool's options:
a 5120-facet sphere, the same sphere with a transmissive band (classification 9), an 8192-facet heightfield with a
cliff, and a 2976-facet vessel with a UV seam, two UV islands and three authored sharp rings. Nothing renders in it.

### Run

```sh
git archive 6afb6f00b5f25c45f7cda92c7da65b64845768f3 \
    Experimental/VehicleHmiPrototype/Source/GeometryHierarchy.js \
    Experimental/VehicleHmiPrototype/Tools/GeometryHierarchyCorpus.mjs \
    Experimental/VehicleHmiPrototype/package.json | tar -x -C <scratch>
node Exhibits/VirtualizedGeometry/Code/CorpusExport.mjs <scratch>/Experimental/VehicleHmiPrototype <corpus>
cmake -S Exhibits/VirtualizedGeometry -B <build> -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build <build>
<build>/TierComparison <corpus> marked > <corpus>/TierComparisonMarked.json
<build>/TierComparison <corpus> crease > <corpus>/TierComparisonCrease.json
node Exhibits/VirtualizedGeometry/Code/SurfaceDeviation.mjs <corpus>
```

The upstream commit must be fetched first (`git fetch --filter=blob:none --depth 1
https://github.com/SultanAladin/Frontier.git 6afb6f00b5f25c45f7cda92c7da65b64845768f3`). `marked` keeps only the
authored sharp edges, which is the demo's input meaning; `crease` is the C++ default (45° dihedral re-detection on
every tier). The `Results/` files were produced on 2026-11-02 (Node.js 22.22, GCC 12.2 `-O2`, 2-CPU Linux sandbox).

### Measures

| Measure | Definition |
|---|---|
| Levels | DAG levels (demo) or tiers (C++), leaf first; triangles and clusters per level |
| Per-cluster cut | each cluster decided alone, selected iff its error ≤ τ < its enclosing group's error, at 12 thresholds τ from 10⁻³ to 10^0.2 of the largest error. **Gaps** = leaf clusters nothing covers (holes); **Overlaps** = selected clusters enclosing another selected cluster (double cover). This is the demo's GPU cut rule |
| Sharp edges / UV seams | undirected edges carrying a sharp bit / manifold edges whose two corner UVs disagree |
| Off-crease | sharp-edge endpoints no longer on a source crease plane (vessel only) |
| Seam crossing | triangles whose U span exceeds 0.5, i.e. smeared across the vessel's meridian seam |
| Needles | triangles with longest² / (2·area) > 20 |
| Deviation | two-sided distance between a level and the source surface, exhaustive point-to-triangle, as % of the source bounding-box diagonal: Hausdorff maximum / root-mean-square |

### Results

**Build, reduction and cut** (`Results/DemoNesting.json`, `Results/TierComparisonMarked.json`,
`Results/TierComparisonCrease.json`):

| Surface | Builder | Build | Levels | Leaf → coarsest (root clusters) | Per-cluster cut: thresholds with defects, worst overlap |
|---|---|---|---|---|---|
| Sphere | demo | 531 ms | 7 | 5120 → 80 (1) | 0 / 12 |
| | C++ marked | 37 ms | 7 | 5120 → 92 (1) | 10 / 12, 27 clusters |
| Glass sphere | demo | 400 ms | 5 | 5120 → 352 (25) | 0 / 12 |
| | C++ marked | 45 ms | 11 | 5120 → 370 (4) | 4 / 12, 22 clusters |
| Heightfield cliff | demo | 712 ms | 7 | 8192 → 254 (11) | 0 / 12 |
| | C++ marked | 59 ms | 10 | 8192 → 133 (2) | 10 / 12, 40 clusters |
| UV vessel | demo | 277 ms | 6 | 2976 → 92 (1) | 0 / 12 |
| | C++ marked | 22 ms | 9 | 2976 → 236 (2) | 6 / 12, 16 clusters |
| | C++ crease | 37 ms | 24 | 2976 → 302 (9) | 4 / 12, 11 clusters |

Build times are the median of three runs and vary by about 15 % between runs on this sandbox. No cut had gaps. Every
build passed its own builder's validator (`ValidateGeometryHierarchy`, `ValidateTierSequence`).

**Features on the UV vessel** (per level, leaf first):

| Builder | Sharp edges | UV seam edges | Off-crease | Seam crossing |
|---|---|---|---|---|
| demo | 144 → 144 → 110 → 74 → 52 → 32 | 116 → 115 → 74 → 54 → 36 → 23 | 0 | 0 |
| C++ marked | 144 → 0 on every coarser tier | 116 on every tier (seam vertices pinned) | — | 0 |
| C++ crease | 240 at the leaf, 147 at the coarsest (re-detected by angle) | 116 on every tier | — | 0 |

`ClassifyEdges` in `QuadricTierBuild.cpp` clears each tier's sharp mask and rebuilds it from open borders and,
optionally, the dihedral angle, so authored sharp edges do not reach coarser tiers.

**Deviation at matched triangle counts** (`Results/SurfaceDeviation.json`; C++ interpolated log-linearly between its
neighbouring tiers; Hausdorff / RMS, % of the diagonal):

| Surface | Triangles | demo | C++ marked | C++ crease |
|---|---|---|---|---|
| Sphere | 1280 | 0.47 / 0.077 | 0.24 / 0.067 | 0.24 / 0.067 |
| | 160 | 0.99 / 0.355 | 1.10 / 0.407 | 1.16 / 0.413 |
| Glass sphere | 640 | 0.56 / 0.180 | 0.80 / 0.194 | 0.80 / 0.194 |
| | coarsest | 20.80 / 9.969 (352) | 19.64 / 1.856 (370) | 17.34 / 1.795 (382) |
| Heightfield cliff | 4096 | **0.06** / 0.013 | 1.03 / 0.043 | 0.88 / 0.036 |
| | 1022 | **0.53** / 0.065 | 1.24 / 0.084 | 2.10 / 0.147 |
| | 254 | 9.34 / 3.337 | **2.36** / 0.356 | 6.86 / 1.418 |
| UV vessel | 748 | **0.45** / 0.114 | 0.90 / 0.109 | 0.75 / 0.086 |
| | 374 | **0.91** / 0.199 | 2.18 / 0.326 | 3.68 / 0.636 |
| | 92 | 2.72 / 0.864 | not reached | not reached |

### Reading the results

- The demo's per-cluster errors are group-coherent: every cluster of a simplification group carries the same enclosing
  error and sphere, so a per-cluster GPU cut is crack-free at every threshold. The C++ errors are not (a cluster's
  coarser error comes from whichever enclosing cluster was visited); its CPU cut compensates by walking groups
  serially, which does not map onto a parallel compute dispatch.
- The demo simplifies along UV seams and creases and keeps them exact; the C++ build pins seam vertices (the vessel
  stalls at 236–302 triangles) and drops authored creases after the leaf tier.
- The C++ build is 7–14× faster because it is compiled; the demo's builder runs single-threaded in JavaScript.
- The demo's coarse terrain levels are poor: 217 of its 254 coarsest triangles are needles and the deviation reaches
  9.3 %. The C++ build is better there. Both builders lock class borders, which ruins the glass sphere's coarsest level.
