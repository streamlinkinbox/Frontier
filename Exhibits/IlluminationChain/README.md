# Illumination chain validation

Headless statistical proof for the Phase 5 portable core (plan §8.2, §13 Phase 5):

- `Frontier/Engine/Foundation/DeterministicRandom.h` — PCG32, bit-exact replay per seed;
- `Frontier/Engine/Rendering/DiscrepancySequence.*` — Halton/Hammersley + Cranley–Patterson;
- `Frontier/Engine/Rendering/ReservoirSampling.*` — WRS/RIS reservoir with temporal
  (M-clamped) and spatial (Jacobian-factored) reuse combines;
- `Frontier/Engine/Rendering/VarianceFiltering.*` — SVGF temporal moments, edge-avoiding
  à-trous wavelet with variance propagation, joint bilateral 2× upsample;
- `Frontier/Engine/Rendering/IlluminationUnits.*` — the §8.2 chain declared for
  `SubmissionOrder` / `TransientTargetAtlas` / `BarrierDerivation`.

Oracles are analytic (the demo's GPU golden images join at the SDK phase — its
`RestirPathTracer.test.mjs` was never copied, per the HTML-only rule):

- RIS with a perfect target is **exactly** zero-variance (algebraic identity, seed-independent);
- rough-target RIS, temporal reuse and spatial reuse are unbiased within 5σ Monte-Carlo
  bounds at fixed seeds, with large measured variance reductions (RIS 10× under naive;
  temporal 0.71× of single-frame at 2× effective M);
- a 256-receiver / 32-light analytic scene runs discrepancy → RIS → temporal → spatial
  end-to-end: RMSE 0.385 vs 3.86 for naive 1-spp (mean truth 1.86);
- SVGF: RMSE 0.277 → 0.023 (12×) against known ground truth, guided step retained at 97%;
- the declared chain resolves in §8.2 order, keeps ping-pong/history targets off the
  transient atlas (52% memory saved on the rest), and derives 45 barriers over the
  merged frame (the indirect prelude — Distance* + Card* — now feeds DirectRadiance;
  see Exhibits/IndirectIllumination).

Latest result: **55/55 checks passed** (GCC, `-std=c++17 -Wall -Wextra -Werror`).
Optional argument: a path prefix that writes `<prefix>svgf-strip.ppm`
([ground truth | noisy 1-spp | filtered]; PNG master in `Capture/`).

Scope honesty: the discrete-domain spatial test uses Jacobian factor 1 (identical sampling
measure at both receivers) and separately proves the Jacobian factor scales combine weights
as specified; the full solid-angle Jacobian parity is an SDK-phase golden test. The variance
filter core is single-channel double precision — the exact recurrences the Slang bodies will
run per channel. Nothing here claims GPU execution.
