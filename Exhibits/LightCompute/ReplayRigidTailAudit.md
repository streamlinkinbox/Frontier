# Rigid GI rare-sample replay audit

The [2,048-seed study](ReplayRigidVarianceRepeat.md) showed that rare rigid-red values strongly affect
the measured noise ratio. This diagnostic inspects the three largest **uniform spatial** samples and
a median-ranked control, selected after observing that table. For each frame it also evaluates the
paired mixture sample. This is a targeted mechanism check, not a new random experiment.

An exhibit-only, default-off dispatch reads the five already-generated donor seeds and re-evaluates
the unchanged Engine replay path at the selected receiver. A separate host implementation reconstructs
the reservoir's draws, weights and output from those recorded values. The audit is enabled only for
this bounded fixture; ordinary submission remains unchanged. It reuses the shader's own path evaluator,
so it is **not an independent validation of the path radiance itself**.

## Result

| Uniform-spatial rank | Frame seed | Selected donor | Replayed path red at receiver | Reservoir scale | Final red |
|---:|---:|---:|---:|---:|---:|
| 1 | `3636229639` | 4 | `0.253270` | `0.366236` | `0.092756` |
| 2 | `3505352021` | 4 | `0.301015` | `0.227295` | `0.068419` |
| 3 | `1703867237` | 4 | `0.196118` | `0.240472` | `0.047161` |
| 1025 (median control) | `2841748405` | 2 | `0.002154` | `1.123207` | `0.002419` |

For these three large observations, the selected **replayed path values are already high** and the
final reservoir scale is less than one; a large final normalization factor is not the explanation for
these particular spikes. Each fresh donor's stored source weight is one. The paired mixture paths use
the same frame and donor seeds but follow different proposal draws; their final red values for ranks
1–3 are `0.003306 / 0.000555 / 0.001887`. Selection was performed after seeing the data, so this is not
evidence of a universal mixture advantage.

## Bounce-level localization

A subsequent audit-only mirror of the path loop records direct-facet and BSDF-hit/environment terms
at each bounce. For all three high uniform cases, the dominant red contribution occurs at bounce index
`2` (the third segment), where the BSDF ray hits the same **bright emissive facet 12**:

| Rank | Bounce-2 direct-facet red | Bounce-2 emitter-hit red | Combined bounce-2 red |
|---:|---:|---:|---:|
| 1 | `0.136989` | `0.115401` | `0.252390` |
| 2 | `0.145163` | `0.155851` | `0.301013` |
| 3 | `0.002801` | `0.193315` | `0.196116` |

Facet 12 emits RGB `12 / 10 / 8` in this scene; the other radiant facet 16 emits approximately
`0.96 / 0.80 / 0.64`. Their uniform source probabilities are both `0.5`, while the mixture probabilities
are approximately `0.713 / 0.287`. The recorded hit facet identifies the BSDF-hit term; the sampled
facet behind the separate direct term has not been recorded. The mirrored loop's RGB totals agree with
the original shader paths to maximum absolute error `4.48e-9` across eight selected cases. It shares
shader helpers and is **not** an independent proof of the bounce values, PDF/MIS correctness or PRNG
independence. These post-selected paths localize a mechanism; they do not establish the cause of the
old low mean or show whether the probabilities and tails are correct.

The complete and instrumented submissions produced bit-identical selected RGBA for all eight cases
(32 components), and reproduced the stored CSV values. The host reconstruction matched all selected
RGB outputs with maximum absolute error `0` at the logged precision. Release and ASan/UBSan-host runs
both exited zero and their diagnostic lines match; the driver and shader are not sanitizer-instrumented.
The pre-existing 16-seed smoke fixture also passed after the default-off hook was added, with its sample
CSV byte-identical to the previous capture. A separate moving/blocked/reopened smoke test also passed
in both builds with byte-identical previous samples. These are finite fixture checks, not full-image proofs.

[Release audit](Results/ReplayRigidTailAudit-release.txt) ·
[sanitized audit](Results/ReplayRigidTailAudit-sanitized.txt) ·
[default-path regression](Results/ReplayRigidTailDefault-regression.txt) ·
[sanitized default-path regression](Results/ReplayRigidTailDefault-sanitized.txt) ·
[motion/occlusion regression](Results/ReplayRigidTailMotionDefault-release.txt) ·
[sanitized motion/occlusion regression](Results/ReplayRigidTailMotionDefault-sanitized.txt) ·
[bounce breakdown, release](Results/ReplayRigidTailBreakdown-release.txt) ·
[bounce breakdown, sanitized](Results/ReplayRigidTailBreakdown-sanitized.txt) ·
[bounce-hook motion regression](Results/ReplayRigidTailBreakdownMotion-release.txt) ·
[sanitized bounce-hook regression](Results/ReplayRigidTailBreakdownMotion-sanitized.txt).

## Reproduce

```sh
# Reads the tracked 2,048-row sample table; selects the top three and median uniform-spatial red cases
Exhibits/LightCompute/RunReplayRigidTailAudit.sh
SANITIZE=1 BUILD_JOBS=1 Exhibits/LightCompute/RunReplayRigidTailAudit.sh
```

The next useful checks are an independent evaluation of these bright-emitter paths and their PDF/MIS
weights, disjoint repetitions, and equal-cost performance measurements. No physical-GPU performance,
general unbiasedness or complete ReSTIR GI claim follows from this audit.
