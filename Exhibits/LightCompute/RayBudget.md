# Exact-sample nearest-facet query-budget audit

## Frozen protocol before measurement

Pair six inherited receiver-bank arms with six query-elided counterparts. No sampler, RNG advance, density,
normalization, candidate count, storage or refresh policy changes. This is query work, not physical-GPU timing.

| Concern | Fixed choice |
|---------|--------------|
| Arms | Legacy/fast inline area8,full bank8,rotate4/2,fresh cosine6/8;12 arms in one submission |
| Sampling | Same .75 receiver cosine/.25 known-square area mixture;fresh controls use cosine only |
| Trajectories | Same32 cohorts×1024 logical targets×16 poses×4 fixtures0/.12/.3/.6 |
| Events | Cold pose0;half-light9/restored13;same seed schedule and cyclic replacement |
| Elision1 | Cosine first-hit already proves source visibility;omit repeated source checks,including misses |
| Elision2 | Reuse source visibility already encoded by new q>.25,only for slots born at current pose |
| History | Always retrace receiver visibility for retained older candidates;never infer it from old q |
| Identity | Pointwise paired estimates<2e-6;stored candidate words equal;fast queries≤legacy per invocation |
| Fresh budget | Require exactly6/8 fewer nearest-facet calls per paired fresh6/8 invocation |
| Quality | 768 per-pose reference means within4 cohort nominal SE+.0001;all prior temporal/error metrics |
| Contrasts | Fast rotate4/2 vs fast fresh6/8,paired cohort MSE/nominal SE |
| Edges | 1024 explicit rays/fixture at square/shadow boundaries,y−.5/0/.5,16 receiver positions |
| Edge offsets | −1e-5,−1e-7,0,+1e-7,+1e-5;reconstruct actual hit point and compare first-hit/recheck |
| Safety | Any numerical edge disagreement blocks acceptance;no tolerance/holdout tuning after results |
| Captures | Cohort0 all64 output/payload submissions plus4 edge arrays;release/sanitizer parity required |
| Promotion | None automatic;production348.046875MiB/Engine/layout/defaults untouched |

q>.25 encodes birth visibility only in this fixed-normal,known-square mixture with a positive cosine density.
It is not a general-purpose visibility bit for arbitrary materials/surfaces. Slot birth is deterministic from
rotation and explicit revision rebuilds;no memory is added. Probe results do not prove every possible float ray.
Full bank and inline estimates/query counts must still match. All cost totals include cold/revision rebuilds.

Results pending. Reproduce: `bash Exhibits/LightCompute/RunRayBudget.sh`, then the same command prefixed by
`SANITIZE=1 BUILD_JOBS=1`. Host sanitizers do not instrument SwiftShader/shaders;no Windows/editor/GPU timing
acceptance. Existing144B bank payload+16B telemetry remains a production storage blocker,not solved here.

A setup attempt stopped before any physics checks because the submission forbids changing receiver extent.
The corrected probe preserves the prepared extent, repeats the1024 probe set and captures/checks its first1024
results. Receiver defaults are restored before trajectories. No sampler, threshold or edge case changed.
The historical setup log is retained separately;it is not a passing runtime measurement.
