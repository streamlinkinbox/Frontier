# Bounded vertex-cache refresh and moving-receiver holdouts

## Protocol fixed before measurement

Test refresh before adding accumulation/merging. The prior persistence gate preserves one frozen bank; this phase
measures whether reduced redraw noise conceals correlated error and poor temporal averaging.

| Concern | Fixed choice |
|---------|--------------|
| Arms | Fresh RIS8 inline; cache refresh1/4/8 submissions; fresh cosine6/8 restricted-component controls |
| Cohorts | 32 independent cold cohorts × 1024 reservoirs × 16 receiver poses |
| Fixtures | Triangle square/strip, half-width0/.12/.3/.6; .3 is a new geometry holdout |
| Motion | x=.5−.5cos(2π pose/16), uploaded float coordinates, y=z=0, normal+Z |
| Illumination | Directional light, intensity half at pose9, restored at pose13; explicit revision tokens |
| Refresh | CPU periodic force at pose divisible by1/4/8; light revision always invalidates all cache periods |
| Source | Original donor x=0 and .75 cosine/.25 uniform area; eight candidates, target floor .01 |
| Fresh controls | Six/eight current-receiver cosine paths for this component, not full four-bounce Whole6/Whole8 |
| Randomness | MT19937-64 0x38d29a17 + fixture×0x1000193; new draw each pose/cohort; paired GPU local index |
| Reference | Independent clipped-area midpoint quadrature1024²/2048²; refinement<1e-7 |
| Means | Each pose/method within4 nominal SE+.0001; period1/inline estimates within2e-6 pointwise |
| Temporal metric | RMSE of each reservoir's 16-pose mean residual against changing reference |
| Instant metric | RMSE over all pose/cohort/reservoir residuals |
| Flicker metric | RMS successive residual difference, subtracting physical reference changes |
| Correlation | Centered lag1 residual correlation pooled across within-cohort adjacent poses |
| Paired contrasts | Period4/8 minus fresh6/8 instantaneous and temporal MSE, cohort nominal SE |
| Work | All cold/periodic/revision rebuilds included; actual nearest-facet invocations, not GPU timing |
| Promotion | Zero; no tuning cadence, thresholds or holdouts after measurement |

A low flicker number is not automatically better quality: a frozen wrong estimate can be stable. Temporal RMSE
measures averaging along this prescribed receiver trajectory, not a production TAA reconstruction metric.
Cached estimates share source samples across poses; repeated samples are never counted as independent cohorts.
No new spatial donors, sample-count growth, general material/suffix support or production allocation changes.
Results pending.
