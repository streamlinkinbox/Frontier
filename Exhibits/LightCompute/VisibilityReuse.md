# Confirmed visibility reuse: complete numerical contract and matched-work comparison

## Protocol fixed before measurement

Preserve the reconstructed-point visibility decision,including the three previously failing rays. A primary
hit is not substituted for that decision. Cache one actual reconstructed-point visibility result per new bank
candidate and reuse it for validation,density and newborn receiver evaluation. Older candidates are retraced.
Fresh cosine paths skip only rechecks for out-of-domain/null proposals,which contribute zero regardless.

| Concern | Fixed choice |
|---------|--------------|
| Arms | Legacy6 + confirmed-reuse6:area8,bank8,rotate4/2,fresh6/8;12 arms in one dispatch |
| Samples | Same32 independent cold cohorts×1024 trajectories×16 poses×four existing fixtures |
| Events | Same cold0,half-light9,restored13;identical RNG advances,mixture,normalization and rotation |
| Contract | Confirmed reconstructed-point visibility is authoritative for a stored surface position |
| Numerical fix | Query once and reuse its decision;no shadow margin,epsilon or tolerance changes |
| Instrumentation | Record original/reconstructed directions,hit points,facet IDs,distances,float words |
| Ray isolation | Query original and reconstructed directions with identical origins and short maximum distances |
| Boundary probes | First960 historical entries plus64 neighboring representable inputs per fixture |
| Neighbors | Receiver1 and its adjacent floats;target shadow-cut+1e-7 ±3 ULPs;three y coordinates |
| Regressions | Original indices918/938/958 must still have first=1,recheck=0,safe=0 |
| Safety | All4096 retained boundary cases preserve recheck;log old first-hit disagreements separately |
| Main identity | All paired estimates within2e-6;all stored candidate words equal;queries never increase |
| Fresh savings | Exactly the number of out-of-domain misses,not the entire6/8 candidate budget |
| Means | 768 pose means within4 nominal cohort SE+.0001 versus independent converged quadrature |
| Quality/work | All instant/temporal/flicker/lag1 metrics;paired fast rotate4/2 vs fast fresh6/8 MSE/SE |
| Cold cost | Every submission,cold/revision rebuild and current receiver query included |
| Storage | Existing144B bank payload+16B telemetry;no production storage claim |
| Promotion | No automatic production promotion,no physical-GPU timing claim |

The positive q>.25 indicator encodes the confirmed birth query only in this fixed-normal,known-square mixture.
Only candidates born at the current submission use it;all historical slots get a fresh visibility query.
The failed blanket-elision exhibit remains unchanged and failing;this is a different implementation preserving
its original semantics,not a weakened safety gate. Numerical diagnostic input repeats preserve prepared extent.

Reproduce `bash Exhibits/LightCompute/RunVisibilityReuse.sh`,then `SANITIZE=1 BUILD_JOBS=1` for the host-sanitized
build. Results pending. Production output348.046875MiB,Engine/defaults/layout unchanged;no memory packing.
