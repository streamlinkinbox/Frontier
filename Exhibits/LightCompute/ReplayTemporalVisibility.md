# Same-seed moving-visibility temporal replay audit

**Phase outcome:** completed as a test-only C++/Vulkan investigation. It now records the exact previous-reservoir seed, the previous resolved sample's radiance, the current same-seed retrace's first-ray outcome and radiance, plus the stored source weight, retained count, history weight, and roulette probability. No estimator or production shader behavior was changed.

## Status and scope

| Workstream | Estimate | Proven here | Still open |
|---|---:|---|---|
| 🏆 Moving-visibility provenance | **100% of this diagnostic phase** | 128 matched cohorts × 512 far-group samples × two temporal modes; per-sample prior/current seed identity and weight accounting checked | Add a controlled emissive source for direct-NEE/shadow-ray visibility; investigate cause of prior quality gates without changing the estimator |
| ReSTIR GI/rendering | **~40% ±20 pp** | Isolated C++/software-Vulkan studies | Correctness root cause, production integration, physical GPU, Windows, editor/scene, performance and image-quality acceptance |
| PBR materials/reflections | **~35% ±20 pp** | Existing isolated validation remains in the host suite | Integrated editor/production-scene and physical-hardware validation |
| Clouds, fog, sky/sun, lights, UI | Deferred | Not in this phase | Revisit after ReSTIR GI/rendering acceptance |

These are subjective engineering estimates, not coverage percentages or completion claims. Four stars go to the selected task because it directly tested moving visibility on the *same* reservoir seed, instead of inferring cause from aggregate RNG selection rates.

## Experimental method

- `motion_visibility_far`, lower firefly floor, temporal-1 and temporal-4; 128 cohorts × 512 far-group samples per mode (**65,536 per mode**, 131,072 total).
- Four frames per cohort. At the final frame, the prior selected-reservoir seed saved by the preceding Resolve must exactly match the seed being replayed now. A mismatch would fail validation; all 131,072 rows passed.
- For each sample, the test-only shader records the current replay's first BSDF-ray event (environment miss, non-emissive surface hit, emissive hit, or no event), current path luminance/target, and the previous selected sample's saved path luminance. It also records source weight/count, candidate history weight, and roulette probability.
- Host checks on every sample verified: `history_weight = current_target × stored_source_weight × min(source_count, cap)` and `p_history = history_weight / (fresh_weight + history_weight)`. The test ran with cap one.
- Executed the Release C++ validation harness on **SwiftShader software Vulkan**; Slang CLI 2026.14.1. This fixture contains **no emissive facets**: direct-NEE segment rays and NEE contributions are therefore zero. The actual visibility event measured here is the first BSDF continuation ray against the opaque occluder/environment, not a direct-light shadow ray.

## Results

| Mode | First-ray events | Current first-ray visibility rate | Mean target: environment miss vs opaque hit | Mean history weight: environment miss vs opaque hit | Same-seed prior/current radiance-state counts |
|---|---|---:|---:|---:|---|
| Temporal 1 | 65,331 environment misses; 198 opaque hits; 0 emitter hits; 7 with no recorded first-ray event | 99.6978% environment miss among 65,529 traced rays | 0.904294749 vs **1.0×10⁻⁴** | 0.809216597 vs **9.7126×10⁻⁵** | nonzero→zero 165; zero→nonzero 17; stable nonzero 65,314; stable zero 40 |
| Temporal 4 | 65,400 environment misses; 136 opaque hits; 0 emitter hits; 0 unclassified | 99.7925% environment miss | 0.912283747 vs **1.0×10⁻⁴** | 0.810009019 vs **1.0610×10⁻⁴** | nonzero→zero 136; zero→nonzero 0; stable nonzero 65,400; stable zero 0 |

“Nonzero/zero” classifies saved/current **path luminance**, not a separately saved prior ray-event flag. Thus temporal-4's 136 nonzero→zero transitions exactly coincide with its 136 current opaque hits in this run. For temporal-1, do not equate every zero transition with an opaque hit: seven current samples had no first-ray event, and the previous zero state is inferred from radiance. The current first-ray hit/miss counts themselves are instrumented directly.

For both modes, current opaque hits returned the lower target floor and correspondingly small history weights, while environment misses had much larger mean targets and weights. This shows that, in this fixture, re-evaluating a prior sample on the current moving receiver can turn a formerly nonzero sample into a zero-radiance outcome and sharply lower its reservoir weight. It is a measured mechanism in this holdout—not proof that it caused the earlier aggregate mean-gate failures, not proof of unbiasedness, and not evidence that a production fix is warranted.

### Baseline and earlier-study comparison

The instrumented baseline reproduces each earlier path-stream and selection-stream baseline cohort's mean and MSE in **256/256 mode/cohort rows**. Maximum absolute deltas were **6.99×10⁻¹⁰** in mean and **2.267×10⁻¹⁰** in MSE. Image-hash matches remain **68/128 temporal-1** and **64/128 temporal-4**; the exact cause of the other hash differences is still unknown, so bitwise reproduction is not claimed.

The previous RNG alternatives remain rejected: path-hash temporal-1 mean **0.800349002** and selection-hash temporal-4 mean **0.814837773**, each below the **0.82268910511** reference. Neither is a fix. The new instrumented baseline itself remains **0.813958218** (temporal-1, RMSE 3.766765) and **0.819690989** (temporal-4, RMSE 0.787031); this phase did not improve those quality gates.

## Interpretation and limitations

The strong signal is that exact prior/current seed pairs are now traced and their reservoir arithmetic reconciles sample-by-sample. In temporal-4, every observed current opaque hit corresponds to a same-seed positive-to-zero path-luminance change and a near-floor history weight. Temporal-1 has seven no-event samples, so its prior/current zero-state transitions are a radiance proxy rather than a complete event-to-event transition audit.

This is not a rendered production-scene frame. The SVG visualizes measured diagnostic quantities; it is not a renderer screenshot. This test does **not** exercise direct-light shadow-ray visibility because the fixture has no emitter, and it does not establish temporal estimator bias, full-support correctness, physical-GPU behavior, Windows behavior, editor integration, or production performance. Next, add an explicit controlled emitter and record prior/current direct-segment outcomes before drawing conclusions about shadow-ray visibility.

No source from peer branch `arena/08e7de48-frontier` was incorporated for this phase; no peer change was adopted without directly comparable, independently verified evidence.

## Validation

- C++ Release validation harness built; both generated shader entry points compiled to SPIR-V 1.3 with the packaged Slang CLI.
- CTest: **10/10 passed**.
- Per-pixel summary/auditor tests: **4/4 passed**, including synthetic transition math, malformed/incomplete runs, weight-identity rejection, intervention instrumentation coverage, and SVG XML parsing.
- Python compilation, runner `bash -n`, and `git diff --check` passed.
