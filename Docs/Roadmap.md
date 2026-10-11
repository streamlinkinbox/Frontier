# Roadmap — what is left, in one place

Written 2026-09-17. Supersedes the "what's next" list in the proofs report (§13) as the *index* of outstanding work;
the report keeps the evidence, this file keeps the queue. Percentages are engineering estimates of the work
*remaining-to-done*, not metrics: 100 % means shipped and measured, not "code exists".

**Legend** ✅ shipped & measured · ⚠️ landed but unverified where it matters · ❌ not started

## A. Renderer / ReSTIR — ≈ 86 %

> The ≈ 86 % is the author's hand-set scope aggregate from 32afd9f, not the mean of the rows below (that mean was
> 57.8 % then and is 78.6 % now that #7 and #2's soak closed — the two items this section was waiting on). Left as set;
> the rows carry the measured figures.

| # | Item | | % | What's left → next step |
|---|---|---|:--:|---|
| 1 | Spatial-reuse convergence fix (history split + pre-merge cap) | ✅ | 95 % | One GPU A/B on the owner's card |
| 2 | Temporal reuse, M growth bounded | ✅ | 100 % | **The soak is run** (`CheckRestirSoak.sh`, GREEN at 1 000 frames): mean M 58.9 / max M 84, unchanged since frame 25 and 0.980x over a 4x longer run. The finding is two-sided — a bounded M bounds the *error* too, so the clamp is also the floor: ReSTIR stops improving past ~250 frames (7 334.12 → 7 423.79 on an independent stream) while the plain arm keeps improving (1 017.76 → 734.05, −28 %). Accuracy past this point is dials, not frames. Report §14.4 |
| 3 | Indirect/GI pool in the CPU mirror | ✅ | 100 % | — (A/B: 7 387 vs 7 508 · 7 223 vs 7 295 · 7 632 vs 7 680) |
| 4 | Indirect/GI pool in the kernel (`kFeatureGiReuse`, ON by default) | ⚠️ | 85 % | Landed text-verified; **needs the GPU run** |
| 5 | Indirect coverage 16 % → 100 % (replay + shift mapping) | ❌ | 10 % | **Measured twice, and the second measurement says do NOT build it** — §14.5 found where the missing pixels are (69 % sky escapes), §14.6 found where the ERROR is: the pool's penalty over the plain arm is UNIFORM across classes (3.75× in the class it covers, 3.75× in the class it cannot reach, 3.4–4.5× everywhere else), so covering the sky-escape pixels would import an estimator already worse than the fallback rather than fix a class-specific loss. Re-pointed at what the class table names: **#6** (sky, the largest excess at 4.45×) and glass/SSS (2.7 % of pixels carrying 17.8 % of the MSE). Revive on a lamp-dominated interior, where the covered class's excess is the thing to re-measure first |
| 6 | Sun-coin variance, occluded-selection weight loss | ✅ | 100 % | **The occluded-weight half is CLOSED as a negative result** (§14.7, 2026-09-23): skipping dead (W = 0) entries at the merge gates — the obvious fix, built at all four gates in kernel and mirror — measured **+9 % RMSE** (5 685 → 6 221) with a +1.3 % brightening, because refusing an entry on its observed outcome is adaptive-M bias; the M a dead reservoir carries is real information. The honest rule ships; the refused arm stays under `--restir-dead-skip`; the runner-up form is designed but deliberately unbuilt (§14.7 names its cost). The sun-coin half CLOSED the same way (§14.8, 2026-09-23): the flat cone estimator is the ZERO-variance form for the disc's flux — a limb-weighted coin would add ~18 % per-sample noise on every sun-lit pixel to remove a ≤2.34 % penumbra-band shape error (`SunLimbProof`, gated). **#6 is done: both halves measured, refused, pinned** |
| 7 | Independent reference (second seed stream) for the RMSE floor | ✅ | 100 % | `--seed-stream N` (0 = the identity, verified byte-identical against the pre-change binary). The floor is measured, not shared: the plain arm moves 0 → **1 181.57** and ReSTIR 6 123.37 → **5 991.04**, so §14.3's headline is **5.1×** rather than 7.8× — and the flattery is per-arm (21.6 % for plain, −2.2 % for ReSTIR), not the ~15 % the variance algebra predicted. The convergence sheet prints both columns (`⑨ THE FLOOR`), so the table cannot drift from the evidence |
| 8 | Quality dials (render scale, candidates, extra, taps slider, GI toggle) | ✅ | 100 % | Exposed and documented |
| 9 | Materials M1–M10 + kernel K0–K5 | ✅ | 96 % | GPU pixels for the triptych, M10 level, denoiser A/B |
| 10 | Deferred material work (M4c dispersion, glints, displacement ch20, Tier-B multi-slab) | ⚠️ | 25 % | **Glints DONE** (§15, 2026-09-23): `slate_glint_*` read in kernel + mirror through the shared automotive flake body; row-12 sheet gated (`RunGlintSheet.sh` — arms differ 4.5 %, energy within 1.5 %). Left: M4c dispersion (spectral seam), displacement ch20 (tessellation), Tier-B multi-slab |

## B. GPU verification — ≈ 5 % (no GPU, no SPIR-V compiler in the sandbox)

| # | Item | | % | What's left → next step |
|---|---|---|:--:|---|
| 11 | GPU render-verification (K0–K5, GI pool, M10 level, denoiser under motion) | ❌ | 0 % | **The single biggest open item** |
| 12 | Owner's fullscale blur + fireflies report closed | ❌ | 0 % | Needs commit + tier + denoise setting from the test |
| 13 | Brute-force-vs-reservoir budget study on GPU | ❌ | 0 % | Mirror says plain wins **5.1×** at equal resolve rate on an independent stream (§14.3, re-measured with #7 — the old 7.8× was shared-stream flattery) |

## C. Dynamic geometry (BVH for moving and animated objects) — D6/D7 delivered · D8 refit · D9/D9b device path · D10 temporal identity

See `Docs/DynamicGeometry.md` for the plan, the measured budget table and the decision record.

| # | Item | | % | What's left → next step |
|---|---|---|:--:|---|
| 14 | D6 object-space BLAS + per-instance transforms in the kernel | ⚠️ | 90 % | Built and CPU-proven (`Exhibits/Workbench/Traversal`, 103 gates: identity bit-identical, transform agreement, payload, wiring pins). Left: **one GPU run** |
| 15 | D7 TLAS over instances (build at load, rebuild per frame) | ⚠️ | 85 % | Measured **0.07 / 0.32 / 1.42 ms** at 256 / 1 024 / 4 096 all-moving instances (TLAS alone 0.05 / 0.23 / 1.06). ⚠️ 4 096 misses the ≤1 ms plan number on the 2-core proof host |
| 16 | D8 dynamic BLAS update path (refit for deforming meshes) | ✅ | 90 % | **In-place refit of the packed layout**: 4.39 ms for 31 927 triangles (vs 74.84 ms rebuild; the packed re-emit is 7.76 ms and no longer fits the uploaded slice). 116 gates, 0 failed — refit ≡ rebuild on 20 000 rays, the blob reaches its own geometry, untouched BLAS slices byte-identical. ⚠️ Left: one GPU run (kernel-side refit + rendered frame) |
| 17 | D9 GPU refit / GPU build kernels (wide-AABB refit; a build for topology changes) | ⚠️ | 95 % | **Both kernels written, gated, and now with their whole host and Vulkan halves; the RUN is the one thing left.** `BlasBuild.slang` (Morton prepass · per-level octant partition, which is also the sort · childBase scan · emit · leaf runs) and `BlasRefit.slang` (leaf rewrite + one dispatch per level, deepest first) — 15/15 lowered, §⑩ pins B1–B115. **The build rule was chosen by measurement** (§⑨g, four builds, same 12 000 rays): D9 v1 (octant, no pack) 17 835 nodes / 17.9 ms / 10.93 ms walk · clustered (bins + SAH merge) 4 681 / 15.8 ms / **19.73 ms** · collapse (uniform 8-way) 4 681 / 12.1 ms / **24.72 ms** · **shipped (octant + the format rule) 7 185 / 12.5 ms / 10.11 ms**. The two clustering rules produce the SAME tree here, so the SAH merge never fires — and both walk 2.4× slower than the shipped rule at 26 % empty slots against 46.7 %: in a wide format **a full node is not a faster node**, because filling the eight slots forces each box to be a union of slices the octant rule keeps apart. **The serial stages are gone**: the level's childBase sum and the arena's run sum are block-local scans plus a block prefix (§⑨j checks that arithmetic against the serial one over the shipped build's 7 185 nodes, block boundaries included). **The device path is written**: `BlasDevicePayload` (push blocks, soup, level table, dispatch plan — §⑨i checks 60 build dispatches and 9 refit dispatches against the tree that exists), `BlasBuildPipeline` (pipelines, buffers, barriers, readback verification against the mirror — links no Vulkan loader, so it compiles and is gated where no libvulkan exists), and `Exhibits/Workbench/Traversal/BlasDeviceRun.cpp` + `RunBlasDevice.sh` (`--refit` checks the refit against a mirror refit of the same deformation). 236 gates, 0 failed. ⚠️ **Nothing has been executed on a device** — this sandbox has no GPU: `RunBlasDevice.sh` reports SKIPPED here and is the command to run elsewhere. Left: that run |
| 18 | D10 ReSTIR/temporal integration for moving geometry | ✅ | 100 % | Delivered and gated (`CheckTemporalIdentity.sh`, GREEN): the history records WHICH surface it came from (bit 9, the OBJECT — never the triangle), stored in the moment image's reserved z/w and in an 80 B reservoir record, validated in the mean and in both pools. The shadow follows the object (865 / 1 072 floor points, footprint moves), a still scene is left alone (0.8 % of pixels), and the ghost is refused — **1.94× lower error** against the untouched level at the same pose (891 vs 1 732 RMSE). ⚠️ Left: one GPU run |

## D. Project format — P1–P6 shipped and gated

See `Docs/ProjectFormat.md` for the plan, the divergences and the measured sizes.

| # | Item | | % | What's left → next step |
|---|---|---|:--:|---|
| 19 | P1 header + directory + `TYPE`/`META` + checksum + gate | ✅ | 100 % | `SpaceFormat.h`/`SpaceCodec.{h,cpp}`; `CheckSpaceFamily.sh` GREEN (19 claims, 2 GPU skips) |
| 20 | P2 `.geometry` / `.material` / `.instance` + `MaterialSlot` | ✅ | 100 % | `SpaceExport.{h,cpp}`; 49/49 record sets identical by memcmp (CPU half) — AE=0 on device still owed |
| 21 | P3 `REFS` + `BLOB` + five modes + `-Pack`/`-Explode` | ✅ | 100 % | Dedup flat at 136 B/copy; `Pack(Explode(X)) == X` (`SpaceTool`, `Tools/Scripts/*.sh`) |
| 22 | P4 Unreal-style CLI, migrate Project-Zero, glTF → interchange | ✅ | 100 % | `CommandLine.{h,cpp}` + `SpaceTool`; CPU parity by memcmp (49 geometry + 49 material record sets) |
| 23 | P5 `.runtime` (`.state` family deferred by the owner's call) | ✅ | 100 % | Written by `SpaceTool`; a file claiming the state-embedding policy is refused by name |
| 24 | P6 `.environment` / `.pigment` / `.uvspace` / `.workflow` / `.archive` | ✅ | 100 % | Six exporters in `SpaceExport.cpp`; `-Bake=Sky` probes from `AtmosphereModel`; editors deferred (§11.3) |
| 25 | §10 open questions (8), esp. material assignment copy vs share | ✅ | 100 % | q4 answered **CopyOnWrite** and taken in code (project export slots); the rest documented defaults |
| 26 | Environment lighting stage A — bake the sky probe | ❌ | 0 % | CPU-measurable; the smallest version that pays |
| 27 | Environment lighting stage B — sky as a reservoir candidate | ❌ | 0 % | After A; fixes glass/specular sky variance |
| 28 | "This is the new master branch" designation | ✅ | 95 % | **Answered by the owner 2026-09-18: yes.** `main` is the repo's default branch and holds only the initial commit, so the promotion is [PR #3](https://github.com/unassignedinbox/Slate/pull/3) — 67 commits, `arena/01a0af43-slate` → `main`. A session can only push its own branch, so the merge click is the owner's; once it lands, `main` is this tree. Until then this branch is still the tip |

## Weighted summary

- Renderer/ReSTIR ≈ 86 % (the hand-set figure under §A) — everything left is GPU-verified or a deliberate research
  step: #5 (indirect coverage, the largest **measured** residual, and §14.4 says coverage/dials are what buys accuracy
  past the clamp's floor), #6 (sun-coin and occluded-selection weight loss), #10 (deferred materials).
- GPU verification ≈ 5 % — nothing in the sandbox can move it; it gates all remaining confidence.
- Dynamic geometry ≈ 92 % (mean of D6–D10) — D8/D9/D9b are built and CPU-gated, D10 is delivered and gated; what is
  left is the device run that closes D6/D7/D9, which is §B's item.
- Project format ≈ 70 % (mean of #19–#28) — P1–P6 shipped and gated (§D), plus q4 answered in code; what is left is
  the environment-lighting pair (#26/#27) and the branch designation (#28). The device-side AE = 0 parity run is owed
  to §B, not here.
- Whole product ≈ 66 % (mean of the 28 rows; 62 % before #7 and #2's soak closed and the project format landed).

## Next three, in order

1. 🔎 Run the current tip on the GPU with the HUD's ReSTIR row open ("indirect pool on/off · N taps") and report
   commit + tier + whether the blur/fireflies survive — closes #4 and #12 together, and §B is where the P1–P6 device
   parity run (#20) belongs too.
2. ✅ Done since this list was written: #7 (independent reference — the floor is measured, and §14.3's headline is
   5.1× not 7.8×) and #2's 1 000-frame soak (`CheckRestirSoak.sh` GREEN; the clamp bounds M *and* the error, so it is
   also the floor — more frames buy nothing past ~250, coverage and dials do).
3. 🧭 Pick the next CPU-measurable build: **replay + shift mapping** (indirect 16 % → 100 % — §14.3's biggest named
   residual and the only item that moves the headline), the sun-coin/occluded-weight study (#6), or the sky probe bake
   (#26, the smallest version that pays).

## E. Render finishing pass — owner's list (2026-09-28) — 4 items advanced + 2 diagnostics answered

> **Progress 2026-09-28:** #30 (flakes on non-RT paths) and #29 (materials through the multi-slab layered path) are
> CPU-proven with rendered evidence; #27 (sky as a reflection reservoir candidate) is prototyped and measured at
> ≈ 2.53× lower error; #31 (hardware RT-core traversal) is researched and fully authored as the `VK_KHR_ray_query`
> path (shader + host + clean patch), pending a GPU run. Two direct questions were also answered with evidence:
> emissive lights work in Surfel GI (yes), and the geometry-cluster feature is culling-only / not full Nanite LOD
> (new row #39). All land in `RaytraceToggle/`; the GPU-dependent parts are device-run away from shipped.

> Captured verbatim from the owner's pre-games hand-off: the renderer is judged feature-complete for building actual
> games *except* the items below. The owner's standing assessment is that, beyond these, the pipeline "cannot be
> improved any further (maybe minimal changes) without adding third-party libraries." These rows are the queue that
> keeps that claim honest. Grounded against the tree at this commit so the "what's left" column names real code, not
> intentions.

| # | Item | | % | What's left → next step |
|---|---|---|:--:|---|
| 29 | **Materials verified through the multi-slab (layered) path** | ⚠️ | 60 % | **CPU-proven this session.** A layered-slab evaluator (`shadeStack`, `--slabs`) was built in the mirror `RaytraceToggle/CpuMirror/ModeMatrix.cpp`: a material is now an ORDERED STACK OF SLABS (coat → flake → conductor/pigment) composited top→bottom with energy throughput, not one flat lobe-sum. The full 15-family showcase grid renders through it and **every family still resolves** (RMSE vs the flat path ≈ 1.6 %, the delta being energy conservation — the coat no longer double-counts). Evidence `RaytraceToggle/MaterialGrid/MultiSlab_Compare.png`. Note slabs already existed as the single 304 B slab (`MaterialIndex.{h,cpp}`); this is the Tier-B **multi**-slab half of #10. Left: wire the stack into `MaterialCodec`/`MaterialIndex` on the device + one GPU run. |
| 30 | **Flakes on the non-raytraced paths** | ⚠️ | 70 % | **DONE on the CPU mirror this session.** Root cause: on the non-RT paths a flake was a mirror aligned with the surface normal, so a cluster reflected the same smooth sky and read flat. Fixed with (1) a **sun disc** in `skyColor` (a sharp feature to catch) and (2) **per-flake facet normals** (`flakeFacetNormal`) — each fired flake is a randomly-tilted micro-mirror sampling the env in its own direction. Flakes now sparkle at Standard fidelity on **plain raster and surfel GI** (RT keeps the highest contrast). Evidence `RaytraceToggle/MaterialGrid/GlintSheet_Modes.png` (3-mode sheet). Left: mirror the facet-normal model into the GPU shader + one GPU run. |
| 31 | **Wire the hardware-RT traversal paths (RT cores)** | ⚠️ | 55 % | **Authored this session** (`RaytraceToggle/HardwareRT/`, research doc `RayQueryHardwarePath.md`). Decision from research: inline **`VK_KHR_ray_query`** (trace on RT cores from the existing compute shader), NOT a ray-tracing pipeline/SBT — the kernel is one dispatch, ray query is a drop-in and the fastest option for this megakernel; SBT is a later high tier. Delivered: `Shaders/ReSTIRTraversal_RayQuery.slang` (RayQuery `TraceTraversal`/`TraceTraversalOccluded` written to the exact existing seam — ray query returns the same `(t, instance, local-primitive)` the kernel already consumes, so nothing downstream changes); `Patches/ReSTIRViewport_RayQuery.patch` (**`git apply --check` clean**); `HostWiring/RayQueryAccel.{h,cpp}` (BLAS-per-instance + TLAS build with `instanceCustomIndex`=top-level row, set-1 AS descriptor, device-requirement + tier gating). Left: build the `-DFRONTIER_RAYQUERY` SPIR-V variant on an RT-core card and A/B vs the software walk (must match within traversal precision). Needs a device — §B GPU verification. |
| 32 | **Sky + cloud bake** | ❌ | 25 % | Stage A partly exists: `SkyRecords.slang` already has a **baked dome** slot (`SkyControl.w`, one bilinear fetch stands in for the march, sun cone inpainted from its boundary; proof `Exhibits/Workbench/Sky/SkyProbeProof.cpp`) — this is roadmap **#26**. Step: bake the **cloud** layer too (currently always marched via `WeatherMedia.slang`), decide the refresh cadence (per weather-change, not per frame), and wire the dome/cloud bake to invalidate on sun/weather edits. |
| 33 | **Sun bake** | ❌ | 20 % | Tied to #32/#26: the sun disc is inpainted OUT of the baked dome today so its energy never smears. Step: bake the sun's direct contribution (transmittance + disc) into a small resident table so sun-lit shading and the sky reservoir candidate (#27) read a baked value instead of re-integrating the disc each frame. |
| 34 | **Clamp maximum star size (no big-circle stars)** | ❌ | 30 % | Root cause found: both the shader (`PostRecords.slang` `StarAlong`) and its CPU twin (`VisibilityRaster.cpp`) size a star as `Radius = max(StarSize * 0.0002, PixelAngle * 0.5)` — there is a **lower** floor (never smaller than half a pixel) but **no upper clamp**, so a bright/large `StarSize` renders as a visible disc. Step: add an upper bound (e.g. cap radius at ~1.0–1.5 px and push extra magnitude into brightness/bloom, not diameter) in BOTH twins, keep them byte-identical (gate `Tools/CheckPostKernel.sh`), and re-shoot `StarFix/Stars_Compare.png`. Small, self-contained — a good first pick. |
| 35 | **Fix the fog** | ❌ | 10 % | `FogModel.h` + `WeatherMedia.slang` integrate fog with the cloud slab (`Docs/CloudFogViewportRepair.md`). Symptom not yet pinned by the owner. Step: reproduce in the cloud/fog viewport, identify the defect (banding, extinction/altitude coupling, or the height-fog falloff), fix in `WeatherMedia.slang` + `FogModel`, add a before/after to the CloudFog evidence. |
| 36 | **Scale / quality tiers verified** | ❌ | 40 % | The ladder exists — `FidelityClassifier` has 5 tiers (Minimal → Reference) driving render scale, candidate count, spatial passes, GI and AA; the Control Centre tile cycles them. Step: verify each tier actually applies its render-scale + dials end-to-end (resolution scale especially), confirm the tile wrap, and document the measured cost per tier so "Standard" is a real balanced baseline. |
| 37 | **Fix wind (clouds look wrong under wind)** | ❌ | 10 % | Wind is bound through the outliner (`Docs/OutlinerWindBindings.md`, `WindBindingEvidence`) and drives cloud advection. Symptom: wind makes the clouds "look weird" — likely the advection offset feeds the noise domain wrongly (stretching/tearing the cloud field instead of translating it). Step: audit the wind→cloud-noise coupling in `WeatherMedia.slang`, make wind translate the sampling domain coherently across octaves, verify against a time-lapse in the cloud viewport. |
| 38 | **Cross-target verification ("test it works on both")** | ❌ | 5 % | Owner scope to confirm — read as: prove the pipeline runs on **both hardware-RT paths** (software CWBVH walk *and* the RayQuery path from #31), and ideally on the owner's card vs a GTX-class card. Overlaps §B (#11) GPU verification. Needs a device. |
| 39 | **Full Nanite-style cluster LOD across all render paths** | ❌ | 15 % | Diagnostic finding (`RaytraceToggle/Diagnostics/`): Frontier has GPU **cluster culling** (frustum→cone→HiZ, `ClusterCull.slang`, raster/indirect-draw path) but NOT a virtual-geometry **simplification hierarchy** — no QEM simplifier, cluster-group DAG, streaming, certified error bounds (stated in `Docs/ClusterLodLab.md`). Cluster culling is raster-only; the ray paths traverse the BVH and ignore clusters. Step (large, separate from #31): offline cluster-group build with parent alternatives + geometric error + locked boundaries, runtime projected-error selection, and — to cover reflection/shadow rays — a cluster-group-over-BLAS so RT sees the same LOD. |

**Diagnostics answered this session (no deferred work):**
- **Emissive lights in Surfel GI — YES** (`RaytraceToggle/Diagnostics/Emissive_SurfelGI.png`): an emissive-sphere-only-lit scene shows warm GI bleed onto floor/wall/spheres in surfel GI while plain raster lights nothing. In the real engine, emissive triangles are also `Luminaires[]` (NEE) + emission-on-hit on the ReSTIR path.
- **Geometry clusters — see #39**: cluster culling works (raster path only); full Nanite LOD is unbuilt.

### Open decision — ReSTIR for the surfel GI (reflections / denoiser)?

**Question (owner):** should ReSTIR be added on top of the surfel GI, for reflections and/or as a denoiser input — and is it cheap?

**Recommendation:** *Reflections — yes, and it is the cheap, high-value half. Denoiser — no, that is not what ReSTIR is.*

- **For reflections / the sky as a candidate — do it.** This is already the tree's own **roadmap #27** ("sky as a reservoir candidate"), which §14.5/§14.6 named as the largest measured residual class (sky 4.45× excess; glass/specular sky variance). Reusing reservoirs for the glossy/reflection lobe is the natural extension of the existing spatial/temporal reuse and is *incremental*, not a new system — the reservoir machinery, the shift-mapping and the merge gates all already exist. Cost is a second small reservoir per pixel plus one reuse pass; on the CPU mirror the reuse passes are cheap relative to candidate generation, so on GPU it is well under a millisecond at Standard. This is the cheap win.
- **As a "denoiser" — no.** ReSTIR is variance *reduction at the sampling stage*, not a spatial denoiser; it feeds fewer-but-better samples INTO the denoiser, it does not replace it. The denoiser question is already answered separately (`DenoiserResearch.md`: the SVGF-family "smart" denoiser with albedo demodulation, Control Centre plumbing at `f8a62e6`). So: add ReSTIR reuse for reflections (#27), keep feeding its output into the existing denoiser — do **not** try to make ReSTIR be the denoiser.
- **Cheap?** The reflection-reuse pass: yes (sub-ms class on GPU, reuses existing code). A *full* indirect coverage rebuild (replay + shift mapping, #5) is the expensive cousin and §14.6 says do NOT build it on this scene — revive only on a lamp-dominated interior. Keep #27 (reflections) and #5 (full coverage) separate: do the first, defer the second.

**Prototyped & measured this session (2026-09-28):** a CPU reflection-reservoir prototype
(`RaytraceToggle/ReflectionReservoir/ReflectReSTIR.cpp`) implements exactly this — RIS with the **sun/sky as an
explicit candidate** + bounded spatial reuse over a glossy floor under a sun-disc sky (the specular-sky variance
class). Result: **RMSE 0.236 → 0.093 vs a 512-spp reference, ≈ 2.53× lower error at ~1 effective sample**; visually
the 1-spp baseline is a firefly storm and the reservoir image is smooth. Evidence
`RaytraceToggle/ReflectionReservoir/ReflectReSTIR_Compare.png`. This confirms #27 pays and is cheap; the remaining
work is porting the reuse pass into the device ReSTIR kernel.

### Adjacent renderer items already tracked (so the list stays complete)

Not in the owner's note but open and renderer-relevant — cross-referenced, not duplicated:
- **#11 GPU render-verification** — the single biggest open item; gates confidence on #31/#38 and everything ⚠️.
- **#5 indirect GI coverage** (16 % → 100 %, replay + shift) — measured twice, deferred by measurement; revive on a lamp interior.
- **#26 / #27 environment lighting bake** — the sky-probe bake and sky-as-candidate; #32/#33 above are the cloud/sun extensions of these.
- **#10 remainder** — M4c spectral **dispersion** (glass colour fringing) and **displacement / ch20 tessellation** are still deferred alongside multi-slab (#29).
