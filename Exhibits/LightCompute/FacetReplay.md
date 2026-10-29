# Explicit facet sampling for path replay

The opt-in replay mode samples one emissive triangle uniformly at every diffuse or rough-opaque bounce, then a
uniform point on that triangle. The contribution includes the receiver BRDF, both geometric cosines, inverse
squared distance, triangle area, source-count probability and current-BVH shadow visibility. The host rebuilds
the source index list when the scene changes. Degenerate emissive triangles are rejected.

The default explicit-facet mode uses **one strategy per light path**: later BSDF hits on emissive triangles
do not add that same emitted radiance again. A separate, opt-in [balance-heuristic mode](ReplayBalance.md)
combines the two strategies; it improves one large-light variance fixture but worsens a small-light fixture.
Uniform-environment misses remain evaluated by the BSDF path. A deliberately double-counted audit mode is
included only as a negative control. Source selection is uniform by triangle, not weighted by area or power.

## Vulkan results

SwiftShader CPU Vulkan, 8 batches of 1,024 centre samples per case; SE is the sample standard deviation divided
by √8,192. The area references are independently integrated in double precision.

| Fixture | Reference | Vulkan result |
|---|---:|---:|
| Small source, BSDF-only | 0.003178859 | 0.004272461 ± 0.000720633 SE |
| Small source, explicit facet sample | 0.003178859 | 0.003179019 ± 0.000000074 SE |
| Occluder moves away, temporal + spatial reuse | 0.119909106 | 0.120816319 ± 0.000491795 SE |
| Dark source becomes emissive, reused history | 0.239818213 | 0.239877730 ± 0.000145445 SE |
| Three emissive triangles | 0.267649921 | 0.266224913 ± 0.001545690 SE |
| Double-count negative control | 0.479636426 | 0.482404441 ± 0.004754729 SE |

For the small unoccluded source only, the explicit/BSDF-only **per-sample standard-deviation ratio** was
`0.0001024`. This is not a runtime or equal-ray-cost comparison: explicit sampling adds a shadow ray per
bounce and a host-side source list. The negative control was `2.0115×` the correct open-source integral.
A fully blocked source returned zero, the no-source uniform-environment case remained correct, and a
degenerate source was rejected. Rough opaque channels also passed independent area quadrature.

A 24-triangle room used 400,000 independent double-precision CPU paths per configuration, brute-force
intersections and a different RNG. Four-hit diffuse and rough-opaque open/blocked cases, including temporal and
spatial reuse across a moving blocker, passed the recorded six-combined-SE-plus-tolerance bounds. For the
rough blocked/reused red channel the CPU reference was `0.0218093 ± 0.0003296` SE and Vulkan was
`0.0217390 ± 0.0004958` batch SE. Pixel and batch SEs are both recorded; independence and finite-sample tails
remain assumptions. This is a model/implementation comparison, not real-world calibration.

## Captures

[Four unfiltered Vulkan readbacks](FacetReplayCaptures.md) show the open, blocked and reopened room at one or
four hits. They are authored SwiftShader scenes, not an editor or hardware-GPU capture.

## Scope and reproduction

The implementation remains a full random-sequence replay experiment, not classical cheap endpoint
reconnection or an integrated editor renderer. It handles bounded opaque triangles, constant two-sided
facet radiance and the existing uniform environment; it does not handle textured or transparent sources,
analytic lights, volumes or general materials. The shadow test now skips the sampled source triangle instead
of discarding a fixed endpoint interval; it blocks a previously leaking 84-float32-ULP separation, including
under moving temporal/spatial reuse. Float intersection and origin-offset limitations remain.
[Endpoint audit](ReplayEndpoint.md). Uniform triangle selection can be inefficient with many unequal lights.
Finite 32-bit RNG streams and the positive target floor are not a proof of independence or continuous support.

```bash
python3 Exhibits/LightCompute/Code/Prepare.py  # if the pinned Vulkan tools are absent
bash Exhibits/LightCompute/RunFacetReplay.sh
SANITIZE=1 bash Exhibits/LightCompute/RunFacetReplay.sh
```

[Release output](Results/FacetReplay-release.txt) · [sanitized-host output](Results/FacetReplay-sanitized.txt).
The external SwiftShader Vulkan driver is not sanitizer-instrumented; no hardware-GPU performance claim is made.
