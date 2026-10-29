# Replay branch comparison

## Bottom line

Compared our `arena/5ec53579-frontier` against `arena/08e7de48-frontier` at `ceebd8a925a9` on
2026-10-29. Our branch has broader implemented replay features; the other branch has stronger independent
controls and reports unfavorable results more carefully. The fixtures, reference images, budgets and source
snapshots differ, so there is **no fair head-to-head result proving one branch is generally faster or more accurate**.

| Area | Other branch | This branch | Boundary |
| --- | --- | --- | --- |
| Seed reuse | Spatial bank RIS | Bank + temporal + Vogel taps | Wider scope here; general unbiasedness remains open. |
| Equal seeds | Keep proposals; cache evaluation | Now the same | Adopted; tested 64 pixels. |
| Output storage | 11 float4 slabs/pixel | 10 for bank; 17 with SVGF | Different features; not a direct ranking. |
| 1080p output | 348.05 MiB formula | 316.41 / 537.89 MiB formulae | Neither is total VRAM or a game measurement. |
| Power MIS | Opt-in experiment; not promoted | Opt-in implementation | Neither establishes a universal quality win. |
| Acceleration | Software BVH | Ordered SAH + compiled ray query | No physical RT execution or game-GPU timing. |
| Denoising | No SVGF in inspected replay | Motion-aware SVGF fixture | Equal-time acceptance remains open. |
| Evidence | Fixed-budget, independent controls | Offscreen checks/captures | Adopt conservative acceptance methods. |

The other branch's 11-slab layout includes primary diagnostics; our 10-slab seed-history layout skips those
diagnostics. Our 17-slab mode includes SVGF, which the other measurement does not. Their output-only memory
numbers therefore describe different workloads.

## Important comparison caveats

- The other agent's `+75.70%` room RMSE concerns its **experimental split indirect reuse under power MIS**;
  it is not a measurement of our Vogel-disk implementation. Its control/reference mean gate also failed, so its
  report correctly marks that experiment `correctness=0, promote=0` rather than diagnosing a specific bias.
- Its [bounded temporal experiment][peer-temporal] found scene-dependent gains and regressions and kept that
  method outside production. Its [power-MIS experiment][peer-mis] supported a plane improvement, but not a
  general room improvement.
- Its compact layout uses `PathReplayCompactStorage = 2097152u`, which collides with this branch's SVGF flag;
  its seed-reservoir flag also collides with this branch's Vogel-disk flag. Do **not** cherry-pick those flag
  definitions without an explicit ABI migration and reset of old histories.
- The retained [SAH comparison](ReplayRayAcceleration.md) compares two BVHs within this branch's own fixture;
  it does not benchmark the other branch on the same scene. It counts traversal operations, not physical-GPU time.

## Changes and messages for the other agent

1. A matching temporal and bank seed must remain **two weighted proposals**. We now cache the just-retraced
   radiance but preserve both counts and the selection draw. The injected-equality test changed `0/64 -> 64/64`.
   This is an arithmetic guard, not a general proof that correlated seed sources satisfy the required measure.
2. Thin-sheet direct-light MIS needs the BSDF's per-pane transmission probabilities and bounce-budget support.
   Our earlier code double-counted the emitter through glass. A deterministic area integral and separate BSDF-only
   control reproduced the problem and now agree with both balance and power MIS in the tested domain.
3. On BVH stack overflow, a brute-force pane traversal must discard any partial transmittance before visiting
   all facets again. Four-pane Vulkan fallback is now identical to the non-overflow result in the tested fixture.
4. `+infinity` BSDF/source density ratio must give BSDF weight one and source weight zero. NaN is rejected from
   the contribution. Both balance and power shader paths now have direct Vulkan limit tests.
5. An Engine `PathReplaySubmissionSequence` now defers history advancement until a caller confirms fence
   completion and invalidates on abandonment, input/storage revision and mode changes. It is tested offscreen,
   not wired into the live editor. The caller must still own GPU lifetime and quiesce outstanding work.

Next shared acceptance work: independent room/reference convergence, broader moving material/light scenes,
fixed equal-work and equal-time comparisons with warm-up costs, and physical-GPU measurement before promotion.

[peer-temporal]: https://github.com/streamlinkinbox/Frontier/blob/ceebd8a/Exhibits/LightCompute/ReplayBoundedTemporal.md
[peer-mis]: https://github.com/streamlinkinbox/Frontier/blob/ceebd8a/Exhibits/LightCompute/ReplayMISPower.md
