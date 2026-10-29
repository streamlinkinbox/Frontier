# Two weighted reservoirs:lower-storage candidate retention

## Protocol frozen before measurement — 2026-10-29

Problem:replace eight raw density-bearing candidates with two selected samples without changing historical
importance weights or losing support when the receiver moves. No production adoption,memory packing or tuning.

| Concern | Fixed specification |
|---------|---------------------|
| Controls | Previous12 arms unchanged;new full2 and rotate1 weighted pairs;14 arms |
| Pair | Two float4(point.xy,outgoing,Z) reservoirs plus one float4 revision descriptor:48B logical payload |
| Sources | Four proposals per reservoir;fixed .25 uniform+.75 cosine mixture at current receiver |
| Selection | Positive geometry-only target h=cosine×area-measure/π;exclude visibility and outgoing from h |
| Weight | w=h/q;select proportional to w;Z=sum(w)/(4×h(selected));nulls count in divisor4 |
| Randomness | Proposal stream unchanged;separate deterministic stream for reservoir selection |
| Refresh | Full2 replaces both every pose;rotate1 alternates one reservoir,full rebuild at0/9/13 |
| Current | Confirmed birth-query reuse for newborns;retrace historical visibility;average2 of .6×L×hCurrent×Z×V |
| History | Preserve selected point,outgoing,and birth Z bitwise;never replace birth h with current h |
| Samples | Same32 cold cohorts×1024 trajectories×16 poses×strips0/.12/.3/.6 and previous seeds |
| Mean gates | All896 means within4 nominal cohort SE+.0001;reference quadrature convergence<1e-7 |
| Identity | Previous12 estimates/payloads/query totals unchanged;all old safety gates retained |
| Exact check | Full2 versus full-bank8 on empty strip,where target matches integrand,within2e-6 |
| Independent | Enumerate finite RIS/null cases;reconstruct selection,Z,current estimate and retained payloads |
| Quality | Compare new arms vs corrected Fresh6/8 and Rotate4;no cadence/target tuning after measurement |
| Acceptance | Rotate1 instant+temporal RMSE no worse than corrected Fresh8 in all fixtures;queries≤Fresh8 |
| Evidence | Release and host ASan/UBSan,raw/module/source hashes,actual-buffer images,paired MSE/SE |
| Allocation | Common debug SSBO unchanged;48B logical payload is NOT measured allocation/bandwidth reduction |

The score is positive on every supported surface point even when birth visibility/outgoing is zero. A birth
visibility-weighted target would delete future-visible support;it is an explicit negative control,not a repair.
If quality fails,retain the rejection and stop this design rather than tune against the existing holdouts.

Results pending. Production Engine/defaults/layout and accepted348.046875MiB output-only remain unchanged.

Preflight before any trajectory means:reuse confirmed newborn visibility via existing telemetry,as for controls;
record all four resampling weights per reservoir for exact float32 replay. Neither change alters sampling.
