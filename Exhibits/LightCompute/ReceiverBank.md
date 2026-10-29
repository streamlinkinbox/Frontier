# Receiver-anchored proposals with historical candidate densities

## Frozen premeasurement protocol

Change only the generation anchor: new draws use the current receiver position. Each retained candidate still
uses its own stored unconditional area density, including the visibility and geometry at its generation anchor.
The estimator remains sum(F_current/q_birth)/8, with canonical null draws included in M=8.

| Concern | Fixed choice |
|---------|--------------|
| Arms | Inline area8, full bank8, rotate4/2, current-receiver cosine6/8 restricted-component controls |
| Proposal | .75 cosine from generation receiver + .25 uniform over the known secondary square |
| Rotation/storage | Unchanged8 slots, replace4/2 per pose,144B payload+16B telemetry/test reservoir |
| Motion | Same16-pose x=.5−.5cos(2π pose/16), float upload, fixed normal+Z |
| Cohorts/fixtures | 32 cold cohorts×1024 trajectories;strip half-widths0/.12/.3/.6, all existing holdouts |
| Revisions | Full refresh at cold pose0, half-light9, restored-light13 |
| Seeds | MT19937-64 0x38d29a17 + fixture×0x1000193;same shared seeds/fresh controls as preceding phase |
| Correctness | 384 pose means:4 nominal cohort SE+.0001 versus independent converged clipped quadrature |
| Identity | Independent inline/full-bank pointwise difference<2e-6;query counts equal |
| Retention | All32 cohorts check unchanged retained payloads;first cohort captures all bank/output words |
| Audit | Independently infer each slot's birth pose and verify actual q_birth and current receiver estimate |
| Negative | CPU counterfactual rotate4 replaces q_birth with q_current;require at least1 failed pose mean overall |
| Metrics | Instant/16-pose mean-residual RMSE, residual-difference RMS, centered pooled lag1, actual BVH queries |
| Paired | Rotate4/2 vs fresh6/8 instantaneous/temporal MSE across32 cohorts with nominal SE |
| Promotion | None automatic;no cadence, mixture, threshold or holdout tuning after measurement |

The negative control uses actual GPU candidates but CPU analytic visibility/current density, not another GPU
arm. It isolates the denominator error;it does not independently verify a deliberately incorrect GPU program.
M stays8 despite temporal reuse. Each surviving draw is unbiased with its own generation density;substituting
another pose's density changes that sampling law. Slot age is inferred by deterministic rotation, without adding
anchor/age storage to the GPU payload. No balance heuristic over mixed birth proposals is introduced.

Reproduce with `bash Exhibits/LightCompute/RunReceiverBank.sh`, then `SANITIZE=1 BUILD_JOBS=1` for the host
sanitizer build. Results pending. This remains software Vulkan, not physical-GPU timing or production TAA.
No production Engine/layout/default change;output stays348.046875MiB. The bank's existing large footprint is
not solved here;the uniform proposal still relies on one known diffuse square/deterministic directional suffix.
