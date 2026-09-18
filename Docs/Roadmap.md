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
| 6 | Sun-coin variance, occluded-selection weight loss | ❌ | 10 % | Named and measured as the residual; no fix attempted |
| 7 | Independent reference (second seed stream) for the RMSE floor | ✅ | 100 % | `--seed-stream N` (0 = the identity, verified byte-identical against the pre-change binary). The floor is measured, not shared: the plain arm moves 0 → **1 181.57** and ReSTIR 6 123.37 → **5 991.04**, so §14.3's headline is **5.1×** rather than 7.8× — and the flattery is per-arm (21.6 % for plain, −2.2 % for ReSTIR), not the ~15 % the variance algebra predicted. The convergence sheet prints both columns (`⑨ THE FLOOR`), so the table cannot drift from the evidence |
| 8 | Quality dials (render scale, candidates, extra, taps slider, GI toggle) | ✅ | 100 % | Exposed and documented |
| 9 | Materials M1–M10 + kernel K0–K5 | ✅ | 96 % | GPU pixels for the triptych, M10 level, denoiser A/B |
| 10 | Deferred material work (M4c dispersion, glints, displacement ch20, Tier-B multi-slab) | ❌ | 0 % | Queued, unscheduled |

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
