# Temporal direct-light endpoint-pair and visibility-counterfactual audit

**Scope:** test-only `overhead_emitter` moving-receiver fixture. This does not change estimator math or production shaders and is not production-scene evidence.

## Runtime and pairing checks

- SwiftShader CPU Vulkan, LLVM 10 driver; `ReplayResolve.spv` was passed through the recorded, non-inlining SPIR-V simplification sequence. The successful driver was a scratch build with Reactor optimization level 0 and ASTC disabled. This backend configuration is for completing this fixture audit, not performance evidence.
- 32 matched cohorts × 512 checkerboard holdout pixels = 16,384 samples per scenario/mode. Host seed sequences and pixel identities were matched.
- Every mode/scenario had 16,384/16,384 stored prior endpoints and 16,384/16,384 prior-to-history replay endpoint matches. Paired retraces passed; no missing direct rays.
- Current reservoir-selected endpoint still matched the prior endpoint less often: base-only temporal1 9,810/16,384 (59.88%), temporal4 4,750/16,384 (28.99%); added-only temporal1 8,873/16,384 (54.16%), temporal4 3,670/16,384 (22.40%).

## Direct-estimate comparison

| Visibility arm | Mode | Mean estimate | Reference | Relative error | Mean gate |
|---|---|---:|---:|---:|---|
| base-only | temporal1 | 5.36925822 | 5.37194399 | −0.0500% | PASS |
| base-only | temporal4 | 5.35041562 | 5.37194399 | −0.4008% | PASS |
| added-only | temporal1 | 1.69965081 | 1.84228819 | −7.7424% | **FAIL** |
| added-only | temporal4 | 1.81740654 | 1.84228819 | −1.3506% | PASS |

The base-only temporal1 pass and added-only temporal1 miss reproduce the earlier direct-estimate audit within the same fixture and matched seed protocol.

## Same-endpoint visibility transitions

Counts below are `blocked→blocked`, `blocked→visible`, `visible→blocked`, `visible→visible`. The base-only control was entirely visible→visible. In added-only, the same prior endpoint retraces included:

| Mode | Transition counts | Visibility changed | Cohort correlation with direct-estimate mean |
|---|---|---:|---|
| temporal1 | 585, 85, 559, 15,155 | 644/16,384 (3.93%) | visible→blocked count: −0.284; blocked→visible count: +0.436 |
| temporal4 | 387, 56, 1,107, 14,834 | 1,163/16,384 (7.10%) | visible→blocked count: −0.064; blocked→visible count: +0.129 |

For added-only temporal1, the selected-endpoint-match count has only a weak correlation with cohort estimate mean (**r = −0.076**); transition-conditioned history-weight correlations are also weak-to-moderate (visible→blocked **r = −0.077**, blocked→visible **r = +0.299**). Temporal4 selected-endpoint-match correlation is **r = −0.230**. Base-only transition correlations are undefined because there are no transition-count variations.

## Fixed-origin, fixed-endpoint mask-1 vs mask-3 counterfactual

The previous `base_only`/`added_only` comparison uses separate geometry arms. This additional test instead held each GPU-read-back receiver origin and stored world-space emitter endpoint fixed, then traced that same segment through mask 1 (base geometry plus emitter) and mask 3 (the same base geometry plus the added blocker and emitter). The current receiver pose was taken from the final motion frame, and the test asserted it was identical across both arms. It used the test-only CPU `TriangleVisibilitySpace`; it did not change estimator or production shader code.

| Source endpoint arm | Mode | mask 1 → mask 3 counts (blocked→blocked, blocked→visible, visible→blocked, visible→visible) | Added-blocker visibility losses | Cohort correlation with direct-estimate mean |
|---|---|---:|---:|---:|
| base-only endpoint samples | temporal1 | 0, 0, 4,592, 11,792 | 4,592/16,384 (28.03%) | +0.449 |
| base-only endpoint samples | temporal4 | 0, 0, 7,650, 8,734 | 7,650/16,384 (46.69%) | +0.075 |
| added-only endpoint samples | temporal1 | 0, 0, 1,144, 15,240 | 1,144/16,384 (6.98%) | −0.210 |
| added-only endpoint samples | temporal4 | 0, 0, 1,494, 14,890 | 1,494/16,384 (9.12%) | −0.092 |

The mask-1 CPU trace matched the existing paired GPU visibility code for all **16,384/16,384** base-only endpoints in each temporal mode (zero disagreements). This cross-check validates the CPU visibility classification for that endpoint/origin set. It is not a GPU execution of the mask-3 counterfactual. The added-only endpoint samples come from mask 2 (added blocker without base geometry), so their mask-1/mask-3 results are CPU counterfactual traces, not comparisons to that arm's original GPU visibility code.

The monotonic visible→blocked changes establish that adding the blocker changes visibility for these fixed segments in this fixture. This is stronger than the prior descriptive transition comparison for the *ray classification*, but does not prove those changes caused the temporal1 direct-estimate miss. The per-cohort correlations above remain descriptive and do not establish estimator bias or justify changing estimator math.

## Interpretation and limits

The paired runtime audit still confirms that replay re-created the identical stored endpoint for every sample, even though the current reservoir often selected a different endpoint. The fixed-segment counterfactual now directly verifies an added-blocker visibility effect while separating it from endpoint resampling. However, the selected-endpoint-match metric remains weakly associated with the temporal1 miss, and the mask-1/mask-3 cohort correlations do not establish estimator causality. Keep the path-stream and selection-stream candidates test-only.

Visuals: [`../ReplayTemporalDirectEndpointPair.svg`](../ReplayTemporalDirectEndpointPair.svg) (history transitions) and [`../ReplayTemporalDirectVisibilityCounterfactual.svg`](../ReplayTemporalDirectVisibilityCounterfactual.svg) (fixed-segment mask comparison). The complete 54 MB per-pixel runtime log remains in ignored scratch storage at `_ClaudeScratch/provenance/direct-endpoint-pair.log`; the runner records driver/tool hashes, runtime configuration, source hashes, paired rows, counterfactual codes, and summary output there.
