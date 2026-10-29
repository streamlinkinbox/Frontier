# Optional power-weighted emitter sampling for replay GI

The bounded `PathReplayReservoir.slang` experiment can now opt into the Engine's existing global
`RadiantFacetDistribution`: emitter selection is proportional to triangle area × emitted luminance,
with the Engine CDF's positive 24-bit probability floor. A selected point remains uniform over its
triangle. The estimator divides by the **encoded** selection probability; the optional facet/BSDF
balance heuristic uses that same probability on both the light-sampled and BSDF-hit branches. The
CDF compiler emits facet indices in ascending order for the BSDF-hit lookup.

This option is **off by default**. Changing the proposal, emissive geometry or source radiance retraces
retained random sequences and caps history at one for that transition. It is global, two-sided and
visibility-blind—not clustered emissive-light selection. It adds CDF storage/build work and shader
binary searches; no equal-time or hardware speedup is claimed.

## Independent references and convergence

All shader results use **SwiftShader CPU Vulkan**; the release and ASan/UBSan logs instrument only
the host, not the Vulkan driver or shader. The two-emitter reference uses independent triangle-area
quadrature and compares 24 reset batches of 1,024 centre receivers. The bright/dim selection
probabilities are approximately `0.997921 / 0.002079`.

| Fixture | Independent value | Vulkan result | Observation |
|---|---:|---:|---|
| Unblocked, uniform facets | `0.544713` | `0.542907 ± 0.003569` | baseline batch SE |
| Unblocked, weighted facets | `0.544713` | `0.544227 ± 0.000540` | per-sample SD `0.181×` uniform |
| Bright source blocked, uniform | `0.00113246` | `0.00113486 ± 0.00000595` | dim light remains visible |
| Bright source blocked, weighted | `0.00113246` | `0.00078890 ± 0.00012136` | per-sample SD `18.26×` uniform; the short run underestimates |
| Bright blocked, weighted + MIS | `0.00113246` | `0.00114053 ± 0.00003107` | this control is less noisy than weighted-only, still not a universal improvement |

The quoted errors are sample standard errors across reset batch means; finite pseudorandom streams
and spatial samples are not proven independent. The obstructed weighted-only estimator has rare,
large contributions and its 24-batch mean is about **30% low** despite the broad six-SE test limit.
A separate 256-batch run using distinct, pseudorandomly generated frame seeds produced
`0.00116835 ± 0.00005290`, with 560 nonzero centre samples out of 262,144. Its running 32/64/128/256
results are retained in the log; additional samples improve this particular observation but do not
prove general unbiasedness or monotonic convergence. The counterexample is a reason to leave this
visibility-blind proposal optional.

A separate rough, textured **four-hit** room uses three independently seeded 400,000-path CPU
references for the original source powers, swapped source powers and an inserted blocker. Fresh and
reused Vulkan means agree with the documented statistical tolerances. Twelve batches exercise
61,440 temporal accepts each for proposal switch, relighting and blocker insertion, plus 437,760
spatial links. These are fixed-receiver fixtures, not general moving/deforming-mesh reprojection.
Empty-emitter fallback and invalid/degenerate source guards also pass.

Default-off parity compares the current shader with actual checkpoint `496a076`: five cosine and
five GGX-mode float4 readbacks, 327,680 components in total, plus the history counters, match bit for
bit. The prior shader is compiled from Git by the runner; the current host submits both. The existing
GGX proposal, half-visible emitter and four-hit room suite also passed with weighting disabled:
[regression log](Results/ReplayFacetPower-regression.txt). The existing uniform explicit-emitter and
rough/diffuse four-hit checks also passed: [facet regression](Results/ReplayFacetPowerFacet-regression.txt).

The existing replay approach still fully retraces random sequences in a bounded opaque triangle
scene. Analytic lights, heterogeneous media, general materials, deforming motion, scalable history,
production/editor submission, real-time performance and general ReSTIR GI correctness remain open.

## Reproduce

Requires the prepared Slang/Vulkan/SwiftShader toolchain, CMake and Git checkpoint `496a076` for
parity. Set `VULKAN_TOOLS`, `SLANGC`, `VULKAN_LIBRARY` or `CMAKE` to use other prepared tools.

```sh
Exhibits/LightCompute/RunReplayFacetPower.sh
CHECK=--replay-facet-power-parity Exhibits/LightCompute/RunReplayFacetPower.sh
SANITIZE=1 BUILD_JOBS=1 Exhibits/LightCompute/RunReplayFacetPower.sh
CHECK=--replay-facet-power-parity SANITIZE=1 BUILD_JOBS=1 Exhibits/LightCompute/RunReplayFacetPower.sh
```

[Release](Results/ReplayFacetPower-release.txt) · [Sanitized host](Results/ReplayFacetPower-sanitized.txt) ·
[Default parity](Results/ReplayFacetPowerParity-release.txt) ·
[Sanitized default parity](Results/ReplayFacetPowerParity-sanitized.txt).
