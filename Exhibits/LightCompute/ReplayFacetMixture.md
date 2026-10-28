# Optional uniform/power mixture for replay GI

The Engine replay shader can optionally choose an emissive triangle from a **50% uniform, 50%
area/luminance-weighted** mixture. The chosen point remains uniform on that triangle. Both explicit
light sampling and the optional BSDF-hit balance heuristic use the encoded mixture probability
`q[i] = 0.5 * p_power[i] + 0.5 / N`. With no emitters the environment path remains available.

This mode is **off by default**. Uniform and pure-power sampling remain available. Switching the
proposal or changing source radiance/occlusion retraces retained random sequences and caps history
at one for that transition. The CDF is global, two-sided and visibility-blind; this is not clustered
emissive selection. It adds a mixture draw and may trace shadow rays. Vulkan submission remains
exhibit-owned; no editor integration, physical-GPU result or equal-time performance claim follows.

## What the probability floor does—and does not—guarantee

For an ideal fixed receiver with a single explicitly sampled emitter and the same conditional point
density per source, `q[i] >= 0.5 / N`. Thus the second moment of that one-sample light estimator is
at most twice the second moment under uniform source selection, assuming the samples follow the
stated probabilities. This is **not** a two-times variance bound: variance subtracts the squared mean.
Nor does this elementary bound establish the behavior of balanced MIS, adaptive reservoir reuse,
finite correlated pseudorandom streams, motion or a complete GI pipeline.

## Independent references and measured counterexample

All Vulkan measurements below use **SwiftShader CPU Vulkan**, not a physical GPU. Independent
triangle-area quadrature is compared with 24 reset batches of 1,024 centre receivers; `±` is the
sample standard error of the batch means. The unblocked uniform mean is about 3.2 batch SE below its
reference in this particular sequence, so the quoted uncertainty should not be treated as proof of
independent samples or convergence.

| Fixture | Independent integral | Uniform | Pure power | 50/50 mixture |
|---|---:|---:|---:|---:|
| Both lights visible | `0.544713` | `0.530825 ± 0.004285` | `0.542872 ± 0.000793` | `0.544980 ± 0.002175` |
| Bright light blocked | `0.00113246` | `0.00113222 ± 0.00000635` | `0.00101451 ± 0.00011002` | `0.00113022 ± 0.00000933` |

In this sequence the mixture's per-sample standard deviation is `0.597×` uniform with both lights
visible and `1.708×` uniform with the bright light blocked. Pure power is `20.586×` uniform in the
blocked case. The observed mixture/uniform second-moment ratios are `0.686` and `1.989`, respectively;
these are finite-sample measurements, not a general variance guarantee. The mixture trades some of
pure power's visible-light advantage for much more frequent draws of the dim, unobstructed source.
The encoded two-source mixture probabilities are `0.748960465 / 0.251039535`.

### Follow-up on the low uniform result

Replaying the original 24 uniform batches shows that the bright source was selected for **11,975 of
24,576** centre samples (`0.487264`, versus `0.5` expected). The batch-frequency SE was `0.003718`;
a host implementation of the same shader generator reproduced all 24,576 selections. That generator
comparison is a diagnostic, not an independent reference or proof of a generator defect. Assuming
exact conditional area means, the observed selection frequency predicts `0.530896`, close to the
measured `0.530825`: the source imbalance explains most of this particular short-run discrepancy.

A new `mt19937` sequence supplies 128 unique frame seeds, none used by the original uniform batches.
The same frame seed is paired across the uniform/mixture and open/blocked comparisons. The running
32/64/128 results are retained in the logs; at 128 batches the measurements were:

| Fixture | Uniform | Mixture | Independent integral |
|---|---:|---:|---:|
| Both visible | `0.543709 ± 0.001565` | `0.543700 ± 0.000946` | `0.544713` |
| Bright source blocked | `0.00113521 ± 0.00000325` | `0.00113709 ± 0.00000578` | `0.00113246` |

The open uniform bright-source frequency in the repeat was `0.499252 ± 0.001364` across batch
means. These follow-up results do not distinguish an ordinary fluctuation from possible PRNG correlations,
prove that batches or pixels are independent, or guarantee monotonic convergence. They also do not
establish general adaptive-reservoir unbiasedness.

Separate **one-, three- and five-source** fixtures use distinct RGB masks to count actual selected
sources, including odd source counts, and compare colour-channel estimates with independent area
quadrature. For five sources, one observed selection frequency is `0.104675` versus encoded `0.108903`,
and another is `0.398621` versus `0.399659`; all individual checks and uncertainties are in the logs.
The no-source environment fallback and the missing-CDF
guard are also exercised. The frequency tests do not prove PRNG independence.

A rough-textured **four-hit** room uses three separately seeded 400,000-path CPU references. Fresh
mixture sampling and retained-history proposal, blocker and relighting transitions agree within the
recorded tolerances. Across twelve GPU batches, each transition has 61,440 temporal accepts and
437,760 spatial links total. This is a fixed-receiver fixture, not general deforming-mesh reprojection.
The numerical tests include mixture/BSDF balance weighting; that is independent of the second-moment
bound above.

A subsequent [moving-primary mixture validation](ReplayFacetMixtureMotion.md) compares two textured
object/floor probes against six independently seeded four-hit CPU references as camera, rigid pose,
material, emissive-triangle positions/powers and blocker change. Release and sanitized-host runs pass;
the fresh mixture reduced floor batch SD in this fixture but increased object-green batch SD. This
extends the tested cases, not a general variance, convergence or unbiasedness guarantee.

Default-off readbacks compare with the actual preceding shader checkpoint `0d5af89`. Uniform and
pure-power modes, each with cosine and GGX proposals, produce 20 float4 images: 655,360 components
are bit-identical and eight recorded history outcomes match. Existing uniform-emitter, pure-power
and GGX regressions are retained separately. ASan/UBSan instrument the host; the Vulkan driver and
shader are not instrumented.

The current approach fully retraces random sequences through a bounded opaque triangle scene.
Analytic-light GI, heterogeneous media, general materials and deforming motion, scalable history,
production/editor submission, physical-GPU verification, equal-time performance and a general
unbiasedness proof remain open. See the [previous power-weighted counterexample](ReplayFacetPower.md),
the [mixture motion readbacks](ReplayFacetMixtureCaptures.md) and the
[prior rendered motion gallery](RoughTextureReplayCaptures.md).

## Reproduce

Requires the prepared Slang/Vulkan/SwiftShader toolchain, CMake and Git checkpoint `0d5af89` for
parity. `VULKAN_TOOLS`, `SLANGC`, `VULKAN_LIBRARY`, `CMAKE`, `BUILD_JOBS` and `RESULTS` can be
set to select prepared tools or alternate output paths.

```sh
Exhibits/LightCompute/RunReplayFacetMixture.sh
CHECK=--replay-facet-mixture-spectrum Exhibits/LightCompute/RunReplayFacetMixture.sh
CHECK=--replay-facet-mixture-convergence Exhibits/LightCompute/RunReplayFacetMixture.sh
CHECK=--replay-facet-mixture-parity Exhibits/LightCompute/RunReplayFacetMixture.sh
SANITIZE=1 BUILD_JOBS=1 Exhibits/LightCompute/RunReplayFacetMixture.sh
CHECK=--replay-facet-mixture-convergence SANITIZE=1 BUILD_JOBS=1 Exhibits/LightCompute/RunReplayFacetMixture.sh
CHECK=--replay-facet-mixture-parity SANITIZE=1 BUILD_JOBS=1 Exhibits/LightCompute/RunReplayFacetMixture.sh
```

[Release](Results/ReplayFacetMixture-release.txt) · [Sanitized host](Results/ReplayFacetMixture-sanitized.txt) ·
[Focused spectrum check](Results/ReplayFacetMixtureSpectrum-release.txt) ·
[Convergence repeat](Results/ReplayFacetMixtureConvergence-release.txt) ·
[Sanitized convergence repeat](Results/ReplayFacetMixtureConvergence-sanitized.txt) ·
[Default parity](Results/ReplayFacetMixtureParity-release.txt) ·
[Sanitized default parity](Results/ReplayFacetMixtureParity-sanitized.txt) ·
[Pure-power regression](Results/ReplayFacetMixturePower-regression.txt) ·
[Uniform-emitter regression](Results/ReplayFacetMixtureUniform-regression.txt) ·
[GGX regression](Results/ReplayFacetMixtureImportance-regression.txt) ·
[Engine CTests](Results/ReplayFacetMixtureEngine-regression.txt).
