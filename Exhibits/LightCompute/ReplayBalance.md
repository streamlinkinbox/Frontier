# Balanced facet and BSDF sampling

The opt-in replay mode combines one uniformly selected emissive-triangle point and one cosine-distributed
BSDF direction at each bounce, using the balance heuristic. For a triangle of area `A`, `N` source triangles,
distance `r`, receiver cosine `cᵣ` and absolute light cosine `cₗ`, the solid-angle densities are
`p_light = r² / (N A cₗ)` and `p_bsdf = cᵣ / π`. The light sample uses `p_light / (p_light + p_bsdf)`;
a BSDF hit on that emissive triangle uses `p_bsdf / (p_light + p_bsdf)`. An environment miss retains its full
BSDF weight. Occlusion is re-evaluated against the current triangle BVH.

This mode is **optional and off by default**. It requires explicit facet sampling and rejects the intentional
double-count audit control. Switching estimators retains the seed but caps represented history for that frame.
Only the existing bounded diffuse and rough-opaque models are covered. The default proposal is cosine-weighted;
rough replay now has a separately [opt-in GGX mixture](ReplayImportance.md). Triangles are chosen uniformly,
not by power or area.

## Results

SwiftShader CPU Vulkan; 8 batches of 1,024 centre samples. The table compares per-sample standard deviations,
not equal-time measurements. Both modes trace a light sample and a BSDF direction; runtime was not benchmarked.

| Source fixture | Integral | Facet-only mean ± pixel SE | Balanced mean ± pixel SE | Balanced / facet-only SD |
|---|---:|---:|---:|---:|
| Large square | 0.951516 | 0.956678 ± 0.027311 | 0.947849 ± 0.004196 | 0.154 |
| Small square | 0.00317886 | 0.00317886 ± 0.00000007 | 0.00318039 ± 0.00000211 | 28.5 |

The large-light fixture benefits from adding BSDF hits, whereas the small-light case gets **worse** because
its facet-only contribution is already unusually uniform. These are fixture-specific observations, not a
universal variance or speed claim. Batch SE and all RGB values are in the logs.

Half-visible and three-source scenes passed independent area quadrature; the near-source blocking regression
remained zero. A no-source environment remained 0.3 for a 0.5-albedo receiver under 0.6 radiance. Moving
blockers and facet-only → balanced-mode transitions each retained 40,960 temporal candidates and capped
changed history at one, with 97,280 spatial links in the moving test.

For four-hit diffuse and rough rooms, independent double-precision brute-force CPU tracing used 400,000 paths
per scene and a different RNG; all RGB channels passed the recorded six-combined-SE-plus-tolerance bounds.
The rough blocked/reused red reference was `0.0223152 ± 0.0003396` SE versus Vulkan
`0.0224561 ± 0.0003644` batch SE using sample variance. The earlier log reported `0.0003409` using population
variance for eight batches; see the corrected [texture-room regression](Results/ReplayTextureRoom-regression.txt).
These are comparisons of the same model, not physical calibration or a proof of unbiased adaptive reuse. Finite
pseudorandom correlations, origin offsets and near-coincident geometry remain limitations.

```bash
bash Exhibits/LightCompute/RunReplayBalance.sh
SANITIZE=1 bash Exhibits/LightCompute/RunReplayBalance.sh
```

[Release](Results/ReplayBalance-release.txt) · [sanitized host](Results/ReplayBalance-sanitized.txt) ·
[six comparison captures](ReplayBalanceCaptures.md) · [facet-only mode](FacetReplay.md) ·
[visibility endpoint audit](ReplayEndpoint.md).
The external SwiftShader driver is not sanitizer-instrumented; hardware-GPU and editor validation remain open.
