# Temporal direct-light endpoint-pair audit

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

## Interpretation and limits

The visibility transition audit reproduces the prior measurements, and the current receiver can see the stored endpoint change visibility while the prior endpoint and replayed history endpoint remain identical. Thus, the event is not explained by endpoint resampling during that replay. However, current reservoir selection often chooses a different endpoint, and the selected-endpoint-match metric does not track the temporal1 miss strongly. The transition correlations are descriptive across 32 cohorts; they do **not** establish causality, identify estimator bias, or justify changing estimator math. Keep the path-stream and selection-stream candidates test-only.

The visual is [`../ReplayTemporalDirectEndpointPair.svg`](../ReplayTemporalDirectEndpointPair.svg). The complete 51 MB per-pixel runtime log remains in ignored scratch storage at `_ClaudeScratch/provenance/direct-endpoint-pair.log`; the runner records driver/tool hashes, runtime configuration, source hashes, paired rows, and summary output there.
