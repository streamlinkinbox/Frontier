# Four weighted reservoirs:intermediate memory/variance compromise

## Protocol frozen before measurement — 2026-10-29

| Concern | Fixed choice |
|---------|--------------|
| Problem | Reduce eight-candidate history without the pair's temporal regressions |
| New arms | full4w and rotate2w;four reservoirs,two proposals each |
| Controls | Previous14 arms unchanged,including rejected pair,corrected Rotate4 and Fresh6/8 |
| Target | Same positive geometry h;w=h/q,Z=Σw/(2hselected);never include birth visibility/outgoing in h |
| Estimate | Average4 of .6×outgoing×hCurrent×birthZ×currentVisibility;null draws count in divisor2 |
| Injection | Replace2 reservoirs/update,alternate halves;full rebuild0/9/13;8 cold/4 warm proposals |
| Visibility | Reuse actual confirmed newborn query;retrace history;no epsilon or tolerance relaxation |
| Storage | Four float4s+revision float4=80 logical bytes,versus144 raw/48 pair;telemetry16B unchanged |
| Allocation | Shared32-slot debug SSBO;logical reduction is not measured allocation or bandwidth saving |
| Samples | 32 cold cohorts×1024 trajectories×16 poses;previous seeds,path and intensity events |
| Fixtures | Existing0/.12/.3/.6 plus new predeclared .45 strip;no post-result score/cadence tuning |
| Means | All1280 means within4 nominal cohort SE+.0001;quadrature convergence<1e-7 |
| Safety | All previous boundary probes retained,including original three disagreement regressions |
| Baseline | Previous14-arm mean/result rows and captured bytes unchanged on original four fixtures |
| Identity | Empty-strip full4w estimate within2e-6 of full-bank8;old exact elision identities maintained |
| Audit | Exact float32 selection replay,Z/current-estimate reconstruction,retention/epoch checks,null controls |
| Acceptance | rotate2w instant/temporal RMSE≤corrected Fresh8 and queries≤Fresh8 in ALL five fixtures |
| Comparisons | Paired MSE/SE vs corrected Fresh6/8,Rotate4 and weighted pair;full4w vs Fresh8 |
| Completion | Both builds,all comparisons,visual evidence,independent audit,report and GitHub backup |
| Production | No Engine/default/layout change,packing,PR,Actions or physical-GPU timing claim |

If any acceptance fixture fails,retain the result without tuning the score or injection cadence against it.
Production output-only348.046875MiB stays accepted and unchanged. Broader RGB/GI/game acceptance is separate.

Results pending.
