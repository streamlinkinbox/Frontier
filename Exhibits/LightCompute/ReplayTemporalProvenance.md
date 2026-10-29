# Moving-visibility temporal-reservoir provenance audit

**Outcome:** completed as a test-only Vulkan/SwiftShader experiment. The run validates RNG-stream isolation and audits aggregate temporal-reservoir selection provenance. It does **not** diagnose an estimator defect or change the production renderer.

## Method and reproducibility

- Scenario: `motion_visibility_far`, `far` group, lower firefly floor, temporal-1 and temporal-4 modes.
- Three matched arms: baseline, path-hash intervention, and selection-hash intervention. The path intervention changes the path-seed hash while retaining the selection-seed hash; the selection intervention does the converse.
- 128 cohorts × 512 pixels per arm and mode (65,536 pixels per cell); 768 continued frames. Executed with the Release C++ `LightComputeValidation` harness on **SwiftShader software Vulkan** (`SwiftShader Device (Subzero)`). Slang CLI was 2026.14.1.
- Every arm passed its runtime validation and exit status. Per arm, 262,144 path-seed and 262,144 selection-seed values matched the independent host reconstruction. All 512 far-group pixels per cohort had an accepted history sample in both temporal modes (100% in these runs).
- Candidate RNG streams were isolated in all 256 paired mode/cohort rows: path intervention changed the path hash and held selection hash fixed (256/256); selection intervention changed the selection hash and held path hash fixed (256/256).

## Quantitative results

Reference mean: **0.82268910511**. `mean ± SE` is the across-cohort mean and standard error; `RMSE` is the square root of mean cohort MSE. The selection residual z-score is an approximate iid-Bernoulli diagnostic, not an independence test.

| Mode | Arm | Mean ± cohort SE | Cohort RMSE | History selected / accepted | Logged expected | Approx. z | History-weight share |
|---|---|---:|---:|---:|---:|---:|---:|
| Temporal 1 | Baseline | 0.813958218 ± 0.014657433 | 3.766765 | 0.500519 | 0.501112 | −0.3205 | 0.495470 |
| Temporal 1 | Path hash | 0.800349002 ± 0.000622794 | 0.169220 | 0.498123 | 0.500719 | −1.4024 | 0.486166 |
| Temporal 1 | Selection hash | 0.817774877 ± 0.014880948 | 3.802742 | 0.500046 | 0.501185 | −0.6156 | 0.497823 |
| Temporal 4 | Baseline | 0.819690989 ± 0.003129742 | 0.787031 | 0.195221 | 0.195890 | −0.4343 | 0.197202 |
| Temporal 4 | Path hash | 0.833564826 ± 0.016877823 | 4.313781 | 0.196320 | 0.196038 | +0.1824 | 0.210890 |
| Temporal 4 | Selection hash | 0.814837773 ± 0.000428555 | 0.112751 | 0.195251 | 0.195815 | −0.3655 | 0.192411 |

The paired interventions preserved the intended companion stream. Across cohorts, path-hash versus baseline changed history-weight share by −0.002858 (temporal-1) and +0.002790 (temporal-4); selection-hash versus baseline changed it by +0.001599 and −0.003764, respectively. These are descriptions of this finite holdout, not causal explanations for its output means.

The two previously reported lower-floor gates remain failures: the path-stream candidate is below the reference in temporal-1 (0.800349), and the selection-stream candidate is below it in temporal-4 (0.814838). Neither intervention is a fix and neither should become a production default. The high cohort RMSE in baseline temporal-1, selection temporal-1, and path temporal-4 also cautions against reading the across-cohort means alone as quality wins or losses.

## Reproduction cross-check and image-hash caveat

Compared with the earlier path-stream and selection-stream logs, the newly instrumented baseline's final image hash matched **132/256** cohort/mode rows (51.6%): temporal-1 **68/128**, temporal-4 **64/128**. Thus **124/256 hashes differ**. Both run generations report Slang 2026.14.1, so a compiler-version explanation is not established. The exact source of the hash mismatch remains unknown; do not claim bitwise reproduction.

Despite those hash differences, all 256 baseline cohort means and MSEs match each prior study within floating-point output precision: maximum absolute mean delta **6.99×10⁻¹⁰**, maximum absolute MSE delta **2.267×10⁻¹⁰**. The path candidate likewise matches its earlier path-stream cohort mean/MSE in 256/256 rows (max deltas **4.66×10⁻¹⁰** and **1.275×10⁻¹⁰**); the selection candidate matches its earlier selection-stream arm in 256/256 (max deltas **6.98×10⁻¹⁰** and **1.433×10⁻¹⁰**). Matching aggregate moments do not explain the differing per-cohort image hashes.

## Interpretation and limits

In this holdout, observed aggregate history-selection frequencies are close to the logged expected probabilities, and history acceptance is complete. That is evidence against a **gross aggregate roulette-count mismatch in this particular test**, not proof that temporal reuse is unbiased or that visibility handling is correct. The approximate z values assume iid Bernoulli choices while pixels may be correlated. The instrumentation records seed, choice, and cohort-level weight provenance; it does not identify every ray's visibility contribution, establish complete support, prove independence, or diagnose asymptotic bias. RNG agreement does not establish a cause for the earlier mean-gate failures.

This is actual **C++ harness execution over software Vulkan**, not a production-renderer integration or an editor/production-scene image. No production shader, reservoir default, or renderer behavior was changed. There is no physical-GPU, Windows, editor-viewport, or production-scene validation in this result. The SVG is a visual summary of measured diagnostic quantities, not a rendered scene frame.

## Validation run

- Release build completed for the provenance harness; the harness compiled all three shader variants to SPIR-V 1.3 using the packaged Slang CLI.
- CTest: **10/10 passed**.
- Provenance summary auditor synthetic tests: **7/7 passed**, including generated-SVG XML parsing, complete/incomplete log handling, nonzero exit rejection, and candidate intervention isolation.
- Python compilation, runner `bash -n`, and `git diff --check` passed.
- Direct SwiftShader device enumeration and shader execution succeeded. SGL's no-WSI build could not create its instance due to missing `VK_KHR_xlib_surface`; the direct C++ Vulkan driver path was used instead.

## Broader progress estimate (subjective, ±20 percentage points)

| Workstream | Estimate | Proven here | Remaining |
|---|---:|---|---|
| ReSTIR GI and rendering | **~40%** | Deterministic C++/Vulkan validation harnesses and several isolated statistical/temporal studies | Resolve estimator correctness; production shader/renderer integration; physical GPU, editor viewport, production scene, Windows, performance and image-quality acceptance |
| PBR materials/reflections | **~35%** | Existing isolated C++ reflection/PBR validation is included in the passing host test suite | Finish material/reflection behavior and validate it in the integrated editor/production scene on physical hardware |
| Clouds, fog, sky/sun, lights and UI | Deferred | Not part of this priority phase | Revisit only after ReSTIR GI and rendering acceptance |

These percentages are rough engineering estimates, not test coverage or claims of feature completion.

## Integration status / next investigation

This phase adds **test-only provenance instrumentation and reproducible audit tooling**, not production renderer features. No code from peer branch `arena/08e7de48-frontier` was incorporated for this experiment; previously inspected peer ideas were not adopted without directly comparable, independently verified evidence.

Next, retain the production baseline and instrument per-pixel/per-ray history visibility outcomes and contribution weights in a controlled moving-visibility case, then test whether any discrepancy survives independent stream controls and larger cohorts. Do not change the production estimator until that evidence identifies a specific defect.
