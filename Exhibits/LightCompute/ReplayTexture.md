# Opaque diffuse texture replay

An opt-in replay mode evaluates the existing float-atlas albedo pyramid at every secondary triangle hit. Primary
camera strikes already use the same `ApplyRayMaterial` path. This exhibit records the original **Lambertian**
subset. A later [rough-textured extension](RoughTextureReplay.md) also tests guarded authored GGX. Neither enables
normal maps, automotive paint, glass, layered materials or analytic lights.

## Bounded domain

- One opaque, non-metallic, full-coverage stratum with roughness coefficient `1` and a power-of-two RGB albedo
  pyramid, up to 1,024 texels per side. Absorption, coating classification, sheen intensity and the reserved
  optics component must be zero. Replay uses Lambertian response, not authored specular terms.
- The earlier capture fixture at `4ed4ef6` mistakenly set the reserved optics component to `1`; the shader did
  not use it. The corrected fixture sets it to `0`, and the guard rejects the old layout. All 14 refreshed PNGs
  are byte-identical to the earlier captures.
- Float atlas material and mip spans, finite `[0,1]` texels, finite bounded UVs/mapping and triangle/BVH XYZ are
  checked before submission. Textured vertices keep UVs in the `.w` channels, which need not equal BVH padding.
- The camera and triangle coordinates are bounded; primary cameras must have finite orthonormal current and
  previous axes. The experiment rejects a light CDF, so the entire atlas participates in the revision.
- The production sampler wraps UVs, uses bilinear interpolation within a selected mip and estimates footprint
  from camera distance, view angle and vertical pixel size. It is not a ray-differential or anisotropic filter.
- Atlas changes are compared by contents, not pointer. Replay retraces retained seeds and caps their represented
  history at one for the changed submission. A separate pending reset preserves the original hard-reset policy
  for ordinary pipelines. Unrelated geometric, camera and estimator changes retain their existing guards.

## Validation

Source revision `ba385ae`; SwiftShader CPU Vulkan, Linux x86-64, GCC 12.2 release and ASan/UBSan debug hosts,
Slang 2026.14.1 with `-O0` SPIR-V. The external Vulkan driver is not sanitizer-instrumented. Expected plane values
are effectively exact for these 2,000-unit-wide fixtures; extremely
rare rays can miss their finite boundaries.

| Check | Result |
| --- | --- |
| Malformed atlas, UV, camera and domain inputs | 33 cases rejected; diffuse-only opt-in and material guards held |
| Two-hit planes, RGB analytic `(0.8, 0.2, 0.1)` | Fresh `(0.8000000, 0.2000000, 0.1000000)` |
| Black → coloured atlas with reused candidates | `(0.7999905, 0.1999976, 0.0999988)` |
| Moving blocker / reopened | `0` / `(0.7999882, 0.1999970, 0.0999985)` |
| Changed atlas, blocked, reopened | 40,960 temporal accepts each; changed-atlas test 97,280 spatial links |
| Secondary texture probes | Centre, bilinear, negative wrap, seam and 1×1 mip matched analytic RGB |
| Primary CPU filter | 8,625 strikes, 7,858 wrapped; mips 0/1/2/3: 1,236/5,098/2,007/284; max RGB error `7.28e-7` |
| Other pipeline reset | Pending ordinary-history reset tested after the replay atlas change |

Primary filtering comparison uses Vulkan's hit positions and barycentrics with an independent double-precision
CPU sampler; it is not an independent primary-intersection test. The secondary plane probes independently specify
the expected texel colours and transport. Earlier diffuse, rough, balance, motion and direct-light regression
fixtures still pass; see the retained logs.

## Four-hit room

The floor and cuboid use an 8×8 checker pyramid; the fixed manual receiver stays untextured so differences originate
at secondary hits. Three independent CPU baselines each trace 400,000 paths with double-precision brute-force
triangle tests and `mt19937_64`. Fresh Vulkan facet-only and balanced modes, plus reused balanced transitions,
agree within the recorded six-combined-SE-plus-tolerance limits. Each GPU comparison uses eight reset batches of
1,024 centre pixels. Batch-mean SE now uses **sample** variance, correcting the earlier population-variance report.

| Transition / check | Independent CPU mean ± SE | SwiftShader mean ± batch SE |
| --- | ---: | ---: |
| Open, changed texture, reused — red | `0.815045 ± 0.003769` | `0.811707 ± 0.001845` |
| Changed texture + moving blocker, reused — red | `0.027136 ± 0.000433` | `0.027303 ± 0.000292` |
| Reopened and original texture, reused — red | `0.809796 ± 0.003761` | `0.809596 ± 0.001510` |
| Textured minus untextured, paired — green | `-0.0003039 ± 0.0000245` | `-0.0002953 ± 0.0000513` |

The paired counterfactual uses the same random draws within each CPU pair and, separately, within each GPU pair:
120,000 CPU paths and 24 GPU batches of 1,024 pixels. It detects a secondary-albedo effect rather than merely
matching a scene dominated by the direct source. A one-hit control is bitwise identical after the secondary-only
texture change. Each of the atlas, blocker and combined transitions retained 40,960 temporal candidates, capped
history at one and exercised spatial reuse. This is a statistical comparison of a specific model, not a general
unbiasedness proof.

```bash
bash Exhibits/LightCompute/RunReplayTextureRoom.sh
SANITIZE=1 bash Exhibits/LightCompute/RunReplayTextureRoom.sh
```

[Four-hit release](Results/ReplayTextureRoom-release.txt) ·
[four-hit sanitized host](Results/ReplayTextureRoom-sanitized.txt) ·
[texture, diffuse and balanced regressions](Results/ReplayTextureRoom-regression.txt) ·
[four settled and three first-transition captures](ReplayTextureCaptures.md) ·
[independent textured camera, rigid-motion and blocker reference](ReplayTextureMotion.md).

```bash
bash Exhibits/LightCompute/RunReplayTexture.sh
SANITIZE=1 bash Exhibits/LightCompute/RunReplayTexture.sh
```

[Release](Results/ReplayTexture-release.txt) · [sanitized host](Results/ReplayTexture-sanitized.txt) ·
[replay regressions](Results/ReplayTexture-regression.txt) ·
[direct-light regression](Results/ReplayTexture-direct-regression.txt) · [prior sampling audit](ReplayBalance.md).

The validation host uses full CPU-Vulkan retracing, not hardware ray queries or the editor. Finite random-sequence
correlations, scale-dependent motion guides, origin offsets and general adaptive-reuse bias remain open. No
real-time, physical-material or complete ReSTIR GI claim is made.
