# Seed-bank correlation: measured bias and the unused-draw fix

This closes the open item left by [combined seed-bank reuse](ReplaySampleCombined.md): "Correlations between
retained seeds across frames and pixels remain unresolved." They are now measured — across frames and across
pixels (spatial study below) — and the harmful part is fixed in `PathReplayReservoir.slang` /
`PathReplaySampleHistory.slang`.

## Method

`ReplaySeedCorrelationValidation` (host CTest, no GPU) mirrors `GenerateReplayReservoirs` candidate order,
hash streams (`NextRandom`/`RandomUnit` verbatim), duplicate guard, temporal weight
(`target * W * retained count`), the bank shift, and the pre-spatial resolve estimator on a scalar one-bounce
fixture: a seed consumes the same two draws as `ReplayDirection` and maps to a peaked radiance, compared
against a deterministic quadrature reference (state 0: 0.241271, state 1: 0.410160). 4,096 independent
replicate chains × 24 frames; per-chain post-warmup means give z-scores. A state change at frame 30 tests
dynamic tracking; a 60,000-frame chain gives lag correlations. This tests reservoir bookkeeping statistics,
not scene rendering; the GPU combined check remains blocked in this sandbox (no Vulkan driver).

## Result: the original combination was biased

| Configuration (stationary) | Bias | z |
|---|---|---|
| Fresh only | +0.31% | +0.38 |
| Seed bank only (3 ages entered) | +0.75% | +0.97 |
| Temporal only (history cap 1 / 8 / 64) | +0.58 / +0.41 / +0.33% | ≤ +0.75 |
| **Temporal + bank (original semantics)** | **−25.6%** | **−46.3** |
| Fresh-draw banking, newest age only | −14.8% | −24.1 |
| Unused-draw banking, all ages entered | −14.3% | −29.5 |
| **Unused-draw banking + newest age only under temporal reuse (shipped)** | **+0.43%** | **+0.84** |

Mechanism: a banked seed that had competed in a reservoir is correlated with that reservoir's stored weight
`W = Sum / (Count * TargetChosen)` — its own target was a summand of `Sum`. When the seed later enters a
reservoir alongside the temporal candidate carrying that `W`, the merge identity no longer holds and the
estimator is pulled low. Banking the generator stream's **next unused draw** removes birth-frame competition;
additionally entering **only the newest banked seed while temporal reuse is active** removes later
re-competition against descendant summaries. Either change alone is insufficient (−14%).

## Shipped semantics

- `FinalizeReplaySampleHistory` (combined mode) banks the draw after the generation draws
  (`max(Sampling.y, 1)` draws skipped), never a candidate or the selected seed.
- `GenerateReplayReservoirs` enters all banked seeds when temporal reuse is off, and only the newest banked
  seed when it is on. The duplicate guard against the temporal candidate is unchanged.
- Post-change (dynamic) bias for the shipped configuration: +1.23% (z +0.68). Bank-only output standard
  deviation halves vs fresh (0.260 vs 0.523) with lag-1 correlation +0.50 from candidate persistence — a
  variance/correlation trade, quantified here, **not** an equal-time speedup claim (combined modes trace
  2.9–3.8× the paths per frame in this harness; see the results file for path accounting).

## Cross-pixel (spatial) study

A six-pixel strip whose fixture peak (0.26→0.36) and scale (0.90→1.15) drift per pixel, so spatial donors
carry genuinely different integrands. Resolve mirrors the shader cross tap reduced to 1D (self, left,
right), retraces donors at the receiver, and persists the pre-spatial Local as temporal history. Both
spatial-only and full temporal+spatial+bank modes stay unbiased at every pixel (worst |z| = 1.82 over
12 pixel/mode pairs, inside multiple-comparison noise): the spatial merge does not reintroduce the
correlation bias, because a pixel's banked seeds never competed in a neighbour's reservoir.

An exact 2D version mirrors `ResolveReplayReservoirs`' five offsets {(0,0), (−1,0), (1,0), (0,−1), (0,1)}
and centre mapping on a 4×3 grid with position-drifting peak/scale: spatial-only worst |z| = 2.00 and
temporal+spatial+bank worst |z| = 1.69 over 12 pixels each, again consistent with unbiased merges.

## Multiple fresh candidates (Sampling.y > 1)

The unused draw sits after `max(Sampling.y, 1)` generator draws; the harness now also runs
`Sampling.y ∈ {2, 4}` for bank-only and temporal+bank modes: all unbiased (|z| ≤ 0.91). A negative
control with an off-by-one skip (banking an already-used draw) fails as predicted in the temporal
modes (−9.9%, z −20.6 at 2 candidates; −4.7%, z −12.5 at 4) while bank-only stays clean, so the
check has teeth exactly where the correlation mechanism operates.

## Verification

| Check | Result |
|---|---|
| `ReplaySeedCorrelationValidation` (release + ASan/UBSan) | ✔️ Pass (stationary/dynamic \|z\| ≤ 0.92) |
| LightCompute host CTests (release + ASan/UBSan) | ✔️ 6/6 |
| Slang → SPIR-V: Generate, Resolve, Gather, Finalize, Primary | ✔️ Compiled (Slang 2026.12 library, sandbox) |
| GPU combined check (`ValidateReplaySampleCombined`) | 🔴 Not run — no Vulkan driver in sandbox; its bank expectation was updated to the unused draw (`ReplayBankSeed`) for when it runs |

## Reproduce

```sh
cmake -S Exhibits/LightCompute -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel && ctest --test-dir build -R ReplaySeedCorrelationValidation
```

Raw output: [Results/ReplaySeedCorrelation-host.txt](Results/ReplaySeedCorrelation-host.txt), including the
pre-fix failing numbers measured with the same harness.
