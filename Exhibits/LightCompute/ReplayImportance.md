# Optional GGX-aware replay proposal

The bounded rough-opaque replay shader in `Frontier/Engine/Rendering/Shaders/` has a **default-off**
50/50 cosine/GGX-normal proposal. It is a convergence option for the existing experiment, not a complete
ReSTIR GI implementation. The Engine domain rejects this option outside rough-opaque mode; the exhibit
Vulkan host exposes it only to validation code. Existing cosine mode remains the default.

For upper-hemisphere direction `l`, view `v`, normal `n`, half-vector `h = normalize(v+l)`, and the
existing isotropic GGX normal distribution `D`, the mixture density is

`p(l) = 0.5 (n·l)/π + 0.5 D(h) (n·h) / (4 v·h)`.

The GGX branch samples the full normal distribution. Draws whose normals face away from `v` or reflect
below the surface contribute zero: they are **not** resampled. Cosine retains positive support for the
upper hemisphere. Path throughput uses `f (n·l)/p`; the opt-in facet/BSDF balance heuristic uses the
same mixture density for both the sampled light and BSDF-hit light. Changing proposals retraces saved
seeds and caps retained history at one for that transition, as for other domain changes.

This is not visible-normal sampling, an environment importance map, power-weighted emissive sampling,
a near-delta solution or a guarantee of lower variance. The guarded roughness range remains `0.25–1`;
analytic lights, general layered materials, glass and deforming reprojection remain outside this replay
subset. The renderer/editor and a native hardware ray-query backend do not yet submit this feature.

## Numerical checks

All GPU results below are SwiftShader **CPU Vulkan**, not physical-GPU measurements. Independent CPU
references evaluate the same bounded model; they are not measured material or scene calibration. The complete
importance and motion logs were collected at `3656f4f`; a later identifier-only formatting cleanup produced
an identical release host executable (`d52e7f99…`) and identical shader SPIR-V after removing debug names
(generate `1fc9b41a…`, resolve `29adc522…`). Default-off parity and diffuse/texture regressions were rerun
after that cleanup.

- **Proposal mass:** independent `1024×2048` quadrature gives upper direction mass `0.8176251` and rejected-GGX
  mass `0.3646301`; `upper + rejected/2 = 0.9999402`.
- **Rough metal, white environment:** quadrature red `0.767077`; cosine `0.715263 ± 0.027716` batch SE;
  mixture `0.760769 ± 0.003699`. The mixture/cosine per-sample SD ratio was `0.138`.
- **Nearly matte, white environment:** quadrature red `0.699976`; cosine sample SD `0.0000546`; mixture SD
  `0.444149`—a clear counterexample where the mixture is much worse.
- **Half-visible emissive square:** red area integral `0.129912`; mixture with facet/BSDF MIS
  `0.129175 ± 0.001013` batch SE. BSDF-only and facet-only controls also agree; a fully covered light yields zero.
- **Four-hit textured room:** two independent 400,000-path CPU references agree with fresh, proposal-switch and
  moving-blocker replay within the recorded statistical bounds. The switch and blocker each retain 81,920
  temporal candidates across 16 batches; 389,120 spatial links are exercised.
- **Moving textured floor, blue:** independent CPU `0.065135 ± 0.001922`; cosine fresh `0.053904 ± 0.008074`;
  mixture fresh `0.061921 ± 0.002496` batch SE. The mixture/cosine batch SD ratio was `0.309` across 64 batches.
- **Moving textured object, red:** the same ratio was `1.333`: the mixture worsened this probe. Other channels
  and two 32-batch cohorts are recorded in the motion log.
- **Default-off parity:** five actual Vulkan float4 readbacks, including changed textures, a moving blocker,
  reuse and textured primary rays, are bit-identical to shader checkpoint `3c8a286`: `163,840` components and
  both history counters match.

The environment comparison uses 32 reset batches of 1,024 centre pixels per mode; the reported errors
use sample batch variance, with pixel SE also retained in the log. The moving comparison uses 64 reset
batches per probe with spatial reuse and 160,000 independent `mt19937_64` CPU paths for each open/blocked
object/floor reference. It shifts the camera, rotates/translates the object, changes the atlas, switches
from cosine to mixture and inserts a blocker. Both probes retain one capped temporal candidate per
transition; primary texture and geometry readbacks also match independent rays.

Splitting the moving-floor blue samples into two disjoint 32-batch seed cohorts gives mixture/cosine SD
ratios `0.536` and `0.255`; the moving-object red ratios are `1.348` and `1.311`. This exposes substantial
cohort variation rather than implying a single ratio is a convergence guarantee.

An interleaved SwiftShader timing is recorded, including host upload/readback; it is not a hardware-GPU
benchmark. Per-sample and batch variance ratios are fixture observations, not a universal equal-time
speedup. The 32-batch cohorts use disjoint seed ranges, but finite GPU pseudorandom streams and adaptive
reservoir reuse have **not** been proven independent or generally unbiased. The earlier fresh-floor
repeat and its increased GPU standard error remain documented in [RoughTextureMotion.md](RoughTextureMotion.md).

## Reproduction

```bash
bash Exhibits/LightCompute/RunReplayImportance.sh
CHECK=--replay-importance-motion RESULTS="$PWD/_ClaudeScratch/importance-motion.txt" \
  bash Exhibits/LightCompute/RunReplayImportance.sh
CHECK=--replay-importance-parity RESULTS="$PWD/_ClaudeScratch/importance-parity.txt" \
  bash Exhibits/LightCompute/RunReplayImportance.sh
SANITIZE=1 bash Exhibits/LightCompute/RunReplayImportance.sh
```

The motion and parity checks can likewise be run with `SANITIZE=1`; only the host is instrumented.
The parity runner retrieves the prior shader from the local Git checkpoint, not an external download.

- [Release](Results/ReplayImportance-release.txt) · [Sanitized host](Results/ReplayImportance-sanitized.txt)
- [Motion release](Results/ReplayImportanceMotion-release.txt) ·
  [Motion sanitized](Results/ReplayImportanceMotion-sanitized.txt)
- [Default parity release](Results/ReplayImportanceParity-release.txt) ·
  [Default parity sanitized](Results/ReplayImportanceParity-sanitized.txt)
- [Engine domain release](Results/ReplayImportance-engine-release.txt) ·
  [Engine domain sanitized](Results/ReplayImportance-engine-sanitized.txt)
- [Default diffuse/texture regression](Results/ReplayImportance-regression.txt)
- [Existing rough-textured renders](RoughTextureReplayCaptures.md).
