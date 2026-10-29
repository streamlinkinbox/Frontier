# Density-bearing candidate retention and rotating refresh

## Protocol fixed before measurement

Retain eight proposals before donor-specific RIS winner selection. Every slot stores XY, outgoing scalar radiance
and its unconditional area-mixture density. Invalid/null proposals remain zero slots and still count in M=8.
At the receiver, sum F_receiver/q_area/8 across all eight slots, evaluating current visibility and geometry.
For this scalar component, exact receiver-contribution-proportional RIS would return this same sum; no extra
winner-selection RNG is needed. This is not a claim about exact RGB/luminance-target equivalence.

| Concern | Fixed choice |
|---------|--------------|
| Arms | Nonpersistent area-IS8; bank full-refresh8; replace4/2 slots per pose; current-receiver cosine6/8 |
| Rotation | Replace contiguous cyclic slots starting pose×replacementCount modulo8 |
| Invalidations | Force full8 on cold pose0 and explicit light revisions at poses9/13 |
| Retention | Rotate4 keeps4 slots, rotate2 keeps6; every slot replaced within2/4 poses |
| Storage | Eight float4 candidates128B + epoch metadata16B + telemetry16B =160B/test reservoir |
| Source | Fixed donor x=0; unchanged .75 cosine/.25 uniform area full-support mixture |
| Cohorts | Same32 independent cold cohorts ×1024 trajectories ×16 poses ×4 fixtures0/.12/.3/.6 |
| Motion | x=.5−.5cos(2π pose/16), float-upload coordinates, fixed normal+Z |
| Light | Half intensity at9, restored at13, mandatory explicit revisions |
| Randomness | MT19937-64 0x38d29a17 + fixture×0x1000193, independent new seed each pose/cohort |
| Means | Independent clipped-area1024²/2048² reference; refinement<1e-7;4 nominal SE+.0001 mean gate |
| Identity | Full-refresh bank versus separately implemented inline area8, pointwise tolerance2e-6 |
| Work | Include all cold/revision/rotation generation and up to8 current-receiver nearest-facet queries |
| Metrics | Instant RMSE,16-pose mean-residual RMSE, successive residual-difference RMS, centered pooled lag1 |
| Contrasts | Rotate4/2 vs cosine6/8, instantaneous and temporal MSE paired across32 cohorts, nominal SE |
| Acceptance | No automatic promotion; do not tune rotation, density or holdout after measurement |

Temporal averaging measures residuals along this prescribed moving receiver, not a production TAA image.
A slot always retains its actual source proposal density; it is not assigned the current receiver's density.
Deterministic replacement does not depend on candidate value. M remains8: retained proposals never become
new independent draws. Correlation may remain despite replacing the donor-selected winner representation.

This replaces the prior32B single-winner payload with144B candidate/epoch payload, plus16B telemetry; it is
an explicit prototype memory tradeoff, not a claim to fit the existing production allocation. Existing exhibit
SSBO spare slots hold the bank. Production348.046875MiB/defaults/layout remain unchanged.

A preliminary run was stopped after fixture0 to add direct payload-preservation/canonical-null checks and
payload captures, and to eliminate a redundant visibility query for out-of-domain cosine misses. No sampler,
rotation, density, thresholds or holdout changed. The full-refresh control now also requires exact query identity.
Results pending. Reproduction: `bash Exhibits/LightCompute/RunCandidateBank.sh`, then the same with
`SANITIZE=1 BUILD_JOBS=1`. Software Vulkan and host sanitizer checks are not physical-GPU/Windows acceptance.
