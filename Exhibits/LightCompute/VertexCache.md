# Persistent vertex/outgoing cache — fixed Vulkan experiment

## Protocol before measurement

Retain selected secondary position, outgoing diffuse lighting and area normalization across actual submissions.
Use the verified triangle/BVH fixture and source proposal unchanged. No production port or PR.

| Concern | Fixed choice |
|---------|--------------|
| Source | Eight .75 cosine/.25 uniform-triangle candidates, donor x=0, target floor .01 |
| Payload | Two float4 arrays: XY/outgoing/area weight, M/present/light revision/valid |
| Telemetry | One separate float4 array; 32 B payload + 16 B diagnostic telemetry per test receiver |
| Arms | Inline reference; correct cache; deliberately stale light; deliberately stale geometry/material |
| Cohorts | 32 independent cohorts × 4096 reservoirs; MT19937-64 0x73a4c951 + fixture×0x1000193 |
| Pairing | Same source seed for all eight states within a cohort and all arms |
| Fixtures | Prior no-strip/self, no-strip/shift, .12 strip, .6 strip; packed triangle BVH |
| Cadence | Cold, static warm, receiver move, half light, half albedo, wider strip, light off, light on |
| Cache validity | Explicit light revision plus existing host geometry/material reset; cold overrides negatives |
| Movement | Only receiver moves; donor domain stays x=0, so source payload remains applicable |
| Receiver shading | Fresh BVH visibility every submission; stored outgoing suffix, no warm source retracing |
| Mean gate | Inline/cache within 4 nominal SE+.0001 of independent clipped-area quadrature |
| Pair gate | Every correct cached estimate within 2e-6 of same-seed inline current evaluation |
| Lifecycle gate | Cache reuse only at warm/receiver steps; all other correct-cache steps regenerate |
| Work gate | Warm correct-cache source queries=0; at most one current receiver query/reservoir |
| Negatives | Stale light must fail at half-light/off; stale geometry must fail at albedo revision |
| Negative gate | Mean error >6 nominal SE+.005 integral; no acceptance tuning |
| Promotion | Zero; no general GI, history reprojection, multiple donor merging or physical-GPU timing |

Half-light uses an explicit caller revision token; changing intensity without that token is intentionally unsafe.
Geometry/material resets use the existing exhibit host's actual packed-facet/BVH change detection. The geometry
step widens the opaque strip by .05. Receiver motion does not change the stationary source domain.
Cold resets isolate cohorts; repeated warm submissions are not counted as independent samples or larger M.
This is payload persistence, not temporal sample accumulation. The production 348.046875 MiB output is unchanged.
Results pending.
