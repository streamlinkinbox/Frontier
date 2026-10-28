# Near-emitter shadow visibility

The explicit-facet replay experiment previously shortened every shadow segment by
`max(1e-5, distance × 1e-6)`. A fully covering opaque blocker at `z = 0.999994993`, in front of a light at
`z = 1`, fell entirely inside that discarded interval. The separation is 84 float32 ULPs at the light, yet
the blocker leaked almost the full unblocked contribution.

The replay shader now tests the open segment to the sampled point, excluding **only the sampled triangle**
by index. The BVH overflow fallback uses the same segment and exclusion. This removes the deliberate
endpoint gap in this fixture; it does not make floating-point intersections exact. The origin offset remains
0.001 world units, and coincident surfaces, shared edges, grazing rays and separations close to numerical
precision remain limitations.

## SwiftShader CPU Vulkan checks

| Case | Independent or expected result | Observed |
|---|---:|---:|
| Near blocker before the change | 0 | 0.238862 leaked |
| Near blocker after the change | 0 | 0 |
| Open light, independent area integral | 0.239818 | 0.240574 ± 0.000648 SE |
| Blocker beyond the source | 0.239818 | 0.240352 ± 0.000656 SE |
| Near blocker covering half the light | 0.119909 | 0.120709 ± 0.001924 SE |
| Four-hit open → blocked → revealed, temporal/spatial reuse | 0 / 0.239818 | 0 / 0.239964 ± 0.000206 SE |

The moving tests retained 20,480 temporal candidates in each direction and 97,280 spatial links in total;
no global reset occurred and changed history was capped at one. A forced one-entry BVH stack exercised
5,120 overflow fallbacks each for blocked and unblocked scenes and preserved both results. The independent
area integral is a double-precision midpoint calculation; SE assumes independent centre samples, so these
finite fixtures are not a general unbiasedness or robustness proof. The external Vulkan driver was not
sanitizer-instrumented and no hardware-GPU performance is claimed.

```bash
bash Exhibits/LightCompute/RunReplayEndpoint.sh
SANITIZE=1 bash Exhibits/LightCompute/RunReplayEndpoint.sh
```

[Before](Results/ReplayEndpoint-before.txt) · [release](Results/ReplayEndpoint-release.txt) ·
[sanitized host](Results/ReplayEndpoint-sanitized.txt) · [facet-sampling scope](FacetReplay.md).
