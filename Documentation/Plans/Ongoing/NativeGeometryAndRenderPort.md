# Frontier — geometry build and render engine, native port plan

Status: **Ongoing**. Division 1 step K1 is implemented and proven (see *Evidence*). Divisions 2–3 are sequenced,
not started.

This plan replaces the earlier `VirtualizedGeometryCppPort.md` (branch `arena/e9bd7db5-frontier`, commit `c4ef131`).
That document chose a sound order — geometry before renderer — but its first step did not reproduce the reference
behaviour, and it tracked no way to notice. Section 4 records what was wrong with it, with numbers.

---

## 1. What actually exists, and where

Everything below was read from the repositories, not from earlier summaries.

| Line | Head | Holds | State |
|---|---|---|---|
| `SultanAladin/Frontier` @ `arena/db2f9953-frontier` | `44df519` (2026-10-21 22:53) | The native C++ application: `Engine/Interface`, `Engine/Render`, `Engine/ContentInterchange`, `UI/`, `Executables/Validation` | **The build line.** Windows CI green; artifacts `frontier-windows-*` |
| same, `master` | `4a77bfd` | Diagnostics-only uploads | Stale relative to the branch |
| `SultanAladin/Frontier` @ `arena/e9bd7db5-frontier` | `c4ef131` (2026-10-21 22:22) | HTML editor import (`Experimental/VehicleHmiPrototype`, 473 files) + the K1a geometry step | Reference material; separate line, 13 commits off the shared base |
| `streamlinkinbox/Frontier` (this checkout) | `68f3016` | A 158 KB patch holding the HTML lobby/wallet pages | Not the engine |

The merge base of the two SultanAladin lines is `e016a0b`. They are parallel, not stacked: the native engine line does
not contain the HTML prototype, and the HTML line does not contain the engine.

### 1.1 The HTML reference is two bodies of code

| Reference | Path | Pins down |
|---|---|---|
| Virtualized geometry | `Experimental/VehicleHmiPrototype/Source/GeometryHierarchy.js` (68 KB), `GeometryHierarchyRuntime.js`, `VisibilityResolve.js` | Weld, adjacency, scored clusters, QEM tiers, runtime cut, page residency, Hi-Z, visibility-resolve, two-layer glass |
| Geometry evidence | `Experimental/VehicleHmiPrototype/Evidence/GeometryHierarchy/stats.json` | Per-level cluster/triangle/vertex counts, error bounds, cut counts at 64/8/1 px |
| ReSTIR | `Experimental/SlateReference/Shared/References/ReSTIR.html` (492 KB), `ResTir-2.0.html`, `RestirGIRealtimeArchitecturePlan.md` | Reservoir framework, capability tiers, frame-graph shape, denoiser inputs |
| Renderer exhibits | `Experimental/SlateReference/A/Exhibits/VehicleProvingGrounds`, `B/Exhibits/IlluminationHost` | Working WGSL renderer, clustered lighting, proving-ground scene |

### 1.2 What the native application is today

`README.md` and `CLAUDE.md` on `db2f9953` are explicit, and the source agrees:

- The root CMake target builds "only the selected UI foundations and Windows UI exhibit, **not the old engine**".
- `CLAUDE.md`: "Keep the scene renderer excluded from the current selective UI port."
- `Engine/Render/` therefore holds material/preview rendering (`SurfaceRasterization`, `MaterialPreview*`,
  `BrowserPreviewRendering`, `AutomotivePaint.slang`, `PreviewPbrSurface.slang`) — a material authoring renderer,
  not a scene renderer. There is no BVH, no cluster build, no GI, no ReSTIR in the native line
  (`git grep -i restir` finds documentation only; the single source hit is an SVG clip identifier, and `Engine/` has no
  BVH, ray-query or cluster-build reference at all).

So the previous plan's framing was right: the geometry build and the scene renderer must be written, not ported in
from a C++ predecessor. What is missing is not "a port of a C++ engine"; it is **the first two native engine
divisions, with the HTML implementation as the behavioural specification**.

### 1.3 What is reused as-is (do not rebuild)

| Existing unit | What the new work takes from it |
|---|---|
| `Engine/Interface/VulkanPresentation.{h,cpp}` | Instance/device/swapchain/present, `QueryRasterAccess()` for offscreen targets, `ConfigureRendering(command hook, confirm hook)` |
| `Engine/Interface/WindowsApplication.{h,cpp}` | Window + input, the callback order (`Initialize`, `InitializeVulkan`, `ConstructInterface`, `AfterRender`, `Finalize`) |
| `Engine/Render/BrowserPreviewRendering.{h,cpp}` | The precedent for an offscreen render unit: request → construct commands → confirm submission → query result image |
| `Automation/SurfaceRasterization.cmake` + `Automation/ShaderPlan.ps1` | `.slang` → SPIR-V compile and staging, so new shaders enter the same manifest |
| `Executables/Validation/*/Build.bat` + `Binaries/Validation` | The CPU gate convention this plan's K1 validation follows |
| `Engine/Diagnostics/DiagnosticJournal.h` | Frame instrumentation (`BrowserTileReady`, `BrowserPreviewRefused`, …) |
| `EngineDocs/AgenticInstuctions/SKILL-Naming.md` | Identifier rules; every name below is already conformant |

### 1.4 Material authoring is the bridge, not a separate product

The user's target is material authoring with a real renderer behind it. The native application already evaluates
materials (`Engine/Render/SurfaceMaterial.cpp`, `Shader/MaterialSurface.slang`, `AutomotivePaint.slang`,
`MaterialProceduralBaking.cpp`). Division 2 must shade its scene **through those same units** rather than growing a
second material path — otherwise the authoring preview and the game render will disagree, which is the defect class
this plan exists to prevent.

---

## 2. The blocker: the Project Browser crash

Facts, from the repository and CI:

| Item | Value |
|---|---|
| Build the user has | artifact `39682561975` = run `39682561975` = commit **`06aa448`** (2026-10-21 21:54) |
| Newest build on that branch | artifact **`39689691437`** = commit **`44df519`** (2026-10-21 22:53), CI `success` |
| Commit between them | `061301e` (22:28) |
| Crash the newest commit fixes | "Hoist browser thumbnail closures to main scope; fix frame-2 use-after-return crash" |

`44df519` is a real defect and a real fix: `Describe` and `NoteFailure` were declared inside the
`Callbacks.InitializeVulkan` block, while `Browser->QueryThumbnailImage` / `QueryLiveImage` /
`QueryImportedImage` / `CaptureLiveThumbnail` stored `[&]` closures that capture them by reference. Those closures
outlive the block and are first touched on frame 2 by the tile resolve, so the captured references address dead
stack. The fix hoists both lambdas to `main` scope, where the closures' referents outlive the closures.

**Action 1 — use the newer build.** `frontier-windows-39689691437` (built from `44df519`) already contains this fix,
plus the frame-2 trace journal. The build in hand predates it by two commits.

**Action 2 — if it still dies, the log names the stage.** `DiagnosticJournal` writes
`%LOCALAPPDATA%\Frontier\Logs\Frontier-<epoch>.log`, keeps the eight newest, and already records `BrowserTileReady`,
`BrowserTileDescribed`, `BrowserPreviewRefused`, plus the present/acquire traces added on `061301e`. The last record
before the death is the answer. Fetch that file; it contains paths and stage names, no material contents.

**Why this sandbox cannot hunt it directly.** No GPU, no Vulkan, no Windows: present/acquire `DEVICE_LOST` cannot be
reproduced here. Blind "fixes" to a device-loss window are guesses. The honest split is: the user supplies the
journal from a crashing run; the fix is written against the recorded stage.

**Unrelated but real, found while reading the crash fix:** `PreviewDescriptors` is a `std::map<VkImageView,
VkDescriptorSet>` keyed on raw image views, and `ImGui_ImplVulkan_RemoveTexture` is called for every entry in
`Finalize`. If `BrowserPreviewRendering` ever destroys or recycles a preview image while frames still reference the
map, the map keeps a stale key and the descriptor is never removed. Pooled extents currently keep images alive until
`Finalize`, which is why this is latent rather than wrong. Treat it as a defect to close when R1 introduces a second
producer of preview images.

---

## 3. The port, in three divisions

Ordered so that every division ends with something the user can see, and so that nothing in Division 2 depends on an
unproven Division 1 result.

```
Division 1  geometry build (CPU)          K1 ──▶ K1b ──▶ K2 ──▶ K3        provable headless
Division 2  render engine (Vulkan/Slang)  R0 ──▶ R1 ──▶ R2 ──▶ R3          needs the Windows GPU
Division 3  ReSTIR GI                     S1 ──▶ S2 ──▶ S3                 needs Division 2
```

### Division 1 — geometry build, `Engine/Geometry/` (CPU, C++20, no device)

| Step | Deliverable | Acceptance (headless) | State |
|---|---|---|---|
| **K1** | `FacetClusterBuild`: exact-bit weld → `PolygonComplex`; edge incidence with marked sharp, UV seam, dihedral crease, open boundary; scored cluster partition under facet and welded-vertex caps; enclosure sphere/box and normal cone; `ValidateClusterBuild` | Reference corpora reproduce the HTML numbers **and** per-cluster membership: sphere 5120 facets → 2562 vertices → 48 clusters; heightfield 18432 → 9409 → 159; all invariants pass | **Done** |
| **K1b** | `QuadricDeviation` + `EdgeContraction`; simplification with locks on group borders, open borders, UV seams and creases; groups of ≤8 clusters; additive monotonic `Deviation`; enclosure = union of enclosed | Sphere level progressions 5120→2558→1280→640→320→160→80 with 48→39→20→5→3→2→1 clusters; vessel sharp vertices stay on the crease rings; shared border positions bit-identical across tiers | Next |
| **K2** | `TierCutSelection` (projected-deviation antichain + frustum and cone rejection), `ClusterPage` packing + `PageVacancyTable` residency (LRU, page 0 pinned), CPU Hi-Z pyramid, two-phase occlusion | Cut cluster/triangle counts reproduce `stats.json` at 64/8/1 px; a cold-residency cut selects only resident clusters and queues the rest | Pending |
| **K3** | CPU visibility field, deferred resolve, two-layer transmissive gather (glass never depth-writes) | Images in `Exhibits/`; glass pixel statistics match the reference's `VisibilityResolve` counts | Pending |

K1 is in this branch as `Engine/Geometry/FacetClusterBuild.{h,cpp}` with the gate
`Executables/Validation/FacetClusterValidation/`.

### Division 2 — render engine, `Engine/Render/` + `Engine/Render/Shader/`

Naming follows the render-unit suffixes in `SKILL-Naming.md` (§2): `…Rasterization` for draw units, `…Submission`
for batched GPU work, `…Sequence` for ordered chains. No `Pass`, no `stage`.

| Step | Deliverable | Acceptance | Runs where |
|---|---|---|---|
| **R0** | One viewport render unit attached beside the Project Browser: acquire the existing device and an offscreen target, clear to a known colour, present | Capture shows the viewport; the journal records device selection and target extent | Windows (user) |
| **R1** | Polygon upload, camera matrices, cluster-record buffer, indirect draw submission; the K1 sphere and the heightfield drawn **only** through the cluster path; per-cluster frustum + cone rejection with counters | Capture shows the scene; counter table proves rejected clusters are non-zero and the frame is identical with cone rejection off | Windows (user) |
| **R2** | Tier cut on GPU (Slang mirror of `TierCutSelection`), Hi-Z occlusion, page streaming into arena slots with coarser-tier fallback | Cut cluster counts on screen match the CPU values for the same camera; a bound smaller than the working set forces eviction and the image still resolves | Windows (user) |
| **R3** | Visibility raster + deferred resolve + two-layer transmissive gather; debug views equivalent to the HTML cluster modes | Glass object refracts the resolved opaque scene; debug captures in `Exhibits/` | Windows (user) |

**First pixels are R0, not R3.** The prior plan's first visible milestone was a triangle built on a new device set-up;
that is unnecessary work here, because `VulkanPresentation` already owns device, swapchain and offscreen targets and
`BrowserPreviewRendering` already proves the offscreen round trip. R0 is a small render unit, not a renderer.

### Division 3 — ReSTIR, after geometry is on screen

Follow `RestirGIRealtimeArchitecturePlan.md` without inventing a second architecture:

| Step | Deliverable | Gate |
|---|---|---|
| **S1** | Motion vectors, history validation, disocclusion rejection; capability probe (RT extensions present or not) | Motion reprojection proven on a moving camera before any reservoir exists |
| **S2** | ReSTIR DI over the clustered light set | Noise/1 spp comparison against the raster baseline, same scene |
| **S3** | ReSTIR GI one bounce, then denoise (`AtrousDenoise` equivalent) | Temporal stability: image difference between consecutive frames under a static camera |

Two constraints already recorded by earlier work on this project, which this plan keeps:

- Pipeline creation for ReSTIR can stall for minutes on a cold driver cache (`Docs/ReSTIRPipelineInvestigation.md`
  on the other line). Ship the pipeline-cache fingerprinting and the opt-in `FRONTIER_PIPELINE_TEST` modes from the
  start, not after a third report of "it hangs on first launch".
- GTX 1060-class hardware is the baseline. Ray queries are a capability branch, never an assumption.

---

## 4. What was wrong with the earlier K1a, with numbers

`c4ef131` added `Engine/Geometry/FacetTierBuild.{h,cpp}` (313 lines) and reported "sphere 5120 facets → 2562 welded
vertices / 63 clusters, all invariants PASS". The weld is right. The partition is not the reference algorithm, and
63 is not the reference number. Reproduced here by running the reference itself:

| Property | Reference (`GeometryHierarchy.js`, run in Node) | K1a (`c4ef131`) | Consequence |
|---|---|---|---|
| Sphere level-0 clusters | **48** | 63 | Different clustering; every later tier, cut count and page layout inherits the drift |
| Partition growth | Best-first over a frontier, scored by cone alignment − 0.3·(distance / expected radius) − seam penalty | Breadth-first from the lowest unassigned facet | 63 vs 48 is exactly this omission |
| Partition key | transmissive **class** (`class:0` / `class:1`) | material **family** | Splits one opaque surface into per-family clusters the reference merges; changes the glass-band rule |
| Welded-vertex cap | 256 (`VertexLimit`) | absent | A cluster may exceed the vertex budget a later indirect draw can address |
| Distance normalisation | mean adjacency spacing → expected radius `√(triangles/π)` | absent | Clusters grow by facet index, not by spatial coherence |
| Zero-length axis | normalises to +Z | returns a zero vector | Cone rejection silently disables itself on a degenerate cluster |
| Validation against the reference | none — expectations were invented in the validation program itself | | A gate that cannot fail the way the reference can is not a gate |

Verdict: **keep the type vocabulary** (`FacetField`, `PolygonComplex`, `FacetCluster`, `WeldOutcome`), replace the
algorithm, and gate it on the reference's own output. K1 in this branch does that; §5 is the proof.

---

## 5. Evidence for K1

Reference and port were both run on identical corpora and compared cluster by cluster
(`Exhibits/FacetCluster/README.md` holds the transcript and the replay steps).

| Corpus | Facets | Welded vertices | Clusters | Reference | Max deviation over sphere, box and cone |
|---|---:|---:|---:|---:|---|
| sphere (icosphere, 4 subdivisions) | 5120 | 2562 | **48** | 48 | 1.1 × 10⁻¹⁶ m |
| heightfield cliff (96 × 96 quads, open border) | 18432 | 9409 | **159** | 159 | 1.8 × 10⁻¹⁵ m |
| seam cylinder (48 × 20, meridian seam, marked sharp rims) | 1920 | 1008 | 16 | — (own corpus) | invariants only |

Cluster **membership** matches the reference exactly: same seed order, same first facet, same facet count, zero
mismatches over all 207 clusters. Enclosure sphere centres/radii, cones, boxes and cutoffs agree to double round-off.
That is the property the earlier attempt could not claim, and the property every later glyph of this plan depends on:
a coarser tier is only meaningful if the finest tier is the same tier the reference built.

Compilation: `g++ 12.2`, `-std=c++20 -O2 -Wall -Wextra`, no warnings. Build+validate time 0.1 s for all three corpora.

---

## 6. Risks and the honest limits of this environment

| Risk | Effect | Handling |
|---|---|---|
| Crash not cleared by `39689691437` | Nothing else can be tested by the user | Journal from a crashing run; the fix is written against the recorded stage (§2) |
| No GPU, Vulkan, CMake or MSVC here | Division 2 cannot be compiled or run in this sandbox | Divisions 1 gates run here; Division 2 gates run on the user's Windows machine and are never reported as passed from here |
| Windows CI only builds `arena/73e163a3-frontier` and `arena/db2f9953-frontier` | A push of this branch produces no artifact | Merge this branch into `arena/db2f9953-frontier` (or add the branch to `.github/workflows/WindowsWorkspaces.yml`) before expecting a binary |
| Two parallel SultanAladin lines | Duplicate geometry work, divergent evidence | This branch's K1 lands on the engine line as new files only; `c4ef131`'s K1a is retired, not merged |
| Reference JS is JavaScript | V8 and glibc transcendental functions can differ in the last bit | Welding is unaffected (duplicates are computed by identical expressions); normals/centroids differ ≤ 2 × 10⁻¹⁵, measured, not assumed |
| Driver pipeline cache stalls (GTX 1650 class) | First launch appears hung | Pipeline-cache fingerprints and the opt-in test modes ship with S1, not after |

Not verified here, and not to be reported as verified: the Windows build of anything in this branch, the Project
Browser after `44df519`, and every Division 2/3 item.

---

## 7. Immediate sequence

1. **User:** run `frontier-windows-39689691437`. If it dies, send the newest
   `%LOCALAPPDATA%\Frontier\Logs\Frontier-*.log`.
2. **K1:** merge this branch's `Engine/Geometry/` + `Executables/Validation/FacetClusterValidation/` into
   `arena/db2f9953-frontier`; add the CMake target and the `Build.bat` to the existing validation set
   (snippet in the validation folder's README).
3. **K1b:** QEM tier construction against the level progressions in §3, gate extended to compare every level with
   `stats.json`.
4. **R0:** viewport render unit beside the Project Browser, presenting a cleared target — the first pixels, and the
   first time the engine line has a renderer at all.
5. **R1:** the K1 sphere through the cluster path. From then on every milestone has a picture.

Deferred deliberately: ReSTIR (S1–S3), the GLTF/OBJ interchange question, and any scene-renderer work inside the
material authoring preview until Division 2 has a viewport to share. A renderer is not a feature of a material
editor; the material editor is a client of the renderer, and that ordering has not yet been established in the
native line.
